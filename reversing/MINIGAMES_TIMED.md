# Timed and reflex minigames

Chocobo racing, lightning dodging, butterfly catching, the Cactuar hunt, the Via Purifico and the
Jecht Shot tutorial. What drives each one, what counts its clock, and which of them survive
delayed-input lockstep.

**Addresses in this document are RVAs**, that is IDA VA minus 0x400000, which is what goes into
`loader\workshop\include\ffx\addresses\`. The other docs in this folder use VAs, so if you are
comparing a number against `MAIN_LOOP.md` or `CUTSCENE.md`, add 0x400000 first. Section 10 is the
full list, also as RVAs.

Blitzball and the Monster Arena are other agents' subjects and I left them alone. I tripped over
Blitzball twice and will say where. I also tripped over a battle overdrive minigame that is a
genuine wall-clock counterexample, and that is in section 9 because it is the one thing in this area
that does not replicate.

Everything below came out of the disassembly, out of the shipped `.ebp` bytecode read with
`tools/ebp.py`, or out of both. I have marked the handful of interpretations that are not proven.

## Summary, read this first

**Every one of the six replicates for free.** Not one of them reads a wall clock, not one of them
lives in a magic DLL, not one of them takes a port or device argument, and all six are ATEL event
script stepped from inside the `FFX_MainStep` sub-step loop. The work left for the mod is two shared
quantities and one input detail, listed under the table.

| Minigame | Package | Logic | Timer | RNG | Verdict |
|---|---|---|---|---|---|
| Chocobo race, Calm Lands trainer | `nagi0000` | pure ATEL | `WaitFrames(3)` per tenth of a second, 10/60 cascade, class-6 vars | stream 2, 50 draws | **replicates for free** |
| Lightning dodging, Thunder Plains | `kami0000`, `kami0100`, `kami0400` | pure ATEL | `WaitFrames(45)` dodge window, `WaitFrames(rand(32))` strike gap | stream 2, 12 draws in `kami0000` | **replicates for free** |
| Butterfly catching, Macalania | `mcfr0100` | pure ATEL | `WaitFrames(3)` per tenth, same cascade, class-3 vars | stream 2, 5 draws | **replicates for free** |
| Jecht Shot tutorial | `swin0000`, `swin0200` | pure ATEL | `var48 < 300` loop budget, one `WaitFrames(1)` per pass | stream 2, 11 draws | **replicates once the mod ships the raw analog BYTE** |
| Cactuar hunt, Bikanel | `bika0000` .. `bika0400` | pure ATEL | no clock at all | **no draws at all** | **replicates for free** |
| Via Purifico | `bvyt0500`, `bvyt0900` | pure ATEL | no clock at all | **no draws at all** | **replicates for free** |

Five things matter more than the rest.

1. **The only wait primitive a field script has is a step counter.** `FFX_AtelOp_WaitFrames_poll
   0x45C6B0` does `dec esi` once per poll and reports done at zero. Shipped scripts call its start
   handler 122,690 times across 391 of the 397 packages. There is no script op anywhere in the game
   that waits on milliseconds.
2. **ATEL advances once per SUB-step, not once per simulation step.** `FFX_Atel_StepOnce 0x48D3D0`
   is called at `0x42101A`, inside the loop that `FFX_Player__getSubStepCount 0x42D7E0` bounds. So
   `WaitFrames(3)` is three sub-steps. Both peers must produce the same sub-step count on the same
   step index or every minigame clock in the game runs at a different rate on each machine. This is
   the single shared quantity the whole area depends on. `CUTSCENE.md` says "one step per simulation
   step", which is the wrong granularity.
3. **I censused every blocking script op in the game.** 2,020 handler slots across the 11 live
   syscall tables, of which 211 have a non-null `poll`, which is the definition of a timer or a
   wait. Exactly one of those 211 can reach a wall clock, `core:600`, and it reaches it through
   dlmalloc's one-shot heap cookie, and no shipped package uses it. Details in section 1.4.
4. **Live minigame timers are NOT in the save block.** They are ATEL class 6 (the loaded `.ebp`
   image) or class 3 (the actor pool). A desync hash that covers only the 26,816 bytes at
   `g_ffxSaveData 0xD2CA90` cannot see a minigame clock drift. Section 2 has the exact variable
   descriptors, and section 10 has the save offsets the results land in.
5. **There is one script op that would be catastrophic and the shipped data never calls it.**
   `core:573` reaches `FFX_Rand_SeedAllStreams 0x398890` and re-seeds all 68 LCG streams from
   `GetSystemTime`. It appears 3 times in all 397 packages, in `test20` and `testbattle`, neither of
   which loads in a real playthrough. So the stream state at `0xD35EE0` is written once at process
   start and after that only advanced by draws. Sync it once at session start and script RNG is
   solved for the whole area.

What the mod still has to do:

- **Agree on `FFX_Player__getSubStepCount()` per step.** See point 2.
- **Sync the RNG stream state at `0xD35EE0` once at session start.** Stream 2 is the one the scripts
  draw from.
- **Ship the raw quantised analog byte, not a reconstructed float.** The Jecht Shot tutorial compares
  `FFX_Pad__readStagingAnalogByte(0, 0, 2)` and `(..., 3)` against the integer literals `64` and
  `190`. A float round trip can land on the other side of 64 for one peer. Section 6.
- **Hash three process-local debug flags at session join**, because two of them swap the bytecode
  package for two of these very minigames. `g_ffxChocoboGameDebugEnable 0x8CCAB8`,
  `g_ffxThunderPlainTreasureEnable 0x1685BA4`, `g_ffxFullNagi0700Enable 0x1685BA8`. Section 9.3.
- **Hash `byte_133C913 0xF3C913`.** It gates `core:465`, the second `WaitFrames` variant, and it is
  process-local `.data` outside the save block. Section 1.3.

## 1. Why all six are the same problem

### 1.1 The step clock, verified in the disassembly

`FFX_MainStep 0x420AE0` has a sub-step loop. Read with `idc.GetDisasm`, not the decompiler:

```
00420FFD  call FFX_Player__getSubStepCount     ; 0x42D7E0
00421010  call FFX_Player__stepControl         ; 0x42D180   <- loop head
00421015  call FFX_Btl_MainStep                ; 0x390C10
0042101A  call FFX_Atel_StepOnce               ; 0x48D3D0   <- the event script VM
0042101F  call FFX_MesWin_StepAll              ; 0x4AB910
0042103E  call FFX_Magic_CallOverlayStep       ; 0x387E00
0042104D  call FFX_Came_StepAll                ; 0x3BE110
0042107D  call FFX_Ch_UpdateMotionAll          ; 0x432E10
0042108F  call FFX_PlayerCam_Step              ; 0x43F2E0
00421095  call FFX_Player__getSubStepCount
0042109C  jl   loc_421010
```

Below `FFX_Atel_StepOnce` the chain is `FFX_Atel_StepFieldAll 0x472BD0` -> `FFX_Atel_StepFieldContexts
0x4666D0` -> `FFX_Atel_StepFrame 0x467950` -> `FFX_Atel_RunScript 0x4641E0` per actor. Nothing on
that path touches a clock.

So an ATEL "frame" is a simulation sub-step. One presented frame can be several of them. Every
number a script passes to `WaitFrames` is therefore a sub-step count, and the wall duration of a
scripted timer is `n / 29.97` seconds only when the sub-step count is 1.

### 1.2 `core:0`, the only wait a field script has

`FFX_AtelOp_WaitFrames_start 0x45C4E0` pops one int and stores it as the countdown. The poll is the
important half:

```
0045C6B3  cmp     g_ffxFmvScriptRunning, 0      ; 0xD2A008
0045C6C1  jz      short loc_45C6E0
0045C6C3  call    FFX_Fmv_CurrentMovieNeedsCameraRestore   ; 0x36EFC0
0045C6CA  jz      short loc_45C6E0
0045C6CC  call    FFX_Fmv_FramesSinceMark       ; 0x245D90
0045C6D1  sub     esi, eax                      ; <- the ONE non-step path
0045C6D3  mov     [edi], esi
...
0045C6E0  dec     esi                           ; <- the normal path
0045C6E1  mov     [edi], esi
0045C6E3  xor     eax, eax
0045C6E5  test    esi, esi
0045C6EA  setle   al
```

Normal path: one decrement per poll, done when it hits zero. No clock.

The FMV branch is the only exception in the whole game. When a movie is running and the current
movie needs a camera restore, the countdown is slaved to the video decoder's frame counter,
`FmvMgr[0x6D8] - FmvMgr[0x6E0]`. None of the six minigames runs an FMV while a clock is counting
(section 9.4 shows the FMV usage per package), so it cannot fire here, but it is a live hazard for
cutscenes and it belongs in `CUTSCENE.md` or `FMV_SYNC.md`.

`WaitFrames_start` also has a shipped fudge worth knowing about: if `sub_782970() == 0x610000` and
`MagicFile__getMagicId() == 0x290` and the requested count is exactly 10, the count is forced to 40.
It is deterministic, so it does not matter for lockstep, but it will confuse anyone counting frames
by hand in that one overlay.

### 1.3 `core:465`, the second `WaitFrames`, and the one global you have to hash

`FFX_AtelSys_Core_465_poll 0x45FF70`:

```
0045FF73  cmp     ds:byte_B5EC4F, 0     ; 0x75EC4F, const 1 in .rdata, a build switch
0045FF7A  jz      short loc_45FF96      ; -> return 5, done immediately
0045FF7C  cmp     byte_133C913, 0       ; 0xF3C913, .data
0045FF83  jnz     short loc_45FF96      ; -> return 5, done immediately
0045FF8A  dec     ecx                   ; otherwise count down like core:0
```

`byte_133C913` is the media layer's busy-or-absent flag. It is initialised to 0xFF in `.data` and
written by the disc and stream code (`libmscd`, `sceCdRead`, `sub_76D200`, `sub_76E1C0`,
`movie_str`), by `movie:143` and `movie:145`, by `hdd_install_check`, by the cutscene text engine
`tklib__f88D710`, and by `FFX_SaveFile_CommitLoad`. So `core:465` means "wait N sub-steps unless the
media layer says do not bother".

It is step-counted, but its gate is a process-local byte outside the save block, and the Chocobo
race is its heaviest user in the game at 166 calls. **Put `0xF3C913` in the desync hash.** Other
heavy users: `bvyt0500` (Via Purifico) 132, `kami0100` 86, `kami0000` 81.

### 1.4 The census: every blocking script op in the game

I walked all 11 live syscall tables straight out of `.rdata`, using the record counts from
`tools/ebp.py`'s `LIB_INFO`, 16 bytes per record as `{start, poll, resf, resi}`.

| Lib | Name | Table RVA | Records | Blocking (`poll != 0`) |
|---|---|---|---|---|
| 0 | core | 0x850050 | 616 | 39 |
| 1 | sys | 0x852BE0 | 30 | 0 |
| 4 | sg | 0x888D88 | 71 | 2 |
| 5 | ch | 0x8891F8 | 145 | 11 |
| 6 | came | 0x843998 | 138 | 7 |
| 7 | btl | 0x842628 | 235 | 25 |
| 8 | mapfx | 0x85DC90 | 108 | 3 |
| 9 | test | 0x85D8C0 | 1 | 0 |
| 11 | movie | 0x840E30 | 145 | 123 |
| 12 | save | 0x852DD8 | 94 | 1 |
| 13 | abmap | 0x885EB0 | 1 | 0 |
| | **total** | | **1,584 records, 2,020 handler slots** | **211** |

The library names are `tools/ebp.py`'s `LIB_NAMES`, which I checked against the handler names
already in the IDB and against the two leaked symbols in those tables (`sgMenuExec` at `sg:29` poll
and `chReadMotionGroupSync` at `ch:102` poll). They agree. Note that lib 6 is `came` and lib 7 is
`btl`, which is easy to get backwards, and lib 12 is `save` while lib 1 is `sys`.

Then I built the complete reverse-reachability closure from every wall-clock seed in the binary:
`QueryPerformanceCounter`, `GetTickCount`, `GetTickCount64`, `timeGetTime`, `GetSystemTime`,
`GetSystemTimeAsFileTime`, `GetTimeZoneInformation`, `_time64`, `clock`, `_difftime64`,
`_gmtime64`, `_localtime64`, `_mktime64`, both the `.idata` thunks and the in-`.text` jump thunks,
17 seeds in all. BFS up the call graph:

- **631 of 49,128 functions in `FFX.exe` can reach a wall clock at all.** 1.3 percent.
- **25 of the 2,020 ATEL handler slots are in that set.**
- **Exactly 1 of the 211 blocking ops is**, `core:600`, and it is spurious: the path runs
  `core:600 -> sub_8B0340 -> sub_782050 -> sub_782090 -> FFX_Btl_SetupFieldPointers ->
  maybe_FFX_Atel_ReleaseAllActorChrs -> FFX_Ch_Dispose -> FFX_Chr_FreeClonedClassCharacter ->
  sub_6EBEE0 -> sub_6EB3E0 -> sub_6EA050 -> GetTickCount`, which is dlmalloc `init_mparams`
  computing its heap magic cookie once at startup. And `core:600` is used by zero of the 397 shipped
  packages.

Of the other 24 dirty slots, 20 are the same allocator artefact, reached through the actor model
load and dispose paths or through the save-file ops (`core:1`, `core:2`, `core:5`, `core:6`,
`core:7`, `core:308`, `core:413`, `core:515`, `sg:30`..`sg:33`, `ch:61`, `ch:62`, `btl:164`,
`movie:9`, `save:3`, `save:25`, `save:36`, `save:49`). The four that are real:

- `core:573` resi, which reaches `FFX_Rand_SeedValueFromSystemClock 0x398950`. Only `test20` and
  `testbattle` call it, 3 times between them. See point 5 of the summary.
- The `movie:0`, `movie:10` and `movie:136` start handlers, which reach the video decode thread's
  `GetTickCount` through `FFX_Fmv_SetFullscreenMode 0x2427C0`. That is FMV, and it is
  `FMV_SYNC.md`'s problem, not a minigame timer. See 9.4 for which of my packages touch it.

**No field minigame in FFX reads a wall clock, and that is a census rather than a sample.**

### 1.5 The other waits the six minigames actually use

Across the six packages exactly three blocking ops appear:

| Op | Handler | What it waits on | Clock? |
|---|---|---|---|
| `core:0` | `0x45C4E0` / `0x45C6B0` | a sub-step countdown | no |
| `core:95` | `_poll 0x45C420` | forever, until another script releases it | no |
| `core:246` | `0x457DF0` / `0x457FF0` | until an actor's queued task list drains | no |

`core:246` is worth a line because it is the butterfly hunt's heaviest op at 434 calls. Its poll is a
pure predicate: `FFX_Atel_ActorHasTaskAtLevel 0x463520` walks the singly-linked list at `Actor+0x80`,
skips nodes whose byte at `+0x0F` is 3 (finished), and returns 1 if any remaining node has
`(node[0x0E] & 0x0F) >= level`. The task list is itself advanced once per sub-step by
`FFX_Atel_StepFrame`, so this is step-driven too.

Call counts, scanned over all 397 packages:

```
package              core:0  core:95  core:246  core:166  movie:0
nagi0000              1312     150        14        50       -
dbg_nagi0000          1312     150        14        50       -
full_nagi0700          578      19         2         2       -
kami0000               282      63         -        12       -
kami0100               240      25         -         1       -
kami0200                77      16        20         -       -
kami0300               382      84        29        26       -
kami0400                59      24         -         6       -
200thunder_kami0400     59      24         -         6       -
mcfr0100               359      54       434         5       -
mcfr0200                67      39         -         2       -
bika0000               181      39         -         -       -
bika0100               344      66         -         -       -
bika0200               311      69         -         -       -
bika0300               393      95         -         -       1
bika0400                79      28         -         -       -
swin0000               194      56         -        11       -
swin0200               520      49         4        11       -
bvyt0500               319      24         -         -       -
bvyt0900               227      72         -         -       -
```

## 2. Where minigame state lives

### 2.1 The variable descriptor, verified against the resolver

`FFX_Atel_ResolveVarAddress 0x46C2E0`:

```
0046C2E8  shr eax, 19h ; and eax, 7      ; class = (desc >> 25) & 7
0046C2F1  and edx, 0FFFFFFFh             ; strip the 4 type bits
...       and edx, 0F0FFFFFFh            ; EVERY case does this, so offset = desc & 0xFFFFFF
```

| Field | Bits |
|---|---|
| type | `desc >> 28`, 0 u8, 1 s8, 2 u16, 3 s16, 4 u32, 5 s32, 6 f32 |
| class | `(desc >> 25) & 7` |
| bit 24 | a separate flag, cleared by the resolver, **not part of the offset** |
| offset | `desc & 0xFFFFFF` |

Class bases, straight from the jump table at `0x46C300`:

| Class | Base | Lives in |
|---|---|---|
| 0 | `g_ffxAtelCtx[0x2C] + off` = `g_ffxSaveData + 0x1EC + off` | **the save block**, persistent |
| 1 | `g_ffxAtelCtx[0x30] + off` | - |
| 2 | `arg0[0][0x28] + arg0[4] + off` | the actor pool |
| 3 | `arg0[0][0x2C] + arg0[4] + off`, **unless** `g_ffxAtelCtx[0x54] != 0`, in which case it tail-calls `g_ffxAtelCtx[0x54](arg0, off)` | the actor pool, or wherever the hook says |
| 4 | `arg0[0][0x30] + arg0[4] + off` | the actor pool |
| 5 | `arg0 + 0x48 + off` | the actor's own register block |
| 6 | `arg0[4] + arg0[4][0x20] + off` | the loaded `.ebp` image |

Note the class-3 hook. The Macalania butterfly clock is class 3, so if anything ever installs
`g_ffxAtelCtx[0x54]` the mod has to account for it. In a normal field session it is null.

**`tools/ebp.py` has a bug here.** `cmd_vars` masks the offset with `0x1FFFFFF`, one bit too wide, so
it folds bit 24 into the offset. Proof: across all 397 packages there are 19,040 descriptors, 3,281
of them class 0, and bit 24 is set on 4,582 of the 19,040. With mask `0xFFFFFF` every single class-0
descriptor lands inside the 0x2000-byte ScriptWork region, maximum offset `0x1910`. With
`0x1FFFFFF`, 1,476 of them fall outside it. The resolver's own `and edx, 0F0FFFFFFh` settles it.
`WORLD_STATE.md` section 7 is fine, it quotes a span of `0x0000..0x1A00`, which matches the correct
mask.

### 2.2 So where is the clock

| Minigame | Live clock variable | Class | Where that is |
|---|---|---|---|
| Chocobo race | `nagi0000` var104/105/106 = tenths/seconds/minutes, u8 | 6 | `.ebp` image at atel offsets 0xB0/0xB1/0xB2 |
| Chocobo race | var147 finish flag, u8 | 6 | atel 0xDF |
| Butterflies | `mcfr0100` var50/var51, s32 | 3 | actor pool at 0x0/0x4 |
| Butterflies | var57 gauge, u8 | 3 | actor pool at 0x16 |
| Jecht Shot | `swin0000` var48 loop budget, s32 | 6 | atel 0x1C |
| Jecht Shot | var29 score, u8 | 6 | atel 0x2 |
| Lightning | `kami0000` var23 dodge window, u8 | 6 | atel 0x7 |
| Lightning | var31 current streak, u16 | 6 | atel 0x12 |

**Not one live timer is in the save block.** The persistent results are, and they are in section 10.
The practical consequence for the mod: a save-block hash is not a sufficient desync detector while a
minigame is running. If you want one you need the loaded `.ebp` image's class-6 region and the actor
pool as well, or you accept that a minigame drift shows up only when the result is written.

One more thing worth knowing before you read the per-minigame sections. The class-0 descriptors at
ScriptWork offsets `0xA00..0xAA5` and `0x1000..0x1910` are a **shared prelude every package
declares identically**. `nagi0000` and `swin0000` declare them with the same offsets and the same
array counts, so the only map-specific class-0 variables are the low-offset ones. Do not read a
match there as two minigames sharing state.

## 3. Chocobo racing, the Calm Lands trainer

**Package `nagi0000`.** Identified from the shipped US message table, which contains "Select
Training Course", "Wobbly chocobo", "Dodger chocobo", "Since you're riding a wild chocobo, it'll
just run whichever way it feels like", and the record readout "Your record is `{12}{35}:{12}{34}.{12}{33}`",
which is the message-field form for minutes, seconds and tenths.

### 3.1 Where the logic is and where it is stepped from

Pure ATEL bytecode. `nagi0000.ebp` carries 1,312 `WaitFrames` calls and 150 `WaitForever`. Stepped
from `FFX_Atel_StepOnce 0x48D3D0` at call site `0x42101A`, inside the `FFX_MainStep` sub-step loop.
There is no native chocobo-race module. The only native strings in the whole binary that mention a
chocobo are `/FFX_Data/GameData/PS3Data/flash/chocobo.swf`, the three Steam achievement ids
`ACH_CHOCOBO_LICENSE` / `ACH_CHOCOBO_RIDER` / `ACH_CHOCOBO_MASTER`, a loading-screen texture, and
the debug label `"Chocobo Game Debug Enable:"`. See 9.2 for the `.swf`.

### 3.2 The race clock, recovered from bytecode

`nagi0000` actor 82 entry 7, offsets are code-relative:

```
0281CE  var390 = 0
L1:
0281D4  core:158(win2, field0, 43, var104)   ; tenths digit into the on-screen window
0281E3  core:158(win2, field1, 44, var105)   ; seconds
0281F2  core:158(win2, field2, 44, var106)   ; minutes
028201  pushimm 3 ; syscall core:0            ; WaitFrames(3)
028207  var104 = var104 + 1
028211  if var104 == 10 { var104 = 0 ; var105 = var105 + 1
02822B     if var105 == 60 { var105 = 0 ; var106 = var106 + 1
028245        if var106 >= 2 { close windows 0..6 ; var147 = 1 ; ... } } }
```

So one tenth of a second is `WaitFrames(3)`, that is **three sub-steps per tenth**, with the
familiar 10 then 60 cascade. Two minutes is the hard stop.

Three sub-steps per tenth means the race clock reads 0.1 s for every 3 sub-steps regardless of the
real elapsed time. At one sub-step per presented frame and 29.97 Hz that is 100.1 ms, so the
displayed clock is correct to within a tenth of a percent. **It is a step counter dressed up as a
stopwatch**, which is exactly what lockstep wants.

### 3.3 RNG

Stream 2, 50 draws, all through `FFX_AtelSys_Core_166_resi 0x457400` (the bounded form). The
one-and-only draw family for scripts. `core:169` (raw) is unused here.

### 3.4 Input

No port or device argument anywhere. `core:76` / `core:77` (remapped) and `core:80` / `core:81`
(raw) all go through `FFX_Atel_GetPadPort0Held 0x46AF30` and `FFX_Atel_GetPadPressed 0x46AF70`,
which read the single globals `g_ffxAtelPadPort0Buttons 0xF270C0` and `g_ffxAtelPadPressed 0xF270D0`.
Port 0 is hardcoded. The mod has to decide which peer's bits land in those globals while the race is
running, which is an ownership question, not a determinism one.

### 3.5 What success writes

`nagi0000` actor 72 entry 12 copies the live class-6 clock into nine class-0 u8 at ScriptWork
`0xA8..0xB0`, three courses times (minutes, seconds, tenths):

```
pushvar 106 ; storevar 25     ; course 1 minutes  -> g_ffxSaveData + 0x294
pushvar 105 ; storevar 26     ; course 1 seconds  -> g_ffxSaveData + 0x295
pushvar 104 ; storevar 27     ; course 1 tenths   -> g_ffxSaveData + 0x296
pushvar 106 ; storevar 28     ; course 2          -> +0x297 / +0x298 / +0x299
...
pushvar 106 ; storevar 31     ; course 3          -> +0x29A / +0x29B / +0x29C
```

This block appears four times in the file at `0x21681`, `0x22758`, `0x2382F` and `0x24906`, one per
dialogue branch. All four write the same nine variables.

Those nine bytes sit inside `WORLD_STATE.md`'s **ScriptWork region** (`+0x01EC .. +0x21EC`), not in
any of `GAME_STATE.md`'s thirteen named regions, which start at `+0x3D0C`. So the Chocobo race
record is world state, and it is covered by whatever the mod already does for ScriptWork.

### 3.6 Verdict

**Replicates for free**, given a shared sub-step count and a synced stream-2 state. The race clock
is three sub-steps per tenth, the chocobo's behaviour is ATEL plus the normal character motion
pipeline, and nothing in the path reads a clock. One caveat that is not a determinism caveat: with
`g_ffxChocoboGameDebugEnable` set the game loads `dbg_nagi0000.ebp` instead, so hash that flag
(9.3).

## 4. Lightning dodging on the Thunder Plains

**Packages `kami0000` (the main plains, where the dodge happens), `kami0100`, `kami0200`,
`kami0300`, `kami0400` (the Rin travel agency, where the rewards are announced).** Identified from
the message tables: "you can dodge lightning by pressing `{0B}{31}`", "Hit `{0B}{31}` as soon as you
see a lightning flash", and in `kami0400` "Congratulations. You've dodged `{N}` lightning bolts in a
row".

### 4.1 Where the logic is

Pure ATEL. Stepped from `FFX_Atel_StepOnce` like everything else. There is no native lightning
module and no magic DLL involvement beyond the visual effect overlays, which are driven through
`mapfx` syscalls from the same scripts.

### 4.2 The strike scheduler

`kami0000` actor 31 entry 1:

```
009E64  pushimm 7 ; pushimm 8 ; syscallf core:166 ; gt       ; 7 > rand(8), so 7 chances in 8
009E6E  jz  -> skip
009E71  runscript2(actor 33, entry 6, 1)                     ; fire one pylon strike
009E7C  pushimm 32 ; syscallf core:166 ; syscall core:0       ; WaitFrames(rand(32))
```

A 7-in-8 chance of a strike, then a gap of 0 to 31 sub-steps. Both the coin flip and the gap are
stream-2 draws, so the whole schedule is a pure function of the stream state. The strike actors
32 to 36 entry 6 then call `mapfx:3`, `core:262`, `core:264`, `core:265` and `mapfx:4` for the flash
and the sound.

### 4.3 The dodge window

`kami0000` actor 30 entry 1 is the entire dodge mechanic:

```
if core:81(bit 5)        ; Circle was pressed this ATEL frame
{
    var23 = 45
    WaitFrames(45)
    var23 = 0
}
```

`var23` is class 6, u8, at atel offset `0x7`. So pressing Circle opens a **45 sub-step window** and
the scorer just checks whether the window is open when a bolt lands. Bit 5 is `0x20`, Circle, per
`INPUT_LAYER.md`.

### 4.4 Scoring and the reward tiers, recovered from bytecode

`kami0000` actor 12 entry 1. At a strike, `0x348E`:

```
var15 = sat(var15 + 1)                 ; every bolt SEEN, class 0 u16, g_ffxSaveData + 0x3FC
sg:3(12, 207, 207, 255)                ; the white screen flash
if (var24 < 12 && core:81(bit 5)) var23 = var23 + 1
```

Then at `0x3848` the scorer:

```
core:223()
if (var23 == 1)                        ; the dodge window was open
{
    var31 = sat(var31 + 1)             ; current consecutive streak, class 6 u16 at atel 0x12
    var16 = sat(var16 + 1)             ; bolts DODGED, class 0 u16, g_ffxSaveData + 0x3FE
    if (var31 > var17) { var17 = sat(var31) ; sel = var17 }   ; best streak, class 0 u16, +0x400
    switch (sel) {                     ; literals read straight out of the chain at 0x38F4
        case   5: var11[0] |= 0x01
        case  10: var11[0] |= 0x02
        case  20: var11[0] |= 0x04
        case  50: var11[0] |= 0x08
        case 100: var11[0] |= 0x10
        case 150: var11[0] |= 0x20
        case 200: var10[2] |= 0x02
        default:  sel = var15 ; switch (sel) { case 30: var11[0] |= 0x40
                                              case 80: var11[0] |= 0x80 }
    }
}
else                                   ; missed
{
    var31 = 0 ; core:577(0) ; runscript2(actor 0, entry 29, 1) ; core:577(1)
    sel = var15 ; switch (sel) { case 30: var11[0] |= 0x40 ; case 80: var11[0] |= 0x80 }
}
```

The 5 / 10 / 20 / 50 / 100 / 150 / 200 tier set is exactly the published Thunder Plains chest
requirement, and it keys off `var17`, the **best consecutive streak**, not the total. The separate
30 / 80 chain keys off `var15`, the count of bolts seen, and is tested on both the hit and the miss
path.

### 4.5 Where the results land

All class 0, so all inside `WORLD_STATE.md`'s ScriptWork region:

| Variable | Type | ScriptWork off | Save off | Meaning |
|---|---|---|---|---|
| var10 | u8[3] | 0x205 | `g_ffxSaveData + 0x3F1` | index 2 bit `0x02` is the 200-dodge reward flag |
| var11 | u8 | 0x208 | `+ 0x3F4` | reward bits for the 5/10/20/50/100/150 tiers and the 30/80 ones |
| var15 | u16 | 0x210 | `+ 0x3FC` | bolts seen |
| var16 | u16 | 0x212 | `+ 0x3FE` | bolts dodged |
| var17 | u16 | 0x214 | `+ 0x400` | best consecutive streak |

### 4.6 Verdict

**Replicates for free.** The schedule is stream-2 draws, the window is a 45 sub-step counter, the
trigger is one button bit, and the result is five save-resident class-0 variables. The only thing to
watch is that `g_ffxThunderPlainTreasureEnable 0x1685BA4` swaps `kami0400.ebp` for
`200thunder_kami0400.ebp`, so hash it (9.3).

An ownership question the mod has to answer, not a determinism one: a single `core:81(bit 5)` read
opens the window, and both peers' Circle presses arrive on the same input channel. Decide whether
either player's press counts as the dodge or whether only the party leader's does, then make the
decision in code rather than letting whoever's bits reach `g_ffxAtelPadPressed` win.

## 5. Butterfly catching in Macalania

**Package `mcfr0100`.** Message table: "Butterfly Hunt", "Approach the butterfly of many hues",
"Try to catch seven of the blue butterflies, but beware the reds, for they call pain", "Butterflies
disappear after a certain time".

### 5.1 The clock

`mcfr0100` actor 37 holds three entries that are the whole timer:

- entry 6: `var50 = 9`, `var51 = 39` -> 40 seconds (the easier course)
- entry 7: `var50 = 9`, `var51 = 29` -> 30 seconds (the harder one)
- entry 8: the countdown, `WaitFrames(3)` per tenth with the same 10 then 60 cascade as the Chocobo
  race

`var50` and `var51` are class 3, `s32`, at actor offsets `0x0` and `0x4`. Same structure as the race
clock, same three sub-steps per tenth, different storage class.

### 5.2 The game body

Actor 38 entry 9 is 44,436 bytes of bytecode, by far the largest entry in the package. It opens:

```
007CCD  core:379(0) ... core:379(28)         ; 29 calls, one per butterfly actor
007D7B  core:94 (FFX_AtelOp_PlayerControlOff)
007D7E  var57 = 128
007D84  core:219(661)
007D8A  if (var57 >= 89) { ... }
```

`var57` is class 3, `u8`, at actor offset `0x16`, and it is moved by `+= 3` at 14 sites and `-= 3` at
14 more, which lines up with 14 blue and 14 red butterflies. **I could not settle what the 128 and
89 mean.** The obvious reading is a gauge where blues add and reds subtract and you need to finish
at or above 89, but I did not trace enough of the 44 KB body to prove it. It does not change the
co-op verdict: it is a plain integer moved by script with no clock anywhere near it.

### 5.3 `core:246`, the heaviest op in the package

434 calls, the most of any package in the game. It waits for an actor's task queue to drain, not for
time. Section 1.5 has the mechanism. This is how the butterfly flight paths are sequenced.

### 5.4 Where the reward lands

`mcfr0100` declares **no class-0 variables of its own**, so there is no ScriptWork byte for
"butterflies caught". The reward goes straight into the equipment table instead, through `core:533`:

`FFX_AtelSys_Core_533_resi 0x45D170` -> `FFX_Equip_SetAbilitySlotsFromTable 0x4C3170`, which switches
on a kind 0..17, picks an 8-byte row out of the eighteen tables based at `off_C86D00` through
`off_C86E10` (RVA 0x886D00 .. 0x886E10), writes four `u16` into an equipment record at `+0x0E`..`+0x15` (which `GAME_STATE.md`
documents as the auto-ability list), and calls `FFX_SaveData_RecomputeAllCharDerived 0x386900`.

All 266 `core:533` calls in `mcfr0100` are in actor 38 entry 9. That is the butterfly reward table,
expressed as a long branch chain in script.

So the butterfly result lands in `GAME_STATE.md`'s **equipment slot table**, not in ScriptWork. That
is a region the mod almost certainly already syncs for other reasons.

### 5.5 Verdict

**Replicates for free.** The clock is three sub-steps per tenth, the sequencing is task-queue waits,
the only randomness is five stream-2 draws, and the reward is an ordinary equipment grant.

## 6. The Jecht Shot tutorial

**Packages `swin0000` and `swin0200`.** Message table: "The Jecht Shot Challenge", "Did you get the
hang of it? Now for the real thing. Show Jecht what you can do." `swin0200` carries the same two
strings, so the tutorial exists in both.

### 6.1 Structure

`swin0000` actor 32 entry 8:

```
005ED1  var32 = 0 ; var34 = 0
005EDD  var46 = rand(5)                   ; core:166(5), which of five prompts to show
005EE6  var47 = rand(4)
005EEF  core:220()
005EF2  core:378(9)
005EF8  while (var48 < 300) {
            if (var30 == 0) { sel = var46 ; switch -> show prompt 0..4, var30 = 1,
                                                      var48++, WaitFrames(1) }
            ... the input test, below ...
        }
