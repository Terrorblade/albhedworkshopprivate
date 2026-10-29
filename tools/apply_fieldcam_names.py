"""
Apply the names from the 2026-10-28 field camera port (Project Zanarkand src/engine/came/CameraStep and
src/engine/atel/CameSyscalls, FieldCamera and CrossingTrigger, PROGRESS 6.3.1) to the FFX.exe IDB.

FFX_Came_StepAll's setters and helpers, the came syscall row helpers they sit behind, and the two
geometry helpers the ATEL type 5 crossing trigger (core:84, 122, 139, 154) uses. Each [V] row was read in
the disassembly and ported bit exact against a line by line model (tests/engine/CameraStepTest.cpp).
Rows marked maybe_ were only inferred from their callers and are not ported.

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

    python apply_fieldcam_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x7C0010: ("FFX_Came_SetHandleSuspended", "sets or clears byte +4 of a camera handle entry, so FFX_Came_GetActiveHandle skips it (came:0 and came:1). [V]"),
    0x7BF700: ("FFX_Came_SetPolar", "mode 2 'pol': the eye at a polar offset from REF. Writes the sub-block mode byte +0xC2 = 2 (came:4). [V]"),
    0x7BF7F0: ("FFX_Came_AddPolar", "mode 7 'polo': adds a polar offset to the current one (came:5). [V]"),
    0x7BED10: ("FFX_Came_ClearClamps", "record +4 = 0, drops every wall and height clamp of the handle (came:15). [V]"),
    0x7BF1A0: ("FFX_Came_SetClampsPaused", "record +5 = on, FFX_Came_PublishPosToOtherSubBlock skips the clamps while set (came:14). [V]"),
    0x7BDDE0: ("FFX_Came_SetDamping", "the per sub-block damper that eases the committed position toward the pending one. [V]"),
    0x7BDF90: ("FFX_Came_SetMoveMask", "which channels a timed move drives. [V]"),
    0x7BEC90: ("FFX_Came_ClearSpeedLimit", "clears the speed limit record. No shipped script sets one (came:24 and 54). [V]"),
    0x7BDE50: ("FFX_Came_StartTimedMove", "linear and cosine moves over N frames, sets the tween records of the sub-block (came:16 and friends). [V]"),
    0x7BDD20: ("FFX_Came_GetMoveState", "reads back whether a move is running, used by the came poll rows 26 and 56. [V]"),
    0x7C0790: ("FFX_Came_SnapAfterSet", "after a mode setter: snaps the committed position to the new pending one unless a move is running. [V]"),
    0x7BFF10: ("FFX_Came_CaptureMoveStart", "copies the current position as the start point of a move. [V]"),
    0x7C0120: ("FFX_Came_ApplyClamps", "runs the wall (line) and height (point) clamps on the eye, from FFX_Came_PublishPosToOtherSubBlock. [V]"),
    0x7C4180: ("FFX_Came_LineSideTest", "which side of a wall clamp line the eye is on. [V]"),
    0x7BB6A0: ("FFX_AtelSys_Came_SetPosRow", "the shared body of the came pos rows (came:2), FFX_Came_SetPos with the hard coded level patches. [V]"),
    0x7BB5D0: ("FFX_AtelSys_Came_SetPolarRow", "the shared body of the came polar rows (came:4 and the target twins). [V]"),
    0x7BB410: ("FFX_AtelSys_Came_SetHypRow", "the shared body of the came hyp1, hyp2, hyp3 rows (came:6 to 8 and twins). [V]"),
    0x7BB2B0: ("FFX_AtelSys_Came_AddWallClampRow", "the shared body of came:10 and 11. [V]"),
    0x7BB370: ("FFX_AtelSys_Came_AddHeightClampRow", "the shared body of came:12 and 13. [V]"),
    0x7BA5B0: ("FFX_AtelSys_Came_TrapezoidMoveRow", "the shared body of the trapezoid move rows, into FFX_Came_StartTrapezoidMove. [V]"),
    0x7BB520: ("FFX_AtelSys_Came_SetDampingRow", "the shared body of the damping rows, into FFX_Came_SetDamping. [V]"),
    0x7BA9A0: ("FFX_Atel_BindActorCameraHandleChecked", "FFX_Atel_BindActorCameraHandle behind a validity check. [V]"),
    0x88BC70: ("FFX_Geom_Orient2D", "the 2D orientation determinant of three points (x0*y1 + x2*y0 + x1*y2 - x0*y2 - x2*y1 - x1*y0 in x87 order), used by the crossing trigger. [V]"),
    0x889BB0: ("FFX_Geom_SegmentsCrossXZ", "whether two segments cross in the XZ plane, from FFX_Atel_TestCrossingForTarget. [V]"),
    0x889E90: ("FFX_Geom_SegmentHeightGapAtCross", "the height gap between two crossing segments at the crossing point. Read at the call site only, not ported. [V]"),
    0x7BEE50: ("FFX_Came_ClearScaleAboutPivot", "orphan code with no xrefs, the only writer of the scale about pivot flag (S+3210) and it only writes 0, so FFX_Came_ScaleAboutPivot 0x7C4D40 never runs in retail. [V]"),
    0x7BCE80: ("maybe_FFX_Came_SetSpeedLimit", "the speed limit setter behind came:24 and 54, which no shipped script calls. Not ported. [I]"),
    0x7BA6D0: ("maybe_FFX_Came_CloseClassHandle", "closes the camera handle of a class, reached from debug code. [I]"),
    0x7BA860: ("maybe_FFX_Came_StopClassMoves", "stops the running moves of a class's camera. [I]"),
    0x7BA950: ("maybe_FFX_Came_CloseBattleViewerHandles", "closes the battle viewer camera handles. [I]"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7C47C0: ("FFX_Came_PolarToVec", "was maybe_FFX_Came_PolarToVec. Ported bit exact against the disassembly (CameraStepPolarToVecMatchesTheModel), so not a guess."),
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
