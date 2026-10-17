#!/usr/bin/env python3
"""minimal Symbian rofs extractor (format from
symbian/.../userlibandfileserver/fileserver/inc/rofs.h). walks the file-entry
block of a rofs image embedded in a flash dump and pulls out files by name
substring. File payloads are E32Images (often byte-pair compressed); use
bytepair.py to unpack them afterwards.

usage: rofs_extract.py <flash.bin> <rofs_file_offset_hex> <name_substr> <outdir>
"""
import struct
import sys
from pathlib import Path


def u8(d, o):
    return d[o]


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def main():
    flash = Path(sys.argv[1]).read_bytes()
    base = int(sys.argv[2], 16)
    substr = sys.argv[3].lower()
    outdir = Path(sys.argv[4])
    outdir.mkdir(parents=True, exist_ok=True)

    # TRofsHeader
    ident = flash[base:base + 4]
    hdr_size = u8(flash, base + 4)
    ver = u16(flash, base + 6)
    dir_tree_off = u32(flash, base + 8)
    dir_tree_sz = u32(flash, base + 12)
    fe_off = u32(flash, base + 16)
    fe_sz = u32(flash, base + 20)
    img_size = u32(flash, base + 24)
    print("ROFS id=%r hdr=%#x ver=%#x dirTree@%#x(%#x) fileEntries@%#x(%#x) img=%#x"
          % (ident, hdr_size, ver, dir_tree_off, dir_tree_sz, fe_off, fe_sz, img_size),
          file=sys.stderr)

    # the file-entry block is a flat list of TRofsEntry. Offsets in the header are
    # relative to the ROFS image start (== base in the flash file).
    p = base + fe_off
    end = p + fe_sz
    found = 0
    while p + 4 <= end:
        struct_size = u16(flash, p)
        if struct_size < 0x1e or p + struct_size > end:
            p += 4
            continue
        name_off = u8(flash, p + 0x12)
        att = u8(flash, p + 0x13)
        file_size = u32(flash, p + 0x14)
        file_addr = u32(flash, p + 0x18)
        name_len = u8(flash, p + 0x1d)
        try:
            name = flash[p + name_off: p + name_off + 2 * name_len].decode("utf-16le", "replace")
        except Exception:
            name = ""
        if name and substr in name.lower():
            print("  %-40s size=%#x addr=%#x att=%#x" % (name, file_size, file_addr, att),
                  file=sys.stderr)
            # file_addr is a linear address into the ROFS image; try both
            # interpretations (relative to ROFS base, and relative to flash).
            for label, fo in (("rel", base + file_addr), ("flashabs", file_addr)):
                if 0 <= fo < len(flash) - 16:
                    blob = flash[fo: fo + min(file_size + 0x400, len(flash) - fo)]
                    out = outdir / (name.split("\\")[-1] + "." + label)
                    out.write_bytes(blob[:file_size + 0x200])
                    print("    [%s] -> %s  head=%s" % (label, out.name, blob[:16].hex()),
                          file=sys.stderr)
            found += 1
        p += (struct_size + 3) & ~3
    print("done, %d match(es)" % found, file=sys.stderr)


if __name__ == "__main__":
    main()