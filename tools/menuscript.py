"""
Read menu/menu_script.bin, menu/system_script.bin and menu/battle_script.bin, the PS2 menu layout
scripts, and draw their rectangles.

All three files share one format, read off the FFX.exe library at 0xA7B220..0xA7C8C0. See
reversing/MENU_SCRIPT.md for the proof of every field. In short:

    0x00  u32 value_bucket[16]    hash = xor of the name's bytes & 15
    0x40  u32 scene_bucket[16]    same hash
    0x80  u32 sequence_table      offset of u32 sequence offsets
    0x84  u32 zero[3]             never read
    0x90  u32 draw_group[16]      a draw priority, a list of scene entry offsets
    value bucket:  u32 count, then per value u32 (name_offset:24 | value_count:8), value_count u32
    scene bucket:  u32 count, then per scene u32 name_offset, u32 track_list | flags, s16 x, s16 y
    track list:    u32 count, then 20 byte tracks, u32 sequence | flags then run time state
    sequence:      u16 words, op = word & 0x7FF, flags in the top bits, ends with op 0

A value is what the menu code asks for by name (rect_, pos_, col_, num_). The game's handle for a
value is its entry offset, which is what FFX_MenuScript_FindValue returns, so `list` prints it. A
scene is a keyframed animation of 2D primitives, played by FFX_MenuScript_StepTrack inside the draw
call. `render` draws value rectangles and points, or the primitives a scene draws at one frame.

The game loads the jppc copy of menu_script.bin and system_script.bin for every language (path
table kind 18 entries 20 and 21, TOInit 0x8AD69E and 0x8AD6AC), and nothing reads system_script.bin
after it is loaded. battle_script.bin is kind 18 or 4 entry 22.

The script reads the user's install at run time and writes only the picture `render` is asked for,
which holds geometry and names, no game art.

Usage:
    python -I menuscript.py list [--file menu|system|battle] [--tree jppc|uspc] [--grep TEXT]
    python -I menuscript.py survey [--exe FFX.exe]
    python -I menuscript.py scenes [--file F] [NAME]
    python -I menuscript.py screens
    python -I menuscript.py render NAME [NAME ...] -o OUT.svg|OUT.png [--file F] [--frame N]
                                   [--offset X,Y] [--scale S]
    NAME is a value name, a scene name, a glob such as 'rect_c*', or @SCREEN from `screens`.
"""

import argparse
import collections
import fnmatch
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vbf import VbfArchive  # noqa: E402

GAME_DIR = r"G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster"
DEFAULT_VBF = os.path.join(GAME_DIR, "data", "FFX_Data.vbf")
DEFAULT_EXE = os.path.join(GAME_DIR, "FFX.exe")

FILES = {
    "system": "system_script.bin",
    "menu": "menu_script.bin",
    "battle": "battle_script.bin",
}
# The slot each file is registered in by TOInit (FFX_MenuScript_SetSlot 0xA7C7F0).
SLOT = {"system": 0, "menu": 1, "battle": 2}

HEADER_SIZE = 0xD0
NAME_RE = re.compile(r"^[A-Za-z0-9_]+$")

# byte_C89C40, the length of each op in u16 words. Index 11 and up are 0 in the binary and never
# appear in a file.
OP_WORDS = [2, 2, 2, 2, 10, 10, 12, 12, 2, 2, 16]
OP_NAMES = {
    0: "end", 1: "blank", 2: "wait_track0", 3: "hide", 4: "rect", 5: "line", 6: "shape",
    7: "digits", 8: "loop", 9: "loop_start", 10: "op10",
}
KEYFRAME_OPS = (4, 5, 6, 7, 10)
# The s16 fields after x and y that FFX_MenuScript_DrawKeyframe 0xA7BBC0 interpolates per op.
OP_FIELDS = {
    4: (16, 18),
    5: (16, 18),
    6: (16, 18, 20),
    7: (16, 18, 20),
    10: (16, 18, 20, 22, 24, 28, 30),
}
F_LOOP_FOREVER = 0x0800
F_EASE = 0x7000
F_RELATIVE = 0x8000

