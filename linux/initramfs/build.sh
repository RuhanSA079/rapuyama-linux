#!/usr/bin/env bash
# builds the tiny static /init + the initramfs list the kernel config points at
# (CONFIG_INITRAMFS_SOURCE = linux/initramfs/initramfs.list)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -o "$HERE/init" "$HERE/init.S"
cat > "$HERE/initramfs.list" <<LIST
dir /dev 755 0 0
nod /dev/console 600 0 0 c 5 1
file /init $HERE/init 755 0 0
LIST
ls -la "$HERE/init"