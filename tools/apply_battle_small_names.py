"""
Apply the small battle names to the FFX.exe IDB (2026-11-01).

These come from four small items of the battle port in Project Zanarkand (PROGRESS 13.1, 13.8 and 10.x):
the battle map's part mask (src/engine/phyre/Scene.h MapPartMaskForPass, the white ring and letter of the
battle map's debug circle), and Wakka's ball throw (src/ffx/btl/UnitMotion.cpp, the arc move of Btl 118,
Btl 119 and the stand in CHR, and src/ffx/btl/BattleEvents.cpp StartUnitAltEvent for Btl 110). Each function
was read in the decompile and the disassembly while porting it. See reversing/BTLMAP_FORMAT.md,
reversing/MAGIC_HOST_API.md, reversing/BATTLE_ACTOR.md and reversing/ATEL_BTL_LIBRARY.md (the 2026-11-01
lines).

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

    python apply_battle_small_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The map part mask
    0x66D610: ("FFX_Gfx_ApplyMapPartMask", "(gfx, mask), thiscall on g_ffxGfxCtx: nothing while the instance list at gfx +4504 is empty or mask is gfx +64276, the mask last applied. Else for each mesh instance whose bit 1 << m_groupID (PMeshInstance +72) differs between the two, the back pointer at +28 of its PMeshInstanceBounds (instance +28) is set to the instance when the new mask has the bit and cleared when not, which shows or hides it. Then gfx +64276 = mask. Group 3 of a battle map is its debug circle, the ring, the axes and the letter (one instance in bsil03_a and in bsil07_a), which the scene pass hides by clearing bit 3. Only from FFX_Map_SetGfxPartMask"),
    0x6423D0: ("FFX_Map_SetGfxPartMask", "(mask): FFX_Gfx_ApplyMapPartMask(g_ffxGfxCtx, mask). From maybe_FFX_Map_UpdateScenePass 0x90BF4F with g_ffxMapPartMask after the pass sets bit 8 for g_ffxLiveSceneId 138 (0x90BF38) and clears bit 3 (0x90BF42), and with 0 at 0x90C102"),
    0x88EBA0: ("FFX_Map_SetPartMaskBit", "(bit, on): FFX_Map_SetPartMask(FFX_Map_GetPartMask() | 1 << bit) when on, & ~(1 << bit) when not. Only from FFX_AtelSys_MapFx_000_SetMapPartMaskBit_resi 0x91C569. The bit only shows on the next scene pass, which clears bit 3 again"),
    # Wakka's ball and the arc move
    0x7A9A40: ("FFX_Btl_GetDistanceToMoveTarget", "(unit, kind, flat): the length of FFX_Btl_GetUnitPos minus the target FFX_Btl_ResolveMoveTargetPos gives for the kind (FFX_Vu0_Sub then FFX_Vu0_Length3, y zeroed when flat), less that call's stop distance, rounded to float. From Btl 46 (0x7A6D9B), Btl 89 (0x7A6E6B) and Btl 119 (0x7A5496), which turns it into fl(d / 20 + 4) frames, at most 20"),
    0x7B7C80: ("FFX_Btl_LatchArcStart", "(unit): FFX_Btl_GetUnitPos into +0x390..+0x398 (the start of the arc) and FFX_Btl_QueryCameraTargetMean(13, +0xDC0, 16, dword_1136AC4), the mean FFX_Btl_BuildArcPath offsets the landing point by. Only from Btl 118 0x7A572B, before it writes the frames, builds the path and calls FFX_Btl_StartPathMove(2)"),
    0x7A0720: ("FFX_BtlEv_StartUnitAltEvent", "(unit, event, flags, caller, out): nothing for a negative event (out not written). Else the requests (2, 1, unit + 79, event), (2, 1, 77 + FFX_Btl_IsEnemySlot(unit), event), (2, 0, unit + 79, event), (2, 0, 77 + IsEnemySlot, event) and FFX_BtlEv_StartFirstMatch(2, 0, unit + 79, flags, caller, out). Writes nothing on the unit, unlike FFX_Btl_StartUnitEvent. True when an entry started. Only from Btl 110 with flags 1. In system_01, slot 77 kinds 9 and 10 run worker 4 entries 3 and 2 (Core 169 and came:128 picks that end in Btl 109) and slot 83 has no kinds"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x18DED0C: ("g_ffxMapPartMask", "g_ffxMapWork +0x67F0C, the bit per m_groupID of the map's mesh instances that is shown. FFX_Map_OnMapOutLoaded and FFX_Map_LoadMapOutSync set it to -1 when g_ffxBtlDbgMapCircle is set, else -9 (group 3 hidden). maybe_FFX_Map_UpdateScenePass sets bit 8 for scene 138 and clears bit 3 in place every pass, then hands it to FFX_Map_SetGfxPartMask, so the debug circle stays hidden. FFX_Map_GetPartMask and FFX_Map_SetPartMask read and write it"),
    0x1136418: ("g_ffxBtlPathPointCounts", "int[3], the points of each move path since FFX_Btl_ClearPath. FFX_Btl_AddPathPoint stops at 32, FFX_Btl_StartPathMove starts nothing on a path with none. FFX_Btl_BuildArcPath adds 11 to path 2 for moveArcMode 0..3 and none for anything else"),
    0x1136428: ("g_ffxBtlPaths", "3 paths of 0x210 bytes: the count FFX_Btl_StartPathMove copies at +0, then the 16 byte points from +0x10 that FFX_Btl_AddPathPoint writes and FFX_Btl_StepUnitMove reads for move kind 7 (FFX_Math_SplineQuadratic_A5One). Also read by FFX_Btl_PredictUnitPos 0x7A94C5"),
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
