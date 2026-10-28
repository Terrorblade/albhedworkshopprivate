# International content: Eternal Calm, the environment switch, Dark Aeons, Penance and the grids

Written 2026-10-28 for Project Zanarkand item 1.12.11. Every address is an IDA VA in FFX.exe
(RVA + 0x400000). Native claims come from the IDB through idalib on a private copy. Script claims
come from the shipped event packages through `../tools/ebp.py`, with battle ids resolved through
`btl.bin` and the monster tables. File names come from `cdrom.fid` and `cdrom.fnd` (`ASSET_PATHS.md`
section 3). Names that were placeholders are proposed by `../tools/apply_international_names.py`.
Save block offsets are from `g_ffxSaveData 0x112CA90`. Script variable class 0 offset N is block
`+0x1EC + N` (`ATEL_CONTEXT.md`).

Marks: [V] read from code or data, [I] inferred, [D] dead in this build.

Companion docs: `COMMAND_LINE.md` (the `_ECalm` token), `BOOT_SEQUENCE.md` (where the boot reads
it), `EVENT_LAYER.md` (the boot scene window 393..399), `MAP_TRANSITIONS.md` (event 399 quits),
`SAVE_FILE.md` (the load path), `CHEAT_SAVEDATA.md` 6.5 (the grid assets), `STEAM_ACHIEVEMENTS.md`.

## 0. The answer in one paragraph

This build has no International switch in the sense the name suggests. The one function that
would choose between the original and the International content, `FFX_GetInternationalEnvId
0x7851D0`, is `xor eax, eax / retn`, and every one of its readers is text, font, layout or asset
set selection. None of them gates a fight or a grid. The Dark Aeons, Penance and the Expert grid
are all unconditional content. The Dark Aeons and Penance are gated only by event script bytecode
that reads the scenario word and a run of flag bytes in the save block. The grid type is two bits
of the save block option dword, chosen once by the player at New Game. Eternal Calm is a launcher
token, `argv[1] == "_ECalm"`, that boots into the seven `sceneN.ebp` packages (events 393..399)
instead of the title (event 23). The swap routine `TOSwapInternationalEnvExec 0x8B0500` is still
called on every PC load, but it is only ever stepped once there, so it does nothing except leave a
busy flag set and clear option bit 16.

## 1. The gate table

| # | gate | what it controls | where the decision lives | readers | data paths it switches | sim state | in the save block |
|---|---|---|---|---|---|---|---|
| 1 | `_ECalm` | boot into Eternal Calm instead of the title | `argv[1]`, string at 0xB0DA90, re-read each time | `FFXApplication__initApplication` 0x42F826 (title), `FFX_BootSequence` 0x64177C (skips `OpeningScreen.swf`), `FFX_MainInit` 0x820A43 (first event) | events 393..399, `host0:/ffx/master/jppc/event/obj/sc/sceneN/sceneN.ebp` from `g_ffxBootPackagePaths 0xC334B8` | yes, a different first event | no, nothing stored |
| 2 | International env id | text, font, layout and asset kind choices | `FFX_GetInternationalEnvId 0x7851D0`, hardcoded 0 | about 74 functions, section 3 | kind 48 vs 3, 13 vs 7, 18 vs 4, kernel string groups 3/7/9 vs 16/14/15 | no, a constant | a copy at save header `+0x1F`, display only |
| 3 | the swap on load | reload of nine `menu/` files and a battle reload | `g_ffxIntlEnvSwapEnabled 0x1865CDC` and `g_ffxLoadIntlEnvSwapPending 0x1866240` | `FFX_SaveFile_CommitLoad` 0x8B4F6B, `FFX_SaveUi_CloseAndSwapEnv 0x8B3DB0`, `FFX_SaveLoadMenu_StepModule 0x8B15D0` states 105..112 | `jppc/menu/` (kind 18) entries 11, 10, 15, 14, 8, 6, 5, 7, 22 | no, see section 4 | clears option bit 16, block `+0x3D0C` |
| 4 | Dark Aeons | eight optional fights | script bytecode in eight packages | section 5 | `<scene>_70` battle dirs | yes | scenario word `+0xBEC`, flag bytes `+0xC89..+0xC90`, `+0xC91` |
| 5 | Penance | the final optional fight | script bytecode in `nagi0000`, `hiku2100`, `matu0000` | section 6 | `hiku15_70` | yes | `+0xC91`, `+0xC82` bit 8 |
| 6 | sphere grid type | which board, starting nodes and node effects | option dword block `+0x3D0C` bits 14..15 (`g_ffxSaveDataOptionFlags 0x113079C`) | section 7 | `jppc/menu/abmap/dat01..03` (layout), `dat09..11` (node kinds), `dat00` or `dat12` (board effect) | yes, stats come off the grid | yes |
| 7 | `IsWesternMasterVersion` (core:595) | per script timing and text branches, Eternal Calm included | LanguageManager master version | 181 script calls in 38 packages (`ATEL_CORE_LIBRARY.md` 5.4) | none | yes where it changes wait counts | no |

