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

## Todo
- egpio (gpio bank layout, irq), lightpdd (backlight - probably via the PMIC on cbus/i2c), keypad_exp, MEDSD (sd host), i2c0, mednand (onenand), the debug uart (if there is one - ekern Kern::Printf goes to XTI/ram).
- fast counter frequency (live: diff vs ms tick).
- Linux side: irqchip (`0x20400000`), clockevent (ms timer, irq 242) + clocksource (`0x20033350`), musb glue, uart driver