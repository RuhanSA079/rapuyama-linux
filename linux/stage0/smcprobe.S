@ one-way secure-call feasibility probe. kill the mmu/symbian like kickloop,
@ feed the two known watchdogs, then issue ONE smc #0 the way securitykext's
@ monitor-call routine does (0x80140480: pass a buffer range via mcrr p15,0,c12,
@ r0=0, barriers) and record whether we came back + the returned regs into the
@ ram log at 0x1f080000. then heartbeat-feed the watchdogs forever so the phone
@ stays alive as long as it can. read back with e6logs, parse_smcprobe.py
@
@ goal: does a bare-metal smc #0 enter+exit the secure monitor safely after we
@ kill symbian? 'after' marker set = we can reach the monitor (foundation for a
@ keepalive). only 'before' set = the smc did not return (hang/reset). the buffer
@ is zeroed so this is not a real request yet, just a round-trip test
@
@ no stack reliance, registers + fixed ram only, phys addresses (mmu off)

        .syntax unified
        .arm
        .arch armv6zk @ arm1176jzf-s: armv6 + trustzone(z) + k, enables smc
        .section .text

        .equ LOG,  0x1f080000 @ result block (survives warm reset, e6logs dumps it)
        .equ BUF,  0x1f0a0000 @ zeroed secure-call buffer (monitor reads it via c12)
        .equ SOC,  0x200336c4 @ soc watchdog counter
        .equ CBUS, 0x20030700 @ cbus master
        .equ FC,   0x20033350 @ fast counter (latch at +4), 32768 hz

        .global smcprobe
        .type smcprobe, %function
smcprobe:
        cpsid   if
        mrs     r12, cpsr
        mrc     p15, 0, r11, c1, c0, 0 @ entry sctlr
        mov     r2, #0
        mcr     p15, 0, r2, c7, c14, 0 @ clean+inval D
        mcr     p15, 0, r2, c7, c5, 0 @ inval I
        mcr     p15, 0, r2, c7, c10, 4 @ DSB
        mcr     p15, 0, r2, c7, c5, 4 @ ISB
        mov     r3, r11
        bic     r3, r3, #0x5 @ clear M (mmu) + C (D cache)
        mcr     p15, 0, r3, c1, c0, 0 @ MMU OFF, symbian is gone
        mcr     p15, 0, r3, c7, c5, 4 @ ISB

        @ init the result block
        ldr     r10, =LOG
        ldr     r0, =0x31434d53 @ 'SMC1'
        str     r0, [r10, #0]
        mov     r0, #1
        str     r0, [r10, #4] @ version
        mov     r0, #0
        str     r0, [r10, #8] @ before marker
        str     r0, [r10, #12] @ after marker
        str     r0, [r10, #16] @ r0 after
        str     r0, [r10, #20] @ r1 after
        str     r0, [r10, #24] @ r2 after
        str     r0, [r10, #28] @ r3 after
        str     r12, [r10, #32] @ entry cpsr
        str     r11, [r10, #36] @ entry sctlr
        str     r0, [r10, #44] @ heartbeat count
        str     r0, [r10, #48] @ last counter
        str     r0, [r10, #52] @ smc count
        str     r0, [r10, #60] @ start counter

        @ zero the secure-call buffer (0x60 bytes = 24 words)
        ldr     r1, =BUF
        mov     r0, #0
        mov     r2, #24
zbuf:
        str     r0, [r1], #4
        subs    r2, r2, #1
        bne     zbuf

        bl      feed @ both watchdogs once, with margin, before the call

        ldr     r9, =FC @ record the start fast counter
        mov     r0, #0
        str     r0, [r9, #4]
        ldr     r0, [r9]
        str     r0, [r10, #60]

        @ === the secure call, faithful to securitykext 0x80140480 ===
        ldr     r1, =BUF
        add     r2, r1, #0x60
        mcrr    p15, 0, r2, r1, c12 @ pass buffer range to the monitor
        mov     r3, #0
        mcr     p15, 0, r3, c8, c7, 1 @ invalidate unified TLB
        mcr     p15, 0, r3, c7, c10, 4 @ DSB
        mcr     p15, 0, r3, c7, c10, 5 @ DMB
        mcr     p15, 0, r3, c7, c5, 4 @ ISB
        mcr     p15, 0, r3, c7, c5, 6 @ invalidate branch predictor
        ldr     r0, =0xb4b4b4b4
        str     r0, [r10, #8] @ before marker, published to ram
        mcr     p15, 0, r3, c7, c10, 4 @ DSB before we switch worlds
        mov     r0, #0 @ r0 = 0, the request selector lives in the buffer
        smc     #0
        @ === back here only if the monitor returned ===
        ldr     r10, =LOG @ reload, the smc may have clobbered everything
        ldr     r4, =0xa5a5a5a5
        str     r4, [r10, #12] @ after marker
        str     r0, [r10, #16]
        str     r1, [r10, #20]
        str     r2, [r10, #24]
        str     r3, [r10, #28]
        mov     r0, #1
        str     r0, [r10, #52] @ smc count
        mov     r0, #0
        mcr     p15, 0, r0, c7, c10, 4 @ DSB (armv6 cp15 form)

        @ heartbeat: feed the watchdogs forever so the phone lives as long as it
        @ can (and so we see if the smc extended survival past ~210 s)
        mov     r8, #0
hb:
        bl      feed
        add     r8, r8, #1
        str     r8, [r10, #44]
        ldr     r9, =FC
        mov     r0, #0
        str     r0, [r9, #4]
        ldr     r0, [r9]
        str     r0, [r10, #48] @ last counter
        ldr     r1, =0x00100000 @ a few ms
hbd:
        subs    r1, r1, #1
        bne     hbd
        b       hb

@ feed both watchdogs like power.dll: soc counter + pmic cmd 0x57 data 0x30
@ (karap FUN_800b8494(1,0x13,..,0x30) -> hw cmd 0x57, 16 bits). clobbers r0-r6
feed:
        ldr     r4, =SOC
        ldr     r5, =0x3fffffff
        str     r5, [r4]
        ldr     r6, =CBUS
        mov     r0, #1
        str     r0, [r6, #0x04] @ enable
        str     r0, [r6, #0x3c] @ write mode
        mov     r0, #15
        str     r0, [r6, #0x2c] @ 16 bits
        mov     r0, #0x30
        str     r0, [r6, #0x20] @ data
        mov     r0, #0x57 @ (1 << 6) | 0x17
        str     r0, [r6, #0x1c] @ go (write cmd)
        ldr     r1, =0x10000
fbusy:
        ldr     r0, [r6, #0x08]
        cmp     r0, #0
        beq     fdone
        subs    r1, r1, #1
        bne     fbusy
fdone:
        mov     r0, #0
        str     r0, [r6, #0x04]
        bx      lr

        .ltorg
        .size smcprobe, .-smcprobe