#!/usr/bin/env python
"""
mapfile.py - reader for the FINAL FANTASY X HD Remaster per-map container and walkmesh.

What this decodes
-----------------
Every field and battle map ships one container at

    ffx_ps2/ffx/master/jppc/map/<group>/<name>/bin/mapout.vpa
    ffx_ps2/ffx/master/jppc/btlmap/<group>/<name>/bin/mapout.vpa

inside FFX_Data.vbf. Despite the .vpa extension the bytes start with the ASCII magic
"MAP1" and the layout is the one FFX_Map_GetSection 0x907F00 reads: a 0x80 byte header
whose last 112 bytes are 28 u32 section offsets relative to the start of the file, 0
meaning the section is absent.

Section index 2 + zoneIndex is the walkmesh. In the PS2 source that section was a
standalone file called "ffxmap.id" - the path "%s/ffxmap.id" and the loader
FFX_Map_LoadCollisionFile 0x844D10 are still in the exe. The walkmesh is what
FFX_Ch_WalkmeshMove 0x83E5F0 collides a CHR against.

Usage
-----
    python mapfile.py list                       # every map in the archive
    python mapfile.py sections hiku01            # the MAP1 section table
    python mapfile.py walkmesh hiku01            # decoded walkmesh summary
    python mapfile.py obj hiku01 -o out.obj      # Wavefront OBJ of the walkmesh
    python mapfile.py verify                     # parse every map, report anomalies

Addresses in the comments are VAs at the preferred base 0x400000, the same as IDA shows.
"""

import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vbf


MAP1_MAGIC = b"MAP1"
MAP1_HEADER_SIZE = 0x80
MAP1_SECTION_COUNT = 28          # (0x80 - 16) / 4
MAP1_SECTION_TABLE_OFF = 16      # FFX_Map_GetSection reads base + 4*index + 16

# What sub_9097C0 (the MAP1 consumer, 0x9097C0) does with each index. Anything marked
# unknown is a section nothing has told us about yet.
MAP1_SECTION_ROLES = {
    0:  "background render data -> sub_91AA60 -> rcbgMain 0x921D60, sets draw flag 0x4000",
    1:  "map object / instance table -> sub_90F8E0 -> sub_90F900. Own magic, the first two "
        "dwords are 0x2140A5 and 0x110, then 64 byte records. Not decoded here.",
    2:  "walkmesh (ffxmap.id) for zone 0",
    3:  "walkmesh (ffxmap.id) for zone 1",
    4:  "walkmesh (ffxmap.id) for zone 2",
    5:  "walkmesh (ffxmap.id) for zone 3",
    6:  "walkmesh (ffxmap.id) for zone 4",
    7:  "walkmesh (ffxmap.id) for zone 5",
    8:  "walkmesh (ffxmap.id) for zone 6",
    9:  "walkmesh (ffxmap.id) for zone 7. In all 38 maps that have it this is a single flat "
        "1000x1000 two-triangle plane, so it is a free-move / cutscene plane, not real geometry.",
    10: "unknown -> sub_72A7F0. u8 at +1 sizes a 8KB*n buffer, u16 at +4 is a record count, "
        "float6 records from +32. Not collision.",
    11: "background render data -> sub_919D30, a thunk to the same sub_91AA60 as section 0",
    12: "background render data -> sub_919B90, also a thunk to sub_91AA60",
}

# Low 7 bits of the triangle attribute dword. sub_83E4E0 is the passability test and
# only these cases are not simply walkable. Codes 48..63 are indirected through the
# runtime table g_ffxGroundTypeRemap 0x13019D8, which an ATEL script writes with
# sub_83E9E0, so their effect is decided at run time and is not in the file.
SURFACE_CODE_NAMES = {
    0:  "walkable",
    1:  "wall (impassable for everyone)",
    2:  "player-only passage (blocks every non-player CHR)",
    13: "conditional on CHR m_flags2 bit 0x4",
    14: "player-blocked (everyone except the player may pass)",
}


