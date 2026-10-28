# Voice timing: SyncData, the voice syscalls, and how a line's length reaches the step

PROGRESS 1.9.4 of Project Zanarkand. Written against `CUTSCENE.md` section 2, and it says where it
agrees, extends or corrects that section (section 1 below is the scorecard).

All addresses are VAs (RVA + 0x400000). Read with idalib on a scratch copy of the IDB, nothing renamed
and nothing saved. Data was read out of the install's `FFX_Data.vbf` with `tools/vbf.py`, read only,
into the scratchpad, and none of it is in either repo. Script facts come from `tools/ebp.py dis`.

Marks: **[V]** read in the binary, **[D]** read in the shipped data, **[I]** inferred.

Related: `CUTSCENE.md` 2 (pacing mode 1), `MAIN_LOOP.md` (the step loop and `g_ffxPendingSteps`),
`THREADS_AND_CLOCKS.md` T5 and C14 (the FMOD thread), `ATEL_CORE_LIBRARY.md` 5.1, `ATEL_BTL_LIBRARY.md`
5.3 (battle voice), `COOP_DESIGN.md` (the pacing and voice notes).

---

## Summary, read this first

1. **SyncData is three tiny binary tables plus one timing track per voiced scene, and only three scenes
   have one.** The `.txt` files are binary, magic `SL\0\0`. `SyncList` names 3 maps, `SyncDataList` has
   3 track keys, `SyncVoiceList` has 29 per-line rows, and there are 3 track files of 3474, 3673 and
   1300 steps (138.3 s, 131.1 s and 65.1 s). The directory is the constant `"PS3"`, not a language,
   so the data is the same for every text and voice language. [V] [D]
2. **A track record is one simulation step of a recorded run:** `{float dueSeconds, float
   stepSeconds, int ticks60}`. `dueSeconds` is cumulative, `stepSeconds` is that step's measured
   length (it equals the difference of consecutive due times to 1.5e-5 s), and `ticks60` is how many
   60 Hz ticks the step advances, 2 for a 30 fps frame and 3 for a 20 fps frame. The scenes play back
   the original console's frame cadence, slowdowns included. [D] for the layout, [I] for "recorded".
3. **Syscalls 213 to 217 are one voiced line.** 213 VoiceStandby queues the load, 214 VoiceStart
   starts it, 216 VoiceStatus and 217 VoiceSync wait for it to end, 215 cuts it short when the player
   presses Circle and no timing track is running. Every countdown inside them counts **ATEL steps**
   (one per sub step, step thread). The open-ended waits read **one FMOD flag, `FmodVoice+64 | +68`**,
   which the FMOD thread clears when **wall-clock time since `Event::start` exceeds the event's
   `lengthms`**, or earlier when **FMOD's own event callback** reports the event ended (the start
   registers `sub_707A50` with `&FmodVoice+64` as its userdata, `SOUND_CLASSES.md` section 6).
   That answers `THREADS_AND_CLOCKS.md`'s open item C14. [V]
4. **The line's length never reaches the step rate.** Outside the three tracked scenes the step rate
   is the normal accumulator and a voiced line only holds the script for however many steps the FMOD
   wall clock takes. Inside a tracked scene the step rate comes from the recorded track, which was
   authored to match the audio, and the line still holds the script through FMOD unless a
   `SyncVoiceList` kind 3 row replaces that wait with a fixed step count (26 lines do). [V] [D]
5. **Lockstep verdict.** 214 is step deterministic on PC. 213 is not (it waits for the local frame's
   catch-up burst to end and for core:515 model loads). 216, 217 and the battle waits are not (FMOD
   wall clock, voice language, missing files, pauses). 215 is input plus the pacing mode. The kind 1,
   2 and 3 countdowns are. Pacing mode 1 is deterministic in content only if both machines put a frame
   boundary on the same steps at the start and end of the track, and **a lockstep layer that writes
   `g_ffxPendingSteps` stops the track from advancing at all.** [V] for the mechanics, [I] for the
   consequences.
6. **For the recreation:** drive every voice wait from a step countdown seeded at the start step, from
   the kind 3 row when there is one and otherwise from a per-language length table built from the
   user's own FMOD banks, consume the timing track one entry per simulation step from the step after
   it is selected, and keep audio and the track's due times as presentation only. Section 7.

---

## 1. Scorecard against CUTSCENE.md section 2

