#!/usr/bin/env bash
# builds the n8 / e7 test 1 kit (docs/other-phones.md) in one go:
# plain usb shell kernel (no videocore, so no kern.bin needed), the n8 /
# e7-usb dtb, <phone>boot.sis + e6logs.sis, all into out/<phone>-test1/
# with SHA256SUMS.
#
# made for debian/ubuntu hosts: the scripts want fedora's arm-linux-gnu-*,
# ubuntu ships the same thing as arm-linux-gnueabi-*. if arm-linux-gnu-gcc is
# missing, symlinks go into tools/bin/cross and that goes first in PATH.
# build_app.sh calls podman, a docker shim goes there too if podman is missing.
#
# needs: gcc-arm-linux-gnueabi, dtc deps (flex bison), docker or podman,
# GCCE in tools/gcce (bootstrap.sh), busybox in work/dl/busybox-armv5l,
# driver/sis/cert.pem + key_trad.pem (self-signed is fine on a hacked phone)
#
# usage: tools/build-n8-kit.sh [n8|e7]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PHONE="${1:-n8}"
case "$PHONE" in
	n8) BOOT_UID=0xe1000050 ;;
	e7) BOOT_UID=0xe1000052 ;;
	*) echo "usage: $0 [n8|e7]"; exit 1 ;;
esac
KSRC="$ROOT/linux/kernel/linux"
KOUT="$ROOT/linux/kernel/build"
KIT="$ROOT/out/$PHONE-test1"
BB="$ROOT/work/dl/busybox-armv5l"

# ubuntu cross names -> the fedora names the scripts use
if ! command -v arm-linux-gnu-gcc >/dev/null; then
	command -v arm-linux-gnueabi-gcc >/dev/null || {
		echo "no arm-linux-gnu-gcc / arm-linux-gnueabi-gcc: apt install gcc-arm-linux-gnueabi"; exit 1; }
	mkdir -p "$ROOT/tools/bin/cross"
	for t in gcc cpp ld as objcopy objdump nm ar ranlib strip readelf size gcc-ar gcc-nm; do
		[ -e "/usr/bin/arm-linux-gnueabi-$t" ] &&
			ln -sf "/usr/bin/arm-linux-gnueabi-$t" "$ROOT/tools/bin/cross/arm-linux-gnu-$t"
	done
fi
if ! command -v podman >/dev/null && command -v docker >/dev/null; then
	mkdir -p "$ROOT/tools/bin/cross"
	printf '#!/bin/sh\nexec docker "$@"\n' > "$ROOT/tools/bin/cross/podman"
	chmod +x "$ROOT/tools/bin/cross/podman"
fi
export PATH="$ROOT/tools/bin/cross:$PATH"
export ARCH=arm CROSS_COMPILE=arm-linux-gnu-

[ -f "$KSRC/Makefile" ] || { echo "no kernel sources: git submodule update --init --depth 1 linux/kernel/linux"; exit 1; }
# the N8 kernel changes are patches on the upstream submodule
"$ROOT/linux/kernel/apply-patches.sh"
[ -f "$BB" ] || { echo "no $BB: https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv5l"; exit 1; }
[ -f "$ROOT/driver/sis/cert.pem" ] || { echo "no driver/sis/cert.pem + key_trad.pem, see driver/DEPLOY.md"; exit 1; }

echo ">> initramfs"
# the checked-in list has absolute paths from the author's machine. our copy
# lives in the build dir, init goes in a subdir (KOUT/init would clash with the
# kernel's init/ objects)
mkdir -p "$KOUT/rootfs-extra"
arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib \
	-o "$KOUT/rootfs-extra/init" "$ROOT/linux/initramfs/init.S"
sed -E -e "s#^(file /bin/busybox ).*( 755 0 0)#\1$BB\2#" \
	-e "s#^(file /sbin/init ).*( 755 0 0)#\1$KOUT/rootfs-extra/init\2#" \
	-e "s#/[^ ]*/linux/initramfs/#$ROOT/linux/initramfs/#" \
	"$ROOT/linux/initramfs/initramfs.list" > "$KOUT/initramfs.list"

