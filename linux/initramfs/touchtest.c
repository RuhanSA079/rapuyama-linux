// SPDX-License-Identifier: GPL-2.0-only
/*
 * touchtest: touchscreen test through the input layer. prints the axis
 * ranges the driver reports, then one line per frame while fingers move:
 *
 *   3.120 2 down  #0 (412,388)  #1 (102,901)
 *
 * and on the last finger up the x/y range that was really touched. drag a
 * finger along all four edges: that range against the reported one shows if
 * the axes are swapped or mirrored against the panel (360x640 on the n8).
 * multitouch (type B slots) and single touch (ABS_X/Y) both work. ctrl-c
 * to stop
 */
#include "evtool.h"

#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_PRESSURE 0x18
#define ABS_MT_SLOT 0x2f
#define ABS_MT_TOUCH_MAJOR 0x30
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
#define ABS_MT_PRESSURE 0x3a
#define BTN_TOUCH 0x14a

#define SLOTS 10

struct slot {
	s32 id; /* -1 = no finger */
	s32 x, y;
};

static struct slot slots[SLOTS];
static u32 cur_slot;
static int is_mt;
static s32 seen_x0, seen_x1, seen_y0, seen_y1, seen_any;
static int was_down;

static int want(const struct dev *d)
{
	return test_bit(d->evbits, EV_ABS) &&
	       (test_bit(d->absbits, ABS_MT_POSITION_X) || test_bit(d->absbits, ABS_X));
}

static void put_range(const struct dev *d, u32 abs, const char *label)
{
	struct input_absinfo a;

	if (!test_bit(d->absbits, abs))
		return;
	memset(&a, 0, sizeof(a));
	if ((s32)syscall3(SYS_ioctl, d->fd, EVIOCGABS(abs), (u32)&a) < 0)
		return;
	puts_("  ");
	puts_(label);
	puts_(" ");
	putd(a.minimum);
	puts_("..");
	putd(a.maximum);
	if (a.resolution) {
		puts_(", ");
		putd(a.resolution);
		puts_(" units/mm");
	}
	putc_('\n');
}

static void note(s32 x, s32 y)
{
	if (!seen_any) {
		seen_x0 = seen_x1 = x;
		seen_y0 = seen_y1 = y;
		seen_any = 1;
	}
	if (x < seen_x0)
		seen_x0 = x;
	if (x > seen_x1)
		seen_x1 = x;
	if (y < seen_y0)
		seen_y0 = y;
	if (y > seen_y1)
		seen_y1 = y;
}

static void frame(const struct input_event *ev)
{
	int down = 0;
	u32 s;

	for (s = 0; s < SLOTS; s++)
		if (slots[s].id >= 0)
			down++;
	if (!down && !was_down)
		return;
	puttime(ev->sec, ev->usec);
	putc_(' ');
	putd(down);
	puts_(down ? " down" : " up");
	for (s = 0; s < SLOTS; s++) {
		if (slots[s].id < 0)
			continue;
		note(slots[s].x, slots[s].y);
		puts_("  #");
		putd(s);
		puts_(" (");
		putd(slots[s].x);
		putc_(',');
		putd(slots[s].y);
		putc_(')');
	}
	putc_('\n');
	if (!down && seen_any) {
		puts_("  touched so far: x ");
		putd(seen_x0);
		puts_("..");
		putd(seen_x1);
		puts_("  y ");
		putd(seen_y0);
		puts_("..");
		putd(seen_y1);
		putc_('\n');
	}
	was_down = down;
}

static void handle(const struct input_event *ev)
{
	struct slot *sl = &slots[cur_slot];

	if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
		frame(ev);
		return;
	}
	if (ev->type == EV_KEY && ev->code == BTN_TOUCH && !is_mt) {
		/* single touch: one pseudo slot that follows BTN_TOUCH */
		slots[0].id = ev->value ? 0 : -1;
		return;
	}
	if (ev->type != EV_ABS)
		return;
	switch (ev->code) {
	case ABS_MT_SLOT:
		if ((u32)ev->value < SLOTS)
			cur_slot = ev->value;
		break;
	case ABS_MT_TRACKING_ID:
		sl->id = ev->value;
		break;
	case ABS_MT_POSITION_X:
		sl->x = ev->value;
		break;
	case ABS_MT_POSITION_Y:
		sl->y = ev->value;
		break;
	case ABS_X:
		if (!is_mt)
			slots[0].x = ev->value;
		break;
	case ABS_Y:
		if (!is_mt)
			slots[0].y = ev->value;
		break;
	}
}

void _start(void)
{
	struct dev devs[MAX_DEVS];
	int n, i;
	u32 s;

	n = open_devs(devs, want);
	if (!n) {
		puts_("touchtest: no touchscreen in /dev/input (did the mxt probe? dmesg | grep -i mxt)\n");
		exit_with_status(1);
	}
	if (n > 1) {
		puts_("touchtest: more than one touch device, using the first\n");
		for (i = 1; i < n; i++)
			syscall3(SYS_close, devs[i].fd, 0, 0);
	}
	is_mt = test_bit(devs[0].absbits, ABS_MT_POSITION_X);
	put_dev(&devs[0]);
	puts_(is_mt ? ", multitouch\n" : ", single touch\n");
	put_range(&devs[0], ABS_MT_POSITION_X, "mt x");
	put_range(&devs[0], ABS_MT_POSITION_Y, "mt y");
	put_range(&devs[0], ABS_MT_SLOT, "slots");
	put_range(&devs[0], ABS_MT_TOUCH_MAJOR, "touch size");
	put_range(&devs[0], ABS_MT_PRESSURE, "mt pressure");
	put_range(&devs[0], ABS_X, "x");
	put_range(&devs[0], ABS_Y, "y");
	put_range(&devs[0], ABS_PRESSURE, "pressure");
	for (s = 0; s < SLOTS; s++)
		slots[s].id = -1;
	puts_("touch the screen, drag along all four edges, ctrl-c to stop\n");

	for (;;) {
		struct input_event ev[32];
		s32 got, j;

		if (wait_devs(devs, 1) < 0)
			continue;
		got = (s32)syscall3(SYS_read, devs[0].fd, (u32)ev, sizeof(ev));
		if (got <= 0) {
			puts_("touchtest: read failed, device gone?\n");
			exit_with_status(1);
		}
		for (j = 0; j < got / (s32)sizeof(ev[0]); j++)
			handle(&ev[j]);
	}
}
