# The overdrive input minigames

Wakka's reels, Lulu's Fury, Auron's Banishing Blade family, Tidus's swing bar, plus the three
overdrives that are menus rather than minigames (Yuna's Grand Summon, Kimahri's Ronso Rage, Rikku's
Mix). What drives each one, where its clock comes from, how it reads input, where the result lands,
and which of them survive delayed-input lockstep.

**Addresses in this document are RVAs**, that is IDA VA minus 0x400000. Section 11 is the full
promotion list.

The starting point for this pass was the existing note on `FFX_BtlOd_LuluFuryStickMinigame
0x491B80`, which said the overdrive time limit is measured in real seconds because the function
reads `FFX_Time_AppElapsedSeconds 0x241410` twice. **That turned out to be the wrong conclusion from
a true observation.** The two clock reads are real, but they do not feed the time limit. Section 9
has the correction with the evidence. The family turned out to have four separate wall-clock
problems, none of which is the one the old note describes, and two of which are worse.

## Summary, read this first

| Minigame | Character | Where the logic lives | Timer | Input | Result | RNG | Verdict |
|---|---|---|---|---|---|---|---|
| Swing bar | Tidus, actor 0 | `FFX_BtlOd_TidusSwingBarMinigame 0x492320` in FFX.exe | step-counted | `FFX_Pad__readPressed16` (+6), pure math, at ring lag -1 AND lag 0 | Actor+0xD28 | none of its own | **replicates once the pad ring is replicated** |
| Button sequence | Auron, actor 2 | `FFX_BtlOd_AuronButtonSeqMinigame 0x490F70` in FFX.exe | step-counted | `HIWORD(g_ffxMenuPadHeldTrig)`, pure math, but 4 of the 8 mask bits are synthesized from the raw analog bytes | Actor+0xD28 | none of its own | **replicates once the raw analog BYTE is shipped** |
| Stick rotation | Lulu, actor 5 | `FFX_BtlOd_LuluFuryStickMinigame 0x491B80` in FFX.exe | step-counted | pad: `FFX_Pad__readAnalogByte` at ring lag -1 AND lag 0, pure math. **keyboard: two keys inside a 0.5001 WALL-second window** | Actor+0xD28 | none of its own | **replicates on a pad once the ring is replicated. Does NOT replicate on keyboard** |
| Reels | Wakka, actor 4 | **not in FFX.exe.** 10 magic DLLs | step-counted (host 651/657) | reads host 302, 303 and 304 but **only 304 (+6, newly pressed) is used**, see 7.4 | Actor+0xD40, 31 bytes | 1 `FFX_Btl_Rand` site per DLL | **replicates. The wall-clock word it samples is never read** |
| Grand Summon | Yuna, actor 1 | battle menu page stack | no minigame timer | battle menu | aeon selection | none | menu, see below |
| Ronso Rage | Kimahri, actor 3 | battle menu page stack | no minigame timer | battle menu | 12 abilities | none | menu, see below |
| Mix | Rikku, actor 6 | battle menu page stack | no minigame timer | battle menu | 64 recipes | none | menu, see below |
| Swordplay | Tidus, actor 0 | battle menu page stack | - | battle menu | - | - | the parent command, not a minigame |
| Bushido | Auron, actor 2 | battle menu page stack | - | battle menu | - | - | the parent command |
| Slots | Wakka, actor 4 | battle menu page stack | - | battle menu | - | - | the parent command |
| Fury | Lulu, actor 5 | battle menu page stack | - | battle menu | - | - | the parent command |

And the thing that matters more than any single minigame:

**Every battle menu page in the game has a wall-clock input lockout.** `FFX_BtlMenu_Step 0x49AE20`
stamps a page's `startTime` at record+0xDC from `FFX_Time_AppElapsedSeconds`, computes
`elapsed` at record+0xE0 every step, and eight of the nine state-4 callbacks refuse to run their
page proc until `elapsed` passes 0.30 s (`flt_B3EB44`) or 0.45 s (`dbl_B43D50`). On top of that the
cursor repeat in `FFX_BtlMenu_PageProc_Root/B/C/D` reads `g_ffxMenuPadWord10`, which is the
wall-clock auto-repeat mask. So Grand Summon, Ronso Rage and Mix are wall-clock dependent not
because they are overdrives but because they are menus. Section 8.

Seven things to take away.

1. **The overdrive minigame clock is a step counter, not a wall clock.**
   `FFX_BtlOd_StepSharedTimer 0x491AC0` adds 1.0 per sub-step and divides by a constant 30 to get
   seconds. The old note's premise is wrong on this point. Section 3.
2. **The divisor is nominally picked from the LANGUAGE, but in practice it is always 30.** 25 when
   `g_ffxMesWinFontMode 0x1465F00` is non-zero, 30 when it is zero, and 25 against 30 turns out to
   be the original PS2 region field rate. That global is computed once inside `FFX_MainInit` from a
   byte that is still zero at that point, so it is 0 on every machine and the 25.0 path is dead
   code. I chased this one hard because it looked like the cheapest desync in the family. It is not
   a desync at all. Section 3.2.
3. **The timer burns one `FFX_Btl_Rand` draw per sub-step while a minigame is running**, purely to
   randomise the hundredths digit of the on-screen countdown. The draw is cosmetic. Its effect on
   `g_ffxBattleRandState 0x842200` is not. Section 5.
4. **The live minigame state is a single global set, the result is per-character.** Phase, step
   counter, budget, remaining and display all live in one eight-word block at `0xF3F77C..0xF3F794`.
   Two characters cannot be inside an overdrive minigame at the same time. The result lands in the
   battle actor record at Actor+0xD28/0xD2C/0xD30 (and Actor+0xD40 for the reels). Section 6.
5. **Wakka's reels are not in the executable.** FFX.exe supplies the shared timer, the HUD, the
   sound cues, the per-actor symbol strip and the level number. The reel spin, the stop and the
   symbol match are inside 10 of the 581 shipped `magic_NNNN.dll` files. Those DLLs sample the
   wall-clock auto-repeat mask through magic host API slot 303 and then never read what they stored,
   so the reels replicate anyway. Section 7, settled in 7.4.
6. **The minigame is started from two places, not one.** The native gate
   `FFX_BtlOd_MaybeStartMinigame 0x3AFC90` only passes four ability kinds. Everything else, including
   all four of Auron's overdrives and all four of Wakka's, is started by the effect DLL calling
   magic host API slot 630. Section 2.4.
7. **Tidus's and Lulu's minigames read the pad RING at two lags, not just the live sample.** The
   last argument to `FFX_Pad__readPressed16` and `FFX_Pad__readAnalogByte` is a ring lag, because
   `FFX_Pad__getRingSlot 0x488B60` indexes a four-entry ring of 32-byte samples with
   `(writeCursor + lag) & 3`. Tidus reads lag -1 and lag 0. Lulu reads X and Y at lag -1 and then X
   and Y at lag 0. The good news is that `FFX_Pad__commitRingSlot 0x489790` builds the whole entry
   from the port's STAGING fields with pure math, so the mod should write `ps[0x98]` and
   `ps[0x84..0x93]` and let the engine derive the pressed and released masks. What has to match is
   the number of commits, and that is once per frame except during Lulu's minigame, which adds one
   per sub-step. Sections 4.4 and 4.5.

What the mod has to do:

- **Inject at the pad port's STAGING fields, `ps[0x98]` and `ps[0x84..0x93]`, not at the ring
  entry**, and make sure `FFX_Pad__commitAllPorts` runs the same number of times on both peers.
  Tidus and Lulu both read ring lag -1 as well as lag 0, and the commit derives +6 and +8 as pure
  math, so getting the staging fields and the commit count right gets the whole ring right.
  Sections 4.4 and 4.5.
- **`g_ffxMesWinFontMode 0x1465F00` needs no agreement**, it is always 0. Hashing it is still a
  cheap guard, because if it were ever non-zero every overdrive minigame would run 20 percent
  faster. Section 3.2.
- **Agree `FFX_Player__getSubStepCount()` per step.** Same requirement `MINIGAMES_TIMED.md`
  already identifies for the field minigames, for the same reason: the clock is a sub-step count.
- **Ship the raw quantised analog bytes, not reconstructed floats.** Lulu's quadrant classifier
  compares the raw bytes against 0x60 and 0xA0. Auron's sequence contains direction bits that
  `FFX_MesWin_SamplePadPort0` synthesizes by comparing the raw bytes against 0x18 and 0xE8. Same
  trap as the Jecht Shot tutorial.
- **Decide what to do about Lulu's keyboard path.** It is the only input path in the family that
  reads a wall clock to make a gameplay decision. Options are to force the pad path, to replace the
  0.5001 s window with a sub-step count, or to accept that Lulu desyncs on keyboard.
- ~~**Decide what to do about the reels.**~~ SETTLED, see 7.4. Nothing to do. The slot-303 read is
  dead and the stop runs off slot 304 plus the step-counted timer.
- **Replicate or neutralise the per-sub-step `FFX_Btl_Rand` draw** in `FFX_BtlOd_StepSharedTimer`.
  A one-instruction patch removes it and the only visible change is that the HUD's hundredths digit
  stops flickering.
- **Deal with the battle menu's 0.30 s / 0.45 s wall-clock lockout and the `g_ffxMenuPadWord10`
  cursor repeat.** Bigger than this document, but Mix, Grand Summon and Ronso Rage all sit on it.
- **Agree `g_boosterAutoBattle 0xF3D6E0`.** It forces input in two places in this family
  (`FFX_BtlOd_PressCircleGate` and the quantity page) and suppresses drawing in a third.

## 1. The family, enumerated

### 1.1 How I enumerated it

Three independent routes, which agree.

**Route 1, the caller list of `FFX_BtlMenu_Step 0x49AE20`.** The note the user had already written on
that function says it calls the Lulu minigame. Reading its disassembly gives the whole per-sub-step
call list, and the Lulu minigame is one of eight unconditional calls in a block:

```
0089AE2A  call sub_891140                         ; target candidate list rebuild
0089AE2F  call sub_8920D0                         ; ally HP/MP bar animation
0089AE34  call sub_891220                         ; enemy HP bar animation
0089AE39  call FFX_BtlOd_PressCircleGate          ; 0x491A30
0089AE3E  call FFX_BtlOd_StepSharedTimer          ; 0x491AC0  <- the minigame clock
0089AE43  call FFX_BtlOd_AuronButtonSeqMinigame   ; 0x490F70
0089AE48  call FFX_BtlOd_TidusSwingBarMinigame    ; 0x492320
0089AE4D  call FFX_BtlOd_LuluFuryStickMinigame    ; 0x491B80
0089AE52  cmp  g_ffxThreadedPadMode, 0
0089AE59  jz   short loc_89AE6F
0089AE5B  call FFX_Input__consumeThreadedSample
0089AE60  call FFX_Pad__updateAll
0089AE65  call FFX_Pad__commitAllPorts
0089AE6A  call FFX_BtlOd_LuluFuryStickMinigame    ; 0x491B80 AGAIN
```

Note the second Lulu call. Section 9.4.

**Route 2, the sound cue strings.** The cue names are `b` plus a character abbreviation and they
sit in one run in `.rdata` at RVA 0x75ECCC..0x75ED31:

| String | RVA | Referenced by |
|---|---|---|
| `bauro`, `bauro2` | 0x75ECCC, 0x75ECD4 | `FFX_BtlOd_AuronButtonSeqMinigame 0x490F70` |
| `btidu` | 0x75ECEC | `FFX_BtlOd_TidusSwingBarMinigame 0x492320` |
| `blulu`, `blulu2`, `bgoodl` | 0x75ED14, 0x75ED1C, 0x75ED24 | `FFX_BtlOd_LuluFuryStickMinigame 0x491B80` |
| `bwakk` | 0x75ED2C | `FFX_BtlOd_WakkaReels_StopCue 0x490F10`, `FFX_BtlOd_WakkaReels_Draw 0x4974A0`, `FFX_BtlOd_WakkaReels_IsCuePlaying 0x4977C0`, `FFX_BtlOd_WakkaReels_Begin 0x498DC0` |
| `bgood` | 0x75ECDC | the Auron and Tidus minigames, plus `0x4977E0` and `0x49AB80` |

There is no `byuna`, no `bkimah` and no `brikku`. That is the first sign that Yuna, Kimahri and
Rikku have no input minigame.

**Route 3, the actor dispatch in the launcher.** `FFX_BtlOd_StartMinigameForActor 0x3AFD30`
dispatches on the actor index and nothing else, and it has exactly three branches. Section 2.

### 1.2 The complete function list

The exe side. All verified by reading the disassembly of each one.

