# Nokia E6 display: BCM2727B1 VideoCore III from Linux

## Status

**Linux shows an image on the E6 panel** (14.10.2026). The kernel driver
`drivers/soc/rapuyama/videocore.c` does the following:

1. Boots the VideoCore over the ViSSI host bus with Nokia's own firmware.
2. Brings up VCHI.
3. Powers the LCD panel up the way Symbian does.
4. Exposes a 640x480 XRGB8888 `/dev/fb0`.
5. Pushes the framebuffer to the VideoCore whenever it changes.

fbcon runs on it, so the kernel console and the shell are visible on the phone
itself. The log of the first run is in
[`first-display_SUCCESS.log`](first-display_SUCCESS.log).

Known limitations:

- Some artifacts are visible on screen. They are not diagnosed yet: pixel
  format, stride, or fbcon writing while a frame is in flight.
- Every update sends the whole frame: 1.2 MB of PIO bulk, about 170 ms. There
  is no dirty-rectangle or DMA path yet.
- Only boot loader A is accepted. Loader B, Symbian's power-up variant, makes
  the VC send its hello again (`loader answered 1/1`).
- No suspend, blanking, backlight class or rotation. Brightness is a module
  parameter.

The wire-level details (host bus registers, boot protocol, VCHI slots, bulk
transfers, DSPC/DISP/BFMX message layouts) are in
[`videocore-link.md`](videocore-link.md). This page is the overview.

## Stack

```text
Linux fbdev (/dev/fb0, deferred I/O)                videocore.c
  |_ BFMX  vc_bufmanx: copy the frame into a VC resource (bulk)
  |_ DISP  dispman: display, resource, element, update/submit
  |_ UPDH  dispman update handler (opened, messages ignored)
  |_ DSPC  dispcommanddrv: panel config, power, DCS batches/reads
       |_ VCHI: 0x1020-byte slots, credits, CTRL service, BULX bulk
            |_ ViSSI host bus 0x20025800, 16-bit PIO, HAT/RUN
                 |_ BCM2727B1, booted with loader A + main image
                      |_ panel 01 8b 79, 640x480 over interface id 4
```

On Symbian the same layers are `vcdriver.dll`, `vchi.dll`, `vc_bufmanx`,
`vcdispman*.dll`, `dispcommanddrv.dll` and
`display_chipset_drv_ive_bc.dll`. The Linux driver puts all of them into one
file and keeps only what the E6 needs.

## Bring-up sequence

The driver repeats what Symbian sends. The sequence was not guessed: it was
captured by running Symbian's display stack from `kern.bin` in an emulator
(see "How the sequence was obtained" below).

1. **VC boot.** Raise RUN (gpio 92). The VC sends a hello, the host sends
   `vc-loader-a` and gets an ack. The VC sends a second hello, the host sends
   `vc-main` and gets the final ack.
2. **VCHI.** Wait for the VC's CTRL connect, answer it, then open the services
   with the client flags `0x10`.
3. **DSPC install.** Panel config (cmd 3), then ping (cmd 5).
4. **Op 1, init.** Read panel registers `0a`..`0e`. Symbian branches on `0x0a`
   bit 2 (display on).
   - **Cold** (the driver default): power `0x41`, `0x40`, wait 245 ms; read
     the panel id (`dc db da`); config; power `0x81`, wait 51 ms; batch
     `[53 3f, 51 3f, 55 00, 11]` (sleep out), wait 120 ms; config; colour
     matrix (cmd 7, all zero); batch `[26 01, 36 00, 3a 70]`.
   - **Warm**: config; power `0x81`; backlight; mode; reads; op 8.
5. **Compose.** Run the dispman calls in this order:
   - `DisplayOpen(0)`
   - `ResourceCreate(0x2b, 640, 480)`
   - BFMX write
   - `UpdateStart`
   - `ElementAdd`
   - `SetBackground`
   - `SubmitSync`
6. **Display on.** Send batch `[29]`, read registers `0a`..`0e`, then op 8 three
   seconds later.
7. **Refresh, op 3.** The first refresh sends batch `[3a 70, 35 00, 34]`,
   every later one sends `[35 00]`. Each refresh then does the following:
   - BFMX write
   - `UpdateStart`
   - `ElementModified` with the full rectangle
   - `SubmitSync`

