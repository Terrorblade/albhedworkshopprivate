# The HD Remaster boosters

Every booster the PC build of FFX.exe has, with its toggle, its state, every reader, what it changes,
whether it is simulation state, and what it writes into the save block. Written for Project Zanarkand
item 1.12.10 and for the co-op layer, which needs every peer to agree on each one.

Marks: [V] read in IDA (Hex-Rays and the disassembly) during this pass, [I] inferred, with the reason.
Addresses are VAs at the preferred base 0x400000, subtract 0x400000 for an RVA.

## 0. The short answer

The PC build has **two kinds of booster**, and the PROGRESS item's guess at the set was only half right.

1. **Five hotkey toggles**, polled once per presented frame by `FFX_Frame_UpdateBoostersAndOverlays
   0x657140`: Turbo (F1), Supercharge (F2), Enemy Encounter Override (F3), Auto Battle (F4) and Hide
   HUD (F5). These are runtime globals. None is in the save block and none is saved anywhere.
2. **Three one-shot actions** on the Esc menu's "Change Parameters" page: Get all items, Unlock all
   skills and MAX gil. These are not toggles. Each one **writes the save block permanently** the moment
   the player confirms it.

Two more toggles, Free Camera and Time Stop, still have working code, toast lines and HUD art, but the
build makes them unreachable (section 4). One more hotkey poller, action 35, has no caller at all.

**There is no Al Bhed primer booster and no sphere grid booster.** The only writer of the primer mask
is `FFX_SaveData_LearnAlBhedPrimer 0x906B30`, called from the script syscall Save 39 and the key item
path `sub_88E790`, never from booster or Esc menu code [V]. Nothing in the booster or Change Parameters
code touches the sphere grid nodes (`+0x21EC`), sphere levels or AP [V by enumeration of every booster
path below]. "Unlock all skills" sets ability bits, not grid nodes.

| booster | toggle | state | readers | effect | simulation | saved |
|---|---|---|---|---|---|---|
| Turbo | F1, action 28, mask 0x10000 | `g_boosterSpeedIndex 0xCE82B4` 0/1/2, gate `g_boosterEnabled 0xCE82BC` | `FFX_Time_ScaleDtForBooster 0x6F7340`, `FFX_Booster_IsSpeedUpActive 0x6F7330`, `FFX_Btl_GetOptionBit9UnlessFfwd 0x785210` via the latch | dt x1/x2/x4, so more steps per frame. Battle script btl:222 answers 0 | **yes**, step count and battle script control flow | no |
| Supercharge | F2, action 33, mask 0x200000 | `g_boosterInvincible 0x12FB7CC` 0/1 | `FFX_Btl_SendMenu 0x792A90`, four sites | at a unit's turn: HP, MP and overdrive gauge to max | **yes**, battle unit state | no |
| Enemy Encounter Override | F3, action 34, mask 0x400000 | `g_boosterEncounterRate 0xC421D8` 0/1/2 | `FFX_Field_StepRandomEncounter 0x780D10` at 0x780D73 | 0 none, 1 normal, 2 distance x10 and ignores No Encounters | **yes**, RNG streams 0 and 1, battle count | no |
| Auto Battle | F4, action 29, mask 0x20000 | `g_boosterAutoBattle 0x133D6E0` 0/1 | 26 reads in 19 functions, section 3.4 | forges battle menu button presses, picks Attack | **yes**, chooses battle commands | no |
| Hide HUD | F5, action 31, mask 0x80000 | byte `*g_ffxGfxCtx + 0x10DCC`, 1 = HUD shown | seven presenter functions under `FFX_Frame_RenderScene 0x668930` | hides the 2D HUD and the booster icons | no | no |
| Get all items | Esc menu, Change Parameters row 0 | none, one shot | - | inventory slots 0..111 := item 0x2000+i, count 99 | **yes**, save block | **save block** +0x3ECC, +0x40CC |
| Unlock all skills | Esc menu, Change Parameters row 1 | none, one shot | - | ORs the all-abilities rows of `ply_save.bin` into every character | **yes**, save block | **save block** +0x55CC records, +0x3D6C |
| MAX gil | Esc menu, Change Parameters row 2 | none, one shot | - | gil := 999,999,999 | **yes**, save block | **save block** +0x3D48 |
| Free Camera (unreachable) | action 30, mask 0x40000, bound to key 0 | `*g_ffxFmvState + 0` dword | `FFX_Frame_StepSuspendOverlay 0x680030` | a mouse driven render camera | no | no |
| Time Stop (unreachable) | action 32, mask 0x100000, bound to key 0 | `*g_ffxFmvState + 0x338` byte | `FFX_Frame_GetSuspendState 0x6411E0` -> `FFX_MainStep` | `FFX_MainStep` returns before any simulation | **yes**, freezes the step | no |

The display names are the LocKit's (`FFX_LOC_KIT_PS3_US.BIN`, read through `FFX_LocKit_GetLine`). The
row to name mapping is in section 5.

## 1. Where the hotkeys are read