# Which names each menu loader reads from slot 1 (menu_script.bin), read off the call sites of the
# value getters in FFX.exe. Key: (loader, what it is, [names]). The names are the loaders' own
# string arguments, in call order.
SCREENS = {
    "common": ("sub_8AE6F0, from TOInit, FFX_Map_ReturnToTitle and the international swap",
               "the shared block: nameplates, command plate, help and place windows",
               ["col_mssgwind0", "col_mssgwind1", "dum73", "dum74", "dum75", "col_bsoplt0", "col_bsoplt1",
                "col_bitemplt0", "col_bitemplt1", "col_babplt0", "col_babplt1", "col_lmtplt0", "col_lmtplt1",
                "col_menubg", "col_menubg0", "col_menubg1", "rect_nameplt", "num_namepltspc", "col_nameplt0",
                "col_nameplt1", "pos_namestr", "pos_hptex", "pos_hpstr", "pos_mptex", "pos_mpstr",
                "pos_slvshape", "pos_spstr", "rect_complt", "col_complt0", "col_complt1", "num_compltspc",
                "pos_comstr", "rect_helpsubplt", "col_helpsubplt0", "col_helpsubplt1", "rect_helpplt",
                "col_helpplt0", "col_helpplt1", "pos_helpstr", "rect_placesubplt", "col_placesubplt0",
                "col_placesubplt1", "rect_placeplt", "col_placeplt0", "col_placeplt1", "pos_placeshape",
                "pos_placestr", "col_isnmplt", "col_ibnmplt0", "col_ibnmplt1", "col_inmline", "col_inmplt0",
                "col_inmplt1", "c_cnameplt0", "c_cnameplt1", "pos_cname", "rect_cscroll", "col_5nameplt0",
                "col_5nameplt1", "rect_snoplt", "col_snoplt0", "col_snoplt1", "pos_snoplt"]),
    "status": ("FFX_Menu_StatusLoadLayout 0x8D4920", "module 6, the Status screen",
               ["col_menubg0", "col_menubg1", "rect_3nameplt", "pos_6nameplt", "rect_wpnbplt", "rect_wpnlplt",
                "rect_wpnrplt", "rect_wpnbline", "pos_wpnlplt", "rect_lg", "pos_lg", "col_lmtg0", "col_lmtg1",
                "col_wpnbplt", "col_wpnlplt0", "col_wpnlplt1", "col_wpnrplt0", "col_wpnrplt1", "col_lmtgf0",
                "pos_lpmplt", "pos_rpmplt", "pos_pmshape", "num_pmplt", "rect_lgplt", "col_lgplt0",
                "col_lgplt1", "rect_3lgplt", "col_3lgplt0", "col_3lgplt1", "rect_ltplt", "col_ltplt0",
                "col_ltplt1", "rect_ltbplt", "col_ltbplt", "rect_stsmw", "col_mssgwind0", "col_mssgwind1",
                "pos_extex", "pos_exmssg", "num_wpnplt", "pos_typshape", "pos_ltstr", "pos_lgshape",
                "pos_3lgshape", "pos_wpnmoji", "pos_wpnicon", "pos_wpnnm", "num_staplt", "num_face1",
                "num_face2", "pos_face", "rect_smojiplt", "pos_lstaplt", "pos_rstaplt", "pos_faceb"]),
    "customize": ("FFX_Menu_CustomizeLoadLayout 0x8D6BE0", "module 9, the Customize screen",
                  ["r_kbackplt", "col_shohiplt0", "col_shohiplt1", "pos_kzonmplt", "r_kmojiplt1", "r_kmojiplt2",
                   "pos_kabplt", "pos_ksobiplt", "r_kscrol", "r_kmojiplt3", "pos_kbackplt", "pos_kitmplt",
                   "r_kmojiplt4", "pos_kmaru", "pos_klshohi", "pos_krshohi", "pos_ksobiplt2"]),
    "config": ("FFX_Menu_ConfigSelectRowTable 0x8CB350", "module 10, the Config screen",
               ["r_cnameplt", "c_cnameplt0", "c_cnameplt1", "num_cnameplt", "r_cnameplt_nonhdd",
                "num_cnameplt_nonhdd", "rect_cchoplt", "rect_cbline", "rect_cscroll", "pos_cchoplt",
                "pos_cname", "pos_cbline", "c_cbline0", "c_cbline1", "col_helpplt0", "col_helpplt1"]),
    "8C5050": ("sub_8C5050, from sub_8C33F0 and sub_8C4250", "name entry, by its kana keys (inferred)",
               ["rect_fchplt", "col_fchplt", "rect_nmfwnd", "col_nmfwnd0", "col_nmfwnd1", "rect_nmnplt",
                "col_nmnplt", "num_fchplt", "num_name1", "num_name2", "col_nmline", "pos_nface",
                "pos_fchfont", "pos_nmline", "pos_name", "pos_kanaa", "pos_kanama", "pos_kanaga", "pos_ace",
                "rect_nmline"]),
    "8CEEF0": ("sub_8CEEF0, from sub_8CC1A0", "a goods list (shohi), not identified",
               ["rect_snoplt", "col_snoplt0", "col_snoplt1", "r_shohiplt", "col_shohiplt0", "col_shohiplt1",
                "pos_shiplt", "pos_shshape", "pos_lshohi", "pos_rshohi", "rect_shabscroll", "r_shmojiplt1",
                "r_shmojiplt2", "pos_shs", "pos_snoplt", "pos_snomoji", "pos_sno2moji", "pos_5nameplt",
                "pos_namestr", "pos_shabplt", "num_5nameplt"]),
    "8CFC40": ("sub_8CFC40", "not identified",
               ["r_somojiplt", "pos_sobiplt", "r_somojiplt2", "pos_soabplt", "r_soscrol"]),
    "8D0CD0": ("sub_8D0CD0", "not identified",
               ["pos_4nameplt", "rect_2lgplt", "rect_lwplt", "col_lwplt0", "col_lwplt1", "pos_2llplt",
                "pos_2lrplt", "rect_lscroll", "rect_lmojiplt"]),
    "8D9190": ("sub_8D9190, from sub_8C64F0, the +0x08 slot of descriptor 0xC5A320", "a page with gil and total (gokei) plates, not identified",
               ["rect_wshplt", "rect_wshscrol", "rect_wgokei", "rect_wgokeiw", "r_gomojiplt2", "r_wmojiplt1",
                "r_wmojiplt2", "pos_wshnmplt", "pos_wshnm", "pos_wsobipplt", "pos_wabplt", "pos_wshabplt",
                "pos_wgokeiw", "pos_wsobiplt", "p_gomojiplt2", "pos_wgilshape1", "pos_wgilshape2", "pos_wgil1",
                "pos_wgil2", "col_wgokeiw", "col_wgilshape1", "col_wgilshape2", "col_wshplt1", "col_wshplt2",
                "col_mssgwind0", "col_mssgwind1"]),
    "8DB080": ("sub_8DB080, from sub_8C64F0, the +0x08 slot of descriptor 0xC5A320", "a page with gil, price and total plates, not identified",
               ["r_ashchoplt", "r_ashmojiplt1", "r_ashnoplt", "r_2shohiplt", "r_gomojiplt", "rect_ashscrol",
                "rect_aprplt", "rect_gokei", "rect_gokeiw", "p_ashgil", "p_ashnoplt", "p_gomojiplt",
                "pos_aprline", "pos_aprice", "pos_ashmaru", "pos_ashno", "pos_cgilshape1", "pos_cgilshape2",
                "pos_cgil1", "pos_cgil2", "pos_gokeiw", "pos_2shohiplt", "pos_2shiplt", "pos_2shshape",
                "pos_2lshohi", "pos_2rshohi", "c_ashnoplt0", "c_ashnoplt1", "col_aprplt0", "col_aprplt1",
                "col_2shohiplt0", "col_2shohiplt1", "col_gokeiw", "col_cgilshape1", "col_cgilshape2",
                "pos_itmlplt", "pos_itmrplt", "rect_iscroll", "col_mssgwind0", "col_mssgwind1", "pos_gilstr",
                "pos_gilshape"]),
    "8E2630": ("sub_8E2630", "an inactive row overlay (noactive)",
               ["rect_noactive", "pos_noactive", "pos_nmblkline", "rect_nmblkline", "col_noactive0",
                "col_noactive1"]),
}
# Every scene of slot 1 that code starts by name, and who starts it.
STARTED_SCENES = {
    "scene0": "sub_8CF830, module 4 (Equip) descriptor 0xC5A744 +0x0C",
    "scene11": "sub_8CF830, module 4 (Equip) descriptor 0xC5A744 +0x0C",
    "scene20": "sub_8CF830, module 4 (Equip) descriptor 0xC5A744 +0x0C",
    "scene2": "sub_8B3680, the save and load screen",
    "scene3": "sub_8B5000, the save and load screen (stopped by FFX_SaveLoadMenu_StepModule)",
    "scene33": "sub_8B5000, the save and load screen",
    "scene34": "sub_8B5000, the save and load screen",
    "scene30": "sub_8E2B60, the world map marker, offset to (marker x / 2, marker y / 2 - 16)",
    "scene7": "sub_8E2F80, a window, offset to the window position",
}


