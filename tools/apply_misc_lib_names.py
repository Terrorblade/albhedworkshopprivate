"""
Apply the Sys, Sg, MapFx, Test and AbMap syscall library names from reversing/ATEL_MISC_LIBRARIES.md
to the FFX.exe IDB.

These are five of the ATEL syscall libraries, each a table of 16 byte rows {start, poll, float getter,
int getter}, registered by FFX_Atel_Init 0x86D6D0:

    lib  name   table     rows  registered at
    1    Sys    0xC52BE0    30  0x86D811
    4    Sg     0xC88D88    71  0x86D859
    8    MapFx  0xC5DC90   108  0x86D880
    9    Test   0xC5D8C0     1  0x86D841
    13   AbMap  0xC85EB0     1  0x86D88C

Every handler without a descriptive name carries a positional one, FFX_AtelSys_<Lib>_NNN_<slot> with
<slot> one of start, poll, resf, resi. This script reads each table out of the IDB, and for every row
in NAMES gives each non-null slot the name

    FFX_AtelSys_<Lib>_NNN_<Role>_<slot>        (default)
    FFX_AtelSys_<Lib>_NNN_<Role>               (--no-slot-suffix, single slot rows only)

The positional prefix stays, because a (library, function) pair is the script ABI.

It refuses to touch a slot whose current name is not a positional name of that row or a
sub_/loc_/nullsub_ placeholder. A positional name is FFX_AtelSys_<Lib>_NNN_<slot>, with or without the
zero padding, or FFX_AtelSys_<Lib>_NNN alone on a single slot row. MISNAMED lists exact signature
misnames that may also be replaced, and is empty for these five libraries. So the names an earlier pass
gave (sgMenuInit, FFX_AtelOp_MapParticleBindPos, atel_test_print and the like) are never overwritten.
It also skips a slot that is not the start of a function, a function that sits in more than one slot,
and a new name that already exists elsewhere.

Dead rows (no use in any shipped script) are left positional unless --include-dead is given.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. The script never saves the database, save it yourself afterwards.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_misc_lib_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--include-dead]
                                   [--no-slot-suffix] [--lib Sys,Sg,MapFx,Test,AbMap] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
INCLUDE_DEAD = False
SLOT_SUFFIX = True
VERBOSE = False

# lib -> (name, table, rows)
LIBS = {
    1: ("Sys", 0xC52BE0, 30),
    4: ("Sg", 0xC88D88, 71),
    8: ("MapFx", 0xC5DC90, 108),
    9: ("Test", 0xC5D8C0, 1),
    13: ("AbMap", 0xC85EB0, 1),
}
SLOTS = ("start", "poll", "resf", "resi")
PLACEHOLDER = re.compile(r"^(sub|loc|nullsub)_[0-9A-Fa-f]+$")
ROLE_OK = re.compile(r"^[A-Z][A-Za-z0-9]{1,47}$")
# (lib, index, slot) -> an exact signature misname that may be replaced. None known for these libraries.
MISNAMED = {}

# lib -> index -> (role, dead, one line meaning). Generated from reversing/ATEL_MISC_LIBRARIES.md.
# Not listed, because they keep their descriptive names: sg:29 (sgMenuInit, sgMenuExec, sgMenuResult),
# sg:49 (j_maybe_FFX_Player__isMode2), mapfx:10 (FFX_AtelOp_MapParticleBindPos), test:0
# (atel_test_print) and abmap:0 (abiritymap_debug). sg:2 is a null row.
# BEGIN NAMES
NAMES = {
    1: {  # Sys
        0: ('DebugWatchVar', False, "debug overlay entry: ctx+0xA4+8*slot (slot clamped 0..31) gets byte (x/8)&0x3F|0x80, byte (y/4)&0x7F, the caller's actor id (word at actor+46) and the raw var reference. A negative x or y clears the entry. Nothing in the binary reads ctx+0xA4..0x1A3 back"),
        1: ('Sin', False, 'sub_88C3A0, the CRT sin'),
        2: ('Cos', False, 'sub_88B3E0, the CRT cos'),
        3: ('Tan', True, 'sub_88C420, tan'),
        4: ('Atan', True, '(float)atan(v)'),
        5: ('Atan2', False, '(float)atan2(y, x). Only test23 calls it'),
        6: ('Sqrt', False, '(float)sqrt(v), the heaviest Sys row'),
        7: ('RSqrt', True, '1 / sqrt(v)'),
        8: ('FixedSin', True, 'sub_88C380: table sine from word_C54E40 through sub_88C690, 0x8000 units per turn'),
        9: ('FixedCos', True, 'sub_88C120: the table cosine twin of sys:8'),
        10: ('ToInt', False, 'returns FFX_Atel_PopInt, so a float operand comes back truncated toward zero'),
        11: ('IntShl15', True, 'v << 15'),
        12: ('FloatToFixed15', True, '(int)(v * 32768.0)'),
        13: ('RadToAngle32768', True, 'sub_88C040: radians to 0x8000 units per turn'),
        14: ('RadToAngle4096', True, 'sub_88BFE0: radians to 4096 units per turn'),
        15: ('Angle32768ToRad', True, 'sub_88C0E0, the inverse of sys:13'),
        16: ('Angle4096ToRad', True, 'sub_88C0A0, the inverse of sys:14'),
        17: ('Atan2Between2D', True, 'sub_88B430: the heading from one XZ point to another'),
        18: ('AnglesBetween3DToVars', True, 'sub_88B4D0 writes the yaw and pitch from point 1 to point 2 into two script variables and the row returns the yaw'),
        19: ('WrapAnglePi', False, 'sub_88BC20: wraps into -pi..pi'),
        20: ('TurnAngleToward', True, 'sub_88B570: steps one angle toward another by at most step'),
        21: ('InFacingCone', False, 'FFX_Atel_TestFacingCone with the six floats in push order'),
        22: ('InSphere', True, 'FFX_DistSqLE: 1 when the squared distance between the points is at most r * r'),
        23: ('SplineSampleToVar', True, 'sub_88A4A0 with two resolved script variable addresses, a count and t. The spline reading comes from the helper shape'),
        24: ('PolarToVecVar', True, 'sub_88A3A0 writes a vector built from two angles and a length into a script variable'),
        25: ('Fabs', False, '(float)fabs(v)'),
        26: ('Dist2D', False, 'sqrt of the XZ squared distance'),
        27: ('Dist3D', False, 'sqrt of the XYZ squared distance'),
        28: ('DebugWatchPtrMode0', False, 'sub_878140: ctx+0x1A4+8*slot (slot clamped 0..7) = the resolved address of var, or 0, then +4 = 0. No reader in the binary'),
        29: ('DebugWatchPtrMode1', True, 'as sys:28 with +4 = 1'),
    },
    4: {  # Sg
        0: ('ReturnZero', True, 'returns 0'),
        1: ('SetWaterLevel', False, 'FFX_Map_SetWaterLevel: g_ffxWaterLevel, read by FFX_Ch_UpdateMotionAll and the auto swim animation'),
        3: ('ScreenColorFade', False, 'sub_82CCA0: screen fade channel type 6 at 0x1300728 toward the colour'),
        4: ('FadeInFromBlack', False, 'FFX_ScreenFade_Request(frames, 1, 0), type 2'),
        5: ('FadeOutToBlack', False, 'FFX_ScreenFade_Request(frames, 0, 0), type 3'),
        6: ('FadeInFromWhite', False, 'FFX_ScreenFade_Request(frames, 1, 1), type 4'),
        7: ('FadeOutToWhite', False, 'FFX_ScreenFade_Request(frames, 0, 1), type 5'),
        8: ('SetScreenTint', False, 'sub_82CC20: additive tint, fade type 7, level 0 clears'),
        9: ('SetScreenTintSubtract', False, 'sub_82CC80: the subtractive twin, type 8'),
        10: ('SetMotionBlur', False, 'sub_82BEE0, the same blur helper came:125 reaches'),
        11: ('EndScreenFade', False, 'sub_82CC00: word_130074C = 0, the fade counter'),
        12: ('ClearScreenTint', False, 'sub_82CC70: drops the tint channel'),
        13: ('WaitScreenFade', False, 'start is empty, poll is sub_82CC10() == 0, the fade counter FFX_Sg_StepScreenFades counts down'),
        14: ('SetMapDrawEnabled', False, 'FFX_SetMapDrawEnabled, read by FFX_MainStep for the draw'),
        15: ('SetChrDrawEnabled', False, 'FFX_SetChrDrawEnabled, read by FFX_MainStep for the draw'),
        16: ('PopDiscard', True, 'pops three values and does nothing'),
        17: ('SetLightDir', True, 'sub_8364F0 on the directional light record i'),
        18: ('SetLightColor', True, 'sub_836400 on the directional light record i'),
        19: ('SetLight', False, 'sub_836520 -> sub_836310 and sub_836430 (magic host 589 and 590): colour 0..255 and direction in degrees for light i'),
        20: ('SetDofEnabled', False, 'sub_82CB60: dword_130076C = on & 1, the depth of field enable FFX_Sg_StepScreenFades reads'),
        21: ('SetDofDepth', False, 'sub_82CE20: dword_1300758[which] = z plus a bit in dword_1300768, the focus depths StepScreenFades hands to the renderer'),
        22: ('SetPlayerSpeedParam', False, 'FFX_Player__setSpeedParam: kind 1 walk, 2 run, 18 swim, 19 the second swim speed, one global each'),
        23: ('SetGroundTypeRemap', False, 'FFX_Map_SetGroundTypeRemap, read by FFX_Map_GroundTypeOf and the triangle block test'),
        24: ('GetGroundTypeRemap', True, 'FFX_Map_GetGroundTypeRemap'),
        25: ('BeginAccBufCapture', False, 'pops and drops one int, then sub_82BDF0 starts the accumulation buffer capture'),
        26: ('SetAccBufAlpha', False, 'sub_82BD10 (magic host 693): the accumulation blend level, tweened over frames, with a hard coded case for event 258'),
        27: ('SetOffscreenComposite', False, 'sub_82BCB0: byte_1300776, FFX_Gs_CompositeOffscreenBuffer'),
        28: ('SetMapToOffscreen', False, 'sub_82BE10: byte_1300777, FFX_Gs_RedirectMapToOffscreen'),
        30: ('DevMemOp', True, 'sub_8467B0: op 0 loads the dev maps sugimoto and kaisou from the host, 2 allocates 64 KB into dword_13036B8, 3 frees it and requests a CHR memory move'),
        31: ('FreeUnlockedChrCache', False, 'j_FFX_ChCache_FreeUnlockedEntries'),
        32: ('FreeStaleChrCache', False, 'j_FFX_ChCache_FreeStaleUnlocked'),
        33: ('FreeChrCacheForId', True, 'FFX_ChCache_FreeAllForId'),
        34: ('SetMotionKeepSpeed', False, 'FFX_Mot_SetKeepSpeed, one global the motion step reads'),
        35: ('AllocPointLights', False, 'sub_831950 (magic host 400): frees the old set, FFX_MemAlloc(44 * count) into dword_13008CC, count in dword_13008D0'),
        36: ('FreePointLights', False, 'sub_831A30'),
        37: ('SetPointLight', False, "sub_8319B0 fills point light record i. ebp.py's lgt"),
        38: ('SetPointLightPosMasked', True, 'sub_831CA0: the axes picked by mask'),
        39: ('GetPointLightPosToPtrs', True, 'sub_831B30 writes through raw pointers, not var references'),
        40: ('SetPointLightColor', True, 'sub_831C10'),
        41: ('GetPointLightColorToPtrs', True, 'sub_831A60 writes through raw pointers'),
        42: ('SetPointLightEnabled', False, 'sub_831C60'),
        43: ('GetPointLightEnabled', True, 'sub_831AD0'),
        44: ('SetPointLightRadius', True, 'sub_831CE0'),
        45: ('GetPointLightRadius', True, 'sub_831B80'),
        46: ('SetPointLightGroup', True, 'sub_831C80'),
        47: ('GetPointLightGroup', True, 'sub_831B00'),
        48: ('SetPlayerMode2', True, 'maybe_FFX_Player__setMode2'),
        50: ('SetPointLightFalloff', True, 'sub_831BF0, the field reading as falloff is from the record layout'),
        51: ('GetPointLightFalloff', True, 'sub_831A40'),
        52: ('SetMapAlphaPassFirst', False, 'FFX_SetMapAlphaPassFirst, draw order'),
        53: ('GetMapAlphaPassFirst', True, 'calls FFX_GetMapAlphaPassFirst and returns 0.0 anyway'),
        54: ('SetWind', False, 'sub_824E30: flt_12FBC44.., read by FFX_Ch_StepSecondaryPhysics'),
        55: ('GetWindToPtrs', True, 'sub_824DD0 writes through raw pointers'),
        56: ('DebugCacheDump', True, 'sub_83BB50, a Cache Dump print'),
        57: ('SetOffscreenClear', False, 'sub_82BDC0, read by FFX_Gs_BeginFrameDrawEnv'),
        58: ('SetChrToOffscreen', False, 'sub_82BE00: byte_1300778, FFX_Gs_SetChrFrameTarget'),
        59: ('SetFootstepSound', False, 'FFX_Map_SetFootstepSound, audio only'),
        60: ('SetWaterLevelMax', False, 'FFX_Map_SetWaterLevelMax'),
        61: ('SetUnreadByte13019B5', True, 'sub_83C520 writes byte_13019B5, which nothing reads'),
        62: ('SetPlayerFixedYaw', False, 'FFX_Player__setFixedYawDeg: g_ffxPlayerUseFixedYaw and g_ffxPlayerFixedYaw, which replace the camera yaw in stepControl'),
        63: ('SyncPlayerCamYaw', False, 'FFX_Player__syncCamYaw: g_ffxPlayerCamYaw = j_FFX_Came_GetYaw, heading offset 0'),
        64: ('SetPlayerHeadingOffsetDeg', False, 'FFX_Player__setHeadingOffsetDeg: latches the camera yaw into g_ffxPlayerCamYaw, then g_ffxPlayerHeadingOffset = wrap(deg) + g_ffxPlayerDesiredHeading'),
        65: ('ReturnZero', True, 'returns 0'),
        66: ('ReturnZero', True, 'returns 0'),
        67: ('SetBlobShadowParams', False, 'sub_830120, read by FFX_Ch_BuildBlobShadow'),
        68: ('PopDiscard3', False, 'pops three ints and does nothing'),
        69: ('SetSwimRiseEnabled', False, 'FFX_Player__setSwimRiseEnabled, read by stepControl'),
        70: ('SetScriptEffectsPassOff', False, 'FFX_SetScriptEffectsPassOff, read by FFX_MainStep'),
    },
    8: {  # MapFx
        0: ('SetMapPartMaskBit', False, 'sub_88EBA0 sets or clears one bit of the mask sub_908960 returns and stores it through sub_90A2B0 (dword_18DED0C, magic host 557 and 560, also written by the map loaders). In scene 392 bit 14 is forced on'),
        1: ('SetMagicDrawFlag10', False, 'FFX_MagicDraw_SetFlags or ClearFlags(0x10) plus sub_643DF0 -> sub_66E7B0 on the gfx context'),
        2: ('RestartOrStopGroup', False, 'restart nonzero: FFX_MapFx_ResetGroup. Zero: sub_729660 sets runtime +29 = 1 (stop) on every record of the placed particle set dword_D2C258 in that group'),
        3: ('StartGroupOneShot', False, 'sub_729810: countdown 0 and +29 = 2, which UpdatePlacedEffects turns into a stop when the countdown runs out'),
        4: ('WaitGroupGone', False, "pushes 1 and runs mapfx:5, so it waits until the group's first record is freed"),
        5: ('WaitGroup', False, 'poll on sub_72A760: keeps waiting while the record runs (+29 = 0), and with untilGone also while it is stopping, done when freed or missing'),
        6: ('SetZone', False, 'sub_874000: g_ffxPendingZoneMode = 1, g_ffxPendingZoneNotify = zone, and FFX_SaveData_SetSceneAndSub writes the zone into the save block. Level hash 5532 at story 2020 with zone 2 sets dword_193BF68. The poll returns 1, done at once'),
        7: ('ResetAllGroups', False, 'sub_729740: countdown 0 and +29 = 0 for every grouped record'),
        8: ('KillAllGroupedEffects', False, 'sub_7296A0: frees the prim list of every grouped record and marks it -4096'),
        9: ('BindGroupToSelf', False, 'sub_862800: up to four bindings at actor +710, then by actor kind: 1 a CHR (sub_72A530 with the bone), 4 a map part (sub_72A590), 5 a map node (sub_72A5E0)'),
        11: ('UnbindGroup', False, "sub_8628A0: drops that group (or every group for 0, negatives clamp to 0) from the actor's four slots and clears the attach record"),
        12: ('SetPlacedEffectsActive', False, 'sub_729050: dword_CEC1F0, the gate of FFX_MapFx_UpdatePlacedEffects. The same global as core:550'),
        13: ('SetGroupActive', False, 'second argument nonzero: FFX_MapFx_ResumeGroup (clears runtime flag 0x10, the group runs), zero: FFX_MapFx_SuspendGroup. Was named SetGroupHidden with the sense backwards, RCFLEX.md 7'),
        14: ('GetLayerCount', True, 'sub_91A3D0, the rcBg layer count (192 byte records)'),
        15: ('ShowLayer', False, 'sub_6412B0(FFX_GetLanguage(), level hash) can swap the layer for a language variant, then sub_91A830 clears hidden bit 0. Raises g_ffxTitleScreenActive in te/test20, events 347 and 212 widen the bound to 28'),
        16: ('HideLayer', False, 'sub_91A7C0: sets hidden bit 0, the same 347 and 212 case'),
        17: ('SetLayerPos', False, 'sub_91AB30: +12/+16 and moves the layer by the delta (sub_639D40). Layer 26 in scene 382 nudges y by one at two rows'),
        18: ('GetLayerX', True, '+12'),
        19: ('GetLayerY', True, '+16'),
        20: ('SetLayerField20', False, 'sub_91ACC0: +20. Only bltz0201'),
        21: ('GetLayerField20', True, '+20'),
        22: ('SetLayerFields36And40', True, 'sub_91AC40: +36 and +40'),
        23: ('GetLayerField36', True, '+36'),
        24: ('GetLayerField40', True, '+40'),
        25: ('SetLayerOrigin', True, 'sub_91AC80: +4 and +8, the pair the scale rows pass to sub_639D60'),
        26: ('GetLayerOriginX', True, '+4'),
        27: ('GetLayerOriginY', True, '+8'),
        28: ('GetLayerFrameCount', True, '+152'),
        29: ('SetLayerFrame', False, 'sub_91A930: the word at +150'),
        30: ('SetLayerFlag1', False, '+144 |= 1'),
        31: ('ClearLayerFlag1', False, '+144 &= ~1'),
        32: ('SetLayerFlag2', False, '+144 |= 2'),
        33: ('ClearLayerFlag2', False, '+144 &= ~2'),
        34: ('SetLayerFlagNibble', False, 'sub_919FE0: clears +144 bits 8..11 and ORs v in as given'),
        35: ('SetSceneCamOffset', True, 'sub_91AD40: scene +184/+188, then FFX_RcBg_ApplySceneCamera'),
        36: ('GetSceneCamOffsetX', True, 'scene +184'),
        37: ('GetSceneCamOffsetY', True, 'scene +188'),
        38: ('StartSceneTween', False, 'sub_91AD80: scene +192..+216 with the counter at +212 zeroed'),
        39: ('ResetSceneCamOffset', True, 'sub_91A900 -> sub_921200(0)'),
        40: ('SetSceneField172', True, 'sub_91AD10: scene +172, then ApplySceneCamera'),
        41: ('GetSceneField172', True, 'scene +172'),
        42: ('StartSceneTween220', True, 'sub_91AA00: scene +220..+240'),
        43: ('ResetSceneField172', True, 'sub_91A8D0 -> sub_920650(0)'),
        44: ('EnableSceneFlag0', False, 'sub_91A220: bit 0 of the rcBg object, then FFX_Gfx_RelinkMapMeshInstances'),
        45: ('DisableSceneFlag0', False, 'sub_91A200: clears bit 0, then sub_65F0E0'),
        46: ('SetLayerField164', False, 'sub_91A9C0: +164. Only hiku0500'),
        47: ('SetLayerLastFrame', False, 'sub_91A970: +150 = +152 - 1'),
        48: ('SetLayerByte67', False, 'sub_91AA90: sub_639D20 and the byte at +67'),
        49: ('GetLayerByte67', True, '+67'),
        50: ('SetGuideMapParam', False, 'sub_919DF0 -> sub_91D580 on the guide map model, about one call per package'),
        51: ('GetGuideMapParam', True, 'sub_919CA0 -> sub_91D450'),
        52: ('DebugDumpPlacedEffects', True, 'sub_919B70 -> sub_729060'),
        53: ('RestartOrStopGroup2', False, 'mapfx:2 keyed on the second group word (record +70)'),
        54: ('SetGroup2Active', False, 'mapfx:13 keyed on the second group word, active != 0 resumes. Was named SetGroup2Hidden with the sense backwards, MAP_PARTICLES.md 8'),
        55: ('SetFog', False, 'sub_90A350: flt_18DF148/14C scaled by flt_18DEFF8, flt_18DF144. Event 224 and event 320 (sub_91C8A0) also drive the renderer directly'),
        56: ('SetFogNear', False, 'ignored when the level hash is 3234, 7736 or 1689'),
        57: ('GetFogNear', False, 'sub_908BB0, which reads the Phyre application instead in event 224'),
        58: ('SetFogFar', False, 'the same three level skips'),
        59: ('GetFogFar', False, 'sub_908BB0'),
        60: ('SetFogLimit', False, 'the same three level skips'),
        61: ('GetFogLimit', False, 'sub_908BB0'),
        62: ('SetFogColor', False, 'FFX_MagicDraw_SetFogColor: dword_18DF140, pushed to the renderer in events 224 and 320'),
        63: ('SetFogColorR', False, 'reads the colour with sub_908980, replaces red, stores it back'),
        64: ('SetFogColorG', False, 'as mapfx:63 for green'),
        65: ('SetFogColorB', False, 'as mapfx:63 for blue'),
        66: ('GetFogColorR', False, 'sub_908980: red of dword_18DF140, then overwritten by the Phyre application float +96 (+80 under magic draw flag 4) times 255'),
        67: ('GetFogColorG', False, 'as mapfx:66, Phyre +100'),
        68: ('GetFogColorB', False, 'as mapfx:66, Phyre +104'),
        69: ('SetClearColor', False, 'maybe_FFX_MagicDraw_SetClearColor: byte_18DF178.., and sub_642E90 unless magic 706 is loaded'),
        70: ('SetClearColorR', True, 'sub_9086D0, replace red, store'),
        71: ('SetClearColorG', True, 'as mapfx:70 for green'),
        72: ('SetClearColorB', True, 'as mapfx:70 for blue'),
        73: ('GetClearColorR', False, 'sub_9086D0: byte_18DF178, scaled by the magic draw colour matrix when flags 1 and 4 are set'),
        74: ('GetClearColorG', False, 'as mapfx:73 for green'),
        75: ('GetClearColorB', False, 'as mapfx:73 for blue'),
        76: ('SetLayerScale', True, 'sub_91ABC0(i, s, s): +68 and +72 (0 keeps an axis), tween reset, sub_639D60'),
        77: ('SetLayerScaleX', True, 'sub_91ABC0(i, s, 0)'),
        78: ('SetLayerScaleY', True, 'sub_91ABC0(i, 0, s)'),
        79: ('TweenLayerScale', False, 'sub_91A150: tween of +68/+72 toward s over frames'),
        80: ('GetLayerScaleX', True, '+68'),
        81: ('GetLayerScaleY', True, '+72'),
        82: ('SetLayerPairB', True, 'sub_91AAE0: +76 and +80, tween counters zeroed. What the pair means is not traced'),
        83: ('SetLayerPairBX', True, 'sub_91AAE0 with y read back from +80'),
        84: ('SetLayerPairBY', True, 'sub_91AAE0 with x read back from +76'),
        85: ('TweenLayerPairB', True, 'sub_91A0F0: tween of +76/+80 through +100..+112, frames at +124'),
        86: ('GetLayerPairBX', True, '+76'),
        87: ('GetLayerPairBY', True, '+80'),
        88: ('BindGroupToPartyChr', True, 'sub_72A530 with the CHR maybe_FFX_Atel_PartyCharHasPendingScript returns for the party index'),
        89: ('BindGroupToMapPart', False, 'sub_72A590: attach flag 2'),
        90: ('UnbindGroupById', True, 'sub_72A120: clears the attach flags and target'),
        91: ('SetGroupPos', False, 'sub_72A630: identity matrix and the position in the attach record. The first pop (z) lands in the first vector slot, so the axis order in the record is reversed'),
        92: ('SetPostFxPreset', False, 'dword_25D5EB8 = on != 0, then sub_645E40 -> sub_66E850'),
        93: ('RestartAllEffectsAndResetLayerFrames', False, 'sub_7297E0 zeroes every countdown, which restarts every placed effect, then every layer frame goes to 0. Was named KillAll..., MAP_PARTICLES.md 5.2'),
        94: ('SelectGuideMapModel', False, 'FFX_RcBg_SelectGuideMapModel'),
        95: ('KillGroup', False, 'sub_7295F0: frees each record of the group'),
        96: ('KillGroup2', False, 'sub_729480: the same keyed on the second group word'),
        97: ('SetMagicDrawFlag100', True, 'sub_908580: g_ffxMagicDrawFlags |= 0x100 and dword_18DED2C = v, which nothing reads'),
        98: ('ClearMagicDrawFlag100', True, 'sub_908440'),
        99: ('SetUnreadVec1931208', True, 'sub_90A200 writes flt_1931208.., write only'),
        100: ('SetUnreadVec1931218', True, 'sub_90A230, write only'),
        101: ('IsMapPartValid', True, 'sub_9086B0: 0 when i is below dword_18DF588, else -1'),
        102: ('SetVfxTimeFrozen', False, 'sub_72A750: dword_230FD34. Only znkd0800'),
        103: ('LinkRcBgObjChrs', False, "sub_919A80: rcBg object 0x20008 gets the two actors' CHRs and the mode (sub_922EF0). Blitzball only"),
        104: ('StopRcBgObj', True, 'sub_919AB0 -> sub_922B50 on rcBg object 0x20008'),
        105: ('BindGroupToMapNode', True, 'sub_72A5E0: attach flag 8'),
        106: ('SetSceneField260', False, 'sub_91A260: scene +260'),
        107: ('SetPostFxPreset2', False, 'dword_25D5EB8 = 2 when on, then sub_645E40. Only one package'),
    },
    9: {  # Test

    },
    13: {  # AbMap

    },
}
# END NAMES


def positional_re(lib_name):
    return re.compile(r"^FFX_AtelSys_%s_0*(\d+)(?:_(start|poll|resf|resi))?$" % lib_name)


def is_positional(rx, cur, i, slot, single):
    m = rx.match(cur)
    if not m or int(m.group(1)) != i:
        return False
    if m.group(2) is None:
        return single
    return m.group(2) == slot


def main(dry_run=DRY_RUN, include_dead=INCLUDE_DEAD, slot_suffix=SLOT_SUFFIX, verbose=VERBOSE, libs=None):
    import ida_bytes
    import ida_funcs
    import ida_name
    import idaapi
    import idc

    # every slot target of every table, to find functions shared between slots or libraries
    owners = {}
    table = {}
    for lib, (lname, base, rows) in LIBS.items():
        for i in range(rows):
            for k, slot in enumerate(SLOTS):
                ea = ida_bytes.get_dword(base + 16 * i + 4 * k)
                if ea:
                    table[(lib, i, slot)] = ea
                    owners.setdefault(ea, []).append((lname, i, slot))

    plan = []
    skips = []
    kept = []
    nrows = 0
    for lib in sorted(LIBS):
        lname, base, rows = LIBS[lib]
        if libs and lname not in libs:
            continue
        rx = positional_re(lname)
        tag = lname.lower()
        names = NAMES.get(lib, {})
        nrows += len(names)
        for i in sorted(names):
            role, dead, meaning = names[i]
            if dead and not include_dead:
                continue
            if i >= rows:
                skips.append("%s:%d past the end of the table" % (tag, i))
                continue
            if not role or not ROLE_OK.match(role):
                skips.append("%s:%d bad role %r" % (tag, i, role))
                continue
            slots = [s for s in SLOTS if (lib, i, s) in table]
            if not slots:
                skips.append("%s:%d null row, nothing to name" % (tag, i))
                continue
            for slot in slots:
                ea = table[(lib, i, slot)]
                cur = idc.get_name(ea) or ""
                if slot_suffix or len(slots) > 1:
                    new = "FFX_AtelSys_%s_%03d_%s_%s" % (lname, i, role, slot)
                else:
                    new = "FFX_AtelSys_%s_%03d_%s" % (lname, i, role)
                if cur == new:
                    kept.append("%s:%d %s 0x%X already %s" % (tag, i, slot, ea, cur))
                    continue
                if (not is_positional(rx, cur, i, slot, len(slots) == 1) and not PLACEHOLDER.match(cur)
                        and MISNAMED.get((lib, i, slot)) != cur):
                    skips.append("%s:%d %s 0x%X keeps its name %s (not positional)" % (tag, i, slot, ea, cur))
                    continue
                f = ida_funcs.get_func(ea)
                if f is None or f.start_ea != ea:
                    skips.append("%s:%d %s 0x%X is not a function start, make one first" % (tag, i, slot, ea))
                    continue
                if len(owners[ea]) > 1:
                    skips.append("%s:%d %s 0x%X is shared by %s" % (
                        tag, i, slot, ea, ", ".join("%s:%d %s" % (o[0].lower(), o[1], o[2]) for o in owners[ea])))
                    continue
                other = ida_name.get_name_ea(idaapi.BADADDR, new)
                if other != idaapi.BADADDR and other != ea:
                    skips.append("%s:%d %s name %s already used at 0x%X" % (tag, i, slot, new, other))
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
          % (len(plan), len(skips), len(kept), nrows))

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
    ap.add_argument("--lib", default="", help="comma separated subset of Sys,Sg,MapFx,Test,AbMap")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    libs = set(x.strip() for x in a.lib.split(",") if x.strip()) or None
    import idapro
    if idapro.open_database(a.idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % a.idb)
    try:
        main(dry_run=not a.apply, include_dead=a.include_dead, slot_suffix=not a.no_slot_suffix,
             verbose=a.verbose, libs=libs)
    finally:
        idapro.close_database(save=False)


if __name__ == "__main__":
    if "idaapi" in sys.modules or "ida_kernwin" in sys.modules:
        main()
    else:
        headless()
