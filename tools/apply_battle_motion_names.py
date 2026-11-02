"""
Apply the battle motion names to the FFX.exe IDB (2026-11-01).

These come from porting FFX's battle unit motion into Project Zanarkand (src/ffx/btl/UnitMotion.cpp):
the motion table and blend frames, the per-step move and turn of a battle unit (FFX_Btl_StepUnitMove
0x7AA2A0 and its helpers), the away-from-home bookkeeping, and the VU0 vector helpers they call.
Each row was read in the decompile and the disassembly while porting it. See
reversing/BATTLE_START_AND_END.md (the unit CHR) and reversing/BATTLE_ACTOR.md (the move fields).

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

    python apply_battle_motion_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The motion table and the blend
    0x79CB40: ("FFX_Btl_MotionBlendFrames", "(unit, motion, previous): the blend frames into a battle motion from the unit's mot.bin table (+0x4F8, in g_ffxBtlMotBin). A binary search of the table's (id, list offset) pairs for the new motion, then each record of its list: frames, a from list (0xFFFF matches any) and an except list read on from where the from scan stopped. 0 when nothing matches. FFX_Btl_PlayScriptMotion uses it for the hokan frames"),
    0x79CD80: ("FFX_Btl_SetUnitMotionTable", "(unit, index): unit +0x4F8 = g_ffxBtlMotBin + its u16 offset at 2 + 2 * index when the bin is loaded and index < its u16 count, else 0"),
    # The move and the turn
    0x7AAB80: ("FFX_Btl_StepUnitMoves", "from FFX_Btl_MainStep 0x790D8D, every battle step: for each unit 0..30 that exists (+0x10) or plays on another's CHR (+0x12), a linked CHR (+0x43F) goes onto its parent's bone +0x441 and FFX_Btl_StepUnitMove runs. Then a countdown at +0x439 that on 0 calls sub_834F60 or sub_835050 toward unit +0x438. Before FFX_Btl_UpdateAwayFromHomeAll"),
    0x7AAC40: ("FFX_Btl_StepMoveTimeline", "the timed move's progress curve: from the move's frames, accel and decel counts, the distance covered by this step on an ease in, cruise, ease out profile. Computed in double and rounded to float at each store (x87)"),
    0x7A9710: ("FFX_Btl_ResolveTurnAngle", "the facing the turn kind (+0x42B) aims at: 1 the home facing, 2 and 9 toward a unit (+0x431, 9 adds pi), others toward the target point, the move target or a given angle (7 uses the acting actor's target point). Returns the angle FFX_Btl_StepUnitMove tweens to, unwrapped by FFX_Btl_UnwrapTurnAngle"),
    0x7A9650: ("FFX_Btl_UnwrapTurnAngle", "brings a turn's target angle within pi of the current facing (adds or takes 2 pi), and with the flip bound 2.0943952 (2 pi / 3) picks the short way"),
    0x7A9150: ("FFX_Btl_StartMoveFromScript", "(context, actor, stack, gravity), Btl 12's body: pops three floats (the height factor, the +0x48C value, the stop offset), two ints, the frames, a speed float and the kind, skips unit 255 or frames <= 0, then FFX_Btl_StartUnitMove(unit, kind, unit +0x416, speed, frames, the two ints, the three floats in push order, gravity). Btl 12 passes gravity 0"),
    0x7AA050: ("FFX_Btl_GetUnitTargetPoint", "the point a unit moves or turns toward, the mean of FFX_Btl_QueryCameraTargetMean over its target mask (FFX_Btl_TargetPointMask narrows a multi-target mask to the flagged units)"),
    0x796EA0: ("FFX_Btl_ReadUnitPos", "(unit, out): a linked unit (+0x43F != 0xFF) with a parent CHR reads that CHR's bone +0x441 in world space, else its own CHR position, else FFX_Btl_ReadUnitPosNoChr. w = 1"),
    0x795AC0: ("FFX_Btl_ReadUnitPosNoChr", "(unit, out): no link (+0x43F == 0xFF) gives FFX_Btl_GetUnitPlacementPos, else the parent CHR's bone +0x441, nothing when the parent has no CHR. Does not set w"),
    0x7A93E0: ("FFX_Btl_PredictUnitPos", "where a unit will be dword_1136424 frames ahead: its saved position when +0x425 is set, else the timed move run forward on a copy of the move state (the distance and progress saved and restored). Camera value kind 12"),
    0x787900: ("FFX_Btl_TargetPointMask", "(mask): with more than one unit in the mask, only the units whose +0xDDA is set, else the mask as it is"),
    0x796D70: ("FFX_Btl_TargetPointSelfTest", "(unit, actor, mask): 0 when the mask is empty or just the unit, or holds the unit with +0x3F8 set and more than two units, else -1"),
    0x7AA280: ("FFX_Btl_FixUnitMotionSpeed", "unit +0x404 = 1, +0x402 = 0x100: FFX_Btl_StepUnitMotionSpeed then keeps the CHR at normal speed"),
    0x7AA260: ("FFX_Btl_ClearUnitMotionSpeedFix", "unit +0x404 = 0, +0x402 = 0x100"),
    0x7AA140: ("FFX_Btl_UpdateAwayFromHomeAll", "from FFX_Btl_MainStep 0x790D92: maybe_FFX_Btl_UpdateAwayFromHome for each unit 0..30"),
    0x7AAAE0: ("FFX_Btl_AwayFromHomeGate", "whether a unit's away-from-home flag is kept live: no link (+0x43F == 0xFF), chrReady +0x38, +0x43E and +0x426 clear, and either (not petrified, +0xDCC != 2, and one of +0xDC8, +0xDCB, +0x3F9) or (+0xDE6 or +0xDE7, with another turn or command pending)"),
    0x794EC0: ("FFX_Btl_GetUnitAltPlacementPos", "the placement point of the unit's group + 3, the mode 6 reload's position in FFX_Btl_OnUnitChrReloaded"),
    0x796F70: ("FFX_Btl_ApplyUnitChrModes", "(chr, unit): the unit's modes onto its CHR. Offset 0, update while hidden +0x37, locomotion and b185 +0x20, flags1 0x80 inverted from +0x32, ground mode +0x34, flags1 bit 1 from +0x33, flags1 0x800 off, clip Z 5000, then FFX_Btl_SetFlag100Vote(unit, 2, 1) and FFX_Btl_SetChrB183Request(unit, 2, -1). The tail of FFX_Btl_SetupUnitChr"),
    # The VU0 vector helpers
    0x7C2EC0: ("FFX_Vu0_Distance3", "|a - b| over x, y, z"),
    0x7C3030: ("FFX_Vu0_DistanceXZ", "|a - b| over x and z"),
    0x7C4280: ("FFX_Vu0_Length3", "sqrt(z*z + (y*y + x*x)), each product and sum rounded to float"),
    0x7C43A0: ("FFX_Vu0_LengthXZ", "sqrt(z*z + x*x), rounded to float like FFX_Vu0_Length3"),
    0x7C40B0: ("FFX_Vu0_Sub", "out = a - b on all four lanes"),
    0x7C44A0: ("FFX_Vu0_Normalize", "xyz times float(1 / sqrt(len2)), 0 for a zero vector, w kept"),
    0x7C2E10: ("FFX_Vu0_Scale", "out = v * s on all four lanes"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7AA100: ("FFX_Btl_UpdateAwayFromHome", "was maybe_FFX_Btl_UpdateAwayFromHome. Confirmed by the port: +0x508 = FFX_Btl_AwayFromHomeGate and FFX_Btl_IsUnitAwayFromPlace(unit, +0x34)"),
    0x793670: ("FFX_Btl_SetupUnitChr", "was maybe_FFX_Btl_SetupUnitChr. Confirmed by the port: allocates the unit's CHR when it has none, then the y offset, the placement, the radius, scale, speed, the motion table and the CHR modes. FFX_Btl_OnUnitChrLoaded and FFX_Btl_OnUnitChrReloaded (every mode but 2) call it"),
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