```
FFXApplication::animate 0x42F520          (skipped whole while the Esc menu is open, MAIN_LOOP.md 6)
  FFX_MainStepLoop 0x822840                -> FFX_MainStep 0x820AE0, once per pending step
  FFX_GameTick 0x639300                    mov ecx, g_ffxGfxCtx / jmp
    FFX_Frame_UpdateBoostersAndOverlays 0x657140      once per presented frame
      event 23, 348 or 349: reset everything, return early        [V]
      FFX_Booster_UpdateSpeedHotkey 0x6F7470          Turbo, and the battle ffwd latch
      FFX_Booster_PollAutoBattleKey 0x6F7580          Auto Battle
      FFX_Booster_IsAutoBattleAllowed 0x786F50        forced off in listed battles
      FFX_Booster_PollInvincibleKey 0x6F76C0          Supercharge
      FFX_Booster_PollEncounterRateKey 0x6F75C0       Enemy Encounter Override
      FFX_Input__isPressed(0x80000)  at 0x6573D4      Hide HUD
      FFX_Input__isPressed(0x100000) at 0x657423      Time Stop (never fires)
      FFX_Frame_StepSuspendOverlay 0x680030           Free Camera (never fires), the two toasts
```

Facts that matter for anyone replicating this [V]:

- **Frame, not step.** The toggles run in `FFX_GameTick`, after all of the frame's steps, so a change
  takes effect from the first step of the next frame. At 2x or 4x one frame holds several steps.
- **Keyboard only.** `FFX_Input__buildActionMap 0x6315C0` builds entries 28 to 40 from the keyboard
  binding slots `+208..+256` (primary) and `+348..+376` (alternate) and tests them with `isKeyDown`.
  No pad entry carries a booster mask. Action 31's primary reads `+544`, the decimal for hex slip
  `INPUT_LAYER.md` 6.1 describes.
- **The edges.** Each poller clears and sets a one-frame edge global: `g_boosterAutoBattleKeyEdge
  0xCE82C8`, `g_boosterInvincibleKeyEdge 0xCE82CC`, `g_boosterEncounterKeyEdge 0xCE82D0`. Nothing else
  reads them.
- **The commit can depend on the UI.** Supercharge and Encounter Override store the new value only if
  `FFX_Toast_ShowLine 0x642930` returns non-zero, and that returns 0 when the toast object
  `g_ffxToastHud 0xCE6EA4` has no Iggy player loaded (`FFX_Toast_PlayLine 0x6E8770` tests `*this`).
  Turbo refuses the press outright unless `g_ffxIggyUi`, the toast object and `g_boosterHud` all
  exist. Auto Battle and Hide HUD commit unconditionally. So the same press can land on one machine
  and not on another. Sync values, never keypresses. The co-op `BoosterSync` already does.
- **The reset.** While `g_ffxCurrentEventId` is 23 (the title), 348 or 349, every frame runs
  `FFX_Booster_Enable 0x6F72F0` (speed index 0), Supercharge 0, Encounter 1, Auto Battle 0, HUD shown 1,
  destroys every booster icon and returns before any hotkey is polled [V]. So all five boosters start
  every game from the title at their defaults, whatever was set before.
- **Boot values.** `g_boosterEncounterRate` is in initialised `.data` with the value 1 (file offset
  0x8409D8 holds 1). The other globals are past the end of initialised data and read 0 at boot.
  `sub_657A50` (from `FFX_Boot_FirstAsyncLoad`) calls `FFX_Booster_Enable` and sets the HUD byte to 1.
  `FFX_Booster_Disable 0x6F72A0` runs only at shutdown, from `sub_659D20` <- `FFX__graphicFinalize`.

## 2. Saved or not

**No booster state is saved.** [V]

- **Not in the save block.** None of the globals is inside `g_ffxSaveData 0x112CA90 .. 0x1133350`.
  `g_boosterSpeedBattleFfwdExported 0x1133354` sits four bytes past its end, so a world transfer does
  not carry it either.
- **Not in GameSetting.ini, despite two keys.** The config has `Invincible` (key 19, cfg +0x4C) and
  `EncounterRate` (key 20, cfg +0x50, values `ER_OFF` / `ER_NORMAL` / `ER_HIGH`). The file parser fills
  them and the serializer writes them back, but:
  - no caller of `FFX_Config_SetValue 0x402A00` passes key 19 or 20. Every caller was listed: the
    immediates are 1, 2 and 9..18, and the audio callers take 5..8 from `g_escAudioRowTable` [V].
  - in every function that references `g_ffxGameSettings` the only operands with displacement 0x4C
    or 0x50 are `lea ecx, [edi+50h]` in `Phyre__PApplication__onInit` and `[ecx+4Ch]` in `sub_62E3A0`,
    and neither has the config object as its base [V].
  So the two keys are dead. A bounded scan, not a proof against an indexed read, but it agrees with
  `SETTINGS_MENU.md` section 8.
- **What is saved is the key binding.** The five hotkeys live in the FFX key binding array at cfg
  +0x60 and persist with the rest of GameSetting.ini. That is per player by nature.

## 3. The five hotkey boosters

### 3.1 Turbo, the speed booster (already covered)

Full write up in `MAIN_LOOP.md` section 5 and `SETTINGS_MENU.md` section 8. Summary [V]:

