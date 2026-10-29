#!/usr/bin/env python3
"""
tu_map.py - translation unit map of FFX.exe's ported PS2 game code, 0x760000..0x948000.

Two steps, like datamap.py. The dump reads a COPY of the IDB through idalib and writes one JSON
file. The report reads that JSON with plain Python, so the rules can be rerun and changed without
IDA.

  "<IDA>/venv/Scripts/python.exe" -I tu_map.py dump --idb path/to/COPY/FFX.exe.i64 --out tu.json
  python -I tu_map.py report --dump tu.json [--table out.md] [--tsv out.tsv]
                             [--doc ../reversing/TRANSLATION_UNITS.md]

--doc rewrites the table between the two tu_map markers in the doc and leaves the rest alone.

dump never saves the database. report runs under any Python 3, no packages needed.

The method is in reversing/TRANSLATION_UNITS.md. In short:

1. MSVC lays the object files out in link order, and every section follows the same order. So the
   .rdata strings, the initialized .data and the .bss each rise with code address one object at a
   time (the layout law of reversing/LUA_MODULE.md seen from the data side).
2. Inside one object the functions are alphabetical but the data is in source order, so the data
   addresses of neighbouring functions interleave. An interleave across a cut means the cut is not
   an object boundary. A clean cut between two groups of k1 and k2 data-owning functions happens
   inside one object with chance about 1/C(k1+k2, k1), so a clean cut with enough functions on
   both sides is a boundary.
3. Anchors name the units: functions that read an embedded source file name, the surviving
   <Module>__f<ADDR> names, and file ranges already proved in other docs (CURATED below).
4. A boundary that lands in a run of functions with no data is placed by call and shared-data
   affinity. Whatever the affinity cannot decide is listed as unattributable, with both
   neighbours named.

Tags: [V] read in the binary (a string xref, a doc range read in IDA), [I] inferred.
"""

import bisect
import collections
import json
import math
import re
import sys

REPORT_LO = 0x760000
REPORT_HI = 0x948000
CTX_LO = 0x740000      # analysis context on both sides of the report range
CTX_HI = 0x968400      # through the end of Lua (0x94A3D0..0x968390, LUA_MODULE.md), a blind test set

SECS = (("r", 0xB0C6C0, 0xC0A000), ("d", 0xC0A000, 0xC8B600), ("b", 0xC8B600, 0x25D7000))
TREND_TOL = {"r": 0x800, "d": 0x1000, "b": 0x20000}
CUT_WINDOW = 120       # functions on each side looked at when testing a cut
CUT_SLACK = 1          # this many out-of-order points are forgiven at a cut
AFF_CTX = 60           # functions of context used for affinity placement
DEBUG_CAND = False
CLEAN_MIN = 1          # a clean cut stands only with this many data-owning functions on the bigger side

# ---------------------------------------------------------------------------------------------
# File ranges proved in other docs. (start, end, name, single, tag, source). single True means
# the range is one object file and nothing inside it may split. single False means the doc only
# fixes the edges of a group of files.
# ---------------------------------------------------------------------------------------------