# input / sensor test tools (linux/initramfs/{keytest,touchtest}.c, sensortest.sh)
for t in keytest touchtest physdump; do
	arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -fno-pie -no-pie \
		-ffreestanding -fno-builtin -fomit-frame-pointer -fno-stack-protector \
		-Os -Wall -Wextra -Werror \
		-Wl,-e,_start -o "$KOUT/rootfs-extra/$t" "$ROOT/linux/initramfs/$t.c" -lgcc
done
{
	printf '\ndir /usr 755 0 0\ndir /usr/bin 755 0 0\n'
	printf 'file /usr/bin/keytest %s 755 0 0\n' "$KOUT/rootfs-extra/keytest"
	printf 'file /usr/bin/touchtest %s 755 0 0\n' "$KOUT/rootfs-extra/touchtest"
	printf 'file /usr/bin/sensortest %s 755 0 0\n' "$ROOT/linux/initramfs/sensortest.sh"
	printf 'file /usr/bin/physdump %s 755 0 0\n' "$KOUT/rootfs-extra/physdump"
	printf 'file /usr/bin/swddump %s 755 0 0\n' "$ROOT/linux/initramfs/swddump.sh"
} >> "$KOUT/initramfs.list"

echo ">> kernel"
cp "$ROOT/linux/kernel/e6.config" "$KOUT/.config"
"$KSRC/scripts/config" --file "$KOUT/.config" --set-str INITRAMFS_SOURCE "$KOUT/initramfs.list"
# n8 chips (docs/n8-hardware.md, used by n8.dts): gpio keys, lis302dl,
# ak8974, tsl2563, the synaptics rmi4 touchscreen. the e6 dtbs don't name them, so they stay idle there
"$KSRC/scripts/config" --file "$KOUT/.config" --enable KEYBOARD_GPIO_POLLED \
	--enable IIO --enable IIO_BUFFER --enable IIO_TRIGGERED_BUFFER \
	--enable IIO_ST_ACCEL_3AXIS --enable IIO_ST_ACCEL_I2C_3AXIS \
	--enable AK8974 --enable SENSORS_TSL2563 \
	--enable RMI4_CORE --enable RMI4_I2C --enable RMI4_2D_SENSOR --enable RMI4_F11
# /dev/mem for all of RAM: physdump / swddump read the firmware's memory
"$KSRC/scripts/config" --file "$KOUT/.config" --disable STRICT_DEVMEM
make -C "$KSRC" O="$KOUT" olddefconfig >/dev/null
make -C "$KSRC" O="$KOUT" -j"$(nproc)" Image scripts_dtc

echo ">> dtb"
# n8: the one n8.dts with everything verified; e7: still the first usb test
DTS="$PHONE-usb"
[ "$PHONE" = n8 ] && DTS=n8
"$KOUT/scripts/dtc/dtc" -q -I dts -O dtb -i "$ROOT/linux/boot" \
	-o "$KOUT/$PHONE.dtb" "$ROOT/linux/boot/$DTS.dts"

echo ">> loader"
(cd "$ROOT/linux/stage0/symbian-loader" &&
	# e6bfix with its own name, uid and dtb: <phone>boot reads E:\<phone>.dtb
	RB_DEFS="-DRB_IMAGE_BOOT -DRB_DTB=\"$PHONE\"" \
		./build_app.sh "${PHONE}boot" boot_app.cpp "$BOOT_UID" &&
	./build_app.sh e6logs ramread_app.cpp 0xe1000022)

echo ">> kit"
rm -rf "$KIT"
mkdir -p "$KIT/phone-E-drive" "$KIT/install" "$KIT/pc"
cp "$KOUT/arch/arm/boot/Image" "$KIT/phone-E-drive/Image"
cp "$KOUT/$PHONE.dtb" "$KIT/phone-E-drive/$PHONE.dtb"
cp "$ROOT/out/${PHONE}boot.sis" "$ROOT/out/e6logs.sis" "$KIT/install/"
cp "$ROOT/tools/parse_klog.py" "$KIT/pc/"
cp "$ROOT/docs/other-phones.md" "$KIT/INSTRUCTIONS.md"
(cd "$KIT" && find . -type f ! -name SHA256SUMS ! -name '*.md' | sort | xargs sha256sum > SHA256SUMS)

echo
echo "kit: $KIT"
cat "$KIT/SHA256SUMS"
