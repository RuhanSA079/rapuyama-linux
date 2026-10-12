@ reversible MMU-OFF trampoline stub (trampoline stage 2)
@
@ MUST run from an IDENTITY-mapped page (virt==phys) so pc stays valid when the
@ mmu goes off. uses ONLY registers between mmu off and mmu on (the stack is at
@ a non-identity kernel va and would be garbage). no literal pool (all constants
@ are inline immediates) so no pc-relative data loads needed
@
@ aapcs: r0 = marker PHYS address, r1 = marker value. returns r0 = 0
@ with mmu+caches off and irq/fiq masked it writes r1 to phys [r0], then
@ brings the mmu back and returns. proves we can drop and restore the mmu in a
@ live kernel, the last primitive before the one-way jump into linux

        .syntax unified
        .arm
        .section .text
        .global stub2
        .type stub2, %function
stub2:
        mrs     r12, cpsr @ save caller CPSR (mode + I/F)
        cpsid   if @ mask irq + fiq for the mmu-off window

        mov     r2, #0
        mcr     p15, 0, r2, c7, c14, 0 @ clean+invalidate whole D cache
        mcr     p15, 0, r2, c7, c5, 0 @ invalidate whole I cache
        mcr     p15, 0, r2, c7, c10, 4 @ DSB
        mcr     p15, 0, r2, c7, c5, 4 @ ISB (prefetch flush)

        mrc     p15, 0, r3, c1, c0, 0 @ r3 = SCTLR (saved)
        bic     r2, r3, #0x5 @ clear M (bit0, mmu) and C (bit2, D cache)
        mcr     p15, 0, r2, c1, c0, 0 @ MMU OFF
        mcr     p15, 0, r2, c7, c5, 4 @ ISB

        str     r1, [r0] @ phys write of the marker (mmu off)

        mcr     p15, 0, r3, c1, c0, 0 @ restore SCTLR, MMU ON
        mcr     p15, 0, r3, c7, c5, 4 @ ISB
        mov     r2, #0
        mcr     p15, 0, r2, c8, c7, 0 @ invalidate whole TLB
        mcr     p15, 0, r2, c7, c5, 4 @ ISB

        msr     cpsr_c, r12 @ restore irq/fiq state
        mov     r0, #0
        bx      lr
        .size stub2, .-stub2