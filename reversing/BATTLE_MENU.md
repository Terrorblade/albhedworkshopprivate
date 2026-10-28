# The battle command menu and the battle_script.bin scenes

PROGRESS item 1.11.12. All addresses are VAs as IDA shows them, base 0x400000. RVA = VA - 0x400000.

Tags: **[V]** read in the binary at the address given, **[M]** measured by a scan of the binary or of
the game data, **[I]** inferred. Only [V] and [M] are proof.

Names used here are the ones `../tools/apply_battle_menu_names.py` gives. Names that were already in
the IDB (`FFX_BtlMenu_Step`, `FFX_BtlMenu_PushPage`, `FFX_BtlMenu_PageProc_Root` and so on) are kept
as they are. The command record, the queue and the commit are in `BATTLE_COMMAND.md`. The scene
format and the menu_script library are in `MENU_SCRIPT.md`. The overdrive minigames are in
`OVERDRIVES.md`.

---

## 1. Summary

- The menu is a **page stack**, 8 pages of 240 bytes at `g_ffxBtlMenuPages 0x133C950`, with one
  depth byte `g_ffxBtlMenuPageDepth 0x23CC092`. There is no single "menu state" variable. Each page has
  its own state byte at page +0x01 with 7 values (0 to 6), and a kind at page +0x06 that picks five
  callbacks. [V]
- The menu as a whole is open when `g_ffxBtlMenuOpenMask 0x23CC08C` is non-zero. [V]
- `FFX_BtlMenu_Step 0x89AE20` runs **once per sub-step, catch-up steps included**. The page draws run
  once per drawn `FFX_MainStep` only. Three things in the menu are clocked by something other than the
  step: a wall-clock open lockout (0.45 s or 0.30 s), the wall-clock cursor repeat, and a **scroll lock
  that only the draw releases**. [V]
- A chosen command leaves the menu in exactly one place, `FFX_BtlMenu_ConfirmCommand 0x8975A0`, which
  calls `FFX_Btl_CommitCommand`. The four 0x10 shortcut commits are the only other exits. [V]
- The menu is not pure presentation. The target page calls into the battle core **every step** and that
  call turns units, aims a neck and fires battle event 25 whenever the cursor mask changes. Cancel fires
  event 132. Select toggles an option bit in the save block. The first item of a Mix writes the single
  `g_ffxItemInUse`. [V]
- battle_script.bin has **34 scenes**. The battle menu starts 7 of them, the overdrive minigames start 8,
  and **19 are never started**. None of those 19 names exists as a string in FFX.exe except as the
  prefix of a longer name. [M]

---

## 2. Call chain

| caller | site | callee | rate |
|---|---|---|---|
| `FFX_Btl_BeginBattle` | 0x7814D5 | `FFX_BtlMenu_InitForBattle 0x89B660` | once per battle [V] |
| `FFX_Btl_Shutdown` | 0x7815E4 | `FFX_BtlMenu_ShutdownForBattle 0x89AD90` | once per battle [V] |
| `FFX_Btl_SendMenu 0x792A90` | 0x792CF3 | `FFX_BtlMenu_Open 0x89BB10` | when a player actor's turn comes up [V] |
| `FFX_Btl_MainStep` | 0x790D74 | `FFX_BtlMenu_Step 0x89AE20` | every call, no condition. After SendMenu (0x790D6F), before `FFX_Btl_ExecCommand` (0x790D79) [V] |
| `FFX_MesWin_DrawAllWindows 0x8ABD80` | | `FFX_BtlMenu_DrawRoot 0x89B360` | once per drawn step [V] |

`FFX_MainStep` runs its sub-step loop at 0x820FFD, sized by `FFX_Player__getSubStepCount`. Each sub-step
calls `FFX_Player__stepControl` (0x821010), `FFX_Btl_MainStep` (0x821015), `FFX_Atel_StepOnce` (0x82101A)
and `FFX_MesWin_StepAll` (0x82101F) in that order. `FFX_MesWin_StepAll` samples the menu pad block
through `FFX_MesWin_SamplePadPort0` at 0x8AB912. So the menu reads the pad sample the previous sub-step
took. [V for the order, I for the one sub-step lag being visible in play]

The draw is after the loop. `cmp g_ffxMesWinDrawEnabled, 0` at 0x82152A and `cmp g_ffxIsCatchUpStep, 0`
at 0x821533 gate `call FFX_MesWin_DrawAllWindows` at 0x82153C. On a catch-up step the game runs
`push 1 / call FFX_MesWin_ArmPadSampler` at 0x821543 instead and draws nothing. [V]

`FFX_BtlMenu_InitForBattle` sets `g_ffxBtlMenuActive 0x133C948` = 1, `g_ffxBtlMenuPartyWindowOn 0xC58EF4`
= 1, open mask 0, owner 255, reads the `dumm79` and `dum201` values from battle_script.bin and calls
`FFX_MenuScene_StopAll(2)`. `FFX_BtlMenu_ShutdownForBattle` sets `g_ffxBtlMenuActive` back to 0. [V]

