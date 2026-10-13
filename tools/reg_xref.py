#!/usr/bin/env python3
"""
register xref over annotated decomp (work/ghidra/kern/ann/*.c)
prints phys reg -> module:function list, grouped by 4k page

usage: reg_xref.py <ann_dir> [phys_prefix]
"""
import re
import sys
from collections import defaultdict
from pathlib import Path


def main():
	d = Path(sys.argv[1])
	pre = sys.argv[2].lower() if len(sys.argv)>2 else ""
	refs = defaultdict(set)
	for f in sorted(d.glob("*.c")):
		mod = f.name[:-2]
		fn = "?"
		for l in f.read_text().splitlines():
			m = re.match(r"// ==== (\S+) @", l)
			if m:
				fn = m.group(1)
				continue
			for p in re.findall(r"P:([0-9a-f]{8})", l):
				if p.startswith(pre):
					refs[int(p, 16)].add(f"{mod}:{fn}")
	page = None
	for p in sorted(refs):
		if p&~0xfff!=page:
			page = p&~0xfff
			print(f"\n{page:08x}")
		print(f"  {p:08x}  {' '.join(sorted(refs[p]))}")


if __name__=="__main__":
	main()