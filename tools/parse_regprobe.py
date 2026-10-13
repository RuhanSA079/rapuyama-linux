#!/usr/bin/env python3
"""
parses E:\\regprobe.bin (regprobe.sis, crash resilient version):
records of 3 u32 {c6 virt addr, value, status}, status 600d600d = read ok,
bad0bad0 = the read killed the phone (that reg is NOT readable as is)
names the regs and decodes what the linux drivers care about
"""
import struct
import sys

NAMES = {
	0x200: "i2c REV", 0x204: "i2c IE", 0x208: "i2c STAT", 0x210: "i2c SYSS",
	0x220: "i2c SYSC", 0x224: "i2c CON", 0x228: "i2c OA", 0x22c: "i2c SA",
	0x230: "i2c PSC", 0x234: "i2c SCLL", 0x238: "i2c SCLH", 0x23c: "i2c SYSTEST",
}


def name(v):
	p = v-0xc6000000+0x20000000
	if 0x20032200<=p<0x20032240:
		return NAMES.get(p-0x20032000, "i2c ?")
	if 0x20031990<=p<0x200319f0 or p==0x20031ab0:
		k = 6 if p==0x20031ab0 else (p-0x20031990)//0x10
		return f"gpio out latch bank {k}"
	if 0x200319f0<=p<0x20031a40:
		return f"gpio in level bank {(p-0x200319f0)//0x10}"
	if p in (0x20033058, 0x20033084, 0x200330d4, 0x200330b4):
		return "clock gate set reg"
	if 0x20033400<=p<0x20033540:
		k, r = divmod(p-0x20033400, 0x40)
		return f"gpio irq bank {k} " + {4: "bank enable", 8: "raw state", 0x18: "enabled mask"}.get(r, hex(r))
	if 0x20025a00<=p<0x20025b00:
		return "bt uart +" + hex(p-0x20025a00)
	return {0x20033010: "clk div sel (i2c bits 12-14)", 0x20033130: "clk src (bit 14 = pll)",
		0x20032644: "pll"}.get(p, "")


DIVS = {0: 1, 1: 2, 2: 3, 3: 4, 4: 8, 5: 12, 6: 16} # i2c0.dll 0x801cd234


def main():
	path = sys.argv[1] if len(sys.argv)>1 else "logs/regprobe.bin"
	d = open(path, "rb").read()
	recs = [struct.unpack_from("<3I", d, i*12) for i in range(len(d)//12)]
	r = {}
	for x, y, st in recs:
		p = x-0xc6000000+0x20000000
		if st==0x600d600d:
			r[x] = y
			print(f"  {p:08x} = {y:08x}  {name(x)}")
		elif st==0x3017e000:
			print(f"  {p:08x} <- {y:08x}  WRITE  {name(x)}")
		elif st==0x5c1bbed0:
			print(f"  {p:08x}   skipped (its block crashed)  {name(x)}")
		elif st==0xbad0bad0:
			print(f"  {p:08x} = CRASHED THE PHONE  {name(x)}")
		else:
			print(f"  {p:08x} = error {st:#x}  {name(x)}")
	print(f"\n{len(recs)} records")
	rev = r.get(0xc6032200)
	if rev is not None and 0xc6032224 in r and 0xc6032234 in r and 0xc6032238 in r:
		print(f"\ni2c REV = {rev&0xffff:#06x} (i2c-omap: 16 bit rev, omap2420 is 0x0020ish)")
		print(f"i2c CON = {r[0xc6032224]&0xffff:#06x} (bit 15 = enabled)  SCLL/SCLH = {r[0xc6032234]&0xffff}/{r[0xc6032238]&0xffff}")
	c = r.get(0xc6033130)
	if c is not None and 0xc6033010 in r and 0xc6032644 in r:
		div = DIVS.get((r[0xc6033010]>>12)&7, 1)
		pll = r[0xc6032644]
		f = 38400*((pll&0xffff)>>5)//((pll&0x1f)+1)
		print(f"i2c fclk: {f'pll {f} khz / {div} = {f//div} khz' if c&0x4000 else '19200 khz (no pll)'}")
	q = r.get(0xc6025a8c)
	if q and 0xc6025a04 in r and 0xc6025a84 in r and 0xc6025a14 in r:
		baud = 38400000//q
		print(f"\nbt uart: div q={q} -> ~{baud} baud, active={r[0xc6025a04]} enable={r[0xc6025a84]} bits={r[0xc6025a14]}")
		# same math as rapuyama_earlycon.c, for the standard baud closest to q
		best = min((115200, 230400, 460800, 576000, 921600, 1152000, 3000000, 4000000, 9600, 19200, 38400, 57600),
			key=lambda b: abs(38400000//b-q))
		m = 0xffffffff
		rr, r2 = 38400000%best, 38400000%(best*2)
		a = (38400000//best-3)>>1
		want = {0x08: a, 0x5c: a//5, 0x0c: a+1, 0x10: a, 0x88: q-1, 0x8c: q,
			0x60: (r2*(0x80000000//best)-0x80000000)&m, 0xb8: (rr*(0x80000000//(best>>1)))&m}
		ok = True
		for off, w in want.items():
			got = r.get(0xc6025a00+off)
			flag = "ok" if got==w else "DIFF"
			ok &= got==w
			print(f"  +{off:#04x} got {got:08x} want {w:08x} {flag}")
		print(f"-> formula for {best} baud {'MATCHES' if ok else 'does NOT match'} the firmware")
	lvl = r.get(0xc60319f0+0x10)
	if lvl is not None:
		print(f"gpio 42 (keypad irq) level = {(lvl>>10)&1}")


if __name__=="__main__":
	main()