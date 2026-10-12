#!/usr/bin/env python3
"""
parses E:\\scandata.bin from scan.exe (the mmio scanner that survives crashes)

record = 20 little-endian u32:
[idx][phys][virt][rc][w0..w15] (first 16 regs 0x00..0x3C of the page)
rc==0 means the page read fine. gaps in idx (pages in the map that never got
recorded) are pages that rebooted the phone -> unreadable mmio
"""
import struct
import sys

REC = 20 # words


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "logs/scandata.bin"
    d = open(path, "rb").read()
    n = len(d) // (REC * 4)
    print(f"{path}: {len(d)} bytes, {n} page records\n")

    seen = set()
    ok = []
    for r in range(n):
        rec = struct.unpack_from("<%dI" % REC, d, r * REC * 4)
        idx, phys, virt, rc = rec[0], rec[1], rec[2], rec[3]
        regs = rec[4:]
        seen.add(idx)
        if rc == 0:
            ok.append((idx, phys, virt, regs))

    print(f"== readable pages: {len(ok)} / {n} recorded ==")
    for idx, phys, virt, regs in ok:
        nz = sum(1 for x in regs if x not in (0, 0xFFFFFFFF))
        if nz == 0:
            continue # all zero/FF, probably not a live register block
        line = " ".join(f"{x:08x}" for x in regs[:8])
        line2 = " ".join(f"{x:08x}" for x in regs[8:])
        print(f"\n  phys {phys:08x} (virt {virt:08x}) idx={idx}")
        print(f"    +00: {line}")
        print(f"    +20: {line2}")

    # gaps = pages that were tried but never got a record => crashers
    if seen:
        maxidx = max(seen)
        gaps = [i for i in range(maxidx + 1) if i not in seen]
        if gaps:
            print(f"\n== pages that likely CRASHED (gaps, {len(gaps)}) ==")
            print("   indices:", gaps)

    # dumb uart heuristic: 16550 LSR is often 0x60, PL011 FR (0x18) has bit4/bit7
    print("\n== quick heuristics ==")
    for idx, phys, virt, regs in ok:
        lsr = regs[5] & 0xff # offset 0x14 (16550 LSR)
        fr = regs[6] # offset 0x18 (PL011 FR)
        if lsr in (0x60, 0x61, 0x62, 0x63, 0xe0, 0xe1):
            print(f"  phys {phys:08x}: +14=0x{lsr:02x} looks like a 16550 LSR")
        if (fr & ~0xff) == 0 and (fr & 0x90):
            print(f"  phys {phys:08x}: +18=0x{fr:08x} could be a PL011 FR")


if __name__ == "__main__":
    main()