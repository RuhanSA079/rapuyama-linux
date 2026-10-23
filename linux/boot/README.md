
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

## e6bfix: boot the Image in place (18.10.2026)
`e6boot.sis` decompresses the zImage to a fixed `0x11600000`, RAM that Symbian never gave us. Big kernels died at random boots there. `e6bfix.sis` boots the uncompressed `E:\Image` where it was loaded instead:
- the chunk is the biggest block `Epoc::AllocPhysicalRam` gives (160 down to 32 MB), the kernel sits at chunk + `0x8000`, the DTB at chunk + `0x1f00000`. `ARM_PATCH_PHYS_VIRT` makes the chunk `PHYS_OFFSET`
- `cma=0` in the bootargs frees 32 MB
- build: `RB_DEFS=-DRB_IMAGE_BOOT ./build_app.sh e6bfix boot_app.cpp 0xe1000024` in `../stage0/symbian-loader`
- copy `Image` to `E:\Image` and the DTB to `E:\e6.dtb`, run the e6bfix icon

## RAM below the kernel (22.10.2026)

Booting in place costs RAM: the block Symbian hands out sits high (`0x19a00000`..`0x1a000000`) and ARM Linux can't use RAM below `PHYS_OFFSET`, so only ~100 MB of the 256 MB were left. The rapuyama kernel now has `TEXT_OFFSET = 0x08a08000`: `PHYS_OFFSET` = block - `0x8a00000` lies under the RAM start, so `0x11600000` up to the kernel is lowmem again (`arch/arm/mach-rapuyama/lowmem.c`).
- with `e6.lowmem=1` (in the daily `e6-vc-565.dtb`) Linux gets that RAM back. Every page Symbian's kernel L1 maps (the ISAH copy the PA_SWD bridge runs on + the live TTBR1 table) and the L2 tables stay reserved, ~6 MB below the kernel and ~4.5 MB above. On the phone: 174 MB available instead of 70 MB (31 MB below the kernel kept for Symbian), PA_SWD calls unchanged
- `e6.lowmem=0` (the kernel default) drops it like before; `e6.lowwatch=1` then checksums every page of it Symbian doesn't map, every 30 s for 10 min, and logs `rapuyama: lowwatch pass N: ... changed`. Anything listed there = something (modem DMA?) still writes into that RAM, it must be reserved before `e6.lowmem=1` is safe. 15 passes on the phone (22.10.2026): nothing changed
- kernel side: `head.S` maps only the kernel's own sections (not everything from `PHYS_OFFSET`), `ARCH_PFN_OFFSET` follows the RAM start, `map_lowmem` keeps going after the bank that holds the kernel
- the gfx DMA push buffer and the VCHI tx slot sit right after the kernel in the booter's block (`dmesg | grep "gfx dma:"`). The idea was that the DMA can't read high RAM; it still timed out with the tx slot at `0x1b5bc000`, so that was not the cause (see `docs/videocore-display.md`)
- the zImage path (`e6boot.sis`) does not work with this `TEXT_OFFSET`, use e6bfix

## Prevent the former 240-second reset (confirmed 22.10.2026)

The E6 also has a secure PA_SWD watchdog beyond the SoC and PMIC watchdogs. The Linux bridge must be enabled with **`e6.swd=1`** in `/chosen/bootargs`; it is disabled by default. Kernel #196 serviced it successfully four times through 648 seconds, with the USB shell still running.

Build with `build-vc.sh` (`e6-vc-565.dts` already has `e6.swd=1`), copy `Image` and the DTB to `E:\Image` and `E:\e6.dtb` and boot using the **e6bfix** loader. e6calb works too but waits up to ~3.5 min for Symbian's phase, which is not needed and leaves the clocks at Symbian's idle level (PIO pushes 250 ms per frame instead of 97 ms). The bridge requires the preserved E6 firmware context and ISAH handoff; older loaders and other firmware versions are not covered by this validation.

