#!/usr/bin/env python
"""
btlmap.py - reader for the FINAL FANTASY X HD Remaster battle map containers.

What this decodes
-----------------
Every battle background ships one MAP1 container at

    ffx_ps2/ffx/master/jppc/btlmap/<group>/<name>/bin/mapout.vpa

inside FFX_Data.vbf, the same MAP1 format as a field map (tools/mapfile.py, reversing/MAP_FORMAT.md
and MAP_SECTIONS.md), plus a Phyre scene beside it at

    ffx_data/gamedata/ps3data/btlmap/<group>/<name>/<name>.ahwin32 and mdl/d3d11/<name>.dae.phyre

The btlmap directory holds 59 files: 58 MAP1 containers and one zero length build leftover,
kino/kino01_a/bin/mapout.$$$. The write up is reversing/BTLMAP_FORMAT.md.

A battle loads a battle map through FFX_Btl_StepSubPhase 0x783020 case 8 ->
FFX_Map_LoadMapAsync 0x9083A0 -> FFX_Map_LoadMapOutAsync 0x909A20(26, row, zone 0). The row is the
low ten bits of the map id in the btl.bin zone record when bit 0x400 is set, and asset kind 26 of
the cd path table holds two entries per row, 2*row (mapout.txa, absent on PC) and 2*row+1 (the
mapout.vpa). FFX_Map_OnMapOutLoaded 0x9097C0 then reads sections 0, 1, zone+2, 10, 12 and 11.

Every byte of every container is accounted for by the survey command. A region is one of

    sized    its extent comes from a count or size field the exe reads (headers, tables, records,
             vertex and triangle arrays, YN chunk bodies)
    bounded  a blob whose start is a referenced offset and whose end is the next referenced offset
             (the particle programs and texture blobs of section 10, whose interiors are the ppp
             particle VM and not decoded here)
    pad      zero bytes between regions
    stale    under 16 bytes before a 16 byte aligned start that are not zero. Only section 10 has
             them, after its tables, and nothing counts or points into them
    opaque   a stream the exe refuses to read (mihn05_a section 1, version 0x100)

Usage
-----
    python -I btlmap.py list                 # every file under btlmap/, with its kind 26 row
    python -I btlmap.py survey               # parse every container to the last byte, exit 1 on failure
    python -I btlmap.py show bika00_a        # the region map of one container
    python -I btlmap.py rows                 # kind 26 rows, and which btl.bin scenes use each
    python -I btlmap.py assets               # the ps3data/btlmap files, counts by class (needs phyre.py)

Nothing is written anywhere. The archive is opened read only and parsed in memory.
Addresses in the comments are VAs at the preferred base 0x400000, the same as IDA shows.
"""

import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vbf
import mapfile


PS2_DIR = "ffx_ps2/ffx/master/jppc/btlmap/"
PS3_DIR = "ffx_data/gamedata/ps3data/btlmap/"
PATH_TABLE = "ffx_ps2/ffx/proj/battle/jp/cddata/cdrom.fnd"      # FFX_Asset_LoadIndexTables 0x642FC0, slot 12
KIND_TABLE = "ffx_ps2/ffx/proj/battle/jp/cddata/cdrom.fid"      # same, slot 11, s16 first index per kind
BTL_BIN = "ffx_ps2/ffx/master/jppc/battle/kernel/btl.bin"       # FFX_Btl_InitEncounterTablePtrs 0x79D250
KIND_BATTLE_MAP = 26          # FFX_Map_LoadMapAsync 0x9083A0 passes 26 to FFX_Map_LoadMapOutAsync
KIND_FIELD_MAP = 19           # FFX_Map_LoadFieldMapOutAsyncFromBattle 0x908600 passes 19
BATTLE_MAP_BIT = 0x400        # FFX_Btl_StepSubPhase case 8: (g_ffxBattleSceneAndMap & 0x400) picks group 26
DEFAULT_PZ_TOOLS = r"E:\Git\projectzanarkand\tools"            # where phyre.py lives, override with PZ_TOOLS

# Section 1 (3D map stream) record handling in FFX_Map_ParseMapStream 0x90F900. A type above 8 hits
# the default case, which advances by the 64 byte record header only.
S1_KNOWN_TYPES = range(0, 9)


