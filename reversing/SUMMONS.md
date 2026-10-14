# Summons

PROGRESS item 1.11.11 of Project Zanarkand. All addresses are VAs as IDA shows them, RVA = VA -
0x400000. Every claim is marked:

- **[V]** verified, read off Hex-Rays and, where it mattered, the disassembly
- **[D]** verified against the shipped data files in the install (kernel tables, system_01.bin,
  the magic DLLs)
- **[I]** inferred, with the reason given

The IDB was read only. Nothing was renamed or commented. Names for the `sub_` functions are the
proposals in section 12, and the doc uses the proposal in brackets the first time.

Read with: `BATTLE_START_AND_END.md` (roster, LoadUnitParams, the commit), `BATTLE_CTB_AND_STATUS.md`
(the turn queue, the counters, the stream map), `BATTLE_COMMAND.md` (the 72 byte command record and
the execute path), `MONSTER_AI.md` (ATEL in battle, the event map, `FFX_Btl_RunAiAndCommit`),
`MAGIC_DLL.md` (the effect DLLs), `CHEAT_SAVEDATA.md` (the character records). The reader is
`tools/aeons.py` in the projectzanarkand repo, section 11.

---

## 0. The short version

- **Aeons are ordinary battle units 8..17**, built at roster time like the party, sitting off the
  field until summoned: Valefor 8, Ifrit 9, Ixion 10, Shiva 11, Bahamut 12, Anima 13, Yojimbo 14,
  Cindy 15, Sandy 16, Mindy 17. [V]
- **Summon is a party swap.** `FFX_Btl_StartCommandExec 0x7AE370` sends Summon (0x3117), Grand Summon
  (0x3118) and Magus Sisters (0x30FF) to `FFX_Btl_BeginSummon` [FFX_Btl_BeginSummon]. It pushes the party
  arrangement, swaps every party member out (Yuna into the bench slot, the rest to no slot) and the
  aeon into Yuna's slot, then queues a fake 0x3100 action that plays the entrance effect. [V]
- **The aeon acts right after its entrance.** Its real turn is inserted with variant 0, which zeroes
  its CTB counter. No RNG draw anywhere on the way in. [V]
- **Loading is disk dependent.** The aeon's CHR body, motion set and weapon model go through a 32
  entry load ring, and the entrance effect is a magic DLL loaded asynchronously. Actions do not
  finish while a load is pending. [V]
- **The only skip is the config option** "Aeons: Default / Short", bit 0x800 of the save option
  flags. Short plays the full entrance the first time each aeon is summoned and a short one after.
  There is no button skip. [V] for the option, [I] for no button
- **Aeon stats are derived from Yuna's**, through the aeon's ply_rom growth row, then floored by
  `sum_assure.bin` by battle count / 30 (tiers 1..20), then the aeon's own record bonus is added.
  The tier 1 row of every aeon equals the formula at Yuna's starting stats exactly. [V] [D]
- **Dismiss and KO both end in `FFX_Btl_RecallParty 0x7AEF20`**, which restores the pushed
  arrangement and swaps the party back. A KO'd aeon gets a recovery countdown that runs out over
  later battles. [V]
- **Yojimbo and the Magus Sisters run ATEL scripts** from `battle/btl/system_01/system_01.bin`, slots
  37..40. Yojimbo draws stream 17 every turn to decide whether he acts on his own. [V] [D]
- **Co-op**: the aeon is owned by whoever owns the summoner (actor `+0x6D1`). The summon state is
  one slot (one aeon out, one summoner), and a summon removes every other party member, other
  players' characters included. [V] for the state, [I] for the ownership rule

---

## 1. Units and slots

| unit | aeon | chr id | ply_rom +16 (`+0x590`) | recovery battles (ply_rom +43) | AI slot |
|---|---|---|---|---|---|
| 8 | Valefor | 0x3001 | 4 | 8 | - |
| 9 | Ifrit | 0x3002 | 4 | 12 | - |
| 10 | Ixion | 0x3003 | 4 | 20 | - |
| 11 | Shiva | 0x3004 | 4 | 20 | - |
| 12 | Bahamut | 0x3006 | 4 | 24 | - |
| 13 | Anima | 0x3007 | 4 | 24 | - |
| 14 | Yojimbo | 0x3008 | 4 | 24 | 37 |
| 15 | Cindy | 0x3009 | 0xC | 30 | 38 |
| 16 | Sandy | 0x300A | 0xC | 30 | 39 |
| 17 | Mindy | 0x300B | 0xC | 30 | 40 |

- Chr ids from `g_ffxCharIndexToChrId 0xC423A0`. [V]
- Units 0..6 are the party, 7 is Seymour's guest slot, 20..27 are the monsters. The aeons have their
  own fixed slots, so a summon never takes a monster slot and never shifts a party unit. [V]
- `FFX_Btl_SetupUnitRoster 0x79C110` gives every aeon slot `+0x4FD = 4`, `+0x4FE = k-8`, `+0x1E = 1`,
  `+0x3F9 = 1`, and unit 16 also `+0x3F8 = 1`. They are built, but off the field (`+0xDC8 = 0`). [V]
