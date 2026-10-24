// WL1271 NVS for Linux from the N8's wlanhwinit.dll data, shared by the
// n8wlnv helper (Symbian) and the host test n8nvs_test.cpp. The same
// conversion as tools/wl1271_nvs_convert.py (see there for the layout):
//
//   in:  GetHwInitData's "firmware" block: u32 468 | 468 byte TI NVS section
//        | u32 208 | 208 byte radio INI (| u32 0)
//   out: struct wl1271_nvs_file, 912 bytes = the 468 byte NVS + the 208 byte
//        INI + 236 bytes 5 GHz part from TI's reference wl127x-nvs.bin (the
//        N8 is 2.4 GHz only, wl12xx never uses them, only the size counts)
//
// the NVS holds the phone's MAC and calibration: never publish the output.
// plain C++, no Symbian or libc headers
#ifndef N8NVS_H
#define N8NVS_H

enum {
	N8NVS_NVS_LEN = 468,
	N8NVS_INI_LEN = 208,
	N8NVS_OUT_LEN = 912,
	// return codes of n8nvs_convert
	N8NVS_OK = 0,
	N8NVS_ESIZE = -1,	// input not 684 or 688 bytes
	N8NVS_ENVSLEN = -2,	// first u32 is not 468
	N8NVS_EINILEN = -3,	// second u32 is not 208
	N8NVS_ETAIL = -4,	// the 4 bytes after the INI are not 0
	N8NVS_EMAC = -5,	// NVS doesn't start with the two MAC writes
	N8NVS_EPAD = -6,	// INI padding not 0: layout mismatch
	N8NVS_EREFCLK = -7,	// INI ref_clock out of range
	N8NVS_EDUAL = -8,	// dual band: the 5 GHz defaults would be used
};

// TI reference wl127x-nvs.bin (linux-firmware 20240318), INI bytes 208..443
static const unsigned char n8nvs_ti_5ghz[236] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xee, 0xf4, 0xf8, 0xfe, 0x05, 0xea, 0xf3, 0xfa, 0x01, 0x06,
	0xe5, 0xf1, 0xfa, 0x02, 0x07, 0x00, 0x9a, 0x01, 0xae, 0x01, 0xc2, 0x01,
	0xcc, 0x01, 0xdb, 0x01, 0xdb, 0x01, 0xd1, 0x01, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x1e,
	0x21, 0x23, 0x25, 0x50, 0x18, 0x1e, 0x21, 0x23, 0x25, 0x50, 0x16, 0x1c,
	0x1e, 0x20, 0x20, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
	0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
	0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
	0x50, 0x50, 0x50, 0x01, 0x02, 0x02, 0x02, 0x02, 0x00, 0x1a, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1a, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1e, 0x2d,
	0x00, 0x9a, 0x01, 0xae, 0x01, 0xc2, 0x01, 0xcc, 0x01, 0xdb, 0x01, 0xdb,
	0x01, 0xd1, 0x01, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x1e, 0x21, 0x23, 0x25, 0x50, 0x18,
	0x1e, 0x21, 0x23, 0x25, 0x50, 0x16, 0x1c, 0x1e, 0x20, 0x20, 0x50, 0x50,
	0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
	0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
	0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x01, 0x02,
	0x02, 0x02, 0x02, 0x00, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x1e, 0x2d, 0x00,
};

static inline unsigned n8nvs_u32(const unsigned char* p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

// aIn/aLen: the GetHwInitData block; aOut: N8NVS_OUT_LEN bytes
static inline int n8nvs_convert(const unsigned char* aIn, unsigned aLen, unsigned char* aOut)
{
	const unsigned char* nvs = aIn + 4;
	const unsigned char* ini = aIn + 8 + N8NVS_NVS_LEN;
	const unsigned base = 8 + N8NVS_NVS_LEN + N8NVS_INI_LEN;
	unsigned i;

	if (aLen != base && aLen != base + 4)
		return N8NVS_ESIZE;
	if (n8nvs_u32(aIn) != N8NVS_NVS_LEN)
		return N8NVS_ENVSLEN;
	if (n8nvs_u32(aIn + 4 + N8NVS_NVS_LEN) != N8NVS_INI_LEN)
		return N8NVS_EINILEN;
	if (aLen == base + 4 && n8nvs_u32(aIn + base) != 0)
		return N8NVS_ETAIL;
	// TI NVS: TLV register writes, the first two set the MAC
	if (nvs[0] != 0x01 || nvs[1] != 0x6d || nvs[2] != 0x54 ||
	    nvs[7] != 0x01 || nvs[8] != 0x71 || nvs[9] != 0x54)
		return N8NVS_EMAC;
	// padding1, padding2, the padding after each 2.4 GHz FEM block
	if (ini[57] || ini[75] || ini[76 + 65] || ini[76 + 66 + 65])
		return N8NVS_EPAD;
	if (ini[0] > 5)
		return N8NVS_EREFCLK;
	if (ini[4] != 0)
		return N8NVS_EDUAL;
	for (i = 0; i < N8NVS_NVS_LEN; i++)
		aOut[i] = nvs[i];
	for (i = 0; i < N8NVS_INI_LEN; i++)
		aOut[N8NVS_NVS_LEN + i] = ini[i];
	for (i = 0; i < sizeof(n8nvs_ti_5ghz); i++)
		aOut[N8NVS_NVS_LEN + N8NVS_INI_LEN + i] = n8nvs_ti_5ghz[i];
	return N8NVS_OK;
}

// the MAC as wlcore reads it from a converted NVS (wl1271_boot_upload_nvs
// order): bytes 11, 10, 6, 5, 4, 3
static inline void n8nvs_mac(const unsigned char* aNvs, unsigned char* aMac)
{
	aMac[0] = aNvs[11];
	aMac[1] = aNvs[10];
	aMac[2] = aNvs[6];
	aMac[3] = aNvs[5];
	aMac[4] = aNvs[4];
	aMac[5] = aNvs[3];
}

// standard crc32 (zlib / ethernet), bitwise: the handoff block checksum
static inline unsigned n8nvs_crc32(const unsigned char* p, unsigned n)
{
	unsigned c = 0xffffffffu;
	int k;

	while (n--) {
		c ^= *p++;
		for (k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
	}
	return ~c;
}

#endif
