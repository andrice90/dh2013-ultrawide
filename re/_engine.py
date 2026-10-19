"""Shared helpers for the reverse-engineering scripts: find the game, map CloakNTEngine.dll, read export names.

The game folder comes from the DH2013_GAME_DIR environment variable or, failing that, from Steam (app 225580).
Export names come from re/exports.txt, written by exports.py.
"""
import bisect
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
EXPORTS = os.path.join(HERE, 'exports.txt')
APP_ID = '225580'


def _steam_libraries():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as k:
            steam = winreg.QueryValueEx(k, 'SteamPath')[0]
    except OSError:
        return []
    libs = [steam]
    vdf = os.path.join(steam, 'steamapps', 'libraryfolders.vdf')
    if os.path.exists(vdf):
        text = open(vdf, encoding='utf-8', errors='replace').read()
        libs += [p.replace('\\\\', '\\') for p in re.findall(r'"path"\s+"([^"]+)"', text)]
    return libs


def game_dir():
    env = os.environ.get('DH2013_GAME_DIR')
    if env:
        return env
    for lib in _steam_libraries():
        acf = os.path.join(lib, 'steamapps', 'appmanifest_%s.acf' % APP_ID)
        if os.path.exists(acf):
            m = re.search(r'"installdir"\s+"([^"]+)"', open(acf, encoding='utf-8', errors='replace').read())
            if m:
                return os.path.join(lib, 'steamapps', 'common', m.group(1))
    raise SystemExit('game folder not found: set DH2013_GAME_DIR')


def engine_path():
    return os.path.join(game_dir(), 'CloakNTEngine.dll')


class Engine:
    """CloakNTEngine.dll mapped as an image: `img` bytes, `base` (preferred image base), `text` (start, end VA)."""

    def __init__(self, path=None):
        import pefile
        self.pe = pefile.PE(path or engine_path(), fast_load=True)
        self.img = self.pe.get_memory_mapped_image()
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        text = next(s for s in self.pe.sections if s.Name.rstrip(b'\0') == b'.text')
        self.text = (self.base + text.VirtualAddress, self.base + text.VirtualAddress + text.Misc_VirtualSize)

    def read(self, va, n):
        return self.img[va - self.base:va - self.base + n]

    def unpack(self, fmt, va):
        return struct.unpack_from('<' + fmt, self.img, va - self.base)[0]


class Names:
    """Export names by address, from exports.txt (empty if it does not exist yet)."""

    def __init__(self, path=EXPORTS):
        self.by_addr = {}
        if os.path.exists(path):
            for line in open(path):
                a, n = line.split(' ', 1)
                self.by_addr.setdefault(int(a, 16), n.strip())
        self.keys = sorted(self.by_addr)

    def get(self, va, default=''):
        return self.by_addr.get(va, default)

    def owner(self, va, width=80):
        """'name+offset' of the export at or before `va`."""
        i = bisect.bisect_right(self.keys, va) - 1
        return '%s+%x' % (self.by_addr[self.keys[i]][:width], va - self.keys[i]) if i >= 0 else '?'
