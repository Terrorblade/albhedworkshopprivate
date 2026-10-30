# Battle start and end, rewards, game over

PROGRESS item 1.11.10 of Project Zanarkand. All addresses are VAs as IDA shows them, RVA = VA -
0x400000. Every claim is marked:

- **[V]** verified, read off Hex-Rays and, where it mattered, the disassembly
- **[D]** verified against the shipped data files in the install
- **[I]** inferred, with the reason given

The IDB was read only. Nothing was renamed or commented. Other agents renamed a few things while
this was being written (`g_ffxFieldBootMode 0x1326B48`, `FFX_ScreenFade_Request 0x82CDC0`,
`FFX_ScreenFade_RequestMode3 0x82CBC0`, `FFX_Field_FreezeActorsToScratch 0x866240`) and this doc
uses those current names. Names for the `sub_` functions are the proposals in section 10.

Read with: `BATTLE_CTB_AND_STATUS.md` (CTB init, the per-unit stream map), `RANDOM_ENCOUNTER.md`
(the roll), `CHEAT_BATTLE.md` section 13 (`btl.bin`, the battle field file, the monster set),
`ASYNC_LOAD_REQUESTS.md` (H1, H5), `GAME_STATE.md` (the save block). The model of the payout is
`tools/btl_rewards.py` in the projectzanarkand repo, section 11.

---

## 0. The short version

- One function, `FFX_Btl_StepSubPhase 0x783020`, walks sub phases 4 -> 5 -> 6 -> 7 -> (8 -> 9 ->
  34) -> 10 -> 11 -> 12 -> 13 -> 14 -> 15 -> 17 -> 18 -> 19 -> 20 -> 21 -> 22 -> 1. Eleven of those
  wait on a disk load, a boot task or a callback. Phase 1 is the running battle. [V]
- `FFX_Btl_SetupUnitRoster 0x79C110` runs at phase 7, before the battle map loads. It reads the
  field party, the aeon order, the in-party bit of each character record and the 8 monster ids of
  the battle field file, and calls `FFX_Btl_LoadUnitParams` for units 0..17. Monster params come
  later, after every monster file has loaded. [V]
- Battle start draws: stream 1 once (the advantage roll, phase 10), then the monster group draws and
  the CTB counters on the monster kind 0 and party kind 0 streams. The last two run inside the
  completion callback of the last monster file. [V]
- The battle ends when `sub_7928D0` (propose `FFX_Btl_TestBattleOver`) returns non zero: 1 party
  wiped, 2 victory, 3 everyone left alive has escaped, 4 the enemies are gone with no kill, or a
  script forced value through `Btl 052`. [V]
- Spoils are not paid at the end. **Every kill pays into one global buffer at once**
  (`g_ffxTreasureStaging 0x2310EA0`, the same buffer chests use), drawing on streams 10 to 13 even
  when the victim is a party member. At the end, AP banked by anyone who never acted or is KO or
  petrified is taken back. The results screen, menu module 17, then moves gil, AP, items and
  equipment into the save block over several frames, after the battle module has shut down. [V]
- `FFX_Btl_CommitActorsToSave 0x785FC0` writes HP, MP, the two equipment slot ids, the overdrive
  mode, gauge and max and the aeon recovery countdown into all 18 character records, plus the party
  and aeon order. KO'd members come back with 1 HP. [V]
- Game over is result 1 with bit 0x200 of `g_ffxFieldBootMode` clear. The battle plays its game over
  sequence, tears down normally (the commit still runs) and `maybe_FFX_Field_QueueBootTasks
  0x861160` calls `FFX_Map_ReturnToTitle` instead of booting the field. [V]

---

## 1. The globals

| VA | current name | what | |
|---|---|---|---|
| 0x112A8E2 | `g_ffxBattlePendingKind` | 0 none, 1 random, 2 scripted. Consumed by `FFX_Btl_MainStep` | [V] |
| 0x112C254 | `g_ffxBattleSceneAndMap` | low word battle map id (bit 0x400 = battle map), high word scene index | [V] |
| 0x112C258 / 0x112C259 | `g_ffxBattleZoneIndex` / `g_ffxBattleFormationIndex` | zone and formation slot | [V] |
| 0x112A9D4 | `g_ffxBtlReturnTasks` | low byte: the return mode a script passed to `Btl 002` (1 = no results screen when the boot mode byte is 1..4, section 3.4). High byte: a scripted battle argument read by `sub_7829B0` at begin. Cleared by `FFX_Field_ResetEncounterAccum` and after the return | [V] |
| 0x112A8E0 | `g_ffxBtlSubPhase` | the one phase variable. `FFX_Btl_GetPhase` returns it. 0 = no battle | [V] |
| 0x112A8E3 | `word_112A8E3` low byte | **the end result**, copied from the over test at end phase 29. 5 or 6 ends the battle on the same step | [V] |
| 0x112A8E4 | `word_112A8E3` high byte | the script hold `Btl 060` / `Btl 151` (BATTLE_CTB_AND_STATUS.md) | [V] |
| 0x112C9F1 | `word_112C9F1` low byte | forced result. Written by `Btl 052` (which also sets phase 2), by the debug end combo (5), and at end phase 23 from the test. Overrides the test when non zero | [V] |
| 0x112C9F2 | `word_112C9F1` high byte | victory pose wanted | [V] |
| 0x112C9F4 | `dword_112C9F4` byte 0 | game over sequence running | [V] |
| 0x112C9F5 | `dword_112C9F4` byte 1 | party fled (Flee, or the recall in `sub_7AEF20`) | [V] |
| 0x112C9EC | `word_112C9EC` | two frame counters. `FFX_Btl_EndPhaseStep` only runs on a step where the low byte read 0. Clarified 2026-10-29: the value tested is the one this step's count down left (0x790D2D..0x790D61, `cl` after the `dec`), so a count of 1 runs it on this step | [V] |
| 0x112C9EB | `g_ffxBtlMonstersKilledThisBattle` | ++ per enemy death, picks result 2 over 4 | [V] |
| 0x112C9DC | (`dword_112C9DB` byte 1) | the advantage, 0 normal, 1 pre-emptive, 2 ambush | [V] |
| 0x112C9FF | `g_ffxBtlEscapeMode` (was `dword_112C9FC` byte 3, the dword is four bytes since 2026-10-29) | escape mode. 0 roll, 1 never, 2 always. `Btl 166` writes it, begin resets it to 0 | [V] |
| 0x112CA00 | `g_ffxBtlEscapeAllowed` | 1 at begin, `Btl 184` writes it, read by `FFX_Btl_GetEscapeCommandId` | [V] |
| 0x112C9E5 | `g_ffxBtlScriptMode` | `Btl 275` = 1, `Btl 276` = 2. Zeroed at begin by the dword store of 0xFF to 0x112C9E4 | [V] |
| 0x112C9E7 | (byte 3 of that dword) | party arrangement was pushed by `FFX_Btl_BeginSummon`, so the commit restores it | [V] |
| 0x1326B48 | `g_ffxFieldBootMode` | the battle options, section 3.4 | [V] |
| 0x1136068 | `dword_1136068` | 31 x 280 byte reward records, allocated and zeroed per battle | [V] |
| 0x2310EA0 | `g_ffxTreasureStaging` | the spoils buffer, 432 bytes, section 4.1 | [V] |
| 0x11307A4 | `dword_11307A4` | saveData+0x3D14, battle count | [V] |
| 0x112CB60 | (saveData+0xD0) | byte, the last battle's result, written on the way back to the field | [V] |
| 0x112CB74 | (saveData+0xE4) | dword ORed into the battle options for a random battle | [V] |

The save block is `g_ffxSaveData 0x112CA90` (`FFX_GetSaveData`). The character records are
`g_ffxCharRecords 0x113205C` = saveData+0x55CC, 148 bytes each. [V]

---

## 2. Battle start

### 2.1 From the request to `FFX_Btl_BeginBattle`

`FFX_Field_StepRandomEncounter` (or `FFX_Btl_RequestScriptedBattle 0x781C90` for `Btl 002`)
writes the scene, zone, formation and `g_ffxBattlePendingKind`, as in RANDOM_ENCOUNTER.md section
6. `FFX_Btl_MainStep 0x790C10` with phase 0 calls `FFX_Btl_BeginBattle(0, scene, zone,
formation)`. [V]

The formation and battle id tables are `btl.bin` (CHEAT_BATTLE.md 13.1 to 13.5): scene entry 14
bytes, zone record, formation entry `{u8 encounter id, u8 weight}`. The battle id is `(mapId << 16)
| encounterId`, and `FFX_Btl_ResolveFieldAssetIndex 0x79D0E0` turns (scene, zone, slot) into the
asset kind 14 index of the battle field file, whose section at `+12` holds the 8 monster ids. [V]
(already verified there)

### 2.2 `FFX_Btl_BeginBattle 0x781020`, mode 0, in order

Hex-Rays renders this function wrong because IDA has `__exit 0x82CB80` marked noreturn. It is not
an exit, it is `FFX_ScreenFade_Request(code, 1, 0)`. Read the disassembly. [V]