class Regions(object):
    """Byte regions of one container, checked to tile it exactly."""

    def __init__(self, data):
        self.data = data
        self.rows = []
        self.problems = []

    def add(self, start, end, kind, label):
        if not 0 <= start <= end <= len(self.data):
            self.problems.append("region %s 0x%X..0x%X outside the file" % (label, start, end))
            return
        if end > start:
            self.rows.append((start, end, kind, label))

    def problem(self, text):
        self.problems.append(text)

    def close(self):
        """Fill zero gaps with pad, report non zero gaps and overlaps. Returns the sorted rows."""
        rows = sorted(self.rows)
        out = []
        pos = 0
        for start, end, kind, label in rows:
            if start < pos:
                self.problems.append("%s at 0x%X overlaps the region before it (ends 0x%X)" % (label, start, pos))
            elif start > pos:
                gap = self.data[pos:start]
                if gap.count(0) == len(gap):
                    out.append((pos, start, "pad", "zero padding"))
                elif start - pos < 16 and start % 16 == 0:
                    # The exporter pads a table to 16 bytes and does not clear the padding. Only
                    # the section 10 tables do it. Nothing counts or points into these bytes.
                    out.append((pos, start, "stale", "16 byte alignment padding, stale bytes"))
                else:
                    self.problems.append("%d unclaimed non zero bytes at 0x%X..0x%X" % (start - pos, pos, start))
                    out.append((pos, start, "UNCLAIMED", "non zero bytes nothing references"))
            out.append((start, end, kind, label))
            pos = max(pos, end)
        if pos < len(self.data):
            gap = self.data[pos:]
            if gap.count(0) == len(gap):
                out.append((pos, len(self.data), "pad", "zero padding"))
            else:
                self.problems.append("%d unclaimed non zero bytes at the end, 0x%X" % (len(gap), pos))
                out.append((pos, len(self.data), "UNCLAIMED", "non zero bytes nothing references"))
        self.rows = out
        return out

    def totals(self):
        t = collections.Counter()
        for start, end, kind, _ in self.rows:
            t[kind] += end - start
        return t


# -----------------------------------------------------------------------------
# section parsers. Each returns the exact end offset of what it parsed.

def parse_header(m, r):
    """MAP1 header, FFX_Map_GetSection 0x907F00. +4 is the map flags dword, bit 0 indoor
    (MAP_SECTIONS.md 4), +8 and +12 are zero and unread."""
    d = m.data
    r.add(0, 4, "sized", "MAP1 magic")
    r.add(4, 16, "sized", "header dwords +4 flags, +8, +12")
    r.add(16, mapfile.MAP1_HEADER_SIZE, "sized", "28 section offsets")
    flags, h8, h12 = m.header_unknown
    if flags not in (0, 1):
        r.problem("header +4 is %d, only 0 and 1 ship" % flags)
    if h8 or h12:
        r.problem("header +8/+12 not zero")
    for i, off in enumerate(m.sections):
        if off and not mapfile.MAP1_HEADER_SIZE <= off < len(d):
            r.problem("section %d offset 0x%X outside the file" % (i, off))
    return mapfile.MAP1_HEADER_SIZE


