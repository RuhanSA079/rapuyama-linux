// SPDX-License-Identifier: GPL-2.0-only
/*
 * fbtouch [seconds]: graphical touchscreen check on /dev/fb0. Coloured boxes
 * mark the corners (top left red, top right green, bottom left blue, bottom
 * right yellow); every touch leaves a white dot where the driver puts it,
 * scaled from the touch axis ranges to the screen. Touch a box: the dot has
 * to land under the finger. If it lands in another corner, that corner says
 * which axis is swapped or mirrored. Raw and screen coordinates also go to
 * stdout. Stop weston first (killall weston); the console is switched to
 * graphics mode while it runs. Ends after [seconds] (default 60) or ctrl-c.
 */
#include "evtool.h"

#define SYS_mmap2 192
#define SYS_munmap 91
#define SYS_clock_gettime 263
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define O_RDWR 2
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define KDSETMODE 0x4b3a
#define KD_TEXT 0
#define KD_GRAPHICS 1
#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_MT_SLOT 0x2f
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
#define BOX 48

struct fb_bitfield {
	u32 offset;
	u32 length;
	u32 msb_right;
};
struct fb_fix_screeninfo {
	char id[16];
	unsigned long smem_start;
	u32 smem_len;
	u32 type;
	u32 type_aux;
	u32 visual;
	u16 xpanstep;
	u16 ypanstep;
	u16 ywrapstep;
	u32 line_length;
	unsigned long mmio_start;
	u32 mmio_len;
	u32 accel;
	u16 capabilities;
	u16 reserved[2];
};
struct fb_var_screeninfo {
	u32 xres;
	u32 yres;
	u32 xres_virtual;
	u32 yres_virtual;
	u32 xoffset;
	u32 yoffset;
	u32 bits_per_pixel;
	u32 grayscale;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
	u32 nonstd;
	u32 activate;
	u32 height;
	u32 width;
	u32 accel_flags;
	u32 pixclock;
	u32 left_margin;
	u32 right_margin;
	u32 upper_margin;
	u32 lower_margin;
	u32 hsync_len;
	u32 vsync_len;
	u32 sync;
	u32 vmode;
	u32 rotate;
	u32 colorspace;
	u32 reserved[4];
};


static inline u32 syscall6(u32 number, u32 arg0, u32 arg1, u32 arg2,
			   u32 arg3, u32 arg4, u32 arg5)
{
	register u32 r0 __asm__("r0") = arg0;
	register u32 r1 __asm__("r1") = arg1;
	register u32 r2 __asm__("r2") = arg2;
	register u32 r3 __asm__("r3") = arg3;
	register u32 r4 __asm__("r4") = arg4;
	register u32 r5 __asm__("r5") = arg5;
	register u32 r7 __asm__("r7") = number;

	__asm__ volatile("svc 0"
			 : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5),
			   "r"(r7)
			 : "memory", "cc");
	return r0;
}

static u8 *fb;
static u32 fb_w, fb_h, fb_pitch, fb_bpp;
static s32 tty = -1;

static u32 now_ms(void)
{
	u32 ts[2];

	syscall3(SYS_clock_gettime, 1 /* CLOCK_MONOTONIC */, (u32)ts, 0);
	return ts[0] * 1000 + ts[1] / 1000000;
}

/* colour as 0xRRGGBB, written as RGB565 or XRGB8888 */
static void pixel(u32 x, u32 y, u32 c)
{
	if (x >= fb_w || y >= fb_h)
		return;
	if (fb_bpp == 16)
		*(u16 *)(fb + y * fb_pitch + 2 * x) =
			(c >> 8 & 0xf800) | (c >> 5 & 0x07e0) | (c >> 3 & 0x1f);
	else
		*(u32 *)(fb + y * fb_pitch + 4 * x) = c;
}

static void rect(u32 x, u32 y, u32 w, u32 h, u32 c)
{
	u32 i, j;

	for (j = y; j < y + h; j++)
		for (i = x; i < x + w; i++)
			pixel(i, j, c);
}

static void dot(s32 x, s32 y)
{
	rect(x < 4 ? 0 : x - 4, y < 4 ? 0 : y - 4, 9, 9, 0xffffff);
}

static void draw_frame(void)
{
	rect(0, 0, fb_w, fb_h, 0x202833);
	rect(0, 0, fb_w, 1, 0x808080);
	rect(0, fb_h - 1, fb_w, 1, 0x808080);
	rect(0, 0, 1, fb_h, 0x808080);
	rect(fb_w - 1, 0, 1, fb_h, 0x808080);
	rect(0, 0, BOX, BOX, 0xff0000);			/* top left */
	rect(fb_w - BOX, 0, BOX, BOX, 0x00ff00);		/* top right */
	rect(0, fb_h - BOX, BOX, BOX, 0x0000ff);		/* bottom left */
	rect(fb_w - BOX, fb_h - BOX, BOX, BOX, 0xffff00);	/* bottom right */
	rect(fb_w / 2 - 20, fb_h / 2, 41, 1, 0x808080);	/* centre cross */
	rect(fb_w / 2, fb_h / 2 - 20, 1, 41, 0x808080);
}

static void text_mode(void)
{
	if (tty >= 0)
		syscall3(SYS_ioctl, tty, KDSETMODE, KD_TEXT);
}

