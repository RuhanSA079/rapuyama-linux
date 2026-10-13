# Symbian -> Linux trampoline (design)

How to hand control from a patched Symbian to the Linux kernel on the ARM1176 (ARMv6K).
This is the original design. It's implemented now: `tramp_boot.S` + the booter in
`../stage0/symbian-loader/runblob_ldd.cpp`, status in `README.md`.

## Launch context
Our code starts as a **kernel-side LDD** (like ptdump): kernel privileges, MMU ON,
we're in Symbian's virtual address space (TTBR1 global, peripherals at
`0xc6000000`, RAM phys at `0x10000000`). Linux has to start with the **MMU off**,
in physical addresses, following the ARM boot protocol.

## Linux kernel entry protocol (ARM, non-DT and DT)
On entry to `zImage`/the kernel:
- MMU **off**, D-cache **off**, I-cache may be on.
- CPU in **SVC**, IRQ/FIQ **masked** (CPSR.I=1,F=1).
- `r0 = 0`
- `r1 = machine type` (ATAG path) **or** `~0` / don't care with DT
- `r2 = phys address` of the DTB (device tree) **or** the ATAG list
- The kernel relocates itself; `zImage` is self-decompressing.

## Trampoline steps (kernel side)
1. **Reserve phys RAM.** Need a contiguous phys region for zImage+DTB, outside
   whatever the running Symbian uses. Options:
   - allocate a big physically contiguous block through the kernel API
     (`Epoc::AllocPhysicalRam` / `TRamDefragRequest`), remember the phys base;
   - or a "hard" top chunk of the 256 MB if Symbian doesn't touch it (risky).
   The current loader puts zImage at chunk phys `+ TEXT_OFFSET` (`0x8000`) and
   the DTB at chunk `+0x02000000`. The E6 kernel uses fixed
   `PHYS_OFFSET=0x10000000` (and `ARM_PATCH_PHYS_VIRT`), so the decompressor
   puts the kernel at `0x10008000` regardless of the allocated chunk address.
   Do not enable `AUTO_ZRELADDR`: with that heuristic, a chunk at
   `0x18000000` makes Linux infer `PHYS_OFFSET=0x18000000` and omit the lower
   128 MiB from its memory map.
2. **Copy** zImage and DTB into the reserved phys pages (through temporary
   virt mappings), sync the cache (`Cache::IMB_Range` / clean+invalidate).
3. **Point of no return (MMU off).** Can't run C with the MMU off in virt space,
   need a small **PIC stub in phys memory** that gets control at its PHYS address,
   either with an identity chunk prepared beforehand or by jumping through a
   register to a phys address. The stub:
   - masks IRQ/FIQ (`cpsid if`), switches to SVC;
   - cleans and disables caches (clean+invalidate D, inval I, DSB/ISB);
   - turns off the MMU (SCTLR.M=0), barriers, TLB invalidate;
   - sets `r0=0, r1=machid/~0, r2=dtb_phys`;
   - `bx` to the zImage phys address.
   The stub must already be running at phys addresses -> either create an
   identity mapping (virt==phys) for the stub page and zImage in Symbian's tables
   beforehand, jump there and only then kill the MMU; or do the whole jump to phys
   through a short identity page. This is the trickiest part, work it out on the
   bench step by step (first a stub that just blinks the LED and hangs, no Linux).
4. Linux starts, decompresses, early code brings up its UART/timer from
   our DT.

## DTB (device tree), minimal
```
/ { compatible = "nokia,e6", "ti,rapuyama";
    #address-cells=<1>; #size-cells=<1>;
    memory@10000000 { device_type="memory"; reg=<0x10000000 0x10000000>; };  // 256MB
    chosen { bootargs = "earlycon console=... "; };
    cpus { cpu@0 { compatible="arm,arm1176jzf-s"; }; };
    // intc@? timer@? uart@?  -- phys bases come from the phone (ptdump)
};
```
Interrupt controller/timer/UART get filled in once the live search gives phys bases
and semantics (INTC type, timer frequency, UART divider).

## Why probably NOT a one-way "kexec" right away
Without an observable signal (no adapter) it's safer to have a stub that DOESN'T go
into Linux, but does something visible (blink) and lets us return/reboot. That's why
Milestone 1 (baremetal blink) comes before Milestone 2 (the real jump).

## Risks
- XN (execute-never): kernel heap IS non-executable (first M0 try crashed).
  Solved: `Epoc::AllocPhysicalRam` + `DPlatChunkHw` mapping with exec rights.
- Physical contiguity of RAM for the zImage: solved the same way (48 MB chunk).
- Identity mapping to kill the MMU without crashing: solved, tested with
  `IdentityTest` / `TrampTest` / `FullTramp`.
- Watchdog: still open. With symbian dead nobody kicks it and the phone resets in
  seconds. Not a brick, but the kernel has to deal with it very early.