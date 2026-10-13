@ one-way watchdog test: kill the mmu/symbian like takeover, then loop FOREVER
@ kicking both watchdogs the way power.dll does (docs/driver-re.md), every few
@ ms. the first linux boot powered the phone off ~2 s after the jump, and so
@ did the old takeover test. if the phone stays alive (frozen) with this, our
@ kicks are right and enough -> linux just dies before its watchdog driver.
@ if it still dies in ~2 s there is another watchdog we don't know yet
@
@ no stack, registers only, phys addresses (mmu off)

        .syntax unified
        .arm
        .section .text
        .global kickloop
        .type kickloop, %function
kickloop:
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

        ldr     r4, =0x200336c4 @ soc watchdog counter
        ldr     r5, =0x3fffffff
        ldr     r6, =0x20030700 @ cbus master
kick:
        str     r5, [r4] @ soc counter reload, like power.dll

        @ pmic dev 1 reg 0x17 = 0x30 (48 s), karap 0x800b8494 order
        mov     r0, #1
        str     r0, [r6, #0x04] @ enable
        str     r0, [r6, #0x38] @ write mode
        mov     r0, #15
        str     r0, [r6, #0x28] @ 16 bits
        mov     r0, #0x30
        str     r0, [r6, #0x1c] @ data
        mov     r0, #0x57 @ (1 << 6) | 0x17
        str     r0, [r6, #0x18] @ go
        ldr     r1, =0x10000 @ bounded busy wait
busy:
        ldr     r0, [r6, #0x08]
        cmp     r0, #0
        beq     done
        subs    r1, r1, #1
        bne     busy
done:
        mov     r0, #0
        str     r0, [r6, #0x04]

        ldr     r1, =0x00100000 @ a few ms
dly:
        subs    r1, r1, #1
        bne     dly
        b       kick

        .ltorg
        .size kickloop, .-kickloop