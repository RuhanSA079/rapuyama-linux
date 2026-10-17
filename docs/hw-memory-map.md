# Nokia E6 memory map (RapuYama)

State as of 12.10.2026. Everything here is from a live E6 (ptdump/scan/runblob,
dumps in `logs/`) or from the firmware (core-bin reverse), marked in the source column.

## Hard facts
| What | Value | Source |
|---|---|---|
| CPU | ARM1176JZF-S **r0p4**, ARMv6K (MIDR `0x410fb764`, no movw/movt in ARM) | live, runblob cp15 dump |
| Caches | separate I/D (CTR `0x1d992992`) | live |
| Physical RAM | **`0x10000000`, 256 MB** (`0x10000000`-`0x20000000`) | live |
| Peripheral window (kernel virt) | **`0xc6000000`** (NOT `0xE0000000`) | live + firmware |
| TTBR1 | `0x10000018` -> global kernel table at phys `0x10000000`, 16 KB | live |
| TTBR0 | per process (`0x1a48e018`, `0x17828018`, `0x19138018`...), 8 KB table | live |
| TTBCR | `0x00000001` -> **N=1**: VA<`0x80000000` via TTBR0, VA>=`0x80000000` via TTBR1 | live |
| SCTLR | `0x30e5387d`: MMU, I/D cache, branch pred on, **high vectors** (V=1), **AFE=1, XP=1** | live |
| PRRR / NMRR | `0x000aa8a4` / `0xa840a840` (TRE=1 memory type remap) | live |
| boot ROM / vectors | phys `0x00000000` (virt `0x908c0000`), high vectors at `0xffff0000` | live |

