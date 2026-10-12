# Analysis of Nokia E6 core-ROM (RM-609 111.140.0058)

-> unpacked `tools/fpsx_extract.py` -> `work/core/m54_t17_p01.bin`
-> walked `tools/rom_walk.py` (structures from `symbian/oss.FCL.sf.os.kernelhwsrv`).

## ROM overall
| Parameter | Value |
|---|---|
| Format | EKA2, `EPOCARM5ROM` |
| ROM file base | file `0x13c1000` = virtual `0x80000000` |
| Kernel-ROM size | `0x01d00000` (29 MB) |
| Kernel data in RAM | `0xc8000000` .. `0xca100000` |
| Peripherals (virtual window) | ~~`0xE0000000`+~~ turned out to be HAL ids, the real window is `0xc6000000` (see `hw-memory-map.md`) |
| iKernelConfigFlags | `0x67e` (parse by TKernelConfigFlags) |
| Files in ROM | 1805 |

CPU Nokia **RapuYama** (Nokia ASIC, ARM1176/ARMv6K), confirmed by strings `Start RapuYama VCore calibration`, paths `base_rapu/...`. Same as in N8/C7/E7.

## Key base-port modules (all uncompressed, extracted to `work/modules/`)
| File | Load base (vaddr) | Code | Role |
|---|---|---|---|
| `ekern.exe` | `0x80059f88` | `0x8005a000` | EKA2 kernel (iPrimaryFile) |
| **`karap.dll`** | `0x800b7b60` | `0x800b7bd8` | **RapuYama ASSP extension** (iExtensionFile): interrupts, timers, clocking |
| `ecust.dll` | `0x800bd060` | `0x800bd0d8` | Variant/board customization (iVariantFile) |
| `GAPE.DLL` | `0x800bc990` | `0x800bca08` | APE (application processor engine) glue |
| **`bc02_uart1.PDD`** | `0x801c33c0` | `0x801c3438` | **UART** debug console (target for earlyprintk) |
| `ecomm.ldd` | `0x800dc000` | - | comms LDD on top of UART |
| `egpio.dll` | `0x800c0970` | - | GPIO |
| `mednand.pdd` | `0x80108fe0` | `0x80109058` | NAND/OneNAND media (storage) |
| `medint.pdd` / `MEDSD.PDD` | - | - | internal memory / SD |
| `lightpdd.pdd` | `0x801051f0` | `0x80105268` | backlight/LED - for first "blink" test |
| `dma.dll`, `spi1/2/3.dll`, `EPBUSM(V).DLL` | - | - | DMA, SPI, peripheral bus/I2C |
| `power*.dll`, `dvfs*.dll` | - | - | power and clocking (clock tree) |
| `securitykext.dll`, `secldd.ldd`, `isaaccess*` | - | - | security / modem interface |

## Candidates for MMIO registers (virtual, from literal pools)
All in the `0xE0000000` window (= HAL ids, not addresses, see below). Dominant in `karap.dll`:
- `0xe0800000` (x17) is most likely interrupt controller / system registers
- `0xe082c000`, `0xe0822000`, `0xe0820000`, `0xe0831000`, `0xe0805000`, `0xe0804000`, `0xe0811000` timers/clocking nearby
- UART (`bc02_uart1`): `0xe0410000`, `0xe0411000`, `0xe0451000`, `0xe0424000`
- GPIO (`egpio`): `0xe0100000`, `0xe0840000`, `0xe0850000`, `0xe0810000`

## What this gives and what is missing
These are **virtual** addresses from the Symbian MMU map. Linux needs **physical**
RapuYama bases. They are set by bootstrap/ASSP when configuring the MMU. To obtain physical:
1. **Ghidra on `karap.dll`** - `Init1/Init3` ASSP functions program page tables; there are phys↔virt pairs, interrupt vectors, timer/UART bases.
2. **HCR** (Hardware Config Repository) in ROM - peripheral settings table.
3. **Live reading** on the phone (kernel driver via RomPatcher) - confirm.

