@ fake linux kernel entry, to prove the handoff abi
@
@ sits at payload base + 0x8000 (like a real zImage TEXT_OFFSET) and gets
@ entered by tramp_entry with the MMU OFF and the arm linux boot regs set.
@ echoes the boot regs it got into the marker page and writes a "ran"
@ signature, then returns (reversible test). registers only, no stack, no
@ literal pool. for the real boot this gets replaced by the actual zImage
@
@ on entry (mmu off): r0=0, r1=machtype, r2=dtb phys, r3=marker phys

        .syntax unified
        .arm
        .section .text
        .global fake_kernel
        .type fake_kernel, %function
fake_kernel:
        str     r0, [r3, #0] @ echo r0 (expect 0)
        str     r1, [r3, #4] @ echo r1 (machtype)
        str     r2, [r3, #8] @ echo r2 (dtb ptr)
        mov     r12, #0x5A000000 @ "i ran as the kernel" signature
        str     r12, [r3, #12]
        bx      lr
        .size fake_kernel, .-fake_kernel