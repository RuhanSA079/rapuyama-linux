# RapuYama drivers from the firmware

Static reverse of the kernel-side ROM (13.10.2026). No phone needed for any of this.

## Pipeline
| Step | Tool | Output |
|---|---|---|
| kernel rom `0x80000000`-`0x8022d380` (ekern + 60 kexts/ldd/pdd) | `work/ghidra/kern/kern.bin`, `modules.txt` | raw blob + text ranges |
| ghidra headless, arm, c6 window + kernel data as blocks | `tools/ghidra_kern_setup.java` + `tools/ghidra_kern_decomp.java` | one `.c` per module in `work/ghidra/kern/` |
| inline rom literals, c6 virt -> phys, ekern export names | `tools/annotate_decomp.py` (+ `ekern_exports.txt` from `eabi/ekernu.def`) | `work/ghidra/kern/ann/*.c` |
| which module touches which page | `tools/kmod_scan.py` | `work/kmod_scan.txt` |
| reg -> module:function | `tools/reg_xref.py` | `work/reg_xref.txt` |

Ghidra run (flatpak): `flatpak run --command=/app/lib/ghidra/support/analyzeHeadless org.ghidra_sre.Ghidra work/ghidra kern -import work/ghidra/kern/kern.bin -overwrite -processor ARM:LE:32:v6 -loader BinaryLoader -loader-baseAddr 0x80000000 -scriptPath tools -preScript ghidra_kern_setup.java work/ghidra/kern/modules.txt -postScript ghidra_kern_decomp.java work/ghidra/kern/modules.txt` (~5 min).

## Two old mistakes
- `0xe0xxxxxx` "HAL ids" are **arm opcodes** (`0xe0800004` = `add r0,r0,r4`, `0xe0410000` = `sub r0,r1,r0`), a raw word scan picked them up. Drivers use plain c6 virt addresses in literal pools.
- the "HAL helper" `0x8005ca30` is `BTrace::OutFiltered` (trace), `0x008xxxxx` args are trace ids.
- `0x20000000`+ is NOT top of dram: ram ends at `0x20000000` exactly. `0x20020000`-`0x2004ffff` + `0x20400000` = **the SoC peripheral bus** (the old scans filtered it out as "ram").

## Peripheral map (phys)
| phys | who uses it | what |
|---|---|---|
| `0x20020400` / `0x20020600` / `0x20020a00` | spi1/2/3.dll | SPI x3 (same layout, `+0x04..+0x64`), `0x20020820` shared |
| `0x20020200`, `0x20020300` | bc02_uart1 | uart/bt side config (`+0x08`) |
| `0x20024000` | dma.dll, secldd, securitykext, vcdriver | dma channels, crypto |
| `0x20025a00` | bc02_uart1.PDD | **UART to the CSR BlueCore02 bluetooth chip** (hci, up to 4 Mbaud, dma) |
| `0x20025800`, `0x20024800` | vcdriver | same reg pattern as the uart, probably more uart/ssi blocks (to VideoCore?) |
| `0x20030700` | karap | **CBUS-like serial master to the PMIC** (dev<<6 / reg / `0x20`=read) |
| `0x200318xx`-`0x20031axx` | egpio, bc02_uart1, power | **GPIO** + pin mux (uart uses `+0x868/870/87c`) |
| `0x20032000` | EPBUSMV, i2c0 (`+0x200`), ecust, karap | i2c/peripheral bus, PLLs at `+0x604/624/644/664` |
| `0x20033000` | karap, power, power_resources, dvfs, egpio, ekern | **clock/power controller** (gates, wakeup irq banks, counters, ms timer) |
| `0x20038800` | usb.dll | **USB OTG = Mentor MUSB** (`+0x00` FADDR, `+0x01` POWER, `+0x02` INTRTX ... `+0x60` DEVCTL) |
| `0x20040000` | karap, usb, dvfs, ecust | misc control (`+0x180`, `+0x280..288` set/clr/status) |
| `0x20043000` | dma, ecust | dma global |
| `0x20400000` | karap | **interrupt controller** |
| `0x30000000` | karap, ecust | sysctrl (only the base is used) |
| `0x40100000`-`0x401fffff` | xtitx, xtiwriter, ecust | **XTI trace port** (reads FF from the scan - its a write-only trace fifo) |

## Interrupt controller `0x20400000`
From the karap IRQ/FIQ dispatchers (`0x800b7c88` = `Arm::SetIrqHandler` target, `0x800b7e40` = FIQ) and the Interrupt:: functions.

| off | what |
|---|---|
| `+0x04` | global enable (init writes 1) |
| `+0x08` | IRQ: current pending number, read = take it, `-1` = none |
| `+0x0c` | write N -> enable line N |
| `+0x10` | write N -> disable line N (`-1` = all) |
| `+0x1c` | init writes 0 |
| `+0x20` | write N -> select line for config |
| `+0x24` | config of selected line (priority 0..7, Bind writes 2) |
| `+0x40` / `+0x48` / `+0x50` | FIQ versions of `+0x00` / `+0x08` / `+0x10` |
| `+0x80` | read at end of IRQ (eoi / priority drop) |
| `+0x84` | init writes 5 |
| `+0x90` / `+0x9c` | nest flag irq / fiq (set 1 around nested handler) |

