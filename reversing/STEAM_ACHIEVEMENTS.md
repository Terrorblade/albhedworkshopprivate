# Steam achievements

2026-10-27. PROGRESS 1.12.9 for Project Zanarkand, and what 3.2.4 needs to fire each one at the
original's trigger. All addresses are IDA VAs at base 0x400000. Read through idalib on a scratch copy
of the IDB, nothing saved. The script census walked the install with `../tools/ebp.py`, read only,
no script bytes copied. `../tools/apply_achievement_names.py` puts the names used here into the IDB.

Marks: [V] read in the decompile or the disassembly, [D] counted in shipped data, [I] inferred.

## 1. Short version

- **34 achievements, one table, no stats.** The exe never calls GetStat or SetStat. Every progress
  counter lives in the save block or in a process global. [V]
- **One class does all the Steam work**, `StatsAndAchievements` (the name is in the RTTI of its three
  `CCallback<StatsAndAchievements, ...>` members). It is the Steamworks SDK sample class of that name
  with the stats stripped out. One instance, built at boot only when `SteamAPI_Init` succeeded. [V]
- **Every unlock is `FFX_Trophy_Unlock(row)` 0x822380**, row 0..33 being the Steam table row. It ends
  in `SetAchievement(apiName)` then `StoreStats()`. 30 native call sites, plus ATEL `core:603` for 9
  rows. [V]
- **Scripts pass console trophy numbers, native code passes the Steam row.** The two differ in three
  places, which `core:603` patches: script 22 is dropped, 23 becomes 22, 25 becomes 24. [V] That the
  script numbers are the PS3/Vita list is [I].
- **Unlock is idempotent and nearly ungated.** A local achieved byte per row stops a second call. The
  only gates are `g_ffxSteamApiReady` and a save manager flag that is never cleared. Debug mode,
  boosters and the loaded save do not gate anything, and the Esc menu "all abilities" grant itself
  unlocks two rows. [V]
- **Several triggers are edge triggered on process state**, so a save that is already past the
  condition does not unlock on load. Rows 3, 5, 6, 20 and 27 are the ones a recreation will get wrong
  if it tests "is the condition true now". Section 6.
- **Everything runs on the main thread (T0)**, inside `FFX_MainStep` except the Esc menu grant. Rows 16,
  18 and 21 fire from the message window draw pass, which is skipped on catch-up steps. [V]

## 2. How FFX.exe reaches Steam user stats

### 2.1 Imports [V]

`steam_api.dll` (SDK 1.31, `STEAM_ABI.md`) exports nine functions to the exe. The four that matter here:
`SteamAPI_Init`, `SteamAPI_RunCallbacks`, `SteamUserStats`, `SteamAPI_RegisterCallback` (and its
unregister twin). `__imp_SteamUserStats` 0xB0C898 has 11 code xrefs, all in 0x6F11A0..0x6F1948:

| site | method | ISteamUserStats011 slot |
|---|---|---|
| 0x6F12C9, 0x6F12EE | ctor, null check then `RequestCurrentStats` (call [edx] at 0x6F12F8) | 0 |
| 0x6F1420 | `ResetAllStats(true)` (call [edx+54h]) | 21 |
| 0x6F1635 | `GetAchievement(api, &row.achieved)` (call [edx+18h]) | 6 |
| 0x6F164C, 0x6F1679 | `GetAchievementDisplayAttribute(api, "name" / "desc")` (call [edx+30h]) | 12 |
| 0x6F17D9, 0x6F1800 | orphan `RequestCurrentStats` | 0 |
| 0x6F18BF, 0x6F18CF | orphan `SetAchievement(name)` (call [edx+1Ch]) and `StoreStats` (call [edx+28h]) | 7, 10 |
| 0x6F1930, 0x6F193E | `SetAchievement(row api)` (0x6F193B call [edx+1Ch]) and `StoreStats` (0x6F1948 call [edx+28h]) | 7, 10 |

Slots checked against Valve's 1.31 `isteamuserstats.h` (Proton's `steamworks_sdk_131` tree): 0
RequestCurrentStats, 6 GetAchievement, 7 SetAchievement, 10 StoreStats, 12
GetAchievementDisplayAttribute, 21 ResetAllStats. The MSVC overload reordering only swaps slots 1..4
(GetStat and SetStat pairs), which nothing calls. So no stat is touched anywhere. [V]

