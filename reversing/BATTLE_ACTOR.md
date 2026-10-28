# Battle actor records: BTLUNIT and BTLCHR

2026-10-13. PROGRESS 1.11.6. The layouts live in `ffx_types.h` (`BTLCHR`, `BTLHIT`, `BTLHITLOG`,
`BTLUNIT`). This note is the evidence and the method. `tools/apply_battle_structs.py` puts them in the
IDB. All addresses are IDA VAs at base 0x400000.

Tags as elsewhere: [V] read in the code, [I] inferred, [D] from shipped data names.

## 1. The records and their getters

`FFX_Battle_GetActor` 0x794020 (u8 index) hands out 93 records [V]:

| index | record | address |
|---|---|---|
| 0..30 | BTLUNIT, 0xF90 (3984) | `g_ffxBattleUnits` 0x11334D4, a pointer to a heap block, + 3984 * i |
| 31..92 | bare BTLCHR, 0x390 (912) | `g_ffxBattleSideRecords` 0x113446C, also a pointer, + 912 * (i - 31) |
| 93..255 | a spare static BTLUNIT | 0x11334D8 |

Sizes are pinned by the allocator `FFX_Btl_AllocActorArrays` 0x793530 [V]: `FFX_MemAlloc(0x1E270)` = 31 * 3984 and
`FFX_MemAlloc(0xDCE0)` = 62 * 912. It sets `g_ffxBattleEnemyUnits` 0x1134468 = ally base + 79680
(= &unit[20]), allocates the 36 byte `dword_2311050`, and zeroes every record with `sub_79F230(rec, 3984)`
or `sub_79F230(rec, 912)`, the spare one included. `FFX_Btl_FreeActorArrays` 0x793500 frees them.

The other getters:

| getter | returns |
|---|---|
| `FFX_Btl_GetEnemyUnit` 0x795AA0 (i) | `g_ffxBattleEnemyUnits + 3984 * i`, unit 20 + i |
| `FFX_Btl_GetSideRecord` 0x795B10 (unit, k) | bare record `unit + 31 * (k != 0)` for unit < 31, else 0. GetActor index 31 + unit or 62 + unit |
| `FFX_Btl_GetWeaponStandInUnit` 0x795BB0 (unit, k) | k == 0 only: unit 4 -> GetActor(28), 5 -> 29, 14 -> 30, else 0 |

### 1.1 The 912 byte records are not monsters

Every doc so far called indices 31..92 "monster actors" or "light records". They are the **weapon and
armour model records**, two per unit 0..30 [V]:

- `FFX_Btl_GetSideRecord` maps (unit, side) to them, nothing indexes them by monster slot.
- `FFX_Btl_SetupUnitRoster` 0x79C110 gives them `m_typeId` 0x5000 | unit and 0x5100 | unit and
  `m_attachKind` 14 and 15.
- Their chr id is `sub_7ABDC0(unit->m_equipIds[side])`, the model id at equipment entry +12, loaded by
  `FFX_Btl_QueueWeaponChrLoads` 0x7AD470 and disposed by `sub_7AD750` / `sub_7AD820`.
- The 912 bytes are exactly the head of the 3984 byte unit record. 0x000..0x04F is the CHR binding
  block, then the three monster texts `m_name[64]` 0x050, `m_description[256]` 0x090 and
  `m_scanText[512]` 0x190 end at 0x390. A side record only ever touches 0x000..0x047, the texts only
  matter on a monster unit.
- The same functions run on a unit and on its side records: `FFX_Btl_SetChrB183Request` (b183 vote),
  `FFX_Btl_SetFlag100Vote` (flag 0x100 vote), `sub_796480` (alpha +0x40), Btl 294 (+0x3E). That is why the head
  is a separate struct and `BTLUNIT` embeds it as `m_head`.

Monsters are units 20..27 in the big array, as MONSTER_ARENA.md already said.

### 1.2 Units 28..30 are weapon stand-ins

`FFX_Btl_SetupWeaponChr` 0x7938E0 sets `owner->m_weaponUnit` (+0x13) to 28, 29 or 30 for unit 4 (Wakka), 5 (Lulu)
and 14 (Yojimbo), hands the stand-in the owner's weapon CHR with `m_chrBorrowed` (+0x12) set and
`m_primaryUnit` (+0x14) pointing back at the owner [V]. That these are the ball, the doll and Daigoro is
[I], from which three units get one and that the CHR is the weapon model.

## 2. Method

The IDB has typing traps: `FFX_BtlAi_SetProperty` has unit records typed `CHR *`, and Hex-Rays reuses
one register variable for an actor pointer and for unrelated values. So a field was only counted when the
base provably came from a getter. The walk (idalib, on a copy of the IDB, nothing saved):