- `+0x590` is the "summoned unit" flag source. Every aeon has the bit set, so the CTB stream aliasing
  question in `BATTLE_CTB_AND_STATUS.md` is settled: units 8..17 always take the summon branch. [V] [D]

The party order `g_ffxBattlePartyOrder 0x112C895` and the aeon order `g_ffxBattleAeonOrder 0x112C8A3`
are the arrangement. A summon writes the aeon index into a party order slot and the summoner into a
bench slot. [V]

---

## 2. The summon, step by step

### 2.1 The command

`FFX_Btl_StartCommandExec 0x7AE370` dispatches by ability id. [V]

| id | name | goes to |
|---|---|---|
| 0x3117 | Summon | `FFX_Btl_BeginSummon(user, aeon)`, aeon = record word `+10 + 16*entry`, then `sub_793260` |
| 0x3118 | Grand Summon | same as Summon, the overdrive latch is set first (section 6) |
| 0x30FF | Magus Sisters | `FFX_Btl_BeginSummon(user, 15)` |
| 0x3056 / 0x3057 | Dismiss | `FFX_Btl_QueueAeonTransition(FFX_Btl_AeonLeadUnit(user), user, 1)` |
| 0x3129 | Combine powers! | `FFX_Btl_QueueDeltaAttack(15, user, 0x30EA)`, Delta Attack |
| 0x3002 | Switch | Swap mode 0 and battle event 11 |
| 0x3018 | Flee | `FFX_Btl_PartyFlee` [FFX_Btl_PartyFlee] |

Every executed command also calls `FFX_SaveData_SetCharAbility(user, id, 1)` on the "has used" bitmap
at `saveData+0x6034` (`g_ffxCharAbilityFlags 0x1132AC4`, 18 records of 44 bytes). That bitmap is what
the Short option tests (section 3). [V]

`FFX_Btl_AeonLeadUnit` [FFX_Btl_AeonLeadUnit] maps Sandy and Mindy to Cindy and leaves every other unit alone,
so a dismiss or a KO of the Magus Sisters always goes through Cindy. [V]

### 2.2 FFX_Btl_BeginSummon [FFX_Btl_BeginSummon] 0x7ADF90

1. `0x112C9E7 = 1`, the arrangement pushed flag [g_ffxBtlSummonPushed]. [V] (the decompiler shows
   this as `HIBYTE(g_ffxBtlPoisonPendingActor) = 1`, the disassembly is `mov byte ptr g_ffxBtlSummonPushed, 1`)
2. Copies `+0xDC8` to `+0xDC9` (on field -> was on field) for units 0..17. [V]
3. Saves `g_ffxBattleAeonOrder` to `0x112C8B4` and `g_ffxBattlePartyOrder` to `0x112C89C`. [V]
4. Every other party member is swapped out with `Swap(member, 255, 0, 0)`: off the field, motion 18,
   `+0x4FD += 3`. No turn insert, so their counters freeze while off field. [V]
5. The aeon gets `+0x6D1 = summoner`. [V]
6. Magus path: `Swap(summoner, 15, 1, 0)`, then `byte_112C886[m] = m` for m = 15..17 (aeon order 0..2
   become 15, 16, 17), and Sandy and Mindy each get `+0x6D1 = summoner` and `Swap(255, m, 1, 0)`. Those
   two get no turn queue insert and no bench write. [V]
7. Normal path: `Swap(summoner, aeon, 1, 0)`. [V]
8. `sub_793CF0` repositions the enemies. [V]
9. Returns `FFX_Btl_QueueAeonTransition(aeon, summoner, 0)`, the entrance. [V]

### 2.3 FFX_Btl_SwapUnitIntoPartySlot 0x7ADAE0, summon mode (a3 > 0)

Leaving unit (the summoner): [V]

- `+0xDC8 = 0`, `+0xDF8 = 1`, `sub_78D4E0`, `sub_78E410`
- `FFX_Btl_TurnQueue_RemoveAllForActor(out, 1)`, which keeps the current entry, so Yuna's own turn
  that issued the Summon finishes normally
- `+0xDCB = 1`, `+0x1A = 1`
- `0x112C9E8 = in` [g_ffxBtlAeonOut], `0x112C9E9 = out` [g_ffxBtlSummoner]
- `+0x4FD = 12`, `+0x4FE = aeon`, motion 3
- the aeon index goes into the summoner's party order slot, the summoner into the bench slot

Arriving aeon: [V]

- `+0xDC8 = 1`, `+0xDF8 = 0`
- `FFX_Btl_GrantAeonFullGauge` [FFX_Btl_GrantAeonFullGauge], Grand Summon only (section 6)
- `+0x1E = 0`, `+0x43D = 4`
- three loads queued with `FFX_Btl_QueueChrLoad` [FFX_Btl_QueueChrLoad] (section 2.5)
- `sub_7AD470`
- `+0x716 = 1` and `FFX_Btl_TurnQueue_Insert(aeon, prio 0, real 1, variant 0, issuer summoner,
  claimed 0)`. Variant 0 forces the aeon's CTB counter `+0x65C` to 0.