Rows 1 and 7 are machine local and can differ between peers. Rows 4, 5 and 6 ride in the save
block. Row 2 is a constant. Row 3 is local and has no simulation effect.

## 2. Eternal Calm [V]

**How it is reached.** The Steam launcher `FFX&X-2_LAUNCHER.exe` carries
`<SideStory>FFX.exe</SideStory><SideStoryParam>_ECalm</SideStoryParam>`, so its side story button
starts FFX.exe with `_ECalm` as `argv[1]`. `FFX&X-2_Will.exe` also holds the string (its pdb is
`SideStory_Final`). It was not examined.

**What reads it.** Three strcmps of `argv[1]` against "_ECalm" (0xB0DA90). Nothing is stored.

1. `FFXApplication__initApplication 0x42F7F0`, at 0x42F826: the window title becomes
   "FINAL FANTASY X ETERNAL CALM" (0xB0DA98).
2. `FFX_BootSequence 0x6416C0`, at 0x64177C: skips `FFX_Boot_LoadOpeningScreenSwf`, sets
   `app+0x3A0 = 1` and presents 30 frames through `FFX_Boot_PresentFrame`. `app+0x3A0` is the
   present and loading flag, not an Eternal Calm flag.
3. `FFX_MainInit 0x820860`, at 0x820A43. The debug check comes first, so `debug` wins. Otherwise
   the strcmp at 0x820A50..0x820A77 falls through to `push g_ffxFirstBootSceneEventId` at 0x820A7A,
   and the init calls `FFX_Map_RequestChange(393, 0)` and `maybe_FFX_Scene_Init(393)` in place of
   23.

**What it loads.** The boot scene window is events 393..399 (`FFX_Ev_IsBootSceneEvent 0x67AB50`,
`(id - 393) <= 6`). Their packages are not in the event table. They come from the hardcoded path
table `g_ffxBootPackagePaths 0xC334B8`, `host0:/ffx/master/jppc/event/obj/sc/sceneN/sceneN.ebp` for
N = 1..7, plus motion groups from `g_ffxBootMotionGroupPaths 0xC334D4`. The size reader
`FFX_Ev_MeasureHost0File 0x67A9F0` logs "failed to open EternalCalm data file", which is how
Virtuos named this set.

**The script chain.** scene1 (393) -> 394 -> 395 -> 396 -> 397 -> 398 -> 399.
- scene5 plays movies through `movie:0`, `movie:1`, `movie:3` and `movie:9`.
- scene6 unlocks achievement 33 (`ACH_A_JOURNEY_CATALYST`), confirmed by 58.
- 399 is `g_ffxQuitPseudoMapId 0xC33480`. `FFX_Map_RequestChange(399)` sets `g_ffxQuitPhase`, the
  copyright screen draws for 304 frames, then `FFX_MainStep_CheckQuit` posts `WM_CLOSE`
  (`MAP_TRANSITIONS.md`). So Eternal Calm ends by quitting to desktop.
- scene1 also carries the interactive branches seen in `matu0000` (warps to 347, 374, 255 and 194).
  They are not reached in practice because input is zeroed in the window [I].

