#!/bin/busybox sh
# sensortest: every iio sensor (accelerometer, magnetometer, light, ...) and
# the battery, one line per sample, in real units:
#
#   sensortest          every 500 ms until ctrl-c
#   sensortest 200 10   every 200 ms, 10 samples
#   sensortest list     the devices and their sysfs channels, once
#
# value = in_<ch>_input if the driver has it, else (raw + offset) * scale,
# scale per channel (in_accel_x_scale) or per type (in_accel_scale). turn
# the phone around while it runs: accel z ~ +-9.8 m/s2 lying flat, magn
# follows north, light drops when you cover the sensor (top left of the
# front on the n8). proximity is a gpio (genio3), not iio: keytest
MS="${1:-500}"
COUNT="${2:-0}"
# both overridable, for testing against a fake sysfs tree
IIO="${IIO:-/sys/bus/iio/devices}"
PSY="${PSY:-/sys/class/power_supply}"

# channels of one device: in_*_raw / in_*_input -> "accel_x" ...
chans() {
	for f in "$1"/in_*_raw "$1"/in_*_input; do
		[ -e "$f" ] || continue
		f="${f##*/in_}"
		echo "${f%_*}"
	done | sort -u
}

# scale for a channel: per channel first, then per type (accel_x -> accel)
scale_of() {
	for s in "$1/in_$2_scale" "$1/in_${2%%_*}_scale" "$1/in_${2%_*}_scale"; do
		[ -e "$s" ] && { cat "$s"; return; }
	done
	echo 1
}

offset_of() {
	for s in "$1/in_$2_offset" "$1/in_${2%%_*}_offset"; do
		[ -e "$s" ] && { cat "$s"; return; }
	done
	echo 0
}

unit_of() {
	case "$1" in
	accel*) echo "m/s2" ;;
	magn*) echo "G" ;;
	anglvel*) echo "rad/s" ;;
	illuminance*) echo "lx" ;;
	temp*) echo "mC" ;;
	pressure*) echo "kPa" ;;
	proximity*) echo "" ;;
	*) echo "raw" ;;
	esac
}

value_of() {
	if [ -e "$1/in_$2_input" ]; then
		cat "$1/in_$2_input" 2>/dev/null || echo "?"
		return
	fi
	raw=$(cat "$1/in_$2_raw" 2>/dev/null) || { echo "?"; return; }
	# unscaled counts stay integers, scaled values get 3 decimals
	awk -v r="$raw" -v o="$(offset_of "$1" "$2")" -v s="$(scale_of "$1" "$2")" \
		'BEGIN { if (s == 1 && o == 0) print r; else printf "%.3f", (r + o) * s }'
}

devs=$(ls -d "$IIO"/iio:device* 2>/dev/null)
bats=$(ls -d "$PSY"/* 2>/dev/null)
if [ -z "$devs" ] && [ -z "$bats" ]; then
	echo "sensortest: no iio devices and no battery (is n8-hw.dtb booted? dmesg | grep -i -E 'iio|lis3|ak8974|tsl')"
	exit 1
fi

if [ "$1" = list ]; then
	for d in $devs; do
		echo "${d##*/}: $(cat "$d/name")"
		for c in $(chans "$d"); do
			echo "  $c  scale $(scale_of "$d" "$c") offset $(offset_of "$d" "$c") [$(unit_of "$c")]"
		done
	done
	for b in $bats; do
		echo "${b##*/}: $(cat "$b/type" 2>/dev/null) $(ls "$b" | tr '\n' ' ')"
	done
	exit 0
fi

for d in $devs; do
	echo "${d##*/} $(cat "$d/name"): $(chans "$d" | tr '\n' ' ')"
done
echo "every $MS ms, ctrl-c to stop"

n=0
while [ "$COUNT" = 0 ] || [ "$n" -lt "$COUNT" ]; do
	line="$(cut -d' ' -f1 /proc/uptime)"
	for d in $devs; do
		line="$line | $(cat "$d/name")"
		last=""
		for c in $(chans "$d"); do
			type="${c%_*}"
			[ "$type" = "$c" ] && type="$c"
			# one unit per sensor type, after its last axis
			[ -n "$last" ] && [ "$last" != "$type" ] && line="$line $(unit_of "$last")"
			line="$line ${c#${type}_}=$(value_of "$d" "$c")"
			last="$type"
		done
		[ -n "$last" ] && line="$line $(unit_of "$last")"
	done
	for b in $bats; do
		[ -e "$b/voltage_now" ] || continue
		v=$(cat "$b/voltage_now" 2>/dev/null)
		t=$(cat "$b/temp" 2>/dev/null)
		line="$line | ${b##*/} $(awk -v v="$v" 'BEGIN { printf "%.3f", v / 1000000 }')V"
		[ -n "$t" ] && line="$line $(awk -v t="$t" 'BEGIN { printf "%.1f", t / 10 }')C"
		[ -e "$b/status" ] && line="$line $(cat "$b/status")"
	done
	echo "$line"
	n=$((n + 1))
	usleep $((MS * 1000))
done
