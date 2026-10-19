"""Prints the printable strings in a file between two offsets.

usage: py re/strrange.py FILE HEXSTART HEXEND
"""
import re
import sys

data = open(sys.argv[1], 'rb').read()
a, b = int(sys.argv[2], 16), int(sys.argv[3], 16)
for m in re.finditer(rb'[\x20-\x7e]{3,}', data[a:b]):
    print(hex(a + m.start()), m.group().decode()[:120])
