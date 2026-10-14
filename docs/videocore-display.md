# Nokia E6 BCM2727B1 VideoCore III display: current findings

## Status

The command-mode panel refresh path is not implemented in Linux. The kernel now has an experimental fbdev staging driver for reserved framebuffer memory, but it only maps that memory and does not submit updates to VideoCore. The normal E6 device tree does not enable it - a separate diagnostic DTB does.

Most findings below come from checked-in reverse-engineering notes and
locally extracted ROM/Ghidra output. A read-only USB-shell inspection confirmed that Linux exposes no `/dev/fb*` device (only the virtual `fbcon` class entry) and that the running kernel's reserved image range covers
the known framebuffer address. No framebuffer writes or display-register accesses were attempted.

## Current best-fit architecture

```text
ARM vcdriver.dll
  |_ VideoCore boot-ROM exchange over MeSSI; transfer the onboard VC image
       |_ VCHI message/bulk transport over MeSSI and CCP2
            |_ vcdispmanvchi.dll / vcdispman.dll
                 |_ dispcommanddrv.dll / display_chipset_drv_ive_bc.dll
                      |_ BCM2727B1 VideoCore III and command-mode panel
```

The names and overall layering are supported by the ROM module inventory and the existing decompilation. The exact message schemas and service command IDs are not yet reconstructed.

## Firmware image package

The ROM listing contains `Sys\\Bin\\unsigned_2727b1.dll` (about 1 MiB). Its strings include `bcm2727b1`, `dispcontrol.vll`, `vchi_dispcontrol_server_init`, and the build path `/Z:/vlls/bcm2727b1/`. This confirms the firmware bundle contains the VideoCore display-control service, but does not establish that the E32 container can be loaded directly by Linux or that those service modules are individually extractable from it. The Symbian-side `DVCDriver::Boot()` remains the only recovered loader path; its staged transfer descriptors still need decoding.
The ROM can be re-extracted with [`tools/rom_walk.py`](../tools/rom_walk.py).

## More detail from the transport setup

The existing `vcdriver.dll` decompilation exposes concrete setup steps beyond the high-level module names:

1. `FUN_8032a3c8` configures channel-related state through a helper for IDs `0x7e`, `0x7d`, `0x7c`, and `0x6a..0x79`, then installs callbacks and creates a semaphore. The helper's exact semantics are not recovered.
2. It initializes the block at physical `0x20025800` through `FUN_8032a368`, then initializes the block at `0x20024800` through `FUN_8032a154`. The latter binds interrupt line `0x92` and programs multiple registers, including a slot populated with a linear-to-physical address. The raw values in these setup functions are not sufficient to identify the full register semantics or safe reset state.
3. The interrupt callback `FUN_8032a020` reads and acknowledges status at `0x20024818`; it handles a FIFO-related status bit and invokes `FUN_80329ec8` to process queued work. Other status bits are logged asgraphics FIFO underflow and sync loss.
4. The boot routine `FUN_8032cd50` sends a message, polls for a response, and parses response type/status fields. On the success path it retrieves image metadata, sends data using `FUN_8032cb40`, polls/parses again, and handles another transfer using a descriptor at image offset `0x1c`. It only marks the final stage successful after a further response. This establishes a multi-exchange image boot path, but not the exact on-wire message formats, meanings of status values, retry timing, or whether this is the only boot path used by the shipped device.
5. The same module configures CCP2 receive registers around `0x20034000..0x20034240` and uses the clock/pin-related registers noted in [driver-re.md](./driver-re.md). The current static analysis does not yet prove which CCP2 channel carries each VCHI traffic class or establish a complete, safe initialization order across power, clocks, pins, MeSSI, and CCP2.

The separate `dispcommanddrv.dll` creates synchronization objects named for matrix, power, ping, self-test, and callbacks. That is evidence of display control operations, but not the actual DISPMANX service protocol. The minimum command sequence to configure the panel or submit an update remains unknown.

The host-side `vcdispman.dll` does expose a VCHI request wrapper. Its helpers submit request IDs `0x04`, `0x05`, `0x0a`, `0x0c`, `0x0d`, `0x0f`, `0x10`, `0x11`, `0x12`, `0x13`, `0x15`, `0x17`, `0x18`, and `0x19` with payloads between 4 and 64 bytes. The wrapper marks some IDs as reply-bearing, serializes each call with a host mutex, and waits for a four-byte reply for synchronous requests. The service registration itself goes through VCHI with a 36-byte descriptor. This recovers part of the host RPC envelope, not the VideoCore service's semantics: the registration descriptor values and command payload schemas are still unresolved, so the IDs cannot safely be called from Linux yet.

