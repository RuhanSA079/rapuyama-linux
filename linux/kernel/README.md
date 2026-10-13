
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

## Status

The E6 kernel has booted on the phone through to BusyBox initramfs userspace.
The checked-in [`../../docs/first-boot_SUCCESS.log`](../../docs/first-boot_SUCCESS.log) records the first timer IRQ, the switch to `rapuyama-fc` as clocksource, and execution of `/init`. The USB-enabled board DTB is used for the interactive
USB gadget serial shell, confirmed on the device with `picocom`.

This is an early userspace bring-up rather than a complete phone port. Many peripheral drivers and board functions remain incomplete or unvalidated.
For the separate E6 board DTB build and phone boot procedure, see [`../boot/README.md`](../boot/README.md).

## Why the RPi1 base and not multi_v7
ARM1176 is ARMv6K, NOT v7. `multi_v7_defconfig` produces code with v7 instructions -> undefined instruction on the E6. So strictly a V6 config (bcm2835).