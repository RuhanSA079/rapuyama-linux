# Porting guide: Linux on other RapuYama / VideoCore phones

This guide is for anyone who wants to bring this port to another phone: a
different Symbian^3/Anna/Belle model, or the same E6 with a different
firmware. It lists every device-specific value the E6 port relies on, where
each one came from, and how to find it on your own device. It also covers the
mistakes that cost us the most time.

The E6 is the only device this has been tested on. Treat statements about
other models as hypotheses until you have checked them on hardware.

## 0. Is your phone a candidate?

The approach needs three things.

1. **Same CPU class: an ARM1176 (ARMv6K) application processor.** Check the MIDR from a live phone: on the E6 the `ptdump` tool reads `0x410fb764`. A v7 core (Cortex-A8/A9) means a different kernel config, see [`linux/kernel/README.md`](../linux/kernel/README.md).
2. **The RapuYama SoC.** Its peripheral bus sits at `0x20020000..0x2004ffff`, with the interrupt controller at `0x20400000`. See [`hw-memory-map.md`](hw-memory-map.md) and [`driver-re.md`](driver-re.md). If the ROM's kernel extensions (`karap`, `egpio`, `power_resources`) touch the same pages, you are probably on the same SoC. `tools/kmod_scan.py` shows which module touches which page.
3. **A BCM2727 VideoCore behind the display**, for the display part. Check the ROM listing for `unsigned_2727b1.dll`, `vcdriver.dll`, `vchi.dll`, `vcdispman*.dll`, `dispcommanddrv.dll` and `display_chipset_drv_ive_bc.dll`, and the strings for `bcm2727b1`.

## 1. What you need

| What | Why | How we got it on the E6 |
| --- | --- | --- |
| The phone's firmware (core image) | Every address and constant below comes from it | Stock RM-609 firmware, unpacked with `tools/fpsx_extract.py`, walked with `tools/rom_walk.py` ([`rom-analysis.md`](rom-analysis.md)) |
| `kern.bin`, the kernel ROM from `0x80000000` | Ghidra, the emulator, `vc_extract.py` | Cut out of the core image, see [`driver-re.md`](driver-re.md) |
| `ekern_exports.txt` | Function names for ekern calls | Generated from `eabi/ekernu.def` and placed next to `kern.bin` |
| A way to run your own code on the phone | Probes and the Linux handoff | `driver/` SIS packages (`runblob.ldd`, `boot.exe`, `ptdump`) |
| Observability | Seeing anything at all | RAM console first, then the USB gadget shell ([`linux/boot/README.md`](../linux/boot/README.md)) |
| Tools on the host | | `arm-linux-gnu-gcc`, Ghidra 11, Python 3 with `unicorn` |

## 2. Boot Linux and get a shell (no display yet)

Port in this order. Each step gives you the observability for the next one.

1. **Memory.** Find the physical RAM base and size from a live page-table dump (`ptdump`). On the E6 it is `0x10000000`, 256 MiB. Put it in your DTS `/memory` node, and set a fixed `PHYS_OFFSET` in the kernel config. Do not use `AUTO_ZRELADDR` ([why](../linux/boot/README.md)).
2. **Handoff.** `tramp_boot.S` and the `runblob` loader are generic for EKA2 on ARM11. Only the chunk layout and the DTB address may need care.
3. **RAM console.** Reserve a page at a fixed address (the E6 uses `0x1f000000`, `earlycon=ramcon,...`) and read it back from Symbian after a watchdog reset. This is how every early crash was debugged.
4. **Interrupts and timer.** The controller is at `0x20400000` and the timer comes from the clock/power block `0x20033000`. These are SoC-level and probably identical. See the live checks in [`driver-re.md`](driver-re.md).
5. **Watchdog and PMIC.** These are board-level. A wrong CBUS write to the PMIC powers the phone off within about 2 seconds, which happened once. Take the PMIC access from your ROM's `karap`, byte for byte.
6. **USB gadget shell.** MUSB is at `0x20038800`. Connect USB before the handoff, so Symbian leaves the block powered.

## 3. The display

The display stack is described in [`videocore-display.md`](videocore-display.md) (overview) and [`videocore-link.md`](videocore-link.md) (wire level). Below is what changes per device or per firmware.

### 3.1 Device-specific values

