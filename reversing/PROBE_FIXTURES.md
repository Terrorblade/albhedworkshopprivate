# Probe fixtures: per step recordings of the original for Project Zanarkand

The probe plugin, `loader/plugins/probe` (`AlBhedProbe.dll`), records what the original `FFX.exe` does per
step so the recreation's unit tests can be fed the original's inputs and checked against its outputs. It
serves Project Zanarkand PROGRESS 18.1 (the oracle harness) and the done-whens of 3.4.3, 6.3.1, 10.4 and
10.5. Written 2026-10-13, the joints recipe added the same day. First run in game 2026-10-29: recording on
from boot (`probe.capture=1`), pacing, camera and motion, ended by quitting the game. All three files read
back whole against the known build, no lost records by `probe_fixture.py --check`, and the plugin ran next
to Pilgrimage Together and the cheats plugin with every hook installed. Joints has still not been run.

The C++ side of every layout below is `loader/plugins/probe/capture/FixtureFormat.h`, with static_asserts
on each size and offset. The reader is `projectzanarkand/tools/probe_fixture.py`, which checks every record
size against the one the file's header states.

Related docs: `MAIN_LOOP.md` (FFX_MainStep and FFX_StepPacing), `CAMERA_BUILD.md` section 10 (the camera
recipe), `CHARACTER_MOTION.md` section 10.4 (the motion recipe) and section 11.12 (the joints recipe).

---

## 0. The short version

- Four recipes, one file each per capture session: pacing (kind 1), camera (kind 2), motion (kind 3) and
  joints (kind 4). Joints is off by default because one CHR costs about 4 MB a second.
- Each file is a 64 byte header, a per kind extension, then fixed size little endian records. No record
  count anywhere, the reader takes it from the file size. A capture killed mid write leaves a partial
  last record, which the reader reports and ignores.
- The header carries the PE TimeDateStamp, SizeOfImage and CheckSum of the `FFX.exe` that was recorded,
  so a fixture from another build is caught rather than silently compared.
- Start and stop with `probe.capture=1` in `<game>\AlBhedWorkshop\albhed_probe.txt` or with the hotkey,
  Scroll Lock by default. Works with no co-op session and with Pilgrimage Together not installed.
- Files land in `%LOCALAPPDATA%\AlBhedWorkshop\probe` unless `probe.output_dir` says otherwise. Never in
  the game folder.
- The step thread only copies into memory. A shared writer thread does every WriteFile. A slow disk
  costs dropped records, counted in the log, never a stalled step.

| Kind | Name | Hooked | Records | Consumer |
|---|---|---|---|---|
| 1 | pacing | `FFX_StepPacing 0x821E80`, entry and return | one per call | `tests/engine/StepPacingReplayTest.cpp`, PROGRESS 3.4.3 |
| 2 | camera | `FFX_Came_ActivateScreen 0x7BC110`, entry and return | one per call, 9 per step (section 3) | `tests/engine/CameraReplayTest.cpp`, PROGRESS 6.3.1 |
| 3 | motion | `FFX_Ch_UpdateMotionAll 0x832E10`, entry and return | one per in use CHR per side | `tests/engine/MotionTest.cpp`, PROGRESS 10.5 |
| 4 | joints | `FFX_Mot_AdvanceFrame 0x838C90` at its call site, entry and return, plus three calls inside it | one per chosen CHR per side | the 10.4 playback tests, PROGRESS 10.4 |

---

## 1. Files

Named `probe_YYYYMMDD_HHMMSS_<session>_<kind>.bin`, local time at the start, with `<session>` counting
captures since the game started. All files of one session share the stem.

### 1.1 Header, 64 bytes

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0x00 | char[8] | magic | `ABWPROBE` |
| 0x08 | u16 | version | 1 |
| 0x0A | u16 | kind | 1 pacing, 2 camera, 3 motion, 4 joints |
| 0x0C | u32 | headerBytes | 64 + extensionBytes, where the first record starts |
| 0x10 | u32 | recordBytes | 232, 13572, 656, or for joints 688 + 492 x capacity (67600 at 136) |
| 0x14 | u32 | exeTimeDateStamp | `0x6AA2219C` for the build the kit was derived from |
| 0x18 | u32 | exeSizeOfImage | `0x0237D000` |
| 0x1C | u32 | exeCheckSum | `0x00A32338` |
| 0x20 | u32 | exeModuleBase | where FFX.exe loaded this run, for reading the raw pointers |
| 0x24 | u32 | sessionIndex | |
| 0x28 | u64 | startFileTime | UTC FILETIME at the start |
| 0x30 | u32 | extensionBytes | |
| 0x34 | u32 | flags | 0 |
| 0x38 | u32[2] | reserved | 0 |

