# Sound classes: FmodManager, FfxFmod and what the step can see of them

This is the FMOD side of FFX on PC: the manager thread and its command ring, the `FfxFmod` hub, and
the four players under it (`FmodMusic`, `FmodVoice` with `FmodShout`, `FmodSfx` with `SfxProject`
and `SfxEvent`). It also covers the step side PS2 shim that feeds them, the booster's audio path,
and every place where audio decides something the simulation then reads.

The voice line itself (SyncData, syscalls 213 to 217, the line length) is in `VOICE_TIMING.md` and
is not redone here. Section 6 only adds what that doc did not have.

Marks: **[V]** read in the binary (decompile or disassembly, with the address), **[D]** counted in
the shipped data (`tools/ebp.py` over every `.ebp`), **[I]** inferred. All addresses are IDA VAs,
image base 0x400000. The names used below are the ones `tools/apply_sound_names.py` proposes. The
IDB still has the old ones until that script is run, and section 15 maps them.

---

## Summary, read this first

1. **Two threads, one ring.** The step thread (T0) never touches FMOD. It calls the
   `FFX_FmodCmd_*` wrappers at 0x679FB0..0x67A7D0, which post a 20 byte command into the 150 entry
   ring at 0xCE8458. The FmodManager thread (T5) wakes at 30 Hz, drains the ring through a 70 entry
   handler table at 0xC3A368 and runs every FMOD call. [V]
2. **Seven commands run on the caller, not on the FMOD thread.** `FfxFmod_RunCommandSync 0x6FA500`
   runs 17, 18, 19, 22, 25, 63 and 65 inline on the step thread, reading FMOD thread state without
   the FMOD thread's help. Exactly one command the step actually uses, 20 (is the battle track still
   loading), crosses threads synchronously, and it gives up after two 5 ms waits and returns 1. [V]
3. **One callback clears three kinds of "playing" flag.** `FfxFmod_EventCallback_ClearFlag
   0x707A50` writes 0 to its userdata on callback types 2 and 4. It is registered with
   `&FmodVoice+64` (the voice busy flag), `&track+32` (a music track playing) and `&entry+4` (an
   SFX instance playing). So the FMOD event's own end, on FMOD's clock, clears all three. For voice
   this is a second path next to the `lengthms` wall clock in `VOICE_TIMING.md`. [V], the meaning
   of types 2 and 4 [I]
4. **The script reads three of these back.** core:443 (4 scripts) waits for the music to end. The
   battle MUSIC boot task waits on command 20. core:403, core:416, the battle SE follow list and
   BtlOd branch on "is this SE playing". All three answers come from FMOD's clock. [V] [D]
5. **The booster speeds up nothing in audio.** `g_boosterSpeedIndex` is never read by sound code.
   `FFX_Snd_SetFastForward 0x821C60` stops the one shot SFX, pauses all SFX and mutes the voice.
   Music keeps playing at 1x, the voice line keeps running at 1x on its wall clock, and there is no
   pitch or frequency call anywhere in the FMOD imports. [V]
6. **Paused SFX starts are dropped, not deferred.** While `FmodSfx+0` is set (fast forward, the
   pause overlay, the EscMenu) `FmodSfx__play` and `FmodSfx__delayStart` return without starting
   anything, so a later "is it playing" reads 0. [V]
7. **Volume, pan, mute and fades are presentation only.** A muted or zero volume event still plays
   and still ends on its own clock, so none of the mute paths change timing. [V] for the paths, [I]
   that FMOD keeps a silent event alive.
8. **For lockstep and for the recreation:** every FMOD sourced answer the step reads (music ended,
   battle track loaded, SE playing, voice ended) has to become a step side countdown or a host
   ordered event. Section 14.

---

## 1. The shape

```
step thread (T0)
  ATEL syscalls, battle, menus, FFX_MainInit, scene init
    -> PS2 shim: FFX_Bgm_* 0x8863C0..0x886B40, FFX_Snd_*/FFX_Voice_* 0x886xxx..0x887xxx,
                 FFX_Se_* 0x81CF40..0x81EE20
    -> FFX_FmodCmd_* 0x679FB0..0x67A7D0                    (one per command)
    -> FfxFmod_QueueCommand 0x6FA300 / FfxFmod_RunCommandSync 0x6FA500
       ring g_ffxFmodCmdRing 0xCE8458, 150 x {cmd, a, b, c, d}

FmodManager thread (T5), 30 Hz on NowMicroseconds
  FmodManager__tick 0x6FA700 -> FfxFmod_DrainCommandQueue 0x6FA7E0
    -> g_ffxFmodCmdHandlers 0xC3A368[cmd](FfxFmod*, &args)  (FfxFmod_CmdNN_*)
    -> FfxFmod_* forwarders 0x7069E0..0x7075xx
    -> FmodMusic  (FfxFmod+12)
       FmodVoice  (FfxFmod+16) -> FmodShout (FmodVoice+132)
       FmodSfx    (FfxFmod+20) -> SfxProject, SfxEvent
  FfxFmod__tick 0x7070D0 every 33 ms of wall time
```

The PS2 layer is still there under the shim: an IOP command buffer at `g_ffxSndIopCmdWrite
0xC4946C`, flushed by `maybe_FFX_SndIop_FlushCommandBuffer 0x81D5F0`, and pitch handlers that print
"not implemented". Only the shout path still writes into it (section 7). [V]

---

## 2. FmodManager

RTTI `.?AVFmodManager@@`, vtable 0xB4C2E4, 0x50 bytes, singleton `g_ffxFmodManager 0xCE9010`.
Created at boot by `FmodManager__createSingleton 0x6FA0B0` (`BOOT_SEQUENCE.md` step 81), destroyed
by `FFX_Fmod_DestroyManager 0x679FB0` from `FFXApplication__exitApplication`. It owns the FMOD
thread and nothing else: every FMOD call happens under it, on T5. [V]

| offset | field | evidence |
| --- | --- | --- |
| +0 | vtable | ctor |
| +8 | `FfxFmod*` | `FfxFmod__ctor` called from `FmodManager__ctor`, deleted by `FmodManager__dtor` |
| +12 | thread object | ctor |
| +16 | ring mutex | QueueCommand |
| +20 | sync mutex | RunCommandSync takes it for the whole call |
| +24 | kick event | RunCommandSync and QueueCommand set it |
| +28 | sync done event | the drain sets it |
| +32 | double, base time in ms | tick |
| +40 | ring write index | QueueCommand |
| +44 | ring read index | drain |
| +48 | sync command | RunCommandSync |
| +52..+64 | sync args a..d | RunCommandSync |
| +68 | byte, sync done | drain sets it to 1 after running the sync command |
| +72 | sync result | drain, or 1 preset by RunCommandSync |
| +76 | byte, running | dtor clears it |
| +77 | byte, exited | thread sets it, dtor sleeps on it |

