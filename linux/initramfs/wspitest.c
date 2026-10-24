// SPDX-License-Identifier: GPL-2.0-only
/*
 * wspitest <spidev> [hz]: reads the TI WL1271 chip id over WSPI through
 * spidev, the same steps wlcore does before it loads any firmware
 * (wl12xx_set_power_on + wl12xx_identify_chip):
 *
 *   1. reset: 8 bytes of 0xff                        (wl12xx_spi_reset)
 *   2. WSPI init command with crc7, then 4 bytes of
 *      0xff with the chip select inverted            (wl12xx_spi_init)
 *   3. boot partition: 8 writes to 0x1ffc0..0x1ffdc  (wlcore_set_partition)
 *   4. ELP wake: write 1 to 0x1fffc                  (wlcore_fw_wakeup)
 *   5. read 4 bytes at 0x1a634 = REG_CHIP_ID_B       (wl12xx 0x305674
 *      through the boot partition)                    expect 0x04030111
 *
 * Writes only go to the chip's own partition / sleep registers, nothing is
 * flashed or calibrated. The enable line (genio100) must already be high and
 * the 19.2 MHz reference clock running (WLAN on in Symbian before the jump).
 *
 * WSPI words are 32 bit, MSB first on the wire. This tool uses 8 bit spidev
 * transfers and builds the byte order itself, so the controller's 32 bit
 * handling is not involved. Prints every byte it gets back.
 *
 * Build: arm-linux-gnu-gcc -march=armv6k -marm -static -nostdlib -O2 \
 *          -I linux/initramfs -o wspitest work/spi-n8/wspitest.c
 */
#include "evtool.h"

#define SYS_nanosleep 162
#define O_RDWR 2

/* spidev UAPI, 32-bit ARM */
struct spi_ioc_transfer {
	u32 tx_buf, tx_buf_hi;
	u32 rx_buf, rx_buf_hi;
	u32 len;
	u32 speed_hz;
	u16 delay_usecs;
	u8 bits_per_word;
	u8 cs_change;
	u8 tx_nbits;
	u8 rx_nbits;
	u8 word_delay_usecs;
	u8 pad;
};

#define SPI_IOC_MESSAGE_1 0x40206b00u	/* _IOW('k', 0, char[32]) */
#define SPI_IOC_WR_MODE 0x40016b01u
#define SPI_IOC_WR_BITS 0x40016b03u
#define SPI_IOC_WR_SPEED 0x40046b04u
#define SPI_CS_HIGH 0x04

#define WSPI_READ 0x40000000u
#define WSPI_LEN(n) (((u32)(n) << 17) & 0x1ffe0000u)
#define WSPI_ADDR(a) ((u32)(a) & 0x1ffffu)

#define CHIP_ID_ADDR 0x1a634u	/* 0x14fc0 + 0x5674 */

static s32 fd;
static u32 hz = 1000000;
static u8 txb[64], rxb[64];

static void fail(const char *msg)
{
	puts_(msg);
	flush();
	exit_with_status(1);
}

static void puthex(u32 v, int digits)
{
	while (digits--)
		putc_("0123456789abcdef"[(v >> (digits * 4)) & 0xf]);
}

static u32 parse(const char *s)
{
	u32 v = 0;

	for (; *s >= '0' && *s <= '9'; s++)
		v = v * 10 + (u32)(*s - '0');
	return v;
}

static void msleep(u32 ms)
{
	u32 ts[2] = { ms / 1000, (ms % 1000) * 1000000 };

	syscall3(SYS_nanosleep, (u32)ts, 0, 0);
}

static void set_mode(u8 mode)
{
	if ((s32)syscall3(SYS_ioctl, fd, SPI_IOC_WR_MODE, (u32)&mode) < 0)
		fail("wspitest: can't set the spi mode\n");
}

/* one chip select assertion, len bytes out of txb, into rxb */
static void xfer(const char *what, u32 len)
{
	struct spi_ioc_transfer t = { 0 };
	u32 i;
	s32 r;

	t.tx_buf = (u32)txb;
	t.rx_buf = (u32)rxb;
	t.len = len;
	t.speed_hz = hz;
	t.bits_per_word = 8;
	r = (s32)syscall3(SYS_ioctl, fd, SPI_IOC_MESSAGE_1, (u32)&t);
	puts_(what);
	if (r < 0) {
		puts_(": ioctl failed ");
		putd(r);
		putc_('\n');
		exit_with_status(1);
	}
	puts_(" rx:");
	for (i = 0; i < len; i++) {
		putc_(' ');
		puthex(rxb[i], 2);
	}
	putc_('\n');
}

