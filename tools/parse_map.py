#!/usr/bin/env python3
"""
parses E:\\map.bin from scan.exe (= the current ptdump ReadL1 layout)
layout (little-endian u32):
[0..3]=TTBR0,TTBR1,TTBCR,SCTLR [4]=nRuns [5]=nCand [6..10]=0 [11]=nCWin
then nRuns*3 {virt,phys,pages} (filtered non-ram runs, whole space)
then nCand*2 {halVirt, LinearToPhysical}
then nCWin*3 {virt,phys,pages} (UNFILTERED sweep of 0xc0000000..0xc8000000)
"""
import struct
import sys


def ram(p):
    return 0x10000000 <= p < 0x21000000


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "logs/map.bin"
    w = list(struct.unpack_from("<%dI" % (len(open(path, 'rb').read()) // 4),
                                open(path, "rb").read(), 0))
    print(f"{path}: {len(w)} words")
    print(f"TTBR0={w[0]:08x} TTBR1={w[1]:08x} TTBCR={w[2]:08x} SCTLR={w[3]:08x}")
    nRuns, nCand, nCWin = w[4], w[5], w[11]
    print(f"nRuns={nRuns} nCand={nCand} nCWin={nCWin}\n")

    off = 16
    print("== filtered non-RAM runs (whole space) ==")
    for i in range(nRuns):
        v, p, pg = w[off+i*3:off+i*3+3]
        print(f"  {v:08x} -> {p:08x}  {pg} pg ({pg*0x1000:#x})")
    off += nRuns*3
    off += nCand*2 # skip probes

    print("\n== UNFILTERED 0xc0000000..0xc8000000 sweep (NEW) ==")
    print("  (phys in RAM range 0x10000000-0x21000000 marked [ram], these were")
    print("   hidden from the filtered sweep; a non-contiguous/odd one = suspect)")
    for i in range(nCWin):
        v, p, pg = w[off+i*3:off+i*3+3]
        tag = " [ram]" if ram(p) else ""
        print(f"  {v:08x} -> {p:08x}  {pg} pg ({pg*0x1000:#x}){tag}")
    off += nCWin*3


if __name__ == "__main__":
    main()