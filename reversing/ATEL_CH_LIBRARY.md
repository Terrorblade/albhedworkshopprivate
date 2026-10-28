# The ATEL Ch syscall library, all 145 rows

2026-10-26. PROGRESS 1.8.7. Library 5 of `g_ffxAtelSysFuncLibs 0x1328558`, the table
`g_ffxAtelSysFuncLib05_Ch 0xC891F8`, 145 rows of {start, poll, float getter, int getter}.
`FFX_Atel_Init 0x86D6D0` registers it with `FFX_Atel_RegisterSysFuncLib(5, &g_ffxAtelSysFuncLib05_Ch)`.
The ABI is `ATEL_CONTEXT.md` 11.7 and 13. This note names every row that a shipped script calls, marks
the rest dead, and lists what each row means for lockstep. `../tools/apply_ch_names.py` puts the names
in the IDB. All addresses are IDA VAs at base 0x400000.

Ch is library 5, not library 1. Library 1 is Sys. A script operand picks the library in bits 12..15
and the function in bits 0..11, so a Ch call is operand 0x5NNN. [V]

Read only throughout. The handlers were decompiled through idalib on a scratch copy of the IDB, with
nothing renamed and nothing saved. The census walked the install with `../tools/ebp.py` and the
recreation's `tools/mon_ai.py` loader, and no script bytes were copied anywhere.

Marks: [V] read in the decompile or the disassembly, [I] inferred (a role read from the shape of a
wrapper, an engine field with no traced reader), [D] counted in shipped data. The table carries V or I
per row.

## 1. Short version

- **112 of the 145 indexes are called by shipped scripts, 33 are not.** 253,293 call sites, every one
  of them in an event package. No monster AI, battle field file or system_01 calls the Ch library. Two
  of the dead rows (18 and 28) are null rows. Every used row has a role, and the 31 dead rows with a
  body got one too, with the name left positional. Eight used roles name the bit or byte they write
  rather than a purpose (86, 87, 88, 103, 131, 134, 135, 141), because no reader that gives the
  purpose was found. Section 4.7 and 7. [D] [V]
- **Names follow the slot suffix form, `FFX_AtelSys_Ch_NNN_<Role>_<slot>`**, the same form as the
  Core library. Twelve slots on eleven rows already had descriptive names, all of them right.
  Section 3.
- **No Ch handler draws from an RNG stream.** Nothing in the call closure reaches `FFX_Rand_Stream`,
  `FFX_Btl_Rand` or the CRT `rand`. The one clock hit, on ch:61 and 62, is a false path through the
  allocator's one time heap cookie. [V]
- **The render job is where Ch reaches randomness and the camera.** ch:118 and 126 turn on the
  random head sway that `FFX_Ch_UpdateBlink` draws from the CRT `rand()` on a Phyre worker thread, and
  ch:124 aims the head at the local camera eye. Both only move bones, but ch:38, 54 and 55 copy bone
  and joint world positions into script variables, so a script can carry them into the simulation.
  Section 5.1.
- **Eleven rows touch the disk.** ch:79, 99, 102 and 104 hold the script until an asynchronous read
  finishes, 78, 97 and 101 start one, 98 and 100 (dead) can hand the load state to the script, and
  ch:1 and 16 load synchronously inside the step, which is a stall and not a divergence. ch:104 waits
  for every pending ROM read of any subsystem. [V]
- **Hard coded special cases sit in the motion family.** The `FFX_EvScript_chSetMotion` helper behind
  ch:25, 26, 65 to 70 and 137 to 140 patches the joint composer mode by scene and story progress and
  sets a one slot flag for chr 102. ch:8 and 13 refuse to hide chr 0x108A on one map, and ch:84 feeds
  a four entry global ring for model f117. [V]
- **Eight rows pop their arguments only when the actor has a CHR** (111, 115, 118, 129, 130, 131, 143,
  144). Called from an actor with no CHR the arguments stay on the stack. A port must keep this. [V]

## 2. The census [D]

Sources, all read from `FFX_Data.vbf`:

| source | files | Ch calls |
|---|---|---|
| event packages, `jppc/event/obj/*.ebp` | 397 | 253,293 |
| battle field files, `battle/btl/*/*.bin` section at header +4 | 816 | 0 |
| monster AI, `battle/mon/_mNNN/mNNN.bin` section 0 | 361 | 0 |
| `battle/btl/system_01/system_01.bin` | 1 | 0 |

375 of the 397 event packages contain at least one Ch call. No call names an index past the end of
the table. For scale, the library totals over the same sources are Core 1,861,120, library 6 801,131,
Ch 253,293, library 7 127,268, library 8 50,989, library 4 25,955, Sys 7,332, library 11 869 and
library 12 328.

The arity column comes from the handler's pops. The census also derives one from the bytecode, the
same way as the Core census (the stack depth at every `0x58` call, a one unknown solve for nested
`0x35` calls, seeded with the settled Core arities). Where both exist they agree on every used row.
Six used rows are only called in expression form (53, 81, 93, 94, 114, 117), and ch:12 has too few
calls for an estimate, so for those the arity is the pop count alone.

The heaviest rows by calls: ch:0 SetMotionKey 38,040 in 264 packages, 52 SetBlendFrames 30,659, 92
chSetNextHokan 30,539, 37 chSetMotionSlot 23,505, 126 SetHeadSwayEnabledOf 16,838, 3 WaitMotionDone
12,465, 26 SetMotionFrames 11,439, 8 SetEventHidden 10,938, 15 SetNoBodyCollision 9,680. The widest
are ch:1 LoadEventMotionGroupSync in 366 packages, 8 in 315, 15 in 313, 16 in 304 and 135 in 292.

Priority sets. The title (event 23, `test20`) makes no Ch call. The Besaid packages (`bsil*`, `bsvr*`)
call 68 indexes, all named.

Seven used rows are called from a single package (54, 63, 74, 75, 82, 106, 141), and three have a
single call site (82, 106, 141). ch:54 is only in `test23`.

Dead rows, 33: 2, 4, 10, 17, 18, 28, 29, 35, 36, 39, 43, 46, 47, 48, 49, 56, 65, 67, 69, 83, 85, 89,
91, 98, 100, 107, 108, 112, 116, 119, 127, 138, 140. 18 and 28 are null in all four slots.

Row shapes, matching `ATEL_CONTEXT.md` 13.2: 126 rows have only the int getter, 6 only the float
getter (9, 40, 57, 58, 77, 94), 7 have start, poll and int getter (1, 16, 79, 98, 99, 100, 102), 3
have start and poll (3, 30, 73), 1 has only a poll (104), and 2 are null. The 16 bytes after row 144
are string data. [V]

Many rows are copies. ch:0, 2, 27, 29, 63 and 64 are the same SetMotionKey body, 5 and 32 the same
StopMotion, 8 and 13 the same SetEventHidden, 24 and 33 the same SetSpeedScale, 35 and 36 the same
pop and ignore, and the chSetMotion helper calls come in pairs (25 and 65, 26 and 66, 67 and 69, 68
and 70, 137 and 139, 138 and 140). Each copy is its own function in the IDB, so no slot is shared.
The script compiler most likely kept several names for one command. [V] [I]

