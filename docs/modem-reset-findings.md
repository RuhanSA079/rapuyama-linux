# rapuyama modem reset: complete findings

Everything learned about the random modem reset that kills linux boot on the E6. Consolidated 2026-10-18 from
~14 on-hardware experiments + full static reverse engineering of the symbian firmware and the decrypted modem MCU firmware (ISASW)

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

## 8. Current best state

zImage built with `isasw_host=1 isasw_sched=0 rapuyama_keepalive.mask=0x7`: linux boots, drivers and the VideoCore display come up, and it runs to ~220 s before the modem resets it. All the EM / feed-IRQ machinery is in `drivers/misc/rapuyama_keepalive.c` and `drivers/misc/rapuyama_isasw.c` behind flags, off in this config.