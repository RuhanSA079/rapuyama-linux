#!/usr/bin/env bash
# applies linux/kernel/patches/*.patch (in order) to the kernel submodule.
# the submodule stays at the upstream commit, the patches only change its
# working tree. once applied it does nothing, so the build scripts can run
# this every time. "reset" puts the tree back to the upstream commit.
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
	rm -f "$(git -C "$KSRC" rev-parse --absolute-git-dir)/n8-patches.stamp"
	echo "kernel tree reset to $(git -C "$KSRC" rev-parse --short HEAD)"
	exit 0
fi

# the patches stack (several touch the same file), so check the series as a
# whole: a stamp in the submodule's git dir holds the hash of the patch set and
# of the tree diff they produced. matching stamp = nothing to do (no file is
# touched, so nothing rebuilds); clean tree = apply all; anything else = stop
STAMP="$(git -C "$KSRC" rev-parse --absolute-git-dir)/n8-patches.stamp"
want="$(cat "$PATCHES"/*.patch | sha256sum | cut -d' ' -f1)"
tree() { git -C "$KSRC" diff | sha256sum | cut -d' ' -f1; }

if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$want $(tree)" ]; then
	echo "kernel patches: all in already"
	exit 0
fi
if ! git -C "$KSRC" diff --quiet; then
	echo "kernel tree has changes that aren't this patch set (edited by hand,"
	echo "or older patches): commit them as patches, or $0 reset"
	exit 1
fi
for p in "$PATCHES"/*.patch; do
	[ -e "$p" ] || continue
	git -C "$KSRC" apply "$p"
	echo "  applied: $(basename "$p")"
done
echo "$want $(tree)" > "$STAMP"
