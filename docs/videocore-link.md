# VideoCore link: wire-level notes

Lab notebook of the reverse engineering, in chronological order (14.10.2026). Later sections correct earlier ones. For the overview and the current driver, see [`videocore-display.md`](videocore-display.md).

## VideoCore link (vcdriver.dll), resolved against kern.bin, 14.10

Literals resolved from kern.bin, register sequences captured by running the ROM code in a unicorn harness with the Symbian kernel calls stubbed.

## Blocks, clocks, pins

| what | value |
|---|---|
| host bus (PIO) | phys `0x20025800` |
| gfx DMA engine feeding it | phys `0x20024800`, irq `0x92` |
| bus clock | PRM `0x18` EHwClkViSSI -> gate `0x17` = `0x20033090` bit 5 (`+4` set, `+8` clr). clk-rapuyama has no `is_enabled`, so "disable unused" leaves it on |
| other PRM requests | `0x04` EClockSdram, `0x06` EPerformance (during DMA) |
| regulators | `0x30` EHwRegVOutVSel = 9, `0x34` EHwRegVAux1Enable = 2, `0x2f` EHwRegVOutEnable = 2, `0x2d` EHwRegVIoSleep = 4 (2 when off) |
| HAT | gpio 63 (VC -> host attention, level irq) |
| RUN | gpio 92 (high = VC runs) |
| torch | gpio 93 |
| bus pins | Symbian gpio ids `0x6a..0x79` (16 data), `0x7c..0x7e`, set to function mode at init; mux regs `0x200318e0/f0`, `0x20031900` |

## Host bus registers (0x20025800)

| off | what |
|---|---|
| `0x10` | ctrl: `0x0a` = reset (wait `0x14` bit 0), then `0x11` |
| `0x14` | status: bit 8 = bus cycle busy, bit 0 = reset busy |
| `0x40` | init `0x64`, then bits `0x11` cleared; DMA mode sets `0x11` |
| `0x44` | DMA halfword count |
| `0x4c` | W: channel word (sent before a tx stream; 0 before rx) |
| `0x50` | W: one 16-bit data write |
| `0x58` | W 0 = start a 16-bit read; R = data |
| `0x60` | mode: `0x100023` normal, `0x110023` = reads return status |
| `0x64`/`0x68` | cycle timings, from the AHB clock (FUN_8032a6b4) |
| `0x6c` | `0x10` |
| `0x90` | `0x6d60` |

Init (FUN_8032fb08 + FUN_8032a368): `0x10=0xa`, wait `0x14&1`, `0x10=0x11`, `0x60=0x100023`, `0x6c=0x10`, `0x90=0x6d60`, `0x40=0x64`, wait busy, `0x40 &= ~0x11`.

## Framing

- tx: write the channel word (`2 | tx_channel` for boot images, VCHI uses `0 | ch`), then little-endian halfwords. Messages <= 48 bytes go by PIO, bigger ones by the DMA engine (same bus, same result).
- rx (FUN_8032bb18, on HAT): once: channel word 0. Per message: one status read that is dropped, then status reads until bit 29; `len = (st & 0xffffff) + 1` bytes, read as halfwords from `0x58` (max 0x800 per HAT round), then two more status reads.
- status read: `0x60 = 0x110023`, `a = rd`, `b = rd`; if `a & 0x8000`: `a = b, b = rd`; if `b & 0x4000` and the previous status had no bit 29: `a = rd, b = rd`; `0x60 = 0x100023`; status = `a | b << 15`. bit 29 = data waiting, 28 = new message, 27 = rx channel.

## Boot (DVCDriver::Boot, FUN_8032cd50)

- message = 8 words `{0xafe45251, type << 24 | value, 5 words, 0xf001bc4e}` 
- RUN high -> VC sends type 1 (value 1, word 2 = rom id, picks the image set)
- host sends an image on tx ch1 (channel word 3), padded to 16 bytes, then `{magic, 0x02000000 | size, 20-byte trailer, magic}` on tx ch0 (word 2)
- VC: type 3 value 1 = accepted. Loader first, then VC sends type 1 again, then the main image, then type 3 value 1 = booted. 500 ms per wait.
- images live in `unsigned_2727b1.dll` inside kern.bin (vcexe image set registered by its entry 0x8022d4b4): loader A `0x8022d5e8`/`0x2038`, loader B `0x8022f638`/`0x2038` (used at power-up), main `0x80231688`/`0xf6aab`, trailer = SHA-1-sized, at the addresses in `vc_extract.py`.

