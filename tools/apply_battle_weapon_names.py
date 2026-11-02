"""
Apply the battle weapon names to the FFX.exe IDB (2026-11-01).

These come from porting the weapon CHRs of a battle into Project Zanarkand (src/ffx/btl/BattleSession.cpp
WeaponChrs, src/engine/chr/ChrAttach.cpp and render::skin::AttachedRootMatrix, PROGRESS 10.8 and 13.1): what
FFX_Btl_SetupWeaponChr 0x7938E0, FFX_Btl_AttachWeapon 0x793330 and FFX_Btl_DetachWeapon 0x793F10 call on the
side record's CHR, and where the battle's end hands those CHRs back to the field. Each function was read in
the decompile and the disassembly while porting it. See reversing/BATTLE_ACTOR.md (the 2026-11-01 lines
under section 1.2) and reversing/HELD_OBJECTS.md.

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

    python apply_battle_weapon_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # What the weapon attach and detach call on the owner and the record
    0x795C00: ("FFX_Btl_SetRikkuWeaponPartHidden", "(unit, hide): only for unit 6 whose chr id (+0x04) is 7, FFX_Ch_ModelSetHide(FFX_Btl_GetUnitChr(6), part 1, hide) when the CHR is there. FFX_Btl_AttachWeapon 0x793386 hides the part (1) before it hangs the weapon CHR on the bone, FFX_Btl_DetachWeapon 0x793F8E shows it (0), and the battle's end (FFX_Btl_ReturnChrsToField 0x78226B) shows it"),
    0x793D90: ("FFX_Btl_CreatePendingModelPacket", "(edi, record): when +0x3A is set and the record has a CHR, FFX_Ch_ModelCreatePacket on it and 1, else 0. +0x3A is cleared either way. The record is a unit or a side record, both start with the CHR pointer. From maybe_FFX_Btl_SetupUnitChr 0x79369F, FFX_Btl_SetupWeaponChr 0x79395B and FFX_Btl_ReturnChrsToField"),
    0x79ED40: ("FFX_Btl_StartElementalWeaponFx", "(unit, actor): nothing when the unit left the battle (+0xDCD). Else for each of the four low bits of +0x5D9 (the attack elements) whose slot +0xE90 + 4i holds no handle, FFX_Magic_PackEffectHandle(+0xE28, 13, unit) with +0xE28 counted up and sub_7FC2E0(g_ffxBtlEffectSet, 0, 60 + i, handle). A start that returns more than 0 keeps the handle in the slot. The count started. From FFX_Btl_SetupWeaponChr 0x793B83 (pending 0), btl:121 and maybe_FFX_Btl_ReturnUnitToField"),
    # The battle's end
    0x7821F0: ("FFX_Btl_ReturnChrsToField", "(), only from FFX_Btl_Shutdown 0x7815E9: FFX_Btl_StopStatusFx on units 0..30, then for every battle actor 0..92 with a CHR that does not play on another's (+0x12 == 0) the collision radius scale -1, the tex anim byte, sub_83C940(chr, 0), the tex list copy released and the hide bits 0x10 and 0x02 cleared. FFX_Btl_SetRikkuWeaponPartHidden(6, 0). Then each ATEL actor of kind 1 with a CHR takes the battle unit of its chr id (+0x04) back, with its two side records' CHR, bone (+0x0D) and model (+0x04) through sub_86FDE0, sub_86FE20, sub_86FDA0 and sub_86FE60, hide bit 1 set or cleared, and the record's CHR pointer and +0x22 zeroed. No CHR is detached or disposed here"),
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
