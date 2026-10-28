"""
Apply the Came syscall library names from reversing/ATEL_CAME_LIBRARY.md to the FFX.exe IDB.

The Came library is ATEL library 6, the camera, 138 rows of {start, poll, float getter, int getter}
at g_ffxAtelSysFuncLib06_Came 0xC43998, registered by FFX_Atel_Init 0x86D6D0 (push 6 at 0x86D835).
The dword after the last row, 0xC44238, is g_ffxCameMainNearLatch and not a row. Every handler without
a descriptive name carries a positional one, FFX_AtelSys_Came_NNN_<slot> with <slot> one of start,
poll, resf, resi. This script reads the table out of the IDB, and for every row in NAMES gives each
non-null slot the name

    FFX_AtelSys_Came_NNN_<Role>_<slot>        (default)
    FFX_AtelSys_Came_NNN_<Role>               (--no-slot-suffix, single slot rows only)

The positional prefix stays, because a (library, function) pair is the script ABI.

It refuses to touch a slot whose current name is not a positional name of that row or a
sub_/loc_/nullsub_ placeholder. A positional name is FFX_AtelSys_Came_NNN_<slot>, with or without the
zero padding, or FFX_AtelSys_Came_NNN alone on a single slot row. The int getter of row 70 is also
taken over from the exact signature misname _wcstoul_0. So the names an earlier pass gave
(MsCameraSetScrDpt, FFX_AtelCmd_CameScreenOpen and the like) are never overwritten. It also skips a
slot that is not the start of a function, a function that sits in more than one slot, and a new name
that already exists elsewhere.

Dead rows (no use in any shipped script) are left positional unless --include-dead is given.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. The script never saves the database, save it yourself afterwards.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_came_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--include-dead]
                               [--no-slot-suffix] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
INCLUDE_DEAD = False
SLOT_SUFFIX = True
VERBOSE = False

TABLE = 0xC43998
ROWS = 138
SLOTS = ("start", "poll", "resf", "resi")
PLACEHOLDER = re.compile(r"^(sub|loc|nullsub)_[0-9A-Fa-f]+$")
POSITIONAL = re.compile(r"^FFX_AtelSys_Came_0*(\d+)(?:_(start|poll|resf|resi))?$")
ROLE_OK = re.compile(r"^[A-Z][A-Za-z0-9]{1,47}$")
# A positional int getter that a signature pass misnamed (ATEL_CONTEXT.md, the misnamed handler table).
# An IDB that still carries this exact name gets it replaced, any other name on the slot is kept.
MISNAMED = {(70, "resi"): "_wcstoul_0"}

# index -> (role, dead, one line meaning). Generated from reversing/ATEL_CAME_LIBRARY.md. Rows 59, 78,
# 79, 117 and 120 keep their descriptive names (MsCameraSetScrDpt, FFX_AtelCmd_CameScreenOpen, Close,
# SetSlot and the thunk j_FFX_Came_GetScrDpt) and are not listed.
# BEGIN NAMES
NAMES = {
    0: ('SuspendCameraOf', False, "pops an actor id and sets byte +4 of that actor's handle entry (sub_7C0010), so FFX_Came_GetActivePrioSlot skips it until ResumeCameraOf"),
    1: ('ResumeCameraOf', False, "clears byte +4 of that actor's handle entry, the inverse of came:0"),
    2: ('CamSetPos', False, "sub_7BB6A0 -> FFX_Came_SetPos, mode 1 'pos' on the eye. Hard coded cases: level 1761 at story 260 and one exact position calls sub_79E2A0, level 8207 under magic 250 at one position sets dword_1136B58, scene 248 in battle 0x1390000 patches x"),
    3: ('CamGetPosToVars', False, 'sub_7BD420: copies the committed eye position (sub-block +0x20) of screen 0 into three script variables'),
    4: ('CamSetPolar', False, "sub_7BF700, mode 2 'pol': the eye at a polar offset (degrees to radians) from the look-at point"),
    5: ('CamAddPolar', False, "sub_7BF7F0, mode 7 'polo': adds the polar offset to the current one"),
    6: ('CamSetHyp1', False, "FFX_Came_SetHyp1, mode 4 'hyp1', the third value in degrees"),
    7: ('CamSetHyp2', False, "FFX_Came_SetHyp23 with 0, mode 5 'hyp2'"),
    8: ('CamSetHyp3', False, "FFX_Came_SetHyp23 with 1, mode 6 'hyp3'"),
    9: ('CamSetTargetActors', True, "FFX_AtelSys_Came_SetTargetActors on the eye, mode 3 'act'"),
    10: ('CamAddWallClamp', False, 'maybe_FFX_Came_AddLineFilter kind 0: keeps the eye on one side of an XZ line with an atan falloff k, up to 5 clamps per record'),
    11: ('CamAddWallClampScaled', True, 'as came:10, kind 2, the overshoot scaled by k'),
    12: ('CamAddHeightClamp', True, 'maybe_FFX_Came_AddPointFilter kind 1: keeps the eye Y inside a band with an atan falloff'),
    13: ('CamAddHeightClampScaled', True, 'as came:12, kind 3, the overshoot scaled by k'),
    14: ('CamSetClampsPaused', True, 'record+5 = on: FFX_Came_PublishPosToOtherSubBlock skips the clamps while it is set'),
    15: ('CamClearClamps', False, 'record+4 = 0, drops every clamp'),
    16: ('CamMoveLinear', False, 'sub_7BDE50: sets the three tween records of the sub-block (and the roll and ScrDpt ones for the eye) to linear (type 1) over frames, in xyz space (B+1 = 0)'),
    17: ('CamMoveLinearPolar', False, 'sub_7BDE50: sets the three tween records of the sub-block (and the roll and ScrDpt ones for the eye) to linear over frames, in polar space (B+1 = 1, the eye orbits)'),
    18: ('CamMoveCosine', False, 'sub_7BDE50: sets the three tween records of the sub-block (and the roll and ScrDpt ones for the eye) to cosine ease (type 3) over frames, xyz space'),
    19: ('CamMoveCosinePolar', False, 'sub_7BDE50: sets the three tween records of the sub-block (and the roll and ScrDpt ones for the eye) to cosine ease over frames, polar space'),
    20: ('CamMoveTrapezoid', False, 'sub_7BD860: trapezoid tween (type 2) on the axes record+7 does not mask, xyz space'),
    21: ('CamMoveTrapezoidPolar', False, 'sub_7BD860: trapezoid tween (type 2) on the axes record+7 does not mask, polar space'),
    22: ('CamStopMove', False, 'sub_7BED60: zeroes the tween counters and sets the pending eye to the committed one, so the move stops where it is. Under magic 146 it also counts calls in dword_C494C8 and on the second sets the dword_12FB7E0 latch that came:128 and btl:30 read'),
    23: ('CamSetDamping', False, 'sub_7BDDE0: record+112..124, the per axis divisors of the first order damper FFX_Came_StepAll runs at commit, 0 snaps'),
    24: ('CamSetSpeedLimit', True, 'sub_7BCE80: B+3 = 1, B+8 and B+12, StepAll then limits the step length'),
    25: ('CamClearSpeedLimit', False, 'sub_7BEC90: B+3 = 0 and the velocity at B+0x40 zeroed'),
    26: ('WaitCamMoveDone', False, "poll only, ebp.py's CameraWait: returns 0 while sub_7BDD20 is nonzero (a tween running, the damper moved this step, or a spline in progress), else 5. Prints a debug count while dword_12FB80C == 1"),
    27: ('GetCamMoveState', False, "sub_7BDD20 for the eye: 1 a tween (or the roll or ScrDpt tween) is running, 2 B+2, the damper moved this step, 4 the winning record is mode 14 'spln'"),
    28: ('CamFollowPointSetSq', False, "FFX_Came_SetPointFollow, mode 8 'pflw', the point set at a script variable address, weights 1/d squared (record+3 bit 0)"),
    29: ('CamFollowPointSetPolarSq', False, "FFX_Came_SetPointFollowPolar, mode 9 'phyp', 1/d squared"),
    30: ('CamFollowPointSet', False, 'as came:28 with 1/d weights'),
    31: ('CamFollowPointSetPolar', False, 'as came:29 with 1/d weights'),
    32: ('RefSetPos', False, 'came:2 on the look-at point, the same hard coded cases'),
    33: ('RefGetPosToVars', False, 'came:3 for the committed look-at point'),
    34: ('RefSetPolar', True, 'came:4 on the look-at point'),
    35: ('RefAddPolar', True, 'came:5 on the look-at point'),
    36: ('RefSetHyp1', True, 'came:6 on the look-at point'),
    37: ('RefSetHyp2', True, 'came:7 on the look-at point'),
    38: ('RefSetHyp3', True, 'came:8 on the look-at point'),
    39: ('RefSetTargetActors', False, "FFX_AtelSys_Came_SetTargetActors on the look-at point, mode 3 'act': the average of up to two ATEL actors' positions plus the offset, 0xFFFF unused"),
    40: ('RefAddWallClamp', False, 'came:10 on the look-at point'),
    41: ('RefAddWallClampScaled', False, 'came:11 on the look-at point'),
    42: ('RefAddHeightClamp', False, 'came:12 on the look-at point'),
    43: ('RefAddHeightClampScaled', False, 'came:13 on the look-at point'),
    44: ('RefSetClampsPaused', True, 'came:14 on the look-at point'),
    45: ('RefClearClamps', False, 'came:15 on the look-at point'),
    46: ('RefMoveLinear', False, 'came:16 on the look-at point'),
    47: ('RefMoveLinearPolar', True, 'came:17 on the look-at point'),
    48: ('RefMoveCosine', False, 'came:18 on the look-at point'),
    49: ('RefMoveCosinePolar', True, 'came:19 on the look-at point'),
    50: ('RefMoveTrapezoid', False, 'came:20 on the look-at point'),
    51: ('RefMoveTrapezoidPolar', False, 'came:21 on the look-at point'),
    52: ('RefStopMove', False, 'sub_7BED60 on the look-at point, without the magic 146 counter'),
    53: ('RefSetDamping', False, 'came:23 on the look-at point'),
    54: ('RefSetSpeedLimit', True, 'came:24 on the look-at point'),
    55: ('RefClearSpeedLimit', True, 'came:25 on the look-at point'),
    56: ('WaitRefMoveDone', False, 'poll only, came:26 for the look-at point, without the debug print'),
    57: ('GetRefMoveState', True, 'came:27 for the look-at point'),
    58: ('SetRoll', False, "sub_7BFA80: the roll target of the handle's record and slot +0xC94, snapped into +0xC90 when no roll tween runs"),
    60: ('CamSetTargetActorHeight', False, "FFX_AtelSys_Came_SetTargetActorHeight on the eye, mode 10 'act2'. The last pushed value is popped and dropped"),
    61: ('RefSetTargetActorHeight', True, 'came:60 on the look-at point'),
    62: ('CamSetBattleBpos', True, "sub_7BAD10 -> FFX_Came_SetBattleTarget kind 0, mode 11 'bpos', zOfs / 100, battle handle"),
    63: ('RefSetBattleBpos', False, 'came:62 on the look-at point'),
    64: ('CamSetBattleBpl1', False, "FFX_Came_SetBattleTarget through sub_7BADB0 with the battle handle (class 5), kind 1, mode 12 'bpl1', on the eye. zOfs and len / 100. sub_7BADB0 patches the arguments for magic ids 88, 146, 163, 201, 704, 705 and 76/311 on some battles (sub_782970), and at level 8207 under magic 251 hides the CHRs queued by dword_1136B58"),
    65: ('RefSetBattleBpl1', False, 'came:64 on the look-at point'),
    66: ('RefSetMoveMask', False, 'sub_7BDF90: record+7 = ~mask, the axes (1 x, 2 y, 4 z, 8 roll, 0x10 ScrDpt) the next move tweens. Cleared after each move'),
    67: ('CamSetMoveMask', False, 'came:66 on the eye'),
    68: ('CamSetBattleBpl2', False, "FFX_Came_SetBattleTarget through sub_7BADB0 with the battle handle (class 5), kind 2, mode 13 'bpl2', on the eye"),
    69: ('RefSetBattleBpl2', False, 'came:68 on the look-at point'),
    70: ('CamSetSpline', False, "sub_7BB970 -> FFX_Came_SetSpline, mode 14 'spln'. points 0 or 1 take the recorded path of came:73/75 (3 points at least), anything else is a script variable address"),
    71: ('RefSetSpline', False, 'came:70 on the look-at point'),
    72: ('CamPathClear', False, 'dword_1136B64 = 0 when the own handle is the active one, empties the eye path buffer'),
    73: ('CamPathAddPoint', False, 'sub_7BAAC0: appends the pending eye position to the 32 entry path buffer at 0x1136B68, active handle only'),
    74: ('RefPathClear', False, 'came:72 for the look-at path, dword_1136B60'),
    75: ('RefPathAddPoint', False, 'came:73 for the look-at path'),
    76: ('CamSetBattleCpl1', False, "FFX_Came_SetBattleTarget through sub_7BADB0 with the battle handle (class 5), kind 3, mode 15 'cpl1', on the eye"),
    77: ('CamSetBattleCpl2', False, "FFX_Came_SetBattleTarget through sub_7BADB0 with the battle handle (class 5), kind 4, mode 16 'cpl2', on the eye"),
    80: ('SetScreenDrwRect', False, 'sub_7BC750: the DRW target (screen +0x90) and rect (+0x50)'),
    81: ('SetScreenDrwMode', False, 'sub_7BBDD0: screen +0xC3, 1 take size, 2 copy rect, 3 centre'),
    82: ('SetScreenWinMode', False, 'screen +0xC2, the WIN twin of came:81'),
    83: ('ScreenWinTweenLinear', True, 'sub_7BBDF0: the four WIN rect tweens, linear over frames'),
    84: ('ScreenWinTweenCosine', True, 'as came:83, cosine ease'),
    85: ('ScreenWinTweenTrapezoid', False, 'sub_7BBEB0: the four WIN rect tweens, trapezoid'),
    86: ('ScreenDrwTweenLinear', True, 'came:83 for the DRW rect'),
    87: ('ScreenDrwTweenCosine', True, 'came:84 for the DRW rect'),
    88: ('ScreenDrwTweenTrapezoid', False, 'came:85 for the DRW rect'),
    89: ('RefAddRailClamp', False, 'sub_7BFE70: a clamp of kind 4 (5 when kind is 3) that pulls the look-at point onto a spline through sub_A44120, step limit 0'),
    90: ('RefAddRailClampRate', False, 'as came:89 with the step limit (record+32) from the script'),
    91: ('CamSetSpline2', False, "FFX_Came_SetSpline2, mode 17 'spl2', on the eye, the same point sources as came:70"),
    92: ('RefShakeSineOut', False, 'sub_7C4FC0: arms the slot shake records picked by mask with wave type 1, sine fading out, look-at point. Active handle only'),
    93: ('CamShakeSineOut', False, 'came:92 on the eye'),
    94: ('ScreenShakeSineOut', False, 'FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 1, sine fading out, through FFX_AtelCmd_CameScreenShake'),
    95: ('RefClearShake', False, 'sub_7C4F50: clears the three slot shake records of the look-at point, active handle only'),
    96: ('CamClearShake', False, 'came:95 on the eye'),
    97: ('ClearScreenShake', False, 'FFX_Came_ClearScreenShake'),
    98: ('WaitRefShakeDone', False, 'poll only, returns 0 while sub_7C4F10 finds a live slot shake record on the look-at point, else 5'),
    99: ('WaitCamShakeDone', False, 'came:98 for the eye'),
    100: ('WaitScreenShakeDone', False, 'start stores the screen, poll returns 0 while FFX_Came_IsScreenShakeActive'),
    101: ('SetCameraPriority', False, 'sub_7BE0A0: the priority byte (+6) of every entry of the own handle, ignored while dword_2311424 is set (sub_7BE100, from FFX_Btl_BeginBattle)'),
    102: ('RefShakeSineIn', False, 'sub_7C4FC0: arms the slot shake records picked by mask with wave type 2, sine fading in, look-at point'),
    103: ('CamShakeSineIn', False, 'came:102 on the eye'),
    104: ('ScreenShakeSineIn', True, 'FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 2, sine fading in'),
    105: ('RefShakeMixOut', False, 'sub_7C4FC0: arms the slot shake records picked by mask with wave type 3, sin plus cos(t + 0.3) fading out'),
    106: ('CamShakeMixOut', True, 'came:105 on the eye'),
    107: ('ScreenShakeMixOut', False, 'FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 3'),
    108: ('RefShakeMixIn', False, 'sub_7C4FC0: arms the slot shake records picked by mask with wave type 4, sin plus cos(t + 0.3) fading in'),
    109: ('CamShakeMixIn', True, 'came:108 on the eye'),
    110: ('ScreenShakeMixIn', False, 'FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 4'),
    111: ('RefShakeStutterOut', True, 'sub_7C4FC0: arms the slot shake records picked by mask with wave type 5, fading out with a second stream 9 draw that halves the held amplitude at random'),
    112: ('CamShakeStutterOut', True, 'came:111 on the eye'),
    113: ('ScreenShakeStutterOut', False, 'FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 5'),
    114: ('RefShakeStutterIn', True, 'sub_7C4FC0: arms the slot shake records picked by mask with wave type 6, the fading in twin of type 5'),
    115: ('CamShakeStutterIn', True, 'came:114 on the eye'),
    116: ('ScreenShakeStutterIn', False, 'FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 6'),
    118: ('CloseBattleCameras', False, 'sub_7BA950: closes the battle and viewer handles of all three screens and sets screen +0xD0, the depth of field mode FFX_Came_ActivateScreen last sent, to -1 so the next activation reloads it'),
    119: ('GetRollDeg', False, 'FFX_Came_GetRoll (g_ffxCameActiveSlot +0xC90) unwrapped near 0 and turned into degrees. The Hex-Rays output drops the call result, the disassembly at 0x7B91AB keeps it'),
    121: ('StopScreenWinTween', True, 'sub_7BC010: zeroes the four WIN tweens and snaps the rect to its target'),
    122: ('StopScreenDrwTween', True, 'came:121 for the DRW rect'),
    123: ('WaitScreenWinTween', False, 'start stores the screen, poll returns 0 while sub_7BBFC0 counts a live WIN tween'),
    124: ('WaitScreenDrwTween', True, 'came:123 for the DRW rect'),
    125: ('SetScreenBlur', False, 'sub_7BCDD0 -> sub_82BEE0 when the own handle is active: the blur that SG_DebugWin_BlurProc and FFX_Btl_BeginBattle also drive, strength / 128'),
    126: ('SetDofEnabled', False, "FFX_Came_SetDofEnabled 0x7BCF50: slot +0xCAC (and the record copy), with +0xCA8 dirty. A negative value keeps the record's value with bit 2. FFX_Came_ActivateScreen hands it to FFX_Gfx_SetDofEnabled 0x82CB60"),
    127: ('SetDofDepth', False, "FFX_Came_SetDofDepth 0x7BF280 (was FFX_Came_SetRoll), which writes slot +0xCB0 + 4 * which and sets +0xCA8, the depth of field depths ActivateScreen passes to FFX_Gfx_SetDofDepth 0x82CE20. It does not touch the roll"),
    128: ('RandCamera', False, 'FFX_Rand_Stream(14 + (which & 1)) returned to the script. Under magic 146 with the came:22 latch set it returns 0 or 2 instead and clears the latch'),
    129: ('RefShakeSineSteady', False, 'sub_7C4FC0: arms the slot shake records picked by mask with wave type 7, sine with no envelope'),
    130: ('CamShakeSineSteady', False, 'came:129 on the eye'),
    131: ('RefShakeMixSteady', False, 'sub_7C4FC0: arms the slot shake records picked by mask with wave type 8, sin plus cos(t + 0.1) with no envelope'),
    132: ('CamShakeMixSteady', False, 'came:131 on the eye'),
    133: ('CamGetLivePosToVars', False, 'sub_7BD4B0: the live eye position (sub-block +0x10, committed plus shake) into three script variables'),
    134: ('RefGetLivePosToVars', False, 'came:133 for the live look-at point'),
    135: ('RefRestorePriorityPos', False, 'sub_7BEAE0: copies record+128, the position this priority last committed, into the committed and pending look-at point, active handle only'),
    136: ('CamRestorePriorityPos', False, "came:135 on the eye, plus the record's roll and ScrDpt"),
    137: ('ScreenSpringShake', True, 'FFX_Came_SetScreenSpringShake, type 9. Draws stream 9 in the setter when randDiv is nonzero'),
}
# END NAMES


def positional(i, slot):
    return "FFX_AtelSys_Came_%03d_%s" % (i, slot)


def is_positional(cur, i, slot, single):
    m = POSITIONAL.match(cur)
    if not m or int(m.group(1)) != i:
        return False
    if m.group(2) is None:
        return single
    return m.group(2) == slot


def main(dry_run=DRY_RUN, include_dead=INCLUDE_DEAD, slot_suffix=SLOT_SUFFIX, verbose=VERBOSE):
    import ida_bytes
    import ida_funcs
    import ida_name
    import idaapi
    import idc

    # every slot target, to find functions shared between slots
    owners = {}
    table = {}
    for i in range(ROWS):
        for k, slot in enumerate(SLOTS):
            ea = ida_bytes.get_dword(TABLE + 16 * i + 4 * k)
            if ea:
                table[(i, slot)] = ea
                owners.setdefault(ea, []).append((i, slot))

    plan = []
    skips = []
    kept = []
    for i in sorted(NAMES):
        role, dead, meaning = NAMES[i]
        if dead and not include_dead:
            continue
        if not role or not ROLE_OK.match(role):
            skips.append("came:%d bad role %r" % (i, role))
            continue
        slots = [s for s in SLOTS if (i, s) in table]
        if not slots:
            skips.append("came:%d null row, nothing to name" % i)
            continue
        for slot in slots:
            ea = table[(i, slot)]
            cur = idc.get_name(ea) or ""
            if slot_suffix or len(slots) > 1:
                new = "FFX_AtelSys_Came_%03d_%s_%s" % (i, role, slot)
            else:
                new = "FFX_AtelSys_Came_%03d_%s" % (i, role)
            if cur == new:
                kept.append("came:%d %s 0x%X already %s" % (i, slot, ea, cur))
                continue
            if (not is_positional(cur, i, slot, len(slots) == 1) and not PLACEHOLDER.match(cur)
                    and MISNAMED.get((i, slot)) != cur):
                skips.append("came:%d %s 0x%X keeps its name %s (not positional)" % (i, slot, ea, cur))
                continue
            f = ida_funcs.get_func(ea)
            if f is None or f.start_ea != ea:
                skips.append("came:%d %s 0x%X is not a function start, make one first" % (i, slot, ea))
                continue
            if len(owners[ea]) > 1:
                skips.append("came:%d %s 0x%X is shared by %s" % (
                    i, slot, ea, ", ".join("came:%d %s" % o for o in owners[ea])))
                continue
            other = ida_name.get_name_ea(idaapi.BADADDR, new)
            if other != idaapi.BADADDR and other != ea:
                skips.append("came:%d %s name %s already used at 0x%X" % (i, slot, new, other))
                continue
            plan.append((ea, cur, new, meaning))

    for line in kept:
        if verbose:
            print("ok    " + line)
    for line in skips:
        print("skip  " + line)
    for ea, cur, new, meaning in plan:
        print("name  0x%X %s -> %s%s" % (ea, cur, new, ("    " + meaning) if verbose else ""))
    print("%d to rename, %d skipped, %d already named, %d rows in NAMES"
          % (len(plan), len(skips), len(kept), len(NAMES)))

    if dry_run:
        print("DRY_RUN, nothing renamed")
        return True

    failed = 0
    for ea, cur, new, _ in plan:
        if not idc.set_name(ea, new, ida_name.SN_NOWARN | ida_name.SN_NOCHECK):
            print("FAILED 0x%X %s -> %s" % (ea, cur, new))
            failed += 1
    print("renamed %d, failed %d. The database is NOT saved, save it when you are happy"
          % (len(plan) - failed, failed))
    return failed == 0


def headless():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True, help="a COPY of the database")
    ap.add_argument("--apply", action="store_true", help="rehearse the renames, the database is not saved")
    ap.add_argument("--include-dead", action="store_true", help="also name rows no shipped script calls")
    ap.add_argument("--no-slot-suffix", action="store_true",
                    help="drop the _<slot> suffix on rows that have a single slot")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    import idapro
    if idapro.open_database(a.idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % a.idb)
    try:
        main(dry_run=not a.apply, include_dead=a.include_dead, slot_suffix=not a.no_slot_suffix,
             verbose=a.verbose)
    finally:
        idapro.close_database(save=False)


if __name__ == "__main__":
    if "idaapi" in sys.modules or "ida_kernwin" in sys.modules:
        main()
    else:
        headless()
