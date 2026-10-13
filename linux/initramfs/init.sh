#!/bin/busybox sh
# bring-up /init (busybox). with e6.usbsh on the kernel cmdline: shell on the
# usb gadget serial console (ttyGS0 -> ttyACM0 on the pc), forever. without it:
# the sign of life, sleep 30 s then power off (pmic watchdog expire)
/bin/busybox --install -s /bin
mkdir -p /proc /sys /dev /tmp
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null
mount -t tmpfs tmp /tmp

if grep -q e6.usbsh /proc/cmdline; then
	echo "e6: shell on ttyGS0" > /dev/kmsg
	while true; do
		setsid sh -c 'exec sh </dev/ttyGS0 >/dev/ttyGS0 2>&1'
		sleep 1
	done
fi

echo "e6: userspace up, power off in 30 s" > /dev/kmsg
sleep 30
poweroff -f