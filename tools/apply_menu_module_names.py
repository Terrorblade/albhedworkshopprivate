"""
Apply the menu module names from reversing/MENU_SYSTEM.md (the 2026-10-28 module table pass) to the
FFX.exe IDB.

Settling which screen each of menu modules 12 to 17 is turned up the name entry screen's own object
and helpers (module 14), the two shops' hooks, stock and price tables (modules 12 and 13), the PS2
save and load hooks (modules 15 and 16) and the scripted menu demo player that the old notes called
a dev test harness (module 22). Game mode bits are the high half of the mode FFX_MenuSys_RequestOpen
0x821750 is given, FFX_Module_SwitchGameMode 0x8AA780 maps each bit to a module.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all. Module 14's exec 0x8C3340 is a
loc_ label, not a function, so it is named as a label.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten. 0x8B15D0 carries the same name tools/apply_international_names.py
proposes, so whichever runs first wins and the other reports it as already named.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_menu_module_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # module 14, name entry, descriptor off_C5A0B0, game mode 0x80000
    0x8C3340: ("FFX_Menu_ExecModule14NameEntry", "module 14 exec (desc off_C5A0B0 +0). Not a function in the IDB. Waits for the name entry object g_ffxNameInputObj +0x30 (set by FFX_NameInput_Step state 10), then FFX_Module_Suspend(14) and a tail jump to sub_900F40. The screen itself runs as a menu object"),
    0x8C3360: ("FFX_Menu_DrawModule14NameEntry", "module 14 draw (desc +4), the generic menu draw sequence the two shops use too"),
    0x8C3380: ("FFX_Menu_PrepareModule14NameEntry", "module 14 prepare (desc +8): dword_1871538 = 1. FFX_Menu_TermModule14NameEntry clears it"),
    0x8C3390: ("FFX_Menu_InitModule14NameEntry", "module 14 init (desc +0xC), arg = the low half of the 0x4008xxxx mode. For char 11 (Shiva) it first copies her default name_txt name into her record. Then FFX_NameInput_Open(0, arg)"),
    0x8C33E0: ("FFX_Menu_TermModule14NameEntry", "module 14 term (desc +0x14): dword_1871538 = 0"),
    0x8C4250: ("FFX_NameInput_Open", "(noPanel, who) builds the name entry menu object: step FFX_NameInput_Step at +12, draw FFX_NameInput_Draw at +16. noPanel 0 also builds the character panel object at +96 (sub_8C43F0) and starts the cursor in the command column. who 0..17 is a character (name_txt 11 'Please enter name.', or 78, the new aeon line, for who >= 8 under game mode 0x80000). who > 17 is free text into save +0xEC with name_txt 12 'Please enter password.' Callers: module 14 init and the Items screen for the Rename Card. Stores the object in g_ffxNameInputObj"),
    0x8C3580: ("FFX_NameInput_Step", "name entry object step. Glyph grid 15 wide (g_ffxNameInputGridCursor), a command column (three page commands, then name_txt 3 Confirm, 4 Delete, 5 Default), Start (0x800) to finish, Select (0x100) toggles grid and column. State 5 calls FFX_NameInput_CommitName, state 7 asks name_txt 8 'Proceed?' Yes/No, state 10 moves to the next Magus sister or sets obj +0x30 done"),
    0x8C3D00: ("FFX_NameInput_Draw", "name entry object draw, the window frame plus the grid, the typed name and the command column"),
    0x8C33F0: ("FFX_NameInput_SwitchToChar", "re-targets the open name entry at another character 0..17 (prompt, default buffer, layout). Called from FFX_NameInput_Step state 10 to go from one Magus sister to the next"),
    0x8C5050: ("FFX_NameInput_LoadLayout", "reads the name entry layout rows from menu_script: rect_fchplt, rect_nmfwnd, rect_nmnplt, num_name1, num_name2, col_nmline, pos_nface, pos_name, pos_kanaa, pos_kanama, pos_kanaga, rect_nmline"),
    0x8C5280: ("FFX_NameInput_SetBuffer", "copies a glyph string into g_ffxNameInputBuffer, 4 bytes per cell, multi byte glyphs kept together, zero fills the rest of the 32 cells"),
    0x8C5320: ("FFX_NameInput_LoadDefaultName", "__fastcall, stack arg = char. Loads the char's default name from name_txt (ids 13..23 through word_C5A0D8 or word_C5A120) into the buffer. The 'Default' command"),
    0x8C5390: ("FFX_NameInput_IsDefaultName", "1 when g_ffxNameInputBuffer equals the char's default name_txt name, 0 otherwise or for who < 0. FFX_NameInput_CommitName stores it at name record +19, so +19 = 1 means the name is still the default"),
    0x8C5440: ("FFX_NameInput_SetChar", "g_ffxNameInputChar = a1"),
    0x8C5370: ("FFX_NameInput_CommitDefaultShivaName", "SetChar(11), LoadDefaultName(11), CommitName: writes Shiva's default name into her record. From FFX_SaveData__reloadCharNames (story >= 0x609) and FFX_SaveData_ReencodeCharNames (language 11). Char 11 unlocks no trophy in CommitName"),
    0x8C4C90: ("FFX_NameInput_TypeGlyph", "Circle on the grid: writes the glyph under the grid cursor into the next buffer cell, capped at a per entry maximum length"),
    0x8C44D0: ("FFX_NameInput_DeleteGlyph", "drops the last glyph. Cross and the Delete command. Languages 9, 10 and 11 clear the whole buffer when it holds the default name"),
    0x8C4FF0: ("FFX_NameInput_GetCommandList", "returns the command column table for the current language and for a character or free text entry (unk_C5A188 / C5A19C / C5A1CC / C5A1E4)"),
    0x8C5260: ("FFX_NameInput_SetGridCursor", "g_ffxNameInputGridCursor = col + 15 * row"),
    # module 12, equipment shop, descriptor off_C5A820, game mode 0x40000
    0x8D7560: ("FFX_Menu_DrawModule12EquipShop", "module 12 draw (desc +4)"),
    0x8D7580: ("FFX_Menu_PrepareModule12EquipShop", "module 12 prepare (desc +8)"),
    0x8D7590: ("FFX_Menu_InitModule12EquipShop", "module 12 init (desc +0xC): g_ffxEquipShopId = arg, the low half of the 0x4004xxxx mode. Scripts pass 0..46, the row count of arms_shop.bin"),
    0x8D75B0: ("FFX_Menu_SuspendModule12EquipShop", "module 12 suspend (desc +0x10), ends with FFX_SaveData_RecomputeAllCharDerived"),
    0x8D85D0: ("FFX_EquipShop_CanAfford", "buy check: FFX_EquipShop_GetBuyPrice(entry) <= FFX_SaveData_GetGil(), 0 when dword_186AB14 is 0"),
    0x8D9370: ("FFX_EquipShop_GetBuyPrice", "price of a 22 byte equipment entry: the arms_rate value of each auto-ability (id & 0xFFF) summed, through sub_A5CBE0, times sub_86C480() / 100, at least 1"),
    0x8D9410: ("FFX_EquipShop_GetSellPrice", "sell price of an owned equipment entry, the same ability sum through sub_A5CF10(g_ffxEquipShopId). 0 when entry +3 & 0xD"),
    0x8D9170: ("FFX_EquipShop_GetStockEntry", "the shop_arms.bin row for (id & 0xFFF), a 22 byte equipment entry template"),
    # module 13, item shop, descriptor off_C5A844, game mode 0x20000
    0x8D9600: ("FFX_Menu_ExecModule13ItemShop", "module 13 exec, the item shop. Loads kind 47 rows 1 (item_rate) and 3 (item_shop), buys through FFX_ItemShop_CanBuy (count < 99, price <= gil), says mmain 25 'You can't carry any more.' or 26 'You don't have enough gil.', and suspends 13 in state 32. Help line mmain id 12 'Items' through FFX_Menu_BuildMainMenuRows(0, 13)"),
    0x8D9AA0: ("FFX_Menu_DrawModule13ItemShop", "module 13 draw (desc +4)"),
    0x8D9AC0: ("FFX_Menu_PrepareModule13ItemShop", "module 13 prepare (desc +8)"),
    0x8D9AD0: ("FFX_Menu_InitModule13ItemShop", "module 13 init (desc +0xC): g_ffxItemShopId = arg, the low half of the 0x4002xxxx mode. Scripts pass 0..46, the row count of item_shop.bin"),
    0x8D9AE0: ("FFX_Menu_SuspendModule13ItemShop", "module 13 suspend (desc +0x10)"),
    0x8DAB30: ("FFX_ItemShop_CanBuy", "FFX_SaveData_GetItemCount(item) < 99 and FFX_ItemShop_GetPrice(item) <= FFX_SaveData_GetGil()"),
    0x8DB3B0: ("FFX_ItemShop_GetPrice", "item_rate.bin row (id & 0xFFF) times sub_86C480() / 100, at least 1"),
    # modules 15 and 16, the PS2 save and load screens, descriptors off_C59FF0 and off_C5A014
    0x8B15D0: ("FFX_SaveLoadMenu_StepModule", "exec of modules 15 (save) and 16 (load), the PS2 memory card screens. g_ffxSaveLoadMenuIsSave picks the path: state 36 help is save_txt 4 'Select save area.' or 31 'Select file to load.', state 37 serialises the block when saving. States 104..113 install a block and step TOSwapInternationalEnvExec. On PC only reachable when a 0x40200000 / 0x40100000 request lands while FFX_SaveUi_IsIdle is 0"),
    0x8B34F0: ("FFX_Menu_PrepareModule15Save", "module 15 prepare (desc +8): dword_18663C0 = sub_8741D0()"),
    0x8B3510: ("FFX_Menu_InitModule15Save", "module 15 init (desc +0xC): resets the save/load state and sets g_ffxSaveLoadMenuIsSave = 1 at 0x8B3556"),
    0x8B3580: ("FFX_Menu_InitModule16Load", "module 16 init (desc +0xC): the same reset with g_ffxSaveLoadMenuIsSave = 0 at 0x8B35C6"),
    0x8B3600: ("FFX_Menu_TermModule15And16SaveLoad", "term of modules 15 and 16 (desc +0x14): sub_888420(0)"),
    # module 22, the scripted menu demo player, descriptor off_C5A8D8, game mode 0x800000
    0x8DB790: ("FFX_Menu_PrepareModule22ScriptedDemo", "module 22 prepare (desc +8): state = 0"),
    0x8DB7A0: ("FFX_Menu_InitModule22ScriptedDemo", "module 22 init (desc +0xC): g_ffxMenuDemoIndex = arg, the low half of the 0x408000xx mode. dword_25D16BC = 1"),
    0x8DB7D0: ("FFX_Menu_TermModule22ScriptedDemo", "module 22 term (desc +0x14): dword_25D16BC = 0"),
    0x8BE370: ("FFX_MenuSys_StartAtelMenuThread", "(a1, a2, a3) selects ATEL context 6, the menu context, and calls FFX_Atel_StartThreadDeferred(0xFFFF, a2, 0, a1, a3). Module 22 passes (1, 0, the demo row's +4 word)"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x8D6E40: ("FFX_Menu_ExecModule12EquipShop", "was FFX_Menu_ExecModule12Shop. Module 12 is the equipment shop, the item shop is module 13. Loads kind 47 rows 0 (arms_rate), 2 (arms_shop) and 4 (shop_arms), sells through FFX_SaveData_RemoveEquipEntry and FFX_SaveData_SpendGil(-price) with mmain 52..54 'You can't sell ...', suspends 12 in state 36. Help line mmain id 13 'Equipment'"),
    0x8DB4E0: ("FFX_Menu_ExecModule22ScriptedDemo", "was FFX_MenuTest_ExecTK. Not dev only: 31 sg:29 sites in 18 events open 0x408000xx, 16 of them not test or dbg maps. Row g_ffxMenuDemoIndex of g_ffxMenuDemoTable gives a module, a script argument and a kind 42 canned save. It turns the virtual pad on, swaps the canned save in, starts the script in ATEL context 6, runs the module until its bit clears, then restores the real save. Rows run modules 7, 19, 19, 9, 7, 7, 21. Very likely the menu tutorials, which is inferred"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x186A258: ("g_ffxNameInputChar", "int, who is being named: 0..17 a character record index, -1 free text into save +0xEC (passwords). Set by FFX_NameInput_Open"),
    0x186A280: ("g_ffxNameInputBuffer", "the typed name, 32 cells of 4 bytes (a glyph and up to two trail bytes), up to word_186A300"),
    0x186A278: ("g_ffxNameInputObj", "the name entry menu object. +0x30 is the done flag module 14's exec waits for"),
    0x186A274: ("g_ffxNameInputPrompt", "the prompt text pointer, name_txt 11, 12 or 78"),
    0x186A268: ("g_ffxNameInputGridCursor", "glyph grid cursor, col + 15 * row"),
    0x186A270: ("g_ffxNameInputInCommandColumn", "1 while the cursor is in the command column, 0 on the glyph grid. Select toggles it"),
    0x186A26C: ("g_ffxNameInputCommandCursor", "row in the command column"),
    0x186A25C: ("g_ffxNameInputCharsetPage", "0..2, the glyph page. Pressed & 5 (L2, L1) and & 0xA (R2, R1) cycle it, only when the language is 0"),
    0x186AB1C: ("g_ffxEquipShopId", "the equipment shop's shop id, a row of arms_shop.bin"),
    0x186ABA4: ("g_ffxEquipShopAbilityRates", "pointer to the loaded arms_rate.bin (kind 47 row 0), a dword per auto-ability, summed into equipment prices"),
    0x186ABA8: ("g_ffxEquipShopLists", "pointer to the loaded arms_shop.bin (kind 47 row 2), one stock list per shop id"),
    0x186ABAC: ("g_ffxEquipShopStock", "pointer to the loaded shop_arms.bin (kind 47 row 4), 22 byte equipment entry templates"),
    0x186AC20: ("g_ffxItemShopId", "the item shop's shop id, a row of item_shop.bin"),
    0x186AC28: ("g_ffxItemShopPrices", "pointer to the loaded item_rate.bin (kind 47 row 1), a dword price per item"),
    0x186AC2C: ("g_ffxItemShopLists", "pointer to the loaded item_shop.bin (kind 47 row 3), one stock list per shop id"),
    0x186628A: ("g_ffxSaveLoadMenuIsSave", "byte, 1 for module 15 (save), 0 for module 16 (load)"),
    0xC5A868: ("g_ffxMenuDemoTable", "7 rows of 16 bytes {module, script argument, rom kind 42, rom entry 4..10} read by module 22. Modules 7, 19, 19, 9, 7, 7, 21"),
    0x1871528: ("g_ffxMenuDemoIndex", "row of g_ffxMenuDemoTable module 22 runs"),
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