| RVA | Name | Role |
|---|---|---|
| 0x3AFC90 | `FFX_BtlOd_MaybeStartMinigame` | the native gate, from action phase 3 |
| 0x3AFD30 | `FFX_BtlOd_StartMinigameForActor` | **the one entry point**, magic host API 630 |
| 0x3AFDE0 | `FFX_BtlOd_IsMinigamePending` | host 629 |
| 0x3B0470 | `FFX_BtlOd_ReportMinigameResult` | the per-character result write |
| 0x491AC0 | `FFX_BtlOd_StepSharedTimer` | the step-counted clock, plus the RNG draw |
| 0x497F00 | `FFX_BtlOd_ResetSharedTimer` | host 654 |
| 0x497780 | `FFX_BtlOd_GetTimeRemaining` | host 651 |
| 0x49A2A0 | `FFX_BtlOd_SetTimeBudget` | host 657 |
| 0x49A2D0 | `FFX_BtlOd_SetHudPos` | host 658 |
| 0x4955E0 | `FFX_BtlOd_DrawTimerHud` | the countdown readout |
| 0x490E70 | `FFX_BtlOd_SetPhaseIdle` | host 648, phase 0 |
| 0x49AB20 | `FFX_BtlOd_SetPhaseArmed` | host 659, phase 1 |
| 0x49AB50 | `FFX_BtlOd_SetPhaseRunning` | host 660, phase 2 |
| 0x49ABA0 | `FFX_BtlOd_SetPhaseArmed_dup` | host 662, byte-identical to 659 |
| 0x4977E0 | `FFX_BtlOd_IsGoodCuePlaying` | host 653 |
| 0x49AB80 | `FFX_BtlOd_PlayGoodCue` | host 661 |
| 0x491A30 | `FFX_BtlOd_PressCircleGate` | a DLL-armed "press Circle" stall |
| 0x490EC0 | `FFX_BtlOd_ArmPressCircleGate` | host 372, arms it |
| **Tidus** | | |
| 0x492320 | `FFX_BtlOd_TidusSwingBarMinigame` | the game |
| 0x498CA0 | `FFX_BtlOd_TidusMinigame_Launch` | level, budget, geometry |
| 0x498180 | `FFX_BtlOd_TidusMinigame_Reset` | clears state, anchors the HUD on `dum203` |
| 0x497240 | `FFX_BtlOd_DrawTidusBar` | |
| **Auron** | | |
| 0x490F70 | `FFX_BtlOd_AuronButtonSeqMinigame` | the game |
| 0x498AD0 | `FFX_BtlOd_AuronMinigame_Launch` | level, budget, sequence pointer |
| 0x497970 | `FFX_BtlOd_AuronMinigame_Reset` | clears state, anchors on `dum217` |
| 0x492740 | `FFX_BtlOd_DrawAuronSeq` | |
| **Lulu** | | |
| 0x491B80 | `FFX_BtlOd_LuluFuryStickMinigame` | the game |
| 0x498BF0 | `FFX_BtlOd_LuluMinigame_Launch` | level from the ability id, rotation table |
| 0x497F70 | `FFX_BtlOd_LuluMinigame_Reset` | clears 0x2C bytes, anchors on `dum209`..`dum212` |
| 0x495660 | `FFX_BtlOd_DrawLuluGauge` | |
| **Wakka** | | |
| 0x498DC0 | `FFX_BtlOd_WakkaReels_Begin` | host 656, starts `bwakk`, sets the active flag |
| 0x498230 | `FFX_BtlOd_WakkaReels_End` | host 655 |
| 0x490F10 | `FFX_BtlOd_WakkaReels_StopCue` | host 649 |
| 0x4974A0 | `FFX_BtlOd_WakkaReels_Draw` | host 650 |
| 0x4977C0 | `FFX_BtlOd_WakkaReels_IsCuePlaying` | host 652 |
| 0x3B1910 | `FFX_BtlOd_WakkaReels_GetStrip` | host 645, the per-actor symbol strip |
| 0x3B19F0 | `FFX_BtlOd_WakkaReels_GetLevel` | host 646, ability id -> level 0..3 |
| 0x3B1A70 | `FFX_BtlOd_WakkaReels_PostDone` | host 647, fires battle event 136 |

Magic host API slots 629, 630, 645, 646, 647 and the contiguous block 648..662 are the whole
overdrive-minigame plugin surface. 645..647 are Wakka-specific, 648..662 are shared.

### 1.3 The three overdrives that are menus

Nothing in `FFX_BtlOd_StartMinigameForActor` handles actors 1 (Yuna), 3 (Kimahri) or 6 (Rikku), and
no sound cue exists for them. Their overdrives are battle menu pages. Section 8 covers what that
costs.

## 2. How a minigame starts

### 2.1 The one entry point

`FFX_BtlOd_StartMinigameForActor 0x3AFD30`, disassembled:

```
007AFD3F  call FFX_Battle_GetActor
007AFD44  mov  word ptr [eax+0D24h], 1       ; <- the PENDING flag, Actor+3364
007AFD4D  movzx eax, word ptr [eax+0F5Ch]    ; the current ability id, Actor+3932
007AFD62  call FFX_Btl_ResolveAbilityId
007AFD6E  sub  ecx, 0
007AFD71  jz   loc_7AFD9D                    ; actor 0 -> TIDUS
007AFD73  sub  ecx, 2
007AFD76  jz   loc_7AFD8C                    ; actor 2 -> AURON
007AFD78  sub  ecx, 3
007AFD7B  jnz  loc_7AFDAF                    ; anything else -> nothing
007AFD7D  call FFX_BtlOd_LuluMinigame_Reset  ; actor 5 -> LULU
007AFD85  call FFX_BtlOd_LuluMinigame_Launch ; takes the ABILITY ID
007AFD8C  call FFX_BtlOd_AuronMinigame_Reset
007AFD91  movzx eax, byte ptr [edi+58h]      ; the ability row's kind byte
007AFD96  call FFX_BtlOd_AuronMinigame_Launch
007AFD9D  call FFX_BtlOd_TidusMinigame_Reset
007AFDA2  movzx eax, byte ptr [edi+58h]
007AFDA7  call FFX_BtlOd_TidusMinigame_Launch
007AFDC6  call sub_798000                    ; (0, actor, 0, 135, 0xFFFF, 0, 1, 0, 0)
```

Two things worth noting. The PENDING flag is set **before** the dispatch, so it is set for every
actor, including Wakka, Yuna, Kimahri and Rikku. And the Lulu launcher takes the ability id while the
Tidus and Auron launchers take the row's kind byte.

**This really is the only entry point.** I scanned every segment of `FFX.exe` (`.text`, `.idata`,
`.rdata`, `.data`, `.rodata`, `_RDATA`) for the raw little-endian dword of each of
`FFX_BtlOd_StartMinigameForActor`, `FFX_BtlOd_ReportMinigameResult`, the three per-character launch
functions and the three per-character minigame functions. Only one occurrence of anything, the
host-table entry for 0x3AFD30 at VA 0xC656C0. So no address-taken call, no vtable, no second
dispatcher. I also scanned all 7,385,088 bytes of `.text` for the displacement `24 0D 00 00`
(Actor+0xD24): seven raw hits, which I resolved to instruction heads by hand. Four are real accesses
to the pending flag (0x3AFD44 the only write of 1, 0x3AFDF0 the read, 0x7B0489 and 0x7B04A2 the test
and clear) and three belong to unrelated structures that happen to use the same displacement. That
scan is a byte scan and it over-counts, which is exactly why I hand-resolved each hit.

### 2.2 The kind byte, decoded from the shipped data

`FFX_BtlOd_MaybeStartMinigame 0x3AFC90` reads byte +0x58 of the resolved ability row:

```
007AFCC3  0F B6 48 58           movzx ecx, byte ptr [eax+58h]
007AFCC7  8D 41 E0              lea   eax, [ecx-20h]
007AFCCA  83 F8 20              cmp   eax, 20h
007AFCCD  77 29                 ja    default
007AFCCF  0F B6 80 08FD7A00     movzx eax, byte_7AFD08[eax]
007AFCD6  FF 24 85 00FD7A00     jmp   jpt_7AFCD6[eax*4]
```

The 33-byte index table at 0x3AFD08 is
`00 01 01 01 01 00 01 01 01 01 01 01 01 01 01 01 00 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 00`
and the 2-entry jump table at 0x3AFD00 is `{0x7AFCDD, 0x7AFCF8}`, where 0x7AFCDD is the branch that
calls the launcher. I read both with `ida_bytes.get_original_byte` and `get_wide_byte` and they
agree. So the gate passes kind bytes **0x20, 0x25, 0x30, 0x40** and nothing else.

To find out what those are I read `battle/kernel/command.bin` out of `data/FFX_Data.vbf` with
`tools/vbf.py` (path `ffx_ps2/ffx/master/new_uspc/battle/kernel/command.bin`, 44,558 bytes) and
walked it with the layout `FFX_KernelTable_GetRow`'s IDB comment documents: one range descriptor,
rows 0..319, stride 96, data at +0x14, strings at +0x7814. Byte +0x58 is non-zero on 141 of the 320
rows, and the layout is clean:

**The kind byte is `(level + 2) << 4 | characterId`.**

| Kind | Abilities | Decoded names |
|---|---|---|
| 0x20 / 0x30 / 0x40 / 0x50 | 0x3060 / 0x3061 / 0x3062 / 0x3063 | Tidus: Spiral Cut, Slice & Dice, Energy Rain, Blitz Ace |
| 0x22 / 0x32 / 0x42 / 0x52 | 0x3064 / 0x3065 / 0x3066 / 0x3067 | Auron: Shooting Star, Dragon Fang, Banishing Blade, Tornado |
| 0x23 | 0x3068 .. 0x3073, 12 rows | Kimahri: Ronso Rage moves, first is Jump |
| 0x24 / 0x34 / 0x44 / 0x54 | 0x3074 / 0x3075 / 0x3076 / 0x3077 | Wakka: Element Reels, Attack Reels, Status Reels, Aurochs Reels |
| 0x25 | 0x3078 .. 0x308A, 19 rows | Lulu: the 19 Fury spells, first is Blizzard |
| 0x26 | 0x308B .. 0x30CA, 64 rows | Rikku: the 64 Mix recipes, first is Grenade |
| 0x10 .. 0x16 | 0x3119, 0x3118, 0x311B, 0x311A, 0x311C, 0x311D, 0x311E | the seven parent commands: Swordplay, Grand Summon, Bushido, Ronso Rage, Slots, Fury, Mix |

Note the extra kind values 0x20 and 0x30 etc. also appear on a second, higher id range
(0x30EB..0x30FE, 0x310A..0x3112, 0x312E). Those are the same overdrives again, presumably the
scripted or cutscene variants. I did not chase them.

I recovered the names by decoding the string area with the FFX message table, which I derived from
the known answer "Spiral Cut" for row 0x3060: uppercase A-Z is 0x50..0x69, lowercase a-z is
0x70..0x89, space is 0x3A, `&` is 0x40. Every other name I checked came out correct with that table
(Blitz Ace, Banishing Blade, Tornado, Aurochs Reels, Grand Summon, Ronso Rage, Bushido, Slots, Fury,
Mix, Doublecast, Jump, Grenade), so the row mapping and the names are solid.

### 2.3 Which means `FFX_BtlOd_MaybeStartMinigame` only covers Tidus and Lulu

Cross the gate against the data and the native path passes exactly:

- 0x20 Spiral Cut, 0x30 Slice & Dice, 0x40 Energy Rain. Tidus levels 0, 1 and 2.
- 0x25, all 19 Fury spells. Lulu, and her launcher ignores the kind byte and uses the ability id
  anyway, so all 19 work.

And rejects Blitz Ace (0x50 is out of the `cmp eax, 20h` range entirely), all four of Auron's, all
four of Wakka's, all of Kimahri's and all of Rikku's.

### 2.4 The other route: the effect DLL starts it

`FFX_BtlOd_StartMinigameForActor` is magic host API slot **630**, so a magic DLL can call it.

`tools/magicdll.py`'s `host_calls` used to report zero DLLs calling slot 630. **That was a tool
bug, and it has since been fixed.** The old matcher wanted `mov reg,[reg+disp32]` immediately
followed by `call reg` with one register throughout, so argument pushes between the load and the
call hid the site, and that is exactly what happens here: the `6A 00` sits between them. Hand
reading the bytes in `magic_0393.dll` at `.text+0x57A`:

```
a1 04ab0f10        mov eax, ds:100FAB04h        ; the saved host table pointer
6a 00              push 0
8b 80 d8090000     mov eax, [eax+9D8h]          ; 0x9D8 = 630 * 4
51                 push ecx
ff d0              call eax
```

Same shape at `.text+0x542` for slot 629 (`+0x9D4`), which the old `host_calls` *did* find, because
there the sequence is `8B 80 D4 09 00 00; FF D0` with nothing in between. `magic_0450.dll` at
`.text+0x462A` is byte-identical in shape.

So I rescanned all 581 DLLs with an encoding-verified filter instead: the 4-byte displacement must
be preceded by a modrm byte with mod=10 and no SIB, under opcode `8B` (`mov r32, [r32+disp32]`) or
`FF /2` (`call [r32+disp32]`). That rejects coincidental 4-byte matches such as the one at
`magic_0691.dll` `.text+0x550F`, where the bytes `B8 04 00 00` are the tail of `mov eax, 0x19`.
The filter can still over-count in principle, so I hand-verified one site per slot of interest.

**That filter does over-count, and the "DLLs" and "Sites" columns below have been corrected for
three rows.** It has no test that the base register actually holds the table pointer, so an ordinary
struct field access at the same displacement counts. `tools/magicdll.py` now applies that test, and
agrees with this filter on every row except slots 324, 629 and 630. The corrected numbers are in
the table. `reversing/MAGIC_DLL.md` section 7 lists the rejected sites one by one and says what each
of them really is.

