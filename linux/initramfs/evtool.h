// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared bits of the freestanding input test tools (keytest, touchtest):
 * raw ARM EABI syscalls, a few print helpers and the evdev UAPI layouts for
 * 32-bit ARM. No libc, so they run from any initramfs (busybox only or
 * alpine) and need nothing but /dev/input.
 */
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;

#define SYS_exit 1
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_ioctl 54
#define SYS_poll 168

#define O_RDONLY 0
#define POLLIN 1

static inline u32 syscall3(u32 number, u32 arg0, u32 arg1, u32 arg2)
{
	register u32 r0 __asm__("r0") = arg0;
	register u32 r1 __asm__("r1") = arg1;
	register u32 r2 __asm__("r2") = arg2;
	register u32 r7 __asm__("r7") = number;

	__asm__ volatile("svc 0"
			 : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r7)
			 : "memory", "cc");
	return r0;
}

/* gcc may emit calls to these for struct copies and zeroing */
void *memset(void *dst, int c, unsigned int n)
{
	u8 *d = dst;

	while (n--)
		*d++ = (u8)c;
	return dst;
}

void *memcpy(void *dst, const void *src, unsigned int n)
{
	u8 *d = dst;
	const u8 *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

/* libgcc's division helpers call raise() on a divide by zero */
int raise(int signal)
{
	(void)signal;
	return 0;
}

static __attribute__((noreturn)) void exit_with_status(u32 status)
{
	syscall3(SYS_exit, status, 0, 0);
	__builtin_unreachable();
}

/* --- output, line buffered so a slow USB tty gets whole lines --- */

static char out_buf[512];
static u32 out_len;

static void flush(void)
{
	if (out_len)
		syscall3(SYS_write, 1, (u32)out_buf, out_len);
	out_len = 0;
}

static void putc_(char c)
{
	if (out_len == sizeof(out_buf))
		flush();
	out_buf[out_len++] = c;
	if (c == '\n')
		flush();
}

static void puts_(const char *s)
{
	while (*s)
		putc_(*s++);
}

static void putd(s32 v)
{
	char tmp[12];
	int n = 0;
	u32 u = v < 0 ? -(u32)v : (u32)v;

	if (v < 0)
		putc_('-');
	do {
		tmp[n++] = '0' + u % 10;
		u /= 10;
	} while (u);
	while (n)
		putc_(tmp[--n]);
}

/* seconds.milliseconds of an event timestamp */
static void puttime(u32 sec, u32 usec)
{
	u32 ms = usec / 1000;

	putd(sec);
	putc_('.');
	putc_('0' + ms / 100);
	putc_('0' + ms / 10 % 10);
	putc_('0' + ms % 10);
}

/* --- evdev, 32-bit ARM layouts --- */

struct input_event {
	u32 sec;
	u32 usec;
	u16 type;
	u16 code;
	s32 value;
};

struct input_absinfo {
	s32 value;
	s32 minimum;
	s32 maximum;
	s32 fuzz;
	s32 flat;
	s32 resolution;
};

struct pollfd {
	s32 fd;
	short events;
	short revents;
};

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define EV_SW 0x05
#define SYN_REPORT 0

#define IOC_READ 2u
#define EVIOC(nr, len) ((IOC_READ << 30) | ((u32)(len) << 16) | ('E' << 8) | (nr))
#define EVIOCGNAME(len) EVIOC(0x06, len)
#define EVIOCGKEY(len) EVIOC(0x18, len)
#define EVIOCGSW(len) EVIOC(0x1b, len)
#define EVIOCGBIT(ev, len) EVIOC(0x20 + (ev), len)
#define EVIOCGABS(abs) EVIOC(0x40 + (abs), sizeof(struct input_absinfo))

#define MAX_DEVS 16 /* kept open at once */
#define MAX_NODES 32 /* event0..31 looked at */
#define BITS_LEN 96 /* bytes, covers KEY_MAX 0x2ff */

struct dev {
	s32 fd;
	u32 num;
	char name[64];
	u8 evbits[4];
	u8 keybits[BITS_LEN];
	u8 absbits[8];
	u8 swbits[4];
};

static int test_bit(const u8 *bits, u32 bit)
{
	return bits[bit / 8] >> (bit % 8) & 1;
}

/*
 * opens /dev/input/event0..31, fills the capability bits and keeps the ones
 * want() accepts. returns how many were kept
 */
static int open_devs(struct dev *devs, int (*want)(const struct dev *))
{
	char path[] = "/dev/input/eventNN";
	int n = 0;
	u32 i;

	for (i = 0; i < MAX_NODES && n < MAX_DEVS; i++) {
		struct dev *d = &devs[n];
		char *p = path + 16;
		s32 fd;

		if (i >= 10)
			*p++ = '0' + i / 10;
		*p++ = '0' + i % 10;
		*p = 0;
		fd = (s32)syscall3(SYS_open, (u32)path, O_RDONLY, 0);
		if (fd < 0)
			continue;
		memset(d, 0, sizeof(*d));
		d->fd = fd;
		d->num = i;
		syscall3(SYS_ioctl, fd, EVIOCGNAME(sizeof(d->name) - 1), (u32)d->name);
		syscall3(SYS_ioctl, fd, EVIOCGBIT(0, sizeof(d->evbits)), (u32)d->evbits);
		syscall3(SYS_ioctl, fd, EVIOCGBIT(EV_KEY, sizeof(d->keybits)), (u32)d->keybits);
		syscall3(SYS_ioctl, fd, EVIOCGBIT(EV_ABS, sizeof(d->absbits)), (u32)d->absbits);
		syscall3(SYS_ioctl, fd, EVIOCGBIT(EV_SW, sizeof(d->swbits)), (u32)d->swbits);
		if (want(d))
			n++;
		else
			syscall3(SYS_close, fd, 0, 0);
	}
	return n;
}

static void put_dev(const struct dev *d)
{
	puts_("event");
	putd(d->num);
	puts_(" \"");
	puts_(d->name);
	puts_("\"");
}

/* waits for any of the devices, returns the index of one with data */
static int wait_devs(const struct dev *devs, int n)
{
	struct pollfd pfd[MAX_DEVS];
	int i;

	for (i = 0; i < n; i++) {
		pfd[i].fd = devs[i].fd;
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;
	}
	if ((s32)syscall3(SYS_poll, (u32)pfd, n, (u32)-1) <= 0)
		return -1;
	for (i = 0; i < n; i++)
		if (pfd[i].revents)
			return i;
	return -1;
}
