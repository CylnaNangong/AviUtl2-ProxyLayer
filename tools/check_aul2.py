#!/usr/bin/env python3
"""Check Language/*.aul2 display packs against the strings compiled into ProxyLayer.

The plugin's item names ARE the translation keys: AviUtl2 looks them up in the
.aul2 section named after the plugin file ([ProxyLayer]).  A key that drifts by
one character silently falls back to the original (Chinese) label, which is
exactly the kind of bug nobody notices for months -- so CI checks it here.

  python3 tools/check_aul2.py            # check every Language/*.aul2
  python3 tools/check_aul2.py -v         # also list the keys

Rules enforced
  1. every non-ASCII key defined in src/ProxyLayer.cpp is translated;
  2. the plugin NAME ("ProxyLayer") is never translated (the plugin identifies
     its own effects with wcscmp(name, L"ProxyLayer"));
  3. the section is [ProxyLayer] and the file is valid UTF-8, LF-free, CRLF;
  4. no stray keys that no longer exist in the source.
Only the standard library is used, so this also runs in plain CI.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src" / "ProxyLayer.cpp"
LANG_DIR = REPO / "Language"
SECTION = "ProxyLayer"
FORBIDDEN_KEYS = {"ProxyLayer"}          # never translate the plugin name

C_STRING = r'L"((?:[^"\\]|\\.)*)"'
ITEM_RE = re.compile(
    r"FILTER_ITEM_(TRACK|CHECK|CHECK_SECTION|COLOR|SELECT|FILE|FOLDER|STRING|TEXT|DATA|GROUP|"
    r"SEPARATOR|BUTTON)\s*\("
)
STRING_RE = re.compile(C_STRING)
# FILTER_ITEM_TRACK(name, value, s, e, step, zero_display) -- the zero value
# caption is also shown to the user, so it needs a translation as well.
TABLE_RE = re.compile(r"FILTER_PLUGIN_TABLE\s+\w+\s*=\s*\{")


def _balanced(text: str, start: int) -> str:
    """Return the text of a (...) or {...} block that starts at `start`."""
    opener, closer = text[start], ("}" if text[start] == "{" else ")")
    depth, i = 0, start
    while i < len(text):
        if text[i] == opener:
            depth += 1
        elif text[i] == closer:
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return text[start:]


def src_keys() -> tuple[list[str], list[str]]:
    """Return (translatable keys, zero-display captions) from the plugin source."""
    text = SRC.read_text(encoding="utf-8")
    names: list[str] = []
    zero: list[str] = []
    # FILTER_PLUGIN_TABLE = { flag, name, label, information, items, ... }
    # `name` is the identity used by create_effect()/get_effect_name() and must
    # never be translated; `label` is the thing the user actually sees.
    table = TABLE_RE.search(text)
    if table:
        lits = STRING_RE.findall(_balanced(text, table.end() - 1))
        if len(lits) >= 2:
            names.append(lits[1])
    for m in ITEM_RE.finditer(text):
        block = _balanced(text, m.end() - 1)     # at '('
        lits = STRING_RE.findall(block)
        # Concatenated literals (a long separator caption) arrive as several
        # pieces; glue the leading ones back together.
        if not lits:
            continue
        if m.group(1) == "SEPARATOR":
            names.append("".join(lits))
            continue
        if m.group(1) == "TRACK" and len(lits) >= 2:
            names.append(lits[0])
            zero.append("".join(lits[1:]))
        else:
            names.append(lits[0])
    return names, zero


def parse_pack(path: Path) -> tuple[dict[str, str], list[str]]:
    """Return ({key: value}, warnings) for one .aul2 file."""
    raw = path.read_bytes()
    warnings: list[str] = []
    if raw.startswith(b"\xef\xbb\xbf"):
        warnings.append("starts with a UTF-8 BOM (the official samples have none)")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SystemExit(f"[X] {path.name}: not valid UTF-8 ({exc})")
    if b"\n" in raw.replace(b"\r\n", b""):
        warnings.append("contains bare LF line endings (use CRLF)")

    section = None
    entries: dict[str, str] = {}
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith(";"):
            continue
        if s.startswith("[") and s.endswith("]"):
            section = s[1:-1]
            continue
        if section != SECTION or "=" not in s:
            continue
        key, value = s.split("=", 1)
        entries[key.strip()] = value.strip()
    if section is None:
        warnings.append(f"no [{SECTION}] section")
    return entries, warnings


def needs_translation(key: str) -> bool:
    return any(ord(ch) > 0x7F for ch in key)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not SRC.is_file():
        raise SystemExit(f"[X] missing source file: {SRC}")
    if not LANG_DIR.is_dir():
        raise SystemExit(f"[X] missing language folder: {LANG_DIR}")

    names, zero = src_keys()
    required = [k for k in dict.fromkeys(names + zero) if needs_translation(k)]
    packs = sorted(LANG_DIR.glob("*.aul2"))
    if not packs:
        raise SystemExit("[X] no Language/*.aul2 found")

    if args.verbose:
        print(f"source: {len(names)} item name(s), {len(zero)} zero-value caption(s)")
        print(f"keys that need a translation: {len(required)}")

    failures = 0
    for path in packs:
        entries, warnings = parse_pack(path)
        missing = [k for k in required if k not in entries]
        extra = [k for k in entries if k not in required and k not in ("ProxyLayer",)]
        forbidden = [k for k in entries if k in FORBIDDEN_KEYS]
        ok = not missing and not forbidden
        status = "[OK]" if ok else "[X] "
        print(f"{status} {path.name}: {len(entries)} key(s), "
              f"{len(required) - len(missing)}/{len(required)} translated")
        for w in warnings:
            print(f"      ! {w}")
        for k in missing:
            print(f"      - missing: {k!r}")
        for k in forbidden:
            print(f"      - must NOT be translated (plugin name): {k!r}")
        for k in extra:
            print(f"      ? not found in src (stale key?): {k!r}")
        if args.verbose and ok:
            for k in required:
                print(f"      {k} = {entries[k]}")
        failures += 0 if ok else 1

    # A language folder with no pack for the default host language is fine, but
    # shipping a pack that renames the effect is not.
    print(f"\n{len(packs)} pack(s) checked, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