| CUTSCENE.md says | verdict | detail |
| --- | --- | --- |
| 2.1 the only writer of `g_ffxPacingMode` is `FFX_StepPacing_LoadTimingTrack 0x821D60` | **agrees** [V] | re-checked, two stores at 0x821DA5 and 0x821E04 |
| 2.2 the triple is `(dueTime, tolerance, step88)` with terminator `(1.0, 1.0, 999)` | **agrees, extends** | the "tolerance" is the recorded length of the step, used as the catch-up threshold (section 2.4). `step88` is a 60 Hz tick count, `<< 7` makes 2 ticks 256, a 1.0 scale [V] [D] |
| 2.3 path `/FFX_Data/GameData/<lang>Data/SyncData/SyncList.txt` | **corrects** | `%s` is the string `"PS3"` written by `sprintf` in `FFX_SyncData__init 0x6FA8D0`. The VBF path is `ffx_data/gamedata/ps3data/syncdata/`. Not language dependent [V] [D] |
| 2.3 the other tables are read by 213, 214 and 215 | **corrects** | `FFX_SyncData_LookupVoiceSync` has three call sites: 213 start (kind 1), 214 start (kind 2), 214 getter 0x85DE80 (kind 3). 215 never reads the table. Kind 3 feeds **217**, not 216 [V] |
| 2.3 `word_112D67C` is probably story progress | **agrees, now proved enough** | it is `g_ffxStoryProgress` in the IDB, and the three track keys are 1530, 1885 and 2815, which the scenes' own scripts push (mcyt0600 pushes 1530 three times) [V] [D] |
| 2.3 the manager layout | **extends** | full layout in 2.2 below |
| 2.4 the content of step N is identical on both machines | **qualified** | true only when both machines start and end the track on the same step, which depends on where each one's frame boundaries fall. Section 6.2 [V] [I] |
| 2.4 lockstep owns the step count through `g_ffxPendingSteps`, so the track advances the agreed entries | **corrects** | `LoadTimingTrack` only runs when `g_ffxPendingSteps == 0` at `FFX_StepPacing` entry. A value written from outside skips it, so the track does not advance on forced steps and the step scale reads stale or negative indices. Section 6.3 [V] |
| 2.6 core:215 "falls back to a manual advance" | **corrects** | 215 calls `sub_887780`, which clears the prepared flag and queues FMOD command 10, `FmodVoice` stop. It **stops the voice**. Scripts call it from the 216 loop on a Circle press, so it is "skip this line", and mode 1 disables the skip [V] [D] |
| 2.6 debug prints are gated on `dword_12FB80C` | **agrees** | zero at runtime, nothing else reads it on this path [V] |

Corrections to the neighbours, found on the way:

- `ATEL_CORE_LIBRARY.md` 5.1 says 214's wait is "the audio sample position (`sub_885D20`, 48 kHz)".
  **Wrong.** `sub_885D20` turns `dword_132FF28`, a PS2 voice clip size, into a frame count, it returns
  0 unless the PAL flag `byte_1330831` is set, and `dword_132FF28` is only written from dead core:470.
  It is 0 in retail. Section 3.2. [V]
- The same section says 213 "also waits for the disc idle (`sub_76F060`)". **Wrong.** `sub_76F060` is
  `xor eax, eax / retn`. The real extra hold is `dword_1326B94`, the "a core:515 model load is in
  flight" set. Section 3.1. [V]
- The 216 row says "1 while the kind 3 countdown `dword_1325AE0` runs". **Wrong.** `dword_1325AE0` is
  never written nonzero anywhere in the binary, so that countdown is dead and 216 always reads FMOD.
  Kind 3 goes to `dword_1325AE8`, which 217 counts. [V]
- `THREADS_AND_CLOCKS.md` row C16 calls the mode 1 track "the FMV timing track". It is the voice
  scene timing track. And T5 says "213, 214 and 215 poll the voice object". The pollers are 216, 217
  and btl:103, 104, 169. [V]

---

## 2. The SyncData tables

### 2.1 Where they come from and when they load

```
boot
  FFXApplication__initApplication -> sub_67A960 -> FFX_SyncData__ctor -> FFX_SyncData__init 0x6FA8D0
      eight 0x200 byte arrays (128 slots each), a 0x190 byte SyncList array (100 maps), +0 = "PS3",
      +0x20 (load finished) = 1
  FFX_MainInit -> FFX_SyncData_LoadSyncList 0x67A950 -> FFX_SyncData__loadSyncList 0x6FB210
      reads /FFX_Data/GameData/PS3Data/SyncData/SyncList.txt synchronously, keeps the map ids

every map load
  maybe_FFX_Scene_Init 0x88E31B -> FFX_SyncData_RequestForMap 0x67A8E0 -> __requestForMap 0x6FB080
      only for map 1..500 and a map different from +0x0C
      +0x0C = map, +0x1C = (map is in SyncList), +0x08 track cursor = NULL
      if the map has data: sleep in 5 ms steps until the previous load finished (+0x20),
        clear the tables (0x6FAEE0), +0x20 = 0, enqueue job 0x6FB130 on the AsyncLoadManager
  AsyncLoadManager thread (THREADS_AND_CLOCKS T7)
      0x6FB130 -> FFX_SyncData__loadMapData 0x6FAF90
        SyncDataList.txt (0x6FAA30)  keep rows for this map  -> +0x10 count, keys at +0x28..+0x38
        SyncVoiceList.txt (0x6FABA0) keep rows for this map  -> +0x14 count, +0x24, +0x2C, +0x3C
        one SyncData_%4.4d_%4.4d_%4.4d_%4.4d_%4.4d.txt per kept track key -> +0x04[i]
      +0x20 = 1
```

All reads go through `FFX_fiosUnifyFilename` and `FFX_SyncData__readWholeFile 0x6FACF0` (open, seek to
the end for the size, aligned alloc, read, close). [V]

Every reader of the per-map tables (`__selectTimingTrack`, `__lookupVoiceSync`,
`__readNextTimingEntry`) first sleeps in 5 ms steps on `+0x20`, but only when `+0x1C` says the map has
data. So on the three tracked maps the first call-actor opcode, voice syscall or pacing read after a
map change can stall the step thread on the disk. [V]

### 2.2 The manager, `g_ffxSyncDataMgr 0xCE9014`, 0x44 bytes