The VCHI service identifier initialized by `vcdispmanvchi.dll` is the eight-byte value `DISPUPDH` (two little-endian four-character words, `DISP` + `UPDH`). This identifies the host-side display-update-handler service, but does not imply that the raw chipset `0x40` batch is the payload of that service; the code shows separate host wrappers and transport layers.

The extracted `display_chipset_drv_ive_bc.dll` provides a little more detail about the framebuffer-facing API. Its allocation-size helper rounds the width up to a multiple of 16, then computes row bytes as follows:

| API format value | Row-byte calculation |
|---|---|
| `2` or `4` | `ALIGN(width, 16) * 4` |
| `0x10` | `ALIGN(width, 16) * 3` |
| `0x20` | `ALIGN(width, 16) * 2` |
| `0x2000` | zero (special/non-pixel allocation) |

The code multiplies this row size by the height when allocating physical RAM.
This is useful evidence for the allocation ABI, but does not identify channel order, alpha semantics, or a panel update format. Its update path gathers a batch of pending operations and submits them through the display-command transport with command `0x40`. The ARM-side batching helper treats each operation as a 24-byte record: it emits the first five 32-bit fields in the order at offsets `0x00`, `0x0c`, `0x08`, `0x04`, and `0x10`; if the record's word at `0x14` is nonzero, it also emits a variable-length data segment whose length is the word at `0x04`. The batch starts with a four-byte operation-count segment. These are host-side scatter/gather details only; the meanings of the fields, the variable data, and the VC-side meaning of command `0x40` are still unresolved. Therefore this information is not yet enough to safely construct a Linux update request.

## Evidence

| Finding | Confidence | Evidence and limits |
|---|---|---|
| The E6 display uses a Broadcom BCM2727B1 / VideoCore III, and the VideoCore image package is included in Nokia firmware. | High | ROM strings include `Video/Core III H/W`, `bcm2727b1`, `VideoCore image ID`, and `unsigned_2727b1.dll`. |
| The ARM-side `vcdriver.dll` has a multi-exchange VideoCore boot path that parses image metadata, transfers image data, and polls for responses. | High for code structure; medium for protocol interpretation | Ghidra decompilation of `FUN_8032cd50` at `0x8032cd50` and helper `FUN_8032cb40` at `0x8032cb40` in the extracted kernel ROM. The exact wire constants, payload meanings, and handshake timing are not fully recovered. |
| Two MeSSI blocks participate in the VideoCore link, with block `0x20025800` initialized before `0x20024800`; interrupt line `0x92` is bound by the latter's setup. | High for static code references; not live-tested here | Ghidra decompilation of `FUN_8032a3c8`, `FUN_8032a368`, and `FUN_8032a154`. This does not establish reset-state requirements, complete register semantics, or a safe Linux initialization sequence. |
| The firmware also configures clocks, GPIO/mux, and CCP2 receive support around the VideoCore link. | Medium | References in `docs/driver-re.md` include `0x200318e0`, `0x200319e4`, `0x20033008`, `0x20033038`, `0x20033178`, and `0x20034000..0x20034240`. Exact sequencing and safe Linux ownership remain to be established. |
| VCHI handles both MeSSI and CCP2 data paths and has transport error handling. | Medium | `vchi.dll` decompilation contains MeSSI/CCP2 CRC-failure paths, VCHI receive/bulk handling, and a VideoCore-enable failure path. The complete channel negotiation and slot layout have not been translated into a Linux implementation. |
| Display-management modules sit above VCHI. | Medium | ROM module inventory includes `vcdispmanvchi.dll`, `vcdispman.dll`, `dispcommanddrv.dll`, and `display_chipset_drv_ive_bc.dll`. This confirms the component chain, not the complete commands or ABI. |
| The ARM `vcdispman.dll` has a VCHI RPC wrapper with a set of numeric request IDs and optional four-byte replies. | High for wrapper structure; low for command meanings | `FUN_80344fcc`, `FUN_803450d8`, `FUN_80345168`, and the wrappers in `vcdispman.dll`; request payload schemas and registration descriptor are not decoded. |
| The display-update-handler VCHI service uses the eight-byte identifier `DISPUPDH`. | High for the host-side identifier | `vcdispmanvchi.dll`, `FUN_80345384` initializes the two little-endian words `0x50534944` and `0x48445055`; this does not identify its message schema. |
| The display-chipset update path builds scatter/gather batches from 24-byte host operation records and sends them under command `0x40`. | High for host record traversal/order; low for field semantics | `FUN_80341244` constructs segments; `FUN_80340b7c` queues the command path. The VC-side protocol and actual operation meanings remain unknown. |
| `0x10480000` is a 4 MiB framebuffer allocation, but writing it alone does not refresh the panel after Symbian stops. | High in the existing hardware notes | `docs/hw-memory-map.md` records a fill test while Symbian was alive and explains that the panel is command-mode. A Linux display path must trigger updates through VideoCore; direct scanout is not established. |
| The current kernel image overwrites the entire framebuffer allocation. | High for the current build | `Image` loads at `0x10008000`, has size 19,643,844 bytes, and ends at `0x112c3dc4`; framebuffer is `0x10480000..0x10880000`. Resolve memory ownership before any simplefb/preserved-framebuffer experiment. |
| The running Linux USB-shell image does not expose a framebuffer device. | High for the inspected boot | Read-only shell query found no `/dev/fb*`; `/sys/class/graphics` contains only virtual `fbcon`. `/proc/iomem` lists kernel code at `0x10008000..0x10efffff`, intersecting the framebuffer range. |
| The display chipset's buffer allocator uses 16-pixel row alignment and supports API format values `2`, `4`, `0x10`, and `0x20` with 4/3/2 bytes per aligned pixel respectively. | High for allocation arithmetic; low for pixel semantics | `display_chipset_drv_ive_bc.dll`, helper `FUN_8034c808` and caller `FUN_8034c86c`. The implementation does not establish color channel order or the panel's accepted format. |

