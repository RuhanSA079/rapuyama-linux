#!/usr/bin/env python3
"""
parses E:\\pdiff.bin (Kern::Printf-diff uart hunt over live peripheral pages)

record = 33 little-endian u32: [phys][A0..A15][B0..B15]
A = snapshot before a debug uart burst, B = after. regs that differ A->B got
touched by the kernel uart driver => that page is the uart, and the changed
offsets are its data/status/fifo regs
"""
import struct
import sys

REC = 33


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "logs/pdiff.bin"
    d = open(path, "rb").read()
    n = len(d) // (REC * 4)
    print(f"{path}: {len(d)} bytes, {n} pages\n")

    any_diff = False
    for r in range(n):
        rec = struct.unpack_from("<%dI" % REC, d, r * REC * 4)
        phys = rec[0]
        A = rec[1:17]
        B = rec[17:33]
        diffs = [(i, A[i], B[i]) for i in range(16) if A[i] != B[i]]
        if diffs:
            any_diff = True
            print(f"* phys {phys:08x}: {len(diffs)} register(s) changed during UART burst")
            for i, a, b in diffs:
                print(f"    +{i*4:02x} (phys {phys + i*4:08x}): {a:08x} -> {b:08x}")
            print(f"    => UART candidate base (4K): {phys & ~0xFFF:08x}\n")

    if not any_diff:
        print("No register changed during the debug-UART burst on any live page.")
        print("=> The debug UART is NOT in the scanned live regions (likely in the")
        print("   gated 0x40100000 bank, or debug output is disabled). Wait for the")
        print("   USB-UART adapter to find/confirm it by trial.")
    else:
        print("Non-zero diff found above = the active UART. Use that base for earlycon.")


if __name__ == "__main__":
    main()