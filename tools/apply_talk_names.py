"""
Apply the names from the 2026-10-28 proximity and talk port (Project Zanarkand
src/engine/atel/PlayerProximity, PROGRESS 11.12) to the FFX.exe IDB.

The player bind rows, the talk bonus suppress mask and its setters, the examine scan disarm, and the
two ply_rom bit testers the talk bonus picks its amount with. Each row was read in the disassembly and
ported (tests/engine/PlayerProximityTest.cpp, PlayerProximityOpensTheBsil0100Chest).

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

    python apply_talk_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x86A0E0: ("FFX_Atel_RebindPlayerChr", "re-resolves the bound player CHR after a party index or bindable change, called by core:66 before its 0x184 slot write. [V]"),
    0x870250: ("FFX_Atel_SetTalkBonusSuppressed", "sets or clears bit (1 << character) of g_ffxAtelTalkBonusSuppressMask (core:544). [V]"),
    0x86BD40: ("FFX_Atel_GetTalkBonusSuppressMask", "returns g_ffxAtelTalkBonusSuppressMask. [V]"),
    0x863AE0: ("FFX_Atel_ClearTalkBonusSuppressMask", "zeroes g_ffxAtelTalkBonusSuppressMask. [V]"),
    0x865550: ("FFX_Atel_DisarmExamineScan", "clears the examine scan armed byte ctx+0x1E4, the reverse of FFX_Atel_ArmExamineScan. [V]"),
    0x784F60: ("FFX_PlyRom_HasFlag10Bit1", "ply_rom record +0x10 bit 1 for a character. FFX_Atel_GrantTalkBonusOnce takes ctx+0x04 as the amount when set. [V] body, [I] what the bit means"),
    0x784F90: ("FFX_PlyRom_HasFlag10Bit0", "ply_rom record +0x10 bit 0 for a character. FFX_Atel_GrantTalkBonusOnce takes ctx+0x06 when set, tested after bit 1 so it wins. [V] body, [I] what the bit means"),
    0x85DFC0: ("FFX_AtelSys_Core_543_ClearTalkBonusSuppress_resi", "FFX_Atel_ClearTalkBonusSuppressMask, returns 0. [V]"),
    0x856BA0: ("FFX_AtelSys_Core_049_EnableActorPlayerBind_resi", "marks the actor bindable as the player. [V]"),
    0x856DA0: ("FFX_AtelSys_Core_050_DisableActorPlayerBind_resi", "clears the actor's bindable bit. [V]"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x85F700: ("FFX_AtelSys_Core_402_SetPlayerReach_resf", "was FFX_AtelSys_Core_402_SetContextFloat1F0_resf. ctx+0x1F0 is added to every examine target's talk radius, so it is the player's reach. [V]"),
    0x85E010: ("FFX_AtelSys_Core_544_SetTalkBonusSuppress_resi", "was FFX_AtelSys_Core_544_SetFieldMaskBit_resi. FFX_Atel_SetTalkBonusSuppressed(character, on). [V]"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x1326B90: ("g_ffxAtelTalkBonusSuppressMask", "one bit per party character, FFX_Atel_GrantTalkBonusOnce pays nothing to a set bit. [V]"),
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