class ParseError(Exception):
    pass


def name_hash(name):
    h = 0
    for c in name.encode("ascii"):
        h ^= c
    return h & 0xF


def s32(v):
    return v - 0x100000000 if v & 0x80000000 else v


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Value(object):
    __slots__ = ("index", "offset", "bucket", "name", "values")

    def __init__(self, index, offset, bucket, name, values):
        self.index = index
        self.offset = offset
        self.bucket = bucket
        self.name = name
        self.values = values

    @property
    def kind(self):
        n = len(self.values)
        return {1: "word", 2: "point", 4: "rect"}.get(n, "x%d" % n)

    def show(self):
        v = self.values
        if len(v) == 1:
            w = v[0]
            if self.name.startswith(("col_", "c_")):
                return "rgba(%d,%d,%d,%d)" % (w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, w >> 24)
            if self.name.startswith(("num_", "n_")):
                return "%d" % s16(w)
            return "0x%08X" % w
        return " ".join("%d" % s16(x) for x in v)


class Scene(object):
    __slots__ = ("offset", "bucket", "name", "tracks_offset", "x", "y", "tracks", "groups")

    def __init__(self, offset, bucket, name, tracks_offset, x, y):
        self.offset = offset
        self.bucket = bucket
        self.name = name
        self.tracks_offset = tracks_offset
        self.x = x
        self.y = y
        self.tracks = []
        self.groups = []


