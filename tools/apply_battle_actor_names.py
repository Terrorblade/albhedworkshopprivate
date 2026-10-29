"""
Apply the battle actor names from reversing/BATTLE_ACTOR.md section 9 to the FFX.exe IDB.

PROGRESS 1.11.6, second pass (2026-10-28). Six helpers the BTLUNIT field pass leans on: the zeroing
helper that is the only writer of BTLUNIT 0x568..0x58F, the battle name getter, the HP getter, the
equipment model id getter, the placement average used to aim the battle camera, and the cell printer
of the BattleInfo debug window whose labels named several fields. The struct fields themselves go
through reversing/ffx_types.h and tools/apply_battle_structs.py.

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

    python apply_battle_actor_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
COMMENTS = False
VERBOSE = False

# ---------------------------------------------------------------------------------------------
# Functions whose current name is a placeholder
# ---------------------------------------------------------------------------------------------

NAMES = {
    0x79F230: ("FFX_Btl_ZeroBytes", "memset(p, 0, n) when n > 0, else nothing (0x79F230). 66 callers. FFX_Btl_AllocActorArrays zeroes each BTLUNIT (3984) and side record (912) with it, and FFX_Btl_LoadUnitParams 0x79B513 zeroes the 492 byte stat block 0x540..0x72B. Those two are the only writers of BTLUNIT 0x568..0x58F. BATTLE_ACTOR.md 9.1"),
    0x79AF10: ("FFX_Btl_GetUnitBattleName", "Returns FFX_Battle_GetActor(unit) + 0x540, BTLUNIT.m_battleName. FFX_Btl_GetAbilityRecord hands it out for class 0 command ids, and the battle menu passes it straight to the text draws (sub_89BF70 0x89BF95, sub_896450), which read to the zero terminator. BATTLE_ACTOR.md 9.1"),
    0x79ADD0: ("FFX_Btl_GetUnitHp", "Returns the dword at BTLUNIT +0x5D0, m_hp, the current HP (the first HP value the BattleInfo debug cell prints). 13 callers. BATTLE_ACTOR.md"),
    0x7ABDC0: ("FFX_Btl_GetEquipModelId", "Equipment entry index -> model id: 0 for 255 or a missing entry, else the u16 at FFX_SaveData__getEquipEntry(i) + 12. The chr id of a weapon or armour side record (FFX_Btl_SetupUnitRoster, FFX_Btl_SetupWeaponChr, FFX_Btl_QueueWeaponChrLoads). BATTLE_ACTOR.md 1.1"),
    0x794B70: ("FFX_Btl_AveragePresentPlacementPos", "(first unit, count, out vec4): sums FFX_Btl_GetUnitPlacementPos over the units first..first+count-1 that have +0xDC8 set, then scales the sum by 1/n (sub_7C2520, flt_C0A010 / n). sub_793BA0 calls it for units 0..17 and for 20..27, the two sides of the field. BATTLE_ACTOR.md 9.3"),
    0x7DBEE0: ("SG_DebugWin_FormatBattleInfoCell", "sprintf of one cell of the BattleInfo debug window: (is enemy, out, label, cell id, BTLUNIT). SG_DebugWin_BattleInfoProc adds the cells with SG_DebugWin_AddWidget (LV, HP, P1..P4, CT, CS, SS, CH, EF, BD, SD, MN, ET, s1..s3, MV, MT, PS, LK, SM, VS, DM, AC, SG, S1..S8, D1..D8). Its format strings label many BTLUNIT fields. BATTLE_ACTOR.md 9"),
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
