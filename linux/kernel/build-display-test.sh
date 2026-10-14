#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
KSRC="$HERE/linux"
KOUT="$HERE/build-display"

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnu-

mkdir -p "$KOUT"
cp "$HERE/e6.config" "$KOUT/.config"
arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -fno-pie -no-pie \
	-fomit-frame-pointer -fno-stack-protector -Os -Wall -Wextra -Werror \
	-Wl,-e,_start -o "$KOUT/fbtest" "$HERE/../initramfs/fbtest.c" -lgcc
cp "$HERE/../initramfs/initramfs.list" "$KOUT/initramfs.list"
printf 'dir /usr 755 0 0\ndir /usr/bin 755 0 0\nfile /usr/bin/fbtest %s 755 0 0\n' \
	"$KOUT/fbtest" >> "$KOUT/initramfs.list"
"$KSRC/scripts/config" --file "$KOUT/.config" --set-val PHYS_OFFSET 0x10880000
"$KSRC/scripts/config" --file "$KOUT/.config" --enable FB_RAPUYAMA
"$KSRC/scripts/config" --file "$KOUT/.config" --set-str INITRAMFS_SOURCE "$KOUT/initramfs.list"

make -C "$KSRC" O="$KOUT" olddefconfig
make -C "$KSRC" O="$KOUT" -j"$(nproc)" zImage
make -C "$KSRC" O="$KOUT" scripts_dtc

cp "$HERE/../boot/e6-usb.dtb" "$KOUT/e6-usb.dtb"
"$KOUT/scripts/dtc/dtc" -I dts -O dtb -i "$HERE/../boot" \
	-o "$KOUT/e6-display-test.dtb" "$HERE/../boot/e6-display-test.dts"

echo
echo "Framebuffer-preserving zImage: $KOUT/arch/arm/boot/zImage"
echo "USB shell DTB: $KOUT/e6-usb.dtb"
echo "Framebuffer test DTB: $KOUT/e6-display-test.dtb"
ls -lh "$KOUT/arch/arm/boot/zImage"
