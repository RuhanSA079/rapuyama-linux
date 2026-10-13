@ first-boot /init: sleeps forever. if the phone stays alive (frozen, not powering
@ off) for minutes after boot.sis, the kernel got all the way to userspace with
@ working timer/irqs/scheduler and the watchdog driver keeps the pmic fed
        .arm
        .global _start
_start:
        mov     r7, #29 @ __NR_pause (arm eabi)
1:      svc     #0
        b       1b