#!/usr/bin/env python3
"""
annotates ghidra decomp output: every DAT_8xxxxxxx (rom literal) gets its value
inlined, and peripheral window values get the phys address next to them
  DAT_800b8d30 -> DAT_800b8d30/*c6060004=P:20400004*/
calls into ekern get the export name from ekern_exports.txt (next to kern.bin)
  thunk_FUN_800644b0 -> thunk_FUN_800644b0/*Arm::SetIrqHandler*/

usage: annotate_decomp.py <kern.bin> <in.c> <out.c>
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from kmod_scan import v2p


def main():
	rom = Path(sys.argv[1]).read_bytes()
	src = Path(sys.argv[2]).read_text()

	def val(m):
		a = int(m.group(1), 16)
		o = a-0x80000000
		if o+4>len(rom):
			return m.group(0)
		w = struct.unpack_from("<I", rom, o)[0]
		p = v2p(w)
		ps = f"=P:{p:08x}" if p is not None else ""
		return f"{m.group(0)}/*{w:08x}{ps}*/"

	src = re.sub(r"DAT_(8[0-9a-f]{7})", val, src)
	exp = {}
	ef = Path(sys.argv[1]).parent/"ekern_exports.txt"
	if ef.exists():
		for l in ef.read_text().splitlines():
			a, _, name = l.split(" ", 2)
			exp.setdefault(int(a, 16), name.split("(")[0])
	src = re.sub(r"FUN_(8[0-9a-f]{7})\b", lambda m: m.group(0)+(f"/*{exp[int(m.group(1), 16)]}*/" if int(m.group(1), 16) in exp else ""), src)
	# bare c6 constants the decompiler did inline
	src = re.sub(r"0x(c6[0-9a-f]{6})\b", lambda m: m.group(0)+(f"/*P:{v2p(int(m.group(1), 16)):08x}*/" if v2p(int(m.group(1), 16)) is not None else ""), src)
	Path(sys.argv[3]).write_text(src)


if __name__=="__main__":
	main()