class Script(object):
    """One file, parsed strictly. Every byte is accounted for or ParseError is raised."""

    def __init__(self, data, label):
        self.data = data
        self.label = label
        self.cover = bytearray(len(data))
        self.values = []
        self.by_name = {}
        self.scenes = []
        self.scene_by_name = {}
        self.scene_by_offset = {}
        self.sequences = []
        self.seq_offsets = []
        self.groups = {}
        self.ops = collections.Counter()
        self.parse()

    # -- helpers --------------------------------------------------------------------------------

    def fail(self, msg):
        raise ParseError("%s: %s" % (self.label, msg))

    def u32(self, o):
        if o < 0 or o + 4 > len(self.data):
            self.fail("u32 read at 0x%X is outside the file" % o)
        return struct.unpack_from("<I", self.data, o)[0]

    def u16(self, o):
        if o < 0 or o + 2 > len(self.data):
            self.fail("u16 read at 0x%X is outside the file" % o)
        return struct.unpack_from("<H", self.data, o)[0]

    def claim(self, o, n, what):
        if o < 0 or o + n > len(self.data):
            self.fail("%s at 0x%X+%d runs past the end" % (what, o, n))
        for i in range(o, o + n):
            if self.cover[i]:
                self.fail("%s at 0x%X overlaps another structure at 0x%X" % (what, o, i))
            self.cover[i] = 1

    def name_at(self, o, what):
        d = self.data
        if o < HEADER_SIZE or o >= len(d):
            self.fail("%s name offset 0x%X is out of range" % (what, o))
        e = d.find(b"\0", o)
        if e < 0:
            self.fail("%s name at 0x%X has no terminator" % (what, o))
        raw = d[o:e]
        try:
            s = raw.decode("ascii")
        except UnicodeDecodeError:
            self.fail("%s name at 0x%X is not ASCII" % (what, o))
        if not NAME_RE.match(s):
            self.fail("%s name at 0x%X is %r, not an identifier" % (what, o, s))
        # Names can be shared between entries, so the string bytes are claimed once.
        if not self.cover[o]:
            self.claim(o, e + 1 - o, "name %s" % s)
        return s

    # -- the parse ------------------------------------------------------------------------------

    def parse(self):
        d = self.data
        if len(d) < HEADER_SIZE:
            self.fail("shorter than the 0xD0 byte header")
        hdr = struct.unpack_from("<52I", d, 0)
        self.claim(0, HEADER_SIZE, "header")
        if hdr[33] or hdr[34] or hdr[35]:
            self.fail("header words 0x84..0x8C are not zero: %r" % (hdr[33:36],))

        # Values. FFX_MenuScript_FindValue 0xA7B7D0 walks bucket hash & 15 and steps by
        # 1 + (header >> 24) dwords, comparing the name at header & 0xFFFFFF.
        index = 0
        for b in range(16):
            o = hdr[b]
            if not o:
                continue
            n = self.u32(o)
            self.claim(o, 4, "value bucket %d count" % b)
            p = o + 4
            for i in range(n):
                w = self.u32(p)
                no, cnt = w & 0xFFFFFF, w >> 24
                if cnt == 0:
                    self.fail("value at 0x%X has no values" % p)
                self.claim(p, 4 + 4 * cnt, "value entry")
                name = self.name_at(no, "value")
                if name_hash(name) != b:
                    self.fail("value %s is in bucket %d, its hash is %d" % (name, b, name_hash(name)))
                vals = list(struct.unpack_from("<%dI" % cnt, d, p + 4))
                v = Value(index, p, b, name, vals)
                index += 1
                if name in self.by_name:
                    self.fail("value %s appears twice" % name)
                self.values.append(v)
                self.by_name[name] = v
                p += 4 + 4 * cnt

        # Sequences. The table at hdr[32] holds one u32 per sequence. Its length is not stored, it
        # ends where the first track list starts, and the sequences tile the bytes before it.
        st = hdr[32]
        scene_rows = []
        for b in range(16):
            o = hdr[16 + b]
            if not o:
                continue
            n = self.u32(o)
            self.claim(o, 4, "scene bucket %d count" % b)
            for i in range(n):
                e = o + 4 + 12 * i
                self.claim(e, 12, "scene entry")
                no, tl, x, y = struct.unpack_from("<IIhh", d, e)
                scene_rows.append((e, b, no, tl, x, y))
        if scene_rows and not st:
            self.fail("scenes but no sequence table")
        if st:
            first_tl = min(r[3] & 0x3FFFFFFF for r in scene_rows) if scene_rows else len(d)
            if first_tl <= st or (first_tl - st) % 4:
                self.fail("sequence table 0x%X does not end cleanly at the first track list 0x%X" % (st, first_tl))
            count = (first_tl - st) // 4
            self.claim(st, 4 * count, "sequence table")
            self.seq_offsets = list(struct.unpack_from("<%dI" % count, d, st))
            order = sorted(set(self.seq_offsets))
            if len(order) != count:
                self.fail("two sequence table rows share an offset")
            decoded = {}
            for k, so in enumerate(order):
                end = order[k + 1] if k + 1 < len(order) else st
                ops, p = self.decode_sequence(so)
                if p != end:
                    self.fail("sequence at 0x%X ends at 0x%X, the next starts at 0x%X" % (so, p, end))
                self.claim(so, p - so, "sequence")
                decoded[so] = ops
            self.sequences = [decoded[so] for so in self.seq_offsets]

        for e, b, no, tl, x, y in scene_rows:
            if tl & 0xC0000000:
                self.fail("scene at 0x%X has run time flags set in the file" % e)
            name = self.name_at(no, "scene")
            if name_hash(name) != b:
                self.fail("scene %s is in bucket %d, its hash is %d" % (name, b, name_hash(name)))
            sc = Scene(e, b, name, tl, x, y)
            nt = self.u32(tl)
            self.claim(tl, 4 + 20 * nt, "track list of %s" % name)
            for k in range(nt):
                t = struct.unpack_from("<IHhHHhHHH", d, tl + 4 + 20 * k)
                seq = t[0] & 0x3FFFFFFF
                if t[0] & 0xC0000000:
                    self.fail("%s track %d has run time flags set" % (name, k))
                if seq >= len(self.sequences):
                    self.fail("%s track %d plays sequence %d of %d" % (name, k, seq, len(self.sequences)))
                if t[1:] != (0, 0, 0, 0, -1, 0, 0, 0):
                    self.fail("%s track %d run time state is not the initial state: %r" % (name, k, t[1:]))
                sc.tracks.append(seq)
            if name in self.scene_by_name:
                self.fail("scene %s appears twice" % name)
            self.scenes.append(sc)
            self.scene_by_name[name] = sc
            self.scene_by_offset[e] = sc

        # Draw groups. FFX_MenuScript_DrawGroup 0xA7B220 clamps the group to 0..15 and runs every
        # scene whose entry offset is in the list.
        for g in range(16):
            o = hdr[36 + g]
            if not o:
                continue
            n = self.u32(o)
            self.claim(o, 4 + 4 * n, "draw group %d" % g)
            lst = list(struct.unpack_from("<%dI" % n, d, o + 4))
            for e in lst:
                sc = self.scene_by_offset.get(e)
                if sc is None:
                    self.fail("draw group %d lists 0x%X, which is not a scene entry" % (g, e))
                sc.groups.append(g)
            self.groups[g] = lst

        # What is left must be zero padding.
        bad = [i for i in range(len(d)) if not self.cover[i] and d[i]]
        if bad:
            self.fail("%d unaccounted non-zero bytes, first at 0x%X" % (len(bad), bad[0]))
        self.padding = sum(1 for i in range(len(d)) if not self.cover[i])
        self.largest_gap = self.largest_uncovered_run()
        # The padding is exactly alignment: up to 3 bytes to the next 4 byte boundary inside the
        # file, and the tail up to a 16 byte boundary.
        i = 0
        while i < len(d):
            if self.cover[i]:
                i += 1
                continue
            j = i
            while j < len(d) and not self.cover[j]:
                j += 1
            if j == len(d):
                if j % 16 or j - i >= 16:
                    self.fail("tail padding 0x%X..0x%X is not alignment to 16" % (i, j))
            elif j % 4 or j - i > 3:
                self.fail("zero run 0x%X..0x%X is not alignment to 4" % (i, j))
            i = j

    def largest_uncovered_run(self):
        best = run = 0
        for c in self.cover:
            run = 0 if c else run + 1
            best = max(best, run)
        return best

    def decode_sequence(self, so):
        ops = []
        p = so
        while True:
            w = self.u16(p)
            op = w & 0x7FF
            if op >= len(OP_WORDS):
                self.fail("sequence at 0x%X has op %d at 0x%X" % (so, op, p))
            n = OP_WORDS[op]
            words = struct.unpack_from("<%dH" % n, self.data, p)
            ops.append((p, op, w & 0xF800, words))
            self.ops[op] += 1
            p += 2 * n
            if op == 0:
                return ops, p
            if p >= len(self.data):
                self.fail("sequence at 0x%X runs off the end" % so)

    # -- scene playback -------------------------------------------------------------------------

    def keyframe(self, seq, pc):
        """The op at word index pc of a sequence: (op, word, raw bytes from the op start)."""
        so = self.seq_offsets[seq]
        o = so + 2 * pc
        w = self.u16(o)
        return w & 0x7FF, w, o

    def field(self, o, off):
        return struct.unpack_from("<h", self.data, o + off)[0]

    def col(self, o, off):
        return struct.unpack_from("<4B", self.data, o + off)

    def play(self, scene, frames, offset=(0, 0)):
        """Run FFX_MenuScript_RunScene 0xA7B460 for `frames` draw calls and return, per frame, the
        primitives drawn and whether the scene was still running."""
        n = len(scene.tracks)
        # (seq, running, counter, dur, pc, prevpc, sync, loop_pc, loop_count)
        st = [[scene.tracks[k], True, 0, 0, 0, 0, -1, 0, 0] for k in range(n)]
        out = []
        running = True
        for _ in range(frames):
            prims = []
            any_alive = 0
            for k in range(n):
                alive, prim = self.step_track(st, k, offset)
                any_alive |= alive
                if prim:
                    prims.append((k, prim))
            out.append((prims, running))
            if not any_alive:
                running = False
        return out

    def step_track(self, st, k, offset):
        """FFX_MenuScript_StepTrack 0xA7B510, then FFX_MenuScript_DrawTrack 0xA7B290."""
        t = st[k]
        if t[2] != 0:
            t[2] -= 1
            return 1, self.draw_track(t, offset)
        go = True
        alive = 1
        guard = 0
        while go:
            guard += 1
            if guard > 100000:
                raise ParseError("%s: sequence %d never yields" % (self.label, t[0]))
            op, w, o = self.keyframe(t[0], t[4])
            if op == 0:
                t[2] = 0
                t[3] = -1
                t[1] = False
                alive = 0
                break
            if op == 1:
                t[5] = t[4]
                t[4] += OP_WORDS[1]
                continue
            if op == 2:
                t[2] = 0
                t[3] = -1
                if t[6] < 0:
                    # The search compares against track 0's sequence on every pass, the register
                    # is loaded once before the loop (0xA7B5B7). So only track 0 can be waited on.
                    target = self.u16(o + 2)
                    target = target - 0x10000 if target & 0x8000 else target
                    if n_tracks(st) > 0 and target == st[0][0]:
                        t[6] = 0
                if t[6] >= 0 and st[t[6]][1]:
                    break
                t[6] = -1
                t[3] = 0
                t[4] += OP_WORDS[2]
                continue
            if op == 3:
                v = self.u16(o + 2)
                t[2] = v
                t[3] = -1
                if v != 0:
                    go = False
                    t[2] = v - 1
                t[5] = t[4]
                t[4] += OP_WORDS[3]
                continue
            if op in KEYFRAME_OPS:
                d = self.u16(o + 2)
                t[5] = t[4]
                t[4] += OP_WORDS[op]
                t[2] = d
                t[3] = d
                if d == 0:
                    continue
                t[2] = d - 1
                break
            if op == 8:
                if w & F_LOOP_FOREVER:
                    c = 1
                elif t[8] > 0:
                    c = t[8] - 1
                else:
                    c = self.u16(o + 2)
                t[8] = c
                if c > 0:
                    t[4] = t[7]
                else:
                    t[4] += OP_WORDS[8]
                    t[8] = 0
                continue
            if op == 9:
                t[4] += OP_WORDS[9]
                t[7] = t[4]
                continue
        return alive, self.draw_track(t, offset)

    def draw_track(self, t, offset):
        cur_op, cur_w, cur_o = self.keyframe(t[0], t[5])
        nxt_op, nxt_w, nxt_o = self.keyframe(t[0], t[4])
        counter, dur = t[2], t[3]
        if dur < 0:
            return self.primitive(cur_w, cur_o, cur_o, 0.0, offset)
        if counter == 0:
            return self.primitive(nxt_w, nxt_o, nxt_o, 0.0, offset)
        if counter == dur:
            return self.primitive(cur_w, cur_o, cur_o, 0.0, offset)
        tt = ease(cur_w, counter, dur)
        if (cur_w ^ nxt_w) & 0x7FF == 0:
            return self.primitive(nxt_w, cur_o, nxt_o, tt, offset)
        if tt >= 0.5:
            return self.primitive(cur_w, cur_o, cur_o, 0.0, offset)
        return self.primitive(nxt_w, nxt_o, nxt_o, 0.0, offset)

    def primitive(self, w, a, b, t, offset):
        """FFX_MenuScript_DrawKeyframe 0xA7BBC0: value = (1 - t) * b + t * a, field by field."""
        op = w & 0x7FF
        if op not in KEYFRAME_OPS:
            return None

        def lerp(x, y):
            return x if x == y else int((1.0 - t) * y + x * t)

        def f(off):
            return lerp(self.field(a, off), self.field(b, off))

        x, y = f(12), f(14)
        rel = bool(w & F_RELATIVE)
        if rel:
            x += offset[0]
            y += offset[1]
        rest = dict((off, f(off)) for off in OP_FIELDS[op])
        c0 = tuple(lerp(p, q) for p, q in zip(self.col(a, 4), self.col(b, 4)))
        c1 = tuple(lerp(p, q) for p, q in zip(self.col(a, 8), self.col(b, 8)))
        if op == 5 and rel:
            rest[16] += offset[0]
            rest[18] += offset[1]
        return {"op": op, "x": x, "y": y, "f": rest, "c0": c0, "c1": c1}