**What gates it while it runs.** `FFX_Ev_IsBootSceneEvent` has 21 readers (`EVENT_LAYER.md`). The
two that matter here: `FFX_Input__poll` zeroes the button mask, so the side story is input free,
and `sub_86BAE0`, the menu open gate, refuses the menu. The scenes call `core:595
IsWesternMasterVersion` a lot (scene6 48 times, scene3 18, scene2 17, scene1 16, scene5 9, scene4
5). An example: scene1 at 0x2069 waits 15 extra frames when the master version is JP or Asia. That
is a timing difference, not a content difference.

## 3. The International env id [V]

`FFX_GetInternationalEnvId 0x7851D0` is three bytes, `xor eax, eax / retn`. Its thunk is
`j_FFX_GetInternationalEnvId 0x86BBC0`. About 74 functions read it. Grouped by what they do with
the value:

- **Asset kind choice, JP set when 0.** `sa_menu_start` (kind 48 `_cdindex_mon_gra` vs 3
  `_cdindex_mon_gra_inter_us`, into `dword_C416C4`), `FFX_SphereGrid_ReadPanelTables 0xA54AB0` (13
  `_cdindex_battle` vs 7 `_cdindex_battle_us`), `FFX_IntlEnv_BeginSwap 0x8B0920` (18
  `_cdindex_menu` vs 4 `_cdindex_menu_inter_us`, into `g_ffxIntlEnvMenuAssetKind 0xC59564`), and
  the kernel string group redirect (3/7/9 vs 16/14/15, `CHEAT_SAVEDATA.md`).
- **Text and layout.** Map names, the font sheet, glyph and layout readers.
  `FFX_Atel_GetEntryMapNo` (env 0 indexes the override table by text language).
  `FFX_MesWin_TypewriterAdvance` (`GetTextSpeedScale(env)`, so slot 0). core:466 picks its message
  table by it.
- **Recorded and compared.** `FFX_SaveFile_Serialize` writes it to header `+0x1F`. `FFX_Btl_Init`
  stores it in `g_ffxBtlLoadedIntlEnvId 0x112CA34`. `FFX_Menu_ConfigInitRowValues` stores it in
  `g_ffxMenuConfigEnvIdAtOpen 0x186A43C`. `FFX_MenuSys_Enter` stores it in
  `g_ffxMenuSysEnvIdAtEnter 0x186A1CC` and compares it again later. Every compare is 0 == 0.
- **Dead because of the compares.** `FFX_Btl_ReloadForIntlEnvStep 0x782090` returns 0 when the env
  id equals `g_ffxBtlLoadedIntlEnvId` (0x7820AA), so the battle reload it would run never runs.
  `FFX_Menu_ExecModule10Config` state 4 never reaches states 6..8. `FFX_IntlEnv_SyncOnReturnToTitle
  0x86ECE0` (from `FFX_Map_ReturnToTitle`) only reloads when the id changed across a call to itself.
  core:600 has no script callers.

No reader gates a Dark Aeon, Penance or the grid type. Kernel tables are read by
`FFX_Btl_ReadKernelBin 0x782DF0` from `new_<lang>pc` by `FFX_GetLanguage`, not by the env id.

The PS2 text language `g_ffxMesWinTextLanguage 0x1330830` and its variant
`g_ffxMesWinTextLangVariant 0x1330833` feed several of these. No script writes either: `save:93`,
the only scripted writer of the language, has zero call sites in the shipped packages. The variant
setter `FFX_MesWin_SetTextLangVariant` is called only from `SG_DebugWin_BattleConsoleProc` and
`FFX_Btl_ParseBtlEnvConfig` (a btl env file loader). So both are 0 in retail [V for the writers,
I for the env file never being present].

## 4. TOSwapInternationalEnvExec and what a load actually does [V]