Also used: `SteamUtils()` slot 9 GetAppID (0x6F1296 call [edx+24h]) for the `CGameID`, `SteamUser()` slot
1 BLoggedOn (0x6F12E8). The callback ids are 1101 UserStatsReceived_t, 1102 UserStatsStored_t, 1103
UserAchievementStored_t (`push 44Dh/44Eh/44Fh` at 0x6F11F4, 0x6F1237, 0x6F1273). [V]

### 2.2 The object [V]

`StatsAndAchievements__ctor 0x6F11A0`, 0x50 bytes, singleton `g_ffxStatsAndAchievements 0xCE81A8`:

| off | what |
|---|---|
| +0x00 | CCallback UserStatsReceived_t -> `StatsAndAchievements__onUserStatsReceived 0x6F15F0` |
| +0x14 | CCallback UserStatsStored_t -> `StatsAndAchievements__onUserStatsStored 0x6F16D0`, empty |
| +0x28 | CCallback UserAchievementStored_t -> `StatsAndAchievements__onAchievementStored 0x6F15E0`, empty |
| +0x3C | CGameID, u64, built from GetAppID with the 0xFFFFFF app id mask |
| +0x44 | `Achievement_t *` = `g_ffxAchievementTable 0xC35790` |
| +0x48 | row count, 34 (`mov dword ptr [esi+48h], 22h` at 0x6F12C2) |
| +0x4C | stats received, 0, set to 1 by the received callback. Nothing reads it |
| +0x4D | enabled, 1 (`mov word ptr [esi+4Ch], 100h` at 0x6F12B5). The only writer. Every unlock tests it |

The ctor ends by calling RequestCurrentStats when SteamUserStats and SteamUser exist and BLoggedOn.
`onUserStatsReceived` checks the game id and `m_eResult == 1`, then for every row copies the Steam
achieved bit into the row and replaces the row's name and description with Steam's display strings.
So the exe's own names (section 3) are only fallbacks, and the descriptions in the exe are empty.

### 2.3 Call path [V]

```
boot
  _WinMain 0x62F1CF -> FFX_SteamApiInit 0x623470
       RestartAppIfNecessary(0), SteamAPI_Init, g_ffxSteamApiReady 0xCC9CF5 = 1 (0x6234A9)
  FFXApplication__initApplication 0x42F899 -> FFX_SaveSys_Init 0x647FE0
       if g_ffxSteamApiReady: FFX_Ach_Create 0x6F1430 -> StatsAndAchievements__ctor -> RequestCurrentStats
every frame
  FFXApplication__animate 0x42F52C: if g_ffxSteamApiReady, SteamAPI_RunCallbacks
       -> onUserStatsReceived 0x6F15F0 (main thread)
unlock
  FFX_Trophy_Unlock(row) 0x822380
    if maybe_FFX_SaveMgr_IsGameStarted 0x6480E0            (SaveMgr+0, see 4)
      FFX_Ach_UnlockIfSteamReady(row) 0x6488C0
        if g_ffxSteamApiReady
          FFX_Ach_Get()->StatsAndAchievements__unlock(row) 0x6F1510
            if +0x4D and row <= 33 (cmp edx, 21h / ja at 0x6F151F) and !table[row].achieved
              StatsAndAchievements__setAchievement(row) 0x6F1900
                 achieved = 1, icon = 0, SetAchievement(table[row].api), StoreStats
              StatsAndAchievements__unlockCompletionIfAllDone 0x6F1550
                 if !row0.achieved and rows 1..33 all achieved: unlock(0)
exit
  _WinMain 0x62F45D SteamAPI_Shutdown. The object is never freed (FFX_Ach_Destroy_ORPHAN 0x6F14A0).
```

## 3. The table

`g_ffxAchievementTable 0xC35790`, 34 rows of the SDK sample's `Achievement_t`, 400 bytes each: +0 row id,
+4 `const char *` API name, +8 `char name[128]`, +0x88 `char desc[256]`, +0x188 `bool achieved`, +0x18C
`int icon`. The row ids in +0 equal the row index. The API names are .rdata strings 0xB4B6F0..0xB4B9C4.
The names below are the exe's fallback copies, typos included. [V]

Column "script" is the id an ATEL script passes to `core:603`, when a script fires it. Column "where"
is the trigger site, a call address for native code, or package / actor / entry / code offset for a
script. Section 5 has the details of every row.