1. A flow-sensitive abstract interpreter over each Hex-Rays ctree. It tracks which local variables hold
   "getter result + constant offset" through assignments, casts, pointer arithmetic and indexing, and
   merges states at if, loops, switch and goto. A register reuse kills the fact instead of carrying it.
2. Parameters: a parameter counts as a record pointer when every call site passes one. This is a
   greatest fixpoint (accept every candidate, then drop any parameter with a call site that does not pass
   a record, repeat). Functions whose address is taken as data are refused, their callers are unknown.
3. Functions that return a record (directly or through another one) become getters too, 27 of them.
   Sub-pointer getters return a record + constant, for example `FFX_Btl_CmdQueue_GetActorCmdOrLast`
   0x7B09B0 (+0x72C), `FFX_BtlOd_WakkaReels_GetStrip` 0x7B1910 (+0xD40), `sub_79AF10` 0x79AF10 (+0x540),
   `sub_78FA80` (+0x608), `sub_78FAA0` (+0x641), `sub_78FAE0` (+0x64D).
4. Every `FFX_BtlAi_GetProperty` 0x7B2DC0 and `FFX_BtlAi_SetProperty` 0x7B4B70 case was mapped to the one
   field it reads or writes, giving the "AI property N" notes.
5. Labelled fields: the debug windows `SG_DebugWin_CHRData1Proc` 0x7DD890 and `SG_DebugWin_CHRData2Proc`
   0x7DE790, and the macro writer `SG_DebugWin_WriteMotionMacro` 0x7DCF70 with its `setstat2( stat_... )` /
   `setmotiondata2( motion_... )` strings at 0xB57424..0xB579A8.

Result: 658 functions, 177 accepted parameters, 27 return getters, 4488 access rows. Fields with a known
offset and size but no understood meaning keep a placeholder name (m_b/m_w/m_i/m_f + hex) and a comment
listing the first writers and readers, so nothing that was seen is left inside an `_unk_` run.

## 3. Before and after

Before this pass neither struct was in `ffx_types.h` and the IDB has no type for either, so each record
was one unknown run of its full size.

| struct | size | before | fields after | named | placeholders | `_unk_` runs | runs over 32 bytes |
|---|---|---|---|---|---|---|---|
| BTLCHR | 0x390 | 1 run, 912 bytes | 50 | 44 | 6 | 1 (1 byte) | 0 |
| BTLHIT | 0x2C | not known | 13 | 13 | 0 | 1 (1 byte) | 0 |
| BTLHITLOG | 0x2D8 | not known | 14 | 14 | 0 | 2 (6 bytes) | 0 |
| BTLUNIT | 0xF90 | 1 run, 3984 bytes | 427 | 210 | 217 | 26 (100 bytes) | 1, 0x568..0x58F (40 bytes) |

The one remaining run over 32 bytes, `BTLUNIT` 0x568..0x58F, has no reader or writer in the walk. It is
inside the 492 byte stat block that `FFX_Btl_LoadUnitParams` 0x79B4F0 zeroes, right after the 40 byte
`m_battleName`. Probably the tail of an 80 byte name buffer: the monster copy and Btl 220 fill 40 bytes,
the character copy 20 bytes from `g_ffxCharNames`. Class 0 command ids resolve their name string to this
buffer (`FFX_Btl_GetAbilityRecord` 0x79A4B0 -> `sub_79AF10`).

## 4. Layout overview

The header file has a comment on every field. The regions:

| range | what | key evidence |
|---|---|---|
| 0x000..0x04F | CHR binding: m_chr, chr ids, unit index, type id, existence, visibility votes, CHR flag mirrors, alpha, data file | `FFX_Btl_SetupUnitRoster` 0x79C110, `FFX_Btl_SetUnitShowVote` 0x797080, `FFX_Btl_LoadMonsterData` 0x783680 |
| 0x050..0x38F | monster name, description, scan text | `FFX_Btl_LoadMonsterData` |
| 0x390..0x4AB | scripted movement: start pos, motion event bits, scale, look-at, saved positions, attach parent and bone, the move timeline and speeds, facing | `FFX_Btl_StartUnitMove` 0x7AB540 (start), `FFX_Btl_StepUnitMove` 0x7AA2A0 (step), `FFX_Btl_OnMotionEvent` |
| 0x40C..0x4DF | script stats (stat_*, motion_*) | `SG_DebugWin_WriteMotionMacro`, Btl 178 for the nine floats at 0x4BC |
| 0x4FC..0x53B | placement key (layout, group, slot), saved copies, home facings, y offset | `sub_7ABFE0` 0x7ABFE0, `FFX_Btl_SwapUnitIntoPartySlot`, `FFX_Btl_ReturnPartyAfterSummon` |
| 0x540..0x72B | the 492 byte stat block: name, HP/MP, stats, elements, statuses, CTB, ability bitfields, costs, working values | `FFX_Btl_LoadUnitParams` 0x79B4F0, `Btl_LoadCharRecordIntoActor` 0x79C5F0, BATTLE_CTB_AND_STATUS.md |
| 0x72C..0x773 | current 72 byte command record | `FFX_Btl_ExecCommand` 0x792803 |
| 0x774..0xD23 | two BTLHITLOG pending effect blocks of 728 bytes | `FFX_Btl_ApplyPendingEffects` 0x78F060 |
| 0xD24..0xD3F | overdrive minigame result, per action damage sums | `BtlOd_ReportMinigameResult` 0x7B0489, `FFX_Btl_ComputeEffectForTarget` |
| 0xD40..0xDBF | target order lists, Wakka's reel strips | `FFX_Btl_BuildTargetOrder` 0x7B7090, `sub_7B6FB0` 0x7B6FB0, `sub_7B1B50` 0x7B1B50 |
| 0xDC0..0xE2F | battle state bytes: target masks, present, KO, capture, CTB flags, queue indices, action state | BATTLE_COMMAND.md, BATTLE_CTB_AND_STATUS.md |
| 0xE30..0xEAB | status effect handles | `sub_79EF40` 0x79EF40, `FFX_Btl_StopStatusFx` 0x79F110, `sub_79ED40`, `sub_79EA90` |
| 0xEAC..0xF2E | 32 handles from the unit's data file, probably sound [I], and their load counters | `sub_7A01B0` 0x7A01B0, `sub_7A0400` 0x7A0400, `sub_7A0540` 0x7A0540, Btl 108 |
| 0xF34..0xF77 | late per unit values, overkill accumulator | `FFX_Btl_ApplyPendingEffects` 0x78F368 |
| 0xF78..0xF8F | monster archive sections and the reward record | `FFX_Btl_BindMonsterArchiveSections` 0x783B32, `Btl_RegisterMonsterMotions` 0x7840BB |

BTLHITLOG: a 24 byte header (next and count, the attacker and command slot it belongs to, reward
flags for the overdrive transfer and Ronso Rage learning, steal and gil results, the rage unit) and 16
BTLHIT entries of 44 bytes (reaction kind, floating number, new status words and counters, an apply mask
and the HP, MP and CTB amounts). Read off `FFX_Btl_ApplyPendingEffects` 0x78F060, `sub_79A920` and
`sub_79A760` [V].

## 5. Corrections to existing docs

Made in place, each pointing here:

- BATTLE_COMMAND.md: indices 31..92 are the weapon and armour side records, not "light records", and
  28..30 are not unassigned, they are the weapon stand-ins. The open question about them is marked
  answered, 18 and 19 remain open.
- BATTLE_CTB_AND_STATUS.md 1.1, FFX_GAME_NOTES.md (0x113446C row), MONSTER_ARENA.md (point 6),
  CHEAT_MODELS.md (`BattleGetActor` row): "monster" for 31..92 replaced.

Not changed, for the coordinator: PROGRESS 1.11.6 says "912-byte monster actor". The IDB's own comment
on `FFX_Battle_GetActor` says "monsters (31..92)" and should be fixed when the IDB is next written.
`FFX_Btl_GetUnitScale` 0x795720 returns the scale at +0x3A0, not a position, see the rename list.

## 6. IDB names

Applied and saved 2026-10-26 (through idalib, the MCP worker was down) after a check of the getter, the
allocator, `FFX_Btl_GetSideRecord` and the stand-in map. Also renamed: `g_ffxBattleMonsterActors` ->
`g_ffxBattleSideRecords 0x113446C` and `g_ffxBattleAllyActors` -> `g_ffxBattleUnits 0x11334D4`, and the
GetActor comment no longer calls 31..92 monsters. The table keeps the old names in its current column.

