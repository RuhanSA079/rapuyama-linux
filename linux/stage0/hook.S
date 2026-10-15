@ transport-hook trampoline for capturing securitykext's secure requests.
@ securitykext calls its monitor transport fn as transport(r0,r1,r2,r3) where
@ r1 = &descptr (0xc670c44c) and the stub's first act is ldr r1,[r1]. we point
@ obj[5] (0xc800d1d8) at THIS code instead; on every secure call we read the
@ descriptor (*r1 -> 0xc670c450 -> {cmd,PAid,w2,reqphys,respphys,flag}), append
@ descptr + those 6 words to a ram ring, restore the regs and bx to the real
@ stub so the call proceeds normally. runs in securitykext's thread, mmu on.
@ the LDD patches the two trailing words after copying: ringva + origstub.
@ entry is ARM; the caller's indirect blx interworks on the low bit (even=ARM).

        .syntax unified
        .arm
        .section .text
        .global hooktransport
        .type hooktransport, %function
hooktransport:
        push    {r0-r5, lr}
        ldr     r4, [r1] @ r4 = descptr = *arg1 (= 0xc670c450)
        ldr     r5, ringva @ ring base (patched by the LDD)
        ldr     r2, [r5] @ write index
        and     r3, r2, #63 @ wrap at 64 entries
        add     r0, r5, #0x20 @ entries start after the header
        add     r0, r0, r3, lsl #5 @ + index*32
        str     r4, [r0, #0] @ [0] = descptr
        ldr     r1, [r4, #0]
        str     r1, [r0, #4] @ [1] = cmd
        ldr     r1, [r4, #4]
        str     r1, [r0, #8] @ [2] = PAid
        ldr     r1, [r4, #8]
        str     r1, [r0, #0x0c] @ [3] = w2
        ldr     r1, [r4, #0x0c]
        str     r1, [r0, #0x10] @ [4] = reqphys
        ldr     r1, [r4, #0x10]
        str     r1, [r0, #0x14] @ [5] = respphys
        ldr     r1, [r4, #0x14]
        str     r1, [r0, #0x18] @ [6] = flag
        add     r2, r2, #1
        str     r2, [r5] @ publish the new index
        pop     {r0-r5, lr}
        ldr     r12, origstub @ the real transport fn (patched, thumb bit kept)
        bx      r12
ringva:  .word 0
origstub: .word 0
        .size hooktransport, .-hooktransport