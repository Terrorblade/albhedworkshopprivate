"""Turn an RVA from an albhed_crash.log report back into a function name.

There are no PDBs in this project on purpose, so a crash or hang report can only
say "AlBhedCheats.dll+0x0005BA13". The linker's /MAP output is the missing half.
Both plugin builds emit one next to the DLL.

    python tools/mapsym.py loader/build/AlBhedCheats.map 0x5BA13
    python tools/mapsym.py loader/build/AlBhedCheats.map 0x5BA13 0x5383E 0x53938

Or hand it a whole report and it resolves every frame in it:

    python tools/mapsym.py loader/build/AlBhedCheats.map --report albhed_crash.log

The map's addresses are "section:offset" plus a preferred load address, so the
RVA of a symbol is its listed address minus the image base the map declares.
"""

import re
import sys


class SymbolMap(object):
    def __init__(self, path):
        self.base = None
        self.symbols = []  # (rva, name)
        self._read(path)
        self.symbols.sort()

    def _read(self, path):
        # "Preferred load address is 10000000"
        base_re = re.compile(r'Preferred load address is\s+([0-9A-Fa-f]+)')
        # " 0001:0005a013       ?Foo@@YAXXZ    1005b013 f   obj:file"
        sym_re = re.compile(
            r'^\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{8,16})\s')

        with open(path, 'r', errors='replace') as f:
            for line in f:
                if self.base is None:
                    m = base_re.search(line)
                    if m:
                        self.base = int(m.group(1), 16)
                        continue
                m = sym_re.match(line)
                if m and self.base is not None:
                    name = m.group(1)
                    addr = int(m.group(2), 16)
                    if addr >= self.base:
                        self.symbols.append((addr - self.base, name))

        if self.base is None:
            raise SystemExit('no "Preferred load address" line in %s, so it is '
                             'not a linker map' % path)
        if not self.symbols:
            raise SystemExit('no symbols parsed out of %s' % path)

    def lookup(self, rva):
        """The symbol containing this RVA, and how far into it we are."""
        lo, hi = 0, len(self.symbols) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            if self.symbols[mid][0] <= rva:
                best = self.symbols[mid]
                lo = mid + 1
            else:
                hi = mid - 1
        if best is None:
            return None, 0
        return best[1], rva - best[0]


def undecorate(name):
    """Enough of MSVC name mangling to read a C++ function name at a glance."""
    if not name.startswith('?'):
        return name.lstrip('_')

    body = name[1:]
    # ?Name@Namespace@@YA... -> Namespace::Name
    at = body.find('@@')
    if at < 0:
        return name
    parts = [p for p in body[:at].split('@') if p]
    if not parts:
        return name
    return '::'.join(reversed(parts))


def report_one(sm, rva):
    name, delta = sm.lookup(rva)
    if name is None:
        return '0x%08X  (before the first symbol)' % rva
    return '0x%08X  %s+0x%X' % (rva, undecorate(name), delta)


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)

    sm = SymbolMap(argv[1])
    rest = argv[2:]

    if rest[0] == '--report':
        if len(rest) < 2:
            raise SystemExit('--report wants a log file')
        # Every "<module>+0xRVA" in the report, in order, deduplicated per line.
        frame_re = re.compile(r'\+0x([0-9A-Fa-f]{8})')
        with open(rest[1], 'r', errors='replace') as f:
            for line in f:
                m = frame_re.search(line)
                if not m:
                    continue
                rva = int(m.group(1), 16)
                name, delta = sm.lookup(rva)
                if name is None:
                    continue
                print('%s        -> %s+0x%X' % (line.rstrip(), undecorate(name), delta))
        return 0

    for arg in rest:
        print(report_one(sm, int(arg, 16)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
