#!/bin/busybox sh
# panel fps test, then prints what the videocore driver really pushed to the
# panel: it logs "N frames in 10 s (dma|pio): push avg .. us" every 10 s.
# letters only on purpose, the e6 has no digit row
#   fps          full screen: scrolls text (every frame changes all rows)
#   fps line     one line: a counter in place (small updates, like typing)
#   fps line 30  30 s instead of 12
MODE=full
[ "$1" = line ] && { MODE=line; shift; }
T=${1:-12}
# read uptime in this shell: no process creation in the redraw loop
clock_read() {
	read -r clock_uptime clock_idle < /proc/uptime || return 1
	clock_seconds=${clock_uptime%%.*}
}

echo "fps: $MODE test for $T s, then the driver stats"
sleep 1
clock_read || exit 1
end=$((clock_seconds + T))
i=0
if [ "$MODE" = line ]; then
	while clock_read && [ "$clock_seconds" -lt "$end" ]; do
		i=$((i + 1))
		printf '\r  counter %d  ' "$i"
	done
	echo
else
	while clock_read && [ "$clock_seconds" -lt "$end" ]; do
		i=$((i + 1))
		echo "$i the quick brown fox jumps over the lazy dog 0123456789 $i"
	done
fi
# the stats line comes every 10 s, wait for the one covering the test
sleep 10
clear
echo "fps: $MODE, $i updates in $T s"
dmesg | grep "frames in 10 s" | tail -3 | while read -r l; do
	n=${l#*videocore: }
	n=${n%% frames*}
	us=${l#*push avg }
	us=${us%% us*}
	rows=${l##*us, avg }
	rows=${rows%% rows*}
	mode=${l#*(}
	mode=${mode%%)*}
	if [ -n "$us" ] && [ "$us" -gt 0 ] 2>/dev/null; then
		echo "  $mode: $n frames in 10 s = $((n / 10)).$((n % 10)) fps, $rows rows/frame, push $((us / 1000)) ms; push-only ceiling $((1000000 / us)) fps (not measured FPS)"
	fi
done