All [V].

| address | name | what it does |
| --- | --- | --- |
| 0x6F9DE0 | `FmodManager__ctor` | creates the mutexes and events, `FfxFmod__ctor`, queues commands 42 (FMOD init) and 43, starts the thread. The priority call with 300 fails, see `THREADS_AND_CLOCKS.md` T5 |
| 0x6F9F70 | `FmodManager__dtor` | clears +76, sleeps until +77, deletes the FfxFmod, closes the handles |
| 0x6FA040 | `FmodManager__scalar_deleting_dtor` | |
| 0x6FA0B0 | `FmodManager__createSingleton` | |
| 0x6FA190 | `FmodManager__destroySingleton` | |
| 0x6FA1B0 | `FmodManager__threadMain` | loops `FmodManager__tick` while +76, then drains once more, `FfxFmod__shutdown`, sets +77 |
| 0x6FA200 | `FmodManager__getFmodSystem` | returns `FfxFmod+8`, the `FMOD::System*`. `sub_6D8F30` hands it to the movie player, so movie audio goes through the same FMOD system |
| 0x6FA280, 0x6FA2A0, 0x6FA2C0, 0x6FA2E0 | `FfxFmod_QueueCommand0..3` | zero to three argument wrappers |
| 0x6FA300 | `FfxFmod_QueueCommand` | under +16: coalesces 59 and 61 with a queued entry of the same id, drops 49 while `g_ffxBtlBootDefersSeUnload 0x1325B7C` is set, logs "Too many fmod commands at the same time!!" when full, kicks the thread for 8 and 58 during boot scenes |
| 0x6FA480, 0x6FA4A0, 0x6FA4C0, 0x6FA4E0 | `FfxFmod_RunCommandSync0..3` | wrappers, the last two are orphans |
| 0x6FA500 | `FfxFmod_RunCommandSync` | section 2.1 |
| 0x6FA690 | `FfxFmod_MarkBgmPlayPending` | **step thread**, writes `FmodMusic+24 = track` and `FmodMusic+32 = 1` directly when track <= 181, before command 39 is queued. The music twin of `FfxFmod_MarkVoiceStartPending 0x6FA6C0` |
| 0x6FA700 | `FmodManager__tick` | waits the kick event for the rest of 33.333 ms, `FfxFmod__tick` when a whole period has passed, then drains |
| 0x6FA7E0 | `FfxFmod_DrainCommandQueue` | runs a pending sync command first (when +68 is 0), then every ring entry |
| 0x6FA880 | `FmodManager__runFmodTick` | orphan, `FfxFmod__tick(this+8)` |

### 2.1 RunCommandSync, inline or cross thread

```
0x6FA500  WaitForSingleObject(this+20)            ; the sync mutex
          +48 = cmd, +52..+64 = args
          switch (cmd)
            case 17, 18, 19, 22, 25, 65:
              +72 = funcs_6FA559[cmd - 17](FfxFmod, this+52)   ; on the CALLER's thread
            case 63:
              +72 = handler(...)
              if (+72 == 0) scan the ring from +44 to +40 for {51, same id} -> +72 = 1
            default:
              +72 = 1, +68 = 0
              up to 2 x { SetEvent(+24), WaitForSingleObjectEx(+28, 5 ms) }
              if (+68 == 0) log "registCommandSync TimeOut: %d"
              +68 = 1
          return +72
```

[V]. Three things follow.

- The inline seven read FMOD thread fields with no lock, except 63, whose handler takes the
  FmodSfx mutex at +204. They are 17 voice playing, 18 voice paused, 19 current track, 22 music
  playing, 25 hold the current track (a write), 63 SE playing, 65 SFX paused. [V]
- The only cross thread sync command the step uses is **20**, from `FFX_FmodCmd_IsBattleBgmLoading
  0x67A240`. Its timeout returns the preset 1, "still loading", and setting +68 = 1 means the late
  FMOD thread will not run it afterwards. So a slow FMOD thread reads as "not ready yet". [V]
- Command 63 also says "playing" when a start for the same id is still queued, so a script that
  starts an SE and asks on the next step gets 1 even if the FMOD thread has not woken. [V]

---

## 3. The command table

`g_ffxFmodCmdHandlers 0xC3A368`, 70 entries, called as `fn(FfxFmod*, int* args)` and returning the
sync result. "inline" marks the seven that RunCommandSync runs on the caller. The step wrapper
column is the `FFX_FmodCmd_*` function, and its callers are the step side users. Every row [V]
unless marked.