class Map1(object):
    """The MAP1 container, i.e. one mapout.vpa."""

    def __init__(self, data, name="<mem>"):
        if data[:4] != MAP1_MAGIC:
            raise ValueError("%s: not a MAP1 container (magic %r)" % (name, data[:4]))
        self.name = name
        self.data = data
        # Bytes 4..15 are zero in every shipped file. Unknown, not padding by proof.
        self.header_unknown = struct.unpack_from("<3I", data, 4)
        if len(data) >= MAP1_HEADER_SIZE:
            self.sections = list(struct.unpack_from(
                "<%dI" % MAP1_SECTION_COUNT, data, MAP1_SECTION_TABLE_OFF))
        else:
            # A 64 byte stub, which 103 of the 491 shipped containers are. No sections.
            self.sections = [0] * MAP1_SECTION_COUNT

    def section(self, index):
        """What FFX_Map_GetSection 0x907F00 returns: the offset, or None if absent."""
        off = self.sections[index]
        return off if off else None

    def section_limit(self, index):
        """Upper bound of a section, taken as the next populated section or EOF."""
        off = self.sections[index]
        if not off:
            return None
        later = [s for s in self.sections if s > off]
        return min(later) if later else len(self.data)

    def walkmeshes(self):
        """Every section that parses as a walkmesh, as (index, Walkmesh)."""
        out = []
        for i in range(MAP1_SECTION_COUNT):
            off = self.section(i)
            if off is None:
                continue
            wm = Walkmesh.try_parse(self.data, off, self.section_limit(i))
            if wm is not None:
                out.append((i, wm))
        return out


