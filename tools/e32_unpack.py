#!/usr/bin/env python3
"""unpack a byte-pair compressed Symbian E32 image (rofs .rel files) into a
flat image: the header unchanged, then the code and the data sections
decompressed. each section starts with an index {u32 size of data, u32
decompressed size, u16 pages, u16 compressed page sizes[pages]} (Symbian
byte_pair.h IndexTableHeader), then the 4 kb pages
usage: e32_unpack.py <in.rel> <out.e32>
"""
import struct
import sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import bytepair


def section(d, off):
	size, usize, n = struct.unpack_from("<IIH", d, off)
	sizes = struct.unpack_from("<%dH" % n, d, off + 10)
	p = off + 10 + 2 * n
	out = b""
	for sz in sizes:
		blk, _ = bytepair.decompress_block(d, p, min(4096, usize - len(out)))
		if blk is None:
			raise ValueError("bad page at %#x" % p)
		out += blk
		p += sz
	return out, off + size


def main():
	d = open(sys.argv[1], "rb").read()
	if d[0x10:0x14] != b"EPOC":
		sys.exit("not an E32 image")
	comp = struct.unpack_from("<I", d, 0x1c)[0]
	hdr = struct.unpack_from("<I", d, 0x64)[0]	# iCodeOffset = header size
	if comp != 0x102822aa:
		sys.exit("not byte-pair compressed (%#x)" % comp)
	code, nxt = section(d, hdr)
	data = b""
	if nxt < len(d):
		data, nxt = section(d, nxt)
	open(sys.argv[2], "wb").write(d[:hdr] + code + data)
	print("code %#x data %#x" % (len(code), len(data)))


if __name__ == "__main__":
	main()