| cmd | FMOD side target | step wrapper | step callers |
| --- | --- | --- | --- |
| 0 | `FfxFmod_SetMuted(on, type)`: 1 `FmodMusic__setMuted`, 2 `FmodVoice__setMuted` | 0x67A570 `SetMuted` | `FFX_Snd_SetFastForward` (type 2), the orphan `FFX_Snd_DebugPadToggles 0x821B30` |
| 1 | `FfxFmod_InitData 0x707080`: language, music projects, `FmodVoice__initList`, voice and shout data | 0x67A500 `InitData` | `FFX_MainInit` |
| 2 | EventSystem::update | 0x67A5F0 (orphan) | none |
| 3 | `FfxFmod__tick` | none | none |
| 4 | `FmodVoice__resetForMap` | 0x67A5D0 `ResetVoiceForMap` | `maybe_FFX_Scene_Init` |
| 5 | voice playing, a copy of 17 | 0x67A520 (orphan) | none |
| 6 | `FmodVoice__playShout` -> `FmodShout__play` | 0x67A4E0 `PlayShout` | `FFX_Shout_Play 0x8875D0` <- Save_058, `sub_7A0820` |
| 7 | `FmodVoice__unloadBanks` | 0x67A740 (orphan) | none |
| 8 | `FmodVoice__setPaused` | 0x67A750 `SetVoicePaused` | `FFX_Snd_SetPausedForOverlay`, `FFX_EscMenu_OnResume`, `sub_68F940`, `sub_68FF60` |
| 9 | `FmodVoice__dataChange`, also `FmodMusic+40 = map` | 0x67A6A0 `VoiceDataChange` | `maybe_FFX_Scene_Init` |
| 10 | `FmodVoice__stop` | `FFX_Voice_Stop` path | `VOICE_TIMING.md` |
| 11, 12 | empty stubs | 0x67A6F0, 0x67A6D0 | core:401, core:400 (both dead), so voice pan does nothing on PC [I for "pan"] |
| 13 | `FmodVoice__beginVolumeFade`, frames halved | 0x67A7D0 `FadeVoiceVolume` | core:399 |
| 14 | `FmodVoice__setVolume` | 0x67A7B0 `SetVoiceVolume` | core:398 (dead), `sub_7FFC60` |
| 15, 16 | voice start, prepare | `FFX_FmodCmd_StartVoice`, `FFX_FmodCmd_PrepareVoice` | `VOICE_TIMING.md` |
| 17 inline | `FmodVoice+64 \| +68` | `FFX_FmodCmd_QueryVoicePlaying 0x67A710` | the voice step loader |
| 18 inline | `FmodVoice+84` paused | 0x67A6C0 `IsVoicePaused` | `sub_68F940` (EscMenu) |
| 19 inline | `FmodMusic+24` current track | 0x679FD0 (orphan) | none |
| 20 | `FmodMusic__isBattleTrackLoading`: track 16 `getState == 2` | 0x67A240 `IsBattleBgmLoading`, **cross thread** | `FFX_Btl_BootTaskMusic 0x862470` state 7 |
| 21 | `FmodMusic__loadBattleTrack`: event index 6 into slot 16 | 0x67A060 `LoadBattleBgm` | `FFX_Bgm_LoadBattle 0x886A00` <- Save_038, the MUSIC task |
| 22 inline | `FmodMusic+32 \| track[FmodMusic+24].+32` | 0x67A150 `IsBgmPlaying` | `FFX_Bgm_IsPlaying 0x8863C0` <- core:443 |
| 23 | `FmodMusic__crossfadeTo` | 0x67A040 `CrossfadeBgm` | `FFX_Bgm_Crossfade 0x886830` <- core:382 |
| 24 | empty stub | | core:360 |
| 25 inline | `track[FmodMusic+24].+4 = 1`, hold | 0x67A1B0 `HoldCurrentBgm` | `FFX_Bgm_HoldCurrent 0x8869F0` <- core:359, the MUSIC task |
| 26 | `track.+4 = 0` | 0x67A190 `ReleaseBgmHold` | `FFX_Bgm_ReleaseHold 0x8869E0` <- core:260, 380, 382, 392, 487, the MUSIC task |
| 27 | `FmodMusic__play`, second argument dropped | 0x67A0D0 `PlayBgmArg2` | `FFX_Bgm_PlayArg2 0x8868F0` <- core:487 |
| 28 | `FmodMusic__fadeOutAndStop` | 0x67A2B0 `FadeOutStopBgm` | `FFX_Bgm_FadeOutAndStop 0x886AF0` |
| 29..32 | empty stubs | | core:389, 388, orphan 0x67A110, core:390 |
| 33 | `FmodMusic__play` | 0x67A090 `PlayBgmNoPending` | `FFX_Bgm_PlayNoPending 0x8868B0` <- core:380, 381 |
| 34 | `FmodMusic__fadeTrackVolume`, frames halved | 0x67A2D0 `FadeBgmVolume` | `FFX_Bgm_FadeVolume 0x886B40`, `FFX_Bgm_FadeVolumeRaw 0x886940` |
| 35 | `FmodMusic__setTrackVolume` | 0x67A290 `SetBgmVolume` | `FFX_Bgm_SetVolume 0x886AB0` |
| 36 | empty stub | | core:361 |
| 37 | `FmodMusic__unloadTrack` | 0x67A010 `UnloadBgm` | `FFX_Bgm_Unload 0x886800` |
| 38 | `FmodMusic__stop` | 0x67A210 `StopBgm` | `FFX_Bgm_Stop 0x886A60` |
| 39 | `FmodMusic__play` | 0x67A1E0 `PlayBgm`, after `FfxFmod_MarkBgmPlayPending` | `FFX_Bgm_Play 0x886A20` |
| 40 | `FmodMusic__loadTrack`, event index `g_ffxFmodBgmEventIndex 0xB4F1C8[track]` | 0x67A170 `LoadBgm` | `FFX_Bgm_Load 0x886960` <- core:258 |
| 41 | `FfxFmod__shutdown` | | exit |
| 42 | `FfxFmod__init` | | `FmodManager__ctor` |
| 43 | two nullsubs | | `FmodManager__ctor` |
| 44 | `FmodMusic__stopAll` | 0x67A230 `StopAllBgm` | `FFX_Snd_StopAndUnloadAll 0x871890` |
| 45 | `FmodMusic__unloadAll` | 0x67A030 `UnloadAllBgm` | `FFX_Snd_StopAndUnloadAll` |
| 46 | `FmodMusic__setPs2Soundtrack` | 0x679FE0 `SetPs2Soundtrack` | `FFX_Menu_ApplySaveConfigBit5EC` |
| 47 | `FmodMusic__setAllPaused` | 0x67A1C0 (orphan) | none |
| 48 | `FmodSfx__loadProject`, `SFX/US\|JP/%04d.fev` | 0x67A360 `LoadSeProject` | `sub_887240` (16 resident ids), `sub_81CF90`, `sub_81E3B0`, `sub_81E600` |
| 49 | `FmodSfx__unloadProjects(0, 0)` | 0x67A4D0 `UnloadMapSeProjects` | `FFX_Se_StopListAndUnload 0x81E750`, the MUSIC task |
| 50 | `FmodSfx__unloadProjectOf` | none | none |
| 51 | `FmodSfx__play` | 0x67A440 `StartSe` | `FFX_Se_Start 0x81E4E0` <- Save_014, `FFX_Snd_StartSeById`, `FFX_Snd_PlaySeDefault` (99 call sites) |
| 52 | `FmodSfx__playIgnorePause` | 0x67A310 `StartSeIgnorePause` | `FFX_Se_StartIgnorePause 0x81E410` <- `FFX_Snd_PlayMenuSe 0x8860F0` |
| 53 | `FmodSfx__delayStart` | 0x67A2F0 `DelayStartSe` | `FFX_Se_DelayStart 0x81E7E0` (orphan) |
| 54 | `FmodSfx__stopSequence` | 0x67A460 `StopSe` | `FFX_Se_Stop 0x81E7C0` |
| 55 | `FmodSfx__stopList` | 0x67A480 `StopSeList` | `0x81E5D0`, `FFX_Se_StopListAndUnload` |
| 56 | `maybe_FmodSfx__stopMapSequence`, ids above 127 only | 0x67A4A0 | `0x81E5D0` |
| 57 | `FmodSfx__stopOneShots` | 0x67A4C0 `StopOneShotSe` | `FFX_Snd_SetFastForward` |
| 58 | `FmodSfx__setPaused` | 0x67A380 `SetSePaused` | `FFX_Snd_SetFastForward`, `FFX_Snd_SetPausedForOverlay`, `FFX_EscMenu_OnResume`, `sub_68F940`, `sub_68FF60`, the debug pad toggles |
| 59 | `FmodSfx__setVolume` | 0x67A400 `SetSeVolume` | `FFX_Se_SetVolume` <- core:254 |
| 60 | `FmodSfx__fadeVolume` | 0x67A420 `FadeSeVolume` | `FFX_Se_FadeVolume`, `FFX_Se_FadeOut` <- core:256 |
| 61 | `FmodSfx__setPan` | 0x67A3C0 `SetSePan` | `FFX_Se_SetPan` <- core:255 (dead) |
| 62 | `FmodSfx__fadePan` | 0x67A3E0 `FadeSePan` | `FFX_Se_FadePan` <- core:257 (dead) |
| 63 inline | `FmodSfx__isSequencePlaying` plus the ring scan | 0x67A340 `IsSePlaying` | `FFX_Se_IsPlaying` <- `FFX_Snd_IsSePlaying 0x886680` |
| 64 | `FmodSfx__playBattleStreaming` | 0x67A3A0 | `FFX_Se_PlayBattleStreaming 0x81CF40` <- `FFX_Snd_PlayBattleStreaming 0x8875A0` |
| 65 inline | `FmodSfx+0` paused | 0x67A330 `IsSePaused` | `sub_68F940` (EscMenu) |
| 66..69 | master, music, voice, sfx option volume, percent / 100 | 0x67A530, 0x67A550, 0x67A5B0, 0x67A590 | the EscMenu audio rows at 0xC39630, and `FFX_Snd_ApplyVolumeOptions 0x67A600` |

