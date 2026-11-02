"""
Apply the item system names to the FFX.exe IDB.

These came out of porting the inventory, gil, shops, the Items menu's use, the results screen payout
and the item text into Project Zanarkand (src/ffx/item/). Each row was read in the disassembly:
the item shop's quantity windows and prices (module 13), the shop price rate the event package load
resets to 100, the Items menu's sort, list build and out of battle restore (module 3), the aeon list
the menu builds, the results screen's gil counter, and the item row and text getters.
CHEAT_SAVEDATA.md 5 and MENU_SYSTEM.md cover the rules.

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

    python apply_item_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # Module 13, the item shop
    0x8D9BD0: ("FFX_ItemShop_StepBuyQuantity", "the buy quantity window's step. The most is gil / price (idiv 0x8D9BFA), less when held + that passes 99. Left and right move by 1, up and down by 10. Confirm does FFX_SaveData_AddItem(item, n) then FFX_SaveData_SpendGil(price * n)"),
    0x8D9E20: ("FFX_ItemShop_StepSellQuantity", "the sell quantity window's step, the most is the count held. Confirm does FFX_SaveData_AddItem(item, -n) then FFX_SaveData_SpendGil(-(price * n))"),
    0x8DA860: ("FFX_ItemShop_OpenBuyQuantity", "opens the buy quantity window over stock row +72: item, FFX_ItemShop_GetPrice, the count held and the gil, the quantity at 1"),
    0x8DAA00: ("FFX_ItemShop_OpenSellQuantity", "opens the sell quantity window over g_ffxMenuListRows row +72: item, FFX_ItemShop_GetSellPrice, the count held and the gil, the quantity at 1"),
    0x8DB400: ("FFX_ItemShop_GetSellPrice", "FFX_Shop_SellPriceFromBase of item_rate.bin row (id & 0xFFF). The price rate does not apply"),
    0x8DB440: ("FFX_ItemShop_CountStock", "item_shop.bin row g_ffxItemShopId: up to 16 u16 ids from row +2, stopping at the first 0 or 0xFF. The count goes to dword_186AC24"),
    0xA5CEE0: ("FFX_Shop_SellPriceFromBase", "(shop, base) -> base / 4 signed, 1 when that is 0. The shop argument is not read"),
    0x86C480: ("FFX_Shop_GetPriceRate", "returns g_ffxShopPriceRate, the percent FFX_ItemShop_GetPrice and FFX_EquipShop_GetBuyPrice multiply by"),
    0x870F60: ("FFX_Shop_SetPriceRate", "g_ffxShopPriceRate = rate. core:549 sets it, FFX_Ev_LoadEventPackage puts it back to 100 (0x872F38)"),
    # Module 12, the equipment shop's commits on the records
    0x8D8520: ("FFX_EquipShop_TryOnLoss", "(id) equips the piece on its character (entry +4) and side (+5), FFX_Menu_RecomputeCharAndClamp, bit 1 if the current HP went down and bit 2 if the MP did, then puts the old piece back, recomputes and restores HP and MP. State 21 picks mmain 49..51 or 40 'Equip?' by it"),
    0x8D9120: ("FFX_EquipShop_EquipBought", "(id) FFX_SaveData__setCharEquip on the piece's character and side, then FFX_Menu_RecomputeCharAndClamp. State 22 on Yes"),
    # Module 3, the Items menu
    0x8C8FE0: ("FFX_ItemsMenu_SortByOrderTable", "the Sort command: empties all 256 slots, sums the listed rows' counts by id & 0xFFF, then writes back in the order of g_ffxItemSortOrder until 0xFFFF. An id the table does not name is dropped. Ends with FFX_ItemsMenu_BuildListRows(4)"),
    0x8C8E60: ("FFX_ItemsMenu_BuildListRows", "fills g_ffxMenuListRows (u16 id, u8 count, u8 usable) from the walked slots with a count. Mode 0 marks usable the rows with item flag +0x1C bit 0x01 and a target the menu can take, mode 4 marks every row, mode 6 keeps only rows with +0x5D not 0xFF"),
    0x8C69A0: ("FFX_ItemsMenu_GetRestoreAmount", "(item, char, mp) out of battle: formula +0x28 6 is 50 * power, 7 is power * ((power + magic) / 2), 8 is power * max / 16 signed with mp picking the max MP. Anything else 0"),
    0x8C6A70: ("FFX_ItemsMenu_CanReceive", "(kind, ?, char): 0 HP below max, 1 MP below max, 2 either, 3 always, else no"),
    0x8C6A60: ("FFX_Menu_IsBelow", "a < b"),
    0x8C8D10: ("FFX_ItemsMenu_RestoreHp", "(char, amount): current HP + amount, at most the max, through FFX_Menu_SetCharCurrentHp"),
    0x8C8E20: ("FFX_ItemsMenu_RestoreMp", "(char, amount): current MP + amount, at most the max, through FFX_Menu_SetCharCurrentMp"),
    0x8C8C40: ("FFX_ItemsMenu_ApplyHp", "the item word_186A3A8 on HP: byte_186A391 == 1 is the chosen char only, else item +0x22 bit 0x80 every aeon of the aeon list and bit 0x40 every party member, each with its own amount"),
    0x8C8D50: ("FFX_ItemsMenu_ApplyMp", "FFX_ItemsMenu_ApplyHp for MP"),
    0x8A9090: ("FFX_Menu_BuildAeonList", "the menu's aeon list: g_ffxMenuAeonList bytes, g_ffxMenuAeonCount and g_ffxMenuAeonMask from the aeons in the party, entry 100 being the three Magus Sisters 15, 16, 17 (mask 0x38000)"),
    0x8A9BC0: ("FFX_Menu_GetAeonListEntry", "g_ffxMenuAeonList[i], i clamped to 0..count - 1"),
    0x8A9BF0: ("FFX_Menu_GetAeonListCount", "returns g_ffxMenuAeonCount"),
    # The results screen
    0x8BDA20: ("FFX_BtlResult_GetSpoilsGil", "returns g_ffxSpoilsGil"),
    0x8BDF10: ("FFX_BtlResult_StartGilCounter", "g_ffxBtlResultGilRemaining = the spoils gil, g_ffxBtlResultGilStep = (gil / 189) | 1, plus 2 when that divides by 5. Unsigned"),
    # Item rows and text
    0x8AB280: ("FFX_Menu_GetItemRowAndName", "(item, *name) returns the item.bin row, *name the name string at row +0, or +4 with the map name alt flag"),
    0x8A98F0: ("FFX_Menu_GetItemHelpText", "the item.bin help string, row +8, or +12 with the map name alt flag"),
    0x8C1B30: ("FFX_Menu_GetItemHelpText_2", "the same body as FFX_Menu_GetItemHelpText, the shop's and the help window's caller"),
    0x7767A0: ("FFX_Btl_GetItemNameText", "the item.bin name string at row +0, never the alt one. Item 0 gives the empty string at 0x112A07C"),
    0x8B8DB0: ("FFX_Menu_GetItemNameTextPlain", "the item.bin name string at row +0, never the alt one"),
    # Chests
    0x798FF0: ("FFX_Treasure_DecodeIntoSecondBuffer", "FFX_Spoils_Reset(0x2310CE0, 11) then FFX_Treasure_GiveReward(id, it), returns the buffer. Equipment decoded here has ids 0xB000 + i, which FFX_SaveData__getEquipEntry maps to this buffer's entries at +0xFE"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x8C1960: ("FFX_Menu_GetAutoAbilityHelp", "was FFX_Menu_GetItemHelp. It reads FFX_Btl_GetAutoAbilityRow, the a_ability.bin help at row +8 or +12, not an item"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x1326B6C: ("g_ffxShopPriceRate", "the shop price percent, 100 after every event package load. FFX_Shop_GetPriceRate and FFX_Shop_SetPriceRate"),
    0xC87578: ("g_ffxItemSortOrder", "u16 ids in the Sort command's order, 0xFFFF ends it"),
    0x1841C38: ("g_ffxMenuAeonList", "the menu's aeon list, char index bytes, FFX_Menu_BuildAeonList"),
    0x1841C50: ("g_ffxMenuAeonCount", "the entries of g_ffxMenuAeonList"),
    0x1841C5C: ("g_ffxMenuAeonMask", "bit n for aeon char n in g_ffxMenuAeonList"),
    0x186A391: ("g_ffxItemsMenuSingleTarget", "1 when the item in use takes one target, FFX_ItemsMenu_ApplyHp and ApplyMp"),
    0x186A3A8: ("g_ffxItemsMenuItemInUse", "the item id the Items menu is using out of battle"),
    0x1869F10: ("g_ffxBtlResultGilStep", "the gil the results counter moves a frame, FFX_BtlResult_StartGilCounter"),
    0x1869F14: ("g_ffxBtlResultGilRemaining", "the spoils gil still to count in, FFX_BtlResult_StepGil takes the step or what is left"),
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
