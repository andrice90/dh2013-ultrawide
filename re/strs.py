"""Prints printable strings in any file that match a regex (case-insensitive), with file offsets.

usage: py re/strs.py FILE REGEX
"""
import re
import sys

data = open(sys.argv[1], 'rb').read()
pat = re.compile(sys.argv[2].encode(), re.I)
for m in re.finditer(rb'[\x20-\x7e]{4,}', data):
    if pat.search(m.group()):
        print(hex(m.start()), m.group().decode()[:160])