Commands 57 and 58 sit swapped in memory (58 at 0x7082D0, 57 at 0x7082F0), the table indexes them
correctly. Entries 70 and 71 are -1 and 2, past the end. [V]

---

## 4. FfxFmod

0x28 bytes at `FmodManager+8`. The hub: it owns the FMOD `EventSystem` and the three players, and
every handler goes through one of its forwarders. FMOD thread only, except for the inline commands.
[V]

| offset | field |
| --- | --- |
| +0 | memory pool |
| +4 | `FMOD::EventSystem*` |
| +8 | `FMOD::System*` |
| +12 | `FmodMusic*` (0x34) |
| +16 | `FmodVoice*` (0x90) |
| +20 | `FmodSfx*` (0xD4) |
| +24, +28, +32, +36 | option volumes master, music, voice, sfx, floats, 1.0 at ctor |

| address | name | what it does |
| --- | --- | --- |
| 0x706490 | `FfxFmod__ctor` | volumes to 1.0, allocates the three players |
| 0x706590 | `FfxFmod__dtor` | |
| 0x706820 | `FfxFmod__init` (command 42) | `FMOD_Memory_Initialize(0, 0, FfxFmod_MemAlloc 0x7066D0, FfxFmod_MemRealloc 0x706730, FfxFmod_MemFree 0x706710, -1)`, `EventSystem_Create`, `setStreamBufferSize(0x10000)`, `init(128)`, `setFileSystem` with open 0x708510, close 0x7084D0, read 0x7085A0, seek 0x7085E0 |
| 0x706FF0 | `FfxFmod__shutdown` (command 41) | voice, music, SFX shutdown, each followed by a 100 ms sleep, then EventSystem unload and release |
| 0x7070D0 | `FfxFmod__tick` | when more than 33000 us passed: EventSystem::update, `FmodVoice__checkLineElapsed`, `FmodMusic__tickFades`, `FmodVoice__stepVolumeFade`, `FmodVoice__startDeferred`, `FmodMusic__startDeferred`, `FmodSfx__tick(dt seconds)` |
| 0x707A50 | `FfxFmod_EventCallback_ClearFlag` | the only event callback in the program, section 13.1 |
| 0x7079D0 | `FfxFmod_CheckResult` | logs a failed `FMOD_RESULT` with source file and line |
| 0x707590 | `FfxFmod_ResultString` | |
| 0x707AA0 | `FfxFmod_BuildDataPath` | `/FFX_Data/GameData/PS3Data/Sound_PC/%s` |
| 0x707B20 | `FfxFmod_Log` | |
| 0x7071E0, 0x707280, 0x7073A0, 0x707330 | `FfxFmod_SetOption{Master,Music,Voice,Sfx}Volume` | store the float and push `master * category` to the player |

The one line forwarders between the handlers and the players are all named in the names module as
`FfxFmod_<what>`. [V]

---

## 5. FmodMusic

0x34 bytes at `FfxFmod+12`, ctor 0x708630, dtor 0x7086B0. BGM: two event projects (the arranged
`Music/ffx_music.fev` and the original `Music_PS2/ffx_music_PS2.fev`), one event group `"music"`
each, and 182 track slots. FMOD thread, except +24 and +32 which the step thread also writes. [V]

| offset | field | evidence |
| --- | --- | --- |
| +0 | EventGroup, arranged | every user takes +0 unless +44 is 1 |
| +4 | EventGroup, PS2 | every user takes +4 when +44 is 1, `FmodMusic__unloadTrack` for one |
| +8, +12 | EventProjects | `FmodMusic__shutdown` releases both |
| +16 | track array, 182 x 60 bytes (0x2AA8) | every loop runs `i < 10920` step 60 |
| +24 | current track | `FmodMusic__play`, `FfxFmod_MarkBgmPlayPending` (step thread) |
| +28 | deferred start pending | `play` sets it when the event is not READY, `startDeferred` clears it |
| +32 | play pending | set to 1 only by `FfxFmod_MarkBgmPlayPending` on the step thread, cleared by `play`, `stop`, `stopAll`, `unloadTrack`, `startDeferred` |
| +36 | muted | `FmodMusic__setMuted` |
| +40 | map id | `FmodVoice__dataChange` |
| +44 | use the PS2 soundtrack | `FmodMusic__setPs2Soundtrack`, picks +4 over +0 everywhere |
| +48 | category volume 0..1 | `FmodMusic__setCategoryVolume` |

Track record, 60 bytes:

| offset | field |
| --- | --- |
| +0 | `FMOD::Event*` |
| +4 | hold: stop pauses it, unload skips it |
| +8 | stopping |
| +12, +16, +20 | pending volume flag, value, extra |
| +24, +28 | deferred fade in flag and frames |
| +32 | **playing**, set at start, cleared by the callback (userdata `&track+32`) and by stop and unload |
| +36, +40, +44, +48 | fade active, ticks left, step, target |
| +52 | base volume 0..127 |
| +56 | normalized volume |

[V]. Fades count FMOD ticks, 30 per wall second, so a fade is wall clock. The FMOD side handlers
halve the frame counts they are given (commands 11, 13, 29, 34) to turn 60 Hz frames into 30 Hz
ticks. [V]

