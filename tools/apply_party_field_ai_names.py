"""
Apply the names from the 2026-10-29 party AI and battle field AI port (Project Zanarkand
src/ffx/btl/BattleEvents, ForcedActions and BattleFieldSyscalls, PROGRESS 13.8) to the FFX.exe IDB.

The battle field scripts run in ATEL context 2 with two blocks, set 0 system_01 (battle field kind 14
index 1, kept for the session in g_ffxBtlFieldRecords) and set 1 this battle's field file. Named here
are the hold event, the show all, the bind helpers, the forced turn paths of FFX_Btl_SendMenu
(Berserk, Confusion, Provoke and their target filter), the unit idle step, the started event poll the
Btl wait rows share, and the Btl rows whose names carried an address. Every name resting on use
rather than on the instructions carries maybe_.

Left out on purpose because another script owns the name: 0x7AB360 FFX_Btl_StartUnitEvent
(apply_status_tick_names.py), 0x7986A0 FFX_BtlEv_HasFieldMonsterSection and 0x7986C0
FFX_BtlEv_ResetEventCamera (apply_monster_ai_names.py), 0x793650 FFX_Btl_IsValidUnitIndex
(apply_monster_ai_names.py) and 0x133D6EC g_ffxBtlAutoBattleRetry (apply_booster_names.py).
0x793CB0 (the per unit angle copy of FFX_Btl_StepSubPhase, through sub_793BA0) is left as it is, it
was not read far enough to name.

These bytes sit inside a larger item, so they need the item undefined and redefined before they can
take a name. They are not in GLOBALS.

    0x112A8E4  the script hold, 2 Btl 60's, 1 Btl 151's, 0 none. The high byte of the 2 byte
               g_ffxBtlEndResult 0x112A8E3
    0x112C9D7  the explode hold, an event with no caller is refused while set. The byte after
               g_ffxBtlExplodeActive 0x112C9D6, a 2 byte item
    0x112C9FC  the bone snap camera flag Btl 160 sets, read by FFX_Came_StepBattleBoneSnap
    0x112C9FD  the script point flag Btl 165 sets with g_ffxBtlScriptPointXY
    0x112C9FE  the show vote 3 pending flag FFX_Btl_SetUnitShowVote3 sets
               (all three inside dword_112C9FC, which FFX_Btl_BeginBattle writes as 1)
    0x112C9DC  the advantage, 0 normal, 1 pre-emptive, 2 ambush, Btl 34. Byte 1 of dword_112C9DB
    0x112C9F5  the game over sequence byte Btl 197 tests. The high byte of g_ffxBtlGameOverSeq
    0x112BDED  the flag Btl 1 writes and nothing reads
    0x112BDEE  the battle position byte of SG_DebugWin_BattlePosProc
               (both inside word_112BDED)
    0x112C9E2  the last executed unit, g_ffxBtlLastExecActor. FFX_Btl_ExecCommand writes it with the
               unit it runs, FFX_Btl_BeginBattle writes the word 0xFFFF, Btl 141 reads it signed. The
               low byte of word_112C9E2, whose high byte Btl 174 reads and was not traced here

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

    python apply_party_field_ai_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x7981E0: ('FFX_BtlEv_FireHoldEvent', '(n, flags, caller, out): n 0..7 sets g_ffxBtlHoldOnFieldSet 0 and starts (2, 1, 62, n) from set 1 slot 62. n 8..23 sets it 1, asks (2, 1, 63, n) and, when FFX_BtlEv_HasFieldMonsterSection, (2, 0, 63, n) started from set 0, otherwise from set 1. Anything else -1. Btl 60 and 151. [V]'),
    0x795C70: ('FFX_Btl_ShowEveryUnitIfPending', 'When the show vote 3 pending byte 0x112C9FE is set, clears it and calls FFX_Btl_SetEveryUnitShowVote3(1), which sets it again. The fifth argument of FFX_BtlEv_FireEvent and FFX_BtlEv_FireAbilityEvent. [V]'),
    0x7967A0: ('FFX_Btl_SetUnitShowVote3', '(unit, on): sets the pending byte 0x112C9FE to 1, then FFX_Btl_SetUnitShowVote(unit, 3, on). FFX_BtlAi_SetProperty and FFX_Btl_SetEveryUnitShowVote3. [V]'),
    0x7967C0: ('FFX_Btl_SetEveryUnitShowVote3', '(on): FFX_Btl_SetUnitShowVote3(u, on) for units 0..30. [V]'),
    0x7967F0: ('FFX_Btl_SetUnitChrHidden', '(unit, on): FFX_Ch_SetHideBit1 on the unit CHR and on the CHR of each of its two side records. FFX_Btl_StepSubPhase shows units 0..17 at the battle start. [V]'),
    0x798720: ('FFX_BtlEv_BindSlotCamera', '(unused, ctx, set, slot): FFX_Atel_BindActorCameraHandleChecked(worker, 2) on the worker of FFX_BtlEv_FindSlotRecord. FFX_BtlEv_BindWorkersToUnits binds context 2 (1, 62) and (0, 0). [V]'),
    0x798760: ('maybe_FFX_BtlEv_SetSlotCameraMode', '(ctx, set, slot, value): sub_7BE0A0(handle, value) on the camera handle of the slot worker, when it has one. FFX_BtlEv_ResetEventCamera. [V] for the calls, [I] the name, sub_7BE0A0 is not read'),
    0x7987B0: ('FFX_BtlEv_BindSlotWorkerToUnit', '(ctx, set, slot, unit): writes unit into the battle unit byte of the slot worker (FFX_Atel_GetActorBattleUnitById), returns 1, or 0 with no record. FFX_BtlEv_BindWorkersToUnits. [V]'),
    0x799690: ('FFX_Btl_GetAbilityReach', '(unit, ability record): 3 when the +0x1A word has 0x200 and the unit is 4, 8 or 13, else (+0x1A >> 7) & 3. FFX_Btl_FilterTargetableUnits compares the protect level against it. [V]'),
    0x7996D0: ('FFX_Btl_PickForcedTargets', '(unit, ability, mode): the side from the low byte of mode, or from bit 1 of the target flags when mode < 0, flipped for a monster. Then FFX_Btl_FilterTargetableUnits with selector -15 or -14 and the scope (+0x1A >> 2) & 3: 0 one unit by FFX_BtlAi_FilterAndPickTarget property 92 on stream 4, 1 FFX_Btl_PickTargetGroup, 3 the unit itself, 2 the whole mask. [V]'),
    0x799820: ('FFX_Btl_FilterTargetableUnits', '(unit, selector, ability record, ability): the selector mask less the units with targetable +0xDD9 at 0, a target lock 0 without flag 0x400, lock 1 without 0x800, lock 2, a protect level above FFX_Btl_GetAbilityReach (not the unit itself), and the KO units unless the flags have 0x40. [V]'),
    0x799910: ('FFX_Btl_PickTargetGroup', '(mask): counts the target groups present, draws FFX_Rand_Stream(5) % that count and keeps the units of the group it lands on. [V]'),
    0x799AC0: ('FFX_Btl_CommitForcedAttack', '(unit, unit record, command record, variant, mode): the Berserk (mode 0) and Confusion (mode 1) turn. FFX_Btl_PickForcedTargets on the forced command +0x6C6, then the record is committed with -1, or with -2, id 255 and mask 0 when the ability is 0 or the mask is not above 0. [V]'),
    0x799D50: ('FFX_Btl_CommitProvokedAttack', '(unit, unit record, command record, variant): the Provoke turn. Clears Provoke and returns 0 when the provoker is off the field, KO or petrified, 0 while the unit ignores Provoke, else filters on the provoker side and keeps the provoker (scope 0 or 3) or its group (scope 1). Commits as FFX_Btl_CommitForcedAttack. [V]'),
    0x794CA0: ('FFX_Btl_GetUnitTargetable', '(unit): the signed byte +0xDD9 for a unit <= 30, else 0. [V]'),
    0x794E00: ('FFX_Btl_GetUnitProtectLevel', '(unit): the signed byte +0x4FF for a unit <= 30, else 0. [V]'),
    0x794F20: ('FFX_Btl_GetTargetLock', '(unit, ability): the signed lock byte +0xDDD, or 2 when it is set, the locked ability +0xDDE is not 255 and differs from ability. 0 past unit 30. [V]'),
    0x795040: ('FFX_Btl_GetUnitTargetGroup', '(unit): +0x504 & 7 for a unit <= 30, else 0. [V]'),
    0x7AA160: ('FFX_BtlEv_StepUnitIdleEvents', 'Per unit 0..30: the motion step sub_78AC20, then the pending unit event +0x3F2 started through FFX_Btl_StartUnitEvent, or the idle event 0 after sub_7AB750. The middle of FFX_BtlEv_StepBattleContext. [V]'),
    0x887170: ('FFX_Snd_QueueIopCmd12', '(value): a thunk to FFX_SndIop_QueueCmd12. FFX_Btl_StepSubPhase sends the field monster sound flag at the battle start, FFX_Btl_MainStep another. [V]'),
    0x81EC20: ('FFX_SndIop_QueueCmd12', '(value): writes the 32 byte sound IOP command {12, value, 0...} at g_ffxSndIopCmdWrite, flushing the buffer first when it is full. [V]'),
    0x7A8700: ('FFX_AtelSys_Btl_StartedEventPoll', '(work): 4 * (work == 0) + 1. The poll of Btl 60, 63, 75, 80, 110, 117 and 151, so each ends at once whether its event landed on the caller or not. [V]'),
    0x799B40: ('maybe_FFX_Btl_RepeatCopycatCommand', '(unit, unit record, command record): copies the last command record unk_112BE38 when the command is 0x3028 or an ability with +0x1C bit 0x10000000, then consumes items and re-picks targets through FFX_Btl_PickForcedTargets. FFX_Btl_ExecCommand. [V] for the copy, [I] the name'),
    0x7B0520: ('maybe_FFX_Btl_ResolveMixCommand', '(unit, unit record, command record): takes the first two item ids that are not 255, 0x311E or 0x3001, removes one of each and looks the result up in g_ffxBtlMixPrepareTable. FFX_Btl_ExecCommand. [V] for the reads, [I] the name'),
    0x780CE0: ('FFX_Btl_GetTargetCursorFacing', 'Returns g_ffxBtlTargetCursorFacing. FFX_BtlMenu_PageProc_Target picks which pad bits step its cursor with it. [V]'),
    0x79DFE0: ('FFX_Came_StepBattleBoneSnap', '(a, b, c): when the bone snap byte 0x112C9FC is set, sub_79E1B0(0, x) on a, b and c and c stored at +0x810 of dword_11360A4. FFX_Came_StepAll. [V] for the calls'),
    0x7B2B60: ('FFX_Btl_SetPendingVoice', '(voice): g_ffxBtlPendingVoice = voice, returns it. Btl 179. [V]'),
    0x782670: ('FFX_Btl_KeepSystemFieldRecord', 'Copies the 32 byte field record g_ffxBtlFieldRecord down to g_ffxBtlFieldRecords (set 0) and zeroes the sub phase and the field blob. FFX_Btl_Init calls it after it loads system_01, so set 0 of context 2 is system_01 for the whole session. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7A39D0: ('FFX_AtelSys_Btl_001_SetUnreadScriptFlag_resi', 'was FFX_AtelSys_Btl_001_SetBtlByteBDED_resi. Pops a flag into the low byte of word_112BDED, which only FFX_Btl_BeginBattle also writes and nothing reads, returns 0. [V]'),
    0x7A76E0: ('FFX_AtelSys_Btl_141_GetLastExecActor_resi', 'was FFX_AtelSys_Btl_141_GetWordC9E2Low_resi. The low byte of word_112C9E2 signed, the unit FFX_Btl_ExecCommand last ran. [V]'),
    0x7A7340: ('FFX_AtelSys_Btl_153_SetTargetCursorFacing_resi', 'was FFX_AtelSys_Btl_153_SetBtlByteC9CD_resi. Pops a value into g_ffxBtlTargetCursorFacing as value & 3 and returns it. 5976 calls in 633 battle field files. [V]'),
    0x7A73D0: ('FFX_AtelSys_Btl_160_SetCameraBoneSnap_resi', 'was FFX_AtelSys_Btl_160_SetByteC9FC_resi. Pops a flag, returns the old signed byte 0x112C9FC and sets it to flag & 1, the gate of FFX_Came_StepBattleBoneSnap. [V]'),
    0x7A7DF0: ('FFX_AtelSys_Btl_179_SetPendingVoice_resi', 'was FFX_AtelSys_Btl_179_SetDwordC8F0_resi. Pops a voice id into g_ffxBtlPendingVoice through FFX_Btl_SetPendingVoice. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x112A8FE: ('g_ffxBtlEventsOff', 'FFX_BtlEv_FireEvent and FFX_BtlEv_FireAbilityEvent return -1 while it is set. Only FFX_Btl_BeginBattle writes it (0). [V]'),
    0x112A8E5: ('g_ffxBtlHoldOnFieldSet', 'FFX_BtlEv_FireHoldEvent sets 1 when the hold event went to slot 63 (n 8..23) and 0 for slot 62 (n 0..7). [V]'),
    0x112A919: ('g_ffxBtlStartDelay', 'FFX_Btl_StepSubPhase sets 60 after the battle start event 24, the debug slow start counts it down. [V]'),
    0x112C9CD: ('g_ffxBtlTargetCursorFacing', 'Two bits Btl 153 sets. FFX_BtlMenu_PageProc_Target reads it through FFX_Btl_GetTargetCursorFacing to pick which pad bits move its cursor. [V]'),
    0x112CA06: ('g_ffxBtlUnit13PartFlag', 'The bit Btl 216 sets, read by FFX_Btl_GetUnit13PartFlag and Btl 217 and 219. [V]'),
    0x112A8D0: ('g_ffxBtlScriptPointXY', 'x and y of the point Btl 165 sets (w 1.0 at g_ffxBtlScriptPointW), read by FFX_Btl_GetBattlePoint while 0x112C9FD is set. [V]'),
    0x112A8D8: ('g_ffxBtlScriptPointZ', 'z of the point Btl 165 sets. [V]'),
    0x112A8DC: ('g_ffxBtlScriptPointW', 'w of the point Btl 165 sets, always 1.0. [V]'),
    0x112A9A4: ('g_ffxBtlFieldRecord', 'The 32 byte record FFX_Btl_SetupFieldPointers fills for the battle field file just loaded, +8 the ATEL block, +0x10 the event map, +0x1C the monster section. It is set 1 of FFX_BtlEv_FindSlotRecord, FFX_Btl_KeepSystemFieldRecord copies it to set 0 once for system_01. [V]'),
    0x112A9AC: ('g_ffxBtlFieldAtel', 'The ATEL block of the battle field file, header +4. FFX_Btl_StepSubPhase adds it as the second block of context 2. [V]'),
    0x112A9B4: ('g_ffxBtlFieldEventMap', 'The event map of the battle field file, header +8. FFX_BtlEv_FindSlotRecord reads it as set 1. [V]'),
    0x112A98C: ('g_ffxBtlSystemFieldAtel', 'system_01 ATEL block, +8 of g_ffxBtlFieldRecords. FFX_Btl_StepSubPhase adds it as the first block of context 2. [V]'),
    0x112A994: ('g_ffxBtlSystemFieldEventMap', 'system_01 event map, +0x10 of g_ffxBtlFieldRecords. FFX_BtlEv_FindSlotRecord indexes it by set * 32, so set 0 reads it and set 1 reads g_ffxBtlFieldEventMap. [V]'),
    0x112C8F0: ('g_ffxBtlPendingVoice', 'The voice FFX_Btl_PrepareVoice and FFX_Btl_StartVoice play. Btl 179 sets it, FFX_Btl_SendMenu zeroes it at each turn offer. [V]'),
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