---

## 3. The state machine

### 3.1 Menu level

| global | VA | values |
|---|---|---|
| `g_ffxBtlMenuOpenMask` | 0x23CC08C | 0 closed. `FFX_BtlMenu_Open` ORs in `1 << actor`. Set to 0 by `FFX_BtlMenu_Close 0x89ADE0`, by `FFX_BtlMenu_ConfirmCommand` at 0x8975DD and by `FFX_BtlMenu_InitForBattle` [V] |
| `g_ffxBtlMenuOwnerActor` | 0x23CC088 | the actor the menu belongs to, 255 none. Confirm keeps it while auto battle is on [V] |
| `g_ffxBtlMenuPageDepth` | 0x23CC092 | -1 empty, else 0..7, the top page. `FFX_BtlMenu_PushPage` does not bound-check it [V] |
| `g_ffxBtlMenuEnabled` | 0xC58EF8 | always 1, see 3.10 [M/V] |
| `g_ffxBtlMenuActive` | 0x133C948 | 1 between battle init and shutdown, gates the draw [V] |
| `g_ffxBtlMenuMultiSelectMode` | 0x133C94C | 0 normal, 0x100 Mix (0x311E), 0x10000 Doublecast (0x3029) [V] |

`FFX_BtlMenu_Open` refuses unless `FFX_Btl_GetPhase() == 1` and the open mask is 0. [V]

### 3.2 The page record, 240 bytes

| off | field |
|---|---|
| +0x01 | u8 state, see 3.3 |
| +0x02 | u8 target page flag, 1 on a target page (set by `FFX_BtlMenu_SetupTargetPage 0x89A860`) |
| +0x06 | u16 kind, picks the callbacks |
| +0x08 | u16 owner actor |
| +0x0A | u8 selections needed (Mix 2) |
| +0x0B | u8 selections made |
| +0x0C | u16 the command id that opened this page |
| +0x0E | u16[4] chosen rows |
| +0x16 | u16[4] chosen ids |
| +0x1E | u16 id under the cursor |
| +0x20 | list pointer, +0x24 list count |
| +0x38 | columns, +0x3A visible rows, +0x3C pixel scroll, +0x40 top row, +0x42 cursor, +0x44 row count |
| +0x88 | scroll callback. `FFX_BtlMenu_ScrollIdle 0x898E40` (was `nullsub_135`) means idle |
| +0x90 | draw callback |
| +0x94 | enter callback (only the Switch page has one) |
| +0x98 | open callback |
| +0x9C | step callback |
| +0xA0 | exit callback |
| +0xA4 | u16 selections required, +0xA6 u16 done (the target pages) |
| +0xD8 | u32 target mask this page contributes |
| +0xDC | f32 start time, from `FFX_Time_AppElapsedSeconds` |
| +0xE0 | f32 elapsed since +0xDC |
| +0xE4 | list flag |

[V for every field from its reads and writes in Step, PushPage, PopPage and the page procs. The names
of +0x38 to +0x44 are I from how the draw uses them]

### 3.3 Page states, the byte at page +0x01 (0x133C951 + 240 * i)

`FFX_BtlMenu_Step` walks the 8 pages and switches on this byte. [V]

| state | meaning | what Step does | next |
|---|---|---|---|
| 0 | idle or covered | nothing. The page is not drawn | set by PushPage's cover via 6 |
| 1 | new, set by `FFX_BtlMenu_PushPage` | nothing | 2 on the next step |
| 2 | enter | +0xDC = `FFX_Time_AppElapsedSeconds()`, +0xE0 = 0, calls +0x94 if set | 3 the same step |
| 3 | open | calls +0x98 until it returns 1. Every open callback returns 1, so this lasts one step | 4 |
| 4 | active | +0xE0 = now - +0xDC, calls +0x9C and ignores its return | stays, until a proc changes it |
| 5 | closing, set by `FFX_BtlMenu_PopPage` | nothing | 6 on the next step |
| 6 | exit | calls +0xA0, zeroes +0xDC once it returns 1 | 0 |

- `FFX_BtlMenu_PushPage` sets the page it covers to 6 and the new page to 1. [V]
- `FFX_BtlMenu_PopPage 0x89A630` sets the top page to 5 and the page under it to **2**, so an uncovered
  page re-enters, restamps its start time and sits through its open lockout again. The page procs then
  `++state` on the popped page, so it reaches 6 in the same step. The target page cancel does not
  increment, so a cancelled target page passes through 5. [V]
- `FFX_BtlMenu_DrawRoot` calls +0x90 for every page with state >= 3 and a non-null pointer, and only
  when the open mask is non-zero and `g_ffxBtlMenuEnabled == 1`. [V]

### 3.4 Page kinds and their callbacks

