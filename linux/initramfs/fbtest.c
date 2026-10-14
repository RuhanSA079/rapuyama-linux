// SPDX-License-Identifier: GPL-2.0-only
typedef unsigned int u32;
typedef unsigned char u8;
typedef unsigned short u16;

/* Freestanding copy of the fbdev UAPI layouts for the ARM 32-bit ABI. */
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

_Static_assert(sizeof(struct fb_fix_screeninfo) == 68,
	       "unexpected ARM fb_fix_screeninfo layout");
_Static_assert(sizeof(struct fb_var_screeninfo) == 160,
	       "unexpected ARM fb_var_screeninfo layout");

#define SYS_open 5
#define SYS_write 4
#define SYS_close 6
#define SYS_ioctl 54
#define SYS_munmap 91
#define SYS_mmap2 192
#define SYS_exit 1
#define O_RDWR 2
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

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

static void report(const char *message, u32 length)
{
	syscall3(SYS_write, 2, (u32)message, length);
}

static __attribute__((noreturn)) void exit_with_status(u32 status)
{
	syscall3(SYS_exit, status, 0, 0);
	__builtin_unreachable();
}

int raise(int signal)
{
	(void)signal;
	exit_with_status(1);
}

static u32 pack_component(u8 component, struct fb_bitfield field)
{
	u32 max;

	if (!field.length)
		return 0;
	if (field.length > 8)
		return 0;

	max = (1U << field.length) - 1;
	return ((component * max + 127) / 255) << field.offset;
}

static u32 pack_pixel(u8 red, u8 green, u8 blue,
		      const struct fb_var_screeninfo *var)
{
	return pack_component(red, var->red) |
	       pack_component(green, var->green) |
	       pack_component(blue, var->blue) |
	       pack_component(255, var->transp);
}

void _start(void)
{
	static const u8 bars[][3] = {
		{ 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 },
		{ 0, 255, 0 }, { 255, 0, 255 }, { 255, 0, 0 },
		{ 0, 0, 255 }, { 0, 0, 0 },
	};
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	u32 required;
	u32 pixel_bytes;
	u32 fd;
	u32 pixels;

	fd = syscall3(SYS_open, (u32)"/dev/fb0", O_RDWR, 0);
	if ((int)fd < 0) {
		report("fbtest: open /dev/fb0 failed\n",
		       sizeof("fbtest: open /dev/fb0 failed\n") - 1);
		exit_with_status(1);
	}
	if ((int)syscall3(SYS_ioctl, fd, FBIOGET_FSCREENINFO, (u32)&fix) < 0 ||
	    (int)syscall3(SYS_ioctl, fd, FBIOGET_VSCREENINFO, (u32)&var) < 0) {
		report("fbtest: framebuffer info ioctl failed\n",
		       sizeof("fbtest: framebuffer info ioctl failed\n") - 1);
		syscall3(SYS_close, fd, 0, 0);
		exit_with_status(1);
	}

	if (!var.xres || !var.yres ||
	    (var.bits_per_pixel != 16 && var.bits_per_pixel != 32)) {
		report("fbtest: unsupported framebuffer mode\n",
		       sizeof("fbtest: unsupported framebuffer mode\n") - 1);
		syscall3(SYS_close, fd, 0, 0);
		exit_with_status(1);
	}

	pixel_bytes = var.bits_per_pixel / 8;
	if (!fix.line_length || var.xres > fix.line_length / pixel_bytes ||
	    var.yres > fix.smem_len / fix.line_length) {
		report("fbtest: invalid framebuffer geometry\n",
		       sizeof("fbtest: invalid framebuffer geometry\n") - 1);
		syscall3(SYS_close, fd, 0, 0);
		exit_with_status(1);
	}
	required = fix.line_length * var.yres;
	pixels = syscall6(SYS_mmap2, 0, required, PROT_READ | PROT_WRITE,
			  MAP_SHARED, fd, 0);
	if (pixels >= (u32)-4095) {
		report("fbtest: mmap /dev/fb0 failed\n",
		       sizeof("fbtest: mmap /dev/fb0 failed\n") - 1);
		syscall3(SYS_close, fd, 0, 0);
		exit_with_status(1);
	}

	for (u32 y = 0; y < var.yres; y++) {
		u8 *row = (u8 *)(pixels + y * fix.line_length);

		for (u32 x = 0; x < var.xres; x++) {
			u32 bar = (x * 8) / var.xres;
			u32 pixel = pack_pixel(bars[bar][0], bars[bar][1],
					       bars[bar][2], &var);

			if (x == 0 || x == var.xres - 1 ||
			    y == 0 || y == var.yres - 1)
				pixel = pack_pixel(255, 255, 255, &var);
			for (u32 byte = 0; byte < pixel_bytes; byte++)
				row[x * pixel_bytes + byte] =
					(u8)(pixel >> (byte * 8));
		}
	}

	__asm__ volatile("" ::: "memory");
	syscall3(SYS_munmap, pixels, required, 0);
	syscall3(SYS_close, fd, 0, 0);
	report("fbtest: wrote color bars to /dev/fb0\n",
	       sizeof("fbtest: wrote color bars to /dev/fb0\n") - 1);
	exit_with_status(0);
}