def parse_map_stream(m, i, off, limit, r, info):
    """Section 1. Header 64 bytes (+0 magic 0x00214365, +4 version, +12 record count), then records
    of a 64 byte header (+0 type, +4 payload size) and the payload, FFX_Map_ParseMapStream 0x90F900.
    FFX_Map_MountMapStream 0x90F8E0 refuses any version but 0x110."""
    d = m.data
    magic, ver, h8, count = struct.unpack_from("<4I", d, off)
    if magic != mapfile.S1_MAGIC:
        r.problem("section 1 magic 0x%X" % magic)
        r.add(off, limit, "UNCLAIMED", "section 1 with a bad magic")
        return limit
    if ver != mapfile.S1_VERSION:
        r.add(off, off + 64, "sized", "section 1 header, version 0x%X" % ver)
        r.add(off + 64, limit, "opaque", "section 1 body, version 0x%X, refused by FFX_Map_MountMapStream" % ver)
        info["s1"] = "v0x%X refused" % ver
        return limit
    r.add(off, off + 64, "sized", "section 1 header")
    p = off + 64
    types = collections.Counter()
    for k in range(count):
        if p + 64 > limit:
            r.problem("section 1 record %d header runs past the section" % k)
            return p
        ty, size = struct.unpack_from("<2I", d, p)
        types[ty] += 1
        r.add(p, p + 64, "sized", "section 1 record header")
        if ty not in S1_KNOWN_TYPES:
            p += 64
            continue
        body = p + 64
        if body + size > limit:
            r.problem("section 1 record %d type %d payload runs past the section" % (k, ty))
            return body
        if ty == 0 and size < 64:
            r.problem("section 1 record %d type 0 payload %d, FFX_Map_ReadPartRecord reads 64" % (k, size))
        if ty == 1:
            # FFX_Map_ReadPartMeshRecord 0x90EF10 reads a 64 byte sub-header, then the parser
            # takes (size - 64) / 16 entries and stops the whole stream unless that equals +12.
            want = struct.unpack_from("<I", d, body + 12)[0]
            if size < 64 or (size - 64) % 16 or (size - 64) // 16 != want:
                r.problem("section 1 record %d type 1 entry count does not match its sub-header" % k)
        if ty == 5 and struct.unpack_from("<I", d, p + 32)[0] == 0 and size % 128:
            r.problem("section 1 record %d type 5 payload %d is not whole 128 byte keys" % (k, size))
        if ty == 8 and struct.unpack_from("<I", d, p + 32)[0] == 0 and size % 32:
            r.problem("section 1 record %d type 8 payload %d is not whole 32 byte keys" % (k, size))
        r.add(body, body + size, "sized", "section 1 type %d payload" % ty)
        p = body + size
    info["s1"] = "%d records" % count
    info["s1_types"] = types
    return p


def parse_walkmesh(m, i, off, limit, r, info):
    """Sections 2..9, the ffxmap.id walkmesh (MAP_FORMAT.md). Header 32, vertices 8 bytes each at
    +0x20, an optional 8 byte slot (the ASCII "DEWPRISM" when present), triangles 16 bytes each,
    then the 16 byte group records, which end the section."""
    d = m.data
    wm = mapfile.Walkmesh.try_parse(d, off, limit)
    if wm is None:
        r.problem("section %d does not parse as a walkmesh" % i)
        r.add(off, limit, "UNCLAIMED", "section %d" % i)
        return limit
    bad = wm.check()
    if bad:
        r.problem("section %d walkmesh: %s" % (i, "; ".join(bad[:3])))
    vend = off + 0x20 + 8 * wm.vertex_count
    r.add(off, off + 0x20, "sized", "walkmesh header")
    r.add(off + 0x20, vend, "sized", "walkmesh vertices")
    pos = vend
    for g in wm.groups:
        t0 = off + g["tri_off"]
        if t0 > pos:
            r.add(pos, t0, "sized", "walkmesh spare slot %r" % bytes(d[pos:t0]))
        r.add(t0, t0 + 16 * g["tri_count"], "sized", "walkmesh triangles")
        pos = t0 + 16 * g["tri_count"]
    gend = off + wm.group_off + 16 * wm.group_count
    r.add(off + wm.group_off, gend, "sized", "walkmesh group records")
    g0 = wm.groups[0]
    if g0["tri_count"] == 2 and wm.vertex_count == 4:
        shape = "plane2"
    elif g0["tri_count"] == 1250 and wm.vertex_count == 676:
        shape = "grid1250"
    else:
        shape = "mesh%d" % g0["tri_count"]
    b = wm.bounds()
    info.setdefault("wm", []).append((i, shape, wm.scale, b))
    return gend