| row | API name | exe name | script | where | condition | counter |
|---|---|---|---|---|---|---|
| 0 | ACH_COMPLETION_OF_FFX | Completion of FFX | | `unlockCompletionIfAllDone` 0x6F1573 | rows 1..33 all achieved, tested after each new unlock | the achieved bytes |
| 1 | ACH_SPEAKING_IN_TONGUES | Speaking in Tongues | | `FFX_SaveData_LearnAlBhedPrimer` 0x906B5B | any primer 0..25 learned | save +0x3D10 |
| 2 | ACH_TEAMWORK_OF_FFX | Teamwork! | 2 | lchb0601 a0 e16 0x71C, bltz0201 a6 e1 0x41086 | Luca story match script, league path when var34 > 0 | script |
| 3 | ACH_THE_RIGHT_TING | The Right Thing | | `FFX_MainStep` 0x820F12 | scenario word changes to 164 | save +0xBEC |
| 4 | ACH_A_TALENT_FOR_ACQUISITION | A Talent for Acquision | | `FFX_Btl_ApplySteal` 0x78B8F2 | Rikku steal success, count >= 200 | save +0x3DB0 |
| 5 | ACH_ALL_TOGETHER | All Together | | `FFX_MainStep` 0x820F12 | scenario word changes to 1085 | save +0xBEC |
| 6 | ACH_HEARTSTRINGS | Heartstrings | | `FFX_MainStep` 0x820F12 | scenario word changes to 2360 | save +0xBEC |
| 7 | ACH_SHOW_OFF | Show Off | 7 | bltz0201 a6 e1 0x3A52E | var170 == 1 | script |
| 8 | ACH_STRIKER | Striker | 8 | swin0000 a32 e8 0x6725, swin0200 a24 e6 0x139B6 | Jecht Shot practice success | script |
| 9 | ACH_CHOCOBO_LICENSE | Chosobo License | 9 | nagi0000 a72 e12 0x2144B, 0x22522, 0x235F9, 0x246D0 | right after `core:347(0, 340)`, one site per course [I] | script |
| 10 | ACH_OVERCOMING_THE_PAST | Overcoming the Past | | `FFX_Ach_CheckBossKill` 0x8227CA | m130 Yunalesca defeated | none |
| 11 | ACH_THE_DESTINATION_OF_HATRED | The Destination of Hatred | | 0x8227CA | m131 Seymour Omnis defeated | none |
| 12 | ACH_LIGHTNING_DANCER | Lightning Dancer | 12 | kami0400 a10 e2 0x1EE4, 200thunder_kami0400 0x1F62 | Thunder Plains dodge reward path | script |
| 13 | ACH_FEEL_THE_PAIN | Fell the Pain | | `FFX_NameInput_CommitName` 0x8C4F55 | char 13 named (Anima) | none |
| 14 | ACH_IT_ALL_ABOUT_THE_MONEY | It's All About the Money | | 0x8C4F6C | char 14 named (Yojimbo) | none |
| 15 | ACH_DELTA_ATTACK | Delta Attack! | | 0x8C4F37 | char 17 named (the third Magus sister) | none |
| 16 | ACH_THEATER_ENTHUSIAST | Theater Enthusiast | | `FFX_MesWin_DrawInsertedValue` 0x8B59C2 | event 158, save +0x34B == 50 and +0x34A == 71, once a process | save +0x34A, +0x34B |
| 17 | ACH_CHOCOBO_RIDER | Chocobo Rider | 17 | nagi0000 a72 e12, 8 sites from 0x20E6F | race time var98 <= 0 | script |
| 18 | ACH_POWER_STRIKE | Power Strike | | `maybe_FFX_BtlMenu_DrawDamagePopups` 0x8932BB | one effect >= 9999 | `g_ffxBtlDealt9999Flag` |
| 19 | ACH_UNDER_THE_TABLE | Under the Table | | `FFX_Btl_StartCommandExec` 0x7AE4A2 | Bribe (0x302A) gil total >= 100,000 | save +0x3DB4 |
| 20 | ACH_MESSENGER_FROM_THE_PAST | Messenger from the Past | | `maybe_FFX_Scene_Init` 0x88E0C6 | save +0xC7F seen at 8, later at 9 | save +0xC7F, latch |
| 21 | ACH_MEGA_STRIKE | Mega Strike | | 0x8932D8 | one effect >= 99999 | `g_ffxBtlDealt99999Flag` |
| 22 | ACH_CHOCOBO_MASTER | Chocobo Master | 23 | lmyt0000 a21 e40 0x38FD, a37 e6 0x5B6B | Remiem Temple race paths, the second when var13 == 5 | script |
| 23 | ACH_SPHERE_MASTER | Sphere Master | | `FFX_SphereGrid_CheckGridTrophies` 0xA47351 | one character holds every node of the grid | sphere grid nodes |
| 24 | ACH_BLITZBALL_MASTER | Blitzball Master | 25 | bltz0201 a6 e1 0x26B43, 0x28D3B, 0x3C6C0, 0x3E8B8, Esc grant 0x6FC998 | after `core:508(4, 119)` gives Wakka ability 0x3077 | script |
| 25 | ACH_LEARNING | Learning! | | `FFX_Btl_ApplyPendingEffects` 0x78F250, Esc grant 0x6FC991 | Kimahri learns by Lancet and bits 8..19 of +0x3D6C are all set | save +0x3D6C |
| 26 | ACH_SUMMON_MASTER | Summon Master | | 0x8C4F3E | char 17 named, with 15 | none |
| 27 | ACH_WEAPON_MASTER | Weapon Master | | `FFX_MainStep` 0x820F52 | save +0xC39 seen at 1..126, later at 127 | save +0xC39, latch |
| 28 | ACH_MASTER_LINGUIST | Master Linguist | | `FFX_SaveData_LearnAlBhedPrimer` 0x906B77, 0x906B96 | all 26 primers, or the set-all value 255 | save +0x3D10 |
| 29 | ACH_PERFECT_SPHERE_MASTER | Perfect Sphere Master | | 0xA473B5 | every node holds all 7 characters (127) | sphere grid nodes |
| 30 | ACH_PERSEVERANCE | Perseverance | | 0x8227CA | m344 Penance defeated | none |
| 31 | ACH_OVERCOMING_THE_NEMESIS | Overcoming the Nemesis | | 0x8227CA | m276 Nemesis defeated | none |
| 32 | ACH_THE_ETERNAL_CALM | The Eternal Calm | | 0x8227CA | m176 Yu Yevon defeated | none |
| 33 | ACH_A_JOURNEY_CATALYST | A Journey's Catalyst | | `FFX_Ach_CountScene6Confirms` 0x8B56B3 | 58 message windows shown in event 398 (sc/scene6) | `g_ffxAchScene6ConfirmCount` |

