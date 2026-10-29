# The save block, every byte

`g_ffxSaveData 0x112CA90 .. 0x1133350`, 0x68C0 = 26,816 bytes. This is the byte map: what every
field is, who reads and writes it, whether it is real state, derived state or scratch, and which
bytes nothing uses. It also settles whether the block can ever hold a pointer (it cannot, section 5)
and lists the fields whose value depends on the machine or the moment (section 6).

The same layout as data is `tools/saveblock_fields.py`. It has 191 top-level rows plus the three
record layouts (equipment entry 13 rows, character record 33, name record 3), and
`python -I tools/saveblock_fields.py check` proves they tile 0..0x68C0 with no gap and no overlap.
`ffx_types.h` carries the same layout as `SAVEDATA`, `SAVE_CHAR_RECORD`, `SAVE_EQUIP_ENTRY` and
`SAVE_CHAR_NAME`, generated from that file.

**Unknown bytes remaining: 0.** Every byte is in one of four classes.

| class | bytes | meaning |
|---|---|---|
| persistent | 19,371 | real state, written by play, the same meaning on any machine |
| derived | 402 | recomputed by the engine from other state, each step or on a recompute |
| scratch | 25 | only means something inside one map, scene or operation, or nothing in retail reads it |
| unused | 7,018 | nothing reads or writes it beyond whole-block copies and clears |

The derived count includes the derived fields inside the 18 character records (max HP and MP,
effective stats, auto-ability words). The 8,192-byte script work area counts as persistent where some
shipped script declares a variable and unused where none does.