CURATED = [
    (0x9199F0, 0x91AF2F, "rcBg FFX side glue", False, "I", "RCBG.md 3"),
    (0x91AF30, 0x91C8AF, "ATEL MapFx library", False, "V", "RCBG.md 3, ATEL_MISC_LIBRARIES.md"),
    (0x91CAE0, 0x91F64D, "rcbgGuide.c", True, "V", "RCBG.md 3"),
    (0x91F650, 0x91FFC9, "rcbgPSim.c", True, "V", "RCBG.md 3, RCFLEX.md"),
    (0x91FFD0, 0x921200, "rcbgScene.c", True, "V", "RCBG.md 3"),
    (0x921220, 0x9215D2, "rcBase math and rcBBox.c", False, "I", "RCBG.md 3"),
    (0x9215E0, 0x9217AF, "rcbgPre.c", True, "V", "RCBG.md 3, start [I]"),
    (0x9217B0, 0x9220B0, "rcbgMain.c", True, "V", "RCBG.md 3"),
    (0x9220C0, 0x922282, "rcbgTrxTex.c", True, "V", "RCBG.md 3"),
    (0x922290, 0x923231, "rcbgFP.c", True, "V", "RCBG.md 3, RCDATA.md 2"),
    (0x923240, 0x923B5E, "rcdtTm2.c", True, "V", "RCDATA.md 2"),
    (0x923B60, 0x92476E, "rcdtRsd2.c", True, "V", "RCDATA.md 2"),
    (0x924770, 0x92594D, "Yanagi host API and rcBase memory", False, "I", "RCBG.md 3"),
    (0x925950, 0x927AFF, "rcBase prims and rcPacket.c", False, "I", "RCBG.md 3"),
    (0x927B00, 0x928573, "rcbgModel.c", True, "V", "RCBG.md 3"),
    (0x9285A0, 0x928A93, "rcfxParticle.c", True, "V", "RCFLEX.md 1"),
    (0x928AA0, 0x928DEA, "rcfxSpring.c", True, "V", "RCFLEX.md 1"),
    (0x928DF0, 0x92929B, "rcfxNail.c", True, "V", "RCFLEX.md 1"),
    (0x9292A0, 0x92943E, "rcfxDamp.c", True, "V", "RCFLEX.md 1"),
    (0x929440, 0x9295DE, "rcfxGravity.c", True, "V", "RCFLEX.md 1"),
    (0x9295E0, 0x92990B, "rcfxWind.c", True, "V", "RCFLEX.md 1"),
    (0x929910, 0x92B19F, "rcBg layer and clut support", False, "I", "RCBG.md 3"),
    (0x92B1A0, 0x92B7C6, "rcbgLoad.c", True, "V", "RCBG.md 3"),
    (0x92B7D0, 0x92B954, "rcdtChunk.c", True, "V", "RCDATA.md 2"),
    (0x92B960, 0x92C125, "rcdtParse.c", True, "V", "RCDATA.md 2"),
    (0x92C130, 0x92D22D, "rcbgDebug.c", True, "V", "RCBG.md 3, RCDATA.md 2"),
    (0x92D230, 0x92D681, "rcbgTrx.c", True, "V", "RCBG.md 3"),
    (0x92D690, 0x93080F, "Yanagi task engine", False, "I", "RCBG.md 3"),
    (0x930830, 0x930A08, "rcbgAnime.c", True, "V", "RCBG.md 3"),
    (0x930D80, 0x937F6F, "rcUI editor (rcuiPre.c first)", False, "I", "RCBG.md 3"),
    (0x938080, 0x9399E1, "rcbgSave.c", True, "V", "RCBG.md 3, start [I]"),
    (0x939B10, 0x93A26B, "rcbgSaveText.c", True, "V", "RCBG.md 3"),
    (0x93A280, 0x93C045, "rcbgLoadText.c", True, "V", "RCBG.md 3"),
]

PLACEHOLDER = re.compile(r"^(sub|nullsub|loc|j_sub|unknown_libname|j_nullsub)_[0-9A-Fa-f]+$")
MODNAME = re.compile(r"^(?:j_)*([A-Za-z0-9_]+?)__f([0-9A-Fa-f]{6})$")
FILE_IN_STRING = re.compile(r"([A-Za-z0-9_]+\.(?:c|cpp))(?![A-Za-z0-9_])")


# =============================================================================================
# dump (idalib)
# =============================================================================================

def dump(idb, out):
    import idapro
    if idapro.open_database(idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % idb)
    try:
        _dump(out)
    finally:
        idapro.close_database(save=False)


def _dump(out):
    import idautils
    import idc
    import ida_bytes
    import ida_funcs
    import ida_nalt

    def strlit(ea):
        if not ida_bytes.is_strlit(ida_bytes.get_flags(ea)):
            return None
        st = ida_nalt.get_str_type(ea)
        b = ida_bytes.get_strlit_contents(ea, -1, st if st is not None and st >= 0 else 0)
        return b.decode("latin-1") if b is not None else None

    funcs = []
    allmod = {}
    for fea in idautils.Functions():
        nm = idc.get_name(fea) or ""
        if MODNAME.match(nm):
            allmod[fea] = nm
        if not (CTX_LO <= fea < CTX_HI):
            continue
        f = ida_funcs.get_func(fea)
        drefs, calls, callers, dr = set(), set(), set(), []
        for h in idautils.FuncItems(fea):
            for d in idautils.DataRefsFrom(h):
                drefs.add(d)
            for c in idautils.CodeRefsFrom(h, 0):
                g = ida_funcs.get_func(c)
                if g and g.start_ea != fea:
                    calls.add(g.start_ea)
        for x in idautils.CodeRefsTo(fea, 0):
            g = ida_funcs.get_func(x)
            if g:
                callers.add(g.start_ea)
        for x in idautils.DataRefsTo(fea):
            if not ida_funcs.get_func(x):
                dr.append(x)
        funcs.append(dict(s=fea, e=f.end_ea, n=nm, d=sorted(drefs), c=sorted(calls),
                          r=sorted(callers), dr=sorted(dr)))
    items = {}
    want = set()
    for f in funcs:
        want.update(f["d"])
    for d in want:
        us = set()
        for x in idautils.DataRefsTo(d):
            g = ida_funcs.get_func(x)
            us.add(g.start_ea if g else -x)
        items[str(d)] = dict(u=sorted(us), n=idc.get_name(d) or "", s=strlit(d) is not None)
    srcs = []
    for s in idautils.Strings():
        t = str(s)
        if FILE_IN_STRING.search(t):
            fs = set()
            for x in idautils.DataRefsTo(s.ea):
                g = ida_funcs.get_func(x)
                if g:
                    fs.add(g.start_ea)
            srcs.append([s.ea, t, sorted(fs)])
    json.dump(dict(funcs=funcs, items=items, srcs=srcs,
                   modnames={str(k): v for k, v in allmod.items()}), open(out, "w"))
    print("dumped %d functions, %d items, %d file strings" % (len(funcs), len(items), len(srcs)))


