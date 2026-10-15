#!/usr/bin/env python3
"""
flight recorder from E:\\klog.bin (ramread.sis dump of phys 0x1f000000), written
by mach-rapuyama every 100 ms: at +0xf8000 u32 'E6FR', u32 samples written,
u32 nregs, u32 nslots, u32 reg offsets (from phys 0x20000000), slots from
+0x100: u32 seq, u32 uptime ms, u32 regs[nregs].

prints the last samples oldest first, only the regs that changed at least
once (the counters always do), and marks each change with *.

usage: parse_flight.py logs/klog.bin [how many samples, default all]
"""
import struct
import sys

LOG_MAGIC = 0x474c3645	# 'E6LG', start of the ramcon buffer
FR_MAGIC = 0x52463645	# 'E6FR'
FR_OFF = 0xf8000
FR_DATA = 0x100
COUNTERS = (0x33350, 0x336c4)	# always move, shown but not starred


def main():
	path = sys.argv[1] if len(sys.argv) > 1 else "logs/klog.bin"
	want = int(sys.argv[2]) if len(sys.argv) > 2 else 0
	d = open(path, "rb").read()
	base = 0
	for i in range(0, len(d) - 8, 4):
		if struct.unpack_from("<I", d, i)[0] == LOG_MAGIC:
			base = i
			break
	fr = base + FR_OFF
	if fr + 16 > len(d) or struct.unpack_from("<I", d, fr)[0] != FR_MAGIC:
		print("no flight recorder ('E6FR') in the dump, old kernel?")
		return
	written, nregs, nslots = struct.unpack_from("<III", d, fr + 4)
	regs = struct.unpack_from("<%dI" % nregs, d, fr + 16)
	slot_size = (2 + nregs) * 4
	n = min(written, nslots)
	samples = []
	for seq in range(written - n, written):
		o = fr + FR_DATA + (seq % nslots) * slot_size
		s = struct.unpack_from("<II%dI" % nregs, d, o)
		if s[0] != seq:
			continue	# torn slot, the reset hit mid write
		samples.append(s)
	if want:
		samples = samples[-want:]
	if not samples:
		print("flight recorder is empty")
		return

	moving = [i for i in range(nregs)
		  if len({s[2 + i] for s in samples}) > 1]
	still = [i for i in range(nregs) if i not in moving]
	print("%d samples written, showing %d (%.1f s), last at uptime %.1f s"
	      % (written, len(samples),
		 (samples[-1][1] - samples[0][1]) / 1000.0,
		 samples[-1][1] / 1000.0))
	print("constant: " + ", ".join("%08x=%08x" % (0x20000000 + regs[i],
						    samples[0][2 + i])
				       for i in still))
	print()
	print("   uptime  " + " ".join("%9x" % (0x20000000 + regs[i])
				       for i in moving))
	prev = None
	for s in samples:
		cells = []
		for i in moving:
			v = s[2 + i]
			mark = " "
			if prev and prev[2 + i] != v and regs[i] not in COUNTERS:
				mark = "*"
			cells.append("%08x%s" % (v, mark))
		print("%9.1f  %s" % (s[1] / 1000.0, " ".join(cells)))
		prev = s


if __name__ == "__main__":
	main()