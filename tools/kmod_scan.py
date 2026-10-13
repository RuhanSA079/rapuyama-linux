#!/usr/bin/env python3
"""
finds which rom modules touch which peripheral page

scans every rom image for aligned words in the 0xc6000000 peripheral window
(the real symbian virt window, NOT the 0xe0xxxxxx stuff - that was arm opcodes)
and maps them to phys with the live c6 window from logs/map (3).bin

usage: kmod_scan.py <core.bin> [module_substr]
"""
import struct
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from rom_walk import Rom

# live c6 window (logs/map (3).bin) virt, phys, pages
WIN = [
	(0xc6010000, 0x09000000, 1), (0xc6011000, 0x08000000, 1),
	(0xc6012000, 0x20010000, 1), (0xc6018000, 0x30000000, 4),
	(0xc6020000, 0x20020000, 48), (0xc6050000, 0x40000000, 1),
	(0xc6060000, 0x20400000, 4), (0xc6070000, 0xfffd0000, 16),
	(0xc60a0000, 0x20600000, 1), (0xc60a1000, 0x20700000, 1),
	(0xc60af000, 0x20680000, 1), (0xc60f0000, 0x11525000, 2),
	(0xc6f00000, 0x40100000, 256),
]


def v2p(v):
	for b, p, n in WIN:
		if b<=v<b+n*0x1000:
			return p+v-b
	return None


def main():
	data = Path(sys.argv[1]).read_bytes()
	want = sys.argv[2].lower() if len(sys.argv)>2 else None
	rom = Rom(data, 0x13c1000)
	pages = defaultdict(lambda: defaultdict(set)) # page -> module -> regs
	for path, e in sorted(rom.walk()):
		name = path.split("\\")[-1]
		if want and want not in name.lower():
			continue
		try:
			h = rom.image_header(e["img_v"])
		except Exception:
			continue
		# only real unpaged rom images (code right after the 0x78 header)
		if h["code_a"]!=e["img_v"]+0x78 or not 0<h["code_sz"]<0x400000:
			continue
		o = rom.f(e["img_v"])
		blob = data[o: o+e["size"]]
		for i in range(0, len(blob)-3, 4):
			w = struct.unpack_from("<I", blob, i)[0]
			if 0xc6000000<=w<0xc7000000:
				pages[w&~0xfff][name].add(w&0xfff)
	for pg in sorted(pages):
		p = v2p(pg)
		ps = f"{p:08x}" if p is not None else "unmapped"
		print(f"{pg:08x} -> {ps}")
		for mod, regs in sorted(pages[pg].items(), key=lambda x: -len(x[1])):
			rs = " ".join(f"{r:03x}" for r in sorted(regs)[:24])
			more = " ..." if len(regs)>24 else ""
			print(f"    {mod:<28} {len(regs):>3}: {rs}{more}")


if __name__=="__main__":
	main()