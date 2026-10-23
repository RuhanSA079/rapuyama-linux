// SPDX-License-Identifier: GPL-2.0-only
/*
 * physdump <phys addr> <length>: copies physical memory from /dev/mem to
 * stdout, e.g. physdump 0x10480000 0x70000 | base64. Reading RAM needs a
 * kernel without CONFIG_STRICT_DEVMEM (the N8 kernels). Used by swddump to
 * get the N8's ISA software for the secure watchdog bridge.
 */
#include "evtool.h"

#define SYS_llseek 140
#define SEEK_SET 0

static u32 parse(const char *s)
{
	u32 v = 0, base = 10;

	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	for (; *s; s++) {
		u32 d;

		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (*s >= 'a' && *s <= 'f')
			d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'F')
			d = *s - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		v = v * base + d;
	}
	return v;
}

static void fail(const char *msg)
{
	u32 n = 0;

	while (msg[n])
		n++;
	syscall3(SYS_write, 2, (u32)msg, n);
	exit_with_status(1);
}

static u8 buf[4096];

/* the kernel enters with sp -> argc, argv[0], argv[1], ... */
void __attribute__((naked)) _start(void)
{
	__asm__ volatile("mov r0, sp\n\tb main_args");
}

void main_args(u32 *sp)
{
	char **argv = (char **)(sp + 1);
	u32 addr, len, result[2];
	s32 fd;

	if (sp[0] != 3)
		fail("usage: physdump <phys addr> <length>\n");
	addr = parse(argv[1]);
	len = parse(argv[2]);
	fd = (s32)syscall3(SYS_open, (u32)"/dev/mem", O_RDONLY, 0);
	if (fd < 0)
		fail("physdump: can't open /dev/mem\n");
	/* llseek(fd, offset high, offset low, &result, whence): 32 bit offsets
	 * above 2 GB would need it, and it is never wrong */
	{
		register u32 r0 __asm__("r0") = fd;
		register u32 r1 __asm__("r1") = 0;
		register u32 r2 __asm__("r2") = addr;
		register u32 r3 __asm__("r3") = (u32)result;
		register u32 r4 __asm__("r4") = SEEK_SET;
		register u32 r7 __asm__("r7") = SYS_llseek;

		__asm__ volatile("svc 0"
				 : "+r"(r0)
				 : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r7)
				 : "memory", "cc");
		if ((s32)r0 < 0)
			fail("physdump: seek failed\n");
	}
	while (len) {
		u32 n = len < sizeof(buf) ? len : sizeof(buf);
		s32 got = (s32)syscall3(SYS_read, fd, (u32)buf, n);

		if (got <= 0)
			fail("physdump: read failed (STRICT_DEVMEM kernel? not mapped?)\n");
		syscall3(SYS_write, 1, (u32)buf, got);
		len -= got;
	}
	exit_with_status(0);
}
