# Linux kernel for Nokia E6 (RapuYama / ARM1176JZF-S, ARMv6K)

## Toolchain (installed 12.10.2026)
`arm-linux-gnu-gcc 16.2.1` + `binutils 2.46` (Fedora packages `gcc-arm-linux-gnu`,
`binutils-arm-linux-gnu`). Prefix `CROSS_COMPILE=arm-linux-gnu-`, target
`arm-linux-gnueabi` (soft-float EABI, correct for ARM1176).

## Config base: bcm2835 (Raspberry Pi 1)
RPi1 = **the same ARM1176JZF-S core (ARMv6K)**, boots via device tree, has
earlycon. Closest mainline relative. `bcm2835_defconfig` is the starting point.
`ARCH_MULTI_V6=y`, `AEABI=y` confirmed.

## Sources
Submodule `linux/` - our fork of `gregkh/linux` (stable), branch `e6` from tag
`v6.12`. Cloned shallow (`.gitmodules: shallow = true`):
```
git submodule update --init --depth 1 linux/kernel/linux
```

## Build
Out-of-tree into `build/` (not in git), the submodule stays clean.
```
./build-kernel.sh e6config # e6.config -> build/.config (or: config = bcm2835_defconfig)
./build-kernel.sh build    # zImage + dtbs
./build-kernel.sh savecfg  # after menuconfig - save back to e6.config
```
Result: `build/arch/arm/boot/zImage`.

## Status and the honest limit
- Toolchain + build are validated by this zImage.
- zImage is a self-decompressing payload for the trampoline test (Milestone 2).
- **This kernel won't actually boot on the E6 yet**: RapuYama has no mainline
  support (no interrupt controller, timer, UART, clock drivers). Need the
  phys bases and semantics of those blocks, and only a live probe on the phone gives them
  (see `../../docs/hw-memory-map.md`, `../README.md`).

## Path to an actually bootable E6 kernel
1. **Live probe** (ptdump v9/v10) -> INTC/timer/UART phys bases + their type.
2. Fill `../boot/e6.dts` with real `reg=<...>` and `compatible`.
3. Write minimal drivers/bindings:
   - if INTC/timer look like something known (e.g. ARM PrimeCell PL190/PL192 vic,
     sp804 timer, pl011 uart) - reuse the mainline drivers via DT;
   - otherwise - minimal irqchip/clocksource/earlycon for RapuYama.
4. Board/machine: DT-only via `ARCH_MULTIPLATFORM` + our `compatible`, or a
   minimal `DT_MACHINE_START`.
5. earlycon to the found UART (for the log; screen/adapter not required if the log
   goes into a RAM buffer and gets read back by the trampoline).

## Why the RPi1 base and not multi_v7
ARM1176 is ARMv6K, NOT v7. `multi_v7_defconfig` produces code with v7 instructions ->
undefined instruction on the E6. So strictly a V6 config (bcm2835).