1. `AssetLoader[2](0)`, `sub_788B90`, `g_ffxBattlePendingKind = 0`.
2. `FFX_Btl_ResolveFieldAssetIndex`, then `sub_781550` returns the field file size (asset kind 14),
   0 aborts the whole begin. The battle map id goes into the low word of
   `g_ffxBattleSceneAndMap` from zone record +1, +2. [V]
3. Mode 0 only: `sub_82BEE0(0)`, `sub_7BBDA0`, `FFX_ScreenFade_Request(1, 1, 0)` (0x82CB80),
   `FFX_Player__setSubStepCount(1)`, `FFX_Ch_SetPlayerChr(0)`, `FFX_Btl_Shutdown` (no-op from phase
   0), then a 16 byte aligned allocation of the field file size tagged `"btlbin"`. [V]
4. `sub_783000`, `sub_7A0500`, `sub_7A0910`, `FFX_Field_ResetEncounterAccum(0)`, `sub_79CDD0`,
   `sub_7BE100`, `sub_799AB0`, `sub_79D290`, `sub_7A2730`, then about 70 battle globals reset. The
   ones that matter later: advantage 0 (`dword_112C9DB = 0x10000`), turn and command queues empty,
   escape allowed 1, escape mode 0, script mode 0, forced result 0, frame counter 0,
   `word_112C94E = 670` (the game over effect id, section 6). [V]
5. The four `btl.bin` pointers `dword_112A9F0..FC`, the scene name `"%s_%02d"` (sound banks and
   per battle text only, CHEAT_BATTLE.md 13.6.1). [V]
6. `FFX_Mot_SetKeepSpeed(1)`, `FFX_Btl_AllocActorArrays`, **`sub_799FB0`** (allocates 0x21E8 = 31 x 280 bytes of
   reward records at `dword_1136068`, zeroed, and a 0x8A88 byte block at `dword_2310CD8` filled
   0xFF), `sub_79F9B0`, **`sub_798E90`** (copies the spoils AP accumulators to the snapshot, 4.4),
   `FFX_SaveData_RebuildItemLists`, `sub_89B660`, `sub_79E7A0`, then `sub_7829B0(high byte of
   g_ffxBtlReturnTasks, &mode)` and `sub_79E5E0` when it returns >= 0. [V]
7. `FFX_Field_FreezeActorsToScratch 0x866240(map)`: for a random battle (low byte of
   `g_ffxBtlReturnTasks` is 0) `g_ffxFieldBootMode = saveData+0xE4 | (g_ffxFieldBootMode & ~0x10000)`.
   Pushes the `"MUSIC"` boot task (`sub_862470`) and parks the field actors. [V] Its state 7 also
   waits on FMOD: `if (sub_67A240() != 0) stay`, FMOD command 20, true while battle track 16 still
   reads `FMOD_EVENT_STATE` LOADING. It is the one command the step thread runs synchronously across
   to the FMOD thread, and a 10 ms timeout reads as "still loading" (`SOUND_CLASSES.md` 2.1). [V]
8. `g_ffxBtlSubPhase = 4`, tail call `FFX_Btl_StepSubPhase`. A non zero mode skips 3, 6 and 7 and
   enters at phase 5. [V]

### 2.3 `FFX_Btl_StepSubPhase 0x783020`, every phase

At the top of every call: with `g_ffxBtlDbgDvdSlowStart` and `byte_112A919` non zero it counts
`byte_112A919` down and returns (a debug DVD delay, 60 frames, set in phases 10, 12, 15). It counts
`dword_112CA64` down (set to 90 by `sub_7F9280`). It sets a local flag for story 2510 on map hash
8767 that changes how the character load waits behave. [V]

| phase | what it does | leaves when | wait kind |
|---|---|---|---|
| 4 | nothing | `g_ffxBootTaskQueues` is empty (`0x875180`, misnamed `?__uncaught_exception`), i.e. the MUSIC task finished | boot task, sound load [V] |
| 5 | `sub_781550`, copies the field file path, posts `FFX_Btl_OpenFieldArchive` as a RegistTask (S1), reads the battle field file with callback `sub_782FE0`, the per battle text when the language is 0 | at once, to 6 | [V] |
| 6 | (not in the switch) | `sub_782FE0` sets 7 when the field file read completes | ROM read [V] |
| 7 | `FFX_Btl_SetupFieldPointers 0x783E20`: section pointers, `FFX_BtlAi_EndScriptContext`, **`FFX_Btl_SetupUnitRoster`**, `sub_7AC680`, `sub_7824E0`, releases the field actor CHRs, `FFX_Map_SetFootstepBank(1)` | to **10 at once** when the battle map equals the current scene id, else to 8 | [V] |
| 8 | `g_ffxBtlMapLoadPending = 1`, `byte_112C9DA = 1`, `FFX_Map_LoadMapAsync(map & 0x3FF)` for a battle map (bit 0x400) else `sub_908600` | to 9 | [V] |
| 9 | waits while `sub_908F10()` has bit 0x20 or 0x40, then `sub_908000` | to 34 | map loader [V] |
| 34 | | `g_ffxBtlMapLoadPending == 0`, cleared on the loader **worker** (H1) | disk, off thread [V] |
| 10 | section 2.6 | runs once, falls into 11 | [V] |
| 11 | spoils participation flags, party CHR loads, section 2.6 | falls into 12 | [V] |
| 12 | | `FFX_Btl_GetPendingChrLoads() == 0` or `g_ffxBattleChrLoadsInFlight <= 0` (H5), then event 117 and `FFX_Btl_LoadMonsterData` | disk, off thread [V] |
| 13 | set by `FFX_Btl_LoadMonsterData` while monster files load, one read per distinct monster, each completion re-enters it through `sub_783990` | the last one calls `FFX_Btl_RegisterMonsterMotions` and sets 14 | ROM reads [V] |
| 14 | monster CHR loads for slots 20..27 with a type id and `+0x24` | to 15 | [V] |
| 15 | | the CHR loads drain, then event 118, `FFX_Btl_LoadFootData(field monster section +2)` | disk [V] |
| 17 | (not in the switch) | foot data read completes, its callback `0x783DD0` | ROM read [V] |
| 18 | `FFX_Btl_LoadWeaponModel` for each front party unit 0..17, one per completion | all issued, to 19 | ROM reads [V] |
| 19 | weapon CHR loads `FFX_Btl_QueueChrLoad(2, ..)` for units 0..17 with `+0xDC8` | to 20 | [V] |
| 20 | | `FFX_Btl_GetPendingChrLoads() == 0` (or the story flag path) | disk [V] |
| 21 | the advantage message: pre-emptive `sub_79CFD0(2, 0, 0, 17, 25)`, ambush `sub_79CFD0(3, 0, 0, 17, 25)`, or 100 for battle id 0x01E60046 | to 22 | [V], "message" [I] |
| 22 | | `dword_112C910 == 0` (the message box), `dword_113618C == 0`, no actor with `+0x433`, `dword_112CA64 == 0`. Then `g_ffxBtlSubPhase = 1`, `word_112C022 = 1`, `sub_7982A0(5, ..)`, event 124 | text, timers [V] |

Every call ends with `sub_7AD190`. [V]

### 2.4 `FFX_Btl_SetupUnitRoster 0x79C110` (phase 7)

In order: [V]

1. Zero 96 bytes at 0x1136008. Default the light fields of all 93 records (0..92).
2. Units 0..30: a long list of defaults, among them `+0xDD2 = +0xDD3 = 1`, `+0x43B = 1`, and two
   side records per unit from `FFX_Btl_GetSideRecord`.
3. **Active party.** For j = 0..2, `c = g_ffxFieldPartyChars01[j]` (saveData+0x3D58). If not 0xFF:
   `actor(c)+0x10 = 1` (exists), `+0xDC8 = 1` (on the field), `+0x4FE = j`, `+0x3F8 = 1` for the
   middle slot. `g_ffxBattlePartyOrder[j] = c`, and `[3..6] = 0xFF`. GAME_STATE.md has this loop.
4. `g_ffxBattleAeonOrder 0x112C8A3[17]` = `g_ffxPartyBenchOrder 0x11307EB[17]` (saveData+0x3D5B).
5. Units 0..17: present (`+0x10 = 1`, `+0xDDC..+0xDDF`) when `FFX_SaveData_IsCharInParty(k)`
   (record +0x2C bit 0) or already marked by step 3.
6. Units 0..17: `+0xDD5 = 1`, type id `+0x0E = k`, `FFX_Btl_ResolveUnitChrId`,
   **`FFX_Btl_LoadUnitParams(0, k)`**, and the two side records get the weapon and armour CHR ids
   from `sub_7ABDC0(+0x592 / +0x593)`.
7. Units 0..30: enemy slots get `+0x3F5 = 0x0303`, `+0x43D = 3`, `+0x4FD = 5`. Others get
   `+0x3F5 = 0`, `+0x43D = 8`, and either the aeon set (`+0x4FD = 4`, `+0x4FE = unit - 8`, ...) or
   the character set (`+0x4FD = 3`, `+0x437 = 1` at 0x79C538, the neck aim enable).
