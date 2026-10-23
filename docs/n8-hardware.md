# Nokia N8-00 (RM-596) hardware notes from the schematics

Source: "Nokia N8-00 Schematics" (<https://bbs.aw-ol.com/assets/uploads/files/1631158689295-nokia-n8.pdf>), Nokia Care Academy, board 3CE_14 v1.0,
15.07.2010, 11 A3 pages. These are real schematics, not just a block diagram.
Page refs = PDF page (p3 = "RAPS, Imaging processor, Display IF, Touch IF, HDMI",
p4 = "RAPS, Mass memory, Combo memory, Sensors, LEDs, UI keys", p5 = "GAZOO,
Battery, HS USB, SIM, Power key", p6 = "GAZOO, Audio, AV connector, MMC",
p7 = "BTH FM, WLAN, GPS").

Legend: **[S]** = read straight off the schematic. **[G]** = inference/guess, verify on hardware.
The schematics name parts only by reference designator (N1103, ...), **no part numbers
and no I2C addresses** are printed. RapuYama pins are given as ball + function name, the
`genioNN` name is printed for most but not all of them.

## I2C buses (net names from the schematic)

The schematic calls them I2C0/I2C1/I2C2. Which Linux controller (`i2c0@20032200`,
`i2c3@20032500` hs-i2c, ...) each one is, is **not** in the schematic.

| Net | RapuYama pins | Pull-ups | Devices | Page |
| --- | --- | --- | --- | --- |
| I2C0 | Genio28 (D16) = SCL, Genio27 (D15) = SDA | R2852/R2853 3k3 to VIO 1.8 V | touch panel flex X2500 (through level shifter N2500, 1.8 V <-> VAUX2 2.5 V), 3D accelerometer N1103, 3D magnetometer N1105 | p4 [S] |
| I2C1 | GenIO29 (G13) = SDA, GenIO30 (C13) = SCL | R2850/R2851 3k3 | GPS module only | p3 [S] |
| I2C2 | I2C2SDA (U15), I2C2SCL (U16) (dedicated pins, no genio) | R2804/R2805 3k3 | LED driver N2402, ALS V1100, FM radio (FMRADIO bus), headphone amp N2000 (AV bus), charger N3350, USB charger N3301, camera side (p2, N3201 area) | p4, p5, p6 [S] |

Likely parts/addresses for those devices (all **[G]**, from the pinout shapes):

| Ref | Function | Pin evidence | Likely part / 7-bit addr |
| --- | --- | --- | --- |
| X2500 | touch controller on the panel flex | signals ATTN, SCL, SDA, VDD | "ATTN" is Synaptics naming -> Synaptics ClearPad (RMI). Address unknown (Synaptics often 0x20 / 0x2c). **Not the E6's maXTouch @0x4a** |
| N1103 | 3D accelerometer | INT1/INT2, SDA/SDI/SDO, SCL/SPC, SDO, CS, Vdd_IO, Res | ST LIS302DL/LIS3xx. CS tied high (I2C mode), SDO tied to Vdd_IO -> SA0=1 -> **0x1d** (LIS302DL) or 0x19 (LIS3DH) |
| N1105 | 3D magnetometer | INT, DRDY, DVDD, AVDD | AKM AK8974 / Aichi AMI30x family (0x0e / 0x0f) |
| V1100 | ambient light sensor | SDA, SCL, VCC (VAUX2), GND | address unknown |
| N1104 | proximity sensor | OUT -> ProxInt, **not on I2C** | GPIO only |
| N2402 | LED driver (key/indicator LEDs V2420, V2422, V2411) | CLK_32K, EN, ADDR_SEL0 tied to VDD, CFLY charge pump | e.g. LP5521/LP5523-like (0x32..0x35), unknown |
| N2000 | headphone amp | SCLK/SDA, INL/INR, CPP/CPN, HPVDD/HPVSS | TI TPA6130A2-like, 0x60 |
| N3350 | battery charger | SCL, SDA, STAT, BOOT, SW, VBUS, PMID, CSIN/CSOUT, SLRST | TI bq2415x, same family as the E6's `ti,bq24153` @0x6b |
| N3301 | USB charger | same pinout + OTG, CD | second bq2415x |

## Touchscreen

| What | Value | Src |
| --- | --- | --- |
| Connector | X2500 "Touch panel flex connector": pin 3 ATTN, 5 VDD, 6 SCL, 7 SDA | p4 [S] |
| Bus | I2C0 via level shifter N2500 (VCCA = VIO 1.8 V, VCCB = VAUX2 2.5 V) | p4 [S] |
| Interrupt | TOUCH_INT via R2500 0R to RapuYama ball **V3 "DISPC_DATA_LCD17"** | p4 [S] |
| Interrupt as gpio | E6 uses touch irq **gpio 123** (`e6.dts`). The E6 ViSSI pin mux uses gpio ids 0x6a..0x79 / 0x7c..0x7e for DISPC_DATA_LCD0..15 + control, so LCD17 = 0x7b = **123** fits | [G] |
| Reset line | none, the controller has no reset pin on the flex | p4 [S] |
| Chip | Synaptics (ATTN naming) | [G] |

## Display

| What | Value | Src |
| --- | --- | --- |
| Panel connector | X1600: MIPI **DSI, 1 data lane** (DSI1_CLKP/N, DSI1_DP0/DN0), RES (pin 13), TE (pin 19), VSEL/VDDI 1.8 V, VPNL = VBAT | p3 [S] |
| Panel driven by | the BCM2727 "Imaging processor" D1400 DSI1 output, **not** by the RapuYama | p3 [S] |
| Panel RES / TE | to D1400 **GPIO37** (L6) and **GPIO33** (L5), VideoCore GPIOs (follow the lines from R1601/R1600) | p3 [S] |
| Host bus RapuYama <-> D1400 | ViSSI: D1400 HD0..HD15 <-> RapuYama DISPC_DATA_LCD0..15, HA0 <-> DISPC_VSYNC, HRD_N <-> DISPC_HSYNC, HWR_N <-> DISPC_PCLK. Same parallel bus as the E6 | p3 [S] |
| HAT (VC attention) | D1400 HAT -> IVE3_HAT_INT -> RapuYama B14 **genio63**, the **same gpio 63 as on the E6** | p3 [S] |
| RUN (VC out of reset) | D1400 RUN (D12) -> J1425 -> RapuYama ball **AB14 "CCPDaP1"** (genio number not printed). E6: gpio 92 | p3 [S] pin, number [G] |
| Backlight | no separate backlight IC in the schematic. The panel gets VPNL = VBAT, the backlight is inside the panel module and is driven by DCS brightness through the VC (the E6 driver already sends DCS brightness) | [G] |
| Other | HDMI connector X1650 (also D1400), composite video out, CCP2 camera link (CCPTX) to the RapuYama | p3 [S] |

Interface on the E6: DSPC interface id 4 (RGB/video), 640x480. The N8 panel is **DSI**,
so `dspc_config()` most likely needs the other interface id (6 in the chipset table) plus
the 360x640 timing. That has to come from the N8 ROM (`vcemu`), see `docs/porting-guide.md` section 3.

## Keys (all straight to RapuYama GPIOs, no keypad matrix, no TC3589x)

| Key | Net | Ball | GPIO | Src |
| --- | --- | --- | --- | --- |
| Volume up | volume up | U10 | genio2 | p4 [S] |
| Volume down | volume down | U20 | genio65 | p4 [S] |
| Menu key | menu key (S2403) | U17 | genio1 | p4 [S] |
| Camera full press | CAPTURE_KEY | G15 | genio36 | p4 [S] |
| Camera half press | CAM_AUTOFOCUS | N17 | genio38 | p4 [S] |
| Lock slider | lock key (S2401) | N7 | genio79 | p4 [S] |
| Power key | S2400 -> PwrOnX on the GAZOO PMIC N2200 (B10), **not a RapuYama gpio** | - | - | p5 [S] |

All keys switch to GND -> active low **[G]**: a switch to GND with no pull-up on the
schematic means internal pull-ups are needed.

## Other GPIOs read off p4

| Signal | Ball | GPIO |
| --- | --- | --- |
| ProxInt (N1104) | Y10 | genio3 |
| AccInt1 / AccInt2 (N1103) | T23 / AA11 | genio12 / genio44 |
| mm_int / mm_drdy (N1105) | K17 / N23 | genio41 / genio40 |
| MMC_1V8_EN (eMMC VIO regulator N3252) | T7 | genio17 |
| MMC_3V0_EN (eMMC VMEM2 DC/DC N3201) | AA4 | genio19 |
| MMCCardDet (uSD) | Y3 | ? |
| uSD dir lines MMCDaDir0..3 / CmdDir / FbClk | Y19, W20, AA21, U22 | genio69, genio11, genio70, genio71 |
| AVVideoCTRL | Y21 | genio13 |
| FMIRQ | Y5 | genio23 |

## Storage, USB, PMIC, audio

| What | Value | Src |
| --- | --- | --- |
| eMMC | D3200 "eMMC 16GB", 4 bit, on RapuYama **MMC2** pins (E22 cmd, K23 clk, L17/L22/G21/H17 data). Rails VMEM2 (DC/DC N3201, en genio19) + VIO_eMMC (LDO N3252, en genio17) | p4 [S] |
| RAM | "Combo memory D3000 on top of RAPUYAMA" (PoP) | p4 [S] |
| microSD | through the **GAZOO** MMC level shifter (ClkA/CMDA/DataNA <-> B side), RapuYama MMC pins AC21/AB21/AB19/AC19/AC20 + direction genios | p4, p6 [S] |
| USB | D3300 ULPI PHY (CHIP_SEL_N, DIR, NXT, STP, DATA0..7, CHRG_DET, PSW, REG1V8/3V3, RREF 12k), ISP1704-like **[G]**, on RapuYama ULPI pins | p3, p5 [S] |
| PMIC | N2200 **GAZOO** ("Gazoo/Pearl"): power key, battery BSI, SIM, audio codec, headset detect, uSD level shifter. Talks to the RapuYama over CBUS (CBusClk/Da/EnX) + EMInt | p3, p5, p6 [S] |
| HW reset | N2400: long power-key press -> GAZOO UserResetX | p5 [S] |
| Speaker amp | N2150 (IHF_AMP_EN from the RapuYama) | p6 [S] |
| Headphone amp | N2000 on I2C2 | p6 [S] |

## UART / test points (BT UART)

| Pad | Net | RapuYama | Src |
| --- | --- | --- | --- |
| J6351 | BTResetX | genio54 (D20) | p3 [S] |
| J6352 | BTWakeUp | genio53 (D19) | p3 [S] |
| J6353 | BTCTS | genio47 (R20) | p3 [S] |
| (R2860 33R) | BTUARTIn | genio49 (D11) | p3 [S] |
| **J6355** | BTUARTOut | genio50 (N9) | p3 [S] |
| J6366 | BTRTS | genio48 (R22) | p3 [S] |

The repo docs name J6354/J6355. **J6354 is not on this schematic version.** BTUARTIn only
has R2860 (no test pad), so on this board revision the second pad would be the resistor.
Whether "In/Out" is from the BT chip's or the RapuYama's side is not stated **[G]**.
1.8 V levels (VIO).

## What the N8 DTs would need (vs `e6.dts` / `e6-vc.dts`)

1. **i2c0 children**: drop `keypad@44` (no TC3589x on the N8) and `touchscreen@4a`
   (maXTouch). Add the Synaptics touch (address from `i2cdetect`), maybe the LIS302 @0x1d
   and the magnetometer. Before anything else, check that `i2c0@20032200` really is net
   I2C0 (Genio27/28): run `i2cdetect -y 0` from the test-1 shell (read-only probe).
2. **Keys**: replace the matrix keypad with `gpio-keys` on gpio 2 (VOLUMEUP), 65
   (VOLUMEDOWN), 1 (menu/HOMEPAGE), 36 (CAMERA), 38 (CAMERA_FOCUS), 79 (lock, `SW_LID`/
   `KEY_SCREENLOCK`), active low. Power key needs a GAZOO/CBUS driver, same as on the E6.
3. **Touch irq**: gpio 123 (LCD17) is the best guess, same as the E6.
4. **videocore**: `run-gpios`: the N8 RUN is on ball AB14 "CCPDaP1". Keep 92 only if the
   N8 ROM's `vcdriver.dll` gpio config says so (porting-guide table). HAT 63 matches.
5. **Panel**: DSI 1-lane 360x640 instead of RGB 640x480: `VC_W`/`VC_H`, `dspc_config()`
   (interface id), the DCS tables, all from the N8 ROM with `tools/vcemu`. RES/TE are VC
   GPIO37/33, so the VC firmware handles them, nothing to do on the RapuYama side.
6. **Charger**: bq2415x likely like the E6, but the N8 has two (N3350 + N3301) on I2C2.
   Keep it off for the first tests.
7. **MMC**: eMMC on MMC2, uSD through GAZOO. Which `mmc@20032800/900` node is which has
   to come from the test-1 `dmesg`/`/proc/partitions`. Keep it read-only.

## Unknown from the schematics alone

- every I2C address (no part numbers printed): the test-1 shell's `i2cdetect` answers it
- net I2C0/1/2 -> Linux controller mapping
- RUN gpio number (ball AB14) and the touch INT gpio number (ball V3, 123 is a guess)
- panel controller IC, panel id and its DCS init (from the ROM / `vcemu`)
- whether "BTUARTOut" (J6355) is RapuYama TX or RX
- touch controller model and address (Synaptics is a guess from "ATTN")

## Confirmed on the phone (23.10.2026, first boots, RM-596)

E6 `Image` + `n8-usb.dtb`, booted with `e6bfix`, USB shell. Linux ran from a boot block
at `0x18c00000`, 93 MB RAM. The battery driver reads 4169 mV. eMMC = `mmcblk1`,
`MAG4EM` 15 GiB (Samsung): `switch to bus width 4 failed`, but 1-bit reads work. Sector 0
is a FAT32 boot sector with no partition table, i.e. the whole device is `E:`.

`i2cdetect -y -r` with `n8-i2c.dtb` (both controllers on, no chips in the DT):

| Linux bus | Controller | Schematic net | Addresses | Chip |
| --- | --- | --- | --- | --- |
| `i2c-0` | `i2c@20032200` (i2c0.dll) | I2C0 | `0x0f` | magnetometer N1105 (ami305 / ak8974) |
| | | | `0x1d` | accelerometer N1103, LIS302DL (as predicted) |
| | | | `0x4b` | touch controller, **maXTouch address range** (the E6 one is at 0x4a), so likely not Synaptics |
| `i2c-1` | `i2c@20032500` (hs i2c port 3) | I2C2 | `0x29` | light sensor V1100 (TSL2563 address) |
| | | | `0x33` | LED driver N2402 (LP5523 range) |
| | | | `0x60` | headphone amp N2000 (TPA6130A2) |
| | | | `0x6a`, `0x6b` | the two bq2415x chargers N3301 / N3350 |

The FM radio didn't answer (probably powered down).

Later on 23.10.2026, with `n8-hw.dtb`:

- **Keys work** (`keytest`): volume up/down, menu, camera focus + capture (two stage), lock slider (momentary), all on the schematic gpios, which are also the ones in the switch.dll config table (`0x801b53f4`: gpio, Symbian scancode x2, ...). The gpio "level" registers `0x200319f0 + 0x10 * k` never move; the pin state is the raw state in the irq block, `0x20033408 + 0x40 * k` (volume down: bank 2 bit 1, 1 -> 0 when held). The gpio driver reads that now.
- **Sensors work** (`sensortest`): LIS302DL (~9.7 m/s2 on z lying flat), TSL2563 (~300 lx in the room, 8 lx covered), magnetometer probes as **AK8974** (follows rotation, ~1 G total, needs calibration).
- **Touch is Synaptics, not maXTouch:** the info block at 0x4b reads all zeros as maXTouch; `i2cdump -y 0 0x4b` shows product id `TM1284_1` at `0x7e` and an RMI4 page description table: F34 (flash) at `0xe9`, F01 at `0xe3`, F08 (self test) at `0xdd`, F11 (2D) at `0xd7`. `n8-hw.dts` uses mainline `syna,rmi4-i2c` with a polling mode added to the rmi4 core (no irq on the ATTN gpio).
- The **power key** goes to the GAZOO PMIC (`PwrOnX`), not to a gpio: needs the PMIC's interrupt / status over CBUS, still to do. `n8-hw.dts` uses these: the
touchscreen, accelerometer, magnetometer, light sensor and `gpio-keys-polled`; the chargers,
the LED driver and the headphone amp stay off.

## WLAN calibration (NVS) and MAC (24.10.2026)

- The WLAN chip is a TI WL127x (WiLink 6) on SPI (`wlanpdd.pdd`: `TWDriverRadio`, `WspiBusDrv`, `ConvertBip2Nvs`). Wiring from the schematic: SPI_CLK genio74, SPI_CSX genio75, SPI_MOSI genio76, SPI_MISO genio77, WLAN_IRQ genio73, WLANENABLE on ball G9 (genio number not printed).
- There is no calibration file. `wlanhwinit.dll` (class `CWlanHwInitPermParser`) gets the MAC and the per-device tuning from the modem's **permanent memory (PMM)** over ISI (`iscapi.dll`, `isimessage.dll`) at every WLAN start, and hands the WLAN driver one block through `CWlanHwInit::GetHwInitData()`.
- `wldump.sis` (`linux/stage0/symbian-loader/wldump_app.cpp`) asks for it through that exported API, read only. It only works when the caller has C++ exception support (`wlanhwinit` uses TRAP inside): built without, `NewL` panicked E32USER-CBase 66.
- On the N8 the "firmware" block is not firmware but: u32 length 468, the **468 byte TI NVS section** (it starts with the MAC register writes `01 6d 54 ..` / `01 71 54 ..`, then calibration tables), u32 length 208, a **208 byte INI section** (radio parameters in an older TI layout). The init data block is 4 zero bytes. MAC prefix `90:cf:15` (Nokia).
- Linux `wl12xx` wants `wl1271-nvs.bin` = the 468 byte NVS section + a 444 byte INI section in the newer layout (912 bytes): the NVS part can be used as is, the INI part has to be converted or taken from TI's reference INI. Dumps are per device (MAC, factory calibration): keep them out of the repo.