**The machine.** `TOSwapInternationalEnvExec 0x8B0500` is a 29 state machine. The state is the
plain dword `g_ffxIntlEnvSwapState 0x1841D28` (the decompiler shows it as a pointer, it is not),
one state per call. States 2..21 call `FFX_Rom_LoadGroupEntry(g_ffxIntlEnvMenuAssetKind, entry,
dest)` and wait on `FFX_Rom_IsReadPending` for entries 11, 10, 15, 14, 8, 6, 5, 7 and 22. With kind
18 those are `jppc/menu/` `meswin.fmt`, `battle.fmt`, `strtex.fmt`, `icon.fmt`, `menu.clp`,
`spcodedic.bin`, `albheddic.bin`, `ffxsjistbl.bin` and `battle_script.bin`. Kind 4 would be the
`uspc/menu/` copies. States 23..25 loop `FFX_Btl_ReloadForIntlEnvStep`, which is dead (section 3).
State 28 sets `g_ffxIntlEnvSwapDone 0x1841D2C = 1` and clears `g_ffxIntlEnvSwapBusy 0x1841D30`.
State 0 sets the busy flag. It returns the done flag.

`TOSwapInternationalEnvExecSync 0x8B0340` does the same loads in one call through `sub_8ABA30` and
then `FFX_Btl_ReloadForIntlEnvSync 0x782050`. Its callers are the core:600 poll and
`FFX_IntlEnv_SyncOnReturnToTitle`, both dead for the reload. `TOSwapInternationalEnvExecMenu
0x8B0440` loads entry 13 (`menumain.msb`) of `dword_C59978`, set by `FFX_IntlEnv_BeginSwapMenu
0x8B0970`, both only from the dead Config states.

`FFX_IntlEnv_BeginSwap 0x8B0920(env)` resets the machine and calls
`FFX_SaveData_SetTextVariantBit 0x785ED0(env)`. That writes option bit 0x10000 at block `+0x3D0C`:
cleared when the text language is 1 or 18, else set to the argument. With env 0 it is always
cleared. A scan of every access through `g_ffxSaveDataOptionFlags` and
`FFX_SaveData_GetOptionFlagsPtr` (86 sites) found no reader of bit 16 other than the setter itself.

**The enable.** `FFX_IntlEnv_IsSwapEnabled 0x8AC390` returns `g_ffxIntlEnvSwapEnabled 0x1865CDC`.
`FFX_MesWin_SelectFontForLanguage 0x8AD900`, called once from `TOInit`, sets it to 1 when the PS2
text language is 0 and the variant is 0, 1 or 2. That is always true on PC.

**The load path.** `FFX_SaveFile_CommitLoad 0x8B4EC0` is the only PC install (`SAVE_FILE.md`
6.3). At 0x8B4F43 it sets `g_ffxLoadIntlEnvSwapPending = 1`, then:

```
008B4F6B  call    sub_8AC390                 ; FFX_IntlEnv_IsSwapEnabled
008B4F70  cmp     eax, 1
008B4F73  jnz     short loc_8B4F9B
008B4F75  cmp     dword_1866240, eax         ; the pending latch, just set to 1
008B4F7B  jnz     short loc_8B4F9B
008B4F7D  call    FFX_GetInternationalEnvId  ; 0
008B4F82  push    eax
008B4F83  call    sub_8B0920                 ; FFX_IntlEnv_BeginSwap(0)
008B4F8B  call    TOSwapInternationalEnvExec ; one step
008B4F90  dec eax / neg eax / sbb eax, eax / and dword_1866240, eax
```

So the swap is started on **every** load, whatever language the save was made in. But it is
stepped exactly once: state 0, which sets the busy flag and moves to state 1. The latch stays 1.
`FFX_SaveUi_CloseAndSwapEnv 0x8B3DB0`, reached from `FFX_SaveUi_Step` on a cancel, a failed verify
or a finished save, restarts it and steps it once more. Nothing on the PC path steps it further.
Only the menu save/load module `FFX_SaveLoadMenu_StepModule 0x8B15D0` (module table
`off_C59FF0`) runs it to completion, in states 105..112, and that module is the PS2 style load
which the PC does not use for installs [I, from `SAVE_FILE.md`].

