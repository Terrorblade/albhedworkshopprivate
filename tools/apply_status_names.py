"""
Apply the names from the 2026-10-29 status effect port (Project Zanarkand src/ffx/btl/Status, PROGRESS
13.7) to the FFX.exe IDB.

The three status info flag accessors and their tables, the regen kind helper, and the functions the
per-turn status step (BATTLE_CTB_AND_STATUS.md 2.4) reaches, which were read for scope but not ported.
The coordinator checked the [V] accessors in the disassembly. Every name resting on use rather than on
the instructions carries maybe_.

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

    python apply_status_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x79E310: ('FFX_Btl_Status1InfoFlags', 'Returns byte +3 of row n of g_ffxBtlStatus1Info 0xC423E8 (movzx byte_C423EB[n*4]). [V]'),
    0x79E320: ('FFX_Btl_Status2InfoFlags', 'Returns byte +3 of row n of g_ffxBtlStatus2Info 0xC42420. [V]'),
    0x79E330: ('FFX_Btl_CounterInfoFlags', 'Returns byte +3 of row n of the counter status info table (byte_C42467[n*4]). [V]'),
    0x78C670: ('maybe_FFX_Btl_IsDistillMask', 'Returns -1 when the status 2 mask has any of 0x3E, else 0. Two callers in FFX_Btl_ApplyStatus2 (0x78B602, 0x78B60F). The behaviour is [V], that 0x3E is the Distill group is [I].'),
    0x7AF770: ('FFX_Btl_RegenPendingKind', '2 if any regen victim is a zombie, else 1 if any, else 0. [V] per the 13.7 port notes'),
    0x78E410: ('maybe_FFX_Btl_ReleaseThreatenAtTurnStart', 'Reached from the per-turn status step. [I]'),
    0x78E460: ('maybe_FFX_Btl_UpdateThreatenLink', 'Reached from the per-turn status step. [I]'),
    0x7AF840: ('maybe_FFX_Btl_NotifyGuardEnd', 'Called when status 2 bits 0x800, 0x2000 or 0x40 are cleared at turn start. [I]'),
    0x7AF320: ('maybe_FFX_Btl_CanPlayStatusReaction', 'Gates PlayUnitReaction on the status tick path. [I]'),
    0x7AF260: ('maybe_FFX_Btl_RefreshSosAutoStatuses', 'Called after every status tick HP change. [I]'),
    0x79EF40: ('maybe_FFX_Btl_SetStatusEffectVisual', 'Starts or stops an effect handle per counter row (sub_7FC2E0, sub_7FC450). [I]'),
    0x78F6A0: ('maybe_FFX_Btl_PlayHitReactionEvent', 'Sets unit +0x41C and calls sub_7AB360. [I]'),
    0x796940: ('maybe_FFX_Btl_PickIdleStyle', 'Writes unit +0x511 from Sleep, +0x6D4 and the poison pending byte. [I]'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xC423E8: ('g_ffxBtlStatus1Info', '12 rows of 4 bytes, one per status 1 kind, byte +3 the flags FFX_Btl_Status1InfoFlags 0x79E310 returns. Initialised .data. [V]'),
    0xC42420: ('g_ffxBtlStatus2Info', '16 rows of 4 bytes, one per status 2 bit, byte +3 the flags FFX_Btl_Status2InfoFlags 0x79E320 returns. [V]'),
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
