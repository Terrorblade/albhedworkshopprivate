# The ATEL Core syscall library, all 616 rows

2026-10-26. PROGRESS 1.8.6. Library 0 of `g_ffxAtelSysFuncLibs 0x1328558`, the table
`g_ffxAtelSysFuncLib00_Core 0xC50050`, 616 rows of {start, poll, float getter, int getter}. The ABI is
`ATEL_CONTEXT.md` 11.7 and 13. This note names every row that a shipped script calls, marks the rest
dead, and lists what each row means for lockstep. `../tools/apply_core_names.py` puts the names in the
IDB. All addresses are IDA VAs at base 0x400000.

Read only throughout. The handlers were decompiled through idalib on a scratch copy of the IDB, with
nothing renamed and nothing saved. The census walked the install with `../tools/ebp.py` and the
recreation's `tools/mon_ai.py` loader, and no script bytes were copied anywhere.

Marks: [V] read in the decompile or the disassembly, [I] inferred (a role read from the shape of a
wrapper whose callee was not read, an engine global with no traced reader), [D] counted in shipped
data. The table carries V or I per row.

## 1. Short version

- **435 of the 616 indexes are called by shipped scripts, 181 are not.** 1,861,120 call sites in all.
  Every one of the 435 has a descriptive role now. Of the 181 dead rows one is the null row (435), and
  the rest got a role too where the body made it obvious, with the name left positional. [D] [V]
- **Names follow the slot suffix form, `FFX_AtelSys_Core_NNN_<Role>_<slot>`**, with `<slot>` one of
  `start`, `poll`, `resf`, `resi`. That is the form the live IDB uses since today (for example
  `FFX_AtelSys_Core_084_SetTriggerLine_resi`). The older names, the `FFX_AtelOp_*` rows and the roughly
  20 `FFX_AtelSys_Core_NNN_<Role>` rows without a suffix, stay as they are. Section 3 lists them, and
  four of them are wrong.
- **Only two rows draw from an RNG stream, core:166 RandBelow and core:169 Rand16, both
  `FFX_Rand_Stream(2)`.** [V] core:169 is mostly a battle row: 27,812 of its 28,308 calls are in the
  816 battle field scripts (566 files), 396 in monster AI, 99 in system_01 and 1 in an event package. [D]
- **The step pacing leaks into three syscalls.** core:213 PrepareVoice and core:258 LoadBgm block
  while `g_ffxPendingSteps > 1`, and core:0 WaitFrames counts FMV decoder frames instead of steps
  while certain movies play. Two peers with a different presented to simulated step ratio finish
  these on different steps. [V]
- **Seven rows wait on disk.** core:213, 219, 220, 222, 317, 453 and 515 hold the script until an
  asynchronous load or save operation completes. [V]
- **Language and region reach the simulation in a few places.** The worst is in the chest grants:
  `FFX_Treasure_GiveReward` swaps item 0x2065 for 0x2060 when the language is 11, so core:347 and
  core:423 give a different item by language. core:595 returns a per machine master version to the
  script. Most other language reads only move text. Section 5.
- **Pad reads.** The held, pressed and released masks (core:68 to 81, 19 used rows carry the input
  flag counting the message waits) are step derived. The auto repeat words (core:69, 73, 79, 83, 591 to 594) run off a
  wall clock timer, and only core:593 is used (12 calls, hiku2100). [V]

## 2. The census [D]

Sources, all read from `FFX_Data.vbf`:

| source | files | Core calls |
|---|---|---|
| event packages, `jppc/event/obj/*.ebp` | 397 | 1,826,629 |
| battle field files, `battle/btl/*/*.bin` section at header +4 | 816 | 32,110 |
| monster AI, `battle/mon/_mNNN/mNNN.bin` section 0 | 361 | 2,074 |
| `battle/btl/system_01/system_01.bin` (the jppc copy, the inpc one is a duplicate) | 1 | 307 |

Only six Core indexes occur outside the event packages: core:0, 2, 95, 169, 304 and 393. The monster
AI uses only core:0, 95 and 169, and system_01 uses core:0, 2, 95 and 169.

The arity column comes from the handler's pops. The census also derives one from the bytecode: at
every `0x58` call (result into R) the operand stack depth since the last statement boundary is the
argument count, and a one unknown solve handles nested `0x35` calls. Where both exist they agree on
every used row. 100 used rows have no static estimate (they are only called in expression form), and
their arity is the pop count alone.

The heaviest rows by calls: core:283 SetEvTexSprite 410,172 in 35 packages, 284 139,694, 297 129,465,
0 WaitFrames 124,718 in 686 files, 288 108,201, 287 48,683, 158 SetMessageNumberSlot 45,839, the
message window quintet 100, 101, 102, 106, 157 at about 33,000 each, 107 32,223, 169 28,308, 51
GetActorId 27,417. The event texture sprite rows (282 to 288, 297, 298) are 35 packages that draw
2D sprite scenes frame by frame.

Priority sets. Event 23 (the title, `test20`) contains calls to 69 indexes, and the Besaid packages
(`bsil*`, `bsvr*`) to 236. All of them are named.

43 used rows are called from a single package, and 25 used rows have a single call site.

Dead rows, 181: 8-15, 20, 30, 34-36, 38, 39, 49, 50, 59, 60, 62, 64, 65, 69, 72-75, 78, 79, 82, 83, 91, 103, 104,
114, 115, 117, 121, 123, 126, 138, 140, 142, 153, 156, 159-161, 173-176, 181-185, 195, 205, 211,
212, 221, 229, 234, 240, 241, 247, 252, 255, 257, 275, 280, 286, 291-293, 300, 303, 305, 314, 316,
327, 331, 332, 336, 338, 339, 346, 348, 354-357, 360, 361, 368, 371, 373, 374, 376, 381, 386, 387,
390, 391, 396-398, 400, 401, 410, 411, 417-419, 425, 426, 430, 435, 436, 441, 446, 449, 455, 456,
459, 461, 467, 469-472, 475, 477, 479, 481, 488, 490, 492, 493, 495, 497, 501-503, 509-511, 520,
524, 526, 529, 532, 536, 537, 540, 543, 546-548, 551, 552, 554-557, 560, 562, 563, 568, 574,
582-585, 590-592, 594, 600, 602.

Two dead rows are worth knowing. core:493 is a real infinite loop in the binary: it prints a
"Virtuos Warnig: Need to check this func" stub, pops, pushes 2 and the entry, and jumps back, so a
script that called it would hang. core:540's IDB comment says nagi0000 calls it, but neither nagi0000
nor dbg_nagi0000 contains a call (they call core:535). [V] [D]

## 3. Names that were already there

Kept as they are, and checked against the bodies [V]:

- `FFX_AtelOp_*`: 0 WaitFrames (start, poll), 1 SetActorModel, 21 SetMoveTargetXYZ, 24 StartMove,
  25 SetRotateCmd, 51 GetActorId, 93 PlayerControlOn, 94 PlayerControlOff, 95 WaitForever (poll), 100
  MesWinSetMessage, 105 MesWinSetChoiceCursorDefaults, 106 MesWinShow, 107 MesWinClose, 114
  SetMoveFrames, 124 MesWinWaitConfirm124 (all three), 125 MesWinWaitChoice, 132 MesWinWaitConfirm132,
  143 MesWinIsIdleOrAnswered, 149 SetActorYaw, 152 SetMoveSpline, 251 MesWinGetLiveChoiceCursor, 271
  MesWinSetNumericEntryDefaults, 272 MesWinGetNumericValue, 313 and 314 AskNumber, 315 and 316
  AskChoice, 489 MesWinCloseAndWaitGone, and 615's start, which is the same function as 315's start
  (0x8602A0).
- 70 `FFX_Atel_GetPadPressedRemapped`.
- The suffixless `FFX_AtelSys_Core_NNN_<Role>` rows: 187 SetFadeOutFrames, 267
  WarpNoSavedFade, 428 GetCaptureCount, 429 AddCapture1, 430 SubCapture1, 431 AddCaptureN, 506 GetSaveLoadMenuResult, 536 GetCaptureCount, 537 AddCaptureN, 538
  CountAreaSpeciesAtLeast, 539 CountSpeciesGroupAtLeast, 540 CountAllSpeciesAtLeast, 572
  AllWorldFlagsSet, 573 BeginNewGame, 586 SetOptionBit13, 596 SetSphereGridTypeAndInit.
- Today's trigger names in the live IDB (84 to 87, 89, 90, 92, 122, 139, 154, 240, 306) are confirmed. On the
  older copy these rows are still positional, so the apply script proposes the same names there.

Wrong or misleading. The script never overwrites a descriptive name, so these were renamed by hand on the
live IDB on 2026-10-26 after checking each body, along with the callees `FFX_SaveData_SetWarpFadeKind`
and `FFX_SaveData_ResetPremovieState`. The "current" column is the old name:

| row | current | what it is |
|---|---|---|
| 188 | `FFX_AtelSys_Core_188_SetFadeInFrames` | writes save byte +0x11, which `FFX_Map_WarpToWithSavedFade` tests only as a fade kind (0 mode 3, 1 mode 5). The frame count is +0x12, core:187. Role `SetWarpFadeKind` |
| 247 | `FFX_AtelOp_ResolvePartyMemberActor` | start and poll only, no getter, so nothing is pushed back. It resolves a party slot to its actor and waits until that actor has no task at or above a level, the party twin of core:246. Role `WaitPartyMemberTasks`. Dead |
| 261 poll | `__RTC_NumErrors_808` | a `return 5` stub misnamed by a signature. The row is an empty start, poll 5, getter 1, the PC no op paired 1:1 with core:258. Role `WaitBgmLoaded` |
| 450 | `FFX_AtelSys_Core_450_ClampOptionWordE0` | save +0xE0 is the premovie state (with +0xD8 the movie number and +0xDC the mode, core:432, 433, 437, 449), and this resets it to 1 from 2..5. Role `ResetPremovieState` |

Comment corrections, no rename, applied to the live IDB on 2026-10-26: core:25's first pushed argument is the channel mask, not a speed. core:93
calls only `FFX_Atel_BindPlayerChr`, not unbind then bind. core:215's comment calls it VoiceStatus, but
core:216 is the one that prints VoiceStatusStart. The callee `FFX_SaveData_TestCharAbilityBit` used by
core:510 tests per character ability bits (`word_113209A` is the char records +0x3E at a 74 word
stride).

Slots that are not functions in the IDB, so the apply script skips them: 214 resi `loc_85DE80`
(VoiceStart result), 449 resi `loc_85A420` (dead), 507 resi `loc_85AF10`, which is `jmp 0x8BEEF0`, and
that is `mov eax, dword_186A228` / `ret`. `FFX_SaveLoadMenu_SetResult` writes `dword_186A228`. [V]

## 4. The families

Counts are used rows. The family is the table's grouping, the full list is in the table.

### 4.1 Flags and variables (35 rows, plus the system rows)

- Save block flag sets: world flags at save +0x3DCC (528 SetWorldFlag 2,724 calls, 571 TestWorldFlag,
  572 AllWorldFlagsSet), event flag bits at save +0x4C (569, and 568, 305 dead), the LoveParam
  affection words at save +0x2C (301, 302, 304, and 333 picks the top one), capture counts (428, 429,
  431, and the Arena counters 538, 539).
- Two per map signal masks that scripts use to hand off between actors: `dword_1326B70` (378 set, 379
  clear, 377 waits for a bit) and `dword_1326B74` (394, 395). These are process globals outside the
  save block, so a desync hash needs them. [V]
- Context bytes: context +2 bits 0x08, 0x10/0x04 (indoor override, 276), 0x40, 0x80 (277, 299, 329),
  context 0 byte 3 (242, 349, 350), context +0x210 bits 0x08 and 0x40 (577, 589). core:577 SetContext
  Flag210Bit8 has 1,800 calls and brackets scripted sequences, its reader is not traced.
- Small save words with no traced reader: +0x2B (250), +0xD7 (445), +0xE4 (451), +0xE8 (452), +0xEA
  (525).

### 4.2 Actors and CHR (54 actor rows, 15 chr rows)

- The attachment at actor +0xA8 / +0xAA: core:1 and 308 SetActorModel (kind 1, a CHR), core:2
  BindCameraHandle (kind 2), 3 and 4 (kinds 3 and 4, not identified), 407 AttachActorToMapNode (kind 5),
  5 and 413 release it by kind. 515 LoadAndSetActorModel streams the model in first and then runs
  core:1, so it blocks on disk. 6 and 7 attach and detach a model on a parent bone.
- Position and pose: 19 SetActorPos and 294 SetActorPosSnapped (snap the previous position), 56 to 58
  GetActorPos, 149 SetActorYaw (stops the rotate command first), 144 to 147 heading and rotY readers,
  164 and 165 distances, 244 and 245 yaw and pitch to a point.
- Thread and event control: 52 and 53 the event mask +0xAB that `FFX_Atel_FireActorEvent` tests (15,000
  calls between them), 167 and 168 the alive bit +0x34 bit 2 that RunScript gates on, 189 kills
  requests above a level, 278 and 279 the extra run pass bits of +0x36, 318 and 375 the gated context
  and battle bits.
- The actor flag word +0x38 bits (458, 464, 523, 553, 561, 588) and the CHR effect kinds (517 to 522)
  have no traced reader.

### 4.3 Motion (50 rows)

The MOVE command is at actor +0x5B8 + 76 * channel, the ROTATE command at actor +0x864 + 68 * channel
(`FFX_GAME_NOTES.md`). Two naming patterns:

- A setter for the current channel and an `All` twin that writes all 9 channels: 22 and 108 (MOVE
  +0x10, probably speed), 23 and 45 (+0x38, the arrival distance), 43 and 109 (+0x3C yaw turn rate),
  44 and 110 (+0x40 pitch turn rate), 46 and 111 (ROTATE +0x10), 47 and 112 (+0x18), 48 and 113
  (+0x1C), 227 and 228 (+0x14), and 116 sets the frame count on all channels (its single channel twin
  114 is dead). The `All` twins are called once per
  package from a type 0 subroutine (actor type 0 in the census is the subroutine container, the code
  runs for the type 1 actor that called it), which is character default setup.
- Field shaped names (`SetMoveCmdField44`, `SetRotCmdField18` and so on) are rows whose field meaning
  is not established. A read of `FFX_Atel_StepMoveCmd 0x868990` would settle them.
- Waits: 26 WaitMoveDone and 27 WaitRotateDone are poll only, done when the command's kind bits clear.
  Step driven.

### 4.4 Player control (15 rows)

- 93 PlayerControlOn and 94 PlayerControlOff bind and unbind the pad driven CHR
  (`FFX_Atel_BindPlayerChr`), 4,600 calls each. 96 SetActorControllable sets +0x34 bit 0x200, the bit
  the bind requires, and rebinds. [V]
- 67 SetSelfAsPlayerActor writes the single player actor slot `ctx+10` with the caller's id (the
  Blitzball ownership slot, 10 sites in bltz0000/2/4). 126 reads it back (dead). 66 BindActorPartyChar
  maps a type 1 actor to a party character index (+0x40) and gives bit 0x200 to index 0 only. 414 and
  415 test whether a bind is allowed. [V]
- Party roster: 202 AddPartyMember, 203 RemovePartyMember, 505 the locked removal, 231 the order
  slots, 230, 232, 236 and 237 the field party, 273 and 274 push and pop a party snapshot in the save
  block, 422 fills empty field slots.

For co-op these are the rows where "who is the player" is decided by script. They all write the one
`ctx+10` slot or the one bound CHR, which is `COOP_BODY` territory (the ownership table).

### 4.5 Camera hand offs (2 rows here, the rest is library 6)

- core:2 BindCameraHandle (2,290 calls, 815 battle files and 334 event packages): releases the old
  attachment, sets +0xAA = 2 and +0xA8 to the handle index, caches
  `FFX_Came_GetHandleForClass(5, flag, n)` at actor +0x3C, and for trigger types sets +0x34 bits 13..14
  = 2 (ATEL_CONTEXT 12.5 item 8). The crossing line triggers use it to switch the field camera.
- core:243 SetGuideMapLayout picks guide map (minimap) layout preset 0..4 (`RCBG.md` 3). It was
  read as a background scene number, but no bg scene is involved.
- core:530 SetIndoorCameraHeightTest arms an indoor test by camera height (ctx +0x208), so a camera
  position feeds a context flag. Two calls.

### 4.6 Map and warp (25 rows)

- Warps: 17 and 171 WarpToMap (`FFX_Map_WarpToWithSavedFade(map, entry, 1)`), 267 and 268
  WarpNoSavedFade (mode 0). All four are gated by context byte 0 bit 0x80, and **core:190 SetWarpGate
  is the script setter of that gate** (130 calls in 52 packages), which the warp's IDB comment said it
  could not find. 187 and 188 set the saved fade frames and kind.
- 393 RequestMapZone writes `g_ffxPendingZoneMode = 2` and the zone, and yields one pass.
- Entry points: 16 the current one, 128 to 131 its position and yaw, 191 to 194 the same by index, 201
  the arrival script entry. Scene and map ids, current and previous: 134 to 137.
- 367 SetCheckpointEntry writes the checkpoint entry and map (save +0xB8, +0xBA).

### 4.7 Message (37 rows)

- The window quintet every line of dialogue runs: 100 MesWinSetMessage, 101 MesWinSetPosition, 102
  MesWinSetPalette, 157 MesWinSetFlags, 106 MesWinShow, then 107 MesWinClose (CUTSCENE.md).
- Waits: 124 and 132 MesWinWaitConfirm, 125 MesWinWaitChoice, 489 MesWinCloseAndWaitGone, and the
  composites 313 AskNumber and 315 AskChoice. 143 and 151 are non blocking state reads that scripts
  poll in a loop instead.
- 158 SetMessageNumberSlot (45,839 calls) sets the numbers a message shows, a format byte and a value
  per window and slot.
- 319 GetDicString returns a raw pointer into the dictionary of the current language, and 566 the same
  for a message. A script should only pass these on, never compare them.