Check `dmesg | grep rapuyama-swd` for repeated `stage 60 transport 1 result 1 timeout 240 token 240`. See the [reset report](../../docs/modem-reset-findings.md#use-the-confirmed-fix) for the full build command, firmware path and [original console evidence](../../docs/secure-watchdog_SUCCESS.txt).

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
2. Copy `linux/kernel/build-vc/arch/arm/boot/Image` to the phone as `E:\Image`.
3. Copy `linux/kernel/build-vc/e6-vc-565.dtb` (daily: gfx DMA + RGB565) to the phone as `E:\e6.dtb`. `e6-vc.dtb` = XRGB8888, `e6-vc-khrn.dtb` / `e6-vc-gl.dtb` = VideoCore GL checks.
4. Boot with `e6bfix.sis` (not e6calb, see above). The USB shell keeps working.

About 2 seconds after the handoff, the driver reboots the VideoCore. The Symbian picture goes away and the backlight comes on, then the console shows up. weston + foot run from the initramfs, keypad and touchscreen work under it. Check with `dmesg | grep -i videocore`. Driver parameters (`videocore.dma=`, `videocore.rgb565=`, `videocore.cold=`, `videocore.debug=` ...), weston and GLES 2 (`libvcgl`, `/dev/vc-khrn`) are described in [`../../docs/videocore-display.md`](../../docs/videocore-display.md).

## Root filesystem on the SD card (22.10.2026)

The microSD card gets two partitions: p1 FAT32 (2 GiB, label `E6CARD`) for Symbian, p2 btrfs (label `e6root`) as the Linux rootfs. Symbian must be able to mount p1: a card it can't mount (it asks for a decryption key) gets its power cut, and Linux only finds the card when Symbian left it powered. It holds the same userspace as the initramfs (alpine + weston + `libvcgl` + VideoCore firmware), but unpruned and persistent. The initramfs stays the fallback.

1. Build the kernel with `build-vc.sh` (the card is filled from its `build-vc/initramfs.list`).
2. Put the card into a reader on the pc and run `tools/sdroot.sh /dev/sdX` (whole card, not a partition). It **erases the card**, asks you to type the device name first, needs `sudo`, `sfdisk`, `mkfs.vfat` and `mkfs.btrfs`. `tools/sdroot.sh <dir>` fills an already mounted filesystem instead.
3. Boot with `e6-vc-565.dtb`, it has `e6.root=/dev/mmcblk0p2` (and `e6.swd=1`).

`init.sh` waits up to 10 s for the device, mounts it as btrfs (`noatime`), moves `/dev /proc /sys /tmp` over and `switch_root`s to the card's `/init`. No card, a different filesystem or no `/init` = it logs why and stays in the initramfs. Only btrfs is mounted, so the internal eMMC (Symbian `E:`) is never touched even if the `mmcblk` numbers swap. Check with `dmesg | grep e6:` for `switching to the rootfs on /dev/mmcblk0p2` and `on the sd rootfs`. Works on the phone (22.10.2026).

Swap: `init.sh` always sets up 128 MB of compressed zram (lzo-rle, priority 100) and, on the card, `/swapfile` (1 GiB, priority 10, created by `sdroot.sh` as a nocow file). `dmesg | grep "e6: swap"` lists both. To add the swapfile to an existing card, mount p2 on the pc and run `tools/sdroot.sh <mount point>`. Do the same after every kernel build: the card has its own copy of `/init`, `weston.ini`, `libvcgl` and the VideoCore firmware, the ones in the new initramfs are not used once the card is the rootfs.

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

The display works with the `build-vc.sh` kernel and `e6-vc-565.dtb` (see "Boot with the display" above): weston, GLES 2 from userspace, keypad and touchscreen are confirmed on the phone. The display comes up a few seconds into boot, so early boot problems still need the USB shell or the RAM log. The SD card and internal eMMC work, the card can be the rootfs (see above), never write the eMMC = Symbian `E:`. The 240-second reset is fixed with `e6.swd=1`. Radio, audio, power key and power management are not done; see [`../../docs/driver-re.md`](../../docs/driver-re.md) and [`../../docs/videocore-display.md`](../../docs/videocore-display.md) for the current driver findings.