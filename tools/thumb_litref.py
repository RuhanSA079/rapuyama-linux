#!/usr/bin/env python3
"""
finds thumb/thumb-2 LDR-literal loads of peripheral-looking constants in a
flat module blob and dumps a bit of code after each, so i can eyeball whether
the loaded value gets dereferenced (a real address) or passed to a function /
stored in a table (a hal-encoded id)

encodings:
T1 LDR (literal): 0x4800..0x4FFF -> Rt=(hw>>8)&7, imm8=(hw&0xFF)*4,
target=(align4(pc))+imm8 (pc = insn+4)
T2 LDR.W (literal): first hw 0xF85F (U=0) / 0xF8DF (U=1),
second hw: Rt=(hw2>>12)&0xF, imm12=hw2&0xFFF,
target=align4(pc) +/- imm12 (pc = insn+4)
"""
import struct
import sys


def align4(x):
    return x & ~3


def main():
    path = sys.argv[1]
    base = int(sys.argv[2], 16) # vaddr of file offset 0
    d = open(path, "rb").read()
    n = len(d)

    def rd16(off):
        return struct.unpack_from("<H", d, off)[0]

    def rd32(off):
        return struct.unpack_from("<I", d, off)[0]

    # collect every plausible LDR-literal and its resolved target value
    refs = [] # (insn_off, kind, rt, tgt_off, value)
    off = 0
    while off + 2 <= n:
        hw = rd16(off)
        # T1
        if 0x4800 <= hw <= 0x4FFF:
            rt = (hw >> 8) & 7
            imm8 = (hw & 0xFF) * 4
            pc = off + 4
            tgt = align4(pc) + imm8
            if 0 <= tgt <= n - 4:
                refs.append((off, "T1", rt, tgt, rd32(tgt)))
            off += 2
            continue
        # T2 LDR.W literal
        if (hw & 0xFF7F) == 0xF85F and off + 4 <= n:
            hw2 = rd16(off + 2)
            if (hw2 & 0x0FFF) == (hw2 & 0x0FFF): # always true, just keeps the structure
                u = (hw >> 7) & 1
                rt = (hw2 >> 12) & 0xF
                imm12 = hw2 & 0xFFF
                pc = off + 4
                tgt = align4(pc) + imm12 if u else align4(pc) - imm12
                if 0 <= tgt <= n - 4:
                    refs.append((off, "T2", rt, tgt, rd32(tgt)))
            off += 2
            continue
        off += 2

    # what we care about: peripheral-looking constants
    def interesting(v):
        return (v & 0xFF000000) == 0xE0000000 or (0x30000000 <= v < 0x50000000) \
            or (0x08000000 <= v < 0x0A000000)

    hits = [r for r in refs if interesting(r[4])]
    print(f"{path}  base={base:#x}  {len(refs)} ldr-literal candidates, "
          f"{len(hits)} into peripheral ranges\n")
    for insn_off, kind, rt, tgt_off, val in sorted(hits, key=lambda x: x[4]):
        iv = base + insn_off
        print(f"val={val:08x}  via {kind} r{rt}  @ insn {iv:08x} "
              f"(pool off {tgt_off:#x})")
        # dump 20 bytes of code after the load to eyeball
        tail = d[insn_off: insn_off + 20]
        hexs = " ".join(f"{b:02x}" for b in tail)
        print(f"    code: {hexs}")


if __name__ == "__main__":
    main()