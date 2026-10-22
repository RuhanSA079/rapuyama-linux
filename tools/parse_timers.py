#!/usr/bin/env python3
"""read E:\\timers.bin from e6tmrs; --all includes timers outside the service window"""
import argparse
import struct
from pathlib import Path


def signed(n):
	return (n+0x80000000)%0x100000000-0x80000000


def read_capture(data):
	if len(data)<32:
		raise ValueError("short header")
	h=struct.unpack_from("<8I", data)
	magic, version, target, stride, limit, count, period, _=h
	if magic!=0x31524d54 or version!=1:
		raise ValueError("not a TMR1 v1 capture")
	if not 0<limit<=512 or stride!=16+limit*32 or not 0<count<=36 or period!=1000:
		raise ValueError("invalid frame dimensions or tick period")
	if len(data)!=32+count*stride*4:
		raise ValueError("truncated capture or unexpected trailing data")
	frames=[]
	for i in range(count):
		o=32+i*stride*4
		f=struct.unpack_from("<16I", data, o)
		if f[3]>limit:
			raise ValueError("row count exceeds frame capacity")
		rows=[]
		delta=0
		for j in range(f[3]):
			r=struct.unpack_from("<17I28s32s", data, o+64+j*128)
			kind, addr=r[:2]
			w=r[2:16]
			arg, fn=w[4:6]
			if kind==1:
				due=signed(w[8]-target)
			elif kind==2:
				delta+=signed(w[2])
				if not f[11]:
					raise ValueError("zero nanokernel tick period")
				due=signed(f[8]-target)+(delta*f[10]-signed(f[9]))/f[11]
			elif kind==3:
				ticks=(w[2]|w[3]<<32)-(f[12]|f[13]<<32)
				due=signed(f[8]-target)+(ticks*f[10]-signed(f[9]))/f[11]
			else:
				raise ValueError("unknown timer queue")
			name="::".join(s.split(b"\0", 1)[0].decode("latin1") for s in (r[18], r[17])).strip(":")
			owner=r[16]
			if kind==1 and fn==0:
				fn=owner
				arg=struct.unpack_from("<I", r[17])[0]
				owner=0
				name="queued DFC"
			rows.append(dict(kind=kind, addr=addr, fn=fn, arg=arg, owner=owner, name=name, due=due))
		frames.append((f, rows))
	return target, frames


def main():
	p=argparse.ArgumentParser(description=__doc__)
	p.add_argument("path", type=Path)
	p.add_argument("--all", action="store_true")
	a=p.parse_args()
	try:
		target, frames=read_capture(a.path.read_bytes())
	except (OSError, ValueError, struct.error) as e:
		p.exit(1, f"{e}\n")
	print(f"expected service at symbian uptime {target/1000:.3f} s; {len(frames)} snapshots")
	by_timer={}
	for f, rows in frames:
		ms=((f[2]-f[1])&0xffffffff)*1000/32768
		print(f"  {signed(f[0]-target):+6d} ms: {len(rows):3d} timers, scan {ms:.3f} ms, flags {f[4]:02x}")
		for r in rows:
			key=(r['kind'], r['addr'], r['fn'], r['arg'])
			by_timer.setdefault(key, []).append((signed(f[0]-target), r))
	print("\ntimers due within 1 s of the expected service (candidate evidence, not proof):")
	for key, seen in sorted(by_timer.items()):
		dues=[r['due'] for _, r in seen]
		if not a.all and not any(abs(d)<=1000 for d in dues):
			continue
		r=seen[0][1]
		print(f"  q{key[0]} {key[1]:08x} fn={key[2]:08x} arg={key[3]:08x} owner={r['owner']:08x} {r['name']}")
		last=None
		for sample, r in seen:
			# rounding of tick queues can move the estimate by 1 ms
			if last is None or abs(r['due']-last)>2:
				print(f"    sampled {sample:+d} ms, deadline {r['due']:+.2f} ms from service")
				last=r['due']
		print(f"    seen {seen[0][0]:+d} .. {seen[-1][0]:+d} ms")
	if any(f[4] for f, _ in frames):
		print("\npartial snapshots: flags 1=unreadable, 2=capacity, 4=broken links, 8=5 ms budget")


if __name__=="__main__":
	main()
