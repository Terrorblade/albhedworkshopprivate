#!/usr/bin/env python
"""
mapparticle.py - reader for the FFX map particle set, MAP1 section 10 (the PS2 .pdt format).

What this decodes
-----------------
Section 10 of a mapout.vpa (see mapfile.py) is the field placed particle set. The exe loads it with
FFX_MapFx_LoadParticleSet 0x72A7F0, which relocates it through FFX_Vfx_RelocateParticleSet 0x711FA0
against the exe's ppp operator table g_ffxPppOpTable 0xC3A508 (145 records of 40 bytes), and every
step FFX_MapFx_UpdatePlacedEffects 0x729AB0 runs it. The layout, every offset relative to the set:

    +0x00  32 byte header      u8 format, u8 count of 8 KB extra heap blocks, u8 work buffer class,
                               u8 0, u16 placed record count, u16 program count, u16 model count,
                               u16 shape count, u16 vertex list count, u16 0, then four u32 table
                               offsets: programs, models, shapes, vertex lists
    +0x20  placed records      80 bytes each, the emitters (PLACED_FIELDS below)
    tables                     programs: u32 offsets. models: 32 byte records with offsets at +20,
                               +24, +28. shapes: u32 offsets. vertex lists: 8 byte records
                               {s16 model, s16 count, u32 offset}
    programs                   16 byte header {u32 end time, u8 loop, 3 x u8 0, u32 op list offset,
                               u32 node list offset}, a chain of nodes from +16, the chain's zero
                               terminator, the op list {u32 n, n x u32 op index} and the node list
                               {u32 n, n x u32 node offset}. Every offset is relative to the program
    node                       40 byte header (NODE_FIELDS) and 16 byte entries {u32 op index,
                               u16 key stride, u16 key count, u32 key offset, u32 descriptor offset}
    keys                       key count x stride bytes, each key starting with u32 time, then the
                               u32 0xFFFFF000 terminator, then the entry's descriptor (u32 work
                               offsets into the particle, op specific length)

Times are fixed point, 4096 per effects pass (one step at normal speed).

Every byte of every shipped set is accounted for by the survey command. A region is one of

    sized    its extent comes from a count, stride or terminator the exe reads
    bounded  a blob whose start is a referenced offset and whose end is the next referenced
             offset: descriptors, the node +4 tables, and the model, shape and vertex list bodies
             (the model draw ops read those, they are not decoded here)
    pad      zero bytes between regions
    stale    under 16 bytes before a 16 byte aligned start that are not zero

Usage
-----
    python -I mapparticle.py survey            # parse every section 10 to the last byte, exit 1 on failure
    python -I mapparticle.py census            # operator, record, program and node statistics
    python -I mapparticle.py show kami00 12    # one set, optionally only some programs
    python -I mapparticle.py rain              # every pppDrawRain instance and what starts it

Nothing is written anywhere. The archive is opened read only and parsed in memory.
Addresses in the comments are VAs at the preferred base 0x400000, the same as IDA shows.
reversing/MAP_PARTICLES.md is the write-up.
"""

import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vbf
import mapfile
from btlmap import Regions


KEY_END = 0xFFFFF000          # a key time no particle age reaches, the key stepper in sub_717010 stops on it
NODE_HEADER = 40
ENTRY_SIZE = 16
PLACED_SIZE = 80
PROGRAM_HEADER = 16
TIME_ONE = 4096               # one effects pass, the dt sub_7123C0 is given by the update
TIME_INFINITE = 0x70000000    # node lifetime that never ends, sub_717010 frees a stopping program on it
TIME_NEVER = 0x7FFFFFFF       # node birth time that the clock never reaches, only an emitter op births it

