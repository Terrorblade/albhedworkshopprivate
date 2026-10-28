#!/usr/bin/env python3
"""
savefile.py - the FINAL FANTASY X HD Remaster (PC) save file, parsed, verified and rebuilt.

The layout and the checksum are documented in reversing/SAVE_FILE.md. Short version:

  ffx_NNN (no extension), NNN 000..199, under
  <Documents>\\Square Enix\\FINAL FANTASY X&X-2 HD Remaster\\FINAL FANTASY X\\
  26,880 = 0x6900 bytes, no compression, no encryption:
    +0x0000  0x40   header, the slot list reads only this
    +0x0040  0x68C0 a byte for byte copy of the live save block g_ffxSaveData

  CRC-16, poly 0x1021, init 0xFFFF, result inverted, MSB first, BUT with table entry 0xFF left
  at zero (FFX_Crc16Ccitt 0x8B1450 builds the table with `cmp ecx, 0FFh / jb`). It covers block
  bytes 0x0000..0x64B8 (header +0x40..+0x64F8) with the dword at block +0x64B4 zeroed first, and
  the result is stored twice: the word at header +0x1A and the dword at block +0x64B4.

Usage
  python -I savefile.py info      <file>...             header fields and checksum state
  python -I savefile.py verify    [file|dir]...         every check the game makes, plus consistency
  python -I savefile.py roundtrip [file|dir]...         parse, re-serialise, compare byte for byte
  python -I savefile.py recompute <in> <out> [--sync-header]
                                                        fix the checksums after an edit, into <out>
                                                        (--sync-header also refreshes the header's
                                                        playtime and gil mirrors from the block)

With no file or directory, verify and roundtrip read the game's own save folder (found the way
the game finds it, SHGetFolderPathW(CSIDL_PERSONAL)). Nothing in that folder is ever written.
recompute refuses to write over any existing file, so it can never touch a real save in place.
info does not print the player's name unless --name is given.
"""

import argparse
import ctypes
import os
import re
import struct
import sys

FILE_SIZE = 0x6900          # 26,880, the fread / fwrite size everywhere
HEADER_SIZE = 0x40
BLOCK_SIZE = 0x68C0         # 26,816, sizeof g_ffxSaveData
BLOCK_CRC_FIELD = 0x64B4    # block offset of the dword copy of the CRC (g_ffxSaveBlockCrc)
PC_CRC_LENGTH = 0x64B8      # FFX_SaveFile_StampCrc / VerifyCrc: up to and including that dword
FULL_CRC_LENGTH = 0x68C0    # FFX_SaveFile_Serialize and the console-era module 15/16 verify
HEADER_VERSION = 256        # header +0x1C, hardcoded by FFX_SaveFile_Serialize
SLOT_COUNT = 200            # the scanner probes ffx_000..ffx_199, 000 is the autosave

LANGUAGES = ["jp", "en", "fr", "de", "it", "es", "kr", "ch"]

# header: (name, offset, struct format). Covers all 64 bytes, nothing is left as an anonymous gap.
HEADER_FIELDS = [
    ("counter", 0x00, "<I"),      # g_ffxSaveCounter + 1 at serialise time, clamped to 0x80000000
    ("kind", 0x04, "B"),          # Serialize arg 3. 1 on disk. VerifyCrc reuses it in memory as "verified"
    ("party", 0x05, "7s"),        # party chr ids for the slot icons, 0xFF padded
    ("language", 0x0C, "B"),      # FFX_GetLanguage() when written, 0 jp 1 en 2 fr 3 de 4 it 5 es 6 kr 7 ch
    ("pad0d", 0x0D, "3s"),        # never written, zero from the memset
    ("playtime", 0x10, "<I"),     # block +0xBC, seconds
    ("gil", 0x14, "<I"),          # block +0x3D48
    ("mapNameId", 0x18, "<H"),    # FFX_SaveData_GetMapNameId, the place name the slot list shows
    ("crc", 0x1A, "<H"),          # the CRC
    ("version", 0x1C, "<H"),      # 256, nothing reads it
    ("rikkuOutfit", 0x1E, "B"),   # FFX_SaveData_GetRikkuAltOutfit, block +0xD1
    ("envId", 0x1F, "B"),         # FFX_GetInternationalEnvId, a hardcoded 0 in this build
    ("name", 0x20, "32s"),        # character name record 0, NUL terminated, zero filled
]

