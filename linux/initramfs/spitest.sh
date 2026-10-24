#!/bin/busybox sh
# spitest: the whole WL1271 SPI test in one go (work/spi-n8/NOTES.md):
# WLAN enable (genio100) high, spidev bound to the WL1271 node, then
# wspitest once per io32 / rxen combination with the driver's register
# dumps (debug=2), the kernel log of each run right after it.
#
#   spitest          the working settings (io32=0 rxen=0) at 1 MHz, no
#                    register dumps: quick, done well before the watchdog
#   spitest 100000   the same at another speed
#   spitest all [hz] every io32 / rxen combination with register dumps
#
# needs n8-spi.dtb as E:\n8.dtb. the WL1271 19.2 MHz reference clock comes from
# the modem: n8boot requests it before the jump (E:\wlansteps.txt). capture the
# whole output (serial terminal log)
ALL=
[ "$1" = all ] && { ALL=1; shift; }
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

if [ -n "$ALL" ]; then
	COMBOS="0_0 1_0 0_7 1_7"
	echo 2 > $P/debug
else
	COMBOS="0_0"
	echo 0 > $P/debug
fi
for combo in $COMBOS; do
	combo=$(echo $combo | tr _ ' ')
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
