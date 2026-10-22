# RapuYama 240-second reset: fixed on Nokia E6

Investigation of the resets that previously stopped Linux on the E6. Initial findings were consolidated on 18.10.2026 from hardware experiments and firmware reverse engineering; the fix was confirmed on hardware on 22.10.2026.

**22.10.2026: FIXED ON THE NOKIA E6.** Kernel **#196** successfully services the secure **PA_SWD watchdog, command 0x1f**. The phone remained running through four successful calls, the last at Linux log timestamp **648.158660 s**, beyond ten minutes and multiple former 240-second deadlines. Both transport and protected-application status returned **1** each time. [Original console evidence](secure-watchdog_SUCCESS.txt).

The SoC and PMIC watchdogs were already being fed; the missing third watchdog was PA_SWD. ISASW normally services it every **timeout - 30 seconds**: a returned timeout of 240 requests the next call after 210 seconds. Linux now continues that service using the preserved firmware context. The old conclusion that a dead ISI link required restoring the full modem stack was wrong; neither the ISASW scheduler nor a modem keepalive protocol is required for this fix.

## Use the confirmed fix

- Build the kernel with `build-vc.sh` (below), copy `Image` and the RGB565 DTB `e6-vc-565.dtb` to the phone as `E:\Image` and `E:\e6.dtb`, then boot with the **e6calb** loader. Kernel #196 was validated this way.
- The DTB must have **`e6.swd=1`** in `/chosen/bootargs`. The bridge is disabled by default, and the checked-in `e6-vc-565.dts` does not set it yet: add it before building.
- Check `dmesg | grep rapuyama-swd`: initialization must reach `PA_SWD 0x1f ready`, followed by repeated `stage 60 transport 1 result 1 timeout 240 token 240`. A failed call logs the failure and stops further attempts.
- The implementation depends on the preserved ISAH handoff, firmware mappings and signed modules from this E6 firmware. It checks the device and firmware signatures. Other firmware versions and other RapuYama devices need separate validation.

For a rebuild, use the full project build from the repository root:

```sh
KERN_BIN=/path/to/kern.bin ./linux/kernel/build-vc.sh
```

Keep `e6.swd=1` in the boot DTB paired with the rebuilt Image. The old e6calb phase wait only extended the time available for investigation; it is no longer the mechanism keeping Linux alive.

The following sections retain the investigation history. Sections 0.10–0.11 give the implementation and hardware result; earlier unresolved hypotheses describe what was known at that point.

## 0. 22.10.2026: reset at a fixed phase, with a 210 s interval

### 0.1 How it was measured
- `e6calb.sis` = e6bfix built with `RB_DEFS="-DRB_IMAGE_BOOT -DRB_CALIB"` (uid 0xe1000026). Right before the handoff the ldd writes a `CALB` block into the isah page (phys 0x1f0f1000, words 0x100..): fast counter at the handoff, securitykext's 300 s NTimer (object `*0xc800d1c0`, timer +0x10, trigger tick +0x20, state +0xa), `NKern::TickCount` + `TickPeriod` (1000 us), the symbian tick when the booter started.
- the kernel (`rapuyama_isah_init`) prints it, the heartbeat prints `e6: alive, ..., symbian uptime N s` every 10 s, the flight recorder keeps the 32 kHz fast counter, so the reset time is known on symbian's clock to ~0.1 s.
- `python3 tools/parse_calib.py logs/klog.bin [logs/calib.csv runN]` prints the whole timeline (`--summary` for many runs).