# g_ffxPppOpTable 0xC3A508, index -> operator name. The name string is at record +0.
PPP_OPS = (
    "pppAccele", "pppAngAccele", "pppSclAccele", "pppColAccele", "pppMove", "pppAngMove",
    "pppSclMove", "pppColMove", "pppPoint", "pppAngle", "pppScale", "pppColor", "pppRandFV",
    "pppRandIV", "pppSRandFV", "pppSMatrix", "pppMatrixXYZ", "pppMatrixScl", "pppParMatrix",
    "pppDrawMatrix", "pppDrawMatrixFront", "pppDrawMdl", "pppDrawMdlSemi", "pppDrawMdlTs",
    "pppDrawShape", "pppKeShpTail", "pppKeShpTail2", "pppVertexAp", "pppVertexApLc", "pppRandUpFV",
    "pppRandDownFV", "pppKeBornRnd5", "pppKeLnsLp", "pppKeLnsArnd", "pppKeLnsClm", "pppKeLnsCrn",
    "pppKeLnsFls", "pppSRandDownFV", "pppRandDownHCV", "pppSRandUpFV", "pppRandDownIV",
    "pppPointAp", "pppKeMdlTfd", "pppKeMvYpEff", "pppKeDMat", "pppKeShpDtt", "pppKeShpTail3",
    "pppKeLnsLpSft", "pppDrawMatrixWood", "pppDrawShapeField", "pppKeMdlDtt", "pppDrawMdlSea",
    "pppKeDMatFr", "pppKeHitBall", "pppKeHitChkPxB", "pppDrawMdl2", "pppKeThRes16", "pppKeDrct",
    "pppMatrixLoc", "pppKeThTp", "pppKeThSft", "pppKeTh", "pppKeBornRnd3", "pppKeBornRnd2",
    "pppSRandHCV", "pppKeThRes32", "pppSRandCV", "pppKeBornRnd6", "pppKeThRes40", "pppKeThRes64x4",
    "pppKeBornRnd", "pppDrawMdlTs2", "pppEiWindFun", "pppKeThRes24x4", "pppKeThRes16x4",
    "pppKeThRes64x16", "pppKeThRes48x4", "pppKeZCrct", "pppDrawMdlTs3", "pppKeZCrctShp",
    "pppDrawMdl3", "pppDrawShapeX", "pppEiWfacc", "pppMatrixXZY", "pppKeShpTailX", "pppRandHCV",
    "pppVtMime", "pppRandFloat", "pppDrawMdlInf", "pppDrawMdlSemi2", "pppKeThRes24", "pppKeThRes8",
    "pppKeGrvTgt", "pppKeGrvEff", "pppDrawMdlSemi3", "pppRandDownFloat", "pppSRandDownHCV",
    "pppDrawRain", "pppFaceAp", "pppKeShpTail2X", "pppFpPointLight", "pppDrawMdlPSim",
    "pppDrawShapeCamera", "pppRandUpHCV", "pppAngMoveLoop", "pppPointLoop", "pppAngleLoop",
    "pppScaleLoop", "pppMatrixLoop", "pppDrawMatrixLoop", "pppDrawMdlLoop", "pppNeiLightEikyo",
    "pppDrawMdlCamera", "pppDrawMdlCameraLoop", "pppVertexAttend", "pppMatrixYXZ", "pppVertexApAt",
    "pppSclMoveLoop", "pppVertexApDisPos", "pppDrawShapeRev", "pppDrawMdlLoopZ",
    "pppDrawMdlLoopDisPos", "pppEiZCrctDisPos", "pppDrawShapeFieldRev", "pppKeShpTail3X",
    "pppDummyFunc", "pppNeiPointLight", "pppDrawShapeCameraDisPos", "pppKeThRes48",
    "pppFpPointLightModel", "pppFpPointLightVsf", "pppDrawShapeFieldSpd", "pppKeMatSN",
    "pppDrawShapeFieldGlobal", "pppFpPointLightModelScl", "pppFpPointLightVsfScl", "pppKeThRes32x4",
    "pppMatrixZXY", "pppDrawFilter", "pppDrawMatrixNoRot", "pppKeLnsLpT", "pppKeLnsArndT",
    "pppKeLnsFlsT", "pppKeLnsClmT", "pppKeLnsCrnT",
)
OP_DRAW_RAIN = 97
OP_FP_POINT_LIGHTS = (100, 129, 130, 134, 135)

