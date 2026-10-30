# The ATEL Came syscall library, all 138 rows

2026-10-26. PROGRESS 1.8.8. Library 6 of `g_ffxAtelSysFuncLibs 0x1328558`, the table
`g_ffxAtelSysFuncLib06_Came 0xC43998`, 138 rows of {start, poll, float getter, int getter}.
`FFX_Atel_Init 0x86D6D0` registers it at 0x86D835 with `FFX_Atel_RegisterSysFuncLib(6,
&g_ffxAtelSysFuncLib06_Came)`. The dword after row 137, 0xC44238, is `g_ffxCameMainNearLatch` and not
a row. The ABI is `ATEL_CONTEXT.md` 11.7 and 13, the camera engine under the rows is `CAMERA_BUILD.md`.
This note names every row that a shipped script calls, marks the rest dead, and lists what each row
means for lockstep, with the co-op question first: can a script read camera state back into the
simulation. `../tools/apply_came_names.py` puts the names in the IDB. All addresses are IDA VAs at
base 0x400000.

A script operand picks the library in bits 12..15 and the function in bits 0..11, so a Came call is
operand 0x6NNN. [V]

Read only throughout. The handlers were decompiled through idalib on a scratch copy of the IDB, with
nothing renamed and nothing saved. The census walked the install with `../tools/ebp.py` and the
recreation's `tools/mon_ai.py` loader, and no script bytes were copied anywhere.

Marks: [V] read in the decompile or the disassembly, [I] inferred (a role read from the shape of a
wrapper, an engine field with no traced reader), [D] counted in shipped data. The table carries V or I
per row.

Words used below. Every camera slot has two sub-blocks, the look-at point ("Ref", sub-block 0 at slot
+0x30) and the eye ("Cam", sub-block 1 at slot +0x64C). Almost every row exists twice, once per
sub-block, and the role says which with a `Cam` or `Ref` prefix. The "own handle" is the camera handle
of the calling actor, section 4.1.

## 1. Short version

- **103 of the 138 indexes are called by shipped scripts, 35 are not.** 801,131 call sites, and unlike
  Ch most of them are in battle: 687,324 in battle field files, 104,049 in event packages, 7,446 in
  monster AI and 2,312 in system_01. No row is null. Every row has a role, the 35 dead ones too, with
  the dead names left positional. [D] [V]
- **Names follow the slot suffix form, `FFX_AtelSys_Came_NNN_<Role>_<slot>`.** Five rows already had
  descriptive names (59, 78, 79, 117, 120), all of them right. Three helper names under the rows are
  wrong: `FFX_Came_SetRoll 0x7BF280` sets the depth of field depths and never touches the roll, and the two
  `maybe_FFX_Came_Add*Filter` helpers are a wall clamp and a height clamp. Section 3.
- **came:128 is a script RNG.** It returns `FFX_Rand_Stream(14 + (which & 1))`, and battle field
  files, monster AI and system_01 use the draw to pick a camera variant through a selector or a branch.
  Which branch runs decides which camera waits run, so streams 14 and 15 have to agree. [V] [D]
- **Camera shake draws stream 9, and so does battle.** `FFX_Came_StepShake` draws stream 9 for every
  live shake record each step, and `FFX_Btl_ApplyPendingEffects` draws the same stream at 0x78F65C and
  0x78F681. A shake that runs on one peer and not the other moves the battle draws. [V]
- **Camera state does reach the script, but in shipped data it only goes back into the camera.**
  came:3, 33, 133 and 134 copy the eye or look-at point into script variables, came:119 and 120 return
  roll and ScrDpt, came:27 returns the move state. Every shipped consumer of those values is another
  Came call, except four calls in the test package `test23`. The variables themselves still hold per
  peer values if the camera differs per peer, so a state hash has to leave them out. Section 5.2. [D]
- **The camera wait is the real co-op hazard.** came:26 and 56 (CameraWait) hold the script until
  `sub_7BDD20` reports no tween, no damper motion and no spline. The damper term depends on where the
  camera started, and scripts start their moves from a snapshot of the current camera (came:3 then
  came:2). If the current camera is a per player follow camera, the wait can end on different steps
  on different peers. Section 5.3. [V] mechanism, [I] reach.
- **Hard coded cases.** came:2 and 32 patch by level, story and exact position, the battle target
  rows patch by magic id and battle id, and came:22, came:128 and btl:30 share a one shot latch for
  magic 146. Section 5.4. [V]
- **No pad, no wall clock, no language and no FMV pacing** anywhere in the call closure. Nothing
  touches the disk directly, the magic plugin handle only gates three of the battle fixups. [V]

## 2. The census [D]

Sources, all read from `FFX_Data.vbf`:

| source | files | files with Came | Came calls |
|---|---|---|---|
| event packages, `jppc/event/obj/*.ebp` | 397 | 377 | 104,049 |
| battle field files, `battle/btl/*/*.bin` section at header +4 | 816 | 814 | 687,324 |
| monster AI, `battle/mon/_mNNN/mNNN.bin` section 0 | 361 | 218 | 7,446 |
| `battle/btl/system_01/system_01.bin` | 1 | 1 | 2,312 |

No call names an index past the end of the table. For scale, the library totals over the same sources
are Core 1,861,120, Came 801,131, Ch 253,293, library 7 127,268, library 8 50,989, library 4 25,955,
Sys 7,332, library 11 869 and library 12 328.

The arity column comes from the handler's pops. The census also derives one from the bytecode, the
same way as the Core and Ch censuses, seeded with the settled Core and Ch arities. Where both exist
they agree on every used row. Four used rows are only called in expression form (27, 119, 120, 128),
so for those the arity is the pop count alone.

The heaviest rows by calls: came:59 MsCameraSetScrDpt 134,067 in 1,334 files, 58 SetRoll 132,521, 65
RefSetBattleBpl1 105,778, 68 CamSetBattleBpl2 101,175, 50 RefMoveTrapezoid 69,943, 20 CamMoveTrapezoid
68,834, 26 WaitCamMoveDone 40,608, 56 WaitRefMoveDone 38,427, 32 RefSetPos 19,390, 4 CamSetPolar
18,429 and 2 CamSetPos 12,932. Battle camera work is set roll, set ScrDpt, aim at two battle targets,
trapezoid move, wait, and that one pattern is most of the library.

Priority sets. The title (event 23, `test20`) makes a single Came call, came:96 CamClearShake. The
Besaid event packages (`bsil*`, `bsvr*`) call 43 indexes and the Besaid battle files 22, all named.

Eight used rows are called from a single package: 19 and 94 (`kino0200`), 27, 85, 88 and 123
(`camtest0`), 42 (`bsil0200`) and 79 (`lmyt0000`). Four have a single call site (19, 27, 79, 88).
came:27 GetCamMoveState is only in the camera test map.

Dead rows, 35: 9, 11, 12, 13, 14, 24, 34, 35, 36, 37, 38, 44, 47, 49, 54, 55, 57, 61, 62, 83, 84, 86,
87, 104, 106, 109, 111, 112, 114, 115, 117, 121, 122, 124, 137. Most are the unused twin of a used
row (the Ref side of a Cam setter or the other way round), and the stutter shakes are dead on the slot
side and alive only on the screen side.

Row shapes, matching `ATEL_CONTEXT.md` 13.2: 129 rows have only the int getter, 2 only the float
getter (119, 120), 3 have start and poll (100, 123, 124) and 4 have only a poll (26, 56, 98, 99). No
row is null. [V]

Unlike Ch, the rows are not copies of each other. The Cam and Ref twins call the same helper with the
sub-block as an argument, but each row is its own small function, so no slot is shared. [V]

## 3. Names that were already there

Kept as they are, and checked against the bodies [V]:

- came:59 `MsCameraSetScrDpt`. It sets the ScrDpt (projection distance) target, slot +0xCA0, and
  snaps the live value when no ScrDpt tween runs.
- came:78 `FFX_AtelCmd_CameScreenOpen`, 79 `FFX_AtelCmd_CameScreenClose`, 117
  `FFX_AtelCmd_CameScreenSetSlot` (117 is dead).
- came:120 `j_FFX_Came_GetScrDpt`, a thunk to `FFX_Came_GetScrDpt`. The thunk name is right and the
  apply script leaves it.
- The helpers `FFX_AtelSys_Came_SetTargetActors 0x7BAB90`, `FFX_AtelSys_Came_SetTargetActorHeight
  0x7BAC40`, `FFX_AtelCmd_CameScreenShake 0x7BB840`, `FFX_Came_SetScreenShake 0x7C56F0`,
  `FFX_Came_SetScreenSpringShake 0x7C57B0`, `FFX_Came_ClearScreenShake 0x7C56C0` and
  `FFX_Came_IsScreenShakeActive 0x7C5680` are right.

