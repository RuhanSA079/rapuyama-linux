"""
runs the Symbian panel driver + display chipset + dispcommanddrv + vcdispman
from kern.bin and logs every message they would send to the VideoCore:
DSPC (dispcommanddrv -> vchi msg queue) and DISP (vcdispman sends).
VC replies are faked: every DSPC request with a token gets {token, 0},
DCS reads get the E6 panel's answers.
"""
import sys, struct
sys.path.insert(0, __import__('os').path.dirname(__file__))
from emu import *

PANEL_ID = {0xda: b'\x01', 0xdb: b'\x8b', 0xdc: b'\x79', 0x04: b'\x01\x8b\x79', 0x0a: bytes([int(__import__('os').environ.get('PANEL_0A', '1c'), 16)]),
            0x0b: b'\x00', 0x0c: b'\x71'}

e = Emu(quiet=False, stub_ranges=[(0x80000000, 0x8033ffff), (0x80362000, 0x804fffff)])
u = e.u
e.allow += [(0x8005fc50, 0x800602a0), (0x800ae2d4, 0x800aee00)]   # run RArray code natively
VTSTUB = 0x80000100          # any address in a stub range: returns 0
dfcs, q, timers = {}, [], {}
replies = [] # (bytes) queued for the dispcommanddrv rx DFC
seq = [] # the captured message log
drv_obj = [0]


def rd(a, n): return bytes(u.mem_read(a, n))


def flat_iov(iov, n):
    out = b''
    for i in range(n):
        p, l = e.rd32(iov + 8 * i), e.rd32(iov + 8 * i + 4)
        if l & 0x80000000:
            out += flat_iov(p, (-l) & 0xffffffff)
        elif l:
            out += rd(p, l)
    return out


def fake_service(em, a, sp):
    # vchi service open (handle params, owner drv, 0x24): owner+0xc = service
    svc = em.alloc(0x40); vt = em.alloc(0x40)
    for k in range(0, 0x40, 4): em.wr32(vt + k, VTSTUB)
    em.wr32(svc, vt)
    em.wr32(a[1] + 0xc, svc)
    w = em.rd32(a[1])
    name = w.to_bytes(4, 'little').decode('latin1')
    lr = em.u.reg_read(UC_ARM_REG_LR)
    if 0x80340000 <= lr < 0x80342000:          # dispcommanddrv: params = drv+0x24
        drv_obj[0] = a[1]
        name = 'DSPC(drv)'
    seq.append('vchi open service %r from %08x params %s' % (name, lr, rd(a[1], 0x24).hex(' ', 4)))
    return 0


def dspc_send(em, a, sp):
    # FUN_80333454(service, iov, n, flags, ...)
    b = flat_iov(a[1], a[2])
    words = [struct.unpack_from('<I', b, i)[0] for i in range(0, len(b) - 3, 4)]
    disp, idn = struct.unpack_from('<HH', b, 0)
    cmd, tok = struct.unpack_from('<II', b, 4)
    seq.append('DSPC %s' % b.hex(' ', 4))
    cmd &= 0xffff
    if cmd == 0x40:
        cnt = struct.unpack_from('<I', b, 12)[0]
        off = 16
        for _ in range(cnt):
            typ, t2, dcs, pad, ln = struct.unpack_from('<5I', b, off)
            off += 20
            data = b[off:off + pad]; off += pad
            if typ == 0:
                ans = PANEL_ID.get(dcs, b'\0' * ln)[:ln]
                replies.append(struct.pack('<III', t2, 0, ln) + ans)
        if tok:
            replies.append(struct.pack('<II', tok, 0))
    elif tok:
        replies.append(struct.pack('<II', tok, 0))
    return 0


rxcur = [None]


def peek(em, a, sp):
    # FUN_803350c0(service, &ptr, &len, 0)
    if not replies:
        em.wr32(a[2], 0); return -1 & 0xffffffff
    b = replies[0]
    p = em.alloc(len(b) + 16); u.mem_write(p, b)
    em.wr32(a[1], p); em.wr32(a[2], len(b))
    return 0


def remove(em, a, sp):
    if replies: replies.pop(0)
    return 0


disp_handles = {0x08: 0x10000000, 0x10: 0x4000010f, 0x03: 0x60000080, 0x13: 0x20000000, 0x04: 0x60000000}