## Next steps
- [x] `karap.dll` in Ghidra (ARM LE, base `0x800b7bd8`) -> it's a HAL layer, no direct phys map (see the conclusion below).
- [ ] `bc02_uart1.PDD` in Ghidra -> UART phys base, divider/baud -> earlyprintk. Not possible statically, waits for the adapter.
- [ ] Decode `iKernelConfigFlags=0x67e`, `iDebugPort`.
- [x] Kernel-side LDD for Symbian: read live registers, verify addresses (`driver/`).
- [x] Minimal Linux (ARM1176) builds (`linux/kernel/`), earlyprintk still needs the UART.
- [x] Trampoline Symbian -> zImage (`linux/boot/`).

## Peripheral block map (from literal pools, window 0xE0xxxxxx)
Collected by `tools/arm_litpool.py` (A32 LDR - 0 hits: driver code is **Thumb-2**) and by a direct scan of aligned words across 7 base-port modules. Grouped by 64 KB.
Code is Thumb, so access direction (R/W) and exact offsets are not extracted yet, only an inventory of blocks.

| Region | Weight (frequency) | Who accesses | Presumably |
|---|---|---|---|
| **0xE0800000–0xE08Cxxxx** | ~1700 (dominant) | **karap**, all | system controller, **interrupt controller**, timers |
| **0xE0400000–0xE04Cxxxx** | ~440 | mednand, uart, ecomm | memory controller/**OneNAND**, DMA |
| **0xE0200000–0xE02Cxxxx** | ~250 | mednand | NAND engine |
| **0xE0000000–0xE00Cxxxx** | ~330 | mednand, ecomm | low peripheral |
| **0xE0410000** | - | **bc02_uart1**, ecust, ecomm | **UART (debug console)** |
| **0xE0600000–0xE06Cxxxx** | ~90 | **lightpdd** | **backlight/LED/PWM** (goal of blink test) |
| **0xE0100000** | 16 | egpio | GPIO |
| 0xE0900000, 0xE0A1/A2/A3, 0xE0C/D | small | various | other controllers |

### Important caveat about interpretation
The low bits of the values are structured (…001, …080, …100, …180). Two options:
1. these are virtual MMIO addresses, low bits = register offset within the block; **or**
2. these are **HAL-encoded register IDs** (packed block/index/width) of Nokia's abstraction layer (ISA/ISI), not direct addresses.

Decided only by analyzing how `karap.dll` uses them (a Thumb disassembler is needed -> Init ASSP functions where the MMU is programmed), or HCR/superpage, or live reading on the phone. And in any case these are **virtual** values - the physical RapuYama bases are given by the MMU table from karap Init or the phone.

## Conclusion of the static analysis (Ghidra, karap.dll)
- Base-port code is **ARM (A32)**, not Thumb (Ghidra determines this itself from entry points).
- On a symbol-less relocated image, auto-analysis finds few functions, so out of ~75 words `0xE0xxxxxx` only ~7 resolve as direct LDR loads.
- Usage context: these values are almost never written directly (`str [0xE0..]`), but are passed as arguments to a helper function (pattern `ldr r0,=0xE0..; ldr r1; ldr r2; mov r3,#0; bl 0x800b8118`) or indexed from descriptor tables (`add r3,r0,r2,lsl#2; ldr r3,[r3,#8]`). -> this is a **HAL layer for register access**, not direct MMIO.

**Bottom line:** the inventory of peripheral blocks (above) is reliable. Exact physical bases and
register semantics cannot be obtained by quick static analysis. It is decided by one of two:
1. multi-day interactive reverse engineering (helper `0x800b8118`, tables, MMU in bootstrap);
2. **live reading on the phone** - a kernel driver reads the MMU page tables (phys↔virt for all peripherals at once) and dumps them to a file. **UART is not needed for this** - the result is written to the phone's disk. This is the right unblocker: ground truth without new hardware, through the existing RomPatcher/signature.