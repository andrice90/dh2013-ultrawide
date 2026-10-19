"""Disassembles CloakNTEngine.dll code, naming exported call targets and showing float constants.

usage: py re/dis.py VA[:count] ...      (count = instructions, default 60; stops at ret when count < 200)
"""
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _engine import Engine, Names  # noqa: E402

e = Engine()
names = Names()
rdata = next(s for s in e.pe.sections if s.Name.rstrip(b'\0') == b'.rdata')
data_lo = e.base + rdata.VirtualAddress
data_hi = e.base + e.pe.OPTIONAL_HEADER.SizeOfImage
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

for spec in sys.argv[1:]:
    a, n = (spec.split(':') + ['60'])[:2]
    a, n = int(a, 16), int(n)
    print('=== %08x %s' % (a, names.get(a)))
    for i, ins in enumerate(md.disasm(e.read(a, n * 8), a)):
        if i >= n:
            break
        note = ''
        for tok in ins.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').split():
            if not tok.startswith('0x'):
                continue
            v = int(tok, 16)
            if names.get(v):
                note += ' <' + names.get(v)[:90] + '>'
            elif data_lo <= v < data_hi and 'dword' in ins.op_str:
                f = e.unpack('f', v)
                if abs(f) < 1e7 and (abs(f) > 1e-6 or f == 0):
                    note += ' {%g}' % f
        print('  %08x %-8s %s%s' % (ins.address, ins.mnemonic, ins.op_str, note))
        if ins.mnemonic == 'ret' and n < 200:
            break