def disp_send(sync):
    def f(em, a, sp):
        cmd, buf, ln = a[1], a[2], a[3]
        words = [e.rd32(buf + i) for i in range(0, ln, 4)] if buf else []
        seq.append('DISP %s cmd %02x %s' % ('sync ' if sync else 'async', cmd, ' '.join('%x' % w for w in words)))
        return disp_handles.get(cmd, 0) if sync else 0
    return f


def dfc_ctor(em, a, sp):
    dfcs[a[0]] = (a[1], a[2]); return a[0]


def enque(em, a, sp):
    q.append(('dfc', a[0])); return 1


def timer_ctor(em, a, sp):
    timers[a[0]] = (a[1], a[2]); return a[0]


def oneshot(em, a, sp):
    q.append(('timer', a[0])); seq.append('  (timer %d ticks)' % a[1]); return 0


def realloc(em, a, sp):
    n = em.alloc(max(a[1], 4))
    if a[0] and a[1]:
        u.mem_write(n, rd(a[0], a[1]))
    return n


phys_next = [0x18000000]
chunks = {}


def alloc_phys(em, a, sp):
    em.wr32(a[1], phys_next[0]); seq.append('  (host phys RAM %08x +%x)' % (phys_next[0], a[0]))
    phys_next[0] += (a[0] + 0xfffff) & ~0xfffff
    return 0


def chunk_new(em, a, sp):
    o = em.alloc(0x40); lin = em.alloc(a[2])
    vt = em.alloc(0x40)
    for k in range(0, 0x40, 4): em.wr32(vt + k, VTSTUB)
    em.wr32(o, vt); em.wr32(o + 0x24, lin); em.wr32(o + 0x28, a[1])
    em.wr32(a[0], o); chunks[lin] = a[1]
    return 0


tick_seen = {}


def tick_dfc(a):
    # periodic owner tick: let it run twice, then drop it
    tick_seen[a[2]] = tick_seen.get(a[2], 0) + 1
    if tick_seen[a[2]] <= 2:
        q.append(('dfc', a[2]))
    return 0


def trace(addr, fmt):
    u.hook_add(UC_HOOK_CODE, lambda uu, ad, sz, d: seq.append(fmt(e)), begin=addr, end=addr)


trace(0x80353784, lambda em: '>> request %x for display %d' % (em.rd32(em.r(1)) & 0xffff, em.r(2)))
trace(0x8033d028, lambda em: '>> BUFMAN write image buf=%x desc=%s res=%x' % (em.r(0), rd(em.r(1), 0x1c).hex(' ', 4), em.r(2)))
trace(0x8034d0f4, lambda em: '>> chipset compose')
trace(0x8034dc80, lambda em: '>> chipset set layers')
trace(0x8034d6f8, lambda em: '>> chipset update params')
trace(0x8036018c, lambda em: '>> panel refresh %x' % em.r(1))
trace(0x8035be64, lambda em: '>> refresh case %d' % (em.rd32(em.r(0) + 0x2c4) + 1))
trace(0x8035b584, lambda em: '>> init case %d' % (em.rd32(em.r(0) + 0x2c4) + 1))
trace(0x8035c610, lambda em: '>> poweron case %d' % (em.rd32(em.r(0) + 0x2c4) + 1))
trace(0x8034d944, lambda em: '>> chipset power mode %x' % em.r(1))
trace(0x8034cae8, lambda em: '>> chipset flush cb=%x' % em.r(1))
trace(0x803500b8, lambda em: '>> op %x step %d' % (em.rd32(em.r(0) + 0x2c0), em.rd32(em.r(0) + 0x2c4)))


st = {
    0x800821a8: lambda em, a, sp: (a[0] + 0xfff) & ~0xfff,
    0x80082370: alloc_phys, 0x80082204: chunk_new,
    0x8009eafc: realloc,
    0x80336d14: fake_service, 0x80333454: dspc_send, 0x803350c0: peek, 0x8033508c: remove,
    0x80336f28: lambda em, a, sp: 0x800,       # max message size
    0x80062fcc: dfc_ctor, 0x80063068: dfc_ctor, 0x80062fa8: dfc_ctor,
    0x80062ee0: enque, 0x8005b260: enque,
    0x80063640: timer_ctor, 0x8005bbcc: oneshot, 0x8005bc18: oneshot,
    0x8005bdd8: lambda em, a, sp: tick_dfc(a),
    0x8009e780: lambda em, a, sp: 0,          # SemaphoreWait
    0x80064028: lambda em, a, sp: a[0],       # TimerTicks
}
for k, v in st.items(): e.stubs[k] = v
for adr, sync in ((0x80345168, 1), (0x803450d8, 0)):
    e.hook_stub(adr, disp_send(sync))