- 421 lines (`0..0x1a4`), handler table at kernel data `0xc8003340` (8 bytes each).
- init: enable=1, `+0x1c`=0, disable all, `+0x84`=5, drain `+0x08` until `-1`, same for fiq,
  then 5 secondary banks in the clock/power block at `0x20033414 + 0x40*k` get disabled at `+0x14`
  and drained by reading their pending-number register at `+0x3c` until `-1`.
- irq loop: `n = rd(+0x08); if n!=-1 -> handler[n]; repeat`, when none `rd(+0x80)`.
  Linux defers per-line unmask operations requested during dispatch until after this final EOI read, preserving the firmware timer ISR's EOI-before-reenable order and preventing an asserted level from being requeued before EOI.

## Timers
- **ms tick** (`BindMsTick`) = **IRQ 242**, timer at `0x200337a0`:
  start = `wr(0x200337a4, 0x9600)` + enable 242, isr = disable 242, `wr(0x200337a8, 0x9600)`, `NTimerQ::Tick`, `rd(0x20400080)`, enable 242.
  `0x9600` = 38400 -> 1 ms at 38.4 MHz.
  Linux keeps this confirmed 1 ms hardware period and divides the IRQs to `HZ`; programming `rate / HZ` at `HZ=100` would request 384000 counts, beyond the only period verified on hardware and possibly beyond the counter width.
- **free running counter** (`NKern::FastCounter`, ekern `0x8005c41c`): write anything to `0x20033354` (latch), read `0x20033350`. Frequency not pinned yet. -> Linux clocksource.

## Clocks
- ref clock 38.4 MHz (`0x0249f000` everywhere).
- PLLs `0x20032604/624/644/664`: f = 38.4M * ((reg & 0xffff) >> 5) / ((reg & 0x1f) + 1), selects in `0x20033090` (bit 4), `0x20033120` (bit 1), `0x20033130` (bit 14), x2 if `0x200330b0` bit 10 (karap `0x800b8750`).

## UART block (bc02, `0x20025a00`)
| off | what |
|---|---|
| `+0x04` | config lock, 1 while configuring, 0 after |
| `+0x08` `+0x0c` `+0x10` `+0x5c` `+0x60` `+0x88` `+0x8c` `+0xb8` | baud, div = 38400000 / baud (`+0x08` = (div-3)>>1, `+0x88` = div-1, `+0x8c` = div, `+0x5c` = ((div-3)>>1)/5, fractions in `+0x60`/`+0xb8`) |
| `+0x14` | data bits (6 / 7 for the two modes) |
| `+0x18` | parity enable |
| `+0x1c` | parity type / stop (0/1/2) |
| `+0x28` | irq clear (write `-1`) |
| `+0x2c` `+0x30` | rx/tx related |
| `+0x34` | irq status (bits 0..3) |
| `+0x38` | irq enable mask (`0x1e` / `0x0e`) |
| `+0x3c` | 0 at 921600, `0x7fffff` at 120000 |
| `+0x84` | 3 = tx+rx enable |
| `+0x9c` | busy, wait while 1 |
| `+0xa0` `+0xa4` | fifo / data |
- irqs 99 and 101 (`Interrupt::Bind(0x63 / 0x65)`), dma via dma.dll.
- pin mux: `0x20031868`, `0x20031870`, `0x2003187c`.

## Linux drivers (13.10.2026)
In the kernel fork (`linux/kernel/linux`, branch `e6`), `CONFIG_ARCH_RAPUYAMA` selects both:
| File | What |
|---|---|
| `arch/arm/mach-rapuyama/Kconfig` | `ARCH_RAPUYAMA` (multi v6) |
| `drivers/irqchip/irq-rapuyama.c` | `nokia,rapuyama-intc`, firmware init order, level irqs, mask/unmask by number, priority 2 |
| `drivers/clocksource/timer-rapuyama.c` | `nokia,rapuyama-timer`: ms timer = periodic clockevent (irq 242, reload rewritten in the isr like symbian), 32k counter = clocksource + sched_clock |
| `linux/boot/e6.dts` | intc + timer nodes, fixed clocks 38.4 MHz / 32768, the wrong `reserved-memory` over `0x20000000` is gone |

Builds clean (0 checkpatch errors). Not run on hw yet, unknowns:
- does reading `+0x08` really ack, is the `+0x80` read enough as eoi
- does the write to `0x200337a8` ack the ms timer irq, counter width (shutdown parks it at `0xffffffff`)

**Checked live 13.10.2026** (`timerprobe.sis`): fast counter +32834 over 1002 ms ticks = **32768 Hz OK**. ms timer counts DOWN ~3870 per 100 us (= 38.4 MHz + NanoWait overhead) and wraps at `0x9600` = **1 ms reload OK**. `0x200337a8` is **write only** (reads 0).
- clocks are assumed on (symbian set them up, we boot from a live symbian)

Live check before trusting it: `driver/timerprobe.sis` -> `E:\timerprobe.bin` -> `tools/parse_timerprobe.py` (32k rate, ms timer counting down inside `0..0x9600`).