| item | value |
|---|---|
| toggle | `FFX_Booster_UpdateSpeedHotkey 0x6F7470`, mask 0x10000, cycles 0 -> 1 -> 2 -> 0, toasts 765 "x2", 766 "x4", 767 "Normal" |
| state | `g_boosterSpeedIndex 0xCE82B4`, gated by `g_boosterEnabled 0xCE82BC` (byte), edge latch `g_boosterSpeedApplied 0xCE82B8`, icon handle `g_boosterSpeedIggyIcon 0xCE82C0` |
| table | `g_boosterSpeedMultipliers 0xC38E1C = {1.0, 2.0, 4.0}` |
| reader 1 | `FFX_Time_ScaleDtForBooster 0x6F7340`, the first call in `FFX_MainStep`, result stored over the dt argument at 0x820B01. Eligibility: battle phase 1 or player bound, no script movie, not a blocked event (`FFX_Booster_IsSpeedUpBlockedEvent 0x6F6FA0`), not a pad driven magic in battle, the result screen closed, no save op, no menu texture load, no menu |
| reader 2 | `FFX_Booster_IsSpeedUpActive 0x6F7330`, read by `FFX_StepPacing_AllowCatchUp`, so the extra dt can become extra steps |
| reader 3 | the battle latch `g_boosterSpeedBattleFfwdLatch 0xCE82BD` -> `FFX_Booster_SetBattleFfwdFlag 0x785990` -> `g_boosterSpeedBattleFfwdExported 0x1133354` -> `FFX_Btl_GetOptionBit9UnlessFfwd 0x785210`, which is btl:222, read once per voiced battle line (`ATEL_BTL_LIBRARY.md` 5.4) |
| audio | `FFX_Snd_SetFastForward 0x821C60` on each applied edge, mutes the voice |
| simulation | **yes.** More steps per presented frame, and a battle script branch |

One hazard the earlier write-ups did not call out [V for the code, I for the consequence]: the
exported latch is updated in `FFX_Booster_UpdateSpeedHotkey`, which runs once per frame in
`FFX_GameTick`, from `FFX_Btl_IsBattlePendingOrActive()`. So when a battle starts with Turbo on, btl:222
keeps answering the save bit until the next frame boundary, and frame boundaries fall on different
steps on two machines. Agreeing on the speed index is not enough for btl:222 to agree step for step.
The recreation should derive the latch per step from the agreed index and the battle state.

### 3.2 Supercharge (`g_boosterInvincible`)

The LocKit calls it Supercharge (lines 660..662 and 758..760). The IDB, the kit and the earlier docs
call it Invincible, and the config key is `Invincible`. It does not make anyone invincible.

| item | value |
|---|---|
| toggle | `FFX_Booster_PollInvincibleKey 0x6F76C0`, mask 0x200000, default F2. New value `(old + 1) % 2`, committed only if toast 758 (on) or 759 (off) played. HUD icon slot 2 |
| state | `g_boosterInvincible 0x12FB7CC`, int 0/1. Writers: the toggle, and the title reset. Nothing else [V] |
| readers | four reads, all in `FFX_Btl_SendMenu 0x792A90` at 0x792C12, 0x792CA1, 0x792CDC, 0x792CFB [V] |
| effect | `FFX_Booster_InvincibleRefillUnit 0x7847C0` for the unit whose turn has come up |

`FFX_Btl_SendMenu` is the turn start, not a damage path. It calls the refill on four branches: units
with the byte at `+0x608` set, the two special state branches on `+0x606` bits 0x200 and 0x100, and the
player menu branch right after `FFX_BtlMenu_Open`. The refill itself [V]:

```c
if (!FFX_Battle_IsActive()) return;
a = FFX_Battle_GetActor(unit);
if ((a[+0x606] & 1) == 0 && a[+0x591] == 17) {       // the guard, fields not decoded here
    hp gap = a[+0x594] max - a[+0x5D0] cur ... cur = max, FFX_Btl_ShowFloatingNumber(unit, 0, ...)
    mp gap likewise on a[+0x598] / a[+0x5D4],     FFX_Btl_ShowFloatingNumber(unit, 1, ...)
    a[+0x5BC] = a[+0x5BD];                         // overdrive gauge = max
    sub_78D7F0(unit, -1);                          // recompute the HP condition bytes +0x63D/+0x63E
    if (sub_7AF320(a, 2)) sub_78CA20(unit, 2, 1);  // a reaction motion through sub_7AB360
}
```

(The dword indices in the decompile are 357/358 max and 372/373 current, which are byte offsets 0x594,
0x598, 0x5D0, 0x5D4.)

