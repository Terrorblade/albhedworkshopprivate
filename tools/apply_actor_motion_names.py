"""
Apply the actor motion names from the projectzanarkand 11.10 port (src/engine/atel/ActorMotion.cpp)
to the FFX.exe IDB.

The port of MOVE, ROT and FFX_Atel_StepActor 0x8666E0 needed the math helpers, the spline
evaluators, the MOVE and ROT stops and a handful of actor setters that were still sub_ names. Each
one was read in the disassembly by the porting agent and the semantics spot checked again before
these rows were written.

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters. The script never saves the
database, save it yourself.

    python apply_actor_motion_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
COMMENTS = False
VERBOSE = False

# ---------------------------------------------------------------------------------------------
# Functions whose current name is a placeholder
# ---------------------------------------------------------------------------------------------

NAMES = {
    # float math, 0x88xxxx
    0x88C3A0: ("FFX_Math_Sinf", "(float)sin(a1)"),
    0x88B3E0: ("FFX_Math_Cosf", "(float)cos(a1)"),
    0x88BC20: ("FFX_Math_WrapAngleToPiF", "adds or subtracts 2pi in float until a1 is in [-pi, pi]. Loops, so a NaN or huge value never leaves"),
    0x88B400: ("FFX_Math_MinAbs", "min(fabs(a1), a2), a2 wins ties. Only the first argument is made absolute"),
    0x88B430: ("FFX_Math_YawToPointXZ", "the yaw from one point to another on XZ, used by the MOVE face and the ROT aim"),
    0x88B4D0: ("FFX_Math_YawPitchToPoint", "yaw and pitch from one point to another, the 3D MOVE kinds and the ROT aim"),
    0x88B570: ("FFX_Math_ApproachAngle", "steps an angle toward a target by at most a rate, the shorter way round"),
    0x88B610: ("FFX_Math_ApproachAngleDirected", "steps an angle toward a target by at most a rate in a fixed direction, the ROT 0x400 and 0x800 turns"),
    0x88B6B0: ("FFX_Math_StepAngleOneWay", "one way angle step used by the ROT one way turn"),
    0x88A3A0: ("FFX_Math_PolarToVector", "out[0..3] from length a2, yaw a3, pitch a4. Folds a pitch past +-pi/2 into yaw + pi first, w = 1"),
    0x88AFD0: ("FFX_Math_SplineQuadratic", "evaluates the quadratic MOVE spline at t over the point list, a5 picks the variant. A count of 1 or less writes nothing"),
    0x88ABD0: ("FFX_Math_SplineQuadraticSegment", "one segment of FFX_Math_SplineQuadratic"),
    0x88AA40: ("FFX_Math_SplineCubic", "evaluates the cubic MOVE spline at t over the point list, a5 picks the variant. A count of 1 or less writes nothing"),
    0x88A500: ("FFX_Math_SplineCubicSegment", "one segment of FFX_Math_SplineCubic"),
    0x88A4A0: ("FFX_Math_SplineQuadratic_A5One", "FFX_Math_SplineQuadratic(a1, a2, a3, a4, 1)"),
    0x88A4D0: ("FFX_Math_SplineCubic_A5One", "FFX_Math_SplineCubic(a1, a2, a3, a4, 1)"),
    # ATEL actor motion
    0x862B20: ("FFX_Atel_EvalMoveSpline2", "MOVE spline kind evaluator over FFX_Math_SplineQuadratic_A5One"),
    0x862B70: ("FFX_Atel_EvalMoveSpline3", "MOVE spline kind evaluator over FFX_Math_SplineCubic_A5One"),
    0x863E10: ("FFX_Atel_StopMoveCmd", "stops MOVE level a2 (a2 < 0 means the current level at +50, clamped to 8, or 1 for actor types 5 and 6), frees every sharer and copies the goal. core:119"),
    0x863FE0: ("FFX_Atel_StopRotCmd", "stops ROT level a2, the ROT half of FFX_Atel_StopMoveCmd. core:120"),
    0x86F8A0: ("FFX_Atel_AimRotCmdAtActor", "points the ROT command at another actor's position"),
    0x86F7F0: ("FFX_Atel_AimRotCmdAtPoint", "points the ROT command at a fixed point"),
    0x86DB80: ("FFX_Atel_CaptureRotIntoRotCmd", "copies the actor's current rotation into the ROT command as its start"),
    0x872160: ("FFX_Atel_PushPartyIndexToChr", "writes the actor's party index into the bound CHR +0x18C"),
    0x862780: ("FFX_Atel_ReleaseBoneChild", "detaches a CHR that was attached to a bone of the actor's CHR"),
    0x885FD0: ("FFX_Atel_SetByte184Owner", "writes the bound CHR +0x184 owner byte"),
    0x862700: ("FFX_Atel_ApplyRenderMode", "applies the actor's render mode to its CHR, presentation only"),
    0x86F4C0: ("FFX_Atel_PushToMapObject", "pushes the actor position to its map object instead of a CHR (attach kinds 4 and 5)"),
    0x8712A0: ("FFX_Atel_SetActorBindable", "sets or clears bit 0x200 of actor flags +0x34 and returns the bit"),
    0x86F080: ("FFX_Atel_SetAllMoveCmdFrames", "writes a2 to +4 of all nine MOVE commands (+0x5B8, stride 76). core:116"),
    0x86F120: ("FFX_Atel_SetAllRotCmdFrames", "writes a2 to +4 of all nine ROT commands (+0x864, stride 68). core:117"),
    0x871170: ("FFX_Atel_SetRotCmdFrames", "writes a3 to +4 of ROT command a2 (+0x864, stride 68). core:115"),
    # callees outside ATEL that the port reaches through its environment
    0x6427E0: ("maybe_FFX_GfxCtx_GetLevelNameHashB", "returns gfx+0xFAF8, the only writer is FFX_GfxCtx_SetLevelNameHash at 0x66CF99. FFX_GfxCtx_GetLevelNameHash 0x65DE90 reads +0xFAF4 instead, so one of those names is off. FFX_Atel_PushActorPosToChr tests both hashes for 0x1CD6"),
    0x7BA9C0: ("FFX_Came_ReleaseActorCameraHandle", "clears the actor's camera handle out of the map, event and battle handle tables. core:413 on a camera attach"),
    0x90A830: ("FFX_MapObj_SetTransformById", "finds every map object whose id field matches a1 and writes rotation a2, position a3 and the rotation matrix. FFX_Atel_PushToMapObject for attach kind 4"),
    0x90A750: ("FFX_MapObj_SetTransformByIndex", "writes rotation a2 and position a3 to map object index a1 when a1 is below the count. FFX_Atel_PushToMapObject for attach kind 5"),
    0x7FB360: ("maybe_FFX_Render_LinkChrToEffectUnit", "registers the current effect unit in a free slot of the first unit, then finds CHR a1 in g_ffxChrShadowRegistry and links it in g_ffxEffectLinkRecords. Called by FFX_Atel_ApplyRenderMode, presentation"),
    0x7FB5A0: ("maybe_FFX_Render_ClearChrModeBit8", "finds CHR a1 in g_ffxChrShadowRegistry and, when its entry has 0x10, clears bit 8 of its per-slot render byte and returns whether it was clear. Called by FFX_Atel_ApplyRenderMode, presentation"),
}

RENAMES = {}

GLOBALS = {}

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
