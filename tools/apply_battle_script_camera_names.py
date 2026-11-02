"""
Apply the battle script camera names to the FFX.exe IDB (2026-11-01).

These come from porting the battle's own script camera into Project Zanarkand (BattleSession::UseCamera,
src/engine/atel/CameSyscalls.cpp and src/engine/came/CameraStep.cpp): the came:3 and came:33 row helper and
the committed position read it ends in, the came:101 priority lock FFX_Btl_BeginBattle sets, the teardown's
battle class move stop, and the magic 146 count and latch came:22 and came:128 share. Each row was read in
the decompile and the disassembly while porting it. See reversing/ATEL_CAME_LIBRARY.md, sections 4.1, 4.2a
and 5.4.

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

    python apply_battle_script_camera_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # came:3 and came:33
    0x7BA3C0: ("FFX_AtelSys_Came_GetPosToVarsRow", "(context, actor, stack, sub): the body of came:3 (0x7B9260, sub 1, the eye) and came:33 (0x7B9D40, sub 0, the look-at). Pops three variable references z, y, x, each through FFX_Atel_ResolveVarAddress, then takes the caller's handle (actor +0x3C, else FFX_Came_GetHandleForClass(5, system actor == the actor's id, 0) cached there) and calls FFX_Came_GetCommittedPos(handle & 0xFF, sub & 1, x, y, z). The battle and field scripts use it to snapshot the camera before a move"),
    0x7BD420: ("FFX_Came_GetCommittedPos", "(slot, sub, x*, y*, z*): the committed x, y and z of the sub-block (+0x20 of REF or CAM in g_ffxCameSlots, slot 872 dwords apart, sub 391 apart) written as floats through the three pointers, a null pointer skipped. The handle's priority is not read, so an inactive handle reads the active view"),
    # The priority lock
    0x7BE100: ("FFX_Came_SetPriorityLock", "(value): g_ffxCamePriorityLock = value. FFX_Btl_BeginBattle passes the sign extended g_ffxBtlDbgCameraPrio (0x781162), which is 0 unless the battle debug config set it. While the lock is not 0 came:101 (SetCameraPriority) does nothing"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7BA860: ("FFX_Came_StopClassMoves", "was maybe_FFX_Came_StopClassMoves. Confirmed by the port: for kind 0 to 3 (event, map, battle, viewer) FFX_Came_StopMove(h, 0) then (h, 1) on each of the kind's three cached handles, nothing for any other kind. The end of FFX_Btl_MainStep calls it with 2 at 0x790E84, only when the sub phase is 0x21 or the end result 5 or 6 (the teardown)"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x2311424: ("g_ffxCamePriorityLock", "came:101's lock, read by sub_7BE0A0 (0x7BE0A3), written by FFX_Came_SetPriorityLock (0x7BE106) and cleared by FFX_Came_Init (0x7BD601). Past the initialized data, 0 at boot"),
    0xC494C8: ("g_ffxCameStopMoveCount", "came:22's count under magic 146, initialized -1. MagicFile__load sets 0 for magic 146 (0x9DA486) and -1 for any other (0x9DA49B). came:22 counts only while it is 0 or more, and the call that finds 1 sets g_ffxCameStopMoveLatch and puts it back to -1"),
    0x12FB7E0: ("g_ffxCameStopMoveLatch", "was maybe_g_ffxCameStopMoveLatch. Confirmed by the port: set to 1 by came:22 (0x7B9BB6) on its second call under magic 146, read and cleared by came:128 (0x7B89C3, 0x7B89D0), which then returns 2 or 0 from the stream 14 or 15 draw instead of the draw, and cleared by btl:30 (0x7A6910) in battles 0x2350000, 0x2360000 and 0x2380000 when its unit count is 0. Past the initialized data, 0 at boot"),
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
