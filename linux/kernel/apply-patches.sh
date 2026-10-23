#!/usr/bin/env bash
# applies linux/kernel/patches/*.patch (in order) to the kernel submodule.
# the submodule stays at the upstream commit, the patches only change its
# working tree. a patch that is already in is skipped, so the build scripts
# can run this every time. "reset" puts the tree back to the upstream commit.
#
# usage: apply-patches.sh [reset]
#
# new patches: commit in the submodule, then
#   git -C linux/kernel/linux format-patch --zero-commit --no-signature \
#       -o ../patches <upstream>..HEAD
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
KSRC="$HERE/linux"
PATCHES="$HERE/patches"

[ -f "$KSRC/Makefile" ] || { echo "no kernel sources: git submodule update --init --depth 1 linux/kernel/linux"; exit 1; }

if [ "${1:-}" = reset ]; then
	git -C "$KSRC" checkout -- .
	echo "kernel tree reset to $(git -C "$KSRC" rev-parse --short HEAD)"
	exit 0
fi

for p in "$PATCHES"/*.patch; do
	[ -e "$p" ] || continue
	name="$(basename "$p")"
	if git -C "$KSRC" apply --check -R "$p" 2>/dev/null; then
		echo "  in already: $name"
	elif git -C "$KSRC" apply --check "$p" 2>/dev/null; then
		git -C "$KSRC" apply "$p"
		echo "  applied:    $name"
	else
		echo "patch doesn't apply: $name (kernel tree changed by hand? $0 reset)"
		exit 1
	fi
done