| Host slot | What it is | DLLs | Sites | Which DLLs |
|---|---|---|---|---|
| 302 | `FFX_Pad__readButtons16` (+4 held) | 12 | 12 | 0565, 0644, 0662, 0691..0698, 0711 |
| 303 | `FFX_Pad__readWord10` (+10, **wall-clock auto-repeat**) | 10 | 10 | 0565, 0644, 0691..0698 |
| 304 | `FFX_Pad__readPressed16` (+6 pressed) | 10 | 10 | 0565, 0644, 0691..0698 |
| 324 | `FFX_Btl_Rand` | **92** | **274** | - (was 99 / 292, 18 sites in 7 DLLs were struct accesses) |
| 629 | `FFX_BtlOd_IsMinigamePending` | **6** | **6** | 0393..0397, 0450 (0165 and 0606 were false positives) |
| 630 | `FFX_BtlOd_StartMinigameForActor` | **6** | **6** | 0393..0397, 0450 (same two false positives) |
| 645 | `FFX_BtlOd_WakkaReels_GetStrip` | 10 | 10 | 0565, 0644, 0691..0698 |
| 646 | `FFX_BtlOd_WakkaReels_GetLevel` | 10 | 10 | same ten |
| 647 | `FFX_BtlOd_WakkaReels_PostDone` | 10 | 10 | same ten |
| 651 | `FFX_BtlOd_GetTimeRemaining` | 10 | 20 | same ten |
| 654 | `FFX_BtlOd_ResetSharedTimer` | 10 | 10 | same ten |
| 656 | `FFX_BtlOd_WakkaReels_Begin` | 10 | 10 | same ten |
| 657 | `FFX_BtlOd_SetTimeBudget` | 10 | 10 | same ten |

So: **6 DLLs start an overdrive minigame themselves**, and the same 6 poll the pending flag. That is
the route Blitz Ace, Auron's four and Wakka's four take. The 10 reels DLLs are a separate, complete
cluster that uses the shared timer and the Wakka helpers and reads the pad.

Note the `FFX_Btl_Rand` figure. The old `magicdll.py` pattern reported 83 of the 581, this section's
looser filter reported 99, and **the right answer is 92 DLLs at 274 call sites.** Section 10 has it
as a correction, and `reversing/MAGIC_DLL.md` section 7 has the derivation both ways.

## 3. The clock, which is a step counter

### 3.1 `FFX_BtlOd_StepSharedTimer 0x491AC0`

Called from exactly one place, `FFX_BtlMenu_Step` at 0x49AE3E, unconditionally. `FFX_BtlMenu_Step`
is called from `FFX_Btl_MainStep 0x390C10` at 0x390D74, also unconditionally (the `jz` just above it
jumps *to* the call). `FFX_Btl_MainStep` is called at 0x421015, inside `FFX_MainStep`'s sub-step
loop. So the timer advances **once per simulation sub-step**.

```
00891AC4  cmp     g_ffxBtlOdMinigamePhase, 2
00891ACB  jnz     loc_891B70
00891AD1  fld     g_ffxBtlOdStepCounter
00891AD7  fadd    ds:qword_B92248          ; 1.0
00891ADD  fstp    g_ffxBtlOdStepCounter
00891AE3  call    sub_8AC3A0               ; returns g_ffxMesWinFontMode
00891AE8  fld     g_ffxBtlOdTimeBudget
00891AEE  fld     g_ffxBtlOdStepCounter
00891AF4  test    eax, eax
00891AF6  jnz     short loc_891B00
00891AF8  fdiv    ds:dbl_B922C8            ; 30.0
00891AFE  jmp     short loc_891B06
00891B00  fdiv    ds:dbl_B54C88            ; 25.0
00891B06  fsubp   st(1), st
00891B08  fstp    g_ffxBtlOdTimeRemaining
00891B0E  fldz
00891B10  fcom    g_ffxBtlOdTimeRemaining
00891B18  test    ah, 1
00891B1B  jnz     short loc_891B2D
00891B1D  fst     g_ffxBtlOdTimeRemaining  ; clamp to 0
00891B23  fstp    g_ffxBtlOdTimeDisplay
00891B2C  retn
00891B2D  call    FFX_Btl_Rand             ; <- see section 5
```

I read the four FP constants out of the bytes: `qword_B92248` = 1.0, `dbl_B922C8` = 30.0,
`dbl_B54C88` = 25.0, and the two in the RNG tail, `dbl_B5F050` = -10.0 and `dbl_B922E0` = 100.0.

So `timeRemaining = budget - stepCount / fps`, with `fps` 25 or 30. There is **no wall clock here at
all**. Every one of the three native minigames and the reels DLLs test only
`g_ffxBtlOdTimeRemaining`, so the overdrive time limit in FFX HD is a sub-step count.

### 3.2 The divisor is the language, which means it is always 30

This looked like the cheapest desync in the whole family, so I followed it all the way down. It is
not a desync. Here is the chain, every link verified.

`FFX_MesWin_GetFontMode 0x4AC3A0` (was `sub_8AC3A0`) is two instructions, `mov eax,
g_ffxMesWinFontMode; retn`. `g_ffxMesWinFontMode` is at VA 0x1865F00, RVA 0x1465F00, and an xref
enumeration gives it **exactly two users**: that reader, and
`FFX_MesWin_SelectFontForLanguage 0x4AD900` (was `sub_8AD900`), which writes it at five sites.

That writer switches on `FFX_MesWin_GetTextLanguage 0x487C30`, which tail-jumps to 0x487D00 =
`movsx eax, g_ffxMesWinTextLanguage`, a signed byte at RVA 0xF30830. The switch has 19 cases through
the 19-byte index table at 0x4ADA18 and the 8-entry jump table at 0x4AD9F8, both read with
`get_original_byte` and `get_original_dword`:

| Language id | Font mode | Divisor |
|---|---|---|
| 0 | 0 (all three sub-variant paths store 0) | 30 |
| 1 | 0 | 30 |
| 2, 3, 4, 5 | 1 | 25 |
| 6 to 17 | the default case at 0x4AD9F5 is a bare `retn`, so it is **left unchanged** | 30 |
| 18 | 1 | 25 |

The language ids come from the data-folder switch in `sub_88C860`, which maps the same index onto
`jppc`, `uspc`, `frpc`, `sppc`, `depc`, `itpc`, `krpc` and `chpc` for cases 0, default, 2, 3, 4, 5,
9 and 10. So font mode 1 is exactly French, Spanish, German and Italian, and **25 against 30 is the
PS2 region field rate, PAL against NTSC**, carried straight into the HD remaster. Japanese, US
English, Korean and Chinese are the 30 group.

Now the part that defuses it. `FFX_MesWin_SelectFontForLanguage` has **one caller**,
`TOInit 0x4AD4D0` at 0x4AD4EB. `TOInit` has **one caller**, `tklib__f88D710 0x48D710`. That has
**one caller**, `FFX_MainInit 0x420860` at 0x420929. So the font mode is computed once, during
process init, and never recomputed.

And at that moment its input is zero. `g_ffxMesWinTextLanguage` RVA 0xF30830 sits in the
uninitialized tail of `.data`, which `ida_bytes.is_loaded` confirms (it returns False, and the whole
region reads back as IDA's 0xFF filler), so the loader zero-fills it. Its only writers are
`FFX_AtelSys_Save_093_resi 0x478470`, an Atel script opcode that pops an int off the script VM, plus
two debug console paths in `sub_7CE450` and `SG_DebugWin_BattleConsoleProc`. All three run long
after `FFX_MainInit`, and none of them re-runs `FFX_MesWin_SelectFontForLanguage`, so even when a
script changes the text language the font mode stays where init left it.

**So `g_ffxMesWinFontMode` is 0 on every machine and the divisor is always 30.0.** The `fdiv 25.0`
at 0x491B00 is dead code in the shipped PC build, and so is the 25 branch of the message-window
typewriter. It is still worth putting the byte in the desync hash, because the cost is nothing and
the blast radius if it is ever non-zero is every overdrive minigame running 20 percent fast.

### 3.3 The budgets and the per-level tunables, from the shipped data

All read out of the binary.

| Character | Accessor | Table | Values |
|---|---|---|---|
| Auron, time budget | `sub_A5CA70 0x65CA70` | `flt_C86B60`, RVA 0x886B60 | 4.0, 4.0, 4.0, 3.0 s |
| Auron, button sequence | `sub_A5CA60 0x65CA60` | `unk_C86B70`, RVA 0x886B70, stride 32 | see 4.2 |
| Tidus, time budget | `sub_A5CBD0 0x65CBD0` | `flt_C86BF0`, RVA 0x886BF0 | 3.0, 3.0, 3.0, 2.0 s |
| Tidus, zone width | `sub_A5CB30 0x65CB30` | inline stack table | 24, 20, 20, 18 |
| Tidus, bar speed | `sub_A5CB80 0x65CB80` | inline stack table | 11, 12, 13, 14 |
| Lulu, time budget | `sub_A5CB10 0x65CB10` | `flt_B922F8`, RVA 0x7922F8 | 4.0 s, a single constant, no levels |
| Lulu, rotation table | `sub_A5CC70 0x65CC70` | ASCII digit strings | see 4.3 |

So the hardest versions are Blitz Ace at 2.0 s with a 14-per-step bar and an 18-wide zone, and
Tornado at 3.0 s with a six-button sequence.

## 4. The three native minigames, and the pad layer they read

### 4.1 Tidus, the swing bar

`FFX_BtlOd_TidusSwingBarMinigame 0x492320`. State machine on the low word of
`g_ffxBtlOdTidusState_lo 0xF3D734`, states 1 to 10. The layout around that word, from
`FFX_BtlOd_TidusMinigame_Reset 0x498180` and `_Launch 0x498CA0`:

| RVA | Meaning |
|---|---|
| 0xF3D734 | state word, 1..10 |
| 0xF3D736 | track left edge, `FFX_UiScaleX(446.0)` |
| 0xF3D738 | `FFX_UiScaleY(511.0)`, a y coordinate |
| 0xF3D73A | track width, `FFX_UiScaleX(1032.0)` minus the left edge |
| 0xF3D73C / 0xF3D73E | target zone lower and upper bound |
| 0xF3D740 | zone width (low word), live bar position (high word) |
| 0xF3D744 | signed bar velocity |

State 4 is the game:

- `FFX_Pad__readPressed16(0, 0, -1)` at 0x492392 and `(0, 0, 0)` at 0x4923A0, testing bit 0x20
  (Circle). Both reads are of pad word +6, the pure-math newly-pressed mask. The third argument is
  a pad RING LAG, so these are the previous ring sample and then the current one, not two devices.
  Section 4.4.
- Fail to state 7 when `g_ffxBtlOdTimeRemaining <= 0.0`.
- On a press, hit if the bar position is inside `[0xF3D73C, 0xF3D73E]`, else sfx 3 and the velocity
  is forced positive.
- Otherwise the bar advances by the velocity and reflects off both ends with the integer formula
  `pos = 2*edge - pos; vel = -vel`.

All 16-bit integer arithmetic. **The only thing stopping this from replicating for free is the
25-vs-30 divisor.**

State 10 calls `FFX_BtlOd_ReportMinigameResult(0, success, remaining, budget)`.

### 4.2 Auron, the button sequence

`FFX_BtlOd_AuronButtonSeqMinigame 0x490F70`. State machine on the low word of
`g_ffxBtlOdAuronState_lo 0xF3D6F4`, states 1 to 10. State 4:

```c
if (g_ffxBtlOdTimeRemaining > 0.0) {
    if (HIWORD(g_ffxMenuPadHeldTrig) != 0) {
        if (HIWORD(g_ffxMenuPadHeldTrig) == *(u16*)(g_ffxBtlOdAuronSeqPtr + 2*g_ffxBtlOdAuronSeqIndex))
            g_ffxBtlOdAuronSeqIndex++;
        else { sfx(105); g_ffxBtlOdAuronSeqIndex = 0; }
        if (g_ffxBtlOdAuronSeqIndex >= g_ffxBtlOdAuronSeqLen) state = 5;   // win
    }
} else state = 7;                                                          // lose
```

The comparison is an **equality** against the whole 16-bit pressed mask, so pressing two buttons at
once is a miss.

The four sequences, read out of `unk_C86B70` (RVA 0x886B70), 16 u16 per row, 0xFFFF terminated, with
the bit names from `INPUT_LAYER.md`:

| Level | Ability | Masks | As buttons |
|---|---|---|---|
| 0 | Shooting Star | 10 40 80 20 8000 2000 20 | Triangle, Cross, Square, Circle, Left, Right, Circle |
| 1 | Dragon Fang | 4000 8000 1000 2000 04 08 40 20 | Down, Left, Up, Right, L1, R1, Cross, Circle |
| 2 | Banishing Blade | 1000 04 4000 08 2000 8000 10 | Up, L1, Down, R1, Right, Left, Triangle |
| 3 | Tornado | 20 2000 08 8000 04 10 | Circle, Right, R1, Left, L1, Triangle |

**`HIWORD(g_ffxMenuPadHeldTrig)` is a pure-math pressed mask and I verified both writers.**

- `FFX_MesWin_SamplePadPort0 0x4B7CD0` computes it itself at 0x4B7D28: `cx = ax; cx ^= di; cx &= ax`
  where `ax` is this frame's `FFX_Pad__readButtons16` and `di` was the previous frame's low word.
  That is `held & (held ^ prev)`.
- `FFX_MenuSys_SamplePad 0x4BE500` takes it from `FFX_Pad__readPressed16` at 0x4BE5BE, pad word +6.

Neither ever stores the wall-clock auto-repeat mask into `+2`. In `FFX_MesWin_SamplePadPort0` the
auto-repeat loop at 0x4B7E04 *reads* `[edi]` (which can be `g_ffxMenuPadHeldTrig+2`) but only ever
*writes* `[esi]`, which is `g_ffxMenuPadWord10` or `g_ffxMenuPadRepeat`. So Auron's read is
deterministic given the same held mask.

**The catch is the direction bits.** Four of the eight distinct masks Auron's sequences use are
0x1000, 0x2000, 0x4000 and 0x8000, and those are not physical buttons. `FFX_MesWin_SamplePadPort0`
synthesizes them at 0x4B7D79..0x4B7DC3 by comparing the raw analog bytes against `0x18` and `0xE8`:

```
analog[1] < 0x18  -> 0x1000     analog[1] > 0xE8  -> 0x4000
analog[0] < 0x18  -> 0x8000     analog[0] > 0xE8  -> 0x2000
```

So Auron's minigame is the Jecht Shot quantisation trap again. A float round trip that lands on
0x17 for one peer and 0x18 for the other produces a hit on one and a reset on the other.

State 10 calls `FFX_BtlOd_ReportMinigameResult(2, success, remaining, budget)`.

### 4.3 Lulu, the stick rotation

`FFX_BtlOd_LuluFuryStickMinigame 0x491B80`. State machine on `g_ffxBtlOdLuluState 0xF3D708`,
states 1 to 9.

State 4 is the game, and the body **runs twice per call** (`xor esi, esi` at 0x491CD0, `inc esi; cmp
esi, 2; jl` at 0x491DF9). Pass 0 reads `FFX_Pad__readAnalogByte(0, 0, 0, -1)` then `(0, 0, 1, -1)`,
and pass 1 reads `(0, 0, 0, 0)` then `(0, 0, 1, 0)`. Argument 3 is the analog AXIS and argument 4 is
the pad RING LAG, so she samples X and Y from the previous ring entry and then X and Y from the
current one. Not two input devices. Section 4.4.

The quadrant classifier, from the disassembly at 0x491D2B:

| Code | Condition on (x, y) |
|---|---|
| 4 | `x >= 0xA0 && y < 0x60` |
| 5 | `x >= 0xA0 && y >= 0xA0` |
| 6 | `x < 0x60 && y >= 0xA0` |
| 7 | `x < 0x60 && y < 0x60` |
| 0xFF | anything else, the dead zone and the four cardinal edges |

A code other than 0xFF is pushed into the ring at `g_ffxBtlOdLuluQuadrantRing 0xF3D721`, with the
count in `g_ffxBtlOdLuluRingCount 0xF3D731` and a dedup that refuses to push the same code twice in
a row. Once the count reaches 5, the five bytes at the ring head are compared against the stack
buffers `{04 05 06 07 04 05 06 07}` and `{07 06 05 04 07 06 05 04}` at a phase taken from the ring's
first entry, so any starting corner and either rotation direction counts. A match plays `bgoodl` and
credits a rotation. Then `memmove(ring, ring+4, 12)` drops four samples and the count resets to 0.

Scoring: `g_ffxBtlOdLuluRotCount 0xF3D71E` counts rotations and is compared against
`g_ffxBtlOdLuluHitThresholdTable[g_ffxBtlOdLuluHitCount]`. `g_ffxBtlOdLuluHitCount 0xF3D71F` is the
credited hit count, capped at 16, which is exactly the maximum number of Fury casts. The gauge
`g_ffxBtlOdLuluGauge 0xF3D71C` climbs by 3 per step toward `hits * (g_ffxBtlOdLuluGaugeMax / 16)`,
with `GaugeMax` = 192 so the step is 12.

The threshold table is loaded by `sub_A5CC70 0x65CC70`, and it is a lovely bit of shipped data:
sixteen ASCII digits picked by thresholding **Lulu's magic stat** (`sub_79AE90(5)` returns
`Actor+0x5AA` for actor 5) and grouping the 19 Fury spells into 6 difficulty families through the
19-byte index table at 0x65CEC8 = `{0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,3,3,4,5}`. Examples:

```
magic <  35, spell family 0 : "1122333344444444"
magic >= 255, any family 0-2: "1111111111111111"
spell family 5 (the 19th spell), magic < 40 : "1357777777777777"
```

So hit 0 always needs 1 rotation and the later hits need more, and higher magic flattens the curve.
That matches the known FFX behaviour.

State 10 equivalent is at 0x492020, calling
`FFX_BtlOd_ReportMinigameResult(5, g_ffxBtlOdLuluHitCount, remaining, budget)`. Note the result here
is the hit COUNT, not a 0/1 success flag, which is how Lulu gets a variable number of casts.

**The pad path is fully deterministic.** The wall clock is in the keyboard path, section 9.

### 4.4 The last argument to every pad read is a RING LAG

This is the thing I would not have guessed from the Hex-Rays output, and it matters more for the mod
than anything else in sections 4.1 to 4.3.

`FFX_Pad__readPressed16 0x488E80` is five instructions. It forwards all three of its arguments to
`FFX_Pad__getReadBuffer 0x488B80` and returns `*(u16*)(buffer + 6)`.
`FFX_Pad__readAnalogByte 0x488C20` forwards arguments 1, 2 and **4** and returns
`*(u8*)(buffer + 0x0C + arg3)`, so argument 3 is the analog axis (0 is X, 1 is Y) and argument 4 is
whatever the third `getReadBuffer` parameter is.

`FFX_Pad__getReadBuffer(a, b, c)` calls `FFX_Pad__getPortState(a, b)`, whose existing IDB comment
says it returns `g_ffxPadPortState + 256 * (slot + port)`, then passes that and `c` to
`FFX_Pad__getRingSlot 0x488B60`, which is this:

```
00888B63  mov   ecx, [ebp+arg_0]          ; portState
00888B66  movzx eax, byte ptr [ecx+9Ch]   ; the write cursor
00888B6D  add   eax, [ebp+arg_4]          ; + lag
00888B70  and   eax, 3                    ; wrap to 4 entries
00888B73  shl   eax, 5                    ; * 32 bytes
00888B76  add   eax, ecx
```

So **a pad port holds a four-entry ring of 32-byte samples**, the write cursor is the byte at
portState+0x9C, and the last argument of every `FFX_Pad__read*` function selects a ring entry
relative to that cursor. Lag 0 is the sample at the cursor, lag -1 the one before it. The cursor is
advanced by `FFX_Pad__commitAllPorts`.

That explains both oddities:

- Tidus reads the pressed mask at lag -1 and then at lag 0. Two consecutive samples, one call.
- Lulu reads X and Y at lag -1 and then X and Y at lag 0. That is how a 30 Hz simulation step can
  see a stick rotation that was sampled at 60 Hz: she walks two ring entries per step, and her
  quadrant ring dedups repeats so a doubled sample costs nothing.

Neither is reading a second device, and neither read is wall-clock. But the determinism requirement
is stricter than "inject the current mask", so I went and read the writer.

### 4.5 How a ring entry is produced, and the clean injection point

`FFX_Pad__commitRingSlot 0x489790` is the only thing that writes a ring entry, and
`FFX_Pad__commitAllPorts 0x4893E0` is a three-line loop that calls it for ports 0 and 1. The body,
from the disassembly:

```
old = getRingSlot(ps, 0)              ; the entry that is about to become lag -1
mode = ps[0x81]
ps[0x9C] = (ps[0x9C] + 1) & 3         ; <- the cursor advance, 0x4897CA
new = getRingSlot(ps, 0)              ; the entry everything below fills
new[0x00] = ps[0x80]
new[0x01] = ps[0x82]
if (mode == 1) {
    held      = ps[0x98]
    new[0x02] = new[0x04] = held
    new[0x06] = (old[0x02] ^ held) & held        ; PRESSED
    new[0x08] = (old[0x02] ^ held) & old[0x02]   ; RELEASED
    for (i = 0; i < 16; i++) new[0x0C + i] = ps[0x84 + i]   ; the analog bytes
    new[0x1E] = 0
    new[0x0A] = FFX_Pad__stepAutoRepeatAllGroups(ps, new, held, old[0x02], 0)
    new[0x1C] = FFX_Pad__stepAutoRepeatAllGroups(ps, new, ..., old[0x1E], 1)
} else if (mode == 2) {
    memcpy(new, old, 32)              ; rep movsd, ecx = 8
    new[0x06] = new[0x08] = 0
    new[0x0A] = 0
}
if (ps[0x9E] == 0) FFX_Pad__clearRingSlot(new)
```

So a ring entry is 32 bytes laid out as:

| Offset | Meaning | Deterministic |
|---|---|---|
| +0x00, +0x01 | port status bytes from `ps+0x80` and `ps+0x82` | yes |
| +0x02, +0x04 | the raw HELD mask, copied from `ps+0x98` | yes |
| +0x06 | PRESSED, `(prevHeld ^ held) & held` | yes, pure math |
| +0x08 | RELEASED, `(prevHeld ^ held) & prevHeld` | yes, pure math |
| +0x0A | auto-repeat word 10 | **NO, wall clock** |
| +0x0C .. +0x1B | the 16 analog bytes, copied from `ps+0x84` | yes |
| +0x1C | auto-repeat word 28 | **NO, wall clock** |
| +0x1E | held snapshot, used as `prevHeld` on the next commit | yes |

**That gives the mod a much better injection point than the ring.** Everything except +0x0A and
+0x1C is derived at commit time from the port's STAGING fields, `ps[0x98]` (held) and
`ps[0x84..0x93]` (the analog bytes, also readable through
`FFX_Pad__readStagingAnalogByte 0x488C00`), plus the previous ring entry. Write the staging fields
and the engine computes the pressed and released masks itself, bit for bit, with no clock involved.
Two peers fed the same staging bytes for the same number of commits get identical +2, +4, +6, +8 and
+0x0C blocks.

What still has to match is the **number of commits**, and that I was able to settle statically.
`FFX_Pad__commitAllPorts` has exactly three callers:

- `FFX_MainStep 0x420BA3` and `FFX_StepPacing 0x42221F`. These are the two arms of one if/else at
  0x420B90 (`cmp g_ffxMenuSysRunningAlt, 0` / `jnz`), and `FFX_StepPacing` calls it in turn, so
  between them it runs **exactly once per frame**. Both sit at 0x420B84..0x420BA8, which is
  **before** the sub-step loop that starts at 0x420FFA.
- `FFX_BtlMenu_Step 0x49AE65`, inside the block gated on `g_ffxThreadedPadMode`, so **once per
  sub-step** while that flag is set.

In this family only Lulu's minigame sets that flag (section 9.4). So the ring advances once per
frame during Tidus's and Auron's minigames, and once per frame plus once per sub-step during Lulu's.
Which means a mod that writes the staging fields once per frame is correct for Tidus and Auron, and
has to write them per sub-step for Lulu.

## 5. RNG

**In the whole battle-menu and overdrive module (RVA 0x490000..0x4B0000) there is exactly one RNG
call site.** I enumerated the callers of `FFX_Rand_Stream 0x3988F0` (30 callers, none in that range),
`FFX_Btl_Rand 0x3989A0` (4 callers, one in range) and `FFX_Btl_RandFloat1to2 0x398930` (no callers at
all), using code cross references to the function entry rather than a byte scan.

The one site is `FFX_BtlOd_StepSharedTimer` at 0x491B30:

```c
if (g_ffxBtlOdTimeRemaining > 0.0)
    g_ffxBtlOdTimeDisplay = (float)(FFX_Btl_Rand() % 10 - 10 * (int)(g_ffxBtlOdTimeRemaining * -10.0)) / 100.0f;
else
    g_ffxBtlOdTimeDisplay = 0.0f;
```

`g_ffxBtlOdTimeDisplay` is read only by `FFX_BtlOd_DrawTimerHud 0x4955E0`. So the draw is cosmetic:
the HUD shows the remaining time truncated to tenths with a **random hundredths digit**, which is
why the countdown looks like it is ticking faster than it is.

The draw itself is not cosmetic. `g_ffxBattleRandState 0x842200` advances once per sub-step for the
whole duration of any overdrive minigame, and `MEMORY.md` already notes that battle RNG moves from
effect playback too. Two consequences:

- If one peer enters a minigame and the other does not, or if they are in it for a different number
  of sub-steps, the battle RNG stream diverges and the next damage roll differs.
- Because the draw is cosmetic, removing it is safe. Patching 0x491B2D..0x491B6A to skip the
  `FFX_Btl_Rand` and store `g_ffxBtlOdTimeRemaining` into `g_ffxBtlOdTimeDisplay` directly would
  cost nothing but a steadier HUD digit.

The reels DLLs each have one `FFX_Btl_Rand` site (host 324), which is presumably the reel symbol
roll. That one is not cosmetic.

## 6. Where the result goes, and whether it is per-character

### 6.1 Per-character, with a hardcoded actor

`FFX_BtlOd_ReportMinigameResult 0x3B0470`:

