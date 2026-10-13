#!/usr/bin/env python3
"""
parses E:\\timerprobe.bin (timerprobe.sis, runblob EControlTimerProbe)
checks the timers reversed from the firmware (docs/driver-re.md):
- fast counter 0x20033350 should do ~32768/s
- ms timer 0x200337a4 should count down inside 0..reload, reload 0x9600
"""
import struct
import sys


def main():
	path = sys.argv[1] if len(sys.argv)>1 else "logs/timerprobe.bin"
	w = struct.unpack("<41I", open(path, "rb").read()[:41*4])
	if w[0]!=0x7e570001:
		print(f"bad magic {w[0]:08x}")
		return
	t0, fc0, c0, r0 = w[1:5]
	t1, fc1, c1, r1 = w[37:41]
	dt = (t1-t0)&0xffffffff
	dfc = (fc1-fc0)&0xffffffff
	print(f"before: tick={t0} fc={fc0:08x} mst cnt={c0:#x} reload={r0:#x}")
	print(f"after:  tick={t1} fc={fc1:08x} mst cnt={c1:#x} reload={r1:#x}")
	print(f"\nsleep: {dt} ticks, fast counter +{dfc}")
	if dt:
		hz = dfc*1000/dt
		print(f"-> fast counter ~{hz:.0f} Hz (expect 32768) {'OK' if abs(hz-32768)<1500 else 'MISMATCH'}")
	# 0x200337a8 is write only, reads 0 (13.10.2026 run), the wrap in the samples shows the reload
	print(f"reload reg reads {r0:#x}/{r1:#x} (write only, 0 is normal)")

	print("\n100us samples: fc, mst cnt (expect cnt going DOWN ~3840 per sample, wrapping at reload)")
	prev = None
	for i in range(16):
		fc, cnt = w[5+i*2], w[6+i*2]
		d = "" if prev is None else f"  dcnt={(prev-cnt)%0x9600:6d}"
		print(f"  {i:2d}: fc={fc:08x} cnt={cnt:#07x}{d}")
		prev = cnt
	inrange = all(w[6+i*2]<=0x9600 for i in range(16))
	print(f"\ncnt always <= reload: {inrange}")


if __name__=="__main__":
	main()