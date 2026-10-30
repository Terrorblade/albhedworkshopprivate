"""
Apply the names from the 2026-10-29 battle command execution port (Project Zanarkand
src/ffx/btl/Command.h, CommandQueue.cpp and CommandExec.cpp, PROGRESS 13.4) to the FFX.exe IDB.

The command path runs a committed 72 byte record from g_ffxBtlCmdQueue through FFX_Btl_ExecCommand
(claim, AI fill, cost, event 2, FFX_Btl_ResolveTargets, FFX_Btl_ApplyActionToTargets) into
FFX_Btl_StartCommandExec, and FFX_Btl_FinishActions flushes the staged hits with
FFX_Btl_ApplyPendingHits(unit, -5, 0x400). The last hit retires the turn through
FFX_Btl_TurnQueue_RemoveAt. Named here are the helpers of those functions that still had sub_ names,
and the command globals they share. Every name resting on use rather than on the instructions carries
maybe_.

Left out on purpose: 0x793EF0 (zeroes dword +0x39C of every unit, the caller was not read), the
presentation callees of FFX_Btl_EndActionPresentation's body (sub_787740, sub_788E30, sub_79E830,
sub_793E00, sub_79DD90, sub_7BBDA0), the visual helpers sub_78DEB0, sub_7972C0, sub_7A0090, sub_7A0100,
sub_7A0140, sub_79ED00, sub_79ED20, sub_79EBE0, sub_79EE20, sub_7969F0, sub_7AF0F0, sub_898C60,
sub_7A04B0, sub_7A99B0, sub_79D050, sub_793260, sub_793410 and sub_78D680, which the port only calls
through its presentation interface and which were not read far enough to name.

These bytes sit inside a larger item, so they need the item undefined and redefined before they can
take a name. They are not in GLOBALS.

    0x112C9D0  the hit phase, FFX_Btl_ApplyPendingHits writes 6 when it retires a turn. The low byte
               of dword_112C9D0
    0x112C9D1  the ability name timer, read signed. sub_78FEA0 sets it and 0x112C9D2 to the same value,
               sub_78FA30(0) zeroes both. Byte 1 of dword_112C9D0
    0x112C9D2  the ability name timer start. Byte 2 of dword_112C9D0
    0x112C9D3  the number wait FFX_Btl_FinishActions and FFX_Btl_TryBeginActionFinish hold on. The
               high byte of dword_112C9D0
    0x112C9D4  the number wait FFX_Btl_ExecCommand holds on. The low byte of word_112C9D4
    0x112C9DE  zeroed by FFX_Btl_FinishActions. Byte 3 of g_ffxBtlMonstersRegistered
    0x112C9F7  zeroed by FFX_Btl_CommitCommand, FFX_Btl_ExecCommand holds on it. Byte 3 of
               g_ffxBtlGameOverSeq
    0x112C023  test for the battle end after the action, FFX_Btl_FinishActions. Byte 1 of
               word_112C022
    0x112CA03  the grand summon overdrive cost latch, FFX_Btl_LatchOverdriveCost. Byte 1 of
               dword_112CA02
    0x112CA04  ability flag 0x40000000 of the running command. Byte 2 of dword_112CA02
    0x112CA05  ability flag 0x20000000 of the running command, the gate FFX_Btl_SendMenu reads. Byte 3
               of dword_112CA02
    0x112CA0A  0 while a command runs, 1 between commands. The low byte of dword_112CA0A
    0x112CA0B  a hold FFX_Btl_ExecCommand waits on, writer not traced. Byte 1 of dword_112CA0A
    0x112CA0D  a hold FFX_Btl_ExecCommand waits on, writer not traced. Byte 3 of dword_112CA0A

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

    python apply_battle_exec_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x78BA70: ('FFX_Btl_BuildTargetHits', '(attacker, ability, mask, ...): stages the hits of one action per target into the target hit logs, with cover through FFX_Btl_PickCoverGuard and reflect. A random target ability (flags 0x8000) picks one unit per hit with FFX_BtlAi_FilterAndPickTarget on stream 5 (0x78BAF6), a reflected hit bouncing to its own side picks on stream 6 (0x78BD21). FFX_Btl_ApplyActionToTargets 0x7893DC. [V]'),
    0x78EEF0: ('FFX_Btl_PickCoverGuard', '(target, ...): the guard of the target side that takes a single physical hit meant for the target, or 0xFF. FFX_Btl_BuildTargetHits. [V]'),
    0x78B920: ('FFX_Btl_ApplyGilSteal', '(attacker, ..., record, target, ...): the gil steal of the reward record. Stream 10 % 255 against the chance when the record has gil and the script mode is not 2 (0x78B968), then stream 10 % 101 on a success (0x78B992) for the amount. FFX_Btl_ApplyActionToTargets. [V]'),
    0x78A240: ('FFX_Btl_StageDrainHit', '(attacker, ...): stages the drain hit on the attacker own hit log, the pool the targets lost with the sign flipped. FFX_Btl_ApplyActionToTargets for an ability with flag 0x100 (Lancet 0x3020 among them). [V]'),
    0x78D040: ('FFX_Btl_AllocHitLog', '(unit): the first of the two hit logs of the unit (+0x774, +0xA4C) whose attacker byte is 0xFF, zeroed, or null. [V]'),
    0x78EE80: ('FFX_Btl_HitLogHasHitsLeft', '(attacker, cursor, unit): 1 when a hit log of the unit for that attacker and command slot still has staged hits (next < count). [V]'),
    0x78D8D0: ('FFX_Btl_CommitArmedCounter', 'The counterattack a target armed while its hit was staged, committed as a priority command. FFX_Btl_ApplyPendingEffects. [V]'),
    0x789160: ('FFX_Btl_OnKoCleared', '(unit, actor): with +0x606 bit 0 zeroes +0x5D0 and returns 1, else FFX_Btl_ResetUnitForReentry(unit, actor, 1) and FFX_BtlEv_NotifyUnitEvent(unit, 10, 1, 0, 0), returns 0. FFX_Btl_ApplyPendingEffects when a hit clears KO. [V]'),
    0x7891B0: ('FFX_Btl_OnEjectChanged', '(unit, ...): the Eject change of a hit, the unit leaves the battle. FFX_Btl_ApplyPendingEffects and FFX_Btl_ApplyPendingHits. [V]'),
    0x78E150: ('FFX_Btl_OnPetrifyChanged', '(unit, ...): the petrify change of a hit, the visual half (sub_78DEB0, sub_7972C0, sub_7A0100(unit, 89)) gated on the unit having a CHR. FFX_Btl_ApplyPendingEffects. [V]'),
    0x7B2510: ('FFX_Btl_TryAutoMed', 'Auto-Med: the item for the status the hit newly inflicted, committed through FFX_Btl_CommitItemOnTarget as a priority 16..20 command. FFX_Btl_ApplyPendingEffects. [V]'),
    0x7B2850: ('FFX_Btl_TryAutoPotion', 'Auto-Potion at half HP or less, using the HP this hit log took (not a total over both logs), as a priority 33..35 command through FFX_Btl_CommitItemOnTarget. FFX_Btl_ApplyPendingEffects. [V]'),
    0x7B26D0: ('FFX_Btl_CommitItemOnTarget', '(unit, target, issuer, item, priority): a zeroed 72 byte record, count 1, ability 0x3001 (Item), sub id the item, mask 1 << target, then FFX_Btl_CommitCommandPriority(rec, priority, 1, issuer). [V]'),
    0x7AF8A0: ('FFX_Btl_CoverMovesDone', 'For each unit covering another (+0x6D4 not 0xFF, +0xDC8 set) ORs both bits into dword_112C8D4 and returns 0 while either is still moving (+0x508), else 1. FFX_Btl_FinishActions. [V]'),
    0x792880: ('FFX_Btl_CountFlaggedMotionTargets', '(mask): how many units of the mask have both +0x40B and +0x40A set. [V]'),
    0x7AF230: ('FFX_Btl_SetAttackInPlace', '(unit, actor): +0x44A = the forced value +0x44B, or sub_7AF0F0(unit, actor, targetMask) when it is 0. [V]'),
    0x792EE0: ('FFX_Btl_IsPartyMemberDying', '-1 when a unit of the first three g_ffxBattlePartyOrder slots has +0x436 set, else 0. FFX_Btl_FinishActions waits on it. [V]'),
    0x7AF4B0: ('FFX_Btl_ResetUnitsPerTurn', 'Every unit at each turn retirement: +0x1BC and +0x6DA zeroed, the place from FFX_Btl_RestorePlaceFromAlt, FFX_Btl_RefreshSosAutoStatuses. Returns the sum of the refreshes. FFX_Btl_TurnQueue_RemoveAt. [V]'),
    0x7AF800: ('FFX_Btl_EndAllCover', 'When byte_112C9FB is set, FFX_Btl_EndCover on every unit covering another (+0x6D4 not 0xFF), then clears it. FFX_Btl_TurnQueue_RemoveAt. [V]'),
    0x7AF880: ('FFX_Btl_EndCover', '(actor): the word +0x6D3 (the cover attacker and the covered unit) to 0xFFFF, then FFX_Btl_PickHomeFacing. [V]'),
    0x7AFB60: ('FFX_Btl_ApplyTurnDeltas', '(unit, actor): when +0x714 is set, adds the signed delta at +0x728 + (+0x715 & 1) to +0x5BC (clamped to +0x5BD) and +0x726 + the same to +0x6E1 (clamped to 100). FFX_Btl_TurnQueue_RemoveAt. [V]'),
    0x7AC600: ('FFX_Btl_RestorePlaceFromAlt', '(actor): copies +0x509..+0x50B to +0x4FC..+0x4FE. [V]'),
    0x79CFD0: ('FFX_Btl_PushBattleMessage', 'A 12 byte message into the four entry ring at 0x112C914, 0 when it is full. The steal and the gil steal report through it. [V]'),
    0x78FA30: ('FFX_Btl_EndAbilityName', '(early): 0 zeroes the ability name timer bytes 0x112C9D1 and 0x112C9D2 and dword_133F520. Non zero shortens a timer still above start - 45 instead. [V]'),
    0x78FEA0: ('FFX_Btl_StartAbilityName', '(ability, frames): when sub_898C60(ability) says the ability shows its name, sets both name timer bytes 0x112C9D1 and 0x112C9D2 to frames. [V]'),
    0x791780: ('FFX_Btl_UnitCanReturnToIdle', '(unit, actor): +0x41C set, +0xDE7 clear, +0x508 clear, +0x432 clear, +0x41D set and the unit is not g_ffxBtlPoisonPendingActor. FFX_Btl_FinishActions starts the idle event 0 on it. [V]'),
    0x791170: ('FFX_Btl_IsWeaponDetached', '(unit, actor): +0x4B6 != 0. FFX_Btl_ExecCommand waits on it for unit 5. [V]'),
    0x7AE120: ('FFX_Btl_GetCasterPoseKind', '(unit, ability row): 1 unless the unit is 1 or 5, then 0 for row flag 0x100000, 2 for 0x200000, else 1. FFX_Btl_StartCommandExec. [V]'),
    0x7AE0F0: ('FFX_Btl_ActionIsDoublecast', '(words): 1 when either of the two words is 0x3029 (Doublecast). FFX_Btl_StartCommandExec stores it as the cut on defeat flag. [V]'),
    0x7AD940: ('FFX_Btl_QueueEquipSwapEffect', '(actor, side, item): when the effect state +0xDFB is 0, +0xF64 = side (1 for Armor 0x3005), +0xF66 = item & 0xFFF, +0xDFB = 8. FFX_Btl_StartCommandExec. [V]'),
    0x7AB500: ('maybe_FFX_Btl_QueueAttackEffect', '(unit): effect state +0xDFB = 15. The attack tail of FFX_Btl_StartCommandExec. [V] for the write, [I] the name'),
    0x7AB520: ('maybe_FFX_Btl_QueueScriptEffect', '(unit): effect state +0xDFB = 7. The script tail of FFX_Btl_StartCommandExec. [V] for the write, [I] the name'),
    0x7AFA80: ('FFX_Btl_StartSummonerCue', '(actor): FFX_Btl_StartUnitEvent(+0x6D1 the summoner, 78, 1, 0, 1, 0, null). FFX_Btl_ExecCommand. [V]'),
    0x7AFA60: ('FFX_Btl_IsUnitNotBusy', '(actor): +0x432 == 0. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7B0770: ('FFX_Btl_CmdQueue_IsPriorityAbsent', 'was FFX_Btl_CmdQueue_FindByPriority. (unit, priority): returns 1 when NO queued entry of the unit has the same priority class (high nibble of +7) as the asked one, 0 when one does. The same sense as FFX_Btl_CmdQueue_IsVariantAbsent, the old name read the other way round. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x112C9FB: ('g_ffxBtlCoverState', '1 when a guard took a hit for an ally (FFX_Btl_BuildTargetHits), 2 once the cover moves ran. FFX_Btl_EndAllCover clears it. [V]'),
    0x112C9FA: ('g_ffxBtlReflectHappened', 'Set by FFX_Btl_BuildTargetHits when a hit was reflected, read by FFX_Btl_StartCommandExec. [V]'),
    0x112CA0F: ('g_ffxBtlCommandRunning', 'A command runs. FFX_Btl_ExecCommand sets it, FFX_Btl_FinishActions clears it. [V]'),
    0x112CA10: ('g_ffxBtlAeonArrivalPending', 'Cleared by the post action kind 1 of a finished command. [V]'),
    0x112CA13: ('g_ffxBtlActionsOfCommand', 'The actions run of the current command, cleared at every finish. [V]'),
    0x112CA14: ('g_ffxBtlPartyDying', 'FFX_Btl_FinishActions holds while a party unit is dying. [V]'),
    0x112CA16: ('g_ffxBtlFireEvent28Pending', 'Event 28 (on the linkUnit +0x422) before the next command, FFX_Btl_ResolveTargets. [V]'),
    0x112C8D4: ('g_ffxBtlShownActionMask', 'The units a command shows, ORed by FFX_Btl_CoverMovesDone with each guard and its covered unit. [V]'),
    0x112C8DC: ('g_ffxBtlExecCommandCopy', 'A copy of the running command id, 255 at rest. FFX_Btl_ExecCommand. [V]'),
    0x112C8F8: ('g_ffxBtlFinishHold', 'FFX_Btl_TryBeginActionFinish waits while it is non zero. The writer was not traced. [V] for the read'),
    0x112C914: ('g_ffxBtlMessageRing', 'Four 12 byte battle messages, FFX_Btl_PushBattleMessage. The count and head are the low two bytes of dword_112C944. [V]'),
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