| address | name | what it does |
| --- | --- | --- |
| 0x708720 | `FmodMusic__unloadTrack` | frees the event unless held or stopping, zeroes the record and +32 |
| 0x708810 | `FmodMusic__unloadAll` | held tracks survive |
| 0x708900 | `FmodMusic__crossfadeTo` | not held: deferred fade in, fade the current out over 60 ticks, load and play. Held and loaded: fade 16 and 138 out over 18, unpause, `+24 = track`, `track+32 = 1`, fade in over 18 |
| 0x708A00 | `FmodMusic__loadBattleTrack` | unloads slot 16 if loaded, then event index 6 of the group into slot 16 when the argument is nonzero, refused while `g_ffxFmodMusicBlocked` |
| 0x708AF0 | `FmodMusic__releaseAllEvents` | including held, for the soundtrack switch |
| 0x708C00, 0x708D40 | `FmodMusic__initData_2`, `FmodMusic__initData` | kept |
| 0x708E50 | `FmodMusic__tickFades` | per FMOD tick, stops or unloads a track when its fade out reaches 0 |
| 0x709000 | `FmodMusic__setAllPaused` | |
| 0x709080 | `FmodMusic__loadTrack` | `g_ffxFmodBgmEventIndex[track]`, refused while `g_ffxFmodMusicBlocked` |
| 0x7091B0 | `FmodMusic__fadeOutOthers` | fades out over 18 every other playing, audible, unpaused track |
| 0x709270 | `FmodMusic__startDeferred` | starts the current track once its event reads READY |
| 0x7093E0 | `FmodMusic__setCategoryVolume` | |
| 0x709520 | `FmodMusic__setMuted` | |
| 0x709640 | `FmodMusic__shutdown` | |
| 0x7096F0 | `FmodMusic__play` | `fadeOutOthers`, `+24 = track`, then if not already playing and READY: `setCallback(ClearFlag, &track+32)`, start, `+32 = 1`, FmodMusic+32 = 0. Not READY: +28 = 1, except that on map 121 an event already PLAYING is stopped and started again [I], the decompile mixes the state and the track here |
| 0x7099A0 | `FmodMusic__stop` | if PLAYING clears `track+32` and FmodMusic+32, then pauses a held track or clears the callback and stops |
| 0x709B10 | `FmodMusic__stopAll` | |
| 0x709C80 | `FmodMusic__setPs2Soundtrack` | releases everything, switches +44, reloads and replays the current track |
| 0x709D80 | `FmodMusic__isBattleTrackLoading` | `getState(track[16])` and `cmp [ebp+var_4], 2` at 0x709DB6 |
| 0x709DD0 | `FmodMusic__setTrackVolume` | |
| 0x709EB0 | `FmodMusic__fadeOutAndStop` | |
| 0x709F20 | `FmodMusic__applyPendingVolume` | |
| 0x709FF0 | `FmodMusic__fadeTrackVolume` | ticks capped at 120, raised to 360 on map 132 and 180 on map 349 for track 130 |

Who calls it on the step side: the BGM syscalls in `ATEL_CORE_LIBRARY.md` (258 load, 259 stop, 260
play, 359 hold, 362/383 volume, 363/384 fade, 364/365 stop, 380/381/487 play, 382 crossfade, 391
fade, 392, 443 is playing, 504), `FFX_Btl_BootTaskMusic`, `FFX_BtlResult_StepStateMachine`,
`FFX_Btl_EndPhaseStep`, `FFX_Map_ReturnToTitle`, `maybe_FFX_Map_Unload`. [V]

The shim keeps its own copy of what plays: `g_ffxSndPlayingBgm 0x132FF5C`, set by `sub_886C70` on
play and reset to -1 by `sub_885E80` when that track stops or unloads, and
`g_ffxBgmVolumeByTrack 0x13300E0`. core:460 returns the first, so it is step deterministic. [V]

---

## 6. FmodVoice

0x90 bytes at `FfxFmod+16`, ctor 0x70A1A0, dtor 0x70A330. One voice line at a time, plus the
battle voice group and the shout player. `VOICE_TIMING.md` section 3 has the start, end and busy
logic, which is not repeated. The layout, as far as recovered: +0 map, +4 US voice flag, +8
language, +12/+24 per map group and project arrays (0x54 each), +16 group, +20 the line's
`Event*`, +28/+32 battle voice group and project, +36 VoiceIDMapper blob, +40 VoiceFevMapper blob,
+44..+60 fade, **+64 playing**, **+68 start pending**, +72 previous +64, +80 volume, +84 paused,
+88 deferred start, +92..+100 prepare retry, +104 `FMOD_EVENT_INFO` buffer (0x34), +108
`lengthms`, +112 start time in us, +120 elapsed, +128 muted, +132 `FmodShout*`, +136 category
volume. [V]

New here:

- **`FmodVoice__start` registers the callback on +64.** At 0x70B60C:
  `lea eax, [esi+40h]` / `push eax` / `push offset sub_707A50` / `push dword ptr [esi+14h]` /
  `call FMOD::Event::setCallback`. `FmodVoice__startDeferred` does the same at 0x70B22A..0x70B236
  with `edi`. So +64 is cleared by the FMOD event end as well as by the `lengthms` check in
  `FmodVoice__checkLineElapsed`, whichever comes first. Both are on the FMOD thread's clock. [V]
- `FmodMusic__initList 0x70AB90` is a FmodVoice method. It is called with `this = FfxFmod+16`, it
  writes the two mapper blobs to +36 and +40, and `FmodVoice__dtor` frees them. Only its log string
  says FmodMusic, a source copy and paste. Renamed `FmodVoice__initList`. [V]
- `FmodVoice__setMuted 0x70B330` (command 0 type 2) sets +128 and the line's volume to 0 or +80.
  The event keeps playing. [V]
- `FmodVoice__setCategoryVolume 0x70B2B0` sets +136 and passes it on to the shout player. [V]
- `FmodVoice__playShout 0x70B430` is command 6. [V]
- `g_ffxFmodVoiceBusyDisabled 0xCEC184` and `g_ffxFmodMusicBlocked 0xCEC174`: `FmodVoice__dataChange`
  sets the first on maps 393..398 and then sets the second whenever the first is 1.
  `FmodVoice__resetForMap` clears both on any other map. The second refuses `FmodMusic__loadTrack`,
  `FmodMusic__loadBattleTrack` and `FmodShout__play`, so on those six maps no BGM loads and no
  shout plays. [V]

---

## 7. FmodShout

0x10 bytes at `FmodVoice+132`, ctor 0x70E290, dtor 0x70E2B0, `FmodShout__initData 0x70E2D0`.
Battle shouts are plain FMOD sounds, not events: `Voice/US|JP/ffx_*_voice_btl_iop_bank00.fsb`
through `createSound`, `getSubSound` by name, `playSound`. FMOD thread. [V]

| address | name | what it does |
| --- | --- | --- |
| 0x70E400 | `FmodShout__setVolume` | `Channel::setVolume` |
| 0x70E450 | `FmodShout__release` | |
| 0x70E480 | `FmodShout__play` | refused while `g_ffxFmodMusicBlocked` or muted |

