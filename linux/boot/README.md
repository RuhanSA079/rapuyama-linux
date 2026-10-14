
# E6 boot pipeline (zImage trampoline + booter)

The loader boots Linux on the Nokia E6 through to BusyBox initramfs userspace. The checked-in successful boot log records timer IRQ delivery, the RapuYama clocksource becoming active, and execution of `/init`. With the USB gadget DTB, an interactive shell is available on the host over USB serial, bidirectional shell I/O has been confirmed with picocom.

## Pieces
| File | Role |
|---|---|
| `tramp_boot.S` | one-way handoff stub: identity page, IRQ/MMU/caches off, set r0=0/r1=mach/r2=dtb, `bx` into the kernel |
| `e6.dts` / `e6.dtb` | device tree; `/memory` describes the fixed 256 MiB RAM range at `0x10000000` |
| `e6-usb.dts` / `e6-usb.dtb` | USB gadget variant; enables MUSB and the `e6.usbsh` init path |
| `e6-vc.dts` | `e6-usb.dts` + the `videocore@20025800` node (`nokia,rapuyama-videocore`: ViSSI host bus, clock `0x18`, RUN = gpio 92) and `console=tty0`; built by `../kernel/build-vc.sh` |
| `../stage0/symbian-loader/runblob_ldd.cpp` | booter driver: `AllocBoot` (contiguous phys via `Epoc::AllocPhysicalRam` + `DPlatChunkHw`), `LoadChunk` (stream bytes in), `Boot` (optional legacy DTB sentinel patch, clean caches, jump via `tramp_boot`) |
| `../stage0/symbian-loader/boot_app.cpp` | streams `E:\zImage` + `E:\e6.dtb` into the chunk and calls `Boot` |
| `../../driver/boot.sis` | installer (runblob.ldd + boot.exe) |
| `../kernel/build/arch/arm/boot/zImage` | ARMv6/ARM1176 kernel (bcm2835 base), built by `../kernel/build-kernel.sh` |

Chunk layout: zImage @ `+0x8000` (TEXT_OFFSET), DTB @ `+0x02000000`, chunk 48 MB.
The kernel is built with fixed `PHYS_OFFSET=0x10000000` and
`ARM_PATCH_PHYS_VIRT`, matching the DTB's 256 MiB RAM range. This deliberately avoids `AUTO_ZRELADDR`, which rounded a zImage chunk at `0x18000000` down to that 128 MiB boundary and caused Linux to discard the lower half of RAM.

## Optional UART console wiring
The separate hardware serial console is the **bluetooth uart**
(`0x20025a00`, `rapuyama_earlycon`); it has test pads on the N8 board (same RapuYama board family as the E6, check the E6 board for the same pads):
| Pad | Net | Adapter |
|---|---|---|
| J6354 | BTUARTIn (SoC tx -> bt chip) | adapter **RX** |
| J6355 | BTUARTOut (bt chip -> SoC rx) | adapter TX (leave off at first, the bt chip drives it too) |
| any | GND | GND |
**1.8 V logic.** A 3.3 V/5 V adapter can kill the pad, use a 1.8 V one or a level shifter.
115200 8N1. (The old "J6/H6" note was the GPS chip uart balls, not a debug port.)

## Boot to the USB shell
1. Copy `linux/kernel/build/arch/arm/boot/zImage` to the phone as `E:\zImage`.
2. Copy `linux/boot/e6-usb.dtb` to the phone as `E:\e6.dtb`.
3. Connect the phone to the host over USB before the handoff, so Symbian leaves the USB block powered. Install and run `driver/boot.sis`. It loads both files into physical RAM and jumps to Linux. `E:\boot.log` records loader progress through `about-to-Boot`.
4. On the host, connect to the enumerated gadget serial port, typically with:
```sh
picocom -b 115200 /dev/ttyACM0
   ```

The BusyBox shell runs on the phone's `ttyGS0`. This is an interactive userspace shell, not yet a complete phone OS or a kernel printk console over USB.

The base `e6.dtb` selects a non-interactive init path that logs a sign of life and reboots after 30 seconds. The USB DTB selects the persistent shell path. If the gadget does not appear or the host does not enumerate it, init logs the failure and reboots so the RAM log can be retrieved.

## Boot with the display

1. Build with `KERN_BIN=/path/to/kern.bin ../kernel/build-vc.sh`.
2. Copy `linux/kernel/build-vc/arch/arm/boot/zImage` to the phone as
   `E:\zImage`.
3. Copy `linux/kernel/build-vc/e6-vc.dtb` to the phone as `E:\e6.dtb`.
4. Boot as for the USB shell. The USB shell keeps working.

About 2 seconds after the handoff, the driver reboots the VideoCore. The
Symbian picture goes away and the backlight comes on. Around 5 seconds the
panel shows 8 colour bars for 3 seconds, then the console. Check with
`dmesg | grep -i videocore`. Driver parameters (`videocore.cold=`,
`videocore.brightness=`, `videocore.debug=` ...) are described in
[`../../docs/videocore-display.md`](../../docs/videocore-display.md).

## Framebuffer-preserving kernel experiment (superseded)

Superseded by the VideoCore driver above. This experiment kept Symbian's
framebuffer memory, but nothing refreshed the panel from it. It is kept for
reference.


The current `Image` is loaded at `0x10008000` and overwrites the framebuffer at
`0x10480000..0x10880000`. A separate diagnostic zImage can be built with
`PHYS_OFFSET=0x10880000`, moving the decompressed kernel start to
`0x10888000` while leaving the standard `e6.config` and `build/` output
untouched:

```sh
./linux/kernel/build-display-test.sh
```

The script places the image at
`linux/kernel/build-display/arch/arm/boot/zImage`, copies the USB-shell DTB,
and builds `linux/kernel/build-display/e6-display-test.dtb`. The latter
reserves the 4 MiB buffer and enables `/dev/fb0` through the diagnostic
`rapuyama-fbmem` driver. On this diagnostic boot only, initramfs writes an
eight-bar color pattern to `/dev/fb0` and logs whether that write succeeded.
This verifies Linux's framebuffer mapping and provides a visible test if the
panel happens to refresh, but it does not send VideoCore update commands and
does not guarantee a change on the E6's command-mode panel.

## IRQ vector probe

To test whether the CPU reaches the ARM IRQ vector before the normal IRQ stack save, copy `e6-irqvector-probe-zImage` to `E:\zImage` and `e6-irqvector-probe.dtb` to `E:\e6.dtb`. This diagnostic image sends the existing PMIC power-off command from the IRQ vector itself. An immediate
power-off confirms that an IRQ reached the vector; no power-off is inconclusive because the IRQ may not have arrived or the early CBUS/PMIC access may have failed. Restore the normal `zImage` and `e6.dtb` after the test.

## Current status and remaining work
The successful boot log is preserved at
[`../../docs/first-boot_SUCCESS.log`](../../docs/first-boot_SUCCESS.log). It shows the timer interrupt being handled, the `rapuyama-fc` clocksource becoming active, and BusyBox `/init` starting. The USB gadget shell is an additional working path confirmed on the phone.

The display works with the `build-vc.sh` kernel and `e6-vc.dtb` (see "Boot with the display" above). It comes up about 5 seconds into boot, so early boot problems still need the USB shell or the RAM log. Reaching a shell does not mean that display, storage, radio, audio, or power-management support is complete; see
[`../../docs/driver-re.md`](../../docs/driver-re.md) and
[`../../docs/videocore-display.md`](../../docs/videocore-display.md) for the current driver findings.