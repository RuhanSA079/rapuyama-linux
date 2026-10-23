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
| Boot from Symbian | `n8boot.sis` (e6bfix built for `E:\n8.dtb`) | ✅ works | Needs a 32 MB+ RAM block starting below `0x1a000000`; the N8 hands out 96 MB at `0x19a00000`, the end check at `0x1f000000` had to go |
| CPU | ARM1176JZF-S r0p4, 677 BogoMIPS | ✅ works | |
| RAM | 93 MB for Linux | ✅ works | `e6.lowmem` (the RAM below the kernel) not tried on the N8 |
| Timers, interrupts | RapuYama intc + timers | ✅ works | |
| USB device: serial shell | MUSB + g_serial, `/dev/ttyACM*` on the PC | ✅ works | |
| SoC + PMIC watchdog | `rapuyama-wdt` | ✅ works | |
| Secure watchdog (PA_SWD) | modem / secure side | 🔬 research | Linux is reset back to Symbian after 0.5..4 min. The E6 bridge (`e6.swd=1`) uses addresses from the E6 firmware, they have to be found in the N8 ROM |
| RAM log after a reset | `ramcon` at `0x1f000000`, `e6logs.sis` | ✅ works | Only after a reset: when the phone powers off, the RAM is lost |
| `reboot` back to Symbian | soc reset `0x20033114` + startup mode in `0x20040400` bits 20-23 | 🧪 test | Symbian left the mode at 0, then the reset powers the N8 off. Setting it to `0xe` first (as `rebootdriver.ldd` does) brought Symbian back (devmem test). Now done by the restart handler with the 4th watchdog reg in the N8 dtbs |
| Internal eMMC (`E:`, 16 GB) | `mmcblk1`, Samsung MAG4EM | ✅ reads | Stuck in 1-bit mode (`switch to bus width 4 failed`). One FAT32 filesystem, no partition table. **Never write to it** |
| microSD | 2nd MMC controller, through the GAZOO level shifter | 🧪 test | No `mmcblk0` seen, probably no card inserted |
| USB host / OTG | MUSB | 🔧 todo | Only the device side is set up |

## Input and sensors

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Side keys (volume, menu, camera half/full, lock slider) | gpio 2, 65, 1, 38, 36, 79 | ✅ works | `gpio-keys-polled`. Needed the gpio driver fix: levels come from the irq block raw state `0x20033408 + 0x40 * k` |
| Power key (top) | `PwrOnX` on the GAZOO PMIC | 🔬 research | PMIC interrupt + status over CBUS. Linux's `retu-pwrbutton` is the same idea on Nokia's older PMIC |
| Touchscreen | Synaptics ClearPad TM1284, RMI4 on i2c-0 `0x4b` | ✅ works | Mainline `rmi4` (F01 + F11) with a new polling mode (15 ms), the ATTN gpio has no irq. 2 fingers, x 0..3155 along the long side, y 0..1756: swapped against the 360x640 portrait panel. With `touchscreen-inverted-x` + `-inverted-y` + `-swapped-x-y`: (0,0) bottom left next to the menu key, max top right (y runs bottom to top, to be matched to the panel once it works) |
| Accelerometer | ST LIS302DL, i2c-0 `0x1d` | ✅ works | `st_accel`, ~9.7 m/s2 lying flat |
| Magnetometer | AK8974, i2c-0 `0x0f` | ✅ works | Reads ~1 G total: needs calibration for a compass |
| Ambient light | TSL2563, i2c-1 `0x29` | ✅ works | |
| Proximity | N1104, output on gpio 3 | 🔧 todo | One more `gpio-keys` entry (as a switch), level read like the keys |

## Power

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Battery voltage / temperature | PMIC ADC (`rapuyama-battery`) | ✅ works | 4.08 V, 36.8 C |
| Charging | 2x bq2415x on i2c-1, `0x6a` (bq24156) / `0x6b` (bq24153) | 🔧 todo | The E6 drives the same bq24153 at `0x6b` with `bq2415x_charger` and charge limits, kept off on the N8 for now: the second charger and the N8 limits need checking first. Boot with a charged battery |
| Vibra | from the GAZOO PMIC (VibraP/N) | 🔬 research | PMIC register over CBUS |

## LEDs and flash

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| Menu key light, notification / white indicator LEDs | LED driver N2402 on i2c-1 `0x33` (LEDs V2420, V2422, V2411) | 🔧 todo | Answers at `0x33`, LP5523 address range: try mainline `leds-lp5523`. Which channel is which LED has to be found by trying. The white LED at the USB port may also be the charger's status LED |
| Camera flash | Xenon flash + a separate LED (`ILED`), camera page of the schematic | 🔬 research | Probably switched together with the camera, through the VideoCore side |

## Display and graphics

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| VideoCore boot | BCM2727, firmware from the N8 ROM (`vcdriver.dll`) | ✅ works | With **loader B** (`videocore.loader_b=1`, `0x802f0a40`): `loader answered 3/1`, `VideoCore booted`, VCHI, dispman and full-frame bulk transfers work. Loader A is refused (`1/1`). Comes up at ~9 s, no start delay needed any more (`videocore.start_delay=30` was used while the panel setup was wrong). `n8.dts` |
| LCD panel | 360x640 MIPI DSI, panel id **`fe 94 80`**, behind the VideoCore on interface 6 | ✅ works | `videocore.panel=n8` (kernel patch 0007): the setup captured from the N8 ROM with `tools/vcemu` ported to the N8 (`work/vcemu-n8/`). Panel answers `0a=9c 0b=d4 0c=07`, no DSI errors, `display up, 360x640`, ~4.8 fps with PIO pushes; with `dma=1 rgb565=1` (in `n8.dts`) **weston runs on the panel**, keys and touch work in it. The Samsung block (Nokia: id `fe 80 xx` only) is not sent for this id |
| Backlight | inside the panel, DCS brightness (`51`) | 🧪 test | Comes on with the panel; `videocore.brightness` sets the `51` value |
| GLES 2 (`libvcgl`) | VideoCore GL server | 🔬 research | Works on the E6, follows once the display works |
| HDMI out | from the VideoCore | 🔬 research | |

## Radios

| Part | Hardware | Status | Notes |
| --- | --- | --- | --- |
| WLAN | TI WL127x on SPI (gpio 74-77), IRQ gpio 73, enable on ball G9 | 🔧 todo | `wlcore` / `wl12xx` exist. Missing: a RapuYama SPI controller driver, the enable gpio, power / sleep clock, NVS calibration file |
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

1. Touch orientation against the panel (graphical test), GLES (`vctri`), frame rate with DMA.
2. LEDs (`leds-lp5523` at i2c-1 `0x33`), proximity sensor, power key.
3. Charging (bq2415x, as on the E6).
4. Secure watchdog for the N8 firmware, so Linux stays up longer than ~4 minutes.
5. Bluetooth (UART driver), then WLAN (SPI driver).
6. Panel bring-up, then GLES, camera, audio.