The script column gives the id the script pushes. The name in "where" is the base package. The
`cn_`, `psv_`, `psvcn_` and `dbg_` variants of the same package carry the same calls at the same
offsets (59 calls in 12 files in all, matching the census in `ATEL_CORE_LIBRARY.md`). [D]

## 4. Gates and idempotence [V]

- `FFX_Trophy_Unlock` tests `maybe_FFX_SaveMgr_IsGameStarted 0x6480E0`, which is just `SaveMgr+0`.
  `FFX_SaveMgr__ctor 0x6F1980` sets it to 1 (`mov dword ptr [edi], 1` at 0x6F1995). `sub_647E40`, now
  `maybe_FFX_SaveMgr_MarkGameStarted`, sets 1 again from core:573 BeginNewGame and from
  `FFX_SaveUi_Step` 0x82019A. The only clearing path is the thunk at 0x647E30 into a folded
  `mov dword ptr [ecx], 0` body, and it has no caller. So in retail the gate is always open, even on the
  title screen. A scan of 0x6F1980..0x6F2800 found no other write to field 0.
- `FFX_Ach_UnlockIfSteamReady` tests `g_ffxSteamApiReady`. With Steam down nothing happens and nothing
  is queued. An unlock missed this way is lost unless its trigger fires again later.
- `StatsAndAchievements__unlock` tests +0x4D (always 1), the row bound and the row's achieved byte. The
  byte is set by the unlock itself and by the received callback, so each row reaches Steam at most once
  a process. Steam's SetAchievement is idempotent anyway, and the exe calls StoreStats after each one.
- Not gated on: debug mode, `FFX_IsDevHarness`, the HD boosters, the language, a save being loaded,
  the scenario. The dev paths unlock like any other: `FFX_AtelSys_Save_040_DevGiveKeyItems` reaches
  rows 1 and 28 through `sub_88E790`.
