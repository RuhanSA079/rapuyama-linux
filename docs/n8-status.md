# Nokia N8 (RM-596): hardware status under Linux

State on 23.10.2026, Symbian Anna 024.001, `n8boot` loader, E6 kernel with the N8 additions (`tools/build-n8-kit.sh n8` or `linux/kernel/build-vc.sh` for the display kernel), one device tree `linux/boot/n8.dts` with everything marked ✅ below. Hardware details and evidence are in [`n8-hardware.md`](n8-hardware.md).

| Status | Meaning |
| --- | --- |
| ✅ works | seen working on the N8 |
| 🧪 test | built and in the kit, not run on the N8 yet |
| 🔧 todo | the hardware and the way to it are known, the Linux side is missing |
| 🔬 research | needs reverse engineering first, the way to it is still open |

## Core

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Boot from Symbian | `n8boot.sis` (e6bfix built for `E:\n8.dtb`) | ✅ works | Needs a 32 MB+ RAM block starting below `0x1a000000`; the N8 hands out 96 MB at `0x19a00000`, the end check at `0x1f000000` had to go. Reads `Image` + `n8.dtb` from E: or the memory card (asks when both have them), blanks the Symbian display at the handoff (`E:\keepscreen.txt` skips that) |
| CPU | ARM1176JZF-S r0p4, 677 BogoMIPS | ✅ works | |
| RAM | 93 MB for Linux | ✅ works | `e6.lowmem` (the RAM below the kernel) not tried on the N8 |
| Timers, interrupts | RapuYama intc + timers | ✅ works | |
| USB device: serial shell | MUSB + g_serial, `/dev/ttyACM*` on the PC | ✅ works | |
| SoC + PMIC watchdog | `rapuyama-wdt` | ✅ works | |
| Secure watchdog (PA_SWD) | modem / secure side | 🔬 research | Linux is reset back to Symbian after 0.5..4 min. The E6 bridge (`e6.swd=1`) uses addresses from the E6 firmware, they have to be found in the N8 ROM |
| RAM log after a reset | `ramcon` at `0x1f000000`, `e6logs.sis` | ✅ works | Only after a reset: when the phone powers off, the RAM is lost |
| `reboot` back to Symbian | soc reset `0x20033114` + startup mode in `0x20040400` bits 20-23 | 🧪 test | Symbian left the mode at 0, then the reset powers the N8 off. Setting it to `0xe` first (as `rebootdriver.ldd` does) brought Symbian back (devmem test). Now done by the restart handler with the 4th watchdog reg in the N8 dtbs |
| Internal eMMC (`E:`, 16 GB) | `mmcblk1`, Samsung MAG4EM | ✅ reads | Stuck in 1-bit mode (`switch to bus width 4 failed`). One FAT32 filesystem, no partition table. **Never write to it** |
| microSD | 2nd MMC controller `0x20032800`, through the GAZOO level shifter | ✅ works | Card found at boot (`mmcblk0`, SDHC 29.1 GiB, `mmcblk0p1`), 24 MHz, 4 bit, 3.3 V. Same setup as the E6 (polled, no card detect line). The earlier "no `mmcblk0`" was an empty slot. Rootfs on the card (`tools/sdroot.sh`, `e6.root=`) not tried on the N8 |
| USB host / OTG | MUSB | 🔧 todo | Only the device side is set up |

## Input and sensors

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Side keys (volume, menu, camera half/full, lock slider) | gpio 2, 65, 1, 38, 36, 79 | ✅ works | `gpio-keys-polled`. Needed the gpio driver fix: levels come from the irq block raw state `0x20033408 + 0x40 * k` |
| Power key (top) | `PwrOnX` on the GAZOO PMIC | 🔬 research | PMIC interrupt + status over CBUS. Linux's `retu-pwrbutton` is the same idea on Nokia's older PMIC |
| Touchscreen | Synaptics ClearPad TM1284, RMI4 on i2c-0 `0x4b` | ✅ works | Mainline `rmi4` (F01 + F11) with a polling mode (15 ms), the ATTN gpio has no irq. 2 fingers. Orientation matched to the panel with `fbtouch` (`touchscreen-inverted-y` + `-swapped-x-y`): x 0..1756 left to right, y 0..3155 top to bottom |
| Accelerometer | ST LIS302DL, i2c-0 `0x1d` | ✅ works | `st_accel`, ~9.7 m/s2 lying flat |
| Magnetometer | AK8974, i2c-0 `0x0f` | ✅ works | Reads ~1 G total: needs calibration for a compass |
| Ambient light | TSL2563, i2c-1 `0x29` | ✅ works | |
| Proximity | N1104, output on gpio 3 (VCC = VAUX1) | 🧪 test | `gpio-keys-polled` switch `SW_FRONT_PROXIMITY`; gpio 3 confirmed by `proximity.pdd`, near = high is inferred (check with `keytest`). See `work/leds-n8/NOTES.md` |

## Power

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Battery voltage / temperature | PMIC ADC (`rapuyama-battery`) | ✅ works | 4.08 V, 36.8 C |
| Charging | 2x bq2415x on i2c-1, `0x6a` (bq24156) / `0x6b` (bq24153) | 🔧 todo | The E6 drives the same bq24153 at `0x6b` with `bq2415x_charger` and charge limits, kept off on the N8 for now: the second charger and the N8 limits need checking first. Boot with a charged battery |
| Vibra | from the GAZOO PMIC (VibraP/N) | 🔬 research | PMIC register over CBUS |

