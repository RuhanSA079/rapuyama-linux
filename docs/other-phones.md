# Testing on the Nokia N8 and E7

The N8 and E7 use the same RapuYama SoC and BCM2727 VideoCore as the E6, so the E6 kernel and loader should run on them. **Nobody has tried yet (23.10.2026).** These builds exist to find out how far they get. Each test is one boot and ends with a reset back to Symbian. Nothing is written to the phone's internal memory, except the files you copy there yourself and two log files.

## What changes against the E6

| Part | E6 | N8 / E7 test build |
| --- | --- | --- |
| Kernel | `Image` | the same `Image` |
| Loader | `e6bfix.sis` | the same `e6bfix.sis` (only uses SoC registers, no E6 firmware addresses) |
| Device tree | `e6-vc-565.dtb` | `n8-usb.dtb` / `e7-usb.dtb` (test 1), `n8-vc.dtb` / `e7-vc.dtb` (test 2) |
| Display | 640x480 LCD, driver set up for it | test 1: off, the Symbian picture just freezes. test 2: the E6 setup (640x480, standard DCS only), the picture is probably wrong, the log tells us what the panel is |
| Keypad / touch | i2c 0x44 / 0x4a | off (the chips at those addresses may be different ones on these boards) |
| 240 s reset | serviced by `rapuyama-swd` (`e6.swd=1`) | off: its addresses come from the E6 firmware. The phone resets back to Symbian 0.5..4 min after the boot |
| Charging | `e6-vc-565-charger.dtb` | off. Linux does not charge, boot with a charged battery |
| Rootfs on the SD card | `e6.root=` | off, everything runs from the initramfs |
| RAM below the kernel | `e6.lowmem=1` | off (~100 MB free) |

## You need

- An N8 or E7 on Symbian Anna or Belle, **hacked**: the installer must accept self-signed packages with all capabilities (the usual RomPatcher+ "Open4All" + "Installserver" patches, e.g. after the Norton hack). Without that the SIS files don't install.
- A charged battery.
- A USB cable and a Linux PC with `picocom` (the shell is a USB serial gadget, it shows up as `/dev/ttyACM0`).
- Python 3 on the PC for the log tool `parse_klog.py` (`tools/parse_klog.py` in this repo).
- Before you start, write down the model code and firmware version: dial `*#0000#` (e.g. `RM-596 111.040.1511`).

## Files

| File | Where it goes |
| --- | --- |
| `Image` | phone `E:\Image` (mass memory root) |
| `n8-usb.dtb` / `e7-usb.dtb` | phone `E:\e6.dtb` for test 1 (the loader always reads that name) |
| `n8-vc.dtb` / `e7-vc.dtb` | phone `E:\e6.dtb` for test 2 |
| `e6bfix.sis` | install on the phone: the loader, icon "e6bfix" |
| `e6logs.sis` | install on the phone: saves the Linux log after the reset, icon "e6logs" |
| `parse_klog.py` | PC: turns `klog.bin` into text |
| `SHA256SUMS` | check the files: `sha256sum -c SHA256SUMS` |

## Test 1: boot to the USB shell

