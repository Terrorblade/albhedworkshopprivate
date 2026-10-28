"""
List data/metamenu.vbf, the launcher's archive, with what opens each file and when.

metamenu.vbf is read by FFX&X-2_LAUNCHER.exe (a .NET WinForms assembly, GameLauncher.exe) and by
nothing else. FFX.exe, FFX-2.exe and FFX&X-2_Will.exe each mount only their own *_Data.vbf. See
reversing/METAMENU.md for the proof and for the launcher methods named below.

The loader column is not a guess per row. The launcher's only route into the archive is
Program::LoadVBFData, and every path it is handed is either a string constant in the IL (the
CONSTANT_PATHS table below, each with the method that holds it) or built from
"MetaMenu/PS3Data/MenuMetaMenu/" + language dir + "/" + an <Image> value of MenuActiveRects.xml,
or "MetaMenu/PS3Data/MenuCredit/" + language dir + "/credits_logo.png". This script reads that XML
out of the archive at run time, in memory, builds the full reachable set and checks every entry
against it. An entry the set does not cover is reported as never opened.

The script reads the user's install and writes nothing. It prints names, sizes, types and PNG
dimensions only, never file contents.

Usage:
    python -I metamenu.py [--vbf PATH] [--md] [--summary]
"""

import argparse
import collections
import os
import struct
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vbf import VbfArchive  # noqa: E402

DEFAULT_VBF = r"G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\data\metamenu.vbf"

# Program::get_LanguagePath (0x060003AA) maps the language code to the folder name. The codes come
# from Program::InitGlobalConfig (GetUserDefaultLangID) and Program::LoadIniFile (GameSetting.ini
# "Language"), and the Settings combobox offers exactly these eight.
LANGUAGE_DIRS = {"en": "us", "es": "sp", "fr": "fr", "de": "de", "it": "it", "jp": "jp", "kr": "kr", "ch": "ch"}

# Every string constant in the launcher IL that names an archive path, with the method that loads it
# and when that method runs.
CONSTANT_PATHS = [
    ("MetaMenu/PS3Data/MenuMetaMenu/MenuActiveRects.xml", "MainForm::InitializeActiveRect", "start, and on Settings close when the language changed"),
    ("MetaMenu/PS3Data/MenuMetaMenu/SettingsLoc.xml", "MainForm::MainForm_Load, ConfigForm::ConfigForm_Load, MainForm::OnConfigClicked", "start (window title), Settings open (labels), Settings close when the language changed"),
    ("MetaMenu/PS3Data/MenuMetaMenu/metamenu.ogg", "MainForm::InitializeFmodResources", "start, looping BGM"),
    ("MetaMenu/PS3Data/MenuMetaMenu/metaMenu_SFX_Comfirme.VAG", "MainForm::InitializeFmodResources", "start, confirm SE"),
    ("MetaMenu/PS3Data/MenuMetaMenu/metaMenu_SFX_Move.VAG", "MainForm::InitializeFmodResources", "start, cursor move SE"),
    ("MetaMenu/PS3Data/MenuMetaMenu/speaker_on.png", "ConfigForm::LoadConfiguration, ConfigForm::pictureBox2_Click", "Settings open when not muted, mute toggled"),
    ("MetaMenu/PS3Data/MenuMetaMenu/speaker_off.png", "ConfigForm::LoadConfiguration, ConfigForm::pictureBox2_Click", "Settings open when muted, mute toggled"),
    ("MetaMenu/PS3Data/MenuCredit/credits_1280.png", "CreditForm::.ctor", "Credits 2014 button"),
    ("MetaMenu/PS3Data/MenuCredit/scollbar.png", "CreditForm::.ctor", "Credits 2014 button"),
    ("MetaMenu/PS3Data/MenuCredit/button_down.png", "CreditForm::.ctor", "Credits 2014 button"),
    ("MetaMenu/PS3Data/MenuCredit/button_over.png", "CreditForm::.ctor", "Credits 2014 button"),
    ("MetaMenu/PS3Data/MenuCredit/button_up.png", "CreditForm::.ctor", "Credits 2014 button"),
    ("MetaMenu/GameData/PS3Data/MenuCreditsText/credit.dat", "CreditForm::.ctor (StaffBinParser::Load)", "Credits 2014 button"),
]

