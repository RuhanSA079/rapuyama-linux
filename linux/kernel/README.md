
# Linux kernel for Nokia E6 (RapuYama / ARM1176JZF-S, ARMv6K)

## Toolchain (installed 12.10.2026)
`arm-linux-gnu-gcc 16.2.1` + `binutils 2.46` (Fedora packages `gcc-arm-linux-gnu`, `binutils-arm-linux-gnu`). Prefix `CROSS_COMPILE=arm-linux-gnu-`, target `arm-linux-gnueabi` (soft-float EABI, correct for ARM1176).

## Config base: bcm2835 (Raspberry Pi 1)
RPi1 = **the same ARM1176JZF-S core (ARMv6K)**, boots via device tree, has earlycon. Closest mainline relative. `bcm2835_defconfig` is the starting point. `ARCH_MULTI_V6=y`, `AEABI=y` confirmed.

## Sources
Submodule `linux/` - a fork of `gregkh/linux` (stable), branch `e6` from tag `v6.12`. Cloned shallow (`.gitmodules: shallow = true`):
```
git submodule update --init --depth 1 linux/kernel/linux
```

## Build
Out-of-tree into `build/` (not in git), the submodule stays clean.
```
./build-kernel.sh e6config
./build-kernel.sh build
./build-kernel.sh savecfg
```
Result: `build/arch/arm/boot/zImage`

## Build with the VideoCore display driver
```
KERN_BIN=/path/to/kern.bin ./build-vc.sh
```
Output goes to `build-vc/` (not in git):
- `arch/arm/boot/zImage`
- `e6-vc.dtb`, built from `../boot/e6-vc.dts`

On top of `e6.config` the script enables `SOC_RAPUYAMA_VIDEOCORE`, `FB` and
`FRAMEBUFFER_CONSOLE` and disables the diagnostic `FB_RAPUYAMA`. It adds two
things to the initramfs:
- `fbtest`
- the VideoCore images (`/lib/firmware/vc/vc-{loader-a,loader-b,main}.{bin,trl}`), which
  `../../tools/vc_extract.py` extracts from `kern.bin` at build time.

The firmware is Nokia's and is never committed. The normal `PHYS_OFFSET` is
kept, because the driver reboots the VideoCore and does not need Symbian's
framebuffer.

The driver is `drivers/soc/rapuyama/videocore.c` in the kernel fork (branch
`e6`). Until it is committed there, apply `videocore-driver.patch` in the
submodule:
```
git -C linux apply ../videocore-driver.patch
```

## Status

The E6 kernel has booted on the phone through to BusyBox initramfs userspace, and with `build-vc.sh` it also drives the display: VideoCore boot, panel power-up, `/dev/fb0` with fbcon (see [`../../docs/videocore-display.md`](../../docs/videocore-display.md)).
The checked-in [`../../docs/first-boot_SUCCESS.log`](../../docs/first-boot_SUCCESS.log) records the first timer IRQ, the switch to `rapuyama-fc` as clocksource, and execution of `/init`. The USB-enabled board DTB is used for the interactive
USB gadget serial shell, confirmed on the device with `picocom`.

This is an early userspace bring-up rather than a complete phone port. Many peripheral drivers and board functions remain incomplete or unvalidated.
For the separate E6 board DTB build and phone boot procedure, see [`../boot/README.md`](../boot/README.md).

## Why the RPi1 base and not multi_v7
ARM1176 is ARMv6K, NOT v7. `multi_v7_defconfig` produces code with v7 instructions -> undefined instruction on the E6. So strictly a V6 config (bcm2835).