_TABLE = None


def _crc_table():
    """The table exactly as FFX_Crc16Ccitt 0x8B1450 builds it. Entry 0xFF is never computed."""
    global _TABLE
    if _TABLE is None:
        t = [0] * 256
        for i in range(255):        # cmp ecx, 0FFh / jb at 0x8B1566: 0..254 only
            c = i << 8
            for _ in range(8):
                c = ((c << 1) ^ 0x1021) if c & 0x8000 else (c << 1)
                c &= 0xFFFF
            t[i] = c
        _TABLE = t
    return _TABLE


def ffx_crc16(data):
    t = _crc_table()
    c = 0xFFFF
    for b in data:
        c = (t[(b ^ (c >> 8)) & 0xFF] ^ (c << 8)) & 0xFFFF
    return c ^ 0xFFFF


def block_crc(block, length=PC_CRC_LENGTH):
    """CRC of a 0x68C0 block the way the game computes it: the dword at +0x64B4 counts as zero."""
    z = bytearray(block[:length])
    if length >= BLOCK_CRC_FIELD + 4:
        z[BLOCK_CRC_FIELD:BLOCK_CRC_FIELD + 4] = b"\0\0\0\0"
    return ffx_crc16(z)


class SaveFile:
    def __init__(self, header, block):
        self.header = header        # dict of HEADER_FIELDS
        self.block = bytearray(block)

    @classmethod
    def parse(cls, data):
        if len(data) != FILE_SIZE:
            raise ValueError("size %d, a save file is %d bytes" % (len(data), FILE_SIZE))
        h = {}
        for name, off, fmt in HEADER_FIELDS:
            h[name] = struct.unpack_from(fmt, data, off)[0]
        return cls(h, data[HEADER_SIZE:])

    def serialize(self):
        out = bytearray(FILE_SIZE)
        for name, off, fmt in HEADER_FIELDS:
            struct.pack_into(fmt, out, off, self.header[name])
        if len(self.block) != BLOCK_SIZE:
            raise ValueError("block is %d bytes, not %d" % (len(self.block), BLOCK_SIZE))
        out[HEADER_SIZE:] = self.block
        return bytes(out)

    # -- the checksum ---------------------------------------------------------------------------

    def block_crc_field(self):
        return struct.unpack_from("<I", self.block, BLOCK_CRC_FIELD)[0]

    def computed_crc(self, length=PC_CRC_LENGTH):
        return block_crc(self.block, length)

    def restamp(self):
        """What FFX_SaveFile_StampCrc 0x648F20 does before every write."""
        c = self.computed_crc()
        self.header["crc"] = c
        struct.pack_into("<I", self.block, BLOCK_CRC_FIELD, c)
        return c

    def game_accepts(self):
        """FFX_SaveFile_VerifyCrc 0x647D70 with the debug skip off. Returns (ok, reason)."""
        if self.header["crc"] != self.block_crc_field():
            return False, "header +0x1A %04X != block +0x64B4 dword %08X" % (self.header["crc"], self.block_crc_field())
        c = self.computed_crc()
        if c != self.header["crc"]:
            return False, "computed %04X != stored %04X" % (c, self.header["crc"])
        return True, "crc %04X" % c

    # -- consistency the game does not check, but its own writer guarantees -------------------

    def consistency(self):
        h, b = self.header, self.block
        notes = []

        def chk(ok, text):
            notes.append(("ok  " if ok else "WARN") + " " + text)

        chk(h["version"] == HEADER_VERSION, "version +0x1C == 256")
        chk(h["pad0d"] == b"\0\0\0", "header +0x0D..+0x0F zero")
        chk(h["playtime"] == struct.unpack_from("<I", b, 0xBC)[0], "header playtime == block +0xBC")
        chk(h["gil"] == struct.unpack_from("<I", b, 0x3D48)[0], "header gil == block +0x3D48")
        chk(h["envId"] == 0, "header +0x1F (international env id) == 0")
        name = h["name"]
        nul = name.find(b"\0")
        chk(nul >= 0 and name[nul:].strip(b"\0") == b"", "name NUL terminated, zero filled")
        chk(b[0x2A] == 1, "block +0x2A loaded-from-file flag == 1")
        chk(h["language"] < len(LANGUAGES), "language byte in range")
        tail = b[PC_CRC_LENGTH:]
        chk(not any(tail), "block +0x64B8..+0x68C0 zero (outside the PC CRC)")
        return notes


