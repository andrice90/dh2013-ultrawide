"""Reads typed values from CloakNTEngine.dll (struct format letter: f float, d double, I uint32, ...).

usage: py re/rd.py TYPE:VA ...      e.g. d:10650310 f:106b0d6c
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _engine import Engine  # noqa: E402

e = Engine()
for spec in sys.argv[1:]:
    t, a = spec.split(':')
    print(spec, e.unpack(t, int(a, 16)))