### 2.4 FFX_Btl_QueueAeonTransition [FFX_Btl_QueueAeonTransition] 0x7AE1A0 (aeon, summoner, mode)

One function queues all three aeon animations as a fake action by the aeon. [V]

- `FFX_Btl_GetAeonTransitionEffect` [FFX_Btl_GetAeonTransitionEffect] reads `0xC43828 + 16*(aeon-8)`
  [g_ffxAeonTransitionEffects], four (u16 effect, u8 flag) pairs:

| aeon | +0 full entrance | +4 short entrance | +8 dismiss | +12 KO |
|---|---|---|---|---|
| Valefor | 148 | 595 | 309 | 279 |
| Ifrit | 304 | 596 | 308 | 161 |
| Ixion | 70 | 597 | 159 | 160 |
| Shiva | 162 | 598 | 166 | 167 |
| Bahamut | 168 | 599 | 323 | 324 |
| Anima | 152 | 601 | 310 | 302 |
| Yojimbo | 430 | 600 | 435 | 436 |
| each sister | 511 | 602 | 564 | 516 |

  The flag is 1 for both entrances and 0 for dismiss and KO. It lands in bit 30 of ability row
  `+28`, which `StartCommandExec` copies into `BYTE2(dword_112CA02)` and brackets with
  `sub_7A0090(101, 30)` at the start and `(127, 30)` at the end. Purpose [I], open.
- mode 0, entrance: the full effect when `g_ffxSaveDataOptionFlags 0x113079C & 0x800` is set or the
  aeon has never used 0x3100, else the short one. Post-action kind 1. Also calls
  `FFX_Btl_SwapPartyArrangement 0x7ADE10`, which temporarily puts the pre-summon arrangement back for
  the effect.
- mode 1, dismiss: effect +8, post kind 3.
- mode 2, KO departure: effect +12, post kind 2.
- It writes the effect into the **shared 0x3100 kernel ability row in place**: `+16` and `+18` =
  effect, `+21` = kind (17 entrance, 2 dismiss, 22 KO), `+26 |= 0x40`, `+28 |= 0x8000000`.
- `FFX_Btl_TurnQueue_Insert(aeon, prio, real 0, variant 1, summoner, claimed 1)`, prio 0, or 64 for
  mode 2 when the aeon has `+0x3F9` set.
- `FFX_Btl_CmdQueue_Insert(aeon, {0x3100, target 1 << summoner}, postKind, prio, 1, 0, summoner)`.
- The debug flags `g_ffxBtlDbgSumNotEff` and `g_ffxBtlDbgMagNotEff` zero the effect and set
  `+0x1E = 1`.

### 2.5 The CHR and the effect load

`FFX_Btl_QueueChrLoad` [FFX_Btl_QueueChrLoad] puts a request in a 32 entry ring at `0x112BEA0`, pending count
`byte_112C021`. `FFX_Btl_PumpChrLoadQueue 0x7AD190` runs it. Three requests per aeon: [V]

| kind | what | path |
|---|---|---|
| 3 | weapon model | `FFX_Btl_LoadWeaponModel` |
| 0 | body | `FFX_Ch_RomRead` -> `FFX_Ch_DataReadSync`, skipped when the actor's CHR at `+0` already exists |
| 2 | motion set | `FFX_Ch_MotionSetReadStart` / `ReadSync`, completion code 8 |

On completion `FFX_Btl_OnUnitChrReloaded(aeon, 8)` runs `maybe_FFX_Btl_SetupUnitChr`, fires AI kind 5
(mode 2) and positions the unit. [V]

`FFX_Btl_GetPendingChrLoads` [FFX_Btl_GetPendingChrLoads] returns the pending count. `FFX_Btl_FinishActions 0x7911E0`
returns early while it is non zero, and so do `FFX_Btl_StepSubPhase`, `sub_792170`, `sub_792E80` and
`sub_792F20`. So the battle waits on the disk. [V]

The entrance itself is a magic effect: `FFX_Btl_BeginMagicEffect 0x787F20` ->
`MagicFile__setMagicId 0x9DA7A0` / `MagicFile__load`, file `magic_%04d.dll`. With
`maybe_FFX_IsAsyncPrxLoad` the effect sits in state 16 until the DLL is in, so that is disk dependent
too. `FFX_Btl_PickAbilityEffectId` [FFX_Btl_PickAbilityEffectId] picks row `+16` or `+18` (full or short) by the same
option bit for every ability, which is how the aeons' own attacks also get short versions (row `+25`
in 8..17). [V]

The camera is whatever the effect DLL drives. There is no separate cutscene or FMV path for a summon.
[I] (nothing on the summon path calls the event or movie code, and the entrance is an ordinary 0x3100
action)

### 2.6 Post-action kinds

