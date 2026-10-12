#!/usr/bin/env python3
"""
decodes fulltramp.bin, the full symbian->kernel handoff (reversible test)
the fake "kernel" at base+0x8000 echoes back the arm linux boot regs it got
with the mmu off
"""
import struct
import sys

d = open(sys.argv[1] if len(sys.argv) > 1 else "logs/fulltramp.bin", "rb").read()
st, phys, r0, r1, r2, sig = struct.unpack_from("<6I", d, 0)
print(f"status = {st} ({'OK' if st==0 else 'VA busy' if st==1 else 'err '+hex(st)})")
print(f"phys   = {phys:08x}  (payload entry at +0x8000 = {phys+0x8000:08x})")
print(f"payload received:  r0={r0:08x} (expect 0)")
print(f"                   r1={r1:08x} (machtype, expect e6e6e6e6)")
print(f"                   r2={r2:08x} (dtb ptr, expect d7b00000)")
print(f"                   ran-signature={sig:08x} (expect 5a000000)")
print()
ok = st == 0 and r0 == 0 and r1 == 0xE6E6E6E6 and r2 == 0xD7B00000 and sig == 0x5A000000
if ok:
    print("OK!!! FULL HANDOFF WORKS. We loaded a payload at the kernel offset,")
    print("   identity-mapped it, dropped the MMU, jumped in with the exact ARM")
    print("   Linux boot registers (r0=0, r1=machtype, r2=dtb), and the payload")
    print("   ran and saw them correctly. For a real boot: swap the payload for")
    print("   zImage, make the jump one-way. The LOADER IS DONE.")
elif st == 1:
    print("!! VA busy, aborted safely. Re-run for a different phys.")
else:
    print("FAIL: not verified, check which echoed reg is wrong above.")