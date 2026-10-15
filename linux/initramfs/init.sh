#!/bin/busybox sh
# bring-up /init (busybox).
#
# with e6.usbsh on the kernel cmdline (e6-usb.dtb): a shell on the usb gadget
# serial port (ttyGS0 on the phone -> /dev/ttyACM0 on the pc). it never
# reboots by itself: without a cable it just keeps running and logs why
# there is no shell. reboot / poweroff work from that shell.
#
# without it: the sign of life, sleep 30 s then a warm reboot (soc reset):
# unlike a power off it keeps the ram, so ramread.sis can pull the whole log
/bin/busybox --install -s /bin
/bin/busybox --install -s /sbin
mkdir -p /proc /sys /dev /tmp /root
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmp /tmp

export PATH=/bin:/sbin HOME=/root TERM=vt100 PS1='e6:\w# '
hostname e6

log() { echo "e6: $*" > /dev/kmsg; }

# the "alive" heartbeat is written by the kernel straight into ramcon now
# (mach-rapuyama, every 10 s), so it no longer floods the console

log "init.sh v5 (usb shell waits for enter, reboot/poweroff)"

# reboot / poweroff / halt typed in the shell: busybox signals pid 1 (TERM =
# reboot, USR2 = poweroff, USR1 = halt), and pid 1 is this script. stop
# everything, sync, then do it for real. reboot = warm reset back to symbian
shutdown_to() {
	log "$1 requested"
	kill -TERM -1 2>/dev/null
	sleep 1
	sync
	"$1" -f
}
trap 'shutdown_to reboot' TERM
trap 'shutdown_to poweroff' USR2
trap 'shutdown_to poweroff' USR1

# what the usb side looks like, into the kernel log
usb_state() {
	log "usb: $*"
	for u in /sys/class/udc/*; do
		[ -e "$u" ] && log "udc $(basename "$u") state=$(cat "$u/state" 2>/dev/null)"
	done
}

# shell on the usb gadget serial port. never reboots: without a cable the
# phone keeps running (display console) and the shell appears once a pc
# enumerates us
usb_shell() {
	# g_serial is built in, it binds as soon as the musb udc shows up
	n=0
	while [ ! -c /dev/ttyGS0 ]; do
		n=$((n + 1))
		if [ $n -eq 20 ]; then
			usb_state "no /dev/ttyGS0 after 20 s (musb or g_serial didn't come up)"
			return
		fi
		sleep 1
	done
	log "ttyGS0 is there, waiting for the pc to enumerate us"

	# the pc has to configure the gadget, else nobody will ever see the shell
	n=0
	until grep -q configured /sys/class/udc/*/state 2>/dev/null; do
		n=$((n + 1))
		[ $n -eq 90 ] && usb_state "not configured by a pc after 90 s, still waiting"
		sleep 1
	done
	log "usb configured, shell on ttyGS0"

	# getty makes ttyGS0 the controlling tty, so ctrl-c and job control work.
	# respawn: closing the terminal on the pc just gives a fresh shell.
	# e6sh waits for a plain Enter first, so pc side junk never hits sh
	while true; do
		if command -v getty >/dev/null; then
			getty -n -l /bin/e6sh -L 115200 ttyGS0 vt100
		else
			setsid sh -c 'exec /bin/e6sh </dev/ttyGS0 >/dev/ttyGS0 2>&1'
		fi
		sleep 1
	done
}

fb_test() {
	n=0
	while [ ! -c /dev/fb0 ] && [ "$n" -lt 10 ]; do
		n=$((n + 1))
		sleep 1
	done
	if [ ! -c /dev/fb0 ]; then
		log "framebuffer test requested but /dev/fb0 is missing"
	elif /usr/bin/fbtest > /dev/kmsg 2>&1; then
		log "framebuffer test pattern written"
	else
		log "framebuffer test pattern failed"
	fi
}

if grep -q e6.usbsh /proc/cmdline; then
	usb_shell &
	grep -q e6.fbtest /proc/cmdline && fb_test &
	# stay pid 1. wait returns on a signal, so the traps above run right away
	while true; do
		sleep 3600 &
		wait $!
	done
fi

log "userspace up, reboot in 30 s"
sleep 30
log "rebooting"
reboot -f
