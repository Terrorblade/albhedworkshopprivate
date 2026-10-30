"""
Apply the overdrive names from reversing/OVERDRIVES.md and the 2026-10-29 Project Zanarkand port
(PROGRESS 13.9) to the FFX.exe IDB.

The gauge and its modes (FFX_Btl_AddOverdrive 0x7B1590, FFX_Btl_BumpOverdriveModeCounter 0x7B10C0 and the
charges around them), Copycat and Mix, the native minigames' helpers (the level tables at 0xA5CA60..0xA5CC70,
the Lulu threshold fill, the restage of the hits by the minigame's result) and the globals they keep. The
guessed maybe_ names were confirmed against the disassembly and lose the prefix.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all. A name starting maybe_ is a guess
and says so.

Not named here, because each is a byte or a word inside a larger data item and naming it needs the item
split first (do that by hand, then name them):

    0x133D736  Tidus's bar left edge, the high word of g_ffxBtlOdTidusState_lo (118)
    0x133D738  Tidus's bar y, the low word of dword_133D738 (196, drawn only)
    0x133D73A  Tidus's bar width, the high word of dword_133D738 (275)
    0x133D73C  Tidus's zone low edge, the low word of dword_133D73C
    0x133D73E  Tidus's zone high edge, the high word of dword_133D73C
    0x133D740  Tidus's zone width by level, the low word of dword_133D740
    0x133D742  Tidus's cursor, the high word of dword_133D740
    0x133D6F6  Auron's prompt x from FFX_MenuLayout_GetPoint "dum217", the high word of g_ffxBtlOdAuronState_lo
    0x133D720  the byte before g_ffxBtlOdLuluQuadrantRing that Lulu's ring append reads as ring[count - 1]
               (0x891DE2). It is not a variable of its own, a name would mislead.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_overdrive_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # The gauge and the modes
    0x7B12C0: ("FFX_Btl_CountHitOverdriveModes", "(attacker, target, statusTally, reducedBy), the status side of an applied hit. Enemy on party: Victim (5) on a tally above 0, Dancer (6) on reducedBy 0x01, Rook (10) on 0x02, each forced, gains 16, 16, 10. Party on enemy with a tally: Tactician (4), 16. Returns the modes counted. Also called from 0x789110 with (..., 0, 2)"),
    0x7B0CA0: ("FFX_Btl_CountActingUnits", "the units 0..30 with actionState (+0xDF0) not 0. FFX_Btl_GrantUnlockedOverdriveModes waits for 0"),
    0x79AE90: ("FFX_Btl_GetUnitMagic", "(unit) -> the unit's magic stat byte +0x5AA. Lulu's Fury thresholds read unit 5's"),

    # Copycat
    0x799610: ("FFX_Btl_CopycatTargetsGone", "(mask, ability row): true for an empty mask or when a unit of it is off the field, not targetable, or KO while the ability lacks +0x1A bit 0x40. FFX_Btl_RepeatCopycatCommand then picks the targets again"),
    0x7995C0: ("FFX_Btl_FirstTargetOnSameSide", "(unit, mask, unused): 1 for an empty mask, else whether the mask's lowest unit is on the unit's side. The forced pick's mode for a repeated Copycat"),
    0x799AB0: ("FFX_Btl_ResetCopycatMemory", "zeroes the remembered Copycat command (0x112BE38), from FFX_Btl_BeginBattle at 0x781167. The remembered gil (0x112BE94) is never reset"),

    # The native minigames
    0x7AFE00: ("FFX_BtlOd_RestageHitsByResult", "(unit, result, ftol remaining unused). Finds each target's hit log of the action, removes hits for Fury below the full count and for Tidus 0x30 and Auron 0x52 failed (stream 16 % the units at the most hits when the row lacks 0x8000 or is missing, else the count off every unit), restages each target with the success or failure ability (grade into +0x15), writes the mask back and rebuilds a drain hit from the restaged amounts after a stream 16 shuffle"),
    0x89A2F0: ("FFX_BtlOd_FillLuluThresholds", "(digits) the 16 digit string into g_ffxBtlOdLuluHitThresholdTable, each entry the previous one plus its digit, so the table is cumulative rotations per hit"),
    0xA5CC70: ("FFX_BtlOd_LuluThresholdsForLevel", "(level = ability id - 0x3078) picks the digit string by level band and unit 5's magic and fills the threshold table. A level past 18 leaves the table as it was"),
    0xA5CA60: ("FFX_BtlOd_AuronSequence", "(level) -> 0xC86B70 + 32 * level, Auron's button sequence, 16 words ended by 0xFFFF"),
    0xA5CA70: ("FFX_BtlOd_AuronBudget", "(level) -> flt_C86B60[level], 4, 4, 4, 3 seconds"),
    0xA5CB10: ("FFX_BtlOd_LuluBudget", "() -> 4.0 seconds for every Fury"),
    0xA5CB30: ("FFX_BtlOd_TidusZone", "(level) -> the hit zone width 24, 20, 20, 18"),
    0xA5CB80: ("FFX_BtlOd_TidusSpeed", "(level) -> the cursor speed 11, 12, 13, 14 UI pixels a frame"),
    0xA5CBD0: ("FFX_BtlOd_TidusBudget", "(level) -> flt_C86BF0[level], 3, 3, 3, 2 seconds"),
    0x645EB0: ("FFX_BtlOd_SampleLuluKeys", "(&keyA, &keyB): FFX_Input__isPressed 0x8000 alone sets keyA, 0x2000 alone sets keyB. Lulu's keyboard path, two halves make one rotation"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x799B40: ("FFX_Btl_RepeatCopycatCommand", "was maybe_FFX_Btl_RepeatCopycatCommand. Confirmed: a fresh record of a unit below 8 is remembered when an id of action 0 has ability flag 0x10000000, a 0x3028 record becomes the remembered one with its targets kept or repicked (streams 4 and 5 at 0x799C87) and the gil capped at the gil held"),
    0x7B0520: ("FFX_Btl_ResolveMixCommand", "was maybe_FFX_Btl_ResolveMixCommand. Confirmed: the first two items leave the inventory, prepare.bin row max(a, b) at u16 min(a, b) is the result, its targets picked at 0x7B065B (streams 4 and 5) and faced, and a two item Mix is learned"),
    0x7B0F80: ("FFX_Btl_ChargeOverdriveOnKill", "was maybe_FFX_Btl_ChargeOverdriveOnKill. Confirmed: enemy kills party, Avenger (7) for every other party unit on the field, 30. Party kills enemy, Slayer (8) 20, Hero (9) counted at max HP above 20x the reference or 10000, Hero's gain 20 at unsigned max HP above 3x (0x7B1073 jbe)"),
    0x7B1090: ("FFX_Btl_ChargeEscapeOverdrive", "was maybe_FFX_Btl_OnEscapeSucceeded. It only charges Coward (12), 10 on the gauge in that mode, and returns 0. The escape itself is the caller's"),
    0x7B13C0: ("FFX_Btl_ChargeTurnOverdriveModes", "was FFX_Btl_PlayTurnStartVoice. Bumps Ally (13, 3), Daredevil (15, 5) in HP band 1 or worse, Loner (16, 16) with no other unit 0..17 standing and taking turns, Sufferer (14, 16) under a bad status, through FFX_Btl_BumpOverdriveModeCounter and FFX_Btl_AddOverdrive. No voice"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x112BE38: ("g_ffxBtlCopycatLastCommand", "the 72 byte command record Copycat repeats, zeroed by FFX_Btl_ResetCopycatMemory at the battle start"),
    0x112BE94: ("g_ffxBtlCopycatGil", "the remembered command's pending gil. Never reset, it lives as long as the process"),
    0x112C94A: ("g_ffxBtlTidusLearnAbility", "word, the Tidus overdrive FFX_Btl_StepTidusOverdriveLearn unlocked (0x3060..0x3063), 0 none. FFX_Btl_GrantUnlockedOverdriveModes learns it when the message ring takes message 6"),
    0x112C02B: ("g_ffxBtlOverdriveLearnPending", "byte, a mode counter reached 0 or Tidus unlocked an overdrive. CTB waits on it, the grant clears it"),
    0x112CA07: ("g_ffxBtlOdStartLatch", "byte, FFX_BtlOd_MaybeStartMinigame zeroes it when it starts a minigame, FFX_Btl_StepMagicEffectStates reads it"),
    0x112CA0E: ("g_ffxBtlOdHitListCount", "byte, the drain hit list's length at the restage (FFX_BtlOd_RestageHitsByResult)"),
    0xC58EF0: ("g_ffxBtlOdLuluPadPath", "dword, FFX_Input__isLastInputFromPad at Lulu's state 3: 1 the stick, 0 the two keys"),
    0xC86B70: ("g_ffxBtlOdAuronSequences", "four sequences of 16 words of button bits, each ended by 0xFFFF (7, 8, 7 and 6 presses)"),
    0xC86BF0: ("g_ffxBtlOdTidusBudgets", "float[4] 3, 3, 3, 2 seconds"),
    0xC86B60: ("g_ffxBtlOdAuronBudgets", "float[4] 4, 4, 4, 3 seconds"),
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
