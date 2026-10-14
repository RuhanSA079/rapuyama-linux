
# Linux on Nokia E6

This directory contains the Linux kernel, board description, boot trampoline and Symbian-side loader for the Nokia E6 (RapuYama, ARM1176JZF-S / ARMv6K).

**Current milestone: Linux drives the phone's own display.** The kernel boots the BCM2727B1 VideoCore with Nokia's firmware, powers the LCD panel up and shows fbcon on `/dev/fb0` (log: `../docs/first-display_SUCCESS.log`, details: `../docs/videocore-display.md`).

**Previous milestone: the phone boots into Linux userspace.** The successful boot log in `../docs/first-boot_SUCCESS.log` shows the timer interrupt being handled, the RapuYama clocksource becoming active, and BusyBox `/init` starting.
The USB gadget configuration exposes an interactive userspace shell over USB serial; the project owner has confirmed terminal input/output with picocom.

Start here:

- [`kernel/README.md`](kernel/README.md) - toolchain and kernel build, including the display build `build-vc.sh`.
- [`boot/README.md`](boot/README.md) - DTBs, boot files, and USB shell.
- [`../docs/driver-re.md`](../docs/driver-re.md) - hardware and driver findings.
- [`../docs/videocore-display.md`](../docs/videocore-display.md) - the VideoCore display driver.
- [`../docs/porting-guide.md`](../docs/porting-guide.md) - porting all this to another phone.
- [`../README.md`](../README.md) - project overview and top-level instructions.

The port is at an early bring-up stage: a usable shell and a framebuffer console are working, but that does not imply complete display (artifacts, no blanking or backlight control), storage, radio, audio, or power-management support.