`sub_788480` effect state 1: kind 1 -> `sub_791630(1)` gate, then `FFX_Btl_SwapPartyArrangement` swaps
the aeon arrangement in live. Kinds 2 and 3 set `BYTE1(dword_112CA13) = 1`. Kind 8 ->
`sub_7B1AA0`, Delta Attack. [V]

`FFX_Btl_FinishActions 0x7911E0` switches on the actor's `+0xDEA` (copied from command record `+6`):
[V]

| +0xDEA | what |
|---|---|
| 1 | `FFX_Btl_PlaceArrivedAeon(E9)` [FFX_Btl_PlaceArrivedAeon]: places the aeons, summoner `+0x4FD = 13`, then `FFX_BtlEv_NotifyUnitEvent(E8, 12)` |
| 2 | `FFX_Btl_RecallParty(0)`, KO departure |
| 3 | `FFX_Btl_RecallParty(1)`, dismiss |
| 4 | escape |
| 5 | flee |
| 6 | switch |
| 7 | game over sequence |
| 8 | (effect state) Delta Attack, Magus "Combine powers!" |

This answers the open question in `BATTLE_CTB_AND_STATUS.md`: post kind 8 is Combine powers. [V]

---

## 3. Skipping and the step count

- No summon effect id is in `FFX_Btl_IsPadDrivenMagicPlaying 0x6F6FF0`'s list, so no button cuts an
  entrance short. [V] for the list, [I] for "no button anywhere" (no other input read on the path was
  found)
- The control is the config row "Aeons: Default / Short" (`config_txt` rows 22..24), getter
  `FFX_Menu_ConfigRowGetFlag_0x800 0x8CAED0`, value = `(flags & 0x800) == 0`. A set bit is Default =
  always full. Clear is Short = full on the first summon of each aeon (0x3100 never used by that aeon),
  short after. [V] [D]
- The option lives in the save block (`saveData+0x3D0C`), so it rides with a transplant. [V]
- The step count changes with the option, with the per-aeon "has used 0x3100" bit, and with disk
  speed (CHR ring and DLL load). The exact counts per effect need a runtime probe. [I]
- Summon DLLs that call `FFX_Btl_Rand` (slot 324, the gameplay independent LCG at `0xC42200`):
  Ixion full 70 (5 sites) and short 597 (5), Shiva full 162 (1), Anima full 152 (4) and short 601 (1).
  The rest have none. These are presentation draws, but `FFX_Btl_Rand` is also read by
  `sub_7ABF40` and `FFX_BtlOd_StepSharedTimer`, so a full vs short difference moves that LCG. [D] from
  `tools/magicdll.py calls`, [V] for the consumers

---

## 4. The aeon stat path

### 4.1 Where

`FFX_SaveData_RecomputeCharDerived 0x7860F0`, index >= 8. It runs at the battle end commit for 8..17
and in the menu RecomputeAll (sphere grid, equip). `FFX_Btl_LoadUnitParams(0, k) 0x79B4F0` at roster
phase 7 copies the record into the actor through `FFX_Btl_LoadCharRecordIntoActor 0x79C5F0`. So a
battle uses the stats computed at the end of the previous battle or the last menu visit. [V]

The battle count `g_ffxBattleCount 0x11307A4` (`saveData+0x3D14`) is bumped at phase 10, after the
roster, so the current battle does not count toward its own aeon stats. [V]

### 4.2 The formula

Y = `FFX_SaveData__getCharBaseStats 0x785B60` for Yuna (record base plus the sphere grid bonus cache).
Clamp HP 0..9999, MP 0..999, the stats 1..255 except Eva 0..255. [V]

```
S    = str + def + mag + mdef + agi + eva + acc + HP/100 + MP/10     (luck is not in S)
R    = ply_rom row k (g_ffxKernelTablePlyRom), bytes +24..+43
HPa  = S*R[24] + HP*R[25]/100
MPa  = S*R[26]/10 + MP*R[27]/100
str  = str*R[29]/10  + S/R[28]
def  = def*R[31]/10  + S/R[30]
mag  = mag*R[33]/10  + S/R[32]
mdef = mdef*R[35]/10 + S/R[34]
agi  = agi*R[37]/10  + S/R[36]
eva  = eva*R[39]/10  + S/R[38]
acc  = acc*R[41]/10  + S/R[40]
luck = Yuna's luck
```

All divisions truncate (everything is positive). [V]

Floor: `n = min(g_ffxBattleCount / 30, 20)`. When n >= 1, `sum_assure.bin` (kind 13 index 14,
`g_ffxKernelTableSumAssure`) row `(k-8) + 10*(n-1)`, 12 bytes: u16 HP, u16 MP, then str, def, mag, mdef, acc, eva,
agi. Each value becomes `max(value, floor)`. [V] [D]

Then the aeon's own record bonus is added: `+4` HP, `+8` MP, `+0x0C..+0x13` str, def, mag, mdef, agi,
luck, eva, acc. ply_save starts these at 0. [V] [D]

