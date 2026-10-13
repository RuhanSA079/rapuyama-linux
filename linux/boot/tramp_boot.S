@ ONE-WAY handoff into a real linux zImage on the e6
@
@ runs from an IDENTITY-mapped executable page (virt==phys) so pc stays valid
@ once the mmu is off. masks interrupts, cleans+invalidates caches, (maybe
@ kicks/disables the watchdog), turns the MMU OFF, sets the arm linux boot
@ regs and jumps into the kernel. never returns, symbian is gone
@
@ caller abi (from the booter driver, mmu still on, in svc):
@ r0 = zImage entry phys address
@ r1 = machine type (a dt kernel ignores r1, pass ~0 or an id)
@ r2 = dtb phys address
@ r3 = watchdog reg address (0 = none), goes to the kick below
@
@ registers only after the mmu goes off, no stack, no literal pool

        .syntax unified
        .arm
        .section .text
        .global tramp_boot
        .type tramp_boot, %function
tramp_boot:
        mov     r4, r0 @ zImage entry (phys)
        mov     r5, r1 @ machine type
        mov     r6, r2 @ dtb phys
        mov     r7, r3 @ watchdog reg (0 = skip)

        cpsid   if @ mask irq + fiq

        @ optional watchdog kick/disable (fill in the real sequence once its known)
        @ cmp     r7, #0
        @ beq     1f
        @ ldr     r8, =0x........ @ magic / disable value
        @ str     r8, [r7]
1:
        mov     r2, #0
        mcr     p15, 0, r2, c7, c14, 0 @ clean+invalidate D cache
        mcr     p15, 0, r2, c7, c5, 0 @ invalidate I cache
        mcr     p15, 0, r2, c7, c10, 4 @ DSB
        mcr     p15, 0, r2, c7, c5, 4 @ ISB
        mrc     p15, 0, r3, c1, c0, 0
        bic     r3, r3, #0x5 @ clear M (mmu) + C (D cache)
        @ symbian leaves SCTLR.FA (bit 29, AP[0] = access flag) set. linux v6
        @ doesn't clear it, so its user-RO ptes (AP0=0, incl. the 0xffff0000
        @ vectors page) give access-flag faults -> the first irq recurses in
        @ the abort vector forever. also clear TR (bit 28, tex remap)
        bic     r3, r3, #0x30000000
        mcr     p15, 0, r3, c1, c0, 0 @ MMU OFF
        mcr     p15, 0, r3, c7, c5, 4 @ ISB

        @ arm linux boot protocol entry state
        mov     r0, #0
        mov     r1, r5 @ machine type
        mov     r2, r6 @ dtb / atags phys
        bx      r4 @ jump into the kernel, no way back
        .size tramp_boot, .-tramp_boot