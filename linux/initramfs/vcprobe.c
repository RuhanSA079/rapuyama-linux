// SPDX-License-Identifier: GPL-2.0-only
/*
 * vcprobe: poke the E6's BCM2727B1 (VideoCore III) host bus from userspace
 * through /dev/mem. Reversed from vcdriver.dll (E6 RM-609 kern.bin):
 *
 *   ViSSI host bus, phys 0x20025800 (PIO side)
 *     +0x14  status: bit 8 = bus cycle busy, bit 0 = reset busy
 *     +0x4c  W   "channel word" (written before a tx stream, 0 before rx)
 *     +0x50  W   tx data, one 16-bit bus write per store
 *     +0x58  W 0 = start a 16-bit read, then R = the data
 *     +0x60  bus mode: 0x100023 normal, 0x110023 = reads return status
 *     +0x64/+0x68 cycle timings (computed from the AHB clock)
 *   gfx DMA engine, phys 0x20024800 (the DMA side, not used here)
 *   HAT  = gpio 63 (VC -> host attention), RUN = gpio 92 (VC out of reset)
 *   clock: PRM EHwClkViSSI = gate 0x17 = 0x20033090 bit 5 (+4 set, +8 clr)
 *
 * status word = two status reads a | b << 15:
 *   bit 29 = a message is waiting, bit 28 = start of a new message,
 *   bit 27 = rx channel, bits 0-23 = length - 1 (bytes)
 *
 * boot ROM protocol (DVCDriver::Boot): 32-byte messages
 *   { 0xafe45251, type << 24 | value, 5 words, 0xf001bc4e }
 *   VC: type 1 "hello", word 2 = chip/rom id
 *   host: image on channel word 3, then { magic, 0x02000000 | size,
 *         20-byte trailer, magic } on channel word 2
 *   VC: type 3 value 1 = image accepted. loader first, then the main image.
 *
 * usage: vcprobe dump
 *        vcprobe hello            reset the VC and print its first message
 *        vcprobe boot DIR [ahfsmwd] full boot with DIR/vc-loader-b.* and vc-main.*
 *                                 a = use loader A, h = header before data,
 *                                 f = keep the fast (normal) bus timings,
 *                                 s = PIO with a pause after every 16 bytes,
 *                                 m = bus block in DMA mode (count first), CPU feeds it,
 *                                 w = swap the halfwords of every image word,
 *                                 d = images by the gfx DMA engine (crashed once)
 *        vcprobe listen DIR      boot (loader A), print what the VC sends for 5 s
 *        vcprobe paint DIR [rrggbb]  boot, VCHI connect, DISP open, fill the
 *                                 screen with a background colour (default ff0000)
 *        vcprobe timings boot|normal
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define SYS_exit 1
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_nanosleep 162
#define SYS_mmap2 192
#define SYS_clock_gettime 263
#define O_RDONLY 0
#define O_RDWR 2
#define O_SYNC 0x101000
#define PROT_RW 3
#define MAP_SHARED 1

static inline u32 sc3(u32 n, u32 a, u32 b, u32 c)
{
	register u32 r0 __asm__("r0") = a;
	register u32 r1 __asm__("r1") = b;
	register u32 r2 __asm__("r2") = c;
	register u32 r7 __asm__("r7") = n;

	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7)
			 : "memory", "cc");
	return r0;
}

static inline u32 sc6(u32 n, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f)
{
	register u32 r0 __asm__("r0") = a;
	register u32 r1 __asm__("r1") = b;
	register u32 r2 __asm__("r2") = c;
	register u32 r3 __asm__("r3") = d;
	register u32 r4 __asm__("r4") = e;
	register u32 r5 __asm__("r5") = f;
	register u32 r7 __asm__("r7") = n;

	__asm__ volatile("svc 0" : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
			 : "memory", "cc");
	return r0;
}

static __attribute__((noreturn)) void die(u32 code)
{
	sc3(SYS_exit, code, 0, 0);
	__builtin_unreachable();
}

int raise(int sig)
{
	(void)sig;
	die(1);
}

static u32 slen(const char *s)
{
	u32 n = 0;

	while (s[n])
		n++;
	return n;
}

static void puts_(const char *s)
{
	sc3(SYS_write, 1, (u32)s, slen(s));
}

static void puthex(u32 v, int digits)
{
	char b[8];

	for (int i = digits - 1; i >= 0; i--, v >>= 4)
		b[i] = "0123456789abcdef"[v & 15];
	sc3(SYS_write, 1, (u32)b, digits);
}

static void putreg(const char *name, u32 v)
{
	puts_(name);
	puthex(v, 8);
	puts_("\n");
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static void msleep(u32 ms)
{
	u32 ts[2] = { 0, 0 };

	while (ms >= 1000) {
		ts[0]++;
		ms -= 1000;
	}
	ts[1] = ms * 1000000;
	sc3(SYS_nanosleep, (u32)ts, 0, 0);
}

static u32 now_ms(void)
{
	u32 ts[2];

	sc3(SYS_clock_gettime, 1, (u32)ts, 0);
	return ts[0] * 1000 + ts[1] / 1000000;
}

static int memfd;

static volatile u32 *map_len(u32 phys, u32 len)
{
	u32 p = sc6(SYS_mmap2, 0, len, PROT_RW, MAP_SHARED, memfd,
		    phys >> 12);

	if (p >= (u32)-4095) {
		puts_("vcprobe: mmap failed for ");
		puthex(phys, 8);
		puts_("\n");
		die(1);
	}
	return (volatile u32 *)p;
}

static volatile u32 *map(u32 phys)
{
	return map_len(phys, 0x1000);
}

static volatile u32 *vb;	/* 0x20025000 */
static volatile u32 *va;	/* 0x20024000 */
static volatile u32 *vg;	/* 0x20031000 */
static volatile u32 *vc;	/* 0x20033000 */
static volatile u32 *vp;	/* 0x20032000 */

#define B(o)	vb[(0x800 + (o)) / 4]
#define A(o)	va[(0x800 + (o)) / 4]
#define GP(o)	vg[(0x900 + (o)) / 4]
#define CK(o)	vc[(o) / 4]

#define HAT_PIN	63
#define RUN_PIN	92
#define VSSI_GATE_REG	0x090
#define VSSI_GATE_BIT	(1u << 5)

#define BOOT_MAGIC0	0xafe45251u
#define BOOT_MAGIC1	0xf001bc4eu

static int pin_level(u32 pin)
{
	return (GP(0xf0 + 0x10 * (pin >> 5)) >> (pin & 31)) & 1;
}

