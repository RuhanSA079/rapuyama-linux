#!/usr/bin/env python3
"""
parses OLD ptdump v9.1-v9.3 dumps (like logs/ptdump-3.bin)
the current driver layout is different, use parse_map.py for that

layout (all little-endian u32):
  [0]=TTBR0 [1]=TTBR1 [2]=TTBCR [3]=SCTLR
  [4]=nRuns [5]=nProbes [6]=periphVirt [7]=periphPhys [8]=pagesProbed
  [9]=primecell [10]=faulted [11..15]=reserved
  then nRuns triples {virt, phys, pages}
  then nProbes pairs {halVirt, linearToPhysical}
  then pagesProbed records {phys, status, word0, periphID, primecellID}
"""
import struct
import sys


def u32s(d):
    return list(struct.unpack_from("<%dI" % (len(d) // 4), d, 0))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "logs/ptdump-3.bin"
    d = open(path, "rb").read()
    w = u32s(d)
    print(f"file: {path}  ({len(d)} bytes, {len(w)} words)\n")

    ttbr0, ttbr1, ttbcr, sctlr = w[0], w[1], w[2], w[3]
    nRuns, nProbes = w[4], w[5]
    periphVirt10, periphPhys10 = w[6], w[7]
    nEnt, nPrime, nFault = w[8], w[9], w[10]
    print("== MMU / CP15 ==")
    print(f"  TTBR0={ttbr0:08x}  TTBR1={ttbr1:08x}  TTBCR={ttbcr:08x}  SCTLR={sctlr:08x}")
    print(f"  nRuns={nRuns}  nProbes={nProbes}  pagesProbed={nEnt}  "
          f"PrimeCell={nPrime}  faulted={nFault}")
    print(f"  0x40100000 virt={periphVirt10:08x}\n")

    off = 16
    print("== non-RAM runs (virt -> phys, size) ==")
    print(f"  {'virt':>10}  {'phys':>10}  {'pages':>6}  {'bytes':>10}")
    runs = []
    for i in range(nRuns):
        v, p, pg = w[off + i*3], w[off + i*3 + 1], w[off + i*3 + 2]
        runs.append((v, p, pg))
        print(f"  {v:10x}  {p:10x}  {pg:6d}  {pg*0x1000:10x}")
    off += nRuns * 3

    print("\n== HAL-ID probes (halVirt -> LinearToPhysical) ==")
    for i in range(nProbes):
        hv, pp = w[off + i*2], w[off + i*2 + 1]
        tag = "UNMAPPED (=> HAL ID, not an address)" if pp == 0xFFFFFFFF else f"MAPPED -> phys {pp:08x}"
        print(f"  {hv:08x}  {tag}")
    off += nProbes * 2

    # arm primecell part number table (PartNumber = PeriphID0 | (PeriphID1&0xF)<<8)
    PRIMECELL = {
        0x010: "PL010 UART", 0x011: "PL011 UART",
        0x022: "PL022 SSP/SPI", 0x031: "PL031 RTC", 0x041: "PL041 AACI audio",
        0x061: "PL061 GPIO", 0x080: "PL080 DMA", 0x081: "PL081 DMA",
        0x110: "PL110 LCD", 0x111: "PL111 LCD",
        0x180: "PL180 MMCI", 0x181: "PL181 MMCI",
        0x190: "PL190 VIC (intc)", 0x192: "PL192 VIC (intc)",
        0x350: "PL350 SMC", 0x353: "PL353 SMC",
        0x804: "SP804 timer", 0x805: "SP805 watchdog", 0x810: "SP810 sysctrl",
    }

    ents = []
    for m in range(nEnt):
        phys, status, word0, pid, pcid = w[off + m*5: off + m*5 + 5]
        ents.append((phys, status, word0, pid, pcid))
    off += nEnt * 5

    def decode_pid(pid):
        p0, p1 = pid & 0xff, (pid >> 8) & 0xff
        p2, p3 = (pid >> 16) & 0xff, (pid >> 24) & 0xff
        part = p0 | ((p1 & 0xf) << 8)
        designer = ((p1 & 0xf0) >> 4) | ((p2 & 0x7) << 4)
        rev = (p2 >> 4) & 0xf
        return part, designer, rev

    print("== PrimeCell peripherals (0D F0 05 B1 signature) ==")
    prime = [e for e in ents if e[1] & 4]
    if not prime:
        print("  NONE found. Peripherals are not ARM PrimeCell, OR their ID regs"
              " faulted. See the readable-page fingerprints below.")
    for phys, status, word0, pid, pcid in prime:
        part, designer, rev = decode_pid(pid)
        name = PRIMECELL.get(part, f"unknown part 0x{part:03x}")
        dn = "ARM" if designer == 0x41 else f"designer 0x{designer:02x}"
        print(f"  phys {phys:08x}  part=0x{part:03x} rev{rev} [{dn}]  -> {name}")

    print("\n== readable pages (word0 fingerprint) ==")
    rd = [e for e in ents if e[1] & 1]
    print(f"  {len(rd)} readable / {nEnt} probed  ({nFault} faulted)")
    for phys, status, word0, pid, pcid in rd:
        tags = []
        if status & 2: tags.append("ID-ok")
        if status & 4: tags.append("PrimeCell")
        print(f"    phys {phys:08x}: word0={word0:08x} pcid={pcid:08x} "
              f"{' '.join(tags)}")

    faulted = [e for e in ents if (e[1] & 3) == 0]
    if faulted:
        print(f"\n== pages that FAULTED on read (logged, not crashed) ==")
        for phys, status, word0, pid, pcid in faulted:
            print(f"    phys {phys:08x}: no decode / abort")


if __name__ == "__main__":
    main()