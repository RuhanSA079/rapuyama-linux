
# rapuyama-linux

<img src="docs/rapuyama-linux.png" alt="logo" width="200">

**A Linux bring-up for the RapuYama hardware platform replacing Symbian at runtime.**

This project takes over the phone from a running Symbian installation, hands control to an ARM Linux kernel, and brings up the RapuYama application SoC.
It combines Symbian-side loaders and hardware probes, reverse engineering of Nokia's firmware, and experimental Linux drivers.

> **Current status: Linux boots on the phone and draws on its screen.** The first boot log shows the RapuYama interrupt controller receiving the timer IRQ, the kernel switching to its RapuYama clocksource, and BusyBox `/init` starting. An interactive shell runs over USB serial. Linux now also boots the Broadcom VideoCore itself, powers the LCD panel up and shows a framebuffer console on the phone's own display (see [the first display log](docs/first-display_SUCCESS.log)).

## Milestones

| Date | Milestone |
| --- | --- |
| 13.10.2026 | Kernel boots through to BusyBox `/init` ([log](docs/first-boot_SUCCESS.log)) |
| 13.10.2026 | Interactive shell over the USB gadget serial port |
| 14.10.2026 | First image on the E6 panel from Linux: VideoCore booted by the kernel, `/dev/fb0` + fbcon ([log](docs/first-display_SUCCESS.log), [notes](docs/videocore-display.md)) |

## The hardware

| Component | What we know |
| --- | --- |
| Application CPU | ARM1176JZF-S, ARMv6K |
| SoC | Nokia RapuYama |
| RAM | 256 MiB at physical `0x10000000` |
| Display | Broadcom BCM2727B1 / VideoCore III, 640x480 panel (id `01 8b 79`), driven over the ViSSI host bus |
| Boot environment | Symbian^3 / EKA2, with a one-way handoff to Linux |
| Kernel | Linux 6.12, built for ARMv6 |

This is a device-specific port, not a generic EKA2 compatibility layer. Some work may carry over to other phones using the same SoC, but each model still needs its own board description, memory and boot validation, and peripheral bring-up.

## What is here

- **Symbian takeover and probes** - inspect memory mappings and peripheral registers, run small ARM payloads, and transfer control to Linux.
- **Linux kernel port** - an ARMv6 kernel configuration plus experimental RapuYama support for interrupt handling, timers, GPIO, clocks, watchdog, UART, and other peripherals.
- **VideoCore display driver** - boots the BCM2727B1 with Nokia's firmware (extracted from your own ROM dump), speaks VCHI to it, powers the panel up like Symbian and exposes `/dev/fb0` with fbcon.
- **Symbian display-stack emulator** - runs Nokia's display drivers from the ROM in Unicorn and records every message they send to the VideoCore; the Linux driver replays those sequences.
- **RAM-backed early logging** - preserve early kernel output in reserved RAM so it can be retrieved after a failed boot, even without a serial console.
- **Hardware notes** - reverse-engineering results from firmware analysis and live probes.

The basic boot, timer/IRQ path, initramfs userspace, USB serial shell and the VideoCore display have been exercised on the phone. Other devices and drivers remain experimental, their presence in the tree does not imply that they have been validated on hardware.

## Build the kernel

On a Linux host, install an ARM GNU EABI cross-toolchain (`arm-linux-gnu-gcc` and binutils), then initialize the kernel submodule and build:

```sh
git submodule update --init --depth 1 linux/kernel/linux
cd linux/kernel
./build-kernel.sh e6config
./build-kernel.sh build
```

The kernel image is written to:

```text
linux/kernel/build/arch/arm/boot/zImage
```

The build script uses `ARCH=arm`, `CROSS_COMPILE=arm-linux-gnu-`, and all available CPU cores by default. `e6config` installs the checked-in E6 configuration, `build` builds the kernel image and the kernel tree's DTBs.

## Build the Nokia E6 device tree

The board DTB used by the E6 loader comes from `linux/boot/e6.dts`. It is separate from the DTBs built by the kernel target above. After building the kernel (which also builds the in-tree `dtc` compiler), run this from the repository root:

