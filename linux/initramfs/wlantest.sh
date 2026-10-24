#!/bin/busybox sh
# wlantest: the N8's TI WL1271 under wlcore (kernel with WLAN=1, n8-wlan.dtb):
# probe, firmware boot, MAC, interrupts, then a scan. capture the output.
#
#   wlantest          probe + firmware + scan (chip kept out of ELP sleep,
#                     WLAN_ELP=1 wlantest allows it)
#   wlantest <ssid> <passphrase>
#                     the same, then WPA2 connect + DHCP (busybox udhcpc)
#
# needs WLAN on in Symbian before the jump (the modem switches the WL1271's
# 19.2 MHz reference clock). the firmware boot over the polled spi takes a
# few seconds
SSID="$1"
PSK="$2"
LOG=/tmp/wlantest.kmsg

kmsg() { dmesg | grep -iE "wl1271|wlcore|wl12xx|spi|wlan|cfg80211|firmware" | grep -v videocore; }
irqs() { grep -iE "wl|gpio.* 73" /proc/interrupts || echo "(no wlan irq line in /proc/interrupts)"; }

echo "== kernel log so far"
kmsg
echo "== interface"
ls /sys/class/net/
# probe again with other settings: unbind / bind the spi glue, the core
# device and its probe (power on, chip id) come back with it
reprobe() {
	echo "== reprobe: $*"
	echo spi0.0 > /sys/bus/spi/drivers/wl1271_spi/unbind 2>/dev/null
	dmesg -c > /dev/null
	echo spi0.0 > /sys/bus/spi/drivers/wl1271_spi/bind
	sleep 5
	dmesg -c | grep -v videocore
}
if [ ! -d /sys/class/net/wlan0 ]; then
	reprobe "again, w32frame=$(cat /sys/module/spi_rapuyama/parameters/w32frame)"
fi
if [ ! -d /sys/class/net/wlan0 ]; then
	echo 16 > /sys/module/spi_rapuyama/parameters/w32frame
	reprobe "w32frame=16 (halfword frames)"
fi
if [ ! -d /sys/class/net/wlan0 ]; then
	echo "no wlan0: wlcore didn't probe (log above)"
	echo "== wlan platform devices"
	ls -l /sys/bus/platform/devices/ | grep -iE "wl1|wl12" 
	for d in /sys/bus/platform/devices/wl1*; do
		[ -e "$d" ] && echo "$d driver: $(readlink $d/driver)"
	done
	echo "== blocked tasks (sysrq w)"
	echo w > /proc/sysrq-trigger
	sleep 1
	dmesg | sed -n '/Show Blocked State/,$p' | grep -v videocore
	exit 1
fi
# keep the chip out of ELP (deep sleep): wlcore enters it through runtime
# PM, and waking it times out on the N8 ("ELP wakeup timeout", likely the
# modem-gated reference clock not coming back). WLAN_ELP=1 allows it
if [ "$WLAN_ELP" != 1 ]; then
	for d in /sys/bus/platform/devices/wl1*; do
		[ -e "$d/power/control" ] && echo on > "$d/power/control" &&
			echo "== $d: runtime pm off (no ELP)"
	done
fi
echo "== firmware files"
ls -la /lib/firmware/ti-connectivity/

echo "== irq before"
irqs
dmesg -c > /dev/null
echo "== ip link set wlan0 up (loads the firmware)"
ip link set wlan0 up
echo "exit $?"
sleep 2
dmesg -c | tee $LOG | grep -v videocore
echo "== irq after"
irqs
ip link show wlan0
iw dev wlan0 info

echo "== scan"
iw dev wlan0 scan > /tmp/scan.txt 2>&1
echo "exit $?"
grep -E "^BSS|SSID:|signal:|freq:" /tmp/scan.txt | head -60
dmesg -c | grep -v videocore
echo "== irq after scan"
irqs

if [ -n "$SSID" ]; then
	echo "== connect to $SSID"
	mkdir -p /run/wpa
	{
		echo "ctrl_interface=/run/wpa"
		echo "network={"
		echo "	ssid=\"$SSID\""
		echo "	psk=\"$PSK\""
		echo "}"
	} > /tmp/wpa.conf
	wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
		sleep 2
		st=$(wpa_cli -p /run/wpa -i wlan0 status | sed -n 's/^wpa_state=//p')
		echo "  $st"
		[ "$st" = COMPLETED ] && break
	done
	dmesg -c | grep -v videocore
	[ "$st" = COMPLETED ] || { echo "not connected"; exit 1; }
	udhcpc -i wlan0 -n -q -s /usr/share/udhcpc/default.script
	ip addr show wlan0
	ping -c 4 1.1.1.1
fi
echo "== done"