- The Esc menu "grant" popup (`FFX_EscMenu_OnButtonClick 0x68F610`, button 13 ->
  `FFX_EscMenu_ShowGrantPopup 0x6FC7B0` -> `FFX_EscMenu_OnGrantPopupResult 0x6FC970`) offers three
  choices. 0 runs `FFX_Debug_FullItem`, 2 runs `FFX_Debug_FullGil`, and 1 unlocks rows 25 and 24 and then
  runs `FFX_SaveData_GrantAllAbilities`, which ORs every character's `ply_data` ability masks in and sets the
  first 131 bits of save +0x3D6C. The grant never unlocks any other row. It is refused outside a game
  ("Function not Available now!"). The popup text is ids 713..715, not read here.
- `StatsAndAchievements__resetAllStats 0x6F1420` is reachable only from the orphan debug page row 12. It
  resets Steam but leaves the local achieved bytes set, so nothing unlocks again until the next boot.
- Completion (row 0) is only tested after a successful new unlock. A user who already has 1..33 on
  Steam but not row 0 never gets row 0, because every later unlock call returns early on its achieved
  byte. A user offline at boot has all local bytes at 0 and gets SetAchievement calls for rows already
  held, which Steam ignores, and row 0 only if all 33 others unlock in that one process.

## 5. Every trigger site

All native sites run on the main thread T0 (`THREADS_AND_CLOCKS.md`). "Step" names where in the
`FFX_MainStep` order the call happens.

### 5.1 Rows 3, 5, 6, 27, `FFX_MainStep` [V]

Inline after the menu poll and the display list swap, once per step, before `FFX_MainStep_PublishStepScale`:

```
0x820EDC  eax = FFX_SaveData_GetScenarioWord()        // save +0xBEC
0x820EE6  if (eax != g_ffxScenarioWordLast 0xC494A0) {
             g_ffxScenarioWordLast = eax
             164 (0xA4) -> 3, 1085 (0x43D) -> 5, 2360 (0x938) -> 6   // call 0x820F12
          }
0x820F1A  cl = save +0xC39
          if ((u8)(cl - 1) <= 0x7D && g_ffxWeaponMasterAchState 0xC494A4 == -1) state = 0
          else if (cl == 0x7F && state == 0) { state = 1, unlock 27 }    // call 0x820F52
```

Both latches are initialized .data, -1 at boot (`is_loaded` is true at 0xC494A0 and 0xC494A4, so the
0xFF bytes are real). Consequences: rows 3, 5 and 6 fire on the step the scenario word becomes exactly
that value, including on the first step after loading a save that sits on it, and never for a save past
it. Row 27 needs the byte seen at 1..126 and then at 127 within one process. A save loaded already at
127 never fires, and neither does a jump from 0 straight to 127. +0xC39 is the seven celestial
weapons found bits [V, 2026-10-28, `MINIGAMES_PROGRESSION.md` 4.2]: each weapon's pickup site does
`core:347(1, treasure)` then sets its bit, 0x01 Tidus, 0x02 Yuna, 0x04 Wakka, 0x08 Auron, 0x10
Kimahri, 0x20 Lulu, 0x40 Rikku, and the debug package `test23` agrees.

### 5.2 Row 20, `maybe_FFX_Scene_Init 0x88E070` [V]

At every scene init, before the event package load: save +0xC7F == 8 clears `g_ffxAchJechtSphereLatch
0xC58E8A`, == 9 with the latch clear sets it and unlocks 20 (0x88E0C6). The latch starts at 1 (real
.data). So the byte must be seen at 8 by one scene init and at 9 by a later one in the same process. The
function-less block at 0x88ECC0 is an identical copy with no caller. +0xC7F counts the Jecht Spheres
after the first [V, 2026-10-28, `MINIGAMES_PROGRESSION.md` 5]: nine packages increment it once each,
and the first sphere is key item 0xA020 which does not touch it, so 9 means all ten.

### 5.3 Rows 1 and 28, `FFX_SaveData_LearnAlBhedPrimer 0x906B30` [V]

`a1` 0..25 sets the bit in save +0x3D10 and unlocks 1 every time, then 28 when the mask is 0x3FFFFFF.
`a1` 255 sets all and unlocks 28. Callers: `sub_88E790` (key item ids 0xA004..0xA01D) from core:269,
core:347, core:351, core:423 and the dev give, and `FFX_AtelSys_Save_039_resi` (clear, dev only).

