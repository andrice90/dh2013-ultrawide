"""Finds instructions in CloakNTEngine.dll .text whose immediate or displacement is a given 32-bit value
(struct field offsets like 1409c, constant addresses like 106b0d6c).

usage: py re/xref.py HEXVALUE ...
Instruction starts are guessed (1-7 bytes before the match), so some lines are misaligned decodes: the
address is right, the mnemonic may not be.
"""
import os
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _engine import Engine, Names  # noqa: E402

e = Engine()
names = Names()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
lo, hi = e.text
text = e.read(lo, hi - lo)

for v in sys.argv[1:]:
    pat = struct.pack('<I', int(v, 16))
    i = 0
    seen = set()
    while True:
        i = text.find(pat, i)
        if i < 0:
            break
        for back in range(1, 8):
            st = i - back
            ins = next(md.disasm(text[st:st + 16], lo + st), None)
            if ins and ins.size >= back + 4 and ins.address not in seen:
                seen.add(ins.address)
                print(v, '%08x %-6s %-40s %s' % (ins.address, ins.mnemonic, ins.op_str, names.owner(ins.address, 70)))
                break
        i += 1