## 3. Names that were already there

Kept as they are, and checked against the bodies [V]:

- ch:12 `FFX_EvScript_chEnGravity` (prints a not supported warning, does nothing).
- ch:16 start `chReadSystemMGRP` and getter `chReadSystemMGRPFinish`. The getter really is the
  finish, it runs `FFX_Ch_LoadMotionSetSync(chrId, set)`. The `maybe_` can go.
- ch:23 `FFX_EvScript_chSetGravMode`, which is `FFX_Ch_SetGroundMode` (0 clamp, 1 snap, 2
  water). The `maybe_` can go.
- ch:31 `FFX_EvScript_chIsMotionActive`, 37 `FFX_EvScript_chSetMotionSlot`, 40
  `FFX_EvScript_chSetRunThreshold`, 92 `FFX_EvScript_chSetNextHokan`, 93 `FFX_EvScript_chGetMotion`,
  94 `FFX_EvScript_chGetMoveSpeed`.
- ch:101 `chReadMotionGroupStart` and ch:102 poll `chReadMotionGroupSync`.

None of them is wrong. The script never overwrites them, so dropping the two `maybe_` prefixes is a
hand edit.

The polls of ch:1 (0xA79A10) and ch:16 (0xA79970) are `return 5` stubs that a signature pass once
named `__RTC_NumErrors_956` and `_955` (`ATEL_CONTEXT.md`, the misnamed handler table). The scratch copy
already has them positional. The apply script takes over those two exact names as well, in case the
live IDB still carries them.

The existing comments on ch:44, 45 and 82 say "Pops (actor_id, joint)" and "(bone, actor_id)". That is
pop order, the reverse of the push order this note uses, and both agree with the bodies. [V]

`tools/ebp.py` already carries names for the same eleven rows in `SYSCALL_NAMES` and `LIB_INFO` 5 = (0xC891F8, 145,
144). Nothing there is wrong.

## 4. The families

### 4.1 Motion (48 rows)

- **Play a motion.** SetMotionKey (0, 2, 27, 29, 63, 64), SetMotionKeyOf (71, 72), SetDefaultMotion
  (50), SetMotionByModeIndex (89, dead), chSetMotionSlot (37), and the chSetMotion helper family. The
  helper takes `([actorId,] motionId, ?, loops, start, end)`. Its mode picks table indexes (Tbl, 25,
  65, 67, 69), explicit frames (Frames, 26, 66, 68, 70) or inclusive explicit frames (FramesIncl, 137
  to 140). The second pushed argument is popped and not used by any mode read so far.
- **Wait and query.** WaitMotionDone (3, 30), WaitMotionDoneOf (73), IsMotionActive (4, 31),
  IsMotionActiveOf (74), GetMotion (93), GetMotionFrame (81, 117), GetMotionLength (53). All of them
  read CHR +0x736 `m_motionRequestActive` or the sequence state.
- **Timing.** SetBlendFrames (52), chSetNextHokan (92), SetSpeedScale (24, 33, 76), SetPendingLoopCount
  (95), SetSeqWaitKey (34, 80), SetMotionStepScaled (105), StopMotion (5, 32, 75).
- **Composer.** SetJointComposerMode (143), SetForceSmoothComposer (144), SetMotionMode (19).

### 4.2 Residency (16 rows)

ChrDataReadStart (78), WaitChrDataRead (79), ChrDataReadSync (100, dead), DisposeChrData (61, 62),
MotionSetReadStart (97), MotionSetReadSync (98, dead), WaitMotionSetRead (99), DisposeMotionSet (59),
DisposeOwnMotionSet (17, dead), chReadSystemMGRP (16), LoadEventMotionGroupSync (1), chReadMotionGroup
Start and Sync (101, 102), ReleaseEventMotionGroup (60), WaitRomIdle (104). `FFX_ChCache_GetState`
returns 0 loaded, 1 in flight, 2 absent. CHR_RESIDENCY.md has the slot machinery.

### 4.3 Placement, ground and collision (20 rows)

SetFreeMove (14), SetNoBodyCollision (15), SetGroundHeight (11), chSetGravMode (23), chEnGravity (12),
SetPushWeight (9), SetCollisionRadiusScale (57, 77), SetCollisionHeight (128), SetHeightCollision
(115, 116), SetSurface13Blocked (129), GetWalkmeshTri (114), GetGroundType and GetGroundSound (46 to
49, all dead), chGetMoveSpeed (94), chSetRunThreshold (40), SetOffset (22).

The sense of 14 and 15 is direct, though both go through a double inversion. `FFX_Ch_SetFreeMove
0x82A980` passes `1 - on` to `FFX_Ch_SetFlags1Bit80Inv 0x82AA70`, which writes the inverse, so the bit
ends up equal to `on`. Same for 0x100 through `0x82A950` and `0x82A9F0`. So ch:14 with 1 is free move
(no walkmesh, no ground clamp, normal forced to (0,-1,0), idshade cleared), and ch:15 with 1 takes the
CHR out of the body collision list. [V]

### 4.4 Visibility, shading and texture (24 rows)

SetEventHidden (8, 13), SetPartHidden and GetPartHidden (84, 85), SetUpdateWhileHidden (111),
IsSkipRender (112, dead), SetZClipEnabled (42), SetClipZ (58), SetShadowKind (41), SetBlobShadowParam
(133), SetBrightness (20), SetShadeColor (21), SetAlpha (51), SetPartAlpha (127, dead), SetShadeCoeff
(136), SetIdShade (96), SetLightOverrideMask and its getter and bit form (106 to 108), SetTexListBFrame
and SetTexListAFrame (90, 91), ResetTexListB (113), SetTexAnimPattern (120), SetTexAnimSlotByteE (141).

### 4.5 Scale and attachment (9 rows)

SetScale (6), SetScaleMasked (7), SetAttachScale (110), SetTiltX (109), AttachToParentJoint (44),
AttachToParentBone (45), AttachToParentOffset (82), SetSecondaryPhysics (130), SetSecondaryVerticalSway
(142). The carry command (44, 45, 82) lands in CHR `m_parent`, so a replicated script reproduces it.

### 4.6 Head, look at and bone readback (15 rows)

SetLookAtEnabled (121), LookAtActor (122), LookAtPoint (123), LookAtCamera (124), LookAtAngles (125),
SetLookAtBlendFrames (132), SetHeadSwayEnabled (118, 126), GetHeadSwayEnabled (119, dead), and the
readback rows GetJointPosToVars (38), GetJointPointToVars (39, dead), GetBonePosToVars (54),
GetBone0PosToVars (55), GetRootPointToVars (56, dead), GetBoneJointIndex (83, dead). The look at and
the sway both run in `FFX_Ch_UpdateRenderJob 0x833530`, section 5.1.

### 4.7 Unread flags and no ops (11 rows)

