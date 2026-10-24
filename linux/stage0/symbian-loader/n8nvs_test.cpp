// host test for n8nvs.h: converts a wldump block and compares it with a
// reference NVS (tools/wl1271_nvs_convert.py output). no MAC is printed.
//   g++ -O2 -Wall -o /tmp/n8nvs_test n8nvs_test.cpp
//   /tmp/n8nvs_test work/wlan/n8-nvs-ini.bin work/wlan/fw/wl127x-nvs.bin
#include <cstdio>
#include <cstring>
#include "n8nvs.h"

static long readall(const char* f, unsigned char* b, long max)
{
	FILE* fp = fopen(f, "rb");
	if (!fp)
		return -1;
	long n = fread(b, 1, max, fp);
	fclose(fp);
	return n;
}

int main(int argc, char** argv)
{
	static unsigned char in[4096], ref[4096], out[N8NVS_OUT_LEN];
	if (argc != 3) {
		fprintf(stderr, "usage: n8nvs_test <wldump block> <reference nvs>\n");
		return 2;
	}
	long ni = readall(argv[1], in, sizeof(in)), nr = readall(argv[2], ref, sizeof(ref));
	if (ni < 0 || nr < 0) {
		fprintf(stderr, "can't read the inputs\n");
		return 2;
	}
	int r = n8nvs_convert(in, ni, out);
	printf("convert(%ld bytes) = %d\n", ni, r);
	if (r)
		return 1;
	int same = nr == N8NVS_OUT_LEN && !memcmp(out, ref, N8NVS_OUT_LEN);
	printf("output %d bytes, reference %ld bytes: %s\n", N8NVS_OUT_LEN, nr,
	       same ? "IDENTICAL" : "DIFFERENT");
	printf("crc32 %08x\n", n8nvs_crc32(out, N8NVS_OUT_LEN));
	return same ? 0 : 1;
}