Step side, `FFX_Shout_Play 0x8875D0` (Save_058, `sub_7A0820`) posts command 6 and then
`FFX_Shout_PostIopCommand 0x81EE20` writes the old IOP command 55 into the dead PS2 buffer and sets
`g_ffxShoutSkipped 0x12F4E94` ("Virtuos: Shout sound skipped!!!"). `FFX_Voice_PollFmodAndShoutBusy
0x8873F0` reads it once through `FFX_Shout_ConsumeSkippedFlag 0x81EDF0`, which clears it. So a shout
holds the voice busy flag for exactly one recompute, decided on the step thread. The shout's real
length never reaches the step. [V]

---

## 8. FmodSfx

0xD4 bytes at `FfxFmod+20`, ctor 0x70BFC0. Sound effects: per map and resident FMOD projects, a
map from sequence id to the list of live `SfxEvent`s, and one battle streaming slot. FMOD thread,
with a mutex at +204 that the inline command 63 also takes. [V]

| offset | field |
| --- | --- |
| +0 | byte, paused |
| +2 | byte, US flag |
| +4 | map of resident projects |
| +52 | map of per map projects |
| +100 | map sequence id -> `SfxEvent` list |
| +148 | map of projects waiting to unload |
| +196 | battle streaming project, `SFX/US\|JP/9999.fev` |
| +200 | battle streaming `SfxEvent` |
| +204 | mutex |
| +208 | category volume |

| address | name | what it does |
| --- | --- | --- |
| 0x70C7D0 | `FmodSfx__canStart` | refuses a second instance of a looping sequence that is still active |
| 0x70C8F0 | `FmodSfx__start` | finds the sequence in a loaded project, makes an `SfxEvent`, at most 5 instances, 30 for sequence 114 |
| 0x70CC30 | `FmodSfx__delayStart` | `if (*(_BYTE *)this == 0 && FmodSfx__canStart(...))` start, then `SfxEvent__delayStart` |
| 0x70CCD0 | `FmodSfx__playIgnorePause` | start without the +0 check, the menu SE path |
| 0x70CE90 | `FmodSfx__isSequencePlaying` | under +204, 1 when any instance has an entry with +4 set |
| 0x70CF10 | `FmodSfx__loadProject` | |
| 0x70D0C0 | `FmodSfx__setPaused` | +0 and `SfxEvent__setPaused` on every instance |
| 0x70D190 | `FmodSfx__playBattleStreaming` | |
| 0x70D500 | `FmodSfx__setCategoryVolume` | |
| 0x70D640, 0x70D690, 0x70D6E0, 0x70D730 | `FmodSfx__setPan`, `fadePan`, `setVolume`, `fadeVolume` | per sequence |
| 0x70D780 | `FmodSfx__releaseAll` | |
| 0x70D890 | `FmodSfx__play` | `if (*(_BYTE *)this == 0 && FmodSfx__canStart(this, id))` start then `SfxEvent__start`. A paused start is dropped |
| 0x70D8F0 | `FmodSfx__stopSequence` | |
| 0x70D930 | `FmodSfx__stopList` | ids above 127 only, -1 ends the list, a list that starts with -1 means all |
| 0x70D9E0 | `FmodSfx__stopOneShots` | every non looping instance and the battle streaming event |
| 0x70DA90 | `FmodSfx__stopAll` | |
| 0x70DB90 | `maybe_FmodSfx__stopMapSequence` | stops only when id > 127 |
| 0x70DBB0 | `FmodSfx__unloadProjectOf` | |
| 0x70DD10 | `FmodSfx__unloadProjects` | busy projects wait on +148 |
| 0x70DEC0 | `FmodSfx__tick` | dt in wall seconds: delayed starts, fades, reaps finished instances, unloads waiting projects |

Step side users go through the `FFX_Se_*` shim at 0x81CF40..0x81EE20 and the id decoding wrappers
`FFX_Snd_*ById` at 0x886DF0..0x886FF0. The busiest is `FFX_Snd_PlaySeDefault 0x886BA0` with 99
call sites (menus, BtlOd minigames, the battle menu), which skips the SE in battle during booster
auto battle. Every start bumps `g_ffxSeStartSerial 0x12F4E7C` on the step thread and returns it. [V]

---

## 9. SfxEvent

0x28 bytes, one per started sequence, ctor 0x70E710, dtor 0x70E850. FMOD thread. [V]

| offset | field |
| --- | --- |
| +0 | entry count |
| +4 | capacity |
| +8 | entries, 8 bytes `{FMOD::Event*, byte playing}` |
| +12 | delay in seconds |
| +16, +20 | start volume, start pan |
| +24, +28 | volume fade, pan fade (allocated) |
| +32 | volume |
| +36 | category volume |

| address | name | what it does |
| --- | --- | --- |
| 0x70EA20 | `SfxEvent__normalize127` | x / 127 clamped to 0..1 |
| 0x70EAC0 | `SfxEvent__delayStart` | +12 = frames / 30 |
| 0x70EC40 | `SfxEvent__isLooping` | `getLoopCount == -1` |
| 0x70ED30 | `SfxEvent__isPlaying` | any entry +4 |
| 0x70EE90 | `SfxEvent__setCategoryVolume` | |
| 0x70EEB0, 0x70EF80 | `SfxEvent__setPan`, `setPan127` | |
| 0x70F000 | `SfxEvent__fadePan` | |
| 0x70F100 | `SfxEvent__setPaused` | |
| 0x70F140, 0x70F1A0 | `SfxEvent__setVolume`, `setVolume127` | |
| 0x70F210 | `SfxEvent__fadeVolume` | |
| 0x70F310 | `SfxEvent__start` | for each entry: `entry+4 = 1`, `setCallback(ClearFlag, &entry+4)`, start, then volume and pan |
| 0x70F490 | `SfxEvent__stop` | |
| 0x70F5C0 | `SfxEvent__tick` | fades and the delayed start |

---

## 10. SfxProject

0xC bytes, one FMOD event project, ctor 0x70F760, which loads the `.fev` and two tables,
`%s_common.txt` (`SfxProject__loadCommonTable 0x70FF50`) and `%s_loop.txt`
(`SfxProject__loadLoopTable 0x710780`). `SfxProject__unload 0x70F930`, `SfxProject__isLoaded
0x70FB20` (`EventProject::getInfo`), `SfxProject__findSequence 0x70FA80`,
`maybe_SfxProject__getEvent 0x70FCD0` (an EventGroup vtable +36 call). FMOD thread. [V]

---

## 11. The booster path

`g_boosterSpeedIndex 0xCE82B4` has 9 xrefs, all in `FFX_Booster_*` and `FFX_Time_ScaleDtForBooster`.
None is in sound code. `FFX_Booster_IsSpeedUpActive` has one caller, `FFX_StepPacing_AllowCatchUp`.
The sound side only learns "fast forward on or off" through `FFX_Snd_SetFastForward 0x821C60`, which
`FFX_Booster_Enable`, `FFX_Booster_Disable` and `FFX_Time_ScaleDtForBooster` call: [V]

