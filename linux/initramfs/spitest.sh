#!/bin/busybox sh
# spitest: the whole WL1271 SPI test in one go (work/spi-n8/NOTES.md):
# WLAN enable (genio100) high, spidev bound to the WL1271 node, then
# wspitest once per io32 / rxen combination with the driver's register
# dumps (debug=2), the kernel log of each run right after it.
#
#   spitest          at 1 MHz
#   spitest 100000   at another speed
#
# needs n8-spi.dtb as E:\n8.dtb and WLAN on in Symbian before the jump (the
# modem switches the WL1271's 19.2 MHz reference clock). capture the whole
# output (serial terminal log)
HZ="${1:-1000000}"
P=/sys/module/spi_rapuyama/parameters
EN=612	# genio100 = gpiochip base 512 + 100

[ -d "$P" ] || { echo "no spi_rapuyama driver: kernel without patch 0008?"; exit 1; }

echo "== spi driver at boot"
dmesg | grep -i rapuyama-spi

echo "== WLAN enable (gpio $EN)"
[ -d /sys/class/gpio/gpio$EN ] || echo $EN > /sys/class/gpio/export
echo "before: $(cat /sys/class/gpio/gpio$EN/value)"
# "out" alone drives it low (chip off), so set the value after it
echo out > /sys/class/gpio/gpio$EN/direction
echo 1 > /sys/class/gpio/gpio$EN/value
sleep 1
echo "after: $(cat /sys/class/gpio/gpio$EN/value)"

echo "== spidev"
D=$(ls /sys/bus/spi/devices/ | head -1)
[ -n "$D" ] || { echo "no spi device: n8-spi.dtb not loaded?"; exit 1; }
if [ ! -e /sys/bus/spi/devices/$D/driver ]; then
	echo spidev > /sys/bus/spi/devices/$D/driver_override
	echo $D > /sys/bus/spi/drivers/spidev/bind
fi
DEV=$(ls /dev/spidev* 2>/dev/null | head -1)
[ -n "$DEV" ] || { echo "spidev didn't bind to $D"; exit 1; }
echo "$D -> $DEV"

echo 2 > $P/debug
for combo in "0 0" "1 0" "0 7" "1 7"; do
	set -- $combo
	echo
	echo "=========== io32=$1 rxen=$2 at $HZ Hz"
	echo $1 > $P/io32
	echo $2 > $P/rxen
	dmesg -c > /dev/null
	wspitest "$DEV" "$HZ"
	echo "----------- kernel log"
	dmesg -c | grep -v videocore
done
echo 0 > $P/debug
echo
echo "== done"