### 5.4 Row 4, `FFX_Btl_ApplySteal 0x78B760` [V]

From `FFX_Btl_ApplyActionToTargets 0x7894B9` for an ability with flag 0x200. On a successful steal, when
the stealer's `actor+0xDED` (chr id) is 6, `++g_ffxRikkuStealCount` (save +0x3DB0) and unlock 4 when it
is >= 200 (`cmp eax, 0C8h / jl` at 0x78B8E9, so every later Rikku steal calls the unlock again and is
stopped by the achieved byte). Other characters' steals do not count. Step: battle, inside the `FFX_Btl_MainStep` sub step.

### 5.5 Row 19, `FFX_Btl_StartCommandExec 0x7AE370` [V]

When the resolved command id `dword_112C8D8` is 0x302A (Bribe, `CHEAT_SAVEDATA.md`), add
`g_ffxBtlPendingGilCost` to `g_ffxGilSpentCmd302A` (save +0x3DB4) and unlock 19 at >= 100,000. The add
happens when the command starts, whatever the bribe's outcome.

### 5.6 Row 25, `FFX_Btl_ApplyPendingEffects 0x78F060` [V]

In the "learn the target's ability" branch, for learner index 3 (Kimahri, so Lancet): when the
monster's ability id is nonzero and not yet learned, `FFX_SaveData_SetCharAbilityFlag(3, id, 1)`, then
unlock 25 when `(dword(+0x3D6C) & 0xFF00) == 0xFF00` and `(byte(+0x3D6E) & 0xF) == 0xF`, which is bits
8..19. That those 12 bits are the 12 Ronso Rages is [I].

### 5.7 Rows 18 and 21 [V]

`FFX_Btl_StageEffectsForTarget 0x789740` sets `g_ffxBtlDealt9999Flag 0x1133390` when one computed effect
is >= 9999 (0x789824) and `g_ffxBtlDealt99999Flag 0x1133394` at >= 99999 (0x789836), only when the C
string at its second argument +0x50 is empty. What that argument is was not settled. A 99999 hit sets
both. `maybe_FFX_BtlMenu_DrawDamagePopups 0x893290` unlocks 18 and 21 and clears the flags. It runs from
`FFX_BtlMenu_DrawRoot`, from `FFX_MesWin_DrawAllWindows`, which `FFX_MainStep` 0x82153C calls only when
`g_ffxMesWinDrawEnabled` and not on a catch-up step. The flags are latched, so a skipped draw only
delays the unlock.

### 5.8 Rows 10, 11, 30, 31, 32, `FFX_Ach_CheckBossKill 0x822760` [V]

From `sub_78DC30`, from `FFX_Btl_OnUnitDefeated 0x78C98C` and `sub_791760`, after `actor[3532] == 1` and
`sub_78C4C0` (not pending, not flagged by +0xF5E bits 1 and 2). The argument is `sub_82A080(actor+0xE)`,
the three digits of the unit's chr ROM path, so the monster model number. A stack table of five pairs:
276 -> 31, 344 -> 30, 130 -> 10, 176 -> 32, 131 -> 11. Names from the monster name tables through
`mon_ai.py list`: m130 Yunalesca, m131 Seymour Omnis, m176 Yu Yevon, m276 Nemesis, m344 Penance. [D]

### 5.9 Rows 13, 14, 15, 26, the naming screen [V]

`FFX_NameInput_CommitName 0x8C4E60`, state 5 of the naming screen `sub_8C3580`, writes the typed name
into the character's name record. Then by the character being named (`dword_186A258`): 17 unlocks 15
and 26, 13 unlocks 13, 14 unlocks 14. Character ids 13 Anima, 14 Yojimbo, 17 the last Magus sister are
[I] from the row names. It also runs from `sub_8C5370` (the char name reload and re-encode paths), which first calls
`sub_8C5440(11)` and `sub_8C5320(11)`. If, as case 5 of the input handler suggests, that selects
character 11, the reload path unlocks nothing [I]. `FFX_Module_SwitchGameMode 0x8AA780` has the same three unlocks for mode
0x80000 when the language is 11, which no PC code ever sets (`ASSET_PATHS.md` 2.1), so that copy is dead.