8. Enemy slots 0..7 (units 20..27): `+0x0E` = the u16 at field monster section `+12 + 2*slot`,
   `FFX_Btl_ResolveUnitChrId`, and for a non 0xFFFF id: the seen bit in saveData+0x440C,
   `+0x4FE` = running visible index, `+0xDF3 = 1` (AI), `+0x10 = 1`, `+0xDC8 = 1`, and others.

Note `+0x3F5`: when it is non zero `FFX_Btl_OnUnitDefeated` clears `+0xDC8` on death. So a dead
enemy leaves the field and a dead character stays on it. That matters to the over test. [V]

### 2.5 What `FFX_Btl_LoadUnitParams 0x79B4F0` reads

It zeroes the 492 byte block at `actor+0x540` first. [V]

**Characters (units 0..17)**, from the save block and `ply_rom`: [V]

| source | actor | |
|---|---|---|
| `g_ffxCharNames + 20*k`, 20 bytes | +0x540 | name |
| `ply_rom` row +16 / +42 / +43 | +0x590 / +0x5C9 / +0x6D9 | +0x6D9 is the aeon recovery length, 3.3 |
| record +0x2C | +0x591 | party flag byte |
| record +0x37 | +0x5BA | poison percent (BATTLE_CTB_AND_STATUS.md 2.2) |
| record +0x38, +0x39, +0x3A | +0x5BB, +0x5BC, +0x5BD | overdrive mode, gauge, gauge max (`FFX_Btl_AddOverdrive`) |
| record +0x3D | +0x6D8 | aeon recovery countdown |
| record +0x14 | +0x5CC | total AP |
| record +0x1C, +0x20 | +0x5D0, +0x5D4 | HP, MP. Also copied to +0x6E4, +0x6E8 |
| record +0x24 | +0x5A4 and +0xF60 | max HP (the +0xF60 slot is the overkill threshold on a monster) |

Then `FFX_Btl_LoadCharRecordIntoActor 0x79C5F0`: max HP / MP (record +0x24, +0x28) into
+0x594..+0x5A0, the eight stats (record +0x2F..+0x36) plus the actor's bonus bytes into
+0x5A8..+0x5AF (agility is +0x5AC), equipment slot ids (record +0x2D, +0x2E) into +0x592, +0x593,
and from each of the two equipment entries in the save block: entry +8 -> +0x5C1, +9 -> +0x5C7,
+10 added into +0x5D8, and every auto-ability row ORed in (a_ability.bin, the +0x6BC / +0x6BE words
among them). Then `sub_78D270`, a clamp of HP and MP to max. [V] Then `+0xF88 =
dword_1136068 + 280*k` (an all zero record, 4.2), `FFX_Btl_ComputeOverdriveRefDamage`, `sub_79BB50` (command lists),
`FFX_Btl_BuildActorEquipLists`, and with `byte_112C9CE` or script mode 2 `sub_79B470(k, 0x3002, 1)`.
[V]

Ability bitmaps come from the save block character records (+0x3E of each `SAVE_CHAR_RECORD`), which
`FFX_InitNewSaveData` seeds from `ply_save.bin` rows 0..17. Corrected 2026-10-27 (`BOOSTERS.md` 5.2):
this used to say they come from `FFX_Btl_LoadPlayerData 0x7848B0` run from `FFX_Btl_Init`. That
function, now `FFX_SaveData_GrantAllAbilities`, is the Unlock all skills booster. `FFX_Btl_Init` calls
it only when `g_ffxBtlDbgFullSet` is set (0x781C2B), and it ORs the all-abilities rows 18 and 19 into
every record, not each character's own row. [V]

**Monsters (units 20..27)**, from `actor+0xF80` (param record, header dword +12 of mNNN.bin):
+0x14 max HP, +0x18 max MP, +0x1C overkill threshold, +0x20..+0x27 the eight stats, +0x28,
+0x2A, +0x2B..+0x2E (absorb, immune, half and weak elements into +0x5DA..+0x5DD), +0x2F..+0x47 added to the 25 status resistances, +0x48..+0x4E, +0x4E,
+0x50.. the command list (into `dword_2310CD8`, 1144 bytes per unit), +0x70, +0x74, +0x76, +0x77, and the
u16 at +0x78, the capture species (into +0x6D6 at 0x79B8C7, MONSTER_ARENA.md). And the 280 byte reward block, header dword +20
of the file, is copied to `dword_1136068 + 280*unit` and that pointer stored at `+0xF88`. [V]
These run from `sub_79CB10` inside `FFX_Btl_RegisterMonsterMotions`, after all monster files are
in. [V]

### 2.6 Phase 10 to the first command menu

**Phase 10**, one call: [V]

```
sub_793CB0()                                   per-unit float from sub_793BA0, no RNG
advantage = FFX_Btl_RollPreemptiveOrAmbush(32) stream 1, one draw, always
sub_797A40 x2, sub_798390(0)                   sound bank names, no RNG
if (g_ffxBtlForcedAdvantage != 0xFF) advantage = forced
sub_887170(field monster section +1 == 1)
if (battleCount < 0x0FFFFFFF && scriptMode != 2) ++battleCount   saveData+0x3D14
sub_797BC0(0, 255, 24, ..)                     battle script event 24
for k in 0..17: sub_7967F0(k, 0), then AI event 63 for on-field units, sub_7966A0(k, 0) for others
if (byte_112C9DA) sub_784420()                 only when a map was loaded at phase 8
```

The roll is in BATTLE_CTB_AND_STATUS.md 1.3. The battle count is also bumped by
`FFX_Field_StepRandomEncounter` at 0x780F97 on its scene request path. [V]

**Phase 11**: `g_ffxBattleChrLoadsInFlight = 0`, then for units 0..17 on the field
`g_ffxTreasureStaging[unit] = 1` (the participation flag, 4.1) and the body load
`FFX_Btl_QueueChrLoad(0, unit, unit, +4, 1)`, then `FFX_Btl_QueueWeaponChrLoads(unit, 3)`. [V]

**Phase 12 -> 13 -> 14.** `FFX_Btl_LoadMonsterData 0x783680` loads one mNNN.bin per call and
returns. Its completion `sub_783990` binds the sections and calls it again. A slot whose type id
was already loaded shares that file. When all eight are done it calls
`FFX_Btl_RegisterMonsterMotions 0x784070`: [V]

```
sub_79CB10()                        FFX_Btl_LoadUnitParams for every present enemy slot
for the 8 slots: sub_797A40(1, "m%03d", ..)    motion names, no RNG
sub_7842F0()                        monster group draws, kind 0 monster streams 28..35
FFX_Btl_InitCtbCounters(advantage)  party kind 0, then monster kind 0 (normal start only)
sub_79A0E0()                        clamp HP and MP of every present unit
```

When the monster files complete through the asset loader, this runs inside
`FFX_AsyncLoad_DispatchCompletions` at the head of `FFX_MainStep`, not inside
`FFX_Btl_MainStep`. With no monster to load it runs straight from phase 12. [V]

**Additions from the Project Zanarkand battle loop port (2026-10-29), checked in IDA.**

