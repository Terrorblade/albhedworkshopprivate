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
| BTLUNIT, 2026-10-28 | 0xF90 | | 428 | 211 | 222 by the regex m_[bwif]+hex (217 in the first count) | 25 (60 bytes, largest 16) | 0, 0x568..0x58F is `m_battleNameSpare[40]`, section 9.1 |

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
| 0xEAC..0xF2E | 32 handles from the unit's data file, probably sound [I], and their load counters | `FFX_Btl_QueueSoundSections` 0x7A01B0, `sub_7A0400` 0x7A0400, `FFX_Btl_ServiceSoundQueue` 0x7A0540, Btl 108 |
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
- 0x568..0x58F, see section 3. Closed 2026-10-28, section 9.1.
- (Answered 2026-10-28, section 9.2: a shipped mistyped read.) The weapon record reading at `sub_795B40` (magic host API 219) indexes `v2[1088]` on a side record,
  beyond its 912 bytes. Either a quirk or a misread of which record it gets, not checked at runtime.
- The magic DLLs reach units through the host API table and were not walked, so "No reader found"
  does not cover them. (Walked 2026-10-28, section 9.4.)
- 2026-10-28: units 18 and 19 answered in 9.3. The placeholder meanings are still open, the field
  naming workers had not reported when this pass was handed back.

## 9. Second pass, 2026-10-28

Tags: [V] read in the binary at the address given, [M] measured by a scan, [I] inferred.

### 9.1 BTLUNIT 0x568..0x58F is a spare tail of the name buffer that nothing addresses

Now `m_battleNameSpare[40]`. Every channel checked:

- **Displacement scan, whole FFX.exe [M].** Every instruction in every code segment with a memory
  operand displacement in 0x568..0x58F, or an `add`/`lea`/`sub` immediate in that range: 126 hits.
  None has a BTLUNIT base. In the battle range they are `sub_7803F0` and `sub_7807C0` (a 0x5D8 byte
  "tex anim set" block from `FFX_MemAlloc`), `FFX_Came_StartTrapezoidMove`, `sub_7BDE50` and
  `sub_7C0790` (camera records). Outside it they are CHR (`FFX_Ch_UpdateRenderJob` 0x833629, CHR is
  0x880), the ATEL actor float getters at 0x86A217..0x86A707 (base `FFX_Atel_GetActor`), Bullet
  constraints, movie and sound code.
- **Sub-pointer scan [M].** For every function that calls a unit getter (`FFX_Battle_GetActor`,
  `FFX_Btl_GetEnemyUnit`, `FFX_Btl_GetSideRecord`, `FFX_Btl_GetWeaponStandInUnit`,
  `FFX_Btl_GetUnitBattleName`) plus every function in 0x770000..0x8C0000 (7250 functions): track a
  register loaded with base + K (K in 0x300..0x567, by `lea` or `add`) and report any later [reg + D]
  with K + D in the run. 5 hits, all the ATEL actor at K = 0x558 (`FFX_Atel_PushActorPosToChr`,
  `FFX_Atel_SetActorPos`). The stat block base `ebx = unit + 0x540` in `FFX_Btl_LoadUnitParams`
  (0x79B509) is never used with a displacement 0x28..0x4F.
- **The Hex-Rays provenance walk of the first pass** (4488 rows, sub-pointer getters included) has no
  row in the run and no variable index row whose base is 0x540 beyond the copy loops below.
- **Bulk writers [V].** Only the zeroing: `FFX_Btl_ZeroBytes(unit + 0x540, 0x1EC)` at 0x79B504..0x79B513
  in `FFX_Btl_LoadUnitParams`, and `FFX_Btl_ZeroBytes(rec, 3984)` in `FFX_Btl_AllocActorArrays`. No
  `push`/`mov` of 0x1EC, 0x7B, 0xF90 or 0x3E4 in the battle range copies a stat block or a unit (the
  0x1EC, 0x390 and 0xF90 pushes at 0x78170F..0x781719 in `FFX_Btl_Init` are printf arguments).
- **Name writers [V].** All four bound their copy at 40 bytes or less: the monster copy (`mov edi,
  28h` at 0x79B78D, from `m_name`), the character copy (20 bytes from `g_ffxCharNames`, `cmp ecx, 14h`
  at 0x79B55B), Btl 220 `FFX_AtelSys_Btl_220_SetBattleNameFromMsg_resi` (`cmp eax, 28h` at 0x7A7A7B)
  and Btl 145 `FFX_AtelSys_Btl_145_CopyBattleName_resi` (`mov esi, 28h` at 0x7A7820).
