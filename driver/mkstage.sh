#!/usr/bin/env bash
# and this one rebuilds driver/stage. epoc32/include-like header tree for gcce builds
# stage/ isnt in git and it calls this when its missing
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
EKA="$ROOT/symbian/oss.FCL.sf.os.kernelhwsrv/kernel/eka"
INC="$EKA/include"
STAGE="$HERE/stage"

[ -d "$INC" ] || { echo "no $INC: git submodule update --init --depth 1"; exit 1; }
rm -rf "$STAGE"
mkdir -p "$STAGE"

(cd "$INC" && find . -type f \( -name '*.h' -o -name '*.inl' \)) | while read -r f; do
    f="${f#./}"
    mkdir -p "$STAGE/$(dirname "$f")"
    ln -s "$INC/$f" "$STAGE/$f"
done

for d in kernel memmodel nkern; do
    for f in "$INC"/*.h "$INC"/*.inl; do
        b="$(basename "$f")"
        [ -e "$STAGE/$d/$b" ] || ln -s "$f" "$STAGE/$d/$b"
    done
done

perl "$EKA/kernel/genexec.pl" -i "$EKA/kernel/execs.txt" -e "$STAGE/exec_enum.h" -u "$STAGE/exec_user.h" -k "$STAGE/kernel/exec_kernel.h"