"""List / extract files from a CloakNT .crf archive ("!CPF", LZO1X chunks).

usage: py -I crf.py <archive.crf> [name-substring] [out-dir]
Format (worked out from shaders.crf): u32 magic "!CPF", u32 version, u32 count, then count entries of 0x124
bytes: u32 flags, u32 archive id, u32 date, u32 ?, u32 size, u32 data offset, u32 chunks, u32 name hash,
char name[0x104]. Data: per chunk u32 header (bit 31 = LZO1X compressed, low bits = stored size) + bytes.
"""
import os
import struct
import sys


def lzo1x_decompress(src):
    out = bytearray()
    ip = 0

    def copy_match(dist, n):
        start = len(out) - dist
        for i in range(n):
            out.append(out[start + i])

    state = 'loop'
    t = 0
    if src[0] > 17:
        t = src[0] - 17
        ip = 1
        if t < 4:
            state = 'match_next'
        else:
            out += src[ip:ip + t]
            ip += t
            state = 'first_literal_run'
    while True:
        if state == 'loop':
            t = src[ip]; ip += 1
            if t >= 16:
                state = 'match'; continue
            if t == 0:
                while src[ip] == 0:
                    t += 255; ip += 1
                t += 15 + src[ip]; ip += 1
            out += src[ip:ip + t + 3]; ip += t + 3
            state = 'first_literal_run'
        if state == 'first_literal_run':
            t = src[ip]; ip += 1
            if t >= 16:
                state = 'match'
            else:
                dist = 1 + 0x0800 + (t >> 2) + (src[ip] << 2); ip += 1
                copy_match(dist, 3)
                state = 'match_done'
        if state == 'match':
            if t >= 64:
                dist = 1 + ((t >> 2) & 7) + (src[ip] << 3); ip += 1
                copy_match(dist, (t >> 5) + 1)
            elif t >= 32:
                t &= 31
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 31 + src[ip]; ip += 1
                dist = 1 + (src[ip] >> 2) + (src[ip + 1] << 6); ip += 2
                copy_match(dist, t + 2)
            elif t >= 16:
                dist = (t & 8) << 11
                t &= 7
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 7 + src[ip]; ip += 1
                dist += (src[ip] >> 2) + (src[ip + 1] << 6); ip += 2
                if dist == 0:
                    return bytes(out)  # end of stream
                copy_match(dist + 0x4000, t + 2)
            else:
                dist = 1 + (t >> 2) + (src[ip] << 2); ip += 1
                copy_match(dist, 2)
            state = 'match_done'
        if state == 'match_done':
            t = src[ip - 2] & 3
            if t == 0:
                state = 'loop'; continue
            state = 'match_next'
        if state == 'match_next':
            out += src[ip:ip + t]; ip += t
            t = src[ip]; ip += 1
            state = 'match'


def entries(d):
    assert d[:4] == b'!CPF', 'not a CRF archive'
    n = struct.unpack_from('<I', d, 8)[0]
    for i in range(n):
        o = 0xc + i * 0x124
        flags, arc, date, unk, size, off, chunks, h = struct.unpack_from('<8I', d, o)
        name = d[o + 0x20:d.index(b'\0', o + 0x20)].decode('latin-1')
        yield name, size, off, chunks


def extract(d, off, chunks):
    out = bytearray()
    for _ in range(chunks):
        hdr = struct.unpack_from('<I', d, off)[0]
        n = hdr & 0x7fffffff
        blob = d[off + 4:off + 4 + n]
        out += lzo1x_decompress(blob) if hdr & 0x80000000 else blob
        off += 4 + n
    return bytes(out)


if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    pat = sys.argv[2].lower() if len(sys.argv) > 2 else None
    outdir = sys.argv[3] if len(sys.argv) > 3 else None
    for name, size, off, chunks in entries(d):
        if pat and pat not in name.lower():
            continue
        if not outdir:
            print('%-60s %9d bytes, %3d chunks at %08x' % (name, size, chunks, off))
            continue
        data = extract(d, off, chunks)
        path = os.path.join(outdir, name.replace('\\', os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(data)
        print('%s: %d bytes (table says %d)' % (name, len(data), size))
