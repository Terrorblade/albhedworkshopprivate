"""
Apply the Core syscall library names from reversing/ATEL_CORE_LIBRARY.md to the FFX.exe IDB.

The Core library is ATEL library 0, 616 rows of {start, poll, float getter, int getter} at
g_ffxAtelSysFuncLib00_Core 0xC50050. Every handler already carries a positional name,
FFX_AtelSys_Core_NNN_<slot> with <slot> one of start, poll, resf, resi. This script reads the table
out of the IDB, and for every row in NAMES gives each non-null slot the name

    FFX_AtelSys_Core_NNN_<Role>_<slot>          (default)
    FFX_AtelSys_Core_NNN_<Role>                 (--no-slot-suffix, single slot rows only)

The positional prefix stays, because a (library, function) pair is the script ABI.

It refuses to touch a slot whose current name is not exactly the positional
FFX_AtelSys_Core_NNN_<slot> of that row, or a sub_/loc_/nullsub_ placeholder. So the names an
earlier pass gave (FFX_AtelOp_*, the trigger rows renamed on 2026-10-26, FFX_AtelSys_Core_573_BeginNewGame
and the like) are never overwritten. It also skips a slot that is not the start of a function, a
function that sits in more than one slot, and a new name that already exists elsewhere.

Dead rows (no use in any shipped script) are left positional unless --include-dead is given.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. The script never saves the database, save it yourself afterwards.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_core_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--include-dead]
                               [--no-slot-suffix] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
INCLUDE_DEAD = False
SLOT_SUFFIX = True
VERBOSE = False

TABLE = 0xC50050
ROWS = 616
SLOTS = ("start", "poll", "resf", "resi")
PLACEHOLDER = re.compile(r"^(sub|loc|nullsub)_[0-9A-Fa-f]+$")
ROLE_OK = re.compile(r"^[A-Z][A-Za-z0-9]{1,47}$")

# index -> (role, dead, one line meaning). Generated from reversing/ATEL_CORE_LIBRARY.md.
# BEGIN NAMES
NAMES = {
    2: ('BindCameraHandle', False, 'releases the old attachment, +0xAA = 2, +0xA8 = camIndex, caches a camera handle via 0x7BA9A0 (index <= 3), +0x36 bit 0x10 iff index 1'),
    3: ('SetActorAttachKind3', False, 'writes +0xA8 = id and +0xAA = 3 (attach kind 3), no release of the previous attachment'),
    4: ('SetActorAttachKind4', False, 'writes +0xA8 = id and +0xAA = 4 (attach kind 4), no release of the previous attachment'),
    5: ('ReleaseActorAttachment', False, "pushes 0 and runs core:413: disposes the actor's CHR (and both bone models) or camera handle per +0xAA, then zeroes +0xA8/+0xAA"),
    6: ('AttachBoneModel', False, 'clears bone slot 0..1, then if the actor has a CHR and chrId != 0 spawns chrId into +0xAF4+4*slot and attaches it to that parent bone'),
    7: ('DetachBoneModel', False, 'disposes the bone-attached CHR in slot and clears its bit in +0xB14, for category 7 slot 0 re-hides part 1 of the carrier'),
    8: ('Nop', True, 'stub, returns 0'),
    9: ('GetActorChrPtr', True, "returns the actor's CHR* (+0x9C) as an int"),
    10: ('GetActorAttachKind', True, 'returns +0xAA, the attach kind (1 CHR, 2 camera, 3, 4)'),
    11: ('GetActorAttachId', True, 'returns +0xA8, the model id or camera handle id'),
    12: ('SetPlayerChrPtr', True, 'pops a raw CHR pointer and makes it g_ffxControlledChr via FFX_Ch_SetPlayerChr, zeroing the direction ramps'),
    13: ('GetPrevMapId', True, "returns the save block's previous map id (j_FFX_SaveData_GetPrevMapId)"),
    14: ('GetPrevEntryPoint', True, "returns the save block's previous entry point (j_FFX_SaveData_GetPrevEntryPoint)"),
    15: ('GetMapId', True, "returns the save block's current map id (j_FFX_SaveData_GetMapId)"),
    16: ('GetEntryPoint', False, "returns the save block's current entry point, the doorway the party arrived through"),
    17: ('WarpToMap', False, 'FFX_Map_WarpToWithSavedFade(mapId, entryPoint, 1): requests a map change using the save block fade mode and frames'),
    18: ('SetActorPosXZ', False, 'FFX_Atel_SetActorPosXZ(actor, x, z, 0), moves the actor and its CHR on XZ only'),
    19: ('SetActorPos', False, 'FFX_Atel_SetActorPos(actor, x, y, z, 0), teleports the actor and its CHR'),
    20: ('SetMoveTargetXZ', True, "FFX_Atel_SetMoveCmdTarget(actor, x, 0, z) for the current channel's MOVE command"),
    22: ('SetMoveSpeed', False, 'writes MOVCMD+0x10 (current channel) = value, returns it'),
    23: ('SetMoveArrivalDistance', False, 'writes MOVCMD+0x38 (current channel) = value, returns it'),
    26: ('WaitMoveDone', False, 'poll only, returns 0 while the MOVE command runs, 5 when it ended'),
    27: ('WaitRotateDone', False, "poll only, returns 0 while the channel's ROTATE command runs, 5 when it ended"),
    28: ('SetMoveCmdTiming', False, 'writes MOVCMD+0x08 = first arg and +0x0C = second arg for the current channel'),
    29: ('SetRotateCmdTiming', False, 'writes the bound ROTATE command +0x08 = first arg and +0x0C = second arg (<= 0 becomes 1)'),
    30: ('PopTwoFloats', True, 'stub, pops 2 floats and returns the first pushed'),
    31: ('GetYawToMoveTarget', False, "atan2 yaw from the actor's XZ position to the current channel's MOVE target XZ (+0x28, +0x30)"),
    32: ('GetPitchToMoveTarget', False, "sub_88B4D0 yaw and pitch from the actor's position to the MOVE target XYZ, returns the pitch"),
    33: ('SetMoveCmdField44', False, 'writes MOVCMD+0x44 = value (a wrap to [-pi, pi] is computed and its result discarded)'),
    34: ('SetMoveCmdField48', True, 'writes MOVCMD+0x48 = value, wrap result discarded'),
    35: ('SetActorYaw', True, 'writes motion state +0x20 (yaw) = value'),
    36: ('SetActorPitch', True, 'writes motion state +0x24 (pitch) = value'),
    37: ('SetActorRotY', False, 'writes motion state +0x34 (rotation Y) = value, wrap result discarded'),
    38: ('SetActorRotX', True, 'writes motion state +0x30 = value, wrap result discarded'),
    39: ('SetActorRotZ', True, 'writes motion state +0x38 = value, wrap result discarded'),
    40: ('SetRotateTargetY', False, 'rotcmd(channel)+0x20 = target, +0x2C = current rotY (+0x34) - target'),
    41: ('SetRotateTargetX', False, 'rotcmd(channel)+0x24 = target, +0x30 = current motion +0x30 - target'),
    42: ('SetRotateTargetZ', False, 'rotcmd(channel)+0x28 = target, +0x34 = current motion +0x38 - target'),
    43: ('SetMoveYawTurnRate', False, 'writes MOVCMD+0x3C (current channel) = value'),
    44: ('SetMovePitchTurnRate', False, 'writes MOVCMD+0x40 (current channel) = value'),
    45: ('SetMoveArrivalDistanceAll', False, 'writes MOVCMD+0x38 = value on all 9 channels'),
    46: ('SetRotateSpeed', False, 'writes rotcmd(channel)+0x10 = value'),
    47: ('SetRotCmdField18', False, 'writes rotcmd(channel)+0x18 = value'),
    48: ('SetRotCmdField1C', False, 'writes rotcmd(channel)+0x1C = value'),
    49: ('EnableActorPlayerBind', True, 'sets +0x34 bit 0x200 (eligible to be the pad CHR) and re-runs unbind/BindPlayerChr'),
    50: ('DisableActorPlayerBind', True, 'clears +0x34 bit 0x200 and re-runs unbind/BindPlayerChr'),
    52: ('EnableActorEvent', False, 'sets bit n of the event mask +0xAB that FireActorEvent tests, returns n'),
    53: ('DisableActorEvent', False, 'clears bit n of the event mask +0xAB'),
    54: ('UnshareActorMoveCmds', False, 'for each of the 9 channels of the target actor, rebinds channels sharing that MOVE command to their own copy (and ROTATE when flag 0x4000)'),
    55: ('UnshareActorRotCmds', False, 'for each of the 9 channels of the target actor, rebinds channels sharing that ROTATE command to their own copy'),
    56: ('GetActorPosX', False, 'returns motion state +0x00 of FFX_Atel_GetActor(index)'),
    57: ('GetActorPosY', False, 'returns motion state +0x04 of the indexed actor'),
    58: ('GetActorPosZ', False, 'returns motion state +0x08 of the indexed actor'),
    59: ('SetMoveTargetActor', True, "FFX_Atel_RetargetMoveCmdAtActor: MOVCMD+0 = actor, target = that actor's live position"),
    60: ('AimMoveCmdAtActor', True, "bound MOVE command +0 = actor, then sub_86F7F0(actor, that actor's position)"),
    61: ('CaptureActorRot', False, 'copies motion rot +0x34/+0x30/+0x38 into the bound ROTATE command +0x38/+0x3C/+0x40 (mask 255)'),
    62: ('CaptureActorRotMasked', True, 'core:61 with a mask: bit 1 rotY, bit 2 rot +0x30, bit 4 rot +0x38'),
    63: ('GetCapturedRotY', False, 'returns the bound ROTATE command +0x38, where core:61 stores rotY'),
    64: ('GetCapturedRotX', True, 'returns the bound ROTATE command +0x3C'),
    65: ('GetCapturedRotZ', True, 'returns the bound ROTATE command +0x40'),
    66: ('BindActorPartyChar', False, 'type 1: +0x40 = charIndex and CHR party index, +0x34 bit 0x200 iff charIndex 0, rebind player CHR, +0xAF and dword_132FFA0 map'),
    67: ('SetSelfAsPlayerActor', False, 'FFX_Atel_SetPlayerActorId(own id): ctx+10 = this actor, caches its position in the context'),
    68: ('GetPadHeldRemapped', False, 'returns the port 0 held mask through FFX_Pad__remapButtonMask (identity)'),
    69: ('GetPadRepeatRemapped', True, 'returns the auto repeat mask (pad word +10, wall clock timer) remapped'),
    71: ('GetPadReleasedRemapped', False, 'returns the released-edge mask remapped'),
    72: ('GetPadHeld', True, 'returns the raw port 0 held mask'),
    73: ('GetPadRepeat', True, 'returns the raw auto repeat mask (wall clock)'),
    74: ('GetPadPressed', True, 'returns the raw pressed-edge mask'),
    75: ('GetPadReleased', True, 'returns the raw released-edge mask'),
    76: ('IsPadHeldRemapped', False, '1 when button bit n is set in the remapped port 0 held mask'),
    77: ('IsPadPressedRemapped', False, '1 when bit n is set in the remapped pressed mask, forced 0 in event 49 for actor 11 when byte_112D6C9 bit 0x20'),
    78: ('IsPadReleasedRemapped', True, '1 when bit n is set in the remapped released mask'),
    79: ('IsPadRepeatRemapped', True, '1 when bit n is set in the remapped auto repeat mask (wall clock)'),
    80: ('IsPadHeld', False, '1 when bit n is set in the raw port 0 held mask'),
    81: ('IsPadPressed', False, '1 when bit n is set in the raw pressed mask'),
    82: ('IsPadReleased', True, '1 when bit n is set in the raw released mask'),
    83: ('IsPadRepeat', True, '1 when bit n is set in the raw auto repeat mask (wall clock)'),
    84: ('SetTriggerLine', False, 'type 2/5 only: point A at motion +0x00, B at +0x30, half height +0x1C = -1, +0x34 OR= 0x80 (arm), +0x36 OR= 1 (retest)'),
    85: ('SetTriggerHalfHeight', False, 'types 2,3,5,6 only: shape +0x1C = halfHeight, sets +0x36 bit 1 (retest)'),
    86: ('SetTriggerArmed', False, 'types 2,3,5,6: +0x34 bit 0x80 = (armed != 0), sets +0x36 bit 1 (retest). Body identical to core:92'),
    87: ('SetTriggerBoxCentre', False, 'types 3,6: shape +0x00/+0x04/+0x08 = x,y,z, arms (+0x34 bit 0x80), sets retest'),
    88: ('SetTriggerBoxExtentsXZ', False, 'pushes halfHeight -1.0 and tail calls core:89, so X and Z half extents with no Y test'),
    89: ('SetTriggerBoxExtents', False, 'types 3,6: shape +0x30 = xHalf, +0x38 = zHalf, +0x1C = halfHeight, sets retest'),
    90: ('SetTriggerBoxRotation', False, 'types 3,6: shape +0x34 = rotation about Y, sets retest. Always returns 0'),
    91: ('SetActorRadius', True, 'dead. Motion/shape +0x48 = radius, sets retest, no type check. Body identical to core:133'),
    92: ('SetTriggerArmed', False, 'types 2,3,5,6: +0x34 bit 0x80 = (armed != 0), sets retest. Body identical to core:86'),
    96: ('SetActorControllable', False, 'sets or clears own +0x34 bit 0x200 (the bit BindPlayerChr requires), then sub_86A0E0 unbinds and rebinds the player CHR'),
    97: ('SetActorMotionField40', False, 'own motion state +0x40 = (float)(int)value, sets +0x36 bit 1, returns it'),
    98: ('SetActorMotionField44', False, 'own motion state +0x44 = (float)(int)value, sets +0x36 bit 1, returns it'),
    99: ('SetActorMotionField3C', False, 'own motion state +0x3C = value, sets +0x36 bit 1, returns it'),
    101: ('MesWinSetPosition', False, 'window rec +0 = x, +2 = y (s16), +28 = skin byte. Hardcoded x/y fixups for events 23, 171, 212, two keyed on language'),
    102: ('MesWinSetPalette', False, 'window rec +32 byte = palette, passed to sub_8B0000 by core:106'),
    103: ('MesWinSetSizePacked', True, 'dead. Splits size into low word -> rec +4 (width) and high word -> rec +6 (height) via a push and pop'),
    104: ('MesWinSetSize', True, 'dead. Window rec +4 = width, +6 = height (used by core:106 when attr bit 8 is set)'),
    108: ('SetMoveSpeedAll', False, 'writes MOVE cmd +0x10 on all 9 channels (actor+0x5C8+76*ch), returns the value'),
    109: ('SetMoveYawTurnRateAll', False, 'writes MOVE cmd +0x3C (yaw turn rate per FFX_GAME_NOTES) on all 9 channels, returns the value'),
    110: ('SetMovePitchTurnRateAll', False, 'writes MOVE cmd +0x40 (pitch turn rate per FFX_GAME_NOTES) on all 9 channels, returns the value'),
    111: ('SetRotateSpeedAll', False, 'writes ROTATE cmd +0x10 (actor+0x874+68*ch) on all 9 channels, returns the value'),
    112: ('SetRotCmdField18All', False, 'writes ROTATE cmd +0x18 (actor+0x87C+68*ch) on all 9 channels, returns the value'),
    113: ('SetRotCmdField1CAll', False, 'writes ROTATE cmd +0x1C (actor+0x880+68*ch) on all 9 channels, returns the value'),
    115: ('SetRotFrames', True, 'dead. ROTATE cmd +4 duration frames for the current channel (actor+0x868+68*ch)'),
    116: ('SetMoveFramesAll', False, 'MOVE cmd +4 duration frames on all 9 channels'),
    117: ('SetRotFramesAll', True, 'dead. ROTATE cmd +4 duration frames on all 9 channels'),
    118: ('GetSelfActorType', False, 'FFX_Atel_GetActorKind(own id +0x2E), the definition type byte'),
    119: ('StopActorMove', False, 'sub_863E10(actor, current channel): resets channels sharing the move cmd to their own with kind 0, keeps target, stops rotate on flag 0x4000'),
    120: ('StopActorRotate', False, 'sub_863FE0(actor, current channel): repoints channels sharing the rotate cmd to their own with kind 0, copying angles'),
    121: ('PassThroughInt', True, 'dead. Pops one int and returns it'),
    122: ('ExtendTriggerLine', False, 'types 2,5: moves point A back along the line by extendA and B forward by extendB, sets retest'),
    123: ('MeasureMessageText', True, 'dead. Picks the text by international env id, then sub_8B7CC0 which is a stub returning 100 OR 100<<16'),
    126: ('GetPlayerActorId', True, 'FFX_Atel_GetPlayerActorId: ctx+10, 0xFFFF -> -1'),
    127: ('SetTriggerBits13To14', False, 'types 2,3,5,6: own +0x34 bits 13..14 = mode, no retest'),
    128: ('GetEntryPointX', False, "entry record +0x0C for the save block's current entry point"),
    129: ('GetEntryPointY', False, 'entry record +0x10 (+Y is down)'),
    130: ('GetEntryPointZ', False, 'entry record +0x14'),
    131: ('GetEntryPointYaw', False, 'entry record +0x08, the scalar before XYZ, probably the facing yaw'),
    133: ('SetActorRadius', False, 'own motion/shape +0x48 = radius, sets retest, no type check. Body identical to core:91'),
    134: ('GetSceneId', False, 'thunk to j_FFX_SaveData_GetSceneId (save block)'),
    135: ('GetMapId', False, 'thunk to j_FFX_SaveData_GetMapId (save block)'),
    136: ('GetPrevSceneId', False, 'thunk to j_FFX_SaveData_GetPrevSceneId (save block)'),
    137: ('GetPrevMapId', False, 'thunk to j_FFX_SaveData_GetPrevMapId (save block)'),
    138: ('GetSystemActorId', True, 'dead. FFX_Atel_GetSystemActorId: context 0 event package +24'),
    139: ('GetCrossingSide', False, 'side byte of the link for the current request (sub_8632B0), else own +0x36 bits 1..2 with 3 -> -1'),
    140: ('GetContextStepCounter', True, 'dead. Returns the per-context step counter at ctx+500'),
    141: ('SetActorMotionField4C', False, 'own motion state +0x4C = value, sets +0x36 bit 1, returns it'),
    142: ('SetActorHalfHeightNoRetest', True, 'dead. Own motion/shape +0x1C = value, no type check and no retest bit'),
    144: ('GetActorHeading', False, 'own motion state +0x20, the heading / move direction'),
    145: ('GetActorRotY', False, 'own motion state +0x34, the facing rotY'),
    146: ('GetActorHeadingById', False, 'motion state +0x20 of the actor given by id'),
    147: ('GetActorRotYById', False, 'motion state +0x34 of the actor given by id'),
    148: ('SetMoveAccel', False, 'MOVE cmd +0x34 (acceleration per FFX_GAME_NOTES) on the current channel, returns the value'),
    150: ('SetActorPitch', False, 'stops own rotate cmd, motion +0x24 and +0x30 = pitch, pushes to a kind 4/5 handle, no CHR update'),
    151: ('MesWinGetState', False, 'returns window rec+20: 0 idle, 1 opening, 2 shown, 3 closing, 4 answered'),
    153: ('SetActorPosOnPath', True, 'dead. Evaluates path variable at t (int to float, clamped 0..1) via sub_862B20 -> sub_88A4A0, then SetActorPos snapPrev 0'),
    154: ('SetTriggerTarget', False, 'types 5,6: shape +0x28 u16 = target actor id, returns it'),
    155: ('SetTriggerLineFromVars', False, 'resolves two vector variables, pushes A xyz then B xyz and tail calls core:84 (set line from two points)'),
    156: ('SetActorPosOnPathAlt', True, 'dead. Same as core:153 with sub_862B70 -> sub_88A4D0 as the evaluator'),
    157: ('MesWinSetFlags', False, 'window rec +24 dword = flags, ORed with rec +40 and passed to sub_8B0000 by core:106'),
    158: ('SetMessageNumberSlot', False, 'table dword_1869AB0[32*win+slot] -> slot record: byte +0 = format, dword +4 = value. The number a message shows'),
    159: ('SetMessageNumberValue', True, 'the value half of core:158 only: dword +4 of the slot record dword_1869AB0[32*win+slot]'),
    160: ('SetMessageNumberFormat', True, 'the format half of core:158 only: byte +0 of the slot record dword_1869AB0[32*win+slot]'),
    161: ('SetMessageWindowFrameStyle', True, 'window record +0x10 (the four frame style bytes) = style as one packed dword, via FFX_Atel_GetMessageWindow'),
    162: ('SetMessageWindowFrameStyle4', False, 'packs four bytes as b3<<24 OR b2<<16 OR b1<<8 OR b0, pushes it and pops it back, then window record +0x10 = it like core:161'),
    163: ('GetActorCentreToVars', False, 'resolves three variables and fills them from FFX_Atel_GetActorCentre (0x86A2F0): line midpoint for types 2/5, else the position'),
    164: ('GetActorDistance', False, 'sqrt of dx^2+dy^2+dz^2 between the motion state positions (+0,+4,+8) of the two actors'),
    165: ('GetActorDistanceXZ', False, 'sqrt of dx^2+dz^2 between the motion state positions of the two actors, Y ignored'),
    166: ('RandBelow', False, 'n<=0 becomes 1, returns (n * (u16)FFX_Rand_Stream(2)) >> 16'),
    167: ('DisableActor', False, "actor +0x34 &= ~2, clears the alive bit RunScript gates on, so that actor's scripts stop being stepped"),
    168: ('EnableActor', False, "actor +0x34 OR= 2, sets the alive bit back so the actor's scripts run again"),
    169: ('Rand16', False, 'returns (u16)FFX_Rand_Stream(2), the raw 16 bit script random'),
    170: ('SetFlag1325DB0', False, 'dword_1325DB0 bit 0 = (on != 0), read back by core:218'),
    171: ('WarpToMap', False, 'FFX_Map_WarpToWithSavedFade(mapId, entry, 1): uses the saved fade bytes +0x11/+0x12. Gated on ctx byte 0 bit 0x80 or debug'),
    172: ('SetMapNameId', False, 'FFX_SaveData_SetMapNameId: save word +0x0E = nameId, the place name the save file and menu show'),
    173: ('SetMapNameIdFromGroundDic', True, "FFX_SaveData_SetMapNameIdFromGroundDic: save +0x0E from the loaded package's place name table"),
    174: ('GetMapNameId', True, 'FFX_SaveData_GetMapNameId: save +0xD4 when the override byte +0xD6 is set, else +0x0E'),
    175: ('SetEncounterGroundAttr', True, 'save byte +0x10 = attr, the encounter zone / ground attribute. PullActorPosFromChr rewrites it every step'),
    176: ('GetEncounterGroundAttr', True, 'returns save byte +0x10, the encounter ground attribute written per step from the player CHR'),
    177: ('SetMoveCmdField18', False, 'own MOVE record of the current level, float at actor +0x5B8 + 76*level + 0x18 = value'),
    178: ('SetDisplayMergeEnabled', False, 'byte_130181E = (on != 0) & 1 via sub_83ADD0, returns the old byte. Boot sets it to 1 among the draw enable flags'),
    179: ('SetDisplayMergeX', False, 'reads the merge offset (dword_130194C/50/54), replaces dx, writes back with FFX_Gs_SetDisplayMergeOffset'),
    180: ('SetDisplayMergeY', False, 'replaces dy of the GS display merge offset, keeps dx and alpha'),
    181: ('SetDisplayMergeAlpha', True, 'replaces alpha of the GS display merge offset, keeps dx and dy'),
    182: ('GetDisplayMergeX', True, 'returns the GS display merge dx'),
    183: ('GetDisplayMergeY', True, 'returns the GS display merge dy'),
    184: ('GetDisplayMergeAlpha', True, 'returns the GS display merge alpha'),
    185: ('GetDisplayMergeEnabled', True, 'sub_83ADD0(0) then sub_83ADD0(old), a non destructive read of the core:178 byte'),
    186: ('ClearEventEntryTable', False, 'own actor +0x44 = 0, the event entry table pointer opcode 0x79 sets, so events fall back to the type default'),
    189: ('KillRequestsAboveLevel', False, 'FFX_Atel_KillRequestsAbove(own actor, level-1, mode 1) via sub_871730, drops queued requests above that level'),
    190: ('SetWarpGate', False, 'current context byte 0 bit 0x80 = (on != 0). That bit is the gate FFX_Map_WarpTo/WarpToWithSavedFade test and consume'),
    191: ('GetEntryPointX', False, 'FFX_Atel_GetEntryRecordByIndex(index) +0x0C, a negative index means the current entry point'),
    192: ('GetEntryPointY', False, 'entry record +0x10 by index'),
    193: ('GetEntryPointZ', False, 'entry record +0x14 by index'),
    194: ('GetEntryPointYaw', False, 'entry record +0x08 by index, the facing yaw in radians per CHEAT_WARP 3.1'),
    195: ('IsRotating', True, 'own channel (level) ROTATE command pointer (channel +0x48): its u16 at +2 & 0x1F != 0, a rotate command is live'),
    196: ('IsMoving', False, 'u16 at actor +0x5BA + 76*level (own MOVE record +2) & 0x7F != 0, a move command is live'),
    197: ('SetTurnMotionSet', False, 'writes four ints to actor +0xAE0..+0xAEC in push order, read back one by one by core:199'),
    198: ('GetTurnKind', False, 'own ROT record (+0x864 + 68*level) +0x2C angle: bit 0 = angle > 0, bit 1 =  OR angle OR  >= flt_1327124 degrees'),
    199: ('GetTurnMotion', False, 'returns the dword actor +0xAE0 + 4*index, the four values core:197 stored. No bound check'),
    200: ('ScaleByTurnAngle', False, 'd = wrap(ROT record +0x20 - motion +0x34), flags&2: large*clamp(1-1.2*(1- OR d OR /pi)), else small*clamp( OR d OR /(pi/2))'),
    201: ('GetEntryArrivalScript', False, 'entry record +0x06 by index as a float, the arrival script entry FFX_Atel_StartEntryArrivalScript launches'),
    202: ('AddPartyMember', False, 'FFX_SaveData_SetCharInParty(id, 1). Char 6 at story 1085 also takes order slot 3, char 15 sets dword_1325AC8 = 3'),
    203: ('RemovePartyMember', False, 'SetCharInParty(id, 0), then any active field slot holding id becomes 255 and FFX_SaveData_SetFieldParty3 rewrites them'),
    204: ('IsCharInParty', False, 'FFX_SaveData_IsCharInParty(id) == 1, the record +0x2C bit 0 membership flag'),
    205: ('IsSelfInFieldParty', True, '1 when own actor +0x40 (party char index) is one of the three active field slots, else 0'),
    206: ('SetActorWordB16', False, 'own actor u16 +0xB16 = value, a type 1 only field'),
    207: ('SetActorIntAF0', False, 'own actor dword +0xAF0 = value, read back by core:208'),
    208: ('GetActorIntAF0', False, 'returns own actor dword +0xAF0'),
    209: ('StopAllMoveCmds', False, 'sub_863E10 on own channels 0..8: each channel sharing a MOVE command gets its own record back, state 0, values copied'),
    210: ('StopAllRotateCmds', False, 'same for ROTATE commands, sub_863FE0 for channels 0..8'),
    211: ('StopMoveCmd', True, "sub_863E10(self, -1), the current level's MOVE command only"),
    212: ('StopRotateCmd', True, "sub_863FE0(self, -1), the current level's ROTATE command only"),
    213: ('PrepareVoice', False, 'start: stores id, SyncData kind 1 countdown. poll: counts it down, or waits for dword_1326B94 == 0. result: sub_887570(id) queues the voice'),
    214: ('StartVoice', False, 'start: SyncData kind 2 countdown, wait frames from ctx +0x214 or sub_885D20 (audio position at 48 kHz). poll counts down'),
    215: ('SkipVoiceUnlessTimed', False, 'when g_ffxPacingMode != 1 stops the voice through FFX_Voice_Stop (FMOD command 10), scripts call it from the core:216 loop on a Circle press, otherwise nothing'),
    216: ('IsVoicePlaying', False, '1 while the kind 3 countdown dword_1325AE0 runs, else sub_8864B0 (voice busy flags dword_132FF10 OR qword_132FF14) != 0'),
    217: ('WaitVoiceDone', False, 'poll only: counts down dword_1325AE8 when a sync row armed it, else finishes when sub_8864B0 reports no voice busy'),
    218: ('GetFlag1325DB0', False, 'returns the bit core:170 sets'),
    219: ('LoadGlobalEffect', False, 'start: dword_1326B70 = 0, dword_1326B58 = 1. poll: requests the effect each pass, done at effect state 3 (or 1 on scene 270)'),
    220: ('PlayGlobalEffect', False, 'poll sub_8633E0(self id): states 0,1,6 wait, 3 calls sub_788D60(id) and waits, 5 clears dword_1326B58 and finishes'),
    221: ('IsGlobalEffectActive', True, 'byte_1133364 state: 0 or 6 -> 0, 1, 3, 5 -> 1, any other state returned as is'),
    222: ('WaitGlobalEffectIdle', False, 'poll only: when the effect state is 0 or 6 sets byte_1326B80 = 4 and finishes, else waits'),
    223: ('PlaySe', False, 'sub_886E80: decodes the id (sub_885D10, sub_885D00) and calls sub_886B80(bank, id, 63, 127), pan centre and full volume'),
    224: ('StopSe', False, 'sub_886EB0: same id decode, then sub_887750(bank, id)'),
    225: ('GetEncounterStepGate', False, 'FFX_Atel_GetFieldStepAllowsEncounter: save byte +0x13 bit 1'),
    226: ('SetEncounterStepGate', False, 'FFX_Atel_SetFieldStepAllowsEncounter: save byte +0x13 bit 1 = (on != 0)'),
    227: ('SetRotCmdField14', False, 'sub_8715A0: own ROT record of the current level, float at actor +0x864 + 68*level + 0x14 = value'),
    228: ('SetRotCmdField14All', False, 'sub_86F300: the same ROT record +0x14 float on all 9 channels'),
    229: ('SetContextFlag0x0800', True, 'current context byte 1 bit 0x08 = (on != 0). The reset sets it (byte 1 = 0x98)'),
    230: ('IsCharInFieldParty', False, '1 when charId is one of the three active field party slots (FFX_SaveData__getFieldParty), else 0'),
    231: ('SetPartyOrderSlot', False, 'FFX_SaveData_SetPartyOrderSlot(charId, slot), -1 becomes 255 (empty). Slots 0..2 active, 3..19 bench'),
    232: ('GetFieldPartyToVars', False, 'fills three variables with the active field party char ids, 255 rewritten as -1'),
    233: ('SetContextFlag0x1000', False, 'current context byte 1 bit 0x10 = (on != 0), read by core:234. The reset sets it'),
    234: ('GetContextFlag0x1000', True, 'current context byte 1 bit 0x10'),
    235: ('GetOwnLevel', False, "returns the running actor's level byte, which picks the channel"),
    236: ('GetFieldPartySlot', False, 'index of charId among the three active field party slots, -1 when absent or charId is -1'),
    237: ('GetOwnPartyIndex', False, 'returns own actor +0x40, the party char index CHRREG passes to FFX_Ch_SetPartyIndex (-1 for none)'),
    238: ('IsActorFlag20', False, 'actor +0x34 bit 0x20 of the given actor'),
    239: ('SetOwnFlag0800', False, 'own actor +0x36 bit 0x800 = (on != 0)'),
    240: ('SetTriggerLinkTable', True, 'resolves a variable and hands it to FFX_Atel_SetTriggerLinkTable: types 5/6 +0x2E4 = it, links cleared, 16 nodes added'),
    241: ('GetCrossingSideForActor', True, 'FFX_Atel_GetLinkCrossingSide(self, id), else when id is own target (motion +0x28) or none, +0x36 bits 1..2 with 3 as -1'),
    242: ('SetCtx0Flag20', False, 'byte_1325BA3 (context 0 byte 3) bit 0x20 = (on != 0), returns the old bit'),
    243: ('SetGuideMapLayout', False, 'FFX_RcBg_SetGuideMapLayout: guide map layout preset 0..4 (rcbgGuideSetLayout on g_ffxRcBgCtx +4). Map entry calls it with 0. RCBG.md'),
    244: ('GetYawToPoint', False, 'atan2(z - self.z, x - self.x) from own motion position via sub_88B430, y popped and ignored'),
    245: ('GetPitchToPoint', False, 'sub_88B4D0 from own motion position: returns the elevation angle atan2(y - self.y, XZ distance), the yaw is dropped'),
    246: ('WaitActorTasksDone', False, 'start stores {actorId, max(level,1)}. poll finishes when FFX_Atel_ActorHasTaskAtLevel(actorId, level) is 0 or actorId < 0'),
    247: ('WaitPartyMemberTasks', True, 'resolves party slot to its actor (FFX_Atel_GetPartyMemberActor), then waits while ActorHasTaskAtLevel(actor, level), level <= 0 means 1'),
    248: ('DevMemsetVar', False, 'dev harness only: memset(ResolveVarAddress(var), value, size). FFX_IsDevHarness is 0 in retail so it only pops 3 and returns size'),
    249: ('DevHarnessQuery', False, 'returns sub_88D5D0() under the dev harness, else 1. FFX_IsDevHarness is constant 0, so retail always gets 1'),
    250: ('SetSaveByte2B', False, 'writes the low byte of value to g_ffxSaveData+0x2B (sub_88EB40), returns value'),
    252: ('SetSaveHeaderBit18', True, 'FFX_SaveData_SetHeaderBit18(bit): sets a bit in the 64 bit array at g_ffxSaveData+0x18'),
    253: ('PlaySe', False, 'sub_886E50(seId, a, b): decodes the sound id (sub_885D10/885D00) and calls sub_886B80 with two byte args'),
    254: ('ControlSe', False, 'sub_886F60(seId, value) -> sub_81E820(bank, index, byte value) on a decoded sound id'),
    255: ('ControlSeAlt', True, 'sub_886DF0(seId, value) -> sub_81E460(bank, index, byte value) on a decoded sound id'),
    256: ('SetSeParam', False, 'sub_886FF0(seId, param, value) -> sub_81E860, sets one parameter of a sound'),
    257: ('SetSeParamAlt', True, 'sub_886E20(seId, param, value) -> sub_81E480, the 81E4xx twin of 256'),
    258: ('LoadBgm', False, 'start latches track, poll waits out a catch-up burst, getter sub_886960(track): for track <= 255 prepares the music, busy pumping the loader'),
    259: ('StopBgm', False, 'if track is the current BGM (dword_1327130) clears it to -1, then sub_886800(track) -> sub_67A010, sub_885E80'),
    260: ('PlayBgm', False, 'sets dword_1325B40 = (track == 37), sub_8869E0 and sub_886A20 (volume 127) on track, records it as current BGM dword_1327130'),
    261: ('WaitBgmLoaded', False, 'empty start, poll returns 5 at once, getter returns 1. A PC no-op left where the PS2 waited for the music load'),
    262: ('BindSoundSlot', False, 'sound slot table dword_1328318 (16 x 36 bytes): rec+0 = seId, rec+4 = address of script var posVar'),
    263: ('SetSoundSlotFollowActor', False, "if the slot has a var: sets bit 2 in the var's first word and rec+8 word = actorId"),
    264: ('SetSoundSlotPos', False, 'if the slot has a var: clears bit 2 in its first word, writes xyz to rec+8 (which = 0) or rec+20 (which != 0)'),
    265: ('PlaySoundSlot', False, 'if rec+0 is set calls sub_876930(rec, 1) -> sub_876950(rec, 1, 256)'),
    266: ('StopSoundSlot', False, 'if rec+0 is set stops that sound (sub_886EB0 -> sub_887750) and clears the slot (sub_8772F0)'),
    268: ('WarpNoSavedFade', False, 'FFX_Map_WarpToWithSavedFade(mapId, entry, 0), same body as core:267'),
    269: ('GivePrimerKeyItem', False, 'sets key item flag 0xA004+n, and for n 0..25 also sub_906B30(n), the Al Bhed primer side'),
    270: ('GetPrimerMask', False, 'returns dword_11307A0 (save+0x3D10)'),
    273: ('PushPartySnapshot', False, 'FFX_Atel_PushPartySnapshot: save +0x21..0x23 = field party, +0x24 = membership mask of chars 0..6, +0x20 = 1'),
    274: ('PopPartySnapshot', False, 'FFX_Atel_PopPartySnapshot: if save+0x20 set, restores membership and field party from the snapshot, then clears +0x20'),
    275: ('GetPartySnapshotValid', True, 'returns the party snapshot valid flag at g_ffxSaveData+0x20'),
    276: ('SetIndoorOverride', False, 'context byte +2: bit 4 = force, bit 2 = indoor. When forced, the indoor state (ambient sound volume, rain effects) uses bit 2'),
    277: ('SetContextFlag08', False, 'sets or clears bit 0x08 of current context byte +2 (default on, ctx init writes 0xC8)'),
    278: ('SetActorExtraPass1', False, 'actor +0x36: bits 9..10 = 1 (0x200), one extra run pass in StepFrame'),
    279: ('SetActorExtraPass1Grant', False, 'actor +0x36: bits 9..10 = 1 and bit 0x1000, which also grants the extra pass to threads it starts'),
    280: ('SetActorExtraPass2', True, 'actor +0x36: bits 9..10 = 2 (0x400)'),
    281: ('GetActorEquipModelId', False, "for this actor's party char (actor+0x40): kind 1 -> FFX_SaveData_GetEquippedArmourChrId, else weapon. kind clamped 0..1"),
    282: ('InitEvTexSprites', False, 'sub_874B70: sets the event sprite buffer (dword_1327144, end at buf+size-160), texVar ptr to dword_23CBC5C, and loads the event TexList'),
    283: ('SetEvTexSprite', False, 'sub_8748A0: slot (0..127) gets the image at spriteVar, resets active, layer 0xFF, color 0x80808080, visible, may kick the TexList load'),
    284: ('ClearEvTexSprite', False, "sub_8747F0: clears the low byte of the slot's first word, the active flag 283 sets"),
    285: ('SetEvTexSpriteAlpha', False, 'sub_8749B0: byte 3 of the slot color dword = alpha'),
    286: ('SetEvTexSpriteLayer', True, "sub_874AD0: high byte of the slot's first word, clamped 0..255, default 0xFF"),
    287: ('SetEvTexSpritePos', False, 'sub_874B30: slot x, y words. Language 11 hack: on level hash 409, (270, 213) becomes (290, 213)'),
    288: ('SetEvTexSpriteScale', False, 'sub_874A80: slot scale words = sx*256, sy*256'),
    289: ('GetPadAnalogRemapped', False, 'port 0 analog staging byte for axis (clamped 0..13) after FFX_Pad__mapButtonCode, which passes 0..3 through'),
    290: ('GetPadAnalogRaw', False, 'port 0 analog staging byte for axis (clamped 0..13), no remap'),
    291: ('EchoArg', True, 'stub, pops 1 and returns it'),
    292: ('EchoArg', True, 'stub, pops 1 and returns it'),
    293: ('SetActorPosXZSnapped', True, 'FFX_Atel_SetActorPosXZ(self, x, z, snapPrev=1), XZ twin of core:294'),
    294: ('SetActorPosSnapped', False, 'FFX_Atel_SetActorPos(self, x, y, z, snapPrev=1), the teleport. On scene 159 with z in (-35.6, -35.5) also sets dword_1325B40'),
    295: ('IsActorWithinOwnRadius', False, 'pushes own motion block float +0x48 and runs core:296 on the same stack: is actorId within that distance of self (3D)'),
    296: ('IsActorWithinDistance', False, '1 when the 3D distance from self to actorId (motion block positions) is <= dist'),
    297: ('SetEvTexSpriteColor', False, 'sub_8749F0: slot color dword bytes 0, 1, 2 from three ints clamped 0..255'),
    298: ('SetEvTexSpriteVisible', False, 'sub_874970: slot byte +3 = on != 0'),
    299: ('SetContextFlag40', False, 'sets or clears bit 0x40 of current context byte +2 (default on)'),
    300: ('SetContextWords4and6', True, 'current context word +4 = w4 (last push), +6 = w6. Defaults 4 and 2'),
    301: ('GetLoveParam', False, 'returns LoveParam[chr] (save +0x2C + 4*chr), chr < 0 reads [0], chr > 7 reads [7]'),
    302: ('AddLoveParam', False, 'LoveParam[chr] += delta, chr clamped to 0..7, returns the new value'),
    303: ('SubLoveParam', True, 'LoveParam[chr] -= amount, floored at 0, chr clamped 0..7'),
    304: ('SetLoveParam', False, 'LoveParam[chr] = max(value, 0), chr clamped 0..7, returns the clamped index'),
    305: ('ClearEventFlagBit', True, 'clears one bit of g_ffxEventFlagBits (save +0x4C, 640 bits), ignores bit >= 640'),
    306: ('SetActorTriggerArmed', False, 'for trigger actor types 2, 3, 5, 6: +0x34 bit 0x80 = armed, +0x36 OR= 1 (retest)'),
    307: ('SetActorTriggerArmed', False, "calls core:306's handler with the same stack"),
    308: ('SetActorModel', False, 'calls FFX_AtelOp_SetActorModel with the same stack: spawns the CHR for chrId on this actor and places it'),
    309: ('GetGil', False, 'returns FFX_SaveData_GetGil()'),
    310: ('AddGil', False, 'FFX_SaveData_SpendGil(-amount), clamped at 999999999, returns 1 on success'),
    311: ('SpendGil', False, 'FFX_SaveData_SpendGil(cost), deducts only when gil >= cost'),
    312: ('SetGil', False, 'FFX_SaveData_SetGil(amount) clamped 0..999999999, returns the stored gil'),
    317: ('LoadEventSoundSet', False, 'state machine: waits for the asset loader idle, sub_871750(ctx block, ctx+0x204, n), then sub_887550 (dword_132FF24 = 1), then counts 2'),
    318: ('SetRunsWhenGated', False, 'own actor +0x36 bit 15 = on, which lets it run while the context +3 gate is in mode 2'),
    319: ('GetDicString', False, 'pointer to string index (alt variant when the map name alt flag is set) of menu dictionary section for the current language'),
    320: ('BuildDicChoiceText', False, 'writes into destVar a choice text of count dictionary strings named by idsVar, colouring by enableMask and numbering enabled ones'),
    321: ('MesWinSetTextFromVar', False, 'window record +8 and +12 = address of textVar, +22 word = attr. The var buffer form of setting a message'),
    322: ('FindNthSetBit', False, 'returns the index of the n-th (0 based) set bit of mask, 0 when there are fewer'),
    323: ('StartFadeOut', False, 'flt_1328310 = -1/(frames/2) (or -1), then sub_861000(frames, 0)'),
    324: ('StartFadeIn', False, 'flt_1328310 = 1/(frames/2) (or 1), then sub_861000(frames, 127)'),
    325: ('SetGuideMapPos', False, 'FFX_RcBg_SetGuideMapPos: rcbgGuideSetPos(guide, a, b), the guide map screen centre. RCBG.md'),
    326: ('SetGuideMapSize', False, 'FFX_RcBg_SetGuideMapSize: rcbgGuideSetSize(guide, a). RCBG.md'),
    327: ('SetActorWordBC', True, 'own actor +0xBC = value OR 0x80000000 (0 stays 0), +0xB4 = -10000.0 (the init sentinel)'),
    328: ('SetActorWordBC', False, 'actor +0xBC = value OR 0x80000000 (0 stays 0), +0xB4 = -10000.0'),
    329: ('SetContextFlag80', False, 'sets or clears bit 0x80 of current context byte +2 (default on)'),
    330: ('SetFieldBootMode', False, 'FFX_Atel_SetFieldBootMode: g_ffxFieldBootMode = mode, dword_1326B50 = b, dword_1326B54 = c'),
    331: ('SetActorCharSoundBit', True, 'if actor+0x40 (party char) is not -1, sets or clears that bit in dword_132FF2C'),
    332: ('SetCharMask2CBit', True, "sets bit 'bit' of dword_132FF2C when on != 0, else clears it, returns on as 0 or 1"),
    333: ('GetTopLoveParamChar', False, 'scans the save LoveParam DWORD[8] at +0x2C and returns the index of the largest, the earlier index wins ties'),
    334: ('GetStickAngleOutsideDeadzone', False, 'centres two 0..255 axis bytes on 128, returns 0.0 when deadzone^2 > dx^2+dy^2, else atan2(axisA-128, axisB-128), 1e-6 when exactly 0'),
    335: ('IsAngleDiffWithin', False, 'wraps a-b into [-pi, pi] and returns tolerance > the difference'),
    336: ('SetCharMask2CSelf', True, "sets the bit for this actor's party character index (+0x40) in dword_132FF2C, no -1 check"),
    337: ('SetCharMask2C', False, "sets bit 'bit' of dword_132FF2C"),
    338: ('ClearCharMask2CSelf', True, "clears the bit for this actor's party character index in dword_132FF2C"),
    339: ('ClearCharMask2C', True, "clears bit 'bit' of dword_132FF2C"),
    340: ('SetCharMask34Self', False, "sets the bit for this actor's party character index in dword_132FF34, skipped when +0x40 is -1"),
    341: ('SetCharMask34', False, "sets bit 'bit' of dword_132FF34"),
    342: ('ClearCharMask34Self', False, "clears the bit for this actor's party character index in dword_132FF34, skipped when +0x40 is -1"),
    343: ('ClearCharMask34', False, "clears bit 'bit' of dword_132FF34"),
    344: ('SetActorSoundSlot', False, 'only in context 0: actor+0xAF = slot, bound CHR+0x184 = slot, dword_132FFA0[slot] = actor id when slot < 16'),
    345: ('SetActorSoundSlotOf', False, 'core:344 for the actor named by actorId, still gated on the calling context index being 0'),
    346: ('SetMesWinTextRaw', True, 'writes window record +0x08 and +0x0C = text as given (no message table lookup) and +0x16 = attr'),
    347: ('GiveTreasureWithMessage', False, "plays SE 0x80000026, decodes the treasure, grants item, key item, equipment and gil, shows 'You got X' in win"),
    348: ('SetSoundFlag4C', True, 'dword_132FF4C = on != 0, returns it'),
    349: ('SetFieldCtxFlag08', False, 'sets or clears bit 0x08 of context 0 byte +3 (byte_1325BA3) and returns whether it was set'),
    350: ('SetFieldCtxFlag40', False, 'sets or clears bit 0x40 of context 0 byte +3 (byte_1325BA3) and returns whether it was set'),
    351: ('GiveKeyItem', False, 'FFX_SaveData_SetKeyItemFlag(id), and for ids 0xA004..0xA01D also sub_906B30(id - 0xA004)'),
    352: ('TestKeyItem', False, 'returns FFX_SaveData_TestKeyItemFlag(id)'),
    353: ('TakeKeyItem', False, 'clears the key item flag through j_FFX_SaveData_ClearKeyItemFlag'),
    354: ('SetCharMask3CSelf', True, "sets the bit for this actor's party character index in dword_132FF3C"),
    355: ('SetCharMask3C', True, "sets bit 'bit' of dword_132FF3C"),
    356: ('ClearCharMask3CSelf', True, "clears the bit for this actor's party character index in dword_132FF3C"),
    357: ('ClearCharMask3C', True, "clears bit 'bit' of dword_132FF3C"),
    358: ('PlayBgm', False, 'sub_886A20(track): start path, per track volume byte 127, then current track dword_1327130 = track'),
    359: ('HoldCurrentBgm', False, 'FMOD command 25 run inline, sets track[cur].+4 = 1 to hold the current BGM. It clears nothing. SOUND_CLASSES.md'),
    360: ('SetBgmDriverParam130', True, 'calls sub_886950(v) -> driver sub_67A130'),
    361: ('SetBgmDriverParam0B0', True, 'calls sub_8868E0(a, b) -> driver sub_67A0B0'),
    362: ('SetBgmVolume', False, 'driver sub_67A290(track, vol) and byte_13300E0[track] = vol, both 0 while byte_1330090 is set'),
    363: ('FadeBgmVolume', False, 'driver sub_67A2D0(track, frames, vol) and byte_13300E0[track] = vol, vol 0 while byte_1330090 is set'),
    364: ('StopCurrentBgm', False, 'when the current track is >= 0 stops it (sub_886A60), then current track = -1'),
    365: ('StopBgm', False, 'stops the track (sub_886A60), and clears the current track id when it was this one'),
    366: ('SetSaveMapWordsB4B6', False, 'writes save word +0xB4 = a and +0xB6 = b'),
    367: ('SetCheckpointEntry', False, 'save +0xB8 = entry, +0xBA = current map id, then FFX_SaveData_SnapshotLocationHistory'),
    368: ('SetCheckpoint', True, 'save +0xB8 = entry, +0xBA = mapId, then FFX_SaveData_SnapshotLocationHistory'),
    369: ('RestoreCharHp', False, 'char record +0x3D = 0, then current HP (+0x1C) = max HP (+0x24) unless it is already above, returns current HP'),
    370: ('RestoreCharMp', False, 'current MP (+0x20) = max MP (+0x28) unless it is already above. Returns current HP (+0x1C) on the restore path'),
    371: ('GetActorPitch', True, "returns this actor's motion state float at +0x24 (pitch, paired with +0x30 by core:150)"),
    372: ('GetActorRotX', False, "returns this actor's motion state float at +0x30, the rotation X that core:38 and core:150 write"),
    373: ('GetActorPitchOf', True, 'core:371 for the actor named by actorId'),
    374: ('GetActorRotXOf', True, 'core:372 for the actor named by actorId'),
    375: ('SetActorRunsInBattle', False, 'actor +0x36 bit 0x40 = on, and when on also sets +0x36 bit 0x8000 (runs in a gated context), never cleared here'),
    376: ('TestSignalFlag', True, "returns bit 'bit' of dword_1326B70 (maybe_FFX_Atel_TestFlag)"),
    377: ('WaitSignalFlag', False, 'stores the bit in work, then polls dword_1326B70 every pass until the bit is set'),
    378: ('SetSignalFlag', False, "sets bit 'bit' of dword_1326B70. Scene 94 at story 390: bit 10 also clears dword_12FB8B4, bit 9 sets it to 1"),
    379: ('ClearSignalFlag', False, "clears bit 'bit' of dword_1326B70"),
    380: ('CueAndPlayBgmAlt', False, 'sub_8869E0(track) (driver 67A190), sub_8868B0(track) (start variant, volume 127), then current track = track'),
    381: ('PlayBgmAlt', True, 'sub_8868B0(track) (start variant, volume 127), then current track = track'),
    382: ('PlayBgmCrossfade', False, 'if sub_8863F0() >= 0 calls sub_886830(track, param), else sub_8869E0(track) and sub_886A20(track). Then current track = track'),
    383: ('SetCurrentBgmVolume', False, 'when a current track exists, sub_886AB0(current, vol)'),
    384: ('FadeCurrentBgmVolume', False, 'when a current track exists, sub_886B40(current, frames, vol)'),
    385: ('GetActorRotZ', False, "returns this actor's motion state float at +0x38, the rotation Z that core:39 and core:387 write"),
    386: ('GetActorRotZOf', True, 'core:385 for the actor named by actorId'),
    387: ('SetActorRotZ', True, 'wraps angle to [-pi, pi], sub_863FE0(actor, -1), motion +0x38 = angle, clears CHR flags1 0x400, kinds 4/5 push the pose to the map object'),
    388: ('SetBgmClampedParam', False, 'clamps v to -128..127 and calls sub_886A90 -> driver sub_67A250'),
    389: ('SetBgmClampedParamOf', False, 'clamps v to -128..127 and calls sub_886AA0(track, v) -> driver sub_67A270'),
    390: ('SetBgmClampedParamB', True, 'pops 2, drops the top one, clamps v to -128..127 and calls sub_886930 -> driver sub_67A0F0'),
    391: ('FadeBgmVolumeClamped', True, 'pops 3, drops the top one, clamps v to -128..127 and calls sub_886940(track, v) -> driver sub_67A2D0'),
    392: ('FadeOutCurrentBgm', False, 'frames > 0: fade out the current track, then start the queued dword_C526D0 track (id, volume). frames <= 0: stop and release it'),
    393: ('RequestMapZone', False, 'g_ffxPendingZoneMode = 2 and g_ffxPendingZonePlain = zone, a deferred map zone (section slot zone+2) change'),
    394: ('SetSignalFlagB', False, "sets bit 'bit' of dword_1326B74, the second per map signal mask"),
    395: ('ClearSignalFlagB', False, "clears bit 'bit' of dword_1326B74"),
    396: ('SetSePitchDirect', True, 'maps seId to bank and index (885D10/885D00) and calls SqsSePitchDirect'),
    397: ('SetSePitchTranslate', True, 'maps seId to bank and index and calls SqsSePitchTranslate'),
    398: ('SetSoundLevel134', True, 'byte_1327134 = level (map reset 127), driver sub_67A7B0(level, or 0 when sub_872340() is 0)'),
    399: ('FadeVoiceVolume', False, 'byte_1327134 = level, FMOD command 13, FmodVoice__beginVolumeFade. SOUND_CLASSES.md'),
    400: ('SetSoundLevel135', True, 'byte_1327135 = level (map reset 63), driver sub_67A6D0(level)'),
    401: ('FadeSoundLevel135', True, 'byte_1327135 = level, driver sub_67A6F0(frames, level)'),
    402: ('SetContextFloat1F0', False, 'writes the current ATEL context float +0x1F0 (reset to 0.0) and returns it'),
    403: ('PlaySe', False, 'if the SE is not playing starts it (pan 63, vol 127), else unless its table flag 0x40 ramps its volume to 127 (63 when save +0x28)'),
    404: ('SetRikkuAltOutfit', False, "save byte +0xD1 = on != 0, which makes Rikku's chr id 41 instead of 7"),
    405: ('WaitAtelBusyClear', False, 'poll only, returns 0 while dword_1326B58 != 0, else 5'),
    406: ('AddActorAttachFlags', False, 'for attach kinds 4 and 5: sub_862800 ORs flags and the +0xA8 id into a free one of 4 slots at actor+0xB18, then forwards by kind'),
    407: ('AttachActorToMapNode', False, 'actor +0xA8 = nodeId and +0xAA = 5, binding the actor to a map object. Does not release the previous attachment'),
    408: ('SetMapNodeAnim', False, "only when this actor's attach kind is 5: sub_9144A0(&g_ffxMapWork, nodeId, a, (s16)b)"),
    409: ('SetMapNodeAnimSelf', False, "core:408 with the actor's own +0xA8 node id, re-pushed and re-popped on the same stack"),
    410: ('BindChrToMapNode', True, 'pops 3, drops the middle one, and when a CHR is bound calls sub_9143C0(&g_ffxMapWork, nodeId, chr, param)'),
    411: ('BindChrToMapNodeSelf', True, "core:410 with the actor's own +0xA8 id as the node, the first pushed value is dropped"),
    412: ('Nop', False, 'does nothing and returns 1'),
    413: ('ReleaseActorAttach', False, 'by +0xAA: 1 frees child CHRs and the CHR (data too when disposeData), 2 closes the camera handle, 4/5 push the pose. Clears +0xA8/+0xAA'),
    414: ('CanBindPlayerLoose', False, 'pushes 0 then sub_86C530(0): returns (context byte 0 & 0x22) != 0'),
    415: ('CanBindPlayer', False, 'sub_86C530: ctx byte 0 & 0x22, and when strict also ctx+1 bit 4 clear and ctx+3 & 0x84 clear, the sub_86A130 bind test'),
    416: ('PlaySeWithPanVol', False, 'not playing: starts the SE with pan and vol. Playing and table flag 0x40 clear: ramps vol (halved when save +0x28) and pan over 12 frames'),
    417: ('SetMapNodeMorphWeight', True, 'pops 4 floats and a node index into a float[4] and calls sub_90A4D0(node, v)'),
    418: ('SetMapNodeMorphWeight', True, 'core:417 with one implicit trailing 0 pushed by the handler'),
    419: ('SetMapNodeMorphWeight', True, 'core:417 with two implicit trailing 0s pushed by the handler'),
    420: ('SetMapNodeMorphWeight', False, 'core:417 with three implicit trailing 0s, so only the weight is supplied'),
    421: ('SetMapTexAnimFrame', False, 'when animIdx < count and frame < its frame count: sets the pending bit (mapWork +645576) and the frame (+645580), stride 1824'),
    422: ('FillFieldPartyGaps', False, 'fills each empty (255) field party slot with the lowest index in-party character not already placed, then FFX_SaveData_SetFieldParty3'),
    423: ('GiveTreasureSilent', False, 'decodes the treasure and grants item, key item, equipment and gil with no message window'),
    424: ('GetCharBattleCounter54', False, 'returns the char record dword +0x54, the second of the four per character battle counters at +0x50..+0x5C'),
    425: ('GetCharBattleCounter0', True, 'returns dword +0x50 of character record charIdx, the battles-taken-part counter bumped at battle end'),
    426: ('GetCharBattleCounter2', True, 'returns dword +0x58 of character record charIdx, the third of the four per-character battle counters'),
    427: ('GetBattleCount', False, 'thunk to sub_784F50, returns g_ffxBattleCount'),
    432: ('LoadPremovie', False, 'unless premovie state (save +0xE0) is 2..5 starts a load (sub_643AA0), state 1, no -> +0xD8, mode -> +0xDC. Same no, new mode -> state 3'),
    433: ('CancelPremovie', False, 'sub_873DB0: premovie state (save +0xE0) = 6 and premovie number (+0xD8) = -1'),
    434: ('GetItemCount', False, 'FFX_SaveData_GetItemCount(itemId), held count minus one when it is the staged g_ffxItemInUse'),
    436: ('IsSoundLoadPending', True, 'returns 1 when HIDWORD(qword_132FF14) in the sound module is nonzero, else 0'),
    437: ('StartPremovie', False, 'sub_644280(1), then if premovie state (save +0xE0) is 2..4 calls sub_76F010 (a print stub on PC) and sets state 5, returns 1'),
    438: ('HasMarkedEquip', False, 'sub_7AD6E0: 1 when an in-use equipment entry has flag 0x08 set and 0x02 (locked) clear in g_ffxEquipmentArray'),
    439: ('OverwriteMarkedEquip', False, 'if a marked unlocked equip entry exists, sub_7AD640(arg): sub_798FF0(arg) then copies scratch entry 0x2310DDE over it keeping id and owner'),
    440: ('UnlockMarkedEquip', False, 'sub_7AD710(1): first in-use equip entry with flag 0x08 gets flag 0x02 cleared'),
    441: ('LockMarkedEquip', True, 'sub_7AD710(0): first in-use equip entry with flag 0x08 gets flag 0x02 set'),
    442: ('GetActorEquipModelId', False, 'armour clamped 0..1, reads party index actor +0x40, returns equipped armour (1) or weapon (0) chr id via sub_86B150'),
    443: ('IsBgmPlaying', False, 'FMOD command 22 run inline: FmodMusic+32 play pending OR track[cur].+32 playing. Cleared on the FMOD clock. SOUND_CLASSES.md'),
    444: ('SetMapNameOverride', False, 'FFX_SaveData_SetMapNameOverride: save word +0xD4 = nameId, byte +0xD6 = 1'),
    445: ('SetSaveFlagD7', False, 'sub_8715F0: save byte +0xD7 = (on != 0)'),
    446: ('GetSaveFlagD7', True, 'sub_86C8A0: returns save byte +0xD7'),
    447: ('SetFieldFlag1326B81', False, 'sub_8708D0: byte_1326B81 = 1'),
    448: ('SetFieldSlot1326CE0', False, 'slot clamped 0..3 (negative -> 0), dword_1326CE0[slot] = value'),
    449: ('SetPremovieState4', True, 'sub_873C40: premovie state (save +0xE0) = 4 when it is 2..5'),
    451: ('SetSaveDwordE4', False, 'sub_874030: save dword +0xE4 = value'),
    452: ('SetSaveFlagE8', False, 'sub_873DD0: save word +0xE8 = (on != 0), returns it'),
    453: ('RunMemCardCheck', False, 'clears ctx +0x210 bit 0x10, dword_1330230 = mode. Actor +0xA8 0x5045 starts save UI state 2 and a save file op. Result remaps a status'),
    454: ('ConsumeLatchC58E89', False, 'sub_88E5E0: returns byte_C58E89 and clears it'),
    455: ('GetSaveSizeKb', True, 'sub_8741D0: cached (sub_88D390(42, 0) + 1023) / 1024 + 32, a size in KB'),
    456: ('PlaySelfMapPartAnim', True, 'if actor +0xAA == 5, sub_9084B0(actor +0xA8, animId): anim >= 0 starts a map part anim, else stops it'),
    457: ('PlayMapPartAnim', False, 'sub_9084B0(partId, animId) on g_ffxMapWork: anim >= 0 starts part animation (sub_914920), negative stops it (sub_9149A0)'),
    458: ('SetSelfActorFlag38Bit4', False, 'own actor +0x38 OR= 4'),
    459: ('SetActorFlag38Bit4', True, 'actor(actorId) +0x38 OR= 4'),
    460: ('GetPlayingBgm', False, 'returns g_ffxSndPlayingBgm, the step side record of the playing track, core:382 tests it >= 0 to pick a crossfade. SOUND_CLASSES.md'),
    461: ('GetCurrentBgm', True, 'sub_873B00: returns dword_1327130, the track id core:487 records'),
    462: ('ShowSelfChrEffects', False, "for a CHR actor: sub_7FB290 starts the CHR's registry effect units (slots 4..7), sub_7FBAB0 clears the hide bit, actor +0x38 OR= 0x10"),
    463: ('HideSelfChrEffects', False, 'for a CHR actor: sub_7FBA40 sets the hide bit 4 in the CHR effect registry, actor +0x38 &= ~0x10'),
    464: ('SetSelfChrActorFlag38Bit8', False, 'own actor +0x38 OR= 8 when actor +0xAA == 1 (CHR backed)'),
    465: ('WaitFramesSkippable', False, 'start stores frames, poll returns 5 at once when maybe_g_ffxMovieStreamFromHdd (0xF3C913) is nonzero, else counts down'),
    466: ('CompareMessageToSaveString', False, 'strcmp of message msgId from the event message table (region variant per GetInternationalEnvId) with the save string at +0xEC'),
    467: ('ReturnOne', True, 'stub, returns 1'),
    468: ('SetSelfAttachedChrFlag800', False, 'if attached CHR actor +0xAF4[slot] exists and mask +0xB14 has the slot, sets its m_flags1 0x800 to on, stores on in bit 0 of +0xB08[slot]'),
    469: ('CallAssetLoaderSlot34', True, 'calls g_ffxAssetLoader[34](a, b)'),
    470: ('RequestVoiceFile', True, 'looks voiceId up in the voice table (sub_887790), resolves its file index via the asset loader (kind 25/27), calls loader [35] on it'),
    471: ('RequestFileIndex', True, 'calls g_ffxAssetLoader[35](fileIdx)'),
    472: ('SetTextOverlayFade', True, 'sub_876050: slot 0..7, layer 0..1 of the 8x2 overlay records at 0x1328150, levels 0..127, frames (negative = instant), state 2'),
    473: ('ResetRikkuNameDefault', False, 'copies kernel string group 3 id 0x202C (variant by the map name alt flag) over character name 6'),
    474: ('SetTextOverlay3Fade', False, 'sub_876050(3, layer, from, to, frames), the slot 3 form of core:472'),
    475: ('ClearTextOverlay', True, 'sub_876290: slot clamped 0..7, state byte 0 and both layer records cleared'),
    476: ('ClearTextOverlay3', False, 'pushes 3 and pops it, sub_876290(3)'),
    477: ('WaitTextOverlayIdle', True, 'poll returns 0 while sub_875DE0(slot) (state byte at 0x1328172 + 36*slot) is nonzero, else 5'),
    478: ('WaitTextOverlay3Idle', False, 'pushes 3 and pops it as the slot, then waits like core:477'),
    479: ('SetChrActorFlag38Bit8', True, 'actor(actorId) +0x38 OR= 8 when its +0xAA == 1'),
    480: ('SetSelfChrI408', False, 'for a CHR actor, FFX_Ch_SetI408: CHR +0x408 = value'),
    481: ('GetSelfChrI408', True, 'for a CHR actor returns FFX_Ch_GetI408'),
    482: ('EnableScreenWave', False, 'sub_9085A0: g_ffxMagicDrawFlags OR= 0x1000000, dword_18DED28 = dword_1934EA8 = 0, distortion enable sub_642F50(1)'),
    483: ('DisableScreenWave', False, 'sub_908450: g_ffxMagicDrawFlags &= ~0x1000000, dword_18DED28 = 0, distortion off sub_642F50(0)'),
    484: ('SetScreenWaveParams', False, 'sub_90A470: flt_1934E90/94/98 = a, b, c and dword_1934EA0 = mode'),
    485: ('SetScreenWaveColor', False, 'sub_90A400: bytes 0x1934EA4..0x1934EA6 = r, g, b'),
    486: ('SetScreenWaveTarget', False, 'sub_90A430: flt_1934E7C = a, flt_1934E9C = b, dword_1934E78 OR= 1, also OR= 2 when flag'),
    487: ('PlayBgm', False, 'sub_8869E0(track), sub_8868F0(track, mode) at volume 127, records track in dword_1327130'),
    488: ('SoundCmdById', True, 'sub_886D80(id): id -1 goes straight to sub_81E5D0(-1), else sub_885D10/sub_885D00 then sub_81E5D0'),
    490: ('SetScriptTextTable', True, 'resolves the variable descriptor and stores its address at context +0x44, the table core:492 reads'),
    491: ('ShowTextOverlay1FromSysTable', False, 'sub_875FB0(1, entry, table 0x13388C0): loads entry (<= 63) from the built in table into overlay slot 1, state 1'),
    492: ('ShowTextOverlayFromScriptTable', True, 'if context +0x44 is set, sub_875FB0(slot, entry, that table)'),
    493: ('ShowTextOverlay2FromScriptTable', True, "real infinite loop in the binary: prints a 'Virtuos Warnig: Need to check this func' stub, pops, pushes 2 and the entry, jumps back"),
    494: ('ClearTextOverlay1', False, 'pushes 1 and pops it, sub_876290(1)'),
    495: ('ClearTextOverlay2', True, 'pushes 2 and pops it, sub_876290(2)'),
    496: ('SetGlobalFloat1327124', False, 'sub_873E50: flt_1327124 = value'),
    497: ('GetGlobalFloat1327124', True, 'sub_873AD0: returns flt_1327124'),
    498: ('GetAeonPartyMask', False, 'bit i set for each character 8..15 (the aeons) with the in-party bit in its record'),
    499: ('BuildAeonChoiceText', False, 'writes the names of chars 8..15 in maskA OR maskB into the string var, maskB entries get numbered choice markup, returns their count << 8'),
    500: ('GetNthAeonInMask', False, 'returns the index of the n-th set bit of mask among bits 8..15'),
    501: ('SetVirtualPadEnabled', True, 'FFX_VirtualPad_SetEnabled(on != 0)'),
    502: ('SetVirtualPadButton', True, "FFX_VirtualPad_SetButtons(1 << bit), next frame's scripted pad mask"),
    503: ('SetGlobals1326B84', True, 'sub_86E840: dword_1326B84 = a, dword_1326B88 = b, dword_1326B8C = c'),
    504: ('PlayOrStopBgm', False, 'fade > 0 and track >= 0: sub_886AF0(track, fade). fade <= 0: stops the current BGM (sub_886A60, sub_886800 on dword_1327130)'),
    505: ('RemovePartyMemberLocked', False, 'runs core:203 (out of party, cleared from the field party) then FFX_SaveData_SetCharSelectable(charIdx, 0)'),
    507: ('GetSaveLoadMenuResultRaw', False, 'jmp 0x8BEEF0 = mov eax, dword_186A228, the word FFX_SaveLoadMenu_SetResult writes. Not a function in the IDB'),
    508: ('SetCharAbilityFlag', False, 'sub_874200(charIdx, abilityId) -> FFX_SaveData_SetCharAbilityFlag(charIdx, abilityId & 0xFFF OR 0x3000, a3)'),
    509: ('SetCharAbilityFlag', True, 'identical body to core:508'),
    510: ('TestCharAbilityFlag', True, 'maybe_FFX_SaveData_TestEventFlag(charIdx, abilityId): bit of record +0x3E words for 0x3000 ids, 0x3060 and up a shared bitmap'),
    511: ('SetSelfSoundMaskBits', True, 'if actor +0x40 != -1 sets bit (actor +0x40) in dword_132FF34 and dword_132FF3C (sub_887100/sub_887130)'),
    512: ('SetSoundMaskBits', False, 'sets bit idx in dword_132FF34 and dword_132FF3C'),
    513: ('ClearSelfSoundMaskBits', False, 'if actor +0x40 != -1 clears bit (actor +0x40) in dword_132FF34 and dword_132FF3C'),
    514: ('ClearSoundMaskBits', False, 'clears bit idx in dword_132FF34 and dword_132FF3C'),
    515: ('LoadAndSetActorModel', False, 'poll waits for idle attach/loader/sound, marks actor pending, reads motion set and rom data. resi runs SetActorModel, clears pending'),
    516: ('SetActorMotionF2C', False, 'own motion state block float +0x2C = value'),
    517: ('EnableSelfChrFxKind0', False, 'for a CHR actor sub_862700(actor, 0): starts the CHR effect set and kind 0, actor +0x38 OR= 0x20'),
    518: ('DisableSelfChrFxKind0', False, 'for a CHR actor sub_862680(actor, 0): stops kind 0, clears +0x38 0x20, stops the set when no kind is left'),
    519: ('EnableSelfChrFxKind1', False, 'for a CHR actor sub_862700(actor, 1): kind 1, actor +0x38 OR= 0x40'),
    520: ('DisableSelfChrFxKind1', True, 'for a CHR actor sub_862680(actor, 1): stops kind 1, clears +0x38 0x40'),
    521: ('EnableSelfChrFxKind2', False, 'for a CHR actor sub_862700(actor, 2): kind 2, actor +0x38 OR= 0x80'),
    522: ('DisableSelfChrFxKind2', False, 'for a CHR actor sub_862680(actor, 2): stops kind 2, clears +0x38 0x80'),
    523: ('SetSelfActorFlag38Bits104', False, 'own actor +0x38 OR= 0x100 and OR= 4'),
    524: ('SetActorFlag38Bits104', True, 'actor(actorId) +0x38 OR= 0x100 and OR= 4'),
    525: ('SetSaveWordEA', False, 'sub_870FA0: save word +0xEA = value'),
    526: ('GetSaveWordEA', True, 'sub_86C4B0: returns save word +0xEA'),
    527: ('SetEncounterSceneOverride', False, 'g_ffxAtelSceneIdOverride = sceneId, then resets the encounter accumulator and zone weights for the override (or the map scene when <= 0)'),
    528: ('SetWorldFlag', False, 'Sets bit flagId of g_ffxWorldFlagBits3DCC (saveData+0x3DCC, 1024 bits, clamped to byte 127) via FFX_SaveData_SetWorldFlag3DCC'),
    529: ('ClearWorldFlag', True, 'Clears bit flagId of g_ffxWorldFlagBits3DCC (saveData+0x3DCC) via FFX_SaveData_SetWorldFlag3DCC(id, 0)'),
    530: ('SetIndoorCameraHeightTest', False, 'Writes ctx+0x208 float = heightY and ctx byte +2 bit 0 = enable, the per-frame indoor test by camera height (camera y >= threshold)'),
    531: ('GetSelfBoneNodeIndex', False, "Pops boneId, pushes own actor id (+0x2E) and boneId, then does core:532: FFX_Ch_GetBoneNodeIndex on the caller's CHR when attach kind is 1"),
    532: ('GetActorBoneNodeIndex', True, 'FFX_Ch_GetBoneNodeIndex(actor->CHR, boneId) for an actor whose +0xAA attach kind is 1 and +0x9C CHR is set, else 0'),
    533: ('SetEquipAbilitiesFromTable', False, 'FFX_Equip_SetAbilitySlotsFromTable(kind 0..17, row 1..2): copies 4 auto-ability u16 from a fixed table into an equip record'),
    534: ('TeachAbilityWithMessage', False, 'If unit lacks the ability: cue, window with unit and ability names, waits confirm (Core 132), then resi sets the char ability flag'),
    535: ('ClearSpoils', False, 'FFX_Spoils_Clear: FFX_Spoils_Reset(g_ffxTreasureStaging, 7), empties the staged treasure / spoils list'),
    541: ('EquipFlaggedWeaponOnChar0', False, 'Lists char 0 weapons (sub_7ABD10(0,0,0)), equips any with equip entry +3 bit 0x08 into char 0 weapon slot, recomputes derived stats'),
    542: ('RunMapWorkOp913F50', False, 'Calls sub_90A420 -> sub_913F50(&g_ffxMapWork), an unread operation on the map work block'),
    543: ('ClearFieldMaskBit', True, 'Clears bit (when < 8) of dword_1326B90 via sub_870250(bit, 0)'),
    544: ('SetFieldMaskBit', False, 'Sets bit (when < 8) of dword_1326B90 via sub_870250(bit, 1)'),
    545: ('GetTablePtrC86E28', False, 'Returns entry index of the pointer table off_C86E28 unchecked, as an int'),
    546: ('GetTablePtrC8756C', True, 'Returns entry index of the pointer table off_C8756C unchecked, as an int'),
    547: ('SetOptionBit4AndApplyPad', True, 'Sets option flags (saveData+0x3D0C) bit 0x10 then sub_872480 pushes it to the pad layer via sub_8894D0(0,0,bit)'),
    548: ('GetOptionBit4', True, 'Returns bit 4 (0x10) of the option flags dword at saveData+0x3D0C'),
    549: ('SetFieldParam1326B6C', False, 'Stores value in dword_1326B6C via sub_870F60, which map load resets to 100'),
    550: ('SetGlobalFlagCEC1F0', False, 'Stores the bool in dword_CEC1F0 via sub_729050'),
    551: ('SetSaveWordE8', True, 'Writes the bool to the save block u16 at +0xE8 via sub_873DD0, a word map load zeroes'),
    552: ('GetTurnAngleScaled', True, 'abs(wrap(actor+0x884+0x44*level float - motion +0x34)), mode&2: /pi*halfScale, else /(pi/2)*quarterScale, clamped at 1.0 of the scale'),
    553: ('SetActorFlags38Bit200', False, "ORs 0x200 into the caller's actor flag word at +0x38"),
    554: ('SetActorFlags38Bit400', True, "ORs 0x400 into the caller's actor flag word at +0x38"),
    555: ('WaitFlag1A85F70', True, 'Counts down 10 polls, then polls sub_A547E0 (dword_1A85F70) until non-zero'),
    556: ('SetVirtualPadAxis', True, 'FFX_VirtualPad_SetAxis(axis 0..3, value byte): next frame virtual stick axis'),
    557: ('WaitPadPressed', True, 'Polls FFX_Atel_GetPadPressed (remapped, identity) until it shares a bit with mask'),
    558: ('SetIndoorEffectGroups', False, 'sub_870280 writes dword_1326CF0..CFC, the four placed effect groups the indoor switch stops and starts'),
    559: ('SetAttachSlotChrFlag8', False, 'If attach slot CHR (actor+0xAF4[slot]) is set and loaded (+0xB14 mask), sets its m_flags2 bit 0x8, and slot flag +0xB08[slot] bit 2'),
    560: ('SetActorFlags38Bit800', True, "ORs 0x800 into the caller's actor flag word at +0x38"),
    561: ('SetOtherActorFlags38Bit800', False, 'ORs 0x800 into the +0x38 flag word of actor actorId (same bit as core:560)'),
    562: ('PressVirtualPadButton', True, 'Holds virtual pad button mask 1<<bit for sub_A54750() = 2 polls via FFX_VirtualPad_SetButtons'),
    563: ('SetModuleDrawPrio', True, 'FFX_Module_SetDrawPrio: g_ffxModuleDrawPrio = prio'),
    564: ('CommitStagedPrimerGrants', False, 'If a grant is staged (dword_1325B90), sets key item 0xA004+i for each staged bit i < 26 not already owned, then clears the stage'),
    565: ('GetPrimerStageWord', False, 'Returns dword_1325B8C, a word next to the primer grant stage'),
    566: ('GetMessageTextPtr', False, 'Returns the message text pointer: ctx+0x38 table under the international env, else FFX_Atel_GetMessageText (ctx+0x34), builtin fallback'),
    567: ('BuildChoiceListText', False, 'Concatenates count strings from the pointer array at srcVar into destVar, tagging enabled entries (mask bits) with numbered choice codes'),
    568: ('TestEventFlagBit', True, 'Tests bit of g_ffxEventFlagBits (saveData+0x4C, 640 bits), 0 when out of range'),
    569: ('SetEventFlagBit', False, 'Sets bit of g_ffxEventFlagBits (saveData+0x4C, 640 bits) when in range'),
    570: ('SetOptionBit7AndApply', False, 'Sets option flags bit 0x80, calls sub_887180(bit7 ? 0 : 2 - bit0) (sound mode?), then reapplies the pad option bit 0x10'),
    571: ('TestWorldFlag', False, 'FFX_SaveData_TestWorldFlag3DCC(flagId): one bit of saveData+0x3DCC'),
    574: ('SetSoundFlag1330091', True, 'sub_886DD0: byte_1330091 = value != 0, a sound layer flag map load sets and clears'),
    575: ('SetGlobal1327F4C', False, 'sub_8749E0: dword_1327F4C = value'),
    576: ('LockEquippedGear', False, "FFX_SaveData_LockEquippedGear: sets or clears equip entry +3 bit 0x02 on the char's weapon and armour (char < 7)"),
    577: ('SetContextFlag210Bit8', False, 'Writes bit 0x08 of the ATEL context dword +0x210 (ctx+528), on by default from the context init'),
    578: ('GetMemCheckStatus', False, 'sub_777E90: returns 2 when byte_112A50A is set, otherwise byte_112A509'),
    579: ('GetMemCheckWord', False, "sub_7787B0: returns dword_112A518, next to core:578's status bytes"),
    580: ('ClearEventEffectFlag800', False, 'Clears bit 0x800 of dword_11A0080 (event effect flags) and refreshes each effect registry entry +22 from FFX_Ch_GetB185'),
    581: ('SetEventEffectFlag800', False, 'Sets bit 0x800 of dword_11A0080 (event effect flags)'),
    582: ('ClearEventEffectFlag2000', True, 'Clears bit 0x2000 of dword_11A0080'),
    583: ('SetEventEffectFlag2000', True, 'Sets bit 0x2000 of dword_11A0080'),
    584: ('ClearEventEffectFlag1000', True, 'Clears bit 0x1000 of dword_11A0080'),
    585: ('SetEventEffectFlag1000', True, 'Sets bit 0x1000 of dword_11A0080'),
    587: ('SetAttachSlotHidden', False, 'If attach slot is loaded (+0xB14 mask) and slot id +0xB04 != 255, FFX_Ch_SetHideBit1 on the slot CHR (+0xAF4[slot])'),
    588: ('SetActorFlags38Bit1000', False, "ORs 0x1000 into the caller's actor flag word at +0x38"),
    589: ('SetContextFlag210Bit40', False, 'Writes bit 0x40 of the ATEL context dword +0x210 (ctx+528), on by default from the context init'),
    590: ('SetContextField214', True, 'Writes the ATEL context dword +0x214 (ctx+532), which context init sets to -1'),
    591: ('GetPadRepeatRemapped', True, 'Returns FFX_Atel_GetPadWord28 (the auto repeat mask, wall clock timed) through FFX_Pad__remapButtonMask'),
    592: ('GetPadRepeat', True, 'Returns FFX_Atel_GetPadWord28, the wall clock auto repeat button mask'),
    593: ('TestPadRepeatBitRemapped', False, 'Tests 1<<bit in the remapped auto repeat mask (FFX_Atel_GetPadWord28)'),
    594: ('TestPadRepeatBit', True, 'Tests 1<<bit in the raw auto repeat mask (FFX_Atel_GetPadWord28)'),
    595: ('IsWesternMasterVersion', False, 'Reads the LanguageManager master version (sub_641100), 1 when it is not 0, 3 or 4 (0 JP, 3 Asia)'),
    597: ('GetSphereGridType', False, 'Returns option flags bits 14..15, the sphere grid type'),
    598: ('SetMessageWindowFlag8000', False, 'Writes 0x8000 into message window dword +0x28'),
    599: ('ClearMessageWindowFlag8000', False, 'Writes 0 into message window dword +0x28'),
    600: ('SwitchTextLanguageAndReload', True, 'If the bool differs from the international env id, sets option bit 0x10000, picks language 4 or 18, poll reloads the text and asset tables'),
    601: ('SetTextSpeedScale', False, 'FFX_Atel_SetTextSpeedScale: g_ffxAtelTextSpeedScale[index != 0] = scale'),
    602: ('GetTextLangVariant', True, 'Thunk to FFX_MesWin_GetTextLangVariant, no pops'),
    603: ('UnlockTrophy', False, 'FFX_Trophy_Unlock (Steam achievement), with 23 -> 22 and 25 -> 24 remaps and id 22 itself ignored'),
    604: ('ReturnZero', False, 'Stub, sub_86C270 returns 0'),
    605: ('SetGlobal1325AC4', False, 'Stores value in dword_1325AC4'),
    606: ('CallPresentation643E80', False, 'sub_643E80: sub_6D2390() then sub_6CE0C0(value), unread presentation side calls'),
    607: ('SetPresentationMode643500', False, 'sub_643500(value, mode): toggles dword_CE517C+1 and Phyre app +928, mode 0 with a zero record also starts sub_6E4FF0(3.0, 1, 0.0)'),
    608: ('PopOneReturnOne', False, 'Stub, pops 1 and returns 1'),
    609: ('PopOneReturnOne', False, 'Stub, pops 1 and returns 1'),
    610: ('ReturnOne', False, 'Stub, returns 1'),
    611: ('ReturnOne', False, 'Stub, returns 1'),
    612: ('SetSaveConfigBit5EC', False, 'FFX_Menu_SetSaveConfigBit5EC: saveData+0x5EC bit 0 = on, then applies it to the engine'),
    613: ('GetBlitzCheatEnabled', False, 'Returns g_ffxBlitzCheatEnabled, the debug page Blitz Cheat toggle'),
    614: ('GetSaveConfigBit5EC', False, 'FFX_Menu_GetSaveConfigBit5EC'),
    615: ('AskChoicePreviewConfigBit5EC', False, 'Core 315 ask composite, poll calls FFX_Menu_SetSaveConfigBit5EC(cursor) every frame so the highlighted option applies live, returns the pick'),
}
# END NAMES


def positional(i, slot):
    return "FFX_AtelSys_Core_%03d_%s" % (i, slot)


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
            skips.append("core:%d bad role %r" % (i, role))
            continue
        slots = [s for s in SLOTS if (i, s) in table]
        if not slots:
            skips.append("core:%d null row, nothing to name" % i)
            continue
        for slot in slots:
            ea = table[(i, slot)]
            cur = idc.get_name(ea) or ""
            if slot_suffix or len(slots) > 1:
                new = "FFX_AtelSys_Core_%03d_%s_%s" % (i, role, slot)
            else:
                new = "FFX_AtelSys_Core_%03d_%s" % (i, role)
            if cur == new:
                kept.append("core:%d %s 0x%X already %s" % (i, slot, ea, cur))
                continue
            if cur != positional(i, slot) and not PLACEHOLDER.match(cur):
                skips.append("core:%d %s 0x%X keeps its name %s (not positional)" % (i, slot, ea, cur))
                continue
            f = ida_funcs.get_func(ea)
            if f is None or f.start_ea != ea:
                skips.append("core:%d %s 0x%X is not a function start, make one first" % (i, slot, ea))
                continue
            if len(owners[ea]) > 1:
                skips.append("core:%d %s 0x%X is shared by %s" % (
                    i, slot, ea, ", ".join("core:%d %s" % o for o in owners[ea])))
                continue
            other = ida_name.get_name_ea(idaapi.BADADDR, new)
            if other != idaapi.BADADDR and other != ea:
                skips.append("core:%d %s name %s already used at 0x%X" % (i, slot, new, other))
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