class Walkmesh(object):
    """
    One ffxmap.id section.

    Header, 32 bytes. CONFIRMED fields are the ones the exe reads.

      +0x00  u32   reloc marker. 0 in the file. FFX_Map_RelocateCollision 0x844BC0
                   computes delta = &header - *(u32*)header, writes the delta back here
                   and turns the three stored offsets into pointers, so re-running it is
                   a no-op. CONFIRMED.
      +0x04  u16   format stamp. Only 1827 (0x0723) and 4611 (0x1203) ship. 0x844BC0
                   tests "> 4610" and pushes the result into g_ffxMapIsNewFormat
                   0x13034B8, which gates whether the per-triangle shade field is used
                   at all (sub_83E980 returns a flat 16912 when it is clear). CONFIRMED.
      +0x06  s16   group count, the number of 16 byte records at +0x1C. CONFIRMED
                   (0x844BC0 relocates exactly this many group records).
      +0x08  u16   unknown. 0 in all 469 shipped walkmeshes.
      +0x0A  u16   vertex count. INFERRED, not read by the exe, but
                   0x20 + 8*count == group[0].triOffset holds in every shipped file.
      +0x0C  f32   scale * 10. FFX_Ch_SetActiveWalkmesh 0x83EA00 does
                   g_ffxWalkmeshScale = this / 10.0. CONFIRMED.
      +0x10  u32   unknown. 0 everywhere.
      +0x14  u32   unknown. 0 everywhere.
      +0x18  u32   offset of the vertex array. 0x20 in every shipped file. CONFIRMED,
                   0x844BC0 relocates it and 0x83EA00 stores it in
                   g_ffxWalkmeshVerts 0x1301A8C.
      +0x1C  u32   offset of the group array. CONFIRMED, same two functions,
                   0x83EA00 reads group[0] only.

    Vertex, 8 bytes, CONFIRMED by sub_83E420 and FFX_Ch_WalkmeshFindTri:
      +0 s16 x, +2 s16 y, +4 s16 z, +6 s16 unknown (0 in every shipped file).
    All three are fixed point: world = value / scale. Y grows downward, which is why
    FFX_Ch_WalkmeshMove seeds the fallback ground normal as (0, -1, 0).

    Group record, 16 bytes:
      +0x00 u32 unknown, 0 in every shipped file (0x844BC0 does not relocate it)
      +0x04 u32 unknown, 0 in every shipped file
      +0x08 s16 triangle count. CONFIRMED, 0x83EA00 -> g_ffxWalkmeshTriCount 0x1301A88
      +0x0A s16 unknown, 0 everywhere
      +0x0C u32 offset of the triangle array. CONFIRMED, relocated by 0x844BC0,
                stored in g_ffxWalkmeshTris 0x1301A84 by 0x83EA00

    Triangle, 16 bytes, all CONFIRMED:
      +0x00 u16 vertex index 0
      +0x02 u16 vertex index 1
      +0x04 u16 vertex index 2
      +0x06 s16 neighbour triangle across edge v0-v1, -1 for none
      +0x08 s16 neighbour triangle across edge v1-v2, -1 for none
      +0x0A s16 neighbour triangle across edge v2-v0, -1 for none
      +0x0C u32 attributes, copied wholesale into CHR+0x828 m_groundAttrs:
                 bits  0..6   surface type, see SURFACE_CODE_NAMES
                 bits  7..8   enc   (sub_83D820)
                 bits  9..10  eff   (sub_83D7C0, footstep effect, indexes the per-map
                                     footstep bank at 0x1301AF8)
                 bits 11..12  dic   (sub_83D7A0)
                 bits 13..14  wat   (sub_83D920)
                 bits 15..16  snd   (sub_83D900)
                 bits 17..31  shd, a 5:5:5 ground shade colour, R = bits 17..21,
                              G = 22..26, B = 27..31 (sub_83E980 / FFX_Ch_GetLightR)
    """

    HEADER_FMT = "<IHhHHfII II"

    def __init__(self, data, off, limit=None):
        self.data = data
        self.off = off
        (self.reloc, self.format_stamp, self.group_count, self.unk08,
         self.vertex_count, scale10, self.unk10, self.unk14,
         self.vert_off, self.group_off) = struct.unpack_from(self.HEADER_FMT, data, off)
        self.scale10 = scale10
        self.scale = scale10 / 10.0
        self.is_new_format = self.format_stamp > 4610
        self.limit = limit if limit is not None else len(data)

        self.vertices = [struct.unpack_from("<4h", data, off + self.vert_off + 8 * i)
                         for i in range(self.vertex_count)]
        self.groups = []
        for i in range(self.group_count):
            g0, g1, tri_count, pad, tri_off = struct.unpack_from(
                "<IIhhI", data, off + self.group_off + 16 * i)
            tris = [struct.unpack_from("<3H3hI", data, off + tri_off + 16 * t)
                    for t in range(tri_count)]
            self.groups.append(dict(unk00=g0, unk04=g1, tri_count=tri_count,
                                    unk0A=pad, tri_off=tri_off, tris=tris))

    # ---- signature test -------------------------------------------------------

    @classmethod
    def try_parse(cls, data, off, limit=None):
        """Parse off as a walkmesh, or return None. Deliberately strict."""
        limit = limit if limit is not None else len(data)
        if off + 32 > len(data):
            return None
        try:
            reloc, stamp, gc, unk08, vc, scale10, u10, u14, voff, goff = \
                struct.unpack_from(cls.HEADER_FMT, data, off)
        except struct.error:
            return None
        if reloc != 0 or gc <= 0 or gc > 64 or vc == 0:
            return None
        if voff != 0x20:                        # true in every shipped file
            return None
        if not 0 < goff <= limit - off:
            return None
        if not 0.0 < scale10 < 1e6:
            return None
        if off + goff + 16 * gc > len(data):
            return None
        for i in range(gc):
            try:
                _, _, tc, _, toff = struct.unpack_from("<IIhhI", data, off + goff + 16 * i)
            except struct.error:
                return None
            if tc < 0 or toff <= 0 or off + toff + 16 * tc > len(data):
                return None
        try:
            return cls(data, off, limit)
        except struct.error:
            return None

    # ---- derived ------------------------------------------------------------

    def world_vertices(self):
        """Vertices in world units. Y is negated so that up is +Y, as in a viewer."""
        s = self.scale
        return [(v[0] / s, -v[1] / s, v[2] / s) for v in self.vertices]

    def check(self):
        """Self-consistency. Returns a list of human readable complaints."""
        bad = []
        if self.vert_off != 0x20:
            bad.append("vertex array not at +0x20")
        if self.groups:
            # The vertex array is followed immediately by the triangle array. Some
            # files carry one spare 8 byte slot between the two, so allow count or
            # count+1 entries.
            slots = (self.groups[0]["tri_off"] - 0x20) // 8
            if slots not in (self.vertex_count, self.vertex_count + 1):
                bad.append("vertex array holds %d slots, header says %d"
                           % (slots, self.vertex_count))
        for gi, g in enumerate(self.groups):
            tc = g["tri_count"]
            for ti, t in enumerate(g["tris"]):
                if max(t[0], t[1], t[2]) >= self.vertex_count:
                    bad.append("g%d t%d vertex index out of range" % (gi, ti))
                    break
            for ti, t in enumerate(g["tris"]):
                for e, nb in enumerate(t[3:6]):
                    if nb == -1:
                        continue
                    if not 0 <= nb < tc:
                        bad.append("g%d t%d edge %d neighbour %d out of range"
                                   % (gi, ti, e, nb))
                        continue
                    if ti not in g["tris"][nb][3:6]:
                        bad.append("g%d t%d edge %d neighbour %d is not mutual"
                                   % (gi, ti, e, nb))
        return bad

    def bounds(self):
        wv = self.world_vertices()
        if not wv:
            return None
        xs, ys, zs = zip(*wv)
        return (min(xs), max(xs)), (min(ys), max(ys)), (min(zs), max(zs))

    # ---- output -------------------------------------------------------------

    def describe(self, verbose=False):
        out = []
        out.append("walkmesh at +0x%X" % self.off)
        out.append("  format stamp   : %d (0x%04X) %s" % (
            self.format_stamp, self.format_stamp,
            "new, per-triangle shade used" if self.is_new_format
            else "old, shade forced to 16912"))
        out.append("  scale          : %.4f (stored %.1f, world = fixed / scale)"
                   % (self.scale, self.scale10))
        out.append("  vertices       : %d at +0x%X" % (self.vertex_count, self.vert_off))
        out.append("  groups         : %d at +0x%X" % (self.group_count, self.group_off))
        out.append("  header unknown : +0x08=%d +0x10=%d +0x14=%d"
                   % (self.unk08, self.unk10, self.unk14))
        b = self.bounds()
        if b:
            out.append("  world bounds   : x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f"
                       % (b[0][0], b[0][1], b[1][0], b[1][1], b[2][0], b[2][1]))
        wpad = set(v[3] for v in self.vertices)
        out.append("  vertex +6 slot : %s" % sorted(wpad))
        for gi, g in enumerate(self.groups):
            out.append("  group %d: %d triangles at +0x%X, unknown +0x00=%d +0x04=%d +0x0A=%d"
                       % (gi, g["tri_count"], g["tri_off"], g["unk00"], g["unk04"], g["unk0A"]))
            codes = collections.Counter(t[6] & 0x7F for t in g["tris"])
            for code, n in sorted(codes.items()):
                out.append("      surface %3d x%-6d %s" % (
                    code, n, SURFACE_CODE_NAMES.get(
                        code, "remapped at run time through g_ffxGroundTypeRemap"
                        if 48 <= code <= 63 else "unknown")))
            open_edges = sum(1 for t in g["tris"] for nb in t[3:6] if nb == -1)
            out.append("      open edges (no neighbour): %d of %d"
                       % (open_edges, 3 * g["tri_count"]))
            bad = self.check()
            out.append("      consistency: %s" % ("ok" if not bad else "; ".join(bad[:4])))
            if verbose:
                for ti, t in enumerate(g["tris"]):
                    attr = t[6]
                    out.append("      t%-5d v=(%d,%d,%d) nb=(%d,%d,%d) attr=%08X "
                               "type=%d enc=%d eff=%d dic=%d wat=%d snd=%d shd=%d/%d/%d"
                               % (ti, t[0], t[1], t[2], t[3], t[4], t[5], attr,
                                  attr & 0x7F, (attr >> 7) & 3, (attr >> 9) & 3,
                                  (attr >> 11) & 3, (attr >> 13) & 3, (attr >> 15) & 3,
                                  (attr >> 17) & 0x1F, (attr >> 22) & 0x1F,
                                  (attr >> 27) & 0x1F))
        return "\n".join(out)

    def to_obj(self, map_name="walkmesh", group=None):
        """Wavefront OBJ. One object group per walkmesh group, one material-ish
        group per surface code so a viewer can colour the walls."""
        lines = ["# FFX walkmesh %s" % map_name,
                 "# scale %.4f, Y negated so up is +Y" % self.scale]
        for x, y, z in self.world_vertices():
            lines.append("v %.5f %.5f %.5f" % (x, y, z))
        for gi, g in enumerate(self.groups):
            if group is not None and gi != group:
                continue
            by_code = collections.defaultdict(list)
            for t in g["tris"]:
                by_code[t[6] & 0x7F].append(t)
            for code in sorted(by_code):
                lines.append("g group%d_surface%d" % (gi, code))
                for t in by_code[code]:
                    lines.append("f %d %d %d" % (t[0] + 1, t[1] + 1, t[2] + 1))
        return "\n".join(lines) + "\n"


