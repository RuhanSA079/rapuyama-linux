# Running the drivers on the Nokia E6

Every `.sis` here installs its ldd + exe into `C:\sys\bin\` and **runs the exe
right during install** (waits for it to finish). Output goes to `E:\` (mass
memory, visible from the pc), ptdump falls back to `F:` and then `C:` if `E:` fails.

## Packages
| SIS | Inside | Writes | Notes |
|---|---|---|---|
| `ptdump.sis` | `ptdump.ldd` + `ptdump.exe` | `ptdump.bin`, `ptdump.log` | first ptdump (cp15 + map), uid `0xE1000003` |
| `ptdump-app.sis` | ptdump v9.x | `ptdump.bin`, `ptdump.log` | parse with `tools/parse_ptdump_v9.py` |
| `scan-app.sis` | `ptdump.ldd` + scan.exe (`scan_app.cpp`) | `map.bin`, `scanidx.bin`, `scandata.bin`, `scan.log`, then `pdiff.bin` | rerun until it says DONE, see below |
| `runblob.sis` | `runblob.ldd` + test app | `runblob.bin`, `idtest.bin`, `tramp.bin`, `runblob.log` | M0 + identity map / trampoline tests |
| `wdfind.sis` | `runblob.ldd` + `wdfind_app.cpp` | `wdiff.bin`, `wdfind.log` | timer/watchdog hunt in `0x30000000`, safe. **not run yet** |
| `takeover.sis` | `runblob.ldd` + `runblob_app.cpp` | `runblob.log` | ONE-WAY, kills symbian, battery pull after |
| `boot.sis` | `runblob.ldd` + `boot_app.cpp` | `boot.log` | ONE-WAY zImage boot, see `linux/boot/README.md` |

Parsers for all the outputs are in `tools/parse_*.py` (they default to `logs/<name>.bin`).

## Installing (one tap)
1. Copy the `.sis` to the phone (USB mass storage / Bluetooth / mail).
2. Open it on the phone (file manager -> tap the `.sis`).
3. The installer asks about an untrusted/self-signed certificate -> **allow**
   (goes through on a hacked phone with a patched installserver).
4. The exe runs during install and writes its files to `E:\`.
5. Copy them off the phone into `logs/`.

### If the install fails
- "Certificate error" -> the hack doesn't accept self-signed.
  Options: allow unsigned in settings (App manager -> Software installation ->
  All / Off for online cert check), or sign the SIS with the hack's certificate.
- "Requested application doesn't meet security requirements" (caps) -> the
  binaries are built with `--capability=all`, so the hack has to let that through.

### Fallback: copy by hand with a full-rights file manager
If there's a FM with AllFiles+TCB (signed for the hack): copy the `.ldd` and
`.exe` into `C:\sys\bin\`, then run the exe.
(Stock X-plore can't get into `\sys\`, no AllFiles/TCB.)

## scan.exe
Reading unknown mmio can reboot the phone, so scan.exe reads one page per driver
call and saves progress on `E:` before every read. After a reboot just run it
again, it skips the page that killed it. Rerun until the note says DONE. The
run after DONE does the `pdiff.bin` uart hunt over the live pages.
To start over delete `E:\scanidx.bin`.

## If the phone reboots
Not a brick. Uninstall the package in App manager (or delete the `.ldd` from
`C:\sys\bin\`), reboot. Note what was on screen.

## Rebuilding a SIS
```
# binaries:
bash driver/build.sh # or build_scan.sh, linux/stage0/symbian-loader/build_*.sh
# packaging (ensymble in a python2 container):
cp driver/build/ptdump.ldd driver/build/ptdump.exe driver/sis/pkgroot/sys/bin/
podman run --rm -v "$PWD/tools/src/ensymble:/ensymble:ro,Z" \
  -v "$PWD/driver/sis:/work:Z" -w /ensymble docker.io/library/python:2.7 \
  python ensymble.py simplesis --uid=0xE1000003 --caption=ptdump --vendor=dev \
  --cert=/work/cert.pem --privkey=/work/key_trad.pem --drive=C \
  /work/pkgroot /work/ptdump.sis
```
ensymble patches (host side, outside the container): `utils/cryptutil.py`
(shell=True in runopenssl) and `actions/simplesis.py` (.exe -> EOpRun+Install+WaitEnd).
`tools/mkreg.py` makes the `_reg.rsc`/`_loc.r01` for an app icon (runs in the same container).