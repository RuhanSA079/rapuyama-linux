// SPDX-License-Identifier: GPL-2.0-only
/*
 * vadump [-m] <l1 phys> <va> <length>: copies a range of Symbian's virtual
 * memory (as it was mapped at the handoff) to stdout. Read only.
 *
 * The N8 loader saves Symbian's 16 KB kernel L1 table (TTBR1, VA >= 0x80000000
 * with TTBCR = 1) in the handoff block: isah phys 0x1f0f1000 + 0x1000 =
 * 0x1f0f2000. vadump walks it like the MMU: section (1 MB), or coarse L2
 * (1 KB table) -> large (64 KB) / small (4 KB) page. Fine L2 tables and
 * supersections are reported and skipped.
 *
 * Safety: only physical addresses in the RAM range 0x10000000..0x1fffffff are
 * ever mapped (other addresses could be device or secure space, a bus abort
 * locks the N8 up). Pages that are unmapped or outside RAM are written as
 * zeros so offsets stay right. Like physdump it mmaps /dev/mem (read() locks
 * the N8 up) and needs a kernel without CONFIG_STRICT_DEVMEM.
 *
 * -m: print the translation of every 4 KB page instead of the data:
 *   va <va> pa <pa> <kind>      kind = section / large / small
 *   va <va> -- <reason>
 */
#include "evtool.h"

#define SYS_mmap2 192
#define SYS_munmap 91
#define PROT_READ 1
#define MAP_SHARED 1
#define PAGE 0x1000u
#define RAM_START 0x10000000u
#define RAM_END 0x20000000u

static s32 fd;
static u8 zero[PAGE];

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

static void puthex(u32 v, int digits)
{
	while (digits--)
		putc_("0123456789abcdef"[(v >> (digits * 4)) & 0xf]);
}

static int in_ram(u32 pa)
{
	return pa >= RAM_START && pa < RAM_END;
}

/* the 4 KB page at pa (in RAM), mapped read only; 0 on failure */
static u32 map_page(u32 pa)
{
	u32 p;

	if (!in_ram(pa))
		return 0;
	p = syscall6(SYS_mmap2, 0, PAGE, PROT_READ, MAP_SHARED, (u32)fd,
		     pa >> 12);
	return p >= 0xfffff000u ? 0 : p;
}

static void unmap_page(u32 p)
{
	syscall3(SYS_munmap, p, PAGE, 0);
}

/* one word of physical memory; 0 and *ok = 0 if it can't be read */
static u32 phys_word(u32 pa, int *ok)
{
	u32 p = map_page(pa & ~(PAGE - 1)), v;

	*ok = p != 0;
	if (!p)
		return 0;
	v = *(volatile u32 *)(p + (pa & (PAGE - 1)));
	unmap_page(p);
	return v;
}

/* VA -> PA through the saved L1; returns 0 and sets *why when unmapped */
static u32 translate(u32 l1, u32 va, const char **kind, const char **why)
{
	int ok;
	u32 e = phys_word(l1 + 4 * (va >> 20), &ok), l2, s;

	*why = 0;
	if (!ok) {
		*why = "l1 copy not readable";
		return 0;
	}
	switch (e & 3) {
	case 2:
		if (e & (1u << 18)) {
			*why = "supersection, not handled";
			return 0;
		}
		*kind = "section";
		return (e & 0xfff00000u) | (va & 0xfffffu);
	case 1:
		l2 = (e & 0xfffffc00u) + 4 * ((va >> 12) & 0xff);
		s = phys_word(l2, &ok);
		if (!ok) {
			*why = "l2 table outside ram";
			return 0;
		}
		if ((s & 3) == 0) {
			*why = "l2 fault (unmapped)";
			return 0;
		}
		if ((s & 3) == 1) {
			*kind = "large";
			return (s & 0xffff0000u) | (va & 0xffffu);
		}
		*kind = "small";
		return (s & 0xfffff000u) | (va & 0xfffu);
	case 3:
		*why = "fine l2, not handled";
		return 0;
	default:
		*why = "l1 fault (unmapped)";
		return 0;
	}
}

/* the kernel enters with sp -> argc, argv[0], argv[1], ... */
void __attribute__((naked)) _start(void)
{
	__asm__ volatile("mov r0, sp\n\tb main_args");
}

void main_args(u32 *sp)
{
	char **argv = (char **)(sp + 1);
	int map_only = 0, a = 1;
	u32 l1, va, len;

	default_signals();
	if (sp[0] == 5 && argv[1][0] == '-' && argv[1][1] == 'm') {
		map_only = 1;
		a = 2;
	} else if (sp[0] != 4) {
		fail("usage: vadump [-m] <l1 phys> <va> <length>\n");
	}
	l1 = parse(argv[a]);
	va = parse(argv[a + 1]);
	len = parse(argv[a + 2]);
	if ((l1 & 0x3fff) || !in_ram(l1))
		fail("vadump: the l1 copy must be 16 KB aligned and in ram\n");
	if ((va | len) & (PAGE - 1))
		fail("vadump: va and length must be 4 KB aligned\n");
	fd = (s32)syscall3(SYS_open, (u32)"/dev/mem", O_RDONLY, 0);
	if (fd < 0)
		fail("vadump: can't open /dev/mem\n");

	for (; len; va += PAGE, len -= PAGE) {
		const char *kind = "", *why;
		u32 pa = translate(l1, va, &kind, &why), p = 0;

		if (!why && !in_ram(pa))
			why = "page outside ram, not read";
		if (map_only) {
			puts_("va ");
			puthex(va, 8);
			if (why) {
				puts_(" -- ");
				puts_(why);
			} else {
				puts_(" pa ");
				puthex(pa, 8);
				putc_(' ');
				puts_(kind);
			}
			putc_('\n');
			continue;
		}
		if (!why)
			p = map_page(pa & ~(PAGE - 1));
		syscall3(SYS_write, 1, p ? p : (u32)zero, PAGE);
		if (p)
			unmap_page(p);
	}
	flush();
	exit_with_status(0);
}
