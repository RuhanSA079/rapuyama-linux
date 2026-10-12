#!/usr/bin/env python3
"""
walks a symbian eka2 rom (inside a flash image) and lists / extracts files

usage:
rom_walk.py <flash.bin> <rom_file_base_hex> list
rom_walk.py <flash.bin> <rom_file_base_hex> extract <substr> <outdir>

the rom is mapped at virt 0x80000000, <rom_file_base_hex> is the file offset
that virt address lands at (0x13c1000 for the rm-609 core). structures are from
oss.FCL.sf.os.kernelhwsrv/kernel/eka/include/e32rom.h
"""
import struct
import sys
from pathlib import Path

ROM_V = 0x80000000


class Rom:
    def __init__(self, data, base_f):
        self.d = data
        self.base_f = base_f
        self.rom_size = self.u32(ROM_V + 0x90)

    def f(self, v):
        return v - ROM_V + self.base_f

    def u32(self, v):
        return struct.unpack_from("<I", self.d, self.f(v))[0]

    def image_header(self, v):
        o = self.f(v)
        (uid1, uid2, uid3, uidchk, entry, code_a, data_a, code_sz, text_sz,
         data_sz, bss_sz) = struct.unpack_from("<11I", self.d, o)
        return dict(uid1=uid1, uid3=uid3, entry=entry, code_a=code_a,
                    data_a=data_a, code_sz=code_sz, text_sz=text_sz,
                    data_sz=data_sz, bss_sz=bss_sz, hdr_v=v)

    def walk(self):
        """yields (path, entry_dict) for every file in the rom"""
        root_v = self.u32(ROM_V + 0x94) # iRomRootDirectoryList
        n = struct.unpack_from("<i", self.d, self.f(root_v))[0]
        dirs = []
        for i in range(n):
            # TRootDirInfo: iHardwareVariant(4), iAddressLin(4)
            hv, addr = struct.unpack_from("<II", self.d, self.f(root_v) + 4 + i * 8)
            dirs.append(addr)
        seen = set()
        for daddr in dirs:
            if daddr in seen:
                continue
            seen.add(daddr)
            yield from self._walk_dir(daddr, "")

    def _walk_dir(self, dir_v, prefix):
        o = self.f(dir_v)
        size = struct.unpack_from("<i", self.d, o)[0]
        p = o + 4
        end = p + size
        while p < end:
            esize, addr = struct.unpack_from("<II", self.d, p)
            att = self.d[p + 8]
            nl = self.d[p + 9]
            name = self.d[p + 10: p + 10 + 2 * nl].decode("utf-16le", "replace")
            if att & 0x10: # directory
                yield from self._walk_dir(addr, prefix + name + "\\")
            elif name not in (".", ".."):
                yield prefix + name, dict(size=esize, img_v=addr, att=att)
            step = 10 + 2 * nl
            p += (step + 3) & ~3


def main():
    data = Path(sys.argv[1]).read_bytes()
    rom = Rom(data, int(sys.argv[2], 16))
    cmd = sys.argv[3]
    files = list(rom.walk())
    if cmd == "list":
        for path, e in sorted(files):
            try:
                h = rom.image_header(e["img_v"])
                info = f"code={h['code_a']:#x} codesz={h['code_sz']:#x} uid3={h['uid3']:#x}"
            except Exception:
                info = ""
            print(f"{e['size']:>9} {e['img_v']:#010x}  {path}  {info}")
        print(f"\n{len(files)} files", file=sys.stderr)
    elif cmd == "extract":
        sub, outdir = sys.argv[4].lower(), Path(sys.argv[5])
        outdir.mkdir(parents=True, exist_ok=True)
        for path, e in files:
            if sub not in path.lower():
                continue
            h = rom.image_header(e["img_v"])
            # dump header + code + const data as a flat image based at img_v
            o = rom.f(e["img_v"])
            blob = data[o: o + e["size"]]
            name = path.split("\\")[-1]
            (outdir / name).write_bytes(blob)
            print(f"{path}: {e['size']} bytes -> {outdir/name}  "
                  f"(load base {e['img_v']:#x}, code at {h['code_a']:#x}, "
                  f"codesz {h['code_sz']:#x}, entry {h['entry']:#x})")


if __name__ == "__main__":
    main()