ACTIVE_RECTS = "metamenu/ps3data/menumetamenu/menuactiverects.xml"


def classify(data):
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        w, h = struct.unpack(">II", data[16:24])
        return "PNG", "%dx%d" % (w, h)
    if data[:4] == b"OggS":
        return "OGG", "Ogg Vorbis"
    if data[:4] == b"VAGp":
        return "VAG", "PS ADPCM"
    if data.lstrip(b"\xef\xbb\xbf")[:5] == b"<?xml":
        return "XML", "UTF-8 with BOM" if data[:3] == b"\xef\xbb\xbf" else "UTF-8"
    return "DAT", "launcher staff table"


def reachable(ar):
    """Map every archive key the launcher can ask for to (loader, when)."""
    out = {}
    for path, loader, when in CONSTANT_PATHS:
        out[path.lower()] = (loader, when)
    for code, d in LANGUAGE_DIRS.items():
        out[("MetaMenu/PS3Data/MenuCredit/%s/credits_logo.png" % d).lower()] = (
            "CreditForm::.ctor", "Credits 2014 button, language %s" % code)
    # MenuActiveRects.xml: Background/Image and ActiveRect/Image, each with a small="true" twin.
    # Program::get_XmlNodeSelectorCondition adds "and not(@small)" unless the working area height is
    # under 720, and the small twin is first in document order, so both variants are reachable.
    root = ET.fromstring(ar.read(ACTIVE_RECTS).decode("utf-8-sig"))
    images = []
    for img in root.iter("Image"):
        images.append((img.text or "").strip())
    for code, d in LANGUAGE_DIRS.items():
        for name in images:
            small = name.lower().startswith("360p/")
            when = "start, language %s, %s" % (code, "work area height < 720" if small else "work area height >= 720")
            out[("MetaMenu/PS3Data/MenuMetaMenu/%s/%s" % (d, name)).lower()] = ("MainForm::InitializeActiveRect", when)
    return out, len(images)


def main(argv=None):
    ap = argparse.ArgumentParser(description="List metamenu.vbf with loaders.")
    ap.add_argument("--vbf", default=DEFAULT_VBF)
    ap.add_argument("--md", action="store_true", help="print a Markdown table")
    ap.add_argument("--summary", action="store_true", help="counts only")
    a = ap.parse_args(argv)
    with VbfArchive(a.vbf) as ar:
        if not ar.verify_header_md5():
            print("header md5 does not verify")
            return 1
        reach, nimg = reachable(ar)
        rows = []
        for e in sorted(ar.entries, key=lambda x: x.name):
            kind, info = classify(ar.read(e))
            loader, when = reach.get(e.name, ("never", "no path in the launcher reaches it"))
            rows.append((e.name, kind, info, e.size, loader, when))
        unmatched = sorted(set(reach) - set(e.name for e in ar.entries))
    if not a.summary:
        if a.md:
            print("| # | path | type | detail | bytes | loaded by | when |")
            print("|---|---|---|---|---|---|---|")
            for i, r in enumerate(rows, 1):
                print("| %d | `%s` | %s | %s | %d | %s | %s |" % ((i,) + r))
        else:
            for r in rows:
                print("%-62s %-4s %-14s %9d  %s  [%s]" % r)
    by_type = collections.Counter(r[1] for r in rows)
    bytes_type = collections.Counter()
    for r in rows:
        bytes_type[r[1]] += r[3]
    by_loader = collections.Counter(r[4] for r in rows)
    print()
    print("%d entries, %d bytes, %d ActiveRects Image names" % (len(rows), sum(r[3] for r in rows), nimg))
    for k, v in by_type.most_common():
        print("  type   %-4s %4d  %10d bytes" % (k, v, bytes_type[k]))
    for k, v in by_loader.most_common():
        print("  loader %4d  %s" % (v, k))
    if unmatched:
        print("paths the launcher can ask for that the archive lacks: %d" % len(unmatched))
        for p in unmatched:
            print("  " + p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
