@ trampoline for hooking the ASIC modem TX send FUN_800beac8 (the single point
@ that writes a framed message to the modem TX byte-FIFO phys 0x40100000). we
@ overwrite its first 2 instructions with `ldr pc,[pc,#-4]; <this>` so every call
@ lands here first. at entry r0=this r1=descriptor(message) r2=channel. we log
@ {descriptor_ptr, channel, descriptor_word0} into a ram ring, then restore the
@ regs, run the two relocated original instructions, and jump back to 0x800bead0
@ (the 3rd insn). the app walks the ring and ReadVirt-decodes each descriptor to
@ recover the exact bytes Symbian sends the modem. ram only, minimal.

        .syntax unified
        .arm
        .section .text
        .global hookbeac8
        .type hookbeac8, %function
hookbeac8:
        push    {r0-r3, r12, lr}
        ldr     r12, ringptr        @ ring base (patched by LDD)
        ldr     r0, [r12]           @ count
        and     lr, r0, #31         @ slot = count & 31
        add     lr, r12, lr, lsl #4 @ ring + slot*16
        str     r1, [lr, #4]        @ descriptor ptr (message)
        str     r2, [lr, #8]        @ channel
        ldr     r3, [r1]            @ descriptor word0 (len | type)
        str     r3, [lr, #12]
        add     r0, r0, #1
        str     r0, [r12]           @ count++
        pop     {r0-r3, r12, lr}
        push    {r4, r5, r6, lr}    @ relocated 0x800beac8 (e92d4070)
        mov     r6, r1              @ relocated 0x800beacc (e1a06001)
        ldr     pc, retbeac8        @ back to the 3rd instruction (0x800bead0)
ringptr:  .word 0
retbeac8: .word 0x800bead0
        .size hookbeac8, .-hookbeac8