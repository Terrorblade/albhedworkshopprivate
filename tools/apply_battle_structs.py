"""
Apply the battle record structs from reversing/ffx_types.h to the FFX.exe IDB.

The structs are BTLCHR (0x390), BTLHIT (0x2C), BTLHITLOG (0x2D8) and BTLUNIT (0xF90), see
reversing/BATTLE_ACTOR.md. The script parses them out of ffx_types.h, checks every size against the
pinned value, compares them with whatever the IDB already has under the same names, and only then
replaces the local types with PT_REPLACE and copies the header comments onto the members.

It refuses to apply when an existing type in the IDB has a member name that is not a placeholder
(m_b/m_w/m_i/m_f + hex, _unk_, field_, gap, var_) and that name is missing from the new layout, or
when an existing member comment would be lost. Fix the header, or rename in the IDB, then run again.

DRY_RUN is True by default. A dry run parses into a scratch til, prints the sizes, the diff and the
member list, and changes nothing. The script never saves the database, save it yourself afterwards.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_battle_structs.py --idb path/to/copy/FFX.exe.i64 [--apply] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import os
import re
import sys

DRY_RUN = True
VERBOSE = False

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "reversing", "ffx_types.h")

# order matters, a struct must come after the structs it embeds
STRUCTS = ["BTLCHR", "BTLHIT", "BTLHITLOG", "BTLUNIT"]
EXPECTED = {"BTLCHR": 0x390, "BTLHIT": 0x2C, "BTLHITLOG": 0x2D8, "BTLUNIT": 0xF90}
# types the structs point at but do not define. Declared forward only when the IDB lacks them,
# a forward declaration under PT_REPLACE would otherwise wipe the real type
EXTERNAL = ["CHR"]

PLACEHOLDER = re.compile(r"^(m_[bwif][0-9A-Fa-f]+|_unk_[0-9A-Fa-f]+|field_[0-9A-Fa-f]+|gap[0-9A-Fa-f]+|var_[0-9A-Fa-f]+)$")
CMT_MAX = 1000  # IDA caps a comment at 1024, keep clear of it


def read_header():
    with open(HEADER, "r", encoding="ascii") as f:
        return f.read()


def struct_block(text, name):
    m = re.search(r"^struct %s \{\n.*?^\};" % re.escape(name), text, re.S | re.M)
    if not m:
        raise SystemExit("struct %s not found in %s" % (name, HEADER))
    return m.group(0)


def strip_comments(block):
    block = re.sub(r"/\*.*?\*/", "", block, flags=re.S)
    return "\n".join(l.split("//", 1)[0].rstrip() for l in block.split("\n"))


def member_comments(block):
    """offset -> comment text, from the /* 0xNNN */ prefix or the _unk_<hex> name of each line."""
    out = {}
    cur = None
    for line in block.split("\n")[1:-1]:
        code, _, cmt = line.partition("//")
        cmt = cmt.strip()
        m = re.match(r"\s*/\*\s*0x([0-9A-Fa-f]+)\s*\*/", code)
        g = re.search(r"\b_unk_([0-9A-Fa-f]+)\s*\[", code)
        if m or g:
            cur = int((m or g).group(1), 16)
            if cmt:
                out[cur] = cmt
        elif not code.strip() and cmt and cur is not None:
            out[cur] = (out.get(cur, "") + " " + cmt).strip()
    return out


def members(tif):
    import ida_typeinf
    udt = ida_typeinf.udt_type_data_t()
    if not tif.get_udt_details(udt):
        return []
    return [(m.offset // 8, m.name, m.type.dstr(), m.cmt or "") for m in udt]


def norm(s):
    return re.sub(r"\s+", " ", s or "").strip()


def main(dry_run=DRY_RUN, verbose=VERBOSE):
    import ida_typeinf
    import idc

    text = read_header()
    blocks = {n: struct_block(text, n) for n in STRUCTS}
    decl_body = "\n\n".join(strip_comments(blocks[n]) for n in STRUCTS)
    comments = {n: member_comments(blocks[n]) for n in STRUCTS}

    idati = ida_typeinf.get_idati()
    # tinfo_t lookup, the low level get_named_type(idati, n, 0) returns None for a type that exists,
    # which would send a forward declaration through PT_REPLACE and wipe the real CHR
    missing_ext = [n for n in EXTERNAL if not ida_typeinf.tinfo_t().get_named_type(idati, n)]
    print("external types missing from the IDB, declared forward:", missing_ext or "none")

    # 1. parse into a scratch til and check the sizes
    scratch = ida_typeinf.new_til("albhed_btl_scratch", "battle struct dry run")
    fwd = "".join("struct %s;\n" % n for n in EXTERNAL + STRUCTS)
    errs = ida_typeinf.parse_decls(scratch, fwd + decl_body, None, ida_typeinf.HTI_DCL)
    if errs:
        raise SystemExit("parse failed with %d error(s)" % errs)
    new_members = {}
    bad = False
    for n in STRUCTS:
        t = ida_typeinf.tinfo_t()
        if not t.get_named_type(scratch, n):
            raise SystemExit("%s missing after parse" % n)
        size = t.get_size()
        ok = size == EXPECTED[n]
        bad |= not ok
        new_members[n] = members(t)
        print("%-10s size 0x%X expected 0x%X %s, %d members, %d comments"
              % (n, size, EXPECTED[n], "ok" if ok else "MISMATCH", len(new_members[n]), len(comments[n])))
    if bad:
        raise SystemExit("size mismatch, nothing applied")

    # 2. diff against the IDB
    problems = []
    for n in STRUCTS:
        old = ida_typeinf.tinfo_t()
        if not old.get_named_type(idati, n):
            print("%-10s not in the IDB yet, it will be created" % n)
            continue
        old_m = members(old)
        new_names = {name: off for off, name, _, _ in new_members[n]}
        new_cmt = comments[n]
        print("%-10s in the IDB, size 0x%X, %d members" % (n, old.get_size(), len(old_m)))
        for off, name, ty, cmt in old_m:
            if not PLACEHOLDER.match(name):
                if name not in new_names:
                    problems.append("%s: member %s at 0x%X (%s) would be dropped" % (n, name, off, ty))
                elif new_names[name] != off:
                    problems.append("%s: member %s moves from 0x%X to 0x%X" % (n, name, off, new_names[name]))
            if cmt and norm(cmt) not in norm(new_cmt.get(off, "")):
                problems.append("%s: comment on 0x%X %s would be lost: %r" % (n, off, name, cmt[:120]))
    if problems:
        print("REFUSING, %d problem(s):" % len(problems))
        for p in problems:
            print("  " + p)
        return False
    print("diff clean, no named member or member comment would be lost")

    if verbose:
        for n in STRUCTS:
            print("-- %s" % n)
            for off, name, ty, _ in new_members[n]:
                print("  0x%03X %-28s %s" % (off, name, ty))

    if dry_run:
        print("DRY_RUN, nothing applied")
        return True

    # 3. apply
    fwd_ext = "".join("struct %s;\n" % n for n in missing_ext)
    errs = idc.parse_decls(fwd_ext + decl_body, idc.PT_REPLACE)
    if errs:
        raise SystemExit("apply parse failed with %d error(s), check the local types" % errs)
    for n in STRUCTS:
        t = ida_typeinf.tinfo_t()
        if not t.get_named_type(idati, n) or t.get_size() != EXPECTED[n]:
            raise SystemExit("%s did not land at 0x%X" % (n, EXPECTED[n]))
        udt = ida_typeinf.udt_type_data_t()
        t.get_udt_details(udt)
        set_count = 0
        for m in udt:
            c = comments[n].get(m.offset // 8)
            if c:
                m.cmt = c[:CMT_MAX]
                set_count += 1
        t2 = ida_typeinf.tinfo_t()
        t2.create_udt(udt, ida_typeinf.BTF_STRUCT)
        if t2.set_named_type(idati, n, ida_typeinf.NTF_REPLACE) != ida_typeinf.TERR_OK:
            raise SystemExit("could not store the member comments of %s" % n)
        print("%-10s applied, size 0x%X, %d member comments" % (n, t2.get_size(), set_count))
    print("applied. The database is NOT saved, save it when you are happy")
    return True


def headless():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True, help="a COPY of the database")
    ap.add_argument("--apply", action="store_true", help="rehearse the apply, the database is not saved")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    import idapro
    if idapro.open_database(a.idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % a.idb)
    try:
        main(dry_run=not a.apply, verbose=a.verbose)
    finally:
        idapro.close_database(save=False)


if __name__ == "__main__":
    if "idaapi" in sys.modules or "ida_kernwin" in sys.modules:
        main()
    else:
        headless()
