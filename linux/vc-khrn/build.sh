#!/usr/bin/env bash
# userspace for the VC khronos channel, built with the pc's clang against
# the phone's musl (linux/initramfs/sysroot.sh):
#   libvcgl.so  EGL + GLES 2 on the VC (libEGL.so.1 / libGLESv2.so.2 are
#               links to it, musl loads one file once)
#   khrntest    the raw rpc test (magenta square), vctri the GLES 2 demo
# MOCK=1: the pc version, every message is checked against the vc-main
# handlers instead of going to /dev/vc-khrn. run with
#   work/dl/qemu/x/usr/bin/qemu-arm -L work/sysroot-alpine -E LD_LIBRARY_PATH=<out> <out>/vctri 1
# usage: build.sh <out dir>
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOP="$HERE/../.."
OUT="${1:?usage: build.sh <out dir>}"
SYSROOT="$TOP/work/sysroot-alpine"

"$TOP/linux/initramfs/sysroot.sh" "$SYSROOT"
CC=(clang --target=armv6-alpine-linux-musleabihf -march=armv6kz -mfloat-abi=hard
	-mfpu=vfp --sysroot="$SYSROOT" --ld-path=/usr/bin/arm-linux-gnu-ld
	-static-libgcc -O2 -Wall -Wextra -Werror)
[ -n "${MOCK:-}" ] && CC+=(-DKHRN_MOCK)
mkdir -p "$OUT"
"${CC[@]}" -o "$OUT/khrntest" "$HERE/khrntest.c" "$HERE/khrn.c"
"${CC[@]}" -fPIC -shared -Wl,-soname,libvcgl.so -o "$OUT/libvcgl.so" \
	"$HERE/khrn.c" "$HERE/egl.c" "$HERE/gles2.c"
ln -sf libvcgl.so "$OUT/libEGL.so.1"
ln -sf libvcgl.so "$OUT/libGLESv2.so.2"
"${CC[@]}" -o "$OUT/vctri" "$HERE/vctri.c" -L"$OUT" -lvcgl -lm