#!/bin/busybox sh
# swdvadump: read-only dump of the secure watchdog (PA_SWD) state the E6 bridge
# borrows, by Symbian virtual address (work/swd-n8/NOTES.md section 6 step 1).
# vadump walks Symbian's page tables as saved at the handoff (L1 copy in the
# isah block); nothing is called, only memory is read.
#
#   swdvadump          base64 blocks on the terminal, for picocom logging, then
#                      python3 tools/parse_physdump.py <log> <out dir>
#   swdvadump sd       the same as files on the microSD card (swd/ on its first
#                      partition): mounts it, writes, syncs, unmounts. faster,
#                      done before the watchdog. only ever the SD card: refuses
#                      unless mmcblk0 reports type SD (the eMMC, E:, is MMC)
#   swdvadump <dir>    .bin files + map.txt + sha256.txt into <dir>
#
# regions, most important first:
#   va_guest  0xc6800000 0x280000  isasw data: token c6818880, heap head c68189c4,
#                                  PA handle c680b244, task stacks
#   va_pool   0xc6a80000 0x80000   the secure buffer pool c6af202c..c6af8000
#   va_code   0xc6300000 0x100000  the isa code (cross-check with code.bin)
L1=0x1f0f2000	# isah phys 0x1f0f1000 + 0x1000: Symbian's kernel L1 at the jump
[ "$(physdump 0x1f0f1000 4 | od -An -tx4 | tr -d ' ')" = 48415349 ] ||
	{ echo "no isah handoff block (magic ISAH) at 0x1f0f1000"; exit 1; }

OUT="$1"
SD=
if [ "$OUT" = sd ]; then
	[ "$(cat /sys/block/mmcblk0/device/type 2>/dev/null)" = SD ] ||
		{ echo "mmcblk0 is not the SD card (no card?), not writing anything"; exit 1; }
	mkdir -p /mnt/sd
	mount -t vfat /dev/mmcblk0p1 /mnt/sd ||
		{ echo "can't mount /dev/mmcblk0p1 (FAT) on /mnt/sd"; exit 1; }
	SD=1
	OUT=/mnt/sd/swd
fi

REGIONS="va_guest:0xc6800000:0x280000 va_pool:0xc6a80000:0x80000 va_code:0xc6300000:0x100000"

if [ -n "$OUT" ]; then
	mkdir -p "$OUT" || exit 1
	: > "$OUT/map.txt"
	: > "$OUT/sha256.txt"
fi

echo "=== swdvadump start $(uname -r)"
echo "=== l1 copy $L1"
for r in $REGIONS; do
	IFS=: read name va len <<EOF
$r
EOF
	if [ -n "$OUT" ]; then
		echo "== $name $va $len: map"
		echo "== $name $va $len" >> "$OUT/map.txt"
		vadump -m $L1 $va $len >> "$OUT/map.txt"
		echo "== $name: data"
		vadump $L1 $va $len > "$OUT/$name.bin"
		echo "$(sha256sum < "$OUT/$name.bin" | cut -d' ' -f1)  $name.bin" >> "$OUT/sha256.txt"
		sync
	else
		echo "=== map $name $va $len"
		vadump -m $L1 $va $len | sed 's/^/=== /'
		echo "=== physdump $name $va $len"
		vadump $L1 $va $len | base64
		echo "=== sha256 $name $(vadump $L1 $va $len | sha256sum | cut -d' ' -f1)"
	fi
done
[ -n "$OUT" ] && { cat "$OUT/sha256.txt"; grep -c " pa " "$OUT/map.txt" | sed 's/^/mapped pages: /'; }
if [ -n "$SD" ]; then
	sync
	umount /mnt/sd && echo "sd card: swd/ written, unmounted"
fi
echo "=== swdvadump end"
