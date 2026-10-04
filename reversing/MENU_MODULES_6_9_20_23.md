# The four unidentified native menu modules: 6, 9, 20 and 23

Target: `G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\FFX.exe`
All addresses are IDA virtual addresses at the loaded base 0x400000, which is RVA + 0x400000.
Companion reading: `MENU_SYSTEM.md` (the module manager and the SECOND PASS), `SETTINGS_MENU.md`,
`GAME_STATE.md` (the save bucket map), `INPUT_LAYER.md`.

Status: complete for the four modules named in the brief. Names and comments are in the IDB and saved.

---

## 0. The answers, up front

| id | descriptor | exec | **what it is** |
|----|-----------|------|----------------|
| 6 | 0xC5A7D8 | 0x8D1E00 | **the Status screen** |
| 9 | 0xC5A7FC | 0x8D5540 | **the Customize screen** |
| 20 | 0xC5B318 | 0x8E2870 | **the PS2 HDD install screen, unreachable in the shipped PC build** |
| 23 | 0xC5B33C | 0x8E2960 | **the world map viewer, the "Map" key item** |

And two corrections that fell out of the same evidence:

| id | exec | was called | **actually is** |
|----|------|------------|-----------------|
| 3 | 0x8C5450 | Customise | **Items** |
| 7 | 0x8CC330 | Items | **Aeons** |

---

## 1. How the identification was done, because it settles all four at once

The first pass found `FFX_Menu_StartSubmoduleByResult 0x8E24C0` and read off a "result code to module"
table but could not say what a result code meant. It means more than it looks.

`FFX_Menu_BuildMainMenuRows` (was `sub_8E1C60`, now named) builds the main menu row list. For each
available row it stores a value in `dword_1871568[2 * slot]`, and then it does two things with that
same value:

```c
v13 = sub_8E2270(g_ffxMenuMainCursor);     // read the row's value
v14 = sub_8DDF60(v13);                     // -> FFX_KernelString_GetDescription(9, v13, lang)
FFX_Menu_SetHelpString(v14);
...
v19 = sub_8E2270(v17);
v20 = sub_8DDF80(v19);                     // -> FFX_KernelString_Get(9, v19, lang)  == the row LABEL
```

So **the result code a submenu is launched by IS the kernel string id of its own menu row**, in string
group 9. Group 9 is `ffx_ps2/ffx/master/<lang>/battle/kernel/mmain_txt.bin`, pulled out of the shipped
VBF with the project's own `tools/vbf.py` and decoded with the table layout already documented on
`FFX_KernelTable_GetRow 0x7AB870` plus the 0x30-based glyph encoding from `CONFIG_ROWS.md` section 5.1.

`new_uspc/battle/kernel/mmain_txt.bin`, 2,395 bytes, WORD count 1 at +0, one range descriptor
`lo=0 hi=55 stride=16 stringOffset=896 dataOffset=0x14`, each row four WORD string offsets
`{name lang0, name lang1, desc lang0, desc lang1}`:

| id | label | id | label |
|----|-------|----|-------|
| 0 | Items | 11 | Load |
| 1 | Abilities | 12 | Items |
| 2 | Equip | 13 | Equipment |
| 3 | Sphere Grid | 14 | Name Entry |
| 4 | **Status** | 15 | Overdrive |
| 5 | Aeons | **20** | **HDD** |
| 6 | Formation | 22 | Buy |
| **7** | **Customize** | 23 | Sell |
| 8 | Config | 24 | Done |
| 9 | Help | 25 | You can't carry any more. |
| 10 | Save | 26 | You don't have enough gil. |
|  |  | 27 | Are you sure? |
|  |  | 28 | Yes |

Two independent cross-checks that the result code really is this id, neither of which I chose after
the fact:

* `FFX_Menu_ExecModule4Equip` calls `sub_8DDF60(2)` at 0x8CF357 to set its own help line. Id 2 is
  "Equip", and module 4 was already proved to be Equip in `MENU_SYSTEM.md` by `setCharEquip` plus the
  `KEEPHP:%d` / `NOW HP:%d` strings.
* `FFX_Menu_CustomizeStep 0x8D5830` calls `sub_8DDF60(7)` at 0x8D586D. Id 7 is "Customize", and that
  function's own debug strings are `TK:SND:KAIZOU` (kaizou, 改造, remodel or customise), `scene23`,
  `SELCOUNTER:%d`, `OK CANCEL`, `TK:MN2:CLOSE`.

