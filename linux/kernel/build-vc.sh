#!/usr/bin/env bash
# kernel + dtb with the VideoCore display driver (CONFIG_SOC_RAPUYAMA_VIDEOCORE)
# usage: KERN_BIN=/path/to/kern.bin ./build-vc.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
KSRC="$HERE/linux"
KOUT="$HERE/build-vc"
# the E6 rom dump, the VideoCore images get pulled out of it
KERN_BIN="${KERN_BIN:?set KERN_BIN=/path/to/kern.bin}"

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnu-

mkdir -p "$KOUT"
cp "$HERE/e6.config" "$KOUT/.config"
arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -fno-pie -no-pie \
	-fomit-frame-pointer -fno-stack-protector -Os -Wall -Wextra -Werror \
	-Wl,-e,_start -o "$KOUT/fbtest" "$HERE/../initramfs/fbtest.c" -lgcc
# userspace /dev/mem probe for the VC bus and DSPC (docs/porting-guide.md)
arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -fno-pie -no-pie \
	-ffreestanding -fno-builtin -fomit-frame-pointer -fno-stack-protector \
	-Os -Wall -Wextra \
	-Wl,-e,_start -o "$KOUT/vcprobe" "$HERE/../initramfs/vcprobe.c" -lgcc
python3 "$HERE/../../tools/vc_extract.py" "$KERN_BIN" "$KOUT/vcfw"

cp "$HERE/../initramfs/initramfs.list" "$KOUT/initramfs.list"
{
	# the list has no trailing newline, start on a fresh line
	printf '\ndir /usr 755 0 0\ndir /usr/bin 755 0 0\n'
	printf 'file /usr/bin/fbtest %s 755 0 0\n' "$KOUT/fbtest"
	printf 'file /usr/bin/vcprobe %s 755 0 0\n' "$KOUT/vcprobe"
	printf 'dir /lib 755 0 0\ndir /lib/firmware 755 0 0\ndir /lib/firmware/vc 755 0 0\n'
	for f in vc-loader-a vc-loader-b vc-main; do
		printf 'file /lib/firmware/vc/%s.bin %s 644 0 0\n' "$f" "$KOUT/vcfw/$f.bin"
		printf 'file /lib/firmware/vc/%s.trl %s 644 0 0\n' "$f" "$KOUT/vcfw/$f.trl"
	done
} >> "$KOUT/initramfs.list"

# the driver reboots the VC, so Symbian's framebuffer area needs no
# protection: the normal PHYS_OFFSET from e6.config stays
"$KSRC/scripts/config" --file "$KOUT/.config" --disable FB_RAPUYAMA
"$KSRC/scripts/config" --file "$KOUT/.config" --enable FB --enable FRAMEBUFFER_CONSOLE
"$KSRC/scripts/config" --file "$KOUT/.config" --enable SOC_RAPUYAMA_VIDEOCORE
"$KSRC/scripts/config" --file "$KOUT/.config" --set-str INITRAMFS_SOURCE "$KOUT/initramfs.list"
"$KSRC/scripts/config" --file "$KOUT/.config" --enable RAPUYAMA_MODEM

make -C "$KSRC" O="$KOUT" olddefconfig
make -C "$KSRC" O="$KOUT" -j"$(nproc)" zImage
make -C "$KSRC" O="$KOUT" scripts_dtc

"$KOUT/scripts/dtc/dtc" -I dts -O dtb -i "$HERE/../boot" \
	-o "$KOUT/e6-vc.dtb" "$HERE/../boot/e6-vc.dts"

echo
echo "zImage: $KOUT/arch/arm/boot/zImage"
echo "DTB:    $KOUT/e6-vc.dtb"
ls -lh "$KOUT/arch/arm/boot/zImage"
