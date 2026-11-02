"""
Apply the equipment names from the Project Zanarkand equipment port to the FFX.exe IDB.

The port is src/ffx/equip/ and src/ffx/btl/WeaponSyscalls.cpp in the Zanarkand repo, written from the
disassembly on 2026-10-31. These are the helpers it read that still had placeholder names: the weapon
side records in battle (attach, detach, the motion model list, the in-battle equip swap), the Equip and
Customize menus' small getters, and the equipment shop's price helpers. CHEAT_SAVEDATA.md 4.1 and
ATEL_BTL_LIBRARY.md rows 115..117, 173, 291 and 293 describe what they do.

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

    python apply_equipment_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The weapon side records, g_ffxBattleSideRecords 0x113446C (62 x 912, the weapon at u, the armour at u + 31)
    0x793330: ("FFX_Btl_AttachWeapon", "Btl 115's body (unit, side). Needs the side record's CHR, the unit's CHR and weaponAttachable +0x4B5. Binds the CHR to the bone at record +13, clears weaponDetached +0x4B6 + side, and for unit 5 restarts its stand-in's turn. Zanarkand WeaponSyscalls.cpp AttachWeapon"),
    0x793F10: ("FFX_Btl_DetachWeapon", "Btl 116's body (unit, side). Sets weaponDetached +0x4B6 + side, unbinds the side record's CHR and leaves it at the bone's world position and the unit's facing. Zanarkand WeaponSyscalls.cpp DetachWeapon"),
    0x7AD130: ("FFX_Btl_IsWeaponMotionModel", "1 when the model id is in the zero ended list at g_ffxBtlWeaponMotionModels 0xC43984 (0x4033..0x4038 and 0x3017), the weapons with a motion set of their own"),
    0x7AD6E0: ("FFX_Equip_AnyRowOneNameWeapon", "Btl 173's body. 1 when any equipment entry in use has flag 0x08 (name id 0x5001, w_name row 1) and not the 0x02 lock. CHEAT_SAVEDATA.md 4.1"),
    0x7AD750: ("FFX_Btl_DisposeWeaponSide", "(unit, side, mask, sideRecord): mask 8 purges the model packet, 1 disposes the CHR, 4 the motion set, 2 the data, then copies the record's handles into the weapon stand-in unit (FFX_Btl_GetWeaponStandInUnit) when there is one"),
    0x7AD820: ("FFX_Btl_SwapEquipInBattle", "(unit, side, id): at effect state 9 swaps the worn piece in battle. setCharEquip, the unit's equipIds, effect state 10, FFX_SaveData_RecomputeCharDerived, then for a new model the side's CHR is disposed and reloaded through FFX_Btl_QueueWeaponChrLoads(1 << side) and, for the weapon, QueueChrLoad(3, u, u, 0, 9). Not ported yet"),
    # The menus
    0x8CF830: ("FFX_Menu_InitModule4Equip", "the Equip screen's init. Keeps current HP and MP of characters 0..6 (dword_186A694, dword_186A6B4) and both record bytes, side 0. Zanarkand EquipMenu.cpp OpenEquipMenu"),
    0x8D0430: ("FFX_Menu_EquipPickedRowSlot", "the row id under the Equip list cursor into dword_186A630, a row of 0 (the armour's none) becomes 0xFF"),
    0x8BF150: ("FFX_Menu_GetEquipString", "FFX_KernelString_Get(5, row, FFX_SaveData_GetMapNameAltFlag()), the Equip screen's strings"),
    0x8BEEB0: ("FFX_Menu_GetCustomizeString", "FFX_KernelString_Get(12, row, FFX_SaveData_GetMapNameAltFlag()), the Customize screen's strings"),
    0x8C1B70: ("FFX_Menu_GetCustomizeTable", "FFX_Equip_GetCustomizeTable's rows, kaizou.bin row 0"),
    0x8C1B90: ("FFX_Menu_GetCustomizeRowCount", "FFX_Equip_GetCustomizeTable's count out parameter, kaizou.bin's row count"),
    # The equipment shop
    0x8C1AF0: ("FFX_EquipShop_GetRateRow", "FFX_KernelTable_GetRow(id, table), the arms_rate.bin row whose first u32 is an auto-ability's price"),
    0xA5CBE0: ("FFX_EquipShop_ScalePrice", "(sum, slots, empty): 100000 for four empty slots, else int(Scale[slots] * float(sum + 50) * Scale[empty]) with Scale 1, 1, 1.5, 3, 5"),
    0xA5CF10: ("FFX_EquipShop_QuarterPrice", "(shop, price): price / 4, at least 1, the sell price. The shop argument is not read"),
    0x86C480: ("FFX_Shop_GetPriceRate", "returns g_ffxShopPriceRate 0x1326B6C, the percent the buy price takes"),
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
    0xC43984: ("g_ffxBtlWeaponMotionModels", "u16[], zero ended. The weapon models with a motion set of their own, FFX_Btl_IsWeaponMotionModel reads it"),
    0x1326B6C: ("g_ffxShopPriceRate", "the shops' price percent, read by both the item and the equipment shop (2026-10-31), FFX_Shop_GetPriceRate"),
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