static int want(const struct dev *d)
{
	return test_bit(d->evbits, EV_ABS) &&
	       (test_bit(d->absbits, ABS_MT_POSITION_X) || test_bit(d->absbits, ABS_X));
}

static s32 range(const struct dev *d, u32 abs, s32 *min)
{
	struct input_absinfo a;

	memset(&a, 0, sizeof(a));
	syscall3(SYS_ioctl, d->fd, EVIOCGABS(abs), (u32)&a);
	*min = a.minimum;
	return a.maximum > a.minimum ? a.maximum - a.minimum : 1;
}

static u32 parse_secs(const char *s)
{
	u32 v = 0;

	while (*s >= '0' && *s <= '9')
		v = v * 10 + (u32)(*s++ - '0');
	return v ? v : 60;
}

void __attribute__((naked)) _start(void)
{
	__asm__ volatile("mov r0, sp\n\tb fbtouch_main");
}

void fbtouch_main(u32 *sp)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct dev devs[MAX_DEVS];
	struct pollfd pfd;
	s32 fd, n, xmin, ymin, xr, yr, x = 0, y = 0, down = 0, mt;
	u32 secs = sp[0] > 1 ? parse_secs(((char **)(sp + 1))[1]) : 60;
	u32 end, slot = 0;

	default_signals();
	fd = (s32)syscall3(SYS_open, (u32)"/dev/fb0", O_RDWR, 0);
	if (fd < 0 ||
	    (s32)syscall3(SYS_ioctl, fd, FBIOGET_VSCREENINFO, (u32)&var) < 0 ||
	    (s32)syscall3(SYS_ioctl, fd, FBIOGET_FSCREENINFO, (u32)&fix) < 0) {
		puts_("fbtouch: no /dev/fb0\n");
		exit_with_status(1);
	}
	fb_w = var.xres;
	fb_h = var.yres;
	fb_bpp = var.bits_per_pixel;
	fb_pitch = fix.line_length;
	fb = (u8 *)syscall6(SYS_mmap2, 0, fb_pitch * fb_h, PROT_READ | PROT_WRITE,
			    MAP_SHARED, fd, 0);
	if ((u32)fb >= 0xfffff000u || (fb_bpp != 16 && fb_bpp != 32)) {
		puts_("fbtouch: can't map /dev/fb0 (or not 16/32 bpp)\n");
		exit_with_status(1);
	}
	n = open_devs(devs, want);
	if (!n) {
		puts_("fbtouch: no touchscreen in /dev/input\n");
		exit_with_status(1);
	}
	mt = test_bit(devs[0].absbits, ABS_MT_POSITION_X);
	xr = range(&devs[0], mt ? ABS_MT_POSITION_X : ABS_X, &xmin);
	yr = range(&devs[0], mt ? ABS_MT_POSITION_Y : ABS_Y, &ymin);
	tty = (s32)syscall3(SYS_open, (u32)"/dev/tty1", O_RDWR, 0);
	if (tty >= 0)
		syscall3(SYS_ioctl, tty, KDSETMODE, KD_GRAPHICS);
	draw_frame();
	puts_("fbtouch: ");
	putd(fb_w);
	putc_('x');
	putd(fb_h);
	puts_(", touch x ");
	putd(xmin);
	puts_("..");
	putd(xmin + xr);
	puts_(" y ");
	putd(ymin);
	puts_("..");
	putd(ymin + yr);
	puts_("\nboxes: top left red, top right green, bottom left blue, bottom right yellow\n");

	end = now_ms() + secs * 1000;
	while ((s32)(end - now_ms()) > 0) {
		struct input_event ev[32];
		s32 got, j;

		pfd.fd = devs[0].fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if ((s32)syscall3(SYS_poll, (u32)&pfd, 1, 200) <= 0)
			continue;
		got = (s32)syscall3(SYS_read, devs[0].fd, (u32)ev, sizeof(ev));
		if (got <= 0)
			break;
		for (j = 0; j < got / (s32)sizeof(ev[0]); j++) {
			struct input_event *e = &ev[j];

			if (e->type == EV_ABS) {
				if (e->code == ABS_MT_SLOT)
					slot = e->value;
				else if (slot == 0 && e->code == ABS_MT_TRACKING_ID)
					down = e->value >= 0;
				else if (slot == 0 && (e->code == ABS_MT_POSITION_X || (!mt && e->code == ABS_X)))
					x = e->value;
				else if (slot == 0 && (e->code == ABS_MT_POSITION_Y || (!mt && e->code == ABS_Y)))
					y = e->value;
			} else if (e->type == EV_KEY && e->code == 0x14a && !mt) {
				down = e->value;
			} else if (e->type == EV_SYN && e->code == SYN_REPORT && down) {
				s32 sx = (x - xmin) * (s32)(fb_w - 1) / xr;
				s32 sy = (y - ymin) * (s32)(fb_h - 1) / yr;

				dot(sx, sy);
				puts_("touch (");
				putd(x);
				putc_(',');
				putd(y);
				puts_(") -> screen (");
				putd(sx);
				putc_(',');
				putd(sy);
				puts_(")\n");
			}
		}
	}
	text_mode();
	exit_with_status(0);
}
