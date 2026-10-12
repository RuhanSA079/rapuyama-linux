#!/usr/bin/env python3
"""decodes runblob.bin (cp15 cpu id regs read on the live e6)"""
import struct
import sys

LBL = ["sig", "cookie", "sum", "cpsr", "MIDR", "CTR", "TLBTR", "ID_PFR0",
       "ID_PFR1", "ID_DFR0", "ID_MMFR0", "ID_MMFR1", "ID_MMFR2", "ID_MMFR3",
       "ID_ISAR0", "ID_ISAR1", "ID_ISAR2", "ID_ISAR3", "ID_ISAR4", "SCTLR",
       "ACTLR", "CTR2", "-"]

PARTS = {0xB76: "ARM1176", 0xB36: "ARM1136", 0xC08: "Cortex-A8", 0xB02: "ARM11 MPCore"}
IMPL = {0x41: "ARM", 0x44: "DEC", 0x4e: "NVIDIA", 0x51: "Qualcomm", 0x54: "TI"}


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "logs/runblob.bin"
    d = open(path, "rb").read()
    w = list(struct.unpack_from("<%dI" % (len(d)//4), d, 0))
    print(f"{path}: {len(w)} words\n")
    for i, v in enumerate(w):
        if i < len(LBL):
            print(f"  [{i:2}] {LBL[i]:8} = {v:08x}")

    if len(w) > 4:
        midr = w[4]
        impl = (midr >> 24) & 0xff
        var = (midr >> 20) & 0xf
        arch = (midr >> 16) & 0xf
        part = (midr >> 4) & 0xfff
        rev = midr & 0xf
        print("\n== MIDR decode ==")
        print(f"  implementer 0x{impl:02x} ({IMPL.get(impl,'?')})")
        print(f"  variant r{var}  arch 0x{arch:x}  revision p{rev}")
        print(f"  part 0x{part:03x} => {PARTS.get(part,'unknown')}")
        print(f"  => core: {PARTS.get(part,'?')} r{var}p{rev}")

    if len(w) > 5:
        ctr = w[5]
        print("\n== CTR (cache type, ARMv6 format) ==")
        print(f"  raw={ctr:08x}  (bit24 'S'={(ctr>>24)&1}: 1=separate I/D caches)")

    if len(w) > 19:
        s = w[19]
        print("\n== SCTLR ==")
        bits = {0:"M(mmu)", 2:"C(dcache)", 11:"Z(brpred)", 12:"I(icache)",
                13:"V(hivecs)", 29:"AFE", 23:"XP(subpage)"}
        on = [n for b, n in bits.items() if (s >> b) & 1]
        print(f"  raw={s:08x}  set: {', '.join(on)}")

    print("\n(ID_ISAR/MMFR/PFR = ISA & MMU feature regs, confirm Thumb/Thumb-2,")
    print(" VFP, cache maintenance ops for the Linux kernel config.)")


if __name__ == "__main__":
    main()