def n_tracks(st):
    return len(st)


def ease(w, counter, dur):
    """FFX_MenuScript_Ease 0xA7C6F0 on the current keyframe's flags."""
    if dur == 0:
        return 1.0
    v = float(counter) / float(dur)
    e = w & F_EASE
    if e == 0x2000:
        return 1.0 - (1.0 - v) * (1.0 - v)
    if e == 0x3000:
        return v * v
    if e == 0x4000:
        a = min(max((1.0 - v) * math.pi, 0.0), math.pi)
        return (math.cos(a) + 1.0) * 0.5
    return v


# ---------------------------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------------------------

def load(ar, which, tree):
    path = "ffx_ps2/ffx/master/%s/menu/%s" % (tree, FILES[which])
    data = ar.read(path)
    return Script(data, "%s/%s" % (tree, FILES[which]))


# ---------------------------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------------------------

def cmd_list(a, ar):
    s = load(ar, a.file, a.tree)
    print("# %s, %d values, %d scenes. index, entry offset (the game's handle), bucket, kind, name, values"
          % (s.label, len(s.values), len(s.scenes)))
    for v in s.values:
        if a.grep and a.grep not in v.name:
            continue
        print("%4d  0x%04X  b%-2d  %-5s  %-24s %s" % (v.index, v.offset, v.bucket, v.kind, v.name, v.show()))
    if not a.values_only:
        for i, sc in enumerate(s.scenes):
            if a.grep and a.grep not in sc.name:
                continue
            print("%4d  0x%04X  b%-2d  scene  %-24s %d tracks, draw group %s"
                  % (i, sc.offset, sc.bucket, sc.name, len(sc.tracks), ",".join(map(str, sc.groups)) or "none"))
    return 0


