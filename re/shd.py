"""Find and disassemble D3D9 shaders inside an extracted CloakNT .shd pack.

usage: py -I shd.py <file.shd> <constant-name substring> [max]
Shader blobs are located by their version token (vs_3_0 / ps_3_0) and end token; disassembly uses the
system d3dcompiler_47.dll (D3DDisassemble handles SM3 bytecode).
"""
import ctypes
import struct
import sys

d = open(sys.argv[1], 'rb').read()
want = sys.argv[2].encode()
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 5

comp = ctypes.WinDLL('d3dcompiler_47.dll')


class Blob(ctypes.Structure):
    pass


def disasm(code):
    blob = ctypes.c_void_p()
    hr = comp.D3DDisassemble(code, len(code), 0, None, ctypes.byref(blob))
    if hr != 0:
        return 'D3DDisassemble failed %08x' % (hr & 0xffffffff)
    vt = ctypes.cast(ctypes.cast(blob, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    getptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])
    getsize = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])
    s = ctypes.string_at(getptr(blob), getsize(blob)).decode('latin-1')
    ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vt[2])(blob)
    return s


found = 0
i = 0
while found < limit:
    a = d.find(b'\x00\x03\xfe\xff', i)
    b_ = d.find(b'\x00\x03\xff\xff', i)
    starts = [x for x in (a, b_) if x >= 0]
    if not starts:
        break
    s = min(starts)
    # walk tokens to the end token 0x0000FFFF
    e = s
    while e + 4 <= len(d) and struct.unpack_from('<I', d, e)[0] != 0x0000FFFF:
        e += 4
    code = d[s:e + 4]
    i = e + 4
    if want in code:
        found += 1
        print('==== shader at %#x, %d bytes' % (s, len(code)))
        print(disasm(code))
