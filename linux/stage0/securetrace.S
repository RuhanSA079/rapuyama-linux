@ One-way TrustZone-adjacent diagnostic:
@ kill Symbian, feed the known SoC + PMIC watchdogs, and log fast-counter
@ heartbeat intervals to reserved RAM. A long interval or abrupt end proves
@ the non-secure loop stopped running; it does not by itself identify TrustZone.
@
@ Log: phys 0x1f080000, magic E6TZ, 8192 records of {fast_counter, delta}.
@ The whole log lives inside the already-reserved 0x1f000000 RAM console area.
@ No stack; registers only.

        .syntax unified
        .arm
        .section .text
        .global securetrace
        .type securetrace, %function
securetrace:
        cpsid   if
        mrs     r12, cpsr
        mrc     p15, 0, r9, c0, c1, 1 @ ID_PFR1, security extension field
        mrc     p15, 0, r8, c1, c0, 0 @ entry SCTLR
        mov     r2, #0
        mcr     p15, 0, r2, c7, c14, 0
        mcr     p15, 0, r2, c7, c5, 0
        mcr     p15, 0, r2, c7, c10, 4
        mcr     p15, 0, r2, c7, c5, 4
        mov     r3, r8
        bic     r3, r3, #0x5
        mcr     p15, 0, r3, c1, c0, 0
        mcr     p15, 0, r3, c7, c5, 4

        ldr     r10, =0x1f080000
        ldr     r11, =0x20033350
        mov     r0, #0
        str     r0, [r10, #0]
        mov     r1, #1
        str     r1, [r10, #4]
        ldr     r0, =8192
        str     r0, [r10, #8]
        mov     r0, #0
        str     r0, [r10, #12]
        str     r0, [r10, #16]
        ldr     r0, =1024
        str     r0, [r10, #20]
        mov     r0, #0
        str     r0, [r10, #24]
        str     r0, [r10, #28]
        str     r0, [r10, #32]
        str     r0, [r10, #36]
        str     r0, [r10, #40]
        str     r0, [r10, #44]
        str     r0, [r10, #48]
        ldr     r0, =0x5a543645 @ "E6TZ"
        str     r0, [r10, #0]
        str     r9, [r10, #36]
        str     r12, [r10, #40]
        str     r8, [r10, #44]

        mov     r0, #0
        str     r0, [r11, #4] @ latch fast counter
        ldr     r5, [r11]
        str     r5, [r10, #48]
        mov     r8, #0 @ records written

loop:
        ldr     r4, =0x200336c4
        ldr     r6, =0x3fffffff
        str     r6, [r4] @ SoC watchdog reload

        @ PMIC watchdog: device 1, register 0x17, value 0x30 (48 seconds).
        ldr     r6, =0x20030700
        mov     r0, #1
        str     r0, [r6, #0x04]
        str     r0, [r6, #0x3c]
        mov     r0, #15
        str     r0, [r6, #0x2c]
        mov     r0, #0x30
        str     r0, [r6, #0x20]
        mov     r0, #0x57
        str     r0, [r6, #0x1c]
        ldr     r1, =0x10000
busy:
        ldr     r0, [r6, #0x08]
        cmp     r0, #0
        beq     sample
        subs    r1, r1, #1
        bne     busy
sample:
        mov     r0, #0
        str     r0, [r11, #4]
        ldr     r6, [r11]
        subs    r7, r6, r5
        cmp     r7, #1024
        blo     loop
        mov     r5, r6
        str     r6, [r10, #48] @ latest counter
        cmp     r8, #8192
        bhs     full

        add     r0, r10, #64
        add     r0, r0, r8, lsl #3
        str     r6, [r0]
        str     r7, [r0, #4]
        ldr     r1, [r10, #24]
        cmp     r7, r1
        strhi   r7, [r10, #24]
        strhi   r8, [r10, #28]
        add     r8, r8, #1
        str     r8, [r10, #12] @ publish only after the record is complete
        b       loop

full:
        mov     r0, #1
        str     r0, [r10, #32]
        b       loop

        .ltorg
        .size securetrace, .-securetrace
