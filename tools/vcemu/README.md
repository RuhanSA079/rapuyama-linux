# vcemu: run Symbian's display stack from the ROM

Unicorn harness that runs Nokia's display drivers straight out of the E6 `kern.bin` and logs every message they would send to the VideoCore. The Linux driver (`drivers/soc/rapuyama/videocore.c`) replays these sequences. See [`../../docs/videocore-display.md`](../../docs/videocore-display.md) and, for other phones, [`../../docs/porting-guide.md`](../../docs/porting-guide.md).

| File | What |
| --- | --- |
| `emu.py` | ARM emulator: ROM mapped at `0x80000000`, kernel data at `0xc8000000`, ekern calls stubbed or named from `ekern_exports.txt`, helpers for `memcpy`/`memmove`/`memset`, `__aeabi_*div*`, `operator new[]` |
| `run_panel.py` | Panel driver + display chipset + dispcommanddrv + vcdispman + vc_bufmanx. It does the following: loads the module `.data`, runs the static constructors, schedules DFCs/timers, fakes VC replies, then runs install, op 1 (init) and op 3 (refresh). |

```sh
pip install unicorn
KERN_BIN=/path/to/kern.bin PANEL_0A=00 python3 tools/vcemu/run_panel.py > cold.log
grep -E '^(DSPC|DISP|>>)' cold.log
```

- `KERN_BIN`: the kernel ROM (default `ann/kern.bin`). `ekern_exports.txt` must sit next to it (generated from `eabi/ekernu.def`, see `docs/driver-re.md`).
- `PANEL_0A`: what the panel answers to the DCS `0x0a` read (hex, default `1c`, the real E6 value after a VC boot). Op 1 branches on bit 2:
  - `00` gives the cold path (`docs/symbian-display-sequence-cold.txt`)
  - `9c` gives the warm one (`docs/symbian-display-sequence-warm.txt`)

  Use the values your real panel returns.

Output lines:

| Prefix | Meaning |
| --- | --- |
| `DSPC <words>` | Raw dispcommanddrv request (header + payload, little-endian words) |
| `DISP sync/async cmd ...` | dispman requests |
| `>> BUFMAN write image ...` | BFMX image writes |
| `>> request N`, `>> init case N`, `>> chipset power mode N` | State machine trace |
| `(timer N ticks)` | Symbian timer delays, in milliseconds |

Every address in `run_panel.py` is specific to the E6 RM-609 111.140.0058 ROM. For another firmware, re-find them as described in the porting guide.