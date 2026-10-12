#!/usr/bin/env python3
"""decodes idtest.bin, trampoline stage 1 (identity section map proof)"""
import struct
import sys

d = open(sys.argv[1] if len(sys.argv) > 1 else "logs/idtest.bin", "rb").read()
w = struct.unpack_from("<8I", d, 0)
st, phys, idx, desc, old, rid, rrev = w[0], w[1], w[2], w[3], w[4], w[5], w[6]
print(f"status      = {st} ({'OK' if st==0 else 'VA already mapped (aborted)' if st==1 else 'error '+hex(st)})")
print(f"phys (1MB)  = {phys:08x}")
print(f"L1 index    = {idx} (VA section {idx<<20:08x})")
print(f"descriptor  = {desc:08x}  (old was {old:08x})")
print(f"read via identity VA = {rid:08x}  (expect deadbeef)")
print(f"read back via alias  = {rrev:08x}  (expect 600df00d)")
ok = st == 0 and rid == 0xDEADBEEF and rrev == 0x600DF00D
print()
if ok:
    print("OK: IDENTITY MAPPING WORKS, the ARMv6 AFE section descriptor is correct.")
    print("   The trampoline can build the 1:1 map it needs to drop the MMU.")
elif st == 1:
    print("!! Aborted safely: that VA was already mapped (no damage). Re-run may")
    print("   pick a different phys; or we switch to an unused VA window.")
else:
    print("FAIL: mapping not verified, descriptor/barrier needs fixing.")