def exe_has(exe, name):
    """True when FFX.exe holds the name as a whole C string: NUL terminated, and starting after a
    NUL or on a 4 byte boundary (string literals are 4 byte aligned in .rdata)."""
    pat = name.encode("ascii") + b"\0"
    i = exe.find(pat)
    while i >= 0:
        if i % 4 == 0 or exe[i - 1] == 0:
            return True
        i = exe.find(pat, i + 1)
    return False


def cmd_survey(a, ar):
    failed = 0
    exe = None
    if a.exe and os.path.exists(a.exe):
        with open(a.exe, "rb") as f:
            exe = f.read()
    parsed = {}
    for tree in ("jppc", "uspc"):
        for which in ("system", "menu", "battle"):
            try:
                s = load(ar, which, tree)
            except (ParseError, KeyError, IOError) as e:
                print("FAIL %s" % e)
                failed += 1
                continue
            parsed[(tree, which)] = s
            kinds = collections.Counter(len(v.values) for v in s.values)
            ntracks = sum(len(sc.tracks) for sc in s.scenes)
            drawn = sum(1 for sc in s.scenes if sc.groups)
            twice = sum(1 for sc in s.scenes if len(sc.groups) > 1)
            print("ok   %-26s %6d bytes  %3d values (%s)  %2d scenes (%d in a draw group, %d in two)  "
                  "%3d tracks  %3d sequences  padding %d (largest run %d)"
                  % (s.label, len(s.data), len(s.values),
                     ", ".join("%d x%d" % (kinds[k], k) for k in sorted(kinds)),
                     len(s.scenes), drawn, twice, ntracks, len(s.sequences), s.padding, s.largest_gap))
            print("     ops %s" % ", ".join("%s %d" % (OP_NAMES[k], s.ops[k]) for k in sorted(s.ops)))
            # Rect and point values are stored as u32 and read back through a _WORD, so a negative
            # one is a sign extended s32. Anything else would lose bits.
            wide = [v.name for v in s.values if len(v.values) > 1 and
                    any(not (-0x8000 <= s32(x) <= 0xFFFF) for x in v.values)]
            neg = sum(1 for v in s.values if len(v.values) > 1 and any(s32(x) < 0 for x in v.values))
            print("     %d point or rect values hold a negative number, %d do not fit 16 bits%s"
                  % (neg, len(wide), (": " + ", ".join(wide[:8])) if wide else ""))
            if which != "battle":
                off = [v.name for v in s.values if len(v.values) == 4 and
                       (s16(v.values[0]) + s16(v.values[2]) > 512 or s16(v.values[1]) + s16(v.values[3]) > 416)]
                print("     %d rects reach past the 512x416 frame%s" % (len(off), (": " + ", ".join(off[:8])) if off else ""))
            if exe is not None and tree == "jppc":
                refs = sum(1 for v in s.values if exe_has(exe, v.name))
                srefs = sum(1 for sc in s.scenes if exe_has(exe, sc.name))
                print("     names FFX.exe holds as a string: %d of %d values, %d of %d scenes"
                      % (refs, len(s.values), srefs, len(s.scenes)))
    for which in ("system", "menu", "battle"):
        j, u = parsed.get(("jppc", which)), parsed.get(("uspc", which))
        if not j or not u:
            continue
        if j.data == u.data:
            print("same %s: jppc and uspc are byte identical" % FILES[which])
            continue
        diff = [v.name for v in j.values if v.name in u.by_name and u.by_name[v.name].values != v.values]
        only = sorted(set(j.by_name) ^ set(u.by_name))
        sdiff = sum(1 for k in range(min(len(j.data), len(u.data))) if j.data[k] != u.data[k])
        print("diff %s: %d bytes differ, %d values differ (%s)%s"
              % (FILES[which], sdiff, len(diff), ", ".join(diff[:12]),
                 (", names in one tree only: %s" % only) if only else ""))
    print("%d of 6 files parsed to the last byte" % (len(parsed)))
    return 1 if failed else 0