`ATEL_CONTEXT.md` lists the int getter of came:70 (0x7B9D20) as misnamed `_wcstoul_0` by a signature
pass. The scratch copy already has it positional. The apply script takes over that exact name as well,
in case the live IDB still carries it.

Wrong, and outside what the apply script touches (helper names, not table slots), so these are hand
edits [V]:

- **`FFX_Came_SetRoll 0x7BF280` sets a depth of field depth, not the roll.** (This first said light set id. The misc library work showed the values are depth of field, see section 6.) Its only caller is came:127. It
  writes `slot +0xCB0 + 4 * (which & 1)` and sets the dirty flag `slot +0xCA8`, plus a per priority
  copy at `slot +0xCCC`. `FFX_Came_ActivateScreen` compares those ids with the screen's cached ones at
  0x7BC682 and 0x7BC690 and hands them to `sub_82CE20` at 0x7BC6D7 and 0x7BC6E9. The roll lives at
  slot +0xC90 and +0xC94 and is set by `sub_7BFA80` (came:58). Applied as `FFX_Came_SetDofDepth`.
- **`maybe_FFX_Came_AddLineFilter 0x7BF0B0` is a wall clamp.** It adds a filter of kind 0 or 2 that
  keeps the point on one side of an XZ line. Proposed `FFX_Came_AddWallClamp`.
- **`maybe_FFX_Came_AddPointFilter 0x7BF1F0` is a height clamp.** Kind 1 or 3, it keeps Y inside a
  band. Proposed `FFX_Came_AddHeightClamp`.
- (Fixed in CUTSCENE.md 8.1 on 2026-10-26.) `CUTSCENE.md` called the first argument of `FFX_Came_GetHandleForClass` "screenArg". It is the owner
  id, the third argument is the screen. It also counts "66 ATEL camera syscalls", which is the number
  of call sites of that function: 51 in the handlers, 13 in helper bodies shared by several handlers,
  one in `FFX_Atel_BindActorCameraHandle` (core:2) and one in `SG_DebugWin_SplineEditorProc`.

`tools/ebp.py` already carries `LIB_INFO` 6 = (0xC43998, 138, 136) and names for came:26 CameraWait,
59, 78, 79, 117 and 120 in `SYSCALL_NAMES`. All of them agree with the bodies, CameraWait is the eye
side wait.

## 4. The families

### 4.1 Handles, suspend and priority

Every row that works on a camera first finds the calling actor's handle. It takes the cache at actor
+0x3C (`FFX_Atel_GetActorCameraHandleSlot`, filled by core:2 `FFX_Atel_BindActorCameraHandle`), and
when that is empty calls `FFX_Came_GetHandleForClass(5, kind, 0)`. Kind 1 is the map class (class 1)
and is used when the caller is the system actor, kind 0 is the event class (class 3), and the battle
target rows use kind 2, the battle class (class 5). A handle packs screen in bits 0..7, priority slot
in 8..15, class in 16..19 and owner in 24..31 (`FFX_Came_OpenHandle 0x7BD110`). [V]

The screen's priority list holds 8 byte entries at `0x11381A8 + 3488 * screen`: +0 handle, +4 suspend,
+5 closed, +6 priority. `FFX_Came_GetActivePrioSlot` picks the highest priority entry with +4 and +5
both clear, and that entry's record is what the camera shows. Several rows (the path recorder, the
shakes, the restore rows, the blur and the depth of field) only act when the own handle is the active
one. [V]

- SuspendCameraOf (0) and ResumeCameraOf (1) set and clear +4 for another actor's handle.
- SetCameraPriority (101) rewrites +6 on every entry of the own handle, and is ignored while
  `dword_2311424` is set, which `FFX_Btl_BeginBattle` sets through `sub_7BE100`.
- CloseBattleCameras (118) closes the battle and viewer handles on all three screens.

### 4.2 Where the point is: the modes

Each priority record has a mode (`g_ffxCameModeNames`): 1 pos, 2 pol, 3 act, 4 hyp1, 5 hyp2, 6 hyp3,
7 polo, 8 pflw, 9 phyp, 10 act2, 11 bpos, 12 bpl1, 13 bpl2, 14 spln, 15 cpl1, 16 cpl2, 17 spl2. The
setters write the mode and its parameters into the record of the sub-block, and `FFX_Came_StepAll`
evaluates it each step into the pending point. [V]

- **Fixed and relative.** SetPos (2, 32), SetPolar (4, 34), AddPolar (5, 35), SetHyp1/2/3 (6 to 8,
  36 to 38). The polar and hyp rows take degrees.
- **Follow actors.** SetTargetActors (9, 39, mode 3) averages up to two ATEL actors, 0xFFFF for
  unused. SetTargetActorHeight (60, 61, mode 10).
- **Follow a point set.** CamFollowPointSet (28 to 31, modes 8 and 9). The point set is a script
  variable array, weighted 1/d or 1/d squared by record+3 bit 0.
- **Splines.** CamSetSpline (70, 71, mode 14) and CamSetSpline2 (91, mode 17). Point source 0 or 1 is
  the path recorded by 72 to 75, anything else is a script variable address.
- **Battle targets.** SetBattleBpos (62, 63), Bpl1 (64, 65), Bpl2 (68, 69), Cpl1 (76) and Cpl2 (77),
  through `sub_7BAD10` and `sub_7BADB0` to `FFX_Came_SetBattleTarget`. They take two battle target
  ids, a height offset and, for the polar kinds, two angles and a length, offsets and length divided
  by 100.
  Correction 2026-10-29 (the 13.12 battle camera port, Project Zanarkand `src/engine/came/BattleCamera`
  and `src/ffx/btl/BattleCameraSyscalls`). The handle is not always the battle one. Both bodies take the
  calling actor's cached handle at actor +0x3C and only fall back to `FFX_Came_GetHandleForClass(5, 2, 0)`,
  caching it there, when that is 0. `FFX_Came_SetBattleTarget` resolves the two targets through
  `FFX_BtlAi_ResolveTargetSelector(self, target, 1, 1)` only for a class 5 handle. Under any other class
  it stores them raw and `FFX_Came_ModeBattle_Step` reads them as ATEL actor ids through `sub_7975C0`
  (-2 the player, -5 and -21 every actor `sub_86D950` passes). The targets are the `cam_chr_*` selectors
  of `g_ffxCameChrTargetNames` (name index -selector - 1) or a unit index. The angles go in as degrees,
  theta = aDeg and phi = bDeg in radians. Every row returns int 0. Nothing in the rows or the modes draws
  RNG. [V]

### 4.3 Moving between points

A setter moves the target, a move row says how to get there. The three tween records of the sub-block
(x, y, z or theta, phi, length) plus the roll and ScrDpt tweens on the eye are armed by:

- MoveLinear (16, 46), MoveLinearPolar (17, 47), MoveCosine (18, 48), MoveCosinePolar (19, 49) over a
  frame count, and MoveTrapezoid (20, 50) and MoveTrapezoidPolar (21, 51) with accel, hold and decel
  frames. The tween types in `FFX_Came_StepScreenRectTween 0x7BDA80` and the slot twin are 1 linear,
  2 trapezoid, 3 cosine. Polar space (sub-block +1 = 1) makes the eye orbit instead of cutting across.
- SetMoveMask (66, 67) masks axes out of the next move (1 x, 2 y, 4 z, 8 roll, 0x10 ScrDpt), cleared
  after the move.
- StopMove (22, 52) zeroes the tweens and sets pending to committed.
- SetDamping (23, 53) sets the per axis divisors of the first order damper that runs at commit.
  SetSpeedLimit (24, 54) and ClearSpeedLimit (25, 55) cap the step length.
- WaitCamMoveDone (26) and WaitRefMoveDone (56) are polls on `sub_7BDD20`, GetCamMoveState (27) and
  GetRefMoveState (57) return its mask: 1 a tween runs, 2 the damper moved this step (sub-block +2),
  4 the record is in spline mode.
- RestorePriorityPos (135, 136) copies record+128, the point this priority last committed, back in.

### 4.4 Clamps

Up to five filters per record, applied in `FFX_Came_PublishPosToOtherSubBlock 0x7BCAB0` through
`sub_7C0120` while record+4 is nonzero and record+5 clear. Kinds: 0 soft wall and 2 scaled wall
(AddWallClamp 10, 11, 40, 41), 1 soft height band and 3 scaled height band (AddHeightClamp 12, 13, 42,
43), 4 and 5 rail onto a spline (RefAddRailClamp 89, 90). SetClampsPaused (14, 44) sets record+5,
ClearClamps (15, 45) drops them all. [V]

