// SPDX-License-Identifier: GPL-2.0-only
/*
 * nvsget <file>: writes the WL1271 NVS that n8boot left at phys 0x1f0f6000
 * (the phone's own calibration + MAC, from wlanhwinit.dll through the
 * n8wlnv helper) to <file>, e.g. /lib/firmware/ti-connectivity/
 * wl127x-nvs.bin. Block (boot_app.cpp, all u32 little endian):
 *   +0 magic 'N8NV'  +4 version 1  +8 length  +12 crc32 (zlib)  +16 data
 * exit 0 written, 2 no block (nowlan.txt, an old n8boot, no helper result),
 * 3 bad block, 1 error. Prints the MAC with the last 3 bytes masked.
 * The area is inside the ramcon no-map reservation; like physdump it mmaps
 * /dev/mem (read() locks the N8 up).
 */
#include "evtool.h"

#define SYS_mmap2 192
#define SYS_munmap 91
#define PROT_READ 1
#define MAP_SHARED 1
#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define NVS_PHYS 0x1f0f6000u
#define NVS_MAGIC 0x564e384eu	/* 'N8NV' */
#define NVS_LEN 912u

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

static void puthex(u32 v, int digits)
{
	while (digits--)
		putc_("0123456789abcdef"[(v >> (digits * 4)) & 0xf]);
}

static void say(const char *msg)
{
	puts_("nvsget: ");
	puts_(msg);
	flush();
}

static u32 le32(const volatile u8 *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

/* zlib crc32, as n8nvs.h */
static u32 crc32(const u8 *p, u32 n)
{
	u32 c = 0xffffffffu;
	int k;

	while (n--) {
		c ^= *p++;
		for (k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
	}
	return ~c;
}

/* the kernel enters with sp -> argc, argv[0], argv[1], ... */
void __attribute__((naked)) _start(void)
{
	__asm__ volatile("mov r0, sp\n\tb main_args");
}

void main_args(u32 *sp)
{
	static u8 nvs[NVS_LEN];
	char **argv = (char **)(sp + 1);
	const volatile u8 *b;
	u32 p, i;
	s32 fd, out;

	if (sp[0] != 2) {
		say("usage: nvsget <file>\n");
		exit_with_status(1);
	}
	fd = (s32)syscall3(SYS_open, (u32)"/dev/mem", O_RDONLY, 0);
	if (fd < 0) {
		say("can't open /dev/mem\n");
		exit_with_status(1);
	}
	p = syscall6(SYS_mmap2, 0, 0x1000, PROT_READ, MAP_SHARED, (u32)fd,
		     NVS_PHYS >> 12);
	if (p >= 0xfffff000u) {
		say("mmap failed\n");
		exit_with_status(1);
	}
	b = (const volatile u8 *)p;
	if (le32(b) != NVS_MAGIC) {
		say("no NVS from n8boot\n");
		exit_with_status(2);
	}
	if (le32(b + 4) != 1 || le32(b + 8) != NVS_LEN) {
		say("unknown block version / length\n");
		exit_with_status(3);
	}
	for (i = 0; i < NVS_LEN; i++)
		nvs[i] = b[16 + i];
	if (crc32(nvs, NVS_LEN) != le32(b + 12)) {
		say("checksum mismatch\n");
		exit_with_status(3);
	}
	syscall3(SYS_munmap, p, 0x1000, 0);

	out = (s32)syscall3(SYS_open, (u32)argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0 || syscall3(SYS_write, (u32)out, (u32)nvs, NVS_LEN) != NVS_LEN) {
		say("can't write the file\n");
		exit_with_status(1);
	}
	syscall3(SYS_close, (u32)out, 0, 0);
	/* the MAC as wlcore reads it: bytes 11, 10, 6 (+ 5, 4, 3 masked) */
	puts_("nvsget: phone NVS, MAC ");
	puthex(nvs[11], 2);
	putc_(':');
	puthex(nvs[10], 2);
	putc_(':');
	puthex(nvs[6], 2);
	puts_(":xx:xx:xx -> ");
	puts_(argv[1]);
	putc_('\n');
	flush();
	exit_with_status(0);
}
