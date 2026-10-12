@ position-independent native blob for the e6 (arm1176)
@
@ M0 proved we can load + run raw arm code. this version also makes that code
@ ASK THE CPU via cp15 so we know the exact core/cache/features to build the
@ linux kernel for (and to get the trampoline handoff right).
@ runs in supervisor mode (driver Request context) so MRC p15 is allowed
@
@ aapcs: r0=TUint32* out, r1=capacity (words), r2=cookie. returns words written
@ layout:
@ [0]=0xCAFEBABE sig [1]=cookie echo [2]=sum(1..256)=0x8080 (ran a loop)
@ [3]=CPSR at entry
@ [4]=MIDR (c0,c0,0 main id) [5]=CTR (c0,c0,1 cache type)
@ [6]=TLBTR (c0,c0,3) [7]=ID_PFR0 (c0,c1,0)
@ [8]=ID_PFR1 (c0,c1,1) [9]=ID_DFR0 (c0,c1,2)
@ [10]=ID_MMFR0 (c0,c1,4) [11]=ID_MMFR1 (c0,c1,5)
@ [12]=ID_MMFR2 (c0,c1,6) [13]=ID_MMFR3 (c0,c1,7)
@ [14]=ID_ISAR0 (c0,c2,0) [15]=ID_ISAR1 (c0,c2,1)
@ [16]=ID_ISAR2 (c0,c2,2) [17]=ID_ISAR3 (c0,c2,3)
@ [18]=ID_ISAR4 (c0,c2,4) [19]=SCTLR (c1,c0,0)
@ [20]=ACTLR (c1,c0,1) [21]=cache size (c0,c0,1 again, as a check)
@ [22]=0

        .syntax unified
        .arm
        .section .text
        .global blob_entry
        .type blob_entry, %function
blob_entry:
        stmfd   sp!, {r4-r7, lr}
        mrs     r4, cpsr
        mov     r5, r0 @ buffer
        mov     r6, r1 @ capacity (words)

        cmp     r6, #23
        movlt   r0, #0
        blt     1f

        ldr     r3, =0xCAFEBABE
        str     r3, [r5, #0]
        str     r2, [r5, #4] @ cookie

        mov     r3, #0 @ sum(1..256) in a loop -> proves it actually ran
        mov     r7, #0
2:      add     r7, r7, #1
        add     r3, r3, r7
        cmp     r7, #256
        blt     2b
        str     r3, [r5, #8]

        str     r4, [r5, #12] @ entry CPSR

        mrc     p15, 0, r3, c0, c0, 0
        str     r3, [r5, #16] @ MIDR
        mrc     p15, 0, r3, c0, c0, 1
        str     r3, [r5, #20] @ CTR
        mrc     p15, 0, r3, c0, c0, 3
        str     r3, [r5, #24] @ TLBTR
        mrc     p15, 0, r3, c0, c1, 0
        str     r3, [r5, #28] @ ID_PFR0
        mrc     p15, 0, r3, c0, c1, 1
        str     r3, [r5, #32] @ ID_PFR1
        mrc     p15, 0, r3, c0, c1, 2
        str     r3, [r5, #36] @ ID_DFR0
        mrc     p15, 0, r3, c0, c1, 4
        str     r3, [r5, #40] @ ID_MMFR0
        mrc     p15, 0, r3, c0, c1, 5
        str     r3, [r5, #44] @ ID_MMFR1
        mrc     p15, 0, r3, c0, c1, 6
        str     r3, [r5, #48] @ ID_MMFR2
        mrc     p15, 0, r3, c0, c1, 7
        str     r3, [r5, #52] @ ID_MMFR3
        mrc     p15, 0, r3, c0, c2, 0
        str     r3, [r5, #56] @ ID_ISAR0
        mrc     p15, 0, r3, c0, c2, 1
        str     r3, [r5, #60] @ ID_ISAR1
        mrc     p15, 0, r3, c0, c2, 2
        str     r3, [r5, #64] @ ID_ISAR2
        mrc     p15, 0, r3, c0, c2, 3
        str     r3, [r5, #68] @ ID_ISAR3
        mrc     p15, 0, r3, c0, c2, 4
        str     r3, [r5, #72] @ ID_ISAR4
        mrc     p15, 0, r3, c1, c0, 0
        str     r3, [r5, #76] @ SCTLR
        mrc     p15, 0, r3, c1, c0, 1
        str     r3, [r5, #80] @ ACTLR
        mrc     p15, 0, r3, c0, c0, 1
        str     r3, [r5, #84] @ CTR (again)
        mov     r3, #0
        str     r3, [r5, #88]

        mov     r0, #23
1:
        ldmfd   sp!, {r4-r7, pc}
        .ltorg
        .size blob_entry, .-blob_entry