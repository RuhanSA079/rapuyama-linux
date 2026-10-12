# runblob loader

Kernel-side LDD that loads raw (non-E32) ARM code into executable kernel memory
and runs it. Started as milestone 0 (run `../blob.bin`), now it also holds the
trampoline tests, the fb/watchdog experiments and the zImage booter.

## Files
| File | Role |
|---|---|
| `runblob_ldd.cpp` | the driver, all the controls below |
| `runblob.h` | shared channel interface (`RRunBlob`) |
| `boot_app.cpp` | boot.exe: streams `E:\zImage` + `E:\e6.dtb` and calls `Boot` |
| `runblob_app.cpp` | takeover launcher: calls `Takeover` (one-way) |
| `wdfind_app.cpp` | timer/watchdog finder: `TimedDiff` over `0x30000000` |
| `build_boot.sh` / `build_runblob.sh` / `build_wdfind.sh` | build ldd + exe, differ only in which `*_app.cpp` goes in |

The asm payloads (`blob.S`, `stub2.S`, `tramp3.S`, `fakekern.S`, `takeover.S`,
`../../boot/tramp_boot.S`) get built by `../Makefile` into `.hdr` byte arrays the
driver includes. The build scripts run `make` themselves.

## Controls
| Control | What | Status |
|---|---|---|
| `RunBlob` | copy `blob.bin` into an exec page, run it, return 32 words (sig + cp15 ids) | works (`tools/parse_sysinfo.py`) |
| `IdentityTest` | build + check a 1:1 section mapping, mmu stays on | works (`parse_idtest.py`) |
| `TrampTest` | jump to `stub2`: mmu off, phys write, mmu back on | works (`parse_tramp.py`) |
| `FullTramp` | `tramp3` hands off to `fakekern` at `+0x8000` with boot regs, comes back | works but ~50% flaky (cache/tlb race), `parse_fulltramp.py` |
| `FbFill` | fill ram through an existing kernel va (framebuffer hunt) | found the fb at `0x10480000` |
| `Takeover` | ONE-WAY: mmu off, loop filling fb candidates | screen doesn't update (command-mode panel) |
| `AllocBoot` / `LoadChunk` / `Boot` | zImage booter, see `../../boot/README.md` | built, blocked on watchdog + drivers |
| `TimedDiff` | read A, spin, read B -> free-running counters | built, not run yet |

## Build
```
./build_wdfind.sh # or build_boot.sh / build_runblob.sh
```
Output: `build/runblob.ldd` + `build/runblob.exe`, then package like in `driver/DEPLOY.md`.
Needs `<kernel/cache.h>` in `driver/stage/` (`driver/mkstage.sh` puts it there).

## If it goes wrong
- **Reboot / no file:** check the `.log` on `E:`, it's flushed after every step.
- Get out of a reboot loop by uninstalling the package (or delete `C:\sys\bin\runblob.ldd`).
- One-way controls (`Takeover`, `Boot`) never return, battery pull to recover.