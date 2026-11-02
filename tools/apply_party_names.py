"""
Apply the party member names from Project Zanarkand's src/ffx/party port to the FFX.exe IDB.

The character records, the lineup, the AP and sphere level path, the battle results screen's AP payment
(the 0x8B9xxx..0x8BExxx results module) and the status and party menu helpers it shares. Every row was
read in the disassembly while porting. SAVE_BLOCK_MAP.md, CHEAT_SAVEDATA.md and
BATTLE_START_AND_END.md have the layouts.

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

    python apply_party_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The save block's character records
    0x7852A0: ("FFX_SaveData_GetMenuPanelWords", "returns &unk_11307B8, save +0x3D28, the menu_panel.bin copy. The status screen tests innate ability bits there"),
    0x7852B0: ("FFX_SaveData_GetApTowardNext", "record +0x18 (dword_1132074 is the records' base + 0x18), the AP banked toward the next sphere level"),
    0x784AC0: ("FFX_SaveData_AddAllToParty", "SetCharInParty(i, 1) for all 18 records, then RecomputeEncountersEnabled"),
    0x784B60: ("FFX_SaveData_AddAllAeonsToParty", "SetCharInParty(i, 1) for records 8..17 only, no encounter recompute"),
    0x7859D0: ("FFX_SaveData_AddCharHp", "record +0x1C += n, clamped 0..+0x24. Returns the new HP"),
    0x785AE0: ("FFX_SaveData_AddCharMp", "record +0x20 += n, clamped 0..+0x28. Returns the new MP"),
    0x790200: ("FFX_Kernel_GetStNumberRow", "FFX_KernelTable_GetRow over g_ffxKernelTableStNumber, st_number.bin"),
    # The status and party menus
    0x8A9930: ("FFX_Menu_GetCharOverdrive", "record +0x39, the overdrive gauge"),
    0x8A9950: ("FFX_Menu_GetCharOverdriveMax", "record +0x3A"),
    0x8A99D0: ("FFX_Menu_GetCharApToNext", "ApNeededForNextLevel minus record +0x18. Not marked as a function in the IDB, the status page calls it"),
    0x8A99F0: ("FFX_Menu_CharRowState", "0 a KO'd row, 2 a live one, 1 greyed: not in the party and either not listed (g_ffxMenuListAllChars) or selectable. HpBand < 1 is the KO test"),
    0x8A9A70: ("FFX_Menu_MainMenuRowState", "in the party: HpBand < 1 ? 0 : 2. Out of it and selectable: 1. Otherwise the HP band again"),
    0x8AA450: ("FFX_Menu_GetListAllChars", "returns g_ffxMenuListAllChars (dword_1840870)"),
    0x8D2D80: ("FFX_Menu_StNumberCount", "entries of an st_number.bin row (4 bytes, 0xFF ends it) with group a3 and subgroup a4. Returns 8 when a2 (the mode) is 0, the gear rows"),
    0x8D2DC0: ("FFX_Menu_StNumberFirst", "the index of the first entry with group a3 and subgroup a4, 0 when none or when a2 is 0"),
    # The battle results screen, the AP payment
    0x8B9EB0: ("FFX_BtlResult_CharRowState", "wraps FFX_Menu_CharRowState"),
    0x8B9EC0: ("FFX_BtlResult_IsCharInFieldParty", "1 when the character is one of the three field slots, 255 never matches"),
    0x8BA370: ("FFX_BtlResult_RowY", "44 * row + 76 + 44 * (7 - rows) / 2, the rows centred"),
    0x8BA3C0: ("FFX_BtlResult_ApChunk", "(ApNeededForNextLevel / 189) | 1, plus 2 when that is a multiple of 5. What one frame of the counting pays"),
    0x8BA400: ("FFX_BtlResult_SortRowsFieldFirst", "swaps the field party's rows to the front, three at most. The row's x word stays with its position"),
    0x8BA4E0: ("FFX_BtlResult_PayMemberAp", "one row's frame: at S.Lv 99 the spoils go to 0 and nothing is paid. Else min(chunk, spoils) through AddApChunk, returns 1 while spoils remain"),
    0x8BDA00: ("FFX_BtlResult_GetApTowardNext", "wraps FFX_SaveData_GetApTowardNext"),
    0x8BDA40: ("FFX_BtlResult_ApNeededForNextLevel", "wraps FFX_SaveData_ApNeededForNextLevel"),
    0x8BDA50: ("FFX_BtlResult_GetSphereLevels", "wraps FFX_SaveData_GetSphereLevels"),
    0x8BDAF0: ("FFX_BtlResult_AddApChunk", "AddAp of the chunk, at S.Lv 98 trimmed to exactly what the level needs (the rest is lost). Shows the level up when +0x18 + paid reaches the need"),
    0x8BDFC0: ("FFX_BtlResult_SetSpoilsAp", "g_ffxSpoilsAp[a1] = a2"),
    0x8BE020: ("FFX_BtlResult_ShowLevelUp", "sets row a2's +0xA (byte_1869F2A, stride 14) to 15"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x8BDA10: ("FFX_BtlResult_GetSpoilsAp", "was ?_Get_future_error_what@std@@YAPBDH@Z_13. A folded library match, it returns g_ffxSpoilsAp[a1]"),
    0x785360: ("FFX_SaveData_IsCharSlotLocked", "was FFX_SaveData_GetCharFlagBit2. Record +0x2C bit 0x04, FFX_SaveData_SetPartyOrderSlot refuses to move a character with it or swap one out"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x1325AC8: ("g_ffxPendingNameEntries", "core:202 sets it to 3 when record 15 (the first Magus Sister) joins, the three name prompts"),
    0x1840870: ("g_ffxMenuListAllChars", "read by FFX_Menu_GetListAllChars, non zero lists every character as live in FFX_Menu_CharRowState"),
    0x1869F20: ("g_ffxBtlResultRows", "the results screen rows, stride 14: +6 character, +8 row state, +9 in the field party, +0xA level up (15)"),
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