Then the equipment percent pass, from the two equip entries at record `+0x2D`/`+0x2E` (12/13 for
Valefor up to 32/33 for Mindy, 26/27 skipped). Then the stores: max HP `+0x24` clamped 9999 or 99999
when record `+0x4C` bit 0x200, max MP `+0x28` clamped 999 or 9999 when bit 0x400, the stats `+0x2F..+0x36`
clamped 1..255 (Eva 0..255), current HP/MP re-clamped. [V] for the code, open for where an aeon's
equip entries and limit break bits come from

ply_rom growth rows (+24..+43): [D]

```
     24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43
 8   06 14 02 04 07 06 05 05 46 0a 1e 0a 14 05 18 05 14 14 00 08
 9   05 46 02 03 07 08 05 11 21 09 22 09 14 04 46 03 14 14 00 0c
10   06 37 02 05 07 0a 05 0a 26 09 1e 0d 14 03 2f 03 14 19 00 14
11   06 28 02 07 08 0c 07 04 1c 0a 19 0a 17 0a 2c 0a 14 14 00 14
12   07 64 03 05 07 10 06 14 fa 09 0c 0a 14 05 14 05 14 14 00 18
13   08 78 04 04 06 21 05 0a 0c 07 1e 0a 14 04 14 05 14 14 00 18
14   09 12 00 00 06 18 08 19 17 06 1e 0a 14 04 14 12 0a 1e 00 18
15   0a f0 03 12 06 17 06 1e 3c 0a 0c 0a 14 05 14 05 14 14 00 1e
16   08 c8 03 05 07 37 06 12 28 0b 0c 0a 14 05 14 04 14 1b 00 1e
17   05 96 04 14 07 10 06 0e 28 0d 0c 0a 14 07 14 06 14 18 00 1e
```

`+42` goes to actor `+0x5C9`. `+43` is the KO recovery length in battles. [V] [D]

**Check.** Yuna's ply_save start record (HP 475, MP 84, str 5, def 5, mag 20, mdef 20, agi 10, luck 17,
eva 30, acc 3) through the formula gives exactly the sum_assure tier 1 row for all ten aeons. Valefor
is 725 HP, 24 MP, 18, 23, 21, 23, acc 11, eva 19, agi 10. `tools/aeons.py selftest` checks all ten.
[D]

The ply_save aeon rows: base stats 0, HP and MP 99999 / 9999, overdrive mode 19, gauge 0, max 20. [D]

---

## 5. Turn order and RNG

### 5.1 Into the CTB

After Yuna's Summon the turn queue is: [V]

```
[ Yuna, current ] [ aeon, variant 1, claimed, entrance 0x3100 ] [ aeon, variant 0, real turn, counter 0 ]
```

So the entrance plays as soon as Yuna's turn ends, and the aeon's first real turn follows it. The aeon
is menu driven (`+0xDF3 = 0`), so that turn waits for input. [V]

- No RNG draw in `FFX_Btl_BeginSummon`, the swap, `FFX_Btl_QueueAeonTransition` or either queue insert. [V]
- The 0x3100 row has hit type 0, so `sub_78A890` returns before its stream draw. [V]
- Off field units do not tick (`FFX_Btl_CtbTick` skips `+0xDC8 == 0`), so the party's counters freeze
  for the whole summon. The returning party gets no turn insert (Swap with a1 = 255), so they pick
  up where they were. [V]
- Sandy and Mindy get no insert and keep the counters from the battle start draw. [V]
- At battle start `FFX_Btl_InitCtbCounters` draws stream 27 for slots 7..17, the summon branch for
  every aeon. [V]
- Summon is rank 0, which `CtbDelayForRank` clamps to 1 tick [I]. Grand Summon is rank 5 with
  overdrive cost 100. [D]
- At an aeon's turn start `FFX_Btl_SendMenu` fires AI kind 1, and for summon units kind 12 (mode 2).
  [V]

### 5.2 Every draw an aeon brings

| when | draw | stream |
|---|---|---|
| battle start, units 7..17 | initial counter | 27 |
| every Yojimbo turn | `FFX_Btl_IsActorAiControlled 0x792120`: `(u8)rand < motivation/4`, motivation `dword_1130834` (`saveData+0x3DA4`) | 17 |
| Yojimbo script, Btl 235 | `FFX_Rand_Stream(17)` | 17 |
| Magus scripts, Btl 243 | `FFX_Btl_RandPercentCheck 0x7A8AD0` | 18 |
| Magus scripts, Btl 251 | RandPercentCheck, then `sub_7A8F60` -> `FFX_BtlAi_FilterAndPickTarget 0x7ACD40` | 18, then 4 |
| Yojimbo script, Btl 231 | `sub_7996D0`: `sub_799910` or FilterAndPickTarget, by path | 5 or 4 |
| any aeon kill | the pay-kill reward draws | 10 (x3 and more, see `BATTLE_START_AND_END.md`) |
| the effect DLLs listed in section 3 | `FFX_Btl_Rand` | the separate LCG at `0xC42200` |

