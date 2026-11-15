#!/usr/bin/env python3
"""Minimal minidump inspector: exception record, faulting module, and a
poor-man's x64 stack trace built by scanning the crashing thread's stack for
return addresses that fall inside loaded module ranges.

No external dependencies, no debugger/symbols required.
Usage: python3 dump_stack.py <path-to-dmp> [max_frames]
"""
import struct
import sys
import os


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def read_wstring(b, rva):
    try:
        n = u32(b, rva)
        raw = b[rva + 4:rva + 4 + n]
        return raw.decode("utf-16-le", "replace").rstrip("\x00")
    except Exception:
        return "?"


def main():
    if len(sys.argv) < 2:
        print("usage: dump_stack.py <dmp> [max_frames]")
        return 1
    path = sys.argv[1]
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 48
    with open(path, "rb") as f:
        b = f.read()

    if b[:4] != b"MDMP":
        print("not a minidump")
        return 1

    nstreams = u32(b, 8)
    dir_rva = u32(b, 12)
    streams = {}
    for i in range(nstreams):
        o = dir_rva + i * 12
        stype, size, rva = struct.unpack_from("<III", b, o)
        streams.setdefault(stype, []).append((size, rva))

    print("== dump ==")
    print(os.path.basename(path), len(b), "bytes,", nstreams, "streams")

    # --- modules ---
    modules = []
    for size, rva in streams.get(4, []):
        n = u32(b, rva)
        for i in range(n):
            o = rva + 4 + i * 108
            base = u64(b, o)
            imgsize = u32(b, o + 8)
            name_rva = u32(b, o + 20)
            name = read_wstring(b, name_rva)
            modules.append((base, base + imgsize, os.path.basename(name)))
    print("== modules ==", len(modules))

    def which(addr):
        for lo, hi, name in modules:
            if lo <= addr < hi:
                return "%s+0x%x" % (name, addr - lo)
        return None

    # --- exception ---
    exc_thread = None
    for size, rva in streams.get(6, []):
        tid = u32(b, rva)
        er = rva + 8
        code = u32(b, er)
        flags = u32(b, er + 4)
        addr = u64(b, er + 16)
        nparams = u32(b, er + 24)
        info = [u64(b, er + 32 + 8 * i) for i in range(min(nparams, 15))]
        ctx_size, ctx_rva = struct.unpack_from("<II", b, er + 152)
        exc_thread = tid
        print("== exception ==")
        print(" thread=0x%x code=0x%08x flags=0x%x address=0x%x %s" %
              (tid, code, flags, addr, which(addr)))
        print(" params:", [hex(x) for x in info[:nparams]])
        if ctx_size >= 256:
            # CONTEXT_AMD64: Rax @120, Rsp @152, Rip @248
            rip = u64(b, ctx_rva + 248)
            rsp = u64(b, ctx_rva + 152)
            rax = u64(b, ctx_rva + 120)
            rcx = u64(b, ctx_rva + 128)
            rdx = u64(b, ctx_rva + 136)
            print(" RIP=0x%x %s   RSP=0x%x" % (rip, which(rip), rsp))
            print(" RAX=0x%x RCX=0x%x RDX=0x%x (%s / %s / %s)" %
                  (rax, rcx, rdx, which(rax), which(rcx), which(rdx)))

    # --- threads ---
    thread_stack = {}
    for size, rva in streams.get(3, []):
        n = u32(b, rva)
        for i in range(n):
            o = rva + 4 + i * 48
            tid = u32(b, o)
            stack_start = u64(b, o + 24)
            stack_size = u32(b, o + 32)
            stack_rva = u32(b, o + 36)
            thread_stack[tid] = (stack_start, stack_size, stack_rva)

    # --- memory64 ranges (fallback for stack bytes) ---
    mem64 = []
    for size, rva in streams.get(9, []):
        nranges = u64(b, rva)
        base_rva = u64(b, rva + 8)
        cur = base_rva
        for i in range(nranges):
            start, dsize = struct.unpack_from("<QQ", b, rva + 16 + i * 16)
            mem64.append((start, dsize, cur))
            cur += dsize

    def read_mem(addr, length):
        # thread inline stack descriptor
        for tid, (start, dsize, srva) in thread_stack.items():
            if start <= addr < start + dsize:
                off = addr - start
                return b[srva + off:srva + min(dsize, off + length)]
        for start, dsize, mrva in mem64:
            if start <= addr < start + dsize:
                off = addr - start
                return b[mrva + off:mrva + min(dsize, off + length)]
        return b""

    # --- stack scan on the faulting thread ---
    if exc_thread is not None and exc_thread in thread_stack:
        start, dsize, srva = thread_stack[exc_thread]
        print("== stack ==")
        print(" thread=0x%x range=0x%x..0x%x (%d bytes) rva=0x%x" %
              (exc_thread, start, start + dsize, dsize, srva))
        stack = b[srva:srva + dsize]
        frames = []
        for off in range(0, len(stack) - 8, 8):
            v = u64(stack, off)
            m = which(v)
            if m:
                frames.append((start + off, v, m))
        # keep the first N in stack order (= innermost first)
        for saddr, v, m in frames[:limit]:
            print("  [rsp+0x%04x] 0x%x  %s" % (saddr - start, v, m))
        print("  (%d candidate return addresses total)" % len(frames))

        # module histogram over the whole stack
        from collections import Counter
        c = Counter(m.split("+")[0] for _, _, m in frames)
        print("== stack module histogram ==")
        for name, cnt in c.most_common(20):
            print("  %-42s %d" % (name, cnt))
    else:
        print("no stack descriptor for exception thread")
        print("threads:", ["0x%x" % k for k in thread_stack])
    return 0


if __name__ == "__main__":
    sys.exit(main())