# Placed record, 80 bytes. Where it goes: FFX_MapFx_LoadParticleSet copies +0..+64 into the 128 byte
# runtime slot at unk_D2C260 + 128 * i (slot +0x40.. for +0..+44, slot +0 for +48, slot +0x20.. for
# +56..+64). The update reads +52, +68 and +74 from the record itself.
PLACED_FIELDS = (
    (0, "4f", "pos", "translation, w 1. MapFx__f72A2E0 puts it in the record matrix row 3"),
    (16, "4i", "rot", "angles, 65536 per turn (FFX_Ppp_RotMatrixZ indexes a 4096 sine table by a >> 4)"),
    (32, "4f", "scale", "per axis scale of the rotation rows, w 1"),
    (48, "i", "delay", "start countdown in steps, slot +0. 0 starts on the first step, -4096 is dormant"),
    (52, "I", "program", "program index, sub_7123C0 is handed programs[i] at every (re)start"),
    (56, "f", "cull_far", "group 0 frustum cull: negative never culls, else the view depth limit"),
    (60, "f", "cull_x", "group 0 frustum cull: half width added to the view position"),
    (64, "f", "cull_y", "group 0 frustum cull: half height"),
    (68, "H", "group", "effect group, the key of mapfx:2, 3, 4, 5, 9, 10, 11, 13, 95 and core:558. 0 is frustum culled"),
    (70, "H", "group2", "second group word, the key of mapfx:53, 54 and 96"),
    (72, "H", "w72", "not read"),
    (74, "B", "view", "matrix mode MapFx__f72A260: 0 view * M, 1 camera aligned, 2 the shared basis of MapFx__f72A690"),
    (75, "B", "rot_order", "MapFx__f72A2E0: 0 XYZ, 1 XZY, 2 YXZ, 3 YZX, 4 ZXY, 5 ZYX"),
    (76, "I", "tail", "not read"),
)

# Node header, 40 bytes. sub_7120F0 relocates only +0 and the entries.
NODE_FIELDS = (
    (0, "I", "next", "next node, relocated. The chain ends at a node whose +0 is 0"),
    (4, "I", "off4", "an offset into the node data. No runtime read found"),
    (8, "I", "w8", "a node offset or a float. No runtime read found"),
    (12, "I", "flags", "bit 0: skip every Draw column of this node (sub_716C40, sub_717010)"),
    (16, "I", "birth", "slot time at which sub_712B80 births the one particle, 0x7FFFFFFF never"),
    (20, "I", "life", "particle age at which it is freed, 0x70000000 never"),
    (24, "I", "loop_to", "age a looping particle restarts at"),
    (28, "I", "loop_at", "age at which it loops (ReCon runs), unless the program is stopping"),
    (32, "I", "keytab", "particle size before its key pointer table: 160 header bytes plus op work"),
    (36, "h", "order", "birth sort key, sub_716770 inserts before the first particle with a larger one"),
    (38, "h", "nops", "entry count"),
)

# pppDrawRain key, 64 bytes, as pppDrawRain__f756320 (create) and pppDrawRain__f755A00 (step) read it.
RAIN_FIELDS = (
    (0, "I", "time", "key time"),
    (4, "f", "half", "half size of the cube around the camera the drops live in"),
    (8, "I", "drops", "drop count, 48 bytes each from the ppp heap"),
    (12, "I", "k12", "not read"),
    (16, "3f", "vel", "velocity per effects pass"),
    (28, "I", "k28", "not read"),
    (32, "3f", "vel_jitter", "plus or minus this per axis, once per drop at spawn"),
    (44, "I", "k44", "not read"),
    (48, "f", "streak", "streak length, opposite the velocity"),
    (52, "f", "streak_jitter", "plus or minus this, once per drop at spawn"),
    (56, "8s", "k56", "not read"),
)


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def unpack_fields(d, base, fields):
    out = collections.OrderedDict()
    for off, fmt, name, _ in fields:
        v = struct.unpack_from("<" + fmt, d, base + off)
        out[name] = v[0] if len(v) == 1 else v
    return out


def op_name(i):
    return PPP_OPS[i] if 0 <= i < len(PPP_OPS) else "op%d" % i