static void put_be32(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static u32 get_be32(const u8 *p)
{
	return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

/* the kernel's crc7_be: crc in bits 7..1 */
static u8 crc7_be(const u8 *p, u32 len)
{
	u8 crc = 0;
	int b;

	while (len--) {
		crc ^= *p++;
		for (b = 0; b < 8; b++)
			crc = crc & 0x80 ? (u8)(crc << 1) ^ 0x12 : (u8)(crc << 1);
	}
	return crc;
}

static void write32(u32 addr, u32 val)
{
	put_be32(txb, WSPI_LEN(4) | WSPI_ADDR(addr));
	put_be32(txb + 4, val);
	xfer("  write", 8);
}

void __attribute__((naked)) _start(void)
{
	__asm__ volatile("mov r0, sp\n\tb main_args");
}

void main_args(u32 *sp)
{
	static const u32 part[8][2] = {
		{ 0x1ffc4, 0x00040000 }, { 0x1ffc0, 0x00014fc0 },
		{ 0x1ffcc, 0x00300000 }, { 0x1ffc8, 0x00008800 },
		{ 0x1ffd4, 0 }, { 0x1ffd0, 0 }, { 0x1ffdc, 0 }, { 0x1ffd8, 0 },
	};
	char **argv = (char **)(sp + 1);
	u8 bits = 8;
	u32 i, id = 0;
	int found = -1;

	default_signals();
	if (sp[0] < 2)
		fail("usage: wspitest /dev/spidevB.C [hz]\n");
	if (sp[0] > 2)
		hz = parse(argv[2]);
	fd = (s32)syscall3(SYS_open, (u32)argv[1], O_RDWR, 0);
	if (fd < 0)
		fail("wspitest: can't open the spidev node\n");
	set_mode(0);
	if ((s32)syscall3(SYS_ioctl, fd, SPI_IOC_WR_BITS, (u32)&bits) < 0 ||
	    (s32)syscall3(SYS_ioctl, fd, SPI_IOC_WR_SPEED, (u32)&hz) < 0)
		fail("wspitest: can't set bits / speed\n");
	puts_("wspitest at ");
	putd((s32)hz);
	puts_(" Hz\n");

	/* 1. reset */
	memset(txb, 0xff, 8);
	xfer("reset", 8);

	/* 2. init: ff ff 40 00 00 00 79 crc|1 (fixed busy off, 0 busy words) */
	txb[0] = 0xff;
	txb[1] = 0xff;
	txb[2] = 0x40;
	txb[3] = 0;
	txb[4] = 0;
	txb[5] = 0;
	txb[6] = 0x40 | 0x20 | 0x10 | 0x08 | 0x01;
	txb[7] = crc7_be(txb + 2, 5) | 1;
	xfer("init", 8);
	set_mode(SPI_CS_HIGH);
	memset(txb, 0xff, 4);
	xfer("extra clocks, cs high", 4);
	set_mode(0);

	/* 3. + 4. */
	for (i = 0; i < 8; i++)
		write32(part[i][0], part[i][1]);
	write32(0x1fffc, 1);
	msleep(10);

	/* 5. command, then up to 7 busy / data words */
	memset(txb, 0, sizeof(txb));
	put_be32(txb, WSPI_READ | WSPI_LEN(4) | WSPI_ADDR(CHIP_ID_ADDR));
	xfer("read chip id", 32);
	for (i = 4; i + 8 <= 32; i += 4) {
		if (rxb[i + 3] & 1) {	/* busy word bit 0 = ready */
			found = (int)i;
			id = get_be32(rxb + i + 4);
			break;
		}
	}
	if (found < 0) {
		puts_("no ready busy word: chip not answering (clock, enable, cs?)\n");
		exit_with_status(2);
	}
	puts_("chip id ");
	puthex(id, 8);
	if (id == 0x04030111)
		puts_(" = wl1271 PG20, OK\n");
	else if (id == 0x04030101)
		puts_(" = wl1271 PG10\n");
	else
		puts_(" (expected 04030111)\n");
	exit_with_status(id == 0x04030111 || id == 0x04030101 ? 0 : 3);
}