| offset | meaning |
| --- | --- |
| `+0x00` | `char*` platform directory, `"PS3"` |
| `+0x04` | `void*[128]` track blobs, parallel to the track key arrays |
| `+0x08` | the timing track cursor. Non-null means mode 1 is possible |
| `+0x0C` | current map id |
| `+0x10` | track key count for this map |
| `+0x14` | voice row count for this map |
| `+0x18` | SyncList count |
| `+0x1C` | this map is in SyncList |
| `+0x20` | the async load has finished (1 at rest) |
| `+0x24` | voice row: kind |
| `+0x28` | track key: story progress |
| `+0x2C` | voice row: voice id |
| `+0x30` `+0x34` `+0x38` | track key: entry, actor, level (the three call-actor operands) |
| `+0x3C` | voice row: value (steps) |
| `+0x40` | SyncList map ids |

[V] from the init, the two list parsers and the three readers.

### 2.3 The file formats and the shipped counts

All three list files start `53 4C 00 00` (`"SL"`) then a `u32` count. [V] [D]

| file | bytes | row | rows | content |
| --- | --- | --- | --- | --- |
| `SyncList.txt` | 20 | `u32 mapId` | 3 | maps 80, 280, 270 |
| `SyncDataList.txt` | 68 | `u32 map, story, entry, actor, level` | 3 | (80, 1530, 3, 20, 1), (280, 1885, 5, 20, 1), (270, 2815, 2, 17, 1) |
| `SyncVoiceList.txt` | 472 | `u32 kind, map, voiceId, steps` | 29 | map 80: 1 kind 2, 22 kind 3. Map 280: 1 kind 1, 4 kind 3, 1 kind 4 |
| `SyncData_0080_1530_0003_0020_0001.txt` | 41700 | 12 bytes | 3475 | 3474 steps + terminator, last due 138.27 s |
| `SyncData_0270_2815_0002_0017_0001.txt` | 44088 | 12 bytes | 3674 | 3673 steps + terminator, last due 131.09 s |
| `SyncData_0280_1885_0005_0020_0001.txt` | 15612 | 12 bytes | 1301 | 1300 steps + terminator, last due 65.06 s |

Maps resolve through the asset path table (`CHEAT_WARP.md` 1.1, entry `562 + 18 * map`) to
`mcyt0600`, `dome0600` and `azit0300`. [D]

The track key's three last fields are the call-actor operands. In each scene there is exactly the
matching `runscript0` (opcode 0x36): mcyt0600 at 0x9130 pushes `1, 20, 3`, dome0600 at 0x4809 pushes
`1, 17, 2`, azit0300 at 0x74CB pushes `1, 20, 5`. `FFX_Atel_CallActorScript0 0x867230` pops them as
entry (last pushed), actor, level, and calls `FFX_SyncData_SelectTimingTrack(g_ffxStoryProgress, entry,
actor, level)` before it even tries to start the thread, so the selection happens whether or not the
start succeeds. [V] [D]

`SyncVoiceList` kinds: `FFX_SyncData__lookupVoiceSync 0x6FB410` scans every row and keeps the **last**
match on `(voiceId, kind)` among the map's rows. The callers only ask for 1, 2 and 3, so the one kind 4
row is never read. Values are small step counts, kind 3 ranges 28 to 670. [V] [D]

Voice ids are `(line << 12) | ((c1 - 0x60) << 6) | (c2 - 0x60)`, a six digit line number and a two
letter speaker code, which is how `sub_887870` builds the PS2 name and how `VoiceIDMapper.txt` (8730
rows) spells them, for example `0x231C84CD` is line 143816 with code `sm`. [V] [D]

### 2.4 What a track record means

| field | content | evidence |
| --- | --- | --- |
| `float dueSeconds` | cumulative seconds since the track started. Strictly increasing | [D] |
| `float stepSeconds` | the length of this step. `due[i] - due[i-1] == stepSeconds[i]` to 1.5e-5 s in all three files | [D] |
| `int ticks60` | 60 Hz ticks this step advances. Histograms: map 80 {2: 2204, 3: 1236, 4..28: 34}, map 270 {2: 3166, 3: 501, 4..7: 6}, map 280 {2: 21, 3: 1267, 4..16: 12} | [D] |

`stepSeconds` tracks `ticks60 / 60` to about 0.01 s on average, but not exactly (worst 0.34 s, 0.014 s
and 0.11 s), so these are measured times, not computed ones. The average rates are 25.1, 28.0 and 20.0
steps per second. Map 280's scene runs almost entirely at 20 fps. The reading that fits is a capture of
the original console version's frame timing for three long voiced scenes, so the HD build's animation
plays at exactly the cadence the audio was mixed against, slowdowns included. [I]

How the engine uses the three fields, in `FFX_StepPacing 0x821E80` and
`FFX_StepPacing_LoadTimingTrack 0x821D60`:

```c
// every frame whose g_ffxPendingSteps is 0 on entry
g_ffxCatchUpExtraSteps = 0;
LoadTimingTrack();                       // reads entry e: NextTime = due(e) ms, Tolerance = dur(e) ms,
                                         // StepTicks[CatchUpExtraSteps] = ticks(e) << 7, mode = 1
if (mode == 1) {
    g_ffxPendingSteps = 1;
    if (first) { StartUs = now; DueTime = NextTime; first = 0; pause totals = 0; }
    else {
        elapsedMs = (now - pauseTotal - StartUs) / 1000;
        if (DueTime <= elapsedMs)                      // DueTime is the PREVIOUS entry's due time
            while (Tolerance < elapsedMs - DueTime && mode == 1) {
                ++g_ffxPendingSteps; ++g_ffxCatchUpExtraSteps;
                DueTime = NextTime; LoadTimingTrack(); // one more entry per extra step
            }
        else while (DueTime - elapsedMs >= 0.1) spin;   // ms, so it waits out the whole lead
        DueTime = NextTime;
    }
}
```

