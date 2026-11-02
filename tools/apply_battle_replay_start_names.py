"""
Apply the names found while checking Project Zanarkand's battle start against six recorded battles
(PROGRESS 13.1, 13.4 and 13.5, 2026-10-31) to the FFX.exe IDB.

They are the small callees the port needed to match the recording at the first command prompt: the
command lock bit LoadUnitParams sets in an airborne fight, the ends of a CHR load and the unit idle
step's helpers. Each one was read in the disassembly, see the comments.

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

    python apply_battle_replay_start_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x79B470: ("FFX_Btl_SetCommandLocked", "(unit, id, on). A class 3 id sets or clears bit id & 0xF of word (id & 0xFFF) / 16 at unit +0x690, the bits FFX_Btl_IsCommandLocked 0x79A5B0 reads. A class 0 id names a unit instead and writes its +0xDD4. FFX_Btl_LoadUnitParams locks 0x3002 for every party unit on an airborne field or in script mode 2 (0x79B745), and Btl 059 SetAbilitySealed calls it"),
    0x788270: ("FFX_Btl_ReleaseHeldMotion", "a unit with +0x3F3 set (a held motion): +0xF5E &= ~2, the word +0x3F2 = 0x00FF (pending idle event 0xFF, held 0), +0xDF2 = 0, FFX_Btl_ClearUnitBusy. 1 when it released, 0 otherwise. Both CHR load ends call it"),
    0x783B90: ("FFX_Btl_OnPartyChrLoaded", "the end of a mode 1 CHR load (the party's first load, sub phase 11): maybe_FFX_Btl_SetupUnitChr and maybe_FFX_Btl_RefreshStatusVisuals. FFX_Btl_PumpChrLoadQueue 0x7AD190 dispatches it"),
    0x7AB750: ("FFX_Btl_IsIdlePlaceClear", "(unit, actor) for a unit away from its place. -1 (clear) when its +0xDF0 is set, its action is shown, the battle ended with no command queued or sub phase is 27, or no unit 0..27 with a CHR and flag100 (+0x2D) overlaps its place by radius. 0 when one does, and word_112CA08 gets that unit and 0xFC. FFX_BtlEv_StepUnitIdleEvents starts the idle event 0 on -1"),
    0x7A9230: ("FFX_Btl_IsUnitAwayFromPlace", "(unit, actor, flat): the distance from FFX_Btl_GetUnitPos to FFX_Btl_GetUnitPlacementPos (sub_7C3030, or sub_7C2EC0 with flat 0) is at least the float at actor +0x4F0. FFX_Btl_OnUnitChrReloaded stores it in +0x508"),
    0x78AC20: ("FFX_Btl_StepUnitMotionSpeed", "the motion speed of a unit's CHR: 8 by default, a linked unit's +0x406, 0 when petrified or when a status flag 0x800 unit with +0x5C6 0xFF stands home, 16 with +0x613, 4 with +0x614 for motions 16..42. Stored at +0x406, scaled by +0x3FC / 8 unless +0x404 is 1 (then +0x402), and set on the CHR with FFX_Ch_SetSpeedMulA when it changed. The head of FFX_BtlEv_StepUnitIdleEvents"),
    0x787650: ("FFX_Btl_IsActionMotionDone", "(unit, actor): 1 when +0x508 (away from home) and +0x436 (dying) are clear and the motion +0x3F0 is none of FFX_Btl_ResolveMotionRequest's requests 12, 13, 2, 3, 4 and 5 (the step in, step out, attack and return motions). FFX_Btl_StepMagicEffectStates waits on it in effect states 7, 8 and 15"),
    0x787D70: ("FFX_Btl_StartActionEvents", "(unit, actor), once an action's motion is over: FFX_Btl_RunAiAndCommit(unit, 255, 7, 0, 2, null), FFX_Btl_RestorePlaceFromAlt, the ability name, +0x3F2 = 0xFF, FFX_Btl_StartUnitEvent(unit, +0xE00, 1, 1, 1, 0, 0) and FFX_BtlEv_FireAbilityEvent(0, unit, +0xF5C, +0xE04, +0xE08, 1, 1, 0, 0). FFX_Btl_StepMagicEffectStates cases 7, 8 and 15"),
    0x79A170: ("FFX_Btl_ClearHitLogAttackers", "(actor): +0x776 and +0xA4E, the attacker byte of the unit's two hit logs (+0x774 and +0xA4C), to 0xFF"),
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