# =============================================================================================
# report
# =============================================================================================

def _sec(d):
    for s, a, b in SECS:
        if a <= d < b:
            return s
    return None


def _lnds(vals):
    tails, tidx, prev = [], [], [-1] * len(vals)
    for i, v in enumerate(vals):
        j = bisect.bisect_right(tails, v)
        if j == len(tails):
            tails.append(v)
            tidx.append(i)
        else:
            tails[j] = v
            tidx[j] = i
        prev[i] = tidx[j - 1] if j > 0 else -1
    out = set()
    k = tidx[-1] if tidx else -1
    while k >= 0:
        out.add(k)
        k = prev[k]
    return out


def data_points(F, I, pos):
    """The (function, data address) pairs whose order is trusted. A .rdata item belongs to its
    first user, because a pooled literal is kept in the first object that has it. A .data or .bss
    item counts when it has one user, or a few users within 0x800 bytes of code."""
    pts = {"r": [], "d": [], "b": []}
    for f in F:
        if f["s"] not in pos:
            continue
        for d in f["d"]:
            s = _sec(d)
            it = I.get(d)
            if not s or not it or not it["n"]:
                continue
            u = [x for x in it["u"] if x > 0]
            if not u:
                continue
            if s == "r":
                if min(u) == f["s"]:
                    pts[s].append((f["s"], d))
            elif len(u) == 1 or (max(u) - min(u) < 0x800 and all(x in pos for x in u)):
                pts[s].append((f["s"], d))
    good = {}
    dropped = 0
    for s, pl in pts.items():
        pl.sort()
        keep = _lnds([p[1] for p in pl])
        bb = [pl[i] for i in sorted(keep)]
        bc = [p[0] for p in bb]
        g = []
        for c, a in pl:
            j = bisect.bisect_left(bc, c)
            k = bisect.bisect_right(bc, c)
            lo_a = bb[j - 1][1] if j > 0 else bb[0][1]
            hi_a = bb[k][1] if k < len(bb) else bb[-1][1]
            if j < k:
                lo_a = min(lo_a, min(x[1] for x in bb[j:k]))
                hi_a = max(hi_a, max(x[1] for x in bb[j:k]))
            if lo_a - TREND_TOL[s] <= a <= hi_a + TREND_TOL[s]:
                g.append((c, a))
            else:
                dropped += 1
        good[s] = g
    return good, dropped


def cut_scores(n, per):
    """score[k]: how many points must be dropped so that, in every section, everything owned
    before the cut after position k sits below everything owned after it."""
    score = [0] * (n - 1)
    for k in range(n - 1):
        tot = 0
        for s in ("r", "d", "b"):
            B = [a for i in range(max(0, k - CUT_WINDOW + 1), k + 1) for (ss, a) in per[i] if ss == s]
            if not B:
                continue
            A = [a for i in range(k + 1, min(n, k + 1 + CUT_WINDOW)) for (ss, a) in per[i] if ss == s]
            if not A:
                continue
            ev = sorted([(a, 0) for a in B] + [(a, 1) for a in A])
            babove, abelow = len(B), 0
            best = babove
            for a, lab in ev:
                if lab == 0:
                    babove -= 1
                else:
                    abelow += 1
                if babove + abelow < best:
                    best = babove + abelow
            tot += best
        score[k] = tot
    return score


