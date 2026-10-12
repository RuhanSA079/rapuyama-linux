# Linux on Nokia E6 (RapuYama) bring-up

Goal: boot Linux on the ARM1176 (ARMv6K) inside the Nokia E6, starting from a
patched Symbian (RomPatcher). This is the roadmap. Hardware facts:
`../docs/hw-memory-map.md`.

## Hard facts (from a live phone)
| What | Value | Source |
|---|---|---|
| CPU | ARM1176JZF-S, ARMv6K (RapuYama) | ROM strings + runblob cp15 dump |
| Physical RAM | **`0x10000000`, 256 MB** (up to `0x20000000`) | ptdump v6/v8 |
| Peripheral window (kernel virt) | `0xc6000000` -> phys `0x30000000`, `0x40000000`, `0x40100000` and others | ptdump v8 + firmware table |
| Peripheral phys bases | `0x08/09000000`, `0x30000000`, `0x40000000`, `0x40100000`, `0xfffd0000` | ptdump v8 |
| MMU | TTBR1=`0x10000000`, TTBCR.N=1, SCTLR=`0x30e5387d` | ptdump v3 |
| Display | Broadcom VideoCore (BCM2727), command-mode panel, fb at phys `0x10480000` | firmware strings + fb fill test |

## What's missing
- **Which block is the UART / INTC / timer / watchdog.** Can't be pulled
  statically (HAL IDs in config tables, translated in ekern), see
  `../docs/hw-memory-map.md` "About 0xE0xxxxxx". The 1 MB bank at `0x40100000`
  reads all FF with symbian running (clock gated?).
- UART divider, timer frequency, INTC type.

## Constraint: no serial console yet
earlyprintk to the UART is **not visible** without an adapter, and the display
can't be used either once symbian is dead (VideoCore command-mode panel, nobody
triggers frames). A USB-UART adapter is on the way, most of what's left waits on it.

## Roadmap
### Milestone 0 - run our own raw ARM code (stage0/) - DONE
`stage0/blob.S` gets loaded by `runblob.ldd` into executable kernel memory and
run. Also proven the same way: identity section mapping, dropping and restoring
the MMU in a live kernel, and a full handoff to a fake kernel at `+0x8000` with
the right boot regs (`tools/parse_idtest.py`, `parse_tramp.py`, `parse_fulltramp.py`).

### Milestone 1 - visible sign of life
Blinking the screen from bare metal doesn't work (see above). Next safe phone
experiment: `driver/wdfind.sis` to find the timer + watchdog regs in
`0x30000000`. The rest comes with the adapter.

### Milestone 2 - handoff to Linux (boot/) - BUILT
`driver/boot.sis` streams `zImage` + DTB into contiguous phys RAM and jumps one-way
(`boot/tramp_boot.S`). See `boot/README.md` and `boot/trampoline.md`. Blocked on
the watchdog (resets the phone seconds after symbian dies) and on drivers + console.

### Milestone 3 - kernel (kernel/)
bcm2835 (RPi1, same ARM1176 core) based config, builds. Needs RapuYama
irqchip/timer/uart (or mainline ones if the blocks turn out to be PrimeCell) and
real `reg=` in `boot/e6.dts`. See `kernel/README.md`.

## Toolchain
- `arm-none-eabi-gcc` - bare metal stage0/boot. **ARMv6K: no movw/movt in ARM
  mode**, 32-bit constants go through the literal pool.
- `arm-linux-gnu-gcc 16.2.1` - the Linux kernel (see `kernel/README.md`).
- Symbian GCCE (`../tools/gcce/`) - the loader drivers.