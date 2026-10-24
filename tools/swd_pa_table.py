#!/usr/bin/env python3
"""
Prints the N8 ISA software's protected-application (PA) table from a
swdvadump (linux/initramfs/swdvadump.sh), the way FUN_c6361f0c walks it:

  table = *( *DAT_c6361fa4 + 4 )
  entries of 32 bytes: +0x00 offset (u32), +0x14 name (zero terminated)
  the table ends at an entry whose first name byte is 0xff

and says whether "PA_SWD" is in it. Read only, works on the dump files.

usage: python3 -I tools/swd_pa_table.py work/swdvadump
"""
import struct
import sys
from pathlib import Path

REGIONS = {  # dump file: base VA
    'va_code.bin': 0xc6300000,
    'va_guest.bin': 0xc6800000,
    'va_pool.bin': 0xc6a80000,
    'va_pa.bin': 0x90440000,  # the PA table (*(c68189c4 + 4) = 90443000)
}
LITERAL = 0xc6361fa4  # DAT_c6361fa4 in FUN_c6361f0c
ENTRY = 32
NAME_OFF = 0x14
MAX_ENTRIES = 128


def load(d):
    return {REGIONS[n]: (d / n).read_bytes() for n in REGIONS if (d / n).exists()}


def word(mem, va):
    for base, data in mem.items():
        if base <= va and va + 4 <= base + len(data):
            return struct.unpack_from('<I', data, va - base)[0]
    return None


def raw(mem, va, n):
    for base, data in mem.items():
        if base <= va and va + n <= base + len(data):
            return data[va - base:va - base + n]
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    mem = load(Path(sys.argv[1]))

    lit = word(mem, LITERAL)
    print(f'DAT_{LITERAL:08x} = {lit:08x}' if lit is not None else
          f'DAT_{LITERAL:08x}: not in the dump')
    if lit is None:
        return
    ptr = word(mem, lit + 4)
    print(f'*({lit:08x} + 4) = table at {ptr:08x}' if ptr is not None else
          f'{lit + 4:08x}: not in the dump (outside the dumped regions)')
    if ptr is None:
        return

    found = False
    for i in range(MAX_ENTRIES):
        e = raw(mem, ptr + i * ENTRY, ENTRY)
        if e is None:
            print(f'entry {i}: {ptr + i * ENTRY:08x} not in the dump, stopping')
            break
        if e[NAME_OFF] == 0xff:
            print(f'entry {i}: end marker (name byte 0xff)')
            break
        name = e[NAME_OFF:].split(b'\0')[0].decode('ascii', 'replace')
        off = struct.unpack_from('<I', e, 0)[0]
        print(f'entry {i:3}: {ptr + i * ENTRY:08x}  offset {off:08x}  name {name!r}'
              f'  raw {e.hex()}')
        found |= name == 'PA_SWD'
    print('PA_SWD:', 'FOUND' if found else 'not found')


if __name__ == '__main__':
    main()
