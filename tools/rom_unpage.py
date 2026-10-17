#!/usr/bin/env python3
"""decompress a demand-paged Symbian rom region and extract files by virtual
address range. The rom page index (SRomPageInfo[page] = {u32 iDataStart; u16
iDataSize; u8 iCompressionType; u8 iPagingAttributes}) maps each 4K virtual page
to its data: attr&1 (pageable) + comp==1 => byte-pair compressed at base_f+
iDataStart for iDataSize bytes; otherwise raw. Page index located by scanning.

usage: rom_unpage.py <flash.bin> <virt_hex> <len_hex> <out> (dump a range)
   or: rom_unpage.py <flash.bin> page_index (print index info)
"""
import struct
import sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import bytepair

ROM_V = 0x80000000
BASE_F = 0x13c1000     # file offset of virt 0x80000000 (rm-609 APE ROM)
TBL = 0x13c3d80        # SRomPageInfo table file offset (found by scan)


def page_info(d, page):
    return struct.unpack_from("<IHBB", d, TBL + page * 8)


def page_bytes(d, page):
    ds, sz, comp, attr = page_info(d, page)
    src = BASE_F + ds
    if (attr & 1) and comp == 1:
        blk, _ = bytepair.decompress_block(d, src, 0x1000)
        if blk is None:
            raise ValueError("bytepair fail page %#x" % page)
        return blk.ljust(0x1000, b"\0")
    return d[src:src + 0x1000]


def read_virt(d, v, n):
    out = bytearray()
    while len(out) < n:
        cur = v + len(out)
        page = (cur - ROM_V) >> 12      # page index is relative to ROM virt base
        off = cur & 0xfff
        pg = page_bytes(d, page)
        take = min(0x1000 - off, n - len(out))
        out += pg[off:off + take]
    return bytes(out)


if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    if sys.argv[2] == "page_index":
        print("TBL=%#x BASE_F=%#x" % (TBL, BASE_F))
        for pg in (0, 1, 0x22b, 0xcbf, 0x1000, 0x1200):
            print("  page %#06x: %s" % (pg, page_info(d, pg)))
        sys.exit(0)
    v = int(sys.argv[2], 16)
    n = int(sys.argv[3], 16)
    out = read_virt(d, v, n)
    open(sys.argv[4], "wb").write(out)
    sys.stderr.write("wrote %#x bytes from virt %#x to %s\n" % (len(out), v, sys.argv[4]))