So **every scheduled step consumes exactly one entry**, and `FFX_MainStep_PublishStepScale 0x8206C0`
hands step `k` of the frame `StepTicks[k]` as its motion scale. The due times only decide how many
steps a frame gets and whether the frame spins. [V]

The spin threshold is 0.1 ms, not 100 ms. `dbl_B921F0` is 0.1000000014901161 and both sides of the
compare are milliseconds: the track's seconds are multiplied by 1000.0 (`dbl_B0DBA0`, the `fmul` at
0x821DF2..0x821DFD) and the clock's microseconds are divided by 1000.0 (the `fdiv` at 0x8220C1), then
`fcomp dbl_B921F0` at 0x822052..0x822069. A frame that is ahead of the previous entry's due time spins
until it is within 0.1 ms of it, so the pacer never runs ahead of the track by more than that. [V]

`g_ffxTimingTrackStepTicks 0x12FB828` is `int[30]` and the store at 0x821E0E
(`mov g_ffxTimingTrackStepTicks[eax*4], ecx`, eax = `g_ffxCatchUpExtraSteps`) has no bound. It runs
after `mov g_ffxPacingMode, 1` at 0x821E04, and index 30 is `g_ffxPacingMode 0x12FB8A0` itself. A
frame 30 entries behind therefore writes `ticks << 7` into the mode, the catch-up loop's mode test
stops it there with 31 steps queued, and those steps all take the clock scale because the mode is no
longer 1. The next frame start reads the next entry into index 0 and the mode is 1 again. Nothing
past index 30 is reachable. [V] for the code, [I] that a real frame ever falls that far behind

---

## 3. The voice pipeline on PC

The PS2 voice layer survives as a shell over FMOD. A voice is an FMOD event in one of 22 voice
projects per language, which `VoiceFevMapper.txt` (337 rows per language, US and JP) assigns to
maps, looked up by voice id through `VoiceIDMapper.txt`. Battle lines live in a 23rd project,
`ffx_<lang>_voice_btl.fev`, and `SOUND_FORMATS.md` section 6.3 has the routing. [V] [D]

| piece | thread | what it does | evidence |
| --- | --- | --- | --- |
| `sub_887570` prepare | step (T0) | `dword_132FF1C = 1` (prepared), FMOD command 16 with the id | [V] |
| `sub_887690` / `sub_887630` start | step | `dword_132FF10 = 1`, issued bits, `FmodVoice+68 = 1` **synchronously** (`sub_6FA6C0 -> sub_707530`), then FMOD command 15 | [V] |
| `sub_887780` stop | step | `dword_132FF1C = 0`, FMOD command 10 | [V] |
| `FmodVoice__f70ADD0` prepare | FMOD (T5) | VoiceIDMapper lookup, `getEvent`, `Event::getInfo`, `+108 = info+8`, which is `lengthms` in FMOD Ex's `FMOD_EVENT_INFO` | [V], the field name [I] |
| `FmodVoice__f70B5C0` start | FMOD | `Event::setCallback(sub_707A50, &+64)` (0x70B60C..0x70B618), `Event::start` and `+112 = FFX_Time_NowMicroseconds()` when ready, else `+88` deferred start, which registers the same callback at 0x70B22A..0x70B236. `+64 = 1` (unless `dword_CEC184`), `+68 = 0` | [V] |
| `sub_70B750`, every FMOD tick | FMOD | `elapsedMs = (now - +112) / 1000`. When `+64 == 1`, `+108 != 0` and `elapsedMs > +108`, release, which clears `+64`. While `+84` (paused) is 1 it re-stamps `+112 = now` | [V] |
| `FmodVoice__f70A4A0`, every tick | FMOD | an `Event::getState` failure also releases and clears `+64` | [V] |
| `sub_707A50`, FMOD event callback | FMOD (inside `EventSystem::update`) | `if (type == 2 \|\| type == 4) *userdata = 0`, so the event's own end clears `+64` too, whichever of this and the `lengthms` check comes first | [V], the callback types and thread [I] |
| `FFX_Voice_StepLoader 0x887B80` | step | from `FFX_Atel_StepOnce` (after the scripts, once per sub step) and from `FFX_Field_DrawEvTexSpritesAndStepVoice` (once per `FFX_MainStep`): shifts the issued bits, then `sub_887700` reads FMOD command 17 **inline** (`FfxFmod_RunCommandSync` runs 17 on the caller), which returns `FmodVoice+64 \| +68` | [V] |
| `sub_8864B0` voice busy | step | `dword_132FF10 \| lo(qword_132FF14)`, both rebuilt by the step loader from command 17 and the issued bits | [V] |

The FMOD thread ticks at 30 Hz and drains the command ring on each wake (`FmodManager__tick
0x6FA700`, `THREADS_AND_CLOCKS.md` T5). Command 15 does not kick it, but `+68` is set on the step thread
before the command is queued, so the busy flag cannot read 0 between the start and the FMOD thread
picking the command up. The issued bits additionally keep "busy" true for one recompute after any
voice command. [V]

So **"the line is over" is decided on the FMOD thread, on the wall clock, against the event's
`lengthms`, or by FMOD's end of event callback if that comes first**, and the step thread samples
it once per ATEL step. [V]

