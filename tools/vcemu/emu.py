import struct, sys
from unicorn import *
from unicorn.arm_const import *

import os
# kern.bin: argv[1], $KERN_BIN or ann/kern.bin; ekern_exports.txt sits next to it
KERN_BIN = sys.argv[1] if len(sys.argv)>1 and sys.argv[1].endswith('.bin') else os.environ.get('KERN_BIN', 'ann/kern.bin')
ROM = open(KERN_BIN,'rb').read()
NAMES = {}
for l in open(os.path.join(os.path.dirname(KERN_BIN), 'ekern_exports.txt')):
    a,_,n = l.rstrip('\n').split(' ',2); n = n.split('(')[0]
    if n.startswith('ord') and int(a,16) in NAMES: continue
    NAMES[int(a,16)] = n
EXTRA = {0x800c5f74:'PowerRes::ChangeState', 0x800c1784:'Gpio::Acquire', 0x800c17f8:'Gpio::Release',
 0x800c1758:'Gpio::c1758', 0x800c1b28:'GpioCfg::Init', 0x800c1b2c:'GpioCfg::SetHigh?', 0x800c1b70:'GpioCfg::SetLow?',
 0x800b89b0:'Interrupt::Bind', 0x800b8a60:'Interrupt::Enable', 0x800b8a98:'Interrupt::Disable', 0x800b8a18:'Interrupt::Unbind',
 0x800c5e50:'PowerRes::Register', 0x800c5e78:'PowerRes::Deregister', 0x800c19f0:'GpioIrq::Enable', 0x800c193c:'GpioIrq::Bind',
 0x800c1900:'GpioIrqWrapper::ctor', 0x800c1ad8:'GpioIrq::SetMode', 0x800b8828:'Clock::Freq?', 0x8022d198:'Rom::c198', 0x8022d238:'Rom::d238'}
NAMES.update(EXTRA)

HIDE = {'NKern::Lock','NKern::Unlock','NKern::ThreadEnterCS','NKern::ThreadLeaveCS','__e32_atomic_load_acq32','Kern::NanoWait','__e32_atomic_store_ord32','__e32_atomic_tas_acq32','__aeabi_uidivmod','NKern::DisableInterrupts','NKern::RestoreInterrupts'}
RET_THIS = {0x800c5d7c, 0x800c5d40, 0x800c5dd8, 0x800c3844, 0x80063624, 0x80062ff0, 0x80063248, 0x800639c8, 0x800c5e10, 0x800c1900}

def p2(v):
    if 0xc6000000 <= v < 0xc6100000: return v - 0xc6000000 + 0x20000000
    return v

