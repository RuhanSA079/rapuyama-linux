@ fallback init (/sbin/init, the kernel tries it when /init = busybox can't
@ run), a sign of life without any console: sleeps 60 s, then powers the
@ phone off (rapuyama_wdt expires the pmic watchdog). so after boot.sis:
@ - clean power off ~30 s in = busybox /init ran (init.sh)
@ - clean power off ~60 s in = busybox failed, but kernel + userspace ok
@ - black screen reset ~13 s in = died before the watchdog driver
@ - frozen for ever = kernel alive but this never ran or power off failed
        .arm
        .global _start
_start:
        adr     r0, ts
        mov     r1, #0
        mov     r7, #162 @ __NR_nanosleep
        svc     #0
        ldr     r0, =0xfee1dead @ LINUX_REBOOT_MAGIC1
        ldr     r1, =672274793 @ LINUX_REBOOT_MAGIC2
        ldr     r2, =0x4321fedc @ LINUX_REBOOT_CMD_POWER_OFF
        mov     r3, #0
        mov     r7, #88 @ __NR_reboot
        svc     #0
        mov     r7, #29 @ __NR_pause, if power off returned
1:      svc     #0
        b       1b
        .align  2
ts:     .word   60, 0 @ struct timespec {60 s, 0 ns}
        .ltorg