```
if (latch == 0 && on)  { latch = 1  -> apply }
if (latch == 1 && !on) { latch = 0  -> apply }
apply: sub_67A4C0();          // command 57, stop every one shot SFX
       sub_67A380(on);        // command 58, pause or unpause all SFX
       return sub_67A570(on, 2); // command 0 type 2, voice muted or unmuted
```

What follows, all [V] from the handlers above:

- **Music** is not touched. It plays at 1x while the step runs faster, and its fades run on FMOD
  ticks.
- **Voice** is muted, not stopped and not sped up. The line still ends on its wall clock or its
  callback, so it holds more steps while fast forward is on. That confirms the [I] row in
  `VOICE_TIMING.md` 4.3 from the audio side.
- **SFX** one shots are stopped and new starts are dropped for the whole fast forward, so every
  "is this SE playing" reads 0, apart from the short window where command 63's ring scan still
  finds the queued start.
- **Nothing is pitched.** The FMOD imports have no `setPitch`, `setFrequency` or event parameter
  call. The event imports are start, stop, getState, setCallback, setVolume, getVolume, setPaused,
  getPaused, getInfo and getChannelGroup. core:396 and 397 reach `SqsSePitchDirect` and
  `SqsSePitchTranslate`, stubs that print "not implemented", and both rows are dead.
- **Nothing feeds back into the step count.** No audio field reaches the pacer. Audio holds steps
  only through the waits in section 13.

The pause overlay (`FFX_Snd_SetPausedForOverlay 0x821CC0`) sends 8 and 58 only, so voice and SFX
pause while music plays on. The EscMenu does the same through `sub_68F940` and restores in
`sub_68FF60` and `FFX_EscMenu_OnResume`. [V]

---

## 12. What the simulation can see

Simulation observable means a script or step side branch reads the value back.

| state | read by | source of truth | mark |
| --- | --- | --- | --- |
| voice playing, `FmodVoice+64 \| +68` | core:216, 217, btl:103, 104, 169, `FFX_Bgm_Load` | FMOD thread: `lengthms` wall clock **or the event callback** | [V], `VOICE_TIMING.md` |
| music playing, `FmodMusic+32 \| track[cur].+32` | core:443 (4 uses: djyt0100, djyt0200, mihn0100, mihn0200) | FMOD callback, FMOD thread stop, unload and fade end, the step side pending mark | [V] [D] |
| battle track 16 loading | `FFX_Btl_BootTaskMusic` state 7 | FMOD `getState`, read cross thread with a 10 ms budget | [V] |
| SE sequence playing | core:403 (20 uses), core:416 (7 uses), Save_062 (dead), `FFX_Btl_TickSeFollowList` from `FFX_Btl_MainStep` 0x790C83, `sub_7FFC60`, the four `FFX_BtlOd_SetPhase*` and `FFX_BtlOd_ResetSharedTimer` | FMOD callback on `entry+4`, reaping in `FmodSfx__tick`, the pause drop, the ring scan | [V] [D] |
| SE paused, voice paused | `sub_68F940` (EscMenu) | FmodSfx+0, FmodVoice+84 | [V], menu only |
| playing BGM per the shim | core:460 (95 uses) | `g_ffxSndPlayingBgm`, step side | [V] [D], deterministic |
| voice and shout busy bits | the voice syscalls | step side recompute plus command 17 | [V] |
| `g_ffxSeStartSerial` | returned by every SE start | step side counter, but which branch bumps it depends on "SE playing" | [V] |

Presentation only: every volume, pan and fade (commands 13, 14, 34, 35, 59 to 62, 66 to 69), the
mutes (0), pause (8, 58), the soundtrack choice as far as playback goes (46), loads and unloads,
shouts, movie audio volume. [V]

How core:403 and core:416 use the answer: playing, they ramp the instance's volume over 12 frames
(`FFX_Snd_FadeSeVolumeById(id, 12, 63 or 127)`) unless the script's table flag 0x40 is set. Not
playing, they start it. Both return the id either way. So the branch changes which FMOD command
goes out and whether the step side serial moves, not the script's stack. [V]

---

## 13. Every place an audio clock or callback decides something the step sees

### 13.1 The callback

```
0x707A50  int __stdcall FfxFmod_EventCallback_ClearFlag(FMOD_EVENT *e, int type, void *p1, void *p2, _DWORD *userdata)
          if (type == 2 || type == 4) *userdata = 0;
          return 0;
```

Registered at 0x70B618 and 0x70B236 (voice, `&FmodVoice+64`), 0x709829, 0x7098AB and 0x7092FE
(music, `&track+32`), 0x70F39A (SFX, `&entry+4`). `FmodMusic__stop`, `FmodMusic__stopAll` and
`SfxEvent__dtor` clear it again with a null callback. It runs on whatever thread FMOD calls back
on, which for FMOD Ex event callbacks is the thread inside `EventSystem::update`, so T5. [V] for the
registrations, [I] for the thread and for types 2 and 4 being the sound end and event finished
callbacks of FMOD Ex 4.x.

### 13.2 The list

| # | where the step reads it | what decides it | effect |
| --- | --- | --- | --- |
| A1 | voice syscalls, see `VOICE_TIMING.md` | `lengthms` wall clock or the callback on +64 | which step a voiced line ends on |
| A2 | core:443 loop `syscall core:443, pushimm 0, ne, jz, WaitFrames(1), jmp` in 4 scripts | the music event's end callback, or a fade out's end on FMOD ticks | which step those scripts continue on |
| A3 | `FFX_Btl_BootTaskMusic` state 7 `if (FFX_FmodCmd_IsBattleBgmLoading() != 0) stay` | FMOD's async load of track 16, plus FMOD thread scheduling (a 10 ms timeout reads "loading") | which step battle start leaves phase 4 (`BATTLE_START_AND_END.md`) |
| A4 | core:403, core:416, `FFX_Btl_TickSeFollowList`, `sub_7FFC60`, BtlOd | SFX end callbacks, `FmodSfx__tick` reaping, starts dropped while paused | which command goes out, whether a follow list entry is dropped, the start serial |
| A5 | `FFX_Bgm_Load 0x886960` (core:258, 392, `FFX_Atel_LoadEventBgm`, `FFX_BtlResult_StepStateMachine`) | spins `while (qword_132FF14) { FFX_Voice_RecomputeBusyFlags(); FFX_Voice_RequestRefresh(0); }` at 0x886978..0x8869AB, so it waits on A1 | holds the step thread inside one step, it does not add steps |
| A6 | the voice and SFX pause | `FmodVoice__checkLineElapsed` re-stamps the line start while paused, SFX starts are dropped | longer lines after a pause, lost SEs |

---

## 14. Co-op and recreation hazards

