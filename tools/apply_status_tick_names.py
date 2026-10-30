"""
Apply the names from the 2026-10-29 per turn status step port (Project Zanarkand
src/ffx/btl/StatusTick and the reaction pick in src/ffx/btl/Status, PROGRESS 13.7) to the FFX.exe
IDB.

The functions the turn start and turn end ticks, the poison tick and FFX_Btl_OnUnitDefeated call,
read block by block for the port (BATTLE_CTB_AND_STATUS.md 2.4.1), and the two latches
FFX_Btl_ApplyHpDamage keeps. The maybe_ names that apply_status_names.py gave are made final where
the port read the whole function, with the old name in the comment. Every name resting on use rather
than on the instructions carries maybe_.

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

    python apply_status_tick_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x79ADF0: ('FFX_Btl_GetUnitMaxHp', 'Returns the unit\'s +0x594 (max HP). FFX_Btl_PickReaction 0x78C2F8 calls it and drops the result. [V]'),
    0x79AE70: ('FFX_Btl_GetUnitMaxMp', 'Returns the unit\'s +0x598 (max MP). FFX_Btl_PickReaction calls it and drops the result. [V]'),
    0x79AD10: ('FFX_Btl_GetUnitCtbBase', 'Returns the unit\'s +0x65D. The other branch cannot be reached from FFX_Btl_PickReaction. [V]'),
    0x78BF00: ('FFX_Btl_HpBand', '(hp, maxHp): 3 at hp <= 0, else q = 4 * hp / maxHp (idiv, 0 for a zero max), q < 1 gives 2, q < 2 gives 1, else 0. The SOS band of +0x63E. [V]'),
    0x78D7F0: ('FFX_Btl_UpdateHpBandAndPose', '(unit, pose). Writes +0x63E = FFX_Btl_HpBand and +0x63D = the weak pose: -1 derives it (1 for Sleep, Silence or Darkness at band 0, the band otherwise, at 0 HP 2 unless KO and settled, then 3, airborne and summon adjustments), any other value is stored, +0x450 forces 0. [V]'),
    0x78C4C0: ('FFX_Btl_IsUnitSettled', 'True when +0xF5E has neither 0x02 nor 0x04, a unit with +0x3F9 is home (+0x508 clear), and FFX_Btl_TurnQueue_ActorHasNonClass4 and FFX_Btl_CmdQueue_ActorHasOtherPending are both false. [V]'),
    0x7AB360: ('FFX_Btl_StartUnitEvent', '(unit, event, wait, held, a5, a6, a7). Resets the event start bytes, with wait sets +0xDF2 = held and +0x432 = 1, then the BtlEv request queue and FFX_BtlEv_StartFirstMatch. No match restores +0x448 and +0x50D and with wait runs FFX_Btl_ClearUnitBusy and gives 0. A match gives 1 and clears +0x432 without wait. [V]'),
    0x7AB6C0: ('FFX_Btl_ClearUnitBusy', 'Word +0x432 = 0 (busy and +0x433), +0x43E = 0, word +0x404 = 0, and +0xDFB 2 -> 3. [V]'),
    0x7AB170: ('FFX_Btl_CancelUnitTurn', '+0x42E = 0 and word +0x42B = 0. The poison tick calls it first. [V] for the writes, the name from the fields'),
    0x78D4E0: ('FFX_Btl_ReleaseProvokeBy', '(unit): clears status 1 Provoke 0x400 on every unit whose +0x5C4 is unit. FFX_Btl_OnUnitDefeated 0x78C8B0. [V]'),
    0x79A190: ('FFX_Btl_ClearUnitStatus', '(unit, keepKo): status 1, the 13 counters, status 2 and the six stacks +0x65E to 0, then FFX_Btl_SetHpMpDoubling(unit, 0), then status 1 bit 0 = keepKo. [V]'),
    0x78D270: ('FFX_Btl_SetHpMpDoubling', '(unit, flags): bit 0 doubles max HP from +0x59C (cap 9999, 99999 with +0x6BE 0x200), bit 1 max MP from +0x5A0 (999, 9999 with 0x400), on a change only. Negative flags reapplies +0x640, else stores it. HP and MP clamp to the new max. [V]'),
    0x7B2910: ('FFX_Btl_TryAutoLife', '(victim, unit, killer, status2): Auto-Life 0x200 in status2 commits 0x311F at priority 64 and gives 0, otherwise +0x5CA = 0 and gives -1, which makes FFX_Btl_OnUnitDefeated try Auto-Phoenix. [V]'),
    0x7B27B0: ('maybe_FFX_Btl_TryAutoPhoenix', '(victim, unit, killer). Looks for an ally with Auto-Phoenix (+0x6BC 0x1000) and a Phoenix Down (0x2006). [I] from the decompile, not ported'),
    0x7B2740: ('FFX_Btl_CommitCommandOnTarget', '(actor, target, issuer, command, priority): a zeroed 72 byte record {actor, +3 count 1, +8 command, +0xA 0xFF, +0x10 1 << target} through FFX_Btl_CommitCommandPriority(rec, priority, 1, issuer). [V]'),
    0x7B0F80: ('maybe_FFX_Btl_ChargeOverdriveOnKill', '(killer, killer unit, victim, victim unit), from FFX_Btl_OnUnitDefeated 0x78C983. 12 functions, no draw. [I] name, not ported'),
    0x78DC30: ('FFX_Btl_StartDefeatSequence', '(victim, a2): 0 unless +0xDCC == 1 and FFX_Btl_IsUnitSettled. Otherwise the boss kill achievement check, the death motion state, and it can start event 57. FFX_Btl_OnUnitDefeated returns its result. [V] for the gate, the rest not ported'),
    0x79EFF0: ('maybe_FFX_Btl_RefreshStatusVisuals', 'Called by FFX_Btl_RefreshSosAutoStatuses after the SOS statuses moved. [I]'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x78C270: ('FFX_Btl_PickReaction', 'was maybe_FFX_Btl_PickReaction. The reaction kind of a staged hit (+0x00), ported whole in 13.7: refusal counts, the target\'s own reaction from +0x447 behind Armored or a defence of 100, the crit 5 or 6, then the tally ladder. No draw. [V]'),
    0x7AF260: ('FFX_Btl_RefreshSosAutoStatuses', 'was maybe_FFX_Btl_RefreshSosAutoStatuses. When +0x63C and +0x63E != +0x63F: FFX_Btl_RestoreAutoStatuses, FFX_Btl_ApplyAutoStatuses, sub_79EFF0, gives 1. [V]'),
    0x7AF320: ('FFX_Btl_CanPlayStatusReaction', 'was maybe_FFX_Btl_CanPlayStatusReaction. (unit, kind): false with +0x508 or Sleep. Kind 1 +0x432 == 0 or motion 0x28/0x29, kind 2 +0x432 == 0 and not those, else +0x432 == 0. [V]'),
    0x7AF840: ('FFX_Btl_PlayGuardEndEvent', 'was maybe_FFX_Btl_NotifyGuardEnd. When FFX_Btl_CanPlayStatusReaction(u, 1): +0x433 = 1 and event 54 with wait. For Defend, Sentinel and Shield ending at turn start. [V]'),
    0x78E410: ('FFX_Btl_ReleaseThreaten', 'was maybe_FFX_Btl_ReleaseThreatenAtTurnStart. A unit holding status 1 0x800 clears it on +0x5C6 (itself for 0xFF), then FFX_Btl_UpdateThreatenLink releases both ends. Turn start and FFX_Btl_OnUnitDefeated. [V]'),
    0x78E460: ('FFX_Btl_UpdateThreatenLink', 'was maybe_FFX_Btl_UpdateThreatenLink. With 0x800 held it sets the link and facing, without it (0x78E522) releases both ends: +0x5C5 = +0x5C6 = 0xFF, FFX_Btl_PickHomeFacing, FFX_Btl_PlayIdleReturnEvent. [V] for the release half'),
    0x796940: ('FFX_Btl_PickHomeFacing', 'was maybe_FFX_Btl_PickIdleStyle. +0x511 = 2 with status 1 0x800, 3 when +0x6D4 != 0xFF, 1 when g_ffxBtlSummonPushed, else 0, and returns it. The old comment (Sleep, the poison byte) was wrong. [V]'),
    0x78F6A0: ('FFX_Btl_PlayIdleReturnEvent', 'was maybe_FFX_Btl_PlayHitReactionEvent. Not KO, no Petrify, no Eject, FFX_Btl_CanPlayStatusReaction(u, 0): +0x41C = 1 and event 0 with wait. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xC421EC: ('g_ffxBtlHpFloorLatch', 'Encounter 0x1590000 in FFX_Btl_ApplyHpDamage: unit 20 taking damage sets it, unit 23 clears it, while set unit 21 cannot go below 1 HP. 1 in the exe image (initialised .data) and never reset, it lives as long as the process. [V]'),
    0x11334C4: ('g_ffxBtlKoCutLatch', 'Set by FFX_Btl_ApplyHpDamage 0x78E2F1 in encounter 0x2090000 when a unit of max HP 24000 or 48000 drops to 0 on action index 1. FFX_Btl_ExecCommand 0x79263C then skips FFX_Btl_ApplyActionToTargets and clears it at 0x7926E8. [V]'),
    0x112CA15: ('maybe_g_ffxBtlCmdActionIndex', 'The action index of the running command, 1 for the first. FFX_Btl_ApplyHpDamage reads it for the 0x2090000 latch. [I]'),
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
