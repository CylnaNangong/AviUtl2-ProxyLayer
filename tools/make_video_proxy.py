#!/usr/bin/env python3
"""ProxyLayer P1 prototype: offline video proxy + reversible project derivation.

HitFilm-style contract, preview-only:
    transcode  4K source -> 1080p H.264 proxy (pluggable encoder backend)
    sidecar    .plv.json records original/proxy paths, size ratio, fingerprint,
               and the exact backend + command used
    convert    derive a project: ONLY video objects are touched --
               ファイル -> proxy, 拡大率 x ratio,
               and 元の動画ファイル / 元の拡大率 stored in the same block
    export     derive an export project (original media + original scale)
    restore    rebuild from metadata, byte-identical to the source project
    verify     content fingerprint (change -> proxy stale)

Encoder backends (no longer tied to Intel QSV):
    qsvencc / nvenc / ffmpeg / custom, auto-detected by default.
    Only QSVEncC has been real-host validated (P1). Other backends are
    command-level equivalents and are NOT claimed as verified here.
    See docs/ENCODER_BACKENDS.md.

All operations are OFFLINE and write only into this test directory.
The user's .aup2 and media files are opened read-only.

Usage:
  python3 make_video_proxy.py transcode <source.mov> [--encoder auto|qsvencc|nvenc|ffmpeg|custom]
                              [--cqp 32] [--gop 60] [--res 1920x1080] [--crf 23]
                              [--template "..."] [--encoder-exe ...] [--dry-run]
  python3 make_video_proxy.py list-backends
  python3 make_video_proxy.py convert  <project.aup2> <sidecar.json>
  python3 make_video_proxy.py export   <converted.aup2>
  python3 make_video_proxy.py restore  <converted.aup2>
  python3 make_video_proxy.py verify   <sidecar.json>
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, 'assets')

# --- Pluggable encoder backends -------------------------------------------
# P1 hard-coded QSVEncC64.exe (Intel QSV only). That exact command survives as
# the already-validated 'qsvencc' backend; it is no longer a requirement.
# A backend only has to: downscale to --res, keep the audio track, and use a
# ~60-frame GOP. P1 measured that the speed-up comes from DOWNSCALING, not from
# GOP/bitrate/codec, so any backend meeting those three is equivalent.
BACKENDS = {
    'qsvencc': {
        'names': ['QSVEncC64.exe', 'QSVEncC.exe'],
        'paths': [r'C:\ProgramData\aviutl2\Plugin\exe_files\QSVEncC\x64\QSVEncC64.exe'],
        'env': 'PROXYLAYER_QSVENCC',
        'note': 'validated in P1 (real host 2.1.18b)',
    },
    'nvenc': {
        'names': ['NVEncC64.exe', 'NVEncC.exe'],
        'paths': [r'C:\ProgramData\aviutl2\Plugin\exe_files\NVEncC\x64\NVEncC64.exe'],
        'env': 'PROXYLAYER_NVENCC',
        'note': 'rigaya CLI, command-level equivalent (not real-host verified)',
    },
    'ffmpeg': {
        'names': ['ffmpeg.exe', 'ffmpeg'],
        'paths': [],
        'env': 'PROXYLAYER_FFMPEG',
        'note': 'CPU libx264 fallback, command-level equivalent (not real-host verified)',
    },
}
DEFAULT_ORDER = ['qsvencc', 'nvenc', 'ffmpeg']

META_FILE = '元の動画ファイル'
META_SCALE = '元の拡大率'
META_TAG = 'ProxyLayer動画プロキシ'
VIDEO_EXT = re.compile(r'\.(mov|mp4|mkv|avi|webm|m4v)$', re.I)
IMAGE_EXT = re.compile(r'\.(png|jpg|jpeg|bmp|webp|gif|tif|tiff)$', re.I)
SECTION_RE = re.compile(r'^\[([^\]]+)\]$')
OBJ_RE = re.compile(r'^(\d+)$')
EFF_RE = re.compile(r'^(\d+)\.(\d+)$')


# ---------------------------------------------------------------- project I/O
def read_project(path):
    raw = open(path, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    t = raw[3 if bom else 0:].decode('utf-8')
    nl = '\r\n' if '\r\n' in t else '\n'
    return t.split(nl), nl, bom


def write_project(path, lines, nl, bom):
    data = (b'\xef\xbb\xbf' if bom else b'') + nl.join(lines).encode('utf-8')
    open(path, 'wb').write(data)
    return len(data)


def sections(lines):
    """-> [ (header_or_None, [body lines]) ]; preserves exact order."""
    out, cur = [], None
    for l in lines:
        if SECTION_RE.match(l):
            cur = [l, []]
            out.append(cur)
        else:
            if cur is None:
                cur = [None, []]
                out.append(cur)
            cur[1].append(l)
    return out


def flatten(secs):
    arr = []
    for h, body in secs:
        if h is not None:
            arr.append(h)
        arr.extend(body)
    return arr


def obj_id(header):
    """object/effect section header -> owning object id (int) or None."""
    if header is None:
        return None
    m = SECTION_RE.match(header)
    if not m:
        return None
    n = m.group(1)
    a = OBJ_RE.match(n)
    if a:
        return int(a.group(1))
    e = EFF_RE.match(n)
    if e:
        return int(e.group(1))
    return None


def kv(body, key):
    for l in body:
        if l.startswith(key + '='):
            return l.split('=', 1)[1]
    return None


def set_kv(body, key, value):
    for i, l in enumerate(body):
        if l.startswith(key + '='):
            body[i] = key + '=' + value
            return True
    return False


def drop_meta(body):
    return [l for l in body if not l.startswith(
        (META_FILE + '=', META_SCALE + '=', META_TAG + '='))]


# ---------------------------------------------------------------- media helpers
def mp4_size(path):
    """Read w/h from the first visual sample entry inside stsd (head + tail)."""
    size = os.path.getsize(path)
    with open(path, 'rb') as f:
        head = f.read(min(size, 8 << 20))
        tail = b''
        if size > 8 << 20:
            f.seek(size - (8 << 20))
            tail = f.read()
    for d in ([head, tail] if tail else [head]):
        for s in {d.find(b'stsd'), d.rfind(b'stsd')}:
            if s < 0:
                continue
            base = s + 4
            for tag in (b'avc1', b'hev1', b'hvc1', b'av01', b'mp4v'):
                i = d.find(tag, base, min(len(d), base + 4096))
                if i < 0 or i + 32 > len(d):
                    continue
                w, h = struct.unpack_from('>HH', d, i + 28)
                if 16 <= w <= 16384 and 16 <= h <= 16384:
                    return w, h
    return None


def fingerprint(path):
    st = os.stat(path)
    h = hashlib.sha256()
    h.update(os.path.abspath(path).encode('utf-8'))
    h.update(str(st.st_size).encode())
    h.update(str(int(st.st_mtime)).encode())
    with open(path, 'rb') as f:
        h.update(f.read(1 << 20))
    return h.hexdigest()[:24]


def load_side(path):
    d = json.load(open(path, encoding='utf-8'))
    for k in ('proxy', 'scale_ratio', 'original'):
        if k not in d:
            sys.exit('sidecar missing key: ' + k)
    return d


# ---------------------------------------------------------------- commands
def find_exe(name):
    """Resolve a backend exe: explicit env override -> PATH -> known paths."""
    cfg = BACKENDS[name]
    override = os.environ.get(cfg['env'], '').strip('"')
    if override and os.path.isfile(override):
        return override
    for n in cfg['names']:
        found = shutil.which(n)
        if found:
            return found
    for p in cfg['paths']:
        if os.path.isfile(p):
            return p
    return None


def detect_backends(order=None):
    rows = []
    for name in (order or DEFAULT_ORDER):
        if name not in BACKENDS:
            continue
        rows.append((name, find_exe(name), BACKENDS[name]['note']))
    return rows


def select_backend(a):
    order = [x.strip() for x in (a.encoder_order or '').split(',') if x.strip()]
    if not order:
        order = list(DEFAULT_ORDER)
    if a.encoder and a.encoder != 'auto':
        if a.encoder == 'custom':
            if not a.template:
                sys.exit('--encoder custom requires --template')
            exe = a.encoder_exe or os.environ.get('PROXYLAYER_ENCODER_EXE', '')
            return 'custom', exe.strip('"'), 'user-supplied command template'
        if a.encoder not in BACKENDS:
            sys.exit('unknown encoder: ' + a.encoder)
        exe = find_exe(a.encoder)
        if not exe and a.dry_run:
            return (a.encoder, BACKENDS[a.encoder]['names'][0],
                    'dry-run (backend not installed)')
        if not exe:
            sys.exit('encoder %s not found. Set %s or install it.'
                     % (a.encoder, BACKENDS[a.encoder]['env']))
        return a.encoder, exe, 'explicit --encoder'
    for name in order:
        if name not in BACKENDS:
            continue
        exe = find_exe(name)
        if exe:
            return name, exe, 'auto-detected (order: %s)' % ','.join(order)
    if a.dry_run:
        name = order[0] if order else DEFAULT_ORDER[0]
        return (name, BACKENDS[name]['names'][0],
                'dry-run (no backend installed)')
    sys.exit('no encoder backend found. Install ffmpeg, or set '
             'PROXYLAYER_FFMPEG / PROXYLAYER_NVENCC / PROXYLAYER_QSVENCC, '
             'or use --encoder custom --template "...".')


def build_command(backend, exe, src, out, a):
    """Build the exact argv. No shell, so quoted paths stay intact."""
    if backend in ('qsvencc', 'nvenc'):
        # QSVEncC command is kept byte-for-byte identical to the P1 validated
        # one. NVEncC is rigaya's sibling CLI and shares the option names, but
        # it has NOT been verified on this machine.
        return [exe, '--avhw', '-i', src, '-o', out, '-c', 'h264',
                '--gop-len', str(a.gop), '--cqp', str(a.cqp),
                '--output-res', a.res, '--audio-copy']
    if backend == 'ffmpeg':
        w, h = a.res.lower().split('x', 1)
        # -vsync 0 preserves the source VFR timestamps (2141 frames in P1).
        # Newer ffmpeg prefers -fps_mode passthrough; -vsync still works.
        return [exe, '-y', '-i', src, '-vf', 'scale=%s:%s' % (w, h),
                '-c:v', 'libx264', '-preset', 'veryfast', '-crf', str(a.crf),
                '-g', str(a.gop), '-vsync', '0', '-c:a', 'copy', out]
    if backend == 'custom':
        w, h = a.res.lower().split('x', 1)
        text = a.template.format(
            exe=exe, input=src, output=out, res=a.res,
            width=w, height=h, gop=a.gop, cqp=a.cqp, crf=a.crf)
        parts = shlex.split(text, posix=False)
        return [p.strip('"').strip("'") for p in parts if p.strip()]
    raise ValueError('unknown backend: ' + backend)


def cmd_list_backends(a):
    print('Encoder backends (auto order: %s)' % ','.join(DEFAULT_ORDER))
    for name, exe, note in detect_backends():
        print('  %-9s %-8s %s' % (name, 'FOUND' if exe else 'missing', note))
        if exe:
            print('            %s' % exe)
    print('  custom    --encoder custom --template "..." [--encoder-exe ...]')
    print('')
    print('Override paths: PROXYLAYER_QSVENCC / PROXYLAYER_NVENCC / PROXYLAYER_FFMPEG')
    print('Only QSVEncC is real-host validated; others are command-level equivalents.')
    return 0


def cmd_transcode(a):
    src = os.path.abspath(a.source)
    if not os.path.isfile(src):
        sys.exit('source not found: ' + src)
    stem = os.path.splitext(os.path.basename(src))[0]
    out = os.path.join(ASSETS, stem + '.proxy.mp4')
    side = os.path.join(ASSETS, stem + '.plv.json')
    orig_size = mp4_size(src)
    backend, exe, why = select_backend(a)
    cmd = build_command(backend, exe, src, out, a)
    print('  backend : %s (%s)' % (backend, why))
    print('  exe     : %s' % (exe or '(none; template is self-contained)'))
    print('  command : %s' % ' '.join(
        '"%s"' % c if ' ' in c else c for c in cmd))
    if a.dry_run:
        print('  DRY RUN: command not executed (no files written)')
        return 0
    os.makedirs(ASSETS, exist_ok=True)
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for l in r.stdout.decode('utf-8', 'replace').splitlines()[-8:]:
        print('   ', l.strip()[:150])
    if r.returncode != 0 or not os.path.isfile(out):
        sys.exit('transcode failed (code %d)' % r.returncode)
    proxy_size = mp4_size(out)
    ratio = (round(orig_size[0] / proxy_size[0], 6)
             if orig_size and proxy_size else 2.0)
    doc = {
        'version': 1, 'original': src, 'proxy': out,
        'original_size': list(orig_size) if orig_size else None,
        'proxy_size': list(proxy_size) if proxy_size else None,
        'scale_ratio': ratio,
        'original_fingerprint': fingerprint(src),
        'proxy_bytes': os.path.getsize(out),
        'encoder_backend': backend,
        'encoder': ' '.join(cmd[1:]),
        'encoder_command': cmd,
        'export_gate': ('Use the EXPORT project (original media + original scale). '
                        'AviUtl2 forbids EDIT_SECTION writes during output, so an '
                        'automatic in-process switch is a P2 item.'),
    }
    json.dump(doc, open(side, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    print('\n  proxy  :', out, '%.1f MB' % (os.path.getsize(out) / 1048576))
    print('  sidecar:', side)
    print('  sizes  :', orig_size, '->', proxy_size, '| scale ratio', ratio)


def video_object_ids(secs):
    """Objects whose 動画ファイル effect points at a video file."""
    ids = set()
    for h, body in secs:
        oid = obj_id(h)
        if oid is None or kv(body, 'effect.name') != '動画ファイル':
            continue
        f = kv(body, 'ファイル') or ''
        if VIDEO_EXT.search(f) and not IMAGE_EXT.search(f):
            ids.add(oid)
    return ids


def cmd_convert(a):
    side = os.path.abspath(a.sidecar)
    d = load_side(side)
    proxy, ratio = d['proxy'], float(d['scale_ratio'])
    if not os.path.isfile(proxy):
        sys.exit('proxy missing: ' + proxy)
    lines, nl, bom = read_project(a.project)
    secs = sections(lines)
    vids = video_object_ids(secs)
    if not vids:
        sys.exit('no 動画ファイル objects found')

    # Each distinct ORIGINAL path must map to a distinct PROXY path. Pointing
    # every object at one file serialises the host's per-file decoder and
    # destroys the concurrency that the proxy is supposed to make cheap
    # (measured: one shared path was ~30% slower than 32 distinct hardlinks).
    # NTFS hardlinks keep the physical footprint at exactly one proxy file.
    stem = os.path.splitext(os.path.basename(d['original']))[0]
    os.makedirs(ASSETS, exist_ok=True)
    proxy_paths = {}

    def proxy_for(orig):
        if orig not in proxy_paths:
            idx = len(proxy_paths)
            dst = os.path.join(ASSETS, '%s.proxy-%02d.mp4' % (stem, idx))
            if os.path.isfile(dst):
                os.unlink(dst)
            os.link(proxy, dst)
            proxy_paths[orig] = dst
        return proxy_paths[orig]

    base = os.path.splitext(os.path.basename(a.project))[0]
    out = os.path.join(HERE, base + '.proxy.aup2')
    nfile = nscale = 0
    for h, body in secs:
        oid = obj_id(h)
        # idempotent: strip our metadata first
        body[:] = drop_meta(body)
        if oid not in vids:
            continue
        if h and EFF_RE.match(SECTION_RE.match(h).group(1)) \
                and kv(body, 'effect.name') == '動画ファイル':
            old = kv(body, 'ファイル')
            if old and VIDEO_EXT.search(old):
                set_kv(body, 'ファイル', proxy_for(old))
                body.append(META_FILE + '=' + old)
                body.append(META_TAG + '=' + side)
                nfile += 1
        elif '拡大率=' in '\n'.join(body) and kv(body, 'effect.name') not in ('動画ファイル',):
            for i, l in enumerate(body):
                if not l.startswith('拡大率='):
                    continue
                v = l.split('=', 1)[1]
                m = re.match(r'^\s*([+-]?[\d.]+)(.*)$', v)
                if not m:
                    continue
                try:
                    x = float(m.group(1))
                except ValueError:
                    continue
                body[i] = '拡大率=' + f'{x * ratio:.3f}' + m.group(2)
                body.append(META_SCALE + '=' + v)
                nscale += 1
                break
    write_project(out, flatten(secs), nl, bom)
    print('  converted:', out)
    print('  video objects      :', len(vids))
    print('  ファイル remapped :', nfile, '(distinct proxy paths: %d)' % len(proxy_paths))
    print('  拡大率 compensated:', nscale, '(x%g)' % ratio)
    print('  metadata           :', META_FILE, '/', META_SCALE, '/', META_TAG)


def _revert(a, suffix):
    lines, nl, bom = read_project(a.project)
    secs = sections(lines)
    base = os.path.splitext(os.path.basename(a.project))[0]
    base = re.sub(r'\.proxy$', '', base)
    out = os.path.join(HERE, base + suffix)
    nf = ns = 0
    for h, body in secs:
        orig_file = kv(body, META_FILE)
        orig_scale = kv(body, META_SCALE)
        if orig_file is not None:
            set_kv(body, 'ファイル', orig_file)
            nf += 1
        if orig_scale is not None:
            set_kv(body, '拡大率', orig_scale)
            ns += 1
        body[:] = drop_meta(body)
    write_project(out, flatten(secs), nl, bom)
    print('  %-8s: %s' % (suffix.strip('.'), out))
    print('  restored ファイル:', nf, '| 拡大率:', ns)
    return out


def cmd_export(a):
    p = _revert(a, '.export.aup2')
    print('  -> open this project before final output (lossless originals).')


def cmd_restore(a):
    _revert(a, '.restored.aup2')


def cmd_verify(a):
    d = load_side(a.sidecar)
    now = fingerprint(d['original'])
    ok = now == d['original_fingerprint'] and os.path.isfile(d['proxy'])
    print('  original:', d['original'])
    print('  fingerprint recorded/now:', d['original_fingerprint'], '/', now)
    print('  proxy exists:', os.path.isfile(d['proxy']),
          '| bytes', d.get('proxy_bytes'))
    print('  VALID' if ok else 'STALE -> re-run transcode + convert')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    t = sub.add_parser('transcode'); t.add_argument('source')
    t.add_argument('--cqp', type=int, default=32)
    t.add_argument('--gop', type=int, default=60)
    t.add_argument('--res', default='1920x1080')
    t.add_argument('--crf', type=int, default=23,
                   help='ffmpeg libx264 quality (default 23)')
    t.add_argument('--encoder', default='auto',
                   help='auto | qsvencc | nvenc | ffmpeg | custom')
    t.add_argument('--encoder-order', default=','.join(DEFAULT_ORDER),
                   help='comma-separated auto-detect order')
    t.add_argument('--template', default='',
                   help='custom command; placeholders: {exe} {input} {output} '
                        '{res} {width} {height} {gop} {cqp} {crf}')
    t.add_argument('--encoder-exe', default='',
                   help='exe path for the {exe} placeholder in --template')
    t.add_argument('--dry-run', action='store_true',
                   help='print the command without running it')
    t.set_defaults(fn=cmd_transcode)
    lb = sub.add_parser('list-backends'); lb.set_defaults(fn=cmd_list_backends)
    c = sub.add_parser('convert'); c.add_argument('project'); c.add_argument('sidecar')
    c.set_defaults(fn=cmd_convert)
    e = sub.add_parser('export'); e.add_argument('project'); e.set_defaults(fn=cmd_export)
    r = sub.add_parser('restore'); r.add_argument('project'); r.set_defaults(fn=cmd_restore)
    v = sub.add_parser('verify'); v.add_argument('sidecar'); v.set_defaults(fn=cmd_verify)
    a = ap.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == '__main__':
    main()
