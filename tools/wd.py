#!/usr/bin/env python3
"""
wd.py - census of the PS2 "WD" wave dictionaries in FINAL FANTASY X HD Remaster.

The 865 .wd files ship under ffx_ps2/ffx/proj/sound/ in FFX_Data.vbf. The PC build never opens
them (reversing/WAVE_DICTIONARIES.md has the proof), so this is not a loader for the recreation.
It is the checker behind that proof: it reads every bank in memory through vbf.py, checks the
header and the sample data, matches the banks against the PS2 sector index that ships beside
them and against the FMOD projects, and finds the byte identical copies embedded in the battle
files and the magic DLLs, whose 4 byte header is the only part of the format the PC reads.
Nothing is written to disk and no sample data is printed.

Usage
  python -I wd.py census                  every shipped .wd: header, PS-ADPCM, sector index,
                                          FMOD project ids
  python -I wd.py embedded [--magic DIR]  WD banks inside battle .bin files and magic DLLs
  python -I wd.py show <bank>             one bank's header and region table

=============================================================================
WD AS SHIPPED  (every field below is checked by `census`)
=============================================================================
  +0x00 char[2] "WD"
  +0x02 u16     bank number. Equals the number in wave%04d.wd for all 864 numbered files
  +0x04 u32     size of the sample data, which is the tail of the file
  +0x08 u32     instrument count
  +0x0C u32     region count
  +0x10 u8[16]  zero
  +0x20 u32[instruments] offset of each instrument's first region, padded to 16 bytes.
                0xFFFFFFFF for an empty instrument
  then          regions, 0x20 bytes each:
                  +0x00 u8[4]  flags. Byte 1 is 1 on the first region of an instrument [I]
                  +0x04 u32    sample offset into the sample data
                  +0x08 u32    loop offset, 0 or equal to +0x04 on one shot sounds [I]
                  +0x0C u16[2] SPU ADSR1, ADSR2
                  +0x10 ...    tuning, key range, volume, pan [I, not decoded]
  then          the sample data, PS2 SPU ADPCM: 16 byte frames, byte 0 is predictor << 4 |
                shift (predictor 0..4, shift 0..12), byte 1 the loop flags (0..7)
"""

import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import vbf  # noqa: E402

WD_ROOT = "ffx_ps2/ffx/proj/sound/"
SECTOR_INDEX = WD_ROOT + "wave/wave_wd.sec"
SFX_ROOT = "ffx_data/gamedata/ps3data/sound_pc/sfx/"
BATTLE_ROOTS = ("ffx_ps2/ffx/master/jppc/battle/mon/", "ffx_ps2/ffx/master/jppc/battle/wep/",
                "ffx_ps2/ffx/master/jppc/battle/scn/")
DEFAULT_MAGIC = os.path.join(os.path.dirname(vbf.DEFAULT_VBF), "..", "magicFiles", "FFX")
SECTOR = 2048
# sub_887450 0x887450 skips a WD chunk whose size argument is 0x30000 or more
CHUNK_SIZE_GATE = 0x30000


class WdError(Exception):
    pass


class Wd(object):
    """Header parse of one WD bank held in memory at offset `at`."""

    def __init__(self, data, at=0, length=None):
        if data[at:at + 2] != b"WD":
            raise WdError("no WD magic")
        self.bank, self.wave_size, self.instruments, self.regions = struct.unpack_from(
            "<HIII", data, at + 2)
        if not (0 < self.instruments < 256 and 0 < self.regions < 4096):
            raise WdError("counts out of range")
        table = at + 0x20
        self.inst_offsets = list(struct.unpack_from("<%dI" % self.instruments, data, table))
        # 818 banks pad the instrument table to 16 bytes, 23 start the regions right after it
        live = [o for o in self.inst_offsets if o != 0xFFFFFFFF]
        self.padded = not live or min(live) % 16 == 0
        self.region_start = 0x20 + 4 * self.instruments
        if self.padded:
            self.region_start = (self.region_start + 15) // 16 * 16
        self.header_size = self.region_start + 0x20 * self.regions
        self.zero_pad = data[at + 0x10:at + 0x20] == b"\0" * 16
        self.region_rows = []
        for i in range(self.regions):
            o = at + self.region_start + 0x20 * i
            flags = bytes(data[o:o + 4])
            start, loop, adsr1, adsr2 = struct.unpack_from("<IIHH", data, o + 4)
            self.region_rows.append((flags, start, loop, adsr1, adsr2))
        self.length = length if length is not None else len(data) - at
        # the sample data follows the region table. The size field can run past the end of
        # the file by up to 60 bytes, so the file length bounds the data, not the field
        self.data_start = (self.header_size + 15) // 16 * 16 if self.padded else self.header_size
        self.overrun = self.wave_size - (self.length - self.data_start)

    def body(self, data, at=0):
        return data[at + self.data_start:at + self.length]


