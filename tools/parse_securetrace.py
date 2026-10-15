#!/usr/bin/env python3
"""
parses the securetrace.S heartbeat log out of E:\\klog.bin (e6logs dumps the
whole 1 mb at phys 0x1f000000; the log lives at +0x80000). layout from the
blob: at 0x80000 a header, then 8 byte records {fast_counter, delta_ticks}.

header words (offsets from 0x80000):
  +0  magic 'E6TZ' (0x5a543645)   +4  version   +8  capacity (8192)
  +12 count                       +20 threshold ticks (1024)
  +24 max delta  +28 max index    +32 full flag
  +36 ID_PFR1    +40 entry cpsr   +44 entry sctlr   +48 last counter

the fast counter is 32768 Hz, so delta / 32768 = seconds between records. a run
of ~equal deltas up to the last record = the non-secure loop kept going right
up to the reset (so the reset is external / instant); one big delta near the
end = the cpu stalled first.

usage: parse_securetrace.py logs/klog.bin
"""
import struct
import sys

MAGIC = 0x5a543645	# 'E6TZ'
OFF = 0x80000
HZ = 32768.0


def main():
	path = sys.argv[1] if len(sys.argv) > 1 else "logs/klog.bin"
	d = open(path, "rb").read()
	if len(d) < OFF + 64 or struct.unpack_from("<I", d, OFF)[0] != MAGIC:
		print("no securetrace log ('E6TZ') at +0x80000 of the dump")
		return
	(magic, ver, cap, count, _r16, thr, maxd, maxi, full,
	 pfr1, cpsr, sctlr, last) = struct.unpack_from("<13I", d, OFF)
	print("securetrace v%d: %d/%d records%s, threshold %d ticks (%.1f ms)"
	      % (ver, count, cap, " (FULL/wrapped-off)" if full else "",
		 thr, thr / HZ * 1000))
	print("entry: ID_PFR1=%08x cpsr=%08x sctlr=%08x" % (pfr1, cpsr, sctlr))
	sec_bit = (pfr1 >> 4) & 0xf
	print("  ID_PFR1 security-extensions field = %d (%s)"
	      % (sec_bit, "present" if sec_bit else "absent"))

	n = min(count, cap)
	recs = [struct.unpack_from("<II", d, OFF + 64 + i * 8) for i in range(n)]
	if not recs:
		print("no records: the loop never completed one interval")
		return
	span = (recs[-1][0] - recs[0][0]) & 0xffffffff
	print("alive for %.2f s across the log (last counter %08x)"
	      % (span / HZ, last))
	print("biggest gap: %.1f ms at record %d" % (maxd / HZ * 1000, maxi))
	print()
	print("last 20 records (counter, gap ms):")
	for c, delta in recs[-20:]:
		flag = "  <-- big gap" if delta >= 2 * thr else ""
		print("  %08x  %8.1f%s" % (c, delta / HZ * 1000, flag))


if __name__ == "__main__":
	main()