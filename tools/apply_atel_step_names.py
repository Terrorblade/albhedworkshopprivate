"""
Apply the names from the 2026-10-29 FFX_Atel_StepOnce wiring (Project Zanarkand src/engine/atel/AtelStep,
WarpSyscalls, src/engine/map/MapZone and FieldScene, PROGRESS 12.3 and 12.6) to the FFX.exe IDB.

The callees of FFX_Atel_StepOnce 0x88D3D0 and FFX_Atel_StepFieldAll 0x872BD0 that had no name: the pad
vibration channels and their publish to pad port 0, the field sound source refresh and its fade, the
step gate stubs, and the pending zone reset. Read in the disassembly for this port. Names that rest on
use carry maybe_.

Two corrections ride along. flt_1328310 is the field sound source fade rate, not a screen fade: its
only reader FFX_Snd_StepFieldSources 0x877380 adds it to flt_132830C, whose only reader 0x876950 scales
every field sound source volume. So 0x877450 is renamed whichever of its two names it has now
(apply_mapchange_names.py used to propose FFX_Atel_SetScreenFadeRate). And the 8 records at 0x1328150
are vibration channels, not text overlays: 0x875B40 takes the per layer maximum over them and
FFX_Atel_StepFieldAll writes it doubled into pad port 0's +0xFF and +0xFE, the rumble bytes.

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
meantime is never overwritten. 0x877450 is in NAMES and RENAMES both, so whichever name it has now,
one row applies and the other is skipped.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_atel_step_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x875B40: ('FFX_Pad_StepVibrationChannels', 'Unless g_ffxPadVibrationPaused: for each of the 8 channels in g_ffxPadVibrationChannelMask, ends a ramp that reached its end (state 1 at 512, state 2 at 0xFFFE), steps both layers through 0x875C20 and keeps the largest level of each layer in g_ffxPadVibrationLevel0 and 1. Called by FFX_Atel_StepFieldAll 0x872C27 and FFX_Pad_StepAndPublishVibration. [V] body, [I] that the layers are the two motors'),
    0x875C20: ('maybe_FFX_Pad_StepVibrationLayer', 'One layer (a2 0 or 1, 16 bytes at +16*a2) of one vibration channel: 0 unless the channel state +34 is 1 or 2, the held level +8 while the ramp position +6 is negative, else 0x875C90 interpolates. Stores the level at +10 and returns it. [V] body, [I] name'),
    0x875E10: ('FFX_Pad_GetVibrationLevels', 'out a1 = g_ffxPadVibrationLevel0, out a2 = g_ffxPadVibrationLevel1, both 0 while g_ffxPadVibrationPaused. [V]'),
    0x875E80: ('FFX_Pad_ResetVibrationChannels', 'From FFX_Atel_Init: the 8 channels and both layers cleared, g_ffxPadVibrationPaused = 0, g_ffxPadVibrationChannelMask = 255. [V]'),
    0x876050: ('maybe_FFX_Pad_StartVibrationRamp', '(channel 0..7, layer 0..1, from, to, frames): levels clamped to 0..127, a negative from keeps the current level, frames negative means -1, state 2. Reached from core:472 (ATEL_CORE_LIBRARY.md read it as a text overlay fade). [V] body, [I] that it is rumble'),
    0x8739A0: ('FFX_Pad_StepAndPublishVibration', 'The menu side twin of the vibration block in FFX_Atel_StepFieldAll: FFX_Pad_StepVibrationChannels when a1 bit 0, then the two levels doubled into pad port 0 +0xFF and +0xFE. Three calls in FFX_MenuSys_StepFrame. [V]'),
    0x889490: ('FFX_Pad_SetVibrationMotor0', 'FFX_Pad__getPortState(a1, a2)[255] = a3. FFX_Atel_StepFieldAll passes 2 * g_ffxPadVibrationLevel0 on port 0 (0x872C43). Never sent on PC, FFX_Pad__sampleIntoPortState only calls setRumble when port +0x9D is set and the reset leaves it 0. [V]'),
    0x8894B0: ('FFX_Pad_SetVibrationMotor1', 'FFX_Pad__getPortState(a1, a2)[254] = a3, 2 * g_ffxPadVibrationLevel1 from FFX_Atel_StepFieldAll (0x872C52). [V]'),
    0x887DF0: ('FFX_Snd_IsSourceRefreshPending', 'Returns g_ffxSndSourceRefreshPending. FFX_Atel_StepFieldAll 0x872C64 steps the field sound sources and the indoor state only while it is set. [V]'),
    0x887D50: ('FFX_Snd_ClearSourceRefresh', 'g_ffxSndSourceRefreshPending = 0, the last call of FFX_Atel_StepFieldAll (0x872C77). [V]'),
    0x887EC0: ('FFX_Snd_RequestSourceRefresh', 'g_ffxSndSourceRefreshPending = 1. From FFX_Voice_RequestRefresh (every FFX_MainStep at 0x821612, maybe_FFX_Map_Unload twice) and the sound init 0x887E30. [V]'),
    0x877380: ('FFX_Snd_StepFieldSources', 'g_ffxSndFieldSourceFadeLevel += g_ffxSndFieldSourceFadeRate clamped to 0..1, FFX_Snd_CaptureListenerMatrices, then FFX_Snd_UpdateFieldSource(rec, 0, 256) for the 16 records from 0x1328318. Only from FFX_Atel_StepFieldAll 0x872C6D while the refresh is pending. [V]'),
    0x877470: ('FFX_Snd_CaptureListenerMatrices', 'FFX_Came_GetViewMatrix into 0x13282C8, then 0x1328288 a copy of it, or with a bound player on context 0 the view inverted, its translation row overwritten by the player actor position, and inverted back (the listener at the player). 0x1328308 = -1 so the next source update reloads its matrix. [V] body, [I] name'),
    0x876950: ('FFX_Snd_UpdateFieldSource', '(rec, isVoice, level): one positional sound source record. The position (the record or an actor, flag bit 1), the distance and yaw in the listener space of FFX_Snd_CaptureListenerMatrices, the volume from the record curve times g_ffxSndFieldSourceFadeLevel clamped 0..127, the pan 0..127, then FFX_Snd_SetSeVolumeById and SetSePanById, or 0x886E50 when a2 is set. Stores pan +32 and volume +33. [V] body, [I] the voice reading of a2'),
    0x877450: ('FFX_Snd_SetFieldSourceFadeRate', 'g_ffxSndFieldSourceFadeRate = a1. The warps pass -1/15 (fade the field sounds out over 15 refreshes), core:323 and 324 the fade out and in, FFX_Atel_StartEventSe and the unload their own. Not a screen fade. [V]'),
    0x877460: ('FFX_Snd_SetFieldSourceFadeLevel', 'g_ffxSndFieldSourceFadeLevel = a1. FFX_Atel_StartEventSe and maybe_FFX_Map_Unload (0). [V]'),
    0x877330: ('FFX_Snd_ResetFieldSources', 'The 16 field sound source records at 0x1328318 cleared (+0, +4, +32), level and rate 0. maybe_FFX_Map_Unload 0x872E27 and 0x877420. [V]'),
    0x873C30: ('FFX_Atel_StepOnceGateClosedStub', 'Empty. FFX_Atel_StepOnce calls it with maybe_g_ffxAtelStepGate 0 (0x88D455). [V]'),
    0x863510: ('FFX_Atel_StepOnceGateOpenStub', 'Empty, called with 1 by FFX_Atel_StepOnce after maybe_g_ffxAtelStepGate = 1 (0x88D466). [V]'),
    0x88E600: ('FFX_Atel_GetStepGate', 'Returns maybe_g_ffxAtelStepGate. Only caller FFX_Atel__samplePadsBothPorts 0x871D70, so between the two writes in FFX_Atel_StepOnce nothing reads it. [V]'),
    0x872B90: ('FFX_Map_ResetPendingZone', 'g_ffxPendingZoneMode = 0, g_ffxPendingZoneNotify = g_ffxPendingZonePlain = -1. From FFX_Atel_Init, FFX_Ev_LoadEventPackage has the same three stores inline (0x873472). [V]'),
}

# ---------------------------------------------------------------------------------------------
# Renames of names that are already descriptive
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x877450: ('FFX_Snd_SetFieldSourceFadeRate', 'was FFX_Atel_SetScreenFadeRate. flt_1328310 only feeds the field sound source fade level, whose only reader 0x876950 scales the source volumes. Not a screen fade. [V]'),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0x13301EC: ('g_ffxSndSourceRefreshPending', 'Set by FFX_Snd_RequestSourceRefresh (from FFX_Voice_RequestRefresh, every FFX_MainStep), read and cleared by FFX_Atel_StepFieldAll, which steps the field sound sources and the indoor state while it is set. Boots 0. [V]'),
    0x132830C: ('g_ffxSndFieldSourceFadeLevel', '0..1, stepped by g_ffxSndFieldSourceFadeRate in FFX_Snd_StepFieldSources, multiplies every field sound source volume in FFX_Snd_UpdateFieldSource. [V]'),
    0x1328310: ('g_ffxSndFieldSourceFadeRate', 'Per refresh step of g_ffxSndFieldSourceFadeLevel. The warps set -1/15, core:323 -1/(frames/2), core:324 1/(frames/2). [V]'),
    0x1328150: ('g_ffxPadVibrationChannels', '8 channels of 36 bytes: two 16 byte layers (+6 a ramp word, +8 from, +9 to, +10 the level), +32 a start counter, +34 the state (1 or 2 while ramping). Stepped by FFX_Pad_StepVibrationChannels. ATEL_CORE_LIBRARY.md called them text overlays. [V] stride and the level, [I] the ramp fields and that they are rumble'),
    0x1328270: ('g_ffxPadVibrationLevel0', 'The largest layer 0 level over the channels, doubled into pad port 0 +0xFF. [V]'),
    0x1328271: ('g_ffxPadVibrationLevel1', 'The largest layer 1 level over the channels, doubled into pad port 0 +0xFE. [V]'),
    0x1328272: ('g_ffxPadVibrationPaused', 'While set FFX_Pad_StepVibrationChannels does nothing and FFX_Pad_GetVibrationLevels reads 0. Written by FFX_Pad_ResetVibrationChannels (0) and the setter at 0x875F50. [V]'),
    0x1328274: ('g_ffxPadVibrationChannelMask', 'A bit per channel, 255 after FFX_Pad_ResetVibrationChannels. [V]'),
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