- **Name readers [V].** `FFX_Btl_GetUnitBattleName` (was `sub_79AF10`) returns unit + 0x540, and its
  users hand it to the text draws (`sub_905930` -> `FFX_Text_DrawStringScaled`, `sub_9016B0`,
  `FFX_MesWin_MeasureTokenRun` in `sub_89BF70` 0x89BF95, the message slot setter `sub_8B57F0` in
  `sub_892870`). Those read to the zero terminator. `FFX_Btl_LoadMonsterData` stores up to 63 bytes
  of the monster name (`cmp ecx, 3Fh` at 0x7837EC), so a monster name of 40 bytes or more is cut at
  40 and its terminator is byte 0x568, which the stat block memset left at zero. That byte is the only
  part of the run any reader can reach, and only as a terminator [V for the code, I that a shipped
  name is that long].
- **Magic DLLs [M].** See 9.4. The one raw unit pointer a DLL dereferences is written at +0x614, and
  the list pointers are read at +0xD40, +0xD60 and +0xD80. Nothing in 0x568..0x58F.

Verdict: written only by the two bulk zeroings, read at most as the terminator of a full 40 byte name.
The field is named and typed `unsigned __int8 m_battleNameSpare[40]`.

### 9.2 `FFX_Btl_GetUnitSideChr` (host API 219) reads a BTLUNIT field through a side record

`FFX_Btl_GetUnitSideChr` 0x795B40 [V]:

```
0x795B52 call FFX_Btl_GetSideRecord      ; eax = g_ffxBattleSideRecords + 912 * (unit or unit + 31)
0x795B5F mov  ebx, [eax]                 ; side record m_chr
0x795B63 jnz  return ebx                 ; has a CHR -> return it
0x795B65 cmp  [eax+440h], bl             ; bl is 0 here
0x795B6B jz   return ebx                 ; -> return 0
         loop units 0..30: [unit+43Fh] == a1 -> ebx = unit CHR, count, stop after the a2-th
```

+0x440 is `BTLUNIT.m_hasAttachedUnits` and +0x43F `m_attachParentUnit`, so the loop is written for
a BTLUNIT, but the test reads offset 0x440 off the 912 byte side record. 0x440 - 0x390 = 0xB0, so for
side records 0..60 it reads byte 0xB0 of the next side record, which is inside that record's
`m_description` text (0x090..0x18F). Side records never write past 0x047 (section 1.1) and
`FFX_Btl_AllocActorArrays` zeroes all 912 bytes, so the byte is 0 and the fallback loop never runs
from those records [V for the read, I for the value]. For the last side record (unit 30, side 1,
record 61) the read is 0xB0 bytes past the end of the 0xDCE0 byte heap block, whatever the heap holds
there. The intended test was almost certainly `FFX_Battle_GetActor(a1)->m_hasAttachedUnits` [I]. In
practice: a unit with no weapon CHR returns 0 even when it carries attached units. 149 DLLs call
this slot, so the recreation should copy the behaviour (return the side record CHR, else 0) and not
the intent.

### 9.3 Units 18 and 19 are an unused gap

Units 0..17 are the 18 character records (0..6 party, 7 Seymour, 8..17 the aeons), and 20..27 the
enemy slots. Nothing fills 18 and 19 [V]:

- `FFX_Btl_SetupUnitRoster` 0x79C110 gives units 0..30 the per record defaults, then loops the
  characters with `cmp edi, 12h` (0x79C45A) and `cmp ebx, 12h` (0x79C4AF), so only 0..17 get a type
  id, `FFX_Btl_LoadUnitParams` and side record models. `m_exists` (+0x10) has three writers, all in
  that function (0x79C39A party, 0x79C43C characters 0..17, 0x79C5A3 enemies), so 18 and 19 never
  exist in a battle.
- Every character loop in the battle code stops at 18: the CHR load phases in `FFX_Btl_StepSubPhase`
  (0x7832F3, 0x78334F, 0x783378, 0x783518), `FFX_Btl_BuildUnitListByKind` (0x7B6C9B, 0x7B6CBE,
  0x7B6D6B), `FFX_Btl_InitCtbCounters` (0x78DFBC), `FFX_Btl_CommitActorsToSave` (0x78609A),
  `FFX_Btl_PayKillRewards` (0x799155). There are 53 compares against 0x12 in 0x770000..0x7F0000, most of them character loops like these [M].
- `FFX_Btl_IsEnemySlot` 0x79AEF0 is `unit - 20 <= 7` and `FFX_Btl_AveragePresentPlacementPos` is
  called as (0, 18) and (20, 8) in `sub_793BA0` (0x793BB5).
