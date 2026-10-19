#!/usr/bin/env bash
# alpine armhf headers + libs to build our own userspace (libEGL/libGLESv2
# for the VC, khrntest) with the pc's clang against the phone's musl. same
# static apk as alpine.sh, never goes into the initramfs. cached in
# work/sysroot-alpine, delete it to rebuild
# usage: sysroot.sh <out dir>
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOP="$HERE/../.."
OUT="${1:?usage: sysroot.sh <out dir>}"
DL="$TOP/work/dl/alpine"
APK="$DL/apkt/sbin/apk.static"
MIRROR=https://dl-cdn.alpinelinux.org/alpine
REL=v3.24
PKGS="musl-dev linux-headers libgcc gcc mesa-dev"

[ -e "$OUT.done" ] && exit 0
[ -x "$APK" ] || { echo "run alpine.sh first (it fetches apk.static)"; exit 1; }

rm -rf "$OUT" "$OUT.done"
mkdir -p "$OUT/etc/apk" "$DL/cache"
printf '%s/%s/main\n%s/%s/community\n' "$MIRROR" "$REL" "$MIRROR" "$REL" \
	> "$OUT/etc/apk/repositories"
ok=
for i in 1 2 3 4 5 6; do
	if "$APK" --root "$OUT" --arch armhf --initdb --usermode --no-scripts \
		--allow-untrusted --cache-dir "$DL/cache" --timeout 60 -U \
		add $PKGS > "$DL/apk-sysroot.log" 2>&1; then
		ok=1
		break
	fi
	sleep 3
done
[ -n "$ok" ] || { tail "$DL/apk-sysroot.log"; exit 1; }
touch "$OUT.done"
du -sh "$OUT"