## Next (as of 14.10)

VCHI (vchi.dll) on top: slot size 0x1020, 16-byte message header with a esrvice fourcc, BULX bulk messages, then the dispman client (`DISPUPDH`).

## VCHI (vchi.dll), from code + emulation

Shared memory (DChunk 0x28320 bytes): 32 rx slots x 0x1010 at +0, 8 tx slots x 0x1020 at +0x20200, scratch at +0x28300.

Global fourccs (static ctor 0x80337ba0): `CTRL` control service, `BULX` bulk, `____` padding, `????` default.

Slots go over the bus as whole transfers on channel word 0 (tx flags 0x50/0x58/0x70); rx buffers are posted 0x1010 bytes each.

Message in a slot (FUN_80331df0), 16-byte aligned entries:
    u16 0xaf05            message magic
    u16 entry size        (header + payload + pad, multiple of 16)
    u32 fourcc            service
    u32 timestamp         (stamped at send time, FUN_803314f8)
    u16 credit            (rx slots the sender has freed, flow control)
    u16 payload size
    payload, zero padded
Padding entry: magic 0x50fa, fourcc `____`. A slot that is closed early gets a pad entry to its end plus a 16-byte 0xaf05 entry (FUN_80331994).

CTRL payloads (word 0 = type, FUN_803350f8 / FUN_80335f0c / FUN_80335eb0):

| type | meaning |
|---|---|
| 0 | connect: {0, flags, slot size, slots, max msg}. VC must report slot size 0x1020; host answers {0, 1, 0x1010, its rx slots, 0x800} |
| 1 | "is service X available?" {1, fourcc, flags} -> answered with type 5 |
| 2 | bulk transfer request {2, fourcc, size, channel byte, ...} |
| 3 / 4 | service flow control off / on {3/4, fourcc} |
| 5 | service available reply {5, fourcc, flags: bit0 available, 2 crc, 4/8 bulk options} |

The host can't send anything before it got the VC's type 0 (its tx credits start at 0).

## DISPMANX on top (vcdispman.dll / vcdispmanvchi.dll)

Two VCHI clients: requests and the update handler (`DISP`/`UPDH`). Request = u32 command (bit 31 = wants a 4-byte reply) + payload. The ids follow Broadcom's older DISPMANX_COMMAND_T:

| id | command | payload |
|---|---|---|
| 0x05 | ResourceDelete | handle |
| 0x0a | DisplayOpenOffscreen | resource, orientation -> display |
| 0x0c | DisplaySetDestination | display, resource |
| 0x0d | DisplaySetBackground | update, display, r, g, b |
| 0x0f | DisplayClose | display |
| 0x10 | UpdateStart | priority -> update |
| 0x11 | UpdateSubmit | update, callback flag |
| 0x12 | UpdateSubmitSync | update |
| 0x13 | ElementAdd | 64 bytes: update, display, layer, dest rect, src resource, src rect, protection, alpha, clamp, transform |
| 0x15 | ElementRemove | update, element |
| 0x17 | ElementChangeAttributes | update, element, flags, ... |

## DSPC (dispcommanddrv.dll): panel config, power, DCS commands

Service fourcc `DSPC`. Request header (FUN_8034173c / FUN_80340b7c): `u16 display | u16 interface id | u32 cmd | 0x100000 | u32 token`, then `u32 arg` for the simple commands, or a payload for config/batch.

| cmd | what | sent by |
|---|---|---|
| 2 | power, arg 0x81 on / 0x80 off / 0x40,0x41 (modes 8/9) | chipset iface +0x1c (FUN_8034d944) |
| 3 | panel config, display = index 0 | FUN_80340d50, sent once after every VC boot before anything else |
| 4 | rotation | |
| 5 | ping | |
| 7 | matrix (bulk) | |
| 8 | selftest | |
| 0x40 | DCS batch, display 0xffff | FUN_80341244 |

