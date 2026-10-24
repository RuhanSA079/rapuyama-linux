#!/bin/busybox sh
# bttest: the N8's bluetooth (CSR BlueCore on the rapuyama uart, kernel patch
# 0015 -> /dev/ttyRY0, then hci_uart).
#
#   bttest            attach with bcsp, show the controller, scan
#   bttest h4         attach with h4 instead (bcsp is what BC02 normally uses)
#   bttest none       set the port up but don't attach, for poking at it by hand
#
# NOENABLE=1 leaves the uart block alone if Symbian left it off,
# NOGPIO=1 leaves the bt reset/wake lines alone.
#
# needs bluetooth switched on in Symbian before the jump (the uart's power,
# clock and pin mux come from there), and n8boot's wlan step on, which also
# turns on the clock BT shares with the WL1271 reference (PRM 0x25 / gate 0x1a).
# capture the whole output.
PROTO="${1:-bcsp}"
RST=566	# genio54, bt reset  (gpiochip base 512 + 54)
WAKE=565	# genio53, bt wake
DEV=/dev/ttyRY0

say() { echo; echo "== $*"; }

say "uart driver at boot"
dmesg | grep -iE "rapuyama_uart|ttyRY" || echo "  (nothing: SERIAL_RAPUYAMA not in this kernel?)"
[ -e "$DEV" ] || { echo "no $DEV, the uart did not probe"; exit 1; }
ls -l "$DEV"

# devmem, not physdump: physdump wants a 4 KB aligned address and these are not
say "uart registers (04 lock, 34 irq status, 38 irq mask, 84 enable, 9c busy, a0 tx room, 2c rx level)"
for off in 04 2c 34 38 84 9c a0; do
	printf '  +0x%s = %s\n' "$off" "$(devmem $((0x20025a00 + 0x$off)) 32 2>/dev/null)"
done

EN=$(devmem 0x20025a84 32 2>/dev/null)
if [ "$EN" = 0x00000000 ]; then
	say "the block is OFF (enable 0x84 = $EN)"
	echo "  Symbian hands the uart over disabled when bluetooth is not in use."
	echo "  Every byte written goes nowhere, which is what the last run did."
	if [ "$NOENABLE" = 1 ]; then
		echo "  NOENABLE=1, leaving it off"
	else
		echo "  switching tx+rx on (0x84 = 3); NOENABLE=1 bttest skips this"
		devmem 0x20025a04 32 1
		devmem 0x20025a84 32 3
		devmem 0x20025a04 32 0
		echo "  enable now $(devmem 0x20025a84 32)"
	fi
fi

say "bt chip gpios (reset $RST, wake $WAKE)"
for g in $RST $WAKE; do
	[ -d /sys/class/gpio/gpio$g ] || echo $g > /sys/class/gpio/export 2>/dev/null
	[ -d /sys/class/gpio/gpio$g ] || { echo "  gpio$g: cannot export"; continue; }
	echo "  gpio$g before: $(cat /sys/class/gpio/gpio$g/value 2>/dev/null)"
	if [ "$NOGPIO" != 1 ]; then
		# "out" alone drives low, so set the value after the direction
		echo out > /sys/class/gpio/gpio$g/direction 2>/dev/null
		echo 1 > /sys/class/gpio/gpio$g/value 2>/dev/null
		echo "  gpio$g after:  $(cat /sys/class/gpio/gpio$g/value 2>/dev/null)"
	fi
done
[ "$NOGPIO" != 1 ] && sleep 1

say "interrupt counts before"
grep -E "rapuyama_uart|ttyRY" /proc/interrupts || echo "  (no uart irq line; polled build?)"

[ "$PROTO" = none ] && { echo; echo "== done (not attached)"; exit 0; }

say "btattach -B $DEV -P $PROTO"
dmesg -c > /dev/null
btattach -B "$DEV" -P "$PROTO" &
BTPID=$!
sleep 4
dmesg -c | grep -viE "videocore" | head -30

say "controller"
hciconfig -a 2>&1 | head -20

if hciconfig 2>/dev/null | grep -q hci0; then
	say "bring hci0 up"
	hciconfig hci0 up 2>&1
	sleep 1
	hciconfig -a 2>&1 | head -15
	say "scan (10 s)"
	timeout 15 hcitool scan 2>&1 | head -20
else
	echo "  no hci0: see the kernel log above"
fi

say "interrupt counts after"
grep -E "rapuyama_uart|ttyRY" /proc/interrupts || echo "  (none)"
say "uart stats"
[ -r /proc/tty/driver/rapuyama_uart ] && cat /proc/tty/driver/rapuyama_uart

kill $BTPID 2>/dev/null
say "done"