1. **A2, the music end wait.** Four scripts wait for a jingle to end on FMOD's clock. Two peers can
   leave the loop on different steps. The arranged and PS2 soundtracks are different recordings, so
   a peer with the other soundtrack setting (command 46, a save config bit) has a different length.
   [V] for the wait, [D] that the lengths differ: both projects play the same event index per track,
   and the subsounds behind it are different lengths, for example track 13 is 151,579 ms arranged
   and 37,442 ms PS2, track 16 is 102,661 ms and 201,906 ms (`fev.py bgm`, `fev.py bgm --ps2`,
   `SOUND_FORMATS.md`).
2. **A2 also has a stale read.** Only command 39 marks the play pending on the step thread. Plays
   through 27 (core:487) and 33 (core:380, 381) do not, so a core:443 on the next step can still read
   the previous track's flag until T5 wakes. [V] for the paths, [I] that any script does this.
3. **A3, the battle music wait.** Disk speed, FMOD's loader and thread timing decide which step battle
   start moves on. This one sits in front of every battle. [V]
4. **A4, SE playing.** Fast forward and the pause overlay make every SE read "not playing", and
   normal play makes it depend on FMOD's clock. Today the branch only changes audio commands and the
   start serial, but anything that later keys off the follow list or BtlOd's SE state inherits it.
   [V] for the branch, [I] that nothing simulation side consumes it beyond that.
5. **A5, the spin.** A BGM load issued while a voice line plays freezes the step thread until the
   line ends on the wall clock. It does not change the step count by itself, but it is a long frame,
   and what the pacer does after a long frame is the pacer's business (`MAIN_LOOP.md`). It also
   trips any hang watchdog that measures frame time. [V] for the loop, [I] for how often it happens.
6. **Booster on one peer only.** Fast forward changes the SE answers (all 0) and the voice wall
   clock relation (more steps per line), and it is decided per machine. A lockstep layer has to
   either drive the booster from the host or treat its audio side as local. [V] mechanics.
7. **Cross thread writes with no lock.** `FfxFmod_MarkBgmPlayPending` writes FmodMusic+24 and +32
   from the step thread while T5 may be inside `FmodMusic__play`. The inline commands 17, 18, 19, 22
   and 25 read or write T5 state with no lock. A kit hook that calls these from a third thread adds a
   third party to that race. [V]
8. **Language and region.** `FmodSfx+2` and `FmodVoice+4` pick the US or JP banks. Different SFX banks can have different lengths, which reaches A4. [V] for the switch, [D]
   for the lengths: of 6,733 sequence ids in the same project number in both languages, 16 resolve
   to subsounds of different length, and 168 more exist in one language only (`SOUND_FORMATS.md`
   section 7).
9. **For the recreation.** Keep FMOD (or its replacement) on its own thread and its own clock, and
   never let the step read it. Each of A1 to A4 becomes a step countdown seeded on the step the
   command was issued: voice per `VOICE_TIMING.md` section 7, music end from a per soundtrack length
   table in steps, battle track ready as a step side "loaded" flag the loader sets in step order (or
   no wait at all), and SE playing from a per sequence length table plus "looping never ends". A5
   becomes a normal wait or goes away. The volume, pan, pause and mute paths can stay exactly as
   they are, presentation only.

---

## 15. Names

`tools/apply_sound_names.py` proposes **357 names**: 336 in `NAMES` (placeholders and positional
`<Class>__f<hex>` methods), 6 in `RENAMES`, 15 in `GLOBALS`. A dry run against the IDB copy
renamed nothing and reported 0 skips, so none of the new names already exists. The renames:

| address | was | now | why |
| --- | --- | --- | --- |
| 0x70AB90 | `FmodMusic__initList` | `FmodVoice__initList` | section 6 |
| 0x679FE0 | `maybe_FFX_Config_SetSlot46FromSaveBit` | `FFX_FmodCmd_SetPs2Soundtrack` | slot 46 is FMOD command 46 |
| 0x859DE0 | `FFX_AtelSys_Core_443_CallSoundCmd67A150_resi` | `FFX_AtelSys_Core_443_IsBgmPlaying_resi` | command 22 |
| 0x85E400 | `FFX_AtelSys_Core_460_GetSoundWord132FF5C_resi` | `FFX_AtelSys_Core_460_GetPlayingBgm_resi` | `g_ffxSndPlayingBgm` |
| 0x85F240 | `FFX_AtelSys_Core_399_FadeSoundLevel134_resi` | `FFX_AtelSys_Core_399_FadeVoiceVolume_resi` | command 13 |
| 0x859C40 | `FFX_AtelSys_Core_359_ClearBgmCue_resi` | `FFX_AtelSys_Core_359_HoldCurrentBgm_resi` | command 25 sets the hold, it clears nothing |
| 0xCE8458 (global) | `ArgList` | `g_ffxFmodCmdRing` | a parameter name that leaked onto the ring |

---

## 16. Worth checking, and what is still open

The claims that carry the most weight:

1. The callback on voice +64: 0x70B60C `lea eax, [esi+40h]` then `call FMOD::Event::setCallback` at
   0x70B618, and 0x707A50 `if (a2 == 2 || a2 == 4) *a5 = 0`. A runtime log of +64 against the
   elapsed time would show which of the two paths ends a line first.
2. Only command 20 crosses threads: the `switch` in 0x6FA500 and the callers of the sync wrappers
   (`FfxFmod_RunCommandSync0` from 0x67A240, 0x67A330, 0x67A6C0, `RunCommandSync1` from 0x67A150 and
   0x67A340, plus `FFX_FmodCmd_QueryVoicePlaying`).
3. Fast forward does not speed up audio: 0x821C60 and the import list.
4. The battle music wait: 0x709DB6 `cmp [ebp+var_4], 2` and `FFX_Btl_BootTaskMusic` state 7.

Open:

- Which FMOD Ex callback types 2 and 4 are. If 2 is not "sound definition end" the voice and SFX
  paths behave the same, but music could clear on a different edge. Check against the FMOD Ex 4.x
  `fmod_event.h` the game shipped with.
- Whether FMOD keeps a zero volume event alive to its natural end (it should, nothing virtualises
  events here).
- What the `sub_7A0A30` return feeds in btl:108 and the four battle callers.
- Sequence ids above 127 being the per map ones is inferred from `stopList` and `stopMapSequence`.
  The data agrees as far as it goes: no map project's side tables use an id of 127 or less, while
  the resident projects use both ranges (484 rows at or below 127, 2,196 above). [D]

Settled since: `FmodMusic__initData_2 0x708C00` is the outer call. It checks the event system and
then calls `FmodMusic__initData 0x708D40` twice, with `Music/ffx_music.fev` (0x708C2B) and
`Music_PS2/ffx_music_PS2.fev` (0x708C3E). `initData` loads one project and takes its `"music"`
group through `EventProject` vtable +8 (0x708E29..0x708E2F). [V]