1. Install `e6bfix.sis` and `e6logs.sis`.
2. Connect the phone in mass storage mode, copy `Image` to `E:\Image` and your phone's `*-usb.dtb` to `E:\e6.dtb` (rename it). Disconnect safely.
3. Plug the USB cable in again and pick **Nokia Suite / PC Suite** mode, **not** mass storage (in mass storage mode the phone can't read `E:` and the loader fails). The cable has to be in before the boot, so Symbian leaves the USB block powered.
4. Close other apps, start **e6bfix**. It loads the kernel (about a minute, "loading Image N %"), then asks "ram ..., kernel ... kb, boot linux?". Press **Boot**. If it says "no free ram block", close apps and press Retry.
5. The screen freezes on the Symbian picture, that's expected. On the PC, within ~20 s:
   ```sh
   dmesg | tail          # expect a new ttyACM0 (Linux "Gadget Serial")
   picocom -b 115200 /dev/ttyACM0
   ```
   Press Enter, you should get a `#` shell. Run and save the output:
   ```sh
   uname -a; cat /proc/cpuinfo; free -m; cat /proc/partitions
   dmesg
   ```
   `dmesg` is the most important part, copy all of it.
6. Within ~4 minutes the phone resets and Symbian boots again. Then **start e6logs right away** (before anything else uses the RAM). It writes `E:\klog.bin`.
7. Copy `E:\klog.bin` and `E:\boot.log` to the PC and run `python3 parse_klog.py klog.bin > klog.txt`.

If nothing shows up on USB: wait 5 minutes, if the phone doesn't come back by itself take the battery out. Then step 6 and 7 anyway, the RAM log shows how far Linux got.

## Test 2: display (only after test 1 worked)

Same as test 1, but with `n8-vc.dtb` / `e7-vc.dtb` as `E:\e6.dtb`. About 2 s after the boot the driver restarts the VideoCore: the Symbian picture goes away. After that the screen may stay black, show garbage or show a shifted console, all of that is useful. In the shell:
```sh
dmesg | grep -i videocore
```
We need the lines `VideoCore booted`, `panel status: ...`, `panel id .. .. ..` and `panel power-up: ...`. The driver only sends standard DCS commands (sleep out, display on, pixel format, brightness), no panel voltage registers. If the screen shows a bright fixed picture for a long time, take the battery out anyway.

## Test 3 (N8): keys, touch and sensors

`tools/build-n8-kit.sh n8` also puts `phone-E-drive/alt/n8-hw.dtb` in the kit: the N8 chips `i2cdetect` found (maXTouch @0x4b, LIS302DL, AMI305 / AK8974, TSL2563, [n8-hardware.md](n8-hardware.md)) plus `gpio-keys-polled` for the side keys. Copy it as `E:\n8.dtb` and boot as in test 1. The initramfs has three test tools:

| Tool | What it shows |
| --- | --- |
| `keytest` | Key devices and the keys they have (held ones marked), then every press / release / repeat. Volume up/down, menu, camera, camera focus and the lock slider should all show up |
| `touchtest` | The touch axis ranges, then one line per frame with every finger. On the last finger up it prints the x/y range that was really touched: drag along all four edges, then compare that with the 360x640 panel to see if the axes are swapped or mirrored |
| `sensortest` | Every IIO sensor and the battery in real units, every 500 ms (`sensortest 200 10`: every 200 ms, 10 samples; `sensortest list`: the channels). Lying flat, the accelerometer z axis should read about +-9.8 m/s2 |

Stop each one with ctrl-c. They are also in the display kernel (`build-vc.sh`). `keyscan` is the level below `keytest`: raw gpio pins, without the `gpio-keys` driver.

## Don't

- Don't write to the internal memory from Linux. `mmcblk*` there is the phone's mass memory (`E:`) and possibly a memory card: no `mkfs`, no `mount -o rw`, no `dd` onto it. Read only.
- Don't run `/tmp/e6-check`, it checks E6 charger hardware that is off in these builds.
- Don't use `e6boot.sis` / `e6calb.sis`, only `e6bfix.sis`.

## What to send back

- model code + firmware version (`*#0000#`)
- which test, which dtb, what the screen did
- the shell output (step 5), `klog.txt`, `boot.log`
- for test 2, the `videocore` lines

## Next steps once this boots

- display: the panel config (`dspc_config()`, `VC_W`/`VC_H`) and the RUN gpio for the N8/E7 panel, from that phone's firmware, see [`porting-guide.md`](porting-guide.md) section 3
- touch and (E7) keyboard: find the chips and their i2c addresses
- the 240 s secure watchdog: the bridge's addresses for that firmware, see [`modem-reset-findings.md`](modem-reset-findings.md)
- optional live console: the N8 board has the bluetooth uart test pads J6354 / J6355 (1.8 V), see [`../linux/boot/README.md`](../linux/boot/README.md)

## Build from source

The kernel is the normal E6 one (`linux/kernel/build-vc.sh`, see [`../linux/boot/README.md`](../linux/boot/README.md)), the loader `RB_DEFS=-DRB_IMAGE_BOOT ./build_app.sh e6bfix boot_app.cpp 0xe1000024` and `./build_app.sh e6logs ramread_app.cpp 0xe1000022` in `linux/stage0/symbian-loader`. The device trees:
```sh
for v in n8-usb n8-vc e7-usb e7-vc; do
	dtc -I dts -O dtb -i linux/boot -o $v.dtb linux/boot/$v.dts
done
```
On Debian / Ubuntu, `tools/build-n8-kit.sh [n8|e7]` does all of the above for test 1 (USB shell kernel, `*-usb.dtb`, both SIS files) and puts the kit with `SHA256SUMS` in `out/<phone>-test1/`. Its loader is `n8boot.sis` / `e7boot.sis` instead of `e6bfix.sis`: the same loader, built with `-DRB_DTB` so it reads `E:\n8.dtb` / `E:\e7.dtb` instead of `E:\e6.dtb` (no renaming to `e6.dtb`). N8 hardware read off the schematics (keys, I2C buses, touch, display, UART pads) is in [`n8-hardware.md`](n8-hardware.md).