```c
Actor = FFX_Battle_GetActor(a1);
if (*(u8*)(Actor + 0xD24) == 0) return 0;     // nothing pending
*(float*)(Actor + 0xD2C) = timeRemaining;
*(u8*)(Actor + 0xD24) = 0;                    // clear the pending flag
*(float*)(Actor + 0xD30) = timeBudget;
*(u32*)(Actor + 0xD28) = result;
sub_7AFE00(a1, Actor, result);
sub_798000(0, a1, 0, 136, 0xFFFF, 0, 1, 0, 0);
```

So the result is in the battle actor record. Three callers, each with a hardcoded actor index: Tidus
passes 0, Auron 2, Lulu 5. Those are the canonical FFX character ids, and each character has exactly
one minigame, so the hardcoding is not ambiguous, but it does mean the actor index is baked into the
code rather than passed in.

Wakka's reels write through a pointer instead. `FFX_BtlOd_WakkaReels_GetStrip 0x3B1910` (host 645)
copies the symbol strip for one reel out of the static block at Actor+0xD80 into the live buffer at
**Actor+0xD40** (31 bytes, 0xFF padded) and returns a pointer to it, which the DLL then writes the
chosen symbols into. Also per-character.

### 6.2 The live state is NOT per-character

The shared block, all single slots:

| RVA | Name | Type | Role |
|---|---|---|---|
| 0xF3F77C | `g_ffxBtlOdMinigamePhase` | dword | 0 idle, 1 armed, 2 running |
| 0xF3F784 | `dword_133F784` | dword | zeroed with the block, purpose not settled |
| 0xF3F788 | `g_ffxBtlOdStepCounter` | float | +1.0 per sub-step |
| 0xF3F78C | `g_ffxBtlOdTimeBudget` | float | seconds |
| 0xF3F790 | `g_ffxBtlOdTimeRemaining` | float | the only timeout test |
| 0xF3F794 | `g_ffxBtlOdTimeDisplay` | float | cosmetic |
| 0xF3F780 / 0xF3F782 | `word_133F780/782` | word | HUD anchor, host 658 |

Plus the per-character state blocks, also single slots: `g_ffxBtlOdAuronState_lo 0xF3D6F4` and its
sequence pointer trio, `g_ffxBtlOdTidusState_lo 0xF3D734` and its geometry, `g_ffxBtlOdLuluState
0xF3D708` and the ring, and `g_ffxBtlOdWakkaReelsActive 0xF3C93F`.

**So two players cannot be inside an overdrive minigame at the same time.** There is one phase, one
step counter, one budget. If the mod ever lets two characters act concurrently this is a hard
blocker: starting a second minigame calls the second character's `_Launch`, which overwrites the
shared block and resets the first one's clock.

The cheanest reading of that constraint for the mod is that overdrive minigames have to be
serialised, which they already are by FFX's own CTB.

`FFX_BtlMenu_DrawRoot 0x49B360` has a convenient predicate for "a minigame is on screen":
`g_ffxBtlOdAuronState_lo || g_ffxBtlOdLuluState || g_ffxBtlOdTidusState_lo ||
g_ffxBtlOdWakkaReelsActive`.

## 7. Wakka's reels

### 7.1 The exe only provides scaffolding

`FFX_BtlOd_WakkaReels_Begin 0x498DC0` (host 656) starts the `bwakk` cue and sets
`g_ffxBtlOdWakkaReelsActive 0xF3C93F`. `FFX_BtlOd_WakkaReels_End 0x498230` (host 655) clears it.
`FFX_BtlOd_WakkaReels_Draw 0x4974A0` (host 650) renders a 256x256 overlay and centres message 12307
while the cue is in state 3. `FFX_BtlOd_WakkaReels_IsCuePlaying 0x4977C0` (host 652) is the sound
poll. That is the whole of it. There is no reel spin, no stop logic and no symbol match anywhere in
`FFX.exe`.

`FFX_BtlOd_WakkaReels_GetLevel 0x3B19F0` (host 646) is the clinching evidence for the ability ids:
it maps `Actor+0xF5C` 0x3074 -> 0, 0x3075 -> 1, 0x3076 -> 2, 0x3077 -> 3, which is exactly the four
rows the shipped data gives kinds 0x24/0x34/0x44/0x54 and the names Element / Attack / Status /
Aurochs Reels.

### 7.2 The logic is in 10 magic DLLs

`magic_0565`, `magic_0644`, `magic_0691`, `magic_0692`, `magic_0693`, `magic_0694`, `magic_0695`,
`magic_0696`, `magic_0697`, `magic_0698`. Every one of the ten calls the same thirteen host slots
(645, 646, 647, 648, 649, 650, 651 twice, 652, 653, 654, 655, 656, 657, 659, 660, 661, 662) and all
ten appear byte-identical in the regions I inspected: the three pad reads sit at exactly
`.text+0x9369`, `.text+0x937E` and `.text+0x9393` in both `magic_0565.dll` and `magic_0691.dll`, and
the host 645/646/647/651 sites are at the same offsets too. So the ten are the same code with
different effect data.

The reels also set their own time budget through host 657 (`FFX_BtlOd_SetTimeBudget`) and read it
back through host 651 (`FFX_BtlOd_GetTimeRemaining`, twice), so the reels clock is the same
step-counted clock as the native minigames, with the same always-30 divisor.

### 7.3 And they read the wall-clock auto-repeat mask

The three consecutive host pad reads in `magic_0691.dll`, hand-read from the bytes:

```
.text+0x9365  a1 90ce1410        mov eax, ds:1014CE90h      ; host table pointer
.text+0x936A  56                 push esi
.text+0x936B  8b 80 b8040000     mov eax, [eax+4B8h]        ; host[302] readButtons16   (+4 HELD)
.text+0x9371  6a 00 57 ff ..     push 0; push edi; call eax
.text+0x937A  a1 90ce1410        mov eax, ds:1014CE90h
.text+0x9380  8b 80 bc040000     mov eax, [eax+4BCh]        ; host[303] readWord10       (+10 AUTO-REPEAT)
.text+0x938F  a1 90ce1410        mov eax, ds:1014CE90h
.text+0x9395  8b 80 c0040000     mov eax, [eax+4C0h]        ; host[304] readPressed16    (+6 PRESSED)
```

`FFX_Pad__readWord10 0x488E50` reads pad word +10, which `FFX_Pad__stepAutoRepeatMask 0x489980`
writes from a wall clock, at the call site `0x489989`. So the reels **sample** a frame-rate-dependent
input source. They do not act on it, which 7.4 settles from the DLL bytes. The reels
module is the only part of the overdrive family in which that mask is read at all: inside `FFX.exe`
the only callers of `readWord10` are `FFX_Atel__samplePadsBothPorts 0x471D70`,
`maybe_FFX_Atel__debugScreen 0x47BD90` and `FFX_MenuSys_SamplePad 0x4BE500`, and `readWord28` has a
single caller, `FFX_Atel__samplePadsBothPorts`. No overdrive minigame in the exe reads either.

### 7.4 SETTLED, 2026-10-04: the reels throw the auto-repeat word away

This section used to ask which of the three pad words gates a reel stop, and said the reels "do NOT
replicate as is" on the assumption it was the wall-clock one. It is not. **Host slot 303 is read and
never used.**

The DLL sampler does not read into locals, it reads into three DLL globals, one per word, per port:

    .text+93B3   mov [10337A70], ax     ; host 302, held
    .text+93BE   mov [10337A74], ax     ; host 303, auto-repeat    <-- wall clock
    .text+93C6   mov [10337A78], ax     ; host 304, newly pressed

Searching the whole `.text` of `magic_0691.dll` for each of those three addresses as a 4-byte
literal gives the answer outright:

| global | word | references in .text |
|---|---|---|
| 0x10337A70 | held, host 302 | 1, its own store at 0x93B3 |
| 0x10337A74 | auto-repeat, host 303 | **1, its own store at 0x93BE** |
| 0x10337A78 | pressed, host 304 | 2, the store at 0x93C6 and a read at 0xA81 |

There is no `.data` pointer to any of them and every reference to the neighbouring 0x10337A6C is
absolute, so no indexed access can reach +0x74 either. The auto-repeat word is write-only.

The one read is the reel stop, in the reel state machine at `.text+0x9F0`:

    .text+0A7E  0f b7 1d 787a3310    movzx ebx, word ptr [10337A78]   ; PRESSED, port 0
    .text+0A85  8b 80 2c0a0000       mov   eax, [eax+0A2Ch]           ; host 651, GetTimeRemaining
    .text+0A8B  ff d0                call  eax
    .text+0A90  f6 c3 20             test  bl, 20h                   ; the reel stop
    .text+0A93  75 11                jne   stop

Bit 0x20 is the same confirm bit `FFX_BtlOd_TidusSwingBarMinigame 0x492320` tests on
`readPressed16`, so it is the same word, the same bit and the same shape as the exe's own minigame.
The reels also read **port 0 only**: 0x10337A72/76/7A are written and never read.

So the reel stop depends on ring +6 newly-pressed, which `commitRingSlot` derives as
`v4 & (v4 ^ prev)`, pure integer math, and on `g_ffxBtlOdTimeRemaining`, which is a sub-step counter
over 30.0. **Both replicate. No clock patch site is needed for the reels and the DLL does not need
patching either.** Verified independently against the shipped DLL bytes, not just from the IDB.

The same picture holds in all 10 reels DLLs. The other variant uses 0x1014A7F0/F4/F8 with its single
read at `.text+0x5C11`.

Why the DLL samples held and auto-repeat at all is not answerable from the bytes. Almost certainly
PS2-era leftovers.

## 8. The three menu overdrives, and Rikku's Mix specifically

### 8.1 Mix goes through battle menu code, not the native menu module manager

This was the explicit question and the answer is clean.

`FFX_MenuSys_PollOpenAndStep 0x420750` is the native menu module manager's entry point. Its existing
IDB comment records that `FFX_MainStep` calls it three times a frame and that it opens on Triangle
read from `g_ffxPlayerPadPressed`, and its only onward call is `FFX_MenuSys_RunFrame 0x4AB030` which
is also reached from `FFX_MenuSys_Enter 0x4BE840`. Nothing in battle calls either.

The battle menu is a separate module: `FFX_BtlMenu_Step 0x49AE20` is called only from
`FFX_Btl_MainStep 0x390C10`, inside the sub-step loop. So **the sync layer that owns Mix is battle,
which the lockstep gate on `FFX_MainStep` already covers.**

Mix is command id **0x311E**, name decoded from `command.bin` as "Mix", kind byte 0x16, page kind
(ability row byte 23) 0x14. Its 64 recipes are rows 0x308B..0x30CA with kind byte 0x26. It never
touches `FFX_BtlOd_StartMinigameForActor`, it has no minigame timer and it draws no RNG.

One correction to an inference I made earlier in this pass and then disproved: the two-selection
command that `sub_899870 0x499870` special-cases at 0x4998F0 by setting the required selection count
to 2 is id **0x3029, which decodes as "Doublecast", not Mix.** The other special case at 0x4998C0,
id 0x311E setting `dword_133C94C = 0x100`, *is* Mix, and it sets the page's byte at +0x0A to 2
rather than the selection count.

### 8.2 But the battle menu has two wall clocks of its own

`FFX_BtlMenu_Step`'s page loop walks `g_ffxBtlMenuPages 0xF3C950`, 8 slots of 240 bytes, through a
six-state machine on the byte at record+0x01:

```
state 1 -> 2
state 2 : record+0xDC = FFX_Time_AppElapsedSeconds();  record+0xE0 = 0.0
          if (record+0x94) (*record+0x94)(record);      -> 3
state 3 : if ((*record+0x98)(record) == 1)              -> 4
state 4 : record+0xE0 = FFX_Time_AppElapsedSeconds() - record+0xDC
          (*record+0x9C)(record)          ; must move the state itself
state 5 -> 6
state 6 : if ((*record+0xA0)(record) == 1) record+0xDC = 0.0, state = 0
```

The callbacks are installed by `FFX_BtlMenu_InstallPageCallbacks 0x4A0DE0`, keyed on the page kind
at record+0x06 which `FFX_BtlMenu_SetPageKindForCommand` takes from the ability row's byte 23. Record+0x90 is the draw
callback, which `FFX_BtlMenu_DrawRoot 0x49B360` runs once the state byte is >= 3.

**Eight of the nine state-4 callbacks gate their page proc on the wall-clock `elapsed`.** Example,
the one installed for page kinds 1, 2, 3, 4, 6, 0xE and 0x11:

```
008A8B26  fld     ds:flt_B3EB44        ; 0.30000001
008A8B2C  fcomp   dword ptr [ecx+0E0h] ; vs elapsed
008A8B34  jz      short loc_8A8B42     ; skip the page proc
008A8B3A  call    FFX_BtlMenu_PageProc_B
```

and for kind 0 the threshold is `dbl_B43D50` = 0.44999999. So a battle menu page ignores input for
the first 0.30 or 0.45 **real** seconds after it opens. At 60 fps that is 18 or 27 sub-steps, at
30 fps it is 9 or 13. The number of sub-steps a page is deaf for is therefore frame-rate dependent,
and a player's first press after a page opens lands on a different step index on each peer.