2026-10-28, from the menu module pass (`MENU_SYSTEM.md` third pass). Two of the [I] above are now
[V]. The character ids: the default name table at `word_C5A0D8` maps 8..17 to name_txt 14..23,
Valefor to Mindy, so 13 is Anima, 14 Yojimbo and 17 Mindy, the last Magus sister. And the reload
path: `sub_8C5440` (`FFX_NameInput_SetChar`) is just `dword_186A258 = a1`, so `sub_8C5370` commits
character 11, Shiva's default name, and unlocks nothing. The naming screen is menu module 14, and
`sub_8C3580` is its object step `FFX_NameInput_Step`. Scripts open it with sg:29 0x4008000D (Anima)
in bjyt1200, 0x4008000E (Yojimbo) in nagi0600 and 0x4008000F..11 (the sisters) in lmyt0100, one
site each. State 10 of the step can also move from one sister to the next inside a single open
(`FFX_NameInput_SwitchToChar 0x8C33F0`). Either way trophies 15 and 26 fire when Mindy is committed.

### 5.10 Row 16, `FFX_MesWin_DrawInsertedValue 0x8B5960` [V]

When a message with an inserted value draws and the save word +0 (the current event id) is 158
(`lc/lchb1100`), save +0x34B is 50 and +0x34A is 71, and `g_ffxAchTheaterArmed 0xC5A03D` is 1 (real
.data, starts 1): clear it and unlock 16. Once a process. +0x34A and +0x34B are script save variables
(`SAVE_BLOCK_MAP.md`). What they count is [I], the sphere theater's progress by the row name. Draw
pass, so not on catch-up steps.

### 5.11 Row 33, `FFX_Ach_CountScene6Confirms 0x8B5680` [V]

`FFX_MesWin_RunConfirmHandler 0x8B6B70` sets `g_ffxMesWinConfirmLatch 0xC5A03C` to 1 once per window
entering its "text drawn, waiting" sub state. The confirm handler `FFX_MesWin_OnConfirmPressed` jumps to
`sub_8B9220`, which calls `FFX_Ach_CountScene6Confirms`: on a latch of 1, when `g_ffxLiveSceneId ==
g_ffxAchScene6Id` (constant 398 at 0xC3347C, which is `sc/scene6`), `++g_ffxAchScene6ConfirmCount
0x18663E4`, and at 58 unlock 33 and set the count to -1. The count is bss (0 at boot) and never reset,
so partial viewings add up within a process. scene6 carries exactly 58 messages (`ebp.py text scene6`),
so the 58th window is the last one. [D] Step: `FFX_MesWin_StepAll`, the sim sub step.

### 5.12 Rows 23 and 29, `FFX_SphereGrid_CheckGridTrophies 0xA47210` [V]

Called after a node activation (`sub_A48910`, after `FFX_SphereGrid_MenuToSave`) and on a character
switch (`FFX_Menu_SphereGridSwitchChar`). Loads the grid layout for the option bits 14..15 (type 0 the
template grid, 1 Standard, 2 Expert, corrected 2026-10-28 from "Standard, Expert and the third"), then for each of the 7 character bits: when every node that is not 0xFF in the
layout has that bit in the save node byte, unlock 23. Then when every such node byte is 0x7F, unlock 29.
An empty layout unlocks both. Step: the menu, so only while the sphere grid is open.

### 5.13 Script rows, `core:603` [V] [D]

`FFX_AtelSys_Core_603_UnlockTrophy_resi 0x859270` pops one int. 22 returns without an unlock (`cmp esi, 16h` at
0x859284), 23 unlocks 22 (0x859297), 25 unlocks 24 (`cmp esi, 19h` at 0x8592A4), anything else unlocks
itself (0x8592AF). Scripts run in `FFX_Atel_StepOnce`, the sim sub step. The sites, from the census:

