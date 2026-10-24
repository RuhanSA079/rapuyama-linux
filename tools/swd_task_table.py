#!/usr/bin/env python3
"""
Prints the N8 ISA OS task table from a swdvadump (va_guest.bin), read only:
0x30 entries of 0x18 bytes at VA 0xc681ae44 (work/swd-n8/NOTES.md section 9).
Per entry: the six raw words, the task id (+0xc), and whether the word at +8
points into the dumped guest region (0xc6800000 +0x280000); if so, the eight
words it points at. Also prints semaphore 0x1e of the table at 0xc681b2dc.

usage: python3 -I tools/swd_task_table.py work/swdvadump
"""
import struct
import sys
from pathlib import Path

GUEST = 0xc6800000
TASKS = 0xc681ae44
N_TASKS = 0x30
TASK_SIZE = 0x18
SEMS = 0xc681b2dc


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    g = (Path(sys.argv[1]) / 'va_guest.bin').read_bytes()

    def words(va, n):
        o = va - GUEST
        if o < 0 or o + 4 * n > len(g):
            return None
        return struct.unpack_from(f'<{n}I', g, o)

    print(f'task table at {TASKS:08x}, {N_TASKS} entries of {TASK_SIZE:#x} bytes')
    for i in range(N_TASKS):
        va = TASKS + i * TASK_SIZE
        w = words(va, TASK_SIZE // 4)
        if w is None:
            print(f'{i:2}: {va:08x} not in the dump')
            continue
        line = f'{i:2}: {va:08x}  ' + ' '.join(f'{x:08x}' for x in w)
        line += f'  id(+0xc)={w[3]:#x}'
        p = w[2]
        tgt = words(p, 8) if p else None
        if tgt is not None:
            line += f'\n      +8 -> {p:08x}: ' + ' '.join(f'{x:08x}' for x in tgt)
        print(line)

    s = words(SEMS, 0x20 // 4 * 0x1f)
    if s is not None:
        o = SEMS - GUEST
        raw = g[o:o + 0x200]
        print(f'\nsemaphore table at {SEMS:08x}, first 0x200 bytes:')
        for r in range(0, len(raw), 16):
            print(f'  {SEMS + r:08x}: {raw[r:r + 16].hex(" ")}')


if __name__ == '__main__':
    main()
