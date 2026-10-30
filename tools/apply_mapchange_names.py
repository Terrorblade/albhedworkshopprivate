"""
Apply the names from the 2026-10-29 map change port (Project Zanarkand src/engine/map/MapChange and
EventIdTable, PROGRESS 12.3) to the FFX.exe IDB.

The map change consumer, the warp fade rate and the map change hook, the getters the checkpoint
write reads, and the boot scene text step. The coordinator checked the consumer's delay rule, the
request writes and the title latch in the disassembly. Names that rest on use carry maybe_.

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

    python apply_mapchange_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x8863D0: ('FFX_Bgm_GetTrackVolume', 'Returns 0 for track -1, else g_ffxBgmVolumeByTrack[track]. Read by the checkpoint write in FFX_Scene_Init. [V]'),
    0x641400: ('maybe_FFX_Gfx_GetBgMovieWord', 'Returns maybe_g_ffxBgMovieWord 0xC322A8, which the checkpoint write stores + 1 at save +0xAA. [V] behaviour, [I] meaning'),
    # Corrected 2026-10-29: flt_1328310 is the field sound source fade rate, not a screen fade (apply_atel_step_names.py)
    0x877450: ('FFX_Snd_SetFieldSourceFadeRate', 'flt_1328310 = a1, the field sound source fade rate. Not a screen fade. [V]'),
    0x86E970: ('FFX_Map_SetChangeHook', 'Stores g_ffxMapChangeHook and its argument. Only caller maybe_FFX_TkHarness_Init_DEAD 0x87F0E6, so the hook is null in retail. [V], [I] that the harness is dead'),
    0x88D710: ('maybe_FFX_Tk_BootInit', 'The TK boot: pads reset, FFX_Map_RequestResumeFromCheckpoint, then the boot asset reads. [I]'),
    0x8B9220: ('maybe_FFX_BootScene_StepScriptedText', 'Advances the boot scene message window when the step count reaches the next value of its text table, indexed by g_ffxLiveSceneId - 393, and clears maybe_g_ffxBootSceneTextActive at the table end (0x8B92E0). [I]'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x88E070: ('FFX_Scene_Init', 'was maybe_FFX_Scene_Init. The map change consumer, called by FFX_Atel_StepOnce at 0x88D428 on call 1 for no delay and call n + 2 after FFX_Map_SetTransitionFrames(n). Tears down and rebuilds the scene synchronously, writes the checkpoint (0x88E239..0x88E298) unless the title latch g_ffxTitleExitCheckpointSuppress holds, and clears g_ffxMapChangePending (0x88E2DC). Read end to end for PROGRESS 12.3. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xB5BE88: ('g_ffxWarpFadeRate', '-1/15 (0xBD888889), read only by FFX_Map_WarpTo and FFX_Map_WarpToWithSavedFade. [V]'),
    0x1327070: ('g_ffxMapChangeHook', 'Called when set by both warps (0x86FEF2, 0x86FF76), zeroed by FFX_Atel_Init 0x86D7E4, set only by FFX_Map_SetChangeHook from the dead TK harness, so null in retail. [V]'),
    0x1327074: ('g_ffxMapChangeHookArg', 'Written beside g_ffxMapChangeHook by FFX_Map_SetChangeHook. [V] written, [I] never read on the play path'),
    0x18663D0: ('maybe_g_ffxBootSceneTextActive', 'Set by FFX_Scene_Init for a boot scene when clear, cleared by maybe_FFX_BootScene_StepScriptedText at 0x8B92E0 at its table end, so not first time only. [V] writers, [I] name'),
    0x18663D4: ('maybe_g_ffxBootSceneTextStepCount', 'Zeroed with maybe_g_ffxBootSceneTextActive, counted per call while it is 1, compared with the boot text table step words. [V] writers, [I] name'),
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
