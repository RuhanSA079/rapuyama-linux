@ fallback init (/sbin/init, the kernel tries it when /init = busybox can't
@ run), a sign of life without any console: sleeps 60 s, then a warm reboot
@ (rapuyama_wdt soc reset, ram and so the ramcon log survive). after boot.sis:
@ - reboot ~30 s in = busybox /init ran (init.sh)
@ - reboot ~60 s in = busybox failed, but kernel + userspace ok
@ - reset ~13 s in = died before the watchdog driver (or hung after it)
@ - power off ~48 s in = pmic watchdog expired: hung with the soc one fed
        .arm
        .global _start
_start:
        adr     r0, ts
        mov     r1, #0
        mov     r7, #162 @ __NR_nanosleep
        svc     #0
        ldr     r0, =0xfee1dead @ LINUX_REBOOT_MAGIC1
        ldr     r1, =672274793 @ LINUX_REBOOT_MAGIC2
        ldr     r2, =0x01234567 @ LINUX_REBOOT_CMD_RESTART
        mov     r3, #0
        mov     r7, #88 @ __NR_reboot
        svc     #0
        mov     r7, #29 @ __NR_pause, if reboot returned
1:      svc     #0
        b       1b
        .align  2
ts:     .word   60, 0 @ struct timespec {60 s, 0 ns}
        .ltorg