### 4.5 Path recording

CamPathClear and CamPathAddPoint (72, 73), RefPathClear and RefPathAddPoint (74, 75) record the
pending point into one of two 32 entry buffers at 0x1136B68 (counts `dword_1136B64` and
`dword_1136B60`), for a later spline with point source 0 or 1. The buffers are single, shared by every
handle and screen. [V]

### 4.6 Roll, ScrDpt and reading the camera back

SetRoll (58) and MsCameraSetScrDpt (59) set the roll and the projection distance of the handle's
record, and snap the live slot value when no tween runs. GetRollDeg (119) and `j_FFX_Came_GetScrDpt`
(120) read them back from the active slot. CamGetPosToVars (3), RefGetPosToVars (33) write the
committed point, CamGetLivePosToVars (133) and RefGetLivePosToVars (134) the live point with the shake
added, into three script variables. Hex-Rays prints came:119 as returning 0 * 180 / pi, the
disassembly at 0x7B9194 to 0x7B91B9 shows it returns `FFX_Came_UnwrapAngleNear(0, FFX_Came_GetRoll())`
in degrees. [V]

### 4.7 Shake

Two kinds of shake record, both stepped by `FFX_Came_StepShake 0x7C50F0`.

- **Slot shakes** move the eye or the look-at point. `sub_7BB8C0` pops (mask, rate, duration, amp,
  range) and `sub_7C4FC0` arms the records the mask picks, active handle only.
- **Screen shakes** move a whole screen. `FFX_AtelCmd_CameScreenShake 0x7BB840` pops a screen first,
  and `FFX_Came_SetScreenShake` arms one of two records at screen +0x16C.

Wave types, odd fading out and even fading in, 7 and 8 with no envelope [V]:

| type | wave | Ref | Cam | Screen |
|---|---|---|---|---|
| 1 | sin, fade out | 92 | 93 | 94 |
| 2 | sin, fade in | 102 | 103 | 104 dead |
| 3 | sin + cos(t + 0.3), fade out | 105 | 106 dead | 107 |
| 4 | sin + cos(t + 0.3), fade in | 108 | 109 dead | 110 |
| 5 | sin + cos(t + 0.1) with a second draw, fade out | 111 dead | 112 dead | 113 |
| 6 | the same, fade in | 114 dead | 115 dead | 116 |
| 7 | sin, steady | 129 | 130 | |
| 8 | sin + cos(t + 0.1), steady | 131 | 132 | |
| 9 | spring | | | 137 dead |

The role names use Sine for 1, 2 and 7, Mix for 3, 4 and 8 and Stutter for 5 and 6. Clears are 95,
96 and 97, waits are 98, 99 (polls) and 100 (start and poll on a screen).

### 4.8 Screens

The three screens at `g_ffxCameScreens 0x1137000` (448 bytes each) have a WIN rect and a DRW rect,
each with four tweens. CameScreenOpen (78), CameScreenClose (79) and CameScreenSetSlot (117) are the
kept rows. SetScreenDrwRect (80), SetScreenDrwMode (81) and SetScreenWinMode (82) set the rects and
fit modes, ScreenWinTween* (83 to 85) and ScreenDrwTween* (86 to 88) animate them, StopScreen*Tween
(121, 122) snap them and WaitScreen*Tween (123, 124) wait on them. Only `camtest0`, `lmyt0000` and
one came:81 in `test23` use the rect rows, so `lmyt0000` is the only non test package that opens a
second screen. [V] [D]

### 4.9 Render state

SetScreenBlur (125), SetDofEnabled (126) and SetDofDepth (127) write render state that
`FFX_Came_ActivateScreen` hands to the renderer (`sub_82BEE0`, `sub_82CE20`, `sub_82CB60`). Nothing in
the simulation reads it back, as far as traced. [I]

### 4.10 RandCamera

came:128 is the only row whose result is a random number, section 5.1.

## 5. Lockstep hazards

The lockstep column uses these flags:

- `rng` draws a game RNG stream in the handler.
- `rng*` arms a record that `FFX_Came_StepShake` later draws stream 9 for, or returns a value that
  includes that shake.
- `camread` hands camera state back to the script, as a value or as the length of a wait.
- `global` touches a single slot global or carries a hard coded id or position case.
- `local` stores a raw pointer to script variable memory in the camera record.
- `render` only sets render state.

### 5.1 RNG

- **came:128 RandCamera, streams 14 and 15.** The handler pushes `14 + (which & 1)` at 0x7B89CC and
  0x7B89F8 and calls `FFX_Rand_Stream` at 0x7B89DA and 0x7B89FC. Under magic 146 with the latch set
  it returns `draw % 2 ? 2 : 0` and clears the latch. In shipped data the draw always feeds a
  selector or a branch that picks a camera variant: 379 calls in battle field files, 148 in monster AI
  (including "pick again while equal to the last pick" loops) and 52 in system_01. The branch picks
  which moves and waits run, which changes when the script resumes, so streams 14 and 15 are
  simulation state even though they only choose cameras. [V] [D]
- **Stream 9, the shake stream, is shared with battle.** `FFX_Came_StepShake` draws stream 9 at
  0x7C5112, 0x7C533E and 0x7C53F6, `FFX_Came_SetScreenSpringShake` draws it in the
  setter, and `FFX_Btl_ApplyPendingEffects` draws it at 0x78F65C (`draw & 3` + 13) and 0x78F681
  (`draw & 1` + 15) for `sub_78CA20`. So the count of live shake records is battle simulation state.
  Screen shakes are stepped once per `FFX_Came_ActivateScreen` call, which runs inside
  `FFX_MainStep` once per enabled screen, so a peer that enables an extra screen for its own view
  also draws stream 9 more often. What `sub_78CA20` does with the 13 to 16 index is open. [V], the
  effect of the index [I].
- Nothing else in the closure draws a stream. [V]

### 5.2 Can camera state read back into the simulation

This is the co-op question, since the camera is meant to be per player.

- **Values.** came:3, 33, 133 and 134 write a camera point into script variables. `sub_7BA3C0`
  resolves the three variable references and `sub_7BD420 0x7BD420` stores the committed point, and
  `sub_7BA460` with `sub_7BD4B0` do the same for the live point. came:119
  and 120 return roll and ScrDpt, came:27 the move mask. [V]
- **Where the values go in shipped data.** Every use of a variable those rows filled was followed to
  its consumer. In battle field files 528 sites read the three values, scale them and pass them to
  came:2. In event packages 210 go to came:2, 31 to came:32, 16 to came:39 arguments. The roll and
  ScrDpt read in system_01 are stored and later handed to came:58 and 59. came:27 is only in
  `camtest0`. The single non camera consumer is the test package `test23`, which passes them to sys:5
  four times. [D]
- **So nothing a shipped script reads from the camera reaches a non camera syscall,** and with a camera
  that is identical on every peer there is no hazard. With a per player camera, the variables those
  rows fill hold per peer values, and a world hash or a desync check has to exclude them. The monster
  AI and battle field restores of the roll and ScrDpt were not traced to the end. [I]
- **Not through the library.** `FFX_Came_GetYaw` turns the pad into world directions for the player,
  core:530 compares the camera height, ch:124 aims a head at the eye, `FFX_Came_GetPos` feeds CHR
  `m_cameLen`, and `FFX_Ch_IsNearCameraForSmoothing` reads the active slot. Those are the readers to
  check when the camera goes per player, and they sit outside this table. [V] names, [I] reach.

### 5.3 Waits whose length depends on the camera

- came:26 and 56 return 0 while `sub_7BDD20` is nonzero. The value 2 in that mask is sub-block +2
  (tested at 0x7BDDB6), which
  `FFX_Came_StepAll 0x7BE110` sets when the damper still moved the pending point this step. A damped
  move therefore lasts as long as the distance takes to settle, not a fixed frame count. SetDamping
  is used 22 times in 7 packages on the eye and 346 times in 44 packages on the look-at point. [V]
- Scripts start moves from a snapshot of the current camera. The pattern in `hiku0800` and many others
  is came:1 (resume), came:3 and 33 (snapshot to variables), came:2 and 32 (start from the snapshot),
  a move, came:26. If the current camera on screen is the player follow camera and that differs per
  peer, the damped part of the move starts from different points and the CameraWait can end on
  different steps. A plain tween with no damping lasts its frame count on every peer. [V] mechanism,
  [I] that a shipped script hits it with a per player camera.
