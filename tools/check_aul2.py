#!/usr/bin/env python3
"""Validate Language/*.ProxyLayer.aul2 against the strings compiled into ProxyLayer.

AviUtl2 language packs are plain INI files. Three sections matter here:

  [ProxyLayer]        display names for the 12 Japanese item keys (zh / en only;
                      the Default language IS Japanese, so it needs none)
  [Panel]             settings-window / menu strings, read at runtime through
                      CONFIG_HANDLE::get_language_text(handle, L"Panel", key)
                      REQUIRED IN EVERY PACK INCLUDING Default (Japanese text
                      there). get_language_text() returns the key itself when
                      undefined, so a missing key shows "title"/"ok" verbatim;
                      the DLL also guards against that, but CI should catch it.
  [Tips.ProxyLayer]   official hover tooltips - effect.name = effect description,
                      then one line per item key. See docs.aviutl2.jp and the
                      header comment of the host's own 简体中文.aul2.

A key that drifts by one character silently falls back to the original, which
is exactly the kind of bug nobody notices for months - so CI checks it here.

  python3 tools/check_aul2.py          # validate every pack
  python3 tools/check_aul2.py -v       # also list the keys

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
TIPS_SECTION = "Tips.ProxyLayer"
PANEL_SECTION = "Panel"
DEFAULT_PACK = "Default.ProxyLayer.aul2"
FORBIDDEN_KEYS = {"ProxyLayer"}          # never translate the plugin name

# Panel keys the DLL looks up via get_language_text(). A missing key is not
# fatal (the Japanese fallback is used), but CI should surface it.
PANEL_KEYS = [
    "title", "switch", "ok", "cancel", "on", "off", "unsaved", "status",
    "menu.auto", "menu.settings", "menu.keepall", "menu.restore",
    # v0.3.8: encoder backend selector + probe button
    "encoder", "test", "encoder_ok", "encoder_missing",
]

C_STRING = r'L"((?:[^"\\]|\\.)*)"'
ITEM_RE = re.compile(
    r"FILTER_ITEM_(TRACK|CHECK|CHECK_SECTION|COLOR|SELECT|FILE|FOLDER|STRING|TEXT|DATA|GROUP|"
    r"SEPARATOR|BUTTON)\s*\("
)
STRING_RE = re.compile(C_STRING)
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
    table = TABLE_RE.search(text)
    if table:
        lits = STRING_RE.findall(_balanced(text, table.end() - 1))
        if len(lits) >= 2:
            names.append(lits[1])       # label; ASCII, filtered out later
    for m in ITEM_RE.finditer(text):
        block = _balanced(text, m.end() - 1)
        lits = STRING_RE.findall(block)
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


def parse_pack(path: Path) -> tuple[dict[str, dict[str, str]], list[str]]:
    """Return ({section: {key: value}}, warnings) for one .aul2 file."""
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

    sections: dict[str, dict[str, str]] = {}
    section = None
    for line in text.splitlines():
        s = line.strip()
        if not s or s.startswith(";"):
            continue
        if s.startswith("[") and s.endswith("]"):
            section = s[1:-1]
            sections.setdefault(section, {})
            continue
        if section is None or "=" not in s:
            continue
        key, value = s.split("=", 1)
        sections[section][key.strip()] = value.strip()
    return sections, warnings


def needs_translation(key: str) -> bool:
    return any(ord(ch) > 0x7F for ch in key)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not SRC.is_file():
        raise SystemExit("[X] missing source file: src/ProxyLayer.cpp")
    if not LANG_DIR.is_dir():
        raise SystemExit("[X] missing language folder: Language/")

    names, zero = src_keys()
    required = [k for k in dict.fromkeys(names + zero) if needs_translation(k)]
    packs = sorted(LANG_DIR.glob("*.ProxyLayer.aul2"))
    if not packs:
        raise SystemExit("[X] no Language/*.ProxyLayer.aul2 found")

    if args.verbose:
        print(f"source: {len(names)} item name(s), {len(zero)} zero-value caption(s)")
        print(f"keys needing translation/tooltips: {len(required)}")

    failures = 0
    for path in packs:
        sections, warnings = parse_pack(path)
        is_default = path.name == DEFAULT_PACK
        problems: list[str] = []

        # 1) official hover tooltips - required in EVERY pack
        tips = sections.get(TIPS_SECTION)
        if tips is None:
            problems.append(f"missing [{TIPS_SECTION}] section")
        else:
            if not tips.get("effect.name"):
                problems.append("missing Tips effect.name")
            for k in required:
                if k not in tips:
                    problems.append(f"missing tooltip for {k!r}")

        # 2) display names - Default (Japanese) needs NO translations
        entries = sections.get(SECTION, {})
        if not is_default:
            for k in required:
                if k not in entries:
                    problems.append(f"missing display name for {k!r}")
            for k in entries:
                if k in FORBIDDEN_KEYS:
                    problems.append(f"must NOT be translated (plugin name): {k!r}")
                elif k not in required:
                    warnings.append(f"stale display key: {k!r}")
        elif entries:
            warnings.append("Default pack has display names (they are unnecessary)")

        # 3) panel / menu strings - REQUIRED IN EVERY PACK, including Default.
        # get_language_text() returns the KEY ITSELF when a key is undefined
        # (config2.h:36), so a missing [Panel] key shows raw ASCII identifiers
        # like "title"/"ok" in the UI rather than falling back. Default is
        # Japanese, so its [Panel] values are Japanese text (no display-name
        # translations needed, but runtime C++ strings still are).
        panel = sections.get(PANEL_SECTION)
        if panel is None:
            problems.append(f"missing [{PANEL_SECTION}] section (raw keys would be displayed)")
        else:
            for k in PANEL_KEYS:
                if k not in panel:
                    problems.append(f"missing Panel key {k!r} (raw key would be displayed)")

        status = "[OK]" if not problems else "[X] "
        print(f"{status} {path.name}: "
              f"display={len(entries)}/{len(required)}, "
              f"tooltips={len(tips or {})}, "
              f"panel={len(sections.get(PANEL_SECTION, {}))}/{len(PANEL_KEYS)}")
        for w in warnings:
            print(f"      ! {w}")
        for p in problems:
            print(f"      - {p}")
        if args.verbose and not problems:
            for k in required:
                print(f"      {k} = {entries.get(k, '(tooltip only)')}")
        failures += 1 if problems else 0

    print(f"\n{len(packs)} pack(s) checked, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