def adpcm_bad_frames(body):
    """Count 16 byte frames that cannot be SPU ADPCM, and frames carrying the end flag."""
    bad = ends = 0
    for o in range(0, len(body) - 15, 16):
        h, f = body[o], body[o + 1]
        if (h >> 4) > 4 or (h & 15) > 12 or f > 7:
            bad += 1
        if f & 1:
            ends += 1
    return bad, ends, len(body) // 16


def shipped(ar):
    """bank -> {"root": entry, "us": entry}, plus the odd named files."""
    banks = {}
    odd = []
    for e in ar.entries:
        if not e.name.startswith(WD_ROOT) or not e.name.endswith(".wd"):
            continue
        m = re.search(r"/sound/(us/)?wave/wave(\d{4})\.wd$", e.name)
        if m:
            banks.setdefault(int(m.group(2)), {})["us" if m.group(1) else "root"] = e
        else:
            odd.append(e)
    return banks, odd


def ranges(xs):
    xs = sorted(xs)
    out = []
    for x in xs:
        if out and x == out[-1][1] + 1:
            out[-1][1] = x
        else:
            out.append([x, x])
    return ", ".join("%d" % a if a == b else "%d-%d" % (a, b) for a, b in out)


def cmd_census(a, ar):
    banks, odd = shipped(ar)
    files = sum(len(v) for v in banks.values()) + len(odd)
    print("files %d: %d numbered banks, %d also under us/wave, %d odd (%s)"
          % (files, len(banks), sum(1 for v in banks.values() if "us" in v), len(odd),
             ", ".join(e.name[len(WD_ROOT):] for e in odd)))

    ok = 0
    problems = []
    frames = bad_frames = unsorted = 0
    overrun = {}
    for n, v in sorted(banks.items()):
        for tree, e in sorted(v.items()):
            d = ar.read(e)
            try:
                w = Wd(d)
            except WdError as ex:
                problems.append("%s: %s" % (e.name, ex))
                continue
            why = []
            if w.bank != n:
                why.append("bank field %d" % w.bank)
            if not w.zero_pad:
                why.append("bytes 0x10..0x1F not zero")
            if w.data_start < w.header_size:
                why.append("sample data overlaps the header")
            starts = [r[1] for r in w.region_rows]
            if any(s >= w.wave_size for s in starts):
                why.append("a region's sample offset is past the sample data")
            if starts != sorted(starts):
                unsorted += 1
            live = [o for o in w.inst_offsets if o != 0xFFFFFFFF]
            if any(not (w.region_start <= o < w.header_size) or (o - w.region_start) % 0x20
                   for o in live):
                why.append("instrument offset not on a region")
            overrun[w.overrun] = overrun.get(w.overrun, 0) + 1
            b, ends, nf = adpcm_bad_frames(w.body(d))
            frames += nf
            bad_frames += b
            if b:
                why.append("%d of %d frames are not SPU ADPCM" % (b, nf))
            if ends < 1:
                why.append("no end flag")
            if why:
                problems.append("%s: %s" % (e.name, ", ".join(why)))
            else:
                ok += 1
    for e in odd:
        d = ar.read(e)
        try:
            w = Wd(d)
            problems.append("%s: bank field %d, sample data size %d against a %d byte file"
                            % (e.name, w.bank, w.wave_size, len(d)))
        except WdError as ex:
            problems.append("%s: %s" % (e.name, ex))
    print("header and sample data clean: %d, frames checked %d, not ADPCM %d"
          % (ok, frames, bad_frames))
    print("  regions not in sample order (shared or reordered samples): %d banks" % unsorted)
    print("  size field minus the bytes after the header, banks per value: %s"
          % ", ".join("%d: %d" % kv for kv in sorted(overrun.items())))
    for p in problems:
        print("  " + p)

    sec = ar.lookup(SECTOR_INDEX)
    if sec is None:
        print("sector index %s: missing" % SECTOR_INDEX)
    else:
        raw = ar.read(sec)
        idx = struct.unpack("<%dI" % (len(raw) // 4), raw)
        present = [i for i, s in enumerate(idx) if s != 0xFFFFFFFF]
        order = sorted(present, key=lambda i: idx[i])
        match = {"us": 0, "root": 0}
        other = []
        for i, j in zip(order, order[1:]):
            gap = idx[j] - idx[i]
            v = banks.get(i, {})
            # the US build packs the us/wave copy where there is one
            for tree in ("us", "root"):
                if tree in v and (v[tree].size + SECTOR - 1) // SECTOR == gap:
                    match[tree] += 1
                    break
            else:
                other.append(i)
        print("sector index: %d slots, %d banks present, ids ascending with sectors: %s"
              % (len(idx), len(present), order == sorted(present)))
        print("  next start - start == the shipped file in sectors: %d (us/wave %d, wave %d),"
              " not: %d (%s)" % (match["us"] + match["root"], match["us"], match["root"],
                                 len(other), ranges(other)))
        print("  in the index, not shipped: %s" % ranges(set(present) - set(banks)))
        print("  shipped, not in the index: %s" % ranges(set(banks) - set(present)))

    fev = set()
    for e in ar.entries:
        m = re.match(re.escape(SFX_ROOT) + r"us/(\d{4})\.fev$", e.name)
        if m:
            fev.add(int(m.group(1)))
    print("FMOD SFX projects %d. Same number as a bank: %d. Bank only: %d. Project only: %s"
          % (len(fev), len(fev & set(banks)), len(set(banks) - fev), ranges(fev - set(banks))))
    print("  bank only: %s" % ranges(set(banks) - fev))
    return 0


def find_embedded(data, known):
    """Offsets of WD banks inside a larger file, 16 byte aligned or not."""
    out = []
    i = data.find(b"WD")
    while i != -1:
        if i + 0x30 <= len(data):
            bank, _, ninst, nreg = struct.unpack_from("<HIII", data, i + 2)
            if 0 < ninst < 256 and 0 < nreg < 4096 and bank in known:
                first = struct.unpack_from("<I", data, i + 0x20)[0]
                if first == 0x20 + (4 * ninst + 15) // 16 * 16:
                    out.append((i, bank))
        i = data.find(b"WD", i + 1)
    return out


def cmd_embedded(a, ar):
    banks, _ = shipped(ar)
    root = {n: v["root"] for n, v in banks.items() if "root" in v}
    cache = {}

    def same(data, at, bank):
        if bank not in cache:
            cache[bank] = ar.read(root[bank])
        w = cache[bank]
        return data[at:at + len(w)] == w, len(w)

    for prefix in BATTLE_ROOTS:
        files = with_wd = copies = identical = 0
        used = set()
        biggest = 0
        for e in ar.entries:
            if not e.name.startswith(prefix) or e.size == 0:
                continue
            files += 1
            d = ar.read(e)
            hits = find_embedded(d, root)
            if hits:
                with_wd += 1
            for at, bank in hits:
                copies += 1
                used.add(bank)
                eq, size = same(d, at, bank)
                identical += eq
                biggest = max(biggest, size)
        print("%s: %d files, %d with a WD bank, %d banks, %d byte identical to wave/wave%%04d.wd,"
              " %d distinct, largest %d bytes (gate 0x%X)"
              % (prefix, files, with_wd, copies, identical, len(used), biggest, CHUNK_SIZE_GATE))

    magic = os.path.normpath(a.magic)
    if not os.path.isdir(magic):
        print("magic DLLs: %s not found, pass --magic" % magic)
        return 0
    files = with_wd = copies = identical = 0
    used = set()
    biggest = 0
    for fn in sorted(os.listdir(magic)):
        if not fn.lower().endswith(".dll"):
            continue
        with open(os.path.join(magic, fn), "rb") as f:
            d = f.read()
        files += 1
        hits = find_embedded(d, root)
        if hits:
            with_wd += 1
        for at, bank in hits:
            copies += 1
            used.add(bank)
            eq, size = same(d, at, bank)
            identical += eq
            biggest = max(biggest, size)
    print("magic DLLs: %d files, %d with a WD bank, %d banks, %d byte identical, %d distinct,"
          " largest %d bytes" % (files, with_wd, copies, identical, len(used), biggest))
    return 0


def cmd_show(a, ar):
    banks, _ = shipped(ar)
    v = banks.get(a.bank)
    if not v:
        print("no wave%04d.wd" % a.bank)
        return 1
    for tree, e in sorted(v.items()):
        d = ar.read(e)
        w = Wd(d)
        b, ends, nf = adpcm_bad_frames(w.body(d))
        print("%s  %d bytes, bank %d, %d instruments, %d regions, header %d, sample data %d at"
              " 0x%X, %d frames, %d end flags, %d not ADPCM"
              % (e.name, len(d), w.bank, w.instruments, w.regions, w.header_size, w.wave_size,
                 w.data_start, nf, ends, b))
        print("  instrument offsets: %s" % " ".join(
            "-" if o == 0xFFFFFFFF else "%X" % o for o in w.inst_offsets))
        for i, (flags, start, loop, adsr1, adsr2) in enumerate(w.region_rows):
            print("  region %3d  flags %s  sample 0x%06X  loop 0x%06X  adsr %04X %04X"
                  % (i, flags.hex(), start, loop, adsr1, adsr2))
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description="Census of the PS2 WD wave dictionaries.")
    p.add_argument("--vbf", default=vbf.DEFAULT_VBF)
    sub = p.add_subparsers(dest="cmd", required=True)
    sp = sub.add_parser("census")
    sp.set_defaults(fn=cmd_census)
    sp = sub.add_parser("embedded")
    sp.add_argument("--magic", default=DEFAULT_MAGIC)
    sp.set_defaults(fn=cmd_embedded)
    sp = sub.add_parser("show")
    sp.add_argument("bank", type=int)
    sp.set_defaults(fn=cmd_show)
    a = p.parse_args(argv)
    with vbf.VbfArchive(a.vbf) as ar:
        return a.fn(a, ar) or 0


if __name__ == "__main__":
    sys.exit(main())
