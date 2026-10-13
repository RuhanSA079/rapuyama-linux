# E6 boot pipeline (zImage trampoline + booter)

The full loader to boot Linux on the Nokia E6 is assembled and compiles. It's
**ready**, but won't boot *visibly* until the adapter phase brings the RapuYama
peripheral drivers, watchdog handling and a serial console (see "Blockers"
below). A blind run today just gets reset by the watchdog.

## Pieces
| File | Role |
|---|---|
| `tramp_boot.S` | one-way handoff stub: identity page, IRQ/MMU/caches off, set r0=0/r1=mach/r2=dtb, `bx` into the kernel |
| `e6.dts` / `e6.dtb` | device tree; `/memory` describes the fixed 256 MiB RAM range at `0x10000000` |
| `../stage0/symbian-loader/runblob_ldd.cpp` | booter driver: `AllocBoot` (contiguous phys via `Epoc::AllocPhysicalRam` + `DPlatChunkHw`), `LoadChunk` (stream bytes in), `Boot` (optional legacy DTB sentinel patch, clean caches, jump via `tramp_boot`) |
| `../stage0/symbian-loader/boot_app.cpp` | streams `E:\zImage` + `E:\e6.dtb` into the chunk and calls `Boot` |
| `../../driver/boot.sis` | installer (runblob.ldd + boot.exe) |
| `../kernel/build/arch/arm/boot/zImage` | ARMv6/ARM1176 kernel (bcm2835 base), built by `../kernel/build-kernel.sh` |

Chunk layout: zImage @ `+0x8000` (TEXT_OFFSET), DTB @ `+0x02000000`, chunk 48 MB.
The kernel is built with fixed `PHYS_OFFSET=0x10000000` and
`ARM_PATCH_PHYS_VIRT`, matching the DTB's 256 MiB RAM range. This deliberately
avoids `AUTO_ZRELADDR`, which rounded a zImage chunk at `0x18000000` down to
that 128 MiB boundary and caused Linux to discard the lower half of RAM.

## Wiring the console (13.10.2026)
The console is the **bluetooth uart** (`0x20025a00`, `rapuyama_earlycon`), it has test pads on the N8 board
(same RapuYama board family as the E6, check the E6 board for the same pads):
| Pad | Net | Adapter |
|---|---|---|
| J6354 | BTUARTIn (SoC tx -> bt chip) | adapter **RX** |
| J6355 | BTUARTOut (bt chip -> SoC rx) | adapter TX (leave off at first, the bt chip drives it too) |
| any | GND | GND |
**1.8 V logic.** A 3.3 V/5 V adapter can kill the pad, use a 1.8 V one or a level shifter.
115200 8N1. (The old "J6/H6" note was the GPS chip uart balls, not a debug port.)

## Usage
0. In Symbian **switch bluetooth ON** (keeps the uart block powered, clocked and muxed for earlycon).
1. Copy `linux/kernel/build/arch/arm/boot/zImage` -> phone `E:\zImage`.
2. Copy `linux/boot/e6.dtb` -> phone `E:\e6.dtb` (rebuild it from `e6.dts` after changing it).
3. Install `driver/boot.sis`, run it. It streams both into phys RAM and
   hands off one-way. (`E:\boot.log` records progress up to `about-to-Boot`.)
4. If it dies: `panic=10` reboots, and if the kernel hangs before the watchdog driver the PMIC watchdog
   powers the phone off ~48 s after symbian's last kick -> no battery pull needed.

## IRQ vector probe

To test whether the CPU reaches the ARM IRQ vector before the normal IRQ stack
save, copy `e6-irqvector-probe-zImage` to `E:\zImage` and
`e6-irqvector-probe.dtb` to `E:\e6.dtb`. This diagnostic image sends the
existing PMIC power-off command from the IRQ vector itself. An immediate
power-off confirms that an IRQ reached the vector; no power-off is inconclusive
because the IRQ may not have arrived or the early CBUS/PMIC access may have
failed. Restore the normal `zImage` and `e6.dtb` after the test.

## Test ladder without a uart adapter (13.10.2026)
Files in `out/`: `zImage` (busybox initramfs), `e6.dtb`, `e6-usb.dtb`, `boot.sis`, `kickloop.sis`.
| step | phone | what it tells |
|---|---|---|
| `kickloop.sis` | stays alive (frozen) | the watchdog kicks are right (**done 13.10, alive**) |
| `boot.sis` + `e6.dtb` | **clean power off ~30 s** after the jump | kernel + timer + irqs + scheduler + busybox `/init` work |
| | clean power off ~60 s | busybox didn't run, the asm `/sbin/init` fallback did |
| | black screen (backlight on) + reboot ~13 s | died before the watchdog driver (soc watchdog reset) |
| | frozen for ever | kernel alive, init stuck / power off failed |
| `boot.sis` + `e6-usb.dtb` renamed to `E:\e6.dtb`, usb cable plugged, bt on | a new usb serial device on the pc (`dmesg`, `/dev/ttyACM0`) | usb gadget works -> `screen /dev/ttyACM0 115200` = busybox shell + kernel log |

## Status 13.10.2026
Drivers reversed from the firmware and in the kernel (`docs/driver-re.md`): intc, ms timer + 32k counter
(**checked live**), gpio (+ pin irqs), i2c (mainline i2c-omap), pmic/soc watchdog, earlycon on the bt uart.
The old blockers below are solved except the display.


## Old blockers (12.10.2026, mostly solved 13.10)
1. **Watchdog.** RapuYama has a WDT (firmware strings `CWatchdog`,
   `CWatchdogTimer`, `ShutdownWatchDog`, `mdswatchdog.exe`). With Symbian dead
   nothing kicks it -> hardware reset in ~seconds (that's what killed the
   takeover test). The kernel has to kick/disable it very early -> need its register
   (HAL-abstracted in firmware, get it in the adapter phase). `tramp_boot.S` has a
   stub slot for a watchdog write.
2. **Display = Broadcom VideoCore, command-mode.** Modules `vcdispman*`,
   `vcdispmanvchi.dll` (VideoCore Host Interface), `dispcommanddrv.dll`,
   `display_chipset_drv_ive_bc.dll` ("bc"=Broadcom). The panel is command-mode,
   driven by the VideoCore GPU, NOT a dumb scanout framebuffer. So writing RAM
   from a dead Symbian shows nothing -> **the display can't be an observability
   channel, a USB-UART is required** to see the kernel.
3. **No RapuYama timer/INTC/UART drivers** in the kernel yet -> even past the
   watchdog it stops at `setup_arch`/timer init. These come from the adapter phase
   (addresses via live probe with a console).

## Status
Loader mechanism: **done & proven** (M0 + identity map + MMU off + full handoff).
Boot pipeline: **built**. What's left = peripheral drivers + watchdog + console,
all waiting on the USB-UART adapter.