Other docs this builds on: `GAME_STATE.md` (the region map, now pointing here), `WORLD_STATE.md`
(transplant sites, the header's map record), `CHEAT_SAVEDATA.md` (the record layouts),
`SAVE_FILE.md` (the file image and CRC), `ATEL_CONTEXT.md` (class 0 variables).

Tags as elsewhere: [V] read in the code, [I] inferred.

---

## 1. Method

Every access to the range was collected six ways and the results merged. All of it ran on a copy of
the IDB through idalib, read only.

1. **Direct operands.** Every instruction in every executable segment with a memory, displacement
   or immediate operand inside `0x112CA90..0x1133350`. This catches `mov eax, offset g_ffx...` and
   `[reg + 0x112xxxx]` indexed forms, which is how most of the record arrays are reached.
2. **Hex-Rays taint.** 690 functions that touch a block address. Every expression whose value is a
   block address (a direct reference, the return of one of the block getters such as
   `FFX_GetSaveData`, `FFX_SaveData_GetCharRecord`, `FFX_SaveData__getEquipEntry`, or arithmetic on
   either) is followed through assignments, so `base + index * stride + field` resolves to an offset
   relative to the record. Reads, writes and read-modify-writes are recorded with size. Calls that
   receive a block pointer as an argument are followed into the callee (memcpy, memset, qmemcpy,
   strcpy, and the engine's own helpers).
3. **Escapes, by hand.** The taint reports every place a block pointer leaves the function, into a
   global, a struct field or a local array dereferenced later. All 18 were read by hand (section 5),
   because a pointer parked in a local array and dereferenced later is the shape the taint misses.
   `FFX_Btl_LoadCharRecordIntoActor` is one: `v32[i] = FFX_SaveData__getEquipEntry(...)` and the
   reads of entry +8, +9 and +0xA happen two loops later.
4. **The file images.** The engine writes into the save file image, not only the live block.
   `g_ffxSaveFileStaging 0x1597F70` is 0x6900 bytes with the block at +0x40, so staging
   `+0x40 + x` is block offset `x`. Every function that calls `FFX_SaveMgr__getStagingImage` or reads
   the autosave or op buffer pointers was scanned with an assembly register tracker that only counts
   a displacement when the base register provably came from one of those sources. This found the
   saved player position at +0x9C, which nothing touches in the live block.
5. **Scripts.** The ATEL class 0 variables live at `g_ffxSaveData + 0x1EC + offset`
   (`FFX_Atel_ResolveVarAddress 0x86C2E0`). A census over every shipped script recorded each
   class 0 declaration (offset, type, count) and every variable opcode that touches it: all 397
   event packages, every monster file's script, every battle script under `/battle/btl/`, and the
   menu context's script `menu/menumain.bin` (ROM kind 18 entry 12), which has 2 actors, 3,526
   instructions and no variables at all. The stack effect of each instruction was simulated to find
   where every argument of a variable-taking syscall came from (section 5).
6. **Real saves, read only.** The 12 saves in the user's save folder, parsed through
   `tools/savefile.py`, give per-byte statistics: zero in every save, the same nonzero value in
   every save, or varying. No value is copied out and nothing there is written. This was used only
   to back up a finding, never as the finding.

A byte is classed unused only when all of these agree: no direct operand, no taint access, no
staging access, no script declaration, and no record-relative access through any record accessor.

---

## 2. The map

### 2.1 Header, +0x0000 .. +0x01EC

| off | size | type | field | readers and writers | class |
|---|---|---|---|---|---|
| +0x00 | 2 | u16 | live map id | `FFX_SaveData_GetMapId 0x88D660`, written by `FFX_Map_RequestChange` and `FFX_Map_RequestResumeFromCheckpoint` | persistent |
| +0x02 | 2 | u16 | previous map id | `FFX_SaveData_GetPrevMapId 0x88D600`, same writers | persistent |
| +0x04 | 2 | u16 | scene id | `FFX_SaveData_GetSceneId 0x88D690`, written by `FFX_SaveData_SetSceneAndSub 0x88EAF0` | persistent |
| +0x06 | 2 | u16 | previous scene id | `FFX_SaveData_GetPrevSceneId`, SetSceneAndSub | persistent |
| +0x08 | 2 | u16 | scene sub id | `FFX_SaveData_GetSceneSubId`, SetSceneAndSub | persistent |
| +0x0A | 2 | u16 | previous scene sub id | `FFX_SaveData_GetPrevSceneSubId`, SetSceneAndSub | persistent |
| +0x0C | 1 | u8 | live entry point | `FFX_SaveData_GetEntryPoint 0x88D670`, RequestChange | persistent |
| +0x0D | 1 | u8 | previous entry point | `FFX_SaveData_GetPrevEntryPoint`, RequestChange | persistent |
| +0x0E | 2 | u16 | map name id | `FFX_SaveData_SetMapNameId`, `SetMapNameIdFromGroundDic` on map entry, `GetMapNameId 0x86C410` | derived |
| +0x10 | 1 | u8 | encounter ground attribute | written every step by `FFX_Atel_PullActorPosFromChr`, `FFX_Atel_SetActorPos`, `SetActorPosXZ`, `FFX_SaveData_SetEncounterGroundAttr`. Read by `FFX_Atel_StepFieldFrame` and core:176 | derived |
| +0x11 | 1 | u8 | warp fade kind | core:188 `FFX_SaveData_SetWarpFadeKind 0x8700C0`, read by `FFX_Map_WarpToWithSavedFade` | persistent |
| +0x12 | 1 | u8 | fade-out frames | core:187 `FFX_SaveData_SetFadeOutFrames 0x8700D0` | persistent |
| +0x13 | 1 | u8 | field flags, bit 1 encounters allowed | `FFX_Atel_StepFrame` rewrites it every step, Reset sets `(old & 1) \| 2`, `sub_88E570` `& 0xF9 \| 2` | derived |
| +0x14 | 4 | | **unused** | Reset writes 0 only | unused |
| +0x18 | 8 | u64 | 64 header bits | `FFX_SaveData_TestHeaderBit18 0x86B680`, `SetHeaderBit18 0x8700E0` | persistent |
| +0x20 | 1 | u8 | party snapshot valid | `FFX_Atel_PushPartySnapshot 0x86E310`, `PopPartySnapshot 0x86DDB0`. core:275 reads it and is unused by any script | persistent |
| +0x21 | 3 | u8[3] | party snapshot | Push / Pop | persistent |
| +0x24 | 4 | u32 | party snapshot mask | Push / Pop | persistent |
| +0x28 | 1 | u8 | indoor | `FFX_Field_SetIndoor 0x865740` copies `g_ffxFieldIndoor`, which `FFX_Field_UpdateIndoorFlag 0x871E50` sets from camera height against ctx+520. Read by core:403 and 416 (PlaySe), `FFX_Atel_LoadResourceList` and others | derived |
| +0x29 | 1 | u8 | map out flag, bit 0 | `FFX_Field_UpdateIndoorFlag`, `FFX_Field_GetIndoorFlag` | derived |
| +0x2A | 1 | u8 | loaded from file | `FFX_SaveData_MarkLoadedFromFile 0x88EA50` sets it on the copy `FFX_SaveFile_Serialize` writes, `SetSceneAndSub` consumes and clears it | scratch |
| +0x2B | 1 | u8 | script byte | core:250 writes it (9 script uses), `sub_88E570` clears it. Only core:249 (no script uses it) and the debug page `sub_8807B0` read it | scratch |
| +0x2C | 32 | u32[8] | LoveParam | core:301..304, `sub_86BCC0` (picks the top one), `FFX_Atel_GrantTalkBonusOnce`, `maybe_FFX_SaveData_AdjustAffection`, `sub_7871B0` | persistent |
| +0x4C | 80 | u8[80] | 640 event flag bits | core:305, 568, 569, `FFX_Atel_GrantTalkBonusOnce` | persistent |
| +0x9C | 12 | f32[3] | **player position at save time** | file image only, see 2.1.1 | persistent |
| +0xA8 | 2 | u16 | checkpoint BGM | `maybe_FFX_Scene_Init` writes, `FFX_SaveUi_BeginSave` sets the staging copy to 0xFFFF for a manual save, the load hands it to `dword_C526D0` | persistent |
| +0xAA | 2 | u16 | checkpoint movie word | `maybe_FFX_Scene_Init` writes `word_C322A8 + 1`, see 2.1.2. 0 in manual saves | persistent |
| +0xAC | 8 | | **unused** | Reset fills it, nothing reads | unused |
| +0xB4 | 2 | u16 | world map marker X | core:366 (323 of 397 packages), read by `sub_873AE0` for the save screen | persistent |
| +0xB6 | 2 | u16 | world map marker Y | core:366, `sub_873AF0` | persistent |
| +0xB8 | 2 | u16 | checkpoint entry | `maybe_FFX_Scene_Init`, core:367, 368. Read by RequestChange and RequestResumeFromCheckpoint | persistent |
| +0xBA | 2 | u16 | checkpoint map | same | persistent |
| +0xBC | 4 | u32 | playtime, seconds | `FFX_Save_StepPlayTimeFromWallClock 0x86CA20`, `FFX_SaveData__setPlaytime 0x787610`. Read by `sub_787350` (debug) and `sub_8A9C50` | persistent, wall clock |
| +0xC0 | 16 | u16[8] | location history | `FFX_SaveData_SnapshotLocationHistory 0x88E800`, read by SetSceneAndSub and RequestChange | persistent |
| +0xD0 | 1 | u8 | battle return mode | `FFX_Btl_ReturnToField` arg 4 (6 is the special fade path), Reset writes 2. **No reader** | scratch |
| +0xD1 | 1 | u8 | Rikku alt outfit | core:404 `sub_86F060`, `sub_8DB7E0` (MenuTest). Read by `FFX_SaveData_GetRikkuAltOutfit` and Serialize | persistent |
| +0xD2 | 1 | u8 | shadow flag | only Reset writes it, `FFX_Ch_ShadowsEnabled` reads it | persistent |
| +0xD3 | 1 | | **unused** | Reset writes it as part of a dword | unused |
| +0xD4 | 2 | u16 | map name override | `FFX_SaveData_SetMapNameOverride`, read by GetMapNameId | persistent |
| +0xD6 | 1 | u8 | map name override active | Set / ClearMapNameOverride, `sub_86E700` clears | persistent |
| +0xD7 | 1 | u8 | script flag | core:445 (64 uses) and `Ev_LoadEventPackage` write through `sub_8715F0`. core:446 (no uses) and `sub_7829B0` read through `sub_86C8A0`. sub_7829B0 picks between ids 49, 63 and 101 on it | persistent |
| +0xD8 | 4 | u32 | premovie number | `FFX_Premovie_Load`, `GetNumber`, `Cancel` | scratch |
| +0xDC | 4 | u32 | premovie mode | `FFX_Premovie_Load`, `GetMode`, `sub_873FC0` | scratch |
| +0xE0 | 4 | u32 | premovie state | core:437, `FFX_Premovie_*`, `FFX_SaveData_ResetPremovieState` | scratch |
| +0xE4 | 4 | u32 | field boot flags | `FFX_SaveData_Get/SetFieldBootFlagsE4`, `FFX_Field_FreezeActorsToScratch`. Cleared by `maybe_FFX_Map_Unload` and `sub_872BB0` | scratch |
| +0xE8 | 2 | u16 | field word | `FFX_SaveData_Get/SetWordE8`, cleared on unload | scratch |
| +0xEA | 2 | u16 | script word | core:525 through `sub_870FA0`, read by core:526 (no uses) and `FFX_SaMenu_GetSaveWordEA` through `sub_86C4B0` | persistent |
| +0xEC | 32 | char[32] | name entry text | `sub_8C4E60` writes it when `dword_186A258 < 0`, core:466 at 0x860180 compares it against a message | persistent |
| +0x10C | 224 | | **unused** | Reset memsets it, nothing else | unused |

#### 2.1.1 The saved player position, +0x9C

[V] This field exists only in the file image. `FFX_MainStep_StepSaveScanAndCachePlayerPos
0x648480` calls `FFX_Player__getPos(&flt_CCB9A8)` every step, caching the local player's position in
three floats outside the block. When a save is written, the orphan at 0x6485A0 copies them into the
staging image:

```
6485A7  call    FFX_SaveMgr__getStagingImage
6485AC  fld     flt_CCB9A8
6485B2  fstp    dword ptr [eax+0DCh]     ; staging +0xDC = block +0x9C
6485BE  fstp    dword ptr [eax+0E0h]
6485CA  fstp    dword ptr [eax+0E4h]
```

`sub_648920` at 0x648DFE does the same. On load the orphan at 0x648110 copies them back out of the
staging image into the cache (`fld [eax+0DCh]` / `fstp flt_CCB9A8`). `sub_648920` and `sub_6EF6F0`
compute distances from the cached point. The copy in the live block is never read or written by
anything, so a live block always carries whatever the last install put there. The value varies
across the real saves.

#### 2.1.2 +0xAA

`word_C322A8` is set by `sub_643AA0(id, flag)`, called from `FFX_Premovie_Load`, `sub_670760` and
`FFX_SaveFile_FinishOp`. `sub_670760` also stores graphics context +4292 and +4296 through
`sub_66E800`. It is most likely the background movie id plus a flag [I], restored through
`sub_643AA0` on load. What the movie is in each case was not chased further.

### 2.2 Script work, +0x01EC .. +0x21EC

8,192 bytes of ATEL class 0 variables. Offset in the block = class 0 offset + 0x1EC.

[V] 3,997 bytes are declared by some shipped script (declared extent = element size times count),
and every one of them is touched by a variable opcode somewhere. No undeclared byte is touched by an
opcode. Array stores clamp the index to `[0, count - 1]` (ATEL_CONTEXT.md), so a script cannot write
past its own declared extent. The full run list is `SCRIPT_WORK_RUNS` in `saveblock_fields.py`, 94
runs, half declared.

Declared runs are persistent. They belong to the scripts, and their meaning is per variable. The C
side reads into a few of them:

| block off | reader |
|---|---|
| +0x034A | `FFX_MesWin_DrawInsertedValue`, a message inserts a script value. The same function unlocks achievement 16 when +0x34A == 71 and +0x34B == 50 in event 158, `STEAM_ACHIEVEMENTS.md` |
| +0x0BEC | the scenario word, `FFX_SaveData_GetScenarioWord 0x86C470` and many others. The orphan at 0x647EB0 reads it out of the staging image at staging +0xC2C |
| +0x0C39 | the seven Celestial weapons found bits, 0x01 Tidus, 0x02 Yuna, 0x04 Wakka, 0x08 Auron, 0x10 Kimahri, 0x20 Lulu, 0x40 Rikku (`MINIGAMES_PROGRESSION.md` 4.2). `FFX_MainStep` (achievement 27 when it goes from 1..126 to 127, `STEAM_ACHIEVEMENTS.md`) and core:077, which ignores the pad in event 49 once bit 0x20 is set. Script-only neighbours: +0x0C6C crest applied and +0x0C6D sigil applied, bit `1 << charId` |
| +0x0C7F | the Jecht Sphere count after the first, 9 = all ten (`MINIGAMES_PROGRESSION.md` 5). `maybe_FFX_Scene_Init`, the inline trophy 20 check at 0x88E09B. The function-less copy at 0x88ECC0 has no caller |
| +0x0C89 | `FFX_SaveData_GetFlagsC89` |
| +0x11EC | Blitz init reads 0x78 bytes |
| +0x157E | Blitz init reads 0x3C bytes |

One undeclared slot is used by C: **+0x5EC, the menu config word**, bit 0. `FFX_Menu_GetSaveConfigBit5EC`,
`FFX_Menu_ConfigRowGetSaveBit5EC` and `FFX_Menu_ApplySaveConfigBit5EC` read it as a word,
`FFX_Menu_SetSaveConfigBit5EC` and `FFX_Menu_ConfigRowSetSaveBit5EC` write the low byte. It is
class 0 offset 0x400, in a gap no script declares.

The large undeclared runs, all unused:

| run | bytes |
|---|---|
| +0x04A5 .. +0x072C | 647, less the two bytes at +0x5EC |
| +0x0AAE .. +0x0BEC | 318 |
| +0x0D66 .. +0x11EC | 1,158 |
| +0x1636 .. +0x163E | 8 |
| +0x1BEC .. +0x21EC | 1,536 |

The smaller undeclared runs, from 1 to 69 bytes, are the gaps between declared variables and are
listed in `SCRIPT_WORK_RUNS`. Clearing: `ClearScriptWorkArea` memsets the first 4 KB at new game.
The developer dump syscalls save:000 and save:001 have no script uses.

### 2.3 Sphere grid, +0x21EC .. +0x350C

0x1320 = 4,896 bytes, `FFX_SaveData_GetSphereGridNodes`. Users: `FFX_SphereGrid_InitNodes 0xA53DE0`
(memsets all 0x1320, then writes kinds, masks and the current-node words), `FFX_SphereGrid_SaveToMenu
0xA49590` (reads grid id and zoom), `FFX_SphereGrid_MenuToSave 0xA5BB70`,
`FFX_SphereGrid_RecomputeDerived 0xA54860`, `FFX_SphereGrid_CheckGridTrophies 0xA47210`, and the
orphan at 0xA5B5C7.

| block off | blob off | size | field | class |
|---|---|---|---|---|
| +0x21EC | +0x0000 | 2,560 | nodes, 1,280 x {panel kind, activation mask} | persistent |
| +0x2BEC | +0x0A00 | 1,280 | links, one byte per line, per-character mask | persistent |
| +0x30EC | +0x0F00 | 14 | u16[7] current node per character | persistent |
| +0x30FA | +0x0F0E | 10 | **unused** | unused |
| +0x3104 | +0x0F18 | 1 | grid id 0..2 | persistent |
| +0x3105 | +0x0F19 | 1 | zoom 0..3 | persistent |
| +0x3106 | +0x0F1A | 1,030 | **unused** | unused |

The grid type that picks the layout is not here. It is bits 14..15 of +0x3D0C.

### 2.4 Guide map, +0x350C .. +0x3D0C

Four records of 512 bytes, one each for scenes 582, 584, 590 and 591. **Each is a 64 x 64 bitmap of
explored cells**, 8 bytes per row. [V]

- `FFX_GuideMap_LoadSceneRecord 0x91CE50` picks the record by scene id and stores its address in
  the guide map object at `a1[28]` (+0x70).
- `sub_91F420` sets bits around the player every step it runs, on the path `FFX_MainStep 0x821525
  -> FFX_Field_DrawGuideMap -> sub_919D00 -> sub_91DA80 -> sub_91F420`. A cell is 40 world units and
  it marks a 13-cell cross pattern. The set is
  `*(u8 *)(((x + (y << 6)) >> 3) + rec) |= 1 << ((x + (y << 6)) & 7)`, at 0x91F506 `or [esi+edx], al`.
- `sub_91ECB0` reads it.
- Only ever set. Cleared at new game.

Persistent, but written from the local player's position. In co-op this diverges as soon as two
players walk different parts of one of those maps.

### 2.5 Party, config and counters, +0x3D0C .. +0x3ECC

| off | size | type | field | readers and writers | class |
|---|---|---|---|---|---|
| +0x3D0C | 4 | u32 | option bits | 0x1 and 0x80 `sub_8CB460`, 0x2 config row 3, 0x8 alternate name strings, 0x10 vibration (core:547, 548), 0x20, 0x40, 0x200 (config row 1, `FFX_Btl_GetOptionBit9UnlessFfwd`), 0x400, 0x800, 0x2000 movie option (core:586), 0xC000 sphere grid type (core:596, 597), 0x10000 text variant (`sub_785ED0`). Setters around 0x785E70 | persistent |
| +0x3D10 | 4 | u32 | Al Bhed primer mask, 26 bits | `sub_906B30`, read by `sub_86A040`, `sub_906780`, `sub_906B10` | persistent |
| +0x3D14 | 4 | u32 | battle count | +1 in `FFX_Field_StepRandomEncounter` and `FFX_Btl_StepSubPhase`, read by RecomputeCharDerived, `sub_784F50`, `sub_787220` | persistent |
| +0x3D18 | 16 | | **unused** | the tail of the party_data template copy | unused |
| +0x3D28 | 32 | u16[16] | innate ability bits, kernel table 31 | words 0..5 ids 0x3000..0x305F, words 6..15 ids 0x3060 and up. `FFX_SaveData_TestInnateAbilityFlag 0x7850B0`, `sub_785D40` | persistent |
| +0x3D48 | 4 | u32 | gil | `FFX_SaveData_GetGil`, `SetGil`, `SpendGil`, `sub_784830` | persistent |
| +0x3D4C | 4 | u32 | RNG seed base | party.bin row 0 +4 (inside the 0x84 byte party_data copy at 0x786BE3, not the conf template, corrected 2026-10-29), read only by `FFX_InitNewSaveData`: `FFX_Rand_SeedAllStreams(dword_11307DC + maybe_FFX_Tk_GetVblankCount())`, and that second term is always 0 on PC | persistent |
| +0x3D50 | 8 | | **unused** | conf template | unused |
| +0x3D58 | 3 | u8[3] | the three active party members | `FFX_SaveData__getFieldParty`, `FFX_Btl_CommitPartyToField`, `SetPartyOrderSlot` | persistent |
| +0x3D5B | 17 | u8[17] | aeon order | `FFX_Btl_CommitAeonOrderToSave`, `FFX_Btl_SetupUnitRoster` | persistent |
| +0x3D6C | 32 | u16[16] | shared ability bits, ids 0x3060 and up | `FFX_SaveData_SetCharAbilityFlag`, `FFX_SaveData_GrantAllAbilities`, `sub_79BB50`, and `FFX_Btl_ApplyPendingEffects` for achievement 25 (bits 8..19). Bytes 28..31 are not reachable by a valid id | persistent |
| +0x3D8C | 16 | u16[8] | start item ids | read once by `FFX_InitNewSaveData` | persistent |
| +0x3D9C | 8 | u8[8] | start item counts | same | persistent |
| +0x3DA4 | 4 | u32 | Yojimbo motivation, 0..255 | `sub_7B2950` (add), `sub_7B2B20` (get) | persistent |
| +0x3DA8 | 4 | u32 | Yojimbo word | Btl 233 and 234 through `sub_7B2B30` / `sub_7B2B40` | persistent |
| +0x3DAC | 4 | u32 | Tidus overdrive learn counter | `sub_7B0CD0` from the command cost path, thresholds at 0xA0A0B0C0 | persistent |
| +0x3DB0 | 4 | u32 | Rikku steal successes | `sub_78B760`, trophy 4 at 200 | persistent |
| +0x3DB4 | 4 | u32 | gil spent through command 0x302A | `FFX_Btl_StartCommandExec`, trophy 19 at 100,000 | persistent |
| +0x3DB8 | 20 | | **unused** | conf template tail | unused |
| +0x3DCC | 128 | u8[128] | world flags, 1,024 bits | `FFX_SaveData_Test/SetWorldFlag3DCC`, `FFX_SaMenu_*WorldFlag*` | persistent |
| +0x3E4C | 128 | | **unused** | the only accessor, 0x785260, has no callers | unused |

### 2.6 Inventory, roster, characters, +0x3ECC .. +0x68C0

| off | size | type | field | notes | class |
|---|---|---|---|---|---|
| +0x3ECC | 512 | u16[256] | item ids | CHEAT_SAVEDATA.md 5 | persistent |
| +0x40CC | 256 | u8[256] | item counts | | persistent |
| +0x41CC | 32 | u16[16] | item gained mask | set only, by `FFX_SaveData_MarkItemDelta 0x790670` from `FFX_SaveData_AddItem`. Nothing reads it | persistent |
| +0x41EC | 32 | u16[16] | item lost mask | same | persistent |
| +0x420C | 512 | u8[512] | capture counts | species 0..138 are valid indexes | persistent |
| +0x440C | 64 | u8[64] | monster seen mask | 512 bits | persistent |
| +0x444C | 64 | u8[64] | monster defeated mask | 512 bits | persistent |
| +0x448C | 16 | u8[16] | key item bits | ids below 0x80, 64 real | persistent |
| +0x449C | 4,400 | entry[200] | equipment, 22 bytes each | 2.6.1 | persistent |
| +0x55CC | 2,664 | record[18] | character records, 0x94 each | 2.6.2 | mixed |
| +0x6034 | 792 | u8[18][44] | per-character ability bits | `FFX_SaveData_SetCharAbility 0x785E00`, `TestCharAbility 0x785140`. Word `(id & 0xFFF) / 16`, no bound beyond the 0x3xxx class. Bytes 0..39 cover ids 0x3000..0x313F. Bytes 40..43 of each row are not reachable by a valid command id (command.bin has 320 rows) | persistent |
| +0x634C | 360 | name[18] | character names, 20 bytes each | 2.6.3 | persistent |
| +0x64B4 | 4 | u32 | CRC | file image only, 0 after a PC load (WORLD_STATE.md 2) | scratch |
| +0x64B8 | 1,032 | | **unused** | section 4 | unused |

#### 2.6.1 Equipment entry, 22 bytes

As CHEAT_SAVEDATA.md 4.1, with the readers it was missing.

| off | size | field |
|---|---|---|
| 0x00 | 2 | name id, 0x5000 based |
| 0x02 | 1 | in use |
| 0x03 | 1 | flags |
| 0x04 | 1 | for character |
| 0x05 | 1 | kind |
| 0x06 | 1 | owner |
| 0x07 | 1 | **unused.** Copied with the entry, never read or written on its own. Zero in every real save |
| 0x08 | 1 | read by `FFX_Btl_LoadCharRecordIntoActor 0x79C5F0` into actor +0x5C1 (0x79C863) |
| 0x09 | 1 | same, into actor +0x5C7 (0x79C86D) |
| 0x0A | 1 | same, both equipped entries added into actor +0x5D8 (0x79C877) |
| 0x0B | 1 | ability slot count |
| 0x0C | 2 | model id |
| 0x0E | 8 | four ability slots |

#### 2.6.2 Character record, 0x94 bytes

As CHEAT_SAVEDATA.md 3, with these settled:

| off | size | field | evidence |
|---|---|---|---|
| 0x00 | 4 | two template words | copied from `ply_save.bin`, never read. Persistent but inert |
| 0x24, 0x28 | 8 | effective max HP and MP | derived, `FFX_SaveData_RecomputeCharDerived` |
| 0x2F | 8 | effective stats | derived |
| 0x37 | 1 | poison percent | `FFX_Btl_LoadUnitParams 0x79B4F0` into actor +0x5BA |
| 0x4A | 6 | auto-ability words | derived, cleared and rebuilt on every recompute |
| 0x60 | 40 | u16[20] **overdrive mode counters**, 0xFFFF = not available | `FFX_Btl_BumpOverdriveModeCounter 0x7B10C0` counts one down for modes up to 0x10 (0x7B1142..0x7B1153 on `[esi+edi*2+60h]`). `FFX_Btl_GrantUnlockedOverdriveModes 0x7B1180` loops all 20 (`cmp esi, 14h` at 0x7B124B) and grants a mode whose counter has reached 0. `Debug_OverdriveModesOneUseAway` sets them to 1 |
| 0x88 | 4 | u32 **learned overdrive mode mask** | GrantUnlockedOverdriveModes loads it at 0x7B11F8 `mov ebx, [eax+88h]` and ORs in each new mode. `FFX_Menu_GetCharOverdriveModeMask 0x8C1BF0` |
| 0x8C | 8 | **unused** | no access through any record accessor, zero in every real save |

The generic record accessors `sub_8CECA0` and `sub_8CEE70` (offset table at 0xC5A608: +4, +8,
+0x0C..+0x13, +0x24, +0x28, +0x2F..+0x36) and `sub_8C1490` (table 0xC5A790, +0x2F..+0x36) only reach
fields already in the table.

#### 2.6.3 Name record, 20 bytes

| off | size | field |
|---|---|---|
| 0x00 | 18 | the name, FFX glyph encoded |
| 0x12 | 1 | language, `FFX_GetLanguage` when it was typed. Per machine |
| 0x13 | 1 | renamed by the player |

`FFX_SaveData_ReencodeCharNames 0x787470` re-encodes on install.

---

## 3. The block-wide paths

These copy or clear the whole block or large parts of it, and are not separate readers of any field.

| function | what |
|---|---|
| `FFX_SaveFile_InstallBlock 0x8B54A0` | `memcpy(dest, image + 0x40, 0x68C0)` |
| `FFX_SaveFile_CommitLoad 0x8B4EC0` | InstallBlock and the re-derive sequence |
| `FFX_Debug_ApplyViewerSave 0x8B55E0` | memcpy and warp |
| `FFX_AtelSys_Save_087_resi 0x8781F0` | memcpy from a script argument, one dev map |
| `FFX_SaveFile_Serialize 0x8B3E60` | memcpy into the staging image, then MarkLoadedFromFile on the copy |
| `FFX_SaveData_ResetHeaderAndScriptWork` | header defaults, see the "Reset" column above |
| `ClearScriptWorkArea` | first 4 KB of script work, new game |
| `FFX_SphereGrid_InitNodes 0xA53DE0` | memset 0x1320 |
| `FFX_InitNewSaveData` | qmemcpy of the party_data (0x1C), table 31 (0x20) and conf (0x84) kernel rows, then the records from templates |
| save:071..078 | raw block peek and poke syscalls, **no uses in any shipped script** |

---

## 4. Unused bytes, and the proof

A run is unused when nothing reads or writes it except the paths in section 3. For each run the
search was the full method of section 1: no direct operand landing in it, no taint access (which
includes `base + index * stride` through every record accessor and every memcpy, memset and qmemcpy
the taint followed), no staging image access, and no script declaration. The real-save column is
supporting only.

| run | bytes | what else touches it | in the 12 real saves |
|---|---|---|---|
| +0x0014 .. +0x0018 | 4 | Reset writes 0 | zero |
| +0x00AC .. +0x00B4 | 8 | Reset writes it | constant |
| +0x00D3 | 1 | Reset, as part of a dword | constant |
| +0x010C .. +0x01EC | 224 | Reset memset | zero |
| script work, undeclared | 4,193 | ClearScriptWorkArea (first 4 KB), no script declares a variable there, no opcode touches it | see note |
| +0x30FA .. +0x3104 | 10 | InitNodes memset | zero |
| +0x3106 .. +0x350C | 1,030 | InitNodes memset | zero |
| +0x3D18 .. +0x3D28 | 16 | party_data template qmemcpy | constant |
| +0x3D50 .. +0x3D58 | 8 | conf template qmemcpy | constant |
| +0x3DB8 .. +0x3DCC | 20 | conf template qmemcpy | constant |
| +0x3E4C .. +0x3ECC | 128 | accessor 0x785260 exists with zero callers | zero |
| equipment +0x07, x200 | 200 | copied with the entry | zero |
| record +0x8C .. +0x94, x18 | 144 | template copy | zero |
| +0x64B8 .. +0x68C0 | 1,032 | see below | zero |

The script work undeclared total is 8,192 - 3,997 = 4,195 bytes, less the two at +0x5EC, so 4,193.
It includes the small gaps between variables. Not every undeclared script byte was checked against the saves, since a script
cannot reach them (the clamp in section 2.2).

**The tail, +0x64B8 .. +0x68C0.** The only instructions that name an address in it are loop-end
compares (`byte_1132F56`, and the end compare in ReloadCharNames), not accesses. The staging image
has a pointer to its own tail: `sub_8B3630` returns staging +0x64F8 (block +0x64B8),
`FFX_SaveMgr__ctor` stores that at SaveMgr+0x35C, and `sub_8B3610` memsets 0x400 bytes there.
Nothing reads SaveMgr+0x35C. The CRC (SAVE_FILE.md) stops at +0x64B8. Zero in all 12 saves.

**Not unused, though they look it:** `+0x2B` and `+0xD0` (written, but nothing in retail reads them,
so they are classed scratch), the item gained and lost masks (set, never read, but they do hold play
history), and the two template words at record +0x00.

---

## 5. A3: can the block hold a pointer?

**No. Nothing ever stores an address into the block, and nothing ever reads a value out of the block
and uses it as an address.** [V]

The evidence, each part independent.

1. **BSS with no fixups.** The block has no initialised image and `ida_fixup` reports no relocation
   inside it (WORLD_STATE.md 2). The only data-segment dword whose value lands in the range is at
   0xC538A4, which is packed shorts in the SG_DebugHud table, not a pointer. The magic host API
   table 0xC64CE8 has no block-pointer entry.
2. **Every store of 4 bytes or more was read.** 190 distinct taint writes of size 4 or more, plus
   every direct-operand write of size 4 or more. They store arithmetic, counters, flags, clamps,
   kernel or template data, or constants. The one that looks like a pointer is
   `FFX_Debug_ApplyViewerSave` at 0x8B5642:

   ```
   8B5642  c7 05 48 cb 12 01  00 00 f6 00   mov g_ffxCurrentMapId, offset unk_F60000
   ```

   Its only fixup is at 0x8B5644, the destination address. The immediate at 0x8B5648 is not
   relocated, so it is the number 0x00F60000, map 246 entry 0.
3. **Every helper that writes into the block was checked for its source.** qmemcpy of kernel rows
   (party_data 0x1C, conf 0x84, table 31 0x20), the memsets in ClearScriptWorkArea, Reset and
   InitNodes, Blitz's memset32, ReencodeCharNames's strcpy of text, and the install memcpy from the
   file image. None copies a pointer.
4. **Every escape goes outward.** The taint found 18 places where a block address is stored
   somewhere. All 18 store the block address into something outside the block, never into it:
   - ctx+0x2C (the ATEL context)
   - two globals `dword_22FD6C0` and `dword_23C4450`, which `FFX_Ch_ShadowsEnabled` sets to &+0xD2.
     Both are write-only
   - the guide map object `a1[28]`
   - menu object +92
   - text slots and menu work name pointers

   Two looked like escapes and are not. `sub_8D79C0` stores entry+4 as a value.
   `sub_8C4E60`'s CharName[18] store is the value of
   `FFX_GetLanguage`, and the taint's path-insensitivity made it look like an address.
5. **No script stores a pointer into class 0.** A class 0 variable is never the target of an
   address value, 0 hits over every script. The only consumers of a class 0 variable reference
   (`pushvarref`, op 0x27) are core:232 in mtgz0600, which writes party character ids into it,
   core:248 DevMemsetVar in test01, which is a no-op in retail, and sys:0 DebugWatchVar, which stores
   the reference in ctx+0xA4, outside the block. Of the 52 syscalls that resolve an argument as a
   variable descriptor, none receives a class 0 literal descriptor in a variable position. The
   literals in those positions decode to classes 1, 3, 4 (type 7) and 6. The only class 0 shaped
   literals are in non-variable positions (262/1, an SE id, and 282/1, a size).
6. **The raw block syscalls are unused.** save:071..078 have no uses in any shipped script, and the
   menu script declares no variables.
7. **Round trip.** The game writes the block to disk and installs it byte for byte under ASLR with
   no fixup pass. Any stored pointer would be stale on the next run.

So the block can go on the wire or into a file as plain bytes. The engine has no pointer to fix up
after a transplant.

---

## 6. Hazards beyond pointers

### 6.1 Depends on the machine, the clock or the local player

| field | why |
|---|---|
| +0xBC playtime | wall clock. `FFX_Save_StepPlayTimeFromWallClock 0x86CA20` adds `FFX_Time_AppElapsedSeconds` (0x86CA33) and clamps to 3,599,999 (`cmp ecx, 36EE7Fh` at 0x86CA4F). It runs on the vsync thread, not the step thread (THREADS_AND_CLOCKS.md). Two peers never agree |
| name record byte 18 | `FFX_GetLanguage` at the time of the rename |
| +0x3D0C option bits | per player settings, including the text variant bit 0x10000 and vibration |
| +0x9C player position | local player, float, file image only |
| +0xA8, +0xAA | differ between a manual save and an autosave of the same moment |
| +0x2A | 1 after a file load, 0 after a new game |
| +0x64B4 CRC | 0 after a PC load, nonzero after a raw install |
| +0x350C guide map | set each step from the local player's position |
| +0x28, +0x29 | follow the local camera height |

### 6.2 Derived each step or on a recompute

| field | when |
|---|---|
| +0x0E map name id | map entry |
| +0x10 encounter ground attribute | every field step |
| +0x13 field flags | every step |
| +0x28, +0x29 indoor and map out | every step the indoor check runs |
| record +0x24, +0x28, +0x2F..+0x36, +0x4A..+0x4F | every recompute (load, equip, grid) |

### 6.3 Scratch that is cleared on unload

+0xD8, +0xDC, +0xE0 (premovie) and +0xE4, +0xE8 (field boot) are cleared by map unload. A block
captured mid-map holds this map's values.

### 6.4 Not hazards, though they were suspected

- The item gained and lost masks are not UI state. They are set only, by `FFX_SaveData_AddItem`,
  and nothing reads them. Two peers that gain and lose the same items agree.
- The script work area does not tick on its own. Scripts write it, and the same scripts run on every
  peer, so it moves in step with the simulation. A per-frame write by a script is still
  deterministic state.

---

## 7. Spot checks

Each of these can be confirmed in IDA in a few seconds.

1. **0x8B5642**, `mov g_ffxCurrentMapId, offset unk_F60000`, bytes `c7 05 48 cb 12 01 00 00 f6 00`.
   The only fixup in the instruction is at 0x8B5644. The immediate is a plain number.
2. **0x6485B2**, `fstp dword ptr [eax+0DCh]` with eax from `FFX_SaveMgr__getStagingImage` at
   0x6485A7. Staging +0xDC is block +0x9C. The live block's +0x9C has no access anywhere.
3. **0x79C863 .. 0x79C87A** in `FFX_Btl_LoadCharRecordIntoActor`: `movzx eax, byte ptr [edx+8]` into
   `[esi+5C1h]`, `[edx+9]` into `[esi+5C7h]`, `mov al, [ecx+0Ah]` / `add [esi+5D8h], al`. edx and ecx
   are equipment entries kept from `FFX_SaveData__getEquipEntry` in a local array.
4. **0x91F506** `or [esi+edx], al` in `sub_91F420`, with `shl ecx, 6` (y * 64) at 0x91F4F1 and
   `sar esi, 3` at 0x91F502. edx is the guide map record from `a1[28]`.
5. **0x7B11F8** `mov ebx, [eax+88h]` and **0x7B121A** `cmp word ptr [edx], 0` with edx starting at
   record +0x60 (0x7B1200) and the loop bound `cmp esi, 14h` at 0x7B124B, in
   `FFX_Btl_GrantUnlockedOverdriveModes`.
6. **0x86CA33** `call FFX_Time_AppElapsedSeconds` then **0x86CA99** `mov [esi+0BCh], eax` in
   `FFX_Save_StepPlayTimeFromWallClock`.

---

## 8. Proposed names

`tools/apply_saveblock_names.py` holds the IDB names that fall out of this (getters, setters and
caches that are still `sub_` or `dword_`). DRY_RUN by default, and it refuses to touch anything that
is not a placeholder.
