"""
Apply the sound class names from reversing/SOUND_CLASSES.md to the FFX.exe IDB.

The sound code is three layers:

    step thread   the PS2 shim (0x81Cxxx..0x81Fxxx for SE, 0x885xxx..0x887xxx for BGM and voice)
                  -> the FFX_FmodCmd_* wrappers at 0x679FB0..0x67A7D0
                  -> FmodManager's 150 entry command ring at 0xCE8458
    FMOD thread   FmodManager__tick -> the 70 entry handler table at 0xC3A368 (FfxFmod_CmdNN_*)
                  -> the FfxFmod_* forwarders -> FmodMusic, FmodVoice (+FmodShout), FmodSfx
                  (+SfxProject, SfxEvent)

Three tables:

    NAMES    {ea: (name, comment)}         the current name must be a placeholder
    RENAMES  {ea: (name, "was OLD. why")}  the current name must be exactly OLD
    GLOBALS  {ea: (name, comment)}         data. A comment that starts "was OLD." works like RENAMES,
                                           otherwise the current name must be a placeholder

A placeholder is sub_/nullsub_/loc_/j_sub_/unk_/dword_/word_/byte_/qword_/off_/flt_/dbl_/stru_/
funcs_ plus hex, a positional <Class>__f<hex> method name, a positional
FFX_AtelSys_<Lib>_NNN_<slot> syscall row name, or no name at all (a few orphan wrappers have no
function and no label). A name starting maybe_ is a guess and says so.

The script refuses a row when the current name is not what it expects, and when the new name
already exists at another address. So running it twice is harmless, and a name somebody gave in the
meantime is never overwritten.

DRY_RUN is True by default. A dry run prints what it would rename and why it would skip, and changes
nothing. With --comments it also appends each row's comment as a repeatable comment, unless the
comment is already there or the result would pass 1000 characters (IDA truncates past 1024, and a
truncating write loses what was there). The script never saves the database, save it yourself.

Run it either inside IDA (File > Script file, or an IDA MCP execute_python), or headless through
idalib on a COPY of the database:

    python apply_sound_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--comments] [--verbose]

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
    # FmodManager, the 0x50 byte singleton at g_ffxFmodManager 0xCE9010
    0x6F9F70: ("FmodManager__dtor", "clears +76 running, sleeps until the thread sets +77 exited, deletes the FfxFmod at +8, closes the 2 events and 2 mutexes"),
    0x6FA190: ("FmodManager__destroySingleton", "deletes g_ffxFmodManager, from FFX_Fmod_DestroyManager <- FFXApplication__exitApplication"),
    0x6FA200: ("FmodManager__getFmodSystem", "returns FfxFmod+8, the FMOD::System*, handed to the movie player by sub_6D8F30 so movie audio shares FMOD"),
    0x6FA2E0: ("FfxFmod_QueueCommand3", "QueueCommand(cmd, a, b, c), the three argument wrapper"),
    0x6FA4A0: ("FfxFmod_RunCommandSync1", "RunCommandSync(cmd, a), the one argument wrapper, used by commands 22 and 63"),
    0x6FA4C0: ("FfxFmod_RunCommandSync2", "orphan, RunCommandSync(cmd, a, b)"),
    0x6FA4E0: ("FfxFmod_RunCommandSync3", "orphan, RunCommandSync(cmd, a, b, c)"),
    0x6FA690: ("FfxFmod_MarkBgmPlayPending", "STEP thread: if track <= 181 writes FmodMusic+24 = track and FmodMusic+32 = 1 directly, before command 39 is queued, so command 22 reads busy at once. No lock"),
    0x6FA880: ("FmodManager__runFmodTick", "orphan, FfxFmod__tick(this+8)"),
    0x679FB0: ("FFX_Fmod_DestroyManager", "FmodManager__destroySingleton, from FFXApplication__exitApplication"),

    # step side command wrappers, FFX_FmodCmd_<what> (command number in the comment)
    0x679FD0: ("FFX_FmodCmd_GetCurrentBgm", "orphan, command 19 inline, returns FmodMusic+24"),
    0x67A010: ("FFX_FmodCmd_UnloadBgm", "command 37 (track), FmodMusic__unloadTrack"),
    0x67A030: ("FFX_FmodCmd_UnloadAllBgm", "command 45, FmodMusic__unloadAll, skips held tracks"),
    0x67A040: ("FFX_FmodCmd_CrossfadeBgm", "command 23 (track, frames), FmodMusic__crossfadeTo"),
    0x67A060: ("FFX_FmodCmd_LoadBattleBgm", "command 21, FmodMusic__loadBattleTrack, slot 16"),
    0x67A090: ("FFX_FmodCmd_PlayBgmNoPending", "command 33 (track), FmodMusic__play without the step side pending mark"),
    0x67A0D0: ("FFX_FmodCmd_PlayBgmArg2", "command 27 (track, x), FmodMusic__play, the forwarder drops x, no pending mark"),
    0x67A110: ("FFX_FmodCmd_Cmd31Noop", "orphan, command 31, an empty stub on PC"),
    0x67A150: ("FFX_FmodCmd_IsBgmPlaying", "command 22 INLINE on the caller: FmodMusic+32 | track[FmodMusic+24].+32. Read by core:443"),
    0x67A170: ("FFX_FmodCmd_LoadBgm", "command 40 (track), FmodMusic__loadTrack"),
    0x67A190: ("FFX_FmodCmd_ReleaseBgmHold", "command 26 (track), clears track.+4 so stop and unload act on it again"),
    0x67A1B0: ("FFX_FmodCmd_HoldCurrentBgm", "command 25 INLINE: track[FmodMusic+24].+4 = 1, stop then pauses it and unload skips it"),
    0x67A1C0: ("FFX_FmodCmd_SetAllBgmPaused", "orphan, command 47, FmodMusic__setAllPaused"),
    0x67A1E0: ("FFX_FmodCmd_PlayBgm", "FfxFmod_MarkBgmPlayPending on the step thread, then command 39 (track), FmodMusic__play"),
    0x67A210: ("FFX_FmodCmd_StopBgm", "command 38 (track), FmodMusic__stop, pauses instead when the track is held"),
    0x67A230: ("FFX_FmodCmd_StopAllBgm", "command 44, FmodMusic__stopAll"),
    0x67A240: ("FFX_FmodCmd_IsBattleBgmLoading", "command 20, the one CROSS THREAD sync command: up to 2 x 5 ms, a timeout returns 1. Read by the battle MUSIC boot task state 7"),
    0x67A290: ("FFX_FmodCmd_SetBgmVolume", "command 35 (track, vol 0..127), FmodMusic__setTrackVolume"),
    0x67A2B0: ("FFX_FmodCmd_FadeOutStopBgm", "command 28 (track, frames), FmodMusic__fadeOutAndStop"),
    0x67A2D0: ("FFX_FmodCmd_FadeBgmVolume", "command 34 (track, frames, vol), frames halved to FMOD ticks, FmodMusic__fadeTrackVolume"),
    0x67A2F0: ("FFX_FmodCmd_DelayStartSe", "command 53, FmodSfx__delayStart, delay frames / 30 counted in wall seconds"),
    0x67A310: ("FFX_FmodCmd_StartSeIgnorePause", "command 52 (id, vol, pan), FmodSfx__playIgnorePause, the menu SE path"),
    0x67A330: ("FFX_FmodCmd_IsSePaused", "command 65 INLINE, FmodSfx+0"),
    0x67A340: ("FFX_FmodCmd_IsSePlaying", "command 63 INLINE under the FmodSfx mutex, plus a ring scan for a pending command 51 with the same id"),
    0x67A360: ("FFX_FmodCmd_LoadSeProject", "command 48 (project, resident), FmodSfx__loadProject, SFX/US or JP/%04d.fev"),
    0x67A380: ("FFX_FmodCmd_SetSePaused", "command 58 (bool), FmodSfx__setPaused. While set, commands 51 and 53 are dropped, not deferred"),
    0x67A3A0: ("FFX_FmodCmd_PlayBattleStreamingSe", "command 64, FmodSfx__playBattleStreaming, project 9999.fev"),
    0x67A3C0: ("FFX_FmodCmd_SetSePan", "command 61 (id, pan), coalesced in the ring"),
    0x67A3E0: ("FFX_FmodCmd_FadeSePan", "command 62 (id, pan, frames)"),
    0x67A400: ("FFX_FmodCmd_SetSeVolume", "command 59 (id, vol), coalesced in the ring"),
    0x67A420: ("FFX_FmodCmd_FadeSeVolume", "command 60 (id, vol, frames)"),
    0x67A440: ("FFX_FmodCmd_StartSe", "command 51 (id, vol, pan), FmodSfx__play, refused while FmodSfx+0 paused"),
    0x67A460: ("FFX_FmodCmd_StopSe", "command 54 (id), FmodSfx__stopSequence"),
    0x67A480: ("FFX_FmodCmd_StopSeList", "command 55 (list, mode), FmodSfx__stopList, ids above 127 only, -1 ends the list or means all"),
    0x67A4A0: ("maybe_FFX_FmodCmd_StopMapSe", "command 56 (id), stops the sequence only when id > 127"),
    0x67A4C0: ("FFX_FmodCmd_StopOneShotSe", "command 57, FmodSfx__stopOneShots, every non looping SfxEvent plus battle streaming. Sent by FFX_Snd_SetFastForward"),
    0x67A4D0: ("FFX_FmodCmd_UnloadMapSeProjects", "command 49, FmodSfx__unloadProjects(0, 0), dropped by QueueCommand while dword_1325B7C is set"),
    0x67A4E0: ("FFX_FmodCmd_PlayShout", "command 6 (id, a, vol), FmodVoice__playShout"),
    0x67A500: ("FFX_FmodCmd_InitData", "command 1, FfxFmod_InitData, from FFX_MainInit"),
    0x67A520: ("FFX_FmodCmd_Cmd5Orphan", "orphan, command 5, a duplicate of the voice playing query"),
    0x67A530: ("FFX_FmodCmd_SetOptionMasterVolume", "command 66 (percent), from the EscMenu audio table"),
    0x67A550: ("FFX_FmodCmd_SetOptionMusicVolume", "command 67 (percent), from the EscMenu audio table"),
    0x67A570: ("FFX_FmodCmd_SetMuted", "command 0 (on, type), type 1 music FmodMusic__setMuted, type 2 voice FmodVoice__setMuted. Sent by FFX_Snd_SetFastForward with type 2"),
    0x67A590: ("FFX_FmodCmd_SetOptionSfxVolume", "command 69 (percent), from the EscMenu audio table"),
    0x67A5B0: ("FFX_FmodCmd_SetOptionVoiceVolume", "command 68 (percent), from the EscMenu audio table"),
    0x67A5D0: ("FFX_FmodCmd_ResetVoiceForMap", "command 4 (map), FmodVoice__resetForMap, from maybe_FFX_Scene_Init"),
    0x67A5F0: ("FFX_FmodCmd_UpdateEventSystem", "orphan, command 2, EventSystem::update"),
    0x67A600: ("FFX_Snd_ApplyVolumeOptions", "g_ffxGameSettings +20 master, +28 music, +24 voice, +32 sfx, +16 mute all, sends 66..69 and sets the FMV volume"),
    0x67A6A0: ("FFX_FmodCmd_VoiceDataChange", "command 9 (map), FmodVoice__dataChange, also writes FmodMusic+40 = map"),
    0x67A6C0: ("FFX_FmodCmd_IsVoicePaused", "command 18 INLINE, FmodVoice+84"),
    0x67A6D0: ("maybe_FFX_FmodCmd_SetVoicePan", "command 12, an empty stub on PC. Pan is a guess from the 63 default in byte_1327135"),
    0x67A6F0: ("maybe_FFX_FmodCmd_FadeVoicePan", "command 11, an empty stub on PC"),
    0x67A740: ("FFX_FmodCmd_UnloadVoiceBanks", "orphan, command 7, FmodVoice__unloadBanks"),
    0x67A750: ("FFX_FmodCmd_SetVoicePaused", "command 8 (bool), FmodVoice__setPaused, which re-stamps the line start while paused"),
    0x67A7B0: ("FFX_FmodCmd_SetVoiceVolume", "command 14 (vol), FmodVoice__setVolume"),
    0x67A7D0: ("FFX_FmodCmd_FadeVoiceVolume", "command 13 (frames, vol), frames halved, FmodVoice__beginVolumeFade"),

    # FMOD thread command handlers, the table at 0xC3A368 (10, 15, 16, 17 were already named)
    0x707B90: ("FfxFmod_Cmd00_SetMuted", "FfxFmod_SetMuted(on, type)"),
    0x707BB0: ("FfxFmod_Cmd01_InitData", "FfxFmod_InitData"),
    0x707BC0: ("FfxFmod_Cmd02_UpdateEventSystem", "FfxFmod__updateEventSystem"),
    0x707BD0: ("FfxFmod_Cmd03_Tick", "FfxFmod__tick"),
    0x707BE0: ("FfxFmod_Cmd04_ResetVoiceForMap", "FfxFmod_ResetVoiceForMap"),
    0x707C00: ("FfxFmod_Cmd05_IsVoicePlayingDup", "same body as command 17"),
    0x707C10: ("FfxFmod_Cmd06_PlayShout", "FfxFmod_PlayShout"),
    0x707C30: ("FfxFmod_Cmd07_UnloadVoiceBanks", "FfxFmod_UnloadVoiceBanks"),
    0x707C40: ("FfxFmod_Cmd08_SetVoicePaused", "FfxFmod_SetVoicePaused"),
    0x707C60: ("FfxFmod_Cmd09_VoiceDataChange", "FfxFmod_VoiceDataChange"),
    0x707C90: ("maybe_FfxFmod_Cmd11_FadeVoicePan", "frames halved then an empty FfxFmod stub"),
    0x707CF0: ("maybe_FfxFmod_Cmd12_SetVoicePan", "an empty FfxFmod stub"),
    0x707D10: ("FfxFmod_Cmd13_FadeVoiceVolume", "frames halved, FfxFmod_FadeVoiceVolume"),
    0x707D70: ("FfxFmod_Cmd14_SetVoiceVolume", "FfxFmod_SetVoiceVolume"),
    0x707DF0: ("FfxFmod_Cmd18_IsVoicePaused", "inline, FmodVoice+84"),
    0x707E00: ("FfxFmod_Cmd19_GetCurrentBgm", "inline, FmodMusic+24"),
    0x707E10: ("FfxFmod_Cmd20_IsBattleBgmLoading", "FfxFmod_IsBattleBgmLoading"),
    0x707E20: ("FfxFmod_Cmd21_LoadBattleBgm", "FfxFmod_LoadBattleBgm"),
    0x707E40: ("FfxFmod_Cmd22_IsBgmPlaying", "inline, FfxFmod_IsBgmPlaying"),
    0x707E60: ("FfxFmod_Cmd23_CrossfadeBgm", "FfxFmod_CrossfadeBgm"),
    0x707E80: ("FfxFmod_Cmd24_Noop", "an empty FfxFmod stub, core:360"),
    0x707EA0: ("FfxFmod_Cmd25_HoldCurrentBgm", "inline, FfxFmod_HoldCurrentBgm"),
    0x707EB0: ("FfxFmod_Cmd26_ReleaseBgmHold", "FfxFmod_ReleaseBgmHold"),
    0x707ED0: ("FfxFmod_Cmd27_PlayBgmArg2", "FmodMusic__play, second argument dropped"),
    0x707EF0: ("FfxFmod_Cmd28_FadeOutStopBgm", "FfxFmod_FadeOutStopBgm"),
    0x707F10: ("FfxFmod_Cmd29_Noop", "frames halved then an empty FfxFmod stub, core:389"),
    0x707F70: ("FfxFmod_Cmd30_Noop", "an empty FfxFmod stub, core:388"),
    0x707F90: ("FfxFmod_Cmd31_Noop", "an empty FfxFmod stub"),
    0x707FB0: ("FfxFmod_Cmd32_Noop", "an empty FfxFmod stub, core:390"),
    0x707FD0: ("FfxFmod_Cmd33_PlayBgmNoPending", "FmodMusic__play"),
    0x707FF0: ("FfxFmod_Cmd34_FadeBgmVolume", "frames halved, FfxFmod_FadeBgmVolume"),
    0x708050: ("FfxFmod_Cmd35_SetBgmVolume", "FfxFmod_SetBgmVolume"),
    0x708080: ("FfxFmod_Cmd36_Noop", "an empty FfxFmod stub, core:361"),
    0x7080A0: ("FfxFmod_Cmd37_UnloadBgm", "FfxFmod_UnloadBgm"),
    0x7080C0: ("FfxFmod_Cmd38_StopBgm", "FfxFmod_StopBgm"),
    0x7080E0: ("FfxFmod_Cmd39_PlayBgm", "FmodMusic__play"),
    0x708100: ("FfxFmod_Cmd40_LoadBgm", "FfxFmod_LoadBgm"),
    0x708120: ("FfxFmod_Cmd41_Shutdown", "FfxFmod__shutdown"),
    0x708130: ("FfxFmod_Cmd42_Init", "FfxFmod__init, queued by FmodManager__ctor"),
    0x708140: ("FfxFmod_Cmd43_InitNullsubs", "two nullsubs, queued by FmodManager__ctor"),
    0x708150: ("FfxFmod_Cmd44_StopAllBgm", "FfxFmod_StopAllBgm"),
    0x708160: ("FfxFmod_Cmd45_UnloadAllBgm", "FfxFmod_UnloadAllBgm"),
    0x708170: ("FfxFmod_Cmd46_SetPs2Soundtrack", "FfxFmod_SetPs2Soundtrack"),
    0x708190: ("FfxFmod_Cmd47_SetAllBgmPaused", "FfxFmod_SetAllBgmPaused"),
    0x7081B0: ("FfxFmod_Cmd48_LoadSeProject", "FfxFmod_LoadSeProject"),
    0x7081D0: ("FfxFmod_Cmd49_UnloadMapSeProjects", "FmodSfx__unloadProjects(0, 0)"),
    0x7081E0: ("FfxFmod_Cmd50_UnloadSeProjectOf", "FfxFmod_UnloadSeProjectOf, no step caller"),
    0x708200: ("FfxFmod_Cmd51_StartSe", "FfxFmod_StartSe"),
    0x708220: ("FfxFmod_Cmd52_StartSeIgnorePause", "FfxFmod_StartSeIgnorePause"),
    0x708240: ("FfxFmod_Cmd53_DelayStartSe", "FfxFmod_DelayStartSe"),
    0x708260: ("FfxFmod_Cmd54_StopSe", "FfxFmod_StopSe"),
    0x708280: ("FfxFmod_Cmd55_StopSeList", "FfxFmod_StopSeList"),
    0x7082B0: ("maybe_FfxFmod_Cmd56_StopMapSe", "maybe_FfxFmod_StopMapSe"),
    0x7082D0: ("FfxFmod_Cmd58_SetSePaused", "FfxFmod_SetSePaused. The table has 58 before 57 in memory"),
    0x7082F0: ("FfxFmod_Cmd57_StopOneShotSe", "FmodSfx__stopOneShots"),
    0x708300: ("FfxFmod_Cmd59_SetSeVolume", "FfxFmod_SetSeVolume"),
    0x708320: ("FfxFmod_Cmd60_FadeSeVolume", "FfxFmod_FadeSeVolume"),
    0x708340: ("FfxFmod_Cmd61_SetSePan", "FfxFmod_SetSePan"),
    0x708360: ("FfxFmod_Cmd62_FadeSePan", "FfxFmod_FadeSePan"),
    0x708380: ("FfxFmod_Cmd63_IsSePlaying", "inline, FfxFmod_IsSePlaying"),
    0x7083A0: ("FfxFmod_Cmd64_PlayBattleStreamingSe", "FfxFmod_PlayBattleStreamingSe"),
    0x7083C0: ("FfxFmod_Cmd65_IsSePaused", "inline, FmodSfx+0"),
    0x7083D0: ("FfxFmod_Cmd66_SetOptionMasterVolume", "percent / 100, FfxFmod_SetOptionMasterVolume"),
    0x708400: ("FfxFmod_Cmd67_SetOptionMusicVolume", "percent / 100, FfxFmod_SetOptionMusicVolume"),
    0x708430: ("FfxFmod_Cmd68_SetOptionVoiceVolume", "percent / 100, FfxFmod_SetOptionVoiceVolume"),
    0x708460: ("FfxFmod_Cmd69_SetOptionSfxVolume", "percent / 100, FfxFmod_SetOptionSfxVolume"),

    # FfxFmod, 0x28 bytes at FmodManager+8. Methods and the one line forwarders the handlers call
    0x706490: ("FfxFmod__ctor", "volumes +24..+36 = 1.0, allocates FmodMusic 0x34 at +12, FmodVoice 0x90 at +16, FmodSfx 0xD4 at +20. From FmodManager__ctor"),
    0x706590: ("FfxFmod__dtor", "deletes the three sub objects"),
    0x7066D0: ("FfxFmod_MemAlloc", "FMOD_Memory_Initialize useralloc callback"),
    0x706730: ("FfxFmod_MemRealloc", "FMOD_Memory_Initialize userrealloc callback"),
    0x706710: ("FfxFmod_MemFree", "FMOD_Memory_Initialize userfree callback"),
    0x706820: ("FfxFmod__init", "was FfxFmod__f706820. FMOD_Memory_Initialize, EventSystem_Create, setStreamBufferSize 0x10000, init 128 channels, setFileSystem"),
    0x706FF0: ("FfxFmod__shutdown", "was FfxFmod__f706FF0. Voice, music, SFX shutdown with 100 ms sleeps, EventSystem unload and release"),
    0x7070D0: ("FfxFmod__tick", "was FfxFmod__f7070D0. Every 33 ms of wall time: EventSystem::update, voice line end, music fades and deferred start, voice fade and deferred start, FmodSfx__tick(dt s)"),
    0x707420: ("FfxFmod__updateEventSystem", "was FfxFmod__f707420. EventSystem::update"),
    0x7079D0: ("FfxFmod_CheckResult", "logs a failed FMOD_RESULT with file and line"),
    0x707590: ("FfxFmod_ResultString", "FMOD_RESULT -> message text"),
    0x707AA0: ("FfxFmod_BuildDataPath", "/FFX_Data/GameData/PS3Data/Sound_PC/%s"),
    0x707B20: ("FfxFmod_Log", "printf style log with a tag"),
    0x707A50: ("FfxFmod_EventCallback_ClearFlag", "THE one FMOD event callback: if type is 2 or 4 writes 0 to the userdata dword. Registered on voice +64, music track +32, SfxEvent entry +4"),
    0x708510: ("FfxFmod_FileOpen", "FMOD setFileSystem open callback"),
    0x7084D0: ("FfxFmod_FileClose", "FMOD setFileSystem close callback"),
    0x7085A0: ("FfxFmod_FileRead", "FMOD setFileSystem read callback"),
    0x7085E0: ("FfxFmod_FileSeek", "FMOD setFileSystem seek callback"),
    0x706B50: ("FfxFmod_IsValidBgmTrack", "track <= 0xB5"),
    0x706B70: ("FfxFmod_SetCurrentBgmTrack", "FmodMusic+24 = track"),
    0x706B80: ("FfxFmod_SetBgmPlayPending", "FmodMusic+32 = value"),
    0x7072F0: ("FfxFmod_SetMuted", "type 1 FmodMusic__setMuted, type 2 FmodVoice__setMuted"),
    0x707080: ("FfxFmod_InitData", "language, music projects, voice maps and banks, shout data"),
    0x707410: ("FfxFmod_ResetVoiceForMap", "FmodVoice__resetForMap"),
    0x706FD0: ("FfxFmod_PlayShout", "FmodVoice__playShout"),
    0x707510: ("FfxFmod_UnloadVoiceBanks", "FmodVoice__unloadBanks"),
    0x707520: ("FfxFmod_SetVoicePaused", "FmodVoice__setPaused"),
    0x707480: ("FfxFmod_VoiceDataChange", "FmodVoice__dataChange"),
    0x707580: ("FfxFmod_FadeVoiceVolume", "FmodVoice__beginVolumeFade"),
    0x707560: ("FfxFmod_SetVoiceVolume", "FmodVoice__setVolume"),
    0x706BF0: ("FfxFmod_IsBattleBgmLoading", "FmodMusic__isBattleTrackLoading"),
    0x706A20: ("FfxFmod_LoadBattleBgm", "FmodMusic__loadBattleTrack"),
    0x706AD0: ("FfxFmod_IsBgmPlaying", "FmodMusic+32 | track[FmodMusic+24].+32"),
    0x706A00: ("FfxFmod_CrossfadeBgm", "FmodMusic__crossfadeTo"),
    0x706B30: ("FfxFmod_HoldCurrentBgm", "track[FmodMusic+24].+4 = 1"),
    0x706B00: ("FfxFmod_ReleaseBgmHold", "track.+4 = 0"),
    0x706A80: ("FfxFmod_PlayBgmArg2", "bounds check, FmodMusic__play(track), the second argument is unused"),
    0x706A40: ("FfxFmod_PlayBgmChecked", "bounds check, FmodMusic__play(track)"),
    0x706B90: ("FfxFmod_PlayBgm", "jmp FmodMusic__play"),
    0x706C40: ("FfxFmod_FadeOutStopBgm", "FmodMusic__fadeOutAndStop"),
    0x706C50: ("FfxFmod_FadeBgmVolume", "FmodMusic__fadeTrackVolume"),
    0x706C20: ("FfxFmod_SetBgmVolume", "FmodMusic__setTrackVolume"),
    0x7069E0: ("FfxFmod_UnloadBgm", "FmodMusic__unloadTrack"),
    0x706BA0: ("FfxFmod_StopBgm", "jmp FmodMusic__stop"),
    0x706AE0: ("FfxFmod_LoadBgm", "FmodMusic__loadTrack"),
    0x706BB0: ("FfxFmod_StopAllBgm", "jmp FmodMusic__stopAll"),
    0x7069F0: ("FfxFmod_UnloadAllBgm", "FmodMusic__unloadAll"),
    0x706BC0: ("FfxFmod_SetPs2Soundtrack", "FmodMusic__setPs2Soundtrack"),
    0x706A70: ("FfxFmod_SetAllBgmPaused", "FmodMusic__setAllPaused"),
    0x706E20: ("FfxFmod_LoadSeProject", "FmodSfx__loadProject"),
    0x706FB0: ("FfxFmod_UnloadSeProjectOf", "FmodSfx__unloadProjectOf"),
    0x706F10: ("FfxFmod_StartSe", "FmodSfx__play"),
    0x706DD0: ("FfxFmod_StartSeIgnorePause", "FmodSfx__playIgnorePause"),
    0x706DB0: ("FfxFmod_DelayStartSe", "FmodSfx__delayStart"),
    0x706F30: ("FfxFmod_StopSe", "FmodSfx__stopSequence"),
    0x706F50: ("FfxFmod_StopSeList", "FmodSfx__stopList"),
    0x706F80: ("maybe_FfxFmod_StopMapSe", "maybe_FmodSfx__stopMapSequence"),
    0x706E50: ("FfxFmod_SetSePaused", "FmodSfx__setPaused"),
    0x706ED0: ("FfxFmod_SetSeVolume", "FmodSfx__setVolume"),
    0x706EF0: ("FfxFmod_FadeSeVolume", "FmodSfx__fadeVolume"),
    0x706E90: ("FfxFmod_SetSePan", "FmodSfx__setPan"),
    0x706EB0: ("FfxFmod_FadeSePan", "FmodSfx__fadePan"),
    0x706DF0: ("FfxFmod_IsSePlaying", "FmodSfx__isSequencePlaying"),
    0x706E70: ("FfxFmod_PlayBattleStreamingSe", "FmodSfx__playBattleStreaming"),
    0x7071E0: ("FfxFmod_SetOptionMasterVolume", "FfxFmod+24, then music, voice and SFX volumes are reapplied"),
    0x707280: ("FfxFmod_SetOptionMusicVolume", "FfxFmod+28, FmodMusic__setCategoryVolume"),
    0x7073A0: ("FfxFmod_SetOptionVoiceVolume", "FfxFmod+32, FmodVoice__setCategoryVolume"),
    0x707330: ("FfxFmod_SetOptionSfxVolume", "FfxFmod+36, FmodSfx__setCategoryVolume"),

    # FmodMusic, 0x34 bytes at FfxFmod+12, 182 track records of 60 bytes at +16
    0x708630: ("FmodMusic__ctor", "allocates the 0x2AA8 byte track array at +16"),
    0x7086B0: ("FmodMusic__dtor", ""),
    0x708720: ("FmodMusic__unloadTrack", "was FmodMusic__f708720. Frees the event unless held (+4) or stopping (+8), zeroes the record and FmodMusic+32"),
    0x708810: ("FmodMusic__unloadAll", "was FmodMusic__f708810. unloadTrack for every track, held ones survive"),
    0x708900: ("FmodMusic__crossfadeTo", "was FmodMusic__f708900. Not held: fade the current out over 60 ticks, load and play the new one. Held and loaded: unpause and fade in over 18 ticks"),
    0x708A00: ("FmodMusic__loadBattleTrack", "was FmodMusic__f708A00. Event index 6 of the music group into slot 16, refused while g_ffxFmodMusicBlocked"),
    0x708AF0: ("FmodMusic__releaseAllEvents", "was FmodMusic__f708AF0. Frees every event including held ones, for the soundtrack switch"),
    0x708E50: ("FmodMusic__tickFades", "was FmodMusic__f708E50. Per FMOD tick (30 Hz wall) volume fades, stops or unloads a track when its fade out ends"),
    0x709000: ("FmodMusic__setAllPaused", "was FmodMusic__f709000"),
    0x709080: ("FmodMusic__loadTrack", "was FmodMusic__f709080. dword_B4F1C8[track] is the event index, refused while g_ffxFmodMusicBlocked"),
    0x7091B0: ("FmodMusic__fadeOutOthers", "was FmodMusic__f7091B0. Fades out over 18 every other playing, audible, unpaused track"),
    0x709270: ("FmodMusic__startDeferred", "was FmodMusic__f709270. When FmodMusic+28, start the current track once its event reads READY"),
    0x7093E0: ("FmodMusic__setCategoryVolume", "was FmodMusic__f7093E0. FmodMusic+48, clamped 0..1, reapplied to every playing track"),
    0x709520: ("FmodMusic__setMuted", "was FmodMusic__f709520. FmodMusic+36, playing tracks go to volume 0, others get a pending 0"),
    0x709640: ("FmodMusic__shutdown", "was FmodMusic__f709640. Unloads and releases the two event projects +8 and +12 with 100 ms sleeps"),
    0x7096F0: ("FmodMusic__play", "was FmodMusic__f7096F0. Fades others out, sets +24, starts with the callback on track +32, or sets +28 deferred when not READY"),
    0x7099A0: ("FmodMusic__stop", "was FmodMusic__f7099A0. If PLAYING clears track +32 and FmodMusic+32, then pauses a held track or stops it"),
    0x709B10: ("FmodMusic__stopAll", "was FmodMusic__f709B10"),
    0x709C80: ("FmodMusic__setPs2Soundtrack", "FmodMusic+44 picks ffx_music.fev or ffx_music_PS2.fev, releases, reloads and replays the current track"),
    0x709D80: ("FmodMusic__isBattleTrackLoading", "was FmodMusic__f709D80. getState of track 16 (+0x3C0) == 2, LOADING"),
    0x709DD0: ("FmodMusic__setTrackVolume", "was FmodMusic__f709DD0. 0..127 base volume +52, normalized +56"),
    0x709EB0: ("FmodMusic__fadeOutAndStop", "was FmodMusic__f709EB0"),
    0x709F20: ("FmodMusic__applyPendingVolume", "was FmodMusic__f709F20. Applies a pending +12/+16 volume to a playing track"),
    0x709FF0: ("FmodMusic__fadeTrackVolume", "was FmodMusic__f709FF0. Fade in FMOD ticks, capped at 120, 360 on map 132 and 180 on map 349 for track 130"),

    # FmodVoice, 0x90 bytes at FfxFmod+16 (most methods were named by VOICE_TIMING.md)
    0x70A1A0: ("FmodVoice__ctor", ""),
    0x70A330: ("FmodVoice__dtor", "frees the VoiceIDMapper and VoiceFevMapper blobs at +36 and +40"),
    0x70B2B0: ("FmodVoice__setCategoryVolume", "+136, applied to the line and to FmodShout"),
    0x70B330: ("FmodVoice__setMuted", "+128, volume 0 or +80"),
    0x70B430: ("FmodVoice__playShout", "FmodShout__play on +132"),

    # FmodShout, 0x10 bytes at FmodVoice+132
    0x70E290: ("FmodShout__ctor", ""),
    0x70E2B0: ("FmodShout__dtor", ""),
    0x70E400: ("FmodShout__setVolume", "was FmodShout__f70E400. Channel::setVolume"),
    0x70E450: ("FmodShout__release", "was FmodShout__f70E450"),
    0x70E480: ("FmodShout__play", "was FmodShout__f70E480. getSubSound by name and playSound from the btl_iop bank, refused while g_ffxFmodMusicBlocked or muted"),

    # FmodSfx, 0xD4 bytes at FfxFmod+20
    0x70BFC0: ("FmodSfx__ctor", "four maps +4 resident projects, +52 map projects, +100 sequence -> SfxEvent, +148 pending unload, mutex +204"),
    0x70C7D0: ("FmodSfx__canStart", "refuses a second instance of an active looping sequence"),
    0x70CCD0: ("FmodSfx__playIgnorePause", "start plus SfxEvent__start without the FmodSfx+0 pause check"),
    0x70CE90: ("FmodSfx__isSequencePlaying", "under the +204 mutex, any SfxEvent of the sequence with entry +4 set"),
    0x70CF10: ("FmodSfx__loadProject", "SFX/US or JP/%04d.fev into the resident or the map map"),
    0x70D0C0: ("FmodSfx__setPaused", "FmodSfx+0 and setPaused on every SfxEvent"),
    0x70D500: ("FmodSfx__setCategoryVolume", "+208, reapplied to every SfxEvent"),
    0x70D640: ("FmodSfx__setPan", "SfxEvent__setPan127 on every instance of a sequence"),
    0x70D690: ("FmodSfx__fadePan", "SfxEvent__fadePan on every instance of a sequence"),
    0x70D6E0: ("FmodSfx__setVolume", "SfxEvent__setVolume127 on every instance of a sequence"),
    0x70D730: ("FmodSfx__fadeVolume", "SfxEvent__fadeVolume on every instance of a sequence"),
    0x70D780: ("FmodSfx__releaseAll", "shutdown path"),
    0x70D890: ("FmodSfx__play", "if FmodSfx+0 is clear and canStart, FmodSfx__start then SfxEvent__start. A paused start is DROPPED"),
    0x70D8F0: ("FmodSfx__stopSequence", "SfxEvent__stop(0) on every instance"),
    0x70D930: ("FmodSfx__stopList", "stops every sequence above 127 in a -1 terminated list, or all of them when the list starts with -1"),
    0x70D9E0: ("FmodSfx__stopOneShots", "stops every non looping SfxEvent and the battle streaming event"),
    0x70DA90: ("FmodSfx__stopAll", ""),
    0x70DB90: ("maybe_FmodSfx__stopMapSequence", "stopSequence only when the id is above 127"),
    0x70DBB0: ("FmodSfx__unloadProjectOf", "finds the map project holding a sequence and unloads it or queues it on +148"),
    0x70DD10: ("FmodSfx__unloadProjects", "(resident too, x), frees map projects, queues busy ones on +148"),
    0x70DEC0: ("FmodSfx__tick", "dt in wall seconds: delayed starts, fades, reaps finished events, unloads queued projects"),

    # SfxEvent, 0x28 bytes, one per started sequence, {Event*, byte playing} entries at +8
    0x70E710: ("SfxEvent__ctor", ""),
    0x70E850: ("SfxEvent__dtor", "clears every entry's callback and releases it"),
    0x70EA20: ("SfxEvent__normalize127", "x / 127 clamped to 0..1"),
    0x70EAC0: ("SfxEvent__delayStart", "+12 = frames / 30 seconds, counted down by FmodSfx__tick"),
    0x70EC40: ("SfxEvent__isLooping", "was SfxEvent__f70EC40. Channel getLoopCount == -1"),
    0x70ED30: ("SfxEvent__isPlaying", "any entry +4 set"),
    0x70EE90: ("SfxEvent__setCategoryVolume", "+36"),
    0x70EEB0: ("SfxEvent__setPan", "was SfxEvent__f70EEB0"),
    0x70EF80: ("SfxEvent__setPan127", "pan from 0..127"),
    0x70F000: ("SfxEvent__fadePan", "was SfxEvent__f70F000"),
    0x70F100: ("SfxEvent__setPaused", "was SfxEvent__f70F100"),
    0x70F140: ("SfxEvent__setVolume", "was SfxEvent__f70F140"),
    0x70F1A0: ("SfxEvent__setVolume127", "volume from 0..127"),
    0x70F210: ("SfxEvent__fadeVolume", "was SfxEvent__f70F210"),
    0x70F310: ("SfxEvent__start", "was SfxEvent__f70F310. setCallback(FfxFmod_EventCallback_ClearFlag, &entry+4), start, entry +4 = 1"),
    0x70F490: ("SfxEvent__stop", "was SfxEvent__f70F490"),
    0x70F5C0: ("SfxEvent__tick", "fades and the delayed start"),

    # SfxProject, 0xC bytes, one FMOD event project
    0x70F760: ("SfxProject__ctor", "loads the .fev plus the %s_common.txt and %s_loop.txt tables"),
    0x70F930: ("SfxProject__unload", "was SfxProject__f70F930"),
    0x70FA80: ("SfxProject__findSequence", ""),
    0x70FB20: ("SfxProject__isLoaded", "was SfxProject__f70FB20. EventProject::getInfo"),
    0x70FCD0: ("maybe_SfxProject__getEvent", "an EventGroup vtable +36 call"),
    0x70FF50: ("SfxProject__loadCommonTable", "%s_common.txt"),
    0x710780: ("SfxProject__loadLoopTable", "%s_loop.txt"),

    # step side PS2 shim
    0x81CF40: ("FFX_Se_PlayBattleStreaming", "command 64 with the magic file sequence, refuses when g_ffxMagicFileId is -1"),
    0x81CF90: ("maybe_FFX_Se_LoadProjectFromWdChunk", "a 'WD' chunk header -> command 48"),
    0x81E3E0: ("FFX_Se_IsPlaying", "command 63"),
    0x81E7A0: ("FFX_Se_IsPlaying_dup", "command 63, same body"),
    0x81E410: ("FFX_Se_StartIgnorePause", "bumps g_ffxSeStartSerial, command 52, returns the serial"),
    0x81E460: ("FFX_Se_SetPan", "command 61"),
    0x81E480: ("FFX_Se_FadePan", "command 62"),
    0x81E4E0: ("FFX_Se_Start", "bumps g_ffxSeStartSerial, command 51, returns the serial"),
    0x81E5D0: ("maybe_FFX_Se_StopMapSe", "-1 stops every sequence above 127 (command 55), else command 56"),
    0x81E750: ("FFX_Se_StopListAndUnload", "command 55 with up to 8 ids, then command 49"),
    0x81E7C0: ("FFX_Se_Stop", "command 54"),
    0x81E7E0: ("FFX_Se_DelayStart", "orphan, bumps g_ffxSeStartSerial, command 53"),
    0x81E820: ("FFX_Se_SetVolume", "command 59"),
    0x81E840: ("FFX_Se_FadeOut", "command 60 with volume 0"),
    0x81E860: ("FFX_Se_FadeVolume", "command 60"),
    0x81EDF0: ("FFX_Shout_ConsumeSkippedFlag", "returns g_ffxShoutSkipped once and clears it"),
    0x81EE20: ("FFX_Shout_PostIopCommand", "writes IOP command 55 into the dead PS2 buffer and sets g_ffxShoutSkipped"),
    0x821B30: ("FFX_Snd_DebugPadToggles", "orphan: pad 0x8000 SE pause, 0x1000 music mute, 0x2000 voice mute"),
    0x886680: ("FFX_Snd_IsSePlaying", "decodes the sound id, command 63. Read by core:403, core:416, the battle SE follow list, BtlOd"),
    0x8860F0: ("FFX_Snd_PlayMenuSe", "FFX_Se_StartIgnorePause(0, id, 63, 127)"),
    0x8863C0: ("FFX_Bgm_IsPlaying", "command 22"),
    0x886800: ("FFX_Bgm_Unload", "command 37, forgets g_ffxSndPlayingBgm"),
    0x886830: ("FFX_Bgm_Crossfade", "core:382, command 23 when the track is not already playing"),
    0x8868B0: ("FFX_Bgm_PlayNoPending", "core:380 and 381, command 33, volume 127"),
    0x8868F0: ("FFX_Bgm_PlayArg2", "core:487, command 27, volume 127"),
    0x886940: ("FFX_Bgm_FadeVolumeRaw", "core:391, command 34 without the silence override"),
    0x886960: ("FFX_Bgm_Load", "SPINS the step thread while the voice busy bits are set, then command 40"),
    0x8869E0: ("FFX_Bgm_ReleaseHold", "command 26"),
    0x8869F0: ("FFX_Bgm_HoldCurrent", "command 25"),
    0x886A00: ("FFX_Bgm_LoadBattle", "command 21"),
    0x886A20: ("FFX_Bgm_Play", "records g_ffxSndPlayingBgm, command 39 with the step side pending mark, volume 127"),
    0x886A60: ("FFX_Bgm_Stop", "command 38, forgets g_ffxSndPlayingBgm"),
    0x886AB0: ("FFX_Bgm_SetVolume", "command 35, 0 while byte_1330090"),
    0x886AF0: ("FFX_Bgm_FadeOutAndStop", "command 28"),
    0x886B40: ("FFX_Bgm_FadeVolume", "command 34, 0 while byte_1330090"),
    0x886BA0: ("FFX_Snd_PlaySeDefault", "FFX_Se_Start(0, id, 63, 127), skipped in battle during booster auto battle"),
    0x886E80: ("FFX_Snd_StartSeById", "decodes the sound id, pan 63, vol 127"),
    0x886EB0: ("FFX_Snd_StopSeById", ""),
    0x886F60: ("FFX_Snd_SetSeVolumeById", "core:254"),
    0x886FF0: ("FFX_Snd_FadeSeVolumeById", "core:256, (id, frames, vol)"),
    0x886DF0: ("FFX_Snd_SetSePanById", "core:255"),
    0x886E20: ("FFX_Snd_FadeSePanById", "core:257"),
    0x871890: ("FFX_Snd_StopAndUnloadAll", "stops every SE, BGM, unloads, pumping the voice loader"),
    0x8873F0: ("FFX_Voice_PollFmodAndShoutBusy", "command 17 plus the one shot shout flag into the issued bits"),
    0x8875A0: ("FFX_Snd_PlayBattleStreaming", ""),
    0x8875D0: ("FFX_Shout_Play", "command 6, then marks the voice busy bits for one recompute"),
    0x887970: ("maybe_FFX_Voice_SetPan", "command 12, a stub on PC"),
    0x887980: ("maybe_FFX_Voice_FadePan", "command 11, a stub on PC"),
    0x887990: ("FFX_Voice_SetVolume", "command 14, 0 when sub_872340 is 0"),
    0x8879B0: ("FFX_Voice_FadeVolume", "command 13, 0 when sub_872340 is 0"),
    0x862470: ("FFX_Btl_BootTaskMusic", "the battle MUSIC boot task, state 7 waits on command 20, the cross thread sync"),
    0x7A0930: ("FFX_Btl_TickSeFollowList", "from FFX_Btl_MainStep, drops an entry when FFX_Snd_IsSePlaying reads 0"),
    0x7A0A30: ("maybe_FFX_Btl_AddSeFollow", "returns 1 when the id is already followed and a4 is set"),
    0x6DB070: ("FFX_Lang_GetManager", "returns the sLanguageManager singleton dword_CDED58"),
    0x6DABF0: ("FFX_Fmv_SetVolume", "flt_C34420"),
    0x81D5F0: ("maybe_FFX_SndIop_FlushCommandBuffer", "the dead PS2 IOP command buffer at off_C4946C"),

    # ATEL rows still positional
    0x85F110: ("FFX_AtelSys_Core_398_SetVoiceVolume_resi", "dead row, byte_1327134 = level, command 14"),
}

# ---------------------------------------------------------------------------------------------
# Real names that are wrong or say less than is now known
# ---------------------------------------------------------------------------------------------

RENAMES = {
    0x70AB90: ("FmodVoice__initList", "was FmodMusic__initList. this is FfxFmod+16, the FmodVoice. It writes the VoiceIDMapper and VoiceFevMapper blobs to +36 and +40, which FmodVoice__dtor frees. The FmodMusic log string is a source copy and paste"),
    0x679FE0: ("FFX_FmodCmd_SetPs2Soundtrack", "was maybe_FFX_Config_SetSlot46FromSaveBit. Slot 46 is FMOD command 46, FmodMusic__setPs2Soundtrack"),
    0x859DE0: ("FFX_AtelSys_Core_443_IsBgmPlaying_resi", "was FFX_AtelSys_Core_443_CallSoundCmd67A150_resi. Command 22, FmodMusic+32 | track[cur].+32"),
    0x85E400: ("FFX_AtelSys_Core_460_GetPlayingBgm_resi", "was FFX_AtelSys_Core_460_GetSoundWord132FF5C_resi. g_ffxSndPlayingBgm, the shim's own record of the playing track"),
    0x85F240: ("FFX_AtelSys_Core_399_FadeVoiceVolume_resi", "was FFX_AtelSys_Core_399_FadeSoundLevel134_resi. Command 13, FmodVoice__beginVolumeFade"),
    0x859C40: ("FFX_AtelSys_Core_359_HoldCurrentBgm_resi", "was FFX_AtelSys_Core_359_ClearBgmCue_resi. Command 25 sets track[cur].+4 = 1, it does not clear anything"),
}

# ---------------------------------------------------------------------------------------------
# Data
# ---------------------------------------------------------------------------------------------

GLOBALS = {
    0xCE8458: ("g_ffxFmodCmdRing", "was ArgList. 150 x 20 byte entries {cmd, a, b, c, d}, write index FmodManager+40, read +44. ArgList was a parameter name that leaked onto the global"),
    0xCE845C: ("g_ffxFmodCmdRingArgs", "the a field of entry 0"),
    0xC3A368: ("g_ffxFmodCmdHandlers", "70 FMOD thread handlers indexed by command, called as fn(FfxFmod*, &args)"),
    0xCEC174: ("g_ffxFmodMusicBlocked", "set by FmodVoice__dataChange while g_ffxFmodVoiceBusyDisabled (maps 393..398), refuses music loads and shouts"),
    0x11361A0: ("g_ffxBtlSeFollowList", "linked list of battle SEs whose pan and volume follow a position"),
    0x12F4E7C: ("g_ffxSeStartSerial", "step side serial bumped by every SE start, never 0"),
    0x12F4E94: ("g_ffxShoutSkipped", "set by FFX_Shout_PostIopCommand, read once by FFX_Shout_ConsumeSkippedFlag into the voice busy bits"),
    0x132FF5C: ("g_ffxSndPlayingBgm", "the shim's playing track, set on play, -1 on stop or unload of that track. core:460 returns it"),
    0xB4F1C8: ("g_ffxFmodBgmEventIndex", "182 dwords, track -> event index in the music group"),
    0xC34420: ("g_ffxFmvVolume", "float"),
    0x1325B7C: ("g_ffxBtlBootDefersSeUnload", "set by the battle MUSIC boot task, QueueCommand drops command 49 while it is set and the task sends 49 itself"),
    0x1327134: ("g_ffxAtelVoiceVolume", "byte, core:398 and 399, 127 at map reset"),
    0x1327135: ("maybe_g_ffxAtelVoicePan", "byte, core:400 and 401, 63 at map reset, the FMOD side is a stub"),
    0x13300E0: ("g_ffxBgmVolumeByTrack", "256 bytes, the shim's last volume per track"),
    0xC4946C: ("g_ffxSndIopCmdWrite", "write pointer into the dead PS2 IOP command buffer"),
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