### 0.2 Runs
| run | booter started | handoff (symbian uptime) | lived | death (symbian uptime) | death - 240 |
|---|---|---|---|---|---|
| first 5 runs (v2) | - | 106.6 .. 185.2 s | 54.8 .. 133.4 s | 240.0 s | 0 |
| klog1 | - | 84.9 s | 155.1 s | 240.0 s | 0 |
| klog2 | - | 136.4 s | 103.6 s | 240.0 s | 0 |
| klog3 | - | 137.1 s | 102.9 s | 240.0 s | 0 |
| 6 min in symbian | - | 441.2 s | 217.9 s | 659.1 s | 419.1 |
| 3:45 in symbian | 267.0 s | 273.2 s | 175.8 s | 449.1 s | 209.1 |
| irq deltas | 78.3 s | 85.9 s | 154.1 s | 240.0 s | 0 |
| changed bits | 273.9 s | 280.7 s | 168.4 s | 449.1 s | 209.1 |
| pmic dump | 99.4 s | 106.5 s | 133.5 s | 240.0 s | 0 |
| wait for service | 98.9 s | 212.3 s | 236.8 s | 449.1 s | 209.1 |
| timer capture | 314.0 s | 421.4 s | 237.6 s | 659.1 s | 419.1 |

At this stage, two models fit these deaths: **(A)** Symbian services a 240 s watchdog at uptime 0, ~209.1 and ~419.1 s (`210*k - 0.9 s`), and Linux stops servicing it; **(B)** another processor or hardware checks APE state at ~240.0, ~449.1 and ~659.1 s with a grace period after losing Symbian. The capture in section 0.7 found no 210 s Symbian timer, so model A was not yet established. Sections 0.10–0.11 later identified and validated its actual PA_SWD service. The first five runs only looked tied to securitykext ("-60.3 s after its request was due") because its timer is armed once at boot (trigger ~300312) and never fired before those handoffs: 300.3 - 60.3 = 240.0.

### 0.3 Ruled out
| what | how |
|---|---|
| linux | bare kickloop dies the same (earlier), death time doesn't depend on the handoff |
| charger | many runs with and without the cable |
| securitykext 300 s request | the booter fired it itself at the handoff (`FUN_80140ea4({0, 0, 9})`, answer 0): death didn't move |
| SoC + PMIC watchdogs | fed; PMIC reg 0x17 reads back 0x30/0x2f (48 s countdown) |
| ISASW hw_wdog | `FUN_c62da8c0` kicks every 10000 / 32000 ms, same two watchdogs |
| PMIC timers | `rapuyama_wdt.pmic_dump=10`: the 53 regs of karap's cbus table every 10 s, only RTC (1.03/1.04), irq status (1.01 bit 3 = RTC minute) and a battery current like ADC (2.0d) move, nothing counts to 240 s |
| a modem ping before the reset | per 10 s irq deltas + changed bits of every flight recorder reg (SISA 0x20040280, secondary irq banks 0x334xx, clocks, gpio): nothing new around the service or death - 30 s |
| a 210 / 240 s constant | none in the kern ROM or ISASW; in the 2397 ROFS executables (`work/rofs-flat`) only unrelated hits (java, flash, sip, pki) |

Not found yet: **whether Symbian does anything at the inferred ~210 s phase.** Sampling Symbian thread cpu time around that phase (`RRunBlob::WatchService`, `tools/parse_svc.py`) returned all zeros; cpu time accounting is off on this ROM. Inspecting live Symbian kernel structures further was not pursued.

### 0.4 Historical phase workaround (superseded by PA_SWD servicing)
`e6calb` after the Boot dialog lets the ldd sleep until just after the inferred 210 s phase and hands off then: linux lives **~237 s every time** (instead of a random 30..240 s). This extended the investigation window before the PA_SWD service was identified. Symbian dialogs (RNotifier) show load/alloc errors, "Retry" on -4, and the final Boot confirm. The ldd also frees the boot block when the booter quits (the old empty destructor leaked up to 160 mb per aborted run, the random -4 after a failed start) and runs `TRamDefragRequest::DefragRam` between alloc rounds.