static int pin_latch(u32 pin)
{
	return (GP(0x90 + 0x10 * (pin >> 5)) >> (pin & 31)) & 1;
}

static void pin_set(u32 pin, int high)
{
	GP(0x90 + 0x10 * (pin >> 5) + (high ? 4 : 8)) = 1u << (pin & 31);
}

static int bus_idle(void)
{
	for (u32 i = 0; i < 2000000; i++)
		if (!(B(0x14) & 0x100))
			return 0;
	puts_("vcprobe: bus stuck busy\n");
	return -1;
}

static int bus_chan(u32 w)
{
	if (bus_idle())
		return -1;
	B(0x4c) = w;
	return 0;
}

static int bus_wr(u32 v)
{
	if (bus_idle())
		return -1;
	B(0x50) = v & 0xffff;
	return 0;
}

static int bus_rd(u32 *v)
{
	if (bus_idle())
		return -1;
	B(0x58) = 0;
	if (bus_idle())
		return -1;
	*v = B(0x58) & 0xffff;
	return 0;
}

static u32 last_status;

static int bus_status(u32 *st)
{
	u32 a, b;
	int r = -1;

	B(0x60) = 0x110023;
	if (bus_rd(&a) || bus_rd(&b))
		goto out;
	if (a & 0x8000) {
		a = b;
		if (bus_rd(&b))
			goto out;
	}
	if ((b & 0x4000) && !(last_status & 0x20000000))
		if (bus_rd(&a) || bus_rd(&b))
			goto out;
	*st = last_status = a | b << 15;
	r = 0;
out:
	B(0x60) = 0x100023;
	return r;
}

static void dump(void)
{
	static const u16 boff[] = { 0x10, 0x14, 0x40, 0x44, 0x60, 0x64, 0x68,
				    0x6c, 0x90 };
	static const u16 aoff[] = { 0x10, 0x14, 0x18, 0x1c, 0x40, 0x44, 0x5c,
				    0x60, 0x64, 0x68, 0x6c, 0x70, 0x7c, 0x80,
				    0x88, 0x8c, 0xa0, 0xac, 0xb0, 0xb4 };

	putreg("pll 0x20032604 = ", vp[0x604 / 4]);
	putreg("pll 0x20032624 = ", vp[0x624 / 4]);
	putreg("pll 0x20032644 = ", vp[0x644 / 4]);
	putreg("pll 0x20032664 = ", vp[0x664 / 4]);
	putreg("clk 0x20033004 = ", CK(0x004));
	putreg("clk 0x20033038 = ", CK(0x038));
	putreg("clk 0x20033090 = ", CK(0x090));
	putreg("clk 0x200330b0 = ", CK(0x0b0));
	putreg("clk 0x20033120 = ", CK(0x120));
	putreg("clk 0x20033130 = ", CK(0x130));
	putreg("mux 0x200318e0 = ", vg[0x8e0 / 4]);
	putreg("mux 0x200318f0 = ", vg[0x8f0 / 4]);
	putreg("mux 0x20031900 = ", vg[0x900 / 4]);
	puts_("HAT (gpio 63) level ");
	puthex(pin_level(HAT_PIN), 1);
	puts_(", RUN (gpio 92) level ");
	puthex(pin_level(RUN_PIN), 1);
	puts_(" latch ");
	puthex(pin_latch(RUN_PIN), 1);
	puts_("\n");
	if (!(CK(VSSI_GATE_REG) & VSSI_GATE_BIT)) {
		puts_("ViSSI clock gate is OFF, not reading the bus blocks\n");
		return;
	}
	for (u32 i = 0; i < sizeof(boff) / sizeof(boff[0]); i++) {
		puts_("bus 0x20025");
		puthex(0x800 + boff[i], 3);
		putreg(" = ", B(boff[i]));
	}
	for (u32 i = 0; i < sizeof(aoff) / sizeof(aoff[0]); i++) {
		puts_("dma 0x20024");
		puthex(0x800 + aoff[i], 3);
		putreg(" = ", A(aoff[i]));
	}
}

static int clock_on(void)
{
	if (!(CK(VSSI_GATE_REG) & VSSI_GATE_BIT)) {
		puts_("ViSSI clock was off, switching it on\n");
		CK(VSSI_GATE_REG + 4) = VSSI_GATE_BIT;
		msleep(1);
	}
	if (B(0x60) != 0x100023) {
		puts_("bus mode is not 0x100023, re-running the firmware bus init\n");
		/* FUN_8032fb08 + FUN_8032a368 */
		B(0x10) = 10;
		for (u32 i = 0; i < 1000000 && (B(0x14) & 1); i++)
			;
		B(0x10) = 0x11;
		B(0x60) = 0x100023;
		B(0x6c) = 0x10;
		B(0x90) = 0x6d60;
		B(0x40) = 0x64;
		if (bus_idle())
			return -1;
		B(0x40) &= ~0x11u;
	}
	return bus_chan(0);
}

static u8 rxbuf[0x800];
static int rx_first = 1;

/* one message from the VC, as FUN_8032bb18 reads it */
static int rx_msg(u32 timeout_ms, u32 *len)
{
	u32 t0 = now_ms(), st = 0, n, v;
	int hat0 = -1;

	/* the firmware drops one status read at the start of every message */
	if (bus_status(&st))
		return -1;
	while (1) {
		int hat = pin_level(HAT_PIN);

		if (hat != hat0) {
			puts_("  HAT=");
			puthex(hat, 1);
			puts_("\n");
			hat0 = hat;
		}
		if (bus_status(&st))
			return -1;
		if (st & 0x20000000)
			break;
		if (now_ms() - t0 > timeout_ms) {
			putreg("  timeout, last status ", st);
			return -1;
		}
		msleep(1);
	}
	putreg("  status ", st);
	n = (st & 0xffffff) + 1;
	if (n > sizeof(rxbuf)) {
		puts_("  message too long for this tool\n");
		return -1;
	}
	for (u32 i = 0; i < n; i += 2) {
		if (bus_rd(&v))
			return -1;
		rxbuf[i] = v;
		if (i + 1 < n)
			rxbuf[i + 1] = v >> 8;
	}
	bus_status(&st);
	bus_status(&st);
	*len = n;
	return 0;
}

static void print_msg(u32 n)
{
	puts_("  rx");
	for (u32 i = 0; i + 3 < n; i += 4) {
		puts_(" ");
		puthex(rxbuf[i] | rxbuf[i + 1] << 8 | rxbuf[i + 2] << 16 |
		       (u32)rxbuf[i + 3] << 24, 8);
	}
	puts_("\n");
}