- came:98, 99, 100, 123 and 124 wait on shakes and screen tweens. They run on fixed counters and only
  differ if a peer arms or steps a shake the others do not. [V]

### 5.4 Single slot globals and hard coded cases

- The handle globals `g_ffxCame*Handles`, the screen priority lists and the three slots are one per
  screen for the whole game, not per player. A per player camera needs its own slot or screen, and
  section 5.1 says what an extra screen does to stream 9. [V]
- The two path buffers at 0x1136B68 and their counts (came:72 to 75). [V]
- `dword_12FB7E0`, a one shot latch. Under magic 146 (`MagicFile__load` arms `dword_C494C8 = 0`)
  the second came:22 sets it, and came:128 and btl:30 (0x7A68E9) test and clear it. [V]
- `dword_1136B58`, set by came:2 and 32 at level 8207 under magic 250 at one exact position, read by
  `FFX_Btl_SetUnitShowVote 0x79714F`, and cleared by the battle target fixup at level 8207 under magic
  251, which also flushes a deferred hide queue through `FFX_Ch_SetHideBit10`. A camera row changing
  CHR visibility. [V]
  Correction 2026-10-29: the flush is `sub_7BADB0` at 0x7BAED4. It counts `dword_11334D0` down over
  `chr[count]` to `chr[1]`, setting hide bit 10 on each, and then writes `dword_1136B58` 0. It is not
  "queued by `dword_1136B58`". It runs only for battle 0xD40000 at level 8207 under magic 251 when
  zOfs * 100 is exactly 88.99999237060547, which never holds on PC (above), so on PC came:64 to 77 never
  change CHR visibility. [V]
- `dword_2311424`, the priority lock that came:101 obeys. [V]
- `sub_7BB6A0` (came:2, 32) patches by level 1761 at story 260 and an exact position (calls
  `sub_79E2A0`), and by scene 248 in battle 0x1390000 (patches x to -68.056053). `sub_7BADB0`
  patches the battle target arguments for magic ids 88, 146, 163, 201, 704, 705 and 76 or 311 with chr
  model 4289, and for battle ids 0xD40000, 0x1A30004, 0x1F60004, 0x25C0013, 0x1590000 and 0x1AE000A.
  All of these key on simulation values, so they agree across peers, but a port has to carry them. [V]
  Correction 2026-10-29: three of the battle id cases can never fire on PC. They compare len * 100
  against 228.99996948242188 (0x1A30004), and zOfs * 100 against 3.9999990463256836 (0x1F60004) and
  88.99999237060547 (0xD40000, the hide below) exactly. Those constants are fl(f * 100) for some float f,
  PS2 single precision arithmetic, and no float f gives f * 100.0 equal to any of them in double, which
  is what the PC x87 computes at 53 bits. So the 0x1A30004 and 0x1F60004 patches and the 0xD40000 hide
  are dead in this build. The port keeps the compares as written and its sweep proves they never hold. [V]

### 5.5 Disk, local and the rest

- The 704, 705 and 146 fixups in `sub_7BADB0` also check `g_ffxMagicFileHinst != -1`, set by
  `MagicFile__onDllLoaded`. If the magic plugin finishes loading on different steps on different
  peers, the fixup applies on different steps. [I]
- The point follow, spline and rail rows store a raw pointer to script variable memory in record+8.
  The pointer itself is machine local, the values behind it are script state. [V]
- came:26 prints a debug count when `dword_12FB80C == 1`. Nothing else. [V]
- No pad read, no wall clock, no language test, no FMV pacing in the closure of any row. [V]

## 6. The apply script

Applied to the live IDB on 2026-10-26: 101 slots renamed (the used rows), and the three helpers by hand,
`FFX_Came_SetLightSetIdSlot`, `FFX_Came_AddWallClamp` and `FFX_Came_AddHeightClamp`. Corrected the same day: the
+0xCAC..+0xCB4 values reach `FFX_Sg_StepScreenFades` as the depth of field (VISUAL_CENSUS.md), so came:126 and
127 are `SetDofEnabled` and `SetDofDepth`, the helpers `FFX_Came_SetDofEnabled 0x7BCF50` and `FFX_Came_SetDofDepth
0x7BF280`, and the renderer side `FFX_Gfx_SetDofEnabled 0x82CB60` and `FFX_Gfx_SetDofDepth 0x82CE20`. Live result: 106 table
slots named, 35 positional (the dead rows), 0 placeholders.

`../tools/apply_came_names.py`, built like `apply_ch_names.py`. It reads the table out of the IDB,
and for each row in its NAMES block gives each non-null slot `FFX_AtelSys_Came_NNN_<Role>_<slot>`
(`--no-slot-suffix` drops the `_<slot>` on single slot rows). It only renames a slot whose current
name is a positional name of that row (`FFX_AtelSys_Came_NNN_<slot>`, with or without padding, or
`FFX_AtelSys_Came_NNN` on a single slot row), a `sub_`, `loc_` or `nullsub_` placeholder, or the exact
misname `_wcstoul_0` on came:70. It skips a slot that is not a function start, a function in more than
one slot and a name already taken. Dead rows are left alone unless `--include-dead`. Dry run by
default, it never saves.

NAMES has 133 rows (all but the five kept rows 59, 78, 79, 117 and 120), 34 of them dead.

Dry run on the scratch copy:

- default: 101 to rename, 0 skipped, 0 already named. 99 used rows, with two slots each on 100 and
  123.
- `--include-dead`: 136 to rename, 0 skipped.
- `--include-dead --apply` as a headless rehearsal: 136 renamed, 0 failed, database not saved.

No slot was refused, so every slot in the scratch copy is positional and no function is shared
between slots.

## 7. Open

- What `sub_78CA20` does with the stream 9 draw (13 to 16) in `FFX_Btl_ApplyPendingEffects`. The
  shared stream is certain, the size of the effect is not.
- The register push callers of `FFX_Rand_Stream` outside the library were not read, so whether any
  battle code also draws streams 14 or 15 is open.
- The argument meanings of the hyp modes (6 to 8) and the fourth trapezoid argument (named startDiv)
  come from the setter shapes, not from the mode step code.
- `sub_82BEE0`, `sub_82CE20` and `sub_82CB60`, the renderer side of came:125 to 127, were not
  followed, so "render only" there is an inference.
- The monster AI and battle field restores of came:119 and 120 were not traced to their consumer.
- btl:30's role around the magic 146 latch belongs in the Btl library note.
- Whether `FFX_Ch_IsNearCameraForSmoothing` changes anything a hash would see.

## 8. The table

Columns: index, call sites, files, source groups (e event, m monster AI, b battle field, s
system_01), arguments in push order, result, whether it blocks, the names, meaning, lockstep flags,
mark.

