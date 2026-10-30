"""
Apply the names from the 2026-10-29 battle camera port (Project Zanarkand src/engine/came/BattleCamera
and src/ffx/btl/BattleCameraSyscalls, PROGRESS 13.12) to the FFX.exe IDB.

FFX_Came_ModeBattle_Step 0x7C58E0 runs modes 11 bpos, 12 bpl1, 13 bpl2, 15 cpl1 and 16 cpl2 from what
FFX_Came_SetBattleTarget 0x7C5C60 stored. Named here are the camera target query it asks (the battle
unit path and the ATEL actor path), the unit values that query reads, the two row bodies the came:62 to
77 rows share, and the battle frame gate FFX_Came_StepAll checks before FFX_Came_StepBattleBoneSnap.
Every name resting on use rather than on the instructions carries maybe_.

Left out on purpose because another script owns the name: 0x79DFE0 FFX_Came_StepBattleBoneSnap and
0x7A73D0 Btl 160 (apply_party_field_ai_names.py), 0x794330 FFX_BtlAi_ResolveTargetSelector
(apply_monster_ai_names.py). Rows 63 to 77 already carry descriptive names and keep them.

Left out because they were not traced far enough: dword_11334D0 and dword_1136B58. The 0x7BADB0 body
counts dword_11334D0 down over chr[count..1], setting FFX_Ch_SetHideBit10 on each, then writes
dword_1136B58 0, and only for encounter 0xD40000 under an exact compare that never holds on PC (see
FFX_AtelSys_Came_SetBattlePolarRow). sub_7BA3C0 and sub_7BD420, the came:3 helpers, were not read.

These bytes sit inside a larger item, so they need the item undefined and redefined before they can
take a name. They are not in GLOBALS.

    0x112C022  the battle frame gate, 0x79DD70 reads the low byte of word_112C022 and wants 1
    0x112A8E6  the battle frame kind, the low byte of word_112A8E6 (1 rotations by the stored angles,
               2 and 3 the matrix at dword_11360A4 + 1984). 0x79DD70 answers 0 unless its high byte
               0x112A8E7 is 0
    0x112C9FC  the bone snap byte Btl 160 sets, already listed by apply_party_field_ai_names.py

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

    python apply_battle_camera_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    0x795170: ('FFX_Btl_QueryCameraTargetMean', '(kind, mask, raw, out): with 0x100 in kind, FFX_BtlEv_QueryActorCameraMean on raw. Otherwise out starts as qword_C8F508 (zero at run time), each unit 0..30 of mask (sar and bit 0, so bit 31 is never read) adds FFX_Btl_GetUnitCameraValue through FFX_Came_ApplyShake, and a non zero count scales out by fl(1 / fl(count)) through FFX_Vec_ScaleByInverse. Kinds 7, 9, 10 and 11 are the ones FFX_Came_ModeBattle_Step asks. Returns the count. Draws no RNG. [V]'),
    0x794060: ('FFX_Btl_GetUnitCameraValue', '(unit, kind, out, firstX): 9 the camera point 0x794AB0, 10 the height 0x794A30, 11 the distance 0x794A70, 7 the facing unwrapped near firstX by 0x7958C0. [V]'),
    0x794AB0: ('FFX_Btl_GetUnitCameraPoint', '(unit, out): the translation of the CHR root matrix override when there is one, else the CHR position (or the bone it rides on), Y moved to +0xF70 for a downed monster and up by half the unit height for a flier or an airborne field. [V] for the paths'),
    0x794A30: ('FFX_Btl_GetUnitCameraHeight', '(unit): fl(CHR +0x4F4 * unit +0x3A4), CHRDATA m_rootPointLift (section 9 +8) times the unit Y scale, 0 with no CHR. [V]'),
    0x794A70: ('FFX_Btl_GetUnitCameraDistance', '(unit): fl(CHR +0x4EC * unit +0x3A8), CHRDATA +0x24 (section 9 +24) times the unit Z scale, 0 with no CHR. [V]'),
    0x7958C0: ('FFX_Btl_GetUnitFacingNear', '(unit, near): the unit facing (+0x53C while +0x510 is set, else +0x49C) unwrapped to within pi of near. 0 for an invalid unit. [V]'),
    0x7975C0: ('FFX_BtlEv_QueryActorCameraMean', '(raw, kind, out): -21 and -5 every actor below FFX_Atel_GetChrActorCount that maybe_FFX_Atel_IsCameraTargetActor passes, -2 the player, else that actor id, each through FFX_BtlEv_GetActorCameraValue. Returns 1 with the mean (w 0) when any was taken, else 0 with out untouched. [V]'),
    0x7984C0: ('FFX_BtlEv_GetActorCameraValue', '(actor, kind, out): kinds 1, 8 and 9 the actor position, 3 and 10 18.0, 4 and 11 10.0, 7 the yaw plus pi, 2 the yaw, added to out x, y and z (z as v.z + acc.z). The scalar kinds leave stack garbage in y and z, nothing reads it. [V]'),
    0x86D950: ('maybe_FFX_Atel_IsCameraTargetActor', '(actor): actor +170 is 1 and +156 is not 0. FFX_BtlEv_QueryActorCameraMean takes the actors it passes for -5 and -21. [V] for the test, [I] the name'),
    0x7C2520: ('FFX_Vec_ScaleByInverse', '(out, in, n): out = in * fl(1 / n) on all four lanes. FFX_Btl_QueryCameraTargetMean divides its sum by the count with it. [V]'),
    0x7BAD10: ('FFX_AtelSys_Came_SetBattleBposRow', '(work, sub): came:62 (sub 1) and 63 (sub 0). The handle is the actor +0x3C, else FFX_Came_GetHandleForClass(5, 2, 0) cached there. Pops zOfs, target 1, target 0, then FFX_Came_SetBattleTarget kind 0 with zOfs / 100. [V]'),
    0x7BADB0: ('FFX_AtelSys_Came_SetBattlePolarRow', '(work, sub, kind): came:64 (1, 1), 65 (0, 1), 68 (1, 2), 69 (0, 2), 76 (1, 3), 77 (1, 4). Pops len, bDeg, aDeg, zOfs, target 1, target 0, converts to zOfs / 100, theta and phi in radians and len / 100, then the encounter specials, then FFX_Came_SetBattleTarget. The three exact compares (len * 100 against 228.99997 for 0x1A30004, zOfs * 100 against 3.999999 for 0x1F60004 and 88.99999 for 0xD40000) never hold under the PC x87 at 53 bits. [V]'),
    0x79DD70: ('FFX_Btl_GetCameraFrameKind', 'The low byte of word_112A8E6 when the low byte of word_112C022 is 1 and the high byte of word_112A8E6 is 0, else 0. FFX_Came_StepAll runs FFX_Came_StepBattleBoneSnap on a non zero answer. [V]'),
    0x7B86D0: ('FFX_AtelSys_Came_062_CamSetBattleBpos_resi', 'FFX_AtelSys_Came_SetBattleBposRow(work, 1), returns 0. Its row 63 twin is FFX_AtelSys_Came_063_RefSetBattleBpos_resi. No shipped script calls it. [V]'),
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
    0xC8F508: ('maybe_g_ffxZeroVec4', 'Uninitialized .data with no direct writer, so zero at run time. FFX_Btl_QueryCameraTargetMean starts its sum from it, and the particle and part code reads it the same way. [V] for the reads, [I] that nothing writes it indirectly'),
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
