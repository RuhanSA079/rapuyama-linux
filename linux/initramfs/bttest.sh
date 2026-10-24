#!/bin/busybox sh
# bttest: the N8's bluetooth (CSR BlueCore on the rapuyama uart, kernel patch
# 0015 -> /dev/ttyRY0, then hci_uart).
#
#   bttest            attach with bcsp, show the controller, scan
#   bttest h4         attach with h4 instead (bcsp is what BC02 normally uses)
#   bttest none       set the port up but don't attach, for poking at it by hand
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

say "uart registers (0x20025a00: 04 lock, 34 irq status, 38 irq mask, 84 enable)"
for off in 04 34 38 84 9c a0 2c; do
	printf '  +0x%s = %s\n' "$off" "$(physdump $((0x20025a00 + 0x$off)) 4 2>/dev/null | od -An -tx4 | tr -d ' ')"
done

say "bt chip gpios (reset $RST, wake $WAKE)"
for g in $RST $WAKE; do
	[ -d /sys/class/gpio/gpio$g ] || echo $g > /sys/class/gpio/export 2>/dev/null
	[ -d /sys/class/gpio/gpio$g ] && echo "  gpio$g = $(cat /sys/class/gpio/gpio$g/value 2>/dev/null)"
done

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