## LEDs and flash

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Menu key light, indicator LED | LED driver N2402 = **TI LP5521** on i2c-1 `0x33`: R output -> V2420 + V2422, B output -> V2411, G unused | 🧪 test | Mainline `leds-lp5521` (ISA software has `hal_light_phy_lp5521.c`, the pinout matches). Currents capped at 5 mA until Symbian's value is read on the phone. See `work/leds-n8/NOTES.md` |
| Charging LED | V2410 on the GAZOO `Chind` pin (schematic p5) | 🔬 research | Not on the LP5521: driven by the PMIC's charger indicator, probably the LED at the USB port |
| Camera flash | Xenon flash + a separate LED (`ILED`), camera page of the schematic | 🔬 research | Probably switched together with the camera, through the VideoCore side |

## Display and graphics

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| VideoCore boot | BCM2727, firmware from the N8 ROM (`vcdriver.dll`) | ✅ works | With **loader B** (`videocore.loader_b=1`, `0x802f0a40`): `loader answered 3/1`, `VideoCore booted`, VCHI, dispman and full-frame bulk transfers work. Loader A is refused (`1/1`). Comes up at ~9 s, no start delay needed any more (`videocore.start_delay=30` was used while the panel setup was wrong). `n8.dts` |
| LCD panel | 360x640 MIPI DSI, panel id **`fe 94 80`**, behind the VideoCore on interface 6 | ✅ works | `videocore.panel=n8` (kernel patch 0007): the setup captured from the N8 ROM with `tools/vcemu` ported to the N8 (`work/vcemu-n8/`). Panel answers `0a=9c 0b=d4 0c=07`, no DSI errors, `display up, 360x640`, ~4.8 fps with PIO pushes; with `dma=1 rgb565=1` (in `n8.dts`) **weston runs on the panel**, keys and touch work in it. The Samsung block (Nokia: id `fe 80 xx` only) is not sent for this id |
| Backlight | inside the panel, DCS brightness (`51`) | 🧪 test | Comes on with the panel; `videocore.brightness` sets the `51` value |
| GLES 2 (`libvcgl`) | VideoCore GL server | ✅ works | `vctri` draws its rainbow triangle on the panel at ~25 fps (E6: 170-250; the N8 DSI panel takes every frame over one lane). Two timeouts at the start (`KHRN message 0x1`, `DISP 0x80000012`) don't stop it. `libvcgl` takes the screen size from fb0 |
| HDMI out | from the VideoCore | 🔬 research | |

## Radios

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| WLAN | TI WL1271 PG2.0 on spi2 (`0x20020600`), IRQ genio73, enable genio100 (Linux gpio 612) | ✅ scans | 24.10.2026: `wlcore` loads, `wlan0` with the phone's MAC (NVS), firmware `wl127x-fw-5-sr` boots (Rev 6.3.10.0.142). Needs: n8boot's WLAN clock step (`E:\wlansteps.txt` = 3: PRM 0x25 = the modem-gated 19.2 MHz reference clock, then genio100 high, 500 ms), kernel patches 0008-0011, `WLAN=1` build, `n8-wlan-keep.dtb`, `rootfstype=ramfs`. The genio73 interrupt never reaches the CPU (no parent irq); polled gpio irqs (patch 0012) in rising, falling and level-low mode didn't deliver the firmware's events either (scans end "Scan completed due to error" with EVENT_A/DATA pending in the chip). Patch 0013 (`wlcore.irq_poll_ms=10`, in `n8-wlan.dtsi`) runs wlcore's interrupt handler off a timer, reading the chip's interrupt status over SPI. ELP wakeup also times out: runtime PM must stay off (`wlantest` writes `on` to `power/control`). Reference clock 38.4 MHz (`ref-clock-frequency`), as the phone's INI says **24.10.2026: `iw dev wlan0 scan` lists the access points** (2.4 GHz, -56 dBm), with patch 0013 (`wlcore.irq_poll_ms=10`), runtime PM off (`wlantest`), 38.4 MHz reference clock, `n8-wlan-keep.dtb`. Connect (WPA2 + DHCP) not tested yet |
| Bluetooth | BT/FM chip on a UART (`BTUART*`), reset gpio 54, wake gpio 53 | 🔧 todo | Needs a full RapuYama UART driver (only the early console exists, registers documented), then `hci_uart` |
| FM radio / FM transmitter | in the BT/FM chip, FMIRQ gpio 23 | 🔬 research | Didn't answer on i2c-1, probably powered down |
| GPS | separate module on I2C1, `GPS_EN_RESET`, `GPS_CLK` | 🔬 research | Proprietary protocol, no Linux driver |
| Cellular modem | integrated modem side (ISA) | 🔬 research | Not supported on the E6 either |

## Audio

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Earpiece, headset, codec | in the GAZOO PMIC | 🔬 research | The audio data path probably runs through the modem side |
| Loudspeaker amp | N2150, enable `IHF_AMP_EN` | 🔬 research | Needs the audio path |
| Headphone amp | N2000, TPA6130A2-like on i2c-1 `0x60` | 🔧 todo | Mainline driver exists, useless without the audio path |
| Microphones | GAZOO codec | 🔬 research | |

## Camera

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Main camera (12 MP) | CCP2 to the VideoCore, which does the ISP / encoding | 🔬 research | Driven over VCHI (`dcam*.dll`, `camldd.ldd`): needs the display / VCHI link first, then the camera protocol |
| Front camera | | 🔬 research | |

## Suggested order

1. Secure watchdog for the N8 firmware (`swddump`), so Linux stays up longer than ~4 minutes and a watchdog reset returns to Symbian.
2. LEDs (`leds-lp5521` at i2c-1 `0x33`) and proximity sensor (in n8.dts, to test), power key.
3. Charging (bq2415x, as on the E6).
4. Bluetooth (UART driver), then WLAN (SPI driver).
5. Camera, audio, GPS.
