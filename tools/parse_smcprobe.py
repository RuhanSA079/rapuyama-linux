#!/usr/bin/env python3
"""
parses the smcprobe.S result block out of E:\\klog.bin (e6logs dumps the whole
1 mb at phys 0x1f000000; the block lives at +0x80000, the secure-call buffer at
+0xa0000).

the question this answers: did a bare-metal smc #0 enter AND leave the secure
monitor after we killed symbian?
  - 'after' marker set (0xa5a5a5a5) -> yes, the monitor returned. r0..r3 are what
    it gave back, and the first words of the buffer show anything it wrote. this
    is the foundation for a real keepalive.
  - only 'before' set (0xb4b4b4b4) -> the smc did NOT return (hang or reset
    inside / on the way to the monitor).
  - neither set -> the blob never reached the smc (died earlier).

layout (words from 0x80000):
  +0 magic 'SMC1'  +4 ver  +8 before  +12 after  +16..28 r0..r3 after
  +32 entry cpsr   +36 entry sctlr    +44 heartbeat count  +48 last counter
  +52 smc count    +60 start counter
the fast counter is 32768 hz, so (last-start)/32768 = seconds the heartbeat lived.

usage: parse_smcprobe.py logs/klog.bin
"""
import struct
import sys

MAGIC = 0x31434d53	# 'SMC1'
OFF = 0x80000
BUF = 0xa0000
HZ = 32768.0


def main():
	path = sys.argv[1] if len(sys.argv) > 1 else "logs/klog.bin"
	d = open(path, "rb").read()
	if len(d) < OFF + 64 or struct.unpack_from("<I", d, OFF)[0] != MAGIC:
		print("no smcprobe block ('SMC1') at +0x80000 of the dump")
		return
	(magic, ver, before, after, r0, r1, r2, r3, cpsr, sctlr, _r40, hb,
	 last, _r56, smcn, _r60, start) = struct.unpack_from("<17I", d, OFF)
	print("smcprobe v%d" % ver)
	print("entry: cpsr=%08x sctlr=%08x" % (cpsr, sctlr))
	print("before marker: %08x (%s)"
	      % (before, "reached the smc" if before == 0xb4b4b4b4 else "NOT reached"))
	if after == 0xa5a5a5a5:
		print("after marker:  %08x  >>> the monitor RETURNED <<<" % after)
		print("  returned r0=%08x r1=%08x r2=%08x r3=%08x" % (r0, r1, r2, r3))
	else:
		print("after marker:  %08x  (the smc did NOT return)" % after)
	print("smc count=%d  heartbeat loops=%d" % (smcn, hb))
	if start and last:
		print("heartbeat lived %.2f s (counter %08x -> %08x)"
		      % (((last - start) & 0xffffffff) / HZ, start, last))

	if len(d) >= BUF + 32:
		w = struct.unpack_from("<8I", d, BUF)
		if any(w):
			print("secure-call buffer @0x1f0a0000 (monitor may have written here):")
			for i, v in enumerate(w):
				print("  +%02x = %08x" % (i * 4, v))
		else:
			print("secure-call buffer @0x1f0a0000: all zero (monitor wrote nothing)")


if __name__ == "__main__":
	main()