```sh
linux/kernel/build/scripts/dtc/dtc -I dts -O dtb -o linux/boot/e6.dtb linux/boot/e6.dts
```

The resulting `linux/boot/e6.dtb` is the board description to pair with the kernel image. If you change the DTS, rebuild this file before deploying it.

## Boot flow

```text
Symbian
  |_ boot.sis / boot.exe
       |_ loads zImage and e6.dtb into physical RAM
       |_ prepares the handoff (MMU and caches off)
       |_ jumps to the Linux ARM kernel
            |_ early log is captured in reserved RAM
```

The loader places the kernel image and DTB in a contiguous physical chunk and passes the DTB address to Linux. The E6 kernel is configured with the fixed physical RAM base `0x10000000`.This avoids deriving the kernel's memory base
from the loader's variable chunk address.

The phone-side package and boot sequence are documented in
[`linux/boot/README.md`](linux/boot/README.md). That guide includes the expected files, recovery/diagnostic notes, and the distinction between normal and probe images. Do not mistake a diagnostic probe image for the regular `zImage`.

For the interactive USB shell, use the USB-enabled DTB as `E:\e6.dtb`, connect the phone to the host over USB, and start the boot package. On a Linux host, connect to the enumerated gadget serial port, typically:

```sh
picocom -b 115200 /dev/ttyACM0
```

The shell runs on the phone's `ttyGS0`. `picocom` provides the host-side terminal. The USB-enabled DTB opts into this shell, while the base DTB uses the non-interactive init path.

## Build with the display driver

`build-kernel.sh` builds the plain USB-shell kernel. For the display, use `build-vc.sh`. It needs a dump of the phone's kernel ROM (`kern.bin`, see [ROM analysis](docs/rom-analysis.md)), because the VideoCore firmware is extracted from it at build time and is not part of this repository:

```sh
KERN_BIN=/path/to/kern.bin linux/kernel/build-vc.sh
```

Boot `linux/kernel/build-vc/arch/arm/boot/zImage` with `linux/kernel/build-vc/e6-vc.dtb` (as `E:\zImage` / `E:\e6.dtb`). After about 5 seconds the panel shows colour bars for 3 seconds, then the console. Driver parameters, the bring-up sequence and known issues are in S[`docs/videocore-display.md`](docs/videocore-display.md).

## Repository map

| Path | Contents |
| --- | --- |
| [`linux/kernel/`](linux/kernel/) | Kernel configuration, build script, and kernel source submodule |
| [`linux/boot/`](linux/boot/) | E6 DTS, trampoline, and boot documentation |
| [`linux/stage0/`](linux/stage0/) | Symbian-side loader and low-level experiments |
| [`driver/`](driver/) | Symbian kernel drivers, SIS packaging, and deployment notes |
| [`tools/`](tools/) | Log parsers and reverse-engineering utilities |
| [`docs/`](docs/) | Hardware maps, driver notes, investigation reports, and the porting guide |
| [`logs/`](logs/) | Captured output from experiments |

## Notes and references

- [Linux bring-up notes](linux/README.md)
- [Kernel build guide](linux/kernel/README.md)
- [Boot pipeline and phone-side instructions](linux/boot/README.md)
- [RapuYama driver reverse engineering](docs/driver-re.md)
- [Hardware memory map](docs/hw-memory-map.md)
- [ROM analysis notes](docs/rom-analysis.md)
- [VideoCore display: overview and driver](docs/videocore-display.md)
- [VideoCore link: wire-level notes](docs/videocore-link.md)
- [Porting guide for other RapuYama / VideoCore phones](docs/porting-guide.md)

## Current boundaries

- This is an early userspace bring-up, not yet a complete everyday phone OS.
- The display works, but is new: there are visible artifacts, every update sends the full 1.2 MB frame over PIO (~170 ms), and blanking/backlight control are not implemented. The display comes up about 5 seconds into boot, so use the USB shell or the RAM log for early messages.
- Treat register addresses and undocumented peripheral behavior as hypotheses unless the notes explicitly say they were checked on live hardware.
- A successful boot does not imply that every peripheral or power-management path is implemented.
- The boot handoff is one-way. A failed run may require the phone's watchdog reset to recover.