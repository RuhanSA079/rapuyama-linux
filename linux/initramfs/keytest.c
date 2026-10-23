// SPDX-License-Identifier: GPL-2.0-only
/*
 * keytest: button test through the input layer (gpio-keys, keypads).
 * lists every key device with the keys it has and which of them are held
 * right now, then prints each press / release / repeat until ctrl-c:
 *
 *   12.345 event1 "gpio-keys" volume up (115) down
 *
 * touchscreens (devices with axes) are left to touchtest. keyscan.sh is the
 * level below this one: raw gpio pins, before any driver
 */
#include "evtool.h"

static const struct {
	u16 code;
	const char *name;
} key_names[] = {
	{ 1, "esc" },
	{ 14, "backspace" },
	{ 28, "enter" },
	{ 102, "home" },
	{ 103, "up" },
	{ 105, "left" },
	{ 106, "right" },
	{ 108, "down" },
	{ 113, "mute" },
	{ 114, "volume down" },
	{ 115, "volume up" },
	{ 116, "power" },
	{ 139, "menu" },
	{ 152, "screen lock" },
	{ 158, "back" },
	{ 164, "play/pause" },
	{ 169, "phone" },
	{ 172, "homepage (menu key)" },
	{ 212, "camera" },
	{ 352, "ok" },
	{ 528, "camera focus" },
};

static void put_key(u16 code)
{
	u32 i;

	for (i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
		if (key_names[i].code == code) {
			puts_(key_names[i].name);
			puts_(" (");
			putd(code);
			putc_(')');
			return;
		}
	}
	puts_("key ");
	putd(code);
}

static int want(const struct dev *d)
{
	return (test_bit(d->evbits, EV_KEY) || test_bit(d->evbits, EV_SW)) &&
	       !test_bit(d->evbits, EV_ABS);
}

static void list_dev(const struct dev *d)
{
	u8 held[BITS_LEN] = { 0 };
	u8 sw[4] = { 0 };
	u32 k;

	syscall3(SYS_ioctl, d->fd, EVIOCGKEY(sizeof(held)), (u32)held);
	syscall3(SYS_ioctl, d->fd, EVIOCGSW(sizeof(sw)), (u32)sw);
	put_dev(d);
	puts_(":\n");
	for (k = 1; k < BITS_LEN * 8; k++) {
		if (!test_bit(d->keybits, k))
			continue;
		puts_("  ");
		put_key(k);
		puts_(test_bit(held, k) ? "  HELD\n" : "\n");
	}
	for (k = 0; k < 32; k++) {
		if (!test_bit(d->swbits, k))
			continue;
		puts_("  switch ");
		putd(k);
		puts_(test_bit(sw, k) ? "  ON\n" : "  off\n");
	}
}

void _start(void)
{
	struct dev devs[MAX_DEVS];
	int n, i;

	default_signals();
	n = open_devs(devs, want);
	if (!n) {
		puts_("keytest: no key devices in /dev/input (is gpio-keys in the dtb?)\n");
		exit_with_status(1);
	}
	for (i = 0; i < n; i++)
		list_dev(&devs[i]);
	puts_("press keys, ctrl-c to stop\n");

	for (;;) {
		struct input_event ev[16];
		s32 got, j;

		i = wait_devs(devs, n);
		if (i < 0)
			continue;
		got = (s32)syscall3(SYS_read, devs[i].fd, (u32)ev, sizeof(ev));
		if (got <= 0) {
			put_dev(&devs[i]);
			puts_(": read failed, gone?\n");
			exit_with_status(1);
		}
		for (j = 0; j < got / (s32)sizeof(ev[0]); j++) {
			if (ev[j].type != EV_KEY && ev[j].type != EV_SW)
				continue;
			puttime(ev[j].sec, ev[j].usec);
			putc_(' ');
			put_dev(&devs[i]);
			putc_(' ');
			if (ev[j].type == EV_SW) {
				puts_("switch ");
				putd(ev[j].code);
				puts_(ev[j].value ? " on\n" : " off\n");
				continue;
			}
			put_key(ev[j].code);
			puts_(ev[j].value == 1 ? " down\n" :
			      ev[j].value == 2 ? " repeat\n" : " up\n");
		}
	}
}
