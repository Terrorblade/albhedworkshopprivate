"""
Apply the names from the 2026-10-28 battle damage port (Project Zanarkand src/ffx/btl/Damage, PROGRESS
13.6) to the FFX.exe IDB.

The hit, critical and counter rolls, every modifier FFX_Btl_ComputeEffectForTarget applies to a hit in
order, the zeroings after the parts, and the hit band table. Each [V] row was read in the disassembly
while porting it. The three rows whose purpose is inferred carry maybe_.

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

    python apply_damage_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x78A890: ('FFX_Btl_RollHit', "the hit roll: one draw on the unit's stream (0x78A942) before the kind switch, so a miss by Evade and Counter still consumes it, against g_ffxBtlHitChanceByBand. [V]"),
    0x789690: ('FFX_Btl_RollCritical', 'the critical roll, one draw at 0x7896B3, doubles the damage on a hit. [V]'),
    0x78C1D0: ('FFX_Btl_CheckCounter', 'whether the target counters this hit. [V]'),
    0x78BFB0: ('FFX_Btl_ConsumeNulElements', 'consumes a matching NulBlaze, NulFrost, NulShock or NulTide and zeroes the hit with no draw. [V]'),
    0x78C0F0: ('FFX_Btl_WakeOnPhysicalHit', 'clears Sleep on a physical hit. [V]'),
    0x78C530: ('FFX_Btl_ApplyShieldAndBoost', 'the Shield and Boost damage scale. [V]'),
    0x78ADC0: ('FFX_Btl_ApplyShell', 'halves magical damage under Shell. [V]'),
    0x78AD40: ('FFX_Btl_ApplyProtect', 'halves physical damage under Protect. [V]'),
    0x78C0B0: ('FFX_Btl_ApplyBerserk', 'the Berserk damage scale. [V]'),
    0x78C580: ('FFX_Btl_ScaleMagicBooster', 'the Magic Booster damage scale. [V]'),
    0x78C5F0: ('FFX_Btl_ApplyAlchemy', 'the Alchemy item scale. [V]'),
    0x7891E0: ('FFX_Btl_ApplyDamagePercent', 'the percent modifiers on a hit. [V]'),
    0x78AD80: ('FFX_Btl_RefuseFractionFormula', 'refuses the fraction formulas where they do not apply. [V]'),
    0x78A200: ('FFX_Btl_ApplyDrainSign', 'the drain sign flip. [V]'),
    0x78A830: ('FFX_Btl_ApplyArmored', 'the Armored target scale. [V]'),
    0x78A7D0: ('FFX_Btl_ApplyDefendAndSentinel', 'the Defend and Sentinel damage scale. [V]'),
    0x789290: ('FFX_Btl_ApplyBreaks', 'Armor Break and Mental Break, Mental only when the ability has an HP part. [V]'),
    0x78AAC0: ('FFX_Btl_ApplyClassImmunity', 'the physical and magical immunity of the target class. [V]'),
    0x78BD90: ('FFX_Btl_ScaleByOverdriveMinigame', 'the overdrive minigame result scale, through __ftol2_sse. [V]'),
    0x78BD50: ('FFX_Btl_RefuseCtbOnThreatened', 'drops the CTB part against a threatened target. [V]'),
    0x78BA30: ('FFX_Btl_ZeroOnStayingPetrified', 'zeroes the parts against a target that stays petrified. [V]'),
    0x78C180: ('FFX_Btl_ZeroCtbOnImmuneStatus', 'zeroes the CTB part when the target is immune to the status. [V]'),
    0x78C480: ('FFX_Btl_ZeroOnNewKo', 'zeroes the parts on a target the hit newly KOs. [V]'),
    0x78D730: ('FFX_Btl_ApplyStackChanges', 'the status stack changes after the parts. [V]'),
    0x78C640: ('FFX_Btl_HitNumberKind', 'the kind of number a hit shows. [V]'),
    0x78BF50: ('FFX_Btl_MarkActionHadEffect', 'marks the action as having had an effect. [V]'),
    0x78DAE0: ('FFX_Btl_SeedHitFromTarget', "fills a hit record's start state from its target. [V]"),
    0x7845E0: ('maybe_FFX_SaveData_AdjustAffection', 'moves the save +0x2C words when unit 0 hurts or heals slots 1..7, read as affection. [V] body, [I] purpose'),
    0x79E340: ('maybe_FFX_Btl_IsHostileAbility', "the ability's bit 0x80 tables, used only to arm a counter. [V] body, [I] purpose"),
    0x78C270: ('maybe_FFX_Btl_PickReaction', 'not ported, named from what its result feeds (BTLHIT +0x00). [I]'),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x79AF60: ('FFX_Btl_IsSummonUnit', 'was maybe_FFX_Btl_IsSummonUnit. Tests unit +0x590 bit 0x04, and FFX_Btl_RandStreamForUnit treats it as the summon slot. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xC421F0: ('g_ffxBtlHitChanceByBand', '9 bytes 25, 30, 30, 40, 40, 50, 60, 80, 100, indexed by the clamped band at 0x78A9A4. [V]'),
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