# -----------------------------------------------------------------------------
# archive side

def iter_map_entries(archive):
    for e in archive.entries:
        if e.name.endswith("/bin/mapout.vpa"):
            yield e


def map_key(name):
    """'ffx_ps2/.../map/hiku/hiku01/bin/mapout.vpa' -> 'hiku01'."""
    return name.split("/")[-3]


def find_map(archive, want):
    want = want.lower()
    exact, partial = [], []
    for e in iter_map_entries(archive):
        k = map_key(e.name)
        if k == want:
            exact.append(e)
        elif want in e.name.lower():
            partial.append(e)
    return exact or partial


def load(archive, want):
    hits = find_map(archive, want)
    if not hits:
        raise SystemExit("no map matching %r" % want)
    if len(hits) > 1:
        sys.stderr.write("note: %d matches, using %s\n" % (len(hits), hits[0].name))
    e = hits[0]
    return e.name, Map1(archive.read(e), e.name)


# -----------------------------------------------------------------------------
# commands

def cmd_list(args, archive):
    rows = sorted((map_key(e.name), e.size, e.name) for e in iter_map_entries(archive))
    for k, size, n in rows:
        kind = "btlmap" if "/btlmap/" in n else "map"
        print("%-10s %-7s %10d  %s" % (k, kind, size, n))
    print("%d containers" % len(rows))