FilterAndPickTarget draws only when more than one candidate survives the filter. [V]

---

## 6. Overdrive and Grand Summon

- `FFX_Btl_LatchOverdriveCost` [FFX_Btl_LatchOverdriveCost], called from StartCommandExec, sets Yuna's `+0x6CD`
  overdrive cost from ability row `+38`, and sets `BYTE1(dword_112CA02)` when the command is 0x3118
  with a non zero cost. [V]
- On the swap, `FFX_Btl_GrantAeonFullGauge` [FFX_Btl_GrantAeonFullGauge] runs only when that latch is set: saves the
  aeon's gauge `+0x5BC` to `+0x5BE`, fills it to max `+0x5BD`, sets `+0x5BF = 1`. [V]
- `FFX_Btl_RestoreAeonGauge` [FFX_Btl_RestoreAeonGauge] puts `+0x5BE` back when `+0x5BF` is set. It runs from
  `FFX_Btl_RecallParty` and from `FFX_Btl_SpendCommandCost`, so using the free overdrive gives back
  the gauge the aeon had before. [V]
- Aeon overdrive mode 19, in `FFX_Btl_GainOverdriveOnHit` [FFX_Btl_GainOverdriveOnHit] (from
  `FFX_Btl_ApplyPendingEffects`): hit by an enemy, gain `18*dmg/maxHP + 1`. Hitting an enemy (a7 set),
  gain `(16*dmg / actor+0x6F4)/10 + 1`. `+0x6F4` is the reference damage, `max(DamageFormula 14, 20)`
  against itself with random 0, from `FFX_Btl_ComputeOverdriveRefDamage` [FFX_Btl_ComputeOverdriveRefDamage]. Multipliers in
  `FFX_Btl_AddOverdrive 0x7B1590`. [V], a5/a6 meaning open

---

## 7. Dismiss, KO and the return

### 7.1 Dismiss

0x3056 / 0x3057 -> `FFX_Btl_QueueAeonTransition(lead, user, 1)`, the dismiss effect, post kind 3, then
`FFX_Btl_RecallParty(1)`. [V]

### 7.2 Aeon KO

- `FFX_Btl_OnUnitDefeated` sets `+0x702` death kind (1 HP, 2 status) and `+0xDEE = killer`. [V]
- `FFX_Btl_EndPhaseStep 0x7917D0` case 2, when `0x112C9E8 != 0`, `BYTE2(dword_112CA0F) == 0`
  (written only by Btl 280, a script guard, open) and `FFX_Btl_TestBattleOver(0)` gives 1 or 3: [V]
  1. lead = `FFX_Btl_AeonLeadUnit(E8)`
  2. `FFX_Btl_StartAeonRecovery 0x78E0A0`: for every unit in the party order, HP = 0 and
     `+0x6D8 = +0x6D9 + 1` (the recovery countdown)
  3. `FFX_Btl_YojimboOnRecall(lead)` [FFX_Btl_YojimboOnRecall], AI kind 11 for Yojimbo
  4. `+0x702 == 1` -> `FFX_Btl_QueueAeonTransition(lead, killer, 2)`, the KO animation, post kind 2 ->
     `FFX_Btl_RecallParty(0)`
  5. otherwise `FFX_Btl_RecallParty(0)` and `sub_793E00(0)` right away
  6. sub-phase back to 1
- Yuna is off the field while an aeon is out, so she cannot be hit or KO'd then. [I] (she has
  `+0xDC8 = 0` and every target filter tests it)
- If the returning party is all down, the normal over test takes it to post kind 7, game over. [I]

### 7.3 FFX_Btl_RecallParty 0x7AEF20

1. For each aeon in the party order: `FFX_Btl_RestoreAeonGauge`, and when a1 is set and HP <= 0,
   `FFX_Btl_ResetUnitForReentry` (HP = 1, so a dismissed aeon is never left dead). [V]
2. `FFX_Btl_RemoveUnitFromField`. [V]
3. `FFX_Btl_RestorePushedPartyOrder`. [V]
4. `FFX_Btl_ReturnPartyAfterSummon` [FFX_Btl_ReturnPartyAfterSummon] swaps the party back: the summoner with mode -1, the
   others with mode 0, completion codes 0 and 6, `g_ffxTreasureStaging[u] = 1`. [V]
5. `sub_793CF0`, then the E8/E9 word = 0xFF00. [V]

Whether the aeon's CHR is freed on recall (`sub_793410` / `sub_796DC0` mode 7) is open.

### 7.4 What locks an aeon in the menu

`FFX_Btl_IsCommandLocked 0x79A5B0` -> `FFX_Btl_IsUnitUnsummonable` [FFX_Btl_IsUnitUnsummonable]: an aeon is locked
when HP <= 0, `+0xDD4 == 0`, petrified (`+0xDCD`), or the countdown `+0x6D8 != 0`. 0x30FF is locked if
any one sister is locked. [V]

`sub_79BB50` builds the command lists, and special cases the sisters (15..17) and Yojimbo (14)
through ability row `+25` (0xFF, or equal to the unit). [V]