# -- files ---------------------------------------------------------------------------------------

SLOT_RE = re.compile(r"^ffx_(\d{3})$")


def game_save_dir():
    """The folder the game uses: sub_6F0330 0x6F0330 builds it from SHGetFolderPathW(CSIDL_PERSONAL)."""
    docs = None
    if os.name == "nt":
        buf = ctypes.create_unicode_buffer(520)
        if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buf) == 0:
            docs = buf.value
    if not docs:
        docs = os.path.join(os.path.expanduser("~"), "Documents")
    return os.path.join(docs, "Square Enix", "FINAL FANTASY X&X-2 HD Remaster", "FINAL FANTASY X")


def collect(paths):
    """Expand directories to the ffx_000..ffx_199 files the game would probe."""
    if not paths:
        paths = [game_save_dir()]
    out = []
    for p in paths:
        if os.path.isdir(p):
            for n in sorted(os.listdir(p)):
                m = SLOT_RE.match(n)
                if m and int(m.group(1)) < SLOT_COUNT and os.path.isfile(os.path.join(p, n)):
                    out.append(os.path.join(p, n))
        else:
            out.append(p)
    return out


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


# -- commands ------------------------------------------------------------------------------------

def cmd_info(a):
    for p in collect(a.files):
        sf = SaveFile.parse(read_bytes(p))
        h = sf.header
        ok, why = sf.game_accepts()
        party = " ".join("--" if x == 0xFF else "%02X" % x for x in h["party"])
        lang = LANGUAGES[h["language"]] if h["language"] < len(LANGUAGES) else "?%d" % h["language"]
        print(os.path.basename(p))
        print("  counter %d  kind %d  language %s  version %d  envId %d  rikkuOutfit %d" % (
            h["counter"], h["kind"], lang, h["version"], h["envId"], h["rikkuOutfit"]))
        print("  party   %s" % party)
        print("  playtime %d s  gil %d  mapNameId %d" % (h["playtime"], h["gil"], h["mapNameId"]))
        nul = h["name"].find(b"\0")
        nlen = nul if nul >= 0 else len(h["name"])
        if a.name:
            print("  name    %s (%d bytes, FFX glyph encoded)" % (h["name"][:nlen].hex(" "), nlen))
        else:
            print("  name    %d bytes (pass --name to show them)" % nlen)
        print("  crc     header %04X  block dword %08X  -> %s, %s" % (
            h["crc"], sf.block_crc_field(), "accepted" if ok else "REJECTED", why))
        print("  crc over the whole block (in-game serializer, module 15/16): %04X" % sf.computed_crc(FULL_CRC_LENGTH))
    return 0


