"""Writes every named export of CloakNTEngine.dll to re/exports.txt ("address name" per line).

usage: py re/exports.py [out-file]
pefile stops after 8192 names; this reads the export directory itself (the engine has 17,237).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _engine import EXPORTS, Engine  # noqa: E402

e = Engine()
d = e.pe.OPTIONAL_HEADER.DATA_DIRECTORY[0]
_, _, _, _, _, _, nfun, nnames, afun, anames, aord = struct.unpack_from('<IIHHIIIIIII', e.img, d.VirtualAddress)
out = sys.argv[1] if len(sys.argv) > 1 else EXPORTS
with open(out, 'w') as f:
    for i in range(nnames):
        nrva = struct.unpack_from('<I', e.img, anames + 4 * i)[0]
        o = struct.unpack_from('<H', e.img, aord + 2 * i)[0]
        frva = struct.unpack_from('<I', e.img, afun + 4 * o)[0]
        name = e.img[nrva:e.img.index(b'\0', nrva)].decode()
        f.write('%08x %s\n' % (e.base + frva, name))
print('%d functions, %d names -> %s' % (nfun, nnames, out))
