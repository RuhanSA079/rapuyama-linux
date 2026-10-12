#!/usr/bin/env python3
"""
resolves arm (a32) pc-relative literal loads in a flat module blob and shows
which constants land in the peripheral window. those are register bases the
code actually loads (unlike raw word scans, which also hit ascii/data)

LDR (literal): cond 010(P)(U)0(0)1 1111 Rt imm12 -> mask 0x0F7F0000 == 0x051F0000
PC = insn_addr + 8 ; target = PC +/- imm12 ; value = *target
"""
import struct
import sys
from collections import defaultdict

LO, HI = 0xE0000000, 0xE1000000


def resolve(path, base):
    d = open(path, "rb").read()
    hits = defaultdict(list) # value -> [insn_vaddr,...]
    for off in range(0, len(d) - 4, 4):
        w = struct.unpack_from("<I", d, off)[0]
        if (w & 0x0F7F0000) != 0x051F0000:
            continue
        up = (w >> 23) & 1
        imm = w & 0xFFF
        insn_v = base + off
        pc = insn_v + 8
        tgt = pc + imm if up else pc - imm
        toff = tgt - base
        if 0 <= toff <= len(d) - 4:
            val = struct.unpack_from("<I", d, toff)[0]
            if LO <= val < HI:
                hits[val].append(insn_v)
    return hits


def main():
    base = int(sys.argv[2], 16)
    hits = resolve(sys.argv[1], base)
    total = sum(len(v) for v in hits.values())
    print(f"{sys.argv[1]}  base={base:#x}  "
          f"{total} literal loads into {LO:#x}.. ({len(hits)} distinct regs)")
    for val in sorted(hits):
        locs = hits[val]
        where = " ".join(f"{a:#x}" for a in locs[:4]) + (" .." if len(locs) > 4 else "")
        print(f"  {val:#010x}  x{len(locs):<3} @ {where}")


if __name__ == "__main__":
    main()