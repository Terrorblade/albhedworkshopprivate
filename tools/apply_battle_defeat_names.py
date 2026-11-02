"""
Apply the battle defeat sequence names to the FFX.exe IDB (2026-11-01).

These come from porting the defeat sequence into Project Zanarkand (src/ffx/btl/Defeat.cpp, PROGRESS 13.13
and 13.7): FFX_Btl_StartDefeatSequence 0x78DC30 and everything it calls, the queued defeat dispatch
FFX_Btl_MainStep runs after it, the defeat's finish that btl:57 also reaches, the death effect start and the
alpha fade btl:54 and btl:148 share. Each function was read in the decompile and the disassembly while
porting it. See reversing/BATTLE_START_AND_END.md (the correction under the end phase table),
reversing/BATTLE_CTB_AND_STATUS.md (FFX_Btl_OnUnitDefeated), reversing/ATEL_BTL_LIBRARY.md (btl:57) and
reversing/EFFECT_OVERLAY.md (FFX_Btl_EffectAdvanceState's handle kinds).

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

    python apply_battle_defeat_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # FFX_Btl_StartDefeatSequence's calls, in the order it makes them
    0x82A080: ("FFX_Ch_ModelNumberFromRomPath", "(chr id): FFX_Ch_FindRomEntry, then the rom dev path through g_ffxRomDevGetPath, -1 when there is none, else atoi of the three characters 7, 6 and 5 from the end of the path (the NNN of mNNN.chr). FFX_Btl_StartDefeatSequence 0x78DC62 hands the unit's +0x0E to it and the result to FFX_Ach_CheckBossKill"),
    0x78D680: ("FFX_Btl_SnapUnitToBoneIfAway", "(unit, actor): 0 without a CHR, with +0x43F not 0xFF, +0x426 set or +0x4AC bit 0x8000. Else +0x50D is forced to 1 for a FFX_Btl_IsUnitAwayFromPlace(unit, actor, (signed)+0x34) call and put back, and a unit away from its place is moved to FFX_Ch_GetBoneWorldPos(chr, 6) through FFX_Btl_SetUnitPos, 1. Called by FFX_Btl_StartDefeatSequence 0x78DC87, FFX_Btl_PlayUnitReaction and FFX_Btl_StartCommandExec 0x7AEC99"),
    0x78DDC0: ("FFX_Btl_MarkUnitDown", "(unit, actor), only from FFX_Btl_StartDefeatSequence 0x78DC8E: +0xDCC = 2 (0x78DDDB), +0xF70 = the unit's y from FFX_Btl_GetUnitPos (0x78DDEC), FFX_BtlEv_FireEvent(0, unit, 63, 0xFFFF, 0, 1, 0, 0) (0x78DE04), the word +0x50E = 0, FFX_Btl_SetFlag100Vote(actor, 3, 0), +0x41C = 0"),
    0x834F60: ("FFX_Ch_SetLookAtEnabled", "(chr, on): +0x40C = on. On: +0x414 = 1.0 and, when +0x40E differs from +0x40D, FFX_Quat_FromMatrix(+0x46C, +0x4AC) and +0x4BC = 1.0. Off: +0x414 = 0.0. Then +0x40E = +0x40D. No null check, FFX_Btl_StartDefeatSequence 0x78DC9E calls it with 0 on the victim's CHR"),
    0x79E3D0: ("FFX_Btl_StartDeathEffect", "(unit, kind): 0 for unit 0xFF. +0x701 == 3 makes the kind 1. Kind 2 effect 41 sound 76, 3 is 42 and 28, 4 is 43 and 27, 5 is 44 and 73, 7 is 57 and 88 with handle kind 15 and +0xE17 = 0, 8 is 44 with no sound, any other kind returns 0. The sound through FFX_Btl_PlayUnitSound, then FFX_Magic_PackEffectHandle(+0xE1E, 4 or 15, unit) with +0xE1E counted up, +0x3F7 = 1 and sub_7FC2E0(g_ffxBtlEffectSet, 0, effect, handle). 1. The effect's end reaches FFX_Btl_EffectAdvanceState cases 4 and 15, which clear +0x436 and +0x3F7"),
    0x7A2780: ("FFX_Btl_QueueDefeat", "(unit): 1 when the unit is not already among the byte 1 of dword_112CA0A entries of g_ffxBtlDefeatQueue, 0 when its g_ffxBtlDefeatDispatchedMask bit is set. A new unit is appended and the count raised while it is below 31. FFX_Btl_StartDefeatSequence queues a unit that left the battle petrified, ahead of its event 58 (0x78DCE7)"),
    0x78CCA0: ("FFX_Btl_FinishDefeatSequence", "(unit, actor): +0x436 = 0 unless +0x3F7 (a death effect started, its end clears it instead), +0x3F4 = 1 (the defeat is over), maybe_FFX_Btl_RefreshStatusVisuals, FFX_Btl_EndUnitChrEffects. From FFX_Btl_StartDefeatSequence (release kinds 1 and 6) and btl:57"),
    0x788E60: ("FFX_Btl_EndUnitChrEffects", "(unit): with a CHR, +0x1F = 0 (0x788E7A), FFX_Ch_SetI408(chr, 0), FFX_Ch_EndChrEffects. From FFX_Btl_FinishDefeatSequence, FFX_Btl_TakeUnitOffStage and btl:94"),
    0x7A0100: ("FFX_Btl_PlayUnitSound", "(unit, sound): 0 for sound 0 or when dword_112C8E4 already holds sound | 0x80000000. Else maybe_FFX_Btl_AddSeFollow(3, unit, sound | 0x80000000, 0, 0), and that value is kept in dword_112C8E4 and returned"),
    # btl:57's second call, and the fade btl:54 and btl:148 share
    0x7A8F20: ("FFX_Btl_ClearOnceList", "(): zeroes the ten dwords dword_11363C0..dword_11363E4, the once list reversing/ATEL_BTL_LIBRARY.md ties to btl:123. btl:57 calls it after FFX_Btl_FinishDefeatSequence"),
    0x796550: ("FFX_Btl_FadeUnitAlpha", "(unit, from, to, mode, gate): 0 without a CHR or with gate bit 0 clear. Else the float +0x40 = to, +0x423 = 0, FFX_Btl_SetUnitShowVote1(unit, 1), FFX_Ch_SetShadeAlpha(chr, from, 0) then (chr, to, mode), the same on both side records, and for a unit with +0x440 set every unit whose +0x43F is this one. 1. btl:54 passes gate 1, btl:148 passes +0x423, FFX_Btl_OnUnitChrLoaded 0x783ABB fades a unit in (0.0 to 1.0, mode 8, +0x423)"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7A2690: ("FFX_Btl_DispatchQueuedDefeats", "was maybe_FFX_Btl_DispatchQueuedDefeats. Confirmed by the port: 0 with no queued defeat or while a command runs (byte 0 of dword_112CA0A). While some are queued and fewer than 3 run (byte 2): the head of g_ffxBtlDefeatQueue comes off, its g_ffxBtlDefeatDispatchedMask bit is set (0x7A26D0) and FFX_Btl_StartDeathEffect(unit, 7) (0x7A26FD) counts it running when it started. 1. FFX_Btl_MainStep after FFX_Btl_StartDefeatSequences"),
    0x7A2B10: ("FFX_AtelSys_Btl_057_FinishDefeatSequence_resi", "was FFX_AtelSys_Btl_057_ResetActionState_resi. The row is the defeat sequence's finish: FFX_Btl_FinishDefeatSequence on the self unit (not 0xFF), which clears +0x436 when no death effect started and sets +0x3F4, then FFX_Btl_ClearOnceList. 0, no arguments"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x112C950: ("g_ffxBtlDefeatQueue", "31 dwords, the units FFX_Btl_QueueDefeat appends, byte 1 of dword_112CA0A of them. FFX_Btl_DispatchQueuedDefeats takes the head and shifts the rest down"),
    0x112A95C: ("g_ffxBtlEffectSet", "the battle effect set the Oef2 starts read: sub_79E7A0 0x79E7AA copies dword_11360A8 into it after op_et_battle_effect_init (FFX_Btl_BeginBattle 0x7814DA), sub_79E630 zeroes it (FFX_Btl_MainStep 0x790E2F). FFX_Btl_StartDeathEffect, FFX_Btl_StartCriticalFlash and the status effect visuals hand it to sub_7FC2E0"),
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