## GPIO (egpio.dll, block `0x20031900`)
Reversed from egpio + how the bt uart / onenand / usb / power drivers use it.
| off (bank k) | what |
|---|---|
| `0x90 + 0x10k` (k 0-5), bank 6 `0x1b0` | read: output latch |
| `+0x04` / `+0x08` of that | write bits -> high / low |
| `0xf0 + 0x10k` (k 0-4) | read: pin level |
| `+0x04` / `+0x08` of that | write bits -> input / output |
| `0x154 + 0x10k` / `0x158 + 0x10k` | another set/clear pair (pull/drive?) |
- 32 pins per bank, banks 5 and 6 are output only. direction is not readable.
- pin irqs go straight to the main intc: bank 0 = 316.., bank 1 = 348.., bank 2 = 380..403, the rest scattered in 273..420 (table at `0x800c1bfc`, symbian global pin id -> irq/bit/bank).
- trigger/enable for pin irqs in the power block `0x20033400 + 0x40k`: `+0x04` bank enable, `+0x10` irq enable (bit), `+0x14` disable (bit, `-1` = all), `+0x18` enabled mask, `+0x2c`/`+0x30` trigger, `+0x3c` pending number (`-1` = none).
- Linux: `drivers/gpio/gpio-rapuyama.c` (no irqs yet).

## I2C (i2c0.dll, `0x20032200`, irq 150)
**It's the TI OMAP i2c** (omap2420 layout: 16 bit regs 4 apart): REV `+0x00`, IE `+0x04`, STAT `+0x08` (AL/NACK/ARDY/RRDY/XRDY = 1/2/4/8/0x10), CNT `+0x18`, DATA `+0x1c`, CON `+0x24` (`0x8403` = EN|MST|STP|STT), OA `+0x28`, SA `+0x2c`, PSC `+0x30` = 0, SCLL/SCLH `+0x34/+0x38` = fclk*t - 7 / - 5 (omap formula).
fclk 19.2 MHz, or pll/div (`0x20033130` bit 14, div sel `0x20033010` bits 12-14, max 24 MHz). -> mainline `i2c-omap` as `nokia,rapuyama-i2c`: the omap2420 flags, but DATA is 8 bit (i2c0.dll moves one byte per RRDY / XRDY). `ti,omap2420-i2c` zeroes every second byte of longer reads.
Keypad = **LM8320** on i2c (keypad_exp.dll: `DIOExpHwLM8320ST/NSC/Toshiba`), mainline has `lm8323` (platform data only, needs dt glue).
- i2c addr **`0x44`**, irq = gpio pin **42** (bank 1 bit 10, intc 358), config byte `0x87` = 8 rows x 7 cols, uses the LM8323 command set (`0x91` = READ_CFG).
- fifo code = `row << 4 | (col + 1)`, map at kernel data `0xc800f514` (filled from rom `0x801e3610..`): `01 Q 02 W 03 E 04 1 05 2 06 3 07 * 11 A 12 S 13 D 14 4 15 5 16 6 17 # 21 Z 22 X 23 C 24 7 25 8 26 9 27 0 31 I 32 O 33 P 34 RCtrl 36 Dev0 37 Yes 41 K 42 L 43 Backspace 44 VolUp 45 LShift 46 App1A 47 App0 51 , 52 . 53 Enter 54 Dev1 55 No 56 ' 57 VolDown 61 LFn 62 Space 64 App1F 65 App1B 66 ] 67 RFn 71 Up 72 Down 73 Left 74 Right 75 Dev3 77 Dev6` (values = symbian TStdScanCode).
- **correction (13.10 later)**: the E6 rom config (`0x801e35e0 + 0x240` = 1) picks the **`DIOExpHwSmall25Pin`** class, not the LM8320 ones. it still talks to i2c `0x44` and reads chip id reg `0x80`: **0 = NSC (LM8323 style commands), 3 = Toshiba TC3589x** (register style writes like `[01 52 52 xx ff ff]`, `[08 02]`, `[09 03]`). mainline has both `lm8323` and `tc3589x` (mfd + keypad, dt ready). which one is in this phone = read reg 0x80 once i2c works.
- keypad light is NOT driven by the keypad chip (no LM8323 PWM commands 0x95-0x97 anywhere). lightpdd only has a display mentor (`DLightDisplayMentor`) -> backlights go through the display / VideoCore stack.
- the "direct" key path (`0x801d2090`, map `0x801e3464` = Down Up Right Left Device10..13) is NOT gpio: it's a virtual navi layer fed from matrix codes (arrows are matrix codes 0x71-0x74). so no gpio-keys to describe for the keyboard.
- R T Y U F G H J V B N M are NOT in that map -> some other scan path (second expander / direct gpio matrix classes `DIOExpHwDirect36Pin`, `Small25Pin`), get the rest live by pressing keys under linux.
- symbian global gpio id -> bank/bit: id > 105 is id - 18 (egpio `0x800c1900`).

GPIO irqs in linux: `gpio-rapuyama` is a hierarchical irqchip on the (now hierarchical) intc, parent line per pin from the egpio table. Enable/ack/edge/level are from the code, the polarity regs are a guess.

Live check: `driver/regprobe.sis` -> `E:\regprobe.bin` -> `tools/parse_regprobe.py` (i2c REV for i2c-omap, i2c fclk source, gpio state).