class ParticleSet(object):
    """One section 10. Raises ValueError on anything the exe would read out of bounds."""

    def __init__(self, data, base, limit, name="<mem>"):
        self.d = data
        self.base = base
        self.limit = limit
        self.name = name
        d = data
        self.size = limit - base
        (self.format, self.heap_blocks, self.work_class, self.b3, self.n_placed, self.n_programs,
         self.n_models, self.n_shapes, self.n_vlists, self.h14) = struct.unpack_from("<4B6H", d, base)
        self.off_programs, self.off_models, self.off_shapes, self.off_vlists = \
            struct.unpack_from("<4I", d, base + 16)
        self.placed = [unpack_fields(d, base + 32 + PLACED_SIZE * i, PLACED_FIELDS)
                       for i in range(self.n_placed)]
        self.program_offsets = [self.u(self.off_programs + 4 * i) for i in range(self.n_programs)]
        self.programs = [self.parse_program(p) for p in self.program_offsets]

    def u(self, rel):
        if not 0 <= rel <= self.size - 4:
            raise ValueError("%s: read at set +0x%X outside the set" % (self.name, rel))
        return u32(self.d, self.base + rel)

    def parse_program(self, p):
        d, b = self.d, self.base
        prog = {"off": p}
        prog["end"] = self.u(p)
        prog["loop"] = d[b + p + 4]
        prog["pad5"] = d[b + p + 5:b + p + 8]
        prog["off_ops"] = self.u(p + 8)
        prog["off_nodes"] = self.u(p + 12)
        n = self.u(p + prog["off_ops"])
        prog["ops"] = [self.u(p + prog["off_ops"] + 4 + 4 * k) for k in range(n)]
        n = self.u(p + prog["off_nodes"])
        prog["node_list"] = [self.u(p + prog["off_nodes"] + 4 + 4 * k) for k in range(n)]
        nodes = []
        node = 16
        prog["chain_end"] = None
        for _ in range(4096):
            fields = unpack_fields(d, b + p + node, NODE_FIELDS)
            entries = []
            for k in range(max(fields["nops"], 0)):
                e = p + node + NODE_HEADER + ENTRY_SIZE * k
                op, stride, nkeys, okeys, odesc = struct.unpack_from("<IHHII", d, b + e)
                entries.append({"op": op, "stride": stride, "nkeys": nkeys,
                                "keys": okeys, "desc": odesc})
            nodes.append({"off": node, "f": fields, "entries": entries})
            nxt = fields["next"]
            if nxt == 0:
                prog["chain_end"] = None
                break
            if self.u(p + nxt) == 0:
                # sub_7120F0 stops when the node it would move to has a zero +0. That dword is the
                # chain terminator, and only it is read.
                prog["chain_end"] = nxt
                break
            node = nxt
        else:
            raise ValueError("%s: program +0x%X node chain does not end" % (self.name, p))
        prog["nodes"] = nodes
        return prog

    def keys(self, prog, entry):
        """Yield (time, absolute offset) of every key of one entry, up to the terminator."""
        p = prog["off"]
        for j in range(entry["nkeys"]):
            k = p + entry["keys"] + entry["stride"] * j
            yield self.u(k), self.base + k


# -----------------------------------------------------------------------------
# coverage