### 0.5 Side findings (22.10.2026)
- securitykext's periodic request (type 9) is an ISI message: resource 0x08, msg 0x13, data `00 20`, on IAD channel 0xa1 (opened in `FUN_80143c2c`), on the wire `15 00 6c 08 06 00 00 a1 00 13 00 20`, it waits up to 5 s for the answer.
- the real ISI send path: IAD op 0xd -> `DRouter::Send` `FUN_800fd300` -> ROM SetSenderInfo `FUN_800fd01c` -> link = sisa_wrapper (vtable 0x801050ac, send 0x80104d58 -> `FUN_80103338` -> `FUN_8010445c`: deferred call ring + **software irq line 30 on the APE itself**) -> ISASW running on the same ARM. Nothing in it writes 0x40100000.
- so section 4.1 is most likely the **XTI trace** port, not the modem link (`driver-re.md` and `hw-memory-map.md` call 0x40100000 XTI; per channel 0x1000 windows + start/end bytes + 0x401fd800 is the trace format). The EM frames of section 5 probably only went to the trace.
- section 4.3 is wrong for the IAD path: `FUN_800fd01c` sets media 0x15, sender dev 0x6c | (ch >> 8 & 3), sender obj = ch (special cases ch 0x2c and 0x36).
- the EM resource is 0x47 (the frame), the 0xA9 in the header comment of `rapuyama_modem.c` is stale.

### 0.6 Timer queue capture (22.10.2026)
- the type 9 request tested in 0.3 is not the `PA_SWD` path (`securitykext` 0x80142d48 / 0x80142ea0, `secldd` 0x80137684 / 0x801377dc). Its failure to extend survival does not exclude every secure watchdog path. The old raw SMC experiment also does not establish that. No periodic caller had been identified at this stage; section 0.10 later found it in ISASW.
- scanned the decompressed demand-paged core ROM as well as the already available kernel material. No useful literal 210000 ms, 210000000 us or 240000000 us timer interval was found in the core modules. A literal search cannot exclude computed intervals, tables or timers outside Symbian.
- the existing `logs/dram` dump resolves `NTimerQ` at 0xc80006d0, `K::TickQ` through 0xc8000604 and `K::SecondQ` through 0xc8000608. Most linked timer objects map to physical pages outside the captured 0x10000000..0x115fffff, so the old dump cannot identify the service. The firmware timer layouts differ from the checked-in SDK.
- `e6tmrs.sis` is a separate build of the existing Image loader: `RB_DEFS='-DRB_IMAGE_BOOT -DRB_CALIB -DRB_TIMER_CAPTURE' ./build_app.sh e6tmrs boot_app.cpp 0xe1000040` in `linux/stage0/symbian-loader`. It uses the same `E:\Image` and `E:\e6.dtb`, waits for the inferred service, captures 36 snapshots from -3 s to +0.5 s, writes `E:\timers.bin`, then boots Linux. The ordinary e6calb path is unchanged without `RB_TIMER_CAPTURE`.
- snapshots include the nanokernel, tick and second queues, callback addresses, deadlines and owner names for recognized DTimer callbacks. Traversal is bounded by 512 records and a 5 ms counter budget per snapshot, with incomplete or racing reads flagged. It does not hook handlers or modify timer objects. The expected service time is still inferred, not observed by the capture code.
- parse with `python3 tools/parse_timers.py logs/timers.bin` (`--all` for every timer). A timer expiring near the service and rearming ~210 s later would identify a concrete handler or process to investigate. Absence is inconclusive if snapshots are partial or the service does not use these queues.
- host validation: SIS build passed; the actual capture header was exercised with mocked Symbian memory for queue traversal, owner names, invalid links, time limits, aligned reads and output framing; parser checked deadlines, names and truncated input. Capture writes one 64 KiB frame at a time, so the kernel does not need a 2.36 MiB allocation. The corrected sampler then ran successfully on the phone; see section 0.7.
- first phone run of `e6tmrs` hung Symbian completely for over 6 minutes before Linux boot; MTP and input stopped. Two defects were found in that sampler: it held the timer mutex and masked interrupts while using `Kern::SafeRead` on timer-linked addresses, and `TimerName` called `SafeRead` with a byte-aligned address/arbitrary byte count although the API requires word alignment and a nonzero multiple-of-four size. Either defect could explain a hard hang; no trace proves which occurred. `logs/boot-frozen-run.log` stops after `calib: window 0 s`, but the old booter did not log the Boot dialog, countdown or capture start, so it cannot locate the hang. Do not use that SIS again. The sampler now reads without taking the mutex or masking interrupts, reports races as partial snapshots, and reads descriptor bytes through aligned words.

