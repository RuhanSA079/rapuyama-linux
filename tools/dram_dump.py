#!/usr/bin/env python3
"""Pull the dram linux leaves alone (below PHYS_OFFSET 0x11600000) off the
phone through the linux usb shell (ttyGS0 -> /dev/ttyACM0). the decrypted
modem sw (SOS*ISASW @0x10480000) only exists there, so this is how we get it.

boot out/zImage + out/e6-vc.dtb (has e6.usbsh), plug usb, run this. it saves
every 256 kb chunk as soon as it arrives and skips chunks it already has, so
if the phone resets mid-way just boot again and rerun.

usage: dram_dump.py [/dev/ttyACM0] [outdir]
"""
import base64
import binascii
import hashlib
import os
import select
import subprocess
import sys
import termios
import time
import tty

CHUNK = 0x40000
# most useful first: isasw, nloader blobs, symbian kernel data + L2 tables, dsp
REGIONS = [
    (0x10480000, 0x10780000),   # SOS*ISASW (cellular mcu sw, arm code)
    (0x108c0000, 0x10900000),   # PRIMAPP/NPC/CCC/HWC/... small nloader blobs
    (0x10000000, 0x10200000),   # symbian kernel data, L2s for c60/c67/c68/c80
    (0x11500000, 0x11600000),   # L2s for c62..c6c, c81, f0d..f0f
    (0x10200000, 0x10480000),   # SOS*DSP0 (cdsp)
    (0x10780000, 0x108c0000),
    (0x10900000, 0x11500000),   # rom shadow, we have the rom already
    (0x1f8b0000, 0x1fd00000),   # modem sw ram (c6800000): scheduler + timer state
]


def wait_for(fd, pat, tmo):
    buf = b""
    end = time.time() + tmo
    while pat not in buf:
        left = end - time.time()
        if left <= 0:
            raise TimeoutError("no %r, got %r" % (pat, buf[-200:]))
        r, _, _ = select.select([fd], [], [], left)
        if r:
            buf += os.read(fd, 65536)
    return buf


def cmd(fd, s):
    os.write(fd, s.encode() + b"\r")


def main():
    dev = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
    out = sys.argv[2] if len(sys.argv) > 2 else "logs/dram"
    os.makedirs(out, exist_ok=True)
    while not os.path.exists(dev):
        print("waiting for", dev)
        time.sleep(1)
    time.sleep(1)
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    termios.tcflush(fd, termios.TCIOFLUSH)
    # e6sh eats lines until an empty one, a running sh just ignores it.
    # $((..)) so the reply marker never matches the echoed command itself
    cmd(fd, "")
    cmd(fd, "")
    time.sleep(0.5)
    cmd(fd, "stty -echo; echo @@$((1+1))R")
    wait_for(fd, b"@@2R", 15)
    print("shell up")
    # busybox dd reads /dev/mem with read(), which arm refuses below PHYS_OFFSET.
    # tools/memdump mmaps instead, send it over as base64
    md = os.path.join(out, "memdump")
    subprocess.run(["arm-linux-gnu-gcc", "-march=armv6k", "-marm", "-static", "-nostdlib", "-Os",
                    "-o", md, os.path.join(os.path.dirname(os.path.abspath(__file__)), "memdump.c")], check=True)
    blob = open(md, "rb").read()
    cmd(fd, "base64 -d >/tmp/md <<'@@X'")
    b = base64.b64encode(blob).decode()
    for i in range(0, len(b), 76):
        cmd(fd, b[i:i + 76])
    cmd(fd, "@@X")
    cmd(fd, "chmod 755 /tmp/md; md5sum /tmp/md; echo @@$((1+2))U")
    r = wait_for(fd, b"@@3U", 15)
    if hashlib.md5(blob).hexdigest().encode() not in r:
        raise SystemExit("memdump upload broken: %r" % r[-200:])
    print("memdump uploaded")
    t0 = time.time()
    got = 0
    for lo, hi in REGIONS:
        for a in range(lo, hi, CHUNK):
            fn = os.path.join(out, "%08x.bin" % a)
            if os.path.exists(fn) and os.path.getsize(fn) == CHUNK:
                continue
            cmd(fd, "echo @@$((0))S; /tmp/md %x %x | base64; echo @@$((0))E" % (a, CHUNK))
            buf = wait_for(fd, b"@@0E", 60)
            s = buf.find(b"@@0S")
            txt = buf[s + 4:buf.rfind(b"@@0E")]
            try:
                data = base64.b64decode(txt)
            except binascii.Error:
                data = b""
            if len(data) != CHUNK:
                print(txt[:200])
                # /dev/mem said no (strict devmem) or the mmap failed
                print("%08x: got %d bytes, skipping" % (a, len(data)))
                continue
            with open(fn + ".tmp", "wb") as f:
                f.write(data)
            os.rename(fn + ".tmp", fn)
            got += CHUNK
            print("%08x ok  %.1f s  %d kb/s" % (a, time.time() - t0, got / 1024 / (time.time() - t0)))
    print("done, chunks in", out)


if __name__ == "__main__":
    main()