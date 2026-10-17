@ trampoline for hooking karap's cbus write fn FUN_800b8494 (the single choke
@ point for every PMIC/CBUS write symbian makes). we overwrite its first 2
@ instructions with `ldr pc,[pc,#-4]; <this>` so every call lands here first.
@ at entry r0=dev r1=reg r3=val (the original function's args, not yet saved).
@ we log {dev,reg,val} into a ram ring, restore the regs exactly, run the two
@ relocated original instructions, then jump back to 0x800b849c (the 3rd insn).
@ runs in whatever context called the fn (any thread/dfc, maybe irqs off) -> keep
@ it minimal, ram only. the LDD patches ringptr after copying.

        .syntax unified
        .arm
        .section .text
        .global hook8494
        .type hook8494, %function
hook8494:
        push    {r0-r3, r12, lr}
        ldr     r12, ringptr @ ring base (patched by LDD)
        ldr     r2, [r12] @ count
        and     lr, r2, #63 @ slot = count & 63
        add     lr, r12, lr, lsl #4 @ ring + slot*16
        str     r0, [lr, #4] @ dev
        str     r1, [lr, #8] @ reg
        str     r3, [lr, #12] @ val
        add     r2, r2, #1
        str     r2, [r12] @ count++
        pop     {r0-r3, r12, lr}
        push    {r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, lr} @ relocated 0x800b8494 (e92d5ffc)
        mov     r5, r1 @ relocated 0x800b8498
        ldr     pc, ret8494 @ back to the 3rd instruction
ringptr: .word 0
ret8494:  .word 0x800b849c
        .size hook8494, .-hook8494