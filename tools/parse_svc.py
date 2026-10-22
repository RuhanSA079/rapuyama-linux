#!/usr/bin/env python3
"""
E:\\svc.bin from e6calb (RRunBlob::WatchService): every symbian thread's cpu
time in 200 ms slots from 6 s before to 3 s after the expected ~210 s reset
service (symbian tick 210000*k - 900 ms).

prints every thread that ran, one char per slot (. idle, 1-9 share of the
slot, # more), the service slot marked with ^ underneath, and lists the
threads that ran around the service but stayed quiet the rest of the window.

usage: parse_svc.py logs/svc.bin
"""
import struct
import sys

MAGIC = 0x31435653	# 'SVC1'
HDR = 9
NAME = 12
NEAR_MS = 600	# what counts as "at the service"


def main():
	path = sys.argv[1] if len(sys.argv) > 1 else "logs/svc.bin"
	d = open(path, "rb").read()
	h = struct.unpack_from("<%dI" % HDR, d, 0)
	if h[0] != MAGIC:
		print("not an svc.bin (magic %08x)" % h[0])
		return
	target, slot0, nslots, slot_ms, freq, seen, written, after = h[1:]
	print("service expected at tick %u ms, window starts %u ms (%+d ms), %u slots of %u ms"
	      % (target, slot0, slot0 - target, nslots, slot_ms))
	print("cpu time freq %u Hz, %u threads pinned, %u ran, %u threads after the window"
	      % (freq, seen, written, after))
	if not freq:
		freq = 1
	slot_ticks = freq * slot_ms / 1000.0
	# slot s covers [slot0 + s*slot_ms, slot0 + (s+1)*slot_ms)
	near = [abs(slot0 + s * slot_ms + slot_ms / 2 - target) <= NEAR_MS for s in range(nslots)]
	mark = "".join("^" if n else " " for n in near)
	o = HDR * 4
	rows = []
	for _ in range(written):
		name = d[o:o + NAME * 4].rstrip(b"\0").decode("latin-1")
		o += NAME * 4
		v = struct.unpack_from("<%dI" % nslots, d, o)
		o += nslots * 4
		rows.append((name, v))
	if not rows:
		print("no thread ran: cpu time accounting is off on this rom?")
		return
	width = max(len(n) for n, _ in rows)
	cands = []
	for name, v in sorted(rows, key=lambda r: -sum(r[1])):
		line = ""
		for x in v:
			f = x / slot_ticks
			line += "." if x == 0 else ("#" if f >= 0.95 else str(max(1, min(9, int(f * 10)))))
		print("%-*s %s %8.1f ms" % (width, name, line, sum(v) * 1000.0 / freq))
		at = sum(x for x, n in zip(v, near) if n)
		rest = sum(x for x, n in zip(v, near) if not n)
		if at and not rest:
			cands.append((name, at * 1000.0 / freq))
	print("%-*s %s" % (width, "", mark))
	print("\nran only around the service:")
	for name, ms in cands:
		print("  %s  (%.2f ms)" % (name, ms))
	if not cands:
		print("  none")


if __name__ == "__main__":
	main()