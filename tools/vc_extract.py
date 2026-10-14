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


def main():
    rom = Path(sys.argv[1]).read_bytes()
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    if rom[0x22d5a0:0x22d5a9] != b"bcm2727b1":
        sys.exit("not the expected E6 kern.bin (no bcm2727b1 tag at 0x8022d5a0)")
    for name, (ptr, size, trl) in IMAGES.items():
        (out / f"{name}.bin").write_bytes(rom[ptr - BASE:ptr - BASE + size])
        (out / f"{name}.trl").write_bytes(rom[trl - BASE:trl - BASE + 20])
        print(f"{name}: {size:#x} bytes, trailer {rom[trl - BASE:trl - BASE + 20].hex()}")


if __name__ == "__main__":
    main()