## Touchscreen (atmel_qt.dll)
Atmel maXTouch on i2c0 **`0x4a`**, 16 bit register addresses. irq = gpio 123 (egpio id `0x7b`), power = PMIC resource `0x34`, left on by Symbian.
- Linux: `drivers/input/touchscreen/rapuyama-mxt.c`, polled, T9 touches -> multitouch slots. works (19.10.2026)

## Watchdog (power.dll / sisa_wrapper.dll)
- the pmic is **Retu style on CBUS** (karap reg table `0x800bc268`: dev 1 regs `0x00..0x1e`, dev 2 = Tahvo style `0x00..0x1f`, 16 bit) - same as the N800.
- power.dll kicks every 3 s (`NTimer::Again(.., 3000)`): pmic dev 1 reg **`0x17` = `0x30`** (48 s) + **`0x200336c4` = `0x3fffffff`** (soc counter).
- writing **0** to pmic `0x17` = immediate power off (sisa "Force Power Off").
- CBUS master `0x20030700` (karap's base pointer is `0x20030704`, checked in the raw disasm at `0x800b84fc`): `+0x04` en, `+0x08` busy, `+0x18` read cmd (`dev<<6|reg|0x20`), `+0x1c` write cmd (`dev<<6|reg`), `+0x20` data, `+0x2c` bits-1, `+0x3c` = 1 before write. **13.10 bug**: i first had all of these 4 bytes low -> the data `0x30` went into the write cmd reg -> random pmic command -> phone powered off (~2 s after the linux jump, instantly in kickloop). fixed in rapuyama_wdt.c, PrepareHw, kickloop.S.
- Linux: `drivers/watchdog/rapuyama_wdt.c` (kicks both, HW_RUNNING so the core feeds it from boot). mainline retu-mfd/retu_wdt would need the pmic irq line (unknown).

## Reboot (rebootdriver.ldd)
- **soc reset = irqs off, write 2 to `0x20033114`, spin** (`0x800cd0bc`). Linux: restart handler in rapuyama_wdt (3rd reg).
- boot mode survives the reset in **`0x20040400` bits 20-23** (rebootdriver get/set `0x800ccf6c`/`0x800ccff0`): set mode 1..6 -> nibble 0/e/f/8/a/b, read 0 or e -> 2, f -> 3, 8 -> 4, a -> 5, b -> 6. symbian picks its startup mode from it (normal/charging/test...). could carry a progress code from linux across a reset, not used yet.

## Console (bt uart)
- tx: wait `+0xa0` != 0 (fifo room), write `+0xa4`. rx: while `+0x2c` (fifo level) read `+0x64` (bits 8-11 = errors). `+0x38` = rx threshold (<= 60).
- baud: q = 38400000 / baud, `+0x08`/`+0x10` = (q-3)>>1, `+0x5c` = that/5, `+0x0c` = that+1, `+0x88` = q-1, `+0x8c` = q, fractions `+0x60`/`+0xb8` (see `rapuyama_earlycon.c`).
- N8 test pads **J6354 = SoC tx, J6355 = SoC rx, 1.8 V**. power/clock/mux via PowerResourceManager -> switch bt on in symbian before booting.
- Linux: `drivers/tty/serial/rapuyama_earlycon.c` (polled, `earlycon` + `keep_bootcon`). A full tty driver later.
- regprobe.sis also reads the bt uart baud regs -> parse_regprobe.py checks our formula against what symbian set (bt on).

## Clock gates (power_resources.dll / PowerResourceManager.dll)
- drivers ask `PowerResourceManager` (`0x800c5f74(handle, id, value, ..)`) for resources, **ids are 1 based in the order of its name table** (`EClockArm`=1, `EClockMPLL`=2, `ELatency`=5 (value = us), `EPerformance`=6, `EHwClkI2C1`=0x11, `EHwRegVAux2VSel`=0x32, `EHwClkBTExtSysClk`=0x38 ...).
- TWO levels: hw gates (built at boot `0x800c7104..`: `(reg, mask, polarity, gate id, seq idx)`, gate reg R has **R+4 = set, R+8 = clear**, pol 1 -> set = on) and PRM resources -> up to 3 gates (**table `0x800cbee8`**: `{prm id, gate, gate, gate}`). gates with a seq idx are flipped via the modem (`sisa_wrapper` `0x8010365c`) while its link is up, direct write otherwise.
- **13.10 mistake**: i first used PRM ids as gate ids. real: i2c0.dll asks PRM `0x11` = `EHwClkI2C1` = gates `0x7` (`0x200330d0` bit 6) + `0x12` (`0x200330b0` bit 4). bt uart asks `0x1f` = `EHwClkBB38M4AnaClk1` = gate `0x22` (`0x200330d0` bit 12), plus `0x38` BTExtSysClk (gates 0x2e/0x1a, gpio driven) and latency 5.
- both blocks are gated while idle -> touching them = external abort = phone dies (regprobe runs 1 and 2: every bt uart reg and the i2c regs died, the gpio/clock regs on the same pages read fine).
- `drivers/clk/clk-rapuyama.c` = PRM level clocks (dt index = PRM id), 0x11 + 0x1f critical. the booter's `PrepareHw` turns those gates on before the jump. regprobe v3 (`regprobe3.*`) checks it.

## regprobe run 1 (13.10.2026, `logs/regprobe.*`)
- gpio levels/latches, gpio irq banks, clock regs: read fine. gpio 42 (keypad irq) idles high -> active low.
- **i2c fclk = pll 192 MHz (`0x20032644` = `0x1e2`) / 8 (`0x20033010` sel 4) = 24 MHz** (dts fixed).
- **`0x20025a04` (bt uart) KILLED the phone with bt ON** -> the uart is clock gated while the bt stack idles. Next run (`regprobe2.*`) turns gate 0x1f on first to prove it's the uart clock.
- so the booter (`PrepareHw` in runblob_ldd.cpp, Boot control) now turns gates 0x1f + 0x11 on, sets uart active and kicks both watchdogs right before the jump, with irqs off.

## USB (usb.dll, `0x20038800`)
- **Mentor MUSB** core (`+0x00..+0x7f`, ULPI viewport at `+0x74..+0x76`, EPINFO/RAMINFO `+0x78/+0x79`), flat/indexed ep regs from `+0x100`.
- **TI OMAP2430 otg wrapper at `+0x400`**: `0x400` REVISION, `0x404` SYSCONFIG, `0x408` SYSSTATUS, `0x40c` INTERFSEL, `0x414` FORCESTDBY. usb.dll `0x80189550` inits it exactly like omap2430.c: SYSCONFIG = SOFTRST, poll SYSSTATUS bit 0 (10 x 5 ms), FORCESTDBY bit 0 off, SYSCONFIG `|= 0x2010` (smart standby + smart idle) and AUTOIDLE off, INTERFSEL = 1 (12 pin ULPI).
- irqs: **183 = core ("mc"), 215 = dma** (`0x8018919c`).
- phy = ULPI inside the **Gazoo** power asic (N8 schematic: `ULPIData0..7/Clk/Dir/Nxt/Stp` -> "GAZOO > USB controller"; CBus also goes to Gazoo, so Gazoo = the Retu/Tahvo style pmic).
- clocks: PRM `0x0f` EHwClkHSUSB (gates 0x15 + 0x1e) and `0x0e` EHwClkDomHSUSB (0x16, 0x1f, 0x18); 0x15/0x16 are irq driven power domain objects (`0x800c7634`, base `0x20033150`, irqs 0x114/0x115) - not done in clk-rapuyama yet.
- Linux: `drivers/usb/musb/rapuyama.c` (glue, pio only, peripheral, nop phy) + g_serial with console (`console=ttyGS0`, shows up as `ttyACM0` on the pc). `linux/boot/e6-usb.dts` turns it on, boot with the cable plugged so symbian keeps the block powered.

## SD / MMC (EPBUSMV.DLL)
- **TI OMAP1/2420 style MMC** (16 bit regs 4 apart: CMD 00, ARGL 04, ARGH 08, CON 0c, STAT 10, IE 14, CTO 18, DTO 1c, DATA 20, BLEN 24, NBLK 28, BUF 2c, SDIO 34, RSP 40-5c, SYSC 64), mainline `mmc/host/omap.c`.
- **two controllers**: base table `0x80164558` = `{0, 0x100}` -> `0x20032800` and `0x20032900`. N8 schematic: microSD on MMC2 -> probably `0x20032900`.
- per controller tables in EPBUSMV: base `0x80164558` = `{0, 0x100}`, **irq `0x80164568` = `{0xad, 0xab}` = 173 / 171**, **power resource `0x80164578` = `{0x23, 0x24}`** -> gates 0x23+0x25 / 0x24+0x26 (`0x200330d0` bits 5/15, `0x20033180` bits 7/8, all direct, pol 1).
- **correction**: my first note said the sd clocks only go through the modem. wrong - the controllers use 0x23/0x24 (direct gates). `EHwClkMemCard0/1` (modem only) are something else (card power / ext clock?).
- Linux: `mmc/host/omap.c` + a dt fallback for its platform data (one slot from `bus-width`/`max-frequency`), `MMC_OMAP` allowed on ARCH_RAPUYAMA, nodes `mmc1`/`mmc2` in e6.dts (disabled, fclk rate unknown so max-frequency 6 MHz).
- so rootfs = initramfs (busybox) for now.

## VideoCore link (vcdriver.dll, `0x80328170`)
- display = Broadcom BCM2727B1 VideoCore III. the ARM **boots it**: `DVCDriver::Boot()` talks to the VC boot ROM in 2 stages and sends the VC image (`unsigned_2727b1.dll`, 1 MB in rom, "VideoCore image ID").
- message link = **MeSSI** (an SSI block used for VC messages, `DVCMessiTx/RxChannel`, `TControllerMessi`), regs `0x20024800..0x20024880` and `0x20025800..0x20025864` (same reg pattern family as the bt uart at `0x20025a00`).
- bulk VC -> ARM data over **CCP2 rx** (`DVCCcpRxChannel`, `DIspCcp`, camera port), plus gpio handshake pins **HAT** (irq) and **RUN**, a torch enable pin, regulators Vaux/Vout/Vio.
- also uses `0x200318e0..0x200319e8` (gpio/mux), `0x20033008/038/178` (clocks), `0x20034000..240`.
- decompiled (13.10, `work/ghidra/kern2/ann/`, xref `work/reg_xref2.txt`): MeSSI A `0x20024810..0x20024880` (fns `0x8032a154` init, `0x80329c8c/9e24/9db4/9ec8` tx/rx/irq), MeSSI B `0x20025810..0x20025864` (`0x8032a368` init, `0x8032a6b4/a81c/a98c/acc4`), pin mux `0x200318e0/f0/900` + gpio `0x200319e4/e8` (HAT/RUN/torch), clocks `0x20033008/038/178`, CCP2/ISP rx `0x20034000/200/240`.
- More detail: `FUN_8032a3c8` sets up channel-related state for IDs `0x7e`, `0x7d`, `0x7c`, `0x6a..0x79`, then installs callbacks/semaphore; MeSSI B init is followed by MeSSI A init, which binds IRQ `0x92`. The A-block callback reads/acks `0x20024818`, handles FIFO status and logs underflow/sync-loss bits. `FUN_8032cd50` performs several request/response and image-transfer steps, including a later descriptor at image offset `0x1c`; exact wire schemas remain unknown. Expanded evidence: [`videocore-display.md`](videocore-display.md).
- XTI trace port (xtitx/xtirx): `0x40103880`, `0x40103ec0`, `0x401e0000`, `0x401e1000`, `0x401fd000` + `0x20032784/7b0`. needs nokia's trace hardware, not a console option for us.
- ROM analysis shows the boot request/response and image-transfer path in `vcdriver.dll`, while VCHI handles MeSSI/CCP2 traffic and the display-management modules sit above it. Exact framing, service commands, and Linux transport compatibility are still open. See [`videocore-display.md`](videocore-display.md).
- The kernel already includes the generic BCM2835 VCHIQ stack, but its DT binding expects a Broadcom doorbell pair. Do not assume it is compatible with RapuYama's MeSSI/CCP2 transport.
- Linux display driver not implemented. A disabled-by-default, no-MMIO platform-driver scaffold exists under `linux/kernel/linux/drivers/soc/rapuyama/`; it has no DTS node and its probe explicitly reports unsupported operation.

## Modem link (ISA)
- clocks like MemCard/GDMA go through the modem: power_resources -> sisa_wrapper `0x8010365c` -> a function pointer registered at run time (`c800a6e0`), isaaccessextension = phonet style pipes/PEP (`DPepTransceiver`, `MIST2IADApi`), like the N900 ISI stack.
- no separate transport module in the kernel rom (no ssi/hsi/ist dll), RapuYama = RAPU (modem + APE on one chip), so the link is probably an on-chip mailbox / shared memory. not found yet.
- chain: sisa_wrapper (own channel object at `c800a6e0`) -> isaaccessextension (IAD router, phonet-like pipes) -> `MIST2IADApi` transport. modules importing isaaccessextension: dvcsurface, empmIsaIf, mednand(?), secldd, eusbcsc, keypad_exp, tcldd, khbridge, custom.ldd, ssc.ext (stub attribution is approximate). transport candidates to look at next: `ssc.ext`, `khbridge.ldd`.

## RAM console (the real debug channel, 13.10)
The soc watchdog reset is a WARM reset (battery not pulled, phone auto-reboots), so DRAM survives it. Use that to read the kernel log back.
- **RAM earlycon** (`drivers/tty/serial/rapuyama_earlycon.c`, name `ramcon`): writes every printk char into a ring at phys `0x1f000000` (hdr: u32 'E6LG', u32 len, then the log). Active from the START of setup_arch (ramoops console inits far too late - the kernel dies at local_irq_enable, way before that). Touches only RAM, so unlike the uart earlycon it can't fault. `earlycon=ramcon,0x1f000000 keep_bootcon` in e6.dts; the region is a reserved-memory no-map so the allocator stays off it.
- after a boot attempt resets the phone: install+run `out/ramread.sis` (runblob LDD `EControlReadPhys` maps the phys page via `DPlatChunkHw`, copies it out) -> `E:\klog.bin`.
- parse: `tools/parse_klog.py logs/klog.bin` (decodes 'E6LG' header + ring wrap) -> full early kernel log in ONE boot.
- first use: see why it dies at local_irq_enable (cp20 showed our intc handler is never entered). a valid 'E6LG' header + real text also confirms 0x1f000000 survives the reset.

## Sign of life without a console
- `/init` (`linux/initramfs/init.S`) sleeps 30 s then `reboot(POWER_OFF)`, rapuyama_wdt's power off handler writes 0 to the pmic watchdog (= symbian's force power off). clean off ~30 s in = userspace reached.
- watchdog driver is `subsys_initcall` so the soc watchdog (~13 s) is fed before usb/i2c probe.

