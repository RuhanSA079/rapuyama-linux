#!/usr/bin/env python3
"""Turn the N8's WLAN init block into a Linux wl1271 NVS file.

    python3 -I tools/wl1271_nvs_convert.py work/wlan/n8-nvs-ini.bin work/wlan/wl1271-nvs.bin

Input: what wldump.sis got from wlanhwinit.dll (CWlanHwInit::GetHwInitData),
see docs/n8-hardware.md "WLAN calibration (NVS) and MAC":

    u32 le 468 | 468 byte TI NVS section | u32 le 208 | 208 byte radio INI
    | u32 0 (688 bytes on the N8)

Output: struct wl1271_nvs_file (drivers/net/wireless/ti/wlcore/ini.h),
912 bytes = 468 byte NVS section + 444 byte INI section. wlcore loads it as
ti-connectivity/wl127x-nvs.bin (wl127x_data in wlcore/spi.c; wl1271-nvs.bin
is the linux-firmware symlink to the same file).

The 208 byte INI is not an older format: it is the first 208 bytes of the
444 byte Linux INI, i.e. TI's TAutoRadioIniParams (WVSS/misc_files/radio_ini.h
= IniFileGeneralParam + TStatRadioParams + TDynRadioParams[2], the 2.4 GHz
part) with the padding Linux spells out (padding1/padding2/per-FEM padding).
Checked field by field against ini.h and against linux-firmware's reference
wl127x-nvs.bin, whose first 208 INI bytes have the same structure. So bytes
0..207 are copied 1:1 and only the 5 GHz part (236 bytes) is missing.

The 5 GHz part is filled from TI's reference wl127x-nvs.bin (linux-firmware
20240318, ti-connectivity/wl127x-nvs.bin, INI offset 208..443), embedded
below. The N8 is single band (general_params.dual_mode_select = 0), so
wl12xx never enables 11a and the firmware does not use these values; they
are only there so the file has the size wlcore checks.

The dump holds the phone's MAC and factory calibration: the output must not
be committed, and this tool never prints the MAC. No dependencies.
"""
import struct
import sys

NVS_LEN = 468
N8_INI_LEN = 208
LINUX_INI_LEN = 444
OUT_LEN = NVS_LEN + LINUX_INI_LEN  # 912 = sizeof(struct wl1271_nvs_file)

# INI offsets (relative to the start of the INI section), struct wl1271_nvs_file
GENERAL = 0          # wl1271_ini_general_params, 57 bytes
PAD1 = 57            # padding1
STAT2 = 58           # wl1271_ini_band_params_2, 17 bytes
PAD2 = 75            # padding2
DYN2 = 76            # 2 x (wl1271_ini_fem_params_2 65 bytes + 1 padding)
DYN2_LEN = 66
STAT5 = 208          # wl1271_ini_band_params_5, 29 bytes + padding3
DYN5 = 238           # 2 x (wl1271_ini_fem_params_5 102 bytes + 1 padding)

# general params, one per byte at INI offset 0..8 (radio_ini.h / ini.h names)
GENERAL_NAMES = [
    'ref_clock (0 19.2, 1 26, 2 38.4, 3 52 MHz; wl12xx overrides it from DT)',
    'settling_time',
    'clk_valid_on_wakeup',
    'dc2dc_mode',
    'dual_mode_select (0 = 2.4 GHz only)',
    'tx_bip_fem_auto_detect',
    'tx_bip_fem_manufacturer (0 RFMD, 1 TriQuint)',
    'general_settings',
    'sr_state',
]

# 5 GHz defaults: TI reference wl127x-nvs.bin, INI bytes 208..443, layout:
#   208 stat_radio_params_5: rx_trace_insertion_loss[7], tx_trace_loss[7],
#       rx_rssi_process_compens[15], padding3
#   238 dyn_radio_params_5[0] (RFMD), 341 dyn_radio_params_5[1] (TriQuint):
#       tx_bip_ref_pd_voltage le16[7], tx_bip_ref_power[7],
#       tx_bip_ref_offset[7], tx_per_rate_pwr_limits_normal/degraded/
#       extreme[6] each, tx_per_chan_pwr_limits_ofdm[35],
#       tx_pd_vs_rate_offsets[6], tx_ibias[6], rx_fem_insertion_loss[7],
#       degraded_low_to_normal_thr, normal_to_degraded_high_thr, padding
TI_REF_5GHZ = bytes.fromhex(
    '0000000000000000000000000000eef4f8fe05eaf3fa0106e5f1fa020700'
    '9a01ae01c201cc01db01db01d10180808080808080000000000000001c1e'
    '21232550181e21232550161c1e2020505050505050505050505050505050'
    '5050505050505050505050505050505050505050500102020202001a1a1a'
    '1a1a1a101010101010101e2d009a01ae01c201cc01db01db01d101808080'
    '80808080000000000000001c1e21232550181e21232550161c1e20205050'
    '505050505050505050505050505050505050505050505050505050505050'
    '505050500102020202001a1a1a1a1a1a101010101010101e2d00')


