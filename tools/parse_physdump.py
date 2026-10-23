#!/usr/bin/env python3
"""
pulls the base64 blocks swddump (linux/initramfs/swddump.sh) printed out of a
picocom log and writes them as <name>.bin, checked against the sha256 the
phone computed.

usage: parse_physdump.py <picocom log> <out dir>
"""
import base64
import hashlib
import sys
from pathlib import Path


def main():
    log = Path(sys.argv[1]).read_text(errors='replace').replace('\r', '')
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    name, lines, ok = None, [], True
    for line in log.split('\n'):
        if line.startswith('=== physdump '):
            _, _, name, addr, length = line.split()[:5]
            lines = []
        elif line.startswith('=== sha256 ') and name:
            data = base64.b64decode(''.join(lines))
            want = line.split()[3]
            got = hashlib.sha256(data).hexdigest()
            (out / f'{name}.bin').write_bytes(data)
            good = got == want
            ok &= good
            print(f'{name}: {addr} {len(data):#x} bytes, sha256 {"ok" if good else "MISMATCH"}')
            name = None
        elif name and line and not line.startswith('==='):
            lines.append(line.strip())
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