## Rest of the kernel rom (13.10, decompiled, `work/ghidra/kern2/ann/`)
Decompiled every remaining kernel-side module except the camera (dcam*, Ghidra hung in there) and wlan (huge, not boot). Finding: **no new directly-drivable MMIO peripherals**. All of them sit on top of abstractions already mapped:
- thermal sensor = battery temp (`BTemp`), read through the HAL/ADC over the modem ISA path, not a discrete block.
- EDISP / tv_out / sgx (graphics) = all through the VideoCore (VCHI) stack.
- tracecore / xti / exmon = Nokia trace + crash logging (one extra reg `0x20033194` in the debug block next to the reboot reg `0x20033114`).
- ssc / khbridge / dataserviceapi = sw clients on ISA / VCHI.
So the peripheral reverse is **complete** for what's directly drivable from the ARM side: intc, timers, gpio(+irq), i2c, watchdog, clocks, usb/musb, uart, reboot, sd/mmc. The display (VideoCore) and modem (ISA) are protocol stacks, each a separate future project, both needing the live phone.

## INTC initialization and dispatch safeguards (13.10)
`FUN_800b8b18` (firmware "disable all") masks not just the main+fiq banks but **5 secondary gpio/peripheral banks** at `0x20033400 + 0x40*k` (write `~0` to `+0x14`, drain the pending-number register at `+0x3c` until -1). The secondary banks are masked and drained during Linux INTC initialization. The Linux handler reads the main controller's end-of-interrupt register once after draining pending IRQs, matching the firmware dispatcher. It masks unmapped lines, bounds the dispatch loop, and masks all IRQs if hardware reports an out-of-range line. These safeguards prevent an unhandled or malformed pending queue from trapping the CPU in the handler; they do **not** establish that such a queue caused the observed boot hang. `drivers/irqchip/irq-rapuyama.c`.