Then the extension. Camera: four u32, screen bytes (448), screen count (3), slot bytes (0xDA0) and slot
count (3). Motion: the CHR stride (0x880), the span count (22), then that many `{u16 offset, u16 bytes}`,
96 bytes in all. Joints: ten u32, the CHR stride (0x880), the joint capacity, the channel size (44),
channels per joint (9), the engine's joint stride (352), the first and the count of the composed dwords
(57 and 23), the record's joint entry size (96), the head size (688) and the span count (9), then the
spans, 76 bytes in all. Pacing has none.

Adding kind 4 did not change any existing layout, so the format version is still 1. A reader that
predates it rejects a joints file as an unknown kind rather than misreading it.

### 1.2 Record prefix, 20 bytes, every kind

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | u32 | seq | 0, 1, 2 ... per file. A gap would be a bug, drops are not written at all |
| 4 | u32 | phyreFrameTick | the presented frame counter, which repeats across the steps of one frame |
| 8 | u32 | mainStepCounter | the engine's own step counter |
| 12 | u32 | pacingOrdinal | the probe's count of FFX_StepPacing calls since the DLL loaded |
| 16 | u16 | callIndex | pacing: the call's index within its frame. Camera and motion: within the step. Joints: the motion call it ran inside |
| 18 | u8 | phase | motion and joints: 0 entry, 1 return. Others 0 |
| 19 | u8 | flags | per kind, below |

`pacingOrdinal` is the key that joins the files. Every camera, motion and joints record carries the
ordinal of the FFX_StepPacing call that preceded it, and a joints record's (pacingOrdinal, callIndex) is
the same pair as the motion records of the FFX_Ch_UpdateMotionAll call it ran inside.

---

## 2. Pacing, kind 1, 232 bytes

`FFX_StepPacing 0x821E80`, `int __cdecl (float dt)`, one caller at `FFX_MainStep 0x820B99`. The kit's
`ffx::HookStepPacing` (`src/ffx/StepPacing.cpp`) calls the probe before and after the original. Entry
reads the inputs, return reads the outputs, and the record is committed at return.

| Offset | Type | Field | Source |
|---|---|---|---|
| 20 | f32 | dt | the argument, already booster scaled |
| 24 | f32 | frameDelta | the PApplication frame delta animate gets |
| 28 | f32 | fixedTimeStep | PApplication |
| 32 | u32 | appFrameNumber | PApplication |
| 36 | i32 | frameRateDivider | |
| 40 | i32 | boosterEnabled | |
| 44 | i32 | boosterSpeedIndex | |
| 48 | i32 | subStepCount | |
| 52 | u8 | allowCatchUp | 1 or 0, 0xFF when not asked (below) |
| 56 | 40 bytes | entry | pacing globals on entry |
| 96 | 40 bytes | exit | the same at return |
| 136 | 48 bytes | trackEntry | mode 1 timing track on entry |
| 184 | 48 bytes | trackExit | the same at return |

Pacing globals, 40 bytes: pendingSteps, catchUpExtraSteps, pacingMode (i32), frac60 `0x12FB7F8` and
frac30 `0x12FB7FC` (f32), the 60 Hz game clock (u32), menuSysRunning, menuSysRunningAlt, menuOpenPending
and the save UI state (i32).

