#!/usr/bin/env python3
"""
phase calibration result from E:\\klog.bin (ramread / e6logs dump of phys
0x1f000000, 1 MiB)

so for each run this prints how long linux lived counted from the handoff and
counted from the last request symbian sent. if the reset is a supervisor
waiting for that request, "after the request" stays the same from run to run
while "after the handoff" jumps around.

usage:
  parse_calib.py logs/klog.bin # one run
  parse_calib.py logs/klog.bin runs.csv NAME # same, and append it to runs.csv
  parse_calib.py --summary runs.csv # compare the spread over all runs
"""
import csv
import statistics
import struct
import sys

LOG_MAGIC = 0x474c3645	# 'E6LG'
FR_MAGIC = 0x52463645	# 'E6FR'
FR_OFF = 0xf8000
FR_DATA = 0x100
ISAH_OFF = 0xf1000	# phys 0x1f0f1000
CALIB_MAGIC = 0x424c4143	# 'CALB'
CALIB_WORD = 0x100
FC_REG = 0x33350
FC_HZ = 32768.0


def last_fr_sample(d, base):
	"""(uptime s, fast counter) of the newest intact flight recorder slot"""
	fr = base + FR_OFF
	if fr + 16 > len(d) or struct.unpack_from("<I", d, fr)[0] != FR_MAGIC:
		return None
	written, nregs, nslots = struct.unpack_from("<III", d, fr + 4)
	regs = struct.unpack_from("<%dI" % nregs, d, fr + 16)
	if FC_REG not in regs:
		return None
	fci = regs.index(FC_REG)
	slot_size = (2 + nregs) * 4
	for seq in range(written - 1, max(written - nslots, 0) - 1, -1):
		o = fr + FR_DATA + (seq % nslots) * slot_size
		s = struct.unpack_from("<II%dI" % nregs, d, o)
		if s[0] == seq:
			return s[1] / 1000.0, s[2 + fci]
	return None


def one(path):
	d = open(path, "rb").read()
	base = 0
	for i in range(0, len(d) - 8, 4):
		if struct.unpack_from("<I", d, i)[0] == LOG_MAGIC:
			base = i
			break
	co = base + ISAH_OFF + CALIB_WORD * 4
	if co + 0xe0 > len(d) or struct.unpack_from("<I", d, co)[0] != CALIB_MAGIC:
		print("no calibration block ('CALB') in the dump: booter without RB_CALIB?")
		return None
	ver, fc_handoff, fc_req, seen = struct.unpack_from("<IIII", d, co + 4)
	snap = struct.unpack_from("<40I", d, co + 0x40)
	last = last_fr_sample(d, base)
	if not last:
		print("no flight recorder in the dump, can't tell when the reset came")
		return None
	uptime, fc_last = last
	since_handoff = ((fc_last - fc_handoff) & 0xffffffff) / FC_HZ
	print("calib v%d, handoff fc %08x, last flight recorder sample fc %08x (uptime %.1f s)"
	      % (ver, fc_handoff, fc_last, uptime))
	print("lived %.1f s after the handoff" % since_handoff)
	if ver >= 2:
		state, trig, tick, period = struct.unpack_from("<IIII", d, co + 20)
		if state in (0, 0xffffffff):
			print("secure timer not armed at the handoff (state %x)" % state)
		else:
			due = struct.unpack("<i", struct.pack("<I", (trig - tick) & 0xffffffff))[0] * period / 1e6
			print("next secure request was due %.1f s after the handoff (timer state %x)" % (due, state))
			print("lived %.1f s after the secure request was due" % (since_handoff - due))
			up_handoff = tick * period / 1e6
			death = up_handoff + since_handoff
			print("symbian uptime: handoff %.1f s, death %.1f s -> inferred phase ~%.1f s (death - 240)"
			      % (up_handoff, death, death - 240))
			if ver >= 4:
				start = struct.unpack_from("<I", d, co + 44)[0] * period / 1e6
				print("symbian uptime: booter started %.1f s (%.1f s before the handoff)"
				      % (start, up_handoff - start))
			since_kick = None
			if ver >= 3:
				kick, fc_kick = struct.unpack_from("<iI", d, co + 36)
				if kick != 1:
					since_kick = ((fc_last - fc_kick) & 0xffffffff) / FC_HZ
					print("booter sent the request itself at the handoff -> %d" % kick)
					print("lived %.1f s after that request" % since_kick)
			return since_handoff, since_handoff - due, since_kick
	if not seen:
		print("no secure request was seen before the handoff, phase unknown")
		return since_handoff, None, None
	before = ((fc_handoff - fc_req) & 0xffffffff) / FC_HZ
	since_req = ((fc_last - fc_req) & 0xffffffff) / FC_HZ
	print("last secure request %.1f s before the handoff (cmd %08x PAid %08x)"
	      % (before, snap[0], snap[1]))
	print("lived %.1f s after the last secure request" % since_req)
	return since_handoff, since_req, None


def summary(path):
	rows = list(csv.reader(open(path)))
	h = [float(r[1]) for r in rows]
	q = [float(r[2]) for r in rows if r[2]]
	k = [float(r[3]) for r in rows if len(r) > 3 and r[3]]
	for r in rows:
		kick = r[3] if len(r) > 3 else ""
		print("%-16s after handoff %7.1f s   after due %s   after kick %s"
		      % (r[0], float(r[1]), ("%7.1f s" % float(r[2])) if r[2] else "      -",
			 ("%7.1f s" % float(kick)) if kick else "      -"))
	if len(h) > 1:
		print("\nafter handoff: mean %.1f s, stdev %.1f s" % (statistics.mean(h), statistics.stdev(h)))
	if len(q) > 1:
		print("after request: mean %.1f s, stdev %.1f s" % (statistics.mean(q), statistics.stdev(q)))
		print("a much smaller stdev after the request = the reset follows that request")
	if len(k) > 1:
		print("after kick: mean %.1f s, stdev %.1f s" % (statistics.mean(k), statistics.stdev(k)))
		print("a small stdev here = the request itself restarts the countdown")


def main():
	if len(sys.argv) > 2 and sys.argv[1] == "--summary":
		summary(sys.argv[2])
		return
	path = sys.argv[1] if len(sys.argv) > 1 else "logs/klog.bin"
	r = one(path)
	if r and len(sys.argv) > 3:
		with open(sys.argv[2], "a", newline="") as f:
			csv.writer(f).writerow([sys.argv[3], "%.1f" % r[0],
						"" if r[1] is None else "%.1f" % r[1],
						"" if r[2] is None else "%.1f" % r[2]])


if __name__ == "__main__":
	main()