| Value | E6 | Where it comes from | Likely to differ? |
| --- | --- | --- | --- |
| ViSSI host bus base | `0x20025800` (PIO), DMA side `0x20024800`, irq `0x92` | `vcdriver.dll` init `FUN_8032a368` / `FUN_8032a154` | Same SoC: unlikely |
| Bus clock | power resource `0x18` (`EHwClkViSSI`), gate `0x20033090` bit 5 | `vcdriver.dll`, PRM requests | Unlikely |
| Bus timings | boot `0x08008080/0x023c03c3`, normal `0x05008050/0x017c01c3` | Computed from the AHB clock (`FUN_8032a6b4`) | If your AHB clock differs |
| RUN gpio (VC out of reset) | 92 | `vcdriver.dll` gpio config | **Board-specific** |
| HAT gpio (VC attention) | 63 (the driver polls the bus status instead) | `vcdriver.dll` | Board-specific |
| Bus pin mux | gpio ids `0x6a..0x79`, `0x7c..0x7e` | `FUN_8032a3c8` | Board-specific |
| Regulators | `0x30`=9, `0x34`=2, `0x2f`=2, `0x2d`=4 | `vcdriver.dll` power path | Board-specific |
| VC firmware images | loader A `0x8022d5e8`, loader B `0x8022f638`, main `0x80231688` (+ 20-byte trailers) | `unsigned_2727b1.dll`, image set registered by entry `0x8022d4b4` | **Every firmware version** |
| Panel id | `01 8b 79` (DCS `da db dc`) | Read it from your panel (section 3.3) | **Per model, even per batch** |
| Panel config (DSPC cmd 3) | 640x480, format 2, porches 10/20/2 and 3/3/3, 20 MHz pixel, 2 MHz command clock | Panel database `FUN_80349e58`, case `0x79` built on `0x72` | **Per panel** |
| DSPC interface id | 4 (RGB/video form). The other one in the table is 6 | Chipset table `0x8034eaa4` | Per panel |
| Panel DCS sequences | Cold and warm power-up, refresh, op 8 | The emulator, see section 3.4 | **Per panel** |
| Framebuffer format | resource type `0x2b`, image type `0x20008`, 32 bpp, pitch `0xa00` | `FUN_80345920(4)`, `FUN_80345974(4)` | Per panel or resolution |

The Linux side keeps these values in `linux/boot/e6-vc.dts` (bus, clock, RUN gpio) and at the top of `drivers/soc/rapuyama/videocore.c`:
- `VC_W`, `VC_H`, `DSPC_ID`, `VC_IMG_*`
- `dspc_config()`
- the DCS tables in the panel power-up functions

### 3.2 Extract the VideoCore firmware

`tools/vc_extract.py` has the E6 (RM-609 111.140.0058) addresses hardcoded.
For another ROM:

1. Find `unsigned_2727b1.dll` with `tools/rom_walk.py` and note its entry point.
2. In Ghidra, the entry registers a "vcexe" image set. At `+0x04`, `+0x10` and `+0x1c` it holds `{address, size, trailer}` for loader A, loader B and the main image.
3. Put the addresses into `IMAGES` in `vc_extract.py`.

Sanity checks:
- On the E6 both loaders are `0x2038` bytes and the main image is `0xf6aab` bytes.
- The VC acks each image with type 3 value 1. The driver logs the answers as `loader answered 3/1`.
- `1/1` means the VC sent its hello again, i.e. the image was rejected.

Never commit the extracted images. They are Nokia's.

### 3.3 Talk to the panel before writing any driver code

Boot your kernel with the driver and `videocore.debug=2`. Even if nothing shows, the log contains the following:
- the VC boot (`VideoCore booted`)
- the VCHI connect
- the panel reads: `panel status: 0a=.. 0b=.. 0c=.. 0d=.. 0e=..` and `panel id .. .. ..`

If the id is not `01 8b 79`, your panel is different, and the config and DCS sequences must come from your ROM (next step). `vcprobe` does the same from userspace through `/dev/mem`, which is handy for poking single DSPC commands by hand. Its source is `linux/initramfs/vcprobe.c`, and `build-vc.sh` puts it into the initramfs as `/usr/bin/vcprobe`. The probe and the driver must not drive the bus at the same time: boot the `build-vc` kernel with the plain `e6-usb.dtb` (no `videocore` node, so the driver does not probe), then run `vcprobe light /lib/firmware/vc ffffff i`.

### 3.4 Capture your phone's Symbian sequence with the emulator

Do not reconstruct the panel sequence from the decompilation by reading it. Ghidra inverted a branch condition, and the state machine depends on what the panel answers. Run Nokia's code instead: [`tools/vcemu/`](../tools/vcemu/).