def cover_set(ps, r, problems):
    """Add every region of one set to r (a btlmap.Regions over the whole container)."""
    b = ps.base
    n = ps.size
    sized = []        # (rel start, size, label)
    anchors = {}      # rel start -> label, blobs bounded by the next start

    def add_sized(start, size, label):
        if size <= 0:
            return
        if start < 0 or start + size > n:
            problems.append("%s runs past the set (+0x%X, %d bytes)" % (label, start, size))
            return
        sized.append((start, size, label))

    add_sized(0, 32, "header")
    add_sized(32, PLACED_SIZE * ps.n_placed, "placed records")
    add_sized(ps.off_programs, 4 * ps.n_programs, "program table")
    add_sized(ps.off_models, 32 * ps.n_models, "model table")
    add_sized(ps.off_shapes, 4 * ps.n_shapes, "shape table")
    add_sized(ps.off_vlists, 8 * ps.n_vlists, "vertex list table")
    for k in range(ps.n_models):
        for slot in (20, 24, 28):
            v = ps.u(ps.off_models + 32 * k + slot)
            if v:
                anchors.setdefault(v, "model data")
    for k in range(ps.n_shapes):
        v = ps.u(ps.off_shapes + 4 * k)
        if v:
            anchors.setdefault(v, "shape data")
    for k in range(ps.n_vlists):
        v = ps.u(ps.off_vlists + 8 * k + 4)
        if v:
            anchors.setdefault(v, "vertex list data")

    for pi, prog in enumerate(ps.programs):
        p = prog["off"]
        tag = "program %d" % pi
        add_sized(p, PROGRAM_HEADER, tag + " header")
        add_sized(p + prog["off_ops"], 4 + 4 * len(prog["ops"]), tag + " op list")
        add_sized(p + prog["off_nodes"], 4 + 4 * len(prog["node_list"]), tag + " node list")
        if prog["chain_end"] is not None:
            add_sized(p + prog["chain_end"], 4, tag + " chain terminator")
        if prog["pad5"] != b"\0\0\0":
            problems.append("%s header +5..+7 not zero" % tag)
        if prog["node_list"] != [nd["off"] for nd in prog["nodes"]]:
            problems.append("%s node list is not the node chain" % tag)
        used = set()
        for nd in prog["nodes"]:
            no = p + nd["off"]
            f = nd["f"]
            add_sized(no, NODE_HEADER + ENTRY_SIZE * len(nd["entries"]), tag + " node")
            if f["nops"] <= 0:
                problems.append("%s node +0x%X has %d entries" % (tag, nd["off"], f["nops"]))
            if f["off4"]:
                anchors.setdefault(p + f["off4"], "node +4 data")
            for e in nd["entries"]:
                used.add(e["op"])
                if not 0 <= e["op"] < len(PPP_OPS):
                    problems.append("%s op index %d past the 145 record table" % (tag, e["op"]))
                ke = p + e["keys"] + e["stride"] * e["nkeys"]
                add_sized(p + e["keys"], e["stride"] * e["nkeys"] + 4, tag + " keys")
                if ps.u(ke) != KEY_END:
                    problems.append("%s keys of %s do not end in 0xFFFFF000" % (tag, op_name(e["op"])))
                if e["desc"] != e["keys"] + e["stride"] * e["nkeys"] + 4:
                    problems.append("%s descriptor of %s is not right after the keys" % (tag, op_name(e["op"])))
                anchors.setdefault(p + e["desc"], "descriptor")
        if used != set(prog["ops"]):
            problems.append("%s op list %s is not the set of ops its nodes use" % (tag, prog["ops"]))
        # The program extent ends at its node list. Nothing in it may point past that.
    for a in anchors:
        if not 0 < a < n:
            problems.append("offset +0x%X outside the set" % a)
    if problems:
        return

    starts = sorted(set([s for s, _, _ in sized] + list(anchors)))
    sized_at = {}
    for s, size, label in sized:
        if s in sized_at and sized_at[s][0] != size:
            problems.append("two sized objects at +0x%X: %s and %s" % (s, sized_at[s][1], label))
        sized_at[s] = (size, label)
    for idx, s in enumerate(starts):
        nxt = starts[idx + 1] if idx + 1 < len(starts) else n
        if s in sized_at:
            size, label = sized_at[s]
            if s + size > nxt:
                problems.append("%s at +0x%X overlaps the next object at +0x%X" % (label, s, nxt))
            r.add(b + s, b + s + size, "sized", "s10 " + label)
            if s in anchors and s + size < nxt:
                r.add(b + s + size, b + nxt, "bounded", "s10 " + anchors[s])
        else:
            r.add(b + s, b + nxt, "bounded", "s10 " + anchors[s])


def survey_one(name, data):
    """Returns (ParticleSet or None, Regions over the set, problems)."""
    m = mapfile.Map1(data, name)
    off = m.section(10)
    if off is None:
        return None, None, []
    limit = m.section_limit(10)
    problems = []
    sub = Regions(data[off:limit])
    try:
        ps = ParticleSet(data, off, limit, name)
    except (ValueError, struct.error) as e:
        return None, sub, [str(e)]
    shifted = Regions(data)
    cover_set(ps, shifted, problems)
    # re-base onto the section so the totals count only section 10 bytes
    for start, end, kind, label in shifted.rows:
        sub.add(start - off, end - off, kind, label)
    rows = sub.close()
    # The exporter aligns the first key array after a node's entries and does not always clear
    # the gap. Nothing counts or points into those bytes.
    for i, (start, end, kind, label) in enumerate(rows):
        if (kind == "UNCLAIMED" and end - start < 16 and 0 < i < len(rows) - 1
                and rows[i - 1][3].endswith(" node") and rows[i + 1][3].endswith(" keys")):
            rows[i] = (start, end, "stale", "key alignment padding, stale bytes")
            sub.problems.remove("%d unclaimed non zero bytes at 0x%X..0x%X" % (end - start, start, end))
    sub.rows = rows
    problems.extend(sub.problems)
    return ps, sub, problems


def iter_sets(archive):
    for e in sorted(mapfile.iter_map_entries(archive), key=lambda e: e.name):
        data = archive.read(e)
        if data[:4] != mapfile.MAP1_MAGIC:
            continue
        m = mapfile.Map1(data, e.name)
        if m.section(10) is None:
            continue
        yield e, data


def kind_of(name):
    return "btl" if "/btlmap/" in name else "fld"


# -----------------------------------------------------------------------------
# commands