Batch payload: `u32 count`, then per DCS command `u32 1, u32 0, u32 dcs, u32 padded len, u32 len, data` (data bytes LE-packed, padded to 4). Reads use type 0 records with a callback token instead.

Panel side (display chipset interface table built at 0x8034e5xx): `+0x2c` begin, `+0x30` DCS command (FUN_8034cfb0), `+0x34` data byte (FUN_8034ce90, width from config +0x2e = 8), `+0x38` read, `+0x40` flush.

Interface ids from the chipset table 0x8034eaa4: 4 and 6. Panel database FUN_80349e58: the E6 640x480 panels (cases 0x78, 0x79, 0x85) build on case 0x72: porches 10/20/2 and 3/3/3, format 2, 20 MHz pixel clock, colmod sent nibble-swapped (RGB interface) -> id 4, the 32-byte "video" config form.

Panel DCS used by the Symbian driver: 0x11/0x10 sleep, 0x29 display on, 0x3a colmod (0x70), 0x36 madctl, 0x2a/0x2b window, 0x2c write, 0x53 bctrl (0x24 / 0x2c), 0x51 brightness, 0x55 cabc, 0xda/0xdb/0xdc id read.

## The real Symbian sequence (emulated, 14.10)

`tools/vcemu/run_panel.py` runs the Symbian display stack straight from kern.bin (panel driver 0x8034ed18.., display chipset, dispcommanddrv, vcdispman) with the kernel stubbed: module .data loaded from the ROM image headers, static ctors run, RArray code executed natively, DFCs/timers scheduled, VC replies faked ({token, 0}; DCS reads answer the E6 panel 01 8b 79). Captured messages: `symbian-display-sequence-warm.txt`.

Install (FUN_8034f2d0 + FUN_8034f1c8, display 0 = panel id 0x018b79 from c80124ec[0]): panel config (identical to vcprobe's), DSPC ping (cmd 5).

Op 1 (init), on E6 (bus type 0xc, interface id 4):
- reads 0a 0b 0c 0d 0e, then dc db da (panel id)
- panel config again, power mode 0x11 -> cmd 2 arg 0x81
- 0x53 3f, 0x51 3f, 0x55 00
- 0x26 01, 0x36 00, 0x3a 70
- reads 0a..0e
- op 8: 0x29, 0x26 01, 0x36 00, 0x3a 70, 0x38, 0x13, 0x53 3f, 0x51 3f, 0x55 00 No 0x11 (sleep out) and no DISP traffic during init.

Op 3 (refresh):
- reads dc db da
- cmd 7 colour matrix, display 0 param 0, 64 zero bytes (no matrix)
- batch 0x3a 70, 0x35 00, 0x34
- compose (FUN_8034d0f4): DISP 08(0) -> display, 03(0x2b, 640, 480) -> resource, BUFM write image into the resource, 10(1) -> update, 13 {update, display, -400, 0 0 640 480, res, 0 0 640<<16 480<<16, 1, 0xff, 0, 0}, 0d {update, display, 0, 0, 0}, 12(update)

## BUFM (vc_bufmanx, mod 0x8033afd8), write image (FUN_8033b29c)

The service fourcc on the wire is `BFMX` (template at 0x8033df0c, opened with flags 0x1c); `BUFM` below is the old working name. request (0x3c bytes on service BUFM): {6, resource, image type, ?, width, height, pitch, src rect x y w h (16.16), dst rect x y w h}. Symbian's 640x480 host surface: type 0x20008, pitch 0xa00 (32 bpp). reply (0x3c): {status, nchunks, rows per chunk, chunk bytes, ?, last chunk bytes} then one vchi bulk transmit per chunk (data + pitch * rows * i), then a final reply {status}.

## VCHI bulk transmit (FUN_803375f0 -> DFC 0x803331a8)
- small (< VC max msg, service +0x6c set): one BULX message, 0x1c-byte header {service fourcc, size, chunk max, crc, rx size, u16 head offset, u8 has rx, u8 0, u16 head bytes, u16 tail bytes} + inline data
- big: needs a VC bulk-rx descriptor first (queue +0x3320); BULX header carries the unaligned head/tail inline, the aligned middle goes out on vcdriver tx channel 1 (FUN_8032b240(drv, 1, ...))