| came | uses | files | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 863 | 241 | e | actorId:int | int handle | no | `FFX_AtelSys_Came_000_SuspendCameraOf_resi` | pops an actor id and sets byte +4 of that actor's handle entry (sub_7C0010), so FFX_Came_GetActivePrioSlot skips it until ResumeCameraOf |  | V |
| 1 | 986 | 238 | e | actorId:int | int handle | no | `FFX_AtelSys_Came_001_ResumeCameraOf_resi` | clears byte +4 of that actor's handle entry, the inverse of came:0 |  | V |
| 2 | 12932 | 857 | e,b,s | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Came_002_CamSetPos_resi` | sub_7BB6A0 -> FFX_Came_SetPos, mode 1 'pos' on the eye. Hard coded cases: level 1761 at story 260 and one exact position calls sub_79E2A0, level 8207 under magic 250 at one position sets dword_1136B58, scene 248 in battle 0x1390000 patches x | global | V |
| 3 | 769 | 560 | e,m,b,s | xVar:int, yVar:int, zVar:int | int 0 | no | `FFX_AtelSys_Came_003_CamGetPosToVars_resi` | sub_7BD420: copies the committed eye position (sub-block +0x20) of screen 0 into three script variables | camread | V |
| 4 | 18429 | 1026 | e,m,b | aDeg:float, bDeg:float, len:float | int 0 | no | `FFX_AtelSys_Came_004_CamSetPolar_resi` | sub_7BF700, mode 2 'pol': the eye at a polar offset (degrees to radians) from the look-at point |  | V |
| 5 | 21 | 21 | e | aDeg:float, bDeg:float, len:float | int 0 | no | `FFX_AtelSys_Came_005_CamAddPolar_resi` | sub_7BF7F0, mode 7 'polo': adds the polar offset to the current one |  | V |
| 6 | 483 | 69 | e | a:float, b:float, cDeg:float, d:float, e:float, f:float | int 0 | no | `FFX_AtelSys_Came_006_CamSetHyp1_resi` | FFX_Came_SetHyp1, mode 4 'hyp1', the third value in degrees |  | V |
| 7 | 111 | 28 | e | a:float, b:float, c:float, d:float, e:float, f:float | int 0 | no | `FFX_AtelSys_Came_007_CamSetHyp2_resi` | FFX_Came_SetHyp23 with 0, mode 5 'hyp2' |  | V |
| 8 | 320 | 80 | e | a:float, b:float, c:float, d:float, e:float, f:float | int 0 | no | `FFX_AtelSys_Came_008_CamSetHyp3_resi` | FFX_Came_SetHyp23 with 1, mode 6 'hyp3' |  | V |
| 9 | 0 | 0 |  | actorA:int, actorB:int, yOfs:float | int 0 | no | dead `FFX_AtelSys_Came_009_CamSetTargetActors_resi` | FFX_AtelSys_Came_SetTargetActors on the eye, mode 3 'act' |  | V |
| 10 | 2 | 2 | e | x1:float, z1:float, x2:float, z2:float, k:float | int 0 | no | `FFX_AtelSys_Came_010_CamAddWallClamp_resi` | maybe_FFX_Came_AddLineFilter kind 0: keeps the eye on one side of an XZ line with an atan falloff k, up to 5 clamps per record |  | V |
| 11 | 0 | 0 |  | x1:float, z1:float, x2:float, z2:float, k:float | int 0 | no | dead `FFX_AtelSys_Came_011_CamAddWallClampScaled_resi` | as came:10, kind 2, the overshoot scaled by k |  | V |
| 12 | 0 | 0 |  | lo:float, hi:float, k:float | int 0 | no | dead `FFX_AtelSys_Came_012_CamAddHeightClamp_resi` | maybe_FFX_Came_AddPointFilter kind 1: keeps the eye Y inside a band with an atan falloff |  | V |
| 13 | 0 | 0 |  | lo:float, hi:float, k:float | int 0 | no | dead `FFX_AtelSys_Came_013_CamAddHeightClampScaled_resi` | as came:12, kind 3, the overshoot scaled by k |  | V |
| 14 | 0 | 0 |  | on:int | int handle | no | dead `FFX_AtelSys_Came_014_CamSetClampsPaused_resi` | record+5 = on: FFX_Came_PublishPosToOtherSubBlock skips the clamps while it is set |  | V |
| 15 | 149 | 5 | e |  | int handle | no | `FFX_AtelSys_Came_015_CamClearClamps_resi` | record+4 = 0, drops every clamp |  | V |
| 16 | 2765 | 359 | e,m,b,s | frames:int | int 0 | no | `FFX_AtelSys_Came_016_CamMoveLinear_resi` | sub_7BDE50: sets the three tween records of the sub-block (and the roll and ScrDpt ones for the eye) to linear (type 1) over frames, in xyz space (B+1 = 0) |  | V |
| 17 | 65 | 41 | e,b | frames:int | int 0 | no | `FFX_AtelSys_Came_017_CamMoveLinearPolar_resi` | sub_7BDE50: sets the three tween records of the sub-block (and the roll and ScrDpt ones for the eye) to linear over frames, in polar space (B+1 = 1, the eye orbits) |  | V |
| 18 | 108 | 36 | e,b | frames:int | int 0 | no | `FFX_AtelSys_Came_018_CamMoveCosine_resi` | sub_7BDE50: sets the three tween records of the sub-block (and the roll and ScrDpt ones for the eye) to cosine ease (type 3) over frames, xyz space |  | V |
| 19 | 1 | 1 | e | frames:int | int 0 | no | `FFX_AtelSys_Came_019_CamMoveCosinePolar_resi` | sub_7BDE50: sets the three tween records of the sub-block (and the roll and ScrDpt ones for the eye) to cosine ease over frames, polar space |  | V |
| 20 | 68834 | 829 | e,m,b,s | accel:int, hold:int, decel:int, startDiv:int | int 0 | no | `FFX_AtelSys_Came_020_CamMoveTrapezoid_resi` | sub_7BD860: trapezoid tween (type 2) on the axes record+7 does not mask, xyz space |  | V |
| 21 | 1860 | 170 | e,b | accel:int, hold:int, decel:int, startDiv:int | int 0 | no | `FFX_AtelSys_Came_021_CamMoveTrapezoidPolar_resi` | sub_7BD860: trapezoid tween (type 2) on the axes record+7 does not mask, polar space |  | V |
| 22 | 6769 | 1201 | e,m,b,s |  | int handle | no | `FFX_AtelSys_Came_022_CamStopMove_resi` | FFX_Came_StopMove 0x7BED60: zeroes the tween counters and calls FFX_Came_SetPos with the sub-block's own pending position (B+0x50, slot + 0x61C*sub + 0x80 at 0x7BEDFD), then for the CAM sub-block re-sets ScrDpt from S+0xCA0 and roll from the roll target S+0xC94. Corrected 2026-10-28: it does not copy the committed eye. Under magic 146 it also counts calls in dword_C494C8 and on the second sets the dword_12FB7E0 latch that came:128 and btl:30 read | global | V |
| 23 | 22 | 7 | e | x:float, y:float, z:float, w:float | int 0 | no | `FFX_AtelSys_Came_023_CamSetDamping_resi` | sub_7BDDE0: record+112..124, the per axis divisors of the first order damper FFX_Came_StepAll runs at commit, 0 snaps |  | V |
| 24 | 0 | 0 |  | a:float, maxSpeed:float, b:float | int 0 | no | dead `FFX_AtelSys_Came_024_CamSetSpeedLimit_resi` | sub_7BCE80: B+3 = 1, B+8 and B+12, StepAll then limits the step length |  | V |
| 25 | 206 | 21 | e |  | int handle | no | `FFX_AtelSys_Came_025_CamClearSpeedLimit_resi` | sub_7BEC90: B+3 = 0 and the velocity at B+0x40 zeroed |  | V |
| 26 | 40608 | 921 | e,m,b,s |  | none | yes | `FFX_AtelSys_Came_026_WaitCamMoveDone_poll` | poll only, ebp.py's CameraWait: returns 0 while sub_7BDD20 is nonzero (a tween running, the damper moved this step, or a spline in progress), else 5. Prints a debug count while dword_12FB80C == 1 | camread | V |
| 27 | 1 | 1 | e |  | int mask | no | `FFX_AtelSys_Came_027_GetCamMoveState_resi` | sub_7BDD20 for the eye: 1 a tween (or the roll or ScrDpt tween) is running, 2 B+2, the damper moved this step, 4 the winning record is mode 14 'spln' | camread | V |
| 28 | 34 | 15 | e | points:int, angleDeg:float | int handle | no | `FFX_AtelSys_Came_028_CamFollowPointSetSq_resi` | FFX_Came_SetPointFollow, mode 8 'pflw', the point set at a script variable address, weights 1/d squared (record+3 bit 0) | local | V |
| 29 | 67 | 14 | e | points:int, angleDeg:float, a:float, b:float | int handle | no | `FFX_AtelSys_Came_029_CamFollowPointSetPolarSq_resi` | FFX_Came_SetPointFollowPolar, mode 9 'phyp', 1/d squared | local | V |
| 30 | 18 | 11 | e | points:int, angleDeg:float | int handle | no | `FFX_AtelSys_Came_030_CamFollowPointSet_resi` | as came:28 with 1/d weights | local | V |
| 31 | 27 | 14 | e | points:int, angleDeg:float, a:float, b:float | int handle | no | `FFX_AtelSys_Came_031_CamFollowPointSetPolar_resi` | as came:29 with 1/d weights | local | V |
| 32 | 19390 | 1082 | e,m,b,s | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Came_032_RefSetPos_resi` | came:2 on the look-at point, the same hard coded cases | global | V |
| 33 | 47 | 21 | e,m,b,s | xVar:int, yVar:int, zVar:int | int 0 | no | `FFX_AtelSys_Came_033_RefGetPosToVars_resi` | came:3 for the committed look-at point | camread | V |
| 34 | 0 | 0 |  | aDeg:float, bDeg:float, len:float | int 0 | no | dead `FFX_AtelSys_Came_034_RefSetPolar_resi` | came:4 on the look-at point |  | V |
| 35 | 0 | 0 |  | aDeg:float, bDeg:float, len:float | int 0 | no | dead `FFX_AtelSys_Came_035_RefAddPolar_resi` | came:5 on the look-at point |  | V |
| 36 | 0 | 0 |  | a:float, b:float, cDeg:float, d:float, e:float, f:float | int 0 | no | dead `FFX_AtelSys_Came_036_RefSetHyp1_resi` | came:6 on the look-at point |  | V |
| 37 | 0 | 0 |  | a:float, b:float, c:float, d:float, e:float, f:float | int 0 | no | dead `FFX_AtelSys_Came_037_RefSetHyp2_resi` | came:7 on the look-at point |  | V |
| 38 | 0 | 0 |  | a:float, b:float, c:float, d:float, e:float, f:float | int 0 | no | dead `FFX_AtelSys_Came_038_RefSetHyp3_resi` | came:8 on the look-at point |  | V |
| 39 | 4463 | 295 | e | actorA:int, actorB:int, yOfs:float | int 0 | no | `FFX_AtelSys_Came_039_RefSetTargetActors_resi` | FFX_AtelSys_Came_SetTargetActors on the look-at point, mode 3 'act': the average of up to two ATEL actors' positions plus the offset, 0xFFFF unused |  | V |
| 40 | 115 | 32 | e | x1:float, z1:float, x2:float, z2:float, k:float | int 0 | no | `FFX_AtelSys_Came_040_RefAddWallClamp_resi` | came:10 on the look-at point |  | V |
| 41 | 3940 | 162 | e | x1:float, z1:float, x2:float, z2:float, k:float | int 0 | no | `FFX_AtelSys_Came_041_RefAddWallClampScaled_resi` | came:11 on the look-at point |  | V |
| 42 | 2 | 1 | e | lo:float, hi:float, k:float | int 0 | no | `FFX_AtelSys_Came_042_RefAddHeightClamp_resi` | came:12 on the look-at point |  | V |
| 43 | 985 | 162 | e | lo:float, hi:float, k:float | int 0 | no | `FFX_AtelSys_Came_043_RefAddHeightClampScaled_resi` | came:13 on the look-at point |  | V |
| 44 | 0 | 0 |  | on:int | int handle | no | dead `FFX_AtelSys_Came_044_RefSetClampsPaused_resi` | came:14 on the look-at point |  | V |
| 45 | 3255 | 337 | e |  | int handle | no | `FFX_AtelSys_Came_045_RefClearClamps_resi` | came:15 on the look-at point |  | V |
| 46 | 2484 | 344 | e,m,b,s | frames:int | int 0 | no | `FFX_AtelSys_Came_046_RefMoveLinear_resi` | came:16 on the look-at point |  | V |
| 47 | 0 | 0 |  | frames:int | int 0 | no | dead `FFX_AtelSys_Came_047_RefMoveLinearPolar_resi` | came:17 on the look-at point |  | V |
| 48 | 106 | 37 | e,b | frames:int | int 0 | no | `FFX_AtelSys_Came_048_RefMoveCosine_resi` | came:18 on the look-at point |  | V |
| 49 | 0 | 0 |  | frames:int | int 0 | no | dead `FFX_AtelSys_Came_049_RefMoveCosinePolar_resi` | came:19 on the look-at point |  | V |
| 50 | 69943 | 870 | e,m,b,s | accel:int, hold:int, decel:int, startDiv:int | int 0 | no | `FFX_AtelSys_Came_050_RefMoveTrapezoid_resi` | came:20 on the look-at point |  | V |
| 51 | 12 | 8 | e | accel:int, hold:int, decel:int, startDiv:int | int 0 | no | `FFX_AtelSys_Came_051_RefMoveTrapezoidPolar_resi` | came:21 on the look-at point |  | V |
| 52 | 6506 | 1198 | e,m,b,s |  | int handle | no | `FFX_AtelSys_Came_052_RefStopMove_resi` | sub_7BED60 on the look-at point, without the magic 146 counter |  | V |
| 53 | 346 | 44 | e | x:float, y:float, z:float, w:float | int 0 | no | `FFX_AtelSys_Came_053_RefSetDamping_resi` | came:23 on the look-at point |  | V |
| 54 | 0 | 0 |  | a:float, maxSpeed:float, b:float | int 0 | no | dead `FFX_AtelSys_Came_054_RefSetSpeedLimit_resi` | came:24 on the look-at point |  | V |
| 55 | 0 | 0 |  |  | int handle | no | dead `FFX_AtelSys_Came_055_RefClearSpeedLimit_resi` | came:25 on the look-at point |  | V |
| 56 | 38427 | 863 | e,m,b,s |  | none | yes | `FFX_AtelSys_Came_056_WaitRefMoveDone_poll` | poll only, came:26 for the look-at point, without the debug print | camread | V |
| 57 | 0 | 0 |  |  | int mask | no | dead `FFX_AtelSys_Came_057_GetRefMoveState_resi` | came:27 for the look-at point | camread | V |
| 58 | 132521 | 1328 | e,m,b,s | deg:float | int handle | no | `FFX_AtelSys_Came_058_SetRoll_resi` | sub_7BFA80: the roll target of the handle's record and slot +0xC94, snapped into +0xC90 when no roll tween runs |  | V |
| 59 | 134067 | 1334 | e,m,b,s | dpt:float | int handle | no | kept `MsCameraSetScrDpt` | sub_7BFBD0: the ScrDpt (projection distance) target, slot +0xCA0, snapped into +0xC9C when no tween runs |  | V |
| 60 | 347 | 20 | e | aDeg:float, bDeg:float, c:float, unused:float | int 0 | no | `FFX_AtelSys_Came_060_CamSetTargetActorHeight_resi` | FFX_AtelSys_Came_SetTargetActorHeight on the eye, mode 10 'act2'. The last pushed value is popped and dropped |  | V |
| 61 | 0 | 0 |  | aDeg:float, bDeg:float, c:float, unused:float | int 0 | no | dead `FFX_AtelSys_Came_061_RefSetTargetActorHeight_resi` | came:60 on the look-at point |  | V |
| 62 | 0 | 0 |  | target0:int, target1:int, zOfs:float | int 0 | no | dead `FFX_AtelSys_Came_062_CamSetBattleBpos_resi` | sub_7BAD10 -> FFX_Came_SetBattleTarget kind 0, mode 11 'bpos', zOfs / 100, battle handle |  | V |
| 63 | 243 | 95 | e,m,b,s | target0:int, target1:int, zOfs:float | int 0 | no | `FFX_AtelSys_Came_063_RefSetBattleBpos_resi` | came:62 on the look-at point |  | V |
| 64 | 1928 | 584 | e,m,b,s | target0:int, target1:int, zOfs:float, aDeg:float, bDeg:float, len:float | int 0 | no | `FFX_AtelSys_Came_064_CamSetBattleBpl1_resi` | FFX_Came_SetBattleTarget through sub_7BADB0 with the battle handle (class 5), kind 1, mode 12 'bpl1', on the eye. zOfs and len / 100. sub_7BADB0 patches the arguments for magic ids 88, 146, 163, 201, 704, 705 and 76/311 on some battles (sub_782970), and at level 8207 under magic 251 hides the CHRs chr[dword_11334D0..1] and clears dword_1136B58 (corrected 2026-10-29, that hide and the 0x1A30004 and 0x1F60004 patches sit behind exact compares that never hold on PC, section 5.4) | global | V |
| 65 | 105778 | 829 | e,m,b,s | target0:int, target1:int, zOfs:float, aDeg:float, bDeg:float, len:float | int 0 | no | `FFX_AtelSys_Came_065_RefSetBattleBpl1_resi` | came:64 on the look-at point | global | V |
| 66 | 5 | 3 | e | mask:int | int handle | no | `FFX_AtelSys_Came_066_RefSetMoveMask_resi` | sub_7BDF90: record+7 = ~mask, the axes (1 x, 2 y, 4 z, 8 roll, 0x10 ScrDpt) the next move tweens. Cleared after each move |  | V |
| 67 | 405 | 27 | e | mask:int | int handle | no | `FFX_AtelSys_Came_067_CamSetMoveMask_resi` | came:66 on the eye |  | V |
| 68 | 101175 | 828 | e,m,b,s | target0:int, target1:int, zOfs:float, aDeg:float, bDeg:float, len:float | int 0 | no | `FFX_AtelSys_Came_068_CamSetBattleBpl2_resi` | FFX_Came_SetBattleTarget through sub_7BADB0 with the battle handle (class 5), kind 2, mode 13 'bpl2', on the eye | global | V |
| 69 | 5386 | 650 | e,m,b,s | target0:int, target1:int, zOfs:float, aDeg:float, bDeg:float, len:float | int 0 | no | `FFX_AtelSys_Came_069_RefSetBattleBpl2_resi` | came:68 on the look-at point | global | V |
| 70 | 394 | 99 | e,b | points:int, a:float, b:float, kind:int | int handle | no | `FFX_AtelSys_Came_070_CamSetSpline_resi` | sub_7BB970 -> FFX_Came_SetSpline, mode 14 'spln'. points 0 or 1 take the recorded path of came:73/75 (3 points at least), anything else is a script variable address | global, local | V |
| 71 | 388 | 101 | e,b | points:int, a:float, b:float, kind:int | int handle | no | `FFX_AtelSys_Came_071_RefSetSpline_resi` | came:70 on the look-at point | global, local | V |
| 72 | 399 | 101 | e,b |  | int handle | no | `FFX_AtelSys_Came_072_CamPathClear_resi` | dword_1136B64 = 0 when the own handle is the active one, empties the eye path buffer | global | V |
| 73 | 3236 | 101 | e,b |  | int handle | no | `FFX_AtelSys_Came_073_CamPathAddPoint_resi` | sub_7BAAC0: appends the pending eye position to the 32 entry path buffer at 0x1136B68, active handle only | global | V |
| 74 | 391 | 101 | e,b |  | int handle | no | `FFX_AtelSys_Came_074_RefPathClear_resi` | came:72 for the look-at path, dword_1136B60 | global | V |
| 75 | 3182 | 101 | e,b |  | int handle | no | `FFX_AtelSys_Came_075_RefPathAddPoint_resi` | came:73 for the look-at path | global | V |
| 76 | 6 | 2 | e,b | target0:int, target1:int, zOfs:float, aDeg:float, bDeg:float, len:float | int 0 | no | `FFX_AtelSys_Came_076_CamSetBattleCpl1_resi` | FFX_Came_SetBattleTarget through sub_7BADB0 with the battle handle (class 5), kind 3, mode 15 'cpl1', on the eye | global | V |
| 77 | 68 | 22 | e,m,b,s | target0:int, target1:int, zOfs:float, aDeg:float, bDeg:float, len:float | int 0 | no | `FFX_AtelSys_Came_077_CamSetBattleCpl2_resi` | FFX_Came_SetBattleTarget through sub_7BADB0 with the battle handle (class 5), kind 4, mode 16 'cpl2', on the eye | global | V |
| 78 | 14 | 2 | e | screen:int, x:int, y:int, w:int, h:int, kind:int | int 0 | no | kept `FFX_AtelCmd_CameScreenOpen` | FFX_Came_SetScreenEnable(screen, 1) and FFX_Came_SetScreenRect |  | V |
| 79 | 1 | 1 | e | screen:int | int 0 | no | kept `FFX_AtelCmd_CameScreenClose` | FFX_Came_SetScreenEnableAndSlot(screen, 0, screen), which also puts the screen back on its own slot |  | V |
| 80 | 11 | 2 | e | screen:int, x:int, y:int, w:int, h:int | int 0 | no | `FFX_AtelSys_Came_080_SetScreenDrwRect_resi` | sub_7BC750: the DRW target (screen +0x90) and rect (+0x50) |  | V |
| 81 | 10 | 3 | e | screen:int, mode:int | int 0 | no | `FFX_AtelSys_Came_081_SetScreenDrwMode_resi` | sub_7BBDD0: screen +0xC3, 1 take size, 2 copy rect, 3 centre |  | V |
| 82 | 4 | 2 | e | screen:int, mode:int | int 0 | no | `FFX_AtelSys_Came_082_SetScreenWinMode_resi` | screen +0xC2, the WIN twin of came:81 |  | V |
| 83 | 0 | 0 |  | screen:int, frames:int | int 0 | no | dead `FFX_AtelSys_Came_083_ScreenWinTweenLinear_resi` | sub_7BBDF0: the four WIN rect tweens, linear over frames |  | V |
| 84 | 0 | 0 |  | screen:int, frames:int | int 0 | no | dead `FFX_AtelSys_Came_084_ScreenWinTweenCosine_resi` | as came:83, cosine ease |  | V |
| 85 | 5 | 1 | e | screen:int, accel:int, hold:int, decel:int | int 0 | no | `FFX_AtelSys_Came_085_ScreenWinTweenTrapezoid_resi` | sub_7BBEB0: the four WIN rect tweens, trapezoid |  | V |
| 86 | 0 | 0 |  | screen:int, frames:int | int 0 | no | dead `FFX_AtelSys_Came_086_ScreenDrwTweenLinear_resi` | came:83 for the DRW rect |  | V |
| 87 | 0 | 0 |  | screen:int, frames:int | int 0 | no | dead `FFX_AtelSys_Came_087_ScreenDrwTweenCosine_resi` | came:84 for the DRW rect |  | V |
| 88 | 1 | 1 | e | screen:int, accel:int, hold:int, decel:int | int 0 | no | `FFX_AtelSys_Came_088_ScreenDrwTweenTrapezoid_resi` | came:85 for the DRW rect |  | V |
| 89 | 25 | 12 | e | points:int, kind:int | int 0 | no | `FFX_AtelSys_Came_089_RefAddRailClamp_resi` | sub_7BFE70: a clamp of kind 4 (5 when kind is 3) that pulls the look-at point onto a spline through sub_A44120, step limit 0 | global, local | V |
| 90 | 569 | 117 | e | points:int, rate:float, kind:int | int 0 | no | `FFX_AtelSys_Came_090_RefAddRailClampRate_resi` | as came:89 with the step limit (record+32) from the script | global, local | V |
| 91 | 594 | 125 | e | points:int, kind:int | int 0 | no | `FFX_AtelSys_Came_091_CamSetSpline2_resi` | FFX_Came_SetSpline2, mode 17 'spl2', on the eye, the same point sources as came:70 | global, local | V |
| 92 | 171 | 19 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_092_RefShakeSineOut_resi` | sub_7C4FC0: arms the slot shake records picked by mask with wave type 1, sine fading out, look-at point. Active handle only | rng* | V |
| 93 | 164 | 34 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_093_CamShakeSineOut_resi` | came:92 on the eye | rng* | V |
| 94 | 2 | 1 | e | screen:int, mask:int, rate:float, duration:int, amp:int, range:int | int 0 | no | `FFX_AtelSys_Came_094_ScreenShakeSineOut_resi` | FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 1, sine fading out, through FFX_AtelCmd_CameScreenShake | rng* | V |
| 95 | 60 | 18 | e |  | int 0 | no | `FFX_AtelSys_Came_095_RefClearShake_resi` | sub_7C4F50: clears the three slot shake records of the look-at point, active handle only |  | V |
| 96 | 353 | 80 | e |  | int 0 | no | `FFX_AtelSys_Came_096_CamClearShake_resi` | came:95 on the eye |  | V |
| 97 | 17 | 10 | e | screen:int | int 0 | no | `FFX_AtelSys_Came_097_ClearScreenShake_resi` | FFX_Came_ClearScreenShake |  | V |
| 98 | 49 | 12 | e |  | none | yes | `FFX_AtelSys_Came_098_WaitRefShakeDone_poll` | poll only, returns 0 while sub_7C4F10 finds a live slot shake record on the look-at point, else 5 | camread | V |
| 99 | 55 | 15 | e |  | none | yes | `FFX_AtelSys_Came_099_WaitCamShakeDone_poll` | came:98 for the eye | camread | V |
| 100 | 83 | 21 | e,b | screen:int | none | yes | `FFX_AtelSys_Came_100_WaitScreenShakeDone_start`, `FFX_AtelSys_Came_100_WaitScreenShakeDone_poll` | start stores the screen, poll returns 0 while FFX_Came_IsScreenShakeActive | camread | V |
| 101 | 6 | 3 | b | prio:int | int handle | no | `FFX_AtelSys_Came_101_SetCameraPriority_resi` | sub_7BE0A0: the priority byte (+6) of every entry of the own handle, ignored while dword_2311424 is set (sub_7BE100, from FFX_Btl_BeginBattle) | global | V |
| 102 | 3 | 3 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_102_RefShakeSineIn_resi` | sub_7C4FC0: arms the slot shake records picked by mask with wave type 2, sine fading in, look-at point | rng* | V |
| 103 | 7 | 2 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_103_CamShakeSineIn_resi` | came:102 on the eye | rng* | V |
| 104 | 0 | 0 |  | screen:int, mask:int, rate:float, duration:int, amp:int, range:int | int 0 | no | dead `FFX_AtelSys_Came_104_ScreenShakeSineIn_resi` | FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 2, sine fading in | rng* | V |
| 105 | 10 | 5 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_105_RefShakeMixOut_resi` | sub_7C4FC0: arms the slot shake records picked by mask with wave type 3, sin plus cos(t + 0.3) fading out | rng* | V |
| 106 | 0 | 0 |  | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | dead `FFX_AtelSys_Came_106_CamShakeMixOut_resi` | came:105 on the eye | rng* | V |
| 107 | 72 | 28 | e,b | screen:int, mask:int, rate:float, duration:int, amp:int, range:int | int 0 | no | `FFX_AtelSys_Came_107_ScreenShakeMixOut_resi` | FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 3 | rng* | V |
| 108 | 2 | 2 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_108_RefShakeMixIn_resi` | sub_7C4FC0: arms the slot shake records picked by mask with wave type 4, sin plus cos(t + 0.3) fading in | rng* | V |
| 109 | 0 | 0 |  | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | dead `FFX_AtelSys_Came_109_CamShakeMixIn_resi` | came:108 on the eye | rng* | V |
| 110 | 11 | 7 | e | screen:int, mask:int, rate:float, duration:int, amp:int, range:int | int 0 | no | `FFX_AtelSys_Came_110_ScreenShakeMixIn_resi` | FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 4 | rng* | V |
| 111 | 0 | 0 |  | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | dead `FFX_AtelSys_Came_111_RefShakeStutterOut_resi` | sub_7C4FC0: arms the slot shake records picked by mask with wave type 5, fading out with a second stream 9 draw that halves the held amplitude at random | rng* | V |
| 112 | 0 | 0 |  | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | dead `FFX_AtelSys_Came_112_CamShakeStutterOut_resi` | came:111 on the eye | rng* | V |
| 113 | 73 | 29 | e | screen:int, mask:int, rate:float, duration:int, amp:int, range:int | int 0 | no | `FFX_AtelSys_Came_113_ScreenShakeStutterOut_resi` | FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 5 | rng* | V |
| 114 | 0 | 0 |  | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | dead `FFX_AtelSys_Came_114_RefShakeStutterIn_resi` | sub_7C4FC0: arms the slot shake records picked by mask with wave type 6, the fading in twin of type 5 | rng* | V |
| 115 | 0 | 0 |  | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | dead `FFX_AtelSys_Came_115_CamShakeStutterIn_resi` | came:114 on the eye | rng* | V |
| 116 | 12 | 8 | e | screen:int, mask:int, rate:float, duration:int, amp:int, range:int | int 0 | no | `FFX_AtelSys_Came_116_ScreenShakeStutterIn_resi` | FFX_Came_SetScreenShake: arms the screen shake channels picked by mask with wave type 6 | rng* | V |
| 117 | 0 | 0 |  | screen:int, slot:int | int 0 | no | kept `FFX_AtelCmd_CameScreenSetSlot` | FFX_Came_SetScreenSlot, screen +0xC1 |  | V |
| 118 | 25 | 17 | e |  | int 0 | no | `FFX_AtelSys_Came_118_CloseBattleCameras_resi` | sub_7BA950: closes the battle and viewer handles of all three screens and sets screen +0xD0, the depth of field mode FFX_Came_ActivateScreen last sent, to -1 so the next activation reloads it | global | V |
| 119 | 13 | 13 | m,b,s |  | float degrees | no | `FFX_AtelSys_Came_119_GetRollDeg_resf` | FFX_Came_GetRoll (g_ffxCameActiveSlot +0xC90) unwrapped near 0 and turned into degrees. The Hex-Rays output drops the call result, the disassembly at 0x7B91AB keeps it | camread | V |
| 120 | 13 | 13 | m,b,s |  | float ScrDpt | no | kept `j_FFX_Came_GetScrDpt` | A thunk to FFX_Came_GetScrDpt (g_ffxCameActiveSlot +0xC9C) | camread | V |
| 121 | 0 | 0 |  | screen:int | int 0 | no | dead `FFX_AtelSys_Came_121_StopScreenWinTween_resi` | sub_7BC010: zeroes the four WIN tweens and snaps the rect to its target |  | V |
| 122 | 0 | 0 |  | screen:int | int 0 | no | dead `FFX_AtelSys_Came_122_StopScreenDrwTween_resi` | came:121 for the DRW rect |  | V |
| 123 | 4 | 1 | e | screen:int | none | yes | `FFX_AtelSys_Came_123_WaitScreenWinTween_start`, `FFX_AtelSys_Came_123_WaitScreenWinTween_poll` | start stores the screen, poll returns 0 while sub_7BBFC0 counts a live WIN tween | camread | V |
| 124 | 0 | 0 |  | screen:int | none | yes | dead `FFX_AtelSys_Came_124_WaitScreenDrwTween_start`, dead `FFX_AtelSys_Came_124_WaitScreenDrwTween_poll` | came:123 for the DRW rect | camread | V |
| 125 | 125 | 36 | e,m,b,s | strength:int | int handle | no | `FFX_AtelSys_Came_125_SetScreenBlur_resi` | sub_7BCDD0 -> sub_82BEE0 when the own handle is active: the blur that SG_DebugWin_BlurProc and FFX_Btl_BeginBattle also drive, strength / 128 | render | I |
| 126 | 608 | 603 | e,b | id:int | int handle | no | `FFX_AtelSys_Came_126_SetDofEnabled_resi` | FFX_Came_SetDofEnabled 0x7BCF50: slot +0xCAC (and the record copy), with +0xCA8 dirty. A negative value keeps the record's value with bit 2. FFX_Came_ActivateScreen hands it to FFX_Gfx_SetDofEnabled 0x82CB60 | render | V |
| 127 | 626 | 604 | e,b | which:int, id:int | int handle | no | `FFX_AtelSys_Came_127_SetDofDepth_resi` | FFX_Came_SetDofDepth 0x7BF280 (was FFX_Came_SetRoll), which writes slot +0xCB0 + 4 * which and sets +0xCA8, the depth of field depths ActivateScreen passes to FFX_Gfx_SetDofDepth 0x82CE20. It does not touch the roll | render | V |
| 128 | 579 | 202 | m,b,s | which:int | int draw | no | `FFX_AtelSys_Came_128_RandCamera_resi` | FFX_Rand_Stream(14 + (which & 1)) returned to the script. Under magic 146 with the came:22 latch set it returns 0 or 2 instead and clears the latch | rng, global | V |
| 129 | 6 | 4 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_129_RefShakeSineSteady_resi` | sub_7C4FC0: arms the slot shake records picked by mask with wave type 7, sine with no envelope | rng* | V |
| 130 | 4 | 3 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_130_CamShakeSineSteady_resi` | came:129 on the eye | rng* | V |
| 131 | 42 | 7 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_131_RefShakeMixSteady_resi` | sub_7C4FC0: arms the slot shake records picked by mask with wave type 8, sin plus cos(t + 0.1) with no envelope | rng* | V |
| 132 | 42 | 7 | e | mask:int, rate:float, duration:int, amp:int, range:int | int handle | no | `FFX_AtelSys_Came_132_CamShakeMixSteady_resi` | came:131 on the eye | rng* | V |
| 133 | 3 | 3 | b | xVar:int, yVar:int, zVar:int | int 0 | no | `FFX_AtelSys_Came_133_CamGetLivePosToVars_resi` | sub_7BD4B0: the live eye position (sub-block +0x10, committed plus shake) into three script variables | camread, rng* | V |
| 134 | 3 | 3 | b | xVar:int, yVar:int, zVar:int | int 0 | no | `FFX_AtelSys_Came_134_RefGetLivePosToVars_resi` | came:133 for the live look-at point | camread, rng* | V |
| 135 | 132 | 15 | e |  | int 0 | no | `FFX_AtelSys_Came_135_RefRestorePriorityPos_resi` | sub_7BEAE0: copies record+128, the position this priority last committed, into the committed and pending look-at point, active handle only |  | V |
| 136 | 132 | 15 | e |  | int 0 | no | `FFX_AtelSys_Came_136_CamRestorePriorityPos_resi` | came:135 on the eye, plus the record's roll and ScrDpt |  | V |
| 137 | 0 | 0 |  | screen:int, mask:int, iterations:int, duration:int, v0:float, randDiv:int, accel:float, envelope:int | int 0 | no | dead `FFX_AtelSys_Came_137_ScreenSpringShake_resi` | FFX_Came_SetScreenSpringShake, type 9. Draws stream 9 in the setter when randDiv is nonzero | rng | V |
