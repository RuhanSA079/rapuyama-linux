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
  then 5 secondary banks in the clock/power block at `0x20033414 + 0x40*k` get the same disable+drain.
- irq loop: `n = rd(+0x08); if n!=-1 -> handler[n]; repeat`, when none `rd(+0x80)`.

## Timers
- **ms tick** (`BindMsTick`) = **IRQ 242**, timer at `0x200337a0`:
  start = `wr(0x200337a4, 0x9600)` + enable 242, isr = disable 242, `wr(0x200337a8, 0x9600)`, `NTimerQ::Tick`, `rd(0x20400080)`, enable 242.
  `0x9600` = 38400 -> 1 ms at 38.4 MHz.
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
fclk 19.2 MHz, or pll/div (`0x20033130` bit 14, div sel `0x20033010` bits 12-14, max 24 MHz). -> mainline `i2c-omap` with `ti,omap2420-i2c`.
Keypad = **LM8320** on i2c (keypad_exp.dll: `DIOExpHwLM8320ST/NSC/Toshiba`), mainline has `lm8323` (platform data only, needs dt glue).
- i2c addr **`0x44`**, irq = gpio pin **42** (bank 1 bit 10, intc 358), config byte `0x87` = 8 rows x 7 cols, uses the LM8323 command set (`0x91` = READ_CFG).
- fifo code = `row << 4 | (col + 1)`, map at kernel data `0xc800f514` (filled from rom `0x801e3610..`): `01 Q 02 W 03 E 04 1 05 2 06 3 07 * 11 A 12 S 13 D 14 4 15 5 16 6 17 # 21 Z 22 X 23 C 24 7 25 8 26 9 27 0 31 I 32 O 33 P 34 RCtrl 36 Dev0 37 Yes 41 K 42 L 43 Backspace 44 VolUp 45 LShift 46 App1A 47 App0 51 , 52 . 53 Enter 54 Dev1 55 No 56 ' 57 VolDown 61 LFn 62 Space 64 App1F 65 App1B 66 ] 67 RFn 71 Up 72 Down 73 Left 74 Right 75 Dev3 77 Dev6` (values = symbian TStdScanCode).
- R T Y U F G H J V B N M are NOT in that map -> some other scan path (second expander / direct gpio matrix classes `DIOExpHwDirect36Pin`, `Small25Pin`), get the rest live by pressing keys under linux.
- symbian global gpio id -> bank/bit: id > 105 is id - 18 (egpio `0x800c1900`).

GPIO irqs in linux: `gpio-rapuyama` is a hierarchical irqchip on the (now hierarchical) intc, parent line per pin from the egpio table. Enable/ack/edge/level are from the code, the polarity regs are a guess.

Live check: `driver/regprobe.sis` -> `E:\regprobe.bin` -> `tools/parse_regprobe.py` (i2c REV for i2c-omap, i2c fclk source, gpio state).

## Watchdog (power.dll / sisa_wrapper.dll)
- the pmic is **Retu style on CBUS** (karap reg table `0x800bc268`: dev 1 regs `0x00..0x1e`, dev 2 = Tahvo style `0x00..0x1f`, 16 bit) - same as the N800.
- power.dll kicks every 3 s (`NTimer::Again(.., 3000)`): pmic dev 1 reg **`0x17` = `0x30`** (48 s) + **`0x200336c4` = `0x3fffffff`** (soc counter).
- writing **0** to pmic `0x17` = immediate power off (sisa "Force Power Off").
- CBUS master `0x20030700`: `+0x04` en, `+0x08` busy, `+0x14` read cmd (`dev<<6|reg|0x20`), `+0x18` write cmd (`dev<<6|reg`), `+0x1c` data, `+0x28` bits-1, `+0x38` = 1 before write.
- Linux: `drivers/watchdog/rapuyama_wdt.c` (kicks both, HW_RUNNING so the core feeds it from boot). mainline retu-mfd/retu_wdt would need the pmic irq line (unknown).

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

## Todo
- egpio (gpio bank layout, irq), lightpdd (backlight - probably via the PMIC on cbus/i2c), keypad_exp, MEDSD (sd host), i2c0, mednand (onenand), the debug uart (if there is one - ekern Kern::Printf goes to XTI/ram).
- fast counter frequency (live: diff vs ms tick).
- Linux side: irqchip (`0x20400000`), clockevent (ms timer, irq 242) + clocksource (`0x20033350`), musb glue, uart driver