def cmd_sections(args, archive):
    name, m = load(archive, args.map)
    print(name)
    print("magic MAP1, header unknown dwords at +4: %s" % (m.header_unknown,))
    for i, off in enumerate(m.sections):
        if not off:
            continue
        lim = m.section_limit(i)
        wm = Walkmesh.try_parse(m.data, off, lim)
        print("  [%2d] +0x%08X  size<=%-9d %s%s"
              % (i, off, lim - off, MAP1_SECTION_ROLES.get(i, "unknown"),
                 "  <- parses as a walkmesh" if wm else ""))


def cmd_walkmesh(args, archive):
    name, m = load(archive, args.map)
    print(name)
    found = m.walkmeshes()
    if not found:
        print("  no section parses as a walkmesh")
        return
    for idx, wm in found:
        print("section [%d]" % idx)
        print(wm.describe(verbose=args.verbose))


def cmd_obj(args, archive):
    name, m = load(archive, args.map)
    found = m.walkmeshes()
    if not found:
        raise SystemExit("%s has no walkmesh" % name)
    if args.section is not None:
        found = [(i, w) for i, w in found if i == args.section]
        if not found:
            raise SystemExit("section %d is not a walkmesh" % args.section)
    out = args.out
    if out is None:
        out = "%s_walkmesh.obj" % map_key(name)
    if len(found) == 1:
        idx, wm = found[0]
        with open(out, "w") as f:
            f.write(wm.to_obj("%s section %d" % (map_key(name), idx)))
        print("wrote %s: %d vertices, %d triangles"
              % (out, wm.vertex_count, sum(g["tri_count"] for g in wm.groups)))
    else:
        root, ext = os.path.splitext(out)
        for idx, wm in found:
            p = "%s_s%d%s" % (root, idx, ext)
            with open(p, "w") as f:
                f.write(wm.to_obj("%s section %d" % (map_key(name), idx)))
            print("wrote %s: %d vertices, %d triangles"
                  % (p, wm.vertex_count, sum(g["tri_count"] for g in wm.groups)))


