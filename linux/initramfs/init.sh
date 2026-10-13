#!/bin/busybox sh
# bring-up /init (busybox).
#
# with e6.usbsh on the kernel cmdline (e6-usb.dtb): a shell on the usb gadget
# serial port (ttyGS0 on the phone -> /dev/ttyACM0 on the pc), no reboot. if
# the pc never enumerates the gadget, log why and do a warm reboot so
# ramread.sis can show it.
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

# heartbeat into the kernel log: after a reset the ramcon dump shows how long
# linux really lived (helps telling a pmic watchdog death at ~48 s from others)
(while true; do sleep 10; log "alive, uptime $(cut -d' ' -f1 /proc/uptime) s"; done) &

log "init.sh v2 (usb shell + heartbeat)"

# give up on the usb shell: dump what we know into the kernel log, warm reboot
usb_fail() {
	log "usb shell failed: $*"
	for u in /sys/class/udc/*; do
		[ -e "$u" ] && log "udc $(basename "$u") state=$(cat "$u/state" 2>/dev/null)"
	done
	ls /sys/class/udc > /dev/kmsg 2>&1
	log "rebooting in 5 s"
	sleep 5
	reboot -f
}

if grep -q e6.usbsh /proc/cmdline; then
	# g_serial is built in, it binds as soon as the musb udc shows up
	n=0
	while [ ! -c /dev/ttyGS0 ]; do
		n=$((n + 1))
		[ $n -gt 20 ] && usb_fail "no /dev/ttyGS0 after 20 s (musb or g_serial didn't come up)"
		sleep 1
	done
	log "ttyGS0 is there, waiting for the pc to enumerate us"

	# the pc has to configure the gadget, else nobody will ever see the shell
	n=0
	until grep -q configured /sys/class/udc/*/state 2>/dev/null; do
		n=$((n + 1))
		[ $n -gt 90 ] && usb_fail "not configured by the pc after 90 s (cable? vbus? pullup?)"
		sleep 1
	done
	log "usb configured, shell on ttyGS0"

	# getty makes ttyGS0 the controlling tty, so ctrl-c and job control work.
	# respawn: closing the terminal on the pc just gives a fresh shell
	while true; do
		if command -v getty >/dev/null; then
			getty -n -l /bin/sh -L 115200 ttyGS0 vt100
		else
			setsid sh -c 'exec sh </dev/ttyGS0 >/dev/ttyGS0 2>&1'
		fi
		sleep 1
	done
fi

log "userspace up, reboot in 30 s"
sleep 30
log "rebooting"
reboot -f