def cmd_scenes(a, ar):
    s = load(ar, a.file, a.tree)
    for sc in s.scenes:
        if a.name and sc.name != a.name:
            continue
        started = STARTED_SCENES.get(sc.name, "") if a.file == "menu" else ""
        print("scene %-14s entry 0x%04X  tracks 0x%04X  offset (%d, %d)  draw group %s  %s"
              % (sc.name, sc.offset, sc.tracks_offset, sc.x, sc.y,
                 ",".join(map(str, sc.groups)) or "none", ("started by " + started) if started else ""))
        if not a.name:
            continue
        for k, seq in enumerate(sc.tracks):
            print("  track %d  sequence %d at 0x%04X" % (k, seq, s.seq_offsets[seq]))
            for p, op, flags, words in s.sequences[seq]:
                desc = OP_NAMES[op]
                extra = ""
                if op in KEYFRAME_OPS:
                    raw = s.data[p:p + 2 * OP_WORDS[op]]
                    x, y = struct.unpack_from("<hh", raw, 12)
                    rest = " ".join("%d" % struct.unpack_from("<h", raw, o)[0] for o in OP_FIELDS[op])
                    extra = "frames %d  xy %d,%d  %s" % (words[1], x, y, rest)
                elif op in (2, 3, 8):
                    extra = "arg %d" % words[1]
                print("    0x%04X  %-11s flags 0x%04X  %s" % (p, desc, flags, extra))
        frames = s.play(sc, 1200)
        done = next((i for i, (_, r) in enumerate(frames) if not r), None)
        print("  finishes after %s draw calls" % (done if done is not None else "more than 1200 (it loops)"))
    return 0


def cmd_screens(a, ar):
    for key, (loader, what, names) in SCREENS.items():
        print("@%-10s %3d names  %s. %s" % (key, len(names), loader, what))
    print("scenes of menu_script.bin that code starts:")
    for k, v in STARTED_SCENES.items():
        print("  %-8s %s" % (k, v))
    return 0


PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf", "#8c564b", "#e377c2",
           "#7f7f7f", "#bcbd22"]


def select(s, patterns):
    vals, scenes, missing = [], [], []
    for p in patterns:
        if p.startswith("@"):
            key = p[1:]
            if key not in SCREENS:
                raise SystemExit("no screen %s, see `screens`" % key)
            for n in SCREENS[key][2]:
                if n in s.by_name:
                    vals.append(s.by_name[n])
                elif n in s.scene_by_name:
                    scenes.append(s.scene_by_name[n])
                else:
                    missing.append(n)
            continue
        hit = False
        for v in s.values:
            if fnmatch.fnmatchcase(v.name, p):
                vals.append(v)
                hit = True
        for sc in s.scenes:
            if fnmatch.fnmatchcase(sc.name, p):
                scenes.append(sc)
                hit = True
        if not hit:
            missing.append(p)
    seen = set()
    vals = [v for v in vals if not (v.name in seen or seen.add(v.name))]
    return vals, scenes, missing


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def cmd_render(a, ar):
    s = load(ar, a.file, a.tree)
    vals, scenes, missing = select(s, a.names)
    for m in missing:
        print("not in %s: %s" % (s.label, m))
    sc_ = a.scale
    W, H = 512, 416
    items = []      # (kind, label, geometry)
    for v in vals:
        if len(v.values) == 4:
            x, y, w, h = (s16(q) for q in v.values)
            items.append(("rect", v.name, (x, y, w, h)))
        elif len(v.values) == 2:
            x, y = (s16(q) for q in v.values)
            items.append(("point", v.name, (x, y)))
    offset = (0, 0)
    if a.offset:
        offset = tuple(int(q) for q in a.offset.split(","))
    for scn in scenes:
        frames = s.play(scn, max(a.frame + 1, 1) if a.frame >= 0 else 1200, offset)
        if a.frame >= 0:
            idx = a.frame
        else:
            done = next((i for i, (_, r) in enumerate(frames) if not r), None)
            idx = done if done is not None else len(frames) - 1
        prims, _ = frames[idx]
        print("%s at draw call %d: %d primitives" % (scn.name, idx, len(prims)))
        for k, p in prims:
            lab = "%s.t%d" % (scn.name, k)
            if p["op"] == 4:
                items.append(("rect", lab, (p["x"], p["y"], p["f"][16], p["f"][18])))
            elif p["op"] == 5:
                items.append(("line", lab + " (line, no PC draw)", (p["x"], p["y"], p["f"][16], p["f"][18])))
            elif p["op"] == 6:
                items.append(("point", "%s shape %d %d%%x%d%%" % (lab, p["f"][20], p["f"][16], p["f"][18]),
                              (p["x"], p["y"])))
            else:
                items.append(("point", "%s %s" % (lab, OP_NAMES[p["op"]]), (p["x"], p["y"])))
    if not items:
        print("nothing with geometry to draw")
        return 1
    out = a.out
    if out.lower().endswith(".png"):
        write_png(out, items, W, H, sc_)
    else:
        write_svg(out, items, W, H, sc_, " ".join(a.names), s.label)
    print("wrote %d items to %s" % (len(items), out))
    for kind, lab, g in items:
        print("  %-5s %-28s %s" % (kind, lab, " ".join("%d" % q for q in g)))
    return 0


