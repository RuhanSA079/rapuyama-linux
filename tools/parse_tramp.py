#!/usr/bin/env python3
"""decodes tramp.bin, trampoline stage 2 (reversible mmu-off test)"""
import struct
import sys

d = open(sys.argv[1] if len(sys.argv) > 1 else "logs/tramp.bin", "rb").read()
w = struct.unpack_from("<8I", d, 0)
st, phys, mw, mr, desc, prrr, nmrr = w[0], w[1], w[2], w[3], w[4], w[5], w[6]
print(f"status       = {st} ({'OK' if st==0 else 'VA already mapped' if st==1 else 'err '+hex(st)})")
print(f"phys (1MB)   = {phys:08x}")
print(f"identity desc= {desc:08x}")
print(f"PRRR         = {prrr:08x}   NMRR = {nmrr:08x}  (memory-type remap regs)")
print(f"marker write = {mw:08x}")
print(f"marker read  = {mr:08x}  (expect 600dcafe)")
print()
if st == 0 and mr == 0x600DCAFE:
    print("OK: TRAMPOLINE WORKS, we dropped the MMU in a live kernel, wrote to")
    print("   PHYSICAL memory, restored the MMU and returned alive.")
    print("   All THREE trampoline primitives are proven. Next: aim the jump at a")
    print("   real zImage (one-way) once we have peripheral drivers + a console.")
elif st == 1:
    print("!! Aborted safely (VA busy). Re-run to pick a different phys.")
else:
    print("FAIL: not verified. If the phone rebooted instead, the memory type likely")
    print("   isn't executable under TRE, use PRRR above to pick the Normal index.")