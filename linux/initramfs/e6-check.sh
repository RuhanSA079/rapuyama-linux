#!/bin/busybox sh
# one boot report for the charger and display, no raw register reads
snapshot() {
	printf '\n%s\n' "$1"
	cat /proc/uptime
	for p in /sys/class/power_supply/*; do
		[ -e "$p" ] || continue
		echo "$p"
		for n in online current_max voltage_now temp capacity status mode timer charge_status fault_status; do
			[ -r "$p/$n" ] || continue
			printf '  %s: ' "$n"
			cat "$p/$n"
		done
	done
}
report() {
	uname -a
	cat /proc/cmdline
	snapshot 'initial power supplies'
	# retry once after enumeration to distinguish startup from persistent failure
	if [ -e /sys/bus/i2c/devices/1-006b ] &&
	   [ ! -e /sys/bus/i2c/devices/1-006b/driver ] &&
	   [ -w /sys/bus/i2c/drivers/bq2415x-charger/bind ]; then
		echo 'retrying charger probe after enumeration'
		echo 1-006b > /sys/bus/i2c/drivers/bq2415x-charger/bind
	fi
	echo 'waiting 45 seconds for several charger watchdog services'
	sleep 45
	snapshot 'power supplies after 45 seconds'
	dmesg > /tmp/e6-dmesg.txt
	printf '\n%s\n' 'kernel diagnostics'
	grep -Ei 'i2c|bq241|battery|charger|host pads|gfx|dma|display up|frames in|swd.*call|watchdog|oops|panic' /tmp/e6-dmesg.txt
	echo 'full dmesg: /tmp/e6-dmesg.txt; report: /tmp/e6-check.txt'
}
report 2>&1 | tee /tmp/e6-check.txt