### 3.1 core:213 VoiceStandby (start 0x85CD00, poll 0x85CEC0, getter 0x85D360)

- start: `g_ffxAtelVoiceId (0x1325AF0) = id`, `0x1325AD0 = LookupVoiceSync(id, 1)`, armed if nonzero.
- poll, kind 1 armed: count `0x1325AD0` down, one per poll. At zero, return 0 while
  `g_ffxPendingSteps > 1`, else done.
- poll, not armed: `sub_76F060()` is a stub that returns 0, then return 0 while `g_ffxPendingSteps >
  1`, then return 0 while `dword_1326B94` (core:515 model loads in flight, set by `sub_86F460` from the
  515 poll, cleared by `sub_863650` from its getter) is nonzero, else done.
- getter: unless a voice is already prepared and not started (`dword_132FF1C`), `sub_887570(id)`.

`g_ffxPendingSteps > 1` is read after `FFX_MainStep` decremented it, so in a burst of N steps the poll
sees N-1, N-2, ... 0 and can only finish on the last two steps of the frame. [V]

### 3.2 core:214 VoiceStart (start 0x85D6B0, poll 0x85D9D0, getter 0x85DE80)

- start: `0x1325AD8 = LookupVoiceSync(voiceId, 2)`, armed if nonzero. The work word gets the wait:
  `ctx+0x214` when it is `>= 0` and the PAL flag is set, else `sub_885D20()`, and `ctx+0x214` is reset
  to -1. `ctx+0x214` is only ever set by dead core:590 and the PAL flag only by the debug
  `btlenv.txt` parser behind `FFX_IsDebugMode_dup` at 0x7826BE, so the word is 0 in retail. [V], the
  reachability of the parser [I]
- poll, kind 2 armed: count down, done at zero.
- poll, not armed: wait while bit 0x10000 of `dword_12F4E80` is set or `sub_76F060()`. `dword_12F4E80`
  is the receive buffer of an emulated IOP sound RPC whose bind and call functions are stubs
  (`sub_A44740` returns 0, `sub_A44770` returns -1, `SG_Dev_RemoteRead_STUB_ret0`), and nothing writes
  it, so neither holds on PC. Then count the work word down. In retail this poll finishes on its
  first pass. [V]
- getter 0x85DE80 (not a function in the IDB): `sub_887690(byte_1327135, byte_1327134)` **starts the
  audio**, then `0x1325AE8 = LookupVoiceSync(voiceId, 3)`, armed if nonzero.

`sub_885D20` itself: `(int)((((size * 2048 - 32) / 48000) * 16 / 28) * 2016 / 2048 * 4.97)` with `size
= dword_132FF28`. That is a PS2 ADPCM size to frames estimate. `dword_132FF28` is written only by
`sub_887790`, which only dead core:470 reaches. [V]

### 3.3 core:215 (getter 0x85E090)

`if (g_ffxPacingMode != 1) sub_887780();` and return 0. It stops the voice. The scripts use it in one
fixed shape, 43 times in dome0600 alone: [V] [D]

```
core:214 VoiceStart
loop:  if (!core:216) goto done
       if (core:143(0) == 1 && (core:70 & 0x20) && core:412) { core:215; goto done }  // text shown, Circle
       core:0 WaitFrames(1); goto loop
done:  core:217 VoiceSync
```

So a Circle press after the text is fully shown cuts the line, and in a timing tracked scene the press
does nothing and the line plays out. core:70 is the port 0 pressed mask. [V] [D]

### 3.4 core:216 VoiceStatus (getter 0x85E1D0)

Returns 1 while the `0x1325AE0` countdown runs, which never happens because nothing sets it, else
`sub_8864B0()`. So it is "the voice busy flag right now". [V]

### 3.5 core:217 VoiceSync (poll 0x85E4A0)

Kind 3 armed (from the 214 getter): count `0x1325AE8` down, done at zero. Not armed: done when
`sub_8864B0() == 0`. The poll returns 1, done without the continue bit. [V]

The plain line shape, 38 times in mcyt0600, is `213 id`, `core:465 WaitFramesSkippable(5)`, show the
window and wait for state 2, `214`, `217`, close, `core:132`. [D]

### 3.6 The single-slot state

`0x1325AD0..0x1325AFC` and `dword_132FF10..28` are one slot each for the whole game. None of them is
reset on a map change. A kind 3 countdown armed by a 214 whose 217 never runs is consumed by the next
217, whatever line that is. Any lockstep hash or join snapshot has to include them. [V] for the
writers, [I] for whether a script ever does that.

### 3.7 Summary of each wait

| wait | counts | clock | thread that moves it |
| --- | --- | --- | --- |
| 213 kind 1 countdown | polls | ATEL steps | step (T0) |
| 213 `g_ffxPendingSteps > 1` | steps left in this frame | presented frames, from the frame delta or the track's wall clock | step, from `FFX_StepPacing` |
| 213 `dword_1326B94` | outstanding core:515 loads | disk | AsyncLoadManager (T7) and the asset loader |
| 214 kind 2 countdown | polls | ATEL steps | step |
| 214 IOP status and PAL frames | nothing in retail | none | none |
| 215 | `g_ffxPacingMode`, after the script's own core:70 Circle test (215 itself reads no pad) | track consumption, input | step |
| 216, 217 unarmed | `FmodVoice+64 \| +68` | wall clock against `lengthms` or the end of event callback, 30 Hz quantised | FMOD (T5) |
| 217 kind 3 countdown | polls | ATEL steps | step |

