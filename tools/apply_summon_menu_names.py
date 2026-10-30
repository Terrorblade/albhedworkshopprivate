"""
Apply the summon and battle menu names from reversing/SUMMONS.md and reversing/BATTLE_MENU.md to the
FFX.exe IDB.

These are the helpers the Project Zanarkand port of PROGRESS 13.10 and 13.11 needed names for: the unit's
trip off the stage and back (sub_793410, sub_793570, sub_793260, FFX_Btl_SavePlaceToAlt 0x7AC650), the enemies turning to a new
party (sub_793CF0), the menu's unit and command lists (sub_785410 and the seven lists it fills), and the
five builders under FFX_BtlMenu_BuildTargetCandidates 0x89ABD0. Every name follows what the disassembly
does. A name starting maybe_ is a guess from its callers and says so.

Left out because the address is a byte inside a larger item, or not an item at all, and cannot take a name
without a split:
    0x112C9E7  g_ffxBtlSummonPushed, the high byte of g_ffxBtlPoisonPendingActor's dword (SHIBYTE)
    0x112C9F5  the Btl 197 game over byte, byte 1 of g_ffxBtlGameOverSeq, RecallParty writes 1 there
    0x1340456  the gil page's top digit, HIWORD(dword_1340454)
    0x133C862  page -1's target byte, 0xF0 before g_ffxBtlMenuPages. FFX_BtlMenu_PushPage 0x89A330 reads
               it at depth -1 (the root push). Not a variable
    0x112C886  byte_112C886, only the computed base of byte_112C886[m] for m 15..17 at 0x7AE054, which
               lands on g_ffxBattlePartyOrder 0x112C895. Not a variable

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

    python apply_summon_menu_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The unit leaving and returning to the stage (SUMMONS.md)
    0x793410: ("FFX_Btl_TakeUnitOffStage", "(unit, mode): show vote 1 off, the status effects stopped, the unit's ATEL worker, then per side record the CHR, motion and data disposal (sub_796DC0: mode 8 marks the packet purged at +0x3A, mode 1 drops the CHR). Mode 7 from FFX_Btl_RemoveUnitFromField, 12 for post action kind 6. Ends in FFX_Btl_ClearUnitStageFields"),
    0x793570: ("FFX_Btl_ClearUnitStageFields", "zeroes +0x508, +0x432, +0x42E, +0x41C (idle return pending), +0x415 (move state) and the dword +0x4C. The tail of FFX_Btl_TakeUnitOffStage"),
    0x793260: ("FFX_Btl_FaceReturn", "+0x49C = facing[movsx +0x511] + pi (the float 3.141592741012573). Called after FFX_Btl_BeginSummon and for every party unit by FFX_Btl_PlaceArrivedAeon"),
    0x793CF0: ("FFX_Btl_TurnEnemiesToParty", "every enemy 20..27 that exists, has not left, is on the field or has property 295, is not KO and has no home facing set (+0x512): FFX_Btl_PickHomeFacing, idle return pending, facing[1] from sub_793BA0 while the party is pushed aside (the byte 0x112C9E7), and battle event 0. After a summon and after the party's recall"),
    0x793BA0: ("FFX_Btl_EnemyFacingWhilePushed", "the facing an enemy takes while the party is pushed aside by a summon, from the averaged placement points of units 0..17 and 20..27"),
    0x78DEB0: ("FFX_Btl_ApplyProperty334Status", "(unit ptr): when byte +0x3B (AI property 334) is set, status word +0x616 gets 0x200. Called by FFX_Btl_ResetUnitForReentry 0x78D530"),
    0x788B30: ("FFX_Btl_FreeUnitEffect", "frees the effect handle at +0x4C when the owned byte +0x35 is set, and clears both. From FFX_Btl_SwapUnitIntoPartySlot for the leaving unit"),
    0x7972C0: ("FFX_Btl_SetUnitChrF500", "(unit ptr, v): FFX_Ch_SetF500 on the unit's CHR when it has one. FFX_Btl_ResetUnitForReentry passes the float at +0x4DC"),
    0x7A0400: ("maybe_FFX_Btl_ReleaseUnitHandles", "(unit, mode), called for the leaving unit of FFX_Btl_SwapUnitIntoPartySlot with mode 1. Its writes are all handles, nothing the simulation reads back"),
    0x79BB50: ("maybe_FFX_Btl_RebuildUnitCommands", "(unit, ...) rebuilds the unit's command lists after a learned bit changed in battle, called after FFX_SaveData_SetCharAbilityFlag while a battle is active (the Ronso Rage write at 0x78F1F5)"),
    0x8990F0: ("maybe_FFX_BtlMenu_PickGroupSwapCursor", "the target page's cursor in the other list nearest on screen to the one it left, from FFX_BtlMenu_PageProc_Target on a side swap. Screen geometry, not ported"),

    # The battle menu's lists and the target page's builders (BATTLE_MENU.md)
    0x785410: ("FFX_Btl_BuildMenuList", "(unit, kind, &count): the list a menu page shows. Kinds 1..4, 7, 12..15 and the default are fixed lists in the unit's command block dword_2310CD8 (572 words per unit), 6 and 17 two fixed tables, 20 the save's item ids, 21 and 22 one command, and 5, 8, 9, 10, 11 and 18 unit lists rebuilt on every call into the g_ffxBtlMenuList* globals. Kind 5 pads to 31 from a 10 slot buffer and overruns into kind 8's"),
    0x8985B0: ("FFX_BtlMenu_BuildCandidateMasks", "the target context's ally, enemy and all masks from the candidate units (lists 9 and 11)"),
    0x898490: ("FFX_BtlMenu_BuildTargetUnitMasks", "the context's 28 unit masks by the row's target kind (u16 +0x1A >> 2 & 3): 0 one per unit, 1 per target group (+0x504 & 7) on each side, 2 one per side at [0] and [20], 3 the actor alone"),
    0x898330: ("FFX_BtlMenu_BuildTargetUnitFlags", "per candidate unit: targetable (+0xDD9 read signed == 1), FFX_Btl_GetTargetLock 0 and 1, protection +0x4FF == 0, not KO +0xDCC"),
    0x8983F0: ("FFX_BtlMenu_ApplyRowTargetFlags", "from the row's byte and word at +0x1A: 0x40 allows KO targets, protection is always ignored, 0x800 and 0x400 allow lock 1 and lock 0 targets, 2 lists enemies first, 0x20 allows the side swap"),
    0x898640: ("FFX_BtlMenu_BuildTargetLists", "the ally and enemy candidate lists from the final mask, the default side and cursor (the actor itself for a single ally target), and the top page's cursor, mask, side and kind"),
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
    0x112C8D0: ("g_ffxBtlDeltaTargetMask", "dword, Btl 255 publishes it and FFX_Btl_QueueDeltaAttack 0x7AE2E0 aims 0x30EA at it"),
    0x112CA11: ("g_ffxBtlAeonDepartGuard", "byte, Btl 280. Not 0 stops the fallen aeon's departure at the head of FFX_Btl_EndPhaseStep case 2 (0x791837)"),
    0x112C268: ("g_ffxBtlMenuListSummon", "u16[10], FFX_Btl_BuildMenuList kind 5: summonable units 0..17 in order, 0x30FF at the first Magus sister. Its pad to 31 runs into g_ffxBtlMenuListExists"),
    0x112C27C: ("g_ffxBtlMenuListExists", "u16[31], FFX_Btl_BuildMenuList kind 8: units 0..17 that exist"),
    0x112C2BA: ("g_ffxBtlMenuListLiveAllies", "u16[31], FFX_Btl_BuildMenuList kind 9: the party order then 0..17, each existing, not left and on the field or with property 295. The ally target candidates"),
    0x112C2F8: ("g_ffxBtlMenuListProperty215", "u16[31], FFX_Btl_BuildMenuList kind 18: kind 9 limited to units with property 215 (+0xDD8)"),
    0x112C336: ("g_ffxBtlMenuListBench", "u16[31], FFX_Btl_BuildMenuList kind 10: the bench order then 0..7, each existing, off the field and not left. The Switch page"),
    0x112C374: ("g_ffxBtlMenuListSingleCommand", "u16, FFX_Btl_BuildMenuList kinds 21 and 22: 0x3121 or 0x3122, the gil pages"),
    0x112C376: ("g_ffxBtlMenuListLiveEnemies", "u16[8], FFX_Btl_BuildMenuList kind 11: enemies 20..27 alive in the kind 9 sense. The enemy target candidates"),
    0x112C6B6: ("g_ffxBtlMenuListKind6", "u16[20], FFX_Btl_BuildMenuList kind 6. Writer not traced"),
    0x112C796: ("g_ffxBtlMenuListKind17", "u16[50], FFX_Btl_BuildMenuList kind 17. Writer not traced"),
    0x2310CD8: ("g_ffxBtlUnitCommandBlocks", "u16[572] per unit 0..30, the fixed command lists FFX_Btl_BuildMenuList reads: words 0 (root, 20), 20 (left window, 8), 28 (right window, 8), 36, 60, 84, 116, 148, 172 (200) and 372 (200)"),
    0x134080C: ("g_ffxBtlMenuSwitchCount", "dword, the Switch page's list count, FFX_BtlMenu_Switch_Enter 0x8A84D0"),
    0x133F0EC: ("g_ffxBtlMenuTargetEnemyCount", "dword, the enemy candidate count, list 11 trimmed by FFX_BtlMenu_InitTargetUnits 0x8979D0"),
    0x133F0F0: ("g_ffxBtlMenuTargetEnemyUnits", "u16[8], the enemy candidate units, list 11 trimmed by FFX_BtlMenu_InitTargetUnits 0x8979D0"),
    0x133D0D0: ("g_ffxBtlMenuTargetContext", "320 bytes, the target page's working set: actor, command, the masks, the 28 unit masks, the flags, the two candidate lists, the side and the cursor"),
    0x133D210: ("g_ffxBtlMenuSavedTargetContext", "320 bytes, g_ffxBtlMenuTargetContext saved by FFX_BtlMenu_PushPage while a second pick runs, put back by FFX_BtlMenu_PopPage"),
    0x1136008: ("g_ffxBtlZeroAbilityRecord", "the row FFX_Btl_GetAbilityRecord 0x79A4B0 gives a unit id and every class without a table. Past the initialized data, so it reads zero"),
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