def parse_particle_set(m, i, off, limit, r, info):
    """Section 10, the field particle set (.pdt), FFX_MapFx_LoadParticleSet 0x72A7F0 and
    FFX_Vfx_RelocateParticleSet 0x711FA0. Every offset is relative to the set base.

      +0x04 u16 placed effect count, 80 byte records from +0x20
      +0x06 u16 program count, u32 offsets at +0x10
      +0x08 u16 count of 32 byte records at +0x14, three offsets each at record +20/+24/+28
      +0x0A u16 count of u32 offsets at +0x18
      +0x0C u16 count of 8 byte records at +0x1C, one offset each at record +4

    A program (sub_7120F0) holds offsets relative to itself: +8 a u32 count and that many operator
    indices, +12 a u32 count and that many offsets, +16 the first node of a chain. A node is
    +0 next node, +38 s16 entry count, entries of 16 bytes from +40 with offsets at +8 and +12.
    The chain ends at the node whose +0 is zero."""
    d = m.data
    n = limit - off
    h = mapfile.s10_header(d, off)
    tables = [
        (0, 32, "particle set header"),
        (32, 80 * h["placed"], "placed effect records"),
        (h["off16"], 4 * h["programs"], "program offset table"),
        (h["off20"], 32 * h["n20"], "32 byte records (+0x14)"),
        (h["off24"], 4 * h["n24"], "offset list (+0x18)"),
        (h["off28"], 8 * h["n28"], "8 byte records (+0x1C)"),
    ]
    anchors = {}                      # rel start -> label, blobs of unknown size
    for start, size, label in tables:
        if start + size > n:
            r.problem("section 10 %s runs past the section" % label)
            return limit

    def u32(rel):
        return struct.unpack_from("<I", d, off + rel)[0]

    programs = [u32(h["off16"] + 4 * k) for k in range(h["programs"])]
    for p in programs:
        anchors.setdefault(p, "particle program")
    for k in range(h["n20"]):
        for slot in (20, 24, 28):
            v = u32(h["off20"] + 32 * k + slot)
            if v:
                anchors.setdefault(v, "blob of a +0x14 record")
    for k in range(h["n24"]):
        v = u32(h["off24"] + 4 * k)
        if v:
            anchors.setdefault(v, "blob of the +0x18 list")
    for k in range(h["n28"]):
        v = u32(h["off28"] + 8 * k + 4)
        if v:
            anchors.setdefault(v, "blob of a +0x1C record")
    for a in anchors:
        if not 0 < a < n:
            r.problem("section 10 offset 0x%X outside the set" % a)
            return limit

    # Every start, sized or not. A blob runs to the next start, a sized table must end at or
    # before it.
    starts = sorted(set([t[0] for t in tables if t[1]] + list(anchors)))
    sized = dict((t[0], (t[1], t[2])) for t in tables if t[1])
    for idx, s in enumerate(starts):
        nxt = starts[idx + 1] if idx + 1 < len(starts) else n
        if s in sized:
            size, label = sized[s]
            if s + size > nxt:
                r.problem("section 10 %s at +0x%X overlaps the next object at +0x%X" % (label, s, nxt))
            r.add(off + s, off + s + size, "sized", "section 10 " + label)
            if s in anchors and s + size < nxt:
                r.add(off + s + size, off + nxt, "bounded", "section 10 " + anchors[s])
        else:
            r.add(off + s, off + nxt, "bounded", "section 10 " + anchors[s])

    # The program interiors: every offset sub_7120F0 relocates must land inside the program.
    for p in programs:
        pend = min([s for s in starts if s > p] + [n])

        def inside(rel, what, p=p, pend=pend):
            if not p <= p + rel < pend:
                r.problem("section 10 program +0x%X: %s 0x%X outside it" % (p, what, rel))
                return False
            return True

        a8, a12 = u32(p + 8), u32(p + 12)
        if not (inside(a8, "operator list") and inside(a12, "offset list")):
            continue
        if p + a8 + 4 + 4 * u32(p + a8) > pend or p + a12 + 4 + 4 * u32(p + a12) > pend:
            r.problem("section 10 program +0x%X: a list runs past it" % p)
            continue
        for k in range(u32(p + a12)):
            inside(u32(p + a12 + 4 + 4 * k), "list offset")
        # sub_7120F0 handles the node at +16 when its +0 is not zero, then each next node,
        # and stops when the node it would move to has a zero +0. That last one is a sentinel
        # of which only the first dword is read.
        node = 16
        if u32(p + 16) == 0:
            continue
        for _ in range(10000):
            if p + node + 40 > pend:
                r.problem("section 10 program +0x%X: node +0x%X runs past it" % (p, node))
                break
            count = struct.unpack_from("<h", d, off + p + node + 38)[0]
            if p + node + 40 + 16 * max(count, 0) > pend:
                r.problem("section 10 program +0x%X: node entries run past it" % p)
                break
            for e in range(max(count, 0)):
                for slot in (8, 12):
                    inside(u32(p + node + 40 + 16 * e + slot), "entry offset")
            nxt = u32(p + node)
            if not inside(nxt, "next node") or p + nxt + 4 > pend:
                break
            if u32(p + nxt) == 0:
                break
            node = nxt
        else:
            r.problem("section 10 program +0x%X: node chain does not end" % p)
    info["s10"] = (h["placed"], h["programs"])
    return limit