`FFX_BtlMenu_InstallPageCallbacks 0x8A0DE0` picks the callbacks by kind. `FFX_BtlMenu_InstallPageLayout
0x8A10D0` sets the geometry. `FFX_BtlMenu_SetPageKindForCommand 0x899870` sets the kind: 254 gives
kind 0, 0xFFFF gives 0xC, 0xFFFE gives 0xD, anything else takes byte 23 of the command's ability row.
[V]

| kind | commands | open +0x98 | step +0x9C | exit +0xA0 | draw +0x90 |
|---|---|---|---|---|---|
| 0 | the root window | `Root_OpenFirst 0x8A85A0` on the first open of a turn when usable kinds > 0, else `Root_Open 0x8A8640` | `Root_Step 0x8A8A70`, SE 2 once, then `PageProc_Root` at elapsed >= 0.45 s | `Root_Exit 0x8A2A60` | `Root_Draw 0x8A49B0` |
| 1, 2, 3, 4, 6, 0xE, 0x11 | Blk Magic, Wht Magic, Skill, overdrive moves, Item, Special, Use | `List_Open 0x8A86F0`, starts `bsublay` | `List_Step 0x8A8B20`, `PageProc_B` at >= 0.30 s | `List_Exit 0x8A2A90` | `List_Draw 0x8A7520` |
| 5 | Summon, Grand Summon | `Summon_Open 0x8A8710`, `bsublay` | `Summon_Step 0x8A8B50`, >= 0.45 s, `PageProc_B` in list mode 2 | `Summon_Exit 0x8A2AA0` | `Summon_Draw 0x8A75F0` |
| 7, 0xF | Weapon, Armor | `Equip_Open 0x8A8730`, `bsublay` | `Equip_Step 0x8A8B90`, >= 0.45 s, `PageProc_B` in list mode 3 | `Equip_Exit 0x8A2AB0` | `Equip_Draw 0x8A7DC0` |
| 0xA | Switch 0x3002 | `Switch_Open 0x8A8600`, returns 1. Enter +0x94 is `Switch_Enter 0x8A84D0`, which builds the bench list and starts `bclose` | `Switch_Step 0x8A87A0`, `PageProc_Switch 0x8A2160` at >= 0.30 s | `Switch_Exit 0x8A2A30` | `Switch_Draw 0x8A2DA0` |
| 0xC | the left window | `LeftWin_Open 0x8A8620`, `bleftwlay` | `LeftWin_Step 0x8A8A40`, `PageProc_C` at >= 0.45 s | `LeftWin_Exit 0x8A2A50` | `LeftWin_Draw 0x8A3680` |
| 0xD | the right window | `RightWin_Open 0x8A86D0`, `brightwlay` | `RightWin_Step 0x8A8AF0`, `PageProc_D` at >= 0.45 s | `RightWin_Exit 0x8A2A80` | `RightWin_Draw 0x8A6A60` |
| 0x14 | Mix 0x311E | `Mix_Open 0x8A86B0`, `bsublay` | `Mix_Step 0x8A8AC0`, `PageProc_Mix 0x89D610` at >= 0.30 s | `Mix_Exit 0x8A2A70` | `Mix_Draw 0x8A6870` |
| 0x15, 0x16 | Spare Change 0x3024, Bribe 0x302A, Pay 0x3123 | `Gil_Open 0x8A8610` | `Gil_Step 0x8A87D0`, the digit editor, **no lockout** | `Gil_Exit 0x8A2A40` | `Gil_Draw 0x8A32B0` |
| target page | any command with ability row byte 26 bit 0 | `Target_Open 0x8A8780`, `bclose` | `Target_Step 0x8A8BE0`, `PageProc_Target 0x89D390` every step, **no lockout** | `Target_Exit 0x8A2AC0` | `Target_Draw 0x8A84C0`, the hand cursor |

All names in the table carry the `FFX_BtlMenu_` prefix. The target page callbacks come from
`FFX_BtlMenu_InstallTargetCallbacks 0x8A1090`, not from the kind switch. [V]

- The lockouts are `fld [ecx+0E0h] / fcomp dbl_B43D50 / test ah, 5 / jnp skip` at 0x8A8A90 (0.45, skip
  while elapsed < 0.45) and `fld flt_B3EB44 / fcomp [ecx+0E0h] / test ah, 41h / jz skip` at 0x8A8B26
  (0.30, skip while elapsed < 0.30). The constants read 0.44999998 and 0.30000001. [V]
- So 10 step callbacks, 8 with a lockout (5 at 0.45 s, 3 at 0.30 s), and the gil and target pages
  without one. [V]
- `Root_Open` switches on `FFX_Btl_GetUsableCommandKinds` through the jump table at 0x8A8694: 0 starts
  `bnormallay` (0x8A867C), 2 starts `bnormalllay` (0x8A8666), 1 and 3 start nothing. That function
  returns bit 1 for a usable left-window command and bit 2 for a usable overdrive with a full gauge. [V]