class Emu:
    def __init__(self, mmio_read=None, stubs=None, quiet=False, stub_ranges=None):
        u = self.u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        u.mem_map(0x80000000, 0x500000); u.mem_write(0x80000000, ROM)
        u.mem_map(0xc6000000, 0x100000)
        u.mem_map(0xc8000000, 0x2000000)   # driver RAM / heap
        u.mem_map(0xd0000000, 0x100000)    # stack
        u.mem_map(0xe0000000, 0x1000000)   # other periph windows
        self.heap = 0xc9000000
        self.log = []; self.quiet = quiet
        self.mmio_read = mmio_read or (lambda a, n: 0)
        self.stubs = stubs or {}
        u.hook_add(UC_HOOK_MEM_READ, self._rd, begin=0xc6000000, end=0xc60fffff)
        u.hook_add(UC_HOOK_MEM_WRITE, self._wr, begin=0xc6000000, end=0xc60fffff)
        u.hook_add(UC_HOOK_MEM_READ, self._rd, begin=0xe0000000, end=0xe0ffffff)
        u.hook_add(UC_HOOK_MEM_WRITE, self._wr, begin=0xe0000000, end=0xe0ffffff)
        for lo, hi in (stub_ranges or [(0x80000000, 0x80327fff), (0x80350000, 0x804fffff)]):
            u.hook_add(UC_HOOK_CODE, self._code, begin=lo, end=hi)
        self.steps = 0
        self.allow = [(0x8022d118, 0x8022d5e0)]
    def out(self, s):
        key = s.split('(pc')[0]
        if self.log and self.log[-1][0] == key:
            self.log[-1][1] += 1; return
        self.flush()
        self.log.append([key, 1, s])
    def flush(self):
        if self.log and not self.log[-1][3:] and not self.quiet:
            k, n, s = self.log[-1]
            print(s + ('   x%d' % n if n > 1 else ''))
            self.log[-1].append(True)
    def _rd(self, u, acc, a, n, v, d):
        val = self.mmio_read(p2(a), n)
        u.mem_write(a, struct.pack('<I', val & 0xffffffff)[:n])
        self.out('  R%d %08x -> %08x   (pc %08x)' % (n*8, p2(a), val, u.reg_read(UC_ARM_REG_PC)))
    def _wr(self, u, acc, a, n, v, d):
        self.out('  W%d %08x <- %08x   (pc %08x)' % (n*8, p2(a), v & 0xffffffff, u.reg_read(UC_ARM_REG_PC)))
    def r(self, i): return self.u.reg_read(UC_ARM_REG_R0 + i)
    def rd32(self, a): return struct.unpack('<I', self.u.mem_read(a, 4))[0]
    def wr32(self, a, v): self.u.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def alloc(self, n):
        a = self.heap; self.heap += (n + 15) & ~15; return a
    def _code(self, u, addr, size, d):
        for lo, hi in self.allow:
            if lo <= addr < hi and addr not in self.stubs: return
        name = NAMES.get(addr, 'ext_%08x' % addr)
        import re as _re
        name = _re.sub(r'^(__e32_atomic_[a-z]+)_(acq|rel|rlx|ord)(32|8|16)$', r'\1_acq\3', name)
        if name == '__e32_atomic_store_acq32': name = '__e32_atomic_store_ord32'
        a = [self.r(i) for i in range(4)]
        sp = u.reg_read(UC_ARM_REG_SP)
        ret = 0
        cls = name.split('::')
        if addr in RET_THIS or (len(cls) >= 2 and cls[-1] == cls[-2]) or name.startswith('__aeabi_vec_ctor'):
            ret = a[0]
        if name.startswith('__aeabi_vec_ctor') and addr not in self.stubs:
            for i in range(a[3]):
                if a[1]: self.ncall(a[1], a[0] + i * a[2])
            ret = a[0]
        elif addr in self.stubs:
            ret = self.stubs[addr](self, a, sp)
        elif name in ('operator new', 'operator new[]', 'DBase::operator new', 'Kern::Alloc', 'Kern::AllocZ'):
            ret = self.alloc(a[0])
        elif name == '__aeabi_uldivmod':
            n_ = a[0] | a[1] << 32; d_ = a[2] | a[3] << 32
            q_, r_ = (n_ // d_, n_ % d_) if d_ else (0, 0)
            u.reg_write(UC_ARM_REG_R1, (q_ >> 32) & 0xffffffff)
            u.reg_write(UC_ARM_REG_R2, r_ & 0xffffffff); u.reg_write(UC_ARM_REG_R3, (r_ >> 32) & 0xffffffff)
            ret = q_ & 0xffffffff
        elif name == '__aeabi_idivmod':
            sx = lambda v: v - (1 << 32) if v & 0x80000000 else v
            n_, d_ = sx(a[0]), sx(a[1])
            if d_:
                q_ = abs(n_) // abs(d_) * (1 if (n_ < 0) == (d_ < 0) else -1); r_ = n_ - q_ * d_
            else:
                q_, r_ = 0, 0
            u.reg_write(UC_ARM_REG_R1, r_ & 0xffffffff); ret = q_ & 0xffffffff
        elif name == '__aeabi_uidivmod':
            q, rm = (a[0] // a[1], a[0] % a[1]) if a[1] else (0, 0)
            u.reg_write(UC_ARM_REG_R1, rm); ret = q
        elif name in ('memmove', 'memcpy', 'wordmove', 'Mem::Copy', 'Mem::Move', '__aeabi_memmove'):
            u.mem_write(a[0], bytes(u.mem_read(a[1], a[2]))); ret = a[0]
        elif name in ('memset', 'Mem::Fill'):
            u.mem_write(a[0], bytes([a[2] & 0xff]) * a[1] if name == 'Mem::Fill' else bytes([a[1] & 0xff]) * a[2]); ret = a[0]
        elif name in ('memclr', 'Mem::FillZ'):
            u.mem_write(a[0], b'\0' * a[1]); ret = a[0]
        elif name == '__aeabi_memcpy':
            u.mem_write(a[0], bytes(u.mem_read(a[1], a[2]))); ret = a[0]
        elif name == '__aeabi_memclr':
            u.mem_write(a[0], b'\0' * a[1]); ret = a[0]
        elif name == '__e32_atomic_load_acq32': ret = self.rd32(a[0])
        elif name == '__e32_atomic_store_ord32': self.wr32(a[0], a[1]); ret = a[1]
        elif name in ('__e32_atomic_swp_acq32',): ret = self.rd32(a[0]); self.wr32(a[0], a[1])
        elif name == '__e32_atomic_add_acq32': ret = self.rd32(a[0]); self.wr32(a[0], ret + a[1])
        elif name == '__e32_atomic_ior_acq32': ret = self.rd32(a[0]); self.wr32(a[0], ret | a[1])
        elif name == '__e32_atomic_tas_acq32':
            ret = self.rd32(a[0]); t = a[1] - (1<<32 if a[1] & 0x80000000 else 0)
            o = ret - (1<<32 if ret & 0x80000000 else 0)
            self.wr32(a[0], ret + (a[2] if o >= t else a[3]))
        elif name == 'TDesC8::Ptr':
            # TPtrC8: [len|type<<28][ptr] ; TPtr8: [len|type][max][ptr] ; TBuf: data inline
            w = self.rd32(a[0]); t = w >> 28
            ret = self.rd32(a[0] + 4) if t == 1 else self.rd32(a[0] + 8) if t == 2 else a[0] + (4 if t == 0 else 8)
        elif addr == 0x800af1a0:           # TPtrC8(ptr, len)
            self.wr32(a[0], (1 << 28) | a[2]); self.wr32(a[0] + 4, a[1]); ret = a[0]
        elif addr == 0x800af178:           # TPtrC8(const char *)
            n = len(self.cstr(a[1])); self.wr32(a[0], (1 << 28) | n); self.wr32(a[0] + 4, a[1]); ret = a[0]
        elif addr == 0x800af14c:           # TPtrC8(const TDesC8&)
            w = self.rd32(a[1]); t = w >> 28
            p_ = self.rd32(a[1] + 4) if t == 1 else self.rd32(a[1] + 8) if t == 2 else a[1] + (4 if t == 0 else 8)
            self.wr32(a[0], (1 << 28) | (w & 0xfffffff)); self.wr32(a[0] + 4, p_); ret = a[0]
        elif addr == 0x800af434:           # TPtr8(ptr, len, max)
            self.wr32(a[0], (2 << 28) | a[2]); self.wr32(a[0] + 4, a[3]); self.wr32(a[0] + 8, a[1]); ret = a[0]
        elif addr == 0x800af40c:           # TPtr8(ptr, max)
            self.wr32(a[0], (2 << 28)); self.wr32(a[0] + 4, a[2]); self.wr32(a[0] + 8, a[1]); ret = a[0]
        elif addr == 0x8006032c:           # TDes8::SetLength
            w = self.rd32(a[0]); self.wr32(a[0], (w & 0xf0000000) | a[1]); ret = 0
        elif name == 'Epoc::LinearToPhysical': ret = a[0]
        elif name in ('Kern::DfcQue0', 'Kern::CurrentThread', 'NKern::CurrentThread'):
            if not hasattr(self, '_fo'): self._fo = {}
            if name not in self._fo: self._fo[name] = self.fakeobj()
            ret = self._fo[name]
        elif name in ('Kern::DynamicDfcQCreate', 'Kern::DfcQCreate', 'Kern::SemaphoreCreate', 'Kern::MutexCreate'):
            self.wr32(a[0], self.fakeobj()); ret = 0
        elif addr == 0x800c5e50:      # power resource client register(&id, name)
            self.wr32(a[0], 0x77); ret = 0
        elif addr == 0x800c1784:      # gpio acquire(id) -> handle
            ret = a[0]
        elif name == 'Kern::Fault':
            self.out('FAULT %s %d' % (self.cstr(a[0]), a[1])); u.emu_stop(); return
        if name not in HIDE and (not self.quiet or name not in ('NKern::Lock','NKern::Unlock','NKern::ThreadEnterCS','NKern::ThreadLeaveCS','__e32_atomic_load_acq32')):
            s = ''
            if name == 'Kern::Printf': s = ' "%s"' % self.cstr(a[0])
            self.out('CALL %-28s (%08x %08x %08x %08x) -> %x%s' % (name, a[0], a[1], a[2], a[3], ret, s))
        u.reg_write(UC_ARM_REG_R0, ret & 0xffffffff)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
    def hook_stub(self, addr, fn):
        """stub a function inside the emulated module range"""
        self.stubs[addr] = fn
        self.u.hook_add(UC_HOOK_CODE, self._code, begin=addr, end=addr)
    def fakeobj(self):
        o = self.alloc(0x400)
        for k in range(0, 0x100, 4): self.wr32(o + k, self.alloc(0x100))
        return o
    def cstr(self, a):
        try:
            b = bytes(self.u.mem_read(a, 120)); return b.split(b'\0')[0].decode('latin1')
        except Exception: return '?'
    def ncall(self, fn, *args, maxi=2000000):
        """call from inside a hook: own stack below the current one, regs restored"""
        u = self.u
        saved = [u.reg_read(UC_ARM_REG_R0 + i) for i in range(13)] + \
                [u.reg_read(x) for x in (UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)]
        sp = (saved[13] - 0x2000) & ~7
        for i, v in enumerate(args[:4]): u.reg_write(UC_ARM_REG_R0 + i, v)
        u.reg_write(UC_ARM_REG_SP, sp); u.reg_write(UC_ARM_REG_LR, 0xd0000004)
        try:
            u.emu_start(fn, 0xd0000004, count=maxi)
        except UcError as ex:
            self.out('NESTED EMU ERROR %s at pc=%08x' % (ex, u.reg_read(UC_ARM_REG_PC)))
        r0 = self.r(0)
        for i in range(13): u.reg_write(UC_ARM_REG_R0 + i, saved[i])
        for x, v in zip((UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR), saved[13:]): u.reg_write(x, v)
        return r0
    def call(self, fn, *args, maxi=2000000):
        u = self.u
        for i, v in enumerate(args[:4]): u.reg_write(UC_ARM_REG_R0 + i, v)
        sp = 0xd00f0000
        for i, v in enumerate(args[4:]): self.wr32(sp + 4*i, v)
        u.reg_write(UC_ARM_REG_SP, sp); u.reg_write(UC_ARM_REG_LR, 0xd0000000)
        u.mem_write(0xd0000000, b'\0'*4)
        try:
            u.emu_start(fn, 0xd0000000, count=maxi)
        except UcError as e:
            self.out('EMU ERROR %s at pc=%08x lr=%08x r0=%08x r1=%08x' % (e, u.reg_read(UC_ARM_REG_PC), u.reg_read(UC_ARM_REG_LR), self.r(0), self.r(1)))
        self.flush()
        return self.r(0)