def parse_yn_stream(m, i, off, limit, r, info):
    """Sections 0, 11 and 12, a YN chunk stream read by rcbgMain 0x921D60: a 16 byte YNDT header,
    then chunks of a 16 byte header (+0 tag, +4 count of 16 byte body units) and the body, up to
    YNED. YNFT (section 12) is u16, u16, 12 bytes, then two TRX images, each 32 bytes of header
    with w at +24 and h at +28 and w*h bytes (sub_92B780, rcbgLoad_ReadTrxImage 0x92B710). YNGM
    (section 11) is u32 kind, u32 vertex size, the vertex bytes, then the tail rcbgLoad__f92B2F0
    reads as 0x130 bytes of blocks."""
    d = m.data
    chunks, ended = mapfile.walk_yn(d, off, limit)
    if chunks is None or not ended:
        r.problem("section %d YN stream does not reach YNED" % i)
        r.add(off, limit, "UNCLAIMED", "section %d" % i)
        return limit
    r.add(off, off + 16, "sized", "YNDT stream header")
    end = off + 16
    tags = []
    for tag, coff, size, u10 in chunks:
        c = off + coff
        r.add(c, c + 16, "sized", "%s chunk header" % tag)
        r.add(c + 16, c + 16 + size, "sized", "%s chunk body" % tag)
        tags.append(tag)
        if tag == "YNFT":
            q = c + 32
            for _ in range(2):
                w, hh = struct.unpack_from("<2I", d, q + 24)
                q += 32 + w * hh
            if q != c + 16 + size:
                r.problem("section %d YNFT TRX images end at 0x%X, the body at 0x%X" % (i, q, c + 16 + size))
        if tag == "YNGM":
            vsize = struct.unpack_from("<I", d, c + 20)[0]
            tail = size - 8 - vsize
            info.setdefault("yngm_tail", []).append(tail)
            if tail < 0:
                r.problem("section %d YNGM vertex size %d exceeds the body" % (i, vsize))
        end = c + 16 + size
    info.setdefault("yn", {})[i] = tags
    return end


PARSERS = {0: parse_yn_stream, 1: parse_map_stream, 10: parse_particle_set,
           11: parse_yn_stream, 12: parse_yn_stream}
for _z in range(2, 10):
    PARSERS[_z] = parse_walkmesh


def parse_container(name, data):
    """Returns (status, Regions, info). status is ok, opaque, empty or FAIL."""
    r = Regions(data)
    info = {}
    if not data:
        info["empty"] = True
        return "empty", r, info
    try:
        m = mapfile.Map1(data, name)
    except ValueError as e:
        r.problem(str(e))
        return "FAIL", r, info
    parse_header(m, r)
    present = [(off, idx) for idx, off in enumerate(m.sections) if off]
    present.sort()
    info["sections"] = [idx for _, idx in present]
    for k, (off, idx) in enumerate(present):
        limit = present[k + 1][0] if k + 1 < len(present) else len(data)
        fn = PARSERS.get(idx)
        if fn is None:
            r.problem("section %d has no known reader in the exe" % idx)
            r.add(off, limit, "UNCLAIMED", "section %d" % idx)
            continue
        try:
            fn(m, idx, off, limit, r, info)
        except struct.error as e:
            r.problem("section %d: %s" % (idx, e))
    r.close()
    if r.problems:
        return "FAIL", r, info
    if r.totals()["opaque"]:
        return "opaque", r, info
    return "ok", r, info


# -----------------------------------------------------------------------------
# archive side: the entries, the path table rows and the btl.bin users

def container_key(path):
    """'ffx_ps2/.../btlmap/bika/bika00_a/bin/mapout.vpa' -> 'bika00_a'."""
    return path.split("/")[-3]


def btlmap_entries(archive):
    return sorted((e for e in archive.entries if e.name.startswith(PS2_DIR)), key=lambda e: e.name)


def host0_key(path):
    """FFX_File_OpenHost0: drop 'host0:/' and put ffx_ps2/ in front."""
    return ("ffx_ps2/" + path[7:]).lower()