- `Root_OpenFirst` starts `blimitlay` when the usable kinds are exactly 2, then replaces +0x98 with
  `Root_Open` and returns 1. [V]
- Every exit callback except Switch's and the target page's calls `FFX_MenuScene_StopPlayingUnprotected(2)`. [V]
- The left window shows the actor's list at +40 (rows with 0x1000 in the u32 at command row +28): the
  overdrive parents (Swordplay and the rest, the aeon overdrives) and the scripted commands such as
  Pincer Attack, Use crane, Move, Stand by, Talk, Move in, Pull back, Cancel, Struggle, Open lock. The
  right window shows the list at +56 (rows with 0x800): Escape, Weapon, Armor, and for aeons Shield,
  Boost, Dismiss. [M, from command.bin decoded in memory, see 3.11]
- So `FFX_BtlMenu_PageProc_C` is the left window proc and `FFX_BtlMenu_PageProc_D` the right window
  proc. They are kit RVA names and are not renamed. [V]

### 3.5 Inputs

The procs read `g_ffxMenuPadHeldTrig 0x25D09D2`. The low word is held, the high word is newly pressed.
Movement reads `g_ffxMenuPadWord10`, the wall-clock repeat mask. Bits below are the pressed bits (the
high word, so `test 0x200000` is pressed 0x20). [V]

| pressed bit | where | action |
|---|---|---|
| 0x20 | every page | **confirm**. `FFX_BtlMenu_ClassifyCommand 0x89ACA0` on the row, then `FFX_BtlMenu_PushPage` [V] |
| 0x40 | every page | **cancel**. Drops the last pick of a multi-select, else `FFX_BtlMenu_PopPage` [V] |
| 0x10 | Root, B at depth 0, C, D | shortcut commit. Stages actor+0x6C8 through `FFX_Btl_GetEscapeCommandId 0x78FB80`, self target, `FFX_Btl_CommitCommand(staged, 0, 0)`, then Close, StopAll, owner 255. In the PS2 pad layout 0x10 is Triangle [V for the code, I for the button] |
| 0x8000 (Left) | Root | push 0xFFFF, the left window, when `FFX_Btl_GetUsableCommandKinds` > 0 [V] |
| 0x2000 (Right) | Root | push 0xFFFE, the right window, when list 13 is non-empty [V] |
| 0x2000 | C | back to the root [V] |
| 0x8000 | D | back to the root [V] |
| L1 marker | Root (`FFX_Input__isPressed(0x8000000)`), B, C, D (pressed 0x04) | push 0x3002 Switch, unless `sub_784FF0(actor)` is true (ply_rom byte 16 bit 4) [V] |
| 0x100 (Select) | any open page, in `FFX_BtlMenu_StepHelp 0x8912E0` | toggles save option bit 0x400, the help window [V] |
| word10 0x1000 / 0x4000 / 0x8000 / 0x2000 | list pages | cursor up / down / left / right, by the column count at +0x38 [V] |
| L1 / L2 marker repeat | Switch | cycle the bench list [V] |

The Select toggle is `test g_ffxMenuPadHeldTrig+2, 100h` at 0x8912F7, then
`FFX_SaveData_GetOptionFlagsPtr` with `or esi, 400h` at 0x891318 or `and esi, 0FFFFFBFFh` at 0x891327
and the store at 0x89132D. The bit is read in three places only: this toggle, `FFX_BtlMenu_DrawHelp
0x8936D0` (`test [eax], 400h / jz skip` at 0x8936E8 and 0x8936EE) and `maybe_FFX_BtlMenu_DrawTargetInfo
0x895140`. So set means the help line is shown. [V for the three sites, M for there being no others]

**Auto battle** (`g_boosterAutoBattle 0x133D6E0`) forges presses inside the procs: Root forges 0x20 and
forces the cursor onto Attack, B forges 0x40, C forges 0x2000, D forges 0x8000, the target page forges
0x20, Mix and Switch forge 0x40, and the gil page forges 0x20 for Yojimbo and 0x40 otherwise. The
lockouts still apply, so an auto battle turn takes at least 0.45 s of wall clock. [V for the forges, I
for the minimum]

### 3.6 PushPage and ClassifyCommand

`FFX_BtlMenu_PushPage 0x89A330`: [V]

- When the current page is a target page, it compares done with required at +0xA4 / +0xA6. Done
  enough means `FFX_BtlMenu_ConfirmCommand`. Not enough means it pushes 0x3115 again, the second pick of
  Doublecast.
- Otherwise it calls `FFX_BtlMenu_ClassifyCommand(actor, id)` and branches on the result.