AFE=1/XP=1 means section descriptors have to be in the armv6 extended format with
the access flag. Executable normal memory section = `(phys & 0xFFF00000) | 0x40E`
(`0x402` is strongly ordered and can't run code).

## Peripheral map
From the firmware MMU peripheral mapping table (core-bin file `0x13c2f9c`, ROM virt
`~0x80001f9c`, pairs `[phys|size/attr][c6-virt]`), matches the live sweep:

| c6 virt | phys | live size | What |
|---|---|---|---|
| `c6018000` | **`0x30000000`** | 16K | sys/clock controller: indexed regs + pointer table + a LUT. **watchdog suspect** |
| `c6050000` | `0x40000000` | 4K | NOT hardware: kernel debug trace buffer in ram (`Kern::Printf` lands here) |
| `c6f00000` | **`0x40100000`** | 1M | XTI trace port (xtitx/xtiwriter), write-only, reads `FF` |
| `c6070000` | `0xfffd0000` | 64K | exception vectors + thumb handler code |
| `c6011000` | `0x08000000` | 4K | external memory bank config (CS0) |
| `c6010000` | `0x09000000` | 4K | external memory bank config (CS1), almost the same as CS0 |
| `c6020000` | **`0x20020000`** | 192K | **SoC peripheral bus** (spi, uart, dma, gpio, i2c, clock/power, usb), see `driver-re.md` |
| `c6060000` | **`0x20400000`** | 16K | **interrupt controller** |
| `c6012000`, `c60a0000`, `c60af000`, `c60a1000` | `0x20010000`, `0x20600000`, `0x20680000`, `0x20700000` | - | more mmio (NOT dram - ram ends at `0x20000000`) |

Also seen live: virt `0x91d30000`/`0x91adc000` -> phys `0x30000000` (1 page, second mapping of sysctrl).
Low bytes in the firmware table (`0x101/0x901/0x308/0x503...`) = size/attributes. The
table also has ROM pointers (`0x80003010`, `0x80003190`) -> page sub-tables `{off,0x11000}`.

## About `0xE0xxxxxx` (uart `0xe0410000` etc)
**WRONG, see `driver-re.md` (13.10.2026)**: these are arm opcodes picked up by a raw word scan, and `0x8005ca30` is `BTrace::OutFiltered`. Kept below for history.

These are not addresses but **HAL-encoded register ids** in driver config tables
(`bc02_uart1.PDD` etc), passed to the ekern register access helper `0x8005ca30`.
All 9 candidates probed live are UNMAPPED. The helper takes block = `(halID >> 16) & 0xff`
(`0xe0410000` -> block `0x41`), indexes a byte table (pointer at `0x8005c70c`) and
jumps to a per-block handler (`ldr pc,[ip,#0x100]`) that computes the address, so
block -> address is not a flat table and needs live confirmation.
There is no public RapuYama register map (closed Nokia ASIC, no Symbian^3 Linux ports),
and the public Symbian sources only have reference baseports (beagleboard, naviengine, qemu).

## Scan results (scan.exe, `logs/scandata.bin`)
- 280 pages, regs `0x00`-`0x3C` of each, **zero reboots** -> reading the first 64 bytes
  of the mapped pages is safe. The earlier reboots came from the PrimeCell id area
  (`0xFE0`+) and the scary regions (vectors, external banks).
- `0x40100000` (1 MB) reads all `0xFFFFFFFF`.
- Live register blocks: `0x30000000`, `0x08000000`/`0x09000000`, `0xfffd0000` (code).
- uart/timer/intc NOT found among the live blocks.
- printf-diff (`logs/pdiff.bin`) only changed `0x40000000` (+00, +20) = the trace buffer.
  So `Kern::Printf` goes to ram here, no reachable hardware uart with symbian up.
- the unfiltered `0xc0000000`-`0xc8000000` sweep (`logs/map (2).bin`, `logs/map (3).bin`,
  17 runs) only added ram-backed pages to the table above: the `0x20xxxxxx` reserve,
  `c6200000` -> `0x10480000` (4 MB, NOT the framebuffer: the modem's `SOS*ISASW` firmware, see `modem-port-handoff.md` 17.10), `c6800000` -> `0x1f8b0000` (4.3 MB),
  `c60f0000` -> `0x11525000`, `c6700000` -> `0x1ffe0000`. no hidden mmio there.

## Unsafe reads
- Blind mmio reads of unknown space **reboot the phone**, and XTRAP does NOT catch
  external aborts here (the kernel treats them as fatal). Only way is one window per
  driver call with progress saved on disk (scan.exe).
- `Kern::Alloc` memory is XN, code has to go into `Epoc::AllocPhysicalRam` +
  `DPlatChunkHw` with `EMapAttrSupRwx`.

## Display (GPU)
E6 display = **Broadcom BCM2727B1 (VideoCore III)**,
driven via DISPMANX over VCHI on MPHI/CCP2 (`vcdispman*`, `vcdispmanvchi.dll`,
`dispcommanddrv.dll`, `display_chipset_drv_ive_bc.dll`). The VideoCore firmware is
inside the Nokia core-bin and runs on its own. ~~The framebuffer is at phys
**`0x10480000`**~~ (17.10: wrong, `0x10200000..0x10780000` is the modem dsp + mcu
firmware nloader loads into our dram, the fill test wrote over the running modem). The panel is
command-mode: once symbian is dead nobody triggers frames, so writing the fb
shows nothing (`Takeover` confirmed that it does not refresh after the handoff).
Drawing from Linux requires a working VCHI/DispmanX client. Raspberry Pi VCHI
sources are useful protocol references, but its VCHIQ hardware backend is not
compatible with RapuYama's MeSSI/CCP2 transport.

Do not expose the current framebuffer as `simplefb`: the uncompressed kernel
image loads at `0x10008000` and is 19,643,844 bytes, covering
`0x10008000..0x112c3dc4` and overwriting the entire framebuffer range
`0x10480000..0x10880000`. The panel also needs a VideoCore update request;
preserving or mapping the framebuffer alone would not refresh it after handoff.

## Watchdog
Exists (`CWatchdog`, `CWatchdogTimer`, `ShutdownWatchDog`, `mdswatchdog.exe`), resets
the phone seconds after symbian dies. Register is HAL-abstracted, prime suspect is
sysctrl `0x30000000`. `driver/wdfind.sis` (TimedDiff) is built to find the
free-running counters there, not run yet.

## History
- v2 rebooted: wrong TTBR0 mask (16 KB instead of 8 KB with N=1) -> mapped a foreign phys address.
- v3 (cp15 only) first run without a reboot.
- v4-v6: assumed the `0xE0000000` window held peripherals, it doesn't (HAL ids).
- v8: full sweep, real window is `0xc6000000`.
- v9.x: PrimeCell id scan rebooted the phone -> scan.exe (crash-resilient) instead.