### 0.7 First live timer capture (22.10.2026)
- `logs/timers.bin` is complete: 36 snapshots from -2999 to +501 ms around the inferred 419.1 s service, 29-31 timers per snapshot, no unreadable links or time-budget flags. The scan took 0.40-0.49 ms per frame with interrupts enabled.
- No captured `NTimer`, tick-link or second-link had a deadline at the service followed by a ~210 s rearm. The timer expiring at +2 ms (`c8001c44`, DFC `0x800829b4`) is the kernel's 1 s activity timer, not a 210 s event. The securitykext timer (`c822bf88`, callback `0x80141044`) remained due ~181 s after the service.
- The Linux log for that run (`logs/klog.bin`, #190) has no Oops/panic and stops near Symbian uptime 659.1 s = 419.1 + 240 s. The cause of that timing may be a shorter periodic callback, an IRQ, another CPU or secure code; the capture does not distinguish them. Treat 419.1 s as an inferred phase, not a measured Symbian service.

### 0.8 What the existing runs exclude
- `wait for service` handed off at 212.3 s and ran through 240.0 s until 449.1 s. `timer capture` handed off at 421.4 s and ran through 449.1 s until 659.1 s. Therefore an unconditional autonomous reset at each fixed 210 s phase is already excluded. An autonomous check with a grace period between ~28 and ~55 s after losing Symbian remains possible; the earlier handoff at 185.2 s died at 240.0 s.
- A handoff at 230 s would be within that grace period and would not distinguish the remaining models. No such phone run is needed. A 240 s timeout refreshed near 209/419 s remains compatible with all observed runs, even though the timer queue capture did not find its source.
- The more useful missing input is live ISASW RAM. `logs/dram` contains the decrypted ISASW code but no `0x1f8b0000` runtime chunk, which contains the dispatcher object at `0x1f8b29c0` and other channel state. The first `1f8b0000.bin` is enough to begin resolving the live pointers. Capture kernel RAM and page tables in the same boot as the ISASW state: old dynamic pages from another boot cannot establish a coherent set of runtime pointers. See section 0.9 for the corrected capture path.

### 0.9 Failed runtime dump and corrected access path (22.10.2026)
- The first `dram_dump.py` phone run uploaded the helper successfully but returned zero bytes for every runtime chunk. No new RAM files were produced. The old shell pipeline `/tmp/md ... | base64` hid the reader's exit code and the script reported `done` after skipping every failed read.
- The #190 configuration has `CONFIG_STRICT_DEVMEM=y`. The DT reserves `0x1f8b0000..0x1fcfcfff` as mapped RAM, so it remains under System RAM in the ARM resource tree. `drivers/char/mem.c:mmap_mem` rejects it through `range_is_allowed` / `devmem_is_allowed`, which rejects `page_is_ram` even for reserved pages. The exact errno was not recorded by the old helper, but this kernel path denies the requested mapping. `sudo` on the host does not change that.
- Added `/sys/kernel/debug/rapuyama-shm/{11525000,1f8b0000,1ffe0000}` using the shared-RAM watcher's existing mappings. The files have mode 0400 and file operations with no write handler. They expose only the three fixed preserved regions; the `1f8b0000` file is exactly `0x44d000` bytes. The normal `e6.shmwatch` setup must be enabled, as it is in the daily DTB.
- The collector mounts debugfs, checks for the runtime file, reads it with `dd`, and retains the reader's exit status and stderr before encoding data. It rejects failed, short or malformed transfers instead of skipping them. The last runtime chunk is `0xd000` bytes at `0x1fcf0000`; the old full-sized final chunk extended beyond the reservation. Runtime RAM, current kernel data and page tables now precede code dumps.
- Full canonical `build-vc.sh` passed, kernel #193. Its `Image` contains the read path, paired with the #190 RGB565 DTB. Copy them to `E:\Image` and `E:\e6.dtb`, boot with e6calb, then run `sudo python3 tools/dram_dump.py /dev/ttyACM0 <dir>`. Use a fresh directory for this boot's state. Host validation exercised real shell `dd`/base64 transfers at the first, middle and final offsets, reader failure reporting, CRLF framing, empty and corrupt output, and Python syntax. Phone capture subsequently succeeded: the capture contains the complete runtime and coherent page tables used in section 0.10. This capture change enabled finding the reset fix.

### 0.10 PA_SWD command 0x1f and the Linux fix
- `logs/dram-reset` is complete: all expected low-RAM and runtime chunks have the exact sizes, including the last `0xd000` bytes. The dispatcher at guest VA `0xc68029c0` points to `0xc800a6f4`, whose host callbacks resolve through this boot's captured kernel page tables. The real Symbian send function queues ISASW entry `0xc6252ba7`; it does not send through the XTI port.
- The secure API state at `0xc6819080` contains the protected application table pointer `0x90844000` (physical `0x108c5000`). The table's PA_SWD entry has offset `0x32a0`, length `0x2240`, physical header `0x108c82a0`. Extracted bytes and disassembly are in `work/codex-memory/PA_SWD.bin` and `.asm`; the Flatpak Ghidra project is `work/ghidra/pa_reset`, decompilation `work/ghidra/PA_SWD.bin.c`. PA_MODEM was also extracted and decompiled; it is not the identified service path.
- ISASW security-server initialization `FUN_c62587da` initializes PA_SWD and registers the signed firmware modules. The timer callback `FUN_c6258b5c` calls `FUN_c6258ab4`, which sends PA_SWD command **0x1f** through `FUN_c6262b38`. It supplies the saved 16-bit value from `0xc6818f40`, consumes an 8-byte response `{status:u32, timeout:u16, next_value:u16}`, and schedules the next call at **timeout - 30 s** (converted to 5 ms ticks). This dump has timer handle `0x31` at `0xc6818f3e` and saved value **240** at `0xc6818f40`.
- PA_SWD's command-0x1f handler (`FUN_00001abc`, file offset `0x1abc`) validates registered module state and calls secure API offsets `+0x94` with `(timeout + 1)*1000` and `+0x9c`, then returns the timeout and next value. The exact secure API implementation is not in this dump, but this is a concrete watchdog-service path that explains the measured 240/210-second timing. Earlier type-9 and raw zero-SMC experiments did not exercise this command or transport.
- The original ISASW wrapper uses a separate secure channel: context physical `0x1ffe2000`, outer command `0x1f`, PA header physical `0x108c82a0`, request/response physical pointers, PA mode `2`; it enters the monitor through `FUN_c62646a9`. Its pointer validator accepts only buffers in the secure shared-RAM pool `0xc670202c..0xc6708000`. Consequently the Linux bridge allocates and frees its two 8-byte messages through the original firmware pool functions rather than passing arbitrary Linux pointers.
- Added `arch/arm/mach-rapuyama/secure_wdt.c` and `secure_wdt_call.S`, explicitly enabled with `e6.swd=1`. It verifies the E6 handoff and firmware entry signatures, uses the saved Symbian page tables, borrows the stopped server task's mapped stack, invokes only the secure-buffer allocator / PA_SWD wrapper / free functions, restores Linux's MMU registers, stack and interrupt masks, and checks both transport and PA status. It sends its first request five seconds after initialization and rearms at the returned timeout minus 30 seconds. A failed response stops further attempts and logs the result. The existing modem scheduler experiments remain disabled.
- Host validation ran the compiled ARM trampoline plus the **real ISASW wrapper and VA-to-PA path** in Unicorn against this runtime dump. The actual secure monitor was mocked: the tests verify the exact seven transport arguments, subtype 0x1f, request value 240, response decoding, callee-saved registers, CP15/stack/interrupt restoration, allocation-failure cleanup and PA failure propagation. They do not prove that the physical monitor accepts the call. Test source and log are kept under `work/codex-memory`.
- The hardware-validated build is kernel **#196** `Image` with the proven #190 RGB565 DTB plus `e6.swd=1`. The full canonical build passed without compiler warnings. The existing e6calb boots this pair. The following hardware run confirms the real secure monitor accepts the calls and the former reset deadline no longer stops Linux.

### 0.11 Hardware victory (22.10.2026)

The user reported this console output from the E6 running the #196 pair:

```text
e6:/# dmesg | grep rapuyama-swd
[    1.410736] rapuyama-swd: PA_SWD 0x1f ready, token 240 guest stack c68fdc28
[    6.486267] rapuyama-swd: call 1 stage 60 transport 1 result 1 timeout 240 token 240
[  218.083435] rapuyama-swd: call 2 stage 60 transport 1 result 1 timeout 240 token 240
[  433.121124] rapuyama-swd: call 3 stage 60 transport 1 result 1 timeout 240 token 240
[  648.158660] rapuyama-swd: call 4 stage 60 transport 1 result 1 timeout 240 token 240
e6:/# date
Thu Jan  1 00:12:19 GMT 1970
```

Four successive calls completed at stage 60 with transport **1**, PA result **1**, timeout **240** and next value **240**. Linux was still serving the USB shell after the fourth call at **648.158660 s**. This confirms the fix on this E6 beyond ten minutes, spanning multiple former reset windows. The `date` output is the reported epoch-based system clock, not an independent monotonic uptime measurement.

The workqueue requests `(timeout - 30) * HZ`; the observed printk timestamps are approximately 212–215 seconds apart. These are the recorded values, not a claim of exact 210-second wall-clock spacing. No full modem runtime or ISI keepalive was enabled to obtain this result. This run validates the former 240-second reset fix; it does not establish indefinite uptime or suspend/resume support.

## Archived findings from 18.10.2026

**Historical record, not the current diagnosis.** Sections 1–8 below preserve the original experiments and conclusions. The categorical BB/dead-ISI-link explanation in sections 2–3 and 6–7 was disproved by the PA_SWD fix above. The presumed FIFO at `0x40100000` was subsequently identified as the XTI trace port; the old 48-second SoC-watchdog figure was also corrected to approximately 13 seconds (48 seconds belongs to the PMIC).

## 1. The symptom

After trampoline hands control from Symbian to linux kernel, the phone **warm-resets after ~50–220 s**, every time. Linux itself boots fine to that point: rapuyama drivers come up, the VideoCore display initialises, the keepalive/watchdog feeding runs. Then the SoC resets.

- The reset is a **warm reset**, not a brick (RAM persists; ramcon log survives it).
- It is **not** linux crashing: no panic/oops/data-abort in the log before the reset. The log just stops and the next boot starts.
- `sched=0` + surrogate watchdog feeding is the most stable config and reaches the top of the window, ~220 s, consistently.

## 2. Hardware architecture (as relevant)

- APE (application processor): **ARM1176JZF-S, ARMv6K** (MIDR 410fb764 r0p4), the core linux ran on. RAM base 0x10000000, 256 MiB.
- **Separate modem / baseband (BB) CPU** - the cellular subsystem. It is the **SoC-reset master**: it can reset the whole phone, and it is what resets us. Its firmware is NOT in anything anyone has in free possesion.
- **ISASW** = the cellular MCU software, a Nokia RTOS (Master Terminal Control). Crucially it runs **on the APE ARM core**, time-shared with symbian via the `karap` IRQ dispatcher's cb[0..2] hooks. We can and do host it (below).
- Modem ↔ APE link: a byte **FIFO** at phys 0x40100000 (+chan·0x1000), doorbell 0x401fd800, plus a SISA inter-processor block at 0x20040280, DMA-to-RAM for RX, shared memory (SHM) reservations at phys 0x1f8b0000 (4.4 MiB) and 0x1ffe0000.
- Clock/power controller 0x20032xxx/0x20033xxx, PMIC on CBUS 0x20030700, SoC watchdog 0x200336c4, INTC 0x20400000.

## 3. Resets - narrowed mechanism

### 3.1 It is EXTERNAL to the APE software
- It survives **both** watchdogs we feed: the SoC WD (0x200336c4, 48 s, kicking it ~1/s, "wd kicks 240 fails 0") and the PMIC WD (CBUS dev1 reg 0x17). If those were the cause of resettin at <=48 s, we reset at ~220 s.
- It is **not** ISASW's own software reset. ISASW's only SoC-reset primitive is `FUN_c62ef9c0` (in bse_sisa.c), and it has **exactly one caller**, `hw_reset_generate` (`FUN_c62da64c`). On every hosted run the reset generated flag (`c68265e0`) stayed **0** (`rgen=0`) and the modem-sw reset reason read back `valid=0`. So ISASW did not issue the reset.
- Therefore the **BB/modem hardware** resets the APE.

### 3.2 Why the BB resets the APE
The BB resets the APE when the APE stops **servicing the modem link**. i.e. when the APE side of the ISI protocol goes silent (no responses to the modem's requests, no power-state reports). Under symbian this never happens because the full modem-services stack runs, under linux it has nothing driving it. Supporting evidence from symbian:
- idle Symbian streams Energy-Management (EM) "sleep/awake" ISI messages on every power transition and never gets reset.
- a bare-metal spin (no ISI activity at all) dies at ~200 s, like linux boot.
- the modem fires a **line-30 status IRQ ~every 30 s**, symbian answers.

## 4. The ISI/EM link fully decoded

### 4.1 Transport (leaf `FUN_800beac8`, ASIC vtable[0x48])
```
port = 0x40100000 + chan*0x1000;
*port = 0xc0;
for (i=0; i<len; i++) *port = msg[i];
*port = 0x00;
*(u8*)0x401fd800 = chan;
```
The real ISI send (`FUN_800be920`, vtable[0x28]) enqueues to a SISA deferred-call ring that is later drained to this same leaf. i.e. the hardware path is this byte FIFO, fire-and-forget, on a drainer-chosen channel. The clock-bring-up for the FIFO domain is ecust `FUN_800bede4` (mux `FUN_800bec88(3,0xf,2)` at 0x20032644 + trigger 0x2003264c pulsed 0x9480/0xd400/0x9480, then 0x200330b8=1, 0x20033084=8). During boot the FIFO domain is **already clocked** (symbian left it up). Writes to 0x40100000 do NOT external-abort.

### 4.2 The EM "awake" message is 24 bytes (empmIsaIf `FUN_80101364`, case 1)
Built by isimessagekernel primitives into a zeroed buffer; only the body offsets are set, the message is finalised to length 0x18 (24):

```
m[0x03]=0x47 (resource) m[0x04]=0x12 m[0x05]=0x00 (length = 0x18-6)
m[0x09]=0x04 (msg id)
m[0x0a]=0x02 m[0x0b]=0x01 m[0x0c]=0x05 m[0x0d]=0x0c m[0x0e]=0x04 m[0x0f]=0x04
m[0x10]=<EM state byte> (*(piVar8+6), runtime power state)
m[0x14..0x17]=<counter + 0x111, big-endian> (*piVar8, monotonic)
m[0x00,1,2,6,7,8]=0 (phonet routing header, see 4.3)
```

SLEEP is the same but m[0x0e]=0x05 and no 32-bit counter.

### 4.3 The routing header which i couldn't get
Bytes [0,1,2,6,7,8] = media, receiver-device (modem), sender-device (APE), receiver-object, sender-object, transaction. **The isimessagekernel builder zeroes them and nothing in the APE send path (empmIsaIf / IAD opcode 0xd / the LDD / the ASIC leaf) fills them**. So on the wire they are 0 in Symbian too, identical to ours. They are therefore NOT the differentiator for routing at the ISI layer, the **FIFO channel** is what selects the destination. I could not find these as static constants (they are, where used at all, runtime IAD connection state), and an on-wire capture is not possible on this phone (see 6).

## 5. Everything i tried on hardware (survival vs the ~220 s baseline)

| # | Config | Survival | Read |
|---|---|---|---|
| - | sched=0, keepalive mask=0x7, NO EM | **~219, ~219 s** | baseline (two runs) |
| #90 | run ISASW scheduler cb[2] | **instant (~4 s)** crash | cb[2] dispatches real cellular task handlers -> hard reset on 2nd tick |
| #94 | +sisa doorbell pulse (keepalive bit4) | ~190 s | worse; sisa poke ruled out |
| sweep | EM on FIFO **chan 0** | **229 s** | chan 0 stands out |
| sweep | EM on FIFO chan 1..8 | 99–119 s | wrong channels disrupt other links -> **chan 0 = the EM channel** |
| #101 | EM chan0, burst of 8 state bytes/period | ~30 s | too many frames = errors -> fast reset |
| #102 | EM chan0, 1/30 s, high counter 0x10000000 | ~120 s | high counter not better |
| #96 | EM chan0, 1/30 s, counter 0x111, state 0 | ~63 s (one run) | within the random window |
| #103 | feed ISASW its modem IRQs (from hard-IRQ) | **~1 s** | unsafe: isasw_call (MMU switch) from hard-irq context |
| #104 | feed ISASW IRQs (deferred to timer ctx) | **~2 s** | unmasking modem lines 21/36 destabilises fast anyway |

Notes:
- EM frames NEVER extended survival beyond the ~220 s baseline. Best case is "neutral" (chan 0, 1/30 s ≈ baseline); everything else is harmful. The frame is byte-identical to symbian's and i've swept channel (0 confirmed), the state byte (0..7) and the counter. None helped.
- Running ANY real ISASW code (the scheduler cb[2], or the per-line ISRs) in partial host **self-destructs within seconds**. ISASW expects a live runtime (DSP/sibling cores/full ISI stack) that does not exist under linux.
- A scan of the preserved SHM (both 0x1f8b0000 and 0x1ffe0000) for a real EM frame found **nothing** (only 0x47 bytes inside c6xx pointer tables = noise). EM frames go out via the hardware FIFO and are not buffered in a readable form in SHM.

## 6. why it is not fixable from the APE side with what we have

1. **Run the modem scheduler (cb[2])** -> instant crash. It dispatches real cellular task handlers into an environment (DSP, shared mem, FIQ, sibling MCU cores) that is not there.
2. **Keep the link alive by feeding ISASW its device IRQs** -> its real ISRs run in the partial host and it self-resets within ~2 s (same wall as #1).
3. **Fake the EM keepalive externally over the FIFO** -> never beats baseline; a bare fire-and-forget frame is not what keeps the BB happy (the BB wants the APE to actually service the ISI session, not just emit frames), and we can't form a fuller/true session without the BB-side protocol.
4. **Capture what Symbian really sends** -> Symbian captures return nothing on this phone (established across the whole project, a hard rule), so we cannot read the exact live ISI traffic.

Net: the reset is the BB resetting the APE for a **dead ISI link**, and reviving teh link needs either (a) ISASW's real runtime (impossible under our partial host) or (b) the BB-side ISI/EM keepalive protocol, which is not in any binary i have.

## 7. The exact open question for experts

**On a Nokia BB5 / rapuyama phone (E6/N8/C7), what does the application processor have to do to stop the modem (baseband) from warm-resetting the SoC ~200 s after the APE stops running the Symbian modem-services stack?**
Specifically:
- Is a periodic Energy-Management "awake" ISI message (resource 0x47) enough, and if so, what are its routing header bytes and the EM state byte, and must the sequence counter continue from the last value symbian sent?
- Or must the APE maintain a live ISI **session** answer the modem's line-30 (~30 s) status polls and its other requests (RX DMA'd to SHM), not just emit frames?
- What is the BB's APE alive supervision timer, and which register/handshake resets it?

## 8. Best state before 22.10.2026 (see 0.4 for now)

zImage built with `isasw_host=1 isasw_sched=0 rapuyama_keepalive.mask=0x7`: linux boots, drivers and the VideoCore display come up, and it runs to ~220 s before the modem resets it. All the EM / feed-IRQ machinery is in `drivers/misc/rapuyama_keepalive.c` and `drivers/misc/rapuyama_isasw.c` behind flags, off in this config.