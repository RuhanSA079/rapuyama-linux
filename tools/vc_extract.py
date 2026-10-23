#!/usr/bin/env python3
"""
pulls the BCM2727B1 boot images out of the E6 kern.bin (unsigned_2727b1.dll,
registered by its entry point 0x8022d4b4 as a vcexe image set):

  set +0x04  loader A   0x8022d5e8, 0x2038 bytes, 20-byte trailer 0x8022f624
  set +0x10  loader B   0x8022f638, 0x2038 bytes, 20-byte trailer 0x80231674
  set +0x1c  main image 0x80231688, 0xf6aab bytes, 20-byte trailer 0x80328137

DVCDriver::Boot sends loader B when called with variant 1 (the boot DFC and
the normal power-up path), loader A otherwise. the trailer goes into the
32-byte header message that follows each image.

the n8 (RM-596, anna 024.001, rom dumped on the phone) has no
unsigned_2727b1.dll: the images sit in vcdriver.dll, found through the
{size word, image} pairs in its literal pool at 0x801f9218. each image there
ends in "sign" + hash, the part before "sign" is what gets sent and the 20
bytes after it are the trailer, like the e6 layout. which of the two loaders
is A or B is a guess (memory order), videocore.loader_b=1 swaps them.

usage: vc_extract.py kern.bin outdir
"""
import sys
from pathlib import Path

BASE = 0x80000000
IMAGES = {
    "vc-loader-a": (0x8022d5e8, 0x2038, 0x8022f624),
    "vc-loader-b": (0x8022f638, 0x2038, 0x80231674),
    "vc-main": (0x80231688, 0xf6aab, 0x80328137),
}
# n8: image start, address of its size word (the size includes "sign" + hash)
N8_IMAGES = {
    "vc-loader-a": (0x802ee9c0, 0x802f0a30),
    "vc-loader-b": (0x802f0a40, 0x802f2ab0),
    "vc-main": (0x801fc020, 0x802ee9b0),
}


def n8_images(rom):
    """(ptr, size, trailer) like IMAGES, or None if this isn't that n8 rom"""
    if b"vcdriver.dll" not in rom[0x1f4000:0x1f5000] and rom.find(b"DVCDriver") < 0:
        return None
    out = {}
    for name, (ptr, szp) in N8_IMAGES.items():
        total = int.from_bytes(rom[szp - BASE:szp - BASE + 4], "little")
        img = rom[ptr - BASE:ptr - BASE + total]
        sign = img.rfind(b"sign")
        if not 0x1000 <= total < 0x200000 or sign < 0 or sign + 24 > total:
            return None
        out[name] = (ptr, sign, ptr + sign + 4)
    return out


def main():
    rom = Path(sys.argv[1]).read_bytes()
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    images = IMAGES
    if rom[0x22d5a0:0x22d5a9] != b"bcm2727b1":
        images = n8_images(rom)
        if not images:
            sys.exit("neither the E6 kern.bin (bcm2727b1 tag at 0x8022d5a0) nor the N8 024.001 one")
        print("n8 rom layout")
    for name, (ptr, size, trl) in images.items():
        (out / f"{name}.bin").write_bytes(rom[ptr - BASE:ptr - BASE + size])
        (out / f"{name}.trl").write_bytes(rom[trl - BASE:trl - BASE + 20])
        print(f"{name}: {size:#x} bytes, trailer {rom[trl - BASE:trl - BASE + 20].hex()}")


if __name__ == "__main__":
    main()