**The consequence.** After the first load on PC, `g_ffxIntlEnvSwapBusy` stays 1 for the rest of
the process. Its one reader, `sub_8ACF20`, is used by `FFX_Font_UploadMenuSheets 0x8B1190`, which
then skips two `FFX_Gs_UploadTexture` calls (`dword_1841E00` and `dword_1841E08`). Whether that is
visible on PC was not checked [I]. No file is reloaded, and the only block change is bit 16
cleared.

## 5. The Dark Aeons [V]

All eight live in event script bytecode. The flag run is one byte per aeon at block
`+0xC89..+0xC90` (class 0 offsets 0xA9D..0xAA4). Bit 0x80 means defeated. The low bits are per
aeon progress steps. The common gate is the scenario word at block `+0xBEC` (class 0 0xA00) past
2385, which `mcfr0700` actor 9 event 9_1 sets at 0x0ABF.

| aeon | flag | package (event id) | trigger and fight site | battle dir | extra gate |
|---|---|---|---|---|---|
| Dark Valefor | `+0xC89` | `bsvr0000` (17) | a49 e49_1 trigger, a48 e48_2 `btl:2` at 0x14747, battle id 0x480046 | `bsil07_70` | a49 0x15594 `pushvar blk+0xBEC, pushimm 2385, gt, jz`, then 0x155A3 tests `+0xC89 & 0x80` clear |
| Dark Ifrit | `+0xC8A` | `bika0300` (138) | a25 0x3BCC | `bika03_70` | bit 2 set by a19 e19_2, scenario > 2385 at a19 0x2965 |
| Dark Ixion | `+0xC8B` | `kami0300` (162) | a29 0x938A | `kami03_70`, plus `kami03_71` sites | bits 2 and 4 are steps, scenario > 2385 at a7 |
| Dark Shiva | `+0xC8C` | `mcyt0000` (153) | a53 0x8EE8 | `mcyt00_70` | a37 scenario ranges, including >= 2385 |
| Dark Bahamut | `+0xC8D` | `dome0600` (270) | a17 0x4363 | `dome06_70` | scenario select at 2900, 2830 and 2815 |
| Dark Yojimbo | `+0xC8E` | `nagi0500` (56) | a25, five stages | `nagi05_70..74` | bit 1 set in `nagi0400` a17 when block `+0x279` bits are set |
| Dark Anima | `+0xC8F` | `mtgz0000` (259) | a33 0x135A4 | `mtgz01_70` | bit 2 set in `mtgz0700` when scenario > 2680 |
| Dark Magus Sisters | `+0xC90` | `kino0000` (79), `kino0100` (92), `kino0500` (128) | several | `kino00_70`, `kino01_70/71/72`, `kino05_70/71` | bits 1, 2 and 4, scenario >= 2385 |

On a win (`btl:53()` GetForcedResult `== 2`) the fight's package sets that aeon's 0x80 bit. When all
eight 0x80 bits are set, the same setter sets block `+0xC91 = 1` and warps through `core:267` to
`nagi0000` (event 223). Dark Flan (`mtgz06_02`) is an ordinary encounter, not part of this chain.

No `core:595`, no env id and no other native gate appears anywhere in these chains. A battle id is
`(mapId << 16) | enc` for `btl:2` RequestScriptedBattle (`ATEL_BTL_LIBRARY.md`).

## 6. Penance [V]

1. `nagi0000` a72 tests `+0xC91 == 1` at 0x1D1ED and sets the scenario word to 2400.
2. `nagi0000` a88 e88_6 sets `+0xC91 = 2` (0x2A60B) and `+0xC82 |= 8` (0x2A619).
3. The airship destination script `hiku2100` a9 e9_6 tests `+0xC82 & 8` at 0x2444 and calls
   `core:17(392, 1)`, a map change to `matu0000` (event 392).
4. `matu0000` a14 e14_25 at 0x216C and a22 e22_1 at 0x37AF call `btl:2(0x18B0046)`, battle
   `hiku15_70`: Penance with its Right Arm and Left Arm.
5. On a win the script clears `+0xC82` bit 8 and returns to 374, 255 or 194 by scenario.

