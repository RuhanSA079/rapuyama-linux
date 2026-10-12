#!/usr/bin/env bash
# submodules and building host tools
# you can download the rest by hand: tools/gcce/README.md, firmware/README.md
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

echo ">> submodules"
git submodule update --init --depth 1

echo ">> elf2e32_next -> tools/bin"
E2E="$ROOT/tools/src/elf2e32_next"
mkdir -p tools/bin tools/bin/.obj
gcc -O2 -c "$E2E/lib/elf/elf_hash.c" -o tools/bin/.obj/elf_hash.o
g++ -O2 -fpermissive -w -include cstdint -I"$E2E/include" -I"$E2E/lib/e32" -I"$E2E/lib/elf" -I"$E2E/lib/getopt" -I"$E2E/src" "$E2E"/src/*.cpp "$E2E"/lib/*/*.cpp tools/bin/.obj/elf_hash.o -o tools/bin/elf2e32_next
rm -rf tools/bin/.obj

echo ">> gcce"
if [ -x tools/gcce/arm-2012.03/bin/arm-none-symbianelf-gcc ]; then
    echo "ok"
elif [ -f tools/gcce/gcce-linux.tar.bz2 ]; then
    tar -xjf tools/gcce/gcce-linux.tar.bz2 -C tools/gcce
else
    echo "NO GCCE!!!! tools/gcce/README.md"
fi

echo ">> done"