def kind_rows(archive, kind):
    """Rows of one asset kind of the cd path table, two entries per row, as a list of
    (row, txa_path, vpa_path). Format: u32 count, count+1 u32 offsets, strings (CHEAT_WARP.md 1.1)."""
    fnd = archive.read(PATH_TABLE)
    fid = archive.read(KIND_TABLE)
    count = struct.unpack_from("<I", fnd, 0)[0]
    offs = struct.unpack_from("<%dI" % (count + 1), fnd, 4)
    bases = struct.unpack_from("<%dh" % (len(fid) // 2), fid, 0)
    base = bases[kind]
    later = sorted(b for b in bases if b > base)
    stop = later[0] if later else count

    def path(i):
        a, b = offs[i], offs[i + 1]
        return fnd[a:b].split(b"\0")[0].decode("latin-1") if b > a else None

    rows = []
    for k in range((stop - base) // 2):
        rows.append((k, path(base + 2 * k), path(base + 2 * k + 1)))
    return rows


def btl_bin_map_users(archive):
    """map id -> [(scene index, scene name, zone, formation count, rate)] from btl.bin
    (CHEAT_BATTLE.md 13.3). The map id is zone record +1 | +2 << 8, FFX_Btl_BeginBattle stores
    it in the low word of g_ffxBattleSceneAndMap 0x112C254."""
    d = archive.read(BTL_BIN)
    mt, zb = struct.unpack_from("<2I", d, 4)
    users = collections.defaultdict(list)
    for s in range((zb - mt) // 14):
        e = mt + 14 * s
        zoff = struct.unpack_from("<h", d, e + 2)[0]
        name = d[e + 6:e + 14].split(b"\0")[0].decode("latin-1")
        zt = zb + zoff
        p = zt + 2
        for z in range(d[zt + 1]):
            nf, lo, hi, rate = d[p], d[p + 1], d[p + 2], d[p + 3]
            users[lo | (hi << 8)].append((s, name, z, nf, rate))
            p += 2 * nf + 5
    return users


def ps3_files(archive):
    return sorted((e for e in archive.entries if e.name.startswith(PS3_DIR)), key=lambda e: e.name)


def row_of_container(rows):
    out = {}
    for k, _, vpa in rows:
        if vpa:
            out[host0_key(vpa)] = k
    return out


# -----------------------------------------------------------------------------
# commands

def describe_line(e, status, r, info, row):
    t = r.totals()
    secs = ",".join(str(s) for s in info.get("sections", []))
    wm = info.get("wm")
    wm_text = "none" if not wm else " ".join("%s@%.2f" % (w[1], w[2]) for w in wm)
    s10 = info.get("s10")
    return ("%-9s row %-3s %8d %-6s secs %-10s s1 %-14s wm %-14s s10 %-5s sized %7d bounded %6d pad %2d stale %2d opaque %7d"
            % (container_key(e.name), "-" if row is None else row, e.size, status, secs or "-",
               info.get("s1", "-"), wm_text, ("%d/%d" % s10) if s10 else "-",
               t["sized"], t["bounded"], t["pad"], t["stale"], t["opaque"]))


def cmd_list(args, archive):
    rows = row_of_container(kind_rows(archive, KIND_BATTLE_MAP))
    ents = btlmap_entries(archive)
    for e in ents:
        print("%-9s row %-3s %9d  %s" % (container_key(e.name), rows.get(e.name, "-"), e.size, e.name))
    print("%d files under %s" % (len(ents), PS2_DIR))


def cmd_survey(args, archive):
    krows = kind_rows(archive, KIND_BATTLE_MAP)
    rows = row_of_container(krows)
    users = btl_bin_map_users(archive)
    ps3 = set(e.name for e in ps3_files(archive))
    ents = btlmap_entries(archive)
    status = collections.Counter()
    totals = collections.Counter()
    shapes = collections.Counter()
    sec_count = collections.Counter()
    s1_types = collections.Counter()
    failures = []
    no_scene = []
    for e in ents:
        data = archive.read(e)
        st, r, info = parse_container(e.name, data)
        status[st] += 1
        totals.update(r.totals())
        totals["bytes"] += len(data)
        row = rows.get(e.name)
        print(describe_line(e, st, r, info, row))
        for p in r.problems[:6]:
            print("      problem: " + p)
        if st == "FAIL":
            failures.append(container_key(e.name))
        for s in info.get("sections", []):
            sec_count[s] += 1
        for w in info.get("wm", []):
            shapes[w[1]] += 1
        if st in ("ok", "opaque") and 2 not in info.get("sections", []):
            shapes["no walkmesh"] += 1
        s1_types.update(info.get("s1_types", {}))
        if st != "empty":
            g, n = e.name[len(PS2_DIR):].split("/")[:2]
            if "%s%s/%s/%s.ahwin32" % (PS3_DIR, g, n, n) not in ps3:
                no_scene.append(container_key(e.name))

    referenced = [k for k, _, vpa in krows if vpa]
    present = [k for k, _, vpa in krows if vpa and host0_key(vpa) in set(x.name for x in ents)]
    missing = ["%d %s" % (k, vpa.split("/")[-3]) for k, _, vpa in krows if vpa and k not in present]
    unref = [container_key(e.name) for e in ents if e.size and e.name not in rows]
    used = sorted(m & 0x3FF for m in users if m & BATTLE_MAP_BIT)
    unused = [k for k in present if k not in used]
    field_maps = sorted(m for m in users if not m & BATTLE_MAP_BIT)

    print()
    print("files under btlmap/      : %d (%d MAP1 containers, %d empty build leftovers)"
          % (len(ents), len(ents) - status["empty"], status["empty"]))
    print("parsed to the last byte  : %d ok, %d with an opaque refused stream, %d empty, %d FAIL"
          % (status["ok"], status["opaque"], status["empty"], status["FAIL"]))
    print("bytes                    : %d total = %d sized + %d bounded + %d zero pad + %d stale alignment"
          " + %d opaque + %d unclaimed"
          % (totals["bytes"], totals["sized"], totals["bounded"], totals["pad"], totals["stale"],
             totals["opaque"], totals["UNCLAIMED"]))
    print("sections present         : %s" % ", ".join("%d x%d" % kv for kv in sorted(sec_count.items())))
    print("section 1 record types   : %s" % ", ".join("%d x%d" % kv for kv in sorted(s1_types.items())))
    print("zone 0 walkmesh          : %s" % ", ".join("%s %d" % kv for kv in sorted(shapes.items())))
    print("kind 26 path table rows  : %d, %d name a mapout.vpa, %d of those ship, missing: %s"
          % (len(krows), len(referenced), len(present), ", ".join(missing) or "none"))
    print("containers no row names  : %s" % (", ".join(unref) or "none"))
    print("rows btl.bin battles use : %d (%s)" % (len(used), " ".join(str(u) for u in used)))
    print("shipped rows no battle uses: %s" % " ".join("%d %s" % (k, krows[k][2].split("/")[-3]) for k in unused))
    print("field maps used as a battle background: %d map ids" % len(field_maps))
    print("containers with no ps3data scene header: %s" % (", ".join(no_scene) or "none"))
    if failures:
        print("FAILED: %s" % ", ".join(failures))
        return 1
    return 0


def find_entry(archive, want):
    want = want.lower()
    hits = [e for e in btlmap_entries(archive) if container_key(e.name) == want and e.size]
    if not hits:
        hits = [e for e in btlmap_entries(archive) if want in e.name.lower()]
    if not hits:
        raise SystemExit("no btlmap file matching %r" % want)
    return hits[0]


def cmd_show(args, archive):
    e = find_entry(archive, args.map)
    data = archive.read(e)
    st, r, info = parse_container(e.name, data)
    print(e.name)
    print("status %s, %d bytes" % (st, len(data)))
    merged = []
    for start, end, kind, label in r.rows:
        if label.startswith(("section 1 record header", "section 1 type")):
            label = "section 1 records, a 64 byte header and the payload each"
        if merged and merged[-1][2] == kind and merged[-1][3] == label and merged[-1][1] == start:
            merged[-1] = (merged[-1][0], end, kind, label, merged[-1][4] + 1)
        else:
            merged.append((start, end, kind, label, 1))
    for start, end, kind, label, n in merged:
        print("  0x%08X..0x%08X %9d  %-9s %s%s" % (start, end, end - start, kind, label,
                                                  (" (%d regions)" % n) if n > 1 else ""))
    for p in r.problems:
        print("  problem: " + p)
    for w in info.get("wm", []):
        b = w[3]
        print("  walkmesh section %d: %s, scale %.3f, x %.0f..%.0f y %.1f..%.1f z %.0f..%.0f"
              % (w[0], w[1], w[2], b[0][0], b[0][1], b[1][0], b[1][1], b[2][0], b[2][1]))
    return 1 if st == "FAIL" else 0


def cmd_rows(args, archive):
    krows = kind_rows(archive, KIND_BATTLE_MAP)
    users = btl_bin_map_users(archive)
    keys = set(e.name for e in archive.entries)
    for k, txa, vpa in krows:
        name = vpa.split("/")[-3] if vpa else "(empty row)"
        ships = "ships" if vpa and host0_key(vpa) in keys else ("missing" if vpa else "")
        u = users.get(BATTLE_MAP_BIT | k, [])
        scenes = sorted(set(x[1] for x in u))
        print("row %-3d map id 0x%03X  %-10s %-8s %3d formations  %s"
              % (k, BATTLE_MAP_BIT | k, name, ships, sum(x[3] for x in u), " ".join(scenes)))
    fm = sorted((m, sorted(set(x[1] for x in users[m]))) for m in users if not m & BATTLE_MAP_BIT)
    print("field map ids used as a battle background (group %d, or no load at all when it is the"
          " current scene):" % KIND_FIELD_MAP)
    for m, scenes in fm:
        print("  map %-4d %s" % (m, " ".join(scenes)))


def cmd_assets(args, archive):
    files = ps3_files(archive)
    by_ext = collections.Counter()
    by_dir = collections.Counter()
    size_ext = collections.Counter()
    for e in files:
        leaf = e.name.split("/")[-1]
        ext = leaf.split(".", 1)[1] if "." in leaf else ""
        rel = e.name[len(PS3_DIR):].split("/")
        by_ext[ext] += 1
        size_ext[ext] += e.size
        by_dir[(ext, "/".join(rel[2:-1]) or ("(btlmap root)" if len(rel) == 1 else "(map root)"))] += 1
    print("%d files under %s" % (len(files), PS3_DIR))
    for ext, n in by_ext.most_common():
        print("  %-10s %5d  %11d bytes" % (ext, n, size_ext[ext]))
    print("by directory inside a map:")
    for (ext, d), n in sorted(by_dir.items()):
        print("  %-10s %-22s %5d" % (ext, d, n))
    tools = os.environ.get("PZ_TOOLS", DEFAULT_PZ_TOOLS)
    sys.path.insert(0, tools)
    try:
        import phyre
    except ImportError:
        print("phyre.py not found in %s, set PZ_TOOLS to classify the .phyre files by class" % tools)
        return 0
    sig = collections.Counter()
    example = {}
    fails = collections.Counter()
    for e in files:
        if not e.name.endswith(".phyre"):
            continue
        ext = e.name.split("/")[-1].split(".", 1)[1]
        try:
            c = phyre.Cluster(archive.read(e), e.name, want_payload=False)
        except phyre.PhyreError as ex:
            fails[(ext, ex.kind)] += 1
            continue
        names = sorted(set(il.cls.name for il in c.lists))
        key = (ext, tuple(names))
        sig[key] += 1
        example.setdefault(key, e.name[len(PS3_DIR):])
    print(".phyre by the set of classes in the cluster (phyre.py):")
    for (ext, names), n in sorted(sig.items(), key=lambda kv: -kv[1]):
        shown = ", ".join(names) if len(names) <= 6 else "%d classes incl. %s" % (
            len(names), ", ".join(x for x in names if x.startswith(("PCamera", "PMesh", "PModifier", "PTexture"))))
        print("  %5d %-10s %s   e.g. %s" % (n, ext, shown or "(no instance lists)", example[(ext, names)]))
    for k, n in fails.items():
        print("  %5d %-10s refused: %s" % (n, k[0], k[1]))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-a", "--archive", default=vbf.DEFAULT_VBF)
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("list", help="every file under btlmap/ with its kind 26 row")
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser("survey", help="parse every container to the last byte, exit 1 on a failure")
    p.set_defaults(fn=cmd_survey)

    p = sub.add_parser("show", help="the region map of one container")
    p.add_argument("map")
    p.set_defaults(fn=cmd_show)

    p = sub.add_parser("rows", help="kind 26 rows and the btl.bin scenes that use each")
    p.set_defaults(fn=cmd_rows)

    p = sub.add_parser("assets", help="the ps3data/btlmap files by kind and by Phyre class")
    p.set_defaults(fn=cmd_assets)

    args = ap.parse_args(argv)
    if not getattr(args, "fn", None):
        ap.print_help()
        return 1
    with vbf.VbfArchive(args.archive) as archive:
        return args.fn(args, archive) or 0


if __name__ == "__main__":
    sys.exit(main())
