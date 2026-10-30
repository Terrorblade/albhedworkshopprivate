"""
Apply the names the live battle port (Project Zanarkand PROGRESS 13.1, 13.12 and 13.13, 2026-10-29) read
from the disassembly to the FFX.exe IDB.

These are the battle end's helpers (FFX_Btl_EndPhaseStep 0x7917D0 and its globals), the unit command list
rebuild (FFX_Btl_RebuildUnitCommands 0x79BB50), the script motion rows (Btl 7, 8 and 163) and the battle
motion event handler FFX_Btl_OnMotionEvent 0x7B74D0 with what it calls. Each comment says what the code
does, not what it is guessed to be.

Note for anyone reading Btl 158 (0x7A2FE0) in the decompiler: it shows FFX_Btl_OnMotionEvent with two
arguments, but the call pushes three (the first pop, the second pop as the event, the unit). The handler
reads only the first two (arg_0 and arg_4), so the first pop is dead.

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

    python apply_battle_live_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The battle end
    0x7982A0: ("FFX_BtlEv_RunFieldCut", "(2, 1, 108) then either (2, 1, 137) and (2, 0, 137) started from 0, or (2, 1, 137) alone: the field cut events of the battle field context. FFX_Btl_EndPhaseStep and the battle start ready step call it"),
    0x793290: ("FFX_Btl_FaceReturnAll", "FFX_Btl_FaceReturn on every unit 0..30. EndPhaseStep phase 29"),
    0x7A2670: ("FFX_Btl_DefeatsInFlight", "the queued defeats plus those dispatched, bytes 2 and 1 of dword_112CA0A read signed. EndPhaseStep phase 30 waits for 0"),
    # The unit command lists
    0x79AD30: ("FFX_Btl_GetUnitCommandBits", "(unit, command): bit 0 the command's bit (id & 0xFFF) in the unit's word set at +0x664, bit 1 its bit in the set at +0x690"),
    0x79C070: ("FFX_Btl_SetUnitCommandBit", "(unit, command, on): sets or clears the command's bit in the unit's word set at +0x664"),
    0x7AD970: ("FFX_Btl_SortCommandList", "a stable bubble sort of one command list by keys read from unit 18's command block"),
    0x7AEFB0: ("FFX_Btl_SortUnitCommandLists", "FFX_Btl_SortCommandList on each list of a unit's block at word offsets 0, 20, 28, 36, 60, 84, 116 and 148"),
    0x79F260: ("FFX_Btl_FillWords", "a word fill. The battle start fills the command blocks with 0x00FF through it"),
    0x7AC840: ("FFX_Btl_AddScriptCommand", "Btl 55's work: puts a script command into the unit's left window (+0x71A, count +0x6CB)"),
    0x7ACB40: ("FFX_Btl_RemoveScriptCommand", "Btl 56's work: takes a script command out of the unit's left window"),
    # The script motion rows
    0x7A8FC0: ("FFX_Btl_PlayScriptMotion", "Btl 7 and 163 after the pop: motionNotStarted (+0x434) = 1, nothing without a CHR or chrReady (+0x38), the request mapped by FFX_Btl_ResolveMotionRequest, FFX_Mot_SetByModeIndex on mode +0x20, returns the clip frames scaled by the motion speed or 1"),
    0x7A8C80: ("FFX_Btl_ResolveMotionRequest", "a script motion request to a CHRDATA mode index: 1 the rest motion +0x3F1, 2..6 and 12, 13 by the variant bytes +0x408 and +0x409, 130 to 42 after 41, the rest unchanged. Magic 434 and 610 hold a special actor at index 0"),
    0x7A8C50: ("FFX_Btl_MotionWaitPoll", "Btl 8's last test: 0 (keep waiting) while the CHR motion request is active and the motion started, else 5"),
    # The motion events
    0x79A920: ("FFX_Btl_GetHitResult", "(attacker, target, &statusMask, &parts, &pending): the hit of the attacker's current action (cursor of FFX_Btl_CmdQueue_GetActorCmdOrLast) in the target's hit logs +0x774 and +0xA4C, the second winning. Returns the hit's reaction byte, signed. Btl 122"),
    0x7B75C0: ("FFX_Btl_MotionEventMiss", "motion event 1, for an action kind (+0x446) of 1 or 4: a pending miss (reaction 1 or 17) plays the miss sound and lands the hits with flags -3, otherwise FFX_Btl_ClearFlag100Vote2OnMask on the target mask +0xDC0"),
    0x7B7650: ("FFX_Btl_MotionEventClearSavedPos", "motion event 2, for an action kind of 1 or 4: FFX_Btl_ClearSavedPosOnTargets(unit, -5)"),
    0x7B77C0: ("FFX_Btl_MotionEventReaction", "motion events 3..8, for an action kind of 1 or 4 and a reaction past miss: reactions 5, 6, 13, 14, 15, 16 on the hit targets"),
    0x7B7680: ("FFX_Btl_MotionEventHit", "motion events 0 and 9, for an action kind of 1 or 4: the hit sound and the hit effect of a pending hit, then 9 disarms them (+0xE19, +0xE16) and 0 with kind 1 lands the hits, FFX_Btl_ApplyPendingHits(unit, -5, 0)"),
    0x78D620: ("FFX_Btl_ClearSavedPosOnTargets", "clears +0x425 (the saved position byte Btl 157 waits on) on every unit of the selector. Returns the count"),
    0x78DB70: ("FFX_Btl_PlayReactionOnHitTargets", "FFX_Btl_PlayUnitReaction(t, reaction, 1) for every unit of the selector but the attacker that is not ko and holds a hit log of the attacker's current action, once per matching log"),
    0x793DC0: ("FFX_Btl_ClearFlag100Vote2OnMask", "FFX_Btl_SetFlag100Vote(unit, 2, 0) on every unit of the mask"),
    0x79FD40: ("FFX_Btl_PlayHitSound", "the hit sound of a kind on a unit through sub_79FE80 and maybe_FFX_Btl_AddSeFollow, skipped when it is the one dword_112C8E0 already holds"),
    0x79E590: ("FFX_Btl_StartCriticalFlash", "battle effect 54 on the unit with a packed handle from its counter +0xE1E"),
    0x79EBA0: ("FFX_Btl_StartHitEffect", "sub_79EB10(unit, a2, 8, effect, 1), and on a start ++hitEffectCount (+0xE13), which the action finish waits on"),
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
    0x112C9EE: ("g_ffxBtlEndMusicTrack", "FFX_Btl_EndPhaseStep phase 24: 20 game over, 12 victory, also the frames phase 25 waits"),
    0x112C94C: ("g_ffxBtlEndEffect", "FFX_Btl_EndPhaseStep phase 24: 140 game over, 138 victory"),
    0x112C9F0: ("g_ffxBtlEndFadeFrames", "FFX_Btl_EndPhaseStep: 45 game over, else 10"),
    0x112C9F3: ("g_ffxBtlEndDrainEffects", "FFX_Btl_EndPhaseStep phase 30 clears it once no unit is busy or dying and no effect unit runs"),
    0x112C9EF: ("g_ffxBtlGameOverEffect", "FFX_Btl_EndPhaseStep: 1 while the game over effect runs, -1 when a press ended it"),
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