def cmd_survey(args, archive):
    totals = collections.Counter()
    status = collections.Counter()
    failures = []
    for e, data in iter_sets(archive):
        ps, r, problems = survey_one(e.name, data)
        key = mapfile.map_key(e.name)
        st = "FAIL" if problems else "ok"
        status[st] += 1
        status[kind_of(e.name)] += 1
        if r is not None:
            t = r.totals()
            totals.update(t)
            totals["bytes"] += ps.size if ps else 0
        if ps is not None:
            totals["placed"] += ps.n_placed
            totals["programs"] += ps.n_programs
            totals["nodes"] += sum(len(p["nodes"]) for p in ps.programs)
            totals["entries"] += sum(len(nd["entries"]) for p in ps.programs for nd in p["nodes"])
        if args.verbose or problems:
            print("%-4s %-10s %s %8d bytes  placed %3d programs %3d  %s" % (
                st, key, kind_of(e.name), ps.size if ps else 0, ps.n_placed if ps else 0,
                ps.n_programs if ps else 0,
                " ".join("%s %d" % kv for kv in sorted(r.totals().items())) if r else ""))
        for p in problems[:6]:
            print("      problem: " + p)
        if problems:
            failures.append(key)
    print("section 10 sets       : %d (%d field, %d battle), %d parse to the last byte, %d FAIL" % (
        status["ok"] + status["FAIL"], status["fld"], status["btl"], status["ok"], status["FAIL"]))
    print("bytes                 : %d = %d sized + %d bounded + %d zero pad + %d stale + %d unclaimed" % (
        totals["bytes"], totals["sized"], totals["bounded"], totals["pad"], totals["stale"],
        totals["UNCLAIMED"]))
    print("placed records        : %d, programs %d, nodes %d, entries %d" % (
        totals["placed"], totals["programs"], totals["nodes"], totals["entries"]))
    if failures:
        print("FAIL: " + " ".join(failures))
        return 1
    return 0


