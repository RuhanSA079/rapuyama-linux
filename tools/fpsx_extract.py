#!/usr/bin/env python3
"""
unpacks a nokia bb5 .fpsx flash image into raw flash images

layout (reversed from the rm-609 core.fpsx):
0xB2, BE32 header_len, header TLVs ... then a sequence of blocks:
marker, 0x01, block_type, hdr_len, hdr[hdr_len], hdr_csum8, data[len]
marker 0x54 = plain data, 0x5d = signed (block_type 0x27: hdr starts with a
20-byte hash and a 12-byte name like "ADA"/"SWBL").
type 0x17 (hdr_len 0x0e): 00 03 00, checksum16, partition id, BE32 length, BE32 address
type 0x27/0x28: 0x70, hash20, name12, 00 03 00, checksum16, BE32 length, BE32 address[, extra]

writes one address-indexed image per (marker, type, partition): <out>/m<marker>_t<type>_p<part>.bin
"""
import collections
import struct
import sys
from pathlib import Path


def parse(data):
    if data[0] != 0xB2:
        raise ValueError("not an fpsx (first byte %#x)" % data[0])
    (hlen,) = struct.unpack_from(">I", data, 1)
    off = 5 + hlen
    while off < len(data):
        marker, sub, btype, blen = data[off: off + 4]
        hdr = data[off + 4: off + 4 + blen]
        p = off + 4 + blen + 1 # skip header checksum byte
        if btype == 0x17:
            i, part = 0, hdr[5]
            lpos = 6
        elif btype in (0x27, 0x28):
            # 0x70, 20-byte hash, 12-byte name, then the location record
            i, part = hdr.index(b"\x00\x03\x00", 21), 0
            lpos = i + 5
        else:
            raise ValueError("unknown block %02x/%02x/%02x len %#x at %#x: %s"
                             % (marker, sub, btype, blen, off, hdr.hex(" ")))
        if hdr[i:i + 3] != b"\x00\x03\x00":
            raise ValueError("no location record at %#x: %s" % (off, hdr.hex(" ")))
        (length, addr) = struct.unpack_from(">II", hdr, lpos)
        yield off, (marker, btype, part), addr, data[p: p + length]
        off = p + length


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    data = src.read_bytes()
    parts = collections.defaultdict(list)
    for off, part, addr, chunk in parse(data):
        parts[part].append((addr, chunk))
    for part, chunks in sorted(parts.items()):
        chunks.sort()
        end = max(a + len(c) for a, c in chunks)
        img = bytearray(b"\xff" * end)
        for a, c in chunks:
            img[a: a + len(c)] = c
        marker, btype, pid = part
        (out / f"m{marker:02x}_t{btype:02x}_p{pid:02x}.bin").write_bytes(img)
        print(f"marker {marker:#04x} type {btype:#04x} part {pid:#04x}: {len(chunks)} blocks, {chunks[0][0]:#x}..{end:#x} ({end / 2**20:.1f} MiB)")


if __name__ == "__main__":
    main()