The ninth callback, `sub_8A87D0` for page kinds 0x15 and 0x16 (the quantity spinner, which handles
`g_ffxBtlMenuStagedGil`), reads `g_ffxMenuPadWord10` directly. Its quantity step table is the powers
of ten 1, 10, 100, ... 100000000. Watch out for the eighth entry, which IDA renders as
`(offset loc_98967E+2)`: that is the plain constant 0x989680 = 10,000,000, not an address. Exactly
the artefact the project notes warn about.

And `g_ffxMenuPadWord10` is also read by `FFX_BtlMenu_PageProc_Root` (x2), `_PageProc_B` (x7),
`_PageProc_C` (x2), `_PageProc_D` (x2), `sub_89D390`, `sub_89D610` (x7) and `sub_8A2160` (x2). That
is the cursor repeat for every battle menu, so cursor movement in Mix, Ronso Rage, Grand Summon and
the ordinary Attack / Item / Magic lists is frame-rate dependent.

`FFX_MesWin_SamplePadPort0` does not even take that mask from the pad layer: it recomputes it from
`FFX_Input__getTimeSeconds` with the hold timers `g_ffxMenuPadHoldTimer` and
`g_ffxMenuPadLastSampleTime` (16 entries each) and the thresholds `flt_B41378` = 0.13333333 s and
`dbl_B41380` = 0.23333332 s. `FFX_MenuSys_SamplePad` uses `flt_B65758` = 0.3 s and `dbl_B65760` =
0.46666664 s.

This is bigger than the overdrive family and probably belongs in `MENU_SYNC.md` or
`BATTLE_COMMAND.md`. I did not edit those. It is listed in section 10.

## 9. The Lulu correction

### 9.1 What the old note says

The IDB comment on `FFX_BtlOd_LuluFuryStickMinigame 0x491B80`, and
`MINIGAMES_TIMED.md` section 9.5, say: the function reads `FFX_Time_AppElapsedSeconds` at 0x491BB6
and 0x491BD6, its time limit `flt_133F790` is measured in real seconds, and therefore the number of
Fury casts depends on how long the overdrive took in wall time.

The two clock sites are correct. The attribution to Lulu is correct. **The claim about the time
limit is wrong.**

### 9.2 What the clock reads actually do

Disassembled at the top of the function:

```
00891BB6  call  FFX_Time_AppElapsedSeconds
00891BBB  fstp  g_ffxBtlOdLuluClockNow        ; 0xF3C920 = now, every call
00891BC1  fld   g_ffxBtlOdLuluClockMark       ; 0xF3C924
00891BC7  fldz
00891BC9  fucom st(1)
00891BCD  test  ah, 44h
00891BD0  jp    short loc_891BE9              ; mark != 0 -> keep it
00891BD6  call  FFX_Time_AppElapsedSeconds    ; mark == 0 -> latch it
00891BDB  fstp  g_ffxBtlOdLuluClockMark
...
00891BF0  fld   g_ffxBtlOdLuluClockNow
00891BF9  fsubrp st(2), st                    ; now - mark
00891BFE  fstp  [ebp+var_1C]                  ; var_1C = wall seconds since the mark
```

`test ah, 0x44; jp` jumps when the compare was ordered and not equal, so the second read fires only
when the mark is still zero. The mark is latched on the first call and cleared again in state 6 at
0x491FD2.

`flt_133F790` is `g_ffxBtlOdTimeRemaining`, and the only thing state 4 does with it is
`fcomp g_ffxBtlOdTimeRemaining` against 0.0 at 0x491CA8, dropping to state 6 on timeout. That value
comes from `FFX_BtlOd_StepSharedTimer`, which is a step counter (section 3). **The time limit is not
wall-clock.**

### 9.3 Where the clock does matter: the keyboard path

`var_1C` has exactly one consumer, the dead-zone branch at 0x491DA3, which is reached whenever the
stick is not in one of the four corner quadrants:

```
00891DA3  fld   ds:flt_B5EDC8     ; 0.5001
00891DA9  fcomp [ebp+var_1C]
00891DB1  jp    short loc_891D3D  ; less than 0.5001 s elapsed -> carry on
00891DB3  fld   g_ffxBtlOdLuluClockNow
00891DB9  mov   g_ffxBtlOdLuluKeyA, 0
00891DC3  fstp  g_ffxBtlOdLuluClockMark    ; re-mark
00891DC9  mov   g_ffxBtlOdLuluKeyB, 0
```

It re-marks and clears the two key flags. It does **not** touch the quadrant ring. So on a pad the
wall clock is read every step and changes nothing.

The key flags are set by the non-pad path. At 0x491C88 the function calls
`FFX_Input__isLastInputFromPad` and caches it in `dword_C58EF0`. Inside the loop, if that is zero it
calls `sub_645EB0 0x245EB0` instead of reading the analog bytes:

```c
bool sub_645EB0(int *a1, int *a2) {
    bool A = FFX_Input__isPressed(0x8000);
    bool B = FFX_Input__isPressed(0x2000);
    if (A && !B) { *a1 = 1; return ...; }
    if (!A && B) { *a2 = 1; return ...; }
    return B;
}
```

and the hit path at 0x491E86 requires **both** `g_ffxBtlOdLuluKeyA` and `g_ffxBtlOdLuluKeyB` to be
non-zero. So on keyboard, Lulu's Fury is "alternate two keys inside a 0.5001 wall-second window",
and that window is wall time.

This path also scores differently: at 0x491EAF it compares `(rotCount + 1 + 1) / 2` against the
threshold table, where the pad path at 0x491F1D compares `rotCount` directly. Two key alternations
per credited rotation.

**So Lulu's Fury is the one input path in the overdrive family that reads a wall clock to make a
gameplay decision, and only on keyboard.** A slower machine spends more real time per sub-step, so
it clears the key flags after fewer steps, so the player gets fewer credited rotations and fewer
Fury casts, from identical input. The old note's conclusion survives in spirit for keyboard players
and is wrong for pad players.

### 9.4 Two more Lulu-specific hazards

**Double stepping.** State 3 at 0x491C7E sets `g_ffxThreadedPadMode 0xF3C930` to 1, and state 6 at
0x491FD8 clears it. While it is set, `FFX_BtlMenu_Step` calls the Lulu minigame a second time per
sub-step (0x49AE6A), after `FFX_Input__consumeThreadedSample`, `FFX_Pad__updateAll` and
`FFX_Pad__commitAllPorts`. So Lulu's minigame samples the stick twice per sub-step and the second
sample comes from the 60 Hz input thread's queue. The existing `g_ffxThreadedPadMode` comment already
warns that in that mode the mask is popped inside animate, once per catch-up sub-step, clobbering
anything written before animate. Any input injection the mod does has to account for the Lulu
minigame flipping that mode on and off mid-battle.

**The inner double pass.** Independently of threaded mode, the state-4 body runs twice per call,
pass 0 at pad ring lag -1 and pass 1 at lag 0. Settled in section 4.4: that argument is a ring index
relative to the write cursor, not a device selector, so Lulu reads two consecutive samples of the
same stick rather than two input sources. The consequence for the mod is that the ring itself has to
be replicated, and that Lulu is the only thing in this family that makes the ring advance per
sub-step rather than per frame.

## 10. Errors found in existing notes

These are listed rather than edited, per the brief.

1. **`reversing/MINIGAMES_TIMED.md`, section 9.5, lines 801 to 806.** "its time limit `flt_133F790`
   is measured in **real seconds**. So the number of Fury casts a player gets depends on how long
   the overdrive took in wall time." Wrong. `flt_133F790` is
   `g_ffxBtlOdTimeRemaining = g_ffxBtlOdTimeBudget - g_ffxBtlOdStepCounter / (25 or 30)`, computed in
   `FFX_BtlOd_StepSharedTimer 0x491AC0`, which is a sub-step counter, and the divisor is always 30
   in the shipped build (section 3.2). The correction is that the number of Fury casts depends on
   the wall clock only through the keyboard pair-detection window, section 9.3. The two clock reads
   at 0x491BB6 and 0x491BD6 are correctly reported, they just do not do what the note says.

2. **The same section, and the IDB comment on `FFX_BtlOd_LuluFuryStickMinigame`.** "the time limit
   is `flt_133F790` and the elapsed value comes from `flt_133C920`/`flt_133C924`." The second half
   is right and the first half is not related to it. `flt_133C920`/`924` feed only the dead-zone
   idle timeout. I appended the correction to the IDB non-repeatable comment rather than overwriting
   the existing note.

3. **`reversing/MINIGAMES_TIMED.md`, section 9.1, line 727.** "17 of the 741 are RNG-reachable" and
   "83 of the 581 shipped effect DLLs call it" (the latter is in `MEMORY.md`'s
   `ffx-single-instance-globals` / `coop-architecture-goals` summary and was quoted from
   `tools/magicdll.py`). Under-counted. The old `host_calls` matcher wanted
   `mov reg,[reg+disp32]` immediately followed by `call reg` with one register throughout, which is
   blind to displacements under 0x80, to a different destination register, and to the argument
   pushes MSVC puts in between. **The settled figure for host slot 324 `FFX_Btl_Rand` is 92 DLLs and
   274 call sites**, not 83 and 224, and not the 99 and 292 this document's looser filter reported
   above, which counted 18 struct field accesses as calls. Host slot 630 is **6 DLLs and 6 sites**,
   where `host_calls` reported 0 and the looser filter reported 8.

4. **`tools/magicdll.py`, `host_calls`.** Same issue as 3, stated as a tool bug. **Fixed.** The
   byte-pattern matcher is gone. The tool now carries a length-only x86 decoder and `host_calls`
   sweeps `.text` carrying a small abstract register state, recording a call only when the callee
   traces back to a load of the DLL's own stored table pointer global. It also reports every call
   site's address, so a figure can be hand checked. It was validated against IDA Pro on three DLLs
   with exact agreement on every site. `reversing/MAGIC_DLL.md` section 7 has the before and after
   figures.

5. **The IDB function comment cap really is 1024 bytes and `ida_funcs.set_func_cmt` silently
   truncates there.** I hit it: a 1,462-character comment on `FFX_BtlOd_MaybeStartMinigame` read back
   as exactly 1024 with the tail cut mid-word. Worth noting that the pre-existing 2,271-character
   repeatable comment on `FFX_BtlOd_LuluFuryStickMinigame` survives intact and reads back whole, so
   whatever wrote that one was not going through the same path. Keep new comments under about 950 and
   split across the two slots, which is what the brief already says.

6. **The existing IDB repeatable comment on `FFX_Pad__commitRingSlot 0x489790`** ends with
   "Reached from `FFX_MainStep` via `FFX_Pad__commitAllPorts`." That is true but incomplete, and the
   omission is the part that matters for lockstep. `FFX_Pad__commitAllPorts` has three callers, and
   the third is `FFX_BtlMenu_Step` at 0x49AE65 inside the `g_ffxThreadedPadMode` block, which runs
   once per SUB-STEP rather than once per frame. I left the comment intact and added the cadence as
   an item comment at 0x4893F4 and a function comment on `FFX_Pad__commitAllPorts`. Everything else
   in that comment checks out, including the `v4 & (v4 ^ prev)` derivation, which I re-read at
   0x489846.

7. **`reversing/MINIGAMES_TIMED.md`, section 10, line 1007.** The address-table row for
   `FFX_Time_AppElapsedSeconds 0x241410` says "Only gameplay consumer is the Lulu Fury overdrive".
   Not true. An xref enumeration on the function entry also finds `sub_491480` reading it at
   0x491565, `FFX_BtlMenu_Step`'s page state machine reading it twice at 0x49AEF6 and 0x49AF39 (the
   page `startTime` stamp and the `elapsed` compute, section 8.2), and the draw function
   `sub_492910`. The battle menu pair is the important one, because it is what puts a wall-clock
   lockout in front of every battle menu page in the game, Mix and Ronso Rage and Grand Summon
   included.

8. **Not an error, a sharpening.** `MINIGAMES_TIMED.md` section 9.5 says
   "`FFX_BtlMenu_Step`'s own widget array at `0xF3C950` (stride 240, 8 slots) carries a `startTime`
   at `+0xDC` and an `elapsed` at `+0xE0` that are wall-clock seconds". Correct, and the consequence
   is larger than the note implies: those two fields gate the page proc of eight of the nine page
   kinds behind a 0.30 s or 0.45 s real-time lockout, which makes every battle menu in the game
   frame-rate dependent. Section 8.2.

## 11. Addresses to promote

