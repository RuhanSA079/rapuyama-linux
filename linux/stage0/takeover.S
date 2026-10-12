@ one-way takeover: kill the mmu/symbian and drive the display
@
@ entered from an IDENTITY-mapped executable page with the mmu on. masks
@ interrupts, cleans caches, turns the MMU OFF (so symbian is basically dead
@ and we hit phys ram directly), then LOOPS FOREVER filling the framebuffer
@ candidates with a cycling colour. the display controller (already set up by
@ symbian and running on its own) keeps scanning its framebuffer out of phys
@ ram, so whichever candidate is the live fb shows our colour. never returns
@ (battery pull to recover)
@
@ no stack, registers only. adr (pc-relative) gives the real phys address of
@ our data tables with the mmu off, ldr = (constants) reads our own literal pool

        .syntax unified
        .arm
        .section .text
        .global takeover
        .type takeover, %function
takeover:
        cpsid   if @ no interrupts from here on
        mov     r2, #0
        mcr     p15, 0, r2, c7, c14, 0 @ clean+invalidate D cache
        mcr     p15, 0, r2, c7, c5, 0 @ invalidate I cache
        mcr     p15, 0, r2, c7, c10, 4 @ DSB
        mcr     p15, 0, r2, c7, c5, 4 @ ISB
        mrc     p15, 0, r3, c1, c0, 0
        bic     r3, r3, #0x5 @ clear M (mmu) + C (D cache)
        mcr     p15, 0, r3, c1, c0, 0 @ MMU OFF, symbian is gone
        mcr     p15, 0, r3, c7, c5, 4 @ ISB

        mov     r4, #0 @ frame counter
frame:
        and     r6, r4, #3
        adr     r7, colors
        ldr     r5, [r7, r6, lsl #2] @ r5 = colors[frame & 3]
        adr     r7, bases
        mov     r9, #0
baseloop:
        ldr     r0, [r7, r9, lsl #2] @ r0 = bases[r9] (phys fb candidate)
        ldr     r2, =0x00080000 @ 512k words = 2 mb
fill:
        str     r5, [r0], #4
        subs    r2, r2, #1
        bne     fill
        add     r9, r9, #1
        cmp     r9, #3
        blt     baseloop

        ldr     r8, =0x00800000 @ delay between colour changes
dly:
        subs    r8, r8, #1
        bne     dly

        add     r4, r4, #1
        b       frame

        .ltorg
colors: .word 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF @ R G B W
bases:  .word 0x10480000, 0x1f8b0000, 0x20500000 @ fb candidates
        .size takeover, .-takeover