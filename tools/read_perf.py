#!/usr/bin/env python3
"""Summarise ProxyLayer-perf.log.

The plugin writes one aggregate line every 60 filter invocations (tag W) and
one guaranteed final line on unload (tag EXIT). Each line records how often the
render path skipped decoding the full-resolution original, plus a timing
breakdown.

Usage:
    python3 read_perf.py [path-to-ProxyLayer-perf.log]

Default path is the real installation's plugin folder.
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
)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    if not os.path.isfile(path):
        print("no log yet:", path)
        print("Open AviUtl2, play for ~10 seconds, then close it (EXIT flushes).")
        return 1

    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = PAT.search(line)
            if m:
                rows.append({k: (float(v) if k not in ("ts", "tag") else v)
                             for k, v in m.groupdict().items()})
    if not rows:
        print("no parsable lines in", path)
        return 1

    print("file:", path)
    print("windows:", len(rows), " (W = periodic, EXIT = flushed on unload)")
    print()
    hdr = ("%-22s %6s %6s %7s %11s %12s %12s" %
           ("time", "calls", "fast%", "div", "filter_avg", "decode_avg", "decode_max"))
    print(hdr)
    print("-" * len(hdr))
    divs = [(1, "d1"), (2, "d2"), (4, "d4"), (8, "d8"),
            (16, "d16"), (32, "d32"), (64, "d64")]
    tot_calls = tot_fast = 0
    for r in rows:
        dominant = max(divs, key=lambda d: r[d[1]])[0]
        pct = 100.0 * r["fast"] / r["calls"] if r["calls"] else 0
        print("%-22s %6d %5.1f%% %7s %9.3fms %10.3fms %10.1fms" %
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
    print()
    print("Reading the numbers:")
    print("  fast%  ~100  -> the original full-res image is decoded only once per source")
    print("  decode_avg high, filter_avg low -> proxy size is NOT the bottleneck;")
    print("                                    time is in decode / downstream effects")
    print("  div mostly 8 -> AUTO already at its cap; levels 4/5 (1/16, 1/32) are manual")
    return 0


if __name__ == "__main__":
    sys.exit(main())