- No caller passes 18 or 19 as a constant unit index: no `push 12h` / `push 13h` feeds a unit
  function in 0x770000..0x7F0000 [M] (the hits are battle event 18 at 0x7ADAC4 and 0x7ADC0A, RNG
  streams 18 and 19, a trophy id and debug widgets).

The few `< 20` tests (`FFX_Btl_LoadUnitParams` name loop, `sub_79FE80` 0x79FE97 which picks a
sound table for 8..19 and 28..30) treat 18 and 19 as ally side padding. The enemy block starts at 20
so that the eight enemy slots line up, and the two records in between are allocated and never used
[I for the why]. An AI script could still name 18 or 19 through `FFX_BtlAi_SetProperty` (it writes
+0xDC8 on any unit), no shipped use is known.

### 9.4 What the magic DLLs touch

Measured over all 581 FFX DLLs in memory with `magicdll.HostCallScan` and capstone (scratch script,
the install is read only) [M].

**Raw pointers.** Only six host slots hand a DLL a pointer into a unit: 925 `FFX_Battle_GetActor`
(the unit), 218 `FFX_Btl_BuildTargetOrder` and 645 `FFX_BtlOd_WakkaReels_GetStrip` (+0xD40), 407
`FFX_Btl_SortTargetOrderAcrossLine` (+0xD40 through BuildTargetOrder), 368
`FFX_Btl_BuildTargetOrderWithAttached` (+0xD60), 617 `FFX_Btl_GetHitTargetList` and 412
`FFX_Btl_BuildHitTargetList` (+0xD80). A few others leave a unit pointer in eax as a side effect
(226, 433, 528, 688, 808), no DLL uses it (0 dereferences at their 470 call sites). A forward taint
walk from every call site (eax and its register and `[ebp+x]` copies, 120 instructions):

| slot | call sites | dereferenced | offsets |
|---|---|---|---|
| 925 | 4 (magic 0515, 0611, 0667, 0668) | 4 | **write byte +0x614 = 0** |
| 218 | 399 | 21 | read +0xD40 (17 DLLs), indexed +0xD40 (4), 202 sites pass the list on |
| 368 | 3 | 2 | indexed read +0xD60 |
| 407 | 3 | 1 | indexed read +0xD40 |
| 645 | 10 | 10 | `lea` +0xD40 |
| 617, 412 | 9 | 0 | |

So the DLLs never read or write a unit field through a raw pointer, with one exception: magic 0515,
0611, 0667 and 0668 each run `if (FFX_Btl_IsAllyUnit(u)) FFX_Battle_GetActor(u)[0x614] = 0` when
their own frame counter reaches 30 (magic_0515 at 0x100085C5..0x10008605, the counter word
0x10344358 advances only while `g_ffxVfxTimeFrozen` is clear, 0x10008351). +0x614 is
`m_statusCount[12]`, the Slow counter. That is a simulation write from effect code, timed by effect
frames. These are also the four DLLs that read the catch-up flag, but in magic_0515 the catch-up test
(0x1000420D) gates only a trail mesh build, not this counter [V in the DLL bytes].

**Through host functions.** Everything else a DLL does to a unit goes through an exe function that
takes a unit index. 132 slots reach BTLUNIT fields within three call levels (joined with the first
pass's access rows). Directly in the slot body: 94 offsets read and 77 written. Within three levels:
428 read and 186 written. The heavy simulation writers are 239 `FFX_Btl_ApplyPendingHits` (275 DLLs:
HP, MP, CTB, status words and counters, KO and present bytes, the hit logs) and 446
`FFX_Yanagi_ApplyPendingHits` (36), 226 `FFX_Btl_EffectSetReady` and 433 (451 DLLs: +0x731, +0xDF2,
+0xDFB, +0xDFE, +0xE0D), 808 `MsSetReel` (10: the reel hits, +0x774, +0xD24..+0xD3C, +0xD80), 388
`FFX_Btl_PlayUnitReaction` (20) and the motion and move setters 228, 213, 530, 537, 598, 701. Side
records: slot 219 reads +0x000 and +0x440 (9.2) and slot 541 writes +0x040. None of the offsets reached
falls in 0x568..0x58F. Per slot rows are in the scratch output, not kept in the repo.

### 9.5 Tooling

`tools/apply_battle_structs.py` refused every apply before this pass: 57 member comments in the IDB
still named functions as `sub_XXXXXX` that the header had since renamed. The comment check now maps
`sub_XXXXXX` to the current IDB name before comparing, and two hand reviewed renames sit in
`COMMENT_RENAMED`. `tools/apply_battle_actor_names.py` (framework copied from apply_rcflex_names.py)
names six helpers. Rehearsed on a copy: names dry run 6, applied 6 with comments, second dry run 0.
Structs: diff clean, applied, second dry run clean. Nothing saved.

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