---

## 8. Yojimbo and the Magus Sisters

Aeons are menu driven. The two exceptions take an order from the menu and let a script pick the
actual action, through `FFX_Btl_AeonSpecialCommandHook 0x7B2980`: [V]

- Yojimbo: 0x3123 Pay -> AI kind 9 (fill), 0x3057 Dismiss -> kind 10 (mode 2).
- Sisters: 0x3124..0x312B -> kinds 13..20 (fill).
- `FFX_Btl_RunAiAndCommit` uses slots 37..40 for units 14..17, context 2, set 1 (the battle field
  file) then set 0 (`system_01.bin`).

The scripts are in `battle/btl/system_01/system_01.bin` (kind 14 index 1, 180052 bytes, 72 ATEL
actors, ATEL at header +4 = 0x30, event map at header +8 = 0x27590). Of all 817 battle field files
only system_01 has slots 37..40. Context 2 set 0 record is at `0x112A984` (ATEL `0x112A98C`, event map
`0x112A994`), set 1 at `0x112A9A4` (blob `0x112A9A8`, ATEL `0x112A9AC`, event map `0x112A9B4`). [V] [D]

| slot | unit | worker | kinds |
|---|---|---|---|
| 37 | Yojimbo | 59 | 0, 9, 10, 11 |
| 38 | Cindy | 60 | 12..18 |
| 39 | Sandy | 61 | 12..15, 18, 19 |
| 40 | Mindy | 62 | 12..15, 18, 20 |

Cindy alone has 16 / 17 (Go, go! and Help each other!), Sandy 19 (Defense!), Mindy 20 (Are you all
right?). [D]

Yojimbo's other syscalls: Btl 232 the gil paid, Btl 233 `sub_7B2B30`, Btl 234 `sub_7B2B40`, Btl 238
picks Daigoro..Zanmato (0x30DE..0x30E2) by the thresholds 32 / 48 / 63 / 80. [V]

0x3129 Combine powers! -> `FFX_Btl_QueueDeltaAttack` [FFX_Btl_QueueDeltaAttack] queues Delta Attack 0x30EA on unit 15
with variant 0 (Cindy's counter is reset) and post kind 8. [V]

---

## 9. The commit at battle end

`FFX_Btl_CommitActorsToSave 0x785FC0`: [V]

- `FFX_Btl_RestorePushedPartyOrder` first, so the pre-summon party is what gets saved. (Correction:
  `BATTLE_START_AND_END.md` says a script pushes `0x112C9E7`. It is set by `FFX_Btl_BeginSummon`, the summon.)
- The aeon's HP, MP, overdrive gauge and recovery countdown persist into its record. While the
  countdown is non zero HP stays 0. It drops by 1 per battle, and at 0 the aeon comes back at full HP
  and MP.
- Recompute for 8..17 (section 4).
- The commit is the only writer of record `+0x3D` found. [I]
- AP: aeon kills bank AP for units 0..17, the results screen shows only 0..7. [V]

---

## 10. Co-op notes

- **Ownership**: the aeon's `+0x6D1` holds its summoner, and Sandy and Mindy get the same. The rule
  "the player who owns the summoner commands the aeon" falls out of that one byte, and it stays
  stable across a Magus summon. Under the host-published ownership table, resolve an aeon unit through
  `+0x6D1` before the table lookup. [I] for the rule, [V] for the byte
- **One summon at a time**: `0x112C9E8` (aeon out), `0x112C9E9` (summoner), `0x112C9E7` (pushed) and
  the two saved order arrays at `0x112C89C` / `0x112C8B4` are single slots. A second summoner while an
  aeon is out has no slot to go into. In the original the party is gone, so this never arises. [V]
- **A summon removes every other party member**, including characters other players own. Those
  players have nothing to command until the aeon leaves. This is the single player rule, and a co-op
  design has to accept it or replace it. [V] for the behaviour
- **The shared 0x3100 kernel row is patched in place** by every transition. It is engine state outside
  the save block, deterministic given the same order of transitions. [V]
- **Peers must agree** on option bit 0x800 and each aeon's "has used 0x3100" bit (both in the save
  block), or one peer plays a full entrance and the other a short one. [V]
- **Disk dependence**: the CHR ring and the async DLL load gate `FFX_Btl_FinishActions`, so the step an
  aeon first acts on differs between machines. This is presentation time, so the lockstep gate must
  not count steps through it. Same for every magic effect. [I]
- **Yojimbo's stream 17 draw** happens every one of his turns, before any input. [V]
- `FFX_Btl_Rand` draws inside the Ixion, Shiva and Anima effect DLLs differ between full and short.
  [D]

---

## 11. The reader, tools/aeons.py

`E:\Git\projectzanarkand\tools\aeons.py`, stdlib only, read only, through the co-op `vbf.py` and
`ebp.py` via `btl_rewards.Install` and `mon_ai`.