def cmd_census(args, archive):
    C = collections.Counter
    op_uses, op_sets = C(), collections.defaultdict(set)
    st = collections.defaultdict(C)
    sets = 0
    for e, data in iter_sets(archive):
        key = mapfile.map_key(e.name)
        m = mapfile.Map1(data, e.name)
        ps = ParticleSet(data, m.section(10), m.section_limit(10), e.name)
        sets += 1
        st["format"][ps.format] += 1
        st["heap_blocks"][ps.heap_blocks] += 1
        st["work_class"][ps.work_class] += 1
        st["placed per set"][min(ps.n_placed // 10 * 10, 100)] += 1
        for rec in ps.placed:
            st["delay"]["0" if rec["delay"] == 0 else ("-4096" if rec["delay"] == -4096 else "other")] += 1
            st["group 0"][rec["group"] == 0] += 1
            st["cull_far < 0"][rec["cull_far"] < 0] += 1
            st["view"][rec["view"]] += 1
            st["rot_order"][rec["rot_order"]] += 1
            st["w72"][rec["w72"]] += 1
            st["tail"][rec["tail"]] += 1
        for prog in ps.programs:
            st["program loop byte"][prog["loop"]] += 1
            for nd in prog["nodes"]:
                f = nd["f"]
                st["node flags"][hex(f["flags"])] += 1
                st["node birth"]["never" if f["birth"] == TIME_NEVER else ("0" if f["birth"] == 0 else "later")] += 1
                st["node life"]["infinite" if f["life"] == TIME_INFINITE else "finite"] += 1
                for en in nd["entries"]:
                    op_uses[en["op"]] += 1
                    op_sets[en["op"]].add(key)
    print("sets %d" % sets)
    for k in sorted(st):
        print("%-18s %s" % (k, ", ".join("%s: %d" % kv for kv in sorted(st[k].items(), key=lambda kv: -kv[1])[:12])))
    print()
    print("operators used: %d of %d" % (len(op_uses), len(PPP_OPS)))
    for i in sorted(op_uses):
        maps = sorted(op_sets[i])
        print("  %3d %-26s entries %6d  sets %3d  %s" % (
            i, op_name(i), op_uses[i], len(maps), " ".join(maps) if len(maps) <= 10 else ""))
    print("never used: %s" % " ".join(op_name(i) for i in range(len(PPP_OPS)) if i not in op_uses))
    return 0


def describe_fields(fields, values):
    return " ".join("%s=%s" % (name, fmt_value(values[name])) for _, _, name, _ in fields)


def fmt_value(v):
    if isinstance(v, tuple):
        return "(" + ",".join(fmt_value(x) for x in v) + ")"
    if isinstance(v, float):
        return "%g" % v
    if isinstance(v, bytes):
        return v.hex()
    return str(v)


def cmd_show(args, archive):
    name, m = mapfile.load(archive, args.map)
    off = m.section(10)
    if off is None:
        print("%s has no section 10" % name)
        return 1
    ps = ParticleSet(m.data, off, m.section_limit(10), name)
    print("%s section 10 at +0x%X, %d bytes" % (name, off, ps.size))
    print("header: format %d heap_blocks %d work_class %d placed %d programs %d models %d shapes %d vlists %d" % (
        ps.format, ps.heap_blocks, ps.work_class, ps.n_placed, ps.n_programs, ps.n_models, ps.n_shapes,
        ps.n_vlists))
    for i, rec in enumerate(ps.placed):
        print("placed %3d %s" % (i, describe_fields(PLACED_FIELDS, rec)))
    want = set(args.programs)
    for pi, prog in enumerate(ps.programs):
        if want and pi not in want:
            continue
        print("program %d at +0x%X end %d steps loop %d ops %s" % (
            pi, prog["off"], prog["end"] // TIME_ONE, prog["loop"], " ".join(op_name(o) for o in prog["ops"])))
        for nd in prog["nodes"]:
            print("  node +0x%X %s" % (nd["off"], describe_fields(NODE_FIELDS, nd["f"])))
            for en in nd["entries"]:
                ks = list(ps.keys(prog, en))
                desc = [ps.u(prog["off"] + en["desc"] + 4 * j) for j in range(2)]
                print("    %-24s stride %3d keys %d times %s desc %s" % (
                    op_name(en["op"]), en["stride"], en["nkeys"],
                    ",".join(str(t // TIME_ONE) if t % TIME_ONE == 0 else hex(t) for t, _ in ks), desc))
                if en["op"] == OP_DRAW_RAIN:
                    for _, ko in ks:
                        print("      rain " + describe_fields(RAIN_FIELDS, unpack_fields(m.data, ko, RAIN_FIELDS)))
    return 0


def cmd_rain(args, archive):
    n = 0
    for e, data in iter_sets(archive):
        m = mapfile.Map1(data, e.name)
        ps = ParticleSet(data, m.section(10), m.section_limit(10), e.name)
        key = mapfile.map_key(e.name)
        for pi, prog in enumerate(ps.programs):
            for nd in prog["nodes"]:
                for en in nd["entries"]:
                    if en["op"] != OP_DRAW_RAIN:
                        continue
                    n += 1
                    users = ["%d(group %d, delay %d)" % (i, rec["group"], rec["delay"])
                             for i, rec in enumerate(ps.placed) if rec["program"] == pi]
                    f = nd["f"]
                    print("%-9s program %2d node +0x%X %s life %d loop %d..%d  placed %s" % (
                        key, pi, nd["off"], "HIDDEN (flags bit 0)" if f["flags"] & 1 else "drawn",
                        f["life"] // TIME_ONE, f["loop_to"] // TIME_ONE, f["loop_at"] // TIME_ONE,
                        " ".join(users) or "none"))
                    for _, ko in ps.keys(prog, en):
                        print("      " + describe_fields(RAIN_FIELDS, unpack_fields(data, ko, RAIN_FIELDS)))
    print("%d pppDrawRain entries" % n)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-a", "--archive", default=vbf.DEFAULT_VBF)
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("survey", help="parse every section 10 to the last byte")
    p.add_argument("-v", "--verbose", action="store_true", help="one line per set")
    p.set_defaults(fn=cmd_survey)

    p = sub.add_parser("census", help="operator, record, program and node statistics")
    p.set_defaults(fn=cmd_census)

    p = sub.add_parser("show", help="one set")
    p.add_argument("map")
    p.add_argument("programs", nargs="*", type=int)
    p.set_defaults(fn=cmd_show)

    p = sub.add_parser("rain", help="every pppDrawRain instance")
    p.set_defaults(fn=cmd_rain)

    args = ap.parse_args(argv)
    if not getattr(args, "fn", None):
        ap.print_help()
        return 1
    with vbf.VbfArchive(args.archive) as archive:
        return args.fn(args, archive)


if __name__ == "__main__":
    sys.exit(main())
