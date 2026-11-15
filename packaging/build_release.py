#!/usr/bin/env python3
"""Build ProxyLayer release assets (no compiler required).

  ProxyLayer-<tag>.au2pkg.zip      AviUtl2 drag-and-drop package (main plugin only)
  ProxyLayer-<tag>-Installer.zip   one-click installer (main plugin + luaJIT proxy)

Both binaries are committed under dist/ so CI never needs the AviUtl2 SDK headers.
Run locally:  python3 packaging/build_release.py --tag v0.2.0 --out-dir dist
"""
import argparse
import hashlib
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def crlf(data: bytes) -> bytes:
    return data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")


def read(rel: str, text: bool = False) -> bytes:
    p = REPO / rel
    if not p.is_file():
        raise FileNotFoundError(f"missing release input: {rel}")
    data = p.read_bytes()
    return crlf(data) if text else data


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

    ini = (
        "[package]\n"
        "id=ProxyLayer\n"
        "name=ProxyLayer / 代理图层\n"
        f"information=ProxyLayer {tag} - one-click whole-project preview proxy by CylnaNangong & WorkBuddy AI\n"
        "uninstallSubFolderFile=0\n"
    ).encode("utf-8")
    notes = read("packaging/package.txt", text=True).decode("utf-8").replace("{{TAG}}", tag).encode("utf-8")

    # 1) Drag-and-drop AviUtl2 package: safe, plugin only, never touches the exe dir.
    write_zip(out_dir / f"ProxyLayer-{tag}.au2pkg.zip", [
        ("package.ini", crlf(ini)),
        ("package.txt", crlf(notes)),
        ("Plugin/ProxyLayer.aux2", plugin),
    ])

    # 2) One-click installer: plugin + optional luaJIT.dll same-name proxy.
    write_zip(out_dir / f"ProxyLayer-{tag}-Installer.zip", [
        ("README-INSTALL.txt", read("packaging/README-INSTALL.txt", text=True)),
        ("install.bat", read("packaging/install.bat", text=True)),
        ("uninstall.bat", read("packaging/uninstall.bat", text=True)),
        ("install.ps1", read("packaging/install.ps1", text=True)),
        ("Plugin/ProxyLayer.aux2", plugin),
        ("LuaProxy/luaJIT.dll", luaproxy),
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