- `stats [--battles N] [--yuna ...] [--own ...]`: the ten aeons' stats from the formula and floor.
- `tables [--aeon K]`: the growth columns and the 20 sum_assure tiers of one aeon.
- `scripts`: system_01 slots 37..40, each kind's Btl syscalls, flagged with their stream.
- `selftest`: table shapes, the ply_save aeon rows, every tier 1 row against the formula, the
  system_01 slot map.

Selftest output:

```
ply_rom recovery length (+43): Valefor=8 Ifrit=12 Ixion=20 Shiva=20 Bahamut=24 Anima=24 Yojimbo=24 Cindy=30 Sandy=30 Mindy=30
tier 1 rows equal to the rule at Yuna's starting stats: 10 of 10
system_01: 72 actors, slots 37..40 workers [59, 60, 61, 62]
problems: 0
```

---

## 12. IDB names

Applied and saved 2026-10-13 after spot checks of the command routing (0x3117, 0x3118, 0x30FF), the
`g_ffxBtlSummonPushed` writer and the summoner byte at +0x6D1, except `g_ffxYojimboCompatibility`, which
stays proposed while its meaning is inferred. 0x112C9E7..E9 are now separate bytes in the IDB (the old
`HIBYTE(g_ffxBtlPoisonPendingActor)` was the dword write of 0xFF at 0x78121F, which also clears them at
battle start).

| VA | name | reason |
|---|---|---|
| 0x7ADF90 | FFX_Btl_BeginSummon | pushes the arrangement, swaps the party out and the aeon in |
| 0x7AE1A0 | FFX_Btl_QueueAeonTransition | queues entrance, dismiss or KO as a 0x3100 action |
| 0x7AF060 | FFX_Btl_GetAeonTransitionEffect | reads the per-aeon effect table |
| 0xC43828 | g_ffxAeonTransitionEffects | 10 x 16 bytes, four (u16 effect, u8 flag) |
| 0x7ADA40 | FFX_Btl_AeonLeadUnit | 16 and 17 -> 15 |
| 0x7ADF10 | FFX_Btl_ReturnPartyAfterSummon | swaps the party back in |
| 0x7ADA60 | FFX_Btl_PartyFlee | the 0x3018 flee animation loop |
| 0x7AC6A0 | FFX_Btl_PlaceArrivedAeon | post kind 1 |
| 0x7B06E0 | FFX_Btl_GrantAeonFullGauge | Grand Summon gauge fill |
| 0x7B06B0 | FFX_Btl_RestoreAeonGauge | puts the saved gauge back |
| 0x7B03E0 | FFX_Btl_LatchOverdriveCost | `+0x6CD` and the Grand Summon latch |
| 0x7AE2E0 | FFX_Btl_QueueDeltaAttack | Combine powers! |
| 0x7B2AD0 | FFX_Btl_YojimboOnRecall | AI kind 11 |
| 0x7AD3F0 | FFX_Btl_QueueChrLoad | the 32 entry load ring |
| 0x7AD180 | FFX_Btl_GetPendingChrLoads | the ring count |
| 0x79A080 | FFX_Btl_IsUnitUnsummonable | the menu lock test |
| 0x7B0D50 | FFX_Btl_GainOverdriveOnHit | the per-mode gauge gain |
| 0x78D790 | FFX_Btl_ComputeOverdriveRefDamage | writes `+0x6F4` |
| 0x79D2F0 | FFX_Btl_PickAbilityEffectId | full or short effect by option bit |
| 0x112C9E7 | g_ffxBtlSummonPushed | set by the summon, tested by the commit |
| 0x112C9E8 | g_ffxBtlAeonOut | the aeon on field, 0 none |
| 0x112C9E9 | g_ffxBtlSummoner | the unit that summoned |
| 0x112A948 | g_ffxKernelTableSumAssure | sum_assure.bin |
| 0x1130834 | g_ffxYojimboCompatibility | `saveData+0x3DA4`, [I] from its use as the motivation |
| 0x112A984 | g_ffxBtlFieldRecords | context 2 set 0 / set 1 records, 0x20 apart |

Also worth a comment on `0xC42200` (the `FFX_Btl_Rand` state): seeded only by
`FFX_Rand_SeedAllStreams` at new game, drawn by summon DLLs.

---

## 13. Open questions

- Where an aeon's equipment entries 12..33 and the Break HP / MP Limit bits (record `+0x4C`) get set.
- Exact step counts per entrance, dismiss and KO effect, full and short. Needs a runtime probe.
- Whether the aeon CHR is freed on recall (`sub_793410` / `sub_796DC0` mode 7).
- The `BYTE2(dword_112CA0F)` guard on the KO path (only Btl 280 writes it).
- What `sub_7A0090(101, 30)` / `(127, 30)` does around a flagged entrance.
- The `sub_791630(1)` gate in post kind 1.
- The a5 / a6 arguments of `FFX_Btl_GainOverdriveOnHit`.
- Whether a save sphere heals the aeons and clears the countdown.
- Yuna's Summon delay (rank 0 clamped to 1 tick) needs one runtime counter read.