def fail(msg):
    sys.stderr.write('wl1271_nvs_convert: %s\n' % msg)
    sys.exit(1)


def split_dump(data):
    """-> (468 byte NVS, 208 byte INI) from the wldump block"""
    if len(data) < 8:
        fail('input too short')
    (nvs_len,) = struct.unpack_from('<I', data, 0)
    if nvs_len != NVS_LEN:
        fail('NVS length %d, expected %d' % (nvs_len, NVS_LEN))
    nvs = data[4:4 + NVS_LEN]
    (ini_len,) = struct.unpack_from('<I', data, 4 + NVS_LEN)
    if ini_len != N8_INI_LEN:
        fail('INI length %d, expected %d' % (ini_len, N8_INI_LEN))
    ini = data[8 + NVS_LEN:8 + NVS_LEN + N8_INI_LEN]
    rest = data[8 + NVS_LEN + N8_INI_LEN:]
    # the N8 block ends with one more u32 0 (an empty third section)
    if len(ini) != N8_INI_LEN or rest not in (b'', b'\0\0\0\0'):
        fail('input is %d bytes, expected %d (+ u32 0)' % (len(data), 8 + NVS_LEN + N8_INI_LEN))
    return nvs, ini


def check_nvs(nvs):
    # TI NVS: TLV register writes, the first two set the MAC (01 6d 54 ..,
    # 01 71 54 ..), bytes 3..6 and 10..11 hold it. Only the framing is checked.
    if nvs[0] != 0x01 or nvs[1:3] != b'\x6d\x54' or nvs[7] != 0x01 or nvs[8:10] != b'\x71\x54':
        fail('NVS section does not start with the two MAC register writes')


def check_ini(ini):
    for off, what in ((PAD1, 'padding1'), (PAD2, 'padding2'),
                      (DYN2 + 65, 'dyn2[0] padding'), (DYN2 + DYN2_LEN + 65, 'dyn2[1] padding')):
        if ini[off] != 0:
            fail('%s at INI offset %d is %#x, not 0: layout mismatch' % (what, off, ini[off]))
    if ini[0] > 5:
        fail('ref_clock %d out of range: layout mismatch' % ini[0])
    if ini[4] != 0:
        fail('dual_mode_select = %d: the 5 GHz defaults would be used, refusing' % ini[4])


def describe(ini):
    lines = []
    for i, name in enumerate(GENERAL_NAMES):
        lines.append('  %-70s %#04x' % (name, ini[i]))
    for k, fem in enumerate(('RFMD', 'TriQuint')):
        base = DYN2 + k * DYN2_LEN
        (pd,) = struct.unpack_from('<H', ini, base)
        lines.append('  dyn_radio_params_2[%d] (%s): tx_bip_ref_pd_voltage %#06x, tx_bip_ref_power %#04x'
                     % (k, fem, pd, ini[base + 2]))
    return '\n'.join(lines)


def convert(data):
    nvs, ini = split_dump(data)
    check_nvs(nvs)
    check_ini(ini)
    if len(TI_REF_5GHZ) != LINUX_INI_LEN - N8_INI_LEN:
        fail('internal: 5 GHz default is %d bytes' % len(TI_REF_5GHZ))
    out = nvs + ini + TI_REF_5GHZ
    assert len(out) == OUT_LEN
    return out, ini


def main(argv):
    if len(argv) != 3:
        fail('usage: wl1271_nvs_convert.py <n8-nvs-ini.bin> <wl1271-nvs.bin>')
    with open(argv[1], 'rb') as f:
        data = f.read()
    out, ini = convert(data)
    with open(argv[2], 'wb') as f:
        f.write(out)
    print('wrote %s, %d bytes (NVS 468 as dumped, INI 208 as dumped + 236 TI 5 GHz defaults)'
          % (argv[2], len(out)))
    print(describe(ini))


if __name__ == '__main__':
    main(sys.argv)
