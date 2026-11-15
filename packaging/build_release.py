#!/usr/bin/env python3
"""Build ProxyLayer release assets (no compiler required).

  ProxyLayer-<tag>.au2pkg.zip      AviUtl2 drag-and-drop package
                                   (main plugin + Language/*.aul2 display packs)
  ProxyLayer-<tag>-Installer.zip   one-click installer
                                   (main plugin + display packs + luaJIT proxy)

Both binaries are committed under dist/ so CI never needs the AviUtl2 SDK headers.
Run locally:  python3 packaging/build_release.py --tag v0.3.1 --out-dir dist
"""
import argparse
import hashlib
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LANG_DIR = REPO / "Language"


def crlf(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def read(rel: str, text: bool = False) -> bytes:
    p = REPO / rel
    if not p.is_file():
        raise FileNotFoundError(f"missing release input: {rel}")
    data = p.read_bytes()
    return crlf(data) if text else data


def language_packs() -> list[tuple[str, bytes]]:
    """Language/<lang>.ProxyLayer.aul2 -> package entries (one per language)."""
    packs = sorted(LANG_DIR.glob("*.ProxyLayer.aul2"))
    if not packs:
        raise FileNotFoundError("missing release input: Language/*.ProxyLayer.aul2")
    entries = []
    for p in packs:
        data = p.read_bytes()
        if len(data) < 100:
            raise RuntimeError(f"language pack looks wrong (too small): {p.name}")
        try:
            data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise RuntimeError(f"language pack is not valid UTF-8: {p.name} ({exc})")
        entries.append((f"Language/{p.name}", data))
    return entries


def write_zip(out: Path, entries: list[tuple[str, bytes]]) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    # Fixed timestamp keeps the packages byte-for-byte reproducible.
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for arcname, payload in entries:
            zi = zipfile.ZipInfo(filename=arcname, date_time=(2026, 11, 15, 12, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            zf.writestr(zi, payload)
    sha = hashlib.sha256(out.read_bytes()).hexdigest()
    print(f"[OK] {out.name}: {out.stat().st_size:,} bytes, {len(entries)} entries, sha256={sha}")


def build(tag: str, out_dir: Path) -> None:
    plugin = read("dist/ProxyLayer.aux2")
    luaproxy = read("dist/luaJIT.dll")
    if len(plugin) < 10000:
        raise RuntimeError("dist/ProxyLayer.aux2 looks wrong (too small)")
    if len(luaproxy) < 10000:
        raise RuntimeError("dist/luaJIT.dll looks wrong (too small)")

    # package.ini lives in packaging/ so the id/name/information strings have a
    # single home; [package] is what AviUtl2 shows in its package manager.
    ini = read("packaging/package.ini", text=True).replace(b"{{TAG}}", tag.encode("utf-8"))
    notes = read("packaging/package.txt", text=True).decode("utf-8").replace("{{TAG}}", tag).encode("utf-8")
    packs = language_packs()

    # 1) Drag-and-drop AviUtl2 package: safe, plugin + display packs, never
    #    touches the folder next to aviutl2.exe.
    write_zip(out_dir / f"ProxyLayer-{tag}.au2pkg.zip", [
        ("package.ini", crlf(ini)),
        ("package.txt", crlf(notes)),
        ("Plugin/ProxyLayer.aux2", plugin),
        *packs,
    ])

    # 2) One-click installer: plugin + display packs + optional luaJIT.dll
    #    same-name forwarding proxy.
    write_zip(out_dir / f"ProxyLayer-{tag}-Installer.zip", [
        ("README-INSTALL.txt", read("packaging/README-INSTALL.txt", text=True)),
        ("install.bat", read("packaging/install.bat", text=True)),
        ("uninstall.bat", read("packaging/uninstall.bat", text=True)),
        ("install.ps1", read("packaging/install.ps1", text=True)),
        ("package.txt", crlf(notes)),
        ("Plugin/ProxyLayer.aux2", plugin),
        ("LuaProxy/luaJIT.dll", luaproxy),
        *packs,
    ])


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", default="v0.2.0", help="release tag, e.g. v0.2.0")
    ap.add_argument("--out-dir", type=Path, default=REPO / "dist")
    args = ap.parse_args()
    tag = args.tag.strip()
    if not tag.startswith("v"):
        raise SystemExit(f"--tag must start with 'v' (got {tag!r})")
    build(tag, args.out_dir)


if __name__ == "__main__":
    main()
