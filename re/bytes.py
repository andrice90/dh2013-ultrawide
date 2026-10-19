"""Prints CloakNTEngine.dll bytes as C initialisers (for the patch expectations), plus the PE timestamp.

usage: py re/bytes.py VA:HEXLEN ...
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _engine import Engine  # noqa: E402

e = Engine()
print('timestamp', e.pe.FILE_HEADER.TimeDateStamp, 'SizeOfImage', hex(e.pe.OPTIONAL_HEADER.SizeOfImage))
for spec in sys.argv[1:]:
    a, n = spec.split(':')
    a, n = int(a, 16), int(n, 16)
    print('%08x rva %06x [%d] %s' % (a, a - e.base, n, ', '.join('0x%02X' % x for x in e.read(a, n))))
