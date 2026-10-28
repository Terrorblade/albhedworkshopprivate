"""
Apply the battle kernel table names from reversing/CHEAT_SAVEDATA.md section 0 to the FFX.exe IDB.

Section 0 maps all 52 files of ffx_ps2/ffx/master/jppc/battle/kernel/ to the function that reads each
one, the pointer it lands in and the size beside it. That turned up the kernel string group loader and
its helpers, the key item and equipment name loaders, the Mix prepare table read and the pointer and
size globals FFX_Btl_Init and FFX_Btl_LoadKernelTable fill, most of them still placeholders. The
recreation side is projectzanarkand src/engine/kernel/.

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

One fix it cannot make: the function comment already on FFX_KernelTable_GetRow 0x7AB870 calls the
range count a DWORD. It is a signed WORD (movsx ebx, word ptr [edi]). Edit that comment by hand.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_kernel_table_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # kernel string groups, g_ffxKernelStringGroups
    0x78FEF0: ("FFX_KernelString_LoadGroup", "(group, battle). Loads string group 0..16 into g_ffxKernelStringGroups. Groups 0, 1, 3..12 and 14 go through FFX_Btl_ReadKernelBin by name (14 is config_txt again), groups 2, 13, 15 and 16 are asset kind 39 row <group>, sized by FFX_KernelString_GetAssetGroupSize. 2 and 13 are never loaded. CHEAT_SAVEDATA.md 0.3"),
    0x782B70: ("FFX_KernelString_LoadBattleGroup", "FFX_KernelString_LoadGroup(group, 1), run by FFX_Btl_Init over g_ffxKernelStringBattleGroups"),
    0x782A20: ("FFX_KernelString_GetAssetGroupSize", "size of a kind 39 string group: kind 39 row <group>, and when FFX_MesWin_GetTextLanguage() != 1 the larger of that and the other kind's row (9 for a battle load, else 39). Reselects kind 39 after, because FFX_GetInternationalEnvId is 0"),
    0x784E60: ("FFX_MesWin_TextLanguageIsNot1", "returns -1 when FFX_MesWin_GetTextLanguage() != 1, else 0. Gates string groups 14, 15 and 16 in FFX_Btl_Init. The text language is 0 at boot"),
    # table loaders and getters
    0x7908C0: ("FFX_KeyItem_LoadTable", "reads important.bin through FFX_Btl_ReadKernelBin into g_ffxKernelTableImportant, size in g_ffxKernelTableImportantSize. Called from FFX_Btl_Init"),
    0x7A2600: ("FFX_Equip_LoadNameTable", "reads w_name.bin through FFX_Btl_ReadKernelBin into g_ffxKernelTableWName, size in g_ffxKernelTableWNameSize. 72 byte rows read by FFX_Equip_GetNameString. Called from FFX_Btl_Init"),
    0xA54AB0: ("FFX_SphereGrid_ReadPanelTables", "reads sphere.bin and panel.bin through FFX_Btl_ReadKernelBin, the worker of FFX_SphereGrid_LoadPanelTables"),
    0x790970: ("FFX_Btl_GetAutoAbilityRow", "FFX_KernelTable_GetRow(id & 0xFFF, g_ffxKernelTableAutoAbility, outStringBase), the a_ability.bin row of an 0x8000 id"),
    0x7AFBD0: ("FFX_Btl_StartMixPrepareLoad", "for command 0x311E (Mix), reads prepare.bin (asset kind 13 row 32) into g_ffxBtlMixPrepareTable"),
    0x799F00: ("FFX_Btl_TallyKernelAlloc", "g_ffxBtlKernelAllocTally += (size + 79) & ~63, what each battle data allocation costs with its header. Returns the step. Bookkeeping only"),
    0x799EE0: ("FFX_Btl_ResetKernelAllocTally", "g_ffxBtlKernelAllocTally = 64 and the dword after it = 0"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7848B0: ("FFX_SaveData_GrantAllAbilities", "was FFX_Btl_LoadPlayerData. ORs ply_save.bin rows 18 (characters 0..7) and 19 (8 and up) of the jppc copy, asset kind 13 row 1, into each character record's ability words +0x3E..+0x49 and sets every innate ability bit. The Esc menu unlock all skills, and FFX_Btl_Init under g_ffxBtlDbgFullSet only. BOOSTERS.md 5.2"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    # FFX_Btl_LoadKernelTable pointers and sizes. All BSS and zero at boot, the 0xFFFFFFFF IDA shows is not in the file
    0x112A92C: ("g_ffxKernelTableCommand", "command.bin, 320 rows of 96 bytes, ids 0x3000. FFX_Btl_LoadKernelTable case 0"),
    0x112A944: ("g_ffxKernelTableAutoAbility", "a_ability.bin, 134 rows of 108 bytes, ids 0x8000. FFX_Btl_LoadKernelTable case 2"),
    0x112A968: ("g_ffxKernelTableCommandSize", "u16 size of command.bin rounded to 16"),
    0x112A96A: ("g_ffxKernelTablePlyRomSize", "u16 size of ply_rom.bin rounded to 16"),
    0x112A96E: ("g_ffxKernelTableItemSize", "u16 size of item.bin rounded to 16"),
    0x112A970: ("g_ffxKernelTableAutoAbilitySize", "u16 size of a_ability.bin rounded to 16"),
    # FFX_Btl_Init asset loads, whose kind 13 and 47 rows name jppc
    0x112A950: ("g_ffxKernelTableMagic", "magic.bin, asset kind 13 row 28, 2048 bytes with no kernel header"),
    0x112A954: ("g_ffxBtlMotBin", "battle/mot/mot.bin, asset kind 13 row 30. Not a kernel table"),
    0x112A958: ("g_ffxKernelTableStNumber", "st_number.bin, asset kind 13 row 29, 303 rows of 4 bytes"),
    0x112A960: ("g_ffxKernelTableSumGrow", "sum_grow.bin, asset kind 47 row 6, 77 rows of 8 bytes"),
    0x112A972: ("g_ffxKernelTableSumAssureSize", "u16 size of sum_assure.bin, asset kind 13 row 14"),
    0x112A974: ("g_ffxBtlCtbBaseTableSize", "u16 size of ctb_base.bin, asset kind 13 row 11"),
    0x112A976: ("g_ffxKernelTableMagicSize", "u16 size of magic.bin"),
    0x112A978: ("g_ffxKernelTableStNumberSize", "u16 size of st_number.bin"),
    0x112A97A: ("g_ffxBtlMotBinSize", "u16 size of battle/mot/mot.bin"),
    0x112A97E: ("g_ffxKernelTableSumGrowSize", "u16 size of sum_grow.bin"),
    0x112A980: ("g_ffxEquipCustomizeTableSize", "u16 size of kaizou.bin, asset kind 47 row 5"),
    0x11334BC: ("g_ffxKernelTableImportantSize", "dword size of important.bin rounded to 16, FFX_KeyItem_LoadTable"),
    0x11363B0: ("g_ffxKernelTableWNameSize", "dword size of w_name.bin rounded to 16, FFX_Equip_LoadNameTable"),
    # string groups
    0x11333F0: ("g_ffxKernelStringGroups", "17 records of 12 bytes: a busy byte, the u32 size at +4, the table pointer at +8. FFX_KernelString_LoadGroup fills them, FFX_KernelString_Get reads them"),
    0xC421B4: ("g_ffxKernelStringBattleGroups", "bytes 0, 1, 4, 5, 6, 8, 10, 11, 12, 0xFF. The string groups FFX_Btl_Init loads through FFX_KernelString_LoadBattleGroup"),
    0x1730870: ("g_ffxBtlMixPrepareTable", "prepare.bin, 112 rows of 224 bytes, read by FFX_Btl_StartMixPrepareLoad when Mix starts"),
    0x1136000: ("g_ffxBtlKernelAllocTally", "running byte count of the battle data allocations, FFX_Btl_TallyKernelAlloc. Bookkeeping only"),
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