The cold path is required because Linux always boots the VC from scratch.
After the VC boot the panel still reads `0a = 1c`, so Symbian's own check
would pick the warm path. But nothing drives the panel at that point: its
booster stays off and the screen stays black. The cold path power-cycles the
panel through the VC and takes it out of sleep.

## Using it

1. Build the kernel, initramfs and DTB with
   [`linux/kernel/build-vc.sh`](../linux/kernel/build-vc.sh):
   ```sh
   KERN_BIN=/path/to/kern.bin linux/kernel/build-vc.sh
   ```
   The script enables `CONFIG_SOC_RAPUYAMA_VIDEOCORE`, `FB` and
   `FRAMEBUFFER_CONSOLE`. It also extracts the VideoCore images from your ROM
   dump into the initramfs (`/lib/firmware/vc/`) with
   [`tools/vc_extract.py`](../tools/vc_extract.py). The firmware is Nokia's
   and is not in this repository.
2. Boot `linux/kernel/build-vc/arch/arm/boot/zImage` with
   `linux/kernel/build-vc/e6-vc.dtb`. The DTB is `e6-usb.dts` plus the
   `videocore@20025800` node and `console=tty0`.
3. Watch the result with `dmesg | grep -i videocore`. The interesting lines
   are:
   - `panel power-up: cold path`
   - `display up, 640x480`
   - `test pattern: 0`

   The panel first shows 8 colour bars for 3 seconds, then the console.

The kernel driver itself is in the kernel fork, branch `e6`, at
`drivers/soc/rapuyama/`. Until it lands there, apply
[`linux/kernel/videocore-driver.patch`](../linux/kernel/videocore-driver.patch)
to the submodule.

Module parameters (`videocore.<name>=` on the kernel command line):

| parameter | default | meaning |
|---|---|---|
| `cold` | `1` | panel power-up path: `1` cold, `0` warm, `-1` choose by `0x0a` like Symbian |
| `brightness` | `0x3f` | DCS `0x51` value (Symbian uses `0x3f`) |
| `test_pattern` | `1` | colour bars for 3 s after bring-up |
| `matrix` | `0` | send an identity colour matrix instead of Symbian's zero one |
| `loader_b` | `0` | boot with loader B (rejected on the E6, kept for experiments) |
| `debug` | `1` | log VCHI traffic: `1` bring-up and first frames, `2` everything |

## How the sequence was obtained

1. Ghidra decompilation of the display modules from `kern.bin` was used for
   the structure: [`driver-re.md`](driver-re.md) describes the pipeline.
2. Static reading went wrong in places. Ghidra inverted a branch condition, and
   one state machine depends on what the panel answers. So the real Symbian
   code was run in a Unicorn harness:
   [`tools/vcemu/run_panel.py`](../tools/vcemu/run_panel.py).
3. The harness does the following:
   - Loads every module's `.data` from the ROM headers.
   - Runs the static constructors.
   - Stubs the EKA2 kernel: DFCs, timers, physical RAM and the VCHI service
     open.
   - Answers DSPC requests with `{token, 0}` and DCS reads with the E6
     panel's values.
   - Logs every message the stack would send to the VideoCore.
4. `PANEL_0A=<hex>` sets the answer to the `0x0a` read. `00` gives
   [`symbian-display-sequence-cold.txt`](symbian-display-sequence-cold.txt),
   `9c` gives
   [`symbian-display-sequence-warm.txt`](symbian-display-sequence-warm.txt).
5. Timer ticks in those logs are milliseconds.

Lesson learned: the first capture answered `0x0a` with `9c` and therefore
recorded the warm path. The resulting driver was "1:1" but still left the
screen black. When you port the sequence, feed the emulator the values your
real panel returns.

[`linux/initramfs/vcprobe.c`](../linux/initramfs/vcprobe.c) is the userspace
`/dev/mem` probe used before the kernel driver existed. It is still useful to
poke the bus or single DSPC commands by hand.

## Next steps

1. Find out where the artifacts come from: check the colour order of the bars,
   the stride, and whether fbcon writes during a bulk transfer.
2. Send only the dirty rectangle: BFMX source and destination rectangles plus
   `ElementModified` with the real bounds.
3. Use the gfx DMA engine (`0x20024800`, irq `0x92`) for bulk data instead of
   PIO.
4. Blanking and power-off (Symbian op 2: `28 38 13`, `53 00 10`, power
   `0x80 0x41 0x40`), and a backlight class device.
5. Find out why loader B is rejected (`1/1`). Symbian uses it at power-up.
