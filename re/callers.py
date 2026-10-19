"""Lists direct calls (E8) and jumps (E9) to the given addresses in CloakNTEngine.dll.

usage: py re/callers.py VA ...
Calls through vtables or function pointers are not found.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _engine import Engine, Names  # noqa: E402

e = Engine()
names = Names()
lo, hi = e.text
arr = np.frombuffer(e.read(lo, hi - lo), dtype=np.uint8)
idx = np.where((arr[:-4] == 0xE8) | (arr[:-4] == 0xE9))[0]
rel = sum(arr[idx + 1 + k].astype(np.int64) << (8 * k) for k in range(4))
rel = np.where(rel >= 2 ** 31, rel - 2 ** 32, rel)
dest = lo + idx + 5 + rel

for t in sys.argv[1:]:
    t = int(t, 16)
    hits = idx[dest == t]
    print('== %08x %s: %d refs' % (t, names.get(t), len(hits)))
    for h in hits[:60]:
        print('   %08x %s' % (lo + h, names.owner(lo + h)))
