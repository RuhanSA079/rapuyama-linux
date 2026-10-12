@ the full symbian->kernel handoff trampoline (reversible test version)
@
@ runs from an IDENTITY-mapped executable page. turns off irq/mmu/caches, sets
@ the arm linux boot regs (r0=0, r1=machtype, r2=dtb phys) and jumps to the
@ loaded payload entry (the "kernel", here just a checker). for the REAL boot
@ this becomes one-way: `blx` -> `bx` and drop the mmu restore tail
@
@ caller abi: r0=payload entry phys, r1=marker phys, r2=dtb phys, r3=machtype.
@ stack is used ONLY while the mmu is on (entry/exit), never between off and on

        .syntax unified
        .arm
        .section .text
        .global tramp_entry
        .type tramp_entry, %function
tramp_entry:
        stmfd   sp!, {r4-r11, lr} @ mmu on: stack ok. keep aapcs regs
        mov     r8, r0 @ payload entry (phys)
        mov     r9, r1 @ marker (phys)
        mov     r11, r2 @ dtb (phys)
        mov     r6, r3 @ machtype
        mrs     r7, cpsr @ save irq/fiq state
        cpsid   if

        mov     r4, #0
        mcr     p15, 0, r4, c7, c14, 0 @ clean+invalidate D cache
        mcr     p15, 0, r4, c7, c5, 0 @ invalidate I cache
        mcr     p15, 0, r4, c7, c10, 4 @ DSB
        mcr     p15, 0, r4, c7, c5, 4 @ ISB
        mrc     p15, 0, r10, c1, c0, 0 @ save SCTLR
        bic     r4, r10, #0x5 @ M + C off
        mcr     p15, 0, r4, c1, c0, 0 @ MMU OFF
        mcr     p15, 0, r4, c7, c5, 4 @ ISB

        @ arm linux boot protocol regs
        mov     r0, #0
        mov     r1, r6 @ machine type (~0 / id)
        mov     r2, r11 @ dtb / atags phys pointer
        mov     r3, r9 @ (test only) marker phys for the checker
        blx     r8 @ jump into the payload ("kernel")

        @ reversible test: payload returned, bring the mmu back and go home
        mcr     p15, 0, r10, c1, c0, 0 @ MMU ON
        mcr     p15, 0, r10, c7, c5, 4 @ ISB
        mov     r4, #0
        mcr     p15, 0, r4, c8, c7, 0 @ invalidate TLB
        mcr     p15, 0, r4, c7, c5, 4 @ ISB
        msr     cpsr_c, r7 @ restore irq/fiq
        mov     r0, #0
        ldmfd   sp!, {r4-r11, pc} @ mmu on: stack ok, back to symbian
        .size tramp_entry, .-tramp_entry