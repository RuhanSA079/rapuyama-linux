#!/usr/bin/env python3
"""
parses E:\\klog.bin (ramread.sis dump of phys 0x1f000000) = the RAM earlycon
buffer. header: u32 magic 'E6LG' (0x474c3645), u32 len, then the log bytes in a
ring of RY_RAMCON_SIZE (0xf0000). if len <= size: bytes [0..len). if it wrapped
(len > size): the ring is [len%size .. size) + [0 .. len%size).
"""
import struct
import sys

MAGIC = 0x474c3645  # 'E6LG'
SIZE = 0xf0000
HDR = 8


def main():
	path = sys.argv[1] if len(sys.argv) > 1 else "logs/klog.bin"
	d = open(path, "rb").read()
	if len(d) < HDR:
		print("file too short")
		return
	# scan for the 'E6LG' magic anywhere (region may land at an offset)
	off = -1
	for i in range(0, len(d) - HDR, 4):
		if struct.unpack_from("<I", d, i)[0] == MAGIC:
			off = i
			break
	if off < 0:
		print(f"no ramcon header ('E6LG' {MAGIC:#010x}) anywhere in the dump.")
		print("region probably did not survive the reset, or wrong address.")
		print("raw printable strings follow:\n")
		import re
		for m in re.finditer(rb"[\x20-\x7e]{6,}", d):
			print(m.group().decode("latin-1"))
		return
	length = struct.unpack_from("<I", d, off + 4)[0]
	data = d[off + HDR:off + HDR + SIZE]
	if length <= SIZE:
		log = data[:length]
	else:
		p = length % SIZE
		log = data[p:] + data[:p]
	sys.stdout.write(log.decode("latin-1", "replace"))
	print(f"\n\n[ramcon: {length} bytes logged{' (wrapped)' if length > SIZE else ''}]")


if __name__ == "__main__":
	main()