| Classify result | when | PushPage does |
|---|---|---|
| 0 | id 254 | kind 0 page (the root) |
| -2 | id 255, or the MP cost is -1 (Doublecast's second pick uses `g_ffxBtlMenuPendingMpCost 0x133F106`) | refuse |
| -1 | `FFX_Btl_IsCommandLocked` | refuse |
| kind (<= 128) | the row opens a non-empty submenu (row byte 22 & 0xF8 is 8 or 16) | `FFX_BtlMenu_SetPageKindForCommand`, push that page |
| -4 / -3 | an empty submenu | refuse |
| 254 | row byte 26 bit 0 set, or Mix mode on | `FFX_BtlMenu_SetupTargetPage`, push a target page |
| 255 | anything else | `FFX_BtlMenu_ConfirmCommand` |

The cursor memory is restored on push when the option is on. `FFX_BtlMenu_BuildTargetCandidates 0x89ABD0`
clears the target context at 0x133D0D0 and fills the candidate masks through 0x8985B0, 0x898490,
0x898330, 0x8983F0 and 0x898640. [V]

### 3.7 The target page

`FFX_BtlMenu_PageProc_Target 0x89D390`: [V]

- word10 moves `g_ffxBtlMenuTargetCursor` through a camera-dependent table from `sub_780CE0`, and a group
  button swaps between the ally and enemy candidates.
- Every step it stores the cursor's mask in `g_ffxBtlMenuTargetMask` and calls
  `FFX_Btl_OnMenuTargetChanged(actor, mask)` (0x792DE0) at **0x89D572**.
- That function stores `g_ffxBtlCurTargetMask`. When the mask differs from `g_ffxBtlShownTargetMask`
  and the command queue is empty (or byte `dword_112C9DB+3` is set), it calls `sub_7969F0` (writes the
  facing at actor +0x49C), `FFX_Btl_AimNeckAtSingleTarget 0x7B1E80` (actor +0x438 / +0x439, skipped when
  `g_ffxBtlDbgNeckOff`), and `FFX_BtlEv_FireEvent(0, actor, 25, 0xFFFF, 1, 1, 0, 0)` at **0x792E2B**
  (`push 19h` at 0x792E26).
- Cancel calls `FFX_Btl_OnMenuTargetCancelled 0x792A40`, which fires event 132 when the queue is empty,
  sets the shown mask back to -1, and then pops.
- Confirm calls `FFX_BtlMenu_PushPage(actor, 0x3115)`, which commits through 3.6.

### 3.8 Multi-select and gil

- Mix (0x311E): mode 0x100, two items from one list, one target page with one candidate, the
  intersection mask. The first pick calls `FFX_SaveData_SetItemInUse 0x790790`, which writes
  `g_ffxItemInUse 0x112C948`. [V]
- Doublecast (0x3029): mode 0x10000, two spells, two target pages, the second spell's cost is checked
  with the first spell's pending cost. [V]
- `FFX_BtlMenu_PopPage` clears the mode when the popped page was opened by 0x311E or 0x3029. [V]
- The gil pages (Spare Change, Bribe, Pay) are set up by `FFX_BtlMenu_Gil_Init 0x8A2030`: Spare Change
  passes a cap of 100000, the others -1 (no cap but the held gil). Left / Right picks a digit, Up / Down
  adds or subtracts a power of ten. Confirm stores the amount in `g_ffxBtlMenuStagedGil 0x23CC038` and
  pushes. [V]

### 3.9 The hand-off

`FFX_BtlMenu_ConfirmCommand 0x8975A0`: [V]

1. `FFX_BtlMenu_BuildStagedCommand` one more time.
2. `FFX_Btl_CommitCommand(g_ffxBtlMenuStagedCmd 0x23CC040, g_ffxBtlMenuStagedGil 0x23CC038, 0)`.
3. Saves the cursor memory if the option is on.
4. Open mask = 0 (0x8975DD), `FFX_MenuScene_StopAll(2)`, owner 255 (kept under auto battle).

The four shortcut commits at 0x89C9B4, 0x89CE8B, 0x8A2420 and 0x8A27C0 call `FFX_Btl_CommitCommand`
directly. [V] `FFX_Btl_ExecCommand` runs right after `FFX_BtlMenu_Step` in the same
`FFX_Btl_MainStep`, so a command confirmed on a sub-step executes on that sub-step. [V]

### 3.10 What else Step does

Unconditional, every sub-step, in order: [V]

1. `FFX_MenuScene_StepObjects(2)` at 0x89AE22.
2. `FFX_BtlMenu_RefreshTargetUnits 0x891140` (lists 9 and 11, live allies and enemies).
3. `sub_8920D0` (ally bars, calls `sub_898010`) and `sub_891220` (enemy bars).
4. `FFX_BtlOd_PressCircleGate`, `FFX_BtlOd_StepSharedTimer`, then the Auron, Tidus and Lulu minigames.
5. A second Lulu call and a pad commit inside the `g_ffxThreadedPadMode` block.

Gated by `FFX_Btl_GetPhase() == 1` (0x89AE74), open mask != 0 (0x89AE7D) and `g_ffxBtlMenuEnabled != 0`
(0x89AE8A), each a `jz/jnz locret_89AF9C`: [V]

1. `FFX_BtlMenu_StepCursorSpin 0x890F30` (about 6 degrees a step).
2. The page loop of 3.3.
3. `FFX_BtlMenu_StepHelp 0x8912E0`.
4. `FFX_BtlMenu_MarkRootSlideStart`.
5. `maybe_FFX_BtlMenu_StepTargetInfo 0x891580`.
6. `FFX_BtlMenu_BuildStagedCommand`, which calls `FFX_Btl_SetCommandPreview` and
   `FFX_BtlMenu_StepCtbWindow 0x89ECE0`.

`g_ffxBtlMenuEnabled 0xC58EF8` is initialised .data with value 1. Its one writer is
`FFX_BtlMenu_SetEnabled_ORPHAN 0x89BC50` (`mov dword_C58EF8, eax` at 0x89BC56), which has no code or
data reference. So the flag is a constant 1. [M for no reference, V for the value]

`g_ffxBtlMenuPartyWindowOn 0xC58EF4` is set by `FFX_BtlMenu_SetPartyWindowOn 0x89BC60` (Btl 112 and 113)
and by init, and gates `FFX_BtlMenu_DrawPartyWindow 0x896450`, the three front-line rows. [V]

### 3.11 Command ids

Decoded from command.bin in memory through `../tools/vbf.py`
(`ffx_ps2/ffx/master/new_uspc/battle/kernel/command.bin`, rows 0..319, stride 96, data at +0x14, the
name offset is the u16 at row +0, strings at data + 96 * 320). [M]

| id | name | id | name |
|---|---|---|---|
| 0x3000 | Attack | 0x3021 | Defend |
| 0x3001 | Item | 0x3029 | Doublecast |
| 0x3002 | Switch | 0x311E | Mix |
| 0x3003 | Escape | 0x3113 | Skill |
| 0x3004 | Weapon | 0x3114 | Special |
| 0x3005 | Armor | 0x3115 | Blk Magic |
| | | 0x3116 | Wht Magic |
| | | 0x3117 | Summon |

The per-actor lists are at `dword_2310CD8 + 1144 * actor`, read by `sub_785410` and built by
`sub_79BB50`. Kind 0 at +0 (20 entries), 0xC at +40 (8), 0xD at +56 (8), 2 at +72 (24), 1 at +120 (24),
3 at +168 (32), 0xE at +232 (32), 4 at +296 (24). [V]

---

## 4. Clock and gates

| part | clock | catch-up | evidence |
|---|---|---|---|
| `FFX_BtlMenu_Step`, the page states, the procs, the commit | per sub-step | runs | unconditional call at 0x790D74 [V] |
| the open lockouts, page +0xDC / +0xE0 | wall clock | runs, but the time is real time | `FFX_Time_AppElapsedSeconds`, 0x8A8A96, 0x8A8B28 [V] |
| cursor movement | wall-clock repeat mask `g_ffxMenuPadWord10` | runs | the procs [V] |
| the scroll lock, page +0x88 | **draw calls** | stays locked | below [V] |
| the page draws, the party window, help, the CTB window draw | per drawn step | skipped | 0x821533 [V] |
| scene tracks | per draw of their group | skipped | `MENU_SCRIPT.md`, `MENU_DRAW_PATH.md` 12 [V] |
| scene object states | `FFX_MenuScene_StepObjects(2)` per sub-step | runs | 0x89AE22 [V] |

**The scroll lock is clocked by draws.** `FFX_BtlMenu_PageProc_Root`, `_B` and `FFX_BtlMenu_PageProc_Mix`
start with `cmp dword ptr [esi+88h], offset FFX_BtlMenu_ScrollIdle / jnz return` (0x89C917 / 0x89C921,
0x89CDF7, 0x89D625). A proc sets +0x88 to `FFX_BtlMenu_ScrollUpOnDraw 0x898DF0` or
`FFX_BtlMenu_ScrollDownOnDraw 0x898E20` when the cursor leaves the window. Those move +0x3C by 16
(`FFX_BtlMenu_GetScrollPixels 0x8B7C60`) and put `FFX_BtlMenu_ScrollIdle` back. +0x88 is called only from
`FFX_BtlMenu_DrawListBody 0x89F370` at 0x89FF0F (`mov eax, [ebx+88h] / call eax`) and
`FFX_BtlMenu_Root_Draw` at 0x8A669F. So after a scroll the page takes no input until the next drawn step,
and a run of catch-up steps keeps it locked. [V]

**Does the simulation read menu state?**

- `FFX_BtlMenu_GetEnabled 0x89B590` and the open mask are read by `FFX_Btl_IsPadDrivenMagicPlaying
  0x6F6FF0`, whose only caller is `FFX_Time_ScaleDtForBooster`. So whether the menu is open decides Turbo
  eligibility while a pad-driven magic plays, and Turbo changes the step count (`MAIN_LOOP.md`,
  `BOOSTERS.md`). That is the only reader of the open mask outside the menu. [V for the reads, M for
  only]
- `g_ffxItemInUse 0x112C948` is read by `FFX_SaveData_GetItemCount`, which has many callers including
  ATEL. The menu writes it on the first Mix pick. `FFX_Btl_SendMenu` writes it too, at 0x792B66. [V]
- Option bit 0x400 lives in the save block at 0x113079C (save +0x3D0C). Only the menu reads it, but it is
  inside the block a world hash covers. [V for the address and readers, I for the hash]
- The target page writes facing, neck, `g_ffxBtlCurTargetMask`, `g_ffxBtlShownTargetMask` and fires
  events 25 and 132. Those are simulation writes. [V]
- The minigames in Step wait on scenes, which advance per draw. See 5. [V]

---

## 5. The battle_script.bin scenes

34 scenes. Group is the draw group. The entry and track offsets are into the file. [M, the file parsed
in memory]

`FFX_BtlMenu_DrawRoot` draws group 0 before the page draws and group 1 after them. It passes noRect = 1
while any overdrive minigame is active, which skips the op 4 rectangles. Group 4 is drawn only by
`FFX_BtlOd_WakkaReels_Draw` (host 650). [V]

`FFX_MenuScene_IsDone 0x8AE070` returns false when no object with the name exists. StopAll,
StopUnprotected and StopPlayingUnprotected clear the name byte. A state 4 object keeps its name until
the next `StepObjects`. Step runs `StepObjects` before the minigames, so a minigame sees "done" on the
step after the finishing draw. [V]

### 5.1 Started

| scene | group | started by | draws | waited on by |
|---|---|---|---|---|
| `bnormallay` | 1 | `Root_Open` kinds 0, 0x8A8683 | loops, small markers near the window's lower left (shape ids 411..420, lines) | nobody |
| `bnormalllay` | 1 | `Root_Open` kinds 2, 0x8A866D | loops, same family | nobody |
| `blimitlay` | 1 | `Root_OpenFirst` kinds 2, 0x8A85D0 (and the orphan at 0x8A8580) | loops | nobody |
| `bleftwlay` | 1 | `LeftWin_Open` 0x8A8627 | loops | nobody |
| `brightwlay` | 1 | `RightWin_Open` 0x8A86D7 | loops | nobody |
| `bsublay` | 1 | `List_Open` 0x8A86F7, `Summon_Open` 0x8A8717, `Equip_Open` 0x8A8737, `Mix_Open` 0x8A86B7 | loops | nobody |
| `bclose` | 0 | `Switch_Enter` 0x8A853E, `Target_Open` 0x8A8787 | 3 shapes, done after 17 draws, stops itself | nobody |
| `btidu` | 0 | Tidus state 1, `StartHeld` 0x892349 | 45 tracks, 9 draws | `IsDone` state 2 at 0x892360. Stopped in state 8 at 0x8924EE |
| `bgood` | 1 | Tidus state 5 0x8924B4, Auron state 5 0x891069, `FFX_BtlOd_PlayGoodCue` 0x89AB87 (host 661) | 15 tracks, 17 draws | `IsDone` at 0x8924D6 and 0x89108B (state 6), `FFX_BtlOd_IsGoodSceneDone` 0x8977E7 (host 653) |
| `bauro` | 0 | Auron state 1, `StartHeld` 0x890F99 | 48 tracks, 9 draws | `IsDone` state 2 at 0x890FB0, then starts `bauro2`. Stopped state 8 at 0x89109F |
| `bauro2` | 0 | Auron 0x890FC8 | 1 track, loops | stopped state 8 at 0x8910AB |
| `blulu` | 0 | Lulu state 1, `StartHeld` 0x891C1A | 47 tracks, 9 draws | `IsDone` state 2 at 0x891C41, then `blulu2`. Stopped state 8 at 0x892065 |
| `blulu2` | 0 | Lulu 0x891C59 | loops | stopped state 8 at 0x892071 |
| `bgoodl` | 1 | Lulu, on a rotation match, 0x891E63 | 2 tracks, 5 draws | nobody |
| `bwakk` | 4 | `FFX_BtlOd_WakkaReels_Begin` `StartHeld` 0x898DD1 (host 656) | 42 tracks, 9 draws | `FFX_BtlOd_WakkaReels_IsSceneDone` 0x8977D0 (host 652, only while active), `GetState == 3` in `FFX_BtlOd_WakkaReels_Draw` 0x897508 (host 650, draws message 12307) and in `FFX_MenuScript_DrawRectPrim` 0x8FDF5A. Stopped by `FFX_BtlOd_WakkaReels_StopCue` 0x890F17 (host 649) |

[V for every start, wait and stop site. M for the track and draw counts]

### 5.2 Never started

`bnormal`, `bnormale`, `bnormalt`, `bnormall`, `bnormalelay`, `bnormaltlay`, `bleftw`, `brightw`, `bsub`,
`blimit`, `btwo`, `btwolay`, `bevent`, `beventlay`, `blib`, `bhline`, `bstback`, `bwakk2`, `bwakkhelp`.

None of these 19 exists as a NUL-terminated string in FFX.exe. A raw byte search finds them only as the
prefix of a longer started name (`bnormal` inside `bnormallay` and so on). FFX.exe has no other way to
name a scene, so no code reaches them. [M] They are probably the PS2 menu's layouts, with the "lay"
versions being what the PC menu kept. [I]

---

## 6. Simulation and co-op

**What must replicate.** The committed 72 byte record and its gil, the same thing `BATTLE_COMMAND.md` and
`COOP_DESIGN.md` already plan to ship. The record leaves the menu in one function (3.9) plus the four
shortcut commits. [V]

**What also reaches the simulation and needs a decision:**

1. The target preview (3.7). Turning, neck aim, event 25 on every mask change and event 132 on cancel are
   driven by the local cursor through `FFX_Btl_OnMenuTargetChanged` and `FFX_Btl_OnMenuTargetCancelled`.
   A passive peer that does not run the menu gets none of them, so its world diverges. Either replicate
   the cursor mask changes as their own commands or suppress the two calls on every peer and accept a
   change to the presentation. [V for the writes, I for the fix]
2. `g_ffxItemInUse` (3.8). One global, written by the menu and read by the item count. [V]
3. Option bit 0x400 in the save block (3.5). A local preference written into replicated state. Exclude it
   from the hash or replicate the toggle. [I]
4. Turbo eligibility reads the open mask (4). [V]
5. The minigame scenes advance per draw (5), so their timing depends on catch-up and frame rate. Ship
   the minigame result, or drive the scenes per step. This is item 1.10's thread. [V for the clock]

**Local presentation:** the page stack, the lay scenes, `bclose`, the help line, the CTB order window,
the target info slide, the bars, the cursor spin and the party window. Several of these are written on
the step thread (the CTB window rows, the help text, the bars), so a presentation layer that reads them
has to respect `ffx-present-is-not-the-step-thread`. [V for where they are written, I for the rest]

**Only if both peers run the menu:** the wall-clock lockouts, the wall-clock repeat and the draw-clocked
scroll lock make the step a confirm lands on depend on the machine. That does not matter while only the
acting player runs the menu and the record is shipped. [I]

The whole menu is single slot (one stack, one staged record, one owner, one target context), which
`BATTLE_COMMAND.md` section 2 already says. [V]

---

## 7. Corrections to other docs

| doc | was | now |
|---|---|---|
| `BATTLE_COMMAND.md` 2 | `FFX_BtlMenu_Step` and the staged record rebuild run "once per frame" | once per sub-step, catch-up included (0x790D74 in `FFX_Btl_MainStep`, which runs per sub-step) |
| `BATTLE_COMMAND.md` 2 | page +0x01 "1 = new, 6 = suspended" | 0 to 6 as in 3.3. 6 is the exit state, a covered page sits at 0 |
| `BATTLE_COMMAND.md` 2 | page +0x02 "kind" | +0x02 is the target page flag, the kind is the u16 at +0x06 |
| `BATTLE_COMMAND.md` 1 | 0x3000 is "almost certainly Attack, inferred" | Attack, from command.bin [M] |
| `BATTLE_COMMAND.md` 4 and 7 | `FFX_Btl_SendMenu` "called once per frame", "per frame order" | once per sub-step, the order is per sub-step |
| `MENU_SCRIPT.md` 4.3 | the battle scenes include `bnormal`, `bleftw`, `brightw`, `bsub`, `blimit` | only the lay variants, `bclose` and the minigame scenes are started, 19 are never started |
| `OVERDRIVES.md` 1 and 8 | "every battle menu page" has a lockout, "eight of the nine" state 4 callbacks | 10 step callbacks, 8 with a lockout, the gil and target pages without |
| `INPUT_LAYER.md` 2.4 | which bit is confirm is not established | in the battle menu 0x20 confirms and 0x40 cancels [V] |
| IDB comment on `FFX_BtlMenu_Step` | "EVERY state-4 callback gates" | not the gil page and not the target page. Not edited, the apply script appends to other rows only |

---

## 8. Names

`../tools/apply_battle_menu_names.py`, 85 functions and 16 globals, no RENAMES. The rehearsal on a copy
of the IDB: dry run 101 planned and 0 skipped, apply with comments renamed 101 and failed 0, longest
comment 389 characters, second dry run planned 0.

---

## 9. Open

- Who calls host 650 (`FFX_BtlOd_WakkaReels_Draw`) and at what rate. That sets `bwakk`'s clock.
- What events 25 and 132 do in the battle scripts that handle them.
- The shape table ids the lay scenes use, and what they look like on screen.
- Whether the page depth can pass 7. `FFX_BtlMenu_PushPage` does not check it.
- The exact writes of `sub_7969F0` beyond actor +0x49C.
- What actor+0x6C8 holds, and so which command the 0x10 shortcut commits.