Also, the shop ids line up: `FFX_Menu_ExecModule12Shop` asks for ids 0x19/0x1A ("can't carry" / "not
enough gil") and 0x19/0x1A/0x1C/0x1D around its confirm boxes, and 0x31/0x32/0x34/0x36 elsewhere.

### The full main menu, decoded

`byte_C5B2D4[bit]` is the sort slot, so the row order and the availability gates come out too:

| slot | bit | label | available when | module |
|------|-----|-------|----------------|--------|
| 0 | 3 | Sphere Grid | scenario word 0 or >= 67 | 19, started by module 1's exec at 0x8E09C6 |
| 1 | 0 | Items | always | **3** |
| 2 | 1 | Abilities | always | 2 |
| 3 | 15 | Overdrive | always | 21 |
| 4 | 2 | Equip | scenario 0 or >= 210 | 4 |
| 5 | 4 | **Status** | always | **6** |
| 6 | 5 | Aeons | scenario 0 or >= 208 and `sub_8AB1E0()` | **7** |
| 7 | 6 | Formation | scenario 0 or >= 217, `sub_8A9B50() > 1`, `!(sub_86BE50() & 0x40)` | not in `StartSubmoduleByResult` |
| 8 | 7 | **Customize** | scenario 0 or >= 1096 | **9** |
| 9 | 8 | Config | always | 10 |
| 10 | 9 | Help | always | not in `StartSubmoduleByResult` |

The gates are a sanity check in their own right. Customize needs scenario 1096, which is where Rikku
joins and she is the character who customises. Formation needs a party larger than one. Aeons needs
`sub_8AB1E0()` on top of a scenario gate, so you need an aeon first.

---

## 2. MODULE 6 = THE STATUS SCREEN

### 2.1 Identity

* Main menu row id 4, label "Status" (section 1).
* `FFX_Menu_StartSubmoduleByResult` case 4 at 0x8E2526 does `FFX_Module_Start(6, 0)` then
  `FFX_Module_Suspend(1)`.
* Corroboration from the layout table. Module 6's PREPARE slot (descriptor+0x08) is 0x8D1D30, which
  IDA had mislabelled `__cfltcvt_init_99` from a bogus library signature match. It zeroes the six
  panel pointers and calls `FFX_Menu_StatusLoadLayout 0x8D4920`, which is the **only** reader in the
  whole binary of these `menu_script.bin` keys:
  `rect_stsmw` (sts = status, mw = message window), `col_lmtg0` / `col_lmtg1` / `col_lmtgf0`
  (lmtg = limit gauge, which is FFX's internal name for the Overdrive gauge),
  `rect_wpnbplt` / `wpnlplt` / `wpnrplt` / `wpnbline`, `pos_wpnnm` / `pos_wpnicon` / `pos_wpnmoji`,
  `num_staplt` / `pos_lstaplt` / `pos_rstaplt`, `pos_extex` / `pos_exmssg`, `rect_3nameplt`,
  `pos_6nameplt`, `num_face1` / `num_face2` / `pos_face` / `pos_faceb`.
  I checked `rect_stsmw`, `col_lmtg0` and `col_lmtgf0` individually: each string has exactly one xref
  in the binary and it is in `FFX_Menu_StatusLoadLayout`.
* Corroboration from what it reads. Panel 0's list builder reads the equipped weapon's and armour's
  **auto-ability slots**, `entry+7` through `entry+10`, for both the weapon slot and the armour slot.
  That is the auto-ability column of the FFX Status page.

### 2.2 Structure, read off the disassembly

The exec is a 20-state machine on `descriptor+0x1C`. Hex-Rays refuses this function (failure code -12
at 0x8D1E66), so everything below is from the instruction stream, not pseudocode.

Button bits used, in the game's own 16-bit layout which is the PS2 pad word rotated by 8:
`0x20` = Circle (confirm), `0x40` = Cross (cancel), `0x04` = L1, `0x08` = R1.

```
 0  wait for sub_8A8E50() (byte_1340859) != 0   -> 1 -> 2 -> 4
 3  FFX_Menu_StatusSetSlideVelocity(-341)       -> 4
 4  BROWSE
      Pressed & 0x20 -> sound 0x80000001, sub_8AA410(0), state 5
      Pressed & 0x40 -> sound 0x80000004, state 17 -> 18 -> 19
      else, only if FFX_Menu_GetCharListCount() > 1 and sub_8AB220() != 0:
        Repeat & 0x04 and Held == 4 exactly -> FFX_Menu_CursorCharPrev
        Repeat & 0x08 and Held == 8 exactly -> FFX_Menu_CursorCharNext
 5  StatusSetSlideVelocity(+341); StatusCreateListPanel(0, 0, 0x000, 0)  -> 6
 7  StatusCreateListPanel(0, 1, 0x40, 0)   -> 8
 9  StatusCreateListPanel(0, 2, 0x41, 0)   -> 10
11  StatusCreateListPanel(0, 3, 0x45, 0)   -> 12
13  StatusCreateListPanel(0, 4, 0x44, 0)   -> 14
 6/8/10/12/14/16  Circle -> destroy panel N, advance to the next panel
                  Cross  -> destroy panel N, back to state 3
19  FFX_Module_Suspend(6); g_ffxMenuStatusMainWindow+0x41 = 1;
    if descriptor+0x20 == 0 then FFX_Module_Start(1, 0)
```

The five panels are five lists built by `FFX_Menu_StatusBuildAbilityList 0x8D2E10` with
`(a1 = panel index, a2 = the code, a3 = 0, a4 = character)`. Panel 0 with code 0 takes the
`a1 == 0` branch and lists the equipped weapon's and armour's auto-abilities. Panels 1 to 4 with codes
0x40, 0x41, 0x45, 0x44 land in the 64..71 range, take the `FFX_SaveData__getCharRecord` branch and
list the entries of the table from `sub_790200(0, ...)` whose first two key bytes match
`(code, 0)` and that `maybe_FFX_SaveData_TestEventFlag(char, id)` says this character has.
So: auto-abilities, then four per-character ability or command lists.

The slide animation is integer only. `g_ffxMenuStatusSlideVel 0x186A9FC` is a velocity of +-341 and
`FFX_Menu_StatusAdvanceSlide 0x8D3280` accumulates it into `g_ffxMenuStatusSlidePos 0x186AA00`,
clamped to 0..4096. No clock, no float.

### 2.3 The six co-op answers for module 6

1. **What is it?** The Status screen. Evidence in 2.1.
2. **RNG?** **No.** Zero reach. Its 370-function closure (method in section 6) contains neither
   `FFX_Rand_Stream 0x7988F0` nor `FFX_Btl_Rand 0x7989A0`, and no instruction in it references
   `g_ffxRandStreamState 0x1135EE0..0x1135FF0` or `g_ffxBattleRandState 0xC42200..0xC42208`.
3. **Wall clock?** **Yes, and it is draw only.** One reader:
   `FFX_Menu_DrawGaugeBarClockPulsed 0x8D3690` does
   `cos(FFX_Time_AppElapsedSeconds() * 3.0) * 32.0 + 96.0` and puts the result straight into the
   colour argument of `sub_903EE0`. The path is
   `InitModule6Status -> StatusCreateMainWindow -> (object slot +16) sub_8D2710 -> sub_8D4EE0 ->
   sub_8D3D70 -> 0x8D3690`. Object slot +16 is the **draw** slot, see section 5. No `GetTickCount`,
   no `timeGetTime`, no `GetTickCount64` in the closure, and the only `QueryPerformanceCounter`
   reader reachable is `Phyre_Time_NowSeconds 0x42FD80` underneath that same `AppElapsedSeconds`.
   **Nothing the screen commits is derived from a clock, because the screen commits nothing.**
4. **Input, and is it deterministic?** It reads `FFX_MenuSys_GetHeld` / `GetRepeat` / `GetPressed`.
   Confirm and cancel are on `Pressed`, which is pure math and deterministic. **The L1 / R1 character
   switch is on `GetRepeat`, which is wall-clock paced on both of its branches**, so holding L1 or R1
   on the Status screen walks the character cursor at a frame-rate dependent rate. A single tap is
   safe: `FFX_MenuSys_SamplePad` ORs the bit into `g_ffxMenuPadRepeat` on the fresh-trigger frame
   (`if (g_ffxMenuPadHoldCount[i] == 0) g_ffxMenuPadRepeat |= v5;`) before the timer threshold ever
   matters. Note also the test is `Held == 4` and `Held == 8` as **whole-word equalities**, so the
   repeat path is only ever the MenuSys-computed `+0x26` mask, never the pad layer's `+0x10` word,
   because `GetRepeat` only returns `+0x10` when a real dpad bit is set in the held mask.
   It does not touch pad ring `+28`, `FFX_MenuSys_SamplePad` directly or `FFX_MesWin_SamplePadPort0`.
5. **Save block?** **No. Read only.** Zero write references into 0x112CA90..0x1133350 anywhere in the
   closure, and no call to any of the eleven named commit functions. The two functions in the closure
   that take a save pointer are both readers:
   `FFX_Menu_StatusBuildAbilityList 0x8D2E10` fills the **caller's** output array (`a5`) from char
   records and equipment entries, and `sub_8D5180` writes only to its own stack frame (I listed every
   memory-destination instruction in both).
6. **Contended single globals?** Yes, three shapes.
   * **`g_ffxMenuCharCursor 0x1841C28`.** Module 6 is a **writer**, through
     `FFX_Menu_CursorCharPrev 0x8AAFA0` and `FFX_Menu_CursorCharNext 0x8AAF10`, and that cursor is the
     one menu-wide "which character" with 46 readers. Same shape as the dialogue focus at `shell+6`:
     one slot, two potential actors. It also writes `g_ffxMenuCharCursorDir 0x1840886`.
   * **`g_ffxMenuStatusPanels 0x186A820[0..5]`** and **`g_ffxMenuStatusMainWindow 0x186A838`**, one
     slot each. `FFX_Menu_StatusCreateListPanel` overwrites `g_ffxMenuStatusPanels[panel]`
     unconditionally, so a second concurrent Status screen would leak the first panel object out of
     the 32-entry pool.
   * **`FFX_Module_Start(1, 0)` on exit**, guarded only by `descriptor+0x20 == 0`. Two closes would
     re-enter the main menu twice.

---

## 3. MODULE 9 = THE CUSTOMIZE SCREEN

### 3.1 Identity

* Main menu row id 7, label "Customize". `StartSubmoduleByResult` case 7 at 0x8E256E.
* Its own code says so. `FFX_Menu_CustomizeStep 0x8D5830`, which the exec installs at menu-object +12
  through `FFX_Menu_CustomizeCreateListPanel 0x8D6D60`, calls `sub_8DDF60(7)` for its help line.
* Strings in the same translation unit: `TK:SND:KAIZOU`, `scene23`, `SELCOUNTER:%d`, `OK CANCEL`,
  `TK:MN2:CLOSE`.
* Its layout keys (`FFX_Menu_CustomizeLoadLayout 0x8D6BE0`, which is module 9's PREPARE through the
  thunk at 0x8D55D0) are the `k` family: `pos_kabplt`, `pos_ksobiplt`, `pos_ksobiplt2`,
  `pos_kitmplt`, `pos_kzonmplt`, `r_kbackplt`, `r_kmojiplt1..4`, `r_kscrol`, `pos_kmaru`,
  `pos_klshohi`, `pos_krshohi`, `col_shohiplt0/1`. k = kaizou, sobi = 装備 equipment,
  itm = item, shohi = 消費 consumption. An equipment screen that consumes items is Customize.
* Availability gate: scenario word 0 or >= 1096, which is when Rikku joins.

### 3.2 Structure

The exec is a thin shell. It calls the three pad getters and **discards all three return values** (the
calls are there for ordering only, confirmed in the pseudocode and the register use).

```
0  FFX_Menu_CustomizeCreateListPanel()  -> 1
1  wait for *(char*)(g_ffxMenuCustomizeListPanel + 69) < 0   -> 2
2 -> 3 -> 4  (empty)
4  FFX_Module_Suspend(9); FFX_Module_Start(1, 0)
```

INIT (`0x8D55E0`) fetches the full equipment id list with `sub_7ABD10(-1, -1, 0, &count)` into
`g_ffxMenuCustomizeEquipList 0x186AA38`, then trims trailing entries whose `entry+2` is 0 and stores
the usable count in `g_ffxMenuCustomizeEquipCount 0x186AA3C`.

All the behaviour is in `FFX_Menu_CustomizeStep 0x8D5830`, a 14-state machine on
`g_ffxMenuCustomizeState 0x186AA68`. It drives the cursor by calling
`FFX_MenuObj_StepListCursor 0x8B44B0` first (section 5), then:

```
 1  set help string id 7, open
 2  cursor; on confirm, FFX_Menu_CustomizeIsEntryEligible(entryId, 0)
      non-zero -> sound 0x80000001, state 5   (verified at 0x8D58CA: test eax,eax / jz)
      zero     -> sound 0x80000003, message 26 or 27 depending on entry+3 & 0x0C, state 3
 5  sub_8D6CE0(cursor) opens the ability list, state 7
 7  pick an ability; dispatch on BYTE2(dword_1597770[row]) 0x0B / 0x0E / 0x0F / 0x10 / default
 13 THE COMMIT, see below
 8  sub_8C2C10("scene23"); reset; back to 1
```

### 3.3 The six co-op answers for module 9

1. **What is it?** The Customize screen. Evidence in 3.1.
2. **RNG?** **It reaches a function containing an RNG call, and the call is dead on that path.**
   Report it as "no", but here is the full story, because the prior pass's "exactly one RNG path"
   conclusion missed this edge entirely.

   Path, nine hops:
   ```
   FFX_Menu_SuspendModule9Customize 0x8D5650
     -> FFX_BootTask_Push(0, sub_8CFD70, "MODEL")      <- function POINTER, not a call
        -> sub_8CFD70   (the MODEL task: reload changed weapon / armour models)
           -> maybe_FFX_Ch_Spawn 0x872190 -> FFX_Ch_Allocate 0x824F90
              -> sub_7FC780 -> sub_7FD7F0 -> sub_7FEBA0
                 -> sub_7B7170 -> FFX_Rand_Stream
   ```
   Two independent gates kill it, both checked in the disassembly:
   * In `sub_7FEBA0` the whole block containing the call is inside
     `if (FFX_Btl_GetPhase() != 0)`, which is false in the field menu.
   * At the call site 0x7FEC57 the third argument is `push 0` (0x7FEC53), and inside `sub_7B7170` the
     RNG block is guarded by `cmp [ebp+arg_8], 0` at 0x7B7210. `FFX_Rand_Stream(16)` at 0x7B7222 and
     0x7B7236 shuffle a command list and only run when that argument is non-zero. The only caller
     that passes 1 is `sub_7AFE00` at 0x7B02E2, which is battle code and is **not** in module 9's
     closure.

   Nothing in the closure touches `g_ffxRandStreamState` or `g_ffxBattleRandState` directly, and
   `FFX_Btl_Rand 0x7989A0` is not in the closure at all.

   **Modules 4 (Equip) and 12 (the equipment shop) push the same MODEL task**, at 0x8CF5C8 and
   0x8D7472. So this is a shared non-issue, not a module 9 quirk, but it does mean the earlier
   "exactly one RNG path from the menu" figure was derived from an edge relation that ignored
   function pointers handed to a task queue.
3. **Wall clock?** **Yes, draw only, and one extra harmless one.**
   * `FFX_Menu_DrawGaugeBarClockPulsed 0x8D3690` again, reached through object slot +16:
     `CustomizeCreateListPanel -> sub_8D6860 (draw) -> sub_8FF490 -> 0x8D3690`.
   * `sub_8C0580` and `sub_8C0670` do `cos(FFX_Time_AppElapsedSeconds() * 8.0) * 3.0` into an X
     position and then `sub_903BB0`. Draw only, that is the blinking cursor arrow.
   * `GetTickCount` is reachable, at `sub_6EA050` through
     `SuspendModule9Customize -> sub_8CFD70 -> FFX_Ch_DisposeIfLive -> FFX_Ch_Dispose ->
     FFX_Chr_FreeClonedClassCharacter -> sub_6EBEE0 -> sub_6EB3E0 -> sub_6EA050`. It is used once, as
     `dword_CE6EC4 = (GetTickCount() ^ 0x55555550) & 0xFFFFFFF0 | 8`, which is an allocator cookie
     seeded on first use. It never reaches game state. Worth knowing only because it means "no
     GetTickCount in the menu" is not literally true for this module.
   * **No clock value reaches anything module 9 commits.** The commit arguments are the equipment
     entry id, the ability id and the material count, all taken from cursor indices and kernel tables.
4. **Input, and is it deterministic?** The screen navigates on `FFX_MenuObj_StepListCursor 0x8B44B0`,
   which moves the cursor on `FFX_MenuSys_GetRepeat & 0x1000 / 0x4000` and pages on
   `Repeat & 1 with Held == 1` and `Repeat & 2 with Held == 2`. **`GetRepeat` is wall clock on both
   branches, so the cursor is frame-rate dependent.** Confirm is `Pressed & 0x20`, cancel is
   `Pressed & 0x40`, both deterministic. No `+28` read, no direct `FFX_MenuSys_SamplePad` call.
5. **Save block? YES, and two of the writes bypass every named commit function.** This is the one
   finding in this document that changes a conclusion in `MENU_SYSTEM.md`.

   | VA | function | what it writes | save offset | bucket |
   |----|----------|----------------|-------------|--------|
   | 0x8D5680 | `FFX_Equip_AddAbilityToEntry` | `entry + 14 + 2*n`, the first free ability slot, with `entry+11` as the slot count | inside `g_ffxEquipmentArray` at **+0x449C** (22 bytes per entry, 200 entries) | equipment array |
   | 0x7993B0 | `FFX_Equip_RefreshEntryNameId` | `entry + 0`, the recomputed display name id from `FFX_Equip_ComputeNameId` | same, **+0x449C** | equipment array |
   | 0x790550 | `FFX_SaveData_AddItem(material, -n)` | item ids and counts | **+0x3ECC** and **+0x40CC** | item arrays |

   The first two reach the block through `FFX_SaveData__getEquipEntry 0x7ABBD0`, which returns
   `&g_ffxEquipmentArray + 22 * slot` where `g_ffxEquipmentArray` is **0x1130F2C**, i.e.
   `g_ffxSaveData + 0x449C`, inside the 26,816-byte run. Both are called from
   `FFX_Menu_CustomizeStep` state 13 at 0x8D5C0C and 0x8D5C11, in that order, followed by
   `FFX_SaveData_AddItem` at 0x8D5C20.

   **On top of that, module 9's SUSPEND hook is itself a large save-block write.**
   `FFX_Menu_SuspendModule9Customize 0x8D5650` is
   `j_FFX_SaveData_RecomputeAllCharDerived(); sub_8D57F0(); FFX_BootTask_Push(0, sub_8CFD70, "MODEL");`
   and `FFX_SaveData_RecomputeAllCharDerived 0x786900` sets the byte at **0x112A9D7**
   (`g_ffxEncountersEnabled`, which is **outside** the save block, it sits just below 0x112CA90) to 1
   and then runs `FFX_SaveData_RecomputeCharDerived` over all **18** character records, which writes
   derived fields in `g_ffxCharRecords` at **+0x55CC** and, through `sub_785C50 0x785C50`, ability
   bits either at `record + 62 + 2*(id/16)` or in the global word array at **+0x3D6C**. Modules 4 and
   1 do the same thing, so the desync detector should expect the equipment, char-record and
   ability-bitmask buckets to all move when any of those screens closes, even if the player changed
   nothing.
6. **Contended single globals?**
   * `g_ffxMenuCustomizeListPanel 0x186AA30`, `dword_186AA34` (the ability sub-panel),
     `g_ffxMenuCustomizeEquipList 0x186AA38`, `g_ffxMenuCustomizeEquipCount 0x186AA3C`,
     `g_ffxMenuCustomizeState 0x186AA68`: one slot each, all overwritten unconditionally on open.
   * `byte_186AA70` and `byte_186AAB0`: two single string buffers holding the equipment name before
     and after the customise, compared with `strcmp` to pick message 30 or 31.
   * It **reads** `g_ffxMenuCharCursor` (through `FFX_Menu_GetCursorChar`) in `sub_8C2390`,
     `sub_8D5F60` and the draw `sub_8D6860`, but it never writes it, and the commit does **not** use
     it. **The commit identity is the equipment entry**, selected out of the whole 200-entry array:
     `FFX_SaveData__getEquipEntry(g_ffxMenuCustomizeEquipList[listCursor])`. So an ownership rule for
     Customize has to read the entry's owner byte, exactly like the Equip screen, not the character
     cursor. This matters: a player could customise a teammate's weapon.

---

## 4. MODULE 20 = THE PS2 HDD INSTALL SCREEN (dead code)

### 4.1 Identity, and the proof that it cannot run

* Main menu row id 20, and `mmain_txt.bin` id 20 is literally **"HDD"**.
* Its only `FFX_Module_Start(20, ...)` site in the entire binary is `FFX_Menu_StartSubmoduleByResult`
  case 20 at **0x8E250E**. I enumerated every call site of `FFX_Module_Start 0x8AA100` (32 sites) and
  `FFX_Module_Suspend 0x8AAD70` (29 sites) with their pushed immediates to establish that.
* **Row id 20 can never be produced.** `dword_1871568` has exactly two xrefs that write it, both in
  `FFX_Menu_BuildMainMenuRows`, and the general path fills it from the bit indices of a mask built
  like this, read as bytes:

  ```
  008E1DDD  BE 15 00 00 00          mov  esi, 15h          (or 11h on the other branch)
  008E1DE2  81 CE 00 80 00 00       or   esi, 8000h
  008E1DF1  83 CE 08                or   esi, 8
  008E1E00  81 CE 80 00 00 00       or   esi, 80h
  008E1E06  81 CE 02 03 00 00       or   esi, 302h
  008E1E2B  83 CE 40                or   esi, 40h
  008E1E43  83 CE 20                or   esi, 20h
  ```
  Maximum value **0x83FF**, bits 0..9 and 15 only. Bit 20 is never set, so slot 11 is never filled.
  The special single-row modes produce 13, 12, 14, 10, 11 and 55, none of which is 20 either.
  (`mov esi, 20h` at 0x8E1ECF is the 32-iteration loop bound, not a bit. That is the kind of thing
  worth saying out loud given how this codebase reads.)
* The closure's strings are unambiguous about what it would have done: `Mount pfs`, `hdd0:%s,%s`,
  `ffxpassf`, `pfs0:`, `pfs0:ffx%d`, `cannot mount partition %d`, `hdd_mount_in` / `hdd_mount_out_ok`
  / `hdd_mount_out_err`, `PP.SLUS-20312.0.FF10`, `PP.SCES-50490..50494.0.FF10`, `PP.SLPS-25088.0.FF10`
  (the PS2 HDD partition names for each FFX disc id), `read module file : %s`, `%ld check ok`,
  `%ld cmp err %s !!!!!!!!!`, `size err !!!!!!!`. The IDB already carried `hdd_install_check 0x779220`
  and `hdd_thread_main 0x7795A0` next door.

### 4.2 Structure

```
exec  FFX_Menu_ExecModule20HddInstall 0x8E2870
  state 0: FFX_Menu_HddInstallSpawnTask()        -> state 1
  state 1: if g_ffxMenuHddInstallDone != 0 then Suspend(20); Start(1, 0)

FFX_Menu_HddInstallSpawnTask 0x8E2930
  g_ffxMenuHddInstallDone = 0;
  t = g_ffxMenuSaTask;                            // 0x1840874
  if (t) (*(fn**)(t + 8))(FFX_Menu_HddInstallTaskBody, 0, 0);

FFX_Menu_HddInstallTaskBody 0x8E2910
  j_sceRead();                                    // -> sceRead 0x777F00
  g_ffxMenuHddInstallDone = 1;
```

`g_ffxMenuSaTask 0x1840874` is the menu's PS2 `sa_task` handle, assigned in `FFX_MenuSys_RunFrame` at
0x8AB134 from `sa_task__f77A1D0(2, &unk_1586F70, 0)`. `sceRead 0x777F00` mounts the HDD partition via
`sub_779490`, reads `pfs0:ffx%d` in 1 MiB chunks and byte-compares it against the disc copy, logging
`check ok` or `cmp err`, so it is the install-and-verify pass. It even polls the pad itself to let you
abort, with `FFX_Pad__readPressed16` at 0x778389 and 0x7783A2 testing `al & 0x40` (Cross) in a
`sub_77A4E0` (task_wait) spin.

Even if it were reachable it would hang on this port: `sub_77A4B0` and `sub_77A4E0`, the task create
and task wait primitives, are the stubs that print
`Virtuos Warning: Asm function 'task_create_asm' not implementd` and `'task_wait' not implementd`.
With no task spawned, `g_ffxMenuHddInstallDone` would never be set and state 1 would spin forever.
That is an inference from the stub strings in its own closure, not something I executed.

### 4.3 The six co-op answers for module 20

1. **What is it?** The PS2 hard-disk install / verify screen. Unreachable.
2. **RNG?** No. Nothing in the 244-function closure calls either RNG function or references either
   state global.
3. **Wall clock?** No. No `FFX_Time_AppElapsedSeconds`, no `FFX_Time_NowMilliseconds`, no
   `FFX_Input__getTimeSeconds`, and `Phyre_Time_NowSeconds` is not in the closure at all. The only
   timing primitive is the `SleepEx`-free `task_wait` stub.
4. **Input?** The module itself reads none. `GetHeld` / `GetRepeat` / `GetPressed` are called and
   discarded. The abort poll inside `sceRead` goes **around** the menu layer and hits
   `FFX_Pad__readPressed16 0x888E80` (pad ring +6, which is `held & (held ^ prev)`, pure math and
   deterministic) directly. It is the only one of the four modules that reaches the raw pad layer.
5. **Save block?** No. Zero references of any kind into 0x112CA90..0x1133350 from the whole closure.
   It writes the PS2 HDD, not the save block.
6. **Contended globals?** `g_ffxMenuHddInstallDone 0x23CC130` (one dword, three xrefs total) and
   `g_ffxMenuSaTask 0x1840874`. Moot.

**Co-op consequence: ignore it.** It cannot be entered, it commits nothing, and it is the cheapest
possible thing to assert against. If you want a cheap tripwire, a detour on 0x8E2870 that logs and
returns would prove you never got there.

---

## 5. MODULE 23 = THE WORLD MAP VIEWER (the "Map" item)

### 5.1 Identity, with a byte-exact file match

* It is started from exactly one place: `FFX_Menu_ExecModule3Items` state 7, at **0x8C5959**,
  `FFX_Module_Start(23, 0)`, followed by state 8 which waits for bit 23 to clear in
  `FFX_Module_GetActiveMask()`.
* The condition that gets there, in state 11 of the Items screen:
  ```c
  v6 = *(_DWORD *)(panel + 88) + 2 * *(__int16 *)(panel + 72);   // item list base + 2*cursor
  if ( (FFX_Btl_GetItemRow(dword_1597770[v6], &v38)[34] & 8) != 0 )  // -> start module 23
  ```
  `FFX_Btl_GetItemRow 0x7909F0` is `FFX_KernelTable_GetRow(id & 0xFFF, dword_112A940, out)`, and
  `dword_112A940` is `battle/kernel/item.bin`.
* I pulled `new_uspc/battle/kernel/item.bin` (16,572 bytes) out of the VBF and parsed it with the
  documented header format: one range descriptor `lo=0 hi=111 stride=96 stringOffset=10752
  dataOffset=0x14`. **Exactly one of the 112 rows has bit 0x08 set at row+34: id 100, and its name
  string decodes to "Map".** Neighbours sanity-check the parse: 95 Clear Sphere, 96 Return Sphere,
  97 Friend Sphere, 98 Teleport Sphere, 99 Warp Sphere, 101 Rename Card, 102 Musk, 103 Hypello Potion,
  105 Pendulum, 107 Door to Tomorrow.
* And the image matches byte for byte. `FFX_Menu_ExecModule23WorldMap` state 2 does
  `FFX_Rom_LoadGroupEntry(group 18, entry 23, dest g_ffxMenuWorldMapImage 0x23CC480)`, state 3 waits
  for `FFX_Rom_IsReadPending() == 0` then calls
  `FFX_Gs_UploadTexture(dest, dbp = 14336, psm = 19, 0, 0, w = 512, h = 416)`.
  PS2 GS PSM 19 is PSMT8, 8 bits per pixel, so the payload is 512 * 416 = **212,992 bytes**.
  `ffx_ps2/ffx/master/uspc/menu/worldmap.fmt` in the shipped `FFX_Data.vbf` is **212,992 bytes**, and
  it is the only file of that size in the whole `menu/` directory (I listed all 44 entries under
  `master/uspc/menu/`). The HD replacement is
  `ffx_data/gamedata/ps3data/menu/d3d11/worldmap.dds.phyre`.

  For completeness on `FFX_Gs_UploadTexture 0x8B0B10`: `dword_1865EB4 = a3 | ((a7/64 | (a4 << 8)) << 16)`
  is a GS `BITBLTBUF` with `DBP = 14336`, `DBW = 8`, `DPSM = 0x13`, and `dword_1865ED0/ED4` is
  `TRXREG` with `RRW = 512`, `RRH = 416`. The `a4 == 19` case in its size switch is `w * h * 1`.

### 5.2 Structure

```
 0 -> 1 -> 2
 2  FFX_Rom_LoadGroupEntry(18, 23, g_ffxMenuWorldMapImage)   -> 3
 3  if FFX_Rom_IsReadPending() == 0:
      FFX_Gs_UploadTexture(&savedregs, image, 14336, 19, 0, 0, 512, 416)   -> 4
 4  FFX_Menu_WorldMapCreateWindow()                          -> 5
 5  wait for window phase (+100) == 128                      -> 6 -> 7
 7  if Pressed & 0x40 (Cross): sound 0x80000004              -> 8
 8  sub_8AF4F0(1, "scene30");  WorldMapSetWindowVelocity(-6) -> 9
 9  wait for window phase (+100) == 0                        -> 10
10  WorldMapCloseWindow(); FFX_Module_Suspend(23)
```

`FFX_Menu_WorldMapCreateWindow 0x8E2D60` allocates a menu object, installs `nullsub_155` at +8,
`sub_8E2B30` at +12 (step), `sub_8E2B60` at +16 (draw), sets `+88 = +92 = +96 = 128`, `+100 = 0`,
`+104 = 6`, `+62 = 7`, and stores the pointer in `g_ffxMenuWorldMapWindow 0x1871698`. `+104` is the
open / close velocity and `+100` is the resulting phase, 0 to 128.

Note that module 23 never calls `FFX_Module_Start(1, 0)`. It only suspends itself, because the Items
screen is still up and resumes when bit 23 clears.

### 5.3 The six co-op answers for module 23

1. **What is it?** The world map viewer, opened by using the "Map" key item (item id 100) from the
   Items screen. Evidence in 5.1.
2. **RNG?** **No.** Its 189-function closure contains neither RNG function and references neither
   state table.
3. **Wall clock?** **No, not even a draw-side one.** `FFX_Time_AppElapsedSeconds`,
   `FFX_Time_NowMilliseconds`, `FFX_Input__getTimeSeconds` and `Phyre_Time_NowSeconds` are all absent
   from the closure, and so are all four Win32 time imports. This is the only one of the four modules
   with a completely clock-free closure.
4. **Input, and is it deterministic?** **One read, and it is deterministic.** `Pressed & 0x40` in
   state 7. It calls `GetHeld` and `GetRepeat` and discards both. `Pressed` is pad ring +6 logic,
   `held & (held ^ prev)`, pure math. There is no cursor, no list, no repeat mask and no
   `FFX_MenuObj_StepListCursor` in the closure.
5. **Save block?** **No.** Zero writes. The only save references in the closure are the three
   read accessors `FFX_SaveData_GetCharName`, `sub_7851F0` (the language byte at +0x3D0C) and
   `FFX_SaveData__getEquipEntry`, and nothing in the closure calls the last one.
6. **Contended globals?** Two it owns outright, both single slot:
   `g_ffxMenuWorldMapWindow 0x1871698` (overwritten unconditionally by `WorldMapCreateWindow`, which
   would leak the previous object) and `g_ffxMenuWorldMapImage 0x23CC480`, a 212,992-byte staging
   buffer. Because it is only reachable through the Items screen it inherits that screen's ownership
   token, so there is no new contention shape. The only thing to replicate is the single Cross press,
   plus both machines agreeing that the Map item was used in the first place.

**Co-op consequence: this is the easiest menu in the game to sync.** One button, no state, no clock,
no RNG, no save write. The only wrinkle is the 212,992-byte ROM read on state 2, which is an
asynchronous disc read and therefore **takes a different number of simulation steps on each machine**.
State 3 spins until `FFX_Rom_IsReadPending()` returns 0. Under a lockstep gate on `FFX_MainStep` that
is fine because both machines stall together, but if the gate is per-frame rather than per-step, a
slow drive on one side shows up as one machine sitting in state 3 while the other has advanced.

---

## 6. Shared machinery I had to pin down, and the numbers

### 6.1 The menu object pool, and how to tell a step from a draw

Everything above rests on knowing which object slot runs in the step pass and which in the draw pass,
so here it is.

* Pool: 32 objects of 152 bytes at **0x1840900**, allocated by `FFX_MenuObj_Alloc 0x8AA1A0`
  (first object whose `+0x40` byte is 0), cleared by `FFX_MenuObj_Clear 0x8AA4B0`, registered by
  `FFX_MenuObj_Register 0x8AAB00` which sets `+64 = 1`, zeroes `+40` and calls `+8`.
* `FFX_MenuObj_StepAllAtPrio 0x8A9230` walks the pool, matches `obj+63` against its argument, calls
  **`obj+12`** (step), then if the kill flag `obj+65` is non-zero calls `obj+20` (teardown) and
  `obj+24` (free hook, defaulting to `sub_8AA3F0`). It is called nine times from
  `FFX_Module_StepAll` with 0..8.
* `FFX_MenuObj_DrawAllAtPrio 0x8A9690` is the twin and calls **`obj+16`**. It is called nine times
  from `FFX_Module_DrawAll`.

So: `+12` is step, `+16` is draw, `+20` teardown, `+24` free, `+28` the confirm callback, `+65` the
kill request. Both of the clock-reading paths in modules 6 and 9 hang off `+16`, which is why I can
call them draw-side with confidence rather than by eyeballing the code.

### 6.2 `FFX_MenuObj_StepListCursor 0x8B44B0`, the wall-clock cursor

This is the generic list cursor the whole menu shares, and it is where the non-determinism in module 9
actually lives. Worth having named because `MENU_SYSTEM.md` establishes that the repeat mask is
clock-paced but does not name the function that consumes it.

```
Repeat & 0x1000  Up    -> obj+72 (cursor) --, scrolls when it leaves [obj+50, obj+50 + obj+58)
Repeat & 0x4000  Down  -> obj+72 ++
Repeat & 0x0001  L2 and Held == 1 -> page up
Repeat & 0x0002  R2 and Held == 2 -> page down
Pressed & 0x20   Circle -> call obj+28, a 0 return means refuse
Pressed & 0x40   Cross  -> cancel, obj+40 = 17
```
Sounds: 0x80000001 ok, 0x80000003 refuse, 0x80000004 cancel. 49 call sites.

### 6.3 Button bit meanings, with the derivation

`MENU_SYSTEM.md` lists `0x0040 Cross` as **confirm**. In the four modules here, `0x20` is confirm and
`0x40` is cancel, consistently, and so it is in `FFX_MenuObj_StepListCursor`. The resolution is that
the mask is the **PS2 pad word rotated by 8 bits**, which also explains every other bit the doc
already has:

| PS2 bit | FFX bit | button | the menu's use |
|---------|---------|--------|----------------|
| 0x0100 L2 | 0x0001 | L2 | page up |
| 0x0200 R2 | 0x0002 | R2 | page down |
| 0x0400 L1 | 0x0004 | L1 | previous character |
| 0x0800 R1 | 0x0008 | R1 | next character |
| 0x1000 | 0x0010 | Triangle | opens the menu, tested in `FFX_MainStep` |
| 0x2000 | 0x0020 | **Circle** | **confirm** |
| 0x4000 | 0x0040 | **Cross** | **cancel** |
| 0x8000 | 0x0080 | Square | |
| 0x0001 | 0x0100 | Select | excluded from auto-repeat |
| 0x0008 | 0x0800 | Start | excluded from auto-repeat |
| 0x0010..0x0080 | 0x1000..0x8000 | Up / Right / Down / Left | cursor |

This is the PS2-era Japanese convention (Circle confirms) and the PC port does its remapping in the
input layer, not here, so what a Western player presses to confirm may well be the physical Cross
button while the bit the menu sees is 0x20. The bit numbers are what matter for replication.

### 6.4 The counts, and exactly how I measured them

I built two different reachable sets per module and report both, because they answer different
questions and because a single number here is misleading.

* **Direct-call closure.** Transitive closure over `fl_CN` and `fl_CF` xrefs only, seeded with the six
  live descriptor slots of each module. **Module 6: 38. Module 9: 31. Module 20: 14. Module 23: 32.**
  This badly under-counts because every menu screen installs its real logic as a function pointer in
  a pool object.
* **Pointer-following closure.** The same, plus `fl_JN` / `fl_JF` (thunks) and `dr_O` where the xref
  target is the entry point of a function, i.e. "this function's address is taken here".
  **Module 6: 370. Module 9: 1,432. Module 20: 244. Module 23: 189.** This over-counts, because taking
  an address is not calling it and because shared engine tables drag in unrelated code (zlib strings,
  Phyre asset loading, FMOD). **Every negative result in this document is stated against the
  over-counting set**, which is the conservative direction, and every positive result is
  hand-verified at the instruction level.

`MENU_SYSTEM.md` gives 58 / 21 / 11 for modules 6 / 9 / 20. Neither of my two numbers matches any of
those, and since the doc does not state its edge relation I cannot say where the difference is. Not a
correction, just a note that the figure is not reproducible as written.

One place a loose window over-counted and I caught it, flagged because it is exactly the trap the
project has been bitten by: scanning for references into `g_ffxRandStreamState + 0x200` reported three
hits in modules 6, 9 and 23. Hand-checking the addresses showed them at **0x1135FF4, 0x1135FFC and
0x1136008**, all past the end of the 68-dword table at 0x1135FF0. They are adjacent globals read by
`sub_799420` and `FFX_Btl_GetAbilityRecord`, not RNG state. With the exact 272-byte window the only
hits anywhere are `FFX_Rand_Stream` itself inside module 9's closure. The same applies to
`g_ffxBattleRandState 0xC42200`, which is 8 bytes and is immediately followed by
`g_ffxRandStreamMul 0xC42208` and `g_ffxRandStreamXor 0xC42318`, both read-only tables.

### 6.5 Hook-target prologue check

Measured by walking instruction boundaries from each entry point until at least 5 bytes are covered,
then flagging any rel32 branch inside the stolen window.

**Clean 5-byte steal, no branch and no absolute:**

| VA | name | bytes |
|----|------|-------|
| 0x8D1E00 | `FFX_Menu_ExecModule6Status` | `55 8B EC 51 53` |
| 0x8D5680 | `FFX_Equip_AddAbilityToEntry` | `55 8B EC 56 57` |
| 0x8A9230 | `FFX_MenuObj_StepAllAtPrio` | `55 8B EC 56 57` |

**Needs more than 5 stolen bytes but nothing to fix up** (the extra bytes are plain instructions, and
absolute immediates do not need relocation):

| VA | name | steal |
|----|------|-------|
| 0x8D5830 | `FFX_Menu_CustomizeStep` | 6 (`55 8B EC 83 EC 08`) |
| 0x8B44B0 | `FFX_MenuObj_StepListCursor` | 6 |
| 0x8D2E10 | `FFX_Menu_StatusBuildAbilityList` | 6 |
| 0x8D2680 | `FFX_Menu_StatusDestroyListPanel` | 6 |
| 0x8B0B10 | `FFX_Gs_UploadTexture` | 6 |
| 0x8D55E0 | `FFX_Menu_InitModule9Customize` | 7 |
| 0x7993B0 | `FFX_Equip_RefreshEntryNameId` | 7 |
| 0x8D5750 | `FFX_Menu_CustomizeIsEntryEligible` | 7 |
| 0x8E24C0 | `FFX_Menu_StartSubmoduleByResult` | 7 |
| 0x8D3140 | `FFX_Menu_StatusCreateListPanel` | 7 |
| 0x8E1C60 | `FFX_Menu_BuildMainMenuRows` | 9 |
| 0x8D3690 | `FFX_Menu_DrawGaugeBarClockPulsed` | 9 |
| 0x8D6BE0 | `FFX_Menu_CustomizeLoadLayout` | 9 |
| 0x8D4920 | `FFX_Menu_StatusLoadLayout` | 5 (`68 84 28 B6 00`, one absolute push) |
| 0x8A9690 | `FFX_MenuObj_DrawAllAtPrio` | 10 |
| 0x8E2930 | `FFX_Menu_HddInstallSpawnTask` | 10 |
| 0x8D1D80 | `FFX_Menu_InitModule6Status` | 13 |

**Has a rel32 call inside the first 5 bytes, so the trampoline must relocate it:**

| VA | name | first bytes |
|----|------|-------------|
| 0x8D5540 | `FFX_Menu_ExecModule9Customize` | `55 8B EC 56 E8 ...` |
| 0x8E2960 | `FFX_Menu_ExecModule23WorldMap` | `55 8B EC 56 E8 ...` |
| 0x8E2870 | `FFX_Menu_ExecModule20HddInstall` | `55 8B EC 56 E8 ...` |
| 0x8D1DD0 | `FFX_Menu_SuspendModule6Status` | `56 33 F6 56 E8 ...` |
| 0x8D6D60 | `FFX_Menu_CustomizeCreateListPanel` | `56 E8 ...` |
| 0x8E2D60 | `FFX_Menu_WorldMapCreateWindow` | `56 E8 ...` |
| 0x8ABAB0 | `FFX_Rom_LoadGroupEntry` | `55 8B EC E8 ...` |

**The `MenuGetUiString` shape, a rel32 call as the whole first instruction:**

| VA | name | bytes |
|----|------|-------|
| 0x8D5650 | `FFX_Menu_SuspendModule9Customize` | `E8 7B DA FE FF` and nothing else to steal |

If you want one attachment point for the three screens that matter, take
`FFX_Menu_ExecModule6Status 0x8D1E00` (clean 5) and `FFX_Menu_CustomizeStep 0x8D5830` (clean 6)
rather than the module 9 exec, which needs a fixup and does nothing anyway.

---

## 7. Addresses to promote into the mod's address headers

Grouped by what they are. All are IDA VAs, subtract 0x400000 for RVA.

### 7.1 Module exec / init / suspend entry points

| VA | name |
|----|------|
| 0x8D1E00 | `FFX_Menu_ExecModule6Status` |
| 0x8D1D80 | `FFX_Menu_InitModule6Status` |
| 0x8D1D30 | `FFX_Menu_PrepareModule6Status` |
| 0x8D1DD0 | `FFX_Menu_SuspendModule6Status` |
| 0x8D5540 | `FFX_Menu_ExecModule9Customize` |
| 0x8D55E0 | `FFX_Menu_InitModule9Customize` |
| 0x8D55C0 | `FFX_Menu_DrawModule9Customize` |
| 0x8D55D0 | `FFX_Menu_PrepareModule9Customize` |
| 0x8D5650 | `FFX_Menu_SuspendModule9Customize` |
| 0x8E2870 | `FFX_Menu_ExecModule20HddInstall` |
| 0x8E28E0 | `FFX_Menu_InitModule20HddInstall` |
| 0x8E2960 | `FFX_Menu_ExecModule23WorldMap` |
| 0x8E2AD0 | `FFX_Menu_InitModule23WorldMap` |

### 7.2 Screen internals worth calling or hooking

| VA | name | why |
|----|------|-----|
| 0x8D5830 | `FFX_Menu_CustomizeStep` | the entire Customize state machine, clean 6-byte prologue |
| 0x8D5750 | `FFX_Menu_CustomizeIsEntryEligible` | the "may this be customised" predicate |
| 0x8D6D60 | `FFX_Menu_CustomizeCreateListPanel` | installs the Customize panel |
| 0x8D6BE0 | `FFX_Menu_CustomizeLoadLayout` | the Customize layout keys |
| 0x8D2E10 | `FFX_Menu_StatusBuildAbilityList` | the Status page list builder |
| 0x8D2790 | `FFX_Menu_StatusDrawListPanel` | the Status page drawer |
| 0x8D3140 | `FFX_Menu_StatusCreateListPanel` | creates Status page N |
| 0x8D2680 | `FFX_Menu_StatusDestroyListPanel` | destroys Status page N |
| 0x8D3100 | `FFX_Menu_StatusCreateMainWindow` | |
| 0x8D2660 | `FFX_Menu_StatusSetSlideVelocity` | |
| 0x8D3280 | `FFX_Menu_StatusAdvanceSlide` | integer, deterministic |
| 0x8D4920 | `FFX_Menu_StatusLoadLayout` | the Status layout keys, the identity evidence |
| 0x8E2D60 | `FFX_Menu_WorldMapCreateWindow` | |
| 0x8E2D50 | `FFX_Menu_WorldMapGetWindowPhase` | |
| 0x8E2DD0 | `FFX_Menu_WorldMapSetWindowVelocity` | |
| 0x8E2B10 | `FFX_Menu_WorldMapCloseWindow` | |
| 0x8E2930 | `FFX_Menu_HddInstallSpawnTask` | dead code, tripwire only |
| 0x8E2910 | `FFX_Menu_HddInstallTaskBody` | dead code |

### 7.3 Save-block writers the desync detector needs, and that were missing

| VA | name | writes | save offset |
|----|------|--------|-------------|
| 0x8D5680 | `FFX_Equip_AddAbilityToEntry` | equipment entry +14 + 2n | +0x449C region |
| 0x7993B0 | `FFX_Equip_RefreshEntryNameId` | equipment entry +0 | +0x449C region |
| 0x786900 | `FFX_SaveData_RecomputeAllCharDerived` | all 18 char records, plus the byte at 0x112A9D7 | +0x55CC, +0x3D6C |
| 0x7860F0 | `FFX_SaveData_RecomputeCharDerived` | one char record's derived fields | +0x55CC |
| 0x785C50 | `sub_785C50` (ability bit setter) | `record + 62 + 2*(id/16)` or the global word array | +0x3D6C |
| 0x8C30D0 | `j_FFX_SaveData_RecomputeAllCharDerived` | thunk, this is what the menu actually calls | |

### 7.4 Shared menu machinery

| VA | name |
|----|------|
| 0x8B44B0 | `FFX_MenuObj_StepListCursor` |
| 0x8A9230 | `FFX_MenuObj_StepAllAtPrio` |
| 0x8A9690 | `FFX_MenuObj_DrawAllAtPrio` |
| 0x8AA1A0 | `FFX_MenuObj_Alloc` |
| 0x8AAB00 | `FFX_MenuObj_Register` |
| 0x8AA4B0 | `FFX_MenuObj_Clear` |
| 0x8AA470 | `FFX_MenuObj_ClearAll` |
| 0x1840900 | `g_ffxMenuObjPool`, 32 objects of 152 bytes |
| 0x8E1C60 | `FFX_Menu_BuildMainMenuRows` |
| 0x8DDF80 | `FFX_Menu_GetMainMenuRowLabel` (group 9 label) |
| 0x8DDF60 | `FFX_Menu_GetMainMenuRowHelp` (group 9 description) |
| 0x8ABAB0 | `FFX_Rom_LoadGroupEntry` |
| 0x8ABB00 | `FFX_Rom_IsReadPending` |
| 0x8B0B10 | `FFX_Gs_UploadTexture` |
| 0x8D3690 | `FFX_Menu_DrawGaugeBarClockPulsed` (clock, draw only) |

### 7.5 Globals

| VA | name | size |
|----|------|------|
| 0x186A820 | `g_ffxMenuStatusPanels` | 6 dwords |
| 0x186A838 | `g_ffxMenuStatusMainWindow` | dword |
| 0x186A9FC | `g_ffxMenuStatusSlideVel` | word |
| 0x186AA00 | `g_ffxMenuStatusSlidePos` | word |
| 0x186AA30 | `g_ffxMenuCustomizeListPanel` | dword |
| 0x186AA38 | `g_ffxMenuCustomizeEquipList` | dword (pointer to WORD[]) |
| 0x186AA3C | `g_ffxMenuCustomizeEquipCount` | dword |
| 0x186AA68 | `g_ffxMenuCustomizeState` | dword |
| 0x23CC130 | `g_ffxMenuHddInstallDone` | dword |
| 0x1840874 | `g_ffxMenuSaTask` | dword |
| 0x1871698 | `g_ffxMenuWorldMapWindow` | dword |
| 0x23CC480 | `g_ffxMenuWorldMapImage` | 212,992 bytes |
| 0x1130F2C | `g_ffxEquipmentArray` | `g_ffxSaveData + 0x449C`, 200 entries of 22 bytes |
| 0x112A9D7 | `g_ffxEncountersEnabled` | byte, **outside** the save block |
| 0xC5B2D4 | `g_ffxMenuRowSortSlots` | 32 bytes, main menu row sort order by bit index |
| 0x1871568 | `g_ffxMenuRowValues` | the built row list, stride 8, value at +0 |
| 0x1871688 | `g_ffxMenuRowCount` | dword |

---

## 8. Errors found in the existing notes and IDB comments

Listed with file, the line as it reads, and the correction. I have not edited any of these files.

### 8.1 `reversing/MENU_SYSTEM.md`

1. **Line 115 (the module id table), `| 6 | 0xC5A7D8 | 0x8D1E00 | submenu |`** and the "Still
   unidentified: 6, 9, 20 and 23" line in the SECOND PASS, together with the per-screen table.
   **Correction:** 6 is Status, 9 is Customize, 20 is the PS2 HDD install screen (dead), 23 is the
   world map viewer. Evidence in sections 2 to 5.

2. **"WHICH MODULE IS WHICH SCREEN (question 3)" table, the row
   `| 3 | **Customise** | calls FFX_SaveData_AddItem (spends the materials) ... |`.**
   **Correction: module 3 is the ITEMS screen.** Group 9 id 0 is "Items" and result 0 launches module
   3. Its own code agrees: state 14 branches on `byte_186A392` and restores HP via `sub_8C8C40` or MP
   via `sub_8C8D50`, state 15 does `FFX_SaveData_AddItem(itemId, -1)`, and the state-53 submenu has
   four entries whose handlers are use / sort (`FFX_SaveData_SwapEquipEntries` at state 42, which is
   what `TK:EXCHANGE:%d <> %d` and `TK:SRC = %d / DST = %d` are about) / and a third list operation.
   `TK:EXCHANGE` is an item-list sort, not a customise.

3. **Same table, the row `| 7 | **Items** | FFX_SaveData_AddItem with a negative delta, plus
   sub_785C50 to set a learned ability when a sphere or an Al Bhed primer is used. String
   `TK:SMN:Learn id = %d / command = %d(0x%x) / result = %d`. |`**
   **Correction: module 7 is the AEONS screen.** Group 9 id 5 is "Aeons" and result 5 launches module
   7. `SMN` in its own debug string is **summon**, and feeding an item to an aeon to teach it an
   ability is exactly `AddItem` with a negative delta plus `sub_785C50`. The availability gate agrees:
   row bit 5 needs `sub_8AB1E0()` on top of scenario 208, so you need an aeon.

4. **"WHERE THE MENU COMMITS TO THE SAVE BLOCK (question 1)": "No function in the menu touches the
   save block by address" and "the whole in-game menu commits through about ten named functions".**
   **Correction: incomplete, and the gap is the Customize screen.** `FFX_Equip_AddAbilityToEntry
   0x8D5680` and `FFX_Equip_RefreshEntryNameId 0x7993B0` write equipment entries **in place** through
   the pointer `FFX_SaveData__getEquipEntry 0x7ABBD0` returns, which for a normal slot id is
   `g_ffxEquipmentArray + 22 * slot` at `g_ffxSaveData + 0x449C`. Neither has a named setter and
   neither is in that table of eleven.
   The three checks in the doc could not have caught it: check one only looked at functions in the
   ranges 0x8A0000-0x8F0000 and 0xA40000-0xA90000 that reference the block **by address**, and these
   write through a pointer; check three followed `FFX_SaveData__getCharRecord` but **not**
   `FFX_SaveData__getEquipEntry`. For the record, `getEquipEntry` has 20 in-closure callers for module
   9 alone.

5. **Same section: the ten named commit functions should gain
   `FFX_SaveData_RecomputeAllCharDerived 0x786900` (reached from the menu through the thunk
   `0x8C30D0`).** It is called by modules 4, 9 and 1 on close and it writes all 18 character records
   plus the ability bitmasks, so a per-bucket hash will see those buckets move on every menu close
   even when the player changed nothing. It also sets the byte at 0x112A9D7 to 1.

6. **"Does the menu read the clock or any RNG? (question 4)": "RNG: no, not on any decision path ...
   reached exactly one RNG function ... by exactly one path".**
   **Correction: there is a second path, and the walk's edge relation is why it was missed.**
   Module 9's suspend hook (and modules 4 and 12) push `sub_8CFD70` to the boot-task queue as a
   **function pointer**, so a direct-call walk cannot see it. That task reaches `FFX_Rand_Stream`
   through `maybe_FFX_Ch_Spawn -> FFX_Ch_Allocate -> sub_7FC780 -> sub_7FD7F0 -> sub_7FEBA0 ->
   sub_7B7170`. The conclusion still holds, because the call is dead twice over (section 3.3 item 2),
   but "exactly one path" is not true and the method that produced it will miss every callback the
   menu hands to a queue.

7. **"The menu's button conventions" table, `| 0x0040 | Cross | **confirm** (module 10 state 3,
   `Pressed & 0x40`) |`.**
   **Correction: 0x0040 is Cross and it is CANCEL. Confirm is 0x0020, Circle.** The mask is the PS2
   pad word rotated by 8 bits, which reproduces every other row of that same table (section 6.3).
   All four modules here and the shared `FFX_MenuObj_StepListCursor 0x8B44B0` use 0x20 to confirm and
   0x40 to cancel. What module 10 does with `Pressed & 0x40` in its state 3 is therefore worth
   re-reading, since it is probably the exit rather than the confirm.

8. **"Still unidentified" paragraph: "Module 9's subtree is 21 functions and module 20's is 11 ...
   Module 6 is 58 functions."**
   **Note rather than correction:** I get 38 / 31 / 14 / 32 with a direct-call closure and
   370 / 1,432 / 244 / 189 with a pointer-following one, for modules 6 / 9 / 20 / 23. The doc's
   figures are not reproducible as written because the edge relation is not stated.

9. **Same paragraph: "Module 23 is reached only from module 3, so it is a Customise sub-screen."**
   The first half is right, the inference is not, because module 3 is Items, not Customise. Module 23
   is the world map the "Map" item opens.

10. **"Module 21: probably Overdrive mode. Not proved."** Now proved from the string table: group 9
    id 15 is "Overdrive" and result 15 launches module 21.

11. **Module id table, `| 14 | 0xC5A0B0 | 0x8C3340 | game mode 0x80000, FMV / movie |`.**
    **Probably wrong.** `FFX_Menu_BuildMainMenuRows` maps its `a2 == 14` single-row mode to row value
    14, and group 9 id 14 is **"Name Entry"**. The same construction gives 12 -> "Equipment"
    (the equipment shop, module 12), 13 -> "Items" (the item shop, module 13), 15 -> "Save",
    16 -> "Load", which all match the doc. I did not read module 14's exec, so this is flagged rather
    than asserted.

### 8.2 IDB comments

1. **`FFX_GetAssetLoader 0x76D0D0`: "Slot usage seen across 175 call sites: ... [2](x) resets."**
   Slot [2] is `libmscd__f76C490`, and with argument 1 it does **not** reset. It scans the request
   table at `dword_1127CB0` (stride 56, busy byte at +1) and returns 1 while any read is outstanding,
   0 when none is. Only the `a1 != 1` branch calls `libmscd__f76E660`, which is the flush. Every
   caller in the game uses `[2](1)` as a wait-for-load poll. I have added a comment saying so on
   `FFX_Rom_IsReadPending 0x8ABB00`.

2. **`Phyre_Time_NowSeconds 0x42FD80`: "Consumers: ... FFX_Input__getTimeSeconds 0x630C40".**
   Wrong address, and it is the exact confusion the brief warns about. 0x630C40 is
   `FFX_Input__clearThreadedSampleQueues`. `FFX_Input__getTimeSeconds` is **0x630C60**. The same stale
   address appears in `FFX_MenuSys_GetHeld`'s comment chain indirectly. The IDB's own symbol table
   already has both names right, so only the prose is stale.

3. **`0x8D1D30` was named `__cfltcvt_init_99`** by a bogus library signature match. It is module 6's
   PREPARE hook. Renamed `FFX_Menu_PrepareModule6Status`. (`0xA42BA0` carries the same bogus
   `__cfltcvt_init_143` name and is unrelated to CRT float conversion too, though I did not chase it.)

4. **`FFX_Menu_ExecModule3Customise 0x8C5450` and `FFX_Menu_ExecModule7Item 0x8CC330`** carried the
   wrong screen names. Renamed to `FFX_Menu_ExecModule3Items` and `FFX_Menu_ExecModule7Aeons`, with
   the evidence in repeatable comments on both. **If you have either old name in a header, it needs
   changing, and the two are each other's swap.**

---

## 9. What I could not settle, ranked by how much it would bother me

1. **The exact ROM group-18 entry list, so "entry 23 = worldmap.fmt" is a size match rather than an
   index derivation.** The group base comes from `*((short*)dword_2310C6C + group)` and that index
   table is built at runtime from disc data, so I could not enumerate it statically. The size match
   is byte exact and unique within `menu/` (212,992 = 512 * 416, and no other file in that directory
   has that size), and the counted position of `worldmap.fmt` among the 24 top-level files in
   `master/uspc/menu/` is 22, adjacent to 23. I am confident in the identification and not in the
   index arithmetic. A runtime read of `dword_2310C6C` would close it in one line.

2. **What the four Status page codes 0x40, 0x41, 0x44 and 0x45 name.** I know the mechanism
   (`FFX_Menu_StatusBuildAbilityList` filters the `sub_790200(0, ...)` table on those as the first key
   byte and tests `maybe_FFX_SaveData_TestEventFlag(char, id)` per entry) and I know panel 0 is the
   equipment auto-abilities. I did not identify which ability class each of the four is, so I cannot
   say "panel 2 is White Magic". That needs the `sub_790200` table dumped at runtime or the matching
   kernel file parsed.

3. **Module 20's behaviour if it ever did run.** I asserted it would hang because `task_create_asm`
   and `task_wait` are stubs in its own closure. I did not trace `sa_task__f77A1D0`'s returned table
   to prove slot +8 lands on the stub. The reachability proof does not depend on this.

4. **Whether `FFX_Rom_IsReadPending` can return 0 on the very first poll.** Module 23's state 3 would
   then upload from a buffer that has not been filled. `FFX_Rom_LoadGroupEntry` only starts the read
   when the size lookup is non-zero and does not check that the start succeeded, and state 2 advances
   to 3 unconditionally. I could not rule out a one-frame race here, and it is the kind of thing that
   would show up as a one-machine-only garbage frame rather than a desync. Low stakes.

5. **Module 14.** I flagged it as probably "Name Entry" rather than FMV from the single-row mode
   mapping alone and did not open its exec. Out of scope for this task, listed so it is not lost.

6. **Module 6's `sub_8A8E50` gate (`byte_1340859`) and `sub_8AB220` (`dword_1840878`).** The first
   gates state 0 and the second gates the L1 / R1 character switch. Both are plain byte and dword
   reads, so they are not a determinism risk by themselves, but I did not find out what sets them. If
   either is set from anything frame-timing-dependent, module 6's entry would take a different number
   of steps per machine. `byte_1340859` is also tested at the top of `FFX_Module_DrawAll`, which
   suggests it is a "menu is up" flag rather than anything timed.

7. **Whether a second concurrent Status or Customize screen is even representable.** My contention
   answers list the single globals, but the design replicates input into one shared menu, so the real
   question is whether the mod will ever let two players be in different per-character screens at
   once. If the answer is no, `g_ffxMenuCharCursor` stops being a contention and becomes a value the
   desync detector should simply hash. I have described the mechanism rather than guessed the design.