def load_rom_data():
    # TRomImageHeader: +0x18 data in ROM, +0x24 data size, +0x28 bss size, +0x60 data/bss base
    n = 0
    for off in range(0, len(ROM) - 0x64, 4):
        uid1, = struct.unpack_from('<I', ROM, off)
        if uid1 not in (0x10000079, 0x1000007a): continue
        ep, code, dat, csz, tsz, dsz, bsz = struct.unpack_from('<7I', ROM, off + 0x10)
        base, = struct.unpack_from('<I', ROM, off + 0x60)
        if not (0x80000000 <= code < 0x80500000 and 0xc8000000 <= base < 0xc9000000 and dsz + bsz < 0x100000):
            continue
        if dsz and 0x80000000 <= dat < 0x80500000:
            u.mem_write(base, ROM[dat - 0x80000000: dat - 0x80000000 + dsz])
        u.mem_write(base + dsz, b'\0' * bsz)
        n += 1
    print('loaded data of %d modules' % n)


CTOR_LISTS = [(0x80341ea4, 0x80341ea8), (0x80345590, 0x80345594), (0x8034ec54, 0x8034ec58),
              (0x80361588, 0x803615cc), (0x80349a60, 0x80349a6c)]


def run_ctors():
    for lo, hi in CTOR_LISTS:
        for a in range(lo, hi, 4):
            off = struct.unpack_from('<I', ROM, a - 0x80000000)[0]
            fn = (a + off) & 0xffffffff
            e.call(fn)


def pump(n=400):
    for _ in range(n):
        if replies and drv_obj[0]:
            q.append(('dfc', drv_obj[0] + 0x50))
        if not q:
            return
        kind, d = q.pop(0)
        if kind == 'dfc':
            fn, arg = dfcs.get(d, (None, None))
        else:
            fn, arg = timers.get(d, (None, None))
        if fn:
            if fn not in (0x8034066c,):
                seq.append('  run %s %08x fn %08x' % (kind, d, fn))
            e.call(fn, arg)
        else:
            seq.append('  ?? %s %08x has no handler' % (kind, d))


def show():
    for s in seq: print(s)
    seq.clear()


def dump_reg():
    for i in range(4):
        b = rd(0xc8012260 + i * 0x1c, 0x1c)
        print(i, b[:10], b[10:20], struct.unpack_from('<II', b, 0x14))


if __name__ == '__main__':
    load_rom_data()
    run_ctors()
    e.wr32(0xc8012184, 1)                     # dispcommanddrv "created" flag
    e.call(0x8034c21c)                        # display interface registry
    e.wr32(0xc80124ec, 0x018b79)              # panel id of display 0 (DCS da/db/dc)
    e.wr32(0xc8012824, 0); e.wr32(0xc8012828, 9)   # display 1: type 9
    print('### display chipset init'); e.call(0x8034e584); pump(); show(); dump_reg()
    owner = e.alloc(0x2000)
    print('### lcd owner ctor'); e.call(0x8034f2d0, owner); pump(); show()
    for ra in (0x8034f218, 0x8034f234, 0x8034f244, 0x8034f254, 0x8034f264, 0x8034f274, 0x8034f284, 0x8034f2b0):
        u.hook_add(UC_HOOK_CODE, lambda uu, ad, sz, d: print('   ret @%08x r0=%x' % (ad, e.r(0))), begin=ra, end=ra)
    print('### lcd owner install'); r = e.call(0x8034f1c8, owner); pump(); show()
    print('install ->', hex(r))
    print('### request 3 (refresh) for display 0')
    e.call(0x80353b00, owner, 3, 0, 0, 0, 0); pump(); show()
    p0 = e.rd32(owner + 0x4d0)
    print('panel0 %08x desc:' % p0, ' '.join('%x' % e.rd32(p0 + 0x188 + 4 * i) for i in range(0x3d)))
    pn = p0 + 0x498
    print('panel cfg (+0x24):', rd(pn + 0x24, 0x7c).hex(' ', 4))