def family(name):
    """the subsystem word of a behaviour name, None for a placeholder"""
    if not name or PLACEHOLDER.match(name):
        return None
    n = re.sub(r"^(j_)+", "", name)
    n = re.sub(r"^maybe_", "", n)
    m = MODNAME.match(n)
    if m:
        return m.group(1)
    t = n.split("_")
    if t[0] in ("FFX", "SG") and len(t) >= 2:
        if t[1] == "AtelSys" and len(t) >= 3:
            return "FFX_AtelSys_" + t[2]
        return t[0] + "_" + t[1]
    m = re.match(r"^[a-z]+", n)
    if m:
        return m.group(0)
    return t[0]


def dominant(names):
    c = collections.Counter(x for x in names if x)
    if not c:
        return None, 0
    fam, k = c.most_common(1)[0]
    if k >= 2 and k * 2 >= sum(c.values()):
        return fam, k
    return None, 0


def build_edges(F, st, pos, I):
    byS = {f["s"]: f for f in F}
    E = collections.defaultdict(list)

    def add(i, j, w):
        if i != j:
            E[i].append((j, w))
            E[j].append((i, w))
    for s in st:
        f = byS[s]
        for g in f["c"]:
            if g in pos:
                gf = byS[g]
                w = 2.0 if (len(gf["r"]) == 1 and not gf["dr"]) else 1.0
                add(pos[s], pos[g], w)
    seen = set()
    for s in st:
        for d in byS[s]["d"]:
            if d in seen:
                continue
            seen.add(d)
            it = I.get(d)
            if not it:
                continue
            u = [x for x in it["u"] if x > 0]
            if 2 <= len(u) <= 6 and all(x in pos for x in u):
                ps = sorted(pos[x] for x in u)
                for a, b in zip(ps, ps[1:]):
                    add(a, b, 1.0)
    slots = sorted((x, pos[s]) for s in st for x in byS[s]["dr"] if 0xB0C6C0 <= x < 0x25D7000)
    for (x1, p1), (x2, p2) in zip(slots, slots[1:]):
        if x2 - x1 <= 8:
            add(p1, p2, 0.5)
    return E


def place(E, gap, left_ctx, right_ctx):
    """gap: positions of the undecided run. Returns (lo, hi): a split after any p in lo..hi
    (counted in gap positions, 0 means nothing goes left) costs the same and is the cheapest."""
    L, R = set(left_ctx), set(right_ctx)
    gs = set(gap)
    lab = {}
    aff = []
    for p in gap:
        l = sum(w for q, w in E.get(p, []) if q in L)
        r = sum(w for q, w in E.get(p, []) if q in R)
        aff.append([l, r])
    # one round of propagation through edges inside the gap
    for _ in range(2):
        new = []
        for idx, p in enumerate(gap):
            l, r = aff[idx]
            for q, w in E.get(p, []):
                if q in gs and q != p:
                    j = gap.index(q)
                    ql, qr = aff[j]
                    if ql > qr:
                        l += 0.5 * w
                    elif qr > ql:
                        r += 0.5 * w
            new.append([l, r])
        aff = new
    m = len(gap)
    costs = []
    for p in range(m + 1):
        costs.append(sum(a[1] for a in aff[:p]) + sum(a[0] for a in aff[p:]))
    best = min(costs)
    ps = [p for p in range(m + 1) if costs[p] == best]
    return ps[0], ps[-1]


DOC_START = "<!-- tu_map table start, regenerate with tools/tu_map.py report --doc -->"
DOC_END = "<!-- tu_map table end -->"


