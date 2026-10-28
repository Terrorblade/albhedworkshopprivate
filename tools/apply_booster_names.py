"""
Apply the booster names from reversing/BOOSTERS.md to the FFX.exe IDB.

The HD Remaster boosters are five hotkey toggles polled by FFX_Frame_UpdateBoostersAndOverlays
0x657140 (Turbo, Supercharge, Enemy Encounter Override, Auto Battle, Hide HUD) and three one-shot
actions on the Esc menu's Change Parameters page (Get all items, Unlock all skills, MAX gil) that
write the save block. Mapping them named the auto battle helpers, the toast helpers, the Change
Parameters click path and a few globals, and showed three existing names to be wrong.

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

    python apply_booster_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # auto battle
    0x786F50: ("FFX_Booster_IsAutoBattleAllowed", "0 for story progress 15 and 3360, and for 17 (progress, FFX_Btl_GetCurrentEncounterKey) pairs. FFX_Frame_UpdateBoostersAndOverlays clears g_boosterAutoBattle with toast 893 while this is 0. BOOSTERS.md 3.4"),
    0x7871F0: ("FFX_Booster_CancelAutoBattleInBattle", "g_boosterAutoBattle = 0 and toast 893, only while FFX_Btl_IsBattlePendingOrActive. Called by FFX_Btl_OnUnitDefeated (an aeon falls, or all three Magus Sisters) and FFX_BtlMenu_PageProc_Root (Attack unusable). Runs inside the battle step. BOOSTERS.md 3.4"),
    0x7816B0: ("FFX_Btl_GetCurrentEncounterKey", "(battle id of the scene row in g_ffxEncMapTable << 16) + the formation byte, 0 outside battle. The key FFX_Booster_IsAutoBattleAllowed compares against. Also magic host API entry 280"),
    # toasts
    0x642930: ("FFX_Toast_ShowLine", "FFX_Toast_PlayLine(FFX_Toast_GetHud(), line). Returns 0 when the toast movie is not loaded, and Supercharge and Encounter Override only commit a new value when this returns non-zero. BOOSTERS.md 1"),
    0x6E8750: ("FFX_Toast_GetHud", "returns g_ffxToastHud 0xCE6EA4"),
    0x6E8770: ("FFX_Toast_PlayLine", "thiscall on the toast object. Fetches LocKit line a2, returns 0 if *this (the Iggy player) is null, else calls OnPlay(0, text) and returns 1. Presentation"),
    # booster HUD
    0x6F7770: ("FFX_Booster_HudDtor_ORPHAN", "frees the seven icon movies of the booster HUD from slot 6 down. No caller, FFX_Booster_HudShutdown has the same loop inline"),
    # Change Parameters page
    # 0x6FC7B0: ("FFX_EscMenu_OnChangeParaButton", "a Change Parameters row was pressed (queued by FFX_EscMenu_OnButtonClick for arg 13). Refuses with 'Function not Available now!' in a boot scene event, at story progress 0 or while g_ffxMenuSysRunning, else shows the irreversible confirm (713, 714, 715) and arms FFX_EscMenu_ApplyChangeParaRow. BOOSTERS.md 5"),  # dropped 2026-10-27, already named by tools/apply_achievement_names.py
    # 0x6FC970: ("FFX_EscMenu_ApplyChangeParaRow", "the three permanent boosters. a1 = confirmed, *a2 = row: 0 FFX_Debug_FullItem, 1 trophies 25 and 24 then FFX_SaveData_GrantAllAbilities, 2 FFX_Debug_FullGil. Writes the save block while the Esc menu holds the step. Not synced in co-op. BOOSTERS.md 5"),  # dropped 2026-10-27, already named by tools/apply_achievement_names.py
    0x68F8F0: ("FFX_EscMenu_OnPopUpConfirmed", "AS3 callback. Calls the armed callback at g_ffxEscMenu+0x10C with (button, +0x110) once, then clears both"),
    0x68FF10: ("FFX_EscMenu_SetPopUpCallback", "thiscall, menu+0x10C = a2, menu+0x110 = a3. Armed by FFX_EscMenu_OnChangeParaButton"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7848B0: ("FFX_SaveData_GrantAllAbilities", "was FFX_Btl_LoadPlayerData. Unlock all skills: loads kernel asset 13/1 (ply_save.bin, 'ply_data' is only the alloc tag), ORs row 18 (chars 0..7) or row 19 (8..17), the all-abilities masks, into every save char record +0x3E..+0x49 except 7, sets bits 0..130 of block +0x3D6C, and rebuilds battle command lists if in battle. FFX_Btl_Init calls it only under g_ffxBtlDbgFullSet. BOOSTERS.md 5.2"),
    0x6523E0: ("FFX_Booster_IsHudShown", "was FFX_Booster_IsSceneDrawEnabled. The Hide HUD booster (F5, LocKit 676): *g_ffxGfxCtx+0x10DCC != 0 || g_ffxMenuSysRunning. Presentation"),
    0x6F7700: ("FFX_Booster_HudCtor", "was FFX_Booster_LoadSpeedIcons. Ctor of the 0x40 byte booster HUD: 7 icon slots, 7 swf paths, a column per slot at +0x38. Writes 4 bytes past the block at +0x40 (the time_stop path, then 0x0504), onto the next heap header's magic. BOOSTERS.md 3.6"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x11307D8: ("g_ffxGil", "g_ffxSaveData+0x3D48, u32 gil, clamped 0..999999999. FFX_Debug_FullGil (the MAX gil booster) stores 999999999"),
    0x133D6EC: ("g_ffxBtlAutoBattleRetry", "FFX_Btl_ExecCommand writes 0 or 1 on its two branches, 1 when the chosen action was rejected. Auto battle page procs read it to back out and pick again"),
    0x133D6E4: ("maybe_g_ffxBtlAutoBattleSubPageArmed", "auto battle only: FFX_BtlMenu_PageProc_Root clears it, FFX_BtlMenu_PageProc_B sets it to 1"),
    0xCE6EA4: ("g_ffxToastHud", "the toast object whose +0 is the Iggy player FFX_Toast_PlayLine plays LocKit lines on. Booster commits depend on it being loaded"),
    0xCCC884: ("g_escButtonClickRow", "the row argument FFX_EscMenu_OnButtonClick stores and passes to FFX_EscMenu_OnChangeParaButton"),
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
