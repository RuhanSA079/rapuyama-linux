#!/usr/bin/env bash
# builds a minimal armv6 (arm1176) linux kernel for the e6 bring-up
#
# base config is bcm2835_defconfig, the raspberry pi 1 has the SAME core
# (arm1176jzf-s, armv6k) and boots via device tree so its the closest mainline
# thing. builds a zImage (self-decompressing) + dtbs. a real e6 boot still needs
# soc drivers (intc/timer/uart) we dont have yet, for now this just checks the
# toolchain and gives a zImage payload for trampoline testing
#
# sources are the linux/ submodule (my fork of gregkh/linux, branch e6).
# builds out-of-tree into build/ so the submodule stays clean

set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
KSRC="$HERE/linux"
KOUT="$HERE/build"
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnu-
J="$(nproc)"

[ -f "$KSRC/Makefile" ] || { echo "no kernel sources: git submodule update --init --depth 1 linux/kernel/linux"; exit 1; }
mkdir -p "$KOUT"
MAKE=(make -C "$KSRC" O="$KOUT")

case "${1:-build}" in
  config)   "${MAKE[@]}" bcm2835_defconfig ;;
  e6config) cp "$HERE/e6.config" "$KOUT/.config" && "${MAKE[@]}" olddefconfig ;;
  build)    "${MAKE[@]}" -j"$J" zImage dtbs ;;
  menuconfig) "${MAKE[@]}" menuconfig ;;
  savecfg)  cp "$KOUT/.config" "$HERE/e6.config" ;;
  clean)    "${MAKE[@]}" clean ;;
  *) echo "usage: $0 [config|e6config|build|menuconfig|savecfg|clean]"; exit 1 ;;
esac

echo
echo "zImage: $KOUT/arch/arm/boot/zImage"
ls -la "$KOUT/arch/arm/boot/zImage" 2>/dev/null || true