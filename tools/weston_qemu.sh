#!/usr/bin/env bash
# run the phone's weston session on the pc, no phone needed: the alpine
# rootfs under qemu-arm (user mode), headless backend + pixman at 640x480,
# our weston.ini/xkb-e6/e6-term, then a screenshot. checks everything except
# the kernel side (drm driver, keypad)
# qemu-user can't follow weston's fork+exec into arm binaries, so the
# clients weston spawns go through host wrapper scripts (test ini only).
# musl looks in /lib first and qemu -L falls back to host files, so the
# library path is forced to the rootfs dirs
# usage: weston_qemu.sh [out.png] [seconds] [weston args...]
# (needs work/rootfs-alpine,
# i.e. one build-vc.sh run, and work/dl/qemu/x/usr/bin/qemu-arm from the
# debian qemu-user .deb)
set -euo pipefail

TOP="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$TOP/work/weston-qemu.png}"
WAIT="${2:-60}"
shift 2 2>/dev/null || shift $#
Q="$TOP/work/dl/qemu/x/usr/bin/qemu-arm"
APK="$TOP/work/dl/alpine/apkt/sbin/apk.static"
R="$TOP/work/rootfs-qtest"
# short on purpose: wayland socket paths must fit in 108 bytes
RT="$TOP/work/xdg"
W="$TOP/work/qwrap"
LIBS=/usr/lib:/lib:/usr/lib/weston

[ -x "$Q" ] || { echo "no $Q (extract usr/bin/qemu-arm from debian qemu-user)"; exit 1; }
rm -rf "$R" "$RT" "$W"
cp -a "$TOP/work/rootfs-alpine" "$R"
"$APK" --root "$R" --arch armhf --usermode --no-scripts --allow-untrusted \
	--cache-dir "$TOP/work/dl/alpine/cache" --timeout 60 \
	add weston-backend-headless weston-clients > /dev/null
mkdir -p "$R/etc/xdg/weston" "$R/var/cache/fontconfig" "$RT" "$W"
chmod 700 "$RT"
cp "$TOP/linux/initramfs/xkb-e6" "$R/usr/share/X11/xkb/symbols/e6"
cp "$TOP/linux/initramfs/e6-term.sh" "$R/usr/bin/e6-term"
cp "$TOP/linux/initramfs/fonts-local.conf" "$R/etc/fonts/local.conf"

wrap() {
	printf '#!/bin/sh\nexec %s -L %s -E LD_LIBRARY_PATH=%s %s/%s %s"$@"\n' \
		"$Q" "$R" "$LIBS" "$R" "$1" "${3:-}" > "$W/$2"
	chmod +x "$W/$2"
}
wrap usr/libexec/weston-desktop-shell desktop-shell
wrap usr/libexec/weston-keyboard keyboard
# what /usr/bin/e6-term does on the phone
# E6_TERM_ARGS: extra weston-terminal args for a test, e.g. --shell=<host script>
wrap usr/bin/weston-terminal term "--maximized ${E6_TERM_ARGS:-} "
sed -e "s|^\[shell\]$|[shell]\nclient=$W/desktop-shell|" \
	-e "s|^path=/usr/bin/e6-term$|path=$W/term|" \
	"$TOP/linux/initramfs/weston.ini" > "$R/etc/xdg/weston/weston.ini"
printf '\n\n[input-method]\npath=%s/keyboard\n' "$W" >> "$R/etc/xdg/weston/weston.ini"

timeout $((WAIT + 60)) "$Q" -L "$R" -E LD_LIBRARY_PATH=$LIBS \
	-E XDG_RUNTIME_DIR="$RT" -E SHELL=/bin/sh -E HOME=/tmp \
	-U WAYLAND_DISPLAY -U DISPLAY "$R/usr/bin/weston" --backend=headless \
	--renderer=pixman --width=640 --height=480 --debug "$@" \
	> "$TOP/work/weston-qemu.log" 2>&1 &
sleep "$WAIT"
(cd "$RT" && "$Q" -L "$R" -E LD_LIBRARY_PATH=$LIBS -E XDG_RUNTIME_DIR="$RT" \
	-E WAYLAND_DISPLAY=wayland-1 "$R/usr/bin/weston-screenshooter" > /dev/null)
mv "$RT"/wayland-screenshot-*.png "$OUT"
echo "screenshot: $OUT, weston log: $TOP/work/weston-qemu.log"