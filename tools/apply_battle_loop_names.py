"""
Apply the names from the 2026-10-29 battle loop port (Project Zanarkand src/ffx/btl/Ctb, BattleStart and
BattleLoop, PROGRESS 13.1, 13.3 and 13.5) to the FFX.exe IDB.

The battle start from FFX_Btl_BeginBattle 0x781020 through FFX_Btl_StepSubPhase 0x783020 to the first turn,
the turn order (FFX_Btl_CtbTick 0x790FB0, FFX_Btl_InitCtbCounters 0x78DED0) and the battle step
FFX_Btl_MainStep 0x790C10 with FFX_Btl_SendMenu 0x792A90 were already named. Named here are the helpers
those call that still had placeholders, and one misnamed function: 0x7B13C0 does not play a voice, it
charges the overdrive modes a turn earns (counters 13, 15 and 16 of FFX_Btl_BumpOverdriveModeCounter and
FFX_Btl_AddOverdrive for the unit whose mode it is). Every name resting on use rather than on the
instructions carries maybe_.

Left out on purpose because the command path port (PROGRESS 13.4) owns the turn retirement: 0x7AF4B0,
0x7AFB60 and 0x7AF800, which FFX_Btl_TurnQueue_RemoveAt calls. 0x7921C0 (the per unit flag bytes of
FFX_Btl_MainStep's head) and 0x793CB0 (sub phase 10's placement reset) were not read far enough to name.

These bytes sit inside a larger item, so they need the item undefined and redefined before they can
take a name. They are not in GLOBALS.

    0x112C9D0  the hit settle count, 6 after FFX_Btl_ApplyPendingHits retires a turn, counted down by
               FFX_Btl_MainStep, FFX_Btl_IsCtbTickAllowed waits for 0. The low byte of dword_112C9D0
    0x112C9D1  the ability name hold, signed, counted down by FFX_Btl_MainStep, which calls sub_78FA30(0)
               when it reaches 0. FFX_Btl_CtbTick waits while it is positive for six long casts
    0x112C9D2  its start value
    0x112C9D3  the number wait FFX_Btl_SendMenu waits on for a Regen kind 1 unit
               (the four bytes of dword_112C9D0, the command path names them too)
    0x112C944  the battle message count, FFX_Btl_IsCtbTickAllowed waits for 0. The low byte of
               dword_112C944, byte 1 the ring head. FFX_Btl_ResetMessageRing zeroes the dword
    0x112CA0A  between commands, 0 at the battle start and while a command runs, 1 after
               FFX_Btl_FinishActions. maybe_FFX_Btl_DispatchQueuedDefeats waits for it
    0x112CA0B  the queued defeat count, FFX_Btl_IsCtbTickAllowed waits for 0
    0x112CA0C  the queued defeats dispatched
    0x112CA0D  the units in the waited event 79, FFX_Btl_IsCtbTickAllowed waits for 0
               (the four bytes of dword_112CA0A, FFX_Btl_ResetDefeatQueue zeroes the dword)
    0x112A929  the next party unit FFX_Btl_LoadNextWeaponModel tries. The high byte of word_112A928,
               FFX_Btl_ResetLoadCounters writes it and 0x112A92A as one word
    0x112C022  1 at the first turn offer of a battle (sub phase 22). The low byte of word_112C022

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

    python apply_battle_loop_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x7842F0: ('FFX_Btl_RollMonsterGroupOffsets', 'For each monster slot 20..27 that owns its file and has later slots sharing it (primary unit +0x14), n = the members: n draws & 3 on the owner kind 0 stream make the offsets 8 * i + draw, then each member in slot order takes one at draw % the members left on its own kind 0 stream into +0x424. A formation with no shared file draws nothing. FFX_Btl_RegisterMonsterMotions, before FFX_Btl_InitCtbCounters. [V]'),
    0x783000: ('FFX_Btl_ResetLoadCounters', 'g_ffxBtlMonsterBindCounter = 0, the word at 0x112A929 (the next weapon model unit and the next monster file slot) = 0, dword_112CA60 = 0. FFX_Btl_BeginBattle. [V]'),
    0x783F60: ('FFX_Btl_LoadNextWeaponModel', 'Sub phase 18, then from the unit at 0x112A929 the next unit 0..17 on the field whose FFX_Btl_LoadWeaponModel goes in flight (returns -1 with the next unit stored), else sub phase 19 (0). The completion of FFX_Btl_LoadFootData and of each weapon model load. [V]'),
    0x784220: ('FFX_Btl_AnyUnitReactionBusy', '-1 when a unit 0..30 has the reaction busy byte +0x433, writing the unit and 0xD0 to word_112CA08, else 0. Sub phase 22 of FFX_Btl_StepSubPhase waits for 0. [V]'),
    0x7AF2B0: ('FFX_Btl_AnyRegenReactionBusy', '-1 when a unit is on the field (+0xDC8) with the reaction busy byte +0x433 and a Regen count +0x612, else 0. FFX_Btl_MainStep stores it in 0x112C9EA every step, FFX_Btl_IsCtbTickAllowed waits for 0. [V]'),
    0x791760: ('FFX_Btl_StartDefeatSequences', 'FFX_Btl_StartDefeatSequence(unit, 1) for every unit 0..30. FFX_Btl_MainStep after FFX_Btl_FinishActions. [V]'),
    0x7A2690: ('maybe_FFX_Btl_DispatchQueuedDefeats', 'While queued defeats are left (byte 1 of dword_112CA0A) and byte 0 is set (between commands) and fewer than 3 are running (byte 2): takes the head unit of dword_112C950, marks it in dword_112C90C and counts it running when sub_79E3D0(unit, 7) starts. FFX_Btl_MainStep. [V] for the reads, [I] the name'),
    0x79CDD0: ('FFX_Btl_ResetMessageRing', 'dword_112C910 (the message window busy flag) and dword_112C944 (the battle message count and ring head) = 0. FFX_Btl_BeginBattle. [V]'),
    0x7A2730: ('FFX_Btl_ResetDefeatQueue', 'dword_112CA0A (between commands, the queued defeats, those dispatched, the waited event 79 units) and dword_112C90C = 0. FFX_Btl_BeginBattle. [V]'),
    0x7AC650: ('FFX_Btl_SavePlaceToAlt', '(actor): copies the position key +0x4FC..+0x4FE to +0x509..+0x50B, the reverse of FFX_Btl_RestorePlaceFromAlt. The roster pass, FFX_Btl_SetupWeaponChr, FFX_Btl_PlaceArrivedAeon, FFX_Btl_PartyFlee, FFX_Btl_SwapUnitIntoPartySlot. [V]'),
    0x7AC680: ('FFX_Btl_SaveAllPlacesToAlt', 'FFX_Btl_SavePlaceToAlt for units 0..30. FFX_Btl_SetupFieldPointers right after FFX_Btl_SetupUnitRoster, so every turn retirement puts the roster key back. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x7B13C0: ('FFX_Btl_ChargeTurnOverdriveModes', 'was FFX_Btl_PlayTurnStartVoice. (unit, unit record): for a party unit 0..6, the overdrive mode counters a turn earns through FFX_Btl_BumpOverdriveModeCounter (13 always, 15 at an HP band of 1 or more, 16 with no other living party unit on the field) with FFX_Btl_AddOverdrive 3, 5 or 16 when that is the unit mode +0x5BB, then a further counter for a unit with no bad status. No voice. FFX_Btl_CtbTick calls it after it pushes a turn. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x112C9EA: ('g_ffxBtlRegenReactionBusy', 'FFX_Btl_AnyRegenReactionBusy, stored by FFX_Btl_MainStep every step. FFX_Btl_IsCtbTickAllowed waits for 0. [V]'),
    0x112C02B: ('g_ffxBtlOverdriveLearnPending', 'Set by FFX_Btl_BumpOverdriveModeCounter. FFX_Btl_IsCtbTickAllowed waits for 0. [V] for the gate'),
    0x112C9CC: ('maybe_g_ffxBtlStartModelsShown', '0 or 1 from sub phases 12 and 20 of FFX_Btl_StepSubPhase (0 under story progress 2510 with level hash 8767, 1 otherwise or with the help menu open). Read by sub_781650, not traced. [V] for the writes, [I] the name'),
    0x112CA64: ('g_ffxBtlStartHoldFrames', 'Counted down at the head of FFX_Btl_StepSubPhase, sub phase 22 waits for 0 before the first turn. sub_7F9280 sets 90. [V]'),
    0x112A92A: ('g_ffxBtlMonsterLoadNext', 'The next monster slot 0..7 FFX_Btl_LoadMonsterData reads a file for, zeroed by FFX_Btl_ResetLoadCounters. [V]'),
    0x112C9D9: ('g_ffxBtlStartMode', 'The mode argument of FFX_Btl_BeginBattle. 0 a battle, FFX_Btl_SetupFieldPointers sets up the roster and the units only in mode 0. [V]'),
    0x112CA78: ('maybe_g_ffxBtlMonsterNameVariant', '1 for a battle field file other than 258..260 (sub phase 5 of FFX_Btl_StepSubPhase), FFX_Btl_LoadMonsterData clamps it to 1. Reader not traced. [V] for the writes, [I] the name'),
    0x112C904: ('g_ffxBtlPadButtons', 'FFX_Pad__readButtons16(0, 0, 0) at the head of the FFX_Btl_MainStep body. The debug win and restart combinations read it. [V]'),
    0x112C908: ('g_ffxBtlPadButtonsPrev', 'g_ffxBtlPadButtons copied at the end of FFX_Btl_MainStep. [V]'),
    0x112C90C: ('g_ffxBtlDefeatDispatchedMask', 'Bit n set when maybe_FFX_Btl_DispatchQueuedDefeats takes unit n off the queue, zeroed by FFX_Btl_ResetDefeatQueue. [V]'),
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