def cmd_verify(args, archive):
    entries = sorted(iter_map_entries(archive), key=lambda e: e.name)
    n_stub = n_wm = 0
    stamps = collections.Counter()
    unknowns = collections.Counter()
    codes = collections.Counter()
    sect_idx = collections.Counter()
    complaints = []
    group_counts = collections.Counter()
    for e in entries:
        m = Map1(archive.read(e), e.name)
        if not any(m.sections):
            n_stub += 1
            continue
        found = m.walkmeshes()
        if not found:
            complaints.append("%s: no walkmesh section" % map_key(e.name))
            continue
        for idx, wm in found:
            n_wm += 1
            sect_idx[idx] += 1
            stamps[wm.format_stamp] += 1
            group_counts[wm.group_count] += 1
            unknowns[("+0x08", wm.unk08)] += 1
            unknowns[("+0x10", wm.unk10)] += 1
            unknowns[("+0x14", wm.unk14)] += 1
            unknowns[("vtx+6", tuple(sorted(set(v[3] for v in wm.vertices))))] += 1
            for g in wm.groups:
                unknowns[("grp+0x00", g["unk00"])] += 1
                unknowns[("grp+0x04", g["unk04"])] += 1
                unknowns[("grp+0x0A", g["unk0A"])] += 1
                for t in g["tris"]:
                    codes[t[6] & 0x7F] += 1
            bad = wm.check()
            if bad:
                complaints.append("%s[%d]: %s" % (map_key(e.name), idx, "; ".join(bad[:3])))
    print("containers        : %d (%d are 64 byte stubs with no sections)"
          % (len(entries), n_stub))
    print("walkmesh sections : %d" % n_wm)
    print("section indices   : %s" % sorted(sect_idx.items()))
    print("group counts      : %s" % sorted(group_counts.items()))
    print("format stamps     : %s" % sorted(stamps.items()))
    print("surface codes     : %s" % sorted(codes.items()))
    print("fields that are always the same value:")
    for k, v in sorted(unknowns.items(), key=lambda kv: str(kv[0])):
        print("   %-10s = %-20s in %d sections" % (k[0], k[1], v))
    if complaints:
        print("ANOMALIES (%d):" % len(complaints))
        for c in complaints[:40]:
            print("   " + c)
    else:
        print("no anomalies: every triangle index, neighbour link and offset checks out")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-a", "--archive", default=vbf.DEFAULT_VBF)
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("list", help="every map container in the archive")
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser("sections", help="MAP1 section table of one map")
    p.add_argument("map")
    p.set_defaults(fn=cmd_sections)

    p = sub.add_parser("walkmesh", help="decode the walkmesh of one map")
    p.add_argument("map")
    p.add_argument("-v", "--verbose", action="store_true", help="dump every triangle")
    p.set_defaults(fn=cmd_walkmesh)

    p = sub.add_parser("obj", help="write the walkmesh as a Wavefront OBJ")
    p.add_argument("map")
    p.add_argument("-o", "--out")
    p.add_argument("-s", "--section", type=int)
    p.set_defaults(fn=cmd_obj)

    p = sub.add_parser("verify", help="parse every map and report anomalies")
    p.set_defaults(fn=cmd_verify)

    args = ap.parse_args(argv)
    if not getattr(args, "fn", None):
        ap.print_help()
        return 1
    with vbf.VbfArchive(args.archive) as archive:
        args.fn(args, archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