## IRQ / reg audit (13.10, vs firmware Interrupt::Bind)
Cross-checked every DTS irq against the firmware bind sites (`Interrupt::Bind` = `0x800b89b0`, first arg = line number). All confirmed, no collisions, all < 421 (max line 420):
| block | dts irq | firmware |
|---|---|---|
| ms timer | 242 | karap BindMsTick 0xf2 |
| i2c | 150 | i2c0.dll 0x96 |
| bt uart | 99, 101 | bc02 99 / 0x65 |
| usb musb | 183, 215 | usb.dll 0xb7 / 0xd7 |
| mmc@20032800 | 173 | EPBUSMV table 0x80164568[0] = 0xad (indirect bind via FUN_8015dd68) |
| mmc@20032900 | 171 | 0x80164568[1] = 0xab |
other live lines seen in firmware (for later drivers): 128-130 (0x80-0x82), 184-186, 191-193, 248, 255.

## Random reboots after the handoff (15.10)
Linux (and anything else that kills symbian) gets reset after a random 30-210 s, back into symbian.
- **not linux**: `out/e6kick.sis` (kickloop: mmu off, irqs off, only kicks the soc + pmic watchdogs) died the same way after 2:03. drivers, sim in/out, network on/offline change nothing.
- **how it dies**: cpu and usb stop at the same moment (picocom's last `/proc/uptime` vs the flight recorder's last sample, <1 s apart), the reset comes 2-3 s later. no clock / power / gpio reg changes in the 6 s before (`tools/parse_flight.py`).
- **not the modem link**: `0x20040280` (status) / `+4` (set) / `+8` (clear), bits `0x1000`/`0x2000` = lines 30/31 are a self interrupt of `sisa_wrapper`: it queues `{fn, 4 args}` in its own heap, raises line 30, the isr (`FUN_80104194`) runs them. the modem never sees these regs, so the old `rapuyama_modem.c` "kick" did nothing.
- **not shared ram corruption**: the ram symbian maps in its hw window (`0x11525000`, `0x1f8b0000`, `0x1ffe0000`) is reserved in `e6.dts` and watched (`e6.shmwatch`); nothing writes it after the jump.
- **TrustZone is present; its role in this reset is unproven.** The ARM1176 build config enables TrustZone support, and the kernel ROM contains `SMC` instructions in cache/TLB maintenance paths. `securitykext.dll` and `secldd.ldd` resolve a `PA_SWD` entry and submit request types `0x0b` / `0x15`; the available decompilation does not establish that this is a watchdog kick or that missing it causes a reset. The `0x809795a3` magic is a generic BB5 image header, not evidence that the file is a protected application store. A secure-routed interrupt could still explain why IRQ/FIQ-masked `kickloop` stops, but that has not been observed directly.
- **test:** build `e6tztr` via `linux/stage0/symbian-loader/build_app.sh e6tztr securetrace_app.cpp 0xe1000030`, start a stopwatch, run it, then after the phone resets use `e6logs` to dump `E:\klog.bin` and `tools/parse_securetrace.py <dump> <elapsed-seconds>` to inspect the bare-metal heartbeat trace and unrecorded tail. It records `ID_PFR1`'s security-extension field plus entry `CPSR`/`SCTLR`, feeds the known SoC and PMIC watchdogs, and logs the 32.768 kHz fast counter every ~31 ms into reserved RAM at `0x1f080000`. Long gaps or an abrupt trace end show that the non-secure loop stopped or was delayed; they do not alone identify TrustZone as the cause. `ID_PFR1` establishes extension support, not the current security world. A trace reaching the 8192-record limit means the run lasted about 256 s without a recorded long stall; power-cycle to recover if it is still running.
- **result (15.10):** one run logged 6412 records = 200.34 s, every interval exactly 31.2 ms including the last 20, no gap anywhere; wall clock ~204 s to the reset. Entry `CPSR=800001d3` (SVC, I+F masked), `ID_PFR1=0x11`. What this does and does not show:
  - firm: the non-secure loop ran at a perfectly uniform 31 ms cadence right up to the final record, so the cpu was not stalling or slowing before the reset - it stops between two samples. the reset is abrupt, not a hang.
  - firm: the SoC counter (`0x200336c4`, ~13 s if unfed) and the PMIC watchdog (reg `0x17` = 48 s) are being fed correctly - surviving 200 s is far past both.
  - therefore the reset comes from a source we are not feeding, and it fires while both I and F are masked. that is consistent with a secure-routed interrupt or an external/hw reset line, but the trace alone does not prove which, and does not prove it is `PA_SWD`.
  - survivals across runs (30, 80, 123, 129, 200, 209 s) are all under ~210 s, which *suggests* a periodic source with a period near that, but six samples is thin.