`run_panel.py` is written against the E6 ROM. For another ROM, re-find the following in Ghidra. They live in `dispcommanddrv.dll`, `display_chipset_drv_ive_bc.dll`, the LCD/panel driver, `vcdispman.dll`, `vc_bufmanx` and `vchi.dll`:

| `run_panel.py` item | E6 address | What it is |
| --- | --- | --- |
| `stub_ranges` | everything outside `0x80340000..0x80361fff` | Code that runs natively = the display modules; the rest is stubbed |
| `CTOR_LISTS` | 5 ranges | Static constructor tables of those modules |
| `0x8034c21c` | | Display interface registry setup |
| `0xc80124ec`, `0xc8012828` | | Panel id of display 0, type of display 1 |
| `0x8034e584`, `0x8034f2d0`, `0x8034f1c8` | | Chipset init, LCD owner constructor, install |
| `0x80353b00` | | Request entry point (op 1 init, 2 off, 3 refresh, 8, 10 ...) |
| `0x80336d14`, `0x80333454` | | VCHI service open (stubbed) and the DSPC send (logged) |
| `0x80345168`, `0x803450d8` | | dispman sync/async send (logged) |
| kernel stubs (`0x8008…`, `0x8006…`, `0x8005…`, `0x8009…`) | | DFCs, timers, physical RAM, ReAlloc: in ekern, found via `ekern_exports.txt` |

Then run it with the values your real panel returned in step 3.3:

```sh
KERN_BIN=/path/to/kern.bin PANEL_0A=<your 0x0a value> python3 tools/vcemu/run_panel.py
```

Also update `PANEL_ID` for your id. Try both a cold value (`00`) and your real one, then diff the two. The black screen came from exactly this: the first capture answered `0x0a` with `9c` (panel already on), so it recorded Symbian's warm path. On real hardware after a VC reboot the cold path is the one that works. Timer ticks in the log are milliseconds: keep the delays.

### 3.5 Change the driver

1. Board values go into the DTS node: `reg`, `clocks`, `run-gpios`.
2. Panel config goes into `dspc_config()`, and the size into `VC_W`/`VC_H`.
3. The DCS tables go into the panel power-up functions (`bl_wake`, `panel_mode`, op 8, the refresh batches). Copy the bytes and the delays from your capture, in the same order. Do not tidy them up.
4. Bring up with `videocore.debug=1` and `videocore.test_pattern=1`. Expect `display up` and colour bars.

## 4. Lessons from the E6

- **Replicate, don't guess.** Every "reasonable" tweak, such as an identity colour matrix or our own sleep-out, cost a round trip and fixed nothing. The sequence that worked was the one captured from Nokia's code with realistic inputs.
- **Inputs matter as much as code.** An emulator that answers the hardware reads wrongly produces a perfect copy of the wrong path.
- **Every bus transfer ends with channel word 0.** Without it the VC waits forever for the end of the image.
- **The VC tracks credits.** You may not send before the VC's CTRL connect, and rx slots must be given back.
- **Service names are fourccs exactly as on the wire.** The buffer manager is `BFMX`, not `BUFM`. A wrong name gives reply flags 0 ("not available").
- **Loader B was rejected on the E6.** Use loader A.
- **Panel reads are cheap.** `0x0a` is the DCS power mode: bit 7 booster, bit 4 sleep out, bit 3 normal mode, bit 2 display on. `1c` with a black screen means the panel is not being driven.
- **1.8 V logic** on the debug UART pads. A 3.3 V adapter can kill them.

## 5. Contributing a port

Please send:
- your DTS (or the diff to `e6-vc.dts`)
- the values from table 3.1 with the ROM function you took each one from
- your `run_panel.py` address table and capture
- a boot log

Mark every value as **live-checked** or **from the ROM only**, the same way the existing notes do.

## Secure watchdog after handoff (E6, confirmed 22.10.2026)

Feeding the SoC and PMIC watchdogs is insufficient on the E6: a third watchdog is serviced by the secure PA_SWD application, command `0x1f`, with a 240-second timeout. The E6 Linux bridge uses the preserved firmware context and rearms at timeout minus 30 seconds; enable it with `e6.swd=1`. Kernel #196 completed four successful calls through 648 seconds on hardware ([report and evidence](modem-reset-findings.md)).

The bridge checks `nokia,e6`, the ISAH handoff and firmware entry signatures. Its addresses, page tables, shared-buffer pool, protected-application header and borrowed task stack come from the captured E6 runtime. Resolve and validate those separately for another firmware or device before extending the bridge. The current E6 validation does not establish compatibility with other RapuYama phones.