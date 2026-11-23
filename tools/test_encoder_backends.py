#!/usr/bin/env python3
"""Offline self-check for the pluggable encoder backends.

No transcode is executed and no external encoder is required. Verifies command
construction, backend detection, env overrides and custom templates.
"""
import argparse
import os
import sys

import make_video_proxy as m


def opts(**kw):
    base = dict(cqp=32, gop=60, res='1920x1080', crf=23,
                encoder='auto', encoder_order=','.join(m.DEFAULT_ORDER),
                template='', encoder_exe='', dry_run=True)
    base.update(kw)
    return argparse.Namespace(**base)


def check(cond, msg):
    if not cond:
        print('  FAIL:', msg)
        return 1
    print('  ok  :', msg)
    return 0


def with_env(name, value, fn):
    saved = os.environ.get(name)
    if value is None:
        os.environ.pop(name, None)
    else:
        os.environ[name] = value
    try:
        return fn()
    finally:
        if saved is None:
            os.environ.pop(name, None)
        else:
            os.environ[name] = saved


def main():
    src = r'C:\media\IMG_2329.MOV'
    out = r'C:\work\proxy.mp4'
    exe = r'C:\tools\EncC64.exe'
    fails = 0

    # 1) QSVEncC stays byte-for-byte identical to the P1 validated command.
    q = m.build_command('qsvencc', exe, src, out, opts())
    expected = [exe, '--avhw', '-i', src, '-o', out, '-c', 'h264',
                '--gop-len', '60', '--cqp', '32',
                '--output-res', '1920x1080', '--audio-copy']
    fails += check(q == expected, 'qsvencc argv identical to P1 validated command')

    # 2) ffmpeg: downscale + GOP + audio copy + VFR passthrough.
    f = m.build_command('ffmpeg', r'C:\tools\ffmpeg.exe', src, out, opts())
    fails += check('scale=1920:1080' in f, 'ffmpeg downscales to 1920x1080')
    fails += check('-g' in f and f[f.index('-g') + 1] == '60',
                   'ffmpeg GOP = 60')
    fails += check('-c:a' in f and 'copy' in f, 'ffmpeg copies audio track')
    fails += check('-vsync' in f and '0' in f, 'ffmpeg preserves VFR timestamps')
    fails += check(src in f and out in f, 'ffmpeg keeps input/output paths')

    # 3) NVEncC shares the rigaya option names (command-level equivalence only).
    n = m.build_command('nvenc', exe, src, out, opts())
    fails += check(n[1:] == q[1:], 'nvenc uses the same option set as qsvencc')

    # 4) custom template placeholders and quoted Windows paths.
    tpl = ('"{exe}" -i "{input}" -vf scale={width}:{height} '
           '-g {gop} -crf {crf} -o "{output}"')
    c = m.build_command('custom', exe, src, out, opts(template=tpl))
    fails += check(c[0] == exe, 'custom {exe} resolves without quotes')
    fails += check(c[c.index('-i') + 1] == src,
                   'custom {input} resolves without quotes')
    fails += check('-vf' in c and 'scale=1920:1080' in c,
                   'custom {width}/{height} expand')
    fails += check('-g' in c and c[c.index('-g') + 1] == '60',
                   'custom {gop} expands')
    fails += check('-crf' in c and c[c.index('-crf') + 1] == '23',
                   'custom {crf} expands')
    fails += check(c[-1] == out, 'custom {output} resolves without quotes')

    # 5) explicit --encoder uses the env override regardless of host state.
    # find_exe() deliberately requires the override path to exist, so use a
    # real file (this Python interpreter) instead of the fake encoder path.
    real_exe = os.path.abspath(sys.executable)
    for name in ('qsvencc', 'nvenc', 'ffmpeg'):
        env = m.BACKENDS[name]['env']

        def probe(name=name, env=env):
            got, path, _ = m.select_backend(opts(encoder=name))
            return got == name and path == real_exe

        fails += check(with_env(env, real_exe, probe),
                       'explicit --encoder %s honours env override' % name)

    # 6) auto detection honours --encoder-order.
    rows = m.detect_backends()
    print('  detected:', [(r[0], bool(r[1])) for r in rows])
    avail = [r[0] for r in rows if r[1]]
    if avail:
        env = m.BACKENDS[avail[0]]['env']
        got, _, _ = with_env(
            env, exe, lambda: m.select_backend(opts(encoder_order=avail[0])))
        fails += check(got == avail[0], 'auto honours --encoder-order')
    else:
        print('  (no backend installed on this host; auto selection skipped)')

    # 7) custom requires a template.
    try:
        m.select_backend(opts(encoder='custom', template=''))
        fails += check(False, 'custom without --template must fail')
    except SystemExit:
        fails += check(True, 'custom without --template is rejected')

    # 8) list-backends is pure and must not raise.
    fails += check(m.cmd_list_backends(opts()) == 0,
                   'list-backends runs without an encoder installed')

    print('')
    print('RESULT:', 'ALL PASS' if fails == 0 else '%d FAILURE(S)' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