- `0x1ffec000` (the `0x1ffe0000` block) is the save area of symbian's own deep sleep (`power.dll FUN_80146cf4`) and also used by securitykext / secldd.

## Reboot source: what has been ruled in/out (16.10)
Static elimination plus one phone read (`out/e6rstr.sis` -> `logs/e6rst-*.log`).
- **the two watchdogs we feed are correct and are not the killer.** `power.dll`'s periodic handler `FUN_801462b8` re-arms every `0xbb8` = 3000 ms and does exactly two things: `FUN_800b8494(1, 0x13, 0xffff, 0x30)` and `*0x200336c4 = 0x3fffffff`. The cbus helper (`karap FUN_800b8494(dev, reg, mask, data)`, base `0x20030704`) turns logical reg `0x13` into hw command **`0x57`**, width 16, data **`0x30`** (table at `0x800bc268 + reg*0x10`: entry `0x13` -> byte8 `0x17`, word0 `1` -> `(1<<6)|0x17` = `0x57`). `securetrace`/`rapuyama_wdt` already write command `0x57`, data `0x30`, 16-bit - **identical**. The old note "pmic reg 0x17" confused the hw address byte with the logical index; it is the same write. So both the SoC counter and the PMIC watchdog are fed exactly as the firmware does.
- **the boot-reason register does not tell us the source.** `rebootdriver.ldd` uses `0x20040400` bits [23:20] as the reboot reason (decode `FUN_800ccf6c`: nibble `0->1 0xe->2 0xf->3 0x8->4 0xa->5 0xb->6`, sw reasons) and reboots with `*0x20033114 = 2`. `e6rstr` read it after a normal boot and after a linux death-reset: **both `0x00040000`, nibble `0x0`**, identical. `nloader` normalizes the nibble on each boot, so the hw source is already gone by the time a symbian app runs.
- **no other periodic kernel timer in the minutes range.** A sweep of every `NKern::TimerTicks(const)` in the kernel rom found only `power.dll` = 3 s and `securitykext` = 300 s (`0x493e0`) in that range (plus a 15 s one in an unrelated module). So by elimination the un-replicated periodic action is `securitykext`'s 300 s submit, which goes to the **secure world** - consistent with a reset immune to I+F, MMU-off and the fed watchdogs. (6 survival samples all <=209 s mildly favour a ~210 s period over 300 s, so modem supervision is not excluded; both live in a different cpu/world than the non-secure loop.)