static int rx_boot(u32 *type, u32 *value, u32 *arg)
{
	u32 n, *w = (u32 *)rxbuf;

	if (rx_first) {
		/* rx channel armed: the firmware writes channel word 0 first */
		if (bus_chan(0))
			return -1;
		rx_first = 0;
	}
	if (rx_msg(2000, &n))
		return -1;
	print_msg(n);
	if (n != 32 || w[0] != BOOT_MAGIC0 || w[7] != BOOT_MAGIC1) {
		puts_("  not a boot message\n");
		return -1;
	}
	*type = w[1] >> 24;
	*value = w[1] & 0xffffff;
	*arg = w[2];
	return 0;
}

/*
 * bus cycle timings, what FUN_8032fa3c computes for the E6's 597.33 MHz
 * clock (checked: the normal pair equals what Symbian leaves behind).
 * DVCDriver::Boot switches to the slow pair for the boot ROM and back to
 * the normal one once the VC is up.
 */
#define TIM_BOOT_64	0x08008080u
#define TIM_BOOT_68	0x023c03c3u
#define TIM_NORM_64	0x05008050u
#define TIM_NORM_68	0x017c01c3u
static int fast_timings;

static void set_timings(int boot_mode)
{
	if (bus_idle())
		return;
	B(0x64) = boot_mode ? TIM_BOOT_64 : TIM_NORM_64;
	B(0x68) = boot_mode ? TIM_BOOT_68 : TIM_NORM_68;
	puts_(boot_mode ? "bus timings: boot\n" : "bus timings: normal\n");
}

static int vc_reset(void)
{
	if (!fast_timings)
		set_timings(1);
	puts_("RUN low\n");
	pin_set(RUN_PIN, 0);
	GP(0xf8 + 0x10 * (RUN_PIN >> 5)) = 1u << (RUN_PIN & 31);
	msleep(50);
	putreg("  HAT level ", pin_level(HAT_PIN));
	puts_("RUN high\n");
	pin_set(RUN_PIN, 1);
	return 0;
}

static u32 st_or, st_and;
static int pace;
static int dma_mode_pio;

