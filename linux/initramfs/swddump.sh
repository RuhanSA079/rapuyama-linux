#!/bin/busybox sh
# swddump: prints the memory the secure watchdog bridge (PA_SWD, e6.swd=1)
# depends on as base64 blocks, for porting the bridge to another firmware.
# run it with picocom logging on the pc, then
#   python3 tools/parse_physdump.py <picocom log> <out dir>
# needs a kernel without CONFIG_STRICT_DEVMEM (the N8 kernels)
#   isah   0x1f0f1000  handoff header + Symbian's L1 table copy
#   code   0x10480000  the ISA software image (E6: entry points at +0x61abe..)
#   guest  0x1f8b0000  its data / stacks (E6: token, server task stack)
dump() {
	echo "=== physdump $1 $2 $3"
	physdump "$2" "$3" | base64 || { echo "=== failed $1"; return; }
	echo "=== sha256 $1 $(physdump "$2" "$3" | sha256sum | cut -d' ' -f1)"
}
echo "=== swddump start $(uname -r)"
dump isah 0x1f0f1000 0x5000
dump code 0x10480000 0x100000
dump guest 0x1f8b0000 0x30000
echo "=== swddump end"
