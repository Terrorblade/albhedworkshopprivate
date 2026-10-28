"""
Apply the Ch syscall library names from reversing/ATEL_CH_LIBRARY.md to the FFX.exe IDB.

The Ch library is ATEL library 5 (not 1, library 1 is Sys), 145 rows of {start, poll, float getter,
int getter} at g_ffxAtelSysFuncLib05_Ch 0xC891F8, registered by FFX_Atel_Init 0x86D6D0. Every handler
without a descriptive name carries a positional one, FFX_AtelSys_Ch_NNN_<slot> with <slot> one of
start, poll, resf, resi. This script reads the table out of the IDB, and for every row in NAMES gives
each non-null slot the name

    FFX_AtelSys_Ch_NNN_<Role>_<slot>          (default)
    FFX_AtelSys_Ch_NNN_<Role>                 (--no-slot-suffix, single slot rows only)

The positional prefix stays, because a (library, function) pair is the script ABI.

It refuses to touch a slot whose current name is not a positional name of that row or a
sub_/loc_/nullsub_ placeholder. A positional name is FFX_AtelSys_Ch_NNN_<slot>, with or without the
zero padding, or FFX_AtelSys_Ch_NNN alone on a single slot row. The polls of rows 1 and 16 are also
taken over from the exact signature misnames __RTC_NumErrors_956 and _955. So the names an earlier
pass gave (FFX_EvScript_chSetNextHokan, chReadSystemMGRP and the like) are never overwritten. It also
skips a slot that is not the start of a function, a function that sits in more than one slot, and a
new name that already exists elsewhere.

Dead rows (no use in any shipped script) are left positional unless --include-dead is given.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. The script never saves the database, save it yourself afterwards.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_ch_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--include-dead]
                             [--no-slot-suffix] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
INCLUDE_DEAD = False
SLOT_SUFFIX = True
VERBOSE = False

TABLE = 0xC891F8
ROWS = 145
SLOTS = ("start", "poll", "resf", "resi")
PLACEHOLDER = re.compile(r"^(sub|loc|nullsub)_[0-9A-Fa-f]+$")
POSITIONAL = re.compile(r"^FFX_AtelSys_Ch_0*(\d+)(?:_(start|poll|resf|resi))?$")
ROLE_OK = re.compile(r"^[A-Z][A-Za-z0-9]{1,47}$")
# Two return 5 polls that a signature pass misnamed (ATEL_CONTEXT.md, the misnamed handler table). An
# IDB that still carries these exact names gets them replaced, any other name on these slots is kept.
MISNAMED = {(1, "poll"): "__RTC_NumErrors_956", (16, "poll"): "__RTC_NumErrors_955"}

# index -> (role, dead, one line meaning). Generated from reversing/ATEL_CH_LIBRARY.md. Rows 18 and 28
# are null in the table. Rows 12, 23, 31, 37, 40, 92, 93, 94 and 101 keep their descriptive names on
# every slot and are not listed.
# BEGIN NAMES
NAMES = {
    0: ('SetMotionKey', False, "FFX_Ch_SetMotionKey(self, motionId): requests motion key motionId on the actor's CHR, the plain play motion call"),
    1: ('LoadEventMotionGroupSync', False, 'start stores the group, poll is a return 5 stub, the getter calls FFX_Mot_LoadEventMotionGroupSync, which loads inside the step through FFX_Ch_RomPump(0)'),
    2: ('SetMotionKey', True, 'same body as ch:0'),
    3: ('WaitMotionDone', False, 'start resolves the CHR (result ignored), poll returns 0 while CHR +0x736 m_motionRequestActive is set, else 5. With no CHR it returns 0 for ever'),
    4: ('IsMotionActive', True, 'same body as ch:31'),
    5: ('StopMotion', False, 'FFX_Ch_ResetBlock730: m_i730 = 1, m_motionRequestActive = 0, m_clipPlaying = 0'),
    6: ('SetScale', False, 'FFX_Ch_SetScaleUniform'),
    7: ('SetScaleMasked', False, 'FFX_Ch_SetScaleMasked(mask, x, y, z), a per axis scale'),
    8: ('SetEventHidden', False, 'FFX_Ch_SetHideBit1: m_hideFlags bit 0x01 (Ev). Forced to 0 for chr 0x108A when sub_782970() == 0x2370000 and the level name hash is 0x21E8'),
    9: ('SetPushWeight', False, 'CHR +0x500 m_pushWeight, the share of a body overlap this CHR takes in FFX_Ch_SeparatePair'),
    10: ('SetUnreadFloat', True, 'writes CHR +0x4F8, which nothing reads except FFX_Ch_CopyState'),
    11: ('SetGroundHeight', False, 'FFX_Ch_SetGroundHeight'),
    13: ('SetEventHidden', False, 'same body as ch:8'),
    14: ('SetFreeMove', False, 'FFX_Ch_SetFlags1Bit80: m_flags1 0x80 = on. Set is free move, pos += vel with no walkmesh or ground clamp, ground normal forced to (0,-1,0) and idshade 0x40000 cleared. Clear follows the walkmesh and sets idshade'),
    15: ('SetNoBodyCollision', False, 'FFX_Ch_SetFlags1Bit100: m_flags1 0x100 = on. Set takes the CHR out of g_ffxChrCollideList'),
    16: ('ReadSystemMGRP', False, 'kept chReadSystemMGRP start and chReadSystemMGRPFinish getter, the poll is a return 5 stub. The getter runs FFX_Ch_LoadMotionSetSync(chrId, set), a sync load through FFX_Ch_RomPump(0)'),
    17: ('DisposeOwnMotionSet', True, 'FFX_Ch_MotionSetDispose(self->m_id, set)'),
    19: ('SetMotionMode', False, 'CHR +0x185 and FFX_Ch_SetLocomotionMode: 0 field, 1 field battle, 2 swim, 3 swim battle'),
    20: ('SetBrightness', False, 'FFX_Ch_AnimFloatSet on m_animBrightness (CHR +0x360), mode picks snap or ramp'),
    21: ('SetShadeColor', False, 'shade colour, each channel clamped to 1.3'),
    22: ('SetOffset', False, 'FFX_Ch_SetOffset(x, y, z)'),
    24: ('SetSpeedScale', False, 'FFX_Ch_SetSpeedMulA: m_speedMulA, 8.8 fixed point'),
    25: ('SetMotionTbl', False, "FFX_EvScript_chSetMotion helper in table mode, start and end are indexes into the motion's frame table"),
    26: ('SetMotionFrames', False, 'FFX_EvScript_chSetMotion helper with explicit frames. The helper patches m_jointComposerMode for some chr and motion pairs by scene (205, 364, 17) and story progress (2085, 1176, 200), and sets 0x13008E0 for chr 102'),
    27: ('SetMotionKey', False, 'same body as ch:0'),
    29: ('SetMotionKey', True, 'same body as ch:0'),
    30: ('WaitMotionDone', False, "empty start, the poll is ch:3's plus a counter dword_22FB4D8 that only runs when dword_12FB80C == 1, and nothing writes that"),
    32: ('StopMotion', False, 'same body as ch:5'),
    33: ('SetSpeedScale', False, 'same body as ch:24'),
    34: ('SetSeqWaitKey', False, 'CHR +0x737 m_seqWaitKey, the key a motion sequence waits on'),
    35: ('PopAndIgnore', True, 'pops one value and does nothing'),
    36: ('PopAndIgnore', True, 'pops one value and does nothing'),
    38: ('GetJointPosToVars', False, 'maybe_FFX_Ch_GetJointMatrix and writes the joint world position into three script variables'),
    39: ('GetJointPointToVars', True, 'as ch:38 with an offset point transformed by the joint'),
    41: ('SetShadowKind', False, 'CHR +0x183, read by FFX_Ch_BuildBlobShadow and FFX_Ch_ShadowPassAll. FFX_Ch_StepAll overrides it for some level and chr ids'),
    42: ('SetZClipEnabled', False, 'm_flags1 0x800 = on. Clear means the CHR is never distance culled'),
    43: ('Nop', True, 'resolves the CHR and returns 0, no pop'),
    44: ('AttachToParentJoint', False, 'FFX_Ch_AttachToParentJoint, actorId -1 detaches. The carry command'),
    45: ('AttachToParentBone', False, 'as ch:44 by bone id'),
    46: ('GetGroundType', True, "FFX_Map_GroundTypeOf on the CHR's triangle"),
    47: ('GetGroundTypeOf', True, 'as ch:46 for another actor'),
    48: ('GetGroundSound', True, "FFX_Map_GroundAttrGetSnd on the CHR's triangle"),
    49: ('GetGroundSoundOf', True, 'as ch:48 for another actor'),
    50: ('SetDefaultMotion', False, 'plays slot 0 of the current locomotion mode (SetMotionFromModeSlot0)'),
    51: ('SetAlpha', False, 'FFX_Ch_AnimFloatSet on m_animShadeA'),
    52: ('SetBlendFrames', False, 'FFX_Ch_SetBlendFrames'),
    53: ('GetMotionLength', False, 'FFX_Mot_SeqTotalFrames of the current motion'),
    54: ('GetBonePosToVars', False, 'sub_A7A980 with a4 = 0: the bone world position from m_boneWorldPos into three script variables. Only test23 calls it'),
    55: ('GetBone0PosToVars', False, 'as ch:54 for bone 0'),
    56: ('GetRootPointToVars', True, 'CHR +0x6B4 m_rootPoint into three script variables'),
    57: ('SetCollisionRadiusScale', False, 'FFX_Ch_SetCollisionRadiusScale'),
    58: ('SetClipZ', False, 'CHR +0x174 m_clipZ = z * 100'),
    59: ('DisposeMotionSet', False, "FFX_Ch_MotionSetDispose, only when chrId is not the actor's own CHR"),
    60: ('ReleaseEventMotionGroup', False, 'FFX_Mot_ReleaseEventMotionGroup'),
    61: ('DisposeChrData', False, 'disposes the CHR data of chrId, assert flag 1'),
    62: ('DisposeChrDataEx', False, 'as ch:61 with the assert flag from the script'),
    63: ('SetMotionKey', False, 'same body as ch:0'),
    64: ('SetMotionKey', False, 'same body as ch:0'),
    65: ('SetMotionTbl', True, 'same helper call as ch:25'),
    66: ('SetMotionFrames', False, 'same helper call as ch:26'),
    67: ('SetMotionTblOf', True, 'ch:25 on another actor'),
    68: ('SetMotionFramesOf', False, 'ch:26 on another actor'),
    69: ('SetMotionTblOf', True, 'same helper call as ch:67'),
    70: ('SetMotionFramesOf', False, 'same helper call as ch:68'),
    71: ('SetMotionKeyOf', False, 'ch:0 on another actor'),
    72: ('SetMotionKeyOf', False, 'same body as ch:71'),
    73: ('WaitMotionDoneOf', False, "start stores the other actor's raw CHR pointer in the thread work slot, poll waits on its +0x736"),
    74: ('IsMotionActiveOf', False, 'ch:31 on another actor'),
    75: ('StopMotionOf', False, 'ch:5 on another actor'),
    76: ('SetSpeedScaleOf', False, 'ch:24 on another actor'),
    77: ('SetCollisionRadiusScaleAll', False, 'ch:57 on every live CHR'),
    78: ('ChrDataReadStart', False, 'starts the async read of CHR data chrId'),
    79: ('WaitChrDataRead', False, 'poll returns 1 once FFX_Ch_DataReadSync says loaded, waits for ever if the data is absent'),
    80: ('SetSeqWaitKeyOf', False, 'ch:34 on another actor'),
    81: ('GetMotionFrame', False, 'CHR +0x740 m_frame via sub_839860, expression form only'),
    82: ('AttachToParentOffset', False, 'as ch:44 with an offset'),
    83: ('GetBoneJointIndex', True, 'sub_833BF0 scans the CHRDATA bone point table (blob +32, count +36, 16 byte records, key & 0x3FFF) for the joint index'),
    84: ('SetPartHidden', False, 'FFX_Ch_ModelSetHide. For model f117 it also feeds the 4 entry g_ffxF117HideCount ring, cleared by the field map load and read by FFX_Ch_StepAll'),
    85: ('GetPartHidden', True, 'FFX_Ch_ModelGetHide'),
    86: ('SetFlags1Bit1', False, 'm_flags1 0x1 = on. Its only reader is FFX_Ch_SetFlags1Bit8 and Bit10, which refuse to set their bit while it is set'),
    87: ('SetFlags1Bit1Of', False, 'ch:86 on another actor'),
    88: ('SetFlags1Bit1All', False, 'ch:86 on every in use CHR'),
    89: ('SetMotionByModeIndex', True, 'FFX_Mot_SetByModeIndex(mode, index)'),
    90: ('SetTexListBFrame', False, "sub_8313C0: sets a texture list B entry's frame, index -1 means all"),
    91: ('SetTexListAFrame', True, 'sub_831390, the list A twin of ch:90'),
    95: ('SetPendingLoopCount', False, 'FFX_Mot_SetPendingLoopCount, CHR +0x726'),
    96: ('SetIdShade', False, 'FFX_Ch_SetFlags1Bit40000: m_flags1 0x40000 = on, read by FFX_Ch_UpdateShadeFromLights'),
    97: ('MotionSetReadStart', False, 'starts the async read of a motion set'),
    98: ('MotionSetReadSync', True, 'with noWait the load state goes to the result, else it waits'),
    99: ('WaitMotionSetRead', False, 'waits until FFX_Ch_MotionSetReadSync says loaded'),
    100: ('ChrDataReadSync', True, 'the CHR data twin of ch:98'),
    102: ('ReadMotionGroupSync', False, 'start stores the group, the poll (kept chReadMotionGroupSync) waits while the group is reading'),
    103: ('SetFlags1Bit80000', False, 'm_flags1 0x80000 = on, no reader found'),
    104: ('WaitRomIdle', False, 'poll only, FFX_Ch_RomPump(1) == 0: waits until no ROM slot of any subsystem has a read pending'),
    105: ('SetMotionStepScaled', False, 'm_flags1 0x100000 = on. FFX_Mot_AdvanceFrame scales the frame step by g_ffxStepScale88 while g_ffxMotKeepSpeed is set'),
    106: ('SetLightOverrideMask', False, 'sub_830AE0: CHR +0x188 m_lightOverrideMask'),
    107: ('GetLightOverrideMask', True, 'sub_830AD0'),
    108: ('SetLightOverrideMaskBits', True, 'sets and clears bits of +0x188'),
    109: ('SetTiltX', False, 'm_tiltXTarget and m_tiltXStep (+0x160, +0x164), step 0 snaps m_tiltX'),
    110: ('SetAttachScale', False, 'CHR +0x1B4, the scale applied while attached to a parent'),
    111: ('SetUpdateWhileHidden', False, 'CHR +0x181: a hidden CHR keeps stepping and colliding. Pops only when a CHR exists'),
    112: ('IsSkipRender', True, 'maybe_FFX_Ch_IsSkipRender'),
    113: ('ResetTexListB', False, 'pops one value it ignores and calls sub_831300, the texture list B reset'),
    114: ('GetWalkmeshTri', False, 'FFX_Ch_GetWalkmeshTri, CHR +0x824'),
    115: ('SetHeightCollision', False, 'm_flags1 0x40000000 = on, read by FFX_Ch_ResolveCollisionsAll. Pops only when a CHR exists'),
    116: ('GetHeightCollision', True, 'reads m_flags1 0x40000000'),
    117: ('GetMotionFrameOf', False, 'ch:81 on another actor, expression form only'),
    118: ('SetHeadSwayEnabled', False, 'CHR +0x4D4 m_blinkState[0], which turns on the random head sway on bone 13 in FFX_Ch_UpdateBlink. Pops only when a CHR exists'),
    119: ('GetHeadSwayEnabled', True, 'sub_834740, reads m_blinkState[0]'),
    120: ('SetTexAnimPattern', False, 'FFX_Ch_SetTexAnimPattern(a, b)'),
    121: ('SetLookAtEnabled', False, 'sub_834F60: CHR +0x40C m_lookAtEnabled'),
    122: ('LookAtActor', False, 'look at kind 2, another CHR'),
    123: ('LookAtPoint', False, 'look at kind 3. The fourth target word is copied from an uninitialised stack slot (0xA7A04B), but FFX_Ch_UpdateLookAt never uses it'),
    124: ('LookAtCamera', False, 'look at kind 1, the eye from FFX_PlayerCam_GetEyePos'),
    125: ('LookAtAngles', False, 'look at kind 4, yaw and Z angles'),
    126: ('SetHeadSwayEnabledOf', False, 'ch:118 on another actor'),
    127: ('SetPartAlpha', True, 'FFX_Ch_ModelSetAlpha(part, alpha)'),
    128: ('SetCollisionHeight', False, 'CHR +0x51C m_collisionHeight'),
    129: ('SetSurface13Blocked', False, 'm_flags2 0x4 = on: FFX_Map_IsTriBlockedForChr treats walkmesh surface code 13 as a wall. Pops only when a CHR exists'),
    130: ('SetSecondaryPhysics', False, 'm_flags1 0x400000 = on, half of the secondary physics gate with 0x200000. Pops only when a CHR exists'),
    131: ('SetFlags2Bit8', False, 'm_flags2 0x8 = on, no reader found. Pops only when a CHR exists'),
    132: ('SetLookAtBlendFrames', False, 'CHR +0x420 m_lookAtBlendFrames, 0 means 6.0'),
    133: ('SetBlobShadowParam', False, 'CHR +0x17C, read by sub_82E3E0 on the way to FFX_ChrShadow_WriteBlobQuad'),
    134: ('SetFlags1Bit8', False, 'FFX_Ch_SetFlags1Bit8: sets m_flags1 0x8 unless bit 0x1 is set, no reader found'),
    135: ('SetFlags1Bit8All', False, 'ch:134 on every in use CHR'),
    136: ('SetShadeCoeff', False, 'FFX_Ch_AnimFloatSet on a shade coefficient'),
    137: ('SetMotionFramesIncl', False, 'FFX_EvScript_chSetMotion helper with inclusive explicit frames'),
    138: ('SetMotionFramesInclOf', True, 'ch:137 on another actor'),
    139: ('SetMotionFramesIncl', False, 'same helper call as ch:137'),
    140: ('SetMotionFramesInclOf', True, 'same helper call as ch:138'),
    141: ('SetTexAnimSlotByteE', False, "byte_231000E[48 * handle] of the CHR's texture animation slot, read by FFX_ChTexAnim_Step"),
    142: ('SetSecondaryVerticalSway', False, 'm_flags3 0x1 = on, the vertical sway term of StepSecondaryPhysics'),
    143: ('SetJointComposerMode', False, 'CHR +0x870 m_jointComposerMode. Pops only when a CHR exists'),
    144: ('SetForceSmoothComposer', False, 'CHR +0x871 m_forceSmoothComposer. Pops only when a CHR exists'),
}
# END NAMES


def positional(i, slot):
    return "FFX_AtelSys_Ch_%03d_%s" % (i, slot)


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
            skips.append("ch:%d bad role %r" % (i, role))
            continue
        slots = [s for s in SLOTS if (i, s) in table]
        if not slots:
            skips.append("ch:%d null row, nothing to name" % i)
            continue
        for slot in slots:
            ea = table[(i, slot)]
            cur = idc.get_name(ea) or ""
            if slot_suffix or len(slots) > 1:
                new = "FFX_AtelSys_Ch_%03d_%s_%s" % (i, role, slot)
            else:
                new = "FFX_AtelSys_Ch_%03d_%s" % (i, role)
            if cur == new:
                kept.append("ch:%d %s 0x%X already %s" % (i, slot, ea, cur))
                continue
            if (not is_positional(cur, i, slot, len(slots) == 1) and not PLACEHOLDER.match(cur)
                    and MISNAMED.get((i, slot)) != cur):
                skips.append("ch:%d %s 0x%X keeps its name %s (not positional)" % (i, slot, ea, cur))
                continue
            f = ida_funcs.get_func(ea)
            if f is None or f.start_ea != ea:
                skips.append("ch:%d %s 0x%X is not a function start, make one first" % (i, slot, ea))
                continue
            if len(owners[ea]) > 1:
                skips.append("ch:%d %s 0x%X is shared by %s" % (
                    i, slot, ea, ", ".join("ch:%d %s" % o for o in owners[ea])))
                continue
            other = ida_name.get_name_ea(idaapi.BADADDR, new)
            if other != idaapi.BADADDR and other != ea:
                skips.append("ch:%d %s name %s already used at 0x%X" % (i, slot, new, other))
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