RVAs. Promote these into `loader\workshop\include\ffx\addresses\` yourself.

### Entry, gate and result

| RVA | Type | Name |
|---|---|---|
| 0x3AFC90 | fn | `FFX_BtlOd_MaybeStartMinigame` |
| 0x3AFD08 | tbl | the gate's 33-byte kind index table |
| 0x3AFD00 | tbl | the gate's 2-entry jump table |
| 0x3AFD30 | fn | `FFX_BtlOd_StartMinigameForActor`, magic host API 630 |
| 0x3AFDE0 | fn | `FFX_BtlOd_IsMinigamePending`, host 629 |
| 0x3B0470 | fn | `FFX_BtlOd_ReportMinigameResult` |
| 0x388480 | fn | `sub_788480`, the action phase machine, calls the gate at 0x388691 |

### The shared timer

| RVA | Type | Name |
|---|---|---|
| 0x491AC0 | fn | `FFX_BtlOd_StepSharedTimer` |
| 0x491AF6 | site | the `jnz` that picks 25 vs 30. **Patch target** |
| 0x491B2D | site | the `FFX_Btl_Rand` branch head. **Patch target** |
| 0x497F00 | fn | `FFX_BtlOd_ResetSharedTimer`, host 654 |
| 0x497780 | fn | `FFX_BtlOd_GetTimeRemaining`, host 651 |
| 0x49A2A0 | fn | `FFX_BtlOd_SetTimeBudget`, host 657 |
| 0x4AC3A0 | fn | `FFX_MesWin_GetFontMode`, returns `g_ffxMesWinFontMode` |
| 0x1465F00 | glob | `g_ffxMesWinFontMode`. Picks the 25-vs-30 divisor. **Always 0 in practice**, hash it as a cheap guard |
| 0xF30830 | glob | `g_ffxMesWinTextLanguage`, signed byte, the font mode's only input. Zero-init BSS |
| 0xF30833 | glob | `g_ffxMesWinTextLangVariant`, byte, the language-0 sub-variant |
| 0x1465B5C | glob | `g_ffxMesWinFontId`, dword |
| 0x4AD900 | fn | `FFX_MesWin_SelectFontForLanguage`, the only writer, init only |
| 0x4AD4D0 | fn | `TOInit`, its only caller |
| 0x487C30 | fn | `FFX_MesWin_GetTextLanguage` |
| 0x487CA0 | fn | `FFX_MesWin_SetTextLanguage` |
| 0x478470 | fn | `FFX_AtelSys_Save_093_resi`, the Atel opcode that can set the text language at runtime |
| 0xF3F77C | glob | `g_ffxBtlOdMinigamePhase`, dword |
| 0xF3F780 | glob | `word_133F780`, HUD x |
| 0xF3F782 | glob | `word_133F782`, HUD y |
| 0xF3F784 | glob | `dword_133F784`, purpose unsettled |
| 0xF3F788 | glob | `g_ffxBtlOdStepCounter`, float |
| 0xF3F78C | glob | `g_ffxBtlOdTimeBudget`, float |
| 0xF3F790 | glob | `g_ffxBtlOdTimeRemaining`, float |
| 0xF3F794 | glob | `g_ffxBtlOdTimeDisplay`, float, cosmetic |
| 0x792248 | const | 1.0, the per-sub-step increment |
| 0x7922C8 | const | 30.0 |
| 0x754C88 | const | 25.0 |
| 0x75F050 | const | -10.0 |
| 0x7922E0 | const | 100.0 |

### Per-character state

| RVA | Type | Name |
|---|---|---|
| 0xF3D6F2 | glob | `g_ffxBtlOdAuronSuccess`, byte |
| 0xF3D6F4 | glob | `g_ffxBtlOdAuronState_lo`, word (high word is a HUD anchor) |
| 0xF3D6FC | glob | `g_ffxBtlOdAuronSeqIndex` |
| 0xF3D700 | glob | `g_ffxBtlOdAuronSeqLen` |
| 0xF3D704 | glob | `g_ffxBtlOdAuronSeqPtr` |
| 0xF3D6F3 | glob | `g_ffxBtlOdTidusSuccess`, byte |
| 0xF3D734 | glob | `g_ffxBtlOdTidusState_lo`, word |
| 0xF3D736 | glob | Tidus track left edge, word |
| 0xF3D73A | glob | Tidus track width, word |
| 0xF3D73C / 0xF3D73E | glob | Tidus target zone low / high |
| 0xF3D740 | glob | zone width (low word), bar position (high word) |
| 0xF3D744 | glob | `g_ffxBtlOdTidusBarVel`, signed word |
| 0xF3D708 | glob | `g_ffxBtlOdLuluState`, word |
| 0xF3D70E | glob | `g_ffxBtlOdLuluGaugeMax`, word, 192 |
| 0xF3D71C | glob | `g_ffxBtlOdLuluGauge`, word |
| 0xF3D71E | glob | `g_ffxBtlOdLuluRotCount`, byte |
| 0xF3D71F | glob | `g_ffxBtlOdLuluHitCount`, byte, capped at 16 |
| 0xF3D721 | glob | `g_ffxBtlOdLuluQuadrantRing`, 16 bytes |
| 0xF3D731 | glob | `g_ffxBtlOdLuluRingCount`, byte |
| 0xF3C920 | glob | `g_ffxBtlOdLuluClockNow`, float, WALL CLOCK |
| 0xF3C924 | glob | `g_ffxBtlOdLuluClockMark`, float, WALL CLOCK |
| 0xF3C928 | glob | `g_ffxBtlOdLuluKeyA`, dword |
| 0xF3C92C | glob | `g_ffxBtlOdLuluKeyB`, dword |
| 0xF3F798 | tbl | `g_ffxBtlOdLuluHitThresholdTable`, 16 bytes |
| 0xF3C93F | glob | `g_ffxBtlOdWakkaReelsActive`, byte |

### Battle actor record offsets

| Offset | Type | Meaning |
|---|---|---|
| +0x0D24 | word/byte | minigame pending flag. Set by 0x3AFD44, cleared by 0x3B04A2 |
| +0x0D28 | dword | the minigame RESULT |
| +0x0D2C | float | time remaining at the end |
| +0x0D30 | float | time budget |
| +0x0D40 | 31 bytes | Wakka's live reel symbol strip |
| +0x0D80 | bytes | Wakka's static reel strip source |
| +0x0F5C | word | the actor's current ability id |
| +0x05AA | byte | Lulu's magic stat, read by `sub_79AE90 0x39AE90` |

### Per-character minigame functions

| RVA | Type | Name |
|---|---|---|
| 0x492320 | fn | `FFX_BtlOd_TidusSwingBarMinigame` |
| 0x498CA0 | fn | `FFX_BtlOd_TidusMinigame_Launch` |
| 0x498180 | fn | `FFX_BtlOd_TidusMinigame_Reset` |
| 0x497240 | fn | `FFX_BtlOd_DrawTidusBar` |
| 0x490F70 | fn | `FFX_BtlOd_AuronButtonSeqMinigame` |
| 0x498AD0 | fn | `FFX_BtlOd_AuronMinigame_Launch` |
| 0x497970 | fn | `FFX_BtlOd_AuronMinigame_Reset` |
| 0x492740 | fn | `FFX_BtlOd_DrawAuronSeq` |
| 0x491B80 | fn | `FFX_BtlOd_LuluFuryStickMinigame` |
| 0x498BF0 | fn | `FFX_BtlOd_LuluMinigame_Launch` |
| 0x497F70 | fn | `FFX_BtlOd_LuluMinigame_Reset` |
| 0x495660 | fn | `FFX_BtlOd_DrawLuluGauge` |
| 0x491DA3 | site | Lulu's dead-zone wall-clock branch. **Patch target** |
| 0x491E86 | site | Lulu's keyboard hit path |
| 0x491C7E | site | sets `g_ffxThreadedPadMode = 1` |
| 0x491FD8 | site | clears it |
| 0x245EB0 | fn | `sub_645EB0`, Lulu's two-key keyboard reader |
| 0x75EDC8 | const | 0.5001, Lulu's dead-zone window, seconds |

### Wakka's reels

| RVA | Type | Name |
|---|---|---|
| 0x498DC0 | fn | `FFX_BtlOd_WakkaReels_Begin`, host 656 |
| 0x498230 | fn | `FFX_BtlOd_WakkaReels_End`, host 655 |
| 0x490F10 | fn | `FFX_BtlOd_WakkaReels_StopCue`, host 649 |
| 0x4974A0 | fn | `FFX_BtlOd_WakkaReels_Draw`, host 650 |
| 0x4977C0 | fn | `FFX_BtlOd_WakkaReels_IsCuePlaying`, host 652 |
| 0x3B1910 | fn | `FFX_BtlOd_WakkaReels_GetStrip`, host 645 |
| 0x3B19F0 | fn | `FFX_BtlOd_WakkaReels_GetLevel`, host 646 |
| 0x3B1A70 | fn | `FFX_BtlOd_WakkaReels_PostDone`, host 647 |

### Phase and HUD helpers

| RVA | Type | Name |
|---|---|---|
| 0x490E70 | fn | `FFX_BtlOd_SetPhaseIdle`, host 648 |
| 0x49AB20 | fn | `FFX_BtlOd_SetPhaseArmed`, host 659 |
| 0x49AB50 | fn | `FFX_BtlOd_SetPhaseRunning`, host 660 |
| 0x49ABA0 | fn | `FFX_BtlOd_SetPhaseArmed_dup`, host 662 |
| 0x4977E0 | fn | `FFX_BtlOd_IsGoodCuePlaying`, host 653 |
| 0x49AB80 | fn | `FFX_BtlOd_PlayGoodCue`, host 661 |
| 0x49A2D0 | fn | `FFX_BtlOd_SetHudPos`, host 658 |
| 0x4955E0 | fn | `FFX_BtlOd_DrawTimerHud` |
| 0x491A30 | fn | `FFX_BtlOd_PressCircleGate` |
| 0x490EC0 | fn | `FFX_BtlOd_ArmPressCircleGate`, host 372 |
| 0xF3F6A8 | glob | `word_133F6A8`, the press-Circle gate state |
| 0xF3D6E0 | glob | `g_boosterAutoBattle`. **Hash this.** Forces input in two places here |

### Battle menu, because Mix / Grand Summon / Ronso Rage sit on it

| RVA | Type | Name |
|---|---|---|
| 0x49AE20 | fn | `FFX_BtlMenu_Step` |
| 0x49AECD | site | page loop head, `mov esi, offset 0x133C9E4` |
| 0x49AEF6 | site | `startTime` stamp, `FFX_Time_AppElapsedSeconds` |
| 0x49AF39 | site | `elapsed` compute, `FFX_Time_AppElapsedSeconds` |
| 0xF3C950 | tbl | `g_ffxBtlMenuPages`, 8 slots x 240 bytes |
| +0x01 | off | page state byte |
| +0x06 | off | page kind, from ability row byte 23 |
| +0x90 | off | draw callback |
| +0x94 | off | onEnter callback, null for all kinds but 0xA |
| +0x98 | off | state-3 poll callback |
| +0x9C | off | state-4 step callback |
| +0xA0 | off | state-6 exit callback |
| +0xDC | off | `startTime`, WALL CLOCK seconds |
| +0xE0 | off | `elapsed`, WALL CLOCK seconds |
| 0x4A0DE0 | fn | `FFX_BtlMenu_InstallPageCallbacks` |
| 0x4A10D0 | fn | `FFX_BtlMenu_InstallPageLayout` |
| 0x499870 | fn | `FFX_BtlMenu_SetPageKindForCommand`, sets the page kind and the Mix / Doublecast special cases |
| 0x49B360 | fn | `FFX_BtlMenu_DrawRoot` |
| 0x4A8A70 | fn | state-4 step, page kind 0, 0.45 s lockout |
| 0x4A8B20 | fn | state-4 step, kinds 1,2,3,4,6,0xE,0x11, 0.30 s lockout |
| 0x4A8B50 | fn | state-4 step, kind 5 |
| 0x4A8B90 | fn | state-4 step, kinds 7, 0xF |
| 0x4A87A0 | fn | state-4 step, kind 0xA |
| 0x4A8A40 | fn | state-4 step, kind 0xC |
| 0x4A8AF0 | fn | state-4 step, kind 0xD |
| 0x4A8AC0 | fn | state-4 step, kind 0x14 (Mix's page kind) |
| 0x4A87D0 | fn | state-4 step, kinds 0x15/0x16, reads `g_ffxMenuPadWord10` |
| 0x73EB44 | const | 0.30000001, the page input lockout |
| 0x743D50 | const | 0.44999999, the page-kind-0 lockout |

### Input

| RVA | Type | Name |
|---|---|---|
| 0x21D09D2 | glob | `g_ffxMenuPadHeldTrig`. Low word held, **high word pure-math pressed** |
| 0x21D09D6 | glob | `g_ffxMenuPadWord10`. **WALL-CLOCK auto-repeat.** Battle menu cursor |
| 0x21D09DC | glob | `g_ffxMenuPadWord10Sticky` |
| 0x21D09E2 | glob | `g_ffxMenuPadSynthHeldTrig` |
| 0x21D09E6 | glob | `g_ffxMenuPadRepeat`. WALL-CLOCK auto-repeat |
| 0x4B7CD0 | fn | `FFX_MesWin_SamplePadPort0`, the writer that runs in battle |
| 0x4B7D28 | site | where the pressed high word is computed as `held & (held ^ prev)` |
| 0x4B7D79 | site | the start of the analog-to-direction-bit synthesis, thresholds 0x18 / 0xE8 |
| 0x4BE500 | fn | `FFX_MenuSys_SamplePad` |
| 0x741378 | const | 0.13333333, MesWin auto-repeat first delay |
| 0x741380 | const | 0.23333332, MesWin auto-repeat period |
| 0x765758 | const | 0.3, MenuSys auto-repeat first delay |
| 0x765760 | const | 0.46666664, MenuSys auto-repeat period |
| 0x488B60 | fn | `FFX_Pad__getRingSlot(portState, lag)`, the 4-entry ring index. **Read this one** |
| 0x488B80 | fn | `FFX_Pad__getReadBuffer(port, slot, lag)` |
| 0x488C20 | fn | `FFX_Pad__readAnalogByte(port, slot, axis, lag)`, byte at buffer+0x0C+axis |
| 0x488D70 | fn | `FFX_Pad__readButtons16(port, slot, lag)`, word +4 held |
| 0x488E80 | fn | `FFX_Pad__readPressed16(port, slot, lag)`, word +6 pressed |
| 0x488E50 | fn | `FFX_Pad__readWord10(port, slot, lag)`, word +10, WALL-CLOCK auto-repeat |
| 0x488E30 | fn | `FFX_Pad__readWord28(port, slot, lag)`, word +28, WALL-CLOCK auto-repeat |
| 0x489980 | fn | `FFX_Pad__stepAutoRepeatMask`, the wall-clock writer of +10 and +28 |
| 0x489790 | fn | `FFX_Pad__commitRingSlot(port, slot, buf)`, the only writer of a ring entry |
| 0x4893E0 | fn | `FFX_Pad__commitAllPorts`, loops ports 0 and 1. **The cadence lives here** |
| 0x4897CA | site | the cursor advance, `ps[0x9C] = (ps[0x9C] + 1) & 3` |
| 0x489846 | site | where PRESSED and RELEASED are computed, pure math |
| 0x489867 | site | `FFX_Pad__stepAutoRepeatAllGroups`, the only wall-clock part of a commit |
| 0x488C00 | fn | `FFX_Pad__readStagingAnalogByte(port, slot, axis)` = `ps[0x84 + axis]` |
| 0x488EC0 | fn | `FFX_Pad__getPortState(port, slot)` |
| 0x420B90 | site | the if/else that makes the per-frame commit happen on exactly one arm |
| 0x420FFA | site | the top of `FFX_MainStep`'s sub-step loop, for comparison |
| 0xF30288 | glob | `g_ffxPadPortState`, 256 bytes per (slot + port) |
| +0x80, +0x82 | off | staging port status bytes |
| +0x81 | off | staging port mode. 1 builds a fresh entry, 2 copies the previous one |
| +0x84 | off | the 16 STAGING analog bytes. **Injection point** |
| +0x98 | off | the STAGING held mask. **Injection point** |
| +0x9A | off | a second staging word, feeds the word-28 auto-repeat |
| +0x9C | off | the ring write cursor, advanced by `FFX_Pad__commitAllPorts` |
| +0x9E | off | zero here makes the commit call `FFX_Pad__clearRingSlot` on the new entry |
| ring +0x00..+0x1F | off | one 32-byte ring entry, 4 per port. Layout in section 4.5 |

### Tunable data tables

| RVA | Type | Name |
|---|---|---|
| 0x886B60 | tbl | Auron time budgets, 4 floats: 4.0, 4.0, 4.0, 3.0 |
| 0x886B70 | tbl | Auron button sequences, 4 rows x 32 bytes = 16 u16, 0xFFFF terminated |
| 0x886BF0 | tbl | Tidus time budgets, 4 floats: 3.0, 3.0, 3.0, 2.0 |
| 0x65CA60 | fn | `sub_A5CA60`, Auron sequence accessor |
| 0x65CA70 | fn | `sub_A5CA70`, Auron budget accessor |
| 0x65CB10 | fn | `sub_A5CB10`, Lulu budget, returns `flt_B922F8` = 4.0 |
| 0x65CB30 | fn | `sub_A5CB30`, Tidus zone width: 24, 20, 20, 18 |
| 0x65CB80 | fn | `sub_A5CB80`, Tidus bar speed: 11, 12, 13, 14 |
| 0x65CBD0 | fn | `sub_A5CBD0`, Tidus budget accessor |
| 0x65CC70 | fn | `sub_A5CC70`, Lulu rotation-table loader, 19 cases |
| 0x65CEC8 | tbl | its 19-byte family index: 0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,3,3,4,5 |
| 0x39AE90 | fn | `sub_79AE90`, returns Actor+0x5AA (Lulu's magic stat) |
| 0x49A2F0 | fn | `sub_89A2F0`, parses the ASCII digit string into the threshold table |

### Ability ids and kind bytes

The kind byte is `(level + 2) << 4 | characterId`, at offset +0x58 of a `command.bin` row.

| Character | Actor | Ability ids | Kind bytes | Minigame |
|---|---|---|---|---|
| Tidus | 0 | 0x3060..0x3063 | 0x20, 0x30, 0x40, 0x50 | swing bar |
| Yuna | 1 | - | - | none |
| Auron | 2 | 0x3064..0x3067 | 0x22, 0x32, 0x42, 0x52 | button sequence |
| Kimahri | 3 | 0x3068..0x3073 | 0x23 only | none, a menu |
| Wakka | 4 | 0x3074..0x3077 | 0x24, 0x34, 0x44, 0x54 | reels, in a DLL |
| Lulu | 5 | 0x3078..0x308A | 0x25 only | stick rotation |
| Rikku | 6 | 0x308B..0x30CA | 0x26 only | none, a menu |
| parent commands | - | 0x3118..0x311E | 0x10..0x16 | none |

Secondary id ranges carrying the same kinds, not chased: 0x30EB..0x30FE, 0x310A..0x3112, 0x312E.

### The overdrive magic DLLs

| DLLs | What they do |
|---|---|
| 0165, 0393, 0394, 0395, 0396, 0397, 0450, 0606 | call host 630 to start a minigame and host 629 to poll it |
| 0565, 0644, 0691, 0692, 0693, 0694, 0695, 0696, 0697, 0698 | the reels: host 645/646/647, 648..662, and pad reads 302/303/304 |

## 12. What I could not settle, ranked

Four items from my first draft of this list turned out to be cheap to settle, so I settled them
rather than leave them here. They were the language divisor (now section 3.2, and the answer is that
it is always 30), the meaning of the last pad-read argument (now section 4.4, and the answer is a
ring lag), the ring commit cadence and where to inject input (now section 4.5), and whether
`FFX_BtlOd_SetPhaseArmed_dup` is a real duplicate (it is, see below). What is left is ranked
most-likely-to-bite first.

1. **Whether host slot 303 (`FFX_Pad__readWord10`, the wall-clock auto-repeat mask) is what stops a
   reel.** I proved all ten reels DLLs read it, by hand-reading the bytes at `magic_0691.dll`
   `.text+0x9380`. I did not disassemble the DLL far enough to see which of the three pad words
   feeds the stop decision. This is the single most load-bearing open question in the document: if
   the answer is yes, Wakka's reels do not replicate without a patch. The probe in 7.4 settles it
   and is cheap. **Do this first.**

2. **Whether the static commit cadence in 4.5 holds at runtime.** The call-graph answer is clean:
   `FFX_Pad__commitAllPorts` has three callers, two of which are the arms of one if/else and run
   once per frame, and the third runs once per sub-step while `g_ffxThreadedPadMode` is set. What I
   did not do is count the advances with the game running, and `FFX_StepPacing` is a pacing function
   whose internals I did not read, so it could in principle commit more than once. Falsifiable
   probe: log `portState[0x9C]` at the top and the bottom of `FFX_Btl_MainStep` through each of the
   four overdrives, and check the delta is 0 for Tidus and Auron and 1 for Lulu.

3. **Whether the effect-DLL route through host[630] is the ONLY way Auron's, Wakka's and Blitz Ace's
   minigames start.** I proved the native gate's accepted kind set from its tables, and that 8 DLLs
   reference slot 630 with a verified encoding, and I hand-read two of those sites. I did not trace
   the DLL call site's own guard, so I cannot rule out a third route from inside a DLL I did not
   read.

4. **Whether the 8 DLLs that call host 630 are the complete set.** My encoding-verified filter
   accepts any `mov r32, [r32+disp32]` whose displacement equals the slot offset, with the host
   table pointer load not required to be adjacent. I hand-verified four sites (two for slot 630, two
   for 629) and all four had the table pointer loaded two to six bytes earlier. A DLL that computed
   the slot address differently, or kept the table pointer live in a register across a basic block,
   would still be missed. The figure is a strong lower bound, not a census. The same caveat applies
   to every DLL count in section 2.4, including the 99 for `FFX_Btl_Rand`.

5. **The reels' own time budget.** All ten DLLs call host 657 `FFX_BtlOd_SetTimeBudget`, so the
   value lives in DLL code or data I did not read. Section 3.3 has the native budgets but not the
   reels ones.

6. **What `dword_133F784 0xF3F784` is.** Zeroed alongside the shared timer block by
   `FFX_BtlOd_ResetSharedTimer`, and written once in `sub_89B660` from a return value. No reader
   found. Probably harmless.

7. **Whether the 0.30 s / 0.45 s battle menu page lockout matters in practice.** It matters only if
   the mod simulates the battle menu identically on both peers rather than replicating only the
   final command. If the menu stays local and only the chosen command crosses the wire, the lockout
   is harmless. I do not know which way the project has gone on that and I did not read the design
   docs this pass. Same question covers the `g_ffxMenuPadWord10` cursor repeat.

8. **Whether the reels' per-actor strip at Actor+0xD40 is the only thing the reels write.** Host 645
   returns a pointer into the actor record and the DLL writes there. The 13 other host slots the
   reels call are all timer, sound and HUD, so probably nothing else reaches the simulation, but the
   per-DLL effect data was not examined.

9. **The secondary ability id ranges 0x30EB..0x30FE, 0x310A..0x3112 and 0x312E** carry the same kind
   bytes as the main overdrive rows. Presumably the scripted or forced-overdrive variants. I did not
   decode their names or find who issues them. If a cutscene forces an overdrive it would come
   through one of these.

10. **What the 17 battle menu page kinds mean individually.** I established the kind -> callback
    mapping in `FFX_BtlMenu_InstallPageCallbacks` and that Mix's page kind is 0x14, but I did not
    name them. That belongs in a battle-command document rather than here.

11. **Whether the keyboard credit path at 0x491E86 is reachable in all four quadrants.** On the
    keyboard branch Lulu skips both analog reads, so `var_15` and `bh` carry whatever the previous
    iteration left, and the `KeyA && KeyB` test I read sits inside the `bl = 4` quadrant branch. If
    the other three quadrant branches carry the same test then the path is always reachable, and if
    they do not then keyboard Fury only credits when the stale bytes happen to classify as quadrant
    4. Either way it is wall-clock dependent, which is the part that matters, so I stopped there.

### What I proved versus what I inferred

Proved, by reading the disassembly or the shipped data files:

- The family enumeration, and that it is complete. Three independent routes agree, section 1.1.
- The single entry point and the actor dispatch, including a whole-image scan for any second
  reference to it.
- The step-counted clock and both FP divisors, read out of the constant bytes.
- That the divisor is always 30. `g_ffxMesWinFontMode` has exactly two users, its writer is reached
  only from `FFX_MainInit`, and the writer's input byte lies in the uninitialized tail of `.data`.
- The per-sub-step `FFX_Btl_Rand` draw, and that its only consumer is a HUD draw.
- The single-slot shared timer block and the per-character result offsets.
- The kind-byte encoding `(level + 2) << 4 | characterId`, and every ability id and name.
- The native gate's exact accepted kind set, from its index and jump tables.
- Tidus's and Auron's complete input and scoring logic, including Auron's four button sequences.
- That `HIWORD(g_ffxMenuPadHeldTrig)` is pure math in both of its writers.
- That the last argument of every `FFX_Pad__read*` is a four-entry ring lag, and that Tidus and Lulu
  both read lag -1 as well as lag 0.
- The whole 32-byte ring entry layout, and that `FFX_Pad__commitRingSlot` derives +2, +4, +6, +8 and
  the 16 analog bytes from the port staging fields with no clock, leaving only +0x0A and +0x1C
  wall-clock.
- That `FFX_Pad__commitAllPorts` has exactly three callers, that two of them are the arms of one
  if/else before the sub-step loop, and that the third is gated on `g_ffxThreadedPadMode`.
- Lulu's quadrant classifier, ring match, threshold table and both scoring paths.
- That the Lulu wall clock feeds only the dead-zone key-flag reset, not the time limit.
- The second call of the Lulu minigame per sub-step under threaded pad mode.
- That no overdrive minigame inside the exe reads pad word +10 or +28.
- The battle menu page state machine and its two wall-clock lockout thresholds.
- That the reels logic is absent from the exe entirely.
- That `FFX_BtlOd_SetPhaseArmed 0x49AB20` and `_dup 0x49ABA0` are true duplicates. Both are 39
  bytes and differ only in two rel32 encodings, which resolve to the same two targets.
- That 8 DLLs call host 630, with four sites hand-read out of the bytes.

Inferred, with the basis stated:

- That host 303 being present in the reels DLLs means the reels input is frame-rate dependent.
  Presence is proved, use is not.
- That the 10 reels DLLs are the same code. The two I compared have the same host slots at the same
  `.text` offsets, but I did not do a full byte compare.
- The DLL counts for each host slot. Encoding-verified filter plus four hand checks, not an
  exhaustive disassembly of 581 files.
- That language id 0 is Japanese, 1 is US English and so on. Taken from the data-folder switch in
  `sub_88C860`, which uses the same index, not from a language name table.
- The FFX message character table. Derived from one known name, then confirmed on thirteen more.