- Phase 7 with `FFX_Btl_SetupFieldPointers` returning 10 (the battle map is the field's own scene)
  jumps to phase 10's body (LABEL_25) and runs 10, 11 and 12 in the same call. Any other return goes
  to phase 8's body. [V]
- `sub_783000` (proposed `FFX_Btl_ResetLoadCounters`) zeroes `g_ffxBtlMonsterBindCounter`, the word at
  0x112A929 (the next weapon model unit and the next monster file slot) and `dword_112CA60`.
  `sub_7A2730` (proposed `FFX_Btl_ResetDefeatQueue`) zeroes all of `dword_112CA0A`, so byte 0 (between
  commands, 1 after every `FFX_Btl_FinishActions`) is 0 from the start until the first action ends,
  and `dword_112C90C`. `sub_79CDD0` zeroes `dword_112C910` and `dword_112C944`. [V]
- Phase 14 tests the type id (+0x0E) and +0x24, which the roster sets to 1 for every unit, so only the
  type id filters. The phase 22 actor test is `sub_784220` (proposed `FFX_Btl_AnyUnitReactionBusy`),
  which also writes the unit and 0xD0 to `word_112CA08`. [V]
- `sub_7AC680`, right after the roster in `FFX_Btl_SetupFieldPointers` (0x783F06), runs `sub_7AC650`
  for units 0..30, which copies the position key +0x4FC..+0x4FE into the second key +0x509..+0x50B.
  `sub_7AC600` copies it back, and `sub_7AF4B0` runs that for every unit at each turn retirement, so a
  port that skips `sub_7AC680` sends every party and monster slot to 0 after the first turn. The
  other writers of +0x509..+0x50B are `FFX_Btl_SetUnitPlaceLayout` (+0x509), `FFX_BtlAi_SetProperty`
  cases 85 and 86, the summon swaps and the flee. [V]
- The ported start, driven over the install from a pending bsil03 zone 0 formation 0 request with
  every load finishing between steps, ends its steps at sub phases 6, 9, 13, 13, 15, 17, 18, 18 and
  then 1, the first turn pushed on that last step. [V] by the port's test, the order from the
  disassembly.

**First menu.** Phase 22 sets phase 1 inside `FFX_Btl_StepSubPhase`, which is the first call in
`FFX_Btl_MainStep`. The same `FFX_Btl_MainStep` then runs `FFX_Btl_CtbTick` and
`FFX_Btl_SendMenu`, so the first turn is pushed on the step the battle reaches phase 1, and the
first menu or AI decision comes on that step or the next (BATTLE_CTB_AND_STATUS.md 1.4 and 1.5). [V]

---

## 3. Battle end

### 3.1 The over test, `sub_7928D0`

```c
alive = onField = 0;
for (i = 0; i < 7; ++i) {                              // g_ffxBattlePartyOrder, 0xFF skipped
    a = actor(order[i]);
    if (a[0xDD2] && a[0xDD3] && a.hp(+0x5D0) > 0 && !a[0xDCC] && !a[0xDCE]) {
        ++alive;
        if (a[0xDC8]) ++onField;
    }
}
if (alive == 0)        r = 1;                          // party wiped
else if (onField == 0) r = 3;                          // the living ones all left (escape)
else {
    clear = 1;
    for (j = 0; j < 8; ++j) {                          // enemy slots
        e = enemy(j);
        if (e[0xDC8] && e[0xDD2] && !e[0xDCC] && !e[0xDCE]) clear = 0;
    }
    r = clear ? (monstersKilled ? 2 : 4) : 0;
}
return forcedResult ? forcedResult : r;
```

Read off the disassembly at 0x7928D0..0x7929AF. [V] Notes:

- `g_ffxBattlePartyOrder[3..6]` are 0xFF after the roster, so normally only the three active slots
  count. Reserves do not keep a battle going. [V]
- A dead character keeps `+0xDC8` (2.4), so a wipe is result 1, not 3. [V]
- **Petrify counts as dead** on both sides (`+0xDCE`). [V]
- Result 4 is "every enemy gone, none killed": eject, bribe or a script removal with no
  `FFX_Btl_OnUnitDefeated` kill. Bribe and Eject still go through `FFX_Btl_OnUnitDefeated` and do
  count as kills when `a3 == 0` (4.2), so 4 needs something that never calls it. [V] / [I] which
  shipped battles reach 4
- While an aeon is out (`(u8)unk_112C9E8 != 0`), results 1 and 3 are not an end. End phase 2 calls
  `sub_78E0A0` (the aeon gets 0 HP and a recovery countdown), then either `FFX_Btl_QueueAeonTransition` or
  `sub_7AEF20(0)`, which brings the party back, and goes back to phase 1. That is item 1.11.11. [V]

### 3.2 `FFX_Btl_EndPhaseStep 0x7917D0`, the end phases

Runs from `FFX_Btl_MainStep` only when `FFX_Btl_IsCtbTickAllowed` passes **and** the low byte of
`word_112C9EC` was 0 on entry (it is a frame counter decremented just before). [V]

| phase | what | |
|---|---|---|
| 1 | test non zero -> 2 | [V] |
| 2 | waits for an empty command queue and `HIBYTE(word_112C9D4) == 0`. Aeon recall (3.1), or test non zero -> 23, else back to 1 | [V] |
| 23 | waits for `FFX_Field_EncounterHoldStub_impl`, `sub_8864B0`, `sub_886640` (sound) and `sub_79D050` (text) to be idle. Latches the test into `word_112C9F1`, `sub_7982A0(4, ..)` | [V] |
| 24 | waits for the script hold and the same four. `sub_8AB7B0`. Result 1 with `!(g_ffxFieldBootMode & 0x200)`: game over sequence on (`dword_112C9F4 = 1`, waits 30, music track 20 [I], effect 140) -> 25. Result 2 with `!(g_ffxFieldBootMode & 0x100)`: victory pose (`HIBYTE(word_112C9F1) = 1`, wait 10, track 12 [I], effect 138) -> 25. Result 4: wait 30 -> 27. Else -> 27 | [V] |
| 25 | `sub_8863F0` / `sub_886AF0` with the track id | [V] |
| 26 | `sub_79FFF0(effect)`, wait 10 | [V] |
| 27 | every unit 0..30: AI event 0 if `sub_791780`. Returns while any unit is mid animation. Then -> 28 if pose or game over, else straight to 29 | [V] |
| 28 | waits for `sub_7A0040(effect)` | [V] |
| 29 | **`word_112A8E3 = result`**, `sub_793290` (every unit), `sub_7882B0`. Battle script event per result: 1 -> 67, 2 -> 66, 3 and 5 -> 68, 4 -> 69, with `sub_7982A0(1 / 0 / 2 / 3)`. Victory pose wait 88 frames. **`sub_7B1540`**: every front unit 0..6 bumps overdrive mode 11's counter and gains 20 gauge if mode 11 is set (Victor [I], it runs for every result but needs a living front unit) | [V] |
| 30 | victory: units 0..17 on the field, alive, not petrified, with `+0x43B`, play AI event 45 once (`+0x43A`). Game over: `byte_112C9EF = 1` starts global magic effect `word_112C94E` (670) through `maybe_FFX_Magic_BeginGlobalEffect` once the overlay is ready | [V] |
| 31 | waits for the counters and `FFX_ScreenFade_RequestMode3(frames)` (45 on game over, 10 otherwise) when the boot mode keeps the fade or game over runs | [V] |
| 32 | `FFX_Magic_EndPhaseOverlayFree(word_112C94E)` after a game over effect. That is 670 unless a script changed it through Btl_284. Only 671 calls the DLL's overlay slot 8, anything else takes the plain free path (`EFFECT_OVERLAY.md` 2.1) | [V] |
| 33 | done | [V] |

### 3.3 The teardown in `FFX_Btl_MainStep`, and what is written back

`FFX_Btl_MainStep` 0x790E6C onwards, when phase is 33 **or** `(u8)word_112A8E3` is 5 or 6. 5 and
6 therefore skip phases 30..33 and end on the step phase 29 latched them. 6 is written at 0x790DAE
only when `sub_872350()` is non zero, and on PC `sub_872350` is `return 0`. [V]

```
sub_7BA860(2); sub_887170(0); FFX_Mot_SetKeepSpeed(1); sub_8AB590(); j_FFX_SaveData_GetSceneId();
sub_798B70();                         AP end filter and the battles-fought counter, 4.4
if (!g_ffxBtlDbgThrowAwayParm) FFX_Btl_CommitActorsToSave();
FFX_Btl_Shutdown();
maybe_FFX_Field_QueueBootTasks(returnMode, battleId, map, result);
returnMode = 0;
```

**`FFX_Btl_CommitActorsToSave 0x785FC0`**, exactly: [V]

1. `FFX_Btl_RestorePushedPartyOrder`: if `0x112C9E7` is set (a summon pushed the arrangement in `FFX_Btl_BeginSummon 0x7ADF90`, corrected 2026-10-13, see `SUMMONS.md`), restore
   `g_ffxBattlePartyOrder[7]` from 0x112C89C and `g_ffxBattleAeonOrder[17]` from 0x112C8B4, and
   clear the flag. So a forced party does not leak into the field.
2. `FFX_Btl_CommitPartyToField 0x786930`: `g_ffxBattlePartyOrder[0..2]` -> saveData+0x3D58..0x3D5A.
3. `FFX_Btl_CommitAeonOrderToSave 0x7860B0`: 17 bytes from 0x112C8A3 -> saveData+0x3D5B.
4. For each of the 18 records (r = record, a = actor):

| record | from | rule |
|---|---|---|
| +0x2D | a+0x592 | weapon entry id |
| +0x2E | a+0x593 | armour entry id |
| +0x1C HP | a+0x5D0 | if HP <= 0 and the countdown a+0x6D8 is 0, **1** |
| +0x20 MP | a+0x5D4 | |
| +0x1C, +0x20 | record +0x24, +0x28 | when the countdown was non zero and reaches 0 now, full HP and MP |
| +0x3D | a+0x6D8 | the countdown, minus 1 if it was non zero |
| +0x1C, +0x20 | | clamped 0..max |
| +0x38, +0x39, +0x3A | a+0x5BB, +0x5BC, +0x5BD | overdrive mode, gauge, max |

5. `FFX_SaveData_RecomputeCharDerived(8..17)`, the aeons only.

Nothing else on the actor goes back: statuses, MP cost changes, CTB, stat changes from the fight
are dropped. Items, gil spent, steals and captures were written straight into the save block when
they happened (`FFX_SaveData_AddItem`, `FFX_SaveData_SpendGil`, the capture counters), and the
bestiary bits at roster time and at each kill. [V] for the writers named, [I] that this list is
complete.

`FFX_Btl_Shutdown 0x781590` then frees the battle (`FFX_SaveData_RecomputeEncountersEnabled`,
footstep bank 0, about 20 teardown calls) and sets phase 0. [V]

### 3.4 Back to the field, `maybe_FFX_Field_QueueBootTasks 0x861160`

`(returnMode, battleId, map, result)`: [V]

1. `FFX_Atel_BuildPartyVisibleMask`, sound resets, **saveData+0xD0 = result** (scripts read the
   last battle's result from there [I], the write is [V]).
2. result 6: black fade, hold mode 3, return. No boot tasks.
3. result 1 and `!(g_ffxFieldBootMode & 0x200)`: **`FFX_Map_ReturnToTitle`**. Game over.
4. Otherwise the field boot chain: `CAMERA` (unless boot mode bit 0x4000), `MOVIE` when the boot
   mode byte is 2, the effect path for 3 and 4, `SNDCLR`, `SNDREG`, `CHRREG`, `EFFDONE`, `DONE`.
   **The results screen** opens with `FFX_MenuSys_RequestOpen(0x40010002)` (game mode 0x10000 =
   module 17, MENU_SYSTEM.md) when returnMode is not 1, or when it is 1 and the boot mode byte is
   not 1..4. Otherwise `DONE2` is pushed instead. [V]
5. `g_ffxFieldBootMode = 0x10001`. [V]

The battle options word (`g_ffxFieldBootMode 0x1326B48`), as far as the battle reads it: [V]

| bits | meaning | reader |
|---|---|---|
| low byte | post battle mode, 1 default, 2 movie, 3 and 4 effect | `maybe_FFX_Field_QueueBootTasks` |
| 0x100 | no victory pose | `0x86C280` (misnamed `?doThrowTypes@UnDecorator`) returns `!(bit 8)` |
| 0x200 | **losing is allowed**, no game over | `0x86C290` (misnamed `?doMemberTypes`), and the title test |
| 0x400 | keep the field music | the MUSIC boot task |
| 0x4000 | no CAMERA task on return | |
| 0x8000 | read by `0x86C4E0` (misnamed `?doEcsu`) | not traced |
| 0x10000 | no end fade unless game over | `sub_86C4D0` |

It is written by `sub_86FC20` from `Core 330`, map unload and event load, reset to 0x10001 by
`sub_86CFF0`, `FFX_Atel_Init` and the return above, and ORed with saveData+0xE4 for a random
battle. [V]

---

## 4. Rewards

### 4.1 The spoils buffer, `g_ffxTreasureStaging 0x2310EA0`, 432 bytes

The same buffer the chest commands use (GAME_STATE.md "The chest reward path"). `sub_7993E0(buf, 7)`
zeroes 432 bytes and writes the eight equipment ids `0x7000 + i` at +0xEE. [V]

| off | VA | type | what | writers |
|---|---|---|---|---|
| +0x00 | 0x2310EA0 | u8[18] | took part. Shown on the results screen for units 0..7 | phase 11, `FFX_Btl_SwapUnitIntoPartySlot` 0x7ADD5E |
| +0x24 | 0x2310EC4 | u8[18] | acted. Set at action start for units 0..7, every command except Switch 0x3002 | `sub_7AE370` 0x7AED05 |
| +0x38 | 0x2310ED8 | s32[18] | AP at battle start | `sub_798E90` in `FFX_Btl_BeginBattle` |
| +0x80 | 0x2310F20 | s32[18] | AP banked, 0..999999999 | `sub_798A00`, the results screen |
| +0xC8 | 0x2310F68 | s32 | zeroed by `sub_798B70`, no other reader found | |
| +0xCC | 0x2310F6C | s32 | gil | `sub_7990D0`, chests |
| +0xD0 | 0x2310F70 | u8 | item kinds, max 8 | `sub_798AC0` |
| +0xD1 | 0x2310F71 | u8 | key item present | chests only |
| +0xD2 | 0x2310F72 | u8 | equipment count, max 8 | `sub_798BD0` |
| +0xD4 | 0x2310F74 | u16[8] | item ids | |
| +0xE4 | 0x2310F84 | u8[8] | quantities, 0..99 | |
| +0xEC | 0x2310F8C | u16 | key item id | chests only |
| +0xEE | 0x2310F8E | u16[8] | `0x7000 + i`, the scratch equipment ids | |
| +0xFE | 0x2310F9E | 22 x 8 | the staged equipment entries, save block format (CHEAT_SAVEDATA.md 4.1) | `sub_798C10`, chests |

It is cleared by `sub_798B60` at `FFX_Btl_Init`, after each chest command, on
`FFX_Map_ReturnToTitle` and when the results screen closes. **Not at battle start.** [V] So when a
battle ends without opening the results screen (3.4 step 4), its spoils stay in the buffer and are
paid by the next results screen, and AP banked then counts as the next battle's "AP at start". [V]
from the writers and clears, [I] whether a shipped battle does this.

### 4.2 Each kill pays at once: `sub_7990D0` (propose `FFX_Btl_PayKillRewards`)

`FFX_Btl_OnUnitDefeated(killer, victim, a3, a4)` calls `sub_7990D0(killer, victimActor,
victim+0xF88, victim+0xF5F overkill flag, victim status-2 word)` whenever `a3 == 0`, **for any
victim**. Callers with `a3 == 0`: `FFX_Btl_ApplyPendingEffects` (a hit), `FFX_Btl_TurnStartStatusTick`
(zombie regen, killer = victim), `FFX_Btl_ApplyPoisonDamage` (killer = victim), `sub_7B4B70`
(script). `a3 == 1` only from `sub_78D980`, the self KO after an action with ability dword +28 bit
0x400000. [V]

A party member's or aeon's record is the zeroed slot from 2.5, so its death pays nothing but **still
draws three times on stream 10**. [V]

The record pointer non null, then in this order: [V]

```c
ap   = overkill ? rec.u16[+4] : rec.u16[+2];         // x100 with debug g_ffxBtlDbgApTimes100 0x112A912
gil  = rec.u16[+0];                                  // x100 with debug g_ffxBtlDbgGilTimes100 0x112A913
mult = 1;
for (i = 0; i < 18; ++i) mult = sub_798A00(i, actor(i), ap, mult);    // AP, no RNG

if (victim[0x701] == 2) {                            // bribed (FFX_Btl_RollBribe sets it)
    s  = FFX_Btl_RandStreamForUnit(killer, 0);
    r1 = Rand(s) % 11 + 20;
    q  = (float)sqrt((double)victim.s32[0x708]) * rec[+44] * 0.0625 * r1 / 25.0;   // double
    r2 = Rand(s) & 1;
    n  = clamp((int)(float)(r2 + q), 1, 99);
    AddItem(rec.u16[+42], n);                        // no gil, no normal drop
} else {
    spoils.gil = clamp(spoils.gil + gil * mult, 0, 999999999);
    for (k = 0; k < 2; ++k) {                        // two drop slots
        if (Rand(10) % 255 < rec[+8 + k] || (dbgDmgProbOff && rec[+8 + k])) {
            rare = (u8)Rand(11) < 32 || dbgAlwaysRare;        // 32/256
            idx  = 2*k + rare;
            id   = overkill ? rec.u16[+24 + 2*idx] : rec.u16[+12 + 2*idx];
            qty  = overkill ? rec[+32 + idx]       : rec[+20 + idx];
            if (st2 & 0x02) id = 0x2046;             // Power Distiller -> Power Sphere
            else if (st2 & 0x04) id = 0x2047;        // Mana Sphere
            else if (st2 & 0x08) id = 0x2048;        // Speed Sphere
            else if (!(st2 & 0x10) && (st2 & 0x20)) id = 0x2049;   // Ability Sphere
            AddItem(id, qty);
        }
    }
}
if (Rand(10) % 255 < rec[+11] || (dbgDmgProbOff && rec[+11]))
    sub_798C10(killer, rec, &spoils);                // equipment, 4.3
```

`victim+0x708` is the bribe ratio `((gilPaid << 8) / maxHP) / 20 - 64` that `FFX_Btl_RollBribe`
stored, not the gil. The x87 rounding points are kept above (sqrt stored to float, the product
stored to double, the sum stored to float, then truncated). Language 11 swaps item 0x2065 for
0x2048 x4 (bribe, and the swap is written back into the record at 0x79922D) or 0x2032 x4, x8 on an
overkill (drop, 0x799329..0x799336). [V, corrected 2026-10-29]

**`sub_798A00` (propose `FFX_Btl_BankKillAp`)**, per unit 0..17: [V]

```c
if (a[0xDCD]) return mult;                           // left the battle, banks nothing
v = (a.u16[0x6BE] & 0x40) ? 0 : ap;                  // No AP
if (a[0xDC8] && !a[0xDCC]) {                         // on the field and not KO
    if (a.u16[0x6BE] & 0x4000) mult = 2;             // Gillionaire, any one unit is enough
    if (a.u16[0x6BE] & 0x20) v *= 3;                 // Triple AP
    else if (a.u16[0x6BE] & 0x10) v *= 2;            // Double AP
}
spoils.ap[i] = clamp(spoils.ap[i] + v, 0, 999999999);
return mult;
```

So **every** unit 0..17 banks the plain AP of every kill, KO'd and benched ones included, and only
the end filter (4.4) takes it back. The auto-ability bits are [D] from `a_ability.bin` row +100
(0x8011 Overdrive -> AP 0x0008, 0x8012 Double AP 0x0010, 0x8013 Triple AP 0x0020, 0x8014 No AP
0x0040, 0x801A Gillionaire 0x4000). [V] [D]

Overdrive -> AP feeds the same accumulator: `FFX_Btl_AddOverdrive` with `+0x6BE & 8` calls
`sub_798A00(unit, actor, gain * ApNeededForNextLevel / gaugeMax * actor.f32[0x70C], 0)` and
multiplies `+0x70C` by 0.9 each time. [V]

Correction 2026-10-29 (the Project Zanarkand port, PROGRESS 13.9), the order of that arithmetic from
0x7B165E..0x7B16AB: the AP need is multiplied by the scaled gain in 32 bit integers (`imul`), divided by
the gauge max with a signed `idiv`, and only that integer quotient goes to the FPU. `fild`, rounded to a
float, times the float at +0x70C, rounded to a float again, then `__ftol2_sse` truncates. So
`int(float(float(need * v / max) * ratio))`, not a float product throughout. The 0.9 is a double constant
(0x749958), the product rounded to the float at +0x70C. The gain banked is the scaled one, after Triple,
Double, the derived x1.5 and x2, Shield and Boost, and the gauge gets nothing. No writer of +0x70C by
offset was found, so its first value comes from a block copy that is not traced.

**`sub_798AC0` (propose `FFX_Spoils_AddItem`)**: id 0 or quantity <= 0 is ignored, an id already
staged adds (clamp 0..99), else a new kind while fewer than 8. A ninth kind is silently lost. [V]

### 4.3 The equipment drop, `sub_798C10` (propose `FFX_Btl_RollEquipmentDrop`)

`sub_798BD0` takes the next of the 8 scratch entries (none free -> no draws at all). Then: [V]

```c
n = count of c in 0..6 with FFX_SaveData_IsCharSelectable(c);     // record +0x2C bit 0x10
if (killer < 7) n += 3; else killer = 0;           // the killer gets 3 extra tickets
r = Rand(12) % n;                                  // owner: the r-th selectable, else the killer
e.forChar   = owner;
e.kind      = Rand(12) & 1;                        // 0 weapon, 1 armour
e[+6]       = 0xFF;  e[+8] = rec[+46];  e[+9] = rec[+48];  e[+10] = rec[+47];
a = (Rand(12) & 7) + 1;  b = (Rand(12) & 7) + 1;
e.slots = clamp((a + rec[+45] - 5) / 4, 1, 4);     // C division
tries   = (b + rec[+49] - 5) / 8;
tbl = rec + 50 + 32*owner + 16*kind;               // 8 u16 per character per kind
if (tbl[0]) put tbl[0];                            // the forced ability
while (tries-- > 0 && placed < e.slots) {
    x = tbl[1 + Rand(13) % 7];
    if (x && group(x) not already placed) put x;   // group = a_ability row +105
}
fill the rest with 0xFF, FFX_Equip_ComputeNameId, FFX_Equip_GetNameString
```

So the killer has `4 / (n + 3)` of the owner roll and every other selectable character `1 / (n +
3)`. The loop stops drawing once the slots are full. [V]

### 4.4 Who gets AP: `sub_798B70` (propose `FFX_Btl_FinalizeSpoilsAp`)

In the teardown, before the commit: [V]

```c
for (i = 0; i < 18; ++i) {
    if (!spoils.acted[i] || actor(i)[0xDCC] || actor(i)[0xDCE])
        spoils.ap[i] = spoils.apAtStart[i];        // nothing from this battle
    if (spoils.tookPart[i]) FFX_SaveData_BumpCharBattleCounter(i, 0);   // record +0x50
}
spoils[+0xC8] = 0;
```

So a character earns this battle's AP only if it **took an action (anything but Switch) and is
neither KO'd nor petrified at the end**, and it earns the AP of every kill made while it was in
the battle, before or after it acted, including kills made while it was benched. Escaping members
are not KO'd, keep what they banked before leaving, and bank nothing after (`+0xDCD`). [V]

### 4.5 The results screen commits (menu module 17, `FFX_BtlResult_*`)

Opened from the field boot (3.4), on menu frames, after the battle module has shut down: [V]

| what | where | how |
|---|---|---|
| member rows | `sub_8BDE00` | units 0..7 with the took-part flag. AP phase only when one of them has AP |
| equipment | `FFX_BtlResult_Init 0x8BDB70` | if the free equipment entries are at least the staged count, `FFX_SaveData_AddEquipEntry` for each, at once. Otherwise the "equipment full" flag 0x186A184 and a discard flow (states 32..43) [I] for the flow |
| gil | `sub_8BA590` per frame, states 23..24 | chunks of `(gil / 189) \| 1`, +2 when that is a multiple of 5, through `FFX_SaveData_SpendGil(-chunk)` |
| AP | `sub_8BA550` per frame, state 12 | per member chunks of `(ap / 189) \| 1` (+2 likewise) through `FFX_SaveData_AddAp`. A member at 99 sphere levels has its remaining AP set to 0 and loses it. Pressing confirm (0x20) runs the loop to the end in one frame |
| items | `sub_8BD9C0` from `FFX_BtlResult_DrawAndClose 0x8BB7A0` | `FFX_SaveData_AddItem` for every staged kind, when the screen closes |
| clear | `sub_798B60` | right after the items |

The end state does not depend on when the player presses confirm, but the steps on which gil, AP
and items land in the save block do. [V]

### 4.6 Where the reward tables live

- **The 280 byte reward record**: `battle/mon/_mNNN/mNNN.bin`, the block at header dword +20.
  Header `{u32 count = 8, u32 offset[8]}`, all 361 shipped files, the block 280..292 bytes, only the
  first 280 copied. [D] Layout from how 4.2 and 4.3 read it [V], the steal fields from
  `sub_78B760` only by position [I]:

| off | type | field |
|---|---|---|
| +0 | u16 | gil |
| +2 | u16 | AP |
| +4 | u16 | overkill AP |
| +6 | u16 | not read by the payout [I] |
| +8, +9 | u8 | drop chance, slots 0 and 1, out of 255 |
| +10 | u8 | steal chance [I] |
| +11 | u8 | equipment drop chance, out of 255 |
| +12 | u16[4] | drops: slot 0 common, rare, slot 1 common, rare |
| +20 | u8[4] | their quantities |
| +24 | u16[4] | overkill drops |
| +32 | u8[4] | their quantities |
| +36 | u16[2], +40 u8[2] | steal common and rare, quantities [I] |
| +42 | u16 | bribe item |
| +44 | u8 | bribe quantity factor |
| +45 | u8 | equipment slot base |
| +46, +47, +48 | u8 | copied to equipment entry +8, +10, +9 |
| +49 | u8 | equipment ability count base |
| +50 | u16[7][2][8] | ability tables, per character, weapon then armour, [0] forced |

- **Auto-ability groups and bits**: `battle/kernel/a_ability.bin` (kernel table, 134 rows of 108).
  Row +100 is the word ORed into `+0x6BE`, row +105 the group the equipment roll uses. [D]
- **Chests**: `battle/kernel/takara.bin` (`g_ffxTreasureTable 0x1135FF4`, battle kernel index 25)
  and `battle/kernel/buki_get.bin` (`g_ffxEquipTemplateTable 0x1135FFC`, index 26), loaded by
  `FFX_Treasure_LoadTables 0x799020`, resolved through the cd index (`cdrom.fid[13] = 7798`). [V] [D]

### 4.7 `FFX_Treasure`

`FFX_Treasure_DecodeToStaging 0x798FD0(id)` = `FFX_Treasure_GiveReward 0x799420(id, spoils)`. Row
byte 0 is the kind: 2 an item (`sub_798AC0(u16 +2, byte +1)`), 5 equipment from template `u16 +2`
(`sub_798BD0`, abilities 0 -> 0xFF, slots = max(template +7, abilities), name id), 10 a key item
(+0xD1, +0xEC), anything else gil `100 * byte +1`. The chest syscalls `Core 347` and `Core 423`
then commit and call `sub_798B60`. No RNG. [V] Because chests and battles share the buffer, a chest
opened while battle spoils are still staged (4.1) pays them out and clears them. [I]

---

## 5. Escape

| command | id | RNG | effect | |
|---|---|---|---|---|
| Escape | 0x3003 | one draw, the actor's **kind 0** stream (20 + slot) | success when `(u8)r < 191` (191/256), forced by escape mode 2, refused by 1 or `g_ffxBtlDbgDmgHitMiss`. On success `sub_7B1090`, poison cleared, post action kind 4 | [V] |
| Flee | 0x3018 | none | `dword_112C9F4` byte 1, poison cleared, post action kind 5 | [V] |

At action finish (`FFX_Btl_FinishActions`, `actor+0xDEA`): kind 4 is `sub_78DE40(unit, actor, 1)`
(`+0xDCD = 1`, `+0xDC8 = 0`, the unit leaves), kind 5 is `sub_78DE80(1)`, the same for units 0..17.
Kinds 2 and 3 are `sub_7AEF20`, the aeon recall. When no living party member is on the field any
more the test returns 3. [V] Escape is in `sub_7AE370` case 0x3003 at 0x7AEA86 and the draw is in
`sub_78A780` 0x78A796. Because it is the kind 0 stream, a failed or successful escape shifts that
character's damage variance draws. [V]

---

## 6. Game over

**Condition**: the test returns 1 (no active member alive, petrify counting as dead, 3.1) and the
battle options do not have bit 0x200. With 0x200 set (story fights you are meant to lose) result 1
is an ordinary end, saveData+0xD0 = 1, and the field boots. [V]

**Sequence**: end phases 23, 24 (game over flag, track 20 [I], effect 140) -> 25 -> 26 -> 27 ->
28 -> 29 (event 67, `sub_7982A0(1, ..)`) -> 30 (global magic effect 670, the "game over" visual [I])
-> 31 (45 frame fade) -> 32 (overlay free) -> 33 -> teardown. The teardown is the normal one:
`sub_798B70` and **`FFX_Btl_CommitActorsToSave` still run**, then `FFX_Btl_Shutdown`, then
`maybe_FFX_Field_QueueBootTasks` sees result 1 without 0x200 and calls `FFX_Map_ReturnToTitle`,
which also clears the spoils buffer. [V] The writes to the live save block do not matter because
the title reloads it. [I]

Game over waits on the magic overlay for effect 670 (`sub_788BD0` in phases 30 and 32). That is the
H3 / H4 magic DLL load in ASYNC_LOAD_REQUESTS.md, so its length is disk dependent. [V] for the
call, [I] for the H3 / H4 link.

---

## 7. Every RNG draw, start and end, in order

Streams through `FFX_Rand_Stream 0x7988F0`, the per-unit map is `FFX_Btl_RandStreamForUnit
0x78D210` (BATTLE_CTB_AND_STATUS.md 3.1). The call site census (every static caller of
`FFX_Rand_Stream` in 0x780000..0x7C6000) is the source for "no other draw". [V]

**Start**

| # | when | site | stream | count |
|---|---|---|---|---|
| S1 | field step | `FFX_Field_StepRandomEncounter` 0x780EEA, 0x780EFD | 0, then 1 | 1 + 1, random battles only |
| S2 | phase 10 | `FFX_Btl_RollPreemptiveOrAmbush` 0x78D098 | 1 | 1, always, also when the advantage is forced |
| S3 | phase 10, same call | battle script event 24 and AI event 63 per on-field party unit | 16..19, camera, AI target picks | script dependent [I] |
| S4 | last monster file completion | `sub_7842F0` 0x784379, 0x7843C0 | 28..35 | per monster group (BATTLE_CTB_AND_STATUS.md 3.2 row 3) |
| S5 | same | `FFX_Btl_InitCtbCounters` 0x78DF3E | 20..26, then 27 x 11 | 18, normal start only |
| S6 | same | `FFX_Btl_InitCtbCounters` 0x78DFE7 | 28..35 | 8, normal start only |
| S7 | phases 15, 22 | events 118 and 124 | script dependent | [I] |

Phases 11..22 otherwise draw nothing: the loaders, `sub_79CB10`, the motion and sound bank
registration, `sub_79A0E0`, the advantage message. [V]

**During the battle, on the end path**

| # | when | site | stream | count |
|---|---|---|---|---|
| E1 | Escape command | `sub_78A780` 0x78A796 | actor kind 0 | 1 |
| E2 | any death with `a3 == 0` | `sub_7990D0` | see below | |
| E2a | bribed victim | 0x799178, 0x7991DF | killer kind 0 | 2 |
| E2b | other victims, per drop slot 0 then 1 | 0x799282 | 10 | 1 each, always 2 |
| E2c | right after a slot that dropped | 0x7992B2 | 11 | 1 |
| E2d | after both slots (also after a bribe) | 0x79936B | 10 | 1, always |
| E2e | equipment dropped | `sub_798C10` 0x798C76, 0x798CB4, 0x798CF3, 0x798D00 | 12 | 4 (owner, kind, slots, ability count), none when 8 are staged |
| E2f | per ability attempt while slots are free | 0x798DBA | 13 | 0..N |

So each non bribed death draws stream 10 three times plus stream 11 once per dropped slot, and a
party member's or aeon's death draws stream 10 three times and nothing else. [V]

**End**: none. `sub_7928D0`, the end phases, `sub_7B1540`, `sub_798B70`, the commit, the boot
queue and the results screen draw nothing. The victory and game over events (66, 67, 68, 69, 45)
run scripts that could. [V] / [I] for the scripts.

---

## 8. Timing and disk dependent points

Every one of these decides **on which step** something happens. None of them changes an RNG value
by itself, the values follow the order above, but anything that draws on the same streams in
between, or any frame-counted logic, will see the difference. [V] unless marked.

| point | what waits | thread | effect |
|---|---|---|---|
| phase 4 | the MUSIC boot task, which waits on `AssetLoader[2](1)`, the sound loader, and FMOD command 20 (track 16 loading, read across threads with a 10 ms budget) | step, FMOD thread for command 20 | battle start step |
| phase 6 | battle field file ROM read, plus the `FFX_Btl_OpenFieldArchive` RegistTask (S1) | completion on step | roster step |
| phases 9, 34 | battle map load, `g_ffxBtlMapLoadPending` cleared on the **worker** (H1) | off thread | the step of S2. Skipped entirely when the battle map is the current scene, then S2 runs on the roster step |
| phase 12 | party CHR loads (H5) | off thread | |
| phase 13 | one ROM read per distinct monster, S4 to S6 run in the last completion inside `FFX_AsyncLoad_DispatchCompletions` | completion on step | the step of the CTB draws, before `FFX_Btl_MainStep` of that frame |
| phases 15, 17, 18, 20 | monster CHRs, foot data, weapon models, weapon CHRs | mixed | |
| phase 22 | the advantage message and `dword_112CA64` (90 frames when `sub_7F9280` set it) | step | first menu step |
| end 23, 24 | sound and text idle | step | |
| end 27 | every unit's animation done | step | |
| end 30, 32 | game over magic overlay load | off thread | game over length |
| results | gil, AP, items land on menu frames, and confirm skips the AP count | step, input | the step the save block changes |

A debug only path: `g_ffxBtlDbgDvdSlowStart` adds 60 step waits in phases 10, 12 and 15. [V]

For lockstep: the battle start needs one "battle loaded" barrier per peer before phase 10 (S2) and
one after the last monster completion (S4 to S6), or the H1 and phase 13 completions have to be
delivered on an agreed step (the ASYNC_LOAD_REQUESTS.md port rule). The spoils commit happens in a
menu module on frames driven by the local confirm button, so a peer must treat the whole results
screen as one replicated commit, or apply the staged spoils itself at a fixed step. [I]

---

## 9. Corrections to other docs

- **RANDOM_ENCOUNTER.md** section 6: `FFX_Btl_BeginBattle` mode 0 sets sub phase **4**, not 5, and
  0x112A8E0 is `g_ffxBtlSubPhase`, there is no separate `g_ffxBtlPhase`. Phase 5 is the entry for a
  non zero mode. Phase 4 waits for the boot task queue. [V]
- **ASYNC_LOAD_REQUESTS.md** H1: phase 7 goes to phase 10 directly when the battle map equals the
  current scene id, and then H1 does not apply. [V]
- **GAME_STATE.md**: `g_ffxTreasureStaging 0x2310EA0` is also the battle spoils buffer (4.1), and
  `FFX_Btl_CommitPartyToField` is preceded by `FFX_Btl_RestorePushedPartyOrder`, which restores a pushed arrangement, so
  "a party change made inside battle survives" holds only when no script pushed one. [V]
- **CHEAT_SAVEDATA.md** 4.1: equipment entry +0x08, +0x09, +0x0A do have a reader,
  `FFX_Btl_LoadCharRecordIntoActor` (+8 -> actor +0x5C1, +9 -> +0x5C7, +10 added into +0x5D8). [V]
- **IDB names that mislead** (not edited): `__exit 0x82CB80` is a fade request and its noreturn flag
  breaks the decompilation of `FFX_Btl_BeginBattle`. `?doThrowTypes@UnDecorator@@SAHXZ_0 0x86C280`,
  `?doMemberTypes@UnDecorator@@SAHXZ 0x86C290`, `?doEcsu@UnDecorator@@SAHXZ 0x86C4E0` and
  `?__uncaught_exception@@YA_NXZ_1 0x875180` are folded game functions. [V]

These are appended as dated notes to the four docs.

---

## 10. Proposed IDB names

| VA | current | proposed | reason |
|---|---|---|---|
| 0x7928D0 | sub_7928D0 | `FFX_Btl_TestBattleOver` | returns the result code 0..4 or the forced one |
| 0x7990D0 | sub_7990D0 | `FFX_Btl_PayKillRewards` | AP, gil, drops, bribe item, equipment chance per kill |
| 0x798A00 | sub_798A00 | `FFX_Btl_BankKillAp` | per unit AP with No / Double / Triple AP, Gillionaire multiplier |
| 0x798AC0 | sub_798AC0 | `FFX_Spoils_AddItem` | merge or append into the 8 item kinds |
| 0x798BD0 | sub_798BD0 | `FFX_Spoils_AllocEquipEntry` | next of the 8 staged equipment entries |
| 0x798C10 | sub_798C10 | `FFX_Btl_RollEquipmentDrop` | streams 12 and 13 |
| 0x798A90 | sub_798A90 | `FFX_Btl_GetAutoAbilityGroup` | a_ability row +105 |
| 0x798B60 | sub_798B60 | `FFX_Spoils_Clear` | zeroes the buffer, 7 callers |
| 0x7993E0 | sub_7993E0 | `FFX_Spoils_Reset` | memset 432 plus the 0x7000 ids |
| 0x798B70 | sub_798B70 | `FFX_Btl_FinalizeSpoilsAp` | the AP end filter |
| 0x798E90 | sub_798E90 | `FFX_Btl_SnapshotSpoilsAp` | +0x80 -> +0x38 at begin |
| 0x799FB0 | sub_799FB0 | `FFX_Btl_AllocRewardRecords` | 31 x 280 zeroed |
| 0x799F70 | sub_799F70 | `FFX_Btl_FreeRewardRecords` | its pair in the shutdown [I] |
| 0x782FE0 | sub_782FE0 | `FFX_Btl_OnFieldFileLoaded` | sets phase 7 |
| 0x783990 | sub_783990 | `FFX_Btl_OnMonsterFileLoaded` | binds and re-enters the loader |
| 0x79CB10 | sub_79CB10 | `FFX_Btl_LoadEnemyUnitParams` | LoadUnitParams for slots 20..27 |
| 0x79A0E0 | sub_79A0E0 | `FFX_Btl_ClampUnitHpMp` | clamp HP and MP to max |
| 0x7AEEC0 | FFX_Btl_RestorePushedPartyOrder | `FFX_Btl_RestorePushedPartyOrder` | undoes `FFX_Btl_BeginSummon` before the commit |
| 0x78E0A0 | sub_78E0A0 | `FFX_Btl_StartAeonRecovery` | HP 0, countdown = ply_rom +43 + 1 [I] meaning |
| 0x7B1540 | sub_7B1540 | `FFX_Btl_ApplyVictorOverdrive` | mode 11 counter and +20 gauge [I] mode name |
| 0x78A780 | sub_78A780 | `FFX_Btl_RollEscape` | kind 0 draw < 191. The draw (0x78A796) comes before the mode 1 and HitMiss refusals, so a refused escape still moves the stream |
| 0x78DE40 | sub_78DE40 | `FFX_Btl_SetUnitLeftBattle` | +0xDCD and +0xDC8 |
| 0x78DE80 | sub_78DE80 | `FFX_Btl_PartyLeftBattle` | the same for 0..17 |
| 0x7AE0A0 | sub_7AE0A0 | `FFX_Btl_RemoveUnitFromField` | +0xDC8 = 0, queue cleanup |
| 0x7AEF20 | sub_7AEF20 | `FFX_Btl_RecallParty` | returns the party when aeons go [I] |
| 0x7AE370 | sub_7AE370 | `FFX_Btl_StartCommandExec` | the per-command switch, sets acted and the post action kind |
| 0x86FC20 | sub_86FC20 | `FFX_Atel_SetFieldBootMode` | writes 0x1326B48 and the two movie args |
| 0x86CFF0 | sub_86CFF0 | `FFX_Atel_ResetFieldBootMode` | = 0x10001 |
| 0x86C280 | ?doThrowTypes@UnDecorator@@SAHXZ_0 | `FFX_BootMode_VictoryPoseOn` | `!(bit 8)` |
| 0x86C290 | ?doMemberTypes@UnDecorator@@SAHXZ | `FFX_BootMode_GameOverOn` | `!(bit 9)` |
| 0x86C4D0 | sub_86C4D0 | `FFX_BootMode_EndFadeOn` | `!(bit 16)` |
| 0x82CB80 | __exit | `FFX_ScreenFade_RequestMode2` | `FFX_ScreenFade_Request(code, 1, 0)`, and clear noreturn |
| 0x875180 | ?__uncaught_exception@@YA_NXZ_1 | `FFX_BootTask_IsBusy` | `g_ffxBootTaskQueues != 0` |
| 0x861160 | maybe_FFX_Field_QueueBootTasks | `FFX_Btl_ReturnToField` | result to save +0xD0, game over, results screen |
| 0x8BA590 | sub_8BA590 | `FFX_BtlResult_StepGil` | per frame gil chunk |
| 0x8BA550 | sub_8BA550 | `FFX_BtlResult_StepAp` | per frame AP chunks |
| 0x8BD9C0 | sub_8BD9C0 | `FFX_BtlResult_CommitItems` | AddItem per staged kind |
| 0x8BDE00 | sub_8BDE00 | `FFX_BtlResult_BuildRows` | took-part units 0..7 |
| 0x8BA3A0 | sub_8BA3A0 | `FFX_BtlResult_AddGil` | SpendGil(-n) |
| 0x8BA390 | sub_8BA390 | `FFX_BtlResult_AddAp` | the only AddAp caller |
| 0x112A8E3 | word_112A8E3 | `g_ffxBtlEndResult` (low byte) | |
| 0x112C9F1 | word_112C9F1 | `g_ffxBtlForcedResult` (low byte) | `Btl 052` |
| 0x112C9EC | word_112C9EC | `g_ffxBtlEndWaitFrames` | the two end counters |
| 0x112C9F4 | dword_112C9F4 | `g_ffxBtlGameOverSeq` (byte 0) | |
| 0x1136068 | dword_1136068 | `g_ffxBtlRewardRecords` | |
| 0x2310EC4 | byte_2310EC4 | `g_ffxSpoilsActed` | |
| 0x2310ED8 | dword_2310ED8 | `g_ffxSpoilsApAtStart` | |
| 0x2310F20 | dword_2310F20 | `g_ffxSpoilsAp` | |
| 0x2310F6C | dword_2310F6C | `g_ffxSpoilsGil` | |
| 0x11307A4 | dword_11307A4 | `g_ffxBattleCount` | saveData+0x3D14 |

---

## 11. tools/btl_rewards.py

`E:\Git\projectzanarkand\tools\btl_rewards.py`. Reads the monster files and `a_ability.bin`
read-only through the co-op `vbf.py`, resolving `battle/mon` through `cdrom.fnd` / `cdrom.fid`
kind 16 the way `FFX_Btl_LoadMonsterData` does, and the RNG tables from FFX.exe through `ctb.py`.
Nothing from the game is written anywhere.

- `show N..` prints a reward record, ability tables included.
- `scan` one line per shipped monster.
- `kill --monster N` runs 4.2 and 4.3 with the real streams (`--state`, `--no-rng`), the party's
  auto-abilities (`--double-ap`, `--triple-ap`, `--no-ap`, `--gillionaire`), `--overkill`,
  `--distill`, `--bribe-ratio`, `--kills`, prints every draw, then applies 4.4.
- `selftest` checks all 361 files (8 sections, a block of at least 280 bytes), the five
  auto-ability bits in `a_ability.bin`, item merging and the 8 kind cap, the slot and ability count
  clamps, the payout order (drop, rare, drop, rare, equipment chance), Gillionaire, Triple AP, the
  `+0xDCD` and KO rules, the three stream 10 draws for a zero record, and the first draw of streams
  10 to 13. It passes.

Not modelled: steal, the results screen chunking, and which units count as having acted.

---

## 12. Still open

- What `rec+6` is (not read by the payout). A guess is Kimahri's Ronso Rage, not checked. [I]
  2026-10-29: confirmed. `FFX_Btl_ApplyPendingEffects` reads the u16 at `rec+6` of the Lancet
  target and teaches it to Kimahri (BATTLE_COMMAND.md, correction 2026-10-29). [V]
- The steal fields (+10, +36..+41) are placed by position only. `sub_78B760` was not read.
  2026-10-29: read (`FFX_Btl_ApplySteal` 0x78B760). +10 the steal chance, halved on a success
  (50 * chance / 100, at least 1), +36 and +38 the common and rare item, +40 and +41 their
  counts. Stream 10 % 255 below the chance steals, then stream 11 & 0xFF below 32 (128 with
  Pickpocket, always with the master flag) takes the rare one when it is set. +275 is the gil
  steal factor `sub_78B920` reads. [V]
- Which shipped battles set boot mode 0x200 (losable), 0x100, or return mode 1 (no results screen,
  spoils carried over to the next battle, 4.1).
- Result 4 and result 6 in shipped content. 6 has no PC trigger left besides `Btl 052`.
- Equipment entry +8, +9, +10 meanings (actor +0x5C1, +0x5C7, +0x5D8 have not been named).
- The exact frames of the advantage message, the music track ids 12 and 20, and effects 138, 140
  and 670 are inferred from their use, not from their data.
- A recorded battle checking S2 and E2 against a capture of streams 1 and 10 to 13.