| package | actor / entry / offset | pushes | context before the call |
|---|---|---|---|
| bltz0201 | 6 / 1 / 0x11C90 | 22 + var49 * 1000 | `var48.ix59.ix13 > 98`. 22 is dropped and anything else is > 33, so this never unlocks on Steam |
| bltz0201 | 6 / 1 / 0x26B43, 0x28D3B, 0x3C6C0, 0x3E8B8 | 25 -> row 24 | right after `core:508(4, 119)` |
| bltz0201 | 6 / 1 / 0x3A52E | 7 | `var170 == 1` |
| bltz0201 | 6 / 1 / 0x41086 | 2 | `var34 > 0` |
| lchb0601 | 0 / 16 / 0x71C | 2 | after a voice line and `core:107` |
| kami0400 | 10 / 2 / 0x1EE4 | 12 | reward sequence, after ORing 8 into an indexed var1 |
| 200thunder_kami0400 | 10 / 2 / 0x1F62 | 12 | the same |
| lmyt0000 | 21 / 40 / 0x38FD | 23 -> row 22 | end of a race sequence |
| lmyt0000 | 37 / 6 / 0x5B6B | 23 -> row 22 | after `core:347(1, 176)`, when `var13 == 5` |
| nagi0000 | 72 / 12 / 0x20E6F, 0x21F46, 0x2301D, 0x240F4 | 17 | time `var98 <= var99 && var98 <= 0` |
| nagi0000 | 72 / 12 / 0x2145B, 0x22532, 0x23609, 0x246E0 | 17 | `var98 <= 0` |
| nagi0000 | 72 / 12 / 0x2144B, 0x22522, 0x235F9, 0x246D0 | 9 | right after `core:347(0, 340)` |
| swin0000 | 32 / 8 / 0x6725 | 8 | after `var29 > 10`, sets bit 8 of var27 |
| swin0200 | 24 / 6 / 0x139B6 | 8 | the same shape |

`nagi0000` var98 is built as `x * 1000 + var114 * 10 + var115`, the race time, so 17 is a zero time.
The places (Luca, Thunder Plains, Remiem, Calm Lands, Jecht Shot practice) come from `CHEAT_MODELS.md`
and `CHEAT_WARP.md`. Which game condition each script branch tests is [I] beyond what is written here.

## 6. What a recreation has to match (3.2.4)

1. **One unlock function, keyed by Steam row.** Keep `FFX_Trophy_Unlock(row)` with rows 0..33 and the
   API names of section 3, spelling included (`ACH_THE_RIGHT_TING`). Send SetAchievement then
   StoreStats. Bound the row at 33. Keep a per row "already sent" flag seeded from RequestCurrentStats.
2. **Remap the script ids.** `core:603` must drop 22, map 23 to 22 and 25 to 24. Native sites must not.
3. **Row 0 is derived.** After every new unlock, unlock 0 when 1..33 are all held. Reproduce the early
   return, or knowingly fix it and say so.
4. **Edge triggers keep process state.** `g_ffxScenarioWordLast` and `g_ffxWeaponMasterAchState` start
   at -1, `g_ffxAchJechtSphereLatch` and `g_ffxAchTheaterArmed` start at 1, `g_ffxAchScene6ConfirmCount`
   at 0, none of them in the save. A recreation that tests "the condition holds now" unlocks on load
   where the original does not (rows 20, 27), and one that only tests transitions misses the original's
   load-time unlock for rows 3, 5, 6.
5. **Counters that live in the save:** +0x3D10 primers (1, 28), +0x3DB0 Rikku steals (4), +0x3DB4
   bribe gil (19), +0x3D6C ability bits (25), +0xBEC scenario (3, 5, 6), +0xC39 (27), +0xC7F (20),
   +0x34A / +0x34B (16), the sphere grid node array (23, 29). No Steam stats exist to mirror.
6. **Where in the step.** Rows 3, 5, 6, 27 at the point in `FFX_MainStep` above, row 20 in scene init,
   battle rows in the battle step, script rows in the ATEL step, row 33 in the message window step, rows
   16, 18, 21 in the draw pass (skipped on catch-up steps), rows 23 and 29 in the sphere grid menu,
   13, 14, 15, 26 in the naming menu, 24 and 25 also from the Esc menu grant.
7. **Gates.** Only "Steam is up". No debug, booster or difficulty gate. With co-op lockstep every peer
   reaches the same triggers. Firing achievements is a per machine side effect and must never feed back
   into simulation state, which it does not in the original either.

## 7. Open

- What `FFX_Btl_StageEffectsForTarget`'s second argument +0x50 string is, the one that must be empty for
  rows 18 and 21.
- The meaning of save +0x34A / +0x34B for row 16. (+0xC39 and +0xC7F are settled, see 5.1 and 5.2.)
- The text of the Esc grant popup (ids 713..715).
- Whether the console list really numbers these as the scripts do, and what console trophy 22 was.
  Nothing in the binary names the console list.