static u32 udiv(u32 n, u32 d)
{
	u32 q = 0, bit = 1;

	while (d < n && !(d & 0x80000000u)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	return q;
}

/*
 * w x h halfwords for the next DMA chunk of r bytes, FUN_8032aa18 (only
 * the cases our images hit; checked against the ROM code in the emulator:
 * 0x2040 -> 0x38x0x49 + 8x5, 0xf6ab0 -> 0x148x0x604 + 8x7)
 */
static void chunk_dims(u32 r, u32 *w, u32 *h)
{
	u32 add, dv;

	if (!(r & 15) && (r >> 4) >= 1 && (r >> 4) <= 7) {
		*w = 8;
		*h = r >> 4;
		return;
	}
	if (r == 0x80) {
		*w = 0x10;
		*h = 4;
		return;
	}
	if (r <= 0x800)
		add = 8, dv = 0x52;
	else if (r <= 0x6000)
		add = 0x20, dv = 0x1ae;
	else if (r <= 0x3e000)
		add = 0x50, dv = 0x6a4;
	else
		add = 200, dv = 8000;
	*w = (udiv(r, dv) + add + 7) & ~7u;
	if (*w > 0x800)
		*w = 0x800;
	*h = udiv(r, *w * 2);
	if (*h > 0x800) {
		*h = 0x800;
		*w = (r >> 12) & ~7u;
	}
}

/*
 * the bus block's DMA mode, fed by the CPU instead of the gfx DMA engine:
 * count into 0x44, mode bits 0x11 in 0x40, then the halfwords. The guess
 * is that the count is what tells the VC how much is coming.
 */
static int tx_dma_mode(const u8 *p, u32 len, u32 chan)
{
	u32 done = 0;

	if (bus_chan(chan))
		return -1;
	while (done < len) {
		u32 w, h, n;

		chunk_dims(len - done, &w, &h);
		n = w * h;
		puts_("  chunk ");
		puthex(w, 4);
		puts_(" x ");
		puthex(h, 4);
		puts_("\n");
		if (bus_idle())
			return -1;
		B(0x44) = n;
		B(0x40) |= 0x11;
		for (u32 i = 0; i < n; i++, done += 2)
			if (bus_wr(p[done] | p[done + 1] << 8)) {
				puts_("  stuck in dma mode, leaving the block alone\n");
				return -1;
			}
		if (bus_idle())
			return -1;
		B(0x40) &= ~0x11u;
	}
	return bus_chan(0);
}

static int tx_words(const u8 *p, u32 len, u32 chan)
{
	u32 ts[2] = { 0, 2000 };	/* 2 us, really one tick */

	if (bus_chan(chan))
		return -1;
	st_or = 0;
	st_and = ~0u;
	for (u32 i = 0; i < len; i += 2) {
		u32 st = B(0x14);

		st_or |= st;
		st_and &= st;
		if (bus_wr(p[i] | p[i + 1] << 8))
			return -1;
		if (pace && (i & 15) == 14)
			sc3(SYS_nanosleep, (u32)ts, 0, 0);
	}
	if (len > 32 && st_or) {
		putreg("  bus 0x14 seen OR  ", st_or);
		putreg("  bus 0x14 seen AND ", st_and);
	}
	/*
	 * the firmware's completion DFC (FUN_80329a48 -> FUN_8032a98c) writes
	 * channel word 0 after every transfer: that's what ends it for the VC
	 */
	return bus_chan(0);
}

/*
 * DMA source: the reserved framebuffer area. With PHYS_OFFSET 0x10a00000 it
 * is outside Linux RAM, so /dev/mem hands it out (and it is uncached).
 */
#define DMA_PHYS	0x10500000u
#define DMA_LEN		0x00100000u
static volatile u32 *dmabuf;
static int use_dma;

/*
 * one chunk through the gfx DMA engine, the "small" path of FUN_80329ec8
 * (dif+0x180 is ~0 on the E6, so the firmware never takes the other one):
 * w x h halfwords from phys, the bus block paces it (unlike PIO).
 */
static int dma_chunk(u32 phys, u32 w, u32 h)
{
	u32 t0, st;

	if (A(0x18) & 1)
		A(0x18) = 1;
	A(0x80) = phys;
	A(0x7c) = (h - 1) << 16 | (w - 1);
	A(0x8c) = (h - 1) << 16 | (w - 1);
	A(0x6c) = 0;
	A(0x40) = 0x8908;
	A(0x40) |= 0x20;
	A(0x40) |= 1;
	B(0x44) = w * h;
	B(0x40) |= 0x11;
	t0 = now_ms();
	while (!((st = A(0x18)) & 1)) {
		if (now_ms() - t0 > 1000) {
			/* don't touch the blocks again: last time that killed the phone */
			putreg("  dma timeout, dma status ", st);
			return -1;
		}
	}
	A(0x18) = st & 0x1ffff;
	if (st & 0x8040)
		putreg("  dma error bits ", st);
	if (bus_idle())
		return -1;
	B(0x40) &= ~0x11u;
	return 0;
}

static int tx_dma(const u8 *p, u32 len, u32 chan)
{
	u32 hw = len / 2, done = 0;

	if (len > DMA_LEN || (len & 15))
		return -1;
	for (u32 i = 0; i < len; i += 4)
		dmabuf[i / 4] = p[i] | p[i + 1] << 8 | p[i + 2] << 16 |
				(u32)p[i + 3] << 24;
	if (bus_chan(chan))
		return -1;
	/* 8 halfwords wide (as the firmware does for small sizes), <= 0x800 rows */
	while (done < hw) {
		u32 rows = (hw - done) / 8;

		if (rows > 0x800)
			rows = 0x800;
		if (dma_chunk(DMA_PHYS + done * 2, 8, rows))
			return -1;
		done += rows * 8;
	}
	return bus_chan(0);
}

static u8 image[0x100000 + 0x40];
static u8 trailer[20];

static int load(const char *dir, const char *name, const char *ext, u8 *buf,
		u32 max, u32 *len)
{
	char path[256];
	u32 n = 0, fd, r;
	const char *parts[] = { dir, "/", name, ext };

	for (u32 i = 0; i < 4; i++)
		for (const char *s = parts[i]; *s && n < sizeof(path) - 1; s++)
			path[n++] = *s;
	path[n] = 0;
	fd = sc3(SYS_open, (u32)path, O_RDONLY, 0);
	if ((int)fd < 0) {
		puts_("vcprobe: can't open ");
		puts_(path);
		puts_("\n");
		return -1;
	}
	n = 0;
	while (n < max && (int)(r = sc3(SYS_read, fd, (u32)(buf + n), max - n)) > 0)
		n += r;
	sc3(SYS_close, fd, 0, 0);
	*len = n;
	return 0;
}

static int hdr_first;
static int swap_halves;

static int send_image(const char *dir, const char *name)
{
	u32 len, tlen, hdr[8];

	if (load(dir, name, ".bin", image, 0x100000, &len) ||
	    load(dir, name, ".trl", trailer, 20, &tlen) || tlen != 20)
		return -1;
	puts_("sending ");
	puts_(name);
	putreg(", bytes ", len);
	for (u32 i = len; i < ((len + 15) & ~15u); i++)
		image[i] = 0;
	if (swap_halves)
		/* the DMA engine might put the upper halfword of each word first */
		for (u32 i = 0; i < ((len + 15) & ~15u); i += 4) {
			u8 a = image[i], b = image[i + 1];

			image[i] = image[i + 2];
			image[i + 1] = image[i + 3];
			image[i + 2] = a;
			image[i + 3] = b;
		}
	hdr[0] = BOOT_MAGIC0;
	hdr[1] = 0x02000000 | (len & 0xffffff);
	for (u32 i = 0; i < 5; i++)
		hdr[2 + i] = trailer[4 * i] | trailer[4 * i + 1] << 8 |
			     trailer[4 * i + 2] << 16 | (u32)trailer[4 * i + 3] << 24;
	hdr[7] = BOOT_MAGIC1;
	/* the firmware queues the data (tx ch1) before the header (tx ch0) */
	if (hdr_first && tx_words((const u8 *)hdr, 32, 2))
		return -1;
	if (use_dma ? tx_dma(image, (len + 15) & ~15u, 3) :
	    dma_mode_pio ? tx_dma_mode(image, (len + 15) & ~15u, 3) :
		      tx_words(image, (len + 15) & ~15u, 3))
		return -1;
	return hdr_first ? 0 : tx_words((const u8 *)hdr, 32, 2);
}

static int boot(const char *dir, int loader_a)
{
	u32 type, value, arg, t0;

	vc_reset();
	if (rx_boot(&type, &value, &arg))
		return -1;
	putreg("hello: type/value ", type << 24 | value);
	putreg("       rom id ", arg);
	if (type != 1)
		return -1;

	t0 = now_ms();
	if (send_image(dir, loader_a ? "vc-loader-a" : "vc-loader-b"))
		return -1;
	putreg("  sent in ms ", now_ms() - t0);
	for (int i = 0; i < 5; i++) {
		u32 st = 0;

		bus_status(&st);
		puts_("  status now ");
		puthex(st, 8);
		puts_(", HAT ");
		puthex(pin_level(HAT_PIN), 1);
		puts_("\n");
		msleep(20);
	}
	if (rx_boot(&type, &value, &arg))
		return -1;
	putreg("loader ack: ", type << 24 | value);
	if (type != 3 || value != 1)
		return -1;
	if (rx_boot(&type, &value, &arg))
		return -1;
	putreg("loader hello: ", type << 24 | value);
	putreg("       arg ", arg);
	if (type != 1)
		return -1;

	t0 = now_ms();
	if (send_image(dir, "vc-main"))
		return -1;
	putreg("  sent in ms ", now_ms() - t0);
	if (rx_boot(&type, &value, &arg))
		return -1;
	putreg("main ack: ", type << 24 | value);
	if (type != 3 || value != 1)
		return -1;
	puts_("VideoCore booted\n");
	set_timings(0);
	return 0;
}


/*
 * ---- VCHI (vchi.dll) ----
 * slots travel as whole bus transfers on channel word 0. A slot is a run of
 * 16-byte aligned entries:
 *   u16 0xaf05 | u16 entry size | u32 fourcc | u32 timestamp |
 *   u16 credit (rx slots freed) | u16 payload size | payload
 * pad entries use 0x50fa and fourcc "____". A host tx slot is always closed
 * the way FUN_80331994 does it: pad up to 0x1010, then a 16-byte 0xaf05
 * "____" entry, 0x1020 bytes in total.
 * CTRL payload word 0: 0 connect, 1 service query, 2 bulk req, 3/4 flow
 * control, 5 service reply.
 */
#define FOURCC(a, b, c, d)	((u32)(a) | (u32)(b) << 8 | (u32)(c) << 16 | (u32)(d) << 24)
#define FCC_CTRL	FOURCC('C', 'T', 'R', 'L')
#define FCC_PAD		FOURCC('_', '_', '_', '_')
#define FCC_DISP	FOURCC('D', 'I', 'S', 'P')
#define FCC_UPDH	FOURCC('U', 'P', 'D', 'H')
#define FCC_DSPC	FOURCC('D', 'S', 'P', 'C')
#define VCHI_MAGIC	0xaf05u
#define VCHI_PAD	0x50fau
#define HOST_RX_SLOTS	15

static u8 slotbuf[0x1100];
static u8 txslot[0x1020];
static u32 tx_cur;
static u32 credits;
static int vc_connected;
static int host_connected;
static u32 svc_reply_fourcc, svc_reply_flags;
static int svc_reply_seen;
static u32 pending_query[8];
static u32 npending;
static u32 disp_reply;
static int disp_reply_seen;
static u8 dspc_rx[64];
static u32 dspc_rx_len;
static int dspc_rx_seen;

static u32 rd32le(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static void wr32le(u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void putfcc(u32 f)
{
	char c[4];

	for (int i = 0; i < 4; i++) {
		c[i] = f >> (8 * i);
		if (c[i] < 32 || c[i] > 126)
			c[i] = '.';
	}
	sc3(SYS_write, 1, (u32)c, 4);
}

/* one VCHI slot from the VC, reading it in <= 0x800 byte rounds like bb18 */
static int rx_slot(u32 timeout_ms, u32 *len)
{
	u32 t0 = now_ms(), st = 0, got = 0, v;
	int first = 1;

	if (bus_status(&st))
		return -1;
	while (1) {
		u32 n, c;

		while (1) {
			if (bus_status(&st))
				return -1;
			if (st & 0x20000000)
				break;
			if (now_ms() - t0 > timeout_ms) {
				if (!first)
					putreg("  timeout inside a slot, status ", st);
				return 1;
			}
			msleep(1);
		}
		n = (st & 0xffffff) + 1;
		c = n > 0x800 ? 0x800 : n;
		if (got + c > sizeof(slotbuf)) {
			putreg("  slot too long, status ", st);
			return -1;
		}
		for (u32 i = 0; i < c; i += 2) {
			if (bus_rd(&v))
				return -1;
			slotbuf[got + i] = v;
			if (i + 1 < c)
				slotbuf[got + i + 1] = v >> 8;
		}
		got += c;
		first = 0;
		if (c == n) {
			bus_status(&st);
			bus_status(&st);
			break;
		}
		t0 = now_ms();
	}
	*len = got;
	return 0;
}

static void slot_begin(void)
{
	tx_cur = 0;
}

static void slot_msg(u32 fourcc, const u32 *words, u32 nwords)
{
	u32 n = nwords * 4, ent = (n + 0x1f) & ~15u;
	u8 *h = txslot + tx_cur;

	for (u32 i = 0; i < ent; i++)
		h[i] = 0;
	h[0] = VCHI_MAGIC & 0xff;
	h[1] = VCHI_MAGIC >> 8;
	h[2] = ent;
	h[3] = ent >> 8;
	wr32le(h + 4, fourcc);
	wr32le(h + 8, 0);
	/* FUN_803314f8 puts the freed rx slot count into the first message */
	h[12] = credits;
	h[13] = credits >> 8;
	credits = 0;
	h[14] = n;
	h[15] = n >> 8;
	for (u32 i = 0; i < nwords; i++)
		wr32le(h + 16 + 4 * i, words[i]);
	tx_cur += ent;
}

static int slot_send(void)
{
	u8 *h;

	if (tx_cur < 0x1010) {
		h = txslot + tx_cur;
		for (u32 i = 0; i < 0x1010 - tx_cur; i++)
			h[i] = 0;
		h[0] = VCHI_PAD & 0xff;
		h[1] = VCHI_PAD >> 8;
		h[2] = (0x1010 - tx_cur);
		h[3] = (0x1010 - tx_cur) >> 8;
		wr32le(h + 4, FCC_PAD);
	}
	h = txslot + 0x1010;
	for (u32 i = 0; i < 16; i++)
		h[i] = 0;
	h[0] = VCHI_MAGIC & 0xff;
	h[1] = VCHI_MAGIC >> 8;
	h[2] = 0x10;
	wr32le(h + 4, FCC_PAD);
	return tx_words(txslot, 0x1020, 0);
}

static int send_one(u32 fourcc, const u32 *words, u32 nwords)
{
	slot_begin();
	slot_msg(fourcc, words, nwords);
	return slot_send();
}

static void handle_slot(u32 n)
{
	u32 off = 0;

	credits++;
	while (off + 16 <= n) {
		const u8 *h = slotbuf + off;
		u32 magic = h[0] | h[1] << 8, ent = h[2] | h[3] << 8;
		u32 fcc = rd32le(h + 4), size = h[14] | h[15] << 8;
		const u8 *pl = h + 16;

		if (ent < 16 || off + ent > n) {
			puts_("  bad entry, raw:");
			for (u32 i = 0; i < 16 && off + i < n; i++) {
				puts_(" ");
				puthex(h[i], 2);
			}
			puts_("\n");
			return;
		}
		if (magic == VCHI_MAGIC && fcc != FCC_PAD) {
			puts_("  msg ");
			putfcc(fcc);
			puts_(" size ");
			puthex(size, 4);
			puts_(" credit ");
			puthex(h[12] | h[13] << 8, 4);
			puts_(":");
			for (u32 i = 0; i + 3 < size && i < 48; i += 4) {
				puts_(" ");
				puthex(rd32le(pl + i), 8);
			}
			puts_("\n");
			if (fcc == FCC_CTRL && size >= 4) {
				u32 type = rd32le(pl);

				if (type == 0 && size >= 16) {
					putreg("    VC connect, slot size ", rd32le(pl + 8));
					putreg("    VC slots ", rd32le(pl + 12));
					vc_connected = 1;
				} else if (type == 1 && size >= 8) {
					/* VC asks for a host service: we have none.
					 * answered once we are connected ourselves */
					u32 f = rd32le(pl + 4);

					puts_("    VC asks for host service ");
					putfcc(f);
					puts_("\n");
					if (host_connected) {
						u32 r[3] = { 5, f, 0 };

						send_one(FCC_CTRL, r, 3);
					} else if (npending < 8) {
						pending_query[npending++] = f;
					}
				} else if (type == 5 && size >= 12) {
					svc_reply_fourcc = rd32le(pl + 4);
					svc_reply_flags = rd32le(pl + 8);
					svc_reply_seen = 1;
				}
			} else if (fcc == FCC_DSPC) {
				dspc_rx_len = size < sizeof(dspc_rx) ? size : sizeof(dspc_rx);
				for (u32 i = 0; i < dspc_rx_len; i++)
					dspc_rx[i] = pl[i];
				dspc_rx_seen = 1;
			} else if (fcc == FCC_DISP && size == 4) {
				disp_reply = rd32le(pl);
				disp_reply_seen = 1;
			}
		}
		off += ent;
	}
}

/* read and handle slots until cond is set or the time is up */
static int pump(u32 timeout_ms, int *cond)
{
	u32 t0 = now_ms(), n;

	while (!(cond && *cond)) {
		int r;

		if (now_ms() - t0 > timeout_ms)
			return cond ? -1 : 0;
		r = rx_slot(100, &n);
		if (r < 0)
			return -1;
		if (r == 0) {
			puts_("slot from VC, bytes ");
			puthex(n, 4);
			puts_("\n");
			handle_slot(n);
		}
	}
	return 0;
}

static int vchi_connect(void)
{
	u32 hello[5] = { 0, 1, 0x1010, HOST_RX_SLOTS, 0x800 };

	puts_("waiting for the VC's CTRL connect\n");
	if (pump(5000, &vc_connected)) {
		puts_("no connect from the VC\n");
		return -1;
	}
	puts_("sending host connect\n");
	if (send_one(FCC_CTRL, hello, 5))
		return -1;
	host_connected = 1;
	for (u32 i = 0; i < npending; i++) {
		u32 r[3] = { 5, pending_query[i], 0 };

		puts_("answering 'not here' for ");
		putfcc(r[1]);
		puts_("\n");
		if (send_one(FCC_CTRL, r, 3))
			return -1;
	}
	npending = 0;
	return 0;
}

static int vchi_open(u32 fourcc)
{
	u32 req[3] = { 1, fourcc, 0x10 };

	puts_("opening service ");
	putfcc(fourcc);
	puts_("\n");
	svc_reply_seen = 0;
	if (send_one(FCC_CTRL, req, 3) || pump(3000, &svc_reply_seen)) {
		puts_("  no answer\n");
		return -1;
	}
	putreg("  answer flags ", svc_reply_flags);
	return (svc_reply_flags & 1) ? 0 : -1;
}

/* dispman request: command word (bit 31 = wants the 4-byte reply) + args */
static int disp(u32 cmd, const u32 *args, u32 nargs, int sync, u32 *out)
{
	u32 w[20];

	w[0] = cmd | (sync ? 0x80000000u : 0);
	for (u32 i = 0; i < nargs; i++)
		w[1 + i] = args[i];
	puts_("dispman cmd ");
	puthex(cmd, 2);
	puts_("\n");
	disp_reply_seen = 0;
	if (send_one(FCC_DISP, w, 1 + nargs))
		return -1;
	if (!sync)
		return pump(200, 0);
	if (pump(3000, &disp_reply_seen)) {
		puts_("  no reply\n");
		return -1;
	}
	putreg("  reply ", disp_reply);
	if (out)
		*out = disp_reply;
	return 0;
}

static int use_element;

static u32 g_display;

static int disp_open(void)
{
	u32 display = 0, a[1];

	if (g_display)
		return 0;
	if (vchi_open(FCC_DISP))
		return -1;
	vchi_open(FCC_UPDH);
	/* DISPMANX_COMMAND_T (old Broadcom numbering, matches vcdispman.dll) */
	/*
	 * DisplayOpen(device) is 0x08 (vcdispman vtable +0x18, called by the
	 * display chipset with its display index 0). 0x04 is
	 * ResourceCreateFromImage, its 0x60000000 was a resource, not a
	 * display. 0x08 only works once DSPC has configured the panel.
	 */
	a[0] = 0;
	if (disp(0x08, a, 1, 1, &display))
		return -1;
	if (!display) {
		puts_("DisplayOpen(0) failed\n");
		return -1;
	}
	putreg("display handle ", display);
	g_display = display;
	return 0;
}

static int paint(u32 r, u32 g, u32 b)
{
	u32 display, update, a[5];

	if (disp_open())
		return -1;
	display = g_display;
	a[0] = 1;
	if (disp(0x10, a, 1, 1, &update))		/* UpdateStart(priority 1) */
		return -1;
	a[0] = update;
	a[1] = display;
	a[2] = r;
	a[3] = g;
	a[4] = b;
	if (disp(0x0d, a, 5, 0, 0))			/* DisplaySetBackground */
		return -1;
	if (use_element) {
		/*
		 * the display chipset's compose (FUN_8034d0f4): ResourceCreate
		 * (vtable +0x08, cmd 3: type, w, h) then ElementAdd (+0x40,
		 * cmd 0x13, 64 bytes): update, display, layer, dest x y w h,
		 * resource, src x y w h (16.16), flags 1, alpha, mask 0, transform.
		 * the resource is not filled (that goes over vc_bufmanx), so expect
		 * garbage or black, but a full frame.
		 */
		u32 res, el, e[16];

		a[0] = 1;	/* VC image type for Symbian 0x20 (RGB565), FUN_80345920 */
		a[1] = 640;
		a[2] = 480;
		if (disp(0x03, a, 3, 1, &res))
			return -1;
		putreg("resource ", res);
		e[0] = update;
		e[1] = display;
		e[2] = (u32)-400;
		e[3] = 0;
		e[4] = 0;
		e[5] = 640;
		e[6] = 480;
		e[7] = res;
		e[8] = 0;
		e[9] = 0;
		e[10] = 640 << 16;
		e[11] = 480 << 16;
		e[12] = 1;
		e[13] = 0xff;
		e[14] = 0;
		e[15] = 0;
		if (disp(0x13, e, 16, 1, &el))
			return -1;
		putreg("element ", el);
	}
	a[0] = update;
	if (disp(0x12, a, 1, 1, 0))			/* UpdateSubmitSync */
		return -1;
	puts_("background submitted, listening 8 s for UPDH/DSPC traffic\n");
	return pump(8000, 0);
}

/*
 * ---- DSPC: display command service (dispcommanddrv.dll) ----
 * every request is 16 bytes (FUN_8034173c, template 0x80341cc0):
 *   u16 display | u16 interface id | u32 cmd | 0x100000 | u32 token | u32 arg
 * then an optional payload. display is 0xffff for power and DCS batches,
 * the display index (0) for the panel config. the interface id comes from
 * the display chipset table at 0x8034eaa4 (entries 4 and 6); the E6 panel
 * family (database cases 0x78/0x79/0x85 on top of 0x72, 640x480) sets the
 * colmod nibble-swapped, i.e. RGB (DPI) video -> id 4.
 * cmd 2 = power (0x81 on, 0x80 off, 0x40/0x41 for modes 8/9), cmd 3 = panel
 * config (FUN_80340d50), cmd 0x40 = DCS batch (FUN_80341244). config and
 * batch (FUN_80340b7c) have no arg word, their payload follows the token.
 * batch payload:
 *   u32 count, per command: u32 1, u32 0, u32 dcs, u32 padded len, u32 len,
 *   data bytes (LE packed, padded to 4)
 */
static u8 dbuf[0x800];
static u32 dlen;
static u32 dspc_id = 4;
static u32 dspc_token = 0x100;

static void d16(u32 v)
{
	dbuf[dlen++] = v;
	dbuf[dlen++] = v >> 8;
}

static void d32(u32 v)
{
	wr32le(dbuf + dlen, v);
	dlen += 4;
}

static void dspc_hdr(u32 display, u32 cmd, u32 arg)
{
	dlen = 0;
	d16(display);
	d16(dspc_id);
	d32(cmd | 0x100000);
	d32(dspc_token++);
	d32(arg);
}

static int dspc_send(const char *what)
{
	u32 w[0x100];

	while (dlen & 3)
		dbuf[dlen++] = 0;
	for (u32 i = 0; i < dlen / 4; i++)
		w[i] = rd32le(dbuf + 4 * i);
	puts_("DSPC ");
	puts_(what);
	puts_("\n");
	if (send_one(FCC_DSPC, w, dlen / 4))
		return -1;
	return pump(300, 0);
}

/*
 * panel config for the E6 VGA panel, FUN_80359c44 + FUN_80340d50 worked out
 * by hand from the panel database (FUN_80349e58 case 0x72 + 0x78/0x79/0x85):
 * 640x480, format 2, porches 10/20/2 and 3/3/3, polarities active high,
 * 20 MHz pixel clock, 500 ns cycle -> 2 MHz command clock.
 * id 6 (DBI) sends the shorter form with the bus cycle times instead.
 */
static int dspc_config(void)
{
	/* display index 0, token 0, no arg word: the payload follows */
	dspc_hdr(0, 3, 0);
	wr32le(dbuf + 8, 0);
	dlen -= 4;
	d16(640);
	d16(480);
	d16(2);
	d16(0);
	d16(dspc_id == 4 ? 1 : 2);
	d16(0xffff);
	if (dspc_id == 4) {
		d16(10);		/* h: front, back | pol, sync */
		d16(20);
		d16(2);
		d16(3);			/* v */
		d16(3);
		d16(3);
		d32(20000000);		/* pixel clock */
		d32(2000000);		/* 1e12 / 500000 ps */
	} else {
		d32(500000);		/* read cycle, ps */
		d32(250000);		/* write cycle */
		d32(500000);
	}
	return dspc_send("panel config");
}

static int dspc_power(u32 arg)
{
	dspc_hdr(0xffff, 2, arg);
	return dspc_send("power");
}

/* one DCS command per batch: {cmd, n data bytes} */
static int dcs(u32 cmd, const u8 *data, u32 n)
{
	u32 pad = (n + 3) & ~3u;

	dspc_hdr(0xffff, 0x40, 0);
	dlen -= 4;
	d32(1);
	d32(1);
	d32(0);
	d32(cmd);
	d32(pad);
	d32(n);
	for (u32 i = 0; i < pad; i++)
		dbuf[dlen++] = i < n ? data[i] : 0;
	puts_("DCS ");
	puthex(cmd, 2);
	puts_(" ->");
	return dspc_send("batch");
}

static int dcs1(u32 cmd, u32 v)
{
	u8 b = v;

	return dcs(cmd, &b, 1);
}

/* paint a 16x16 square straight into the panel's GRAM over DCS 0x2c */
static int gram_square(u32 rgb)
{
	static u8 px[16 * 16 * 3];
	u8 win[4] = { 0, 0, 0, 15 };

	for (u32 i = 0; i < sizeof(px); i += 3) {
		px[i] = rgb >> 16;
		px[i + 1] = rgb >> 8;
		px[i + 2] = rgb;
	}
	if (dcs(0x2a, win, 4) || dcs(0x2b, win, 4))	/* FUN_8035a284 */
		return -1;
	return dcs(0x2c, px, sizeof(px));
}

/*
 * DCS read: a type 0 record {0, token, dcs, 0, len}. when the last record
 * is a read the batch itself goes out with token 0 (FUN_80341168) and the
 * VC answers on the read's token: {token, status, len, data} (FUN_803403c0)
 */
static int dcs_read(u32 cmd, u32 n)
{
	u32 tok = dspc_token++, t0;

	dspc_hdr(0xffff, 0x40, 0);
	wr32le(dbuf + 8, 0);
	dlen -= 4;
	d32(1);
	d32(0);
	d32(tok);
	d32(cmd);
	d32(0);
	d32(n);
	puts_("read DCS ");
	puthex(cmd, 2);
	puts_(" ->");
	if (dspc_send("batch"))
		return -1;
	t0 = now_ms();
	while (now_ms() - t0 < 1000) {
		if (dspc_rx_seen && rd32le(dspc_rx) == tok)
			break;
		dspc_rx_seen = 0;
		pump(100, &dspc_rx_seen);
	}
	if (!dspc_rx_seen || rd32le(dspc_rx) != tok) {
		puts_("  no answer\n");
		return -1;
	}
	puts_("  => status ");
	puthex(rd32le(dspc_rx + 4), 8);
	puts_(" data");
	for (u32 i = 12; i < dspc_rx_len && i < 12 + n; i++) {
		puts_(" ");
		puthex(dspc_rx[i], 2);
	}
	puts_("\n");
	return 0;
}

static void panel_info(void)
{
	dcs_read(0xda, 1);	/* id1, manufacturer: FUN_8035f7d0 */
	dcs_read(0xdb, 1);	/* id2, version */
	dcs_read(0xdc, 1);	/* id3, panel */
	dcs_read(0x04, 3);	/* display id */
	dcs_read(0x0a, 1);	/* power mode: 0x9c = on, awake, display on */
	dcs_read(0x0b, 1);	/* madctl */
	dcs_read(0x0c, 1);	/* colmod */
}

/*
 * cmd 7, colour matrix (FUN_8034d6f8 -> FUN_80341a60): sent by the display
 * chipset on the first refresh after a config. header display = index 0,
 * param 0, payload 0x40 bytes from chipset +0x6bc: 3 rows x 4 words
 * (r g b coefficient, offset; 1.0 = 1 << 23, FUN_8034dabc), 3 more words,
 * then the enable word (0x800000 with a matrix, 0 without).
 * mode 'z': all zero like Symbian with no matrix set, else identity.
 */
static int colour_matrix(int zero)
{
	dspc_hdr(0, 7, 0);
	dbuf[2] = 0;			/* u16 param 0 here, not the id */
	dbuf[3] = 0;
	dlen -= 4;
	for (u32 row = 0; row < 3; row++)
		for (u32 c = 0; c < 4; c++)
			d32(!zero && c == row ? 1u << 23 : 0);
	d32(0);
	d32(0);
	d32(0);
	d32(zero ? 0 : 0x800000);
	return dspc_send(zero ? "colour matrix (zero)" : "colour matrix (identity)");
}

static int light(const char *opts, u32 rgb)
{
	int no_cfg = 0, no_pwr = 0, gram = 0, mode9 = 0, cmd6 = -1, info = 0;
	int matrix = 1, zero_matrix = 0;

	for (const char *o = opts; *o; o++) {
		no_cfg |= *o == 'n';
		no_pwr |= *o == 'p';
		gram |= *o == 'g';
		mode9 |= *o == '9';
		use_element |= *o == 'e';
		info |= *o == 'i';
		zero_matrix |= *o == 'z';
		if (*o == 'M')
			matrix = 0;
		if (*o == '0' || *o == '1')
			cmd6 = *o - '0';
		if (*o == '6')
			dspc_id = 6;
	}
	putreg("DSPC interface id ", dspc_id);
	if (vchi_open(FCC_DSPC))
		return -1;
	if (!no_cfg && dspc_config())
		return -1;
	if (!no_pwr && dspc_power(0x81))
		return -1;
	/*
	 * power mode 9 (FUN_8035fdb4 -> FUN_8034d944 -> cmd 2 arg 0x41): on the
	 * phone it put the panel to sleep (0x0a read 00 afterwards), opt-in only
	 */
	if (mode9)
		dspc_power(0x41);
	/* cmd 6 (FUN_8034d6f8, sent on refresh when the update flags change) */
	if (cmd6 >= 0) {
		dspc_hdr(0xffff, 6, cmd6);
		wr32le(dbuf + 8, 0);
		dspc_send("cmd 6");
	}
	if (info)
		panel_info();
	if (dcs(0x11, 0, 0))		/* sleep out */
		return -1;
	msleep(150);
	pump(100, 0);
	dcs1(0x3a, 0x70);		/* colmod, FUN_8035fa34 (7, nibble swapped) */
	dcs1(0x36, 0x00);		/* madctl */
	dcs(0x29, 0, 0);		/* display on */
	dcs1(0x53, 0x24);		/* bctrl: brightness ctrl + backlight on */
	dcs1(0x55, 0x00);		/* cabc off */
	dcs1(0x51, 0xff);		/* brightness max */
	if (gram)
		gram_square(rgb);
	dcs_read(0x0a, 1);
	if (matrix)
		colour_matrix(zero_matrix);
	return 0;
}

static u32 atoh(const char *s)
{
	u32 v = 0;

	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	for (; *s; s++)
		v = v << 4 | (*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
	return v;
}

void __attribute__((used)) cmain(u32 argc, char **argv)
{
	int r = 0;

	if (argc < 2) {
		puts_("usage: vcprobe dump | hello | boot DIR [ahfsmwd] | listen DIR | paint DIR [rrggbb] | light DIR [rrggbb [6npg9ie01ozM]] | timings boot|normal\n");
		die(2);
	}
	memfd = sc3(SYS_open, (u32)"/dev/mem", O_RDWR | O_SYNC, 0);
	if ((int)memfd < 0) {
		puts_("vcprobe: can't open /dev/mem\n");
		die(1);
	}
	vb = map(0x20025000);
	va = map(0x20024000);
	vg = map(0x20031000);
	vc = map(0x20033000);
	vp = map(0x20032000);

	if (streq(argv[1], "dump")) {
		dump();
	} else if (streq(argv[1], "hello")) {
		u32 type, value, arg;

		if (clock_on())
			die(1);
		vc_reset();
		r = rx_boot(&type, &value, &arg);
		if (!r) {
			putreg("hello: type/value ", type << 24 | value);
			putreg("       rom id ", arg);
		}
	} else if (streq(argv[1], "paint") && argc >= 3) {
		/* boot with loader A (the one that works), then fill the screen */
		u32 col = argc >= 4 ? atoh(argv[3]) : 0xff0000;

		if (clock_on())
			die(1);
		r = boot(argv[2], 1);
		if (!r) {
			rx_first = 0;
			r = vchi_connect();
			if (!r)
				r = paint(col >> 16 & 0xff, col >> 8 & 0xff, col & 0xff);
		}
	} else if (streq(argv[1], "light") && argc >= 3) {
		/* boot, panel config + power + DCS init/backlight, then paint */
		u32 col = argc >= 4 ? atoh(argv[3]) : 0xffffff;

		if (clock_on())
			die(1);
		r = boot(argv[2], 1);
		if (!r) {
			rx_first = 0;
			r = vchi_connect();
			/*
			 * Symbian opens DISP/UPDH at boot, long before the panel
			 * driver configures and powers the panel; do the same
			 * unless 'o' (old order) is given
			 */
			const char *lo = argc >= 5 ? argv[4] : "";
			int old_order = 0;

			for (const char *o = lo; *o; o++)
				old_order |= *o == 'o';
			if (!r && !old_order && disp_open())
				puts_("early DisplayOpen failed, retrying after DSPC\n");
			if (!r)
				r = light(lo, col);
			if (!r)
				r = paint(col >> 16 & 0xff, col >> 8 & 0xff, col & 0xff);
		}
	} else if (streq(argv[1], "listen") && argc >= 3) {
		if (clock_on())
			die(1);
		r = boot(argv[2], 1);
		if (!r)
			r = pump(5000, 0);
	} else if (streq(argv[1], "timings") && argc >= 3) {
		set_timings(streq(argv[2], "boot"));
	} else if (streq(argv[1], "boot") && argc >= 3) {
		if (clock_on())
			die(1);
		int loader_a = 0;

		for (const char *o = argc >= 4 ? argv[3] : ""; *o; o++) {
			loader_a |= *o == 'a';
			hdr_first |= *o == 'h';
			fast_timings |= *o == 'f';
			use_dma |= *o == 'd';
			pace |= *o == 's';
			dma_mode_pio |= *o == 'm';
			swap_halves |= *o == 'w';
		}
		if (use_dma)
			dmabuf = map_len(DMA_PHYS, DMA_LEN);
		r = boot(argv[2], loader_a);
	} else {
		puts_("vcprobe: unknown command\n");
		r = -1;
	}
	die(r ? 1 : 0);
}

__attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile("ldr r0, [sp]\n"
			 "add r1, sp, #4\n"
			 "bl cmain\n");
}
