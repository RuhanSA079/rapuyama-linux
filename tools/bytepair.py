#!/usr/bin/env python3
"""Symbian bpe decompressor. faithful port of
symbian/.../kernel/eka/kernel/byte_pair.cpp : BytePairDecompress().
used to unpack compressed ROFS/E32 executables (compression type
KUidCompressionBytePair = 0x102822AA). each call decompresses 1 page
(BlockSize 0x1000); a whole file is a sequence of pages.
"""
import struct
import sys

BLOCK = 0x1000


def decompress_block(src, off, dst_size):
    """Decompress one page. Returns (bytes, next_off) or (None, None) on error."""
    LUT0 = list(range(256))
    LUT1 = [0] * 256
    stack = []
    marker = 0xffffffff
    n = len(src)
    s = off

    if s >= n:
        return None, None
    num_tokens = src[s]; s += 1
    if num_tokens:
        if s >= n:
            return None, None
        marker = src[s]; s += 1
        LUT0[marker] = (~marker) & 0xff
        if num_tokens < 32:
            end = s + 3 * num_tokens
            if end > n:
                return None, None
            while s < end:
                b = src[s]; p1 = src[s + 1]; p2 = src[s + 2]; s += 3
                LUT0[b] = p1; LUT1[b] = p2
        else:
            if s + 32 > n:
                return None, None
            bitmask = src[s:s + 32]; s += 32
            left = num_tokens
            b = 0
            while b < 0x100:
                if bitmask[b >> 3] & (1 << (b & 7)):
                    if s + 1 >= n:
                        return None, None
                    p1 = src[s]; p2 = src[s + 1]; s += 2
                    LUT0[b] = p1; LUT1[b] = p2
                    left -= 1
                b += 1
            if left:
                return None, None

    dst = bytearray()
    ap = dst.append
    stk = []
    push = stk.append
    pop = stk.pop
    L0 = LUT0
    L1 = LUT1
    if s >= n:
        return None, None
    b = src[s]; s += 1
    p1 = L0[b]
    NEXT, NOT_SINGLE, DO_PAIR, RECURSE = 0, 1, 2, 3
    st = NEXT if p1 == b else NOT_SINGLE

    while True:
        if st == NEXT:
            if s >= n:                 # done_s
                ap(p1)
                return bytes(dst), s
            b = src[s]; s += 1
            ap(p1)
            if len(dst) >= dst_size:   # done_d (page full)
                return bytes(dst), s - 1
            p1 = L0[b]
            if p1 != b:
                st = NOT_SINGLE
            continue
        if st == NOT_SINGLE:
            if b == marker:            # do_marker
                if s >= n:
                    return None, None
                p1 = src[s]; s += 1
                st = NEXT
                continue
            st = DO_PAIR               # fallthrough to do_pair
        if st == DO_PAIR:
            p2 = L1[b]
            b = p1
            p1 = L0[b]
            if len(stk) >= 0x100:      # firmware: sp<=stack => KErrCorrupt
                return None, None
            push(p2)
            st = RECURSE
        # RECURSE
        while b == p1:
            if not stk:                # sp == stackStart
                st = NEXT
                break
            b = pop()
            ap(p1)
            if len(dst) >= dst_size:
                return None, None
            p1 = L0[b]
        else:
            st = DO_PAIR               # b != p1 -> another pair
            continue
        # reached here only via the 'break' (stack empty) -> st==NEXT
        continue


def decompress(src, off, total_uncompressed):
    """Decompress a full bytepair stream (sequence of 0x1000-byte pages)."""
    out = bytearray()
    s = off
    while len(out) < total_uncompressed:
        want = min(BLOCK, total_uncompressed - len(out))
        blk, nxt = decompress_block(src, s, want)
        if blk is None:
            raise ValueError("bytepair error at off %#x (produced %#x/%#x)"
                             % (s, len(out), total_uncompressed))
        out += blk
        s = nxt
    return bytes(out), s


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    off = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0
    usize = int(sys.argv[3], 0) if len(sys.argv) > 3 else len(data) * 8
    out, nxt = decompress(data, off, usize)
    sys.stdout.buffer.write(out)
    sys.stderr.write("decompressed %#x bytes, next=%#x\n" % (len(out), nxt))