```

`var48` is class 6, `s32`, at atel offset `0x1C`. It is incremented once per loop pass and every
pass runs at least one `WaitFrames(1)`, so **`var48 < 300` is a 300 sub-step budget**, about ten
seconds at one sub-step per frame. `var29` (class 6, u8, atel `0x2`) is the score.

### 6.2 The input test, which is the one real co-op condition in this document

Two accepted ways to score, read at `0x602E`:

```
00602E  pushimm 12 ; syscallf core:80         ; hold L1
006034  pushimm 15 ; syscallf core:80 ; land  ; hold R1
00603B  pushimm  5 ; syscallf core:81 ; land  ; press Circle
006042  pushimm  2 ; syscallf core:290 ; pushimm  64 ; lt      ; analog byte 2 < 64
00604C  pushimm  3 ; syscallf core:290 ; pushimm  64 ; lt ; land ; analog byte 3 < 64
006057  pushimm  5 ; syscallf core:81 ; land
00605E  lor
00605F  jz  -> no score
```

and the next prompt, at `0x60C3`, uses a different region:

```
0060C3  core:80(12) && !core:80(13) && !core:80(15) && core:81(5)
0060E0  || ( core:290(2) > 64 && core:290(2) < 190 && core:290(3) < 64 && core:81(5) )
```

`core:290` is `FFX_AtelSys_Core_290_resi 0x45D310`, which goes straight to
`FFX_Pad__readStagingAnalogByte(0, 0, n) 0x488C00`, that is the **raw 0..255 staging byte**. Indices
0 and 1 are the left stick, which the input layer also uses to synthesize the digital pad bits
(`INPUT_LAYER.md`: `LX < 0x10` sets Left, `LX > 0xF0` sets Right, `LY < 0x10` sets Up,
`LY > 0xF0` sets Down). Indices 2 and 3 are the other pair.

**The script compares that byte against the integer literals 64 and 190.** If the mod reconstructs
the remote analog value from a float, a value that quantises to 63 on one machine and 64 on the
other produces a hit on one peer and a miss on the other, from identical player intent. This is the
same quantisation trap `lockstep-shape` already warns about, and the Jecht Shot tutorial is where it
actually bites a minigame.

### 6.3 Where the result lands

`swin0000`'s own class-0 variables are var26 (`u8[2]` at ScriptWork `0x201` = `g_ffxSaveData +
0x3ED`), var27 (`u8[2]` at `0x203` = `+ 0x3EF`) and var28 (`u8` at `0x20D` = `+ 0x3F9`). All inside
ScriptWork. I did not pin which of the three is the "learned the Jecht Shot" flag, because the write
is buried in the same branch chain as the dialogue, and the answer does not change the verdict.
Worth noting the Jecht Shot overdrive itself is a Blitzball ability, which is the other agent's
subject.

### 6.4 Verdict

**Replicates once the mod ships the raw quantised analog byte.** Everything else about it is already
deterministic: a 300 sub-step budget, eleven stream-2 draws, plain button bits for the L1/R1/Circle
branch. The analog branch is the condition to get right.

## 7. The Cactuar hunt

**Packages `bika0000`, `bika0100`, `bika0200`, `bika0300`, `bika0400`.** Message tables: "Village of
the Cactuars", "Way of the Gatekeepers III", "Sneak up on the cactuars when their backs are turned",
"Fail to catch the gatekeeper unaware, and you'll obtain...", "Writing appears when the sphere is
set into the stone".

### 7.1 There is no clock

I scanned all 397 packages for the 10-then-60 tenths-and-seconds cascade that the Chocobo race and
the butterfly hunt both use. It appears in `nagi0000`, in the `bltz*` Blitzball packages (not my
subject) and in `lmyt0000`. **It does not appear in any `bika` package.** The Cactuar hunt is not
timed. It is a find-and-approach game with a success condition on the cactuar's facing, which is why
the hint text is about backs being turned rather than about speed.

The only waits in the `bika` packages are `core:0` and `core:95`, and the `core:0` counts are
ordinary cutscene pacing (181, 344, 311, 393, 79).

### 7.2 There is no randomness either

**`core:166` and `core:169` call counts across all five `bika` packages: zero.** The hunt draws no
random numbers at all. Cactuar placement and behaviour are fixed in the map data and the script.

### 7.3 Input and results

Ordinary field input, no port argument, no analog comparison. The hunt's state is the ten Cactuar
stones, which are class-0 ScriptWork flags like any other switch or chest, so they are covered by
whatever the mod already does for `WORLD_STATE.md`'s world-state range. I did not enumerate which
ScriptWork bytes they are, because the general ScriptWork sync covers them without needing to know.

### 7.4 Verdict

**Replicates for free, and it is the easiest of the six.** No clock, no RNG, no analog comparison.
The only co-op consideration is the ordinary one: both players walking around the same map, and the
single-slot player position cache that `INTERACTION_PATH.md` section 3.2 and `RANDOM_ENCOUNTER.md`
section 3 are both about. If a cactuar's "is the player behind me" check reads that cache then it
sees only one character, which is a gameplay-fairness issue rather than a desync. **Untested, see
section 11.**

## 8. The Via Purifico

**Packages `bvyt0500` and `bvyt0900`.** Message tables: "No one thrown into the Via Purifico has
ever survived" and, in `bvyt0900`, "To the Condemned: Stone panels are scattered throughout the Via
Purifico".

### 8.1 No clock, no RNG

Same two scans as the Cactuar hunt, same two answers. No 10/60 cascade in either package, and
`core:166` / `core:169` counts are zero in both. The Via Purifico is a maze with stone panels, not a
timed trial. The received idea that it is timed probably comes from the underwater section's oxygen
pressure, which is presentation, not a script timer.

`bvyt0500` is however the second heaviest user of `core:465` in the game at 132 calls, behind only
`nagi0000` at 166. So the one global in 1.3 that has to be hashed matters here too.

### 8.2 Verdict

**Replicates for free.** The co-op question for the Via Purifico is not determinism, it is the
split-party structure (Yuna's group underwater and Tidus's group on foot are different maps with
different parties), which is `WORLD_STATE.md` and `PLACEMENT.md` territory, not mine.

## 9. Other things I checked

### 9.1 The magic DLLs: no minigame lives in one

The lead was worth checking and the answer is clean. `g_ffxMagicHostApiTable 0x864CE8` is handed
to the overlay DLLs by `MagicFile__start 0x5DA7F0`.

**Correction, 2026-10-04: the table is 947 slots, not 741.** Read directly out of the IDB, indices
0 through 946 hold **913 function starts, 33 pointers into `.data`, and one NULL**. The 741 figure
has an obvious cause: slot 741 is exactly where the run of function pointers stops and the engine
globals begin (`unk_230FFE0`), so the scan that produced it walked until the first non-function and
called that the end. From 741 up it is mixed, and there are still 172 function entries above index
740. Slot 936 is `g_ffxIsCatchUpStep`, slot 764 is `g_ffxMainStepCounter`, slot 743 is
`g_ffxEffectUnitPtrs`, and `MAGIC_DLL.md` has the full picture.

- **4 host entries are in the clock-reachable set** (entries 0, 24, 384 and 650), all four reaching
  it through the allocator path described in 1.4, and 17 are RNG-reachable, which matters for effect
  determinism but not for timers. **Both counts were taken over entries 0..740 only**, so treat them
  as lower bounds until somebody re-runs the reachability over all 947. The conclusion they support
  is unaffected: no minigame timer lives behind this table.
- The DLLs themselves are 581 `magic_NNNN.dll` files next to the exe. Each exports exactly two
  symbols, `GetEffectOverlayTable` and `InitMagicPRX`, and their only `kernel32` imports are the
  MSVC `__security_init_cookie` set. **None of them can read a gameplay clock.**
- `FFX_Magic_CallOverlayStep 0x387E00` is called from `FFX_MainStep` at `0x42103E`, inside the
  sub-step loop, so the overlays are stepped on the simulation clock like everything else.

So the magic DLLs are particle and effect overlays. No minigame logic is in one. The Thunder Plains
lightning visuals are driven from the `mapfx` syscalls in `kami0000`'s own script, not from a DLL.

### 9.2 `chocobo.swf` is presentation only

`/FFX_Data/GameData/PS3Data/flash/chocobo.swf` is loaded by a two-instruction thunk at `0x243DE0`
(`mov ecx, g_ffxGfxCtx` then `jmp 0x26F480`) whose body is at `0x26F480`. IDA assigns both of those
as far-flung tail chunks of `FFX_Atel_PullActorPosFromChr 0x469E40`, which tail-jumps to the thunk at
`0x469F69`. The body, read out of the disassembly:

```
0026F484  mov     esi, [edi+10DDCh]       ; the previously loaded movie, if any
0026F490  call    sub_A28A40 ; FFX_Free   ; release it
0026F49E  cmp     dword_1984C6C, 0        ; RVA 0x1584C6C, the Iggy enable gate
0026F4A5  jz      short loc_26F4C8
0026F4A7  push 32h ; push 32h ; push 294h ; push 4C4h   ; y=50, x=50, h=660, w=1220
0026F4B5  push    offset "/FFX_Data/GameData/PS3Data/flash/chocobo.swf"
0026F4BA  call    sub_6E9250
0026F4C2  mov     [edi+10DDCh], eax
```

So `g_ffxGfxCtx 0x8CB9D8` holds the movie at `+0x10DDC`, and the load is gated on
`dword_1984C6C 0x1584C6C`.

It holds the race HUD, which is a presentation layer over the class-6 clock variables the script
already owns. It computes nothing. The mod does not have to sync it, and if it diverges visually the
simulation is still identical.

### 9.3 Three debug flags that swap minigame bytecode, and two of them are mine

This is the one genuine hazard I found that is not about clocks. `FFX_Asset_ResolvePathWithDebugOverrides
0x642C00` resolves every asset path into the 255-byte buffer `g_ffxResolvedAssetPathBuf 0x1685C30`,
expanding `$USER$` and a 16-entry substitution table (keys `0x1685BB0`, values `0x1685BF0`), and then
applies three package swaps. All three verified in the disassembly:

| Site | Flag | Swap |
|---|---|---|
| `0x642D7E` | `g_ffxChocoboGameDebugEnable 0x8CCAB8` via `FFX_Debug_IsChocoboGameDebugEnabled 0x2BC960` | `nagi0000.ebp` -> `dbg_nagi0000.ebp` |
| `0x642DFB` | `g_ffxThunderPlainTreasureEnable 0x1685BA4` | `kami0400.ebp` -> `200thunder_kami0400.ebp` |
| `0x642E6C` | `g_ffxFullNagi0700Enable 0x1685BA8` | `nagi0700.ebp` -> `full_nagi0700.ebp` |

`nagi0000` is the Chocobo race. `kami0400` is the Thunder Plains reward map. All three replacement
packages ship in `FFX_Data.vbf`, so the swap works in a retail build. The debug labels
`"Chocobo Game Debug Enable:"` and `"Thunder Plain Treasure Enable:"` are built by
`FFX_DebugPage_gameParams_initLabels 0x2B9CD0` and `FFX_DebugPage_basicInfo_initLabels 0x2B9360`, and
`sub_6B6790` does a `cmp` / `setz` pair on the chocobo byte at `0x2B67EC`, so it is runtime
togglable.

All three flags are process-local and outside the save block. **If one peer has a flag set and the
other does not, they load different bytecode for the same map id and desync at the script level with
no save-block difference to detect it.** Hash them at session join. They are cheap: two dwords and a
byte.

As a bonus, `dbg_nagi0000` is byte-for-byte identical to `nagi0000` in syscall usage (1312 / 150 /
14 / 50 across `core:0` / `core:95` / `core:246` / `core:166`), so the debug build differs only in
data, not in control flow. `200thunder_kami0400` is likewise identical to `kami0400` in op counts.

### 9.4 FMV inside a minigame map

Only two of the twenty packages I looked at call `movie:0` (the FMV fullscreen start) at all:
`bika0300` once and `lchb1200` once. Neither is inside a timed sequence, both are story movies. So
the `WaitFrames` FMV branch from 1.2 cannot fire in any of the six minigames.

### 9.5 A battle minigame that genuinely does NOT replicate

Worth recording here because the contrast makes the field answer easier to trust, and because it
corrects an existing note. `FFX_BtlOd_LuluFuryStickMinigame 0x491B80` is the rotate-the-stick
overdrive, reached from `FFX_BtlMenu_Step 0x49AE20` at `0x49AE4D` and `0x49AE6A`. It reads the wall
clock through `FFX_Time_AppElapsedSeconds 0x241410` at `0x491BB6` and `0x491BD6`.

**Correction, 2026-10-04.** This section used to say the time limit `flt_133F790` is measured in real
seconds, so the number of Fury casts depended on how long the overdrive took in wall time. Wrong.
`flt_133F790` is `g_ffxBtlOdTimeRemaining`, and the only function that advances it is
`FFX_BtlOd_StepSharedTimer 0x491AC0`, which adds exactly 1.0 to `g_ffxBtlOdStepCounter 0x133F788` per
sub step and computes `remaining = budget - counter / fps`. The time limit is step derived and it
replicates. Every other writer of that float is a launcher or a reset seeding it from
`g_ffxBtlOdTimeBudget 0x133F78C`.

What actually fails to replicate is one branch, and it is the keyboard path. At `0x491DA3`:

    fld   flt_B5EDC8        ; 0.5001
    fcomp [ebp+var_1C]      ; now - mark, both from FFX_Time_AppElapsedSeconds
    test  ah, 5
    jp    loc_491D3D        ; not half a real second yet, leave the latches alone
    fld   g_ffxBtlOdLuluClockNow
    mov   g_ffxBtlOdLuluKeyA, 0
    fstp  g_ffxBtlOdLuluClockMark
    mov   g_ffxBtlOdLuluKeyB, 0

After half a real second with the stick outside all four corner quadrants, the two keyboard key
latches clear and the mark moves forward. That is the entire wall-clock surface of this minigame. It
does not touch the quadrant ring at `byte_133D721` and it does not touch the time limit.

For co-op, a keyboard player's latches clear on a different step on each machine. Patching the two
call sites at `0x491BB6` and `0x491BD6` fixes it. Both call `FFX_Time_AppElapsedSeconds`, the same
function the battle menu page lockout calls, so the kit's existing `AppElapsedHook` in
`ffx/StepClock.h` already backs them and they are two more entries in its site table.

The IDB note on that function used to say it was the Blitzball setup with nine wall-clock reads
through `FFX_Input__getTimeSeconds 0x630C40`. All three claims are wrong and all three had already
propagated into `MAIN_LOOP.md` and `COOP_DESIGN.md`. The correction is in the IDB function comment
with the local evidence (the `blulu` / `blulu2` / `bgoodl` sound cues, the quadrant codes 4..7 from
`FFX_Pad__readAnalogByte(0,0,0/1)` with 0x60 and 0xA0 thresholds, the ring at `byte_133D721`, the
run-of-five match against `0x07060504` and `0x04050607`, the 16-cap at `byte_133D71F`). I am
confident about the two clock sites and the two rotation constants, because I read them in the
disassembly. **I am calling the "it is Lulu's Fury specifically" attribution probable rather than
proven**, because the only naming evidence is the sound-cue strings.

`FFX_BtlMenu_Step`'s own widget array at `0xF3C950` (stride 240, 8 slots) carries a `startTime` at
`+0xDC` and an `elapsed` at `+0xE0` that are wall-clock seconds, so the battle menu is a different
animal from the field in general. None of my six minigames comes through it.

### 9.6 Blitzball and the Monster Arena

Skipped as instructed. I tripped over Blitzball twice and both times backed out:

- `bltz0000.ebp` and `bltz0002.ebp` are the two largest packages in the game, 5.42 MB each, and
  they are among the three packages (with `nagi0000` and `lmyt0000`) that use the 10/60 tenths
  cascade. Someone else's problem.
- The old IDB note attributing the stick-rotation minigame to Blitzball (9.5) was the second one. I
  corrected it in the IDB rather than leaving it, because it was going to mislead the Blitzball
  agent as much as me.

I did not look at the Monster Arena at all.

## 10. Every address I derived, as RVAs

RVA = IDA VA minus 0x400000. Types: `fn` function, `glob` global, `tbl` table, `off` offset inside
`g_ffxSaveData 0xD2CA90`.

### The step clock and the ATEL VM

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x420AE0 | fn | `FFX_MainStep` | the simulation step, with the sub-step loop at `0x421010`..`0x42109C` |
| 0x42D7E0 | fn | `FFX_Player__getSubStepCount` | **the scale factor on every scripted timer**, read at `0x420FFD` and `0x421095` |
| 0x42D180 | fn | `FFX_Player__stepControl` | sub-step loop head |
| 0x48D3D0 | fn | `FFX_Atel_StepOnce` | the event-script VM, called at `0x42101A`, once per SUB-step |
| 0x472BD0 | fn | `FFX_Atel_StepFieldAll` | field step body |
| 0x4666D0 | fn | `FFX_Atel_StepFieldContexts` | runs contexts 0..6 |
| 0x467950 | fn | `FFX_Atel_StepFrame` | one ATEL frame, advances the actor task lists |
| 0x4641E0 | fn | `FFX_Atel_RunScript` | per-actor bytecode interpreter |
| 0x422840 | fn | `FFX_MainStepLoop` | caller of `FFX_MainStep` |
| 0x421E80 | fn | `FFX_StepPacing` | paces against a millisecond clock, outside the sim |
| 0x387E00 | fn | `FFX_Magic_CallOverlayStep` | called at `0x42103E`, so overlays are on the sim clock |
| 0x390C10 | fn | `FFX_Btl_MainStep` | called at `0x421015` |

### The wait and timer primitives

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x45C4E0 | fn | `FFX_AtelOp_WaitFrames_start` | `core:0` start. 122,690 shipped call sites in 391 packages |
| 0x45C6B0 | fn | `FFX_AtelOp_WaitFrames_poll` | `core:0` poll. `dec esi` per poll. The FMV branch at `0x45C6C3` is the one non-step path in the game |
| 0x45C420 | fn | `FFX_AtelOp_WaitForever_poll` | `core:95`, released by another script |
| 0x45FF70 | fn | `FFX_AtelSys_Core_465_poll` | second `WaitFrames`, gated on `byte_B5EC4F` and `byte_133C913` |
| 0x457DF0 | fn | `FFX_AtelSys_Core_246_start` | wait for an actor task queue to drain |
| 0x457FF0 | fn | `FFX_AtelSys_Core_246_poll` | predicate only, no counter |
| 0x463520 | fn | `FFX_Atel_ActorHasTaskAtLevel` | walks `Actor+0x80`, `node[0x0F]==3` means done, `node[0x0E]&0x0F` is the level |
| 0x458DA0 | fn | `FFX_AtelSys_Core_600_poll` | the only blocking op reaching a clock, spuriously, and used by zero packages |
| 0x245D90 | fn | `FFX_Fmv_FramesSinceMark` | `FmvMgr[0x6D8] - FmvMgr[0x6E0]`, sole caller is the WaitFrames FMV branch |
| 0x36EFC0 | fn | `FFX_Fmv_CurrentMovieNeedsCameraRestore` | gates that branch |
| 0xD2A008 | glob | `g_ffxFmvScriptRunning` | the other gate on that branch |
| 0x75EC4F | glob | `byte_B5EC4F` | const 1 in `.rdata`, build switch, also read 3x in `FFX_Btl_MainStep` |
| 0xF3C913 | glob | `byte_133C913` | media-busy flag, gates `core:465`, **hash this**, init 0xFF |

### The syscall tables

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x850050 | tbl | ATEL lib 0 `core` | 616 records x 16 bytes `{start, poll, resf, resi}` |
| 0x852BE0 | tbl | ATEL lib 1 `save` | 30 |
| 0x888D88 | tbl | ATEL lib 4 `ch` | 71 |
| 0x8891F8 | tbl | ATEL lib 5 `sg` | 145 |
| 0x843998 | tbl | ATEL lib 6 `btl` | 138 |
| 0x842628 | tbl | ATEL lib 7 `came` | 235 |
| 0x85DC90 | tbl | ATEL lib 8 `mapfx` | 108 |
| 0x85D8C0 | tbl | ATEL lib 9 | 1 |
| 0x840E30 | tbl | ATEL lib 11 `movie` | 145 |
| 0x852DD8 | tbl | ATEL lib 12 `sys` | 94 |
| 0x885EB0 | tbl | ATEL lib 13 | 1 |

### Variables and storage

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x46C2E0 | fn | `FFX_Atel_ResolveVarAddress` | class = `(desc>>25)&7`, offset = `desc & 0xFFFFFF`, bit 24 is a flag |
| 0xF26B28 | glob | `g_ffxAtelCtx` | `+0x2C` is the class-0 base, `+0x54` is the class-3 resolver hook |
| 0xD2CA90 | glob | `g_ffxSaveData` | the 26,816-byte block |
| 0x46D4E0 | fn | `FFX_SaveData_ClearScriptWorkArea` | `memset(save+0x1EC, 0, 4096)`, first half only |
| 0x47E720 | fn | `FFX_SaveData_BackupScriptWorkArea` | 4 KB push to `g_ffxScriptWorkAreaBackup` |
| 0x47E6F0 | fn | `FFX_SaveData_RestoreScriptWorkArea` | 4 KB pop |
| 0xF2D570 | glob | `g_ffxScriptWorkAreaBackup` | 4 KB shadow of ScriptWork, outside the save block |

### RNG

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x3988F0 | fn | `FFX_Rand_Stream` | the 68-stream LCG. Scripts use stream 2 |
| 0x457400 | fn | `FFX_AtelSys_Core_166_resi` | `core:166`, bounded draw, stream 2. The one the minigames use |
| 0x457680 | fn | `FFX_AtelSys_Core_169_resi` | `core:169`, raw draw, stream 2. Unused by the six |
| 0xD35EE0 | glob | `g_ffxRandStreamState` | **sync this once at session start** |
| 0x398890 | fn | `FFX_Rand_SeedAllStreams` | reseeds all 68 |
| 0x398950 | fn | `FFX_Rand_SeedValueFromSystemClock` | the only reseed source, `GetSystemTime` |
| 0x22F5B0 | fn | `FFX_Time_ReadSystemTimeFields` | the clock read behind it |
| 0x456520 | fn | `FFX_AtelSys_Core_573_resi` | `core:573` -> `FFX_InitNewSaveData` -> reseed. **Only `test20` and `testbattle` call it** |
| 0x386B00 | fn | `FFX_InitNewSaveData` | |

### Input

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x45EC60 | fn | `FFX_AtelSys_Core_080_resi` | `core:80`, raw HELD bit. Jecht Shot uses bits 12, 13, 15 |
| 0x45F0E0 | fn | `FFX_AtelSys_Core_081_resi` | `core:81`, raw PRESSED bit. Bit 5 (0x20, Circle) is the whole lightning dodge |
| 0x45E130 | fn | `FFX_AtelSys_Core_076_resi` | `core:76`, remapped held |
| 0x45E820 | fn | `FFX_AtelSys_Core_077_resi` | `core:77`, remapped pressed |
| 0x45D310 | fn | `FFX_AtelSys_Core_290_resi` | `core:290`, **the raw analog byte**. Compared against 64 and 190 |
| 0x488C00 | fn | `FFX_Pad__readStagingAnalogByte` | `(0, 0, n)`, port 0 hardcoded |
| 0x46AF30 | fn | `FFX_Atel_GetPadPort0Held` | reads `g_ffxAtelPadPort0Buttons` |
| 0x46AF70 | fn | `FFX_Atel_GetPadPressed` | reads `g_ffxAtelPadPressed` |
| 0xF270C0 | glob | `g_ffxAtelPadPort0Buttons` | one slot |
| 0xF270D0 | glob | `g_ffxAtelPadPressed` | one slot |
| 0x45D9B0 | fn | `FFX_Atel_GetPadPressedRemapped` | |

### Message windows, used by all three clock displays

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x455C70 | fn | `FFX_AtelSys_Core_158_resi` | `core:158`, write a number into a message field. All three clocks display through it |
| 0x457870 | fn | `FFX_AtelOp_MesWinSetMessage` | `core:100` |
| 0x4580C0 | fn | `FFX_AtelSys_Core_101_resi` | `core:101` |
| 0x458360 | fn | `FFX_AtelSys_Core_102_resi` | `core:102` |
| 0x458B50 | fn | `FFX_AtelOp_MesWinShow` | `core:106` |
| 0x459060 | fn | `FFX_AtelOp_MesWinClose` | `core:107` |
| 0x4584D0 | fn | `FFX_AtelSys_Core_157_resi` | `core:157` |
| 0x4AB910 | fn | `FFX_MesWin_StepAll` | called at `0x42101F` |

### Chocobo race

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x45C100 | fn | `FFX_AtelSys_Core_527_resi` | `core:527`, used by `nagi0000` only, 12 calls |
| 0x8CCAB8 | glob | `g_ffxChocoboGameDebugEnable` | **hash this**, swaps `nagi0000.ebp` for `dbg_nagi0000.ebp` |
| 0x2BC960 | fn | `FFX_Debug_IsChocoboGameDebugEnabled` | reads it |
| 0x2B9CD0 | fn | `FFX_DebugPage_gameParams_initLabels` | builds the `"Chocobo Game Debug Enable:"` label |
| 0x243DE0 | fn | Iggy `chocobo.swf` loader thunk | body at `0x26F480`, movie stored at `g_ffxGfxCtx + 0x10DDC` |
| 0x469E40 | fn | `FFX_Atel_PullActorPosFromChr` | tail-jumps to the loader at `0x469F69`. IDA owns both chunks |
| 0x8CB9D8 | glob | `g_ffxGfxCtx` | `+0x10DDC` is the loaded `chocobo.swf` |
| 0x1584C6C | glob | `dword_1984C6C` | Iggy enable gate on the `chocobo.swf` load |
| +0x294 | off | course 1 best time | three u8: minutes, seconds, tenths. ScriptWork 0xA8 |
| +0x297 | off | course 2 best time | ScriptWork 0xAB |
| +0x29A | off | course 3 best time | ScriptWork 0xAE |

### Lightning dodging

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x1685BA4 | glob | `g_ffxThunderPlainTreasureEnable` | **hash this**, swaps `kami0400.ebp` for `200thunder_kami0400.ebp` |
| 0x2B9360 | fn | `FFX_DebugPage_basicInfo_initLabels` | builds the `"Thunder Plain Treasure Enable:"` label |
| +0x3F1 | off | `kami0000` var10, u8[3] | index 2 bit 0x02 = 200-dodge reward claimed. ScriptWork 0x205 |
| +0x3F4 | off | `kami0000` var11, u8 | reward bits 0x01/0x02/0x04/0x08/0x10/0x20 for 5/10/20/50/100/150, 0x40/0x80 for 30/80 |
| +0x3FC | off | `kami0000` var15, u16 | lightning bolts SEEN. ScriptWork 0x210 |
| +0x3FE | off | `kami0000` var16, u16 | bolts DODGED. ScriptWork 0x212 |
| +0x400 | off | `kami0000` var17, u16 | best consecutive streak, the value the tier switch reads |

### Butterflies

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x45D170 | fn | `FFX_AtelSys_Core_533_resi` | `core:533`, grant equipment with fixed abilities. 266 calls in `mcfr0100` actor 38 entry 9 |
| 0x4C3170 | fn | `FFX_Equip_SetAbilitySlotsFromTable` | writes four u16 at equip record `+0x0E`..`+0x15` |
| 0x386900 | fn | `FFX_SaveData_RecomputeAllCharDerived` | called after each grant |
| 0x886D00 | tbl | the eighteen 8-byte ability-row tables | IDA `off_C86D00` .. `off_C86E10`, RVA 0x886D00 .. 0x886E10, 0x10 apart |
| 0x45D210 | fn | `FFX_AtelSys_Core_378_resi` | `core:378`, 397 calls in `mcfr0100`. Has a special case for scene 0x5E on map 0x186 |
| 0x45D3D0 | fn | `FFX_AtelSys_Core_379_resi` | `core:379`, 31 calls in `mcfr0100`, 29 in a row at the top of the game body |

### The magic DLLs

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x864CE8 | tbl | `g_ffxMagicHostApiTable` | **947 slots: 913 functions, 33 `.data` globals, 1 NULL.** 4 clock-reachable and 17 RNG-reachable, both counted over entries 0..740 only, so both are lower bounds |
| 0x5DA7F0 | fn | `MagicFile__start` | hands the table to a DLL via `InitMagicPRX` |
| 0x5DA3B0 | fn | `MagicFile__getMagicId` | read by `WaitFrames_start`'s fudge |

### Asset path override

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x642C00 | fn | `FFX_Asset_ResolvePathWithDebugOverrides` | three package swaps at `0x642D7E`, `0x642DFB`, `0x642E6C` |
| 0x1685C30 | glob | `g_ffxResolvedAssetPathBuf` | 255 bytes |
| 0x1685BB0 | tbl | substitution keys | 16 entries, stepped by 4 to 0x40 |
| 0x1685BF0 | tbl | substitution values | 16 entries |
| 0x1685BA8 | glob | `g_ffxFullNagi0700Enable` | **hash this**, swaps `nagi0700.ebp` for `full_nagi0700.ebp` |

### Clock sources, for completeness

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x02FD80 | fn | `Phyre_Time_NowSeconds` | the only `QueryPerformanceCounter` reader in the engine |
| 0x02F690 | fn | `sub_42F690` | wraps it |
| 0x241410 | fn | `FFX_Time_AppElapsedSeconds` | the FFX-side wrapper. **30 callers, 65 sites**, see below |
| 0x241420 | fn | `FFX_Time_NowMilliseconds` | **misnamed, it returns microseconds**, see section 11 |
| 0x230C60 | fn | `FFX_Input__getTimeSeconds` | **not 0x230C40**, which is `FFX_Input__clearThreadedSampleQueues` |
| 0x2EA050 | fn | dlmalloc `init_mparams` | `GetTickCount() ^ 0x55555550`, one shot, the source of nearly all false clock reachability |

**Correction, 2026-10-04.** The row for `FFX_Time_AppElapsedSeconds` used to read "only gameplay
consumer is the Lulu Fury overdrive". That is badly wrong. It has 30 calling functions and 65 call
sites. Walking up from each caller through direct calls only, bounded at six levels, 11 of the 30
land inside the simulation:

| caller | reaches | depth | sites |
|---|---|---|---|
| `FFX_BtlMenu_Step` | `FFX_Btl_MainStep` | 1 | 0x49AEF6, 0x49AF39 |
| `FFX_BtlOd_LuluFuryStickMinigame` | `FFX_BtlMenu_Step` | 1 | 0x491BB6, 0x491BD6 |
| `sub_491480` | `FFX_BtlMenu_Step` | 1 | 0x491565 |
| `sub_2E78A0` | `FFX_MainStep` | 3 | 0x2E78A7 |
| `sub_2E78C0` | `FFX_MainStep` | 3 | 0x2E78CC |
| `sub_493A30` | `FFX_MainStep` | 3 | 0x493A50, 0x494542, 0x4945B9 |
| `sub_4FEB00` | `FFX_MainStep` | 3 | 0x4FEB36 |
| `sub_4960E0` | `FFX_MainStep` | 4 | 0x496105 |
| `sub_49BC80` | `FFX_MainStep` | 4 | 0x49BCAC, 0x49BD17 |
| `sub_4C0670` | `FFX_MainStep` | 4 | 0x4C069F |
| `sub_4F41B0` | `FFX_MainStep` | 4 | 0x4F41B8 |
| `sub_4E8A10` | `FFX_MainStep` | 6 | six sites from 0x4E8B0A to 0x4E8DB0 |

The other 19 showed no anchor within six levels of direct calls, and that is NOT the same as being
outside the simulation. This engine dispatches heavily through vtables, which a direct-call walk
cannot follow, so treat the 19 as unaudited rather than clear. The large clusters among them
(`sub_4A3680` five sites, `sub_4A49B0` nine sites, `sub_4A6A60` four sites) sit next to
`FFX_Menu_DrawGaugeBarClockPulsed`, which does suggest UI pulsing, but nobody has read them.

Of the 11 confirmed, two pairs are already patched or spoken for: the `FFX_BtlMenu_Step` pair is the
page input lockout that `ffx/StepClock.h` patches today, and the Lulu pair is section 9.5. The
remaining seven callers have not been looked at.

### Not minigame state

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x51CE50 | fn | `FFX_GuideMap_LoadSceneRecord` | was `sub_91CE50`. Hands out the four 512-byte `+0x350C` records for scene ids 582/584/590/591 |
| 0x385400 | fn | `FFX_SaveData_GetBlock350C` | the accessor |
| 0x51D5E0 | fn | `rcbgGuide__f91D5E0` | the module those records belong to |
| 0x51ECB0 | fn | `sub_91ECB0` | carries the string `'RenderMiniMapFog'` |

### The battle counterexample

| RVA | Type | Name | Note |
|---|---|---|---|
| 0x491B80 | fn | `FFX_BtlOd_LuluFuryStickMinigame` | reads the wall clock at `0x491BB6` and `0x491BD6`. **Does not replicate** |
| 0x49AE20 | fn | `FFX_BtlMenu_Step` | calls it at `0x49AE4D` and `0x49AE6A`. Widget array `0xF3C950`, stride 240, startTime `+0xDC`, elapsed `+0xE0` |

## 11. What is untested, and what I think is most likely wrong

Ranked, most likely wrong first.

1. **The butterfly `var57` interpretation.** I read `var57 = 128`, 14 sites doing `+= 3`, 14 doing
   `-= 3`, and a test `var57 >= 89`. I called it a gauge. I did not trace the 44 KB of actor 38
   entry 9 far enough to prove that reading, and the message text says "catch seven of the blue
   butterflies", which does not obviously reduce to any of those numbers. If someone needs the
   butterfly win condition exactly, start at `mcfr0100` actor 38 entry 9 offset `0x7D7E` and follow
   every `var57` site. **The co-op verdict does not depend on this.**
2. **The Lulu's Fury attribution in 9.5.** The clock reads and the rotation constants are read out of
   the disassembly and I stand behind them. The "it is Lulu specifically" claim rests on the sound
   cue strings `blulu`, `blulu2`, `bgoodl`, which is good circumstantial evidence and not proof. If
   it turns out to be a different character's overdrive, the wall-clock finding is unaffected.
3. **Whether the Cactuar hunt's "back is turned" check reads the single-slot player position
   cache.** `INTERACTION_PATH.md` section 3.2 and `RANDOM_ENCOUNTER.md` section 3 are both about a
   one-slot cache at `ctx+536` that only ever holds one character. If the cactuar facing check reads
   it then the second player is invisible to the cactuars. **I did not test this**, and it is not a
   desync, it is a fairness and playability bug. Worth 20 minutes from whoever owns the position
   cache work.
4. **Whether a sub-step count greater than 1 ever actually happens during a minigame.** My whole
   "replicates for free" conclusion is conditioned on both peers agreeing on
   `FFX_Player__getSubStepCount()`. I verified the loop exists and that ATEL is inside it. I did not
   verify what makes the count exceed 1, or whether it can differ between two machines running the
   same inputs. If it is a pure function of the booster index and the pending-step count then this
   is already solved by the lockstep gate. If it reads a frame time anywhere, it is the single
   biggest hole in this document. **Check this first.**
5. **Whether the class-3 resolver hook `g_ffxAtelCtx[0x54]` is ever non-null during field play.**
   The butterfly clock is class 3. If a hook is installed, the butterfly timer's storage moves and
   anything the mod does to class-3 memory has to follow it. I found the branch in the resolver but
   did not find a writer of `+0x54`.
6. **Which of `swin0000`'s three class-0 variables is the Jecht Shot completion flag.** I narrowed it
   to `g_ffxSaveData + 0x3ED`, `+0x3EF` or `+0x3F9` and stopped. Low value, since the whole
   ScriptWork range gets synced anyway.
7. **The 30 and 80 tiers in the lightning switch.** I am confident `var15` is bolts seen and the two
   literals are 30 and 80, because I read the chain. I do not know what the two reward bits 0x40 and
   0x80 unlock. It might be NPC dialogue rather than a chest.
8. **`core:378`, 397 calls in `mcfr0100`.** I read enough of `FFX_AtelSys_Core_378_resi 0x45D210` to
   see it forwards to `sub_86FA40(n)` with a scene-0x5E-on-map-0x186 special case, and that it is
   not a timer and not a clock, which is all I needed. I did not work out what it does. If the
   butterfly hunt misbehaves under co-op this is the first thing to look at, since 397 calls in one
   package is a lot.
9. **Whether anything outside the script moves a butterfly or a cactuar.** I proved no clock is read
   on the script path and that no magic DLL can read one. I did not audit the character motion
   pipeline (`FFX_Ch_UpdateMotionAll 0x432E10`) for float non-determinism, which is a general
   engine question rather than a minigame one.
10. **The Via Purifico.** I answered it with two negative scans (no 10/60 cascade, no RNG draws) plus
    the message tables, and I did not read its bytecode. It is the least thoroughly covered of the
    six. If you need certainty there, read `bvyt0500` actor by actor. The reason I am still
    comfortable with the verdict is that the negative scans are over the whole file, not a sample.

**Which four I covered properly:** the Chocobo race, lightning dodging, the butterfly hunt and the
Jecht Shot tutorial, all four with the actual bytecode of the timer in front of me plus the variable
descriptors that say where it lives. The Cactuar hunt and the Via Purifico are covered at a lower
resolution: for both I proved the absence of a clock and the absence of RNG over the whole package
set, identified the packages from the shipped text, and did not read their bytecode. For a
"replicates for free" verdict the absence proofs are the load-bearing part, so I think the answer is
right, but it is a thinner answer than the first four.