---

## 4. How a line's length reaches the pacing

### 4.1 Mode 0, almost every voiced line

The step rate does not change. `FFX_StepPacing`'s accumulator keeps giving one step per frame at 29.97
Hz (more on a long frame), and the line holds the script at 216 and 217 until the FMOD thread says the
event's `lengthms` has passed since `Event::start`. A long line is simply more steps of waiting. [V]

### 4.2 Mode 1, the three tracked scenes

A tracked scene's step rate comes from the track, which does not know about lines at all. The pacer
keeps the simulation within about one step behind and 0.1 ms ahead of the recorded due times. The
audio plays on the wall clock and the steps are paced to the wall clock, so mouths and audio line up
the way they did on the original hardware. Inside those scenes the line waits are still FMOD driven
except where a kind 3 row fixes them (22 of mcyt0600's 38 lines, 4 of azit0300's 28, none of
dome0600's 67), plus one kind 2 start delay of 9 steps and one kind 1 standby delay of 2 steps. [V] [D]

`FFX_MainStep_PublishStepScale` also forces the scale to 256 whenever `FFX_Player__getSubStepCount()
!= 1`, so the booster's sub steps throw the track's per-step scale away. [V]

### 4.3 Missing, muted, other language, paused, fast forward, boot scenes

| case | what happens | mark |
| --- | --- | --- |
| voice file or event missing | `FmodVoice__f70ADD0` leaves `+20` null (`"voiceRead: mVoiceEvent not find!"`), start then writes `+64 = 0` and `+68 = 0`, and the line ends after the one recompute the issued bits hold. The scene runs at text speed | [V] |
| muted, voice volume 0 | volume goes through `Event::setVolume` only, and `+128 == 1` starts at volume 0. The event plays and `lengthms` still decides, so timing is unchanged | [V] for the path, [I] that FMOD keeps the event alive at volume 0 |
| other voice language | `FmodVoice+4` picks `Voice/US/` or `Voice/JP/`, set once at FMOD init by `sub_70B3F0` from the first dword of `sLanguageManager` (`dword_CDED58`): 1 and 2 give US, 0, 3, 4 give JP. A different bank means a different `lengthms`, so a different number of steps. The SyncData is the same for both | [V], the length difference [I] |
| paused audio (FMOD command 8, `FFX_Snd_SetPausedForOverlay`) | `sub_70B750` re-stamps the start while paused, so after unpausing the full `lengthms` has to pass again and the line holds the script longer than it sounded | [V] mechanics, [I] which overlays send 8 |
| fast forward (booster) | more steps per wall second, same wall time per line, so a line holds more steps. `FFX_Snd_SetFastForward 0x821C60` only mutes the voice (FMOD command 0 type 2), it does not stop or speed up the line, and nothing in FMOD is pitched (`SOUND_CLASSES.md` section 11) | [V] audio side, [I] the step count |
| maps 393..398 | `FmodVoice__dataChange` sets `dword_CEC184 = 1`, start then never sets `+64`, so voiced lines there never hold the script | [V] |
| PS2 voice table (`dword_23CBEC4`, asset kind 25) | still loaded by tklib init, read only by dead core:470 | [V] |

---

## 5. Battle voice

Battle has no SyncData path, `LookupVoiceSync` has only the three core callers. btl:100 and 101 wait on
the asset loader and the sound streamer (`sub_7B2C10`), btl:103 and 104 read `sub_7B2BE0 =
sub_8864B0() || sub_79D050()` (the same FMOD busy flag, or battle message window 7 not finished), and
btl:169 closes on the confirm press or once text phase 3 is reached and `sub_8864B0()` is 0. So battle
lines have exactly the field's FMOD dependence and no fixed-step escape hatch. `ATEL_BTL_LIBRARY.md` 5.3
already says this. [V]

---

## 6. Lockstep

### 6.1 Per wait

| wait | can two machines with the same inputs finish it on different steps? | why |
| --- | --- | --- |
| 213 kind 1 countdown | no | step counted from an agreed step |
| 213 `g_ffxPendingSteps > 1` | **yes** | it depends on how many steps the local machine packs into the current presented frame, which is the wall clock (mode 0 dt, mode 1 track lag) |
| 213 `dword_1326B94` | **yes** | a core:515 model load finishes at disk speed |
| 214 | no, on PC | kind 2 countdown or the first poll. The IOP word and the PAL frames are dead in retail |
| 214 getter, audio start | no | starts on the step the poll finishes |
| 215 | **yes**, twice | its trigger is a port 0 Circle press, which the field input relay has to carry, and its effect depends on `g_ffxPacingMode`, which is per-frame state (6.2) |
| 216 | **yes** | FMOD wall clock, 30 Hz quantised, plus language, missing files, pause re-stamps, and the drain latency of command 15 |
| 217 kind 3 | no | step counted |
| 217 unarmed | **yes** | as 216 |
| btl:103, 104, 169 | **yes** | as 216, and 169 also takes the local confirm |
| mode 1 step count per frame | yes, and harmless | it only moves presentation, as `CUTSCENE.md` 2.4 said |
| mode 1 content | **yes at the edges** | 6.2 |

The engine shows its own intent here: the 26 kind 3 rows exist exactly so that some lines in tracked
scenes end on a fixed step. [D] [I]

### 6.2 The track's edges depend on frame grouping

- **Start.** `SelectTimingTrack` sets the cursor while a script step runs. The mode is only re-derived
  at the next frame start (`g_ffxPendingSteps == 0`), so the steps left in the selecting frame run in
  mode 0 and the first track entry lands on the first step of the next frame. A machine that ran the
  selecting step as the last of its frame starts the track on the very next step. A machine that
  still had two catch-up steps queued starts it two steps later. The entry index of a given global
  step then differs by those two steps for the whole scene. [V] for the control flow, [I] for the size in practice, which
  is zero when both machines run one step per frame
- **End.** When the catch-up loop reads the terminator, `LoadTimingTrack` zeroes `StepTicks`, sets
  mode 0 and resets `g_ffxCatchUpExtraSteps`, but the steps already scheduled in that frame still run.
  They run in mode 0 with the clock scale instead of their entries' ticks, and 215 sees mode 0 on them.
  A machine that reached the terminator at a frame start runs those steps in mode 1. [V] [I]
- **Inside.** The 1:1 entry per step holds, so the content of the middle is identical once the start
  offset agrees. [V]

### 6.3 Writing `g_ffxPendingSteps` from outside breaks mode 1

`LoadTimingTrack` and the reset of `g_ffxCatchUpExtraSteps` both sit inside `if (g_ffxPendingSteps ==
0)` at `FFX_StepPacing` entry. A lockstep layer that writes N before `animate` (the
`ffx::RequestCatchUpSteps` primitive in `MAIN_LOOP.md`) therefore makes those N steps consume **no**
entries, and `PublishStepScale` reads `StepTicks[CatchUpExtraSteps - g_ffxPendingSteps]` with a stale
`CatchUpExtraSteps`, which goes negative after the first step and reads the words below `0x12FB828`
(`g_ffxTimingTrackStartUs` and its neighbours) as a motion scale. Both machines then disagree on which
entry belongs to which step, and on the scale. During mode 1 the mod must either leave
`g_ffxPendingSteps` alone or reproduce the track consumption itself. [V] for the code, [I] for the
visible effect.

---

## 7. What the recreation should do

The rule from `THREADS_AND_CLOCKS.md` holds: the step never reads an audio field. Concretely:

1. **A voice line is a step countdown.** At the step the 214 getter runs, compute `L` steps and store
   `doneStep = startStep + L` in the ATEL voice state. 216 returns `step < doneStep`, 217 finishes at
   `doneStep`, btl:103, 104 and 169 read the same state. `L` comes from, in order:
   - the `SyncVoiceList` kind 3 row, which is what the original does for those 26 lines,
   - a length table, voice id to `lengthms`, built at install time from the user's own US and JP FMOD
     banks (the FSB sample headers carry length and rate), converted with the nominal step rate and
     one extra step for the original's recompute hold: `L = ceil(lengthms * stepsPerSecond / 1000) + 1`,
   - 1 when the voice does not exist, which matches the original's behaviour for a missing file.

   The table is data the recreation derives, nothing is shipped. The session agrees on one voice
   language (the host's) so `L` is the same everywhere, or uses the US table for simulation and lets
   each peer hear its own language.
2. **215 becomes step ordered.** It reads the agreed "track active" flag (point 4) and, when it acts,
   sets `doneStep` to the next step and tells the audio side to stop. The Circle press is replicated
   input already under the field model.
3. **213 loses its frame-shape test and its disk test.** Drop `g_ffxPendingSteps > 1` (it only kept the
   standby from finishing inside a catch-up burst), keep the kind 1 countdown, and turn the core:515
   model-load wait into a deterministic load barrier, either synchronous or released by a host ordered
   command, the same treatment as the other disk waits in `ATEL_CORE_LIBRARY.md` 5.2.
4. **The timing track is consumed by the simulation, one entry per step, from the step after the
   selecting call-actor opcode.** The entry's `ticks60 << 7` is that step's scale, the step after the
   last entry is mode 0, and "track active" is part of the step state that 215 reads. The due times go
   to the presentation pacer, which decides how many steps to run per frame and spins when early,
   exactly like the original but without being able to change which entry a step gets.
5. **Audio is presentation.** Start, stop and pause are commands from the step to the audio thread.
   When a lockstep stall holds the simulation, pause the voice and feed the stall to the pacer as the
   original's `g_ffxTimingTrackPauseUsPending` does, so it does not burst afterwards.
6. **Fast forward.** Either scale `L` by the agreed booster factor so a line keeps its wall length, or
   accept that lines are cut at 2x and 4x, which players fast-forwarding a scene will not mind. It has
   to be a session decision because it changes `L`.

What it costs in lip sync: mouths are driven by step-timed motion [I], so at the nominal step rate a
step-counted line matches its audio to within one step (33 ms). When the simulation runs slower than
real time (a lockstep peer holding the session, a hitch), the original kept audio and script in order
by holding the script until the audio ended, while the recreation keeps the script on its count and
the audio drifts ahead of the mouths by the accumulated stall, unless the stall pauses the audio as in
point 5. Using the US table for a JP voice makes JP lines end on the US step count, which can clip a
longer JP line or leave a gap after a shorter one. In the three tracked scenes nothing changes for
solo play, because point 4 preserves the original's pacing.

The host ordered "voice done" event (the `THREADS_AND_CLOCKS.md` decision table option) also works, but
it adds the input delay to every line on every client and ties every peer to the host's audio device.
The table is cheaper and needs no traffic.

---

## 8. Names

Proposals, with evidence, are in the scratch file `voice_names.py` that came with this write-up: 78
functions and 18 globals. The ones on the main path:

| VA | proposed | evidence |
| --- | --- | --- |
| 0x8864B0 | `FFX_Voice_IsBusy` | `dword_132FF10 \| lo(qword_132FF14)`, read by 216, 217, btl:103, 104, 169 |
| 0x887570 | `FFX_Voice_Prepare` | FMOD command 16, sets the prepared flag |
| 0x887690 | `FFX_Voice_Start` | FMOD command 15, only caller the 214 getter |
| 0x887630 | `FFX_Voice_StartDefault` | command 15 with 63 and 127, btl:101 |
| 0x887780 | `FFX_Voice_Stop` | clears prepared, FMOD command 10, core:215 |
| 0x887BD0 | `FFX_Voice_RecomputeBusyFlags` | shift, zero, re-read command 17 |
| 0x67A710 | `FFX_FmodCmd_QueryVoicePlaying` | `RunCommandSync(17)`, inline |
| 0x707DE0 | `FfxFmod_Cmd17_IsVoicePlaying` | returns `FmodVoice+64 \| +68` |
| 0x70B5C0 | `FmodVoice__start` | `Event::start`, stamps `+112`, sets `+64` |
| 0x70ADD0 | `FmodVoice__prepare` | `voiceRead` strings, `getInfo` into `+108` |
| 0x70B750 | `FmodVoice__checkLineElapsed` | the wall-clock end test |
| 0x70A590 | `FmodVoice__release` | clears `+64` and the stamps |
| 0x885D20 | `FFX_Voice_GetPalStartDelayFrames` | PAL-gated size to frames |
| 0x887C40 | `FFX_Snd_IsPalMode` | `byte_1330831`, the `[pal]` key |
| 0x86ADE0 | `FFX_Atel_IsAnyModelLoadPending` | `dword_1326B94`, the core:515 set |
| 0x76F060 | `FFX_Cd_IsBusyStub` | `xor eax, eax / retn` |
| 0x6FAF90 | `FFX_SyncData__loadMapData` | the per-map loader |
| 0x6FAA30 | `FFX_SyncData__loadSyncDataList` | `SyncDataList.txt` |
| 0x6FABA0 | `FFX_SyncData__loadSyncVoiceList` | `SyncVoiceList.txt` |
| 0x6FB130 | `FFX_SyncData__asyncLoadJob` | sets `+0x20` |

Wrong or misleading existing names:

- `FFX_AtelSys_Core_215_AdvanceVoiceUnpaced_resi 0x85E090` stops the voice. Suggest
  `FFX_AtelSys_Core_215_SkipVoiceUnlessTimed_resi`, and its CO-OP comment ("falls back to advancing
  the voice state by hand") needs replacing.
- `?_Get_future_error_what@std@@YAPBDH@Z_1 0x81E250` is a FLIRT misname for `return
  dword_12F4E80[a1]`, a sound status getter.
- `g_ffxTimingTrackTolerance 0x12FB8B0` holds the current entry's recorded step length in ms.
  `g_ffxTimingTrackStepDurationMs` says what it is.

---

## 9. Worth checking, and what is still open

The claims that carry the most weight, with where to look:

1. `dword_1325AE0` is never written nonzero, so 216 always reads FMOD: the four xrefs are at 0x85DEAB,
   0x85E209, 0x85E213, 0x85E21A, and nothing in 0x1325AA0..0x1325B04 is written by any other function.
2. The end of a line is `sub_70B750 0x70B750` comparing `(now - FmodVoice+112) / 1000` with `+108`,
   and `+108` is `FMOD_EVENT_INFO.lengthms` from `Event::getInfo` in `FmodVoice__f70ADD0 0x70ADD0`,
   or the end of event callback `sub_707A50` registered on `&+64` at 0x70B618, whichever is first.
   A runtime log of `+64` and the step counter during one line would confirm both, and show which
   path usually wins.
3. Forced `g_ffxPendingSteps` skips the track: `FFX_StepPacing 0x821E80`, the test at its top, against
   `PublishStepScale 0x8206C0`'s index. The falsifiable version: in a tracked scene, force 2 steps for
   one frame and watch `g_ffxStepScale88` read garbage on the second.
4. The 213 hold on `dword_1326B94` and not on the disc: `sub_86ADE0 0x86ADE0` and the stub
   `sub_76F060 0x76F060`.

Open:

- Whether `FmodVoice__f70B5C0` uses `byte_1327134` and `byte_1327135` at all (the decompile does
  not). Who writes them is now known: core:398 and 399 write `byte_1327134` through `sub_873D20`
  and drive the voice volume (FMOD commands 14 and 13), core:400 and 401 write `byte_1327135`
  through `sub_873D10` and drive FMOD commands 12 and 11, which are empty stubs on PC.
- Which overlays send FMOD command 8, and so which pauses stretch a line. The wrapper `sub_67A750`
  is called by `FFX_Snd_SetPausedForOverlay`, `FFX_EscMenu_OnResume`, `sub_68F940` and
  `sub_68FF60` (both EscMenu). Which overlays call `FFX_Snd_SetPausedForOverlay` is still open.
- What the one kind 4 row in `SyncVoiceList` was for. Nothing reads it.
- The exact step at which `+68` and the issued bits drop when the FMOD thread is late, which only a
  runtime trace can show.
