// SPDX-License-Identifier: GPL-2.0-only
/*
 * physdump <phys addr> <length>: copies physical memory from /dev/mem to
 * stdout, e.g. physdump 0x10480000 0x70000 | base64. Reading RAM needs a
 * kernel without CONFIG_STRICT_DEVMEM (the N8 kernels). Used by swddump to
 * get the N8's ISA software for the secure watchdog bridge.
 *
 * It mmaps 64 KB windows instead of read(): read() goes through the kernel's
 * linear map, and on the rapuyama PHYS_OFFSET lies below the real RAM, so
 * read() of 0x10480000 passed the range check and faulted inside the kernel
 * (the N8 locked up, 24.10.2026). mmap maps the pages into this process only.
 */
#include "evtool.h"

#define SYS_mmap2 192
#define SYS_munmap 91
#define PROT_READ 1
#define MAP_SHARED 1
#define WIN 0x10000u

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

/* the kernel enters with sp -> argc, argv[0], argv[1], ... */
void __attribute__((naked)) _start(void)
{
	__asm__ volatile("mov r0, sp\n\tb main_args");
}

void main_args(u32 *sp)
{
	char **argv = (char **)(sp + 1);
	u32 addr, len;
	s32 fd;

	if (sp[0] != 3)
		fail("usage: physdump <phys addr> <length>\n");
	addr = parse(argv[1]);
	len = parse(argv[2]);
	fd = (s32)syscall3(SYS_open, (u32)"/dev/mem", O_RDONLY, 0);
	if (fd < 0)
		fail("physdump: can't open /dev/mem\n");
	if (addr & 0xfff)
		fail("physdump: address must be 4 KB aligned\n");
	while (len) {
		u32 n = len < WIN ? len : WIN;
		u32 m = (n + 0xfff) & ~0xfffu;
		/* mmap2 takes the offset in 4 KB pages */
		u32 p = syscall6(SYS_mmap2, 0, m, PROT_READ, MAP_SHARED, fd,
				 addr >> 12);

		if (p >= 0xfffff000u)
			fail("physdump: mmap failed (STRICT_DEVMEM kernel?)\n");
		syscall3(SYS_write, 1, p, n);
		syscall3(SYS_munmap, p, m, 0);
		addr += n;
		len -= n;
	}
	exit_with_status(0);
}
