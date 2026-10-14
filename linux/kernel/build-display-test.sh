#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
KSRC="$HERE/linux"
KOUT="$HERE/build-display"
# must stay above the fb (0x10480000..0x10880000) AND be 2 MiB aligned:
# with ARM_PATCH_PHYS_VIRT, __fixup_pv_table in head.S deadloops on a
# misaligned PHYS_OFFSET before the mmu/earlycon are up -> silent wdt reset.
DISPLAY_PHYS_OFFSET=0x10a00000
if (( DISPLAY_PHYS_OFFSET & 0x1fffff )); then
	echo "PHYS_OFFSET $DISPLAY_PHYS_OFFSET is not 2 MiB aligned" >&2
	exit 1
fi
# the E6 rom dump, the VideoCore images get pulled out of it
KERN_BIN="${KERN_BIN:?set KERN_BIN=/path/to/kern.bin}"

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnu-

mkdir -p "$KOUT"
cp "$HERE/e6.config" "$KOUT/.config"
arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -fno-pie -no-pie \
	-fomit-frame-pointer -fno-stack-protector -Os -Wall -Wextra -Werror \
	-Wl,-e,_start -o "$KOUT/fbtest" "$HERE/../initramfs/fbtest.c" -lgcc
arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -fno-pie -no-pie \
	-ffreestanding -fno-builtin -fomit-frame-pointer -fno-stack-protector \
	-Os -Wall -Wextra \
	-Wl,-e,_start -o "$KOUT/vcprobe" "$HERE/../initramfs/vcprobe.c" -lgcc
python3 "$HERE/../../tools/vc_extract.py" "$KERN_BIN" "$KOUT/vcfw"

cp "$HERE/../initramfs/initramfs.list" "$KOUT/initramfs.list"
{
	printf 'dir /usr 755 0 0\ndir /usr/bin 755 0 0\n'
	printf 'file /usr/bin/fbtest %s 755 0 0\n' "$KOUT/fbtest"
	printf 'file /usr/bin/vcprobe %s 755 0 0\n' "$KOUT/vcprobe"
	printf 'dir /lib 755 0 0\ndir /lib/firmware 755 0 0\ndir /lib/firmware/vc 755 0 0\n'
	for f in vc-loader-a vc-loader-b vc-main; do
		printf 'file /lib/firmware/vc/%s.bin %s 644 0 0\n' "$f" "$KOUT/vcfw/$f.bin"
		printf 'file /lib/firmware/vc/%s.trl %s 644 0 0\n' "$f" "$KOUT/vcfw/$f.trl"
	done
} >> "$KOUT/initramfs.list"
"$KSRC/scripts/config" --file "$KOUT/.config" --set-val PHYS_OFFSET "$DISPLAY_PHYS_OFFSET"
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