## Secure-world call gate LOCATED (16.10)
The non-secure -> secure call is **`SMC #0`** in ARM state. Two clean `0xe1600070` sites in `kern/kern.bin`: `0x80140480` (securitykext's own monitor-call routine) and `0x801451d0` (inside the 756-byte transport stub that `FUN_80143964` memcpys from `0x80145050` and registers at `c800d1c4+0x14`). Both are byte-identical. The routine (entry ~`0x801403b0`):
- saves `SCTLR`/`CPSR`/`lr`/`sp` to a frame, passes a buffer range to the monitor via `mcrr p15, 0, r2, r1, c12` (`r2 = r1 + 0x60`),
- `cpsid if`, then cache/TLB maintenance: invalidate unified TLB (`c8,c7,1`), clean D-cache if `SCTLR.C` set (`c7,c10,0`), `DSB`/`DMB` (`c7,c10,4/5`), flush prefetch + branch predictor (`c7,c5,4/6`), clears `SCTLR` bit 12 (I-cache),
- **`smc #0` with `r0 = 0`** - so the operation selector is NOT in a register, it is in the shared buffer / the `c12` pointer, not the SMC immediate.
The secure request content (the periodic type-9 payload) is built behind securitykext's generic indirect-dispatch (`FUN_801442c4`, callback ptr lost by Ghidra) and is not yet recovered. The monitor and its shared page (`0x1ffec000`) survive our handoff (we never touch them), so a bare-metal `SMC #0` is reachable after killing symbian. NEXT: an `SMC #0` round-trip probe from kickloop (faithful prologue, zeroed buffer, log return via the securetrace RAM channel) to confirm we can enter/exit the monitor safely; then recover the shared-buffer request format to reproduce the 300 s keepalive. Tools: capstone scripts in the scratchpad; stub bytes at file `0x145050` of `work/ghidra/kern/kern.bin`.

## Todo
- egpio (gpio bank layout, irq), lightpdd (backlight - probably via the PMIC on cbus/i2c), keypad_exp, MEDSD (sd host), i2c0, mednand (onenand), the debug uart (if there is one - ekern Kern::Printf goes to XTI/ram).
- fast counter frequency (live: diff vs ms tick).
- Linux side: irqchip (`0x20400000`), clockevent (ms timer, irq 242) + clocksource (`0x20033350`), musb glue, uart driver