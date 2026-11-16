#!/usr/bin/env python3
"""Summarise ProxyLayer-perf.log.

The plugin writes one aggregate line every 60 filter invocations (tag W) and
one guaranteed final line on unload (tag EXIT). Each line records how often the
render path skipped decoding the full-resolution original, plus a timing
breakdown.

Three telemetry generations are recognised:
  v0.3.1 and earlier : no L0 suffix
  v0.3.2             : L0:gate= keep= auto1=
  v0.3.3             : + cap= sub= big= maxw= d1mp=

Usage:
    python3 read_perf.py [path-to-ProxyLayer-perf.log] [--last N]

Default path is the real installation's plugin folder. --last N restricts the
report to the final N windows, which is handy when comparing a freshly
restarted session against older runs in the same append-only log.
"""
import os
import re
import sys

DEFAULT = r"C:\ProgramData\aviutl2\Plugin\ProxyLayer-perf.log"

PAT = re.compile(
    r"\[(?P<ts>[^\]]+)\] (?P<tag>\S+) "
    r"calls=(?P<calls>\d+) win=(?P<win>[\d.]+)s "
    r"fast=(?P<fast>\d+) slow=(?P<slow>\d+) "
    r"filter_avg=(?P<filter>[\d.]+)ms filter_sum=(?P<filtersum>[\d.]+)ms "
    r"decode_avg=(?P<decode>[\d.]+)ms decode_max=(?P<decodemax>[\d.]+)ms "
    r"div\[1\]=(?P<d1>\d+) \[2\]=(?P<d2>\d+) \[4\]=(?P<d4>\d+) "
    r"\[8\]=(?P<d8>\d+) \[16\]=(?P<d16>\d+) \[32\]=(?P<d32>\d+) "
    r"\[64\]=(?P<d64>\d+)"
    r"(?:\s+L0:gate=(?P<gate>\d+) keep=(?P<keep>\d+) auto1=(?P<a1>\d+)"
    r"(?:\s+cap=(?P<cap>\d+) sub=(?P<sub>\d+) big=(?P<big>\d+)"
    r"\s+maxw=(?P<maxw>\d+) d1mp=(?P<d1mp>\d+))?)?"
)


def main():
    args = []
    last_n = None
    skip_next = False
    for i, a in enumerate(sys.argv[1:]):
        if skip_next:
            skip_next = False
            continue
        if a == "--last":
            if i + 2 < len(sys.argv):
                last_n = int(sys.argv[i + 2])
                skip_next = True
            continue
        args.append(a)
    path = args[0] if args else DEFAULT
    if not os.path.isfile(path):
        print("no log yet:", path)
        print("Open AviUtl2, play for ~10 seconds, then close it (EXIT flushes).")
        return 1

    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = PAT.search(line)
            if m:
                rows.append({k: (float(v) if v is not None and k not in ("ts", "tag") else v)
                             for k, v in m.groupdict().items()})
    if not rows:
        print("no parsable lines in", path)
        return 1
    if last_n:
        rows = rows[-last_n:]

    print("file:", path)
    print("windows:", len(rows), " (W = periodic, EXIT = flushed on unload)")
    print()
    hdr = ("%-14s %6s %6s %7s %11s %12s %12s" %
           ("time", "calls", "fast%", "div", "filter_avg", "decode_avg", "decode_max"))
    print(hdr)
    print("-" * len(hdr))
    divs = [(1, "d1"), (2, "d2"), (4, "d4"), (8, "d8"),
            (16, "d16"), (32, "d32"), (64, "d64")]
    tot_calls = tot_fast = 0
    for r in rows:
        dominant = max(divs, key=lambda d: r[d[1]])[0]
        pct = 100.0 * r["fast"] / r["calls"] if r["calls"] else 0
        print("%-14s %6d %5.1f%% %7s %9.3fms %10.3fms %10.1fms" %
              (r["ts"], r["calls"], pct, "1/%d" % dominant,
               r["filter"], r["decode"], r["decodemax"]))
        tot_calls += r["calls"]
        tot_fast += r["fast"]

    print()
    print("TOTAL calls=%d fast=%d (%.1f%%) slow=%d" %
          (tot_calls, tot_fast, 100.0 * tot_fast / tot_calls if tot_calls else 0,
           tot_calls - tot_fast))
    slow_rows = [r for r in rows if r["slow"] > 0]
    if slow_rows:
        worst = max(slow_rows, key=lambda r: r["decode"])
        print("worst decode in a window: %.1f ms (that is the cost the fast path avoids)"
              % worst["decode"])

    # ---- L0 reason breakdown (v0.3.2+) -------------------------------------
    diag = [r for r in rows if r["a1"] is not None]
    if diag:
        c = sum(r["calls"] for r in diag)
        print()
        print("L0 reasons (v0.3.2+ windows, calls=%d):" % c)
        for key, label in (("gate", "export/save gate"),
                           ("keep", "layer kept original"),
                           ("a1", "AUTO decided div=1")):
            n = sum(r[key] or 0 for r in diag)
            print("  %-24s %6d  (%5.1f%% of calls)" % (label, n, 100.0 * n / c if c else 0))

    # ---- v0.3.3 fix verification ------------------------------------------
    v33 = [r for r in rows if r["cap"] is not None]
    if v33:
        c = sum(r["calls"] for r in v33)
        print()
        print("AUTO v3 sub-scene fix (v0.3.3+ windows, calls=%d):" % c)
        for key, label in (("cap", "budget raised div"),
                           ("sub", "composited sub-scene"),
                           ("big", "oversized canvas")):
            n = sum(r[key] or 0 for r in v33)
            print("  %-24s %6d  (%5.1f%% of calls)" % (label, n, 100.0 * n / c if c else 0))
        print("  widest scene canvas      %6d px" % max(r["maxw"] or 0 for r in v33))
        print("  largest div=1 source     %6d MPx" % max(r["d1mp"] or 0 for r in v33))
        d1 = sum(r["d1"] or 0 for r in v33)
        print("  div=1 frames             %6d  (%5.1f%% of calls)" %
              (d1, 100.0 * d1 / c if c else 0))
        print("  -> healthy: div=1 near 0 for huge sources, fast%% near 100, "
              "decode_avg near 0")
    return 0


if __name__ == "__main__":
    sys.exit(main())
