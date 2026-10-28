"""
Apply the Movie and Save syscall library names from reversing/ATEL_MOVIE_SAVE_LIBRARIES.md to the
FFX.exe IDB.

Movie is ATEL library 11, the table g_ffxAtelSysFuncLib11_Movie 0xC40E30. It has 16 rows, not the 146
the older notes give. 0xC40F30, where row 16 would start, is off_C40F30, a flat table of 4 byte
pointers that the sa_menu code indexes, and everything after it up to 0xC416E0 is menu globals and
more pointer tables. An earlier pass still gave the functions those pointers reach positional Movie
names, FFX_AtelSys_Movie_016_start up to FFX_AtelSys_Movie_145_resi, 449 of them. They are not
syscall handlers and no script can reach them (no shipped script calls Movie past 13).

Save is ATEL library 12, g_ffxAtelSysFuncLib12_Save 0xC52DD8, 94 rows. Row 94 would be the string
"/ffx/proj/event/header/eventid.bin". Row 10 is null.

Every row is {start, poll, float getter, int getter}. For every row in the NAMES tables below this
script gives each non-null slot the name

    FFX_AtelSys_<Lib>_NNN_<Role>_<slot>        (default)
    FFX_AtelSys_<Lib>_NNN_<Role>               (--no-slot-suffix, single slot rows only)

with <Lib> Movie or Save. The positional prefix stays, because a (library, function) pair is the
script ABI.

It refuses to touch a slot whose current name is not a positional name of that row or a
sub_/loc_/nullsub_ placeholder. A positional name is FFX_AtelSys_<Lib>_NNN_<slot>, with or without
the zero padding, or FFX_AtelSys_<Lib>_NNN alone on a single slot row. The Movie 0 poll is also taken
over from the exact signature misname __RTC_NumErrors_800 (a return 5 stub). Any other name is kept.
It also skips a slot that is not the start of a function, a function that sits in more than one slot
of the same table, and a new name that already exists elsewhere.

Dead rows (no use in any shipped script) are left positional unless --include-dead is given.

--clear-false-movie-rows also removes the 449 false positional Movie names past row 15, and the
"ATEL script syscall handler. Reached as library 11 (Movie) function N" repeatable comment on the same
functions. A name is only removed when it is exactly FFX_AtelSys_Movie_NNN_<slot> with NNN 16 or more
and the pointer at 0xC40E30 + 16 * NNN + 4 * slot really is that function, so a name somebody gave by
hand is never touched. A comment is only removed when it starts with that exact sentence for the same
NNN. The functions go back to sub_XXXXXX.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. The script never saves the database, save it yourself afterwards.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_movsave_lib_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--include-dead]
                                      [--no-slot-suffix] [--clear-false-movie-rows] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
INCLUDE_DEAD = False
SLOT_SUFFIX = True
CLEAR_FALSE_MOVIE_ROWS = False
VERBOSE = False

SLOTS = ("start", "poll", "resf", "resi")
PLACEHOLDER = re.compile(r"^(sub|loc|nullsub)_[0-9A-Fa-f]+$")
ROLE_OK = re.compile(r"^[A-Z][A-Za-z0-9]{1,47}$")

MOVIE_TABLE = 0xC40E30
MOVIE_ROWS = 16
# The rows the older notes counted, 16..145, which are not rows (see the docstring).
MOVIE_FALSE_ROWS = range(16, 146)
FALSE_COMMENT = "ATEL script syscall handler. Reached as library 11 (Movie) function %d,"
SAVE_TABLE = 0xC52DD8
SAVE_ROWS = 94

# Positional slots that a signature pass misnamed (ATEL_CONTEXT.md 13.5). An IDB that still carries
# this exact name gets it replaced, any other name on the slot is kept.
MOVIE_MISNAMED = {(0, "poll"): "__RTC_NumErrors_800"}
SAVE_MISNAMED = {}

# index -> (role, dead, one line meaning). Generated from reversing/ATEL_MOVIE_SAVE_LIBRARIES.md.
# BEGIN MOVIE NAMES
MOVIE_NAMES = {
    0: ('StartMovie', False, 'start: g_ffxFmvCurrentVideoId = videoId, FFX_Fmv_StartVideo(videoId, 0), FFX_Fmv_SetFullscreenMode(1), flags & 0x2000 sets mgr+0x720 through sub_6437F0(1) so the natural end and the skip no longer stop the movie, dword_C40E2C = 1, g_ffxFmvScriptRunning = 0. The poll returns 5, the getter is a print stub'),
    1: ('WaitMovieStopped', False, 'start zeroes the work word. poll: 0 while FFX_Fmv_IsPlaying or FFX_Fmv_IsDecoderBusy, then g_ffxFmvScriptRunning = 0, dword_C40E2C = 1, sub_63DF30 destroys the player and it returns 5. getter: when FFX_Fmv_CurrentMovieNeedsCameraRestore (videos 32, 46, 70, 72 with the gfx byte) clears the matrix override, sets the movie screen mode 0 and calls sub_6F6F80, then g_ffxFmvCurrentVideoId = -1'),
    2: ('MovieStatus', False, 'movie_status_resulti, prints a PS2 name through SG_Printf_STUB and returns 0'),
    3: ('GetMovieFrame', False, 'FFX_Fmv_GetFrameIndex 0x6410D0 -> mgr+1752, which FFX_Fmv_StepPresentation and FFX_Fmv_UploadFrameJob write as (int)(player time ms * 29.97 / 1000) after each consumed frame. 65534 once the movie stops'),
    4: ('StopMovie', False, 'start: sub_6437F0(0) clears mgr+0x720. poll: sub_643E50 clears mgr+1744 IsPlaying, then the camera restore, VideoId -1, g_ffxFmvScriptRunning = 0, dword_C40E2C = 1, returns 1 (done, yield). getter: sub_63DF30 destroys the player'),
    5: ('MoviePause', True, 'movie_pause, prints a PS2 name through SG_Printf_STUB and returns 0'),
    6: ('MovieChannel', True, 'movie_ch, prints a PS2 name through SG_Printf_STUB and returns 0'),
    7: ('MovieVolume', True, 'movie_vol, prints a PS2 name through SG_Printf_STUB and returns 0'),
    8: ('MovieStartDebug', True, 'movie_start_debug_init, _exec and _resulti print stubs. The poll returns 0, so a call would never finish'),
    9: ('WaitMovieFirstFrame', False, 'poll: 0 while FFX_Fmv_IsDecoderBusy or FFX_Fmv_PlaybackFinished is 0, else 5. PlaybackFinished is mgr+1764 while a movie plays, and mgr+1764 is set after the FIRST consumed frame, so this waits for the movie to appear (or to stop). getter: g_ffxFmvScriptRunning = 1, FFX_Fmv_SetFullscreenMode(0), FFX_Fmv_ApplyMovieCamera'),
    10: ('StartMovieWaitFirstFrame', False, 'start: FFX_Fmv_StartVideo(videoId, flags & 0x80) and only when it returns nonzero (only a failed sampler create, so never on a normal start, see MOVIE_DECODE.md) FFX_Fmv_SetFullscreenMode(0), g_ffxFmvScriptRunning = 1, dword_C40E2C = 1, g_ffxFmvScriptBlocking = 1. poll: 0 while Blocking and (decoder busy or no first frame), else 5 through _RTC_NumErrors_803, a return 5 stub'),
    11: ('WaitMovieEndOrSkip', False, "poll: FFX_Fmv_IsFinishedOrSkippedGlobal, 5 when the movie ended or g_ffxFmvSkipRequested is set. getter: for video 52 prints 'Force destroy fmv player for movieId:%d' and calls sub_63DF30, then tail calls the movie:1 getter (camera restore, VideoId -1)"),
    12: ('GetMovieLengthFrames', False, 'movie_end_frame_resulti. sub_6D91D0: (int)(sub_A27C40 duration ms * 29.97 / 1000), the length of the open file in frames, 0 with no player'),
    13: ('HideSceneOverMovie', False, 'dword_C40E2C = 0. sub_653960 at 0x653BFD tests it and draws the scene pass over the movie only while it is set, every other Movie row sets it back to 1'),
    14: ('MovieFrameDrawPath3', True, 'movie_op_frame_draw_acc_path3, prints a PS2 name through SG_Printf_STUB and returns 0'),
    15: ('MovieFramePal', True, 'movie_frame_pal_resulti, prints a PS2 name through SG_Printf_STUB and returns 0'),
}
# END MOVIE NAMES

# Row 10 is null and not listed.
# BEGIN SAVE NAMES
SAVE_NAMES = {
    0: ('DevDumpSaveBlock', True, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. Opens host0 atelsaveram000520.dat for writing (sub_879C60) and writes 8684 bytes of the save block (sub_879E50)'),
    1: ('DevLoadSaveBlock', True, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. Opens the same file for reading (sub_879CF0) and reads 0x21EC bytes over the save block (sub_879DE0)'),
    2: ('SetMapNameAltFlag', False, 'sub_785F40: option flags bit 0x08 = on, the flag FFX_SaveData_GetMapNameAltFlag reads to pick the place name string'),
    3: ('LoadSceneBackgroundAlt', False, 'FFX_Ev_LoadSceneBackgroundAlt 0x86DCA0: FFX_SaveData_SetSceneAndSub then FFX_Map_LoadFieldMapSync, a synchronous load inside the step'),
    4: ('DevCaptureStart', True, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_83A4F0(a, b), the SgCapture start'),
    5: ('DevCaptureArm', True, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_83A580(a)'),
    6: ('DevCaptureStop', True, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_83A570'),
    7: ('SetDebugScreenParams', False, 'sub_88EBF0: dword_133085C = a, dword_1330860 = b. Only maybe_FFX_Atel__debugScreen reads them (sub_88D6B0, sub_88D6C0), sub_88E880 inits 23 and 0'),
    8: ('ResolveVarRet1', True, 'FFX_Atel_ResolveVarAddress(var) and the address is dropped'),
    9: ('SetEventDirMode', False, 'sub_873E40: byte_132711F = mode, the localized event directory mode sub_873AA0 reads. FFX_Atel_Init also writes it. The substitution it picks is dead on PC (ASSET_PATHS.md 7.2)'),
    11: ('DevLoadBattleParticleSet', False, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. yiLoadBattleFieldParticleNo, FFX_MapParticle_LoadBattleSet(id) from host0 f_particle b%03d'),
    12: ('DevLoadMapParticleSet', False, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. yiLoadFieldParticleNo, FFX_MapParticle_LoadMapSet(id) from host0 f_particle m%03d'),
    13: ('SetOptionFlag20', False, 'sub_785E90: option flags bit 0x20 = on. FFX_MesWin_SetTextSource copies that bit into each new message window (+0x22)'),
    14: ('SoundQueueCommand', True, 'sub_81E4E0(a, id, c, d): bumps the dword_12F4E7C command counter, sub_67A440(id, d & 0x7F, c & 0x7F), sets dword_12F4F64 bit 0'),
    15: ('ResolveVarRet0', True, 'as save:8 and returns 0'),
    16: ('PrepareVoice', True, 'sub_887570(voiceId), the same voice prepare call as core:213 and btl:100'),
    17: ('StartVoice', True, 'sub_887630, the prepared voice start of btl:101'),
    18: ('IsVoiceBusy', True, 'sub_8864B0() != 0, the voice busy test of core:216'),
    19: ('GetMapNameAltFlag', False, 'FFX_SaveData_GetMapNameAltFlag() == 1, option flags bit 0x08'),
    20: ('GetOptionFlag20', False, 'sub_7851B0, option flags bit 0x20'),
    21: ('DevLoadFileToSound', True, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. Reads the host0 file named by a script variable into an actor pool block and hands it to sub_886EE0'),
    22: ('PlaySe', True, 'sub_886E80(seId), the core:223 SE start'),
    23: ('StopSe', True, 'sub_886EB0(seId), the core:224 SE stop'),
    24: ('DevSoundFromVar', False, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_81EAB0(address of var)'),
    25: ('LoadSceneBackground', True, 'FFX_Ev_LoadSceneBackground 0x86DD30, the plain form of save:3, a synchronous load'),
    26: ('PlayKindSe1', True, 'sub_8866B0(1, slot, b, c): plays the SE that sound kind table 1 holds for slot 0..15'),
    27: ('PlayKindSe0', True, 'sub_8866B0(0, slot, b, c)'),
    28: ('PlayKindSe5', True, 'sub_8866B0(5, slot, b, 0)'),
    29: ('PlayKindSe2', True, 'sub_8866B0(2, slot, b, c)'),
    30: ('PlayKindSe3', True, 'sub_8866B0(3, slot, b, c)'),
    31: ('PlayKindSe4', True, 'sub_8866B0(4, slot, b, c), kind 4 uses the dword_132FF3C mask'),
    32: ('PassThroughA', True, 'returns its argument'),
    33: ('PassThroughB', True, 'returns its argument'),
    34: ('DevSetDebugGuiDraw', False, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. FFX_SetDebugGuiDrawEnabled(on)'),
    35: ('GetJointPosToVars', True, "maybe_FFX_Ch_GetJointWorldMatrix on the actor's CHR and the translation row into three script variables, zeros with no CHR"),
    36: ('ReloadFieldInPlace', False, 'start: FFX_Field_FreezeActorsToScratch, maybe_FFX_Atel_ReleaseAllActorChrs, FFX_Btl_ReturnToField(1, a, current scene). The poll returns 1 (done, yield)'),
    37: ('SetCharName0FromMessage', False, 'copies message text messageId over FFX_SaveData_GetCharName(0) when it is shorter than sub_784FE0(0)'),
    38: ('SoundQueryToVar', True, 'sub_886A00(address of var): dword_133009C = sub_67A060(address)'),
    39: ('DevClearKeyItems', True, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_906B30(-1) and clears 26 key item flags from 0xA004'),
    40: ('DevGiveKeyItems', False, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_88E790 for 26 key items from 0xA004'),
    41: ('SelectByFlagBit1', True, 'computes the actor yaw difference through sub_88BC20 and drops it, then returns a when flags & 2, else b'),
    42: ('DevReport', False, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_88EBE0(on) sets byte_1330871 and sub_885A80 when on'),
    43: ('ReturnZero', True, 'returns 0'),
    44: ('PassThroughC', False, 'returns its argument'),
    45: ('PlayKindSe6', True, 'sub_8866B0(6, slot, 0, 0)'),
    46: ('PlayKindSe7', True, 'sub_8866B0(7, slot, 0, 0)'),
    47: ('SetIndoorCameraHeightTest', False, 'ctx+0x208 = heightY and ctx byte +2 bit 0 = enable, the same body as core:530. sub_871E50 then sets byte_1326B83 = (heightY <= FFX_Came_GetPos(1).y) every frame, which feeds save +40, the ambient volume and the placed effect groups'),
    48: ('GetContextStepCounter', False, 'returns ctx+0x1F4 of the current context. Every shipped call is the 0x58 form and no window reads R afterwards'),
    49: ('LoadSceneMap', False, 'sub_86DCD0: FFX_SaveData_SetSceneAndSub then FFX_Map_LoadMapSync, synchronous'),
    50: ('TouchThreadChannel', True, 'FFX_Atel_GetActorThreadChannel on the caller and drops the result'),
    51: ('GetDebugGuiByte', True, 'sub_830320 returns byte_13007E8, the debug GUI root window state'),
    52: ('DevSetDebugGuiByte', True, 'dev gated: FFX_IsDevHarness 0x887D10 returns 0 in retail, so the body never runs. sub_830330(1)'),
    53: ('PartyCharHasPendingScript', True, 'maybe_FFX_Atel_PartyCharHasPendingScript(chr), as mapfx:88'),
    54: ('SetPlacedEffectsEnabled', False, 'sub_729050: dword_CEC1F0 = on, the gate FFX_MapFx_UpdatePlacedEffects 0x729AC0 tests first, the same as core:550 and mapfx:12'),
    55: ('GetMapNameString', True, 'returns a pointer into the dword_25D16C4 string table, row 2 * id (+1 under the map name alt flag)'),
    56: ('AssetLoadToVar', True, 'FFX_GetAssetLoader: slot 4 (group), then slot 0 (file, address of var)'),
    57: ('AssetQuery', True, 'FFX_GetAssetLoader slot 2 (file)'),
    58: ('VoiceStartRaw', True, 'sub_8875D0: sub_67A4E0(a, b, c or 0), sub_81EE20(a), then sets the busy bit 0 in qword_132FF14'),
    59: ('DebugFullItem', False, 'FFX_Debug_FullItem: fills the item list (with a language 11 case) and opens a menu'),
    60: ('LoadPlayerData', False, 'FFX_Btl_LoadPlayerData, reloads ply_data'),
    61: ('SetSignalFlagTo', True, 'on: sub_86FA40(bit) sets bit of dword_1326B70 (core:378), off: sub_863860(bit) clears it (core:379)'),
    62: ('PlaySeOrRamp', True, 'sub_886680(seId), the core:403 body'),
    63: ('SetSoundOutputMode', True, 'sub_887180: clamps to 0..2, sub_81EA00(mode), byte_1330092 = mode. FFX_Menu_ApplySaveSoundOptions calls the same'),
    64: ('GetSoundOutputMode', True, 'sub_886430 returns byte_1330092'),
    65: ('SetSoundFlag1330093', True, 'sub_8871C0: byte_1330093 = clamp(on, 0, 1)'),
    66: ('GetSoundFlag1330093', True, 'sub_886440 returns byte_1330093'),
    67: ('StopAllSound', True, 'sub_886DB0: sub_81E5D0(-1), as FFX_Map_ReturnToTitle does'),
    68: ('NopMapOutA', True, 'nullsub_130(x), a stub the map out code also calls'),
    69: ('FutureErrorText', True, 'std::_Get_future_error_what(code), a library string pointer, an identical COMDAT folded into the slot'),
    70: ('NopMapOutB', True, 'nullsub_132(x)'),
    71: ('GetSaveFloat', True, 'the float at save block + offset, unchecked'),
    72: ('SetSaveFloat', True, 'writes a float at save block + offset'),
    73: ('GetSaveDword', True, 'the dword at save block + offset'),
    74: ('SetSaveDword', True, 'writes (int)value as a dword at save block + offset'),
    75: ('GetSaveWord', True, 'the signed word at save block + offset'),
    76: ('SetSaveWord', True, 'writes (int)value as a word'),
    77: ('GetSaveByte', True, 'the signed byte at save block + offset'),
    78: ('SetSaveByte', True, 'writes (int)value as a byte'),
    79: ('ReturnOne', True, 'returns 1'),
    80: ('NopReturnOne', True, 'calls sub_76F090 only when sub_76F060 (return 0) is set, so returns 1 and does nothing'),
    81: ('SetDemoMode', False, 'sub_88EC10: byte_1330832 = on. sub_88E880 picks the FFXDEMO paths while it is set. The title (test20) calls it with 0'),
    82: ('GetDemoMode', False, 'sub_88D6D0 returns byte_1330832, also read by FFX_Field_StepMenuOpenRequest, sub_86BAE0 and the sub_888xxx save menu modules'),
    83: ('Drop4Ret0', False, 'pops four values and returns 0'),
    84: ('AllocActorPoolBlock', False, 'FFX_Tk_AllocSectorRounded(size), a raw heap pointer into the script'),
    85: ('FreeTkBlock', False, 'FFX_Tk_Free(pointer), the free for save:84'),
    86: ('ReadAssetGroup', False, "saveram_read_group, prints 'Read: group = %d/ index = %d/adrs = 0x%x' and sub_88D140: asset loader slots 4, 5, 0 and 2, a load into dest"),
    87: ('CopySaveBlockFrom', False, 'saveram_copy_to, memcpy(&g_ffxSaveData, pointer + 64, 0x68C0), the whole block transplant with nothing re-derived'),
    88: ('GetVarAddress', False, 'FFX_Atel_ResolveVarAddress(var), a raw pointer into script memory'),
    89: ('SwapInnateAbilityFlag', True, 'returns FFX_SaveData_TestInnateAbilityFlag(ability) and sets it to on through sub_785D40'),
    90: ('PlayKindSe255', True, 'sub_8866B0(255, a, 0, b), the direct SE form through sub_8867E0'),
    91: ('DebugPrintDec', False, "sprintf 'DEBUG:%d/%d' of the thread id and x into nullsub_123, returns x"),
    92: ('DebugPrintHex', True, "sprintf 'DEBUG:%d/0x%x' into nullsub_123, returns x"),
    93: ('SetTextLanguage', True, 'FFX_MesWin_SetTextLanguage(lang)'),
}
# END SAVE NAMES

LIBS = (
    ("Movie", "movie", MOVIE_TABLE, MOVIE_ROWS, MOVIE_NAMES, MOVIE_MISNAMED),
    ("Save", "save", SAVE_TABLE, SAVE_ROWS, SAVE_NAMES, SAVE_MISNAMED),
)


def positional(lib, i, slot):
    return "FFX_AtelSys_%s_%03d_%s" % (lib, i, slot)


def is_positional(lib, cur, i, slot, single):
    m = re.match(r"^FFX_AtelSys_%s_0*(\d+)(?:_(start|poll|resf|resi))?$" % lib, cur)
    if not m or int(m.group(1)) != i:
        return False
    if m.group(2) is None:
        return single
    return m.group(2) == slot


def plan_library(lib, short, table_ea, rows, names, misnamed, include_dead, slot_suffix):
    import ida_bytes
    import ida_funcs
    import ida_name
    import idaapi
    import idc

    owners = {}
    table = {}
    for i in range(rows):
        for k, slot in enumerate(SLOTS):
            ea = ida_bytes.get_dword(table_ea + 16 * i + 4 * k)
            if ea:
                table[(i, slot)] = ea
                owners.setdefault(ea, []).append((i, slot))

    plan = []
    skips = []
    kept = []
    for i in sorted(names):
        role, dead, meaning = names[i]
        if dead and not include_dead:
            continue
        if not role or not ROLE_OK.match(role):
            skips.append("%s:%d bad role %r" % (short, i, role))
            continue
        slots = [s for s in SLOTS if (i, s) in table]
        if not slots:
            skips.append("%s:%d null row, nothing to name" % (short, i))
            continue
        for slot in slots:
            ea = table[(i, slot)]
            cur = idc.get_name(ea) or ""
            if slot_suffix or len(slots) > 1:
                new = "FFX_AtelSys_%s_%03d_%s_%s" % (lib, i, role, slot)
            else:
                new = "FFX_AtelSys_%s_%03d_%s" % (lib, i, role)
            if cur == new:
                kept.append("%s:%d %s 0x%X already %s" % (short, i, slot, ea, cur))
                continue
            if (not is_positional(lib, cur, i, slot, len(slots) == 1) and not PLACEHOLDER.match(cur)
                    and misnamed.get((i, slot)) != cur):
                skips.append("%s:%d %s 0x%X keeps its name %s (not positional)" % (short, i, slot, ea, cur))
                continue
            f = ida_funcs.get_func(ea)
            if f is None or f.start_ea != ea:
                skips.append("%s:%d %s 0x%X is not a function start, make one first" % (short, i, slot, ea))
                continue
            if len(owners[ea]) > 1:
                skips.append("%s:%d %s 0x%X is shared by %s" % (
                    short, i, slot, ea, ", ".join("%s:%d %s" % ((short,) + o) for o in owners[ea])))
                continue
            other = ida_name.get_name_ea(idaapi.BADADDR, new)
            if other != idaapi.BADADDR and other != ea:
                skips.append("%s:%d %s name %s already used at 0x%X" % (short, i, slot, new, other))
                continue
            plan.append((ea, cur, new, meaning))
    return plan, skips, kept


def plan_false_movie_rows():
    """The positional Movie names past row 15, each checked against the pointer it claims to be."""
    import ida_bytes
    import ida_funcs
    import idautils
    import idc

    pat = re.compile(r"^FFX_AtelSys_Movie_(\d{3})_(start|poll|resf|resi)$")
    clears = []
    skips = []
    for ea, name in idautils.Names():
        m = pat.match(name)
        if not m:
            continue
        i = int(m.group(1))
        slot = m.group(2)
        if i not in MOVIE_FALSE_ROWS:
            continue
        ptr = ida_bytes.get_dword(MOVIE_TABLE + 16 * i + 4 * SLOTS.index(slot))
        f = ida_funcs.get_func(ea)
        if ptr != ea:
            skips.append("false movie:%d %s 0x%X, the pointer there is 0x%X, kept" % (i, slot, ea, ptr))
            continue
        if f is None or f.start_ea != ea:
            skips.append("false movie:%d %s 0x%X is not a function start, kept" % (i, slot, ea))
            continue
        cmt = idc.get_func_cmt(ea, 1) or ""
        clears.append((ea, name, cmt.startswith(FALSE_COMMENT % i)))
    return clears, skips


def main(dry_run=DRY_RUN, include_dead=INCLUDE_DEAD, slot_suffix=SLOT_SUFFIX,
         clear_false_movie_rows=CLEAR_FALSE_MOVIE_ROWS, verbose=VERBOSE):
    import ida_name
    import idc

    plan = []
    skips = []
    kept = []
    nrows = 0
    for lib, short, table_ea, rows, names, misnamed in LIBS:
        p, s, k = plan_library(lib, short, table_ea, rows, names, misnamed, include_dead, slot_suffix)
        plan += p
        skips += s
        kept += k
        nrows += len(names)
    clears = []
    if clear_false_movie_rows:
        clears, s = plan_false_movie_rows()
        skips += s

    for line in kept:
        if verbose:
            print("ok    " + line)
    for line in skips:
        print("skip  " + line)
    for ea, cur, new, meaning in plan:
        print("name  0x%X %s -> %s%s" % (ea, cur, new, ("    " + meaning) if verbose else ""))
    for ea, name, has_cmt in clears:
        if verbose:
            print("clear 0x%X %s%s" % (ea, name, " and its comment" if has_cmt else ""))
    print("%d to rename, %d skipped, %d already named, %d rows in NAMES"
          % (len(plan), len(skips), len(kept), nrows))
    if clear_false_movie_rows:
        print("%d false Movie names to clear, %d of them with the false comment"
              % (len(clears), sum(1 for c in clears if c[2])))

    if dry_run:
        print("DRY_RUN, nothing renamed")
        return True

    failed = 0
    renamed = 0
    for ea, cur, new, _ in plan:
        if not idc.set_name(ea, new, ida_name.SN_NOWARN | ida_name.SN_NOCHECK):
            print("FAILED 0x%X %s -> %s" % (ea, cur, new))
            failed += 1
            continue
        renamed += 1
    cleared = 0
    for ea, name, has_cmt in clears:
        if not idc.set_name(ea, "", ida_name.SN_NOWARN):
            print("FAILED to clear 0x%X %s" % (ea, name))
            failed += 1
            continue
        cleared += 1
        if has_cmt:
            idc.set_func_cmt(ea, "", 1)
    print("renamed %d, cleared %d, failed %d. The database is NOT saved, save it when you are happy"
          % (renamed, cleared, failed))
    return failed == 0


def headless():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True, help="a COPY of the database")
    ap.add_argument("--apply", action="store_true", help="rehearse the renames, the database is not saved")
    ap.add_argument("--include-dead", action="store_true", help="also name rows no shipped script calls")
    ap.add_argument("--no-slot-suffix", action="store_true",
                    help="drop the _<slot> suffix on rows that have a single slot")
    ap.add_argument("--clear-false-movie-rows", action="store_true",
                    help="also remove the positional Movie names past row 15, which are not syscall handlers")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    import idapro
    if idapro.open_database(a.idb, run_auto_analysis=False) != 0:
        raise SystemExit("could not open %s" % a.idb)
    try:
        main(dry_run=not a.apply, include_dead=a.include_dead, slot_suffix=not a.no_slot_suffix,
             clear_false_movie_rows=a.clear_false_movie_rows, verbose=a.verbose)
    finally:
        idapro.close_database(save=False)


if __name__ == "__main__":
    if "idaapi" in sys.modules or "ida_kernwin" in sys.modules:
        main()
    else:
        headless()