Timing track, 48 bytes: nextTime, tolerance, dueTime, elapsed (f32), start, pending pause and total pause
(i64, microseconds despite the kit's `Ms` names), firstStep (i32), 4 bytes of padding.

Flags: 0x01 the PApplication inputs were read, 0x02 the globals at return were read, 0x04 the track at
return was read.

### 2.1 The catch-up gate

FFX_StepPacing asks `FFX_StepPacing_AllowCatchUp 0x81FD30` only when no steps are pending. The probe does
not detour that function, because Pilgrimage Together's `Cutscene` check looks at its first bytes and a
detour there would change Pilgrimage's behaviour. Instead the probe calls it itself on entry, through
`ffx::QueryCatchUpAllowed`, and only when `entry.pendingSteps == 0`. It is a pure query, and the one call
FFX_StepPacing makes before reaching it (`FFX_StepPacing_UpdateBattleMagicActiveFlag`) writes a flag
nothing reads, so the answer on entry is the answer the engine got. `0xFF` means the engine did not ask
on that call.

### 2.2 What 3.4.3 does with it

Feed `dt` and the entry state into `zan::app::StepPacing::Step` in seq order, with the gates taken from
`allowCatchUp`, the menu flags and the save UI state. The result must equal `exit.pendingSteps`,
`exit.frac60`, `exit.frac30` and `exit.clock60Hz` bit for bit. Mode 1 needs the track fields and is
wall clock driven, so a mode 1 test replays the recorded track rather than a clock.

Chaining one call's exit into the next call's entry needs FFX_MainStep's catch-up decision in between
(pendingSteps 0 when it is 1 or less, else one less) and three things the engine writes between calls:
pendingSteps lowered by a menu step that skips pacing or by the resets at `0x820BFB` and `0x820EBA`,
`g_ffxTimingTrackPauseUsPending` written by FFEscMenu's destructor (`0x6903D2`), and
`g_ffxTimingTrackStartUs` moved on by `FFX_Cutscene_StepPauseOverlay` (`0x8AB301`), which happens in mode 0
too and is zeroed again by the next LoadTimingTrack. Those are the only writers of the pacing globals
outside FFX_StepPacing and FFX_StepPacing_LoadTimingTrack.

---

## 3. Camera, kind 2, 13572 bytes

`FFX_Came_ActivateScreen 0x7BC110`, `int __cdecl (int screen)`, called 9 times per step from three call
sites in `FFX_MainStep`. The screen walk runs twice (the loop at `0x82136E`): `0x821189` activates screen 0,
then `0x82135E` activates 1, 2 and so on until one returns 1, which is index 3 because a disabled screen
returns -1 and the walk goes on. Then `0x821380` activates `g_ffxMainScreenIndex` once more. With the main
screen at 0 that is screen 0 three times and screens 1 to 3 twice each, and the 2026-10-29 recording has
exactly that on every step. Each activate of an enabled screen steps its rect tweens and its two shake
channels, so screen 0's shake advances three times a step. Hooked through `ffx::HookActivateScreen`
(`src/ffx/CameraBuild.cpp`). Reserved on entry, committed at return. The recipe is `CAMERA_BUILD.md`
section 10.

| Offset | Type | Field | Side |
|---|---|---|---|
| 20 | i32 | screen | argument |
| 24 | i32 | result | return: 0 built, -1 screen disabled, 1 screen >= 3 |
| 28 | u32 | rngStream9Entry | RNG stream 9 state, `0x1135EE0 + 0x24` |
| 32 | u32 | rngStream9Exit | |
| 36 | i32 | activeScreenEntry | index of `g_ffxCameActiveScreen 0x23114C0`, -1 if null |
| 40 | i32 | activeSlotEntry | index of the active camera slot |
| 44 | i32 | activeScreenExit | |
| 48 | i32 | activeSlotExit | |
| 52 | u32 | directLookAtFlag | `0x231142C` |
| 56 | u32 | overrideFlag | `0x23114C4` |
| 60 | u32 | levelNameHash | `[g_ffxGfxCtx 0xCCB9D8] + 0xFAF4` |
| 64 | i32 | pendingGlobalEffect | `0xC421E4` |
| 68 | i32 | pendingBattleEffect | `0xC421E8` |
| 72 | i32 | ps2FrameWidth | `0xC44240` |
| 76 | i32 | ps2FrameHeight | `0xC44244` |
| 80 | i32 | magicId | `0xC64CA0` |
| 84 | i32 | magicPrevId | `0xC64CA8` |
| 88 | u32 | magicFreezeFlag | `0x1940ABC` |
| 92 | i32 | liveSceneId | `0x1330844` |
| 96 | u8 | magicFrozen | `MagicFile__isCameraFrozen 0x9DA700`, called on entry, 0xFF if not callable |
| 97 | u8 | nearLatchEntry | `0xC44238` |
| 98 | u8 | nearLatchExit | |
| 99 | u8 | levelNameHashValid | |
| 100 | 64 bytes | directBlock | `0x1136FB8..0x1136FF7` |
| 164 | f32[16] | viewEntry | `g_ffxCameViewMatrix 0x2311440` |
| 228 | f32[16] | projEntry | `g_ffxCameProjMatrix 0x2311480` |
| 292 | f32[16] | viewExit | |
| 356 | f32[16] | projExit | |
| 420 | 1344 bytes | screensEntry | the three screens at `0x1137000` |
| 1764 | 10464 bytes | slotsEntry | the three slots at `0x1137540` |
| 12228 | 1344 bytes | screensExit | |

Flags: 0x01 globals, 0x02 RNG, 0x04 matrices, 0x08 the screen and slot blocks.

The matrices are taken on entry as well as at return because the override and frozen paths can leave
one or both untouched, and a recreation has to start from what was there. Slots are only taken on entry
because ActivateScreen reads them and does not write them. The screens do get one write besides the
rects and shake: when slot `+0xCA8` is set, `0x7BC65B` copies slot `+0xCAC..+0xCB7` (the depth of field
switch and two depths) into screen `+0xD0..+0xDB` and pushes them to the gfx side when they changed.

What 6.3.1 does with it: restore the screens, slots, stream 9 and the globals from the entry side into
the recreation, run `ActivateScreen(screen)` and compare `viewExit`, `projExit`, `screensExit` and
`rngStream9Exit`. The level name hash picks the ssbt01, ssbt00 and hiku15 view gate.

---

## 4. Motion, kind 3, 656 bytes

`FFX_Ch_UpdateMotionAll 0x832E10`, `__cdecl (float dt)`, one caller at `0x82107D`, which always pushes
`0.033373334` (`0x3D08B27A`). Hooked through `ffx::HookUpdateMotionAll` (`src/ffx/MotionStep.cpp`). At
entry (phase 0) and at return (phase 1) every CHR whose in use byte (`+0x002`) is set gets one record. A
side with none in use gets one record flagged NoChr so the globals are never missing. The recipe is
`CHARACTER_MOTION.md` section 10.4.

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 20 | f32 | dt | the argument |
| 24 | u32 | chrArray | the pool base, a raw pointer |
| 28 | i32 | chrCount | |
| 32 | u32 | controlledChr | `g_ffxControlledChr`, raw |
| 36 | f32 | walkmeshScale | |
| 40 | i32 | walkmeshTriCount | |
| 44 | f32 | waterLevel | `0x13009D8` |
| 48 | f32 | waterLevelMax | `0x13009DC` |
| 52 | i32[16] | groundRemap | ground type remap for codes 48..63 |
| 116 | i32 | debugNoClip | |
| 120 | u32 | stepScale88 | |
| 124 | i32 | motHalfSpeed | `0x13017F8` |
| 128 | u8 | chrUpdateFrozen | `0x12FBBBB` |
| 129 | u8 | motKeepSpeed | `0x12FBC34` |
| 130 | u8 | motionUpdateEnabled | `0x12FB8FF` |
| 132 | i32 | currentEventId | `0x12FBC40` |
| 136 | i32 | liveSceneId | `0x1330844` |
| 140 | char[16] | currentEventName | first 16 bytes of `0x23CBC60` |
| 156 | u16 | slot | pool index, 0xFFFF for NoChr |
| 158 | u16 | inUseCount | CHRs in use on this side |
| 160 | u16 | ordinal | this record's index among them |
| 162 | u16 | chrFlags | below |
| 164 | i16 | anmFrameCount | the s16 at `[m_anmHeader]` |
| 168 | 488 bytes | chr | the CHR spans, packed in order |

Flags: 0x01 NoChr, 0x02 globals read, 0x04 the pool span validated.
chrFlags: 0x01 this is the controlled CHR, 0x02 `m_seqPC` non-null, 0x04 `m_anmHeader` non-null,
0x08 anmFrameCount was read, 0x10 the copy faulted and the bytes are partial.

The CHR spans, also written into the extension so a reader never hardcodes them:

| CHR offset | Bytes | Fields |
|---|---|---|
| 0x000 | 4 | m_id, m_inUse, +0x003 |
| 0x00C | 32 | pos, prevPos |
| 0x03C | 28 | walkmeshPos, vel |
| 0x05C | 4 | m_scaleX |
| 0x064 | 4 | m_scaleZ |
| 0x07C | 16 | ground normal and its w |
| 0x154 | 8 | m_speed, m_rotY |
| 0x168 | 12 | m_moveDir, m_groundHeight, m_runThreshold |
| 0x180 | 4 | m_hideFlags, m_b181, m_groundMode, +0x183 |
| 0x184 | 4 | +0x184..0x187, m_locomotionMode at +0x186 |
| 0x194 | 8 | m_flags1, m_flags2 |
| 0x330 | 84 | the seven CHRANIMF |
| 0x4E4 | 64 | collision radius, height band, vertVel and neighbours |
| 0x694 | 32 | m_bodyCentre, m_vec6A4 |
| 0x714 | 24 | m_seqPC (raw), clip start, end, span, loop counts, playing, wait |
| 0x734 | 4 | +0x734..0x737, m_motionRequestActive at +0x736 |
| 0x740 | 4 | m_frame |
| 0x750 | 8 | speedMulA, hokan countdown, playSpeed, frameDelta |
| 0x75C | 128 | m_slots[32] |
| 0x7E0 | 4 | m_frameRateScale |
| 0x800 | 4 | m_anmHeader (raw) |
| 0x824 | 8 | m_walkmeshTri, +0x826, m_groundAttrs |

`probe_fixture.py` has `Fixture.chr_image(record)`, which lays the spans back out in a zeroed 0x880 image
so tests can read fields at their real offsets.

What 10.5 does with it: record a walk across Besaid, load the same map in the recreation, set each CHR
from its phase 0 record, step once, and compare against the phase 1 record of the same ordinal and slot
within the C1 epsilon.

---

## 5. Joints, kind 4, 688 + 492 x capacity bytes

The recipe is `CHARACTER_MOTION.md` section 11.12. Every function involved has exactly one caller, so the
kit (`ffx/MotionPlayback.h`, `src/ffx/MotionPlayback.cpp`) hooks each at that call site rather than
detouring it. One dword changes per site, nothing is relocated, and each patch chains behind another
plugin's patch on the same site.

| Function | VA | Call site | What the probe keeps |
|---|---|---|---|
| `FFX_Mot_AdvanceFrame(CHR *chr, int applyBlend)` | `0x838C90` | `0x833273` in `FFX_Ch_UpdateMotionAll` | a record before and after |
| `FFX_Mot_StepSequence(CHR *chr)` | `0x8377C0` | `0x838E05` in AdvanceFrame | its result and CHR `0x70C..0x7FF` right after it |
| `FFX_Ch_IsNearlyStationary(CHR *chr)` | `0x839200` | `0x838FE5` in `FFX_Mot_BuildJointTransforms` | its answer |
| `FFX_Ch_IsNearCameraForSmoothing(CHR *chr)` | `0x839140` | `0x838FF2` in BuildJointTransforms | its answer |

IsNearlyStationary opens with a rel32 call at +3, so a detour could not have taken its prologue. That is
why these are call site patches. `FFX_Ch_SetMotionKey 0x837AC0` (23 callers) and `FFX_Mot_RunEvents
0x834350` (called from `FFX_Ch_DispatchInBatches` after the step, not inside AdvanceFrame) are not hooked.
The CHR range already shows a clip change through `m_motionId`, and `m_lastEventFrame` sits in the
playback block.

**Which CHRs.** `probe.joints_chr`: -1 (the default) records only the CHR `g_ffxControlledChr` points at,
-2 records every CHR AdvanceFrame is called for, and 0..65535 records CHRs whose u16 `m_id` equals it, so
1 is Tidus wherever he is, controlled or not. Each chosen CHR gets a phase 0 record at entry and a phase 1
record at return.

### 5.1 The head, 688 bytes

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 20 | u32 | chrPointer | the CHR, raw |
| 24 | i32 | applyBlend | the second argument, `m_flags1` bit 24 |
| 28 | u32 | bundleBase | `[CHR+0x7E8]`, `m_motionBundle`, raw |
| 32 | u32 | channelsBase | `[CHR+0x748]`, raw |
| 36 | u32 | jointsBase | `[CHR+0x328]`, raw |
| 40 | u16 | skeletonJoints | the u16 at `[CHR+0x1C0] + 0x0A`, 136 for c001 |
| 42 | u16 | jointsCopied | min(skeletonJoints, capacity) |
| 44 | u16 | chrFlags | below |
| 46 | u8 | stationary | IsNearlyStationary's answer, 0xFF when it was not called |
| 47 | u8 | nearCamera | IsNearCameraForSmoothing's answer, 0xFF when it was not called |
| 48 | u8 | stationaryCalls | |
| 49 | u8 | nearCameraCalls | |
| 50 | u8 | stepSequenceCalls | |
| 51 | u8 | atelContextByte | byte 0 of the context `g_ffxAtelCtx 0x1326B28` points at (bit 2 gates the composer) |
| 52 | i32 | liveSceneId | `g_ffxLiveSceneId 0x1330844` |
| 56 | u16 | storyProgress | `g_ffxStoryProgress 0x112D67C` |
| 58 | u8 | btlSubPhase | `g_ffxBtlSubPhase 0x112A8E0`, a byte |
| 59 | u8 | motKeepSpeed | `0x12FBC34` |
| 60 | i32 | currentEventId | `0x12FBC40` |
| 64 | u32 | stepScale88 | `0x23CBBEC` |
| 68 | i32 | motHalfSpeed | `0x13017F8` |
| 72 | u32 | camSlot | `g_ffxCameActiveSlot 0x2311430`, raw |
| 76 | i32 | camSlotIndex | which of the three slots, -1 if none |
| 80 | f32[4] | camEye | slot `+0x65C`, all four lanes because the query uses FFX_Vec4_Sub |
| 96 | f32 | camPitchWhenSet | slot `+0x6B0` |
| 100 | f32 | camPitchWhenClear | slot `+0x6D0` |
| 104 | u8 | camPitchSelect | slot `+0x64D`, which pitch the query reads |
| 105 | u8 | atelContextValid | the context pointer landed inside the context array |
| 106 | u16 | slot | the CHR's pool index, 0xFFFF if it is not in the pool |
| 108 | f32 | chrDataScale | `[[CHR+0x1C4] + 0x38]`, times 0.001f is the translation scale |
| 112 | char[8] | modelName | the first 8 bytes at `[CHR+4]`, `c001` |
| 120 | 318 bytes | chr | the CHR spans below, packed in order |
| 438 | 2 bytes | pad | |
| 440 | i32 | stepSequenceResult | phase 1 only, 0 when it did not run |
| 444 | 244 bytes | seqExit | CHR `0x70C..0x7FF` right after StepSequence returned, phase 1 only |

Then `channels`, capacity x 396 bytes (nine 44 byte channels a joint, a straight copy of the engine's
array), then `joints`, capacity x 96 bytes: per joint the composed dwords 57..79 (`+0xE4..+0x13F` of the
352 byte joint), then byte 324 (`+0x144`, the Auto composer's switch), then 3 bytes of padding. Only the
first `jointsCopied` of each are filled.

The CHR spans: `0x000` 8 (m_id and the model name pointer), `0x194` 12 (`m_flags1`, `+0x198`, the parent),
`0x1C0` 8 (skeleton and CHRDATA pointers), `0x328` 4 (the joint array), `0x684` 32 (the previous and
current body centre), `0x70C` 244 (the whole playback block), `0x800` 4 (`m_anmHeader`), `0x820` 4
(`m_flags3`), `0x870` 2 (the two composer bytes).

prefix.flags: 0x01 globals read, 0x02 camera slot read, 0x04 channels copied, 0x08 joints copied, 0x10
seqExit filled, 0x20 pointers rebased. chrFlags: 0x01 the controlled CHR, 0x02 more skeleton joints than
the capacity, 0x04 no skeleton, 0x08 no channel array, 0x10 no joint array, 0x20 no bundle (its pointers
are left raw), 0x40 a channel link that did not land on a channel was left raw, 0x80 a copy faulted.

### 5.2 Pointer rebasing

Done by the probe before the record is committed, as section 11.12 asks:

- Channel `+0x08` (stream start) and `+0x0C` (cursor), and CHR `0x70C` (`m_motionEntry`), `0x710`
  (`m_motionGroup`), `0x714` (`m_seqPC`) and `0x7F4` (`m_eventArray`), in the CHR span and in seqExit:
  a non-null pointer becomes pointer - bundleBase, as a u32 with wraparound, so pointer = value +
  bundleBase recovers it exactly. Null stays 0.
- Channel `+0x00` (next streamed channel) and CHR `0x74C` (`m_streamList`): null becomes 0xFFFFFFFF and a
  pointer into the channel array becomes its index, (pointer - channelsBase) / 44. Anything else is left
  raw with chrFlags 0x40.
- CHR `0x748` and `0x7E8` stay raw, and so do the three bases in the head.

A channel past the current clip's joint count keeps stream pointers from whatever clip bound it last,
which can be another bundle. Its offset is still lossless, it just is not an offset into this bundle.

### 5.3 What 10.4 does with it

Load each phase 0 record into a MotionChr: the channels with their links and stream offsets resolved
against the CHR's registered `resident0.mgrp`, the joints, the playback block, and the MotionWorld globals
and camera slot from the head. Run the recreation's AdvanceFrame with `applyBlend`, then compare against
the phase 1 record with the same pacingOrdinal, callIndex and slot: joint dwords 57..79 and every
channel's key, previous key, target and current bit for bit. seqExit splits a mismatch between the VM
and the channels, and the two composer answers settle a mismatch only in dwords 57..63.

---

## 6. Settings

`<game>\AlBhedWorkshop\albhed_probe.txt`, written with defaults on first run, re-read whenever its
timestamp changes. No co-op session is needed and none is looked for.

| Key | Default | Meaning |
|---|---|---|
| probe.capture | 0 | 1 records. The hotkey flips it and saves the file |
| probe.pacing | 1 | record kind 1 |
| probe.camera | 1 | record kind 2 |
| probe.motion | 1 | record kind 3 |
| probe.joints | 0 | record kind 4, off by default because it is heavy |
| probe.joints_chr | -1 | -1 the controlled CHR, -2 every CHR, 0..65535 an `m_id` to match |
| probe.joints_max | 136 | joints each record has room for (8..512), read when a capture starts |
| probe.hotkey | 145 (Scroll Lock) | virtual key that toggles recording, 0 for none |
| probe.max_steps | 0 | stop after this many steps, 0 for no limit |
| probe.budget_mb | 64 | memory per file waiting for the disk |
| probe.output_dir | empty | output folder, empty for `%LOCALAPPDATA%\AlBhedWorkshop\probe` |

`max_steps` stops on the step thread itself, so the pacing file holds exactly that many records when the
capture started between steps. The stop leaves `probe.capture` at 1 in the file and 0 in memory. The next
save of the file or the hotkey starts a fresh session, and the next boot records from the start.

The log is `<game>\AlBhedWorkshop\albhed_probe.log`. Each close logs the record, drop and write error
counts. A capture that ends with the game's exit gets no close line. The files are written whole from
DllMain at process exit, with no log line, so the log stops at "session 0 started". Use
`probe_fixture.py --check` on the session's files instead, see section 7.

---

## 7. Cost

- No VirtualQuery per step. Image addresses take the fast path, the CHR pool and PApplication spans are
  validated once per pointer or count change, and the CHR copy sits in `__try` instead.
- Records are filled in place in the writer's chunk, so there is no extra copy. Chunks are at least
  256 KB and are handed to the writer when full or after a second.
- Camera is the big one, about 13.5 KB a call, about 1.2 MB a second. Motion is about 650 bytes per CHR
  per side, so 20 CHRs is about 0.8 MB a second. Pacing is under 10 KB a second.
- Joints is about 66 KB a side for c001, so one CHR at one sub step a frame is about 4 MB a second, and
  `probe.joints_chr=-2` multiplies that by every CHR on the map. Every engine pointer it follows is
  read inside `__try`, the camera slot and ATEL context are range checked against module memory, and
  the rebasing runs on the copy in the writer's buffer.
- With the budget spent, records are dropped and counted. A dropped record is not written at all, so seq
  never has a gap. Compare the log's drop count to zero before trusting a file for a sequence test. When
  there is no close line (the capture ended with the game), `python -I tools/probe_fixture.py FILES --check`
  looks across the session's files: pacing ordinals without a gap, every motion call with a full side per
  CHR in use, and camera and motion covering the same engine steps, since the two are written apart and a
  lost chunk in one leaves steps the other still has. The first run had 3 engine step numbers missing from
  both camera and motion, which is the engine skipping both functions on those steps, not a loss.

---

## 8. Caveats

- `pacingOrdinal` counts calls since the DLL loaded, title screen included. It is a join key, not a game
  quantity.
- The first step of a session can be partial. A capture started from the control thread can begin
  between the motion and camera calls of one step, so tests should start at the first pacing record and
  drop camera and motion records with an earlier ordinal.
- While the in-game menu holds `MenuSysRunningAlt`, FFX_MainStep skips FFX_StepPacing, so the ordinal
  stops while camera calls can still arrive. Those records share the last ordinal and have rising
  callIndex.
- `allowCatchUp` is asked on entry, see 2.1.
- Camera slot shake runs in `FFX_Came_StepAll 0x7BE110`, per sub step, and is not recorded yet. The
  address is in `addresses/Camera.h` for the next fixture.
- The fov and aspect pushed to the gfx side are not in the camera record.
- Joints: between steps, the render job writes `m_bodyCentre`, `m_prevBodyCentre` and clears `m_flags3`
  bit 4 through `FFX_Ch_StepSecondaryPhysics`. Take each step's phase 0 record as the state rather than
  chaining the recreation's own output, as section 11.12 says.
- Joints: the weapon copy at the end of AdvanceFrame writes joints 1..3 from the parent's joints 9..11, so
  a weapon CHR's phase 1 joints depend on another CHR. Record with `probe.joints_chr=-2` for those.
- Joints: the composer answers say what the queries returned, not which composer ran. The scene
  overrides (368, 285, 80, 311, 100, 60) and the 0x870, 0x871 and `m_flags3` bit 2 checks are applied
  around them, see `CHARACTER_MOTION.md` 11.7. The head and CHR bytes carry every input to that.
- Motion, found 2026-10-29 on the first recording: the return copy races the render job. Pass 4 of
  `FFX_Ch_UpdateMotionAll` posts `FFX_Ch_UpdateRenderJob 0x833530` through `FFX_Ch_DispatchUpdateJob` at
  0x83332D and 0x833374, before its `retn` at 0x83338F, so a worker can be writing the CHR while the hook
  copies it. The fields the job writes can show either value in a phase 1 record: the bone outputs of
  `FFX_Ch_UpdateBonePositions 0x8350F0` (+0x694..+0x6AC, `m_bodyHeight` +0x510, `m_feetDepth` +0x514),
  `m_flags2` bit 0 (cleared at 0x83358A after `FFX_Ch_ShadowRegistrySetBit`), `m_flags1` 0x200 (set to "this
  is the controlled CHR" at 0x833637..0x83365F) and the shade coefficient target and mode (`push 3` at
  0x834280 into `FFX_Ch_SetShadeCoeff`). `tests/engine/MotionReplayTest.cpp` in Project Zanarkand
  accepts those writes field by field and still compares everything else exactly. The next call's phase 0
  copy is clean, the previous job is joined before the step goes on (`CHR_POOL.md` 5.2).
- Raw pointers (chrArray, controlledChr, m_seqPC, m_anmHeader) are only meaningful against
  `exeModuleBase` and that run's heap. Compare them for null or equality, never by value across runs.

---

## 9. Proposed IDB names

IDA was read only for this pass. Every name proposed with the first three recipes is now in the IDB
(checked 2026-10-13): the `int screen` and `float dt` argument types, `g_ffxLiveSceneId`,
`g_ffxPendingGlobalEffectId`, `g_ffxBtlPendingMagicEffectId`, `g_ffxMagicCameraFreezeFlag`,
`g_ffxCameMainNearLatch`, `g_ffxCamePs2FrameW` and `H`, `g_ffxCamePixelBiasX` and `Y`,
`g_ffxCameDebugFovMode` and `g_ffxCameDirectRoll`. Still open:

| VA | Current | Proposed |
|---|---|---|
| 0x782970 | sub_782970 | FFX_Btl_GetSubPhaseKey (tentative) |
| 0x838C90 | FFX_Mot_AdvanceFrame `void (CHR *chr, int)` | name the second argument `applyBlend` |
| 0x8377C0 | FFX_Mot_StepSequence `int (int)` | argument type `CHR *chr` |
| 0x838F70 | FFX_Mot_BuildJointTransforms `int (unsigned __int16 *)` | argument type `CHR *chr` |
| 0x839200 | FFX_Ch_IsNearlyStationary `bool (float *)` | argument type `CHR *chr` |
| 0x839140 | FFX_Ch_IsNearCameraForSmoothing `char (int)` | `bool (CHR *chr)` |
