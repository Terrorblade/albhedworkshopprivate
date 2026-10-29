"""
Apply the names from the 2026-10-29 new game port (Project Zanarkand src/ffx/save/NewGame, PROGRESS
14.10) to the FFX.exe IDB.

The RNG seed counter (a PS2 style vblank count whose tick nothing on PC calls, so the seed term it
adds is always 0), the default name copy, the character ability bit test that was taken for an event
flag test, and the two new game start item lists that were named as party members. The coordinator
checked each row in the disassembly. The sphere grid bonus cache family (g_ffxEquipStatBonus and its
three accessors) is NOT renamed here, because the kit uses those names as identifiers and cannot be
rebuilt right now.

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

    python apply_newgame_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x887E10: ('maybe_FFX_Tk_GetVblankCount', 'Returns dword_1330210 with edx 0. Read by FFX_InitNewSaveData 0x786E09 (added to the RNG seed) and FFX_Rand_UnusedSecondLcg. Its only incrementer 0x887E20 has no xref and no raw pointer in any segment, so on PC it is always 0. The vblank reading is [I].'),
    0x887E20: ('maybe_FFX_Tk_VblankTick_unused', 'inc dword_1330210, inc dword_1330200. No xref and no raw pointer to it anywhere in the image (checked 2026-10-29), so it never runs. [V] for never runs, [I] for the vblank reading'),
    0x887E30: ('maybe_FFX_Tk_ResetVblankAndVsyncEmu', 'Zeroes dword_1330220, dword_1330200 and dword_1330210, disables the vsync emulation and re-arms its slot callbacks. One caller, sub_88E570. [I] name'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x785020: ('FFX_SaveData_TestCharAbilityBit', 'was maybe_FFX_SaveData_TestEventFlag. (char, id): 0 unless id is 0x3xxx. Below 0x3060 it tests bit id & 0xF of word (id & 0xFFF) / 16 in the character record ability words (word_113209A + char * 0x94), from 0x3060 up the shared bits g_ffxSaveSharedAbilityBits. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x1330210: ('maybe_g_ffxTkVblankCount', 'Added to the RNG seed by FFX_InitNewSaveData 0x786E0E through maybe_FFX_Tk_GetVblankCount. Only written by its reset (0x887E44) and the never called tick 0x887E20, so always 0 on PC. [V] for always 0'),
    0x23110A0: ('g_ffxDefaultCharNames', 'The character names FFX_InitNewSaveData copies from ply_save before writing them into the records (0x786D86 area), kept for the name reset. [V] per the 14.10 port'),
    0x113081C: ('g_ffxStartItemIds', 'was g_ffxPartyMemberIds. saveData+0x3D8C, u16[8], the new game starting item ids from party.bin row 0. FFX_InitNewSaveData 0x786E30 skips id 0xFFFF. [V]'),
    0x113082C: ('g_ffxStartItemCounts', 'was g_ffxPartyMemberFlags. saveData+0x3D9C, u8[8], the counts for g_ffxStartItemIds, 0 skips the entry (0x786E3D). [V]'),
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