| VA | current | proposed | why |
|---|---|---|---|
| 0x793530 | FFX_Btl_AllocActorArrays | FFX_Btl_AllocActorArrays | allocates and zeroes both arrays and the spare |
| 0x793500 | FFX_Btl_FreeActorArrays | FFX_Btl_FreeActorArrays | frees them |
| 0x795B10 | FFX_Btl_GetSideRecord | FFX_Btl_GetSideRecord | (unit, side) -> weapon or armour record |
| 0x795BB0 | FFX_Btl_GetWeaponStandInUnit | FFX_Btl_GetWeaponStandInUnit | units 4, 5, 14 -> 28, 29, 30 |
| 0x795720 | FFX_Btl_GetUnitScale | FFX_Btl_GetUnitScale | returns the three floats at +0x3A0, the scale |
| 0x7AB540 | FFX_Btl_StartUnitMove | FFX_Btl_StartUnitMove | fills the move timeline 0x390..0x4A8, magic host API 701 |
| 0x7AA2A0 | FFX_Btl_StepUnitMove | FFX_Btl_StepUnitMove | steps it every frame |
| 0x797080 | FFX_Btl_SetUnitShowVote | FFX_Btl_SetUnitShowVote | four votes -> m_visible, shows or hides the CHR and the side records |
| 0x797020 | FFX_Btl_SetChrB183Request | FFX_Btl_SetChrB183Request | three requests -> FFX_Ch_SetB183 |
| 0x796F20 | FFX_Btl_SetFlag100Vote | FFX_Btl_SetFlag100Vote | four votes -> FFX_Ch_SetFlags1Bit100Inv |
| 0x7938E0 | FFX_Btl_SetupWeaponChr | FFX_Btl_SetupWeaponChr | builds the side record CHRs and the stand-ins |
| 0x7AD470 | FFX_Btl_QueueWeaponChrLoads | FFX_Btl_QueueWeaponChrLoads | queues the weapon and armour model loads |
| 0x7DCF70 | SG_DebugWin_WriteMotionMacro | SG_DebugWin_WriteMotionMacro | prints the setstat2 / setmotiondata2 macro text |
| 0x7B7090 | FFX_Btl_BuildTargetOrder | FFX_Btl_BuildTargetOrder | fills +0xD40, magic host API 218 |
| 0x79F110 | FFX_Btl_StopStatusFx | FFX_Btl_StopStatusFx | stops the 24 handles at +0xE30 |

## 7. Still open

- 217 placeholder fields in BTLUNIT and 6 in BTLCHR. Each has its writers and readers in its comment.
  Most are single flags read by one or two functions. The biggest clusters are 0x3F0..0x451 (motion,
  move and placement state bytes, separate fields and not an array, they have different readers),
  0x504..0x50F (AI properties 26, 31, 85, 86, 95..97, 343), 0x5C0..0x5C3, 0x6CA..0x6E1, 0x700..0x729,
  0xDF8..0xE2C and the dwords 0xF34..0xF58.
- 0x568..0x58F, see section 3.
- The weapon record reading at `sub_795B40` (magic host API 219) indexes `v2[1088]` on a side record,
  beyond its 912 bytes. Either a quirk or a misread of which record it gets, not checked at runtime.
- The magic DLLs reach units through the host API table and were not walked, so "No reader found"
  does not cover them.

## 8. Applying to the IDB

`tools/apply_battle_structs.py`, modelled on the CHR field script from 1.6.5. It extracts the four
structs from `ffx_types.h`, parses them into a scratch til, checks the sizes against
{BTLCHR 0x390, BTLHIT 0x2C, BTLHITLOG 0x2D8, BTLUNIT 0xF90}, and diffs them with any existing IDB type
of the same name. It refuses when a non-placeholder member name would disappear or move, or when an
existing member comment would be lost. Only then does it run `idc.parse_decls(..., PT_REPLACE)` and copy
the header comments onto the members (capped at 1000 characters). It declares `struct CHR;` forward only
when the IDB has no CHR, so it never wipes a real CHR type.

Correction 2026-10-26: the first version tested for CHR with the low level
`ida_typeinf.get_named_type(idati, n, 0)`, which returns None for a type that exists, so the dry run
below reported CHR missing while the IDB had the full 0x880 CHR from 1.6.5. A real apply would have sent
`struct CHR;` through PT_REPLACE. The test now uses `tinfo_t.get_named_type`, reports none missing, and
the applied run kept CHR at 0x880 with all 220 members. The structs are applied, see section 6. `DRY_RUN = True` is the default and it never
saves.

Dry run on a copy of the IDB, 2026-10-13 pass:

```
external types missing from the IDB, declared forward: ['CHR']
BTLCHR     size 0x390 expected 0x390 ok, 51 members, 50 comments
BTLHIT     size 0x2C expected 0x2C ok, 14 members, 13 comments
BTLHITLOG  size 0x2D8 expected 0x2D8 ok, 16 members, 14 comments
BTLUNIT    size 0xF90 expected 0xF90 ok, 453 members, 428 comments
BTLCHR     not in the IDB yet, it will be created
BTLHIT     not in the IDB yet, it will be created
BTLHITLOG  not in the IDB yet, it will be created
BTLUNIT    not in the IDB yet, it will be created
diff clean, no named member or member comment would be lost
DRY_RUN, nothing applied
```

The refusal path was checked by planting an old BTLHIT with a real member name and a comment in an
unsaved copy: it reported both and returned without applying.
