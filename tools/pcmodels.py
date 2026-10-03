#!/usr/bin/env python3
"""
List the playable-category (pc, "c") character models FFX ships, with the motion
sets each one actually has.

Why this exists: FFX_Ch_Allocate takes a chr id, where
    chrId = (category << 12) | number
and category 0 is "c" for playable characters. So spawning a different character
is just a different integer. The catch is that **not every c-series model has a
field motion set**, and one without field locomotion would spawn as a sliding
T-pose. This tells you which ids are safe to spawn and walk around.

The test is the motion id tables in the .chr itself. FFX_Mot_SetByModeIndex reads
    *(CHRDATA.m_blobBase + 16 + 8*g_ffxMotModeToSetSlot[mode])
and g_ffxMotModeToSetSlot 0xC49784 is BYTE[4] = {5,6,7,8}, so .chr sections 5 to 8
are the four mode tables: 5 field, 6 field battle, 7 swim, 8 swim battle. A
section's aux field is its motion id count, so a non-zero section 5 means the
model has field locomotion.

Usage:
    python pcmodels.py              the table
    python pcmodels.py --all        every category, not just pc
    python pcmodels.py --ids        just the spawnable ids, one per line
"""

import argparse
import re
import sys

import vbf
from chrfile import ChrFile

MODE_NAMES = {5: "field", 6: "fieldbtl", 7: "swim", 8: "swimbtl"}

CATEGORY = {"c": (0, "pc"), "m": (1, "mon"), "n": (2, "npc"), "s": (3, "sum"),
            "w": (4, "wep"), "f": (5, "obj"), "k": (6, "skl")}

# What the c-series families look like, from file size and numbering. The family
# split is observed, the identities of individual slots beyond c001 are NOT
# established here and are deliberately left blank rather than guessed.
FAMILY_NOTES = [
    (1,   8,   "main party, field models"),
    (41,  51,  "extra or story field models"),
    (101, 108, "high-detail variants of 1..8, roughly 2x the file size"),
    (121, 122, "high-detail extras"),
    (307, 307, "one-off"),
    (901, 908, "900-series variants of 1..8"),
    (921, 922, "900-series extras"),
    (999, 999, "one-off, often a test or debug slot"),
]


def family_of(num):
    for lo, hi, note in FAMILY_NOTES:
        if lo <= num <= hi:
            return note
    return ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vbf", help="path to the .vbf (default: the Steam FFX one)")
    ap.add_argument("--all", action="store_true",
                    help="every category, not just the playable c-series")
    ap.add_argument("--ids", action="store_true",
                    help="print only the spawnable chr ids, one per line")
    args = ap.parse_args(argv)

    ar = vbf.VbfArchive(args.vbf or vbf.DEFAULT_VBF)

    want = r"/([cmnswfk])(\d{3})/mdl/\1\2\.chr$" if args.all else r"/c(\d{3})/mdl/c\1\.chr$"
    pat = re.compile(want)

    rows = []
    for ent in ar.entries:
        path = ent.name
        m = pat.search(path)
        if not m:
            continue
        if args.all:
            letter, numtxt = m.group(1), m.group(2)
        else:
            letter, numtxt = "c", m.group(1)
        num = int(numtxt)
        cat, catname = CATEGORY[letter]
        chr_id = (cat << 12) | num

        try:
            cf = ChrFile.from_archive(path, ar)
        except Exception as exc:                      # noqa: BLE001
            rows.append((chr_id, letter, num, catname, None, str(exc), path))
            continue

        counts = {}
        for idx in (5, 6, 7, 8):
            sec = cf.sections[idx] if idx < len(cf.sections) else None
            counts[idx] = sec.aux if (sec is not None and sec.present) else 0
        rows.append((chr_id, letter, num, catname, counts, None, path))

    rows.sort()

    if args.ids:
        for chr_id, _l, _n, _c, counts, err, _p in rows:
            if counts and counts[5] > 0:
                print(chr_id)
        return 0

    print("chr ids for FFX_Ch_Allocate, with the motion sets each model ships")
    print("id is (category << 12) | number, so the c-series is just its number")
    print()
    hdr = "%-7s %-6s %7s %9s %6s %9s  %-5s %s" % (
        "id", "name", "field", "fieldbtl", "swim", "swimbtl", "walk?", "family")
    print(hdr)
    print("-" * len(hdr))

    spawnable = 0
    for chr_id, letter, num, _catname, counts, err, _path in rows:
        name = "%s%03d" % (letter, num)
        if err:
            print("%-7d %-6s  PARSE FAILED: %s" % (chr_id, name, err))
            continue
        ok = counts[5] > 0
        if ok:
            spawnable += 1
        print("%-7d %-6s %7d %9d %6d %9d  %-5s %s" % (
            chr_id, name, counts[5], counts[6], counts[7], counts[8],
            "yes" if ok else "NO", family_of(num) if letter == "c" else ""))

    print()
    print("%d of %d models have a field motion set, so %d are safe to spawn and walk."
          % (spawnable, len(rows), spawnable))
    print()
    print("Reading the columns: 'field' is the motion id count in .chr section 5, which is")
    print("the mode 0 table FFX_Ch_AutoLocomotionAnim indexes for idle, walk and run via")
    print("m_slots[0..2]. A 0 there means the model has no field locomotion at all, so it")
    print("would spawn and then slide without animating. 'walk?' is just field > 0.")
    print()
    print("CONFIRMED: c001 is Tidus. FFX_Ch_BindChrData 0x826070 strcmp's CHRDATA.m_name")
    print("against \"c001\" and \"c101\" and caches the CHR in g_ffxTidusChr 0x12FBC60, which")
    print("is the engine's own 'this is the main character' test. The identities of the")
    print("other slots are NOT established and are deliberately not guessed here.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