- Text overlays at 0x1328150 (8 slots x 2 layers): 472 to 478, 491 to 495, inferred.
  Corrected 2026-10-29: these are pad vibration channels, not text overlays. `FFX_Pad_StepVibrationChannels
  0x875B40` takes the largest level of each layer over the 8 channels and `FFX_Atel_StepFieldAll 0x872BD0`
  writes them doubled into pad port 0's +0xFF and +0xFE, the rumble bytes (`FFX_Pad_SetVibrationMotor0
  0x889490`, `Motor1 0x8894B0`). On PC the bytes are never sent, `FFX_Pad__sampleIntoPortState` only calls
  setRumble when port +0x9D is set. The row names below that say TextOverlay are about rumble.
  `tools/apply_atel_step_names.py`. [V] the publish, [I] which motor is which

### 4.8 Sound (50 rows)

- Voice: 213 PrepareVoice (start, poll, result), 214 StartVoice, 217 WaitVoiceDone, 216 IsVoicePlaying,
  215 SkipVoiceUnlessTimed (renamed 2026-10-26, VOICE_TIMING.md). These follow the voice sync table (`FFX_SyncData_LookupVoiceSync`) and the
  audio position, see section 5.
- Music: 258 LoadBgm, 260 and 358 and 487 PlayBgm, 259, 364, 365 stop, 362, 363, 383, 384 volume and
  fades, 382 PlayBgmCrossfade, 392 FadeOutCurrentBgm. `dword_1327130` is the current track. Most of
  these are [I], the driver entries `sub_67Axxx` were not read.
- SE: 223, 253, 403, 416 play, 224 stop, 254 and 256 parameters. 262 to 266 bind a sound to a slot that
  follows an actor or a position.
- Character masks at `0x132FF2C`, `0x132FF34`, `0x132FF3C` (337 to 343, 511 to 514), by party index.

### 4.9 Effects (35 rows)

- Event texture sprites 282 to 288, 297, 298: the per frame 2D sprite scenes, by far the most called
  rows in the library.
- Global magic effects 219 LoadGlobalEffect, 220 PlayGlobalEffect, 222 WaitGlobalEffectIdle: the magic
  DLL load and playback state `byte_1133364`, blocking.
- Screen wave 482 to 486, map node animation 408, 409, 420, 421, 457, CHR effect sets 462, 463, 517 to
  522, fades 323 and 324 (whether the 0 and 127 targets are screen or volume is open).

### 4.10 RNG (2 rows)

| row | body | calls |
|---|---|---|
| core:166 RandBelow(n) | `n <= 0 ? 1 : n`, then `(n * (u16)FFX_Rand_Stream(2)) >> 16` | 13,775 in 90 event packages |
| core:169 Rand16() | `(u16)FFX_Rand_Stream(2)` | 28,308 in 736 files, 27,812 of them in battle field scripts |

[V], and the same as MONSTER_AI.md. No other row reaches `FFX_Rand_Stream`, `FFX_Btl_Rand`, the CRT
`rand` or the cosmetic streams, on a full forward closure of the direct call graph, once two false
paths are removed: `sub_7FEBA0` calls `sub_7B7170` with `a3 = 0` (the push at 0x7FEC57), and
`sub_7B7170` shuffles with stream 16 only when `a3 != 0`, so the CHR spawn and dispose rows (1, 2, 5, 6,
7, 308, 413, 462, 515, 517, 519, 521) do not draw. core:573 BeginNewGame reseeds every stream and
core:600 (dead) reaches the battle start rolls through a load path. Indirect calls (CHR and Phyre
virtuals) are not followed by the closure.

### 4.11 The other families

- **Triggers (15 rows).** The crossing line and box triggers, ATEL_CONTEXT 12.5: 84 SetTriggerLine and
  122 ExtendTriggerLine (2,000 calls each), 155 the same from two vector variables, 85 the half height,
  87 to 90 the box, 86 and 92 arm it, 306 and 307 arm by actor type, 127 the +0x34 bits 13..14 mode, 139
  GetCrossingSide, 154 the target. 240 SetTriggerLinkTable is dead.
- **Wait (5 rows).** 0 WaitFrames, 95 WaitForever (14,636 calls, the idle end of most actor threads and
  in 815 battle files), 246 WaitActorTasksDone, 405 WaitAtelBusyClear, 465 WaitFramesSkippable.
- **Menu and save (38 rows).** Gil (309 to 312), key items and primers (269, 270, 351 to 353, 564, 565),
  the treasure grants 347 and 423, HP and MP restore (369, 370, 2,666 calls each), equipment (439, 533,
  541, 576), the party snapshot (273, 274), the memory card check (453, 578, 579), the save and load
  menu result (506, 507), the option bits (570, 586, 612, 614), sphere grid type (596, 597) and 573
  BeginNewGame.
- **Battle (7 rows).** 225 and 226 the encounter step gate, 527 an encounter scene override, 330
  SetFieldBootMode (297 calls), 427 the battle count, 538 and 539 the Monster Arena species counts.
- **Movie (4 rows).** The premovie state in save +0xD8, +0xDC, +0xE0: 432 LoadPremovie, 433 Cancel, 437
  Start, 450 the reset.
- **System (27 rows) and debug (8 rows).** Single context bits and engine globals (170, 233, 242, 349,
  350, 447, 549, 577, 589 and others), 412 Nop (6,396 calls), 595 IsWesternMasterVersion, 603
  UnlockTrophy (59 calls, a platform call with no simulation effect), the display merge 178 to 180, and
  the return constant stubs 604 and 608 to 611.

## 5. Lockstep hazards

The flags in the table: `rng` (a stream draw), `clock` (wall clock), `input` (pads, sticks, or the
player's answer from a window), `render` (presentation pacing, FMV or audio position), `load` (waits on
an asynchronous disk operation), `local` (language, region, files on disk, a per machine value). A
trailing `*` means the flag came from the call graph closure and not from reading the body.

### 5.1 Pacing and presentation [V]

- **core:0 WaitFrames.** The poll subtracts 1 per pass, except while `g_ffxFmvScriptRunning` and
  `FFX_Fmv_CurrentMovieNeedsCameraRestore()`, when it subtracts `FFX_Fmv_FramesSinceMark()`, the FMV
  decoder's frame count. That is a wall clock rate during those movies. The start has a hidden special
  case: when `sub_782970()` returns 0x610000 and the magic id is 656, a count of 10 becomes 40. A port
  needs both.
- **core:213 PrepareVoice** and **core:258 LoadBgm** polls return 0 while `g_ffxPendingSteps > 1`, so
  they finish on the first step of a frame that is not catching up. 213 also holds while a core:515
  model load is in flight (`dword_1326B94` through `sub_86ADE0`). Corrected 2026-10-26 by
  VOICE_TIMING.md: the "disc idle" call `sub_76F060` is a `return 0` stub. 8,674 and 649 calls.
- **core:214 StartVoice, 216, 217.** Corrected 2026-10-26, see VOICE_TIMING.md. 214 is a SyncData
  kind 2 countdown or finishes on its first poll on PC. `sub_885D20` is not an audio position, it is
  a PAL start delay that returns 0 in retail. 216 reads the voice busy flag, which the FMOD thread
  clears on a WALL CLOCK once the line's length has passed, and 217 is a kind 3 step countdown or
  that same flag. 215 stops the voice (a skip) unless `g_ffxPacingMode` is 1.
- **core:465 WaitFramesSkippable** (8,659 calls) finishes at once when
  `maybe_g_ffxMovieStreamFromHdd` (byte 0xF3C913) is set, a process local byte (MINIGAMES_TIMED 1.3).

### 5.2 Disk [V]

core:213, 219, 220, 222, 317, 453, 515: the poll waits for an asset loader, a magic DLL or a save file
operation. Disk speed decides the step they finish on. core:347 and 423 open the inventory menu when
the bag is full and wait for it to close, which is input, not disk.

### 5.3 Input [V]

- Pad masks, port 0 only, no port argument: 68, 70, 71, 76, 77, 80, 81 used (held, pressed, released,
  raw and remapped, the remap is the identity). 289 and 290 read the live staging analog byte
  (`FFX_Pad__readStagingAnalogByte`), not the ring, which is the Jecht Shot quantisation trap in
  MINIGAMES_TIMED 6. 334 turns two stick bytes into an angle but takes them as arguments.
- The auto repeat words +0x0A (`readWord10`, 69, 73, 79, 83) and +0x1C (`readWord28`, 591 to 594) are
  timed by `FFX_Pad__stepAutoRepeatAllGroups` on the wall clock (INPUT_LAYER 10.1). Only core:593 is
  used (12 calls).
- core:77 is forced to 0 in event 49 for actor 11 when `byte_112D6C9` bit 0x20 is set.
- The message waits (124, 125, 132, 313, 315, 489) and the window readers (143, 151, 251, 272) end on
  the player's confirm or choice. That is input, but through the window system rather than a pad read.

### 5.4 Language, region and machine local state

From a closure over `FFX_GetLanguage`, `FFX_GetInternationalEnvId`, their thunks and
`TOSwapInternationalEnvExec*`, then reading what each path does with the value:

- **Simulation visible** [V]:
  - The treasure grants 347 and 423, and 439, reach `FFX_Treasure_GiveReward`, which on language 11
    rewrites a treasure table row in place from item 0x2065 to 0x2060. Two peers on different languages
    get different items from the same chest.
  - 595 IsWesternMasterVersion returns the LanguageManager master version test to the script (181
    calls in 38 packages).
  - 466 CompareMessageToSaveString returns a strcmp of a message, picked by region, against a save
    string.
  - 473 ResetRikkuNameDefault writes a kernel string, picked by region, into the save block.
  - 465 (5.1) and 453 (a memory card check over the save files on disk).
- **Text and placement only** [V]: 100, 106, 566 and 615 pick the message table by region, 534 the
  unit display name. 101 moves a
  window for three events, two of them keyed on language. 282, 283 and 287 build the texture path by
  language and shift a sprite for language 11. 319 and 320 read the dictionary of the current
  language.
- **Inferred**: 578 and 579 read a memory check state, marked local from the package names
  (memochek, test20) rather than from the body. The confirm and choice waits (124, 125, 132, 143, 313, 315) wait for text phase 3, and
  DIALOGUE.md says the typewriter rate depends on the language. If so, peers on different languages
  finish the same wait on different steps. [I]

### 5.5 Single slot globals

What a co-op port must agree on or own, from the rows above: the player actor slot `ctx+10` (67), the
bound CHR (93, 94, 96), the last message id `dword_1325AC0` (100), the signal masks `dword_1326B70` and
`dword_1326B74` (377 to 379, 394, 395), the current BGM `dword_1327130`, the warp gate (190), the
pending zone (393), and the RNG stream 2 state.

## 6. The apply script

`../tools/apply_core_names.py`, modelled on `apply_battle_structs.py`. It reads the table out of the
IDB and names each slot `FFX_AtelSys_Core_NNN_<Role>_<slot>`. Its `NAMES` block holds 568 rows, 395 used
rows and 173 dead rows whose body gave a role. The other 40 used rows already carry descriptive names
on every slot, and of the dead rows 435 is the null row and 114, 314, 316, 430, 536, 537 and 540 keep
their names. It refuses any slot whose current name is not the
positional one of that row or a `sub_`/`loc_`/`nullsub_` placeholder, skips a slot that is not a
function start, a function shared between slots and a name already taken, leaves dead rows positional
unless `--include-dead`, and never saves. Dry run by default.

Dry run on the scratch copy, 2026-10-26: 419 slots to rename, 4 skipped (214 resi and 507 resi not a
function, 261 poll misnamed, 615 start shared with 315). With `--include-dead`: 596 to rename, 6
skipped (also 247 start and 449 resi). On the live IDB the rows renamed today will show as already
named or as kept.

## 7. Open

- MOVE +0x08, +0x0C, +0x10, +0x18, +0x44, +0x48 and ROTATE +0x14, +0x18, +0x1C: read
  `FFX_Atel_StepMoveCmd` and the rotate stepper to replace the field shaped names.
- Attach kinds 3 and 4 (core:3, 4), the actor +0x38 bits, +0xBC (328, 2,306 calls), +0xAF0, +0xB16 and
  context +0x210 bit 0x08 (577) have no traced reader.
- The sound rows are mostly [I] above the wrapper level. Read `sub_886B80`, `sub_885D10` and the
  `sub_67Axxx` driver entries.
- core:297's push order maps to colour bytes 0, 2, 1 of the sprite (by the asm at 0x85C760), which may
  be a bug in the original or a BGR layout.
- core:323 and 324: screen or volume fade.
- Whether the typewriter rate really follows the language (5.4).
- core:600 SwitchTextLanguageAndReload is dead but would change language mid game.

## 8. The table

Columns: core index, call sites, packages (event, battle field and monster files together), src (e
event packages, m monster AI, b battle field files, s system_01), args in push order, result, blk
(blocking), name (the proposed name per slot, `kept` for a slot that keeps its current name, `live`
for the rows named today, `dead` with the role for an unused row), meaning, lockstep, mark.

| core | uses | pkgs | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 124718 | 686 | embs | frames:int | int 0 | yes | kept `FFX_AtelOp_WaitFrames_start`, kept `FFX_AtelOp_WaitFrames_poll` | start stores the count in the work slot, poll decrements by 1 per pass, or by FMV decoder frames since mark during 4 movies | render | V |
| 1 | 5690 | 375 | e | chrId:int | int 0 | no | kept `FFX_AtelOp_SetActorModel` | releases the old attachment (core:413), spawns CHR chrId into +0x9C, +0xA8 = chrId, +0xAA = 1, applies the actor's yaw and pos |  | V |
| 2 | 2290 | 1149 | ebs | camIndex:int, flag:int, ?:int | int 0 | no | `FFX_AtelSys_Core_002_BindCameraHandle_resi` | releases the old attachment, +0xAA = 2, +0xA8 = camIndex, caches a camera handle via 0x7BA9A0 (index <= 3), +0x36 bit 0x10 iff index 1 |  | V |
| 3 | 11 | 11 | e | id:int | int 0 | no | `FFX_AtelSys_Core_003_SetActorAttachKind3_resi` | writes +0xA8 = id and +0xAA = 3 (attach kind 3), no release of the previous attachment |  | V |
| 4 | 523 | 118 | e | id:int | int 0 | no | `FFX_AtelSys_Core_004_SetActorAttachKind4_resi` | writes +0xA8 = id and +0xAA = 4 (attach kind 4), no release of the previous attachment |  | V |
| 5 | 1547 | 174 | e |  | int 0 | no | `FFX_AtelSys_Core_005_ReleaseActorAttachment_resi` | pushes 0 and runs core:413: disposes the actor's CHR (and both bone models) or camera handle per +0xAA, then zeroes +0xA8/+0xAA |  | V |
| 6 | 562 | 56 | e | slot:int, chrId:int, bone:int | int 1 if attached, else 0 | no | `FFX_AtelSys_Core_006_AttachBoneModel_resi` | clears bone slot 0..1, then if the actor has a CHR and chrId != 0 spawns chrId into +0xAF4+4*slot and attaches it to that parent bone |  | V |
| 7 | 382 | 50 | e | slot:int | int 1 | no | `FFX_AtelSys_Core_007_DetachBoneModel_resi` | disposes the bone-attached CHR in slot and clears its bit in +0xB14, for category 7 slot 0 re-hides part 1 of the carrier |  | V |
| 8 | 0 | 0 |  |  | int 0 | no | dead `Nop` | stub, returns 0 |  | V |
| 9 | 0 | 0 |  |  | int CHR pointer | no | dead `GetActorChrPtr` | returns the actor's CHR* (+0x9C) as an int |  | V |
| 10 | 0 | 0 |  |  | int attach kind | no | dead `GetActorAttachKind` | returns +0xAA, the attach kind (1 CHR, 2 camera, 3, 4) |  | V |
| 11 | 0 | 0 |  |  | int model or handle id | no | dead `GetActorAttachId` | returns +0xA8, the model id or camera handle id |  | V |
| 12 | 0 | 0 |  | chr:int | int the pointer | no | dead `SetPlayerChrPtr` | pops a raw CHR pointer and makes it g_ffxControlledChr via FFX_Ch_SetPlayerChr, zeroing the direction ramps |  | V |
| 13 | 0 | 0 |  |  | int previous map id | no | dead `GetPrevMapId` | returns the save block's previous map id (j_FFX_SaveData_GetPrevMapId) |  | I |
| 14 | 0 | 0 |  |  | int previous entry point | no | dead `GetPrevEntryPoint` | returns the save block's previous entry point (j_FFX_SaveData_GetPrevEntryPoint) |  | I |
| 15 | 0 | 0 |  |  | int current map id | no | dead `GetMapId` | returns the save block's current map id (j_FFX_SaveData_GetMapId) |  | I |
| 16 | 76 | 52 | e |  | int entry point | no | `FFX_AtelSys_Core_016_GetEntryPoint_resi` | returns the save block's current entry point, the doorway the party arrived through |  | I |
| 17 | 2153 | 322 | e | mapId:int, entryPoint:int | int 0 | no | `FFX_AtelSys_Core_017_WarpToMap_resi` | FFX_Map_WarpToWithSavedFade(mapId, entryPoint, 1): requests a map change using the save block fade mode and frames |  | V |
| 18 | 1 | 1 | e | x:float, z:float | int 0 | no | `FFX_AtelSys_Core_018_SetActorPosXZ_resi` | FFX_Atel_SetActorPosXZ(actor, x, z, 0), moves the actor and its CHR on XZ only |  | V |
| 19 | 7661 | 379 | e | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Core_019_SetActorPos_resi` | FFX_Atel_SetActorPos(actor, x, y, z, 0), teleports the actor and its CHR |  | V |
| 20 | 0 | 0 |  | x:float, z:float | int 0 | no | dead `SetMoveTargetXZ` | FFX_Atel_SetMoveCmdTarget(actor, x, 0, z) for the current channel's MOVE command |  | V |
| 21 | 16109 | 336 | e | x:float, y:float, z:float | int 0 | no | kept `FFX_AtelOp_SetMoveTargetXYZ` | FFX_Atel_SetMoveCmdTarget(actor, x, y, z), the MOVE command target |  | V |
| 22 | 2603 | 200 | e | speed:float | float speed | no | `FFX_AtelSys_Core_022_SetMoveSpeed_resf` | writes MOVCMD+0x10 (current channel) = value, returns it |  | I |
| 23 | 4418 | 282 | e | dist:float | float dist | no | `FFX_AtelSys_Core_023_SetMoveArrivalDistance_resf` | writes MOVCMD+0x38 (current channel) = value, returns it |  | I |
| 24 | 14533 | 335 | e | chanMask:int, kindFlags:int, targetActor:int | int 0 | no | kept `FFX_AtelOp_StartMove` | pre-seeds the MOVE command per kind (spline t, L3T start pos, JP1 arc, else +0xC = 1/+0xC), then FFX_Atel_StartMoveCmd |  | V |
| 25 | 20623 | 341 | e | chanMask:int, kindFlags:int, targetActor:int | int 0 | no | kept `FFX_AtelOp_SetRotateCmd` | seeds the ROTATE command (kind 2 relative turn derives +0xC from speed +0x10 and delta), then FFX_Atel_SetupRotCmd |  | V |
| 26 | 16314 | 332 | e |  | int 0 | yes | `FFX_AtelSys_Core_026_WaitMoveDone_poll` | poll only, returns 0 while the MOVE command runs, 5 when it ended |  | V |
| 27 | 9671 | 299 | e |  | int 0 | yes | `FFX_AtelSys_Core_027_WaitRotateDone_poll` | poll only, returns 0 while the channel's ROTATE command runs, 5 when it ended |  | V |
| 28 | 2466 | 106 | e | a8:float, a12:float | float a8 | no | `FFX_AtelSys_Core_028_SetMoveCmdTiming_resf` | writes MOVCMD+0x08 = first arg and +0x0C = second arg for the current channel |  | I |
| 29 | 9155 | 300 | e | a8:float, a12:float | float a8 | no | `FFX_AtelSys_Core_029_SetRotateCmdTiming_resf` | writes the bound ROTATE command +0x08 = first arg and +0x0C = second arg (<= 0 becomes 1) |  | I |
| 30 | 0 | 0 |  | a:float, b:float | float a | no | dead `PopTwoFloats` | stub, pops 2 floats and returns the first pushed |  | V |
| 31 | 13643 | 334 | e |  | float yaw | no | `FFX_AtelSys_Core_031_GetYawToMoveTarget_resf` | atan2 yaw from the actor's XZ position to the current channel's MOVE target XZ (+0x28, +0x30) |  | V |
| 32 | 2440 | 62 | e |  | float pitch | no | `FFX_AtelSys_Core_032_GetPitchToMoveTarget_resf` | sub_88B4D0 yaw and pitch from the actor's position to the MOVE target XYZ, returns the pitch |  | V |
| 33 | 626 | 75 | e | angle:float | float angle | no | `FFX_AtelSys_Core_033_SetMoveCmdField44_resf` | writes MOVCMD+0x44 = value (a wrap to [-pi, pi] is computed and its result discarded) |  | I |
| 34 | 0 | 0 |  | angle:float | float angle | no | dead `SetMoveCmdField48` | writes MOVCMD+0x48 = value, wrap result discarded |  | I |
| 35 | 0 | 0 |  | yaw:float | float yaw | no | dead `SetActorYaw` | writes motion state +0x20 (yaw) = value |  | V |
| 36 | 0 | 0 |  | pitch:float | float pitch | no | dead `SetActorPitch` | writes motion state +0x24 (pitch) = value |  | V |
| 37 | 242 | 47 | e | rot:float | float rot | no | `FFX_AtelSys_Core_037_SetActorRotY_resf` | writes motion state +0x34 (rotation Y) = value, wrap result discarded |  | V |
| 38 | 0 | 0 |  | rot:float | float rot | no | dead `SetActorRotX` | writes motion state +0x30 = value, wrap result discarded |  | I |
| 39 | 0 | 0 |  | rot:float | float rot | no | dead `SetActorRotZ` | writes motion state +0x38 = value, wrap result discarded |  | I |
| 40 | 20727 | 340 | e | rot:float | float rot | no | `FFX_AtelSys_Core_040_SetRotateTargetY_resf` | rotcmd(channel)+0x20 = target, +0x2C = current rotY (+0x34) - target |  | V |
| 41 | 2946 | 95 | e | rot:float | float rot | no | `FFX_AtelSys_Core_041_SetRotateTargetX_resf` | rotcmd(channel)+0x24 = target, +0x30 = current motion +0x30 - target |  | V |
| 42 | 477 | 37 | e | rot:float | float rot | no | `FFX_AtelSys_Core_042_SetRotateTargetZ_resf` | rotcmd(channel)+0x28 = target, +0x34 = current motion +0x38 - target |  | V |
| 43 | 626 | 75 | e | rate:float | float rate | no | `FFX_AtelSys_Core_043_SetMoveYawTurnRate_resf` | writes MOVCMD+0x3C (current channel) = value |  | I |
| 44 | 626 | 75 | e | rate:float | float rate | no | `FFX_AtelSys_Core_044_SetMovePitchTurnRate_resf` | writes MOVCMD+0x40 (current channel) = value |  | I |
| 45 | 2111 | 397 | e | dist:float | float dist | no | `FFX_AtelSys_Core_045_SetMoveArrivalDistanceAll_resf` | writes MOVCMD+0x38 = value on all 9 channels |  | I |
| 46 | 8627 | 316 | e | speed:float | float speed | no | `FFX_AtelSys_Core_046_SetRotateSpeed_resf` | writes rotcmd(channel)+0x10 = value |  | V |
| 47 | 11364 | 333 | e | v:float | float v | no | `FFX_AtelSys_Core_047_SetRotCmdField18_resf` | writes rotcmd(channel)+0x18 = value |  | I |
| 48 | 46 | 11 | e | v:float | float v | no | `FFX_AtelSys_Core_048_SetRotCmdField1C_resf` | writes rotcmd(channel)+0x1C = value |  | I |
| 49 | 0 | 0 |  |  | int 1 | no | dead `EnableActorPlayerBind` | sets +0x34 bit 0x200 (eligible to be the pad CHR) and re-runs unbind/BindPlayerChr |  | V |
| 50 | 0 | 0 |  |  | int 0 | no | dead `DisableActorPlayerBind` | clears +0x34 bit 0x200 and re-runs unbind/BindPlayerChr |  | V |
| 51 | 27417 | 374 | e | actorIndex:int | int actor id | no | kept `FFX_AtelOp_GetActorId` | returns the u16 id at +0x2E of FFX_Atel_GetActor(index), a negative index means the caller |  | V |
| 52 | 5034 | 214 | e | bit:int | int bit | no | `FFX_AtelSys_Core_052_EnableActorEvent_resi` | sets bit n of the event mask +0xAB that FireActorEvent tests, returns n |  | V |
| 53 | 10008 | 248 | e | bit:int | int 0 | no | `FFX_AtelSys_Core_053_DisableActorEvent_resi` | clears bit n of the event mask +0xAB |  | V |
| 54 | 840 | 86 | e | actorIndex:int | int 0 | no | `FFX_AtelSys_Core_054_UnshareActorMoveCmds_resi` | for each of the 9 channels of the target actor, rebinds channels sharing that MOVE command to their own copy (and ROTATE when flag 0x4000) |  | V |
| 55 | 5 | 3 | e | actorIndex:int | int 0 | no | `FFX_AtelSys_Core_055_UnshareActorRotCmds_resi` | for each of the 9 channels of the target actor, rebinds channels sharing that ROTATE command to their own copy |  | I |
| 56 | 6921 | 277 | e | actorIndex:int | float x | no | `FFX_AtelSys_Core_056_GetActorPosX_resf` | returns motion state +0x00 of FFX_Atel_GetActor(index) |  | V |
| 57 | 6927 | 277 | e | actorIndex:int | float y | no | `FFX_AtelSys_Core_057_GetActorPosY_resf` | returns motion state +0x04 of the indexed actor |  | V |
| 58 | 6955 | 277 | e | actorIndex:int | float z | no | `FFX_AtelSys_Core_058_GetActorPosZ_resf` | returns motion state +0x08 of the indexed actor |  | V |
| 59 | 0 | 0 |  | actorId:int | int 0 | no | dead `SetMoveTargetActor` | FFX_Atel_RetargetMoveCmdAtActor: MOVCMD+0 = actor, target = that actor's live position |  | V |
| 60 | 0 | 0 |  | actorId:int | int 0 | no | dead `AimMoveCmdAtActor` | bound MOVE command +0 = actor, then sub_86F7F0(actor, that actor's position) |  | I |
| 61 | 840 | 114 | e |  | int 0 | no | `FFX_AtelSys_Core_061_CaptureActorRot_resi` | copies motion rot +0x34/+0x30/+0x38 into the bound ROTATE command +0x38/+0x3C/+0x40 (mask 255) |  | V |
| 62 | 0 | 0 |  | mask:int | int 0 | no | dead `CaptureActorRotMasked` | core:61 with a mask: bit 1 rotY, bit 2 rot +0x30, bit 4 rot +0x38 |  | V |
| 63 | 850 | 114 | e |  | float rot | no | `FFX_AtelSys_Core_063_GetCapturedRotY_resf` | returns the bound ROTATE command +0x38, where core:61 stores rotY |  | V |
| 64 | 0 | 0 |  |  | float rot | no | dead `GetCapturedRotX` | returns the bound ROTATE command +0x3C |  | V |
| 65 | 0 | 0 |  |  | float rot | no | dead `GetCapturedRotZ` | returns the bound ROTATE command +0x40 |  | V |
| 66 | 3819 | 383 | e | charIndex:int | int charIndex | no | `FFX_AtelSys_Core_066_BindActorPartyChar_resi` | type 1: +0x40 = charIndex and CHR party index, +0x34 bit 0x200 iff charIndex 0, rebind player CHR, +0xAF and dword_132FFA0 map |  | V |
| 67 | 546 | 382 | e |  | int actor id | no | `FFX_AtelSys_Core_067_SetSelfAsPlayerActor_resi` | FFX_Atel_SetPlayerActorId(own id): ctx+10 = this actor, caches its position in the context |  | V |
| 68 | 40 | 14 | e |  | int button mask | no | `FFX_AtelSys_Core_068_GetPadHeldRemapped_resi` | returns the port 0 held mask through FFX_Pad__remapButtonMask (identity) | input | V |
| 69 | 0 | 0 |  |  | int button mask | no | dead `GetPadRepeatRemapped` | returns the auto repeat mask (pad word +10, wall clock timer) remapped | input, clock | V |
| 70 | 6466 | 225 | e |  | int button mask | no | kept `FFX_Atel_GetPadPressedRemapped` | returns the pressed-edge mask remapped | input | V |
| 71 | 5 | 1 | e |  | int button mask | no | `FFX_AtelSys_Core_071_GetPadReleasedRemapped_resi` | returns the released-edge mask remapped | input | V |
| 72 | 0 | 0 |  |  | int button mask | no | dead `GetPadHeld` | returns the raw port 0 held mask | input | V |
| 73 | 0 | 0 |  |  | int button mask | no | dead `GetPadRepeat` | returns the raw auto repeat mask (wall clock) | input, clock | V |
| 74 | 0 | 0 |  |  | int button mask | no | dead `GetPadPressed` | returns the raw pressed-edge mask | input | V |
| 75 | 0 | 0 |  |  | int button mask | no | dead `GetPadReleased` | returns the raw released-edge mask | input | V |
| 76 | 337 | 65 | e | bit:int | int bool | no | `FFX_AtelSys_Core_076_IsPadHeldRemapped_resi` | 1 when button bit n is set in the remapped port 0 held mask | input | V |
| 77 | 206 | 73 | e | bit:int | int bool | no | `FFX_AtelSys_Core_077_IsPadPressedRemapped_resi` | 1 when bit n is set in the remapped pressed mask, forced 0 in event 49 for actor 11 when byte_112D6C9 bit 0x20 | input | V |
| 78 | 0 | 0 |  | bit:int | int bool | no | dead `IsPadReleasedRemapped` | 1 when bit n is set in the remapped released mask | input | V |
| 79 | 0 | 0 |  | bit:int | int bool | no | dead `IsPadRepeatRemapped` | 1 when bit n is set in the remapped auto repeat mask (wall clock) | input, clock | V |
| 80 | 62 | 3 | e | bit:int | int bool | no | `FFX_AtelSys_Core_080_IsPadHeld_resi` | 1 when bit n is set in the raw port 0 held mask | input | V |
| 81 | 48 | 4 | e | bit:int | int bool | no | `FFX_AtelSys_Core_081_IsPadPressed_resi` | 1 when bit n is set in the raw pressed mask | input | V |
| 82 | 0 | 0 |  | bit:int | int bool | no | dead `IsPadReleased` | 1 when bit n is set in the raw released mask | input | V |
| 83 | 0 | 0 |  | bit:int | int bool | no | dead `IsPadRepeat` | 1 when bit n is set in the raw auto repeat mask (wall clock) | input, clock | V |
| 84 | 2086 | 295 | e | ax:float, ay:float, az:float, bx:float, by:float, bz:float | int 1 on type 2/5, else 0 | no | live `FFX_AtelSys_Core_084_SetTriggerLine_resi` | type 2/5 only: point A at motion +0x00, B at +0x30, half height +0x1C = -1, +0x34 OR= 0x80 (arm), +0x36 OR= 1 (retest) |  | V |
| 85 | 697 | 64 | e | halfHeight:float | int 1 (0 on a wrong actor type) | no | live `FFX_AtelSys_Core_085_SetTriggerHalfHeight_resi` | types 2,3,5,6 only: shape +0x1C = halfHeight, sets +0x36 bit 1 (retest) |  | V |
| 86 | 1649 | 160 | e | armed:int | int 1 (0 on a wrong actor type) | no | live `FFX_AtelSys_Core_086_SetTriggerArmed_resi` | types 2,3,5,6: +0x34 bit 0x80 = (armed != 0), sets +0x36 bit 1 (retest). Body identical to core:92 |  | V |
| 87 | 749 | 148 | e | x:float, y:float, z:float | int 1 (0 on a wrong actor type) | no | live `FFX_AtelSys_Core_087_SetTriggerBoxCentre_resi` | types 3,6: shape +0x00/+0x04/+0x08 = x,y,z, arms (+0x34 bit 0x80), sets retest |  | V |
| 88 | 427 | 73 | e | xHalf:float, zHalf:float | int 1 (0 on a wrong actor type) | no | `FFX_AtelSys_Core_088_SetTriggerBoxExtentsXZ_resi` | pushes halfHeight -1.0 and tail calls core:89, so X and Z half extents with no Y test |  | V |
| 89 | 440 | 105 | e | xHalf:float, zHalf:float, halfHeight:float | int 1 (0 on a wrong actor type) | no | live `FFX_AtelSys_Core_089_SetTriggerBoxExtents_resi` | types 3,6: shape +0x30 = xHalf, +0x38 = zHalf, +0x1C = halfHeight, sets retest |  | V |
| 90 | 137 | 36 | e | rotY:float | int 0 | no | live `FFX_AtelSys_Core_090_SetTriggerBoxRotation_resi` | types 3,6: shape +0x34 = rotation about Y, sets retest. Always returns 0 |  | V |
| 91 | 0 | 0 |  | radius:float | int 1 | no | dead `SetActorRadius` | dead. Motion/shape +0x48 = radius, sets retest, no type check. Body identical to core:133 |  | V |
| 92 | 1257 | 112 | e | armed:int | int 1 (0 on a wrong actor type) | no | live `FFX_AtelSys_Core_092_SetTriggerArmed_resi` | types 2,3,5,6: +0x34 bit 0x80 = (armed != 0), sets retest. Body identical to core:86 |  | V |
| 93 | 4614 | 333 | e |  | int 1 | no | kept `FFX_AtelOp_PlayerControlOn` | calls FFX_Atel_BindPlayerChr, which binds the CHR of the actor whose id is ctx+10 as the pad-driven player |  | V |
| 94 | 4597 | 331 | e |  | int 0 | no | kept `FFX_AtelOp_PlayerControlOff` | calls FFX_Atel_UnbindPlayerChr |  | V |
| 95 | 14636 | 1555 | embs |  | never completes | yes | kept `FFX_AtelOp_WaitForever_poll` | poll always returns 0, the parked-thread idiom of entry 1 |  | V |
| 96 | 3004 | 331 | e | on:int | int new state of the bit (0/1) | no | `FFX_AtelSys_Core_096_SetActorControllable_resi` | sets or clears own +0x34 bit 0x200 (the bit BindPlayerChr requires), then FFX_Atel_RebindPlayerChr unbinds and rebinds the player CHR |  | V |
| 97 | 1197 | 397 | e | value:float | float value truncated to int | no | `FFX_AtelSys_Core_097_SetActorMotionField40_resf` | own motion state +0x40 = (float)(int)value, sets +0x36 bit 1, returns it |  | V |
| 98 | 421 | 395 | e | value:float | float value truncated to int | no | `FFX_AtelSys_Core_098_SetActorMotionField44_resf` | own motion state +0x44 = (float)(int)value, sets +0x36 bit 1, returns it |  | V |
| 99 | 512 | 395 | e | value:float | float value | no | `FFX_AtelSys_Core_099_SetActorMotionField3C_resf` | own motion state +0x3C = value, sets +0x36 bit 1, returns it |  | V |
| 100 | 32982 | 359 | e | window:int, messageId:int | int 0 | no | kept `FFX_AtelOp_MesWinSetMessage` | window rec +8 and +12 = GetMessageText(id), +22 = GetMessageAttr(id), also dword_1325AC0 = id (last message id) | local* | V |
| 101 | 33038 | 359 | e | window:int, x:int, y:int, skin:int | int 0 | no | `FFX_AtelSys_Core_101_MesWinSetPosition_resi` | window rec +0 = x, +2 = y (s16), +28 = skin byte. Hardcoded x/y fixups for events 23, 171, 212, two keyed on language | local | V |
| 102 | 33038 | 359 | e | window:int, palette:int | int window index | no | `FFX_AtelSys_Core_102_MesWinSetPalette_resi` | window rec +32 byte = palette, passed to sub_8B0000 by core:106 |  | V |
| 103 | 0 | 0 |  | window:int, size:int | int 0 | no | dead `MesWinSetSizePacked` | dead. Splits size into low word -> rec +4 (width) and high word -> rec +6 (height) via a push and pop |  | V |
| 104 | 0 | 0 |  | window:int, width:int, height:int | int 0 | no | dead `MesWinSetSize` | dead. Window rec +4 = width, +6 = height (used by core:106 when attr bit 8 is set) |  | V |
| 105 | 211 | 21 | e | window:int, initialCursor:int, cancelCursor:int | int window index | no | kept `FFX_AtelOp_MesWinSetChoiceCursorDefaults` | window rec +30 = initial cursor, +31 = cancel cursor, clamped later by core:106 |  | V |
| 106 | 33038 | 359 | e | window:int, attr:int | int window index | no | kept `FFX_AtelOp_MesWinShow` | opens the window: rec+20 = 1, +33 = 1, builds the draw object, numeric entry on attr 0x40, choice list on attr 2, takes input focus | local* | V |
| 107 | 32223 | 348 | e | window:int | int window index | no | kept `FFX_AtelOp_MesWinClose` | FFX_Atel_MesWinRequestClose: rec+20 = 3, +30 = -1 |  | V |
| 108 | 5765 | 397 | e | speed:float | float speed | no | `FFX_AtelSys_Core_108_SetMoveSpeedAll_resf` | writes MOVE cmd +0x10 on all 9 channels (actor+0x5C8+76*ch), returns the value |  | I |
| 109 | 398 | 397 | e | rate:float | float rate | no | `FFX_AtelSys_Core_109_SetMoveYawTurnRateAll_resf` | writes MOVE cmd +0x3C (yaw turn rate per FFX_GAME_NOTES) on all 9 channels, returns the value |  | V |
| 110 | 397 | 397 | e | rate:float | float rate | no | `FFX_AtelSys_Core_110_SetMovePitchTurnRateAll_resf` | writes MOVE cmd +0x40 (pitch turn rate per FFX_GAME_NOTES) on all 9 channels, returns the value |  | V |
| 111 | 398 | 397 | e | value:float | float value | no | `FFX_AtelSys_Core_111_SetRotateSpeedAll_resf` | writes ROTATE cmd +0x10 (actor+0x874+68*ch) on all 9 channels, returns the value |  | I |
| 112 | 397 | 397 | e | value:float | float value | no | `FFX_AtelSys_Core_112_SetRotCmdField18All_resf` | writes ROTATE cmd +0x18 (actor+0x87C+68*ch) on all 9 channels, returns the value |  | V |
| 113 | 397 | 397 | e | value:float | float value | no | `FFX_AtelSys_Core_113_SetRotCmdField1CAll_resf` | writes ROTATE cmd +0x1C (actor+0x880+68*ch) on all 9 channels, returns the value |  | V |
| 114 | 0 | 0 |  | frames:int | int frames | no | dead `SetMoveFrames` | dead. MOVE cmd +4 duration frames for the current channel (actor+0x32) |  | V |
| 115 | 0 | 0 |  | frames:int | int frames | no | dead `SetRotFrames` | dead. ROTATE cmd +4 duration frames for the current channel (actor+0x868+68*ch) |  | V |
| 116 | 794 | 397 | e | frames:int | int frames | no | `FFX_AtelSys_Core_116_SetMoveFramesAll_resi` | MOVE cmd +4 duration frames on all 9 channels |  | V |
| 117 | 0 | 0 |  | frames:int | int frames | no | dead `SetRotFramesAll` | dead. ROTATE cmd +4 duration frames on all 9 channels |  | V |
| 118 | 397 | 397 | e |  | int actor type byte (7 if id out of range) | no | `FFX_AtelSys_Core_118_GetSelfActorType_resi` | FFX_Atel_GetActorKind(own id +0x2E), the definition type byte |  | V |
| 119 | 14378 | 373 | e | actorId:int | int 0 | no | `FFX_AtelSys_Core_119_StopActorMove_resi` | sub_863E10(actor, current channel): resets channels sharing the move cmd to their own with kind 0, keeps target, stops rotate on flag 0x4000 |  | V |
| 120 | 10825 | 330 | e | actorId:int | int 0 | no | `FFX_AtelSys_Core_120_StopActorRotate_resi` | sub_863FE0(actor, current channel): repoints channels sharing the rotate cmd to their own with kind 0, copying angles |  | V |
| 121 | 0 | 0 |  | value:int | int value | no | dead `PassThroughInt` | dead. Pops one int and returns it |  | V |
| 122 | 2052 | 293 | e | extendA:float, extendB:float | int 1 (0 on a wrong actor type) | no | live `FFX_AtelSys_Core_122_ExtendTriggerLine_resi` | types 2,5: moves point A back along the line by extendA and B forward by extendB, sets retest |  | V |
| 123 | 0 | 0 |  | messageId:int | int packed size, always 0x00640064 | no | dead `MeasureMessageText` | dead. Picks the text by international env id, then sub_8B7CC0 which is a stub returning 100 OR 100<<16 | local | V |
| 124 | 10966 | 195 | e | window:int | int 0 | yes | kept `FFX_AtelOp_MesWinWaitConfirm124_start`, kept `FFX_AtelOp_MesWinWaitConfirm124_poll`, kept `FFX_AtelOp_MesWinWaitConfirm124_resi` | core:132 with flag 0: window clamped 0..7, sets rec+29 bit 0x10, same poll, getter closes the window | input, local | V |
| 125 | 213 | 22 | e | window:int | int chosen option (s16), -1 none | yes | kept `FFX_AtelOp_MesWinWaitChoice_start`, kept `FFX_AtelOp_MesWinWaitChoice_poll`, kept `FFX_AtelOp_MesWinWaitChoice_resi` | latches choice pick [0] into cmdstate+4 once text phase 3 and choice state 2, closes the window, returns it | input, local | V |
| 126 | 0 | 0 |  |  | int bound player actor id, -1 none | no | dead `GetPlayerActorId` | FFX_Atel_GetPlayerActorId: ctx+10, 0xFFFF -> -1 |  | V |
| 127 | 589 | 270 | e | mode:int | int 1 (0 on a wrong actor type) | no | `FFX_AtelSys_Core_127_SetTriggerBits13To14_resi` | types 2,3,5,6: own +0x34 bits 13..14 = mode, no retest |  | V |
| 128 | 682 | 324 | e |  | float spawn X | no | `FFX_AtelSys_Core_128_GetEntryPointX_resf` | entry record +0x0C for the save block's current entry point |  | V |
| 129 | 682 | 324 | e |  | float spawn Y | no | `FFX_AtelSys_Core_129_GetEntryPointY_resf` | entry record +0x10 (+Y is down) |  | V |
| 130 | 682 | 324 | e |  | float spawn Z | no | `FFX_AtelSys_Core_130_GetEntryPointZ_resf` | entry record +0x14 |  | V |
| 131 | 682 | 324 | e |  | float entry record +0x08 | no | `FFX_AtelSys_Core_131_GetEntryPointYaw_resf` | entry record +0x08, the scalar before XYZ, probably the facing yaw |  | I |
| 132 | 22675 | 339 | e | window:int, flag:int | int 0 | yes | kept `FFX_AtelOp_MesWinWaitConfirm132_start`, kept `FFX_AtelOp_MesWinWaitConfirm132_poll`, kept `FFX_AtelOp_MesWinWaitConfirm132_resi` | window clamped 0..7, rec+29 bit 0x10. Poll: state 0/4 done, state 2 + text phase 3 + ctx+0 bit 4 -> clears bit, closes | input, local | V |
| 133 | 496 | 395 | e | radius:float | int 1 | no | `FFX_AtelSys_Core_133_SetActorRadius_resi` | own motion/shape +0x48 = radius, sets retest, no type check. Body identical to core:91 |  | V |
| 134 | 243 | 104 | e |  | int current scene id | no | `FFX_AtelSys_Core_134_GetSceneId_resi` | thunk to j_FFX_SaveData_GetSceneId (save block) |  | V |
| 135 | 523 | 99 | e |  | int current map id | no | `FFX_AtelSys_Core_135_GetMapId_resi` | thunk to j_FFX_SaveData_GetMapId (save block) |  | V |
| 136 | 231 | 201 | e |  | int previous scene id | no | `FFX_AtelSys_Core_136_GetPrevSceneId_resi` | thunk to j_FFX_SaveData_GetPrevSceneId (save block) |  | V |
| 137 | 68 | 18 | e |  | int previous map id | no | `FFX_AtelSys_Core_137_GetPrevMapId_resi` | thunk to j_FFX_SaveData_GetPrevMapId (save block) |  | V |
| 138 | 0 | 0 |  |  | int system actor id | no | dead `GetSystemActorId` | dead. FFX_Atel_GetSystemActorId: context 0 event package +24 |  | V |
| 139 | 1082 | 146 | e |  | int side, -1 none | no | live `FFX_AtelSys_Core_139_GetCrossingSide_resi` | side byte of the link for the current request (sub_8632B0), else own +0x36 bits 1..2 with 3 -> -1 |  | V |
| 140 | 0 | 0 |  |  | int ctx+500 | no | dead `GetContextStepCounter` | dead. Returns the per-context step counter at ctx+500 |  | V |
| 141 | 395 | 395 | e | value:float | float value | no | `FFX_AtelSys_Core_141_SetActorMotionField4C_resf` | own motion state +0x4C = value, sets +0x36 bit 1, returns it |  | V |
| 142 | 0 | 0 |  | value:float | float value | no | dead `SetActorHalfHeightNoRetest` | dead. Own motion/shape +0x1C = value, no type check and no retest bit |  | V |
| 143 | 15129 | 271 | e | window:int | int bool | no | kept `FFX_AtelOp_MesWinIsIdleOrAnswered` | true when rec+20 state is 0 or 4, or state 2 with text phase 3 | input, local | V |
| 144 | 100 | 1 | e |  | float own motion +0x20 | no | `FFX_AtelSys_Core_144_GetActorHeading_resf` | own motion state +0x20, the heading / move direction |  | V |
| 145 | 534 | 56 | e |  | float own motion +0x34 | no | `FFX_AtelSys_Core_145_GetActorRotY_resf` | own motion state +0x34, the facing rotY |  | V |
| 146 | 27 | 3 | e | actorId:int | float motion +0x20 | no | `FFX_AtelSys_Core_146_GetActorHeadingById_resf` | motion state +0x20 of the actor given by id |  | V |
| 147 | 166 | 11 | e | actorId:int | float motion +0x34 | no | `FFX_AtelSys_Core_147_GetActorRotYById_resf` | motion state +0x34 of the actor given by id |  | V |
| 148 | 107 | 34 | e | accel:float | float accel | no | `FFX_AtelSys_Core_148_SetMoveAccel_resf` | MOVE cmd +0x34 (acceleration per FFX_GAME_NOTES) on the current channel, returns the value |  | V |
| 149 | 14294 | 371 | e | yaw:float | float yaw | no | kept `FFX_AtelOp_SetActorYaw` | stops own rotate cmd, motion +0x20 and +0x34 = yaw, pushes it to the CHR (attach 1) or a kind 4/5 handle |  | V |
| 150 | 1072 | 47 | e | pitch:float | float pitch | no | `FFX_AtelSys_Core_150_SetActorPitch_resf` | stops own rotate cmd, motion +0x24 and +0x30 = pitch, pushes to a kind 4/5 handle, no CHR update |  | V |
| 151 | 7809 | 251 | e | window:int | int window state rec+20 | no | `FFX_AtelSys_Core_151_MesWinGetState_resi` | returns window rec+20: 0 idle, 1 opening, 2 shown, 3 closing, 4 answered | input | V |
| 152 | 21 | 9 | e | splineVar:int, frames:int | int 1 | no | kept `FFX_AtelOp_SetMoveSpline` | resolves the variable as a spline, installs it in the MOVE cmd, duration = frames, t = 0 |  | V |
| 153 | 0 | 0 |  | pathVar:int, t:int | int 1 | no | dead `SetActorPosOnPath` | dead. Evaluates path variable at t (int to float, clamped 0..1) via sub_862B20 -> sub_88A4A0, then SetActorPos snapPrev 0 |  | V |
| 154 | 116 | 22 | e | actorId:int | int actorId | no | live `FFX_AtelSys_Core_154_SetTriggerTarget_resi` | types 5,6: shape +0x28 u16 = target actor id, returns it |  | V |
| 155 | 5 | 1 | e | pointAVar:int, pointBVar:int | int 1 (0 on a wrong actor type) | no | `FFX_AtelSys_Core_155_SetTriggerLineFromVars_resi` | resolves two vector variables, pushes A xyz then B xyz and tail calls core:84 (set line from two points) |  | V |
| 156 | 0 | 0 |  | pathVar:int, t:int | int 1 | no | dead `SetActorPosOnPathAlt` | dead. Same as core:153 with sub_862B70 -> sub_88A4D0 as the evaluator |  | V |
| 157 | 33038 | 359 | e | window:int, flags:int | int window index | no | `FFX_AtelSys_Core_157_MesWinSetFlags_resi` | window rec +24 dword = flags, ORed with rec +40 and passed to sub_8B0000 by core:106 |  | V |
| 158 | 45839 | 99 | e | winId:int, slot:int, format:int, value:int | int winId | no | `FFX_AtelSys_Core_158_SetMessageNumberSlot_resi` | table dword_1869AB0[32*win+slot] -> slot record: byte +0 = format, dword +4 = value. The number a message shows |  | V |
| 159 | 0 | 0 |  | winId:int, slot:int, value:int | int winId | no | dead `SetMessageNumberValue` | the value half of core:158 only: dword +4 of the slot record dword_1869AB0[32*win+slot] |  | V |
| 160 | 0 | 0 |  | winId:int, slot:int, format:int | int winId | no | dead `SetMessageNumberFormat` | the format half of core:158 only: byte +0 of the slot record dword_1869AB0[32*win+slot] |  | V |
| 161 | 0 | 0 |  | winId:int, style:int | int style | no | dead `SetMessageWindowFrameStyle` | window record +0x10 (the four frame style bytes) = style as one packed dword, via FFX_Atel_GetMessageWindow |  | V |
| 162 | 20 | 2 | e | winId:int, b2:int, b1:int, b0:int, b3:int | int packed dword | no | `FFX_AtelSys_Core_162_SetMessageWindowFrameStyle4_resi` | packs four bytes as b3<<24 OR b2<<16 OR b1<<8 OR b0, pushes it and pops it back, then window record +0x10 = it like core:161 |  | V |
| 163 | 1286 | 63 | e | actorId:int, xVar:var, yVar:var, zVar:var | int 0 | no | `FFX_AtelSys_Core_163_GetActorCentreToVars_resi` | resolves three variables and fills them from FFX_Atel_GetActorCentre (0x86A2F0): line midpoint for types 2/5, else the position |  | V |
| 164 | 657 | 39 | e | actorA:int, actorB:int | float 3D distance | no | `FFX_AtelSys_Core_164_GetActorDistance_resf` | sqrt of dx^2+dy^2+dz^2 between the motion state positions (+0,+4,+8) of the two actors |  | V |
| 165 | 306 | 9 | e | actorA:int, actorB:int | float XZ distance | no | `FFX_AtelSys_Core_165_GetActorDistanceXZ_resf` | sqrt of dx^2+dz^2 between the motion state positions of the two actors, Y ignored |  | V |
| 166 | 13775 | 90 | e | n:int | int 0..n-1 | no | `FFX_AtelSys_Core_166_RandBelow_resi` | n<=0 becomes 1, returns (n * (u16)FFX_Rand_Stream(2)) >> 16 | rng:stream 2 | V |
| 167 | 445 | 81 | e | actorId:int | int 0 | no | `FFX_AtelSys_Core_167_DisableActor_resi` | actor +0x34 &= ~2, clears the alive bit RunScript gates on, so that actor's scripts stop being stepped |  | V |
| 168 | 298 | 42 | e | actorId:int | int 1 | no | `FFX_AtelSys_Core_168_EnableActor_resi` | actor +0x34 OR= 2, sets the alive bit back so the actor's scripts run again |  | V |
| 169 | 28308 | 736 | embs |  | int 0..65535 | no | `FFX_AtelSys_Core_169_Rand16_resi` | returns (u16)FFX_Rand_Stream(2), the raw 16 bit script random | rng:stream 2 | V |
| 170 | 388 | 59 | e | on:int | int on | no | `FFX_AtelSys_Core_170_SetFlag1325DB0_resi` | dword_1325DB0 bit 0 = (on != 0), read back by core:218 |  | I |
| 171 | 21 | 13 | e | mapId:int, entryPoint:int | int 0 | no | `FFX_AtelSys_Core_171_WarpToMap_resi` | FFX_Map_WarpToWithSavedFade(mapId, entry, 1): uses the saved fade bytes +0x11/+0x12. Gated on ctx byte 0 bit 0x80 or debug |  | V |
| 172 | 2 | 1 | e | nameId:int | int nameId | no | `FFX_AtelSys_Core_172_SetMapNameId_resi` | FFX_SaveData_SetMapNameId: save word +0x0E = nameId, the place name the save file and menu show |  | V |
| 173 | 0 | 0 |  | dic:int | int dic | no | dead `SetMapNameIdFromGroundDic` | FFX_SaveData_SetMapNameIdFromGroundDic: save +0x0E from the loaded package's place name table |  | V |
| 174 | 0 | 0 |  |  | int place name id | no | dead `GetMapNameId` | FFX_SaveData_GetMapNameId: save +0xD4 when the override byte +0xD6 is set, else +0x0E |  | V |
| 175 | 0 | 0 |  | attr:int | int attr | no | dead `SetEncounterGroundAttr` | save byte +0x10 = attr, the encounter zone / ground attribute. PullActorPosFromChr rewrites it every step |  | V |
| 176 | 0 | 0 |  |  | int save byte +0x10 | no | dead `GetEncounterGroundAttr` | returns save byte +0x10, the encounter ground attribute written per step from the player CHR |  | V |
| 177 | 205 | 13 | e | value:float | float value | no | `FFX_AtelSys_Core_177_SetMoveCmdField18_resf` | own MOVE record of the current level, float at actor +0x5B8 + 76*level + 0x18 = value |  | I |
| 178 | 1 | 1 | e | on:int | int previous value | no | `FFX_AtelSys_Core_178_SetDisplayMergeEnabled_resi` | byte_130181E = (on != 0) & 1 via sub_83ADD0, returns the old byte. Boot sets it to 1 among the draw enable flags |  | I |
| 179 | 1 | 1 | e | dx:int | int dx | no | `FFX_AtelSys_Core_179_SetDisplayMergeX_resi` | reads the merge offset (dword_130194C/50/54), replaces dx, writes back with FFX_Gs_SetDisplayMergeOffset |  | V |
| 180 | 1 | 1 | e | dy:int | int dy | no | `FFX_AtelSys_Core_180_SetDisplayMergeY_resi` | replaces dy of the GS display merge offset, keeps dx and alpha |  | V |
| 181 | 0 | 0 |  | alpha:int | int alpha | no | dead `SetDisplayMergeAlpha` | replaces alpha of the GS display merge offset, keeps dx and dy |  | V |
| 182 | 0 | 0 |  |  | int dword_130194C | no | dead `GetDisplayMergeX` | returns the GS display merge dx |  | V |
| 183 | 0 | 0 |  |  | int dword_1301950 | no | dead `GetDisplayMergeY` | returns the GS display merge dy |  | V |
| 184 | 0 | 0 |  |  | int dword_1301954 | no | dead `GetDisplayMergeAlpha` | returns the GS display merge alpha |  | V |
| 185 | 0 | 0 |  |  | int byte_130181E | no | dead `GetDisplayMergeEnabled` | sub_83ADD0(0) then sub_83ADD0(old), a non destructive read of the core:178 byte |  | I |
| 186 | 1 | 1 | e |  | int 1 | no | `FFX_AtelSys_Core_186_ClearEventEntryTable_resi` | own actor +0x44 = 0, the event entry table pointer opcode 0x79 sets, so events fall back to the type default |  | V |
| 187 | 3548 | 377 | e | frames:int | int frames | no | kept `FFX_AtelSys_Core_187_SetFadeOutFrames` | save byte +0x12 = frames. core:17/171 warps use it as the transition frames and the screen fade length |  | V |
| 188 | 3548 | 377 | e | kind:int | int kind | no | kept `FFX_AtelSys_Core_188_SetFadeInFrames` (role should be SetWarpFadeKind) | save byte +0x11 = kind. The saved fade warp reads it as a switch: 0 FFX_ScreenFade_RequestMode3, 1 Mode5, else none |  | V |
| 189 | 82 | 25 | e | level:int | int level (clamped to >= 1) | no | `FFX_AtelSys_Core_189_KillRequestsAboveLevel_resi` | FFX_Atel_KillRequestsAbove(own actor, level-1, mode 1) via sub_871730, drops queued requests above that level |  | V |
| 190 | 130 | 52 | e | on:int | int on != 0 | no | `FFX_AtelSys_Core_190_SetWarpGate_resi` | current context byte 0 bit 0x80 = (on != 0). That bit is the gate FFX_Map_WarpTo/WarpToWithSavedFade test and consume |  | V |
| 191 | 70 | 23 | e | entryIndex:int | float spawn X | no | `FFX_AtelSys_Core_191_GetEntryPointX_resf` | FFX_Atel_GetEntryRecordByIndex(index) +0x0C, a negative index means the current entry point |  | V |
| 192 | 70 | 23 | e | entryIndex:int | float spawn Y | no | `FFX_AtelSys_Core_192_GetEntryPointY_resf` | entry record +0x10 by index |  | V |
| 193 | 70 | 23 | e | entryIndex:int | float spawn Z | no | `FFX_AtelSys_Core_193_GetEntryPointZ_resf` | entry record +0x14 by index |  | V |
| 194 | 70 | 23 | e | entryIndex:int | float facing yaw | no | `FFX_AtelSys_Core_194_GetEntryPointYaw_resf` | entry record +0x08 by index, the facing yaw in radians per CHEAT_WARP 3.1 |  | V |
| 195 | 0 | 0 |  |  | int bool | no | dead `IsRotating` | own channel (level) ROTATE command pointer (channel +0x48): its u16 at +2 & 0x1F != 0, a rotate command is live |  | V |
| 196 | 11 | 8 | e |  | int bool | no | `FFX_AtelSys_Core_196_IsMoving_resi` | u16 at actor +0x5BA + 76*level (own MOVE record +2) & 0x7F != 0, a move command is live |  | V |
| 197 | 3205 | 316 | e | m0:int, m1:int, m2:int, m3:int | int 0 | no | `FFX_AtelSys_Core_197_SetTurnMotionSet_resi` | writes four ints to actor +0xAE0..+0xAEC in push order, read back one by one by core:199 |  | I |
| 198 | 6048 | 236 | e |  | int bits | no | `FFX_AtelSys_Core_198_GetTurnKind_resi` | own ROT record (+0x864 + 68*level) +0x2C angle: bit 0 = angle > 0, bit 1 =  OR angle OR  >= flt_1327124 degrees |  | I |
| 199 | 6048 | 236 | e | index:int | int actor +0xAE0[index] | no | `FFX_AtelSys_Core_199_GetTurnMotion_resi` | returns the dword actor +0xAE0 + 4*index, the four values core:197 stored. No bound check |  | I |
| 200 | 6047 | 236 | e | flags:int, small:int, large:int | int scaled value | no | `FFX_AtelSys_Core_200_ScaleByTurnAngle_resi` | d = wrap(ROT record +0x20 - motion +0x34), flags&2: large*clamp(1-1.2*(1- OR d OR /pi)), else small*clamp( OR d OR /(pi/2)) |  | V |
| 201 | 7 | 5 | e | entryIndex:int | float s16 entry id | no | `FFX_AtelSys_Core_201_GetEntryArrivalScript_resf` | entry record +0x06 by index as a float, the arrival script entry FFX_Atel_StartEntryArrivalScript launches |  | V |
| 202 | 2199 | 71 | e | charId:int | int charId | no | `FFX_AtelSys_Core_202_AddPartyMember_resi` | FFX_SaveData_SetCharInParty(id, 1). Char 6 at story 1085 also takes order slot 3, char 15 sets dword_1325AC8 = 3 |  | V |
| 203 | 905 | 46 | e | charId:int | int charId | no | `FFX_AtelSys_Core_203_RemovePartyMember_resi` | SetCharInParty(id, 0), then any active field slot holding id becomes 255 and FFX_SaveData_SetFieldParty3 rewrites them |  | V |
| 204 | 4 | 2 | e | charId:int | int bool | no | `FFX_AtelSys_Core_204_IsCharInParty_resi` | FFX_SaveData_IsCharInParty(id) == 1, the record +0x2C bit 0 membership flag |  | V |
| 205 | 0 | 0 |  |  | int bool | no | dead `IsSelfInFieldParty` | 1 when own actor +0x40 (party char index) is one of the three active field slots, else 0 |  | V |
| 206 | 245 | 14 | e | value:int | int value | no | `FFX_AtelSys_Core_206_SetActorWordB16_resi` | own actor u16 +0xB16 = value, a type 1 only field |  | I |
| 207 | 54 | 17 | e | value:int | int value | no | `FFX_AtelSys_Core_207_SetActorIntAF0_resi` | own actor dword +0xAF0 = value, read back by core:208 |  | I |
| 208 | 39 | 8 | e |  | int actor +0xAF0 | no | `FFX_AtelSys_Core_208_GetActorIntAF0_resi` | returns own actor dword +0xAF0 |  | I |
| 209 | 1034 | 217 | e |  | int 0 | no | `FFX_AtelSys_Core_209_StopAllMoveCmds_resi` | sub_863E10 on own channels 0..8: each channel sharing a MOVE command gets its own record back, state 0, values copied |  | V |
| 210 | 16 | 7 | e |  | int 0 | no | `FFX_AtelSys_Core_210_StopAllRotateCmds_resi` | same for ROTATE commands, sub_863FE0 for channels 0..8 |  | V |
| 211 | 0 | 0 |  |  | int 0 | no | dead `StopMoveCmd` | sub_863E10(self, -1), the current level's MOVE command only |  | V |
| 212 | 0 | 0 |  |  | int 0 | no | dead `StopRotateCmd` | sub_863FE0(self, -1), the current level's ROTATE command only |  | V |
| 213 | 8674 | 261 | e | voiceId:int | int 0 | yes | `FFX_AtelSys_Core_213_PrepareVoice_start`, `FFX_AtelSys_Core_213_PrepareVoice_poll`, `FFX_AtelSys_Core_213_PrepareVoice_resi` | start: stores id, SyncData kind 1 countdown. poll: counts it down, or waits for dword_1326B94 == 0. result: sub_887570(id) queues the voice | render, load | V |
| 214 | 8579 | 261 | e |  | int ? (result slot 0x85DE80 is not a function) | yes | `FFX_AtelSys_Core_214_StartVoice_start`, `FFX_AtelSys_Core_214_StartVoice_poll`, `FFX_AtelSys_Core_214_StartVoice_resi` | start: SyncData kind 2 countdown, wait frames from ctx +0x214 or sub_885D20 (a PAL start delay, 0 in retail, VOICE_TIMING.md). poll counts down | render | V |
| 215 | 6410 | 226 | e |  | int 0 | no | `FFX_AtelSys_Core_215_SkipVoiceUnlessTimed_resi` | when g_ffxPacingMode != 1 stops the voice through FFX_Voice_Stop (FMOD command 10), the skip scripts call from the core:216 loop on a Circle press (VOICE_TIMING.md) | input | V |
| 216 | 6396 | 224 | e |  | int bool | no | `FFX_AtelSys_Core_216_IsVoicePlaying_resi` | 1 while the kind 3 countdown dword_1325AE0 runs, else sub_8864B0 (voice busy flags dword_132FF10 OR qword_132FF14) != 0 | render | V |
| 217 | 8579 | 262 | e |  | int 1 when done | yes | `FFX_AtelSys_Core_217_WaitVoiceDone_poll` | poll only: counts down dword_1325AE8 when a sync row armed it, else finishes when sub_8864B0 reports no voice busy | render | V |
| 218 | 2 | 1 | e |  | int dword_1325DB0 & 1 | no | `FFX_AtelSys_Core_218_GetFlag1325DB0_resi` | returns the bit core:170 sets |  | I |
| 219 | 89 | 52 | e | effectId:int | int 0 | yes | `FFX_AtelSys_Core_219_LoadGlobalEffect_start`, `FFX_AtelSys_Core_219_LoadGlobalEffect_poll` | start: dword_1326B70 = 0, dword_1326B58 = 1. poll: requests the effect each pass, done at effect state 3 (or 1 on scene 270) | load, local | V |
| 220 | 87 | 52 | e |  | int 0 | yes | `FFX_AtelSys_Core_220_PlayGlobalEffect_start`, `FFX_AtelSys_Core_220_PlayGlobalEffect_poll` | poll sub_8633E0(self id): states 0,1,6 wait, 3 calls sub_788D60(id) and waits, 5 clears dword_1326B58 and finishes | load | V |
| 221 | 0 | 0 |  |  | int 0, 1 or the raw state | no | dead `IsGlobalEffectActive` | byte_1133364 state: 0 or 6 -> 0, 1, 3, 5 -> 1, any other state returned as is | local | V |
| 222 | 109 | 58 | e |  | int 0 | yes | `FFX_AtelSys_Core_222_WaitGlobalEffectIdle_poll` | poll only: when the effect state is 0 or 6 sets byte_1326B80 = 4 and finishes, else waits | load | V |
| 223 | 6510 | 286 | e | seId:int | int seId | no | `FFX_AtelSys_Core_223_PlaySe_resi` | sub_886E80: decodes the id (sub_885D10, sub_885D00) and calls sub_886B80(bank, id, 63, 127), pan centre and full volume |  | I |
| 224 | 512 | 78 | e | seId:int | int seId | no | `FFX_AtelSys_Core_224_StopSe_resi` | sub_886EB0: same id decode, then sub_887750(bank, id) |  | I |
| 225 | 2 | 1 | e |  | int bool | no | `FFX_AtelSys_Core_225_GetEncounterStepGate_resi` | FFX_Atel_GetFieldStepAllowsEncounter: save byte +0x13 bit 1 |  | V |
| 226 | 11 | 4 | e | on:int | int on | no | `FFX_AtelSys_Core_226_SetEncounterStepGate_resi` | FFX_Atel_SetFieldStepAllowsEncounter: save byte +0x13 bit 1 = (on != 0) |  | V |
| 227 | 2325 | 56 | e | value:float | float value | no | `FFX_AtelSys_Core_227_SetRotCmdField14_resf` | sub_8715A0: own ROT record of the current level, float at actor +0x864 + 68*level + 0x14 = value |  | I |
| 228 | 392 | 392 | e | value:float | float value | no | `FFX_AtelSys_Core_228_SetRotCmdField14All_resf` | sub_86F300: the same ROT record +0x14 float on all 9 channels |  | I |
| 229 | 0 | 0 |  | on:int | int on != 0 | no | dead `SetContextFlag0x0800` | current context byte 1 bit 0x08 = (on != 0). The reset sets it (byte 1 = 0x98) |  | I |
| 230 | 2 | 2 | e | charId:int | int bool | no | `FFX_AtelSys_Core_230_IsCharInFieldParty_resi` | 1 when charId is one of the three active field party slots (FFX_SaveData__getFieldParty), else 0 |  | V |
| 231 | 1297 | 62 | e | slot:int, charId:int | int charId (255 for -1) | no | `FFX_AtelSys_Core_231_SetPartyOrderSlot_resi` | FFX_SaveData_SetPartyOrderSlot(charId, slot), -1 becomes 255 (empty). Slots 0..2 active, 3..19 bench |  | V |
| 232 | 47 | 9 | e | slot0Var:var, slot1Var:var, slot2Var:var | int 1 | no | `FFX_AtelSys_Core_232_GetFieldPartyToVars_resi` | fills three variables with the active field party char ids, 255 rewritten as -1 |  | V |
| 233 | 200 | 76 | e | on:int | int on != 0 | no | `FFX_AtelSys_Core_233_SetContextFlag0x1000_resi` | current context byte 1 bit 0x10 = (on != 0), read by core:234. The reset sets it |  | I |
| 234 | 0 | 0 |  |  | int bool | no | dead `GetContextFlag0x1000` | current context byte 1 bit 0x10 |  | I |
| 235 | 1 | 1 | e |  | int actor +0x32 | no | `FFX_AtelSys_Core_235_GetOwnLevel_resi` | returns the running actor's level byte, which picks the channel |  | V |
| 236 | 68 | 22 | e | charId:int | int 0..2 or -1 | no | `FFX_AtelSys_Core_236_GetFieldPartySlot_resi` | index of charId among the three active field party slots, -1 when absent or charId is -1 |  | V |
| 237 | 160 | 17 | e |  | int actor +0x40 | no | `FFX_AtelSys_Core_237_GetOwnPartyIndex_resi` | returns own actor +0x40, the party char index CHRREG passes to FFX_Ch_SetPartyIndex (-1 for none) |  | V |
| 238 | 18 | 17 | e | actorId:int | int bool | no | `FFX_AtelSys_Core_238_IsActorFlag20_resi` | actor +0x34 bit 0x20 of the given actor |  | I |
| 239 | 2 | 2 | e | on:int | int on != 0 | no | `FFX_AtelSys_Core_239_SetOwnFlag0800_resi` | own actor +0x36 bit 0x800 = (on != 0) |  | I |
| 240 | 0 | 0 |  | tableVar:var | int 1 | no | live `FFX_AtelSys_Core_240_SetTriggerLinkTable_resi` | resolves a variable and hands it to FFX_Atel_SetTriggerLinkTable: types 5/6 +0x2E4 = it, links cleared, 16 nodes added |  | V |
| 241 | 0 | 0 |  | actorId:int | int side or -1 | no | dead `GetCrossingSideForActor` | FFX_Atel_GetLinkCrossingSide(self, id), else when id is own target (motion +0x28) or none, +0x36 bits 1..2 with 3 as -1 |  | V |
| 242 | 262 | 114 | e | on:int | int previous bit | no | `FFX_AtelSys_Core_242_SetCtx0Flag20_resi` | byte_1325BA3 (context 0 byte 3) bit 0x20 = (on != 0), returns the old bit |  | I |
| 243 | 301 | 298 | e | scene:int | int scene | no | `FFX_AtelSys_Core_243_SetGuideMapLayout_resi` | FFX_RcBg_SetGuideMapLayout: if g_ffxRcBgCtx and its guide +4 are set, rcbgGuideSetLayout(guide, preset), preset 0..4 sets the guide map corner and size (`RCBG.md` 3). Map entry calls it with 0 |  | V |
| 244 | 8662 | 285 | e | x:float, y:float, z:float | float yaw, radians | no | `FFX_AtelSys_Core_244_GetYawToPoint_resf` | atan2(z - self.z, x - self.x) from own motion position via sub_88B430, y popped and ignored |  | V |
| 245 | 1002 | 26 | e | x:float, y:float, z:float | float pitch, radians | no | `FFX_AtelSys_Core_245_GetPitchToPoint_resf` | sub_88B4D0 from own motion position: returns the elevation angle atan2(y - self.y, XZ distance), the yaw is dropped |  | V |
| 246 | 730 | 35 | e | level:int, actorId:int | int 0 | yes | `FFX_AtelSys_Core_246_WaitActorTasksDone_start`, `FFX_AtelSys_Core_246_WaitActorTasksDone_poll` | start stores {actorId, max(level,1)}. poll finishes when FFX_Atel_ActorHasTaskAtLevel(actorId, level) is 0 or actorId < 0 |  | V |
| 247 | 0 | 0 |  | level:int, partySlot:int | int 0 | yes | dead `WaitPartyMemberTasks` | resolves party slot to its actor (FFX_Atel_GetPartyMemberActor), then waits while ActorHasTaskAtLevel(actor, level), level <= 0 means 1 |  | V |
| 248 | 1 | 1 | e | var:int, value:int, size:int | int size | no | `FFX_AtelSys_Core_248_DevMemsetVar_resi` | dev harness only: memset(ResolveVarAddress(var), value, size). FFX_IsDevHarness is 0 in retail so it only pops 3 and returns size |  | V |
| 249 | 2 | 2 | e |  | int 1 in retail | no | `FFX_AtelSys_Core_249_DevHarnessQuery_resi` | returns sub_88D5D0() under the dev harness, else 1. FFX_IsDevHarness is constant 0, so retail always gets 1 |  | I |
| 250 | 9 | 4 | e | value:int | int value | no | `FFX_AtelSys_Core_250_SetSaveByte2B_resi` | writes the low byte of value to g_ffxSaveData+0x2B (sub_88EB40), returns value |  | V |
| 251 | 425 | 27 | e | win:int | int live cursor index or -1 | no | kept `FFX_AtelOp_MesWinGetLiveChoiceCursor` | returns FFX_MesWin_GetChoiceCursor(win), the live cursor of an armed choice, -1 when no choice is armed | input | V |
| 252 | 0 | 0 |  | bit:int | int 1 set, 0 when bit >= 64 | no | dead `SetSaveHeaderBit18` | FFX_SaveData_SetHeaderBit18(bit): sets a bit in the 64 bit array at g_ffxSaveData+0x18 |  | V |
| 253 | 9236 | 106 | e | seId:int, a:int, b:int | int seId | no | `FFX_AtelSys_Core_253_PlaySe_resi` | sub_886E50(seId, a, b): decodes the sound id (sub_885D10/885D00) and calls sub_886B80 with two byte args |  | I |
| 254 | 24 | 9 | e | seId:int, value:int | int value | no | `FFX_AtelSys_Core_254_ControlSe_resi` | sub_886F60(seId, value) -> sub_81E820(bank, index, byte value) on a decoded sound id. That is FMOD command 59, the SE volume (`SOUND_CLASSES.md` section 3) |  | V |
| 255 | 0 | 0 |  | seId:int, value:int | int value | no | dead `ControlSeAlt` | sub_886DF0(seId, value) -> sub_81E460(bank, index, byte value) on a decoded sound id. FMOD command 61, the SE pan |  | V |
| 256 | 138 | 44 | e | seId:int, param:int, value:int | int value | no | `FFX_AtelSys_Core_256_SetSeParam_resi` | sub_886FF0(seId, param, value) -> sub_81E860 -> FMOD command 60, a volume fade: param is the length in frames and value the target volume, the same call core:403 makes with 12 frames |  | V |
| 257 | 0 | 0 |  | seId:int, param:int, value:int | int value | no | dead `SetSeParamAlt` | sub_886E20(seId, param, value) -> sub_81E480 -> FMOD command 62, the pan fade twin of 256 |  | V |
| 258 | 649 | 218 | e | track:int | int track | yes | `FFX_AtelSys_Core_258_LoadBgm_start`, `FFX_AtelSys_Core_258_LoadBgm_poll`, `FFX_AtelSys_Core_258_LoadBgm_resi` | start latches track, poll waits out a catch-up burst, getter sub_886960(track): for track <= 255 prepares the music, busy pumping the loader. The pump spins the step thread `while (qword_132FF14)`, so a load issued while a voice line plays waits for the line to end on the FMOD clock (`SOUND_CLASSES.md` section 13.2 A5), then FMOD command 40 | render | I |
| 259 | 322 | 101 | e | track:int | int 1 | no | `FFX_AtelSys_Core_259_StopBgm_resi` | if track is the current BGM (dword_1327130) clears it to -1, then sub_886800(track) -> sub_67A010, sub_885E80 |  | I |
| 260 | 385 | 160 | e | track:int | int 1 | no | `FFX_AtelSys_Core_260_PlayBgm_resi` | sets dword_1325B40 = (track == 37), sub_8869E0 and sub_886A20 (volume 127) on track, records it as current BGM dword_1327130 |  | I |
| 261 | 648 | 221 | e |  | int 1 | no | `FFX_AtelSys_Core_261_WaitBgmLoaded_start`, kept `__RTC_NumErrors_808`, `FFX_AtelSys_Core_261_WaitBgmLoaded_resi` (role should be WaitBgmLoaded) | empty start, poll returns 5 at once, getter returns 1. A PC no-op left where the PS2 waited for the music load |  | I |
| 262 | 80 | 18 | e | slot:int, seId:int, posVar:int | int 1 | no | `FFX_AtelSys_Core_262_BindSoundSlot_resi` | sound slot table dword_1328318 (16 x 36 bytes): rec+0 = seId, rec+4 = address of script var posVar |  | I |
| 263 | 1 | 1 | e | slot:int, actorId:int | int 1 | no | `FFX_AtelSys_Core_263_SetSoundSlotFollowActor_resi` | if the slot has a var: sets bit 2 in the var's first word and rec+8 word = actorId |  | V |
| 264 | 91 | 17 | e | slot:int, which:int, x:float, y:float, z:float | int 1 | no | `FFX_AtelSys_Core_264_SetSoundSlotPos_resi` | if the slot has a var: clears bit 2 in its first word, writes xyz to rec+8 (which = 0) or rec+20 (which != 0) |  | V |
| 265 | 91 | 18 | e | slot:int | int 1 | no | `FFX_AtelSys_Core_265_PlaySoundSlot_resi` | if rec+0 is set calls sub_876930(rec, 1) -> sub_876950(rec, 1, 256) |  | I |
| 266 | 11 | 5 | e | slot:int | int 1 | no | `FFX_AtelSys_Core_266_StopSoundSlot_resi` | if rec+0 is set stops that sound (sub_886EB0 -> sub_887750) and clears the slot (sub_8772F0) |  | I |
| 267 | 1359 | 249 | e | mapId:int, entry:int | int 0 | no | kept `FFX_AtelSys_Core_267_WarpNoSavedFade` | FFX_Map_WarpToWithSavedFade(mapId, entry, 0): map change without the saved fade, gated by ctx byte 0 bit 0x80 |  | V |
| 268 | 37 | 17 | e | mapId:int, entry:int | int 0 | no | `FFX_AtelSys_Core_268_WarpNoSavedFade_resi` | FFX_Map_WarpToWithSavedFade(mapId, entry, 0), same body as core:267 |  | V |
| 269 | 38 | 30 | e | n:int | int 1, 0 when n > 254 | no | `FFX_AtelSys_Core_269_GivePrimerKeyItem_resi` | sub_88E790(0xA004+n): sets the key item flag, and for n 0..25 also FFX_SaveData_LearnAlBhedPrimer 0x906B30(n), the only writer of the primer mask. 38 shipped calls, MINIGAMES_PROGRESSION.md 2.3 |  | V |
| 270 | 222 | 34 | e |  | int dword_11307A0 | no | `FFX_AtelSys_Core_270_GetPrimerMask_resi` | returns dword_11307A0 (save+0x3D10) through FFX_SaveData_GetAlBhedPrimerMask 0x906B10, the call at 0x85AF50 |  | V |
| 271 | 2 | 2 | e | win:int, digitCursor:int, cancelCursor:int, defaultValue:int | int win | no | kept `FFX_AtelOp_MesWinSetNumericEntryDefaults` | window record +34 = digitCursor, +35 = cancelCursor, +36 dword = defaultValue |  | V |
| 272 | 2 | 2 | e | win:int | int numeric entry value | no | kept `FFX_AtelOp_MesWinGetNumericValue` | returns FFX_MesWin_GetNumericValue(win), choiceObject+12 | input | V |
| 273 | 49 | 31 | e |  | int previous valid flag | no | `FFX_AtelSys_Core_273_PushPartySnapshot_resi` | FFX_Atel_PushPartySnapshot: save +0x21..0x23 = field party, +0x24 = membership mask of chars 0..6, +0x20 = 1 |  | V |
| 274 | 49 | 32 | e |  | int previous valid flag | no | `FFX_AtelSys_Core_274_PopPartySnapshot_resi` | FFX_Atel_PopPartySnapshot: if save+0x20 set, restores membership and field party from the snapshot, then clears +0x20 |  | V |
| 275 | 0 | 0 |  |  | int save+0x20 byte | no | dead `GetPartySnapshotValid` | returns the party snapshot valid flag at g_ffxSaveData+0x20 |  | V |
| 276 | 31 | 14 | e | force:int, indoor:int | int force != 0 | no | `FFX_AtelSys_Core_276_SetIndoorOverride_resi` | context byte +2: bit 4 = force, bit 2 = indoor. When forced, the indoor state (ambient sound volume, the core:558 placed effect groups) uses bit 2 |  | V |
| 277 | 9 | 5 | e | on:int | int on != 0 | no | `FFX_AtelSys_Core_277_SetContextFlag08_resi` | sets or clears bit 0x08 of current context byte +2 (default on, ctx init writes 0xC8) |  | V |
| 278 | 2090 | 100 | e | actorId:int | int actorId | no | `FFX_AtelSys_Core_278_SetActorExtraPass1_resi` | actor +0x36: bits 9..10 = 1 (0x200), one extra run pass in StepFrame |  | V |
| 279 | 192 | 44 | e | actorId:int | int actorId | no | `FFX_AtelSys_Core_279_SetActorExtraPass1Grant_resi` | actor +0x36: bits 9..10 = 1 and bit 0x1000, which also grants the extra pass to threads it starts |  | V |
| 280 | 0 | 0 |  | actorId:int | int actorId | no | dead `SetActorExtraPass2` | actor +0x36: bits 9..10 = 2 (0x400) |  | V |
| 281 | 599 | 54 | e | kind:int | int equipment model id | no | `FFX_AtelSys_Core_281_GetActorEquipModelId_resi` | for this actor's party char (actor+0x40): kind 1 -> FFX_SaveData_GetEquippedArmourChrId, else weapon. kind clamped 0..1 |  | V |
| 282 | 36 | 36 | e | bufVar:int, size:int, texVar:int | int 1 | no | `FFX_AtelSys_Core_282_InitEvTexSprites_resi` | sub_874B70: sets the event sprite buffer (dword_1327144, end at buf+size-160), texVar ptr to dword_23CBC5C, and loads the event TexList | local* | V |
| 283 | 410172 | 35 | e | slot:int, spriteVar:int | int slot | no | `FFX_AtelSys_Core_283_SetEvTexSprite_resi` | sub_8748A0: slot (0..127) gets the image at spriteVar, resets active, layer 0xFF, color 0x80808080, visible, may kick the TexList load | local* | V |
| 284 | 139694 | 34 | e | slot:int | int slot | no | `FFX_AtelSys_Core_284_ClearEvTexSprite_resi` | sub_8747F0: clears the low byte of the slot's first word, the active flag 283 sets |  | V |
| 285 | 88 | 3 | e | slot:int, alpha:int | int slot | no | `FFX_AtelSys_Core_285_SetEvTexSpriteAlpha_resi` | sub_8749B0: byte 3 of the slot color dword = alpha |  | V |
| 286 | 0 | 0 |  | slot:int, layer:int | int slot | no | dead `SetEvTexSpriteLayer` | sub_874AD0: high byte of the slot's first word, clamped 0..255, default 0xFF |  | I |
| 287 | 48683 | 35 | e | slot:int, x:int, y:int | int slot | no | `FFX_AtelSys_Core_287_SetEvTexSpritePos_resi` | sub_874B30: slot x, y words. Language 11 hack: on level hash 409, (270, 213) becomes (290, 213) | local | V |
| 288 | 108201 | 34 | e | slot:int, sx:float, sy:float | int slot | no | `FFX_AtelSys_Core_288_SetEvTexSpriteScale_resi` | sub_874A80: slot scale words = sx*256, sy*256 |  | V |
| 289 | 122 | 21 | e | axis:int | int raw analog byte 0..255 | no | `FFX_AtelSys_Core_289_GetPadAnalogRemapped_resi` | port 0 analog staging byte for axis (clamped 0..13) after FFX_Pad__mapButtonCode, which passes 0..3 through | input | V |
| 290 | 94 | 8 | e | axis:int | int raw analog byte 0..255 | no | `FFX_AtelSys_Core_290_GetPadAnalogRaw_resi` | port 0 analog staging byte for axis (clamped 0..13), no remap | input | V |
| 291 | 0 | 0 |  | value:int | int value | no | dead `EchoArg` | stub, pops 1 and returns it |  | V |
| 292 | 0 | 0 |  | value:int | int value | no | dead `EchoArg` | stub, pops 1 and returns it |  | V |
| 293 | 0 | 0 |  | x:float, z:float | int 0 | no | dead `SetActorPosXZSnapped` | FFX_Atel_SetActorPosXZ(self, x, z, snapPrev=1), XZ twin of core:294 |  | V |
| 294 | 7028 | 260 | e | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Core_294_SetActorPosSnapped_resi` | FFX_Atel_SetActorPos(self, x, y, z, snapPrev=1), the teleport. On scene 159 with z in (-35.6, -35.5) also sets dword_1325B40 |  | V |
| 295 | 1 | 1 | e | actorId:int | int bool | no | `FFX_AtelSys_Core_295_IsActorWithinOwnRadius_resi` | pushes own motion block float +0x48 and runs core:296 on the same stack: is actorId within that distance of self (3D) |  | V |
| 296 | 214 | 4 | e | actorId:int, dist:float | int bool | no | `FFX_AtelSys_Core_296_IsActorWithinDistance_resi` | 1 when the 3D distance from self to actorId (motion block positions) is <= dist |  | V |
| 297 | 129465 | 32 | e | slot:int, c0:int, c2:int, c1:int | int slot | no | `FFX_AtelSys_Core_297_SetEvTexSpriteColor_resi` | sub_8749F0: slot color dword bytes 0, 1, 2 from three ints clamped 0..255 |  | V |
| 298 | 369 | 12 | e | slot:int, on:int | int slot | no | `FFX_AtelSys_Core_298_SetEvTexSpriteVisible_resi` | sub_874970: slot byte +3 = on != 0 |  | V |
| 299 | 27 | 20 | e | on:int | int on != 0 | no | `FFX_AtelSys_Core_299_SetContextFlag40_resi` | sets or clears bit 0x40 of current context byte +2 (default on) |  | V |
| 300 | 0 | 0 |  | w6:int, w4:int | int 1 | no | dead `SetContextWords4and6` | current context word +4 = w4 (last push), +6 = w6. Defaults 4 and 2 |  | V |
| 301 | 250 | 4 | e | chr:int | int affection | no | `FFX_AtelSys_Core_301_GetLoveParam_resi` | returns LoveParam[chr] (save +0x2C + 4*chr), chr < 0 reads [0], chr > 7 reads [7] |  | V |
| 302 | 27 | 9 | e | chr:int, delta:int | int new value | no | `FFX_AtelSys_Core_302_AddLoveParam_resi` | LoveParam[chr] += delta, chr clamped to 0..7, returns the new value |  | V |
| 303 | 0 | 0 |  | chr:int, amount:int | int new value | no | dead `SubLoveParam` | LoveParam[chr] -= amount, floored at 0, chr clamped 0..7 |  | V |
| 304 | 163 | 3 | eb | chr:int, value:int | int clamped chr | no | `FFX_AtelSys_Core_304_SetLoveParam_resi` | LoveParam[chr] = max(value, 0), chr clamped 0..7, returns the clamped index |  | V |
| 305 | 0 | 0 |  | bit:int | int 1 | no | dead `ClearEventFlagBit` | clears one bit of g_ffxEventFlagBits (save +0x4C, 640 bits), ignores bit >= 640 |  | V |
| 306 | 276 | 46 | e | actorId:int, armed:int | int 1, 0 if not a trigger | no | live `FFX_AtelSys_Core_306_SetActorTriggerArmed_resi` | for trigger actor types 2, 3, 5, 6: +0x34 bit 0x80 = armed, +0x36 OR= 1 (retest) |  | V |
| 307 | 359 | 19 | e | actorId:int, armed:int | int 1, 0 if not a trigger | no | `FFX_AtelSys_Core_307_SetActorTriggerArmed_resi` | calls core:306's handler with the same stack |  | V |
| 308 | 7580 | 141 | e | chrId:int | int 0 | no | `FFX_AtelSys_Core_308_SetActorModel_resi` | calls FFX_AtelOp_SetActorModel with the same stack: spawns the CHR for chrId on this actor and places it |  | V |
| 309 | 20 | 10 | e |  | int gil | no | `FFX_AtelSys_Core_309_GetGil_resi` | returns FFX_SaveData_GetGil() |  | V |
| 310 | 144 | 39 | e | amount:int | int bool | no | `FFX_AtelSys_Core_310_AddGil_resi` | FFX_SaveData_SpendGil(-amount), clamped at 999999999, returns 1 on success |  | V |
| 311 | 104 | 49 | e | cost:int | int 1 paid, 0 too poor | no | `FFX_AtelSys_Core_311_SpendGil_resi` | FFX_SaveData_SpendGil(cost), deducts only when gil >= cost |  | V |
| 312 | 1 | 1 | e | amount:int | int gil | no | `FFX_AtelSys_Core_312_SetGil_resi` | FFX_SaveData_SetGil(amount) clamped 0..999999999, returns the stored gil |  | V |
| 313 | 210 | 60 | e | win:int, msgId:int, defaultValue:int, digitCursor:int, cancelCursor:int, x:int, y:int, skin:int | int numeric value | yes | kept `FFX_AtelOp_AskNumber313_start`, kept `FFX_AtelOp_AskNumber313_poll`, kept `FFX_AtelOp_AskNumber313_resi` | FFX_AtelOp_AskNumberComposite attr OR 0x42: places window, sets message and digit defaults, shows it, waits, returns the dialled number | input, local* | V |
| 314 | 0 | 0 |  | win:int, msgId:int, defaultValue:int, digitCursor:int, cancelCursor:int, x:int, y:int, skin:int | int numeric value | yes | dead `AskNumberExplicitSize` | as core:313 with attribute bit 8 added (attr OR 0x4A) | input, local* | V |
| 315 | 3566 | 218 | e | win:int, msgId:int, defaultCursor:int, cancelCursor:int, x:int, y:int, skin:int | int picked option, -1 none | yes | kept `FFX_AtelOp_AskChoice315_start`, kept `FFX_AtelOp_AskChoice315_poll`, kept `FFX_AtelOp_AskChoice315_resi` | FFX_AtelOp_AskChoiceComposite attr OR 2: places window, sets message and cursor defaults, shows a list choice, waits, returns the pick | input, local* | V |
| 316 | 0 | 0 |  | win:int, msgId:int, defaultCursor:int, cancelCursor:int, x:int, y:int, skin:int | int picked option, -1 none | yes | dead `AskChoiceExplicitSize` | as core:315 with attribute bit 8 added (attr OR 0x0A) | input, local* | V |
| 317 | 3 | 3 | e | n:int | int 0 | yes | `FFX_AtelSys_Core_317_LoadEventSoundSet_start`, `FFX_AtelSys_Core_317_LoadEventSoundSet_poll` | state machine: waits for the asset loader idle, sub_871750(ctx block, ctx+0x204, n), then sub_887550 (dword_132FF24 = 1), then counts 2 | load | I |
| 318 | 341 | 41 | e | on:int | int on != 0 | no | `FFX_AtelSys_Core_318_SetRunsWhenGated_resi` | own actor +0x36 bit 15 = on, which lets it run while the context +3 gate is in mode 2 |  | V |
| 319 | 19416 | 26 | e | section:int, index:int | int text pointer | no | `FFX_AtelSys_Core_319_GetDicString_resi` | pointer to string index (alt variant when the map name alt flag is set) of menu dictionary section for the current language | local | V |
| 320 | 4 | 4 | e | section:int, unused:int, count:int, enableMask:int, idsVar:int, destVar:int | int enabledCount << 8 | no | `FFX_AtelSys_Core_320_BuildDicChoiceText_resi` | writes into destVar a choice text of count dictionary strings named by idsVar, colouring by enableMask and numbering enabled ones | local | V |
| 321 | 56 | 7 | e | win:int, attr:int, textVar:int | int 0 | no | `FFX_AtelSys_Core_321_MesWinSetTextFromVar_resi` | window record +8 and +12 = address of textVar, +22 word = attr. The var buffer form of setting a message |  | V |
| 322 | 55 | 6 | e | mask:int, n:int | int bit index, 0 if none | no | `FFX_AtelSys_Core_322_FindNthSetBit_resi` | returns the index of the n-th (0 based) set bit of mask, 0 when there are fewer |  | V |
| 323 | 60 | 40 | e | frames:int | int frames | no | `FFX_AtelSys_Core_323_StartFadeOut_resi` | flt_1328310 = -1/(frames/2) (or -1), then sub_861000(frames, 0) |  | I |
| 324 | 40 | 27 | e | frames:int | int frames | no | `FFX_AtelSys_Core_324_StartFadeIn_resi` | flt_1328310 = 1/(frames/2) (or 1), then sub_861000(frames, 127) |  | I |
| 325 | 17 | 17 | e | a:int, b:int | int 1 | no | `FFX_AtelSys_Core_325_SetGuideMapPos_resi` | FFX_RcBg_SetGuideMapPos: rcbgGuideSetPos(guide, a, b), the guide map screen centre (g_rcbgGuidePosX/Y) |  | V |
| 326 | 17 | 17 | e | a:int | int a | no | `FFX_AtelSys_Core_326_SetGuideMapSize_resi` | FFX_RcBg_SetGuideMapSize: rcbgGuideSetSize(guide, a), the guide map size (g_rcbgGuideSize) |  | V |
| 327 | 0 | 0 |  | value:int | int stored value | no | dead `SetActorWordBC` | own actor +0xBC = value OR 0x80000000 (0 stays 0), +0xB4 = -10000.0 (the init sentinel) |  | V |
| 328 | 2306 | 129 | e | value:int, actorId:int | int stored value | no | `FFX_AtelSys_Core_328_SetActorWordBC_resi` | actor +0xBC = value OR 0x80000000 (0 stays 0), +0xB4 = -10000.0 |  | V |
| 329 | 17 | 17 | e | on:int | int on != 0 | no | `FFX_AtelSys_Core_329_SetContextFlag80_resi` | sets or clears bit 0x80 of current context byte +2 (default on) |  | V |
| 330 | 297 | 89 | e | mode:int, b:int, c:int | int mode | no | `FFX_AtelSys_Core_330_SetFieldBootMode_resi` | FFX_Atel_SetFieldBootMode: g_ffxFieldBootMode = mode, dword_1326B50 = b, dword_1326B54 = c |  | V |
| 331 | 0 | 0 |  | on:int | int on != 0 | no | dead `SetActorCharSoundBit` | if actor+0x40 (party char) is not -1, sets or clears that bit in dword_132FF2C |  | I |
| 332 | 0 | 0 |  | bit:int, on:int | int on != 0 | no | dead `SetCharMask2CBit` | sets bit 'bit' of dword_132FF2C when on != 0, else clears it, returns on as 0 or 1 |  | I |
| 333 | 5 | 2 | e |  | int character index 0..7 | no | `FFX_AtelSys_Core_333_GetTopLoveParamChar_resi` | scans the save LoveParam DWORD[8] at +0x2C and returns the index of the largest, the earlier index wins ties |  | V |
| 334 | 35 | 6 | e | deadzone:int, axisB:int, axisA:int | float angle, 0.0 inside the dead zone | no | `FFX_AtelSys_Core_334_GetStickAngleOutsideDeadzone_resf` | centres two 0..255 axis bytes on 128, returns 0.0 when deadzone^2 > dx^2+dy^2, else atan2(axisA-128, axisB-128), 1e-6 when exactly 0 |  | I |
| 335 | 6 | 3 | e | tolerance:float, a:float, b:float | int bool | no | `FFX_AtelSys_Core_335_IsAngleDiffWithin_resi` | wraps a-b into [-pi, pi] and returns tolerance > the difference |  | I |
| 336 | 0 | 0 |  |  | int 1 | no | dead `SetCharMask2CSelf` | sets the bit for this actor's party character index (+0x40) in dword_132FF2C, no -1 check |  | I |
| 337 | 847 | 321 | e | bit:int | int 1 | no | `FFX_AtelSys_Core_337_SetCharMask2C_resi` | sets bit 'bit' of dword_132FF2C |  | I |
| 338 | 0 | 0 |  |  | int 0 | no | dead `ClearCharMask2CSelf` | clears the bit for this actor's party character index in dword_132FF2C |  | I |
| 339 | 0 | 0 |  | bit:int | int 0 | no | dead `ClearCharMask2C` | clears bit 'bit' of dword_132FF2C |  | I |
| 340 | 2 | 2 | e |  | int 1, or 0 when the actor has no party index | no | `FFX_AtelSys_Core_340_SetCharMask34Self_resi` | sets the bit for this actor's party character index in dword_132FF34, skipped when +0x40 is -1 |  | I |
| 341 | 74 | 25 | e | bit:int | int 1 | no | `FFX_AtelSys_Core_341_SetCharMask34_resi` | sets bit 'bit' of dword_132FF34 |  | I |
| 342 | 2 | 1 | e |  | int 1, or 0 when the actor has no party index | no | `FFX_AtelSys_Core_342_ClearCharMask34Self_resi` | clears the bit for this actor's party character index in dword_132FF34, skipped when +0x40 is -1 |  | I |
| 343 | 142 | 26 | e | bit:int | int 1 | no | `FFX_AtelSys_Core_343_ClearCharMask34_resi` | clears bit 'bit' of dword_132FF34 |  | I |
| 344 | 165 | 59 | e | slot:int | int slot | no | `FFX_AtelSys_Core_344_SetActorSoundSlot_resi` | only in context 0: actor+0xAF = slot, bound CHR+0x184 = slot, dword_132FFA0[slot] = actor id when slot < 16 |  | I |
| 345 | 14 | 11 | e | actorId:int, slot:int | int slot | no | `FFX_AtelSys_Core_345_SetActorSoundSlotOf_resi` | core:344 for the actor named by actorId, still gated on the calling context index being 0 |  | I |
| 346 | 0 | 0 |  | win:int, attr:int, text:int | int 0 | no | dead `SetMesWinTextRaw` | writes window record +0x08 and +0x0C = text as given (no message table lookup) and +0x16 = attr |  | V |
| 347 | 849 | 134 | e | win:int, treasureId:int | int 1 | yes | `FFX_AtelSys_Core_347_GiveTreasureWithMessage_start`, `FFX_AtelSys_Core_347_GiveTreasureWithMessage_poll`, `FFX_AtelSys_Core_347_GiveTreasureWithMessage_resi` | plays SE 0x80000026, decodes the treasure, grants item, key item, equipment and gil, shows 'You got X' in win | local* | V |
| 348 | 0 | 0 |  | on:int | int on != 0 | no | dead `SetSoundFlag4C` | dword_132FF4C = on != 0, returns it |  | I |
| 349 | 257 | 19 | e | on:int | int previous bit state | no | `FFX_AtelSys_Core_349_SetFieldCtxFlag08_resi` | sets or clears bit 0x08 of context 0 byte +3 (byte_1325BA3) and returns whether it was set |  | V |
| 350 | 618 | 115 | e | on:int | int previous bit state | no | `FFX_AtelSys_Core_350_SetFieldCtxFlag40_resi` | sets or clears bit 0x40 of context 0 byte +3 (byte_1325BA3) and returns whether it was set |  | V |
| 351 | 171 | 6 | e | keyItemId:int | int keyItemId | no | `FFX_AtelSys_Core_351_GiveKeyItem_resi` | FFX_SaveData_SetKeyItemFlag(id), and for ids 0xA004..0xA01D also sub_906B30(id - 0xA004) |  | V |
| 352 | 77 | 13 | e | keyItemId:int | int bool | no | `FFX_AtelSys_Core_352_TestKeyItem_resi` | returns FFX_SaveData_TestKeyItemFlag(id) |  | V |
| 353 | 44 | 5 | e | keyItemId:int | int keyItemId | no | `FFX_AtelSys_Core_353_TakeKeyItem_resi` | clears the key item flag through j_FFX_SaveData_ClearKeyItemFlag |  | V |
| 354 | 0 | 0 |  |  | int 1, or 0 when the actor has no party index | no | dead `SetCharMask3CSelf` | sets the bit for this actor's party character index in dword_132FF3C |  | I |
| 355 | 0 | 0 |  | bit:int | int 1 | no | dead `SetCharMask3C` | sets bit 'bit' of dword_132FF3C |  | I |
| 356 | 0 | 0 |  |  | int 1, or 0 when the actor has no party index | no | dead `ClearCharMask3CSelf` | clears the bit for this actor's party character index in dword_132FF3C |  | I |
| 357 | 0 | 0 |  | bit:int | int 1 | no | dead `ClearCharMask3C` | clears bit 'bit' of dword_132FF3C |  | I |
| 358 | 2 | 2 | e | trackId:int | int 1 | no | `FFX_AtelSys_Core_358_PlayBgm_resi` | sub_886A20(track): start path, per track volume byte 127, then current track dword_1327130 = track |  | I |
| 359 | 13 | 10 | e |  | int 1 | no | `FFX_AtelSys_Core_359_HoldCurrentBgm_resi` | calls sub_8869F0 -> driver sub_67A1B0 with no argument, which is FMOD command 25, run inline: `track[current].+4 = 1`, so the current BGM is held (a later stop pauses it and unload skips it). It clears nothing, role should be `HoldCurrentBgm` (`SOUND_CLASSES.md` section 15) |  | V |
| 360 | 0 | 0 |  | v:int | int v | no | dead `SetBgmDriverParam130` | calls sub_886950(v) -> driver sub_67A130 |  | I |
| 361 | 0 | 0 |  | a:int, b:int | int 1 | no | dead `SetBgmDriverParam0B0` | calls sub_8868E0(a, b) -> driver sub_67A0B0 |  | I |
| 362 | 41 | 28 | e | trackId:int, vol:int | int vol | no | `FFX_AtelSys_Core_362_SetBgmVolume_resi` | driver sub_67A290(track, vol) and byte_13300E0[track] = vol, both 0 while byte_1330090 is set |  | I |
| 363 | 126 | 50 | e | trackId:int, frames:int, vol:int | int vol | no | `FFX_AtelSys_Core_363_FadeBgmVolume_resi` | driver sub_67A2D0(track, frames, vol) and byte_13300E0[track] = vol, vol 0 while byte_1330090 is set |  | I |
| 364 | 68 | 24 | e |  | int 1 | no | `FFX_AtelSys_Core_364_StopCurrentBgm_resi` | when the current track is >= 0 stops it (sub_886A60), then current track = -1 |  | I |
| 365 | 199 | 85 | e | trackId:int | int 1 | no | `FFX_AtelSys_Core_365_StopBgm_resi` | stops the track (sub_886A60), and clears the current track id when it was this one |  | I |
| 366 | 323 | 323 | e | a:int, b:int | int 1 | no | `FFX_AtelSys_Core_366_SetSaveMapWordsB4B6_resi` | writes save word +0xB4 = a and +0xB6 = b |  | V |
| 367 | 634 | 104 | e | entry:int | int entry | no | `FFX_AtelSys_Core_367_SetCheckpointEntry_resi` | save +0xB8 = entry, +0xBA = current map id, then FFX_SaveData_SnapshotLocationHistory |  | V |
| 368 | 0 | 0 |  | mapId:int, entry:int | int entry | no | dead `SetCheckpoint` | save +0xB8 = entry, +0xBA = mapId, then FFX_SaveData_SnapshotLocationHistory |  | V |
| 369 | 2666 | 121 | e | charIndex:int | int current HP | no | `FFX_AtelSys_Core_369_RestoreCharHp_resi` | char record +0x3D = 0, then current HP (+0x1C) = max HP (+0x24) unless it is already above, returns current HP |  | V |
| 370 | 2666 | 121 | e | charIndex:int | int current HP (or MP when over max) | no | `FFX_AtelSys_Core_370_RestoreCharMp_resi` | current MP (+0x20) = max MP (+0x28) unless it is already above. Returns current HP (+0x1C) on the restore path |  | V |
| 371 | 0 | 0 |  |  | float motion +0x24 | no | dead `GetActorPitch` | returns this actor's motion state float at +0x24 (pitch, paired with +0x30 by core:150) |  | V |
| 372 | 44 | 3 | e |  | float motion +0x30 | no | `FFX_AtelSys_Core_372_GetActorRotX_resf` | returns this actor's motion state float at +0x30, the rotation X that core:38 and core:150 write |  | V |
| 373 | 0 | 0 |  | actorId:int | float motion +0x24 | no | dead `GetActorPitchOf` | core:371 for the actor named by actorId |  | V |
| 374 | 0 | 0 |  | actorId:int | float motion +0x30 | no | dead `GetActorRotXOf` | core:372 for the actor named by actorId |  | V |
| 375 | 79 | 6 | e | on:int | int on != 0 | no | `FFX_AtelSys_Core_375_SetActorRunsInBattle_resi` | actor +0x36 bit 0x40 = on, and when on also sets +0x36 bit 0x8000 (runs in a gated context), never cleared here |  | V |
| 376 | 0 | 0 |  | bit:int | int bool | no | dead `TestSignalFlag` | returns bit 'bit' of dword_1326B70 (maybe_FFX_Atel_TestFlag) |  | V |
| 377 | 1 | 1 | e | bit:int | int 0 | yes | `FFX_AtelSys_Core_377_WaitSignalFlag_start`, `FFX_AtelSys_Core_377_WaitSignalFlag_poll` | stores the bit in work, then polls dword_1326B70 every pass until the bit is set |  | V |
| 378 | 615 | 40 | e | bit:int | int bit | no | `FFX_AtelSys_Core_378_SetSignalFlag_resi` | sets bit 'bit' of dword_1326B70. Scene 94 at story 390: bit 10 also clears dword_12FB8B4, bit 9 sets it to 1 |  | V |
| 379 | 68 | 4 | e | bit:int | int bit | no | `FFX_AtelSys_Core_379_ClearSignalFlag_resi` | clears bit 'bit' of dword_1326B70 |  | V |
| 380 | 1 | 1 | e | trackId:int | int 1 | no | `FFX_AtelSys_Core_380_CueAndPlayBgmAlt_resi` | sub_8869E0(track) (driver 67A190), sub_8868B0(track) (start variant, volume 127), then current track = track |  | I |
| 381 | 0 | 0 |  | trackId:int | int 1 | no | dead `PlayBgmAlt` | sub_8868B0(track) (start variant, volume 127), then current track = track |  | I |
| 382 | 244 | 109 | e | trackId:int, param:int | int 1 | no | `FFX_AtelSys_Core_382_PlayBgmCrossfade_resi` | if sub_8863F0() >= 0 calls sub_886830(track, param), else sub_8869E0(track) and sub_886A20(track). Then current track = track |  | I |
| 383 | 145 | 77 | e | vol:int | int current track id (negative when none) | no | `FFX_AtelSys_Core_383_SetCurrentBgmVolume_resi` | when a current track exists, sub_886AB0(current, vol) |  | I |
| 384 | 403 | 103 | e | frames:int, vol:int | int current track id (negative when none) | no | `FFX_AtelSys_Core_384_FadeCurrentBgmVolume_resi` | when a current track exists, sub_886B40(current, frames, vol) |  | I |
| 385 | 174 | 23 | e |  | float motion +0x38 | no | `FFX_AtelSys_Core_385_GetActorRotZ_resf` | returns this actor's motion state float at +0x38, the rotation Z that core:39 and core:387 write |  | V |
| 386 | 0 | 0 |  | actorId:int | float motion +0x38 | no | dead `GetActorRotZOf` | core:385 for the actor named by actorId |  | V |
| 387 | 0 | 0 |  | angle:float | float angle | no | dead `SetActorRotZ` | wraps angle to [-pi, pi], sub_863FE0(actor, -1), motion +0x38 = angle, clears CHR flags1 0x400, kinds 4/5 push the pose to the map object |  | V |
| 388 | 2 | 2 | e | v:int | int 1 | no | `FFX_AtelSys_Core_388_SetBgmClampedParam_resi` | clamps v to -128..127 and calls sub_886A90 -> driver sub_67A250 |  | I |
| 389 | 1 | 1 | e | trackId:int, v:int | int 1 | no | `FFX_AtelSys_Core_389_SetBgmClampedParamOf_resi` | clamps v to -128..127 and calls sub_886AA0(track, v) -> driver sub_67A270 |  | I |
| 390 | 0 | 0 |  | v:int, unused:int | int 1 | no | dead `SetBgmClampedParamB` | pops 2, drops the top one, clamps v to -128..127 and calls sub_886930 -> driver sub_67A0F0 |  | I |
| 391 | 0 | 0 |  | trackId:int, v:int, unused:int | int trackId | no | dead `FadeBgmVolumeClamped` | pops 3, drops the top one, clamps v to -128..127 and calls sub_886940(track, v) -> driver sub_67A2D0 |  | I |
| 392 | 778 | 185 | e | frames:int | int frames | no | `FFX_AtelSys_Core_392_FadeOutCurrentBgm_resi` | frames > 0: fade out the current track, then start the queued dword_C526D0 track (id, volume). frames <= 0: stop and release it |  | I |
| 393 | 68 | 53 | eb | zone:int | int 0 | yield | `FFX_AtelSys_Core_393_RequestMapZone_start`, `FFX_AtelSys_Core_393_RequestMapZone_poll` | g_ffxPendingZoneMode = 2 and g_ffxPendingZonePlain = zone, a deferred map zone (section slot zone+2) change |  | V |
| 394 | 5 | 5 | e | bit:int | int bit | no | `FFX_AtelSys_Core_394_SetSignalFlagB_resi` | sets bit 'bit' of dword_1326B74, the second per map signal mask |  | V |
| 395 | 2 | 1 | e | bit:int | int bit | no | `FFX_AtelSys_Core_395_ClearSignalFlagB_resi` | clears bit 'bit' of dword_1326B74 |  | V |
| 396 | 0 | 0 |  | seId:int, a:int, b:int | int 1 | no | dead `SetSePitchDirect` | maps seId to bank and index (885D10/885D00) and calls SqsSePitchDirect |  | I |
| 397 | 0 | 0 |  | seId:int, a:int, b:int, c:int | int 1 | no | dead `SetSePitchTranslate` | maps seId to bank and index and calls SqsSePitchTranslate |  | I |
| 398 | 0 | 0 |  | level:int | int 0 | no | dead `SetSoundLevel134` | byte_1327134 = level (map reset 127), driver sub_67A7B0(level, or 0 when sub_872340() is 0), FMOD command 14, `FmodVoice__setVolume`. So 134 is the voice volume |  | V |
| 399 | 1 | 1 | e | frames:int, level:int | int 0 | no | `FFX_AtelSys_Core_399_FadeVoiceVolume_resi` | byte_1327134 = level, driver sub_67A7D0(frames, level or 0 when sub_872340() is 0), FMOD command 13, `FmodVoice__beginVolumeFade` with the frames halved. Role should be `FadeVoiceVolume` |  | V |
| 400 | 0 | 0 |  | level:int | int 0 | no | dead `SetSoundLevel135` | byte_1327135 = level (map reset 63), driver sub_67A6D0(level), FMOD command 12, an empty stub on PC. 63 is the centre the voice start passes as its first argument, so probably the voice pan |  | I |
| 401 | 0 | 0 |  | frames:int, level:int | int 0 | no | dead `FadeSoundLevel135` | byte_1327135 = level, driver sub_67A6F0(frames, level), FMOD command 11, an empty stub on PC |  | I |
| 402 | 28 | 10 | e | v:float | float v | no | `FFX_AtelSys_Core_402_SetPlayerReach_resf` | writes the current ATEL context float +0x1F0 (reset to 0.0) and returns it |  | V |
| 403 | 20 | 6 | e | seId:int | int seId | no | `FFX_AtelSys_Core_403_PlaySe_resi` | if the SE is not playing starts it (pan 63, vol 127), else unless its table flag 0x40 ramps its volume to 127 (63 when save +0x28) |  | I |
| 404 | 37 | 8 | e | on:int | int on | no | `FFX_AtelSys_Core_404_SetRikkuAltOutfit_resi` | save byte +0xD1 = on != 0, which makes Rikku's chr id 41 instead of 7 |  | V |
| 405 | 26 | 16 | e |  | int 0 | yes | `FFX_AtelSys_Core_405_WaitAtelBusyClear_poll` | poll only, returns 0 while dword_1326B58 != 0, else 5 |  | I |
| 406 | 94 | 21 | e | flags:int | int 1 | no | `FFX_AtelSys_Core_406_AddActorAttachFlags_resi` | for attach kinds 4 and 5: sub_862800 ORs flags and the +0xA8 id into a free one of 4 slots at actor+0xB18, then forwards by kind |  | I |
| 407 | 239 | 9 | e | nodeId:int | int 0 | no | `FFX_AtelSys_Core_407_AttachActorToMapNode_resi` | actor +0xA8 = nodeId and +0xAA = 5, binding the actor to a map object. Does not release the previous attachment |  | V |
| 408 | 56 | 4 | e | nodeId:int, a:int, b:int | int 1, or 0 when not bound to a map node | no | `FFX_AtelSys_Core_408_SetMapNodeAnim_resi` | only when this actor's attach kind is 5: sub_9144A0(&g_ffxMapWork, nodeId, a, (s16)b) |  | I |
| 409 | 49 | 4 | e | a:int, b:int | int 1, or 0 when not bound to a map node | no | `FFX_AtelSys_Core_409_SetMapNodeAnimSelf_resi` | core:408 with the actor's own +0xA8 node id, re-pushed and re-popped on the same stack |  | I |
| 410 | 0 | 0 |  | nodeId:int, unused:int, param:int | int 1 | no | dead `BindChrToMapNode` | pops 3, drops the middle one, and when a CHR is bound calls sub_9143C0(&g_ffxMapWork, nodeId, chr, param) |  | I |
| 411 | 0 | 0 |  | unused:int, param:int | int 1 | no | dead `BindChrToMapNodeSelf` | core:410 with the actor's own +0xA8 id as the node, the first pushed value is dropped |  | I |
| 412 | 6396 | 224 | e |  | int 1 | no | `FFX_AtelSys_Core_412_Nop_resi` | does nothing and returns 1 |  | V |
| 413 | 765 | 89 | e | disposeData:int | int 0 | no | `FFX_AtelSys_Core_413_ReleaseActorAttach_resi` | by +0xAA: 1 frees child CHRs and the CHR (data too when disposeData), 2 closes the camera handle, 4/5 push the pose. Clears +0xA8/+0xAA |  | V |
| 414 | 7 | 5 | e |  | int bool | no | `FFX_AtelSys_Core_414_CanBindPlayerLoose_resi` | pushes 0 then sub_86C530(0): returns (context byte 0 & 0x22) != 0 |  | V |
| 415 | 95 | 5 | e | strict:int | int bool | no | `FFX_AtelSys_Core_415_CanBindPlayer_resi` | sub_86C530: ctx byte 0 & 0x22, and when strict also ctx+1 bit 4 clear and ctx+3 & 0x84 clear, the sub_86A130 bind test |  | V |
| 416 | 7 | 4 | e | seId:int, pan:int, vol:int | int seId | no | `FFX_AtelSys_Core_416_PlaySeWithPanVol_resi` | not playing: starts the SE with pan and vol. Playing and table flag 0x40 clear: ramps vol (halved when save +0x28) and pan over 12 frames |  | I |
| 417 | 0 | 0 |  | node:int, w:float, f1:float, f2:float, f3:float | int 1 | no | dead `SetMapNodeMorphWeight` | pops 4 floats and a node index into a float[4] and calls sub_90A4D0(node, v) |  | V |
| 418 | 0 | 0 |  | node:int, w:float, f1:float, f2:float | int 1 | no | dead `SetMapNodeMorphWeight` | core:417 with one implicit trailing 0 pushed by the handler |  | V |
| 419 | 0 | 0 |  | node:int, w:float, f1:float | int 1 | no | dead `SetMapNodeMorphWeight` | core:417 with two implicit trailing 0s pushed by the handler |  | V |
| 420 | 128 | 7 | e | node:int, w:float | int 1 | no | `FFX_AtelSys_Core_420_SetMapNodeMorphWeight_resi` | core:417 with three implicit trailing 0s, so only the weight is supplied |  | V |
| 421 | 282 | 11 | e | animIdx:int, frame:int | int 1 | no | `FFX_AtelSys_Core_421_SetMapTexAnimFrame_resi` | when animIdx < count and frame < its frame count: sets the pending bit (mapWork +645576) and the frame (+645580), stride 1824 |  | V |
| 422 | 2 | 2 | e |  | int 1 | no | `FFX_AtelSys_Core_422_FillFieldPartyGaps_resi` | fills each empty (255) field party slot with the lowest index in-party character not already placed, then FFX_SaveData_SetFieldParty3 |  | V |
| 423 | 7 | 5 | e | treasureId:int | int 1 | yes | `FFX_AtelSys_Core_423_GiveTreasureSilent_start`, `FFX_AtelSys_Core_423_GiveTreasureSilent_poll`, `FFX_AtelSys_Core_423_GiveTreasureSilent_resi` | decodes the treasure and grants item, key item, equipment and gil with no message window | local* | V |
| 424 | 28 | 1 | e | charIndex:int | int dword at char record +0x54 | no | `FFX_AtelSys_Core_424_GetCharBattleCounter54_resi` | returns the char record dword +0x54, the second of the four per character battle counters at +0x50..+0x5C |  | V |
| 425 | 0 | 0 |  | charIdx:int | int counter | no | dead `GetCharBattleCounter0` | returns dword +0x50 of character record charIdx, the battles-taken-part counter bumped at battle end |  | V |
| 426 | 0 | 0 |  | charIdx:int | int counter | no | dead `GetCharBattleCounter2` | returns dword +0x58 of character record charIdx, the third of the four per-character battle counters |  | V |
| 427 | 8 | 4 | e |  | int battle count | no | `FFX_AtelSys_Core_427_GetBattleCount_resi` | thunk to sub_784F50, returns g_ffxBattleCount |  | V |
| 428 | 8 | 2 | e | speciesIdx:int | int capture count 0..10 | no | kept `FFX_AtelSys_Core_428_GetCaptureCount` | returns g_ffxMonsterCaptureCounts[speciesIdx & 0xFFF] from the save block |  | V |
| 429 | 81 | 4 | e | speciesIdx:int | int new count | no | kept `FFX_AtelSys_Core_429_AddCapture1` | FFX_SaveData_AddCaptureCount(speciesIdx, +1), clamped 0..10, returns the new count |  | V |
| 430 | 0 | 0 |  | speciesIdx:int | int new count | no | dead `SubCapture1` | FFX_SaveData_AddCaptureCount(speciesIdx, -1), clamped 0..10 |  | V |
| 431 | 4 | 2 | e | speciesIdx:int, delta:int | int new count | no | kept `FFX_AtelSys_Core_431_AddCaptureN` | FFX_SaveData_AddCaptureCount(speciesIdx, delta), clamped 0..10 |  | V |
| 432 | 25 | 11 | e | movieNo:int, mode:int | int movieNo | no | `FFX_AtelSys_Core_432_LoadPremovie_resi` | unless premovie state (save +0xE0) is 2..5 starts a load (sub_643AA0), state 1, no -> +0xD8, mode -> +0xDC. Same no, new mode -> state 3 |  | V |
| 433 | 16 | 5 | e |  | int 1 | no | `FFX_AtelSys_Core_433_CancelPremovie_resi` | sub_873DB0: premovie state (save +0xE0) = 6 and premovie number (+0xD8) = -1 |  | V |
| 434 | 1 | 1 | e | itemId:int | int held count | no | `FFX_AtelSys_Core_434_GetItemCount_resi` | FFX_SaveData_GetItemCount(itemId), held count minus one when it is the staged g_ffxItemInUse |  | V |
| 435 | 0 | 0 |  |  | int 0 | no | (null row) | null row: no start, poll 5, int 0 |  | V |
| 436 | 0 | 0 |  |  | int bool | no | dead `IsSoundLoadPending` | returns 1 when HIDWORD(qword_132FF14) in the sound module is nonzero, else 0 |  | I |
| 437 | 7 | 4 | e |  | int 1 started, 0 not ready | no | `FFX_AtelSys_Core_437_StartPremovie_resi` | sub_644280(1), then if premovie state (save +0xE0) is 2..4 calls sub_76F010 (a print stub on PC) and sets state 5, returns 1 |  | V |
| 438 | 3 | 1 | e |  | int bool | no | `FFX_AtelSys_Core_438_HasMarkedEquip_resi` | sub_7AD6E0: 1 when an in-use equipment entry has flag 0x08 set and 0x02 (locked) clear in g_ffxEquipmentArray |  | V |
| 439 | 1 | 1 | e | arg:int | int 1 done, 0 none | no | `FFX_AtelSys_Core_439_OverwriteMarkedEquip_resi` | if a marked unlocked equip entry exists, sub_7AD640(arg): sub_798FF0(arg) then copies scratch entry 0x2310DDE over it keeping id and owner | local* | V |
| 440 | 2 | 2 | e |  | int 1 found, 0 none | no | `FFX_AtelSys_Core_440_UnlockMarkedEquip_resi` | sub_7AD710(1): first in-use equip entry with flag 0x08 gets flag 0x02 cleared |  | V |
| 441 | 0 | 0 |  |  | int 1 found, 0 none | no | dead `LockMarkedEquip` | sub_7AD710(0): first in-use equip entry with flag 0x08 gets flag 0x02 set |  | V |
| 442 | 921 | 46 | e | actorId:int, armour:int | int chr id, 0 when actor +0x40 > 7 | no | `FFX_AtelSys_Core_442_GetActorEquipModelId_resi` | armour clamped 0..1, reads party index actor +0x40, returns equipped armour (1) or weapon (0) chr id via sub_86B150 |  | V |
| 443 | 4 | 4 | e |  | int sound result | no | `FFX_AtelSys_Core_443_IsBgmPlaying_resi` | sub_8863C0(0) -> sub_67A150(0) -> FMOD command 22, run inline on the step thread: `FmodMusic+32 \| track[FmodMusic+24].+32`, 1 while the current BGM is starting or playing. All 4 uses spin on it with WaitFrames(1), so they wait for the music to end on FMOD's clock. Role `IsBgmPlaying` (`SOUND_CLASSES.md` section 13) |  | V |
| 444 | 6 | 6 | e | nameId:int | int 1 | no | `FFX_AtelSys_Core_444_SetMapNameOverride_resi` | FFX_SaveData_SetMapNameOverride: save word +0xD4 = nameId, byte +0xD6 = 1 |  | V |
| 445 | 64 | 27 | e | on:int | int 1 | no | `FFX_AtelSys_Core_445_SetSaveFlagD7_resi` | sub_8715F0: save byte +0xD7 = (on != 0) |  | V |
| 446 | 0 | 0 |  |  | int byte | no | dead `GetSaveFlagD7` | sub_86C8A0: returns save byte +0xD7 |  | V |
| 447 | 20 | 11 | e |  | int 1 | no | `FFX_AtelSys_Core_447_SetFieldFlag1326B81_resi` | sub_8708D0: byte_1326B81 = 1 |  | V |
| 448 | 1 | 1 | e | slot:int, value:int | int value | no | `FFX_AtelSys_Core_448_SetFieldSlot1326CE0_resi` | slot clamped 0..3 (negative -> 0), dword_1326CE0[slot] = value |  | V |
| 449 | 0 | 0 |  |  | int 1 | no | dead `SetPremovieState4` | sub_873C40: premovie state (save +0xE0) = 4 when it is 2..5 |  | V |
| 450 | 2 | 1 | e |  | int 1 | no | kept `FFX_AtelSys_Core_450_ClampOptionWordE0` (role should be ResetPremovieState) | FFX_SaveData_ClampOptionWordE0: save dword +0xE0 = 1 when it is 2..5 |  | V |
| 451 | 13 | 11 | e | value:int | int value | no | `FFX_AtelSys_Core_451_SetSaveDwordE4_resi` | sub_874030: save dword +0xE4 = value |  | V |
| 452 | 27 | 23 | e | on:int | int bool | no | `FFX_AtelSys_Core_452_SetSaveFlagE8_resi` | sub_873DD0: save word +0xE8 = (on != 0), returns it |  | V |
| 453 | 9 | 7 | e | mode:int, remap:int | int status code | yes | `FFX_AtelSys_Core_453_RunMemCardCheck_start`, `FFX_AtelSys_Core_453_RunMemCardCheck_poll`, `FFX_AtelSys_Core_453_RunMemCardCheck_resi` | clears ctx +0x210 bit 0x10, dword_1330230 = mode. Actor +0xA8 0x5045 starts save UI state 2 and a save file op. Result remaps a status | local, load | V |
| 454 | 1 | 1 | e |  | int byte | no | `FFX_AtelSys_Core_454_ConsumeLatchC58E89_resi` | sub_88E5E0: returns byte_C58E89 and clears it |  | I |
| 455 | 0 | 0 |  |  | int kb | no | dead `GetSaveSizeKb` | sub_8741D0: cached (sub_88D390(42, 0) + 1023) / 1024 + 32, a size in KB |  | I |
| 456 | 0 | 0 |  | animId:int | int 1, 0 when not a map part actor | no | dead `PlaySelfMapPartAnim` | if actor +0xAA == 5, sub_9084B0(actor +0xA8, animId): anim >= 0 starts a map part anim, else stops it |  | V |
| 457 | 117 | 7 | e | partId:int, animId:int | int 1 | no | `FFX_AtelSys_Core_457_PlayMapPartAnim_resi` | sub_9084B0(partId, animId) on g_ffxMapWork: anim >= 0 starts part animation (sub_914920), negative stops it (sub_9149A0) |  | V |
| 458 | 26 | 6 | e |  | int 1 | no | `FFX_AtelSys_Core_458_SetSelfActorFlag38Bit4_resi` | own actor +0x38 OR= 4 |  | V |
| 459 | 0 | 0 |  | actorId:int | int 1 | no | dead `SetActorFlag38Bit4` | actor(actorId) +0x38 OR= 4 |  | V |
| 460 | 95 | 23 | e |  | int dword_132FF5C | no | `FFX_AtelSys_Core_460_GetPlayingBgm_resi` | jmp sub_8863F0, returns dword_132FF5C of the sound module, the value core:382 tests >= 0 to pick a crossfade. It is the shim's own record of the playing BGM, set by sub_886C70 on play and reset to -1 by sub_885E80 on stop or unload of that track, all on the step thread. Role `GetPlayingBgm` |  | V |
| 461 | 0 | 0 |  |  | int track id | no | dead `GetCurrentBgm` | sub_873B00: returns dword_1327130, the track id core:487 records |  | V |
| 462 | 72 | 33 | e |  | int 1 | no | `FFX_AtelSys_Core_462_ShowSelfChrEffects_resi` | for a CHR actor: sub_7FB290 starts the CHR's registry effect units (slots 4..7), sub_7FBAB0 clears the hide bit, actor +0x38 OR= 0x10 |  | V |
| 463 | 25 | 15 | e |  | int 1 | no | `FFX_AtelSys_Core_463_HideSelfChrEffects_resi` | for a CHR actor: sub_7FBA40 sets the hide bit 4 in the CHR effect registry, actor +0x38 &= ~0x10 |  | V |
| 464 | 73 | 21 | e |  | int 1 | no | `FFX_AtelSys_Core_464_SetSelfChrActorFlag38Bit8_resi` | own actor +0x38 OR= 8 when actor +0xAA == 1 (CHR backed) |  | V |
| 465 | 8659 | 260 | e | frames:int | int 0 | yes | `FFX_AtelSys_Core_465_WaitFramesSkippable_start`, `FFX_AtelSys_Core_465_WaitFramesSkippable_poll` | start stores frames, poll returns 5 at once when maybe_g_ffxMovieStreamFromHdd (0xF3C913) is nonzero, else counts down | local | V |
| 466 | 6 | 2 | e | msgId:int | int bool | no | `FFX_AtelSys_Core_466_CompareMessageToSaveString_resi` | strcmp of message msgId from the event message table (region variant per GetInternationalEnvId) with the save string at +0xEC | local | V |
| 467 | 0 | 0 |  |  | int 1 | no | dead `ReturnOne` | stub, returns 1 |  | V |
| 468 | 45 | 12 | e | slot:int, on:int | int bool | no | `FFX_AtelSys_Core_468_SetSelfAttachedChrFlag800_resi` | if attached CHR actor +0xAF4[slot] exists and mask +0xB14 has the slot, sets its m_flags1 0x800 to on, stores on in bit 0 of +0xB08[slot] |  | V |
| 469 | 0 | 0 |  | a:int, b:int | int 1 | no | dead `CallAssetLoaderSlot34` | calls g_ffxAssetLoader[34](a, b) |  | V |
| 470 | 0 | 0 |  | voiceId:int | int file index, 0 when not found | no | dead `RequestVoiceFile` | looks voiceId up in the voice table (sub_887790), resolves its file index via the asset loader (kind 25/27), calls loader [35] on it |  | I |
| 471 | 0 | 0 |  | fileIdx:int | int fileIdx | no | dead `RequestFileIndex` | calls g_ffxAssetLoader[35](fileIdx) |  | V |
| 472 | 0 | 0 |  | slot:int, layer:int, from:int, to:int, frames:int | int 1 | no | dead `SetTextOverlayFade` | sub_876050: slot 0..7, layer 0..1 of the 8x2 overlay records at 0x1328150, levels 0..127, frames (negative = instant), state 2 |  | I |
| 473 | 47 | 6 | e |  | int 1 | no | `FFX_AtelSys_Core_473_ResetRikkuNameDefault_resi` | copies kernel string group 3 id 0x202C (variant by the map name alt flag) over character name 6 | local* | V |
| 474 | 175 | 19 | e | layer:int, from:int, to:int, frames:int | int 1 | no | `FFX_AtelSys_Core_474_SetTextOverlay3Fade_resi` | sub_876050(3, layer, from, to, frames), the slot 3 form of core:472 |  | I |
| 475 | 0 | 0 |  | slot:int | int slot | no | dead `ClearTextOverlay` | sub_876290: slot clamped 0..7, state byte 0 and both layer records cleared |  | I |
| 476 | 23 | 16 | e |  | int 3 | no | `FFX_AtelSys_Core_476_ClearTextOverlay3_resi` | pushes 3 and pops it, sub_876290(3) |  | I |
| 477 | 0 | 0 |  | slot:int | int 0 | yes | dead `WaitTextOverlayIdle` | poll returns 0 while sub_875DE0(slot) (state byte at 0x1328172 + 36*slot) is nonzero, else 5 |  | I |
| 478 | 4 | 2 | e |  | int 0 | yes | `FFX_AtelSys_Core_478_WaitTextOverlay3Idle_start`, `FFX_AtelSys_Core_478_WaitTextOverlay3Idle_poll` | pushes 3 and pops it as the slot, then waits like core:477 |  | I |
| 479 | 0 | 0 |  | actorId:int | int 1 | no | dead `SetChrActorFlag38Bit8` | actor(actorId) +0x38 OR= 8 when its +0xAA == 1 |  | V |
| 480 | 5 | 3 | e | value:int | int 1, 0 when no CHR | no | `FFX_AtelSys_Core_480_SetSelfChrI408_resi` | for a CHR actor, FFX_Ch_SetI408: CHR +0x408 = value |  | V |
| 481 | 0 | 0 |  |  | int CHR +0x408, 0 when no CHR | no | dead `GetSelfChrI408` | for a CHR actor returns FFX_Ch_GetI408 |  | V |
| 482 | 80 | 32 | e |  | int 0 | no | `FFX_AtelSys_Core_482_EnableScreenWave_resi` | sub_9085A0: g_ffxMagicDrawFlags OR= 0x1000000, dword_18DED28 = dword_1934EA8 = 0, distortion enable sub_642F50(1) |  | V |
| 483 | 64 | 20 | e |  | int 0 | no | `FFX_AtelSys_Core_483_DisableScreenWave_resi` | sub_908450: g_ffxMagicDrawFlags &= ~0x1000000, dword_18DED28 = 0, distortion off sub_642F50(0) |  | V |
| 484 | 26 | 26 | e | a:float, b:float, c:float, mode:int | int 0 | no | `FFX_AtelSys_Core_484_SetScreenWaveParams_resi` | sub_90A470: flt_1934E90/94/98 = a, b, c and dword_1934EA0 = mode |  | V |
| 485 | 26 | 26 | e | r:int, g:int, b:int | int 0 | no | `FFX_AtelSys_Core_485_SetScreenWaveColor_resi` | sub_90A400: bytes 0x1934EA4..0x1934EA6 = r, g, b |  | I |
| 486 | 26 | 26 | e | flag:int, a:float, b:float | int flag | no | `FFX_AtelSys_Core_486_SetScreenWaveTarget_resi` | sub_90A430: flt_1934E7C = a, flt_1934E9C = b, dword_1934E78 OR= 1, also OR= 2 when flag |  | V |
| 487 | 5 | 5 | e | track:int, mode:int | int 1 | no | `FFX_AtelSys_Core_487_PlayBgm_resi` | sub_8869E0(track), sub_8868F0(track, mode) at volume 127, records track in dword_1327130 |  | I |
| 488 | 0 | 0 |  | id:int | int 1 | no | dead `SoundCmdById` | sub_886D80(id): id -1 goes straight to sub_81E5D0(-1), else sub_885D10/sub_885D00 then sub_81E5D0 |  | I |
| 489 | 318 | 17 | e | winId:int, wait:int | int winId | yes | kept `FFX_AtelOp_MesWinCloseAndWaitGone_start`, kept `FFX_AtelOp_MesWinCloseAndWaitGone_poll`, kept `FFX_AtelOp_MesWinCloseAndWaitGone_resi` | FFX_Atel_MesWinRequestClose(winId), poll waits for window state 0 or 4 when wait is nonzero |  | V |
| 490 | 0 | 0 |  | var:int | int 1 | no | dead `SetScriptTextTable` | resolves the variable descriptor and stores its address at context +0x44, the table core:492 reads |  | V |
| 491 | 105 | 27 | e | entry:int | int 1 | no | `FFX_AtelSys_Core_491_ShowTextOverlay1FromSysTable_resi` | sub_875FB0(1, entry, table 0x13388C0): loads entry (<= 63) from the built in table into overlay slot 1, state 1 |  | I |
| 492 | 0 | 0 |  | slot:int, entry:int | int 1 | no | dead `ShowTextOverlayFromScriptTable` | if context +0x44 is set, sub_875FB0(slot, entry, that table) |  | I |
| 493 | 0 | 0 |  | entry:int | never returns | no | dead `ShowTextOverlay2FromScriptTable` | real infinite loop in the binary: prints a 'Virtuos Warnig: Need to check this func' stub, pops, pushes 2 and the entry, jumps back |  | V |
| 494 | 5 | 2 | e |  | int 1 | no | `FFX_AtelSys_Core_494_ClearTextOverlay1_resi` | pushes 1 and pops it, sub_876290(1) |  | I |
| 495 | 0 | 0 |  |  | int 2 | no | dead `ClearTextOverlay2` | pushes 2 and pops it, sub_876290(2) |  | I |
| 496 | 2 | 2 | e | value:float | float value | no | `FFX_AtelSys_Core_496_SetGlobalFloat1327124_resf` | sub_873E50: flt_1327124 = value |  | V |
| 497 | 0 | 0 |  |  | float value | no | dead `GetGlobalFloat1327124` | sub_873AD0: returns flt_1327124 |  | V |
| 498 | 2 | 1 | e |  | int mask bits 8..15 | no | `FFX_AtelSys_Core_498_GetAeonPartyMask_resi` | bit i set for each character 8..15 (the aeons) with the in-party bit in its record |  | V |
| 499 | 1 | 1 | e | maskB:int, maskA:int, destVar:int | int countB << 8 | no | `FFX_AtelSys_Core_499_BuildAeonChoiceText_resi` | writes the names of chars 8..15 in maskA OR maskB into the string var, maskB entries get numbered choice markup, returns their count << 8 |  | V |
| 500 | 1 | 1 | e | n:int, mask:int | int char index 8..15, -1 none | no | `FFX_AtelSys_Core_500_GetNthAeonInMask_resi` | returns the index of the n-th set bit of mask among bits 8..15 |  | V |
| 501 | 0 | 0 |  | on:int | int bool | no | dead `SetVirtualPadEnabled` | FFX_VirtualPad_SetEnabled(on != 0) |  | V |
| 502 | 0 | 0 |  | bit:int | int bit | no | dead `SetVirtualPadButton` | FFX_VirtualPad_SetButtons(1 << bit), next frame's scripted pad mask |  | V |
| 503 | 0 | 0 |  | a:int, b:int, c:int | int 0 | no | dead `SetGlobals1326B84` | sub_86E840: dword_1326B84 = a, dword_1326B88 = b, dword_1326B8C = c |  | V |
| 504 | 32 | 18 | e | track:int, fade:int | int fade | no | `FFX_AtelSys_Core_504_PlayOrStopBgm_resi` | fade > 0 and track >= 0: sub_886AF0(track, fade). fade <= 0: stops the current BGM (sub_886A60, sub_886800 on dword_1327130) |  | I |
| 505 | 71 | 13 | e | charIdx:int | int 1 | no | `FFX_AtelSys_Core_505_RemovePartyMemberLocked_resi` | runs core:203 (out of party, cleared from the field party) then FFX_SaveData_SetCharSelectable(charIdx, 0) |  | V |
| 506 | 34 | 18 | e |  | int result | no | kept `FFX_AtelSys_Core_506_GetSaveLoadMenuResult` | thunk to FFX_SaveLoadMenu_GetResult, returns g_ffxSaveLoadMenuResult |  | V |
| 507 | 31 | 17 | e |  | int dword_186A228 | no | `FFX_AtelSys_Core_507_GetSaveLoadMenuResultRaw_resi` | jmp 0x8BEEF0 = mov eax, dword_186A228, the word FFX_SaveLoadMenu_SetResult writes. Not a function in the IDB |  | V |
| 508 | 48 | 4 | e | charIdx:int, abilityId:int | int ? | no | `FFX_AtelSys_Core_508_SetCharAbilityFlag_resi` | sub_874200(charIdx, abilityId) -> FFX_SaveData_SetCharAbilityFlag(charIdx, abilityId & 0xFFF OR 0x3000, a3) |  | V |
| 509 | 0 | 0 |  | charIdx:int, abilityId:int | int ? | no | dead `SetCharAbilityFlag` | identical body to core:508 |  | V |
| 510 | 0 | 0 |  | charIdx:int, abilityId:int | int bool | no | dead `TestCharAbilityFlag` | FFX_SaveData_TestCharAbilityBit(charIdx, abilityId): bit of record +0x3E words for 0x3000 ids, 0x3060 and up a shared bitmap |  | V |
| 511 | 0 | 0 |  |  | int 1 | no | dead `SetSelfSoundMaskBits` | if actor +0x40 != -1 sets bit (actor +0x40) in dword_132FF34 and dword_132FF3C (sub_887100/sub_887130) |  | I |
| 512 | 10 | 4 | e | idx:int | int 1 | no | `FFX_AtelSys_Core_512_SetSoundMaskBits_resi` | sets bit idx in dword_132FF34 and dword_132FF3C |  | I |
| 513 | 10 | 5 | e |  | int 1 | no | `FFX_AtelSys_Core_513_ClearSelfSoundMaskBits_resi` | if actor +0x40 != -1 clears bit (actor +0x40) in dword_132FF34 and dword_132FF3C |  | I |
| 514 | 21 | 9 | e | idx:int | int 1 | no | `FFX_AtelSys_Core_514_ClearSoundMaskBits_resi` | clears bit idx in dword_132FF34 and dword_132FF3C |  | I |
| 515 | 29 | 8 | e | modelId:int | int core:SetActorModel result | yes | `FFX_AtelSys_Core_515_LoadAndSetActorModel_start`, `FFX_AtelSys_Core_515_LoadAndSetActorModel_poll`, `FFX_AtelSys_Core_515_LoadAndSetActorModel_resi` | poll waits for idle attach/loader/sound, marks actor pending, reads motion set and rom data. resi runs SetActorModel, clears pending | load | V |
| 516 | 13 | 5 | e | value:float | float value | no | `FFX_AtelSys_Core_516_SetActorMotionF2C_resf` | own motion state block float +0x2C = value |  | V |
| 517 | 12 | 10 | e |  | int 1 | no | `FFX_AtelSys_Core_517_EnableSelfChrFxKind0_resi` | for a CHR actor sub_862700(actor, 0): starts the CHR effect set and kind 0, actor +0x38 OR= 0x20 |  | V |
| 518 | 8 | 3 | e |  | int 1 | no | `FFX_AtelSys_Core_518_DisableSelfChrFxKind0_resi` | for a CHR actor sub_862680(actor, 0): stops kind 0, clears +0x38 0x20, stops the set when no kind is left |  | V |
| 519 | 103 | 12 | e |  | int 1 | no | `FFX_AtelSys_Core_519_EnableSelfChrFxKind1_resi` | for a CHR actor sub_862700(actor, 1): kind 1, actor +0x38 OR= 0x40 |  | V |
| 520 | 0 | 0 |  |  | int 1 | no | dead `DisableSelfChrFxKind1` | for a CHR actor sub_862680(actor, 1): stops kind 1, clears +0x38 0x40 |  | V |
| 521 | 39 | 12 | e |  | int 1 | no | `FFX_AtelSys_Core_521_EnableSelfChrFxKind2_resi` | for a CHR actor sub_862700(actor, 2): kind 2, actor +0x38 OR= 0x80 |  | V |
| 522 | 4 | 3 | e |  | int 1 | no | `FFX_AtelSys_Core_522_DisableSelfChrFxKind2_resi` | for a CHR actor sub_862680(actor, 2): stops kind 2, clears +0x38 0x80 |  | V |
| 523 | 52 | 11 | e |  | int 1 | no | `FFX_AtelSys_Core_523_SetSelfActorFlag38Bits104_resi` | own actor +0x38 OR= 0x100 and OR= 4 |  | V |
| 524 | 0 | 0 |  | actorId:int | int 1 | no | dead `SetActorFlag38Bits104` | actor(actorId) +0x38 OR= 0x100 and OR= 4 |  | V |
| 525 | 17 | 15 | e | value:int | int value | no | `FFX_AtelSys_Core_525_SetSaveWordEA_resi` | sub_870FA0: save word +0xEA = value |  | V |
| 526 | 0 | 0 |  |  | int u16 | no | dead `GetSaveWordEA` | sub_86C4B0: returns save word +0xEA |  | V |
| 527 | 24 | 2 | e | sceneId:int | int effective scene id | no | `FFX_AtelSys_Core_527_SetEncounterSceneOverride_resi` | g_ffxAtelSceneIdOverride = sceneId, then resets the encounter accumulator and zone weights for the override (or the map scene when <= 0) |  | V |
| 528 | 2724 | 3 | e | flagId:int | int flagId | no | `FFX_AtelSys_Core_528_SetWorldFlag_resi` | Sets bit flagId of g_ffxWorldFlagBits3DCC (saveData+0x3DCC, 1024 bits, clamped to byte 127) via FFX_SaveData_SetWorldFlag3DCC |  | V |
| 529 | 0 | 0 |  | flagId:int | int flagId | no | dead `ClearWorldFlag` | Clears bit flagId of g_ffxWorldFlagBits3DCC (saveData+0x3DCC) via FFX_SaveData_SetWorldFlag3DCC(id, 0) |  | V |
| 530 | 2 | 1 | e | enable:int, heightY:float | int enable (0/1) | no | `FFX_AtelSys_Core_530_SetIndoorCameraHeightTest_resi` | Writes ctx+0x208 float = heightY and ctx byte +2 bit 0 = enable, the per-frame indoor test by camera height (camera y >= threshold) |  | V |
| 531 | 5016 | 7 | e | boneId:int | int joint node index, 0 when no CHR model | no | `FFX_AtelSys_Core_531_GetSelfBoneNodeIndex_resi` | Pops boneId, pushes own actor id (+0x2E) and boneId, then does core:532: FFX_Ch_GetBoneNodeIndex on the caller's CHR when attach kind is 1 |  | V |
| 532 | 0 | 0 |  | actorId:int, boneId:int | int joint node index, 0 when no CHR model | no | dead `GetActorBoneNodeIndex` | FFX_Ch_GetBoneNodeIndex(actor->CHR, boneId) for an actor whose +0xAA attach kind is 1 and +0x9C CHR is set, else 0 |  | V |
| 533 | 290 | 3 | e | kind:int, row:int | int 1 ok, 0 when kind >= 18 or no record | no | `FFX_AtelSys_Core_533_SetEquipAbilitiesFromTable_resi` | FFX_Equip_SetAbilitySlotsFromTable(kind 0..17, row 1..2): copies 4 auto-ability u16 from a fixed table into an equip record |  | V |
| 534 | 32 | 15 | e | window:int, unitId:int, abilityId:int | int SetCharAbilityFlag result | yes | `FFX_AtelSys_Core_534_TeachAbilityWithMessage_start`, `FFX_AtelSys_Core_534_TeachAbilityWithMessage_poll`, `FFX_AtelSys_Core_534_TeachAbilityWithMessage_resi` | If unit lacks the ability: cue, window with unit and ability names, waits confirm (Core 132), then resi sets the char ability flag | local* | V |
| 535 | 20 | 18 | e |  | int 1 | no | `FFX_AtelSys_Core_535_ClearSpoils_resi` | FFX_Spoils_Clear: FFX_Spoils_Reset(g_ffxTreasureStaging, 7), empties the staged treasure / spoils list |  | V |
| 536 | 0 | 0 |  | speciesIndex:int | int capture count 0..10 | no | dead `GetCaptureCount` | FFX_SaveData_GetCaptureCount(species): g_ffxMonsterCaptureCounts[idx & 0xFFF] |  | V |
| 537 | 0 | 0 |  | speciesIndex:int, delta:int | int new count, clamped 0..10 | no | dead `AddCaptureCount` | FFX_SaveData_AddCaptureCount(species, delta), clamps the capture count to 0..10, refuses index 255 |  | V |
| 538 | 104 | 2 | e | areaKey:int, minCount:int | int count of species | no | kept `FFX_AtelSys_Core_538_CountAreaSpeciesAtLeast` | FFX_Arena_CountSpeciesWithAtLeast(0, areaKey, minCount): species of one area captured at least minCount times |  | V |
| 539 | 218 | 2 | e | speciesKey:int, minCount:int | int count of species | no | kept `FFX_AtelSys_Core_539_CountSpeciesGroupAtLeast` | FFX_Arena_CountSpeciesWithAtLeast(1, speciesKey, minCount): species of one species group captured at least minCount times |  | V |
| 540 | 0 | 0 |  | minCount:int | int count of species | no | dead `CountAllSpeciesAtLeast` | FFX_Arena_CountSpeciesWithAtLeast(2, 0, minCount): every species captured at least minCount times |  | V |
| 541 | 2 | 2 | e |  | int 1 if one was equipped, else 0 | no | `FFX_AtelSys_Core_541_EquipFlaggedWeaponOnChar0_resi` | Lists char 0 weapons (sub_7ABD10(0,0,0)), equips any with equip entry +3 bit 0x08 into char 0 weapon slot, recomputes derived stats |  | V |
| 542 | 35 | 5 | e |  | int 1 | no | `FFX_AtelSys_Core_542_RunMapWorkOp913F50_resi` | Calls sub_90A420 -> sub_913F50(&g_ffxMapWork), an unread operation on the map work block |  | I |
| 543 | 0 | 0 |  | bit:int | int bit | no | dead, `FFX_AtelSys_Core_543_ClearTalkBonusSuppress_resi` | Clears bit (when < 8) of g_ffxAtelTalkBonusSuppressMask via FFX_Atel_SetTalkBonusSuppressed(bit, 0) |  | V |
| 544 | 30 | 20 | e | bit:int | int bit | no | `FFX_AtelSys_Core_544_SetTalkBonusSuppress_resi` | Sets bit (when < 8) of g_ffxAtelTalkBonusSuppressMask via FFX_Atel_SetTalkBonusSuppressed(bit, 1) |  | V |
| 545 | 60 | 17 | e | index:int | int pointer off_C86E28[index] | no | `FFX_AtelSys_Core_545_GetTablePtrC86E28_resi` | Returns entry index of the pointer table off_C86E28 unchecked, as an int |  | V |
| 546 | 0 | 0 |  | index:int | int pointer off_C8756C[index] | no | dead `GetTablePtrC8756C` | Returns entry index of the pointer table off_C8756C unchecked, as an int |  | V |
| 547 | 0 | 0 |  | on:int | int on (0/1) | no | dead `SetOptionBit4AndApplyPad` | Sets option flags (saveData+0x3D0C) bit 0x10 then sub_872480 pushes it to the pad layer via sub_8894D0(0,0,bit) |  | V |
| 548 | 0 | 0 |  |  | int option flags bit 0x10 | no | dead `GetOptionBit4` | Returns bit 4 (0x10) of the option flags dword at saveData+0x3D0C |  | V |
| 549 | 255 | 37 | e | value:int | int value | no | `FFX_AtelSys_Core_549_SetFieldParam1326B6C_resi` | Stores value in dword_1326B6C via sub_870F60, which map load resets to 100 |  | V |
| 550 | 16 | 7 | e | on:int | int on (0/1) | no | `FFX_AtelSys_Core_550_SetGlobalFlagCEC1F0_resi` | Stores the bool in dword_CEC1F0 via sub_729050 |  | V |
| 551 | 0 | 0 |  | on:int | int on (0/1) | no | dead `SetSaveWordE8` | Writes the bool to the save block u16 at +0xE8 via sub_873DD0, a word map load zeroes |  | V |
| 552 | 0 | 0 |  | mode:int, quarterScale:int, halfScale:int | int scaled angle | no | dead `GetTurnAngleScaled` | abs(wrap(actor+0x884+0x44*level float - motion +0x34)), mode&2: /pi*halfScale, else /(pi/2)*quarterScale, clamped at 1.0 of the scale |  | V |
| 553 | 1 | 1 | e |  | int 1 | no | `FFX_AtelSys_Core_553_SetActorFlags38Bit200_resi` | ORs 0x200 into the caller's actor flag word at +0x38 |  | V |
| 554 | 0 | 0 |  |  | int 1 | no | dead `SetActorFlags38Bit400` | ORs 0x400 into the caller's actor flag word at +0x38 |  | V |
| 555 | 0 | 0 |  |  | int 0 | yes | dead `WaitFlag1A85F70` | Counts down 10 polls, then polls sub_A547E0 (dword_1A85F70) until non-zero |  | V |
| 556 | 0 | 0 |  | axis:int, value:int | int 1 | no | dead `SetVirtualPadAxis` | FFX_VirtualPad_SetAxis(axis 0..3, value byte): next frame virtual stick axis |  | V |
| 557 | 0 | 0 |  | mask:int | int 0 | yes | dead `WaitPadPressed` | Polls FFX_Atel_GetPadPressed (remapped, identity) until it shares a bit with mask | input | V |
| 558 | 1 | 1 | e | group0:int, group1:int, group2:int, group3:int | int 1 | no | `FFX_AtelSys_Core_558_SetIndoorEffectGroups_resi` | sub_870280 writes dword_1326CF0..CFC, the four placed effect groups the indoor switch stops and starts |  | V |
| 559 | 20 | 5 | e | slot:int, on:int | int on (0/1) | no | `FFX_AtelSys_Core_559_SetAttachSlotChrFlag8_resi` | If attach slot CHR (actor+0xAF4[slot]) is set and loaded (+0xB14 mask), sets its m_flags2 bit 0x8, and slot flag +0xB08[slot] bit 2 |  | V |
| 560 | 0 | 0 |  |  | int 1 | no | dead `SetActorFlags38Bit800` | ORs 0x800 into the caller's actor flag word at +0x38 |  | V |
| 561 | 5 | 1 | e | actorId:int | int 1 | no | `FFX_AtelSys_Core_561_SetOtherActorFlags38Bit800_resi` | ORs 0x800 into the +0x38 flag word of actor actorId (same bit as core:560) |  | V |
| 562 | 0 | 0 |  | bit:int | int 0 | yes | dead `PressVirtualPadButton` | Holds virtual pad button mask 1<<bit for sub_A54750() = 2 polls via FFX_VirtualPad_SetButtons |  | V |
| 563 | 0 | 0 |  | prio:int | int prio | no | dead `SetModuleDrawPrio` | FFX_Module_SetDrawPrio: g_ffxModuleDrawPrio = prio |  | V |
| 564 | 5 | 5 | e |  | int 1 | no | `FFX_AtelSys_Core_564_CommitStagedPrimerGrants_resi` | If a grant is staged (dword_1325B90), sets key item 0xA004+i for each staged bit i < 26 not already owned, then clears the stage |  | V |
| 565 | 5 | 5 | e |  | int dword_1325B8C | no | `FFX_AtelSys_Core_565_GetPrimerStageWord_resi` | Returns dword_1325B8C through sub_8729D0. FFX_SaveFile_ImportPrimerFromSlot ORs an imported save file's primer mask into it, so it is the local disk's primer mask as the script sees it. MINIGAMES_PROGRESSION.md 2.4 |  | V |
| 566 | 777 | 2 | e | messageId:int | int pointer to message text | no | `FFX_AtelSys_Core_566_GetMessageTextPtr_resi` | Returns the message text pointer: ctx+0x38 table under the international env, else FFX_Atel_GetMessageText (ctx+0x34), builtin fallback | local | V |
| 567 | 51 | 2 | e | count:int, enabledMask:int, srcVar:int, destVar:int | int enabledCount << 8 | no | `FFX_AtelSys_Core_567_BuildChoiceListText_resi` | Concatenates count strings from the pointer array at srcVar into destVar, tagging enabled entries (mask bits) with numbered choice codes |  | V |
| 568 | 0 | 0 |  | bit:int | int 0/1 | no | dead `TestEventFlagBit` | Tests bit of g_ffxEventFlagBits (saveData+0x4C, 640 bits), 0 when out of range |  | V |
| 569 | 3 | 3 | e | bit:int | int 1 | no | `FFX_AtelSys_Core_569_SetEventFlagBit_resi` | Sets bit of g_ffxEventFlagBits (saveData+0x4C, 640 bits) when in range |  | V |
| 570 | 26 | 2 | e | on:int | int 1 | no | `FFX_AtelSys_Core_570_SetOptionBit7AndApply_resi` | Sets option flags bit 0x80, calls sub_887180(bit7 ? 0 : 2 - bit0) (sound mode?), then reapplies the pad option bit 0x10 |  | V |
| 571 | 72 | 2 | e | flagId:int | int 0/1 | no | `FFX_AtelSys_Core_571_TestWorldFlag_resi` | FFX_SaveData_TestWorldFlag3DCC(flagId): one bit of saveData+0x3DCC |  | V |
| 572 | 74 | 2 | e | baseFlag:int, count:int | int 1 if all count flags from base are set | no | kept `FFX_AtelSys_Core_572_AllWorldFlagsSet` | Tests world flags base..base+count-1 in saveData+0x3DCC, returns 1 only if every one is set |  | V |
| 573 | 3 | 2 | e |  | int 1 | no | kept `FFX_AtelSys_Core_573_BeginNewGame` | Leaves the title screen, FFX_InitNewSaveData (full save block reset), pumps loads. The New Game reset | rng:reseeds all streams from the clock, clock, local | V |
| 574 | 0 | 0 |  | on:int | int value | no | dead `SetSoundFlag1330091` | sub_886DD0: byte_1330091 = value != 0, a sound layer flag map load sets and clears |  | V |
| 575 | 25 | 3 | e | value:int | int value | no | `FFX_AtelSys_Core_575_SetGlobal1327F4C_resi` | sub_8749E0: dword_1327F4C = value |  | V |
| 576 | 2 | 2 | e | charIndex:int, on:int | int on (0/1) | no | `FFX_AtelSys_Core_576_LockEquippedGear_resi` | FFX_SaveData_LockEquippedGear: sets or clears equip entry +3 bit 0x02 on the char's weapon and armour (char < 7) |  | V |
| 577 | 1800 | 156 | e | on:int | int on (0/1) | no | `FFX_AtelSys_Core_577_SetContextFlag210Bit8_resi` | Writes bit 0x08 of the ATEL context dword +0x210 (ctx+528), on by default from the context init |  | V |
| 578 | 2 | 2 | e |  | int 2 if byte_112A50A else byte_112A509 | no | `FFX_AtelSys_Core_578_GetMemCheckStatus_resi` | sub_777E90: returns 2 when byte_112A50A is set, otherwise byte_112A509 | local | I |
| 579 | 3 | 2 | e |  | int dword_112A518 | no | `FFX_AtelSys_Core_579_GetMemCheckWord_resi` | sub_7787B0: returns dword_112A518, next to core:578's status bytes | local | I |
| 580 | 3 | 3 | e |  | int 1 | no | `FFX_AtelSys_Core_580_ClearEventEffectFlag800_resi` | Clears bit 0x800 of dword_11A0080 (event effect flags) and refreshes each effect registry entry +22 from FFX_Ch_GetB185 |  | V |
| 581 | 3 | 3 | e |  | int 1 | no | `FFX_AtelSys_Core_581_SetEventEffectFlag800_resi` | Sets bit 0x800 of dword_11A0080 (event effect flags) |  | V |
| 582 | 0 | 0 |  |  | int 1 | no | dead `ClearEventEffectFlag2000` | Clears bit 0x2000 of dword_11A0080 |  | V |
| 583 | 0 | 0 |  |  | int 1 | no | dead `SetEventEffectFlag2000` | Sets bit 0x2000 of dword_11A0080 |  | V |
| 584 | 0 | 0 |  |  | int 1 | no | dead `ClearEventEffectFlag1000` | Clears bit 0x1000 of dword_11A0080 |  | V |
| 585 | 0 | 0 |  |  | int 1 | no | dead `SetEventEffectFlag1000` | Sets bit 0x1000 of dword_11A0080 |  | V |
| 586 | 2 | 1 | e | on:int | int on (0/1) | no | kept `FFX_AtelSys_Core_586_SetOptionBit13` | Writes option flags bit 13 (0x2000) at saveData+0x3D0C and calls FFX_Menu_ApplySaveMovieOption |  | V |
| 587 | 34 | 4 | e | slot:int, hide:int | int hide (0/1) | no | `FFX_AtelSys_Core_587_SetAttachSlotHidden_resi` | If attach slot is loaded (+0xB14 mask) and slot id +0xB04 != 255, FFX_Ch_SetHideBit1 on the slot CHR (+0xAF4[slot]) |  | V |
| 588 | 10 | 1 | e |  | int 1 | no | `FFX_AtelSys_Core_588_SetActorFlags38Bit1000_resi` | ORs 0x1000 into the caller's actor flag word at +0x38 |  | V |
| 589 | 39 | 7 | e | on:int | int on (0/1) | no | `FFX_AtelSys_Core_589_SetContextFlag210Bit40_resi` | Writes bit 0x40 of the ATEL context dword +0x210 (ctx+528), on by default from the context init |  | V |
| 590 | 0 | 0 |  | value:int | int value | no | dead `SetContextField214` | Writes the ATEL context dword +0x214 (ctx+532), which context init sets to -1 |  | V |
| 591 | 0 | 0 |  |  | int pad word 28, remapped | no | dead `GetPadRepeatRemapped` | Returns FFX_Atel_GetPadWord28 (the auto repeat mask, wall clock timed) through FFX_Pad__remapButtonMask | input, clock | V |
| 592 | 0 | 0 |  |  | int pad word 28 | no | dead `GetPadRepeat` | Returns FFX_Atel_GetPadWord28, the wall clock auto repeat button mask | input, clock | V |
| 593 | 12 | 2 | e | bit:int | int 0/1 | no | `FFX_AtelSys_Core_593_TestPadRepeatBitRemapped_resi` | Tests 1<<bit in the remapped auto repeat mask (FFX_Atel_GetPadWord28) | input, clock | V |
| 594 | 0 | 0 |  | bit:int | int 0/1 | no | dead `TestPadRepeatBit` | Tests 1<<bit in the raw auto repeat mask (FFX_Atel_GetPadWord28) | input, clock | V |
| 595 | 181 | 38 | e |  | int 0/1 | no | `FFX_AtelSys_Core_595_IsWesternMasterVersion_resi` | Reads the LanguageManager master version (sub_641100), 1 when it is not 0, 3 or 4 (0 JP, 3 Asia) | local | V |
| 596 | 8 | 3 | e | gridType:int | int gridType | no | kept `FFX_AtelSys_Core_596_SetSphereGridTypeAndInit` | Writes option flags bits 14..15 (sphere grid type) and rebuilds nodes with FFX_SphereGrid_InitNodes |  | V |
| 597 | 1 | 1 | e |  | int grid type 0..3 | no | `FFX_AtelSys_Core_597_GetSphereGridType_resi` | Returns option flags bits 14..15, the sphere grid type |  | V |
| 598 | 163 | 47 | e | window:int | int window | no | `FFX_AtelSys_Core_598_SetMessageWindowFlag8000_resi` | Writes 0x8000 into message window dword +0x28 |  | V |
| 599 | 170 | 47 | e | window:int | int window | no | `FFX_AtelSys_Core_599_ClearMessageWindowFlag8000_resi` | Writes 0 into message window dword +0x28 |  | V |
| 600 | 0 | 0 |  | international:int | int 0 | yes | dead `SwitchTextLanguageAndReload` | If the bool differs from the international env id, sets option bit 0x10000, picks asset kind 4 (`_cdindex_menu_inter_us`) or 18 (`_cdindex_menu`), not a language (corrected 2026-10-28), poll reloads the text and asset tables | rng (battle start rolls, via a load path), clock (heap cookie only), local, load | V |
| 601 | 8 | 2 | e | index:int, scale:float | float scale | no | `FFX_AtelSys_Core_601_SetTextSpeedScale_resf` | FFX_Atel_SetTextSpeedScale: g_ffxAtelTextSpeedScale[index != 0] = scale |  | V |
| 602 | 0 | 0 |  |  | int text language variant | no | dead `GetTextLangVariant` | Thunk to FFX_MesWin_GetTextLangVariant, no pops | local | I |
| 603 | 59 | 12 | e | trophyId:int | int id used | no | `FFX_AtelSys_Core_603_UnlockTrophy_resi` | FFX_Trophy_Unlock (Steam achievement), with 23 -> 22 and 25 -> 24 remaps and id 22 itself ignored. Scripts pass the console trophy numbering, native code the Steam row, `STEAM_ACHIEVEMENTS.md` 5 |  | V |
| 604 | 86 | 17 | e |  | int 0 | no | `FFX_AtelSys_Core_604_ReturnZero_resi` | Stub, sub_86C270 returns 0 |  | V |
| 605 | 5 | 5 | e | value:int | int value | no | `FFX_AtelSys_Core_605_SetGlobal1325AC4_resi` | Stores value in dword_1325AC4 |  | V |
| 606 | 2 | 1 | e | value:int | int 1 | no | `FFX_AtelSys_Core_606_CallPresentation643E80_resi` | sub_643E80: sub_6D2390() then sub_6CE0C0(value), unread presentation side calls |  | I |
| 607 | 2 | 1 | e | value:?, mode:int | int 0 | no | `FFX_AtelSys_Core_607_SetPresentationMode643500_resi` | sub_643500(value, mode): toggles dword_CE517C+1 and Phyre app +928, mode 0 with a zero record also starts sub_6E4FF0(3.0, 1, 0.0) |  | I |
| 608 | 12 | 11 | e | ?:int | int 1 | no | `FFX_AtelSys_Core_608_PopOneReturnOne_resi` | Stub, pops 1 and returns 1 |  | V |
| 609 | 11 | 11 | e | ?:int | int 1 | no | `FFX_AtelSys_Core_609_PopOneReturnOne_resi` | Stub, pops 1 and returns 1 |  | V |
| 610 | 3 | 3 | e |  | int 1 | no | `FFX_AtelSys_Core_610_ReturnOne_resi` | Stub, returns 1 |  | V |
| 611 | 2 | 2 | e |  | int 1 | no | `FFX_AtelSys_Core_611_ReturnOne_resi` | Stub, returns 1 |  | V |
| 612 | 1 | 1 | e | on:int | int 0 | no | `FFX_AtelSys_Core_612_SetSaveConfigBit5EC_resi` | FFX_Menu_SetSaveConfigBit5EC: saveData+0x5EC bit 0 = on, then applies it to the engine |  | V |
| 613 | 3 | 3 | e |  | int g_ffxBlitzCheatEnabled | no | `FFX_AtelSys_Core_613_GetBlitzCheatEnabled_resi` | Returns g_ffxBlitzCheatEnabled, the debug page Blitz Cheat toggle |  | V |
| 614 | 1 | 1 | e |  | int saveData+0x5EC bit 0 | no | `FFX_AtelSys_Core_614_GetSaveConfigBit5EC_resi` | FFX_Menu_GetSaveConfigBit5EC |  | V |
| 615 | 3 | 2 | e | window:int, messageId:int, defaultChoice:int, cancelChoice:int, x:int, y:int, anchor:int | int chosen index | yes | kept `FFX_AtelOp_AskChoice315_start`, `FFX_AtelSys_Core_615_AskChoicePreviewConfigBit5EC_poll`, `FFX_AtelSys_Core_615_AskChoicePreviewConfigBit5EC_resi` | Core 315 ask composite, poll calls FFX_Menu_SetSaveConfigBit5EC(cursor) every frame so the highlighted option applies live, returns the pick | local* | V |