## 7. The sphere grid type [V]

**Where it is stored.** Bits 14..15 of the option dword, block `+0x3D0C`,
`g_ffxSaveDataOptionFlags 0x113079C`.

**Where it is chosen.** The title script `test20` (event 23), actor 3 events 3_4 and 3_5, calls
`core:573` BeginNewGame. `FFX_InitNewSaveData` copies the `menu.bin` template (option flags
0x00002F61, so grid type 0) and runs `FFX_SphereGrid_InitNodes` with type 0. Then a `core:315`
choice offers "Standard Sphere Grid" and "Expert Sphere Grid", each with a "cannot switch Grids in
game" confirm. Standard pushes 1 and calls `core:596` at 0x5B7. Expert pushes 2 and calls it at
0x628. `FFX_AtelSys_Core_596_SetSphereGridTypeAndInit 0x8585D0`:

```
008585E2  call    FFX_Atel_PopInt
008585E9  mov     ecx, esi
008585EB  shl     ecx, 0Eh
008585EE  xor     ecx, [edi]          ; edi = FFX_SaveData_GetOptionFlagsPtr()
008585F5  and     ecx, 0C000h
008585FB  xor     [edi], ecx          ; bits 14..15 = gridType
008585FD  call    FFX_SphereGrid_InitNodes
```

core:597 at 0x858430 reads the type back.

| type | meaning | layout (`FFX_SphereGrid_LoadLayout 0xA45570`) | node kinds (`FFX_SphereGrid_InitNodes 0xA53DE0`) | nodes | start nodes | pre-activated lists |
|---|---|---|---|---|---|---|
| 0 | the template value, the pre-International grid [I] | kind 37 index 9, `jppc/menu/abmap/dat01.dat` | index 17, `dat09.dat` | 828 | `0xC86C00`: 0, 341, 540, 637, 56, 224, 448 | `0xC86C5C` |
| 1 | Standard | index 10, `dat02.dat` | index 18, `dat10.dat` | 860 | `0xC86C10`, the same seven | `0xC86CA4` |
| 2 | Expert | index 11, `dat03.dat` | index 19, `dat11.dat` | 828 | `0xC86C20`: 43, 9, 38, 13, 24, 19, 80 | `0xC86CEC` |

Kind 37 is `_cdindex_menu2`, base 511 in `cdrom.fid`. Type 0 is reachable only through the
template and the debug and test packages (`test22`, `testbattle`). It shares its start nodes with
Standard but has its own layout (828 nodes against 860), and no menu text names it, so it reads as
the board the International edition replaced. That label is inferred, not read [I].

**Other readers of the type.**
- `FFX_SphereGrid_CheckGridTrophies 0xA47210` loads the layout for the type.
- Expert only (`(flags & 0xC000) == 0x8000`): `FFX_SphereGrid_LoadBoardEffect 0xA57620` (from
  `FFX_Menu_Build`) loads kind 37 index 20 `dat12.dat` for Expert, else index 8 `dat00.dat`, and
  hands it to `FFX_Oef2_SetFromUnit`. `FFX_SphereGrid_ActivateNodeForChar 0xA48910` picks effect 4
  for Expert and 0 otherwise, then sets the activation bit, `FFX_SphereGrid_MenuToSave`,
  `FFX_SphereGrid_RecomputeDerived` and the trophy check. `sub_A48740` picks effect 5 vs 3.
  `sub_A4C700` (from `FFX_Menu_DrawAllPartsIgnoringCatchUp`) draws. All four are presentation
  except that the activation itself is the same for every type.
- `panel.bin` and `sphere.bin` (`FFX_SphereGrid_LoadPanelTables 0xA54810` ->
  `FFX_SphereGrid_ReadPanelTables 0xA54AB0`) are the same for every grid type. Their numeric
  fields are identical across all ten language trees, only the string offsets differ.

`FFX_SphereGrid_InitNodes` has two callers, `FFX_InitNewSaveData` and `core:596`. After New Game
the node state lives in the block (`CHEAT_SAVEDATA.md` 6.1), so the type and the nodes travel
together.

