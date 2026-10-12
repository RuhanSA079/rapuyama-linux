#!/usr/bin/env python3
"""
diffs wdiff.bin (A then B snapshots of the 0x30000000 sysctrl, 256 words each)
regs that changed A->B are free-running counters = system timer / watchdog
"""
import struct, sys
d = open(sys.argv[1] if len(sys.argv) > 1 else "logs/wdiff.bin", "rb").read()
N = (len(d)//4)//2
A = struct.unpack_from("<%dI" % N, d, 0)
B = struct.unpack_from("<%dI" % N, d, N*4)
base = 0x30000000
print(f"{N} words each; phys base {base:08x}\n== changed registers (free-running counters) ==")
any_ch = False
for i in range(N):
    if A[i] != B[i]:
        any_ch = True
        delta = (B[i]-A[i]) & 0xffffffff
        print(f"  +{i*4:04x} (phys {base+i*4:08x}): {A[i]:08x} -> {B[i]:08x}  (+{delta})")
if not any_ch:
    print("  none changed -> no counter in this window; widen/retarget.")
else:
    print("\nA fast-incrementing one = the system timer (clocksource/clockevent).")
    print("A slow one near a reset threshold = the watchdog counter.")
    print("Cross-check the offsets against the DTB sysctrl@30000000 node.")