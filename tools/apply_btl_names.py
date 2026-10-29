"""
Apply the Btl syscall library names from reversing/ATEL_BTL_LIBRARY.md to the FFX.exe IDB.

The Btl library is ATEL library 7, 296 rows of {start, poll, float getter, int getter} at
g_ffxAtelSysFuncLib07_Btl 0xC42628, registered by FFX_Atel_Init 0x86D6D0. A script calls it with
operand 0x7NNN. Every handler without a descriptive name carries a positional one,
FFX_AtelSys_Btl_NNN_<slot> with <slot> one of start, poll, resf, resi. This script reads the table out
of the IDB, and for every row in NAMES gives each non-null slot the name

    FFX_AtelSys_Btl_NNN_<Role>_<slot>          (default)
    FFX_AtelSys_Btl_NNN_<Role>                 (--no-slot-suffix, single slot rows only)

The positional prefix stays, because a (library, function) pair is the script ABI.

It refuses to touch a slot whose current name is not a positional name of that row or a
sub_/loc_/nullsub_ placeholder. A positional name is FFX_AtelSys_Btl_NNN_<slot>, with or without the
zero padding, or FFX_AtelSys_Btl_NNN alone on a single slot row. The int getter of row 111 is also taken
over from the exact signature misname _wcstoul. So the names an earlier pass gave (the nine AI and
property rows, FFX_BtlAtelOp_SetUnitMoveSpeed) are never overwritten. It also skips a slot that is not
the start of a function, a function that sits in more than one slot, and a new name that already
exists elsewhere.

Dead rows (no use in any shipped script) are left positional unless --include-dead is given.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. The script never saves the database, save it yourself afterwards.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_btl_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--include-dead]
                              [--no-slot-suffix] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
INCLUDE_DEAD = False
SLOT_SUFFIX = True
VERBOSE = False

TABLE = 0xC42628
ROWS = 296
SLOTS = ("start", "poll", "resf", "resi")
PLACEHOLDER = re.compile(r"^(sub|loc|nullsub)_[0-9A-Fa-f]+$")
POSITIONAL = re.compile(r"^FFX_AtelSys_Btl_0*(\d+)(?:_(start|poll|resf|resi))?$")
ROLE_OK = re.compile(r"^[A-Z][A-Za-z0-9]{1,47}$")
# The int getter of row 111 forwards to sub_7A8FC0 with a trailing 1, which a signature pass once took for
# wcstoul (ATEL_CONTEXT.md 13.2). An IDB that still carries this exact name gets it replaced, any other
# name on this slot is kept.
MISNAMED = {(111, "resi"): "_wcstoul"}

# index -> (role, dead, one line meaning). Generated from reversing/ATEL_BTL_LIBRARY.md. Rows 11, 15, 16,
# 19, 24, 37, 90, 170, 171 and 279 keep their descriptive names and are not listed.
# BEGIN NAMES
NAMES = {
    0: ('ClearSelfByteDF2', False, 'self m_bDF2 = 0. sub_7AB360 sets m_bDF2 when it starts a unit event with a3 set'),
    1: ('SetBtlByteBDED', False, 'low byte of word_112BDED = on & 1. Reset by FFX_Btl_BeginBattle, no other reader found. 807 calls in 807 battle field files, one each'),
    2: ('RequestScriptedBattle', False, 'start calls FFX_Btl_RequestScriptedBattle(battleId, 1, returnMode). The poll returns 1, done without the continue bit, so the next instruction runs on the next step'),
    3: ('StartTurnKind2', False, 'sub_795EB0(self, 2, 0, -1, ramp, hold): a direction change of kind 2, total m_b42E = hold + 2 * ramp frames. Refused while m_directionFixFlag'),
    4: ('SetBtlFloatBDE4', True, 'unk_112BDE4 = v, read by sub_793BA0'),
    5: ('GetFieldAirborne', False, 'byte_112C9CE, copied from byte 3 of the battle field monster section by FFX_Btl_SetupFieldPointers. When set the unit position code skips the ground height snaps and the entrance effects take the floating variant, so a whole field of floating units. The reading is from its 20 readers'),
    6: ('StartTurnKind1IfFx', False, 'as btl:3 with kind 1, only when self m_directionChangeEffect is set'),
    7: ('PlayMotion', False, "sub_7A8FC0(.., 0): maps the motion kind through sub_7A8C80 (the unit's form bytes +0x408/+0x409, Magic 434 and 610 special case), FFX_Mot_SetByModeIndex, blend frames unless bit 0x8000. Returns the length scaled by the motion speed, 1 when nothing played"),
    8: ('WaitMotion', False, 'poll only. 0 while self m_b3F3 is set, 5 with no unit or no CHR, else sub_7A8C50: 0 while the CHR motion request (+0x736 m_motionRequestActive, read by the misnamed FFX_Mot_IsFinished) is active and BTLUNIT +0x434 is clear, 5 otherwise'),
    9: ('SetFloating', False, 'FFX_Btl_SetUnitGroundMode(self, on ? 0 : 1)'),
    10: ('SetYOffset', False, 'self m_yOffsetBase = base, m_b50C = mode, then sub_7AB300 recomputes m_yOffset (mode 0 centres on flt_112BDE8 minus half the height, mode 1 zero)'),
    12: ('StartMove', False, 'sub_7A9150(.., 0.0): pops eight values and calls FFX_Btl_StartUnitMove on self, nothing when frames <= 0'),
    13: ('GetTargetDirSector', False, 'resolves the move target point of kind targetKind (sub_7A9AC0) and returns sub_7A87F0, the direction to it quantised to one of sectors. A negative count with m_b44E set returns m_b44E < 0'),
    14: ('ApplyPendingHits', False, 'sub_78D980(self, sel, 0): FFX_Btl_ApplyPendingEffects on every selected unit, sub_78EE80 on the rest. With no hit landed and the self KO flag it kills self through FFX_Btl_OnUnitDefeated. Only system_01 calls it, once'),
    17: ('SetBtlByteA8FC', True, 'byte_112A8FC = on & 1, read by FFX_Btl_MainStep and the battle debug window'),
    18: ('ClearBusyFlags', False, 'self m_b435 = 0 and m_b432 = 0, except when m_bDFB is 2, which moves m_bDFB to 3 and keeps m_b432'),
    20: ('GetCommandExecWord', False, 'dword_112C8D8, written by FFX_Btl_BeginBattle and FFX_Btl_StartCommandExec, read by FFX_Btl_CtbTick'),
    21: ('DebugPrintInt', False, 'pops one value into a stubbed debug print. The second heaviest row, 20,814 calls'),
    22: ('SetSelfByte3F2', False, 'unless self m_b3F3 is set it runs sub_7AB6C0 (clears +0x432, +0x43E, +0x404), then m_b3F2 = v'),
    23: ('PlayEffectA', False, 'sub_79EB10(self, id, 7, param, 1) registers a kind 7 effect, and on success ++self m_bE12, which btl:81 waits on'),
    25: ('AiGetCurrentCommand', False, "FFX_BtlAi_GetCurrentEntryId(self): the ability of the issuer's current command, ids 12404..12407 mapped through the sub_7B19D0 slots"),
    26: ('GetAbilityField', False, 'sub_78CD30(self, ability, field), an ability record query, field 0..10'),
    27: ('AiOverrideCurrentCommand', False, "FFX_BtlAi_OverrideCurrentEntry(self, ability, mask): rewrites the issuer's current queued command"),
    28: ('SetMoveArcMode', False, "self m_b420 = mode, the 0..3 mode btl:118's timeline (sub_7B78C0) switches on"),
    29: ('GetMoveArcMode', False, 'self m_b420'),
    30: ('CountUnitsInBoth', False, 'units in both masks. Hard coded: with Magic 146 running, dword_12FB7E0 == 1 and a zero count on battles 0x235/0x236/0x238 it clears dword_12FB7E0'),
    31: ('SetSelfByte3F1', False, 'self m_b3F1 = v'),
    32: ('CountBusyTargets', True, 'sub_7A92C0(self, m_targetMask): target units other than self with m_b508, or with m_b436, also writes word_112CA08'),
    33: ('GetUnitTypeId', False, 'm_head.m_typeId of the selected unit'),
    34: ('SetAdvantage', False, 'byte 1 of dword_112C9DB, the advantage (0 normal, 1 pre-emptive, 2 ambush). btl:218 reads it'),
    35: ('GetAvgDistance', False, 'average over the mask of sub_7A9FC0(unit, 1), the distance from a unit to its kind 1 point less the radii'),
    36: ('GetBattleId', False, 'sub_782970, 0 outside a battle'),
    38: ('SetSelfByte44F', False, 'self m_b44F = v'),
    39: ('GetSelfByte44F', False, 'self m_b44F'),
    40: ('SetScale', False, 'self m_scale and FFX_Ch_SetScaleMasked(chr, 7, x, y, z)'),
    41: ('SetAirborne', False, 'self m_b505 = on & 1, the per unit form of btl:5. btl:265 sets it with a height'),
    42: ('GetSelfByte508', False, 'self m_b508, which btl:62 counts as busy'),
    43: ('IsMotionActive', False, '-1 while the self CHR motion request +0x736 is active (FFX_Mot_IsFinished, which returns that byte), the Btl form of ch:4'),
    44: ('StartArcMove', False, 'sets the move speeds and kind OR 0x80, solves the frame count from the distance and the acceleration, sets the yaw and pitch ramps, returns the direction sector of 16'),
    45: ('ResetMoveState', False, 'sub_7AB730: self +0x415 = 0 and the word at +0x417 = 0'),
    46: ('GetDistanceTo', False, "sub_7A9A40(self, kind, 0): distance to the kind's point less the point's own radius"),
    47: ('ResetUnit', False, 'm_b508 = 0, stops the status effects, resets the motion to its idle and the show vote, with leaveField m_onField = 0'),
    48: ('GetSelfByte412', False, 'self m_b412'),
    49: ('SetBusy', False, 'self m_b432 = 1, the flag btl:76 and btl:62 wait on'),
    50: ('SetHomeFacing', False, 'for each unit m_b512 = 1 and both m_homeFacing slots = the angle'),
    51: ('SetChrFlag80Inv', False, 'self m_head.m_chrFlag80Inv = on & 1'),
    52: ('ForceBattleResult', False, 'low byte of g_ffxBtlForcedResult = result, and sub phase 1 moves to 2 (BATTLE_START_AND_END.md)'),
    53: ('GetForcedResult', False, 'low byte of g_ffxBtlForcedResult'),
    54: ('FadeAlpha', False, 'sub_796550(self, from, to, frames, 1)'),
    55: ('AiQueueCommand', False, 'sub_7AC840(self, ability, sel, 1): appends to m_queuedAiCommand (4 deep) of each unit and inserts into the per unit list at dword_2310CD8'),
    56: ('AiUnqueueCommand', False, 'sub_7ACB40: removes the ability from both lists'),
    57: ('ResetActionState', False, 'sub_78CCA0 (+0x3F4 = 1, refresh status fx, sub_788E60) and sub_7A8F20, which clears the 9 entry once list btl:123 uses'),
    58: ('SetMotionSpeed', False, 'self m_motionSpeed'),
    59: ('SetAbilitySealed', False, 'sub_79B470 per unit: id 0x3NNN sets or clears its m_sealedAbilities bit, an id with no top nibble writes m_bDD4 of unit id'),
    60: ('FireHoldEventA', False, 'start fires a slot 62/63 battle event through sub_7981E0 and sets the high byte of g_ffxBtlEndResult to 2 (the CTB hold, BATTLE_CTB_AND_STATUS.md). The poll sub_7A8700 waits for the event thread'),
    61: ('ReleaseEventHold', False, 'high byte of g_ffxBtlEndResult = 0'),
    62: ('WaitBattleIdle', False, 'poll only. 0 while a poison is pending, the command queue is not empty, three phase bytes are set, or any on field unit has m_b508, m_b432 or m_bDFB, or any unit m_b436 or an action state. Writes the culprit to word_112CA08'),
    63: ('FireEventAndWait', False, 'start FFX_BtlEv_FireEvent(max(group - 1, 0), 255, event, 0xFFFF, 1, 2, actor), the poll waits for the event thread'),
    64: ('PlayEntranceEffect', False, 'sub_79E8C0(self, mode): picks effect 32, 33, 52 or 53 by btl:5, m_b505 and m_b5C0 and plays it'),
    65: ('Nop', False, 'returns 0'),
    66: ('ShowMessage', False, 'opens a battle message window (clamped 0..6) on the text at dword_112A9B8, entry msg, half picked by FFX_SaveData_GetMapNameAltFlag. sub_89B140 moves one window on event 389 for languages 2..5'),
    67: ('WaitMessageConfirm', False, 'start clamps the window. The poll returns 0 until the text phase is 3 and the confirm bit of g_ffxMenuPadHeldTrig is newly pressed (sub_8B8530), then closes the window'),
    68: ('CloseMessage', False, 'FFX_MesWin_Deactivate'),
    69: ('GetApproachDistance', False, 'sub_7A9FC0(unit, 12) / 2.5'),
    70: ('ClearPath', False, 'sub_7AB140: dword_1136418[slot] = 0, slots 0..2'),
    71: ('AddPathPointKind', False, 'sub_7AB0B0: appends the self point of kind to path slot (32 deep)'),
    72: ('AddPathPoint', False, 'sub_7AB0B0 with an explicit point'),
    73: ('StartPathMove', False, 'sub_7AB010: move kind 7 along the path for frames'),
    74: ('CountMovingUnits', False, 'units in the mask with m_moveKind != 0'),
    75: ('RunUnitEventAndWait', False, "start runs sub_7AB360 on every unit, the unit's battle event (enemy groups 3/64/4, party 5 + unit), poll waits for the thread"),
    76: ('WaitUnitsIdle', False, 'start resolves the mask, poll waits until no unit in it has m_b432'),
    77: ('Nop', True, 'returns 0'),
    78: ('SetLeaveOnDeath', True, 'self m_leaveOnDeath and m_leaveOnDeathDefault'),
    79: ('SetByteDDD', True, 'm_bDDD = v for each unit'),
    80: ('ReloadUnitsAndWait', False, 'start runs sub_7A89C0: FFX_Btl_LoadUnitParams, the re-entry reset, CTB 3 x tick, shade reset, status fx and motion 39 per unit (BATTLE_CTB_AND_STATUS.md). The poll waits for the motion event'),
    81: ('WaitEffectA', False, 'poll only, waits for self m_bE12 == 0'),
    82: ('AttachToUnit', False, '+0x43F = parent, +0x441 = bone, clears show vote 0, places the unit, ++parent m_hasAttachedUnits'),
    83: ('StartMoveArg', False, "btl:12 with the extra float as sub_7A9150's a4"),
    84: ('SnapToBone', False, "the unit's position = the bone world position of the other unit's CHR"),
    85: ('SetShowVote0', False, 'FFX_Btl_SetFlag100Vote(self, 0, on & 1)'),
    86: ('SetSpecialBlock', False, 'sub_79DE60(mode): mode 1 allocates a 0x870 byte block into dword_11360A4'),
    87: ('StartTurnKind', False, 'as btl:3 with the kind from the script, the float is popped and dropped'),
    88: ('IsPlayingMotionKind', False, "compares the unit's current motion index +0x3F0 with sub_7A8C80(unit, kind)"),
    89: ('GetDistanceToFlat', False, 'btl:46 with the height ignored'),
    91: ('GetUnitDepth', False, 'sub_794A70: the CHR depth extent times the z scale'),
    92: ('GetUnitHeight', False, 'FFX_Ch_GetF4F4(chr) * m_scale[1]'),
    93: ('RequestShowFx', False, 'pops two and drops them, self m_head.m_pendingShowFx = 1'),
    94: ('ResetChrState', True, 'sub_788E60: m_head.m_b1F = 0, FFX_Ch_SetI408(chr, 0), sub_7FBCD0'),
    95: ('DebugPrintFloat', False, 'pops one float into a stubbed print'),
    96: ('RefreshStatusFx', True, 'sub_79EFF0(self), re-registers the status effects'),
    97: ('StopStatusFx', False, 'FFX_Btl_StopStatusFx(self)'),
    98: ('PlayEffectB', False, 'sub_79EBA0(self, id, param), the kind 8 twin of btl:23, counted in m_bE13'),
    99: ('WaitEffectB', False, 'poll only, waits for self m_bE13 == 0'),
    100: ('PrepareVoice', False, 'start stores id in dword_112C24C, the poll waits for a sound phase (sub_7A8C20) and the sound loader (sub_7B2C10), the getter sets dword_11363BC = (id == 0x41FF0004) and loads the voice with sub_887570. Nothing resets dword_11363BC'),
    101: ('StartVoice', False, 'poll waits like btl:100, the getter sub_887630 starts the prepared voice at volume 63/127'),
    102: ('StopVoice', False, 'sub_7B2C00 -> sub_887780'),
    103: ('IsVoiceBusy', False, 'sub_7B2BE0: the voice status word or the sound status'),
    104: ('WaitVoiceDone', False, 'poll only, returns 1 once sub_7B2BE0 is 0, else 0'),
    105: ('PickUnitStream7', False, 'FilterAndPickTarget(unit 0, .., stream 7, out). Draws stream 7 when more than one survives'),
    106: ('IsSoleTarget', True, "0 when the unit's m_targetMask is exactly itself"),
    107: ('SetUnitPartHidden', False, 'FFX_Ch_ModelSetHide(chr, part, on & 1)'),
    108: ('PlayUnitSound', True, "plays m_soundHandles[index] of the unit's primary through sub_7A0A30 unless it equals dword_112C8E0, the last played handle"),
    109: ('PlayUnitCue', False, 'start stores both, the poll waits for the sound idle (sub_887900), the getter plays byte_C42510[8 * unit + index] through sub_8875D0. Skipped on a btl:5 field'),
    110: ('RunUnitAltEventAndWait', False, 'start runs sub_7A0720 per unit (battle events 79 + unit and 77/78), poll waits'),
    111: ('PlayMotionFlagged', False, 'btl:7 with +0x40A = 1. The IDB prototype is still the wcstoul one'),
    112: ('SetMenuFlagOn', False, 'sub_89BC60(1): byte_C58EF4, read by FFX_BtlMenu_DrawRoot'),
    113: ('SetMenuFlagOff', False, 'sub_89BC60(0)'),
    114: ('ShowSubtitle', False, 'the btl:66 text with a fixed layout (sub_89B230, flags 0x2002). The voiced line pattern pairs it with 100 to 104, 169 and 222'),
    115: ('AttachWeapon', False, 'sub_793330(self, side): parents the side record CHR to its bone on self'),
    116: ('DetachWeapon', False, "sub_793F10: unparents it at the bone's world position"),
    117: ('RunWeaponEventAndWait', False, "btl:75 on each unit's weapon stand in unit"),
    118: ('StartArcTimeline', False, 'records the start position, builds an 8 point path with sub_7B78C0 by m_b420 and starts it with sub_7AB010(2, ..)'),
    119: ('GetApproachFrames', False, 'min(distance / 20 + 4, 20)'),
    120: ('GetAbilityFieldOf', False, 'btl:26 on another unit'),
    121: ('ReloadCharRecord', False, 'restores the auto statuses, reloads the character record, sub_78D7F0, applies the auto statuses, refreshes the status fx'),
    122: ('GetHitResult', False, 'sub_79A920(unit, its first target), the hit log entry kind'),
    123: ('PlayUnitSe', False, 'sub_79FD40(unit, id), id 9 only once per unit through the sub_7A8920 list'),
    124: ('WaitUnitSoundLoaded', False, 'poll only, waits while the primary unit has sound loads issued and m_soundReady is clear'),
    125: ('SetDebugParam', False, 'the battle debug switch: 0 FullItem, 1 to 9 and 12 to 20, 23, 24 debug bytes, 3 g_ffxBtlDbgMonInput, 10 sphere levels and gil, 11 fills equipment, 21 FFX_Btl_LoadPlayerData, 25 g_ffxBtlForcedAdvantage. Called by testbattle and two shipped fields'),
    126: ('GetDebugParam', False, 'reads the same bytes back'),
    127: ('ResetToHomePose', False, 'sub_7AB2A0: rotation to the home facing, position to the target point'),
    128: ('SwapUnit2Parts', False, 'sub_8720E0 on the unit 2 CHR: hides model part 1 or part 2'),
    129: ('WaitFlagC022', False, 'poll only, waits for the low byte of word_112C022 == 1'),
    130: ('PlayEffectKind1', False, 'sub_79EB10(self, id, 1, param, 1)'),
    131: ('AttachChrEffect', False, 'with on > 0, m_bE15 set and none yet, registers an effect on the self CHR (sub_7FC9B0) into m_iEA8'),
    132: ('ReleaseChrEffect', False, 'sub_788E30: releases the +0xEA8 effect'),
    133: ('StartMoveFlag418', False, 'btl:12, then +0x418 = 1'),
    134: ('SpendCommandCost', False, 'FFX_Btl_SpendCommandCost(self)'),
    135: ('PlayStatusSe', False, 'sub_79FCF0 -> sub_79FE80 / sub_7A08A0'),
    136: ('PlayUnitSeOnce', False, 'sub_7A0100: plays id OR 0x80000000 unless it is dword_112C8E4, the last one'),
    137: ('PlayRawSe', True, 'sub_7A00D0 -> sub_7A08A0(id OR 0x80000000)'),
    138: ('ResetZoneWeights', False, 'sub_782B40 -> FFX_Btl_ResetEncounterZoneWeights. Only camtest0 calls it'),
    139: ('GetZoneNibble', False, 'sub_782940'),
    140: ('SetZoneNibble', False, 'sub_782BA0: sets the zone nibble and resets the weights. Event packages call it'),
    141: ('GetWordC9E2Low', False, 'low byte of word_112C9E2'),
    142: ('TestGroundHold', False, 'sub_7AA100 recomputes m_b508, with status 0 and m_b505 it forces ground mode 1 and +0x4F0 = 10 around the call'),
    143: ('GetHomeDirSector', False, 'sub_7A8720(self, sectors, 1): the home facing delta quantised'),
    144: ('PlayEffect12', True, 'sub_79E590: effect 12 through the dword_112A95C CHR, ++m_wE1E'),
    145: ('CopyBattleName', False, 'copies the 40 byte m_battleName and m_bDD5'),
    146: ('GetHeightAboveGround', False, '(ground height - y) * 100'),
    147: ('CanAct', False, 'sub_791780: +0x41C, +0x41D set, +0xDE7, +0x508, +0x432 clear and not the poison pending actor'),
    148: ('FadeAlphaMode', False, 'btl:54 with the mode from m_b423'),
    149: ('GetMoveFramesLeft', False, 'sub_7A9320: frames left on the current move'),
    150: ('GetByteC9FA', False, 'byte_112C9FA, written by begin and FFX_Btl_FinishActions'),
    151: ('FireHoldEventB', False, 'btl:60 with the hold value 1'),
    152: ('HasStatusBits616', False, 'sub_7AF2F0: the +0x616 status word has 0x2800 or 0x40'),
    153: ('SetBtlByteC9CD', False, 'byte_112C9CD = v & 3, reset at begin, read by sub_780CE0 for the menu module 17 call'),
    154: ('IsNotPoisonPending', True, 'true when self is not g_ffxBtlPoisonPendingActor'),
    155: ('GetUnitY', True, 'the unit position y'),
    156: ('GetPitchBetween', False, "the elevation from one mask's point to the other's"),
    157: ('WaitSavedPosCleared', False, 'poll only, waits while self m_savedPosValid'),
    158: ('FireMotionEvent', False, 'FFX_Btl_OnMotionEvent(unit, bit): sets the event bit and runs the hit, the effect or the reaction that bit stands for'),
    159: ('GetHomeFacingDelta', False, 'the home facing minus the current rotation'),
    160: ('SetByteC9FC', False, 'byte 0 of dword_112C9FC = on & 1'),
    161: ('GetUnitOrderCode', False, "an enemy's m_unitIndex, or -6 - its index in g_ffxBattlePartyOrder, 255 none"),
    162: ('LoadSystemMgrp', False, 'start sub_79CCF0 starts the read of the system motion group through the asset loader, the poll waits for byte_112C9CF'),
    163: ('PlayMotionForced', False, 'btl:7 with m_chrReady forced on for the call, then m_b3F3 = 1 when it played'),
    164: ('ReleaseSystemMgrp', False, "sub_79CCA0 frees the block. The clock hit is the allocator's one time heap cookie"),
    165: ('SetBtlPoint', False, 'qword_112A8D0 xyz, w 1.0, byte 1 of dword_112C9FC = 1'),
    166: ('SetEscapeMode', False, 'byte 3 of dword_112C9FC: 0 roll, 1 never, 2 always (BATTLE_START_AND_END.md)'),
    167: ('GetMoveParamOf', True, 'sub_7B2C90: attack start distance, offset, back jump, the run speeds, weight, attack height'),
    168: ('SetUnitFloatOf', False, 'FFX_BtlAi_SetUnitFloat on another unit'),
    169: ('WaitMessageOrVoice', False, 'start clamps the window. The poll closes it on a confirm press, or by itself once the text phase is 3 and sub_8864B0 says no voice is busy'),
    172: ('GetMoveParam', False, 'btl:167 on self'),
    173: ('AnyEquipFlag8', False, 'sub_7AD6E0: any equipment entry in use with flag 8 and without flag 2'),
    174: ('GetWordC9E2High', False, 'high byte of word_112C9E2'),
    175: ('HasFieldGroundSection', False, 'sub_7986A0: field monster section byte 0 set and btl:5 clear'),
    176: ('GetAttackFrames', False, 'the attack motion frame, scaled by the motion speed, plus the run up frames over the approach distance'),
    177: ('DebugPrintInt2', False, 'pops one value into a stubbed print'),
    178: ('SetSelfFloat', False, 'FFX_BtlAi_SetUnitFloat(self, index, v)'),
    179: ('SetDwordC8F0', False, 'sub_7B2B60: dword_112C8F0 = v'),
    180: ('HideSideChr', False, 'm_bF65 = 8, sub_79EA90, side record CHR alpha 0'),
    181: ('ClearW400', False, 'self m_w400 = -1'),
    182: ('GetApproachTime', False, 'sub_7A9FC0(unit, 12) / the self run speed'),
    183: ('ShowSubtitleAlt', False, 'btl:114 with flags 0x6002 (sub_89B290)'),
    184: ('SetEscapeAllowed', False, 'g_ffxBtlEscapeAllowed'),
    185: ('GetMaskPointX11', True, 'sub_795170(11, mask) x'),
    186: ('GetMaskPointX10', True, 'sub_795170(10, mask) x'),
    187: ('StartHoldMove', False, 'self m_moveKind = 16 for frames'),
    188: ('WaitEffectAOf', False, 'btl:81 on another unit'),
    189: ('WaitEffectBOf', False, 'btl:99 on another unit'),
    190: ('GetUnitYaw', False, 'the unit rotation'),
    191: ('SetAttachScale', True, 'self m_attachScale and FFX_Ch_SetAttachScale'),
    192: ('GetUnitYOffset', False, 'maybe_FFX_Btl_GetUnitYOffset'),
    193: ('GetDistanceBetween', True, 'distance between the kind 1 points of two masks'),
    194: ('GetYawBetween', True, "the yaw from one mask's point to the other's"),
    195: ('GetHomeFacingOf', False, 'sub_7958C0, the home facing'),
    196: ('HasEquipFlag8', True, "either of the unit's two equipment entries has flag 8"),
    197: ('IsGameOverSeq', False, 'byte 1 of g_ffxBtlGameOverSeq != 0'),
    198: ('GetTableInt1136B18', True, 'dword_1136B18[index], 16 entries'),
    199: ('GetTableFloat1136AD8', True, 'flt_1136AD8[index], 16 entries'),
    200: ('PlayStatusSeA', True, 'sub_79FDA0'),
    201: ('PlayStatusSeB', False, 'sub_79FDF0'),
    202: ('PlayUnitSeLoop', True, 'sub_7A0140, the no dedupe form of btl:136'),
    203: ('PlayUnitSeOnceAwake', True, 'sub_7A0170: btl:136 unless m_statusCount[11]'),
    204: ('SetTargetMask', False, 'self m_targetMask = mask'),
    205: ('AddTargetMask', False, 'self m_targetMask OR= mask'),
    206: ('RemoveTargetMask', False, 'self m_targetMask &= ~mask'),
    207: ('GetFieldSectionByte1', False, 'byte 1 of the battle field monster section'),
    208: ('SetBtlByteC9DE', False, 'byte 3 of dword_112C9DB = on & 1'),
    209: ('GetEffectACount', False, 'm_bE12 of the unit'),
    210: ('GetEffectBCount', True, 'm_bE13 of the unit'),
    211: ('PlayEffectAOf', False, 'btl:23 on another unit'),
    212: ('PlayEffectBOf', True, 'btl:98 on another unit'),
    213: ('SetSummoner', False, 'self m_summoner'),
    214: ('PreviewDamage', True, 'FFX_Btl_DamageFormula with the random flag 0, so no draw'),
    215: ('PlayLinkReaction', False, "sub_78CA20, the link unit's reaction motion, unless the link unit is KO"),
    216: ('SetUnit13PartFlag', False, 'byte_112CA06 = on & 1'),
    217: ('ApplyUnit13PartFlag', False, 'FFX_Ch_ModelSetHide(unit 13 CHR, 1, byte_112CA06)'),
    218: ('GetAdvantage', False, 'byte 1 of dword_112C9DB'),
    219: ('GetUnit13PartFlag', True, 'byte_112CA06'),
    220: ('SetBattleNameFromMsg', False, "copies 40 bytes of message msg into the unit's m_battleName, half picked by the map name alt flag"),
    221: ('SetDwordC8FC', True, 'dword_112C8FC, read by sub_792F20'),
    222: ('GetConfigBit9UnlessFfwd', False, 'FFX_Btl_GetOptionBit9UnlessFfwd: 0 while the booster battle fast forward is on, else bit 9 of the save battle config at 0x113079C. Every voiced line branches on it'),
    223: ('IsBattleScene', False, 'a battle is running and its scene word equals battleId >> 16'),
    224: ('CanCounter', False, "sub_7B24A0(self, 0, 0, 1) and the issuer's m_bDF1 clear"),
    225: ('IssuerCounterAllowed', True, "the issuer's m_bDF1 clear"),
    226: ('SetTexAnim4', False, 'FFX_Ch_SetTexAnimPattern(chr, 4, v)'),
    227: ('AllocScriptBlock', False, 'allocates size bytes into dword_112BE98, keeps the first size in dword_112BE9C'),
    228: ('GetHitReaction', True, "sub_79A860: the defender's hit log reaction for the attacker's current command"),
    229: ('PlayFieldEffectA', True, 'sub_79ECA0'),
    230: ('PlayFieldEffectB', False, 'sub_79EC40'),
    231: ('YojimboPickTargets', False, 'sub_7996D0: a group pick on stream 5 (sub_799910) or a FilterAndPickTarget on stream 4, by the ability flag, into self m_targetMask'),
    232: ('GetGil', False, 'FFX_SaveData_GetGil'),
    233: ('GetYojimboWord', False, 'sub_7B2B30: dword_1130838'),
    234: ('SetYojimboWord', False, 'sub_7B2B40'),
    235: ('RandStream17', False, 'FFX_Rand_Stream(17)'),
    236: ('GetItemCount', True, 'FFX_SaveData_GetItemCount'),
    237: ('AddItem', False, 'FFX_SaveData_AddItem'),
    238: ('PickZanmatoTier', False, 'a + b against 32 / 48 / 63 / 80 gives 12510..12514'),
    239: ('SetChrFxParam', False, 'sub_79E4F0 -> sub_7FBDF0(chr, v)'),
    240: ('GetBattleFrameCounter', True, 'g_ffxBattleFrameCounter as a float'),
    241: ('ClearAiList', False, 'fills the self 20 word list at dword_2310CD8 + 1144 * unit with 0xFF'),
    242: ('AppendAiList', False, 'writes into the first free word of that list'),
    243: ('RandPercent', False, 'FFX_Btl_RandPercentCheck: with forcedId and word_1136410 set it compares them and draws nothing, else stream 18 against threshold'),
    244: ('PreviewTargets', True, 'sub_7A8B40: self m_targetMask from sub_799790, 0 when the MP cost check fails'),
    245: ('GetMpCost', True, 'FFX_Btl_GetCommandMpCost (CHEAT_BATTLE.md)'),
    246: ('ClearValueQueue', False, 'byte_11363EC = byte_11363ED = 0'),
    247: ('PushValueQueue', False, 'appends to the 16 entry word_11363F0'),
    248: ('PopValueQueue', False, 'next entry, 0 when empty'),
    249: ('PickWithFilter', True, 'sub_7A8F60: FilterAndPickTarget on stream 4, then sub_799A10 and the last pick mask'),
    250: ('SetForcedRollId', False, 'word_1136410, which makes btl:243 and 251 compare instead of draw. Nothing else writes it, not even battle begin'),
    251: ('MagusPickTarget', False, 'sub_7A8B40 preview, then RandPercentCheck on stream 18, then sub_7A8F60 on stream 4'),
    252: ('GetForcedRollId', False, 'word_1136410'),
    253: ('SetCmdRange714', False, '+0x726/+0x727 = y, +0x728/+0x729 = x, and +0x714 = 1 unless btl:250 is set'),
    254: ('SetCmdRange714Ex', False, '+0x714 = 1, +0x729 = a, +0x727 = b, +0x728 = c, +0x726 = d'),
    255: ('PublishTargetMask', False, 'dword_112C8D0 = self m_targetMask'),
    256: ('AnyUnitBusy', False, 'sub_7A8BE0: any unit in the mask with m_b432'),
    257: ('GetDbgFullSet', False, 'g_ffxBtlDbgFullSet'),
    258: ('SetNeckTarget', False, 'neck look-at: m_neckAimDelay +0x439 = 1, m_neckTarget +0x438 = the other unit or -1'),
    259: ('UnitLookAtPoint', False, "sub_8350A0: CHR look at kind 3 on the point, look at on (the Ch library's ch:123)"),
    260: ('PushTripleQueue', False, 'sub_79D2A0, an 8 entry queue at word_1136074'),
    261: ('WaitByteE17', False, 'poll only, waits for self m_bE17 != 0'),
    262: ('TestCharEventFlag', False, 'maybe_FFX_SaveData_TestEventFlag(unit, flag)'),
    263: ('GetFacingStep', False, '(home facing - m_facing) / pi * scale'),
    264: ('StartTurnKind1AltIfFx', False, 'btl:6 with a3 = 1'),
    265: ('SetAirborneMask', False, 'per unit m_b407 = 1, ground mode 0 when noGround, m_b505 = 1, m_b50C = 0, m_yOffsetBase = base'),
    266: ('TestAndSetGate', False, 'byte_112CA10, byte 1 of the four flag bytes at 0x112CA0F, set to 1, returns the old value'),
    267: ('WaitGate', False, 'poll only, waits for that byte != 0'),
    268: ('DecCounterCA0D', False, 'high byte of dword_112CA0A, decremented while positive'),
    269: ('ClearByte411', False, 'self m_b411 = 0'),
    270: ('GetSlotByte0', True, 'first non zero sub_7B19D0 slot, byte 0'),
    271: ('GetSlotByte1', True, 'same slot, byte 1'),
    272: ('EnableHeadSway', False, 'sub_834770(chr, 1): CHR +0x4D4, the random head sway of ch:118. With dword_11363BC set unit 5 becomes 0'),
    273: ('DisableHeadSway', False, 'sub_834770(chr, 0), the same hack'),
    274: ('StartTurnKindAlt', False, 'btl:87 with a3 = 1'),
    275: ('SetScriptMode1', False, 'g_ffxBtlScriptMode = 1'),
    276: ('SetScriptMode2', False, 'g_ffxBtlScriptMode = 2, which also suppresses the statistics and the steal and drop handlers (MONSTER_ARENA.md)'),
    277: ('CancelTurn', False, 'sub_795E70: with a turn running, m_b42B = 12'),
    278: ('PlayStatusLoopFx', False, 'sub_79E510: the Sleep or Silence style loop by status-2 bits 0x40 and 0x2000'),
    280: ('SetGuardByteCA11', False, 'byte_112CA11, the KO path guard SUMMONS.md left open'),
    281: ('SetCountersAllowed', False, 'g_ffxBtlCountersAllowed 0x112CA12, 1 at battle start, 0 refuses every counter (apply_btl_counter_names.py)'),
    282: ('SetChrVec804', False, 'sub_824C90: CHR +0x804..+0x80C, then sub_824CC0(chr, 1)'),
    283: ('SetByteCA13', True, 'byte 0 of dword_112CA13'),
    284: ('SetWordC94E', False, 'word_112C94E, read by FFX_Btl_EndPhaseStep'),
    285: ('SetBlobShadowParam', False, 'sub_830CB0: CHR +0x17C, the ch:133 field'),
    286: ('GetByteCA15', True, 'byte 2 of dword_112CA13'),
    287: ('ShowMessage2', True, 'btl:66 on the second text table dword_112A998, gated by sub_784E40 and dword_1841D20'),
    288: ('ShowSystemMessage', False, 'sub_79D090: message type 7 from the second table'),
    289: ('ShowSubtitle2', True, 'btl:183 on the second table'),
    290: ('EnableHeadSwayPlain', True, 'btl:272 without the unit 5 hack'),
    291: ('AttachWeaponUnit5', False, 'sub_793330(5, side)'),
    292: ('SetDwordC8F4', False, 'dword_112C250 = v, then sub_7B2B50 sets dword_112C8F4 when sub_784E40'),
    293: ('AttachWeaponUnit4', False, 'sub_793330(4, 0) when self is unit 4'),
    294: ('MarkUnitB3E', False, 'm_head.m_b3E = 1 and both side records +0x3E = 1'),
    295: ('SetMonsterShowVote', False, 'sub_7969A0: the show vote of monster unit slot + 20'),
}
# END NAMES


def positional(i, slot):
    return "FFX_AtelSys_Btl_%03d_%s" % (i, slot)


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
            skips.append("btl:%d bad role %r" % (i, role))
            continue
        slots = [s for s in SLOTS if (i, s) in table]
        if not slots:
            skips.append("btl:%d null row, nothing to name" % i)
            continue
        for slot in slots:
            ea = table[(i, slot)]
            cur = idc.get_name(ea) or ""
            if slot_suffix or len(slots) > 1:
                new = "FFX_AtelSys_Btl_%03d_%s_%s" % (i, role, slot)
            else:
                new = "FFX_AtelSys_Btl_%03d_%s" % (i, role)
            if cur == new:
                kept.append("btl:%d %s 0x%X already %s" % (i, slot, ea, cur))
                continue
            if (not is_positional(cur, i, slot, len(slots) == 1) and not PLACEHOLDER.match(cur)
                    and MISNAMED.get((i, slot)) != cur):
                skips.append("btl:%d %s 0x%X keeps its name %s (not positional)" % (i, slot, ea, cur))
                continue
            f = ida_funcs.get_func(ea)
            if f is None or f.start_ea != ea:
                skips.append("btl:%d %s 0x%X is not a function start, make one first" % (i, slot, ea))
                continue
            if len(owners[ea]) > 1:
                skips.append("btl:%d %s 0x%X is shared by %s" % (
                    i, slot, ea, ", ".join("btl:%d %s" % o for o in owners[ea])))
                continue
            other = ida_name.get_name_ea(idaapi.BADADDR, new)
            if other != idaapi.BADADDR and other != ea:
                skips.append("btl:%d %s name %s already used at 0x%X" % (i, slot, new, other))
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