def write_svg(path, items, W, H, sc, title, label):
    pad = 8
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="%d %d %d %d">'
             % ((W + 2 * pad) * sc, (H + 2 * pad + 14) * sc, -pad, -pad - 14, W + 2 * pad, H + 2 * pad + 14),
             '<rect x="%d" y="%d" width="%d" height="%d" fill="#ffffff"/>' % (-pad, -pad - 14, W + 2 * pad, H + 2 * pad + 14),
             '<text x="0" y="-5" font-family="monospace" font-size="8" fill="#000">%s  %s  (PS2 units, 512x416)</text>'
             % (esc(label), esc(title)),
             '<rect x="0" y="0" width="%d" height="%d" fill="#f4f4f4" stroke="#000" stroke-width="0.5"/>' % (W, H)]
    for i, (kind, lab, g) in enumerate(items):
        c = PALETTE[i % len(PALETTE)]
        if kind == "rect":
            x, y, w, h = g
            lines.append('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="%s" stroke-width="0.6"><title>%s %d,%d %dx%d</title></rect>'
                         % (x, y, max(w, 0), max(h, 0), c, esc(lab), x, y, w, h))
            lines.append('<text x="%.1f" y="%.1f" font-family="monospace" font-size="5" fill="%s">%s</text>'
                         % (x + 1, y + 5, c, esc(lab)))
        elif kind == "line":
            x0, y0, x1, y1 = g
            lines.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="0.6" stroke-dasharray="2,1"/>'
                         % (x0, y0, x1, y1, c))
            lines.append('<text x="%d" y="%d" font-family="monospace" font-size="5" fill="%s">%s</text>' % (x0, y0 - 1, c, esc(lab)))
        else:
            x, y = g
            lines.append('<path d="M%d %dh6M%d %dv6" stroke="%s" stroke-width="0.6"/>' % (x - 3, y, x, y - 3, c))
            lines.append('<text x="%d" y="%d" font-family="monospace" font-size="5" fill="%s">%s</text>' % (x + 3, y - 2, c, esc(lab)))
    lines.append("</svg>")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def write_png(path, items, W, H, sc):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        raise SystemExit("PNG output needs Pillow, write an .svg instead")
    pad = 8
    img = Image.new("RGB", ((W + 2 * pad) * sc, (H + 2 * pad) * sc), "white")
    dr = ImageDraw.Draw(img)

    def P(x, y):
        return ((x + pad) * sc, (y + pad) * sc)

    dr.rectangle([P(0, 0), P(W, H)], outline="black", fill="#f4f4f4")
    for i, (kind, lab, g) in enumerate(items):
        c = PALETTE[i % len(PALETTE)]
        if kind == "rect":
            x, y, w, h = g
            dr.rectangle([P(x, y), P(x + max(w, 0), y + max(h, 0))], outline=c)
            dr.text(P(x + 1, y + 1), lab, fill=c)
        elif kind == "line":
            x0, y0, x1, y1 = g
            dr.line([P(x0, y0), P(x1, y1)], fill=c)
            dr.text(P(x0, y0 - 6), lab, fill=c)
        else:
            x, y = g
            dr.line([P(x - 3, y), P(x + 3, y)], fill=c)
            dr.line([P(x, y - 3), P(x, y + 3)], fill=c)
            dr.text(P(x + 3, y - 6), lab, fill=c)
    img.save(path)


def main(argv=None):
    p = argparse.ArgumentParser(description="Read the FFX menu layout scripts.")
    p.add_argument("--vbf", default=DEFAULT_VBF)
    p.add_argument("--tree", default="jppc", choices=["jppc", "uspc"],
                   help="jppc is the copy the game loads for every language")
    sub = p.add_subparsers(dest="cmd")

    sp = sub.add_parser("list")
    sp.add_argument("--file", default="menu", choices=sorted(FILES))
    sp.add_argument("--grep")
    sp.add_argument("--values-only", action="store_true")
    sp.set_defaults(fn=cmd_list)

    sp = sub.add_parser("survey")
    sp.add_argument("--exe", default=DEFAULT_EXE, help="FFX.exe, to count which names it can ask for")
    sp.set_defaults(fn=cmd_survey)

    sp = sub.add_parser("scenes")
    sp.add_argument("--file", default="menu", choices=sorted(FILES))
    sp.add_argument("name", nargs="?")
    sp.set_defaults(fn=cmd_scenes)

    sp = sub.add_parser("screens")
    sp.set_defaults(fn=cmd_screens)

    sp = sub.add_parser("render")
    sp.add_argument("names", nargs="+")
    sp.add_argument("-o", "--out", required=True)
    sp.add_argument("--file", default="menu", choices=sorted(FILES))
    sp.add_argument("--frame", type=int, default=-1, help="scene draw call to show, default the settled one")
    sp.add_argument("--offset", help="scene offset X,Y for keyframes with the relative flag")
    sp.add_argument("--scale", type=int, default=2)
    sp.set_defaults(fn=cmd_render)

    a = p.parse_args(argv)
    if not getattr(a, "fn", None):
        p.print_help()
        return 2
    with VbfArchive(a.vbf) as ar:
        return a.fn(a, ar)


if __name__ == "__main__":
    sys.exit(main())
