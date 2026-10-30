"""
Apply the names from the 2026-10-29 monster AI port (Project Zanarkand src/ffx/btl/MonsterAi,
MonsterAiSyscalls and src/engine/atel/RandSyscalls, PROGRESS 13.8) to the FFX.exe IDB.

The helpers FFX_BtlAi_ResolveTargetSelector, FFX_BtlAi_FilterAndPickTarget, FFX_BtlAi_GetProperty,
FFX_BtlAi_SetProperty, FFX_BtlEv_StartFirstMatch and the Btl AI rows call, read for the port
(MONSTER_AI.md), and the globals they read. FFX_Btl_IsAllyUnit is renamed because it tests
unit <= 30, which takes in the monsters 20..27 too. Every name resting on use rather than on the
instructions carries maybe_.

Two addresses are left out on purpose. 0x112C9E0, the unit FFX_Btl_ExecCommand runs (selectors -2
and -3), is the second byte of the 2 byte item g_ffxBtlActingActor 0x112C9DF, so it cannot take a
name until that item is undefined and redefined as two bytes. 0x1136A7B and 0x1136A7C are the count
(+3) and exec state (+4) of g_ffxBtlAiStagedCmd 0x1136A78, the 72 byte command record, which wants
a struct type rather than names of its own.

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

    python apply_monster_ai_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x7B24A0: ('FFX_Btl_CanAct', '(unit, needOnField, noProvoke, noThreaten): +0xDCE (petrified) clear, status 1 has no 0x300, the counter byte +0x608 is 0, +0x617 bit 0 clear, then +0xDC8 set when needOnField, no Provoke 0x400 when noProvoke, no Threaten 0x800 when noThreaten. Btl 11 asks (u, 0, !+0x5CB, 1). [V]'),
    0x7947F0: ('FFX_BtlAi_ResolveSelectorToUnit', '(self, selector, a3, a4): the lowest set bit of FFX_BtlAi_ResolveTargetSelector below 31, or -1. Btl 33 and about 60 other rows. [V]'),
    0x7957C0: ('FFX_Btl_IsUnitPoisoned', '(unit): FFX_Btl_IsValidUnitIndex and status 1 (+0x606) has Poison 0x08. FFX_BtlAi_GetProperty case 5. [V]'),
    0x795800: ('FFX_Btl_IsUnitPetrified', '(unit): the signed byte +0xDCE for a unit <= 30, else 0. FFX_BtlAi_GetProperty and the selectors. [V]'),
    0x795880: ('FFX_Btl_IsUnitZombie', '(unit): FFX_Btl_IsValidUnitIndex and status 1 (+0x606) has Zombie 0x02. FFX_BtlAi_GetProperty. [V]'),
    0x79AE50: ('FFX_Btl_GetUnitMp', '(unit): the dword +0x5D4 of FFX_Battle_GetActor. [V]'),
    0x871730: ('FFX_Atel_KillActorRequestsAbove', '(actor, level): FFX_Atel_KillRequestsAbove(FFX_Atel_GetActor(actor), level, 1) in the selected context. FFX_BtlEv_StartFirstMatch passes level 0, Core 189 its argument. [V]'),
    0x869350: ('FFX_Atel_EndActorScriptIfRunning', '(actor, a2, a3): FFX_Atel_EndScript(actor, a2, a3) when the actor byte +0x32 is not 0, else nothing. FFX_BtlEv_StartFirstMatch calls it before it creates the event thread. [V]'),
    0x7986A0: ('FFX_BtlEv_HasFieldMonsterSection', 'True when the byte g_ffxBtlFieldMonsterSection points at is not 0 and g_ffxBtlFieldAirborne is 0. Btl 175, FFX_BtlEv_NotifyUnitEvent and two more. [V]'),
    0x7986C0: ('FFX_BtlEv_ResetEventCamera', 'FFX_Sg_SetMotionBlur(0), then sub_798760 on context 2 with (1, 62), (0, 0), (0, 1) and (0, 2). FFX_BtlEv_StartFirstMatch with flag 4. [V] for the calls, the name from the camera event slots'),
    0x7A8970: ('FFX_Btl_GetExecCommandId', 'Returns g_ffxBtlExecCommandId 0x112C8D8. FFX_BtlAi_SetProperty compares it with 0x60EE for property 136 in sins07_07. [V]'),
    0x7AA100: ('maybe_FFX_Btl_UpdateAwayFromHome', '(unit, actor): +0x508 = sub_7A9230(unit, actor, +0x34) when sub_7AAAE0(actor), else 0. Btl 279 calls it for a unit with +0x3F9 before staging. [V] for the writes, [I] the name'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x793650: ('FFX_Btl_IsValidUnitIndex', 'was FFX_Btl_IsAllyUnit. Returns unit <= 30, which is every battle unit, the monsters 20..27 included. It is the test FFX_Battle_GetActor uses to keep a unit index away from the side records 31..92, not an ally test. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x112C9DB: ('g_ffxBtlMonstersRegistered', 'The byte FFX_Btl_RegisterMonsterMotions sets to 1. FFX_BtlEv_FindSlotRecord gives no record for context 3 while it is 0. FFX_Btl_BeginBattle writes the dword 0x10000, so 0x112C9DD is 1 then. [V]'),
    0x1135DF0: ('g_ffxBtlEvEffectEventsLoaded', 'FFX_BtlEv_LoadEffectScriptBlob sets 0xFF, sub_797500 clears it. FFX_BtlEv_FindSlotRecord gives no record for context 4 while it is 0. [V]'),
    0x23112E0: ('g_ffxBtlFieldName', 'The 256 byte name of the battle field archive FFX_Btl_OpenFieldArchive opens, such as znkd09_00. FFX_BtlAi_SetProperty tests it for its encounter specials. [V]'),
    0x112C8D8: ('g_ffxBtlExecCommandId', 'The command FFX_Btl_StartCommandExec stores for the action running now, 0 at FFX_Btl_BeginBattle. Btl 20 and FFX_Btl_GetExecCommandId read it. [V]'),
    0x11363B8: ('maybe_g_ffxBtlShowVoteLatch', 'Btl 24 sets it to 1 for property 84 or 237 in battle 0xD40000 on level hash 8207. FFX_Btl_SetUnitShowVote reads it and clears it. [I] what it means'),
    0x1136AC0: ('g_ffxBtlZnkdHpLatch', 'FFX_BtlAi_SetProperty in znkd09_00: unit 20 property 87 value 4 sets it, then a property 0 write is 100 instead. Any other field clears it. [V]'),
    0x12FB7E0: ('maybe_g_ffxCameStopMoveLatch', 'Came 22 sets it to 1, Came 128 reads it and clears it, Btl 30 clears it when magic 146 is loaded in battle 0x2350000, 0x2360000 or 0x2380000 and the count is 0. [I] what it means'),
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