def cmd_verify(a):
    files = collect(a.files)
    bad = 0
    for p in files:
        try:
            sf = SaveFile.parse(read_bytes(p))
        except ValueError as e:
            print("%s: FAIL %s" % (os.path.basename(p), e))
            bad += 1
            continue
        ok, why = sf.game_accepts()
        notes = sf.consistency()
        warns = [n for n in notes if n.startswith("WARN")]
        print("%s: %s (%s)%s" % (os.path.basename(p), "ok" if ok else "FAIL", why,
                                 "" if not warns else ", %d warning(s)" % len(warns)))
        if a.verbose or warns:
            for n in notes:
                if a.verbose or n.startswith("WARN"):
                    print("    " + n)
        bad += 0 if ok else 1
    print("%d file(s), %d accepted by the game's check, %d rejected" % (len(files), len(files) - bad, bad))
    return 1 if bad or not files else 0


def cmd_roundtrip(a):
    files = collect(a.files)
    bad = 0
    for p in files:
        raw = read_bytes(p)
        try:
            sf = SaveFile.parse(raw)
        except ValueError as e:
            print("%s: FAIL %s" % (os.path.basename(p), e))
            bad += 1
            continue
        again = sf.serialize()
        same = again == raw
        # a second, harder pass: throw the stored CRC away, restamp it, and still get the file back
        sf2 = SaveFile.parse(raw)
        sf2.header["crc"] = 0
        struct.pack_into("<I", sf2.block, BLOCK_CRC_FIELD, 0xDEADBEEF)
        sf2.restamp()
        restamped = sf2.serialize() == raw
        if not same:
            diff = next(i for i in range(FILE_SIZE) if again[i] != raw[i])
            print("%s: FAIL first difference at +0x%04X" % (os.path.basename(p), diff))
        else:
            print("%s: %s" % (os.path.basename(p), "byte equal, restamped crc byte equal" if restamped
                               else "byte equal, BUT a restamp changes it"))
        bad += 0 if (same and restamped) else 1
    print("%d file(s), %d byte equal, %d failed" % (len(files), len(files) - bad, bad))
    return 1 if bad or not files else 0


def cmd_recompute(a):
    src, dst = os.path.abspath(a.input), os.path.abspath(a.output)
    if os.path.exists(dst):
        raise SystemExit("refusing: %s exists. recompute only ever writes a new file" % dst)
    if os.path.normcase(src) == os.path.normcase(dst):
        raise SystemExit("refusing: output is the input")
    sf = SaveFile.parse(read_bytes(src))
    before = sf.header["crc"], sf.block_crc_field()
    if a.sync_header:
        # the two header mirrors FFX_SaveFile_Serialize copies straight out of the block
        sf.header["playtime"] = struct.unpack_from("<I", sf.block, 0xBC)[0]
        sf.header["gil"] = struct.unpack_from("<I", sf.block, 0x3D48)[0]
    after = sf.restamp()
    data = sf.serialize()
    with open(dst, "xb") as f:      # "x" fails if the file appeared in the meantime
        f.write(data)
    print("%s -> %s: crc was header %04X block %08X, now %04X" % (src, dst, before[0], before[1], after))
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description="Parse, verify and rebuild FFX HD Remaster PC save files.")
    sub = p.add_subparsers(dest="cmd", required=True)
    sp = sub.add_parser("info")
    sp.add_argument("files", nargs="*")
    sp.add_argument("--name", action="store_true", help="also print the name bytes")
    sp.set_defaults(fn=cmd_info)
    sp = sub.add_parser("verify")
    sp.add_argument("files", nargs="*")
    sp.add_argument("--verbose", "-v", action="store_true")
    sp.set_defaults(fn=cmd_verify)
    sp = sub.add_parser("roundtrip")
    sp.add_argument("files", nargs="*")
    sp.set_defaults(fn=cmd_roundtrip)
    sp = sub.add_parser("recompute")
    sp.add_argument("input")
    sp.add_argument("output")
    sp.add_argument("--sync-header", action="store_true",
                    help="also refresh the header playtime and gil from the block")
    sp.set_defaults(fn=cmd_recompute)
    a = p.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
