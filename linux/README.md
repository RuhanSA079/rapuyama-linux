
# Linux on Nokia E6

This directory contains the Linux kernel, board description, boot trampoline and Symbian-side loader for the Nokia E6 (RapuYama, ARM1176JZF-S / ARMv6K).

**Current milestone: the phone boots into Linux userspace.** The successful boot log in `../docs/first-boot_SUCCESS.log` shows the timer interrupt being handled, the RapuYama clocksource becoming active, and BusyBox `/init` starting.
The USB gadget configuration exposes an interactive userspace shell over USB serial; the project owner has confirmed terminal input/output with picocom.

Start here:

- [`kernel/README.md`](kernel/README.md) - toolchain and kernel build.
- [`boot/README.md`](boot/README.md) - DTBs, boot files, and USB shell.
- [`../docs/driver-re.md`](../docs/driver-re.md) - hardware and driver findings.
- [`../README.md`](../README.md) - project overview and top-level instructions.

The port is at an early bring-up stage: a usable shell is working, but that does not imply complete display, storage, radio, audio, or power-management support.