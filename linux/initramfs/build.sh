#!/usr/bin/env bash
# builds the initramfs list the kernel config points at
# (CONFIG_INITRAMFS_SOURCE = linux/initramfs/initramfs.list):
# busybox (work/dl/busybox-armv5l, official static musl build, armv5t +
# vfpv2 hard float, runs on the arm1176) + init.sh as /init.
# init.S (pause loop / 30 s power off in asm) is the no-busybox fallback:
# ./build.sh asm
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BB="$HERE/../../work/dl/busybox-armv5l"
if [ "$1" = asm ] || [ ! -f "$BB" ]; then
	arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -o "$HERE/init" "$HERE/init.S"
	cat > "$HERE/initramfs.list" <<LIST
dir /dev 755 0 0
nod /dev/console 600 0 0 c 5 1
file /init $HERE/init 755 0 0
LIST
	echo "asm init"
	exit 0
fi
arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -o "$HERE/init" "$HERE/init.S"
cat > "$HERE/initramfs.list" <<LIST
dir /dev 755 0 0
nod /dev/console 600 0 0 c 5 1
dir /bin 755 0 0
dir /sbin 755 0 0
dir /proc 755 0 0
dir /sys 755 0 0
dir /tmp 1777 0 0
file /bin/busybox $(realpath "$BB") 755 0 0
slink /bin/sh busybox 777 0 0
file /init $HERE/init.sh 755 0 0
file /sbin/init $HERE/init 755 0 0
LIST
echo "busybox init + asm /sbin/init fallback"