## 8. Simulation and co-op

| gate | peers must agree | how they agree | hazard |
|---|---|---|---|
| `_ECalm` | yes | not at all, it is per process | a peer launched from the side story button boots into 393 and the other into 23. A session must refuse a peer whose `argv[1]` is `_ECalm`, or only start after both are in the main game |
| env id | no, a constant 0 | the binary | none in this build. A recreation that adds a real switch must make it session wide |
| swap on load | no simulation effect | none needed | a load leaves `g_ffxIntlEnvSwapBusy` at 1. Harmless for the simulation. The two skipped texture uploads are local |
| Dark Aeons, Penance | yes | the save block carries every flag | none beyond keeping the block in sync. The script reads only the block |
| grid type | yes | the save block carries it and the nodes | the choice is made on the title screen inside the New Game script, so it must be the host's choice relayed as input, like any other `core:315` answer |
| `IsWesternMasterVersion` | yes, for timing | not at all, it follows the master version | peers on a JP or Asia master and a western master take different wait counts in the same scripts. `ATEL_CORE_LIBRARY.md` 5.4 already flags it. Require one master version per session |

Option bit 16 is written on every load and on return to title, but nothing reads it, so it is a
checksum difference at most. Exclude it from a block hash or clear it on both sides [V for no
reader through the two access paths, I for no other path].

## 9. Corrections to other docs

- `WORLD_STATE.md` section 1, row 7 said the swap runs "only when the saved language differs from
  the running one". It is started on every load, regardless of language, and stepped only once
  (section 4).
- `WORLD_STATE.md`, the grid type note said "0 Standard, 1 and 2 the other two layouts". It is 0
  the template and pre-International grid, 1 Standard, 2 Expert.
- `SAVE_FILE.md` 6.3 step 8 and 11 item 4 said the swap runs when the saved language differs.
  Same correction as `WORLD_STATE.md`.
- `MENU_SYSTEM.md` module 10 row said the Config screen "compares the map id". It compares the env
  id, and the path is dead (the same doc's later correction already says so).
- `MENU_SYSTEM.md` RNG paragraph said the Config and Save/Load screens can reach the encounter roll
  through an environment reload. The reload is `FFX_Btl_ReloadForIntlEnvStep`, which returns 0 at
  0x7820AA when the env id equals `g_ffxBtlLoadedIntlEnvId`. Both are 0, so the path is dead from
  every caller.
- `CHEAT_SAVEDATA.md` 6.5 labelled type 0 Standard and called 1 and 2 "the second grid" and "the
  third grid". Relabelled. Its open gap, "could not resolve a class-37 index to a filename", is
  closed: kind 37 is base 511 in `cdrom.fid`.
- `STEAM_ACHIEVEMENTS.md` 5.12 said "Standard, Expert and the third". It is the template, Standard
  and Expert.
- `ATEL_CORE_LIBRARY.md` row 600 said it "picks language 4 or 18". Those are asset kinds 4
  (`_cdindex_menu_inter_us`) and 18 (`_cdindex_menu`), not languages.
- `ASSET_PATHS.md` 2.2 left open which scripts call `save:93`. None do.

## 10. Open

- `FFX&X-2_Will.exe` holds `_ECalm` and was not examined.
- Whether the two texture uploads skipped by `FFX_Font_UploadMenuSheets` after a PC load are
  visible.
- Whether `FFX_SaveLoadMenu_StepModule` is reachable on PC at all. `SAVE_FILE.md` says the PC
  installs only through `FFX_SaveFile_CommitLoad`.
- The Config row callback `sub_8CAFF0` (table entry 0xC5A4B8) also writes option bit 16 from a row
  value. Which row, and whether it is shown on PC, was not traced. Nothing reads the bit.
- Type 0 as the pre-International grid rests on the layout and start nodes, not on a string.
- The IDB comment on `FFX_SphereGrid_InitNodes` and the CHEAT_SAVEDATA labels it came from may
  still say "0 Standard". The names script can only comment the rows it renames, so fix that
  comment by hand.
