"""
Apply the names from the 2026-10-28 player control and field trigger port (Project Zanarkand
src/engine/input/PlayerControl, src/engine/atel/FieldTriggers, PlayerControlSyscalls and the
CrossingTrigger additions, PROGRESS 10.10 and 11.11) to the FFX.exe IDB.

The segment helper the line and crossing triggers use, the VU0 macro mode helpers it is built from, and
the sg:48 row with the player module setter behind it. Each [V] row was read in the disassembly and
ported bit exact (tests/engine/PlayerControlTest.cpp, FieldTriggerTest.cpp, PlayerControlFieldTest.cpp).

The VU0 helpers are named after the PS2 macro op they stand in for, dest field first and broadcast
field second, so AddXY is dst.x = src.x + v.y. The vector operand is passed by value.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all. A name starting maybe_ is a guess
and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_player_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
COMMENTS = False
VERBOSE = False

# ---------------------------------------------------------------------------------------------
# Functions and code labels whose current name is a placeholder
# ---------------------------------------------------------------------------------------------

NAMES = {
    0x88A040: ("FFX_Geom_SegmentClosestParam", "the parameter of the closest point on segment a-b to point p, through the VU0 helpers and the scratch registers at 0xC8F7D0. Used by the line and crossing triggers. [V]"),
    0x74B670: ("FFX_Vu0_SubVec3", "dst.xyz = src.xyz - v.xyz, v by value, tails into FFX_Vu0_ResetVf0. [V]"),
    0x728A60: ("FFX_Vu0_MulVec3", "dst.xyz = src.xyz * v.xyz per component, v by value. [V]"),
    0x74B630: ("FFX_Vu0_AddXY", "dst.x = src.x + v.y, v by value. [V]"),
    0x74B650: ("FFX_Vu0_AddXZ", "dst.x = src.x + v.z, v by value. [V]"),
    0x7676C0: ("FFX_Vu0_MulXX", "dst.x = src.x * v.x, v by value. [V]"),
    0x767720: ("FFX_Vu0_SubXX", "dst.x = src.x - v.x, v by value, tails into FFX_Vu0_ResetVf0. [V]"),
    0x889B40: ("FFX_Vu0_MinXW", "dst.x = v.w <= src.x ? v.w : src.x, v by value. [V]"),
    0x889B00: ("FFX_Vu0_MaxXX", "dst.x = v.x >= src.x ? v.x : src.x, v by value. [V]"),
    0x710E30: ("FFX_Vu0_LoadVec4", "16 byte copy from arg1 into arg0. FFX_Geom_SegmentClosestParam uses it to load its arguments into the scratch registers. [V] copy, [I] the role"),
    0x710E70: ("FFX_Vu0_StoreVec4", "16 byte copy from arg0 into arg1, the reverse of FFX_Vu0_LoadVec4. [V] copy, [I] the role"),
    0xA77950: ("FFX_AtelSys_Sg_048_SetPlayerCtrlModeBit2_resi", "pops an int and calls FFX_Player__setCtrlModeBit2, returns 0. [V]"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x82DAB0: ("FFX_Player__setCtrlModeBit2", "was maybe_FFX_Player__setMode2. ctrlMode = (ctrlMode & ~2) | (on ? 2 : 0), the 0x02 bit of maybe_g_ffxPlayerCtrlMode. [V]"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
}

PLACEHOLDER = re.compile(
    r"^(sub|nullsub|loc|j_sub|unk|dword|word|byte|qword|off|flt|dbl|stru|funcs)_[0-9A-Fa-f]+$"
    r"|^[A-Za-z]+__f[0-9A-Fa-f]{6}$"
    r"|^FFX_AtelSys_[A-Za-z]+_\d+_(start|poll|resf|resi)$")


def _cur_name(ea):
    import idc
    return idc.get_name(ea) or ""


def _plan(table, kind):
    import idaapi
    import ida_name
    plan, skips, kept = [], [], []
    for ea, (new, why) in sorted(table.items()):
        cur = _cur_name(ea)
        if cur == new:
            kept.append("0x%X %s" % (ea, new))
            continue
        m = re.match(r"^was ([^ ]+)\.", why)
        if kind == "RENAMES" or (kind == "GLOBALS" and m):
            if not m:
                skips.append("0x%X %s: comment does not start 'was OLD.'" % (ea, new))
                continue
            if cur != m.group(1):
                skips.append("0x%X is %s, expected %s" % (ea, cur or "(no name)", m.group(1)))
                continue
        else:
            if cur and not PLACEHOLDER.match(cur):
                skips.append("0x%X is %s, not a placeholder" % (ea, cur))
                continue
        other = ida_name.get_name_ea(idaapi.BADADDR, new)
        if other != idaapi.BADADDR and other != ea:
            skips.append("0x%X name %s already used at 0x%X" % (ea, new, other))
            continue
        plan.append((ea, cur, new, why))
    return plan, skips, kept


def _add_comment(ea, text):
    import idc
    import ida_funcs
    if not text:
        return False
    f = ida_funcs.get_func(ea)
    if f and f.start_ea == ea:
        old = idc.get_func_cmt(ea, 1) or ""
        if text in old or len(old) + len(text) + 1 > 1000:
            return False
        return bool(idc.set_func_cmt(ea, (old + "\n" + text) if old else text, 1))
    old = idc.get_cmt(ea, 1) or ""
    if text in old or len(old) + len(text) + 1 > 1000:
        return False
    return bool(idc.set_cmt(ea, (old + " " + text) if old else text, 1))


def main(dry_run=DRY_RUN, comments=COMMENTS, verbose=VERBOSE):
    import idc
    import ida_name
    total = 0
    failed = 0
    for kind, table in (("NAMES", NAMES), ("RENAMES", RENAMES), ("GLOBALS", GLOBALS)):
        plan, skips, kept = _plan(table, kind)
        for line in kept:
            if verbose:
                print("ok    %s %s" % (kind, line))
        for line in skips:
            print("skip  %s %s" % (kind, line))
        for ea, cur, new, why in plan:
            print("name  %s 0x%X %s -> %s%s" % (kind, ea, cur or "(no name)", new, ("    " + why) if verbose else ""))
        print("%s: %d to rename, %d skipped, %d already named, %d rows" % (kind, len(plan), len(skips), len(kept), len(table)))
        total += len(plan)
        if dry_run:
            continue
        for ea, cur, new, why in plan:
            if not idc.set_name(ea, new, ida_name.SN_NOWARN | ida_name.SN_NOCHECK):
                print("FAILED 0x%X %s -> %s" % (ea, cur, new))
                failed += 1
            elif comments:
                _add_comment(ea, why)
    if dry_run:
        print("DRY_RUN, nothing renamed (%d would be)" % total)
        return True
    print("renamed %d, failed %d. The database is NOT saved, save it when you are happy" % (total - failed, failed))
    return failed == 0


def headless():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True, help="a COPY of the database")
    ap.add_argument("--apply", action="store_true", help="rehearse the renames, the database is not saved")
    ap.add_argument("--comments", action="store_true", help="also append each row's comment")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    import idapro
    if idapro.open_database(a.idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % a.idb)
    try:
        main(dry_run=not a.apply, comments=a.comments, verbose=a.verbose)
    finally:
        idapro.close_database(save=False)


if __name__ == "__main__":
    if "idaapi" in sys.modules or "ida_kernwin" in sys.modules:
        main()
    else:
        headless()