SetFlags1Bit1 (86, 87, 88), SetFlags1Bit8 (134, 135), SetFlags1Bit80000 (103), SetFlags2Bit8 (131),
SetUnreadFloat (10, dead), PopAndIgnore (35, 36, dead), Nop (43, dead). Bit 0x1's only reader is
`FFX_Ch_SetFlags1Bit8 0x835710` and `FFX_Ch_SetFlags1Bit10 0x835730`, which refuse to set their bit
while 0x1 is set. No reader of bits 0x8, 0x80000 or flags2 0x8 turned up in a decompile grep over
1,970 functions of the CHR, map and ATEL ranges. ch:135 is called 1,969 times in 292 packages, so a
reader probably exists outside those ranges. [I]

## 5. Lockstep hazards

The flags in the table: `rng` (a random draw), `render` (the camera or the render job's pose),
`load` (waits on or performs a disk read), `local` (a per machine value such as a raw pointer or
uninitialised stack), `global` (a one slot engine global or a hard coded map, scene or chr case). A
trailing `*` means the flag came from the call graph or a reader, not from the handler body itself.
There are no `clock` or `input` flags in this library.

### 5.1 The render job: CRT rand and the camera

`FFX_Ch_UpdateRenderJob 0x833530` runs per CHR on Phyre pool worker threads. It is posted at the end of
`FFX_Ch_UpdateMotionAll` and joined at the next pass 4. Per CHR it runs `FFX_Ch_UpdateBlink`,
`FFX_Ch_UpdateLookAt`, the skin matrices, `FFX_Ch_UpdateBonePositions` (which writes
`m_boneWorldPos`, CHR +0x524) and the idshade. [V]

- **ch:118 and 126 SetHeadSwayEnabled** set `m_blinkState[0]`, and with it set `UpdateBlink` sways
  bone 13 from the CRT `rand()`. [V] The CRT state is per thread (MAGIC_DLL.md), and which pool thread
  runs which CHR's job can change from frame to frame, so the sway is not reproducible even on one
  machine. [I] It does not disturb the main thread's `rand()`. 126 is the fifth heaviest row, 16,838
  calls.
- **ch:124 LookAtCamera** sets look at kind 1, and `UpdateLookAt` aims the head at
  `FFX_PlayerCam_GetEyePos`, the local camera eye. [V]
- **ch:38, 54 and 55** copy joint or bone world positions into script variables. Those positions come
  out of the render job, so they carry the sway and the camera look at if the bone is in the head
  chain. [I] They are rare (11, 12 and 5 calls). A port should compute them from the simulated pose
  only, or check that the bones those scripts read are outside the head chain.

### 5.2 Disk [V]

- Waits: ch:79 WaitChrDataRead (997 calls), 99 WaitMotionSetRead (343), 102 ReadMotionGroupSync (97)
  and 104 WaitRomIdle (1,864). Disk speed decides the step they finish on. 104 is the widest: its poll
  is `FFX_Ch_RomPump(1) == 0` at 0xA795D0, true only when none of the 64 ROM slots has a read pending,
  whichever subsystem queued it.
- ch:79 waits for ever if the data is absent (state 2), the same as the original.
- Starts with no wait: 78, 97, 101.
- In step loads: ch:1 and 16 run `FFX_Ch_RomPump(0)`, which blocks until every slot is idle, inside the
  getter. The step stalls but finishes in the same place on every peer.
- Dead: 98 and 100 with `noWait` hand the load state (0, 1 or 2) to the script, which would be a
  direct divergence.

### 5.3 Single slot globals and hard coded cases [V]

- The chSetMotion helper (25, 26, 65 to 70, 137 to 140) reads `g_ffxLiveSceneId` (scenes 205, 364,
  17) and `FFX_Story_GetProgress` (2085, 1176, 200) to patch `m_jointComposerMode` for some chr and
  motion pairs, and sets `maybe_g_ffxC102SkinHackFlag 0x13008E0` when chr 102 plays. Both inputs are
  simulation state, so it replicates, but the flag is one slot.
- ch:8 and 13 go through `FFX_Ch_SetHideBit1 0x82B2C0`, which forces the hide bit to 0 for chr 0x108A
  when `sub_782970()` returns 0x2370000 and `FFX_Level_GetNameHash()` is 0x21E8. `sub_782970` returns
  0 unless `g_ffxBtlSubPhase` is set, and otherwise packs a battle word and byte, so this is one
  battle on one map.
- ch:84 SetPartHidden on model f117 feeds the four entry `g_ffxF117HideCount` ring in
  `FFX_Ch_ModelSetHide 0x82AEA0`, cleared by the field map load and read by `FFX_Ch_StepAll`.
- ch:30's counter `dword_22FB4D8` only runs when `dword_12FB80C == 1`, and nothing writes that word, so
  it never runs.
- The All rows, 77, 88 and 135, walk every in use CHR. A spawned co-op body is in that array and gets
  the change too. Usually that is right, but it is a place the body can be touched by a script that
  never names it.

### 5.4 Machine local state [V]

- ch:73 WaitMotionDoneOf stores the other actor's raw CHR pointer in the thread work slot
  (0xA785F0 to 0xA78610). Harmless for behaviour, but a hash over thread state has to skip or
  translate it.
- ch:123 LookAtPoint copies the fourth word of `m_lookAtTarget` from an uninitialised stack slot
  (`[ebp+var_8]`, 0xA7A046 to 0xA7A04B). `FFX_Ch_UpdateLookAt` never reads it, but a CHR hash would see
  garbage there.

### 5.5 Port note: conditional pops [V]

ch:111, 115, 118, 129, 130, 131, 143 and 144 call `FFX_Atel_PopInt` only inside `if (chr)`. From an
actor without a CHR the argument stays on the stack. `ATEL_CONTEXT.md` says RunScript only leaves with
an empty stack and an end with a value left keeps fetching the same end, so such a call in the
original would hang the thread at its end. A port must reproduce the pop pattern, not tidy it.

ch:3 WaitMotionDone returns 0 for ever when the actor has no CHR. ch:105 SetMotionStepScaled is not a
hazard: `g_ffxStepScale88` is derived from the 60 Hz game clock, or the authored timing track in pacing
mode 1, both deterministic (MAIN_LOOP.md, CUTSCENE.md).

No handler in the closure reads a pad, the wall clock, the language or the FMV state.

## 6. The apply script

Applied to the live IDB on 2026-10-26: 113 slots renamed, then `maybe_chReadSystemMGRPFinish` and
`maybe_FFX_EvScript_chSetGravMode` lost their prefix (both bodies confirmed). Live result: 125 table slots
named, 35 positional, 0 placeholders. The doc and script now use the unprefixed names.

`../tools/apply_ch_names.py`, modelled on `apply_core_names.py`. It reads the table out of the IDB and
names each slot `FFX_AtelSys_Ch_NNN_<Role>_<slot>`. Its `NAMES` block holds 134 rows, 103 used and 31
dead. The other nine used rows (12, 23, 31, 37, 40, 92, 93, 94, 101) already carry descriptive names
on every slot, and 18 and 28 are null. It refuses any slot whose current name is not positional for
that row (`FFX_AtelSys_Ch_NNN_<slot>` with or without the zero padding, or `FFX_AtelSys_Ch_NNN` on a
single slot row), a `sub_`/`loc_`/`nullsub_` placeholder, or one of the two known misnames on the
polls of 1 and 16. It skips a slot that is not a function start, a function shared between slots and
a name already taken, leaves dead rows positional unless `--include-dead`, and never saves. Dry run by
default.

Dry run on the scratch copy, 2026-10-26: 113 slots to rename, 3 skipped (16 start
`chReadSystemMGRP`, 16 resi `chReadSystemMGRPFinish` and 102 poll `chReadMotionGroupSync`,
each kept because it is descriptive). With `--include-dead`: 148 to rename, the same 3 skipped. Every
slot target is a function start and none is shared.

## 7. Open

- A reader for m_flags1 0x8 (ch:134 and 135, 1,984 calls), 0x80000 (103) and m_flags2 0x8 (131).
  The decompile grep covered 0x820000 to 0x848000, 0x77C000 to 0x782000 and 0x861000 to 0x880000.
- ch:130 SetSecondaryPhysics is read off CHARACTER_MOTION 4.5 and ffx_types, not traced to the
  secondary physics gate itself. [I]
- ch:112 IsSkipRender goes through `maybe_FFX_Ch_IsSkipRender`, not read. [I]
- The second pushed argument of the chSetMotion helper.
- Which bones the ch:38, 54 and 55 scripts read, to settle whether the sway or the camera look at can
  reach a script variable (5.1).
- Whether the CRT `rand()` head sway can be pinned to a per CHR seed in a port without a visible
  change.

## 8. The table

Columns: ch index, call sites, packages, src (e event packages, the only source with Ch calls), args in
push order, result, blk (yes waits in a poll, stall loads inside the step), name (the proposed name
per slot, `kept` for a slot that keeps its current name, `dead` for an unused row, `null` for an empty
row), meaning, lockstep, mark.

| ch | uses | pkgs | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 38040 | 264 | e | motionId:int | int 0 | no | `FFX_AtelSys_Ch_000_SetMotionKey_resi` | FFX_Ch_SetMotionKey(self, motionId): requests motion key motionId on the actor's CHR, the plain play motion call |  | V |
| 1 | 643 | 366 | e | group:int | int 0 | stall | `FFX_AtelSys_Ch_001_LoadEventMotionGroupSync_start`, `FFX_AtelSys_Ch_001_LoadEventMotionGroupSync_poll`, `FFX_AtelSys_Ch_001_LoadEventMotionGroupSync_resi` | start stores the group, poll is a `return 5` stub, the getter calls FFX_Mot_LoadEventMotionGroupSync, which loads inside the step through FFX_Ch_RomPump(0) | load | V |
| 2 | 0 | 0 |  | motionId:int | int 0 | no | dead `FFX_AtelSys_Ch_002_SetMotionKey_resi` | same body as ch:0 |  | V |
| 3 | 12465 | 210 | e |  | none | yes | `FFX_AtelSys_Ch_003_WaitMotionDone_start`, `FFX_AtelSys_Ch_003_WaitMotionDone_poll` | start resolves the CHR (result ignored), poll returns 0 while CHR +0x736 m_motionRequestActive is set, else 5. With no CHR it returns 0 for ever |  | V |
| 4 | 0 | 0 |  |  | int m_motionRequestActive != 0 | no | dead `FFX_AtelSys_Ch_004_IsMotionActive_resi` | same body as ch:31 |  | V |
| 5 | 6376 | 281 | e |  | int 0 | no | `FFX_AtelSys_Ch_005_StopMotion_resi` | FFX_Ch_ResetBlock730: m_i730 = 1, m_motionRequestActive = 0, m_clipPlaying = 0 |  | V |
| 6 | 441 | 59 | e | s:float | int 0 | no | `FFX_AtelSys_Ch_006_SetScale_resi` | FFX_Ch_SetScaleUniform |  | V |
| 7 | 66 | 14 | e | mask:int, x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Ch_007_SetScaleMasked_resi` | FFX_Ch_SetScaleMasked(mask, x, y, z), a per axis scale |  | V |
| 8 | 10938 | 315 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_008_SetEventHidden_resi` | FFX_Ch_SetHideBit1: m_hideFlags bit 0x01 (Ev). Forced to 0 for chr 0x108A when sub_782970() == 0x2370000 and the level name hash is 0x21E8 | global | V |
| 9 | 4084 | 259 | e | w:float | float old value | no | `FFX_AtelSys_Ch_009_SetPushWeight_resf` | CHR +0x500 m_pushWeight, the share of a body overlap this CHR takes in FFX_Ch_SeparatePair |  | V |
| 10 | 0 | 0 |  | v:float | int 0 | no | dead `FFX_AtelSys_Ch_010_SetUnreadFloat_resi` | writes CHR +0x4F8, which nothing reads except FFX_Ch_CopyState |  | I |
| 11 | 253 | 53 | e | y:float | int 0 | no | `FFX_AtelSys_Ch_011_SetGroundHeight_resi` | FFX_Ch_SetGroundHeight |  | V |
| 12 | 4 | 4 | e | ?:int | int 0 | no | kept `FFX_EvScript_chEnGravity` | Prints a not supported warning and does nothing |  | V |
| 13 | 142 | 10 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_013_SetEventHidden_resi` | same body as ch:8 | global | V |
| 14 | 3631 | 286 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_014_SetFreeMove_resi` | FFX_Ch_SetFreeMove: m_flags1 0x80 = on. Set is free move, pos += vel with no walkmesh or ground clamp, ground normal forced to (0,-1,0) and idshade 0x40000 cleared. Clear follows the walkmesh and sets idshade |  | V |
| 15 | 9680 | 313 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_015_SetNoBodyCollision_resi` | FFX_Ch_SetFlags1Bit100: m_flags1 0x100 = on. Set takes the CHR out of g_ffxChrCollideList |  | V |
| 16 | 3511 | 304 | e | chrId:int, set:int | int 0 | stall | kept `chReadSystemMGRP`, `FFX_AtelSys_Ch_016_ReadSystemMGRP_poll`, kept `chReadSystemMGRPFinish` | kept `chReadSystemMGRP` start and `chReadSystemMGRPFinish` getter, the poll is a `return 5` stub. The getter runs FFX_Ch_LoadMotionSetSync(chrId, set), a sync load through FFX_Ch_RomPump(0) | load | V |
| 17 | 0 | 0 |  | set:int | int 0 | no | dead `FFX_AtelSys_Ch_017_DisposeOwnMotionSet_resi` | FFX_Ch_MotionSetDispose(self->m_id, set) |  | V |
| 18 | 0 | 0 |  |  |  |  | null | all four slots are null in the table |  | V |
| 19 | 405 | 51 | e | mode:int | int 0 | no | `FFX_AtelSys_Ch_019_SetMotionMode_resi` | CHR +0x185 and FFX_Ch_SetLocomotionMode: 0 field, 1 field battle, 2 swim, 3 swim battle |  | V |
| 20 | 92 | 12 | e | value:float, mode:int | int 0 | no | `FFX_AtelSys_Ch_020_SetBrightness_resi` | FFX_Ch_AnimFloatSet on m_animBrightness (CHR +0x360), mode picks snap or ramp |  | V |
| 21 | 513 | 20 | e | r:float, g:float, b:float, mode:int | int 0 | no | `FFX_AtelSys_Ch_021_SetShadeColor_resi` | shade colour, each channel clamped to 1.3 |  | V |
| 22 | 801 | 102 | e | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Ch_022_SetOffset_resi` | FFX_Ch_SetOffset(x, y, z) |  | V |
| 23 | 2992 | 213 | e | mode:int | int 0 | no | kept `FFX_EvScript_chSetGravMode` | FFX_Ch_SetGroundMode: 0 clamp, 1 snap, 2 water |  | V |
| 24 | 206 | 7 | e | v:int | int 0 | no | `FFX_AtelSys_Ch_024_SetSpeedScale_resi` | FFX_Ch_SetSpeedMulA: m_speedMulA, 8.8 fixed point |  | V |
| 25 | 2 | 2 | e | motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | `FFX_AtelSys_Ch_025_SetMotionTbl_resi` | FFX_EvScript_chSetMotion helper in table mode, start and end are indexes into the motion's frame table | global | V |
| 26 | 11439 | 278 | e | motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | `FFX_AtelSys_Ch_026_SetMotionFrames_resi` | FFX_EvScript_chSetMotion helper with explicit frames. The helper patches m_jointComposerMode for some chr and motion pairs by scene (205, 364, 17) and story progress (2085, 1176, 200), and sets 0x13008E0 for chr 102 | global | V |
| 27 | 5195 | 284 | e | motionId:int | int 0 | no | `FFX_AtelSys_Ch_027_SetMotionKey_resi` | same body as ch:0 |  | V |
| 28 | 0 | 0 |  |  |  |  | null | all four slots are null in the table |  | V |
| 29 | 0 | 0 |  | motionId:int | int 0 | no | dead `FFX_AtelSys_Ch_029_SetMotionKey_resi` | same body as ch:0 |  | V |
| 30 | 3657 | 205 | e |  | none | yes | `FFX_AtelSys_Ch_030_WaitMotionDone_start`, `FFX_AtelSys_Ch_030_WaitMotionDone_poll` | empty start, the poll is ch:3's plus a counter dword_22FB4D8 that only runs when dword_12FB80C == 1, and nothing writes that |  | V |
| 31 | 15 | 6 | e |  | int m_motionRequestActive != 0 | no | kept `FFX_EvScript_chIsMotionActive` | the existing name matches the body |  | V |
| 32 | 1572 | 173 | e |  | int 0 | no | `FFX_AtelSys_Ch_032_StopMotion_resi` | same body as ch:5 |  | V |
| 33 | 1660 | 174 | e | v:int | int 0 | no | `FFX_AtelSys_Ch_033_SetSpeedScale_resi` | same body as ch:24 |  | V |
| 34 | 1544 | 170 | e | v:int | int 0 | no | `FFX_AtelSys_Ch_034_SetSeqWaitKey_resi` | CHR +0x737 m_seqWaitKey, the key a motion sequence waits on |  | V |
| 35 | 0 | 0 |  | v:int | int 0 | no | dead `FFX_AtelSys_Ch_035_PopAndIgnore_resi` | pops one value and does nothing |  | V |
| 36 | 0 | 0 |  | v:int | int 0 | no | dead `FFX_AtelSys_Ch_036_PopAndIgnore_resi` | pops one value and does nothing |  | V |
| 37 | 23505 | 206 | e | slot:int, value:int | int 0 | no | kept `FFX_EvScript_chSetMotionSlot` | the existing name matches the body |  | V |
| 38 | 11 | 2 | e | actorId:int, joint:int, xVar:int, yVar:int, zVar:int | int 0 | no | `FFX_AtelSys_Ch_038_GetJointPosToVars_resi` | maybe_FFX_Ch_GetJointMatrix and writes the joint world position into three script variables | render* | V |
| 39 | 0 | 0 |  | actorId:int, joint:int, ox:float, oy:float, oz:float, xVar:int, yVar:int, zVar:int | int 0 | no | dead `FFX_AtelSys_Ch_039_GetJointPointToVars_resi` | as ch:38 with an offset point transformed by the joint | render* | V |
| 40 | 97 | 12 | e | v:float | float | no | kept `FFX_EvScript_chSetRunThreshold` | the existing name matches the body |  | V |
| 41 | 811 | 142 | e | v:int | int old value | no | `FFX_AtelSys_Ch_041_SetShadowKind_resi` | CHR +0x183, read by FFX_Ch_BuildBlobShadow and FFX_Ch_ShadowPassAll. FFX_Ch_StepAll overrides it for some level and chr ids |  | V |
| 42 | 2836 | 265 | e | on:int | int old bit | no | `FFX_AtelSys_Ch_042_SetZClipEnabled_resi` | m_flags1 0x800 = on. Clear means the CHR is never distance culled |  | V |
| 43 | 0 | 0 |  |  | int 0 | no | dead `FFX_AtelSys_Ch_043_Nop_resi` | resolves the CHR and returns 0, no pop |  | V |
| 44 | 16 | 9 | e | actorId:int, joint:int | int 0 | no | `FFX_AtelSys_Ch_044_AttachToParentJoint_resi` | FFX_Ch_AttachToParentJoint, actorId -1 detaches. The carry command |  | V |
| 45 | 1071 | 110 | e | actorId:int, bone:int | int 0 | no | `FFX_AtelSys_Ch_045_AttachToParentBone_resi` | as ch:44 by bone id |  | V |
| 46 | 0 | 0 |  |  | int ground type | no | dead `FFX_AtelSys_Ch_046_GetGroundType_resi` | FFX_Map_GroundTypeOf on the CHR's triangle |  | V |
| 47 | 0 | 0 |  | actorId:int | int ground type | no | dead `FFX_AtelSys_Ch_047_GetGroundTypeOf_resi` | as ch:46 for another actor |  | V |
| 48 | 0 | 0 |  |  | int sound code | no | dead `FFX_AtelSys_Ch_048_GetGroundSound_resi` | FFX_Map_GroundAttrGetSnd on the CHR's triangle |  | V |
| 49 | 0 | 0 |  | actorId:int | int sound code | no | dead `FFX_AtelSys_Ch_049_GetGroundSoundOf_resi` | as ch:48 for another actor |  | V |
| 50 | 1292 | 41 | e |  | int 0 | no | `FFX_AtelSys_Ch_050_SetDefaultMotion_resi` | plays slot 0 of the current locomotion mode (SetMotionFromModeSlot0) |  | V |
| 51 | 1431 | 157 | e | value:float, mode:int | int 0 | no | `FFX_AtelSys_Ch_051_SetAlpha_resi` | FFX_Ch_AnimFloatSet on m_animShadeA |  | V |
| 52 | 30659 | 170 | e | frames:int | int 0 | no | `FFX_AtelSys_Ch_052_SetBlendFrames_resi` | FFX_Ch_SetBlendFrames |  | V |
| 53 | 4 | 2 | e |  | int frames | no | `FFX_AtelSys_Ch_053_GetMotionLength_resi` | FFX_Mot_SeqTotalFrames of the current motion |  | V |
| 54 | 12 | 1 | e | actorId:int, bone:int, xVar:int, yVar:int, zVar:int | int 0 | no | `FFX_AtelSys_Ch_054_GetBonePosToVars_resi` | sub_A7A980 with a4 = 0: the bone world position from m_boneWorldPos into three script variables. Only test23 calls it | render* | V |
| 55 | 5 | 4 | e | actorId:int, xVar:int, yVar:int, zVar:int | int 0 | no | `FFX_AtelSys_Ch_055_GetBone0PosToVars_resi` | as ch:54 for bone 0 | render* | V |
| 56 | 0 | 0 |  | actorId:int, xVar:int, yVar:int, zVar:int | int 0 | no | dead `FFX_AtelSys_Ch_056_GetRootPointToVars_resi` | CHR +0x6B4 m_rootPoint into three script variables |  | V |
| 57 | 549 | 172 | e | v:float | float old value | no | `FFX_AtelSys_Ch_057_SetCollisionRadiusScale_resf` | FFX_Ch_SetCollisionRadiusScale |  | V |
| 58 | 221 | 35 | e | z:float | float old value / 100 | no | `FFX_AtelSys_Ch_058_SetClipZ_resf` | CHR +0x174 m_clipZ = z * 100 |  | V |
| 59 | 91 | 21 | e | chrId:int, set:int | int 0 | no | `FFX_AtelSys_Ch_059_DisposeMotionSet_resi` | FFX_Ch_MotionSetDispose, only when chrId is not the actor's own CHR |  | V |
| 60 | 139 | 56 | e | group:int | int 0 | no | `FFX_AtelSys_Ch_060_ReleaseEventMotionGroup_resi` | FFX_Mot_ReleaseEventMotionGroup |  | V |
| 61 | 512 | 79 | e | chrId:int | int 0 | no | `FFX_AtelSys_Ch_061_DisposeChrData_resi` | disposes the CHR data of chrId, assert flag 1 |  | V |
| 62 | 367 | 30 | e | chrId:int, assertFlag:int | int 0 | no | `FFX_AtelSys_Ch_062_DisposeChrDataEx_resi` | as ch:61 with the assert flag from the script |  | V |
| 63 | 35 | 1 | e | motionId:int | int 0 | no | `FFX_AtelSys_Ch_063_SetMotionKey_resi` | same body as ch:0 |  | V |
| 64 | 1569 | 66 | e | motionId:int | int 0 | no | `FFX_AtelSys_Ch_064_SetMotionKey_resi` | same body as ch:0 |  | V |
| 65 | 0 | 0 |  | motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | dead `FFX_AtelSys_Ch_065_SetMotionTbl_resi` | same helper call as ch:25 | global | V |
| 66 | 1116 | 58 | e | motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | `FFX_AtelSys_Ch_066_SetMotionFrames_resi` | same helper call as ch:26 | global | V |
| 67 | 0 | 0 |  | actorId:int, motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | dead `FFX_AtelSys_Ch_067_SetMotionTblOf_resi` | ch:25 on another actor | global | V |
| 68 | 216 | 20 | e | actorId:int, motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | `FFX_AtelSys_Ch_068_SetMotionFramesOf_resi` | ch:26 on another actor | global | V |
| 69 | 0 | 0 |  | actorId:int, motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | dead `FFX_AtelSys_Ch_069_SetMotionTblOf_resi` | same helper call as ch:67 | global | V |
| 70 | 12 | 2 | e | actorId:int, motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | `FFX_AtelSys_Ch_070_SetMotionFramesOf_resi` | same helper call as ch:68 | global | V |
| 71 | 332 | 35 | e | actorId:int, motionId:int | int 0 | no | `FFX_AtelSys_Ch_071_SetMotionKeyOf_resi` | ch:0 on another actor |  | V |
| 72 | 18 | 2 | e | actorId:int, motionId:int | int 0 | no | `FFX_AtelSys_Ch_072_SetMotionKeyOf_resi` | same body as ch:71 |  | V |
| 73 | 64 | 20 | e | actorId:int | none | yes | `FFX_AtelSys_Ch_073_WaitMotionDoneOf_start`, `FFX_AtelSys_Ch_073_WaitMotionDoneOf_poll` | start stores the other actor's raw CHR pointer in the thread work slot, poll waits on its +0x736 | local | V |
| 74 | 4 | 1 | e | actorId:int | int m_motionRequestActive != 0 | no | `FFX_AtelSys_Ch_074_IsMotionActiveOf_resi` | ch:31 on another actor |  | V |
| 75 | 4 | 1 | e | actorId:int | int 0 | no | `FFX_AtelSys_Ch_075_StopMotionOf_resi` | ch:5 on another actor |  | V |
| 76 | 212 | 34 | e | actorId:int, v:int | int 0 | no | `FFX_AtelSys_Ch_076_SetSpeedScaleOf_resi` | ch:24 on another actor |  | V |
| 77 | 14 | 7 | e | v:float | float 0 | no | `FFX_AtelSys_Ch_077_SetCollisionRadiusScaleAll_resf` | ch:57 on every live CHR | global | V |
| 78 | 1776 | 153 | e | chrId:int | int FFX_Ch_RomRead result | no | `FFX_AtelSys_Ch_078_ChrDataReadStart_resi` | starts the async read of CHR data chrId | load | V |
| 79 | 997 | 96 | e | chrId:int | int work slot +12 | yes | `FFX_AtelSys_Ch_079_WaitChrDataRead_start`, `FFX_AtelSys_Ch_079_WaitChrDataRead_poll`, `FFX_AtelSys_Ch_079_WaitChrDataRead_resi` | poll returns 1 once FFX_Ch_DataReadSync says loaded, waits for ever if the data is absent | load | V |
| 80 | 515 | 32 | e | actorId:int, v:int | int 0 | no | `FFX_AtelSys_Ch_080_SetSeqWaitKeyOf_resi` | ch:34 on another actor |  | V |
| 81 | 2487 | 32 | e |  | int frame | no | `FFX_AtelSys_Ch_081_GetMotionFrame_resi` | CHR +0x740 m_frame via sub_839860, expression form only |  | V |
| 82 | 1 | 1 | e | actorId:int, joint:int, ox:float, oy:float, oz:float | int 0 | no | `FFX_AtelSys_Ch_082_AttachToParentOffset_resi` | as ch:44 with an offset |  | V |
| 83 | 0 | 0 |  | actorId:int, boneId:int | int joint | no | dead `FFX_AtelSys_Ch_083_GetBoneJointIndex_resi` | sub_833BF0 scans the CHRDATA bone point table (blob +32, count +36, 16 byte records, key & 0x3FFF) for the joint index |  | V |
| 84 | 349 | 57 | e | part:int, hide:int | int old value | no | `FFX_AtelSys_Ch_084_SetPartHidden_resi` | FFX_Ch_ModelSetHide. For model f117 it also feeds the 4 entry g_ffxF117HideCount ring, cleared by the field map load and read by FFX_Ch_StepAll | global | V |
| 85 | 0 | 0 |  | part:int | int hidden | no | dead `FFX_AtelSys_Ch_085_GetPartHidden_resi` | FFX_Ch_ModelGetHide |  | V |
| 86 | 378 | 70 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_086_SetFlags1Bit1_resi` | m_flags1 0x1 = on. Its only reader is FFX_Ch_SetFlags1Bit8 and Bit10, which refuse to set their bit while it is set |  | V |
| 87 | 1637 | 126 | e | actorId:int, on:int | int 0 | no | `FFX_AtelSys_Ch_087_SetFlags1Bit1Of_resi` | ch:86 on another actor |  | V |
| 88 | 227 | 97 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_088_SetFlags1Bit1All_resi` | ch:86 on every in use CHR | global | V |
| 89 | 0 | 0 |  | mode:int, index:int | int 0 | no | dead `FFX_AtelSys_Ch_089_SetMotionByModeIndex_resi` | FFX_Mot_SetByModeIndex(mode, index) |  | V |
| 90 | 4 | 3 | e | index:int, frame:int | int 0 | no | `FFX_AtelSys_Ch_090_SetTexListBFrame_resi` | sub_8313C0: sets a texture list B entry's frame, index -1 means all |  | V |
| 91 | 0 | 0 |  | index:int, frame:int | int 0 | no | dead `FFX_AtelSys_Ch_091_SetTexListAFrame_resi` | sub_831390, the list A twin of ch:90 |  | V |
| 92 | 30539 | 217 | e | v:int | int 0 | no | kept `FFX_EvScript_chSetNextHokan` | FFX_Ch_SetNextHokan, the interpolation (hokan) length into the next motion |  | V |
| 93 | 1021 | 12 | e |  | int motion id | no | kept `FFX_EvScript_chGetMotion` | FFX_Mot_GetMotionId |  | V |
| 94 | 22 | 3 | e |  | float move speed | no | kept `FFX_EvScript_chGetMoveSpeed` | the existing name matches the body |  | V |
| 95 | 144 | 41 | e | n:int | int 0 | no | `FFX_AtelSys_Ch_095_SetPendingLoopCount_resi` | FFX_Mot_SetPendingLoopCount, CHR +0x726 |  | V |
| 96 | 47 | 15 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_096_SetIdShade_resi` | FFX_Ch_SetIdShade: m_flags1 0x40000 = on, read by FFX_Ch_UpdateShadeFromLights |  | V |
| 97 | 898 | 92 | e | chrId:int, set:int | int 0 | no | `FFX_AtelSys_Ch_097_MotionSetReadStart_resi` | starts the async read of a motion set | load | V |
| 98 | 0 | 0 |  | chrId:int, set:int, noWait:int | int load state | yes | dead `FFX_AtelSys_Ch_098_MotionSetReadSync_start`, dead `FFX_AtelSys_Ch_098_MotionSetReadSync_poll`, dead `FFX_AtelSys_Ch_098_MotionSetReadSync_resi` | with noWait the load state goes to the result, else it waits | load | V |
| 99 | 343 | 51 | e | chrId:int, set:int | int work slot +12 | yes | `FFX_AtelSys_Ch_099_WaitMotionSetRead_start`, `FFX_AtelSys_Ch_099_WaitMotionSetRead_poll`, `FFX_AtelSys_Ch_099_WaitMotionSetRead_resi` | waits until FFX_Ch_MotionSetReadSync says loaded | load | V |
| 100 | 0 | 0 |  | chrId:int, noWait:int | int load state | yes | dead `FFX_AtelSys_Ch_100_ChrDataReadSync_start`, dead `FFX_AtelSys_Ch_100_ChrDataReadSync_poll`, dead `FFX_AtelSys_Ch_100_ChrDataReadSync_resi` | the CHR data twin of ch:98 | load | V |
| 101 | 100 | 44 | e | group:int | int 0 | no | kept `chReadMotionGroupStart` | the existing name matches the body | load | V |
| 102 | 97 | 44 | e | group:int | int work slot +12 | yes | `FFX_AtelSys_Ch_102_ReadMotionGroupSync_start`, kept `chReadMotionGroupSync`, `FFX_AtelSys_Ch_102_ReadMotionGroupSync_resi` | start stores the group, the poll (kept `chReadMotionGroupSync`) waits while the group is reading | load | V |
| 103 | 7 | 3 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_103_SetFlags1Bit80000_resi` | m_flags1 0x80000 = on, no reader found |  | I |
| 104 | 1864 | 111 | e |  | none | yes | `FFX_AtelSys_Ch_104_WaitRomIdle_poll` | poll only, FFX_Ch_RomPump(1) == 0: waits until no ROM slot of any subsystem has a read pending | load | V |
| 105 | 26 | 10 | e | on:int | int old bit | no | `FFX_AtelSys_Ch_105_SetMotionStepScaled_resi` | m_flags1 0x100000 = on. FFX_Mot_AdvanceFrame scales the frame step by g_ffxStepScale88 while g_ffxMotKeepSpeed is set |  | V |
| 106 | 1 | 1 | e | v:int | int 0 | no | `FFX_AtelSys_Ch_106_SetLightOverrideMask_resi` | sub_830AE0: CHR +0x188 m_lightOverrideMask |  | V |
| 107 | 0 | 0 |  |  | int mask | no | dead `FFX_AtelSys_Ch_107_GetLightOverrideMask_resi` | sub_830AD0 |  | V |
| 108 | 0 | 0 |  | set:int, clear:int | int 0 | no | dead `FFX_AtelSys_Ch_108_SetLightOverrideMaskBits_resi` | sets and clears bits of +0x188 |  | V |
| 109 | 98 | 11 | e | angleDeg:float, stepDeg:float | int 0 | no | `FFX_AtelSys_Ch_109_SetTiltX_resi` | m_tiltXTarget and m_tiltXStep (+0x160, +0x164), step 0 snaps m_tiltX |  | V |
| 110 | 10 | 9 | e | s:float | int 0 | no | `FFX_AtelSys_Ch_110_SetAttachScale_resi` | CHR +0x1B4, the scale applied while attached to a parent |  | V |
| 111 | 236 | 87 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_111_SetUpdateWhileHidden_resi` | CHR +0x181: a hidden CHR keeps stepping and colliding. Pops only when a CHR exists |  | V |
| 112 | 0 | 0 |  |  | int byte | no | dead `FFX_AtelSys_Ch_112_IsSkipRender_resi` | maybe_FFX_Ch_IsSkipRender |  | I |
| 113 | 4 | 3 | e | ?:int | int 0 | no | `FFX_AtelSys_Ch_113_ResetTexListB_resi` | pops one value it ignores and calls sub_831300, the texture list B reset |  | V |
| 114 | 9 | 5 | e |  | int triangle | no | `FFX_AtelSys_Ch_114_GetWalkmeshTri_resi` | FFX_Ch_GetWalkmeshTri, CHR +0x824 |  | V |
| 115 | 9 | 4 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_115_SetHeightCollision_resi` | m_flags1 0x40000000 = on, read by FFX_Ch_ResolveCollisionsAll. Pops only when a CHR exists |  | V |
| 116 | 0 | 0 |  |  | int bit | no | dead `FFX_AtelSys_Ch_116_GetHeightCollision_resi` | reads m_flags1 0x40000000 |  | V |
| 117 | 804 | 24 | e | actorId:int | int frame | no | `FFX_AtelSys_Ch_117_GetMotionFrameOf_resi` | ch:81 on another actor, expression form only |  | V |
| 118 | 689 | 44 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_118_SetHeadSwayEnabled_resi` | CHR +0x4D4 m_blinkState[0], which turns on the random head sway on bone 13 in FFX_Ch_UpdateBlink. Pops only when a CHR exists | rng* | V |
| 119 | 0 | 0 |  |  | int | no | dead `FFX_AtelSys_Ch_119_GetHeadSwayEnabled_resi` | sub_834740, reads m_blinkState[0] |  | V |
| 120 | 292 | 59 | e | a:int, b:int | int 0 | no | `FFX_AtelSys_Ch_120_SetTexAnimPattern_resi` | FFX_Ch_SetTexAnimPattern(a, b) |  | V |
| 121 | 333 | 82 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_121_SetLookAtEnabled_resi` | sub_834F60: CHR +0x40C m_lookAtEnabled |  | V |
| 122 | 315 | 79 | e | actorId:int | int 0 | no | `FFX_AtelSys_Ch_122_LookAtActor_resi` | look at kind 2, another CHR |  | V |
| 123 | 34 | 18 | e | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Ch_123_LookAtPoint_resi` | look at kind 3. The fourth target word is copied from an uninitialised stack slot (0xA7A04B), but FFX_Ch_UpdateLookAt never uses it | local | V |
| 124 | 3 | 3 | e |  | int 0 | no | `FFX_AtelSys_Ch_124_LookAtCamera_resi` | look at kind 1, the eye from FFX_PlayerCam_GetEyePos | render | V |
| 125 | 53 | 22 | e | aDeg:float, bDeg:float | int 0 | no | `FFX_AtelSys_Ch_125_LookAtAngles_resi` | look at kind 4, yaw and Z angles |  | V |
| 126 | 16838 | 218 | e | actorId:int, on:int | int 0 | no | `FFX_AtelSys_Ch_126_SetHeadSwayEnabledOf_resi` | ch:118 on another actor | rng* | V |
| 127 | 0 | 0 |  | part:int, alpha:float | int 0 | no | dead `FFX_AtelSys_Ch_127_SetPartAlpha_resi` | FFX_Ch_ModelSetAlpha(part, alpha) |  | V |
| 128 | 14 | 4 | e | h:float | int 0 | no | `FFX_AtelSys_Ch_128_SetCollisionHeight_resi` | CHR +0x51C m_collisionHeight |  | V |
| 129 | 53 | 10 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_129_SetSurface13Blocked_resi` | m_flags2 0x4 = on: FFX_Map_IsTriBlockedForChr treats walkmesh surface code 13 as a wall. Pops only when a CHR exists |  | V |
| 130 | 12 | 5 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_130_SetSecondaryPhysics_resi` | m_flags1 0x400000 = on, half of the secondary physics gate with 0x200000. Pops only when a CHR exists |  | I |
| 131 | 8 | 4 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_131_SetFlags2Bit8_resi` | m_flags2 0x8 = on, no reader found. Pops only when a CHR exists |  | I |
| 132 | 97 | 19 | e | f:float | int 0 | no | `FFX_AtelSys_Ch_132_SetLookAtBlendFrames_resi` | CHR +0x420 m_lookAtBlendFrames, 0 means 6.0 |  | V |
| 133 | 94 | 21 | e | v:float | int 0 | no | `FFX_AtelSys_Ch_133_SetBlobShadowParam_resi` | CHR +0x17C, read by sub_82E3E0 on the way to FFX_ChrShadow_WriteBlobQuad |  | V |
| 134 | 15 | 2 | e |  | int 0 | no | `FFX_AtelSys_Ch_134_SetFlags1Bit8_resi` | FFX_Ch_SetFlags1Bit8: sets m_flags1 0x8 unless bit 0x1 is set, no reader found |  | I |
| 135 | 1969 | 292 | e |  | int 0 | no | `FFX_AtelSys_Ch_135_SetFlags1Bit8All_resi` | ch:134 on every in use CHR | global | I |
| 136 | 65 | 28 | e | v:float, mode:int | int 0 | no | `FFX_AtelSys_Ch_136_SetShadeCoeff_resi` | FFX_Ch_AnimFloatSet on a shade coefficient |  | V |
| 137 | 7 | 3 | e | motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | `FFX_AtelSys_Ch_137_SetMotionFramesIncl_resi` | FFX_EvScript_chSetMotion helper with inclusive explicit frames | global | V |
| 138 | 0 | 0 |  | actorId:int, motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | dead `FFX_AtelSys_Ch_138_SetMotionFramesInclOf_resi` | ch:137 on another actor | global | V |
| 139 | 5 | 2 | e | motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | `FFX_AtelSys_Ch_139_SetMotionFramesIncl_resi` | same helper call as ch:137 | global | V |
| 140 | 0 | 0 |  | actorId:int, motionId:int, ?:int, loops:int, start:int, end:int | int 0 | no | dead `FFX_AtelSys_Ch_140_SetMotionFramesInclOf_resi` | same helper call as ch:138 | global | V |
| 141 | 1 | 1 | e | v:int | int 0 | no | `FFX_AtelSys_Ch_141_SetTexAnimSlotByteE_resi` | byte_231000E[48 * handle] of the CHR's texture animation slot, read by FFX_ChTexAnim_Step |  | V |
| 142 | 13 | 6 | e | on:int | int 0 | no | `FFX_AtelSys_Ch_142_SetSecondaryVerticalSway_resi` | m_flags3 0x1 = on, the vertical sway term of StepSecondaryPhysics |  | V |
| 143 | 129 | 35 | e | v:int | int 0 | no | `FFX_AtelSys_Ch_143_SetJointComposerMode_resi` | CHR +0x870 m_jointComposerMode. Pops only when a CHR exists |  | V |
| 144 | 12 | 4 | e | v:int | int 0 | no | `FFX_AtelSys_Ch_144_SetForceSmoothComposer_resi` | CHR +0x871 m_forceSmoothComposer. Pops only when a CHR exists |  | V |