def report(dumpfile, table=None, tsv=None, curated=True, lo=None, hi=None, doc=None):
    global CURATED, REPORT_LO, REPORT_HI
    if lo is not None:
        REPORT_LO, REPORT_HI = lo, hi
    if not curated:
        CURATED = []
    D = json.load(open(dumpfile))
    F = sorted(D["funcs"], key=lambda f: f["s"])
    I = {int(k): v for k, v in D["items"].items()}
    st = [f["s"] for f in F]
    byS = {f["s"]: f for f in F}
    pos = {s: i for i, s in enumerate(st)}
    n = len(st)

    # ---- data order -------------------------------------------------------------------------
    pts, dropped = data_points(F, I, pos)
    per = [[] for _ in range(n)]
    for s, pl in pts.items():
        for c, a in pl:
            per[pos[c]].append((s, a))
    score = cut_scores(n, per)
    keyed = [bool(per[i]) for i in range(n)]

    # forced join groups: positions tied by an interleave
    grp = [0] * n
    g = 0
    for k in range(n - 1):
        if score[k] > CUT_SLACK:
            grp[k + 1] = g
        else:
            g += 1
            grp[k + 1] = g
    kgroups = collections.OrderedDict()
    for i in range(n):
        if keyed[i]:
            kgroups.setdefault(grp[i], []).append(i)
    KB = []  # (first, last, keyed count)
    for gid, members in kgroups.items():
        allm = [i for i in range(members[0], members[-1] + 1)]
        KB.append((allm[0], allm[-1], len(members)))

    # ---- anchors ------------------------------------------------------------------------------
    # A path string (__FILE__ of an assert or a log) names the file of the function that reads
    # it. A file name inside a Virtuos comment, or a <Module>__f name with no path string behind
    # it, is only a note: it ties neighbours like an anchor but does not name the unit.
    anchor = {}      # pos -> (name, evidence, strong)
    for ea, text, fs in D["srcs"]:
        m = FILE_IN_STRING.findall(text)
        if not m:
            continue
        fname = m[-1]
        strong = "\\" in text
        for f in fs:
            if f in pos:
                shown = text[:48].replace("\n", "\\n")
                anchor[pos[f]] = (fname, "string 0x%X \"%s\"" % (ea, shown), strong)
    files = set(a[0] for a in anchor.values() if a[2])
    for k, v in D["modnames"].items():
        ea = int(k)
        if ea in pos:
            mod = MODNAME.match(v).group(1)
            strong = (mod + ".c") in files
            fname = mod + ".c" if strong else mod
            if pos[ea] not in anchor:
                anchor[pos[ea]] = (fname, "name %s" % v, strong)

    cur = {}         # pos -> curated row index
    for ci, (a, b, nm, single, tag, src) in enumerate(CURATED):
        for i in range(bisect.bisect_left(st, a), bisect.bisect_right(st, b)):
            cur[i] = ci

    # ---- boundaries -------------------------------------------------------------------------
    # A boundary is (left_last, right_first, kind, ambiguous run). Gap boundaries between keyed
    # groups first.
    E = build_edges(F, st, pos, I)
    fam = [family(byS[s]["n"]) for s in st]

    def same_single(i, j):
        ci, cj = cur.get(i), cur.get(j)
        return ci is not None and ci == cj and CURATED[ci][3]

    def cross(l0, l1, r0, r1):
        w = 0.0
        for p in range(l0, l1 + 1):
            for q, ww in E.get(p, []):
                if r0 <= q <= r1:
                    w += ww
        return w

    def nearest_anchor(lo, hi, rev=False):
        rng = range(hi, lo - 1, -1) if rev else range(lo, hi + 1)
        for p in rng:
            if p in anchor:
                return anchor[p][0]
        return None

    cand = []  # one per gap between keyed groups
    for x in range(len(KB) - 1):
        a0, a1, k1 = KB[x]
        b0, b1, k2 = KB[x + 1]
        c = math.comb(k1 + k2, k1)
        lo_ctx = KB[x - 1][1] + 1 if x > 0 else 0
        hi_ctx = KB[x + 2][0] - 1 if x + 2 < len(KB) else n - 1
        lf, _ = dominant(fam[lo_ctx:a1 + 1])
        rf, _ = dominant(fam[b0:hi_ctx + 1])
        if k1 >= 2 and k2 >= 2 and c >= 10:
            kind = "data"
        elif k1 >= 2 and k2 >= 2 and c >= 6:
            kind = "data-weak"
        else:
            # a clean cut with little behind it. It stands unless something ties the sides.
            la = nearest_anchor(lo_ctx, a1, rev=True)
            ra = nearest_anchor(b0, hi_ctx)
            tie = None
            if la and ra and la == ra:
                tie = "anchor"
            else:
                w = cross(lo_ctx, a1, b0, hi_ctx)
                if w >= 0.5:
                    tie = "affinity %.1f" % w
            if not tie and max(k1, k2) < CLEAN_MIN:
                tie = "too little data"
            kind = None if tie else "clean"
        cand.append(dict(L=a1, R=b0, k1=k1, k2=k2, comb=c, kind=kind, famchg=bool(lf and rf and lf != rf),
                         lf=lf, rf=rf, tie=tie if kind is None else None))

    # the long data-less runs: a change of behaviour-name family with three names on each side
    extra = []
    for cd in cand:
        gap = list(range(cd["L"] + 1, cd["R"]))
        if len(gap) < 12:
            continue
        named = [(p, fam[p]) for p in gap if fam[p]]
        for t in range(3, len(named) - 2):
            left = [f for _, f in named[t - 3:t]]
            right = [f for _, f in named[t:t + 3]]
            if len(set(left)) == 1 and len(set(right)) == 1 and left[0] != right[0]:
                if all(f != left[0] for _, f in named[t:]) and all(f != right[0] for _, f in named[:t]):
                    extra.append(dict(L=named[t - 1][0], R=named[t][0], kind="name", lf=left[0], rf=right[0],
                                      k1=0, k2=0, comb=0, famchg=True))

    if DEBUG_CAND:
        for cd in cand:
            print("CAND %X %X %s %d %d %s" % (st[cd["L"]], st[cd["R"]], cd["kind"], cd["k1"], cd["k2"], cd.get("tie")))
    bounds = [dict(cd) for cd in cand if cd["kind"]]
    bounds += extra

    # anchors: same file on both sides means no boundary between them, different files means
    # at least one
    apos = sorted(anchor)
    conflicts = []
    keepb = []
    for b in bounds:
        lA = [p for p in apos if p <= b["L"]]
        rA = [p for p in apos if p >= b["R"]]
        if lA and rA and anchor[lA[-1]][0] == anchor[rA[0]][0]:
            conflicts.append((b, anchor[lA[-1]][0]))
            continue
        if same_single(b["L"], b["R"]):
            continue
        keepb.append(b)
    bounds = keepb
    for p, q in zip(apos, apos[1:]):
        if anchor[p][0] != anchor[q][0] and not any(p <= b["L"] and b["R"] <= q for b in bounds):
            inside = [cd for cd in cand if p <= cd["L"] and cd["R"] <= q]
            if inside:
                best = max(inside, key=lambda cd: (cd["famchg"], cd["comb"]))
                bounds.append(dict(best, kind="anchor"))
            else:
                bounds.append(dict(L=p, R=q, kind="anchor", k1=0, k2=0, comb=0, famchg=False, lf=None, rf=None))
    # curated edges
    for a, b, nm, single, tag, src in CURATED:
        i = bisect.bisect_left(st, a)
        j = bisect.bisect_right(st, b) - 1
        if 0 < i < n:
            bounds.append(dict(L=i - 1, R=i, kind="doc", k1=0, k2=0, comb=0, famchg=False, lf=None, rf=None))
        if 0 <= j < n - 1:
            bounds.append(dict(L=j, R=j + 1, kind="doc", k1=0, k2=0, comb=0, famchg=False, lf=None, rf=None))
    # drop boundaries inside single curated files, and dedupe
    bounds = [b for b in bounds if b["kind"] == "doc" or not any(
        CURATED[ci][3] and bisect.bisect_left(st, CURATED[ci][0]) <= b["L"] and b["R"] <= bisect.bisect_right(st, CURATED[ci][1]) - 1
        for ci in set(cur.get(x) for x in range(b["L"], b["R"] + 1)) if ci is not None)]
    rank = {"doc": 0, "anchor": 1, "data": 2, "data-weak": 3, "clean": 4, "name": 5}
    bounds.sort(key=lambda b: (b["L"], b["R"], rank[b["kind"]]))
    ded = []
    for b in bounds:
        if ded and b["L"] < ded[-1]["R"] and b["R"] > ded[-1]["L"]:
            # overlapping gaps: keep the tighter, better ranked one
            if (rank[b["kind"]], b["R"] - b["L"]) < (rank[ded[-1]["kind"]], ded[-1]["R"] - ded[-1]["L"]):
                ded[-1] = b
            continue
        if ded and b["L"] == ded[-1]["L"] and b["R"] == ded[-1]["R"]:
            continue
        ded.append(b)
    bounds = ded

    # ---- placement inside each boundary's gap -----------------------------------------------
    cuts = []   # (first position of the right unit, ambiguous run (lo, hi) or None, boundary)
    for x, b in enumerate(bounds):
        gap = list(range(b["L"] + 1, b["R"]))
        if not gap:
            cuts.append((b["R"], None, b))
            continue
        lctx = list(range(max(0, b["L"] - AFF_CTX), b["L"] + 1))
        rctx = list(range(b["R"], min(n, b["R"] + AFF_CTX)))
        plo, phi = place(E, gap, lctx, rctx)
        amb = (gap[plo], gap[phi - 1]) if phi > plo else None
        cuts.append((gap[phi] if phi < len(gap) else b["R"], amb, b))

    # ---- units ----------------------------------------------------------------------------------
    rows = []
    starts = [0]
    ambs = {}
    for first, amb, b in cuts:
        if amb:
            ambs[amb[0]] = (amb, b)
            starts.append(amb[0])
            starts.append(amb[1] + 1)
        starts.append(first)
    starts = sorted(set(s for s in starts if 0 <= s < n))
    segs = [(a, b - 1) for a, b in zip(starts, starts[1:] + [n])]
    bound_at = {}
    for first, amb, b in cuts:
        bound_at[first] = b
        if amb:
            bound_at[amb[0]] = b
    for a, z in segs:
        if a in ambs and ambs[a][0][1] == z:
            b = ambs[a][1]
            rows.append(dict(a=a, z=z, kind="amb", b=b))
            continue
        rows.append(dict(a=a, z=z, kind="unit", b=bound_at.get(a)))

    # name the units
    for r in rows:
        r["anchors"] = sorted(set(anchor[p][0] for p in range(r["a"], r["z"] + 1) if p in anchor and anchor[p][2]))
        r["notes"] = sorted(set(anchor[p][0] for p in range(r["a"], r["z"] + 1) if p in anchor and not anchor[p][2]))
        r["aev"] = [anchor[p][1] for p in range(r["a"], r["z"] + 1) if p in anchor and anchor[p][2]]
        r["nev"] = ["%s at 0x%X" % (anchor[p][0], st[p]) for p in range(r["a"], r["z"] + 1) if p in anchor and not anchor[p][2]]
        cis = [cur.get(p) for p in range(r["a"], r["z"] + 1)]
        cis = [c for c in cis if c is not None]
        r["cur"] = collections.Counter(cis).most_common(1)[0][0] if cis else None
        r["keyed"] = sum(1 for p in range(r["a"], r["z"] + 1) if keyed[p])
        r["fam"], _ = dominant(fam[r["a"]:r["z"] + 1])
    for i, r in enumerate(rows):
        nxt = rows[i + 1]["b"] if i + 1 < len(rows) else None
        r["rb"] = nxt
        if r["kind"] == "amb":
            continue
        if r["cur"] is not None and all(cur.get(p) == r["cur"] for p in range(r["a"], r["z"] + 1)):
            a, b, nm, single, tag, src = CURATED[r["cur"]]
            r["name"] = nm if single else "part of " + nm
            r["tag"] = tag if single else "I"
            r["ev"] = src
            r["kind"] = "file" if single else "unit"
            r["core"] = (r["a"], r["z"])
        elif len(r["anchors"]) == 1:
            # the proved core: first to last anchor (contiguity), widened to the interleave
            # groups that hold them. Outside it the edge is only as good as the boundary.
            ap = [p for p in range(r["a"], r["z"] + 1) if p in anchor and anchor[p][2]]
            lo_c, hi_c = min(ap), max(ap)
            for p in ap:
                q = p
                while q - 1 >= r["a"] and grp[q - 1] == grp[p]:
                    q -= 1
                lo_c = min(lo_c, q)
                q = p
                while q + 1 <= r["z"] and grp[q + 1] == grp[p]:
                    q += 1
                hi_c = max(hi_c, q)
            r["core"] = (lo_c, hi_c)
            r["name"] = r["anchors"][0]
            r["tag"] = "V"
            r["ev"] = ", ".join(sorted(set(r["aev"])))[:160] + ", core 0x%X..0x%X" % (st[lo_c], byS[st[hi_c]]["e"] - 1)
            r["kind"] = "file"
        elif len(r["anchors"]) > 1:
            r["name"] = " + ".join(r["anchors"])
            r["tag"] = "I"
            r["ev"] = "two anchors and no boundary found between them"
            r["kind"] = "multi"
        else:
            r["name"] = "unit_%06X" % st[r["a"]]
            r["tag"] = "I"
            r["ev"] = ("%d data-owning functions" % r["keyed"]) + ((", names " + r["fam"]) if r["fam"] else "")
            if r["nev"]:
                r["ev"] += ", note " + ", ".join(sorted(set(r["nev"])))[:120]
            if r["keyed"] == 0 and not r["fam"] and not r["nev"]:
                r["kind"] = "none"

    # ---- output -------------------------------------------------------------------------------
    def end_of(p):
        return byS[st[p]]["e"]

    def bdesc(b):
        if not b:
            return "-"
        k = b["kind"]
        if k in ("data", "data-weak"):
            return "%s %d/%d C=%d" % (k, b["k1"], b["k2"], b["comb"])
        if k == "name":
            return "name %s/%s" % (b["lf"], b["rf"])
        if k == "clean":
            return "clean %d/%d" % (b["k1"], b["k2"])
        return k

    out_rows = []
    cover = collections.Counter()
    for r in rows:
        a = max(r["a"], bisect.bisect_left(st, REPORT_LO))
        z = min(r["z"], bisect.bisect_left(st, REPORT_HI) - 1)
        if a > z:
            continue
        cnt = z - a + 1
        if r["kind"] == "amb":
            b = r["b"]
            name = "unattributable"
            ev = "between the units either side, boundary %s, affinity cannot place these" % bdesc(b)
            tag = "I"
            cover["amb"] += cnt
        elif r["kind"] == "none":
            name = "unattributable"
            ev = "no data owned, no anchor, no behaviour names"
            tag = "I"
            cover["none"] += cnt
        else:
            name, ev, tag = r["name"], r["ev"], r["tag"]
            cover[r["kind"]] += cnt
        core = r.get("core")
        core = (st[max(a, core[0])], end_of(min(z, core[1])) - 1) if core else (0, 0)
        out_rows.append(dict(start=st[a], end=end_of(z) - 1, count=cnt, name=name, tag=tag, ev=ev,
                             lb=bdesc(r["b"]), rb=bdesc(r["rb"]), kind=r["kind"], core=core))

    total = sum(o["count"] for o in out_rows)
    print("functions in 0x%X..0x%X: %d" % (REPORT_LO, REPORT_HI, total))
    print("data points kept: r %d, d %d, b %d, dropped as off trend %d" % (
        len(pts["r"]), len(pts["d"]), len(pts["b"]), dropped))
    print("keyed functions %d, keyed groups %d" % (sum(1 for i in range(n) if keyed[i] and REPORT_LO <= st[i] < REPORT_HI), len(KB)))
    kinds = collections.Counter(b["kind"] for b in bounds)
    print("boundaries: %s" % dict(kinds))
    print("rows %d: %s" % (len(out_rows), dict(collections.Counter(o["kind"] for o in out_rows))))
    print("coverage: %s" % dict(cover))
    if conflicts:
        print("data or name boundaries refused by a same-file anchor on both sides: %d" % len(conflicts))
        for b, nm in conflicts:
            print("  %s between 0x%X and 0x%X inside %s" % (bdesc(b), st[b["L"]], st[b["R"]], nm))
    L = ["| start | end | fns | translation unit | tag | left edge | evidence |", "|---|---|---|---|---|---|---|"]
    for o in out_rows:
        L.append("| 0x%X | 0x%X | %d | %s | %s | %s | %s |" % (
            o["start"], o["end"], o["count"], o["name"], o["tag"], o["lb"], o["ev"].replace("|", "/")))
    head =["%d functions in 0x%X..0x%X. In a named file %d (%.1f%%), in an unnamed unit %d (%.1f%%), "
            "unattributable %d (%.1f%%). %d rows." % (
                total, REPORT_LO, REPORT_HI, cover["file"], 100.0 * cover["file"] / total,
                cover["unit"], 100.0 * cover["unit"] / total, cover["amb"] + cover["none"],
                100.0 * (cover["amb"] + cover["none"]) / total, len(out_rows)), ""]
    if table:
        open(table, "w", newline="\n").write("\n".join(head + L) + "\n")
    if doc:
        text = open(doc).read()
        a = text.index(DOC_START) + len(DOC_START)
        b = text.index(DOC_END)
        text = text[:a] + "\n\n" + "\n".join(head + L) + "\n\n" + text[b:]
        open(doc, "w", newline="\n").write(text)
    if tsv:
        with open(tsv, "w", newline="\n") as fp:
            for o in out_rows:
                fp.write("%X\t%X\t%d\t%s\t%s\t%s\t%X\t%X\n" % (o["start"], o["end"], o["count"], o["name"], o["tag"],
                                                             o["kind"], o["core"][0], o["core"][1]))
    return out_rows


def main():
    import argparse
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    a = sp.add_parser("dump")
    a.add_argument("--idb", required=True, help="a COPY of the database")
    a.add_argument("--out", required=True)
    b = sp.add_parser("report")
    b.add_argument("--dump", required=True)
    b.add_argument("--table", help="write the markdown table here")
    b.add_argument("--tsv", help="write start, end, count, name, tag, kind, core start, core end per row")
    b.add_argument("--no-curated", action="store_true", help="ignore the CURATED doc ranges, to test the rules blind")
    b.add_argument("--range", nargs=2, type=lambda x: int(x, 16), metavar=("LO", "HI"),
                   help="report another window, for example 94A3D0 968390 for the Lua blind test")
    b.add_argument("--doc", help="rewrite the table between the tu_map markers in this markdown file")
    args = ap.parse_args()
    if args.cmd == "dump":
        dump(args.idb, args.out)
    else:
        lo, hi = args.range if args.range else (None, None)
        report(args.dump, args.table, args.tsv, not args.no_curated, lo, hi, args.doc)


if __name__ == "__main__":
    main()