## Linux starting point

The kernel tree already contains the generic Broadcom VCHIQ service stack under `drivers/staging/vc04_services`, and `linux/kernel/e6.config` sets `CONFIG_BCM2835_VCHIQ=m`. The current BCM2835 VCHIQ device binding, however, describes a doorbell register pair (`brcm,bcm2835-vchiq`). The E6 firmware references MeSSI and CCP2 instead. The existing Broadcom platform transport must therefore not be assumed to work on RapuYama merely because both sides use VideoCore/VCHI terminology; reuse would require proving compatibility or adding the correct transport backend.

The new `SOC_RAPUYAMA_VIDEOCORE` option is off by default. Its platform driver matches `nokia,rapuyama-videocore`, which is intentionally absent from the current DTS. If explicitly enabled and bound to a node, probe reports `-EOPNOTSUPP`; it performs no register reads/writes and does not claim that the display works.

An experimental kernel build with `PHYS_OFFSET=0x10880000` is available through [`linux/kernel/build-display-test.sh`](../linux/kernel/build-display-test.sh). Its decompressed image starts at `0x10888000`, above the framebuffer, and the paired USB-shell DTB is copied to `linux/kernel/build-display/e6-usb.dtb`. The additional `e6-display-test.dtb` reserves `0x10480000..0x10880000` and enables `FB_RAPUYAMA`, exposing the mapped staging memory as `/dev/fb0`. The diagnostic initramfs writes color bars to the buffer after the USB shell is configured and logs whether that write succeeds. This provides a concrete framebuffer test without changing the normal kernel configuration. It does not load or update the VideoCore firmware, send display commands, or guarantee a refresh of the command-mode panel.

## Next reverse-engineering steps

1. Recover the VideoCore boot message layouts and exact stage transitions from `vcdriver.dll`, including the image header and transfer chunking.
2. Decode MeSSI initialization, interrupt/handshake behavior, and ownership requirements for clocks, pins, and regulators before any Linux MMIO access.
3. Reconstruct VCHI negotiation, control-message and bulk-transfer framing, determine whether the Linux VCHIQ core can be adapted or only reused at a higher service layer.
4. Trace the display-management client far enough to identify the minimum service sequence for a framebuffer update and the required framebuffer format/stride.
5. Only after those steps, design the real transport and display interfaces, add a DTS binding, and plan separately supervised hardware validation.
