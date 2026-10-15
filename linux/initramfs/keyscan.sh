#!/bin/busybox sh
# key finder: which pin / keypad code is behind each button.
#
#   keyscan            learn the default key list (below)
#   keyscan a b c      learn keys a b c
#   keyscan raw        just print every event, ctrl-c to stop
#
# two sources, polled every ~20 ms:
# - gpio levels, banks 0-4 (0x200319f0 + 0x10 * k, egpio): side keys wired
#   straight to pins. event "gpioN 0/1", N = bank * 32 + bit like in the dt
# - the keypad chip on i2c 0x44 (keypad_exp.dll), if i2c is up (e6-keys.dtb).
#   reg 0x80 = 3 -> toshiba tc3589x (pop the event fifo, reg 0x10), 0 ->
#   lm8323 style (read int 0x82, then fifo 0x89). symbian left it configured,
#   so no setup here. event "kpXX down/up", XX = raw chip code
# learn: a 5 s noise check first (pins that move on their own are dropped),
# then per key "press it twice, Enter on the pc". everything seen twice or
# more is the answer. results in /tmp/keys.txt, paste that to claude
DEFAULT_KEYS="power volup playpause voldown call end home calendar messages
contacts left up right down center q w e r t y u i o p a s d f g h j k l
backspace shift z x c v b n m comma dot enter fn ctrl sym space"

EV=/tmp/keyev
OUT=/tmp/keys.txt
KP=
BUS=

kp_detect() {
	local d id
	for d in /dev/i2c-*; do
		[ -c "$d" ] || continue
		BUS=${d#/dev/i2c-}
		break
	done
	if [ -z "$BUS" ]; then
		echo "no i2c bus (boot e6-keys.dtb for the keypad chip), gpio only"
		return
	fi
	id=$(i2cget -y "$BUS" 0x44 0x80 2>/dev/null)
	case "$id" in
	0x03)	KP=tc
		echo "keypad: toshiba tc3589x (id 3, version $(i2cget -y "$BUS" 0x44 0x81 2>/dev/null))" ;;
	0x00)	KP=lm
		echo "keypad: lm8323 style (id 0)" ;;
	"")	echo "keypad: no answer from i2c-$BUS 0x44, gpio only" ;;
	*)	echo "keypad: unknown id $id at i2c-$BUS 0x44, gpio only" ;;
	esac
}

kp_poll() {
	local n=0 c i
	case "$KP" in
	tc)
		while [ $n -lt 16 ]; do
			c=$(i2cget -y "$BUS" 0x44 0x10 2>/dev/null) || break
			[ $((c & 0x7f)) -eq $((0x7f)) ] && break
			if [ $((c & 0x80)) -ne 0 ]; then
				printf 'kp%02x up\n' $((c & 0x7f))
			else
				printf 'kp%02x down\n' $((c & 0x7f))
			fi
			n=$((n + 1))
		done
		# clear the keypad + event irq like tc3589x-keypad does
		[ $n -gt 0 ] && i2cset -y "$BUS" 0x44 0x08 0x03 2>/dev/null
		;;
	lm)
		i=$(i2cget -y "$BUS" 0x44 0x82 2>/dev/null) || return
		[ $((i & 1)) -eq 0 ] && return
		while [ $n -lt 15 ]; do
			c=$(i2cget -y "$BUS" 0x44 0x89 2>/dev/null) || break
			[ $((c)) -eq 0 ] && break
			if [ $((c & 0x80)) -ne 0 ]; then
				printf 'kp%02x down\n' $((c & 0x7f))
			else
				printf 'kp%02x up\n' $((c & 0x7f))
			fi
			n=$((n + 1))
		done
		;;
	esac
}

gpio_read() {
	local k
	for k in 0 1 2 3 4; do
		eval "n$k=\$(devmem $((0x200319f0 + 0x10 * k)) 32)"
	done
}

# print the pins that changed since the last call
gpio_diff() {
	local k b o n x
	for k in 0 1 2 3 4; do
		eval "o=\$o$k n=\$n$k"
		x=$((o ^ n))
		[ $x -eq 0 ] && continue
		b=0
		while [ $b -lt 32 ]; do
			[ $(((x >> b) & 1)) -eq 1 ] &&
				echo "gpio$((k * 32 + b)) $(((n >> b) & 1))"
			b=$((b + 1))
		done
		eval "o$k=\$n$k"
	done
}

poller() {
	local k
	gpio_read
	for k in 0 1 2 3 4; do
		eval "o$k=\$n$k"
	done
	kp_poll >/dev/null	# whatever symbian left in the fifo
	while :; do
		gpio_read
		gpio_diff
		kp_poll
		usleep 20000
	done
}

# sources seen in the event log after line $1, "count source" per line
seen_since() {
	tail -n +$(($1 + 1)) "$EV" | cut -d' ' -f1 | sort | uniq -c
}

learn() {
	local pid start noise name res cnt src
	: > "$EV"
	poller >> "$EV" &
	pid=$!
	trap 'kill $pid 2>/dev/null; exit' INT TERM EXIT

	echo "noise check: don't touch the phone for 5 s"
	start=$(wc -l < "$EV")
	sleep 5
	noise=$(seen_since "$start" | while read -r cnt src; do echo "$src"; done)
	[ -n "$noise" ] && echo "noisy, ignored:" $noise

	: > "$OUT"
	for name in "$@"; do
		start=$(wc -l < "$EV")
		printf 'press %-10s twice, then Enter here (s = skip): ' "$name"
		read -r res
		[ "$res" = s ] && { echo "$name: skipped" >> "$OUT"; continue; }
		res=$(seen_since "$start" | while read -r cnt src; do
			case " $(echo $noise) " in *" $src "*) continue ;; esac
			[ "$cnt" -ge 2 ] && printf ' %s(x%s)' "$src" "$cnt"
		done)
		[ -z "$res" ] && res=" nothing"
		echo "  ->$res"
		echo "$name:$res" >> "$OUT"
	done
	echo
	echo "done, results in $OUT:"
	cat "$OUT"
}

kp_detect
if [ "$1" = raw ]; then
	echo "every event, ctrl-c to stop"
	poller
elif [ $# -gt 0 ]; then
	learn "$@"
else
	learn $DEFAULT_KEYS
fi