Simulation: **yes.** It rewrites battle unit HP, MP and overdrive gauge at every turn start and queues a
motion. The battle end writes HP and MP back into the save block character records, so the effect
outlives the battle [I, from `BATTLE_START_AND_END.md`'s end of battle commit, not re-read here]. It
draws no RNG itself [V for the refill body, I for `sub_7AB360`'s motion queue].

### 3.3 Enemy Encounter Override (`g_boosterEncounterRate`)

| item | value |
|---|---|
| toggle | `FFX_Booster_PollEncounterRateKey 0x6F75C0`, mask 0x400000, default F3. New value `(old + 1) % 3`, committed only if the toast played: 762 None (0), 761 Normal (1), 763 Frequent (2). Icons: slot 3 for 0, slot 4 for 2, neither for 1 |
| state | `g_boosterEncounterRate 0xC421D8`, int, boots as 1 from initialised `.data` |
| reader | exactly one, `FFX_Field_StepRandomEncounter 0x780D10` at 0x780D73 [V] |

What the reader does, in order [V]:

- The debug R1 path (`g_ffxEncForcedHook` with `g_ffxInputPadR1HeldAtPoll`) runs first and is not affected.
- **2 (Frequent):** the walked distance `a3` is multiplied by 10.0, and the local enable `v3` keeps its
  initial 1, so `g_ffxEncountersEnabled` (the No Encounters armour ability) is **ignored**.
- **1 (Normal):** `v3 = g_ffxEncountersEnabled`.
- **0 (None):** distance forced to 0.0 and `v3 = 0`, so the function returns before it accumulates.

Simulation: **yes.** It decides whether `g_ffxEncDistRemain` / `g_ffxEncDistTotal` accumulate,
whether `FFX_Rand_Stream(0)` is drawn per 10 units and `FFX_Rand_Stream(1)` on a hit, whether
`g_ffxBattlePendingKind` goes to 1, and through the battle the save block battle count `+0x3D14`. A
mismatch shifts RNG stream 0 for the rest of the session. `RANDOM_ENCOUNTER.md` has the rest.

### 3.4 Auto Battle (`g_boosterAutoBattle`)

| item | value |
|---|---|
| toggle | `FFX_Booster_PollAutoBattleKey 0x6F7580`, mask 0x20000, default F4. New value `(old + 1) % 2`, committed unconditionally, toast 895 on / 896 off, icon slot 5 |
| state | `g_boosterAutoBattle 0x133D6E0`, int 0/1 |
| writers | the toggle (0x65722D), the forced off at 0x657288, the title reset (0x6574B0), and `FFX_Booster_CancelAutoBattleInBattle 0x7871F0` (0x787207) [V] |

**It turns itself off** in three places [V]:

1. `FFX_Booster_IsAutoBattleAllowed 0x786F50`, tested every frame in the booster update. It returns 0
   (and the update clears the flag with toast 893 "Disabled") for story progress 15 and 3360 always,
   and for these (progress, battle key) pairs, where the key is `FFX_Btl_GetCurrentEncounterKey
   0x7816B0` = (the scene's battle id << 16) + formation: 55 with 0x220000 or 0x220001, 76 with
   0x390000, 0xD2 with 0x480032 or 0x480033, 0xD8 with 0x460032, 0xD9 with 0x460033, 0x104 with
   0x610000, 0x138 with 0x830032, 0x359 with 0xDE0000, 0x424 with 0xFE0000, 0x43D with 0x1050032, 0x5CD
   with 0x14C0000, 0x604 with 0x1590000, 0x7F8 with 0x18B0000, 0x8E8 with 0x196000A, 0x9E2 with
   0x1E70000, 0xC85 with 0x2470000, 0xCE4 with 0x24A0000. [I] these are the tutorial and scripted
   battles where the Attack default would soft lock.
2. `FFX_Btl_OnUnitDefeated 0x78C740` calls the cancel when an aeon (unit 8..14) with `+0x591 == 17`
   falls, and when all three Magus Sisters (15..17) are down.
3. `FFX_BtlMenu_PageProc_Root 0x89C900` calls the cancel when `FFX_BtlMenu_ClassifyCommand(owner,
   0x3000)` (Attack) is not usable.

The cancel only acts while `FFX_Btl_IsBattlePendingOrActive()`, and shows toast 893.

Readers [V]. Outside the toggle function there are 26 references in 21 functions, one of them the
cancel's own write:

| function | what auto battle does there | kind |
|---|---|---|
| `FFX_BtlMenu_PageProc_Root 0x89C900` | forces the cursor to row 0 (Attack), cancels if Attack is unusable, forges confirm (0x20), and forges cancel (0x10) when `g_ffxBtlAutoBattleRetry` is 1 | input |
| `FFX_BtlMenu_PageProc_B 0x89CDE0` | forges 0x40, arms `maybe_g_ffxBtlAutoBattleSubPageArmed 0x133D6E4` | input |
| `FFX_BtlMenu_PageProc_C 0x8A22F0`, `_D 0x8A2690` | forge 0x2000 and 0x8000 | input |
| `sub_89D390`, `sub_89D610`, `sub_8A2160` | forge 0x20 or 0x40 | input |
| `FFX_BtlOd_PressCircleGate 0x891A30` | forges 0x20 in its case 4, so overdrive press gates pass | input |
| `sub_8A87D0` | the gil quantity page, forges input, and for owner 14 (Yojimbo) stages at least 1 gil | input |
| `FFX_BtlMenu_ConfirmCommand 0x8975A0` | keeps `g_ffxBtlMenuOwnerActor` instead of resetting it to 255 | menu state |
| `FFX_BtlMenu_DrawRootSlide 0x893A30`, `sub_8A49B0`, `sub_8A7520` | skip the wall clock (`FFX_Time_AppElapsedSeconds`) slide and timer UI | menu timing |
| `sub_8936D0`, `sub_895140`, `FFX_BtlMenu_DrawRoot 0x89B360`, `sub_89C870`, `sub_8A0590`, `sub_8A32B0` | skip drawing parts of the menu | presentation |
| `FFX_Snd_PlaySeDefault 0x886BA0` | drops the default menu SE in battle | presentation |
| `FFX_Booster_CancelAutoBattleInBattle 0x7871F0` | the cancel | state |

`g_ffxBtlAutoBattleRetry 0x133D6EC` is the handshake: `FFX_Btl_ExecCommand` writes 0 at 0x7926CC and
1 at 0x7926D8 on its two branches, and the page procs read it [V for the writes, `BATTLE_COMMAND.md`
4 for the meaning, 1 when the chosen action was rejected].

Simulation: **yes.** It never writes the command record itself, it forges the menu pad word
`g_ffxMenuPadHeldTrig`, so the commands it picks are real battle commands. It also replaces wall clock
menu timing with immediate input, which is part of why a recreation can treat it as a menu policy.

### 3.5 Hide HUD

| item | value |
|---|---|
| toggle | `FFX_Input__isPressed(0x80000)` at 0x6573D4, default F5, action 31. `byte = !byte`, toast 752 "Hide HUD (On)" when it becomes 0, 753 when it becomes 1, then `FFX_Booster_SetAllIconsVisible 0x6F7940(byte)` |
| state | byte at `*g_ffxGfxCtx + 0x10DCC` (69068), 1 means the HUD is shown. Set to 1 at boot by `sub_657A50` and by the title reset |
| readers | `FFX_GfxCtx_Draw2DMeshes 0x650340`, `sub_650CB0`, `sub_650EA0`, `sub_650F80`, `FFX_Booster_IsHudShown 0x6523E0`, `sub_652A10`, `sub_653960`. All are reached only from `FFX_Frame_RenderScene 0x668930` [V by a displacement scan of `.text` for 0x10DCC] |

`FFX_Booster_IsHudShown` is `byte != 0 || g_ffxMenuSysRunning != 0`, so the HUD always draws while a
menu runs. Simulation: **no.** Safe to leave per player.

### 3.6 The HUD icons, briefly

`g_boosterHud 0xCE82E4` is a 0x40 byte object built by `FFX_Booster_HudInit 0x6F77F0` with
`FFX_Booster_HudCtor 0x6F7700`: seven Iggy movie slots at +0x00..+0x18 (0 speed_2, 1 speed_4, 2
invincible, 3 enemy_0, 4 enemy_1, 5 autobattle, 6 autobattle_disable), their swf paths at
+0x1C..+0x34, a column per slot at +0x38..+0x3E (0, 0, 1, 2, 2, 3, 3) and the visible flag at +0x3F.
`FFX_Booster_ShowIcon 0x6F7970` creates the movie at x = 53 * column + 10, y = 10, 43 x 43, and
refuses any slot >= 7. `FFX_Booster_HideIcon 0x6F7900` frees it. Toasts go through the separate
`g_ffxToastHud 0xCE6EA4` and `FFX_Toast_PlayLine 0x6E8770` ("OnPlay" with the LocKit line). All
presentation [V].

One real bug in the ctor [V for the writes, I for the harm]: it stores the freecamera path at +0x3C
(then overwrites it with the column bytes), the time_stop path at **+0x40**, and the word 0x0504 at
**+0x40**, which is four bytes past the 0x40 byte block `FFX_Alloc` handed out. `FFX_MemAlloc`
rounds a 0x40 request to exactly 0x40 of payload, so those bytes land on the next block header's
magic at +0, which the allocator never reads. That is harmless, and it is very likely one of the "lost
magic" findings `ffx::VerifyEngineHeap` reports on every boot. The two extra bytes, columns 4 and 5,
were meant for the Free Camera and Time Stop icons (slots 8 and 9), which `ShowIcon` refuses anyway.

## 4. The two unreachable toggles

Both are handled by `FFX_Frame_StepSuspendOverlay 0x680030` and the object `g_ffxFmvState 0xCCC830`
(0x340 bytes, built by `FFX_GetFmvState 0x42F260`, which despite both names is the frame suspend and
free camera object, see `CUTSCENE.md` 7.2).

- **Free Camera.** Mask 0x40000 (action 30) tested at 0x68017F toggles the dword at +0. On a change the
  step shows toast 749 / 750 and icon 8. While on it centres and clips the cursor and drives the
  camera at +0x10 from the mouse (`sub_62E3A0`, `Phyre_PCamera_UpdateViewProjection`). Forced off while
  `g_ffxFmvScriptRunning`. Render camera only, simulation **no** [I, the camera is the object's own
  `PCamera`, not the game camera state].
- **Time Stop.** Mask 0x100000 (action 32) tested at 0x657423 calls `FFX_Frame_ToggleSuspendOverlay
  0x680320`, which flips the byte at +0x338, pauses audio through `FFX_Snd_SetPausedForOverlay`, and
  snapshots the camera. The step shows toast 755 / 756, icon 9, and pauses an FMV. **While the byte is
  set `FFX_Frame_GetSuspendState` returns 2, and `FFX_MainStep` returns at 0x820B83 before
  `FFX_Pad__updateAll`, `FFX_StepPacing` or any simulation** [V]. So Time Stop freezes the step count.
  The Start-button cutscene pause shares this path through the byte at +0x33C.
- **Why neither fires.** `FFX_Input__buildActionMap 0x6315C0` stores the literal key code 0 for both
  masks, primary and alternate: `mov [eax], 0` before `mov dword ptr [eax+4], 40000h` at 0x631DD5 and
  0x6326CD, and before `... 100000h` at 0x631E52 and 0x63274A [V]. Every other PC extra loads its key
  from the settings. Key 0 is not a real key [I], and neither action has a row on the Keyboard page.
- **The leftovers.** LocKit lines 670..675 (descriptions), 749..751 and 755..757 (toasts), the
  `freecamera.swf` and `time_stop.swf` assets, and the two column bytes. One oddity: when
  `g_ffxThreadedPadMode` is set the booster update calls the toggle every frame while any suspend byte
  or the free camera is on, which would flip Time Stop back and forth. It is unreachable in retail.

Also dead [V]: `FFX_Booster_PollCutFeatureKey 0x6F7600` (mask 0x800000, action 35, no caller) and the
debug key pollers `FFX_Booster_PollDebugKeyI/J` (`DEBUG_MODE.md`). The LocKit also carries a "Get 50000
Gil" action (lines 653, 690..693, 764) and an older "Invincible & Intensify" text (681..689) that no
code references [V by an immediate scan of `.text` for message ids 649..767 and 893..896].

## 5. The Change Parameters page, the permanent boosters

The Esc menu's Change Parameters page (`FFX_EscMenu_BuildChangeParaPage 0x6FC3F0`) builds three
`spinnerButton` rows with labels 694, 701 and 708 and the help line 712. LocKit lines 710 and 711 (a
"Status MAX" row) have no builder, so a fourth row was cut. Argument 13 in the click handler is taken
to be this page's id [I, it is the only value that queues this handler].

```
escmenu.swf button press
  FFX_EscMenu_OnButtonClick 0x68F610          arg 0 == 13 -> g_escButtonClickRow 0xCCC884 = arg 1
    FFX_IggyMenu_QueueDeferred1(FFX_EscMenu_ShowGrantPopup, &g_escButtonClickRow)
FFX_EscMenu_ShowGrantPopup 0x6FC7B0
  if (FFX_Ev_IsBootSceneEvent(event) || story progress == 0 || g_ffxMenuSysRunning)
      showPopUpStd("Function not Available now!")
  else
      showPopUpWithBtn(713 "irreversible, use it?", 714 Yes, 715 No)
      FFX_EscMenu_SetPopUpCallback 0x68FF10(FFX_EscMenu_OnGrantPopupResult, &row)   -> menu +0x10C/+0x110
escmenu.swf EscMenuPopUpConfirmed
  FFX_EscMenu_OnPopUpConfirmed 0x68F8F0       calls +0x10C(button, +0x110) once and clears it
FFX_EscMenu_OnGrantPopupResult 0x6FC970(button, &row)
  if (button) switch (row):
    0: jmp FFX_Debug_FullItem 0x7849D0                                     at 0x6FC9A7
    1: FFX_Trophy_Unlock(25), FFX_Trophy_Unlock(24), jmp FFX_SaveData_GrantAllAbilities 0x7848B0   at 0x6FC98F..0x6FC9A1
    2: jmp FFX_Debug_FullGil 0x7849C0                                      at 0x6FC98A
```

All of it is [V]. The Esc menu being open means `FFXApplication::animate` skips the whole step and
`FFX_GameTick` (`MAIN_LOOP.md` 6), so these writes land between steps, with the simulation held.
Note what the gate allows: a battle is not excluded, only the boot scenes, a story progress of 0 and an
open in game menu.

### 5.1 Get all items, `FFX_Debug_FullItem 0x7849D0`

Writes the save block item arrays `g_ffxItemIds 0x113095C` (block +0x3ECC, u16[256]) and
`g_ffxItemCounts 0x1130B5C` (block +0x40CC, u8[256]). The net effect of its two loops, for i in 0..111:
**slot i := item 0x2000 + i, count 99**. On language 11 item 0x2065 becomes 0x2000 with count 10
instead, the same swap `FFX_Btl_PayKillRewards` makes. So it reorders the first 112 slots into id order
and drops whatever was in them. Slots 112..255 are untouched, key items are a different table. Then
`sub_8C8E60(0)` rebuilds the menu item list and, if `dword_1A85F70`, refreshes the open menu page.

### 5.2 Unlock all skills, `FFX_SaveData_GrantAllAbilities 0x7848B0` (was `FFX_Btl_LoadPlayerData`)

Unlocks Steam achievements 25 `ACH_LEARNING` and 24 `ACH_BLITZBALL_MASTER` first (the table at
`0xC35794`, 400 bytes per row), then:

1. Loads battle kernel asset 1 (`FFX_GetAssetLoader()[4](13)`, `[5](1)`). Kind 13 entry 1 in the boot
   path table (`cdrom.fnd`, kind 13 starts at 7798) is **`battle/kernel/ply_save.bin`**. The string
   "ply_data" at 0x7848CD is the allocation tag passed to `sub_83F0A0`, not a file name [V].
2. For each of the 18 character records except 7 (Seymour), ORs six words of `ply_save` **row 18**
   (characters 0..7) or **row 19** (8..17, the aeons) into the record's ability words. The loop starts at
   0x113209C and writes `[esi-2]..[esi+8]`, so record +0x3E..+0x49, the `ability_flags u16[6]` of
   `SAVE_CHAR_RECORD` at block +0x55CC, stride 0x94 [V]. Rows 18 and 19 of the shipped file are the
   near all ones masks (row 18 `fffb ffff fffd ffff ffff ff0f`, row 19 `ffc0 fe0f fa01 ffff fffe 3eff`),
   while rows 0..17 are each character's new game defaults (Tidus `003b 0000 ...`). Read from the
   archive, in memory only.
3. Sets bits 0..130 of the u16 array at `0x11307FC`, block +0x3D6C `shared_ability_bits` (the loop at
   0x784978, bound 0x83) [V].
4. If a battle is active and pending, calls `sub_79BB50(unit, actor)` for units 0..30, which rebuilds
   the battle command lists so the new abilities appear at once.

It does not touch sphere grid nodes, sphere levels, AP, stats or overdrive learn counters. The disk load
in step 1 is a synchronous asset loader read [I, it calls the loader's read and finish slots in line].

### 5.3 MAX gil, `FFX_Debug_FullGil 0x7849C0`

One store, `mov g_ffxGil, 3B9AC9FFh` (999,999,999) at 0x7849C0. `g_ffxGil 0x11307D8` is block
+0x3D48 [V].

### 5.4 What the Change Parameters actions write, for the hash

| action | save block bytes | other state |
|---|---|---|
| Get all items | +0x3ECC..+0x3FAB (slots 0..111 of item_ids), +0x40CC..+0x413B (counts 0..111) | the menu item list |
| Unlock all skills | +0x3D6C..+0x3D7D (bits 0..130), and +0x3E..+0x49 of records 0..6 and 8..17 at +0x55CC + 0x94 * n | Steam achievements 24 and 25, battle command lists |
| MAX gil | +0x3D48..+0x3D4B | none |

All three are simulation state and permanent: they ride in every save written afterwards.

## 6. Every message line the boosters use

| line | text (US) | used by |
|---|---|---|
| 656 | the `$key$` hint template | Special Feature page |
| 657, 658, 659 | Turbo, two descriptions | Special Feature row, action 28 |
| 660, 661, 662 | Supercharge | Special Feature row, action 33 |
| 663, 664, 665 | Enemy Encounter Override | Special Feature row, action 34 |
| 666, 667, 669 | Auto Battle | Special Feature row, action 29 |
| 676, 677, 678 | Hide HUD | Special Feature row, action 31 |
| 694, 701, 708 | Get all items, Unlock all skills, MAX gil | Change Parameters rows |
| 712, 713, 714, 715 | Get, the irreversible warning, Yes, No | Change Parameters |
| 749, 750 | Free Camera On / Off | `FFX_Frame_StepSuspendOverlay`, unreachable |
| 752, 753 | Hide HUD On / Off | Hide HUD |
| 755, 756 | Time Stop On / Off | `FFX_Frame_StepSuspendOverlay`, unreachable |
| 758, 759 | Supercharge On / Off | Supercharge |
| 761, 762, 763 | Encounters Normal / None / Frequent | Encounter Override |
| 765, 766, 767 | Turbo x2 / x4 / Normal | Turbo |
| 893, 895, 896 | Auto Battle Disabled / On / Off | Auto Battle |

The Special Feature page (`g_escSpecialFeatureTable 0xC3A2C0`, `SETTINGS_MENU.md` 3) is read only. It
lists exactly the five hotkeys, which closes that set.

## 7. Co-op and recreation hazards

1. **Turbo, Supercharge, Encounter Override and Auto Battle must agree on every peer.** Each one changes
   step count, battle unit state, RNG draws or battle commands. `pilgrimage::BoosterSync` carries the
   four values host ordered, as values. Keep it that way, because the engine's own commit depends on
   local toast state (section 1).
2. **The engine turns Auto Battle off by itself**, inside the battle step (`FFX_Btl_OnUnitDefeated`,
   `FFX_BtlMenu_PageProc_Root`) and in the per frame update (`FFX_Booster_IsAutoBattleAllowed`).
   `StepBoosterSync` sees that as a local change, requests it, and holds the global at the old agreed
   value until the host answers. Both machines do the same, so they stay in step, but for the round
   trip auto battle is forced back on in a battle the engine wanted it off in, and toast 893 can repeat
   [I from reading `BoosterSync.cpp` step 4]. Better: treat an engine side clear as agreed, for example
   by applying the same clear rules to the agreed value.
3. **btl:222 is frame latched**, section 3.1. Derive it per step in the recreation and force it from the
   agreed value in the mod.
4. **The Change Parameters actions are not synced at all.** One player confirming Get all items, Unlock
   all skills or MAX gil changes their save block and nobody else's, and the next save carries it. Either
   disable the page during a session or send the row as a host ordered command and run
   `FFX_EscMenu_OnGrantPopupResult`'s body on the agreed step on every machine. Unlock all skills also
   does a synchronous disk read and rebuilds battle command lists when used in a battle.
5. **Hide HUD is local.** Leave it per player.
6. **Time Stop would freeze one machine's step.** It is unreachable unless a mod binds action 32. If
   anything ever does, treat it like the Esc menu hold (`MAIN_LOOP.md` 6): a local pause the lockstep
   gate turns into a stall for everyone.
7. **The title resets everything every frame** (events 23, 348, 349). A session that starts on the title
   gets defaults whatever the host said, and BoosterSync will see those as local changes.

For the recreation the simulation side is: Turbo as a step multiplier, Supercharge and Encounter
Override and Auto Battle as agreed rule switches, and the three Change Parameters actions as ordered
commands that write the save block. Hide HUD, the icons and the toasts are presentation.

## 8. What this pass corrected

- `SETTINGS_MENU.md` section 8 said mask 0x40000 (action 30) is used by nothing. It is the Free
  Camera toggle in `FFX_Frame_StepSuspendOverlay`, unreachable because the action map binds key 0.
- `SETTINGS_MENU.md` section 8 and the IDB comment on `FFX_Frame_UpdateBoostersAndOverlays` said mask
  0x100000 (action 32) is "FMV skip". It is Time Stop, the frame suspend toggle, which freezes
  `FFX_MainStep`. Also unreachable.
- `SETTINGS_MENU.md` section 8 called action 31 "a toggle on FFXApp+69068". It is Hide HUD, on
  `*g_ffxGfxCtx + 0x10DCC` (the IDB comment had already fixed the owner).
- `SETTINGS_MENU.md` sections 18 and 19, the kit's `addresses/EscMenu.h` (`BoosterInvincible` comment
  and `DamagePathReader`) call 0x792A90 the damage path. It is `FFX_Btl_SendMenu`, the turn start,
  and the booster is a turn start refill, as `CHEAT_BATTLE.md` already had it. The kit header was left as it is because it is
  code, the constant's value is right and only its name and comment mislead.
- `SETTINGS_MENU.md` section 19 said the three booster globals are written in exactly one place and
  nothing overwrites them until the key is pressed again. Auto Battle has a second writer,
  `FFX_Booster_CancelAutoBattleInBattle`, plus the per frame allowed check, and the title resets all of
  them every frame. The kit header repeats the claim.
- `BATTLE_START_AND_END.md` said ability bitmaps come from `FFX_Btl_LoadPlayerData`, run from
  `FFX_Btl_Init`. `FFX_Btl_Init` calls it only when `g_ffxBtlDbgFullSet` is set (0x781C2B), and it ORs
  the all-abilities rows 18 and 19, not each character's own row. Renamed
  `FFX_SaveData_GrantAllAbilities`. The IDB comment's "ply_data" is the allocation tag, the file is
  `ply_save.bin`.
- `CHEAT_MODELS.md` called the same loop the default ability mask, and `GAME_STATE.md` said it special
  cases 15..17. It only splits 0..7 from 8..17 and skips 7. Whether `sub_79BB50` does, as
  `GAME_STATE.md` also says, was not re-read.
- `INPUT_LAYER.md` 6 said `FFX_Input__poll` takes its short path while a movie is playing. The test at
  0x632B30 is on the frame suspend object: Time Stop (+0x338), the cutscene pause (+0x33C) or Free
  Camera (+0). It is not a movie test.

## 9. Names proposed

Applied by `../tools/apply_booster_names.py` (rehearsed on a copy, not saved).

| VA | old | new |
|---|---|---|
| 0x786F50 | sub_786F50 | `FFX_Booster_IsAutoBattleAllowed` |
| 0x7871F0 | sub_7871F0 | `FFX_Booster_CancelAutoBattleInBattle` |
| 0x7816B0 | sub_7816B0 | `FFX_Btl_GetCurrentEncounterKey` |
| 0x642930 | sub_642930 | `FFX_Toast_ShowLine` |
| 0x6E8750 | sub_6E8750 | `FFX_Toast_GetHud` |
| 0x6E8770 | sub_6E8770 | `FFX_Toast_PlayLine` |
| 0x6F7770 | sub_6F7770 | `FFX_Booster_HudDtor_ORPHAN` |
| 0x6FC7B0 | sub_6FC7B0 | `FFX_EscMenu_ShowGrantPopup` |
| 0x6FC970 | sub_6FC970 | `FFX_EscMenu_OnGrantPopupResult` |
| 0x68F8F0 | sub_68F8F0 | `FFX_EscMenu_OnPopUpConfirmed` |
| 0x68FF10 | sub_68FF10 | `FFX_EscMenu_SetPopUpCallback` |
| 0x7848B0 | FFX_Btl_LoadPlayerData | `FFX_SaveData_GrantAllAbilities` |
| 0x6523E0 | FFX_Booster_IsSceneDrawEnabled | `FFX_Booster_IsHudShown` |
| 0x6F7700 | FFX_Booster_LoadSpeedIcons | `FFX_Booster_HudCtor` |
| 0x11307D8 | dword_11307D8 | `g_ffxGil` |
| 0x133D6EC | dword_133D6EC | `g_ffxBtlAutoBattleRetry` |
| 0x133D6E4 | dword_133D6E4 | `maybe_g_ffxBtlAutoBattleSubPageArmed` |
| 0xCE6EA4 | dword_CE6EA4 | `g_ffxToastHud` |
| 0xCCC884 | dword_CCC884 | `g_escButtonClickRow` |

`g_ffxFmvState` and `FFX_GetFmvState` are misnamed too (section 4) but `CUTSCENE.md` and `FMV_SYNC.md`
use them, so they are left for whoever owns those docs.

## 10. Open

- The guard fields of the Supercharge refill, `+0x591 == 17` and `+0x606` bit 0, and the `+0x608`
  branch in `FFX_Btl_SendMenu`, were not decoded.
- Which battles the auto battle block list names, by encounter, was not looked up.
- Whether key code 0 can ever read as down (it would make Free Camera and Time Stop live) was not
  tested at run time.
- The end of battle HP and MP write back that makes Supercharge stick was taken from the existing doc,
  not re-read.
