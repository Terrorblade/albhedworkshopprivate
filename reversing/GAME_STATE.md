# Game state: party, inventory, equipment, progression

How FFX.exe stores the data the menus edit and the data two co-op machines have to agree on.
Everything here was read out of the IDB rather than assumed, and where something is inference it
says so. Addresses are VAs. RVA = VA - 0x400000.

The code that came out of this is `loader\workshop\include\ffx\addresses\GameState.h` (the
addresses), `loader\workshop\include\ffx\GameState.h` and `loader\workshop\src\ffx\GameState.cpp`
(the typed accessors and the desync hash).

## Relationship to FFX_GAME_NOTES.md

That document's "Game state globals" table was the starting point. Scoring it:

**Confirmed, unchanged.**

- `g_ffxSaveData 0x112CA90`, 0x68C0 = 26,816 bytes, `0x112CA90`..`0x1133350`. The memset in
  `FFX_InitNewSaveData 0x786B00` is literally `sub_79F230(&g_ffxSaveData, 26816)`.
- `FFX_GetSaveData 0x785240` returns `&g_ffxSaveData`.
- `LoveParam` DWORD[8] at +0x2C.
- current map id +0xB8, playtime +0xBC (clamped to 3,599,999), scenario word +0xBEC.
- `g_ffxEquipmentArray` +0x449C, 200 entries x 22 bytes, entry+6 = owning character index.
- `g_ffxCharRecords` +0x55CC, **18** records of 0x94 = 148 bytes. +4 base max HP, +8 base max MP,
  +12..+19 base stats, +0x1C current HP, +0x20 current MP, +45 weapon slot, +46 armour slot,
  +47..+54 effective stats.
- `g_ffxCharNames` +0x634C, 18 x 20 bytes.
- `g_ffxEquipStatBonus 0x1135E00`, 8 x 28 bytes.
- `party_data` block +0x3D0C 0x1C bytes, kernel table 31 at +0x3D28, `conf` block +0x3D48 0x84 bytes.
  Note the bucket covering this range was called "Config" and is now `PartyGilFlags`, because `conf`
  is only 0x84 of its 448 bytes. The rest is `g_ffxGil` (+0x3D48), the party arrays
  (`g_ffxFieldPartyChars01` +0x3D58, `g_ffxPartyMemberIds` +0x3D8C, `g_ffxPartyMemberFlags` +0x3D9C)
  and **256 bytes of world event flags**: `g_ffxWorldFlagBits3DCC` and `g_ffxWorldFlagBits3E4C`, two
  1,024-bit arrays. The first is the most script-written region in the save block. The second is dead
  in this build, its only accessor `sub_785260` has zero callers. So a divergence reported in this
  bucket is most likely a world event flag, not a setting.

**Corrected.**

- **`g_ffxPartyMemberIds 0x113081C` and `g_ffxPartyMemberFlags 0x113082C` are not the party.** They
  are the new-game **starting item ids (WORD[8]) and counts (BYTE[8])**, and they live inside the
  `conf` block (+0x3D48 .. +0x3DCC, so +0x3D8C and +0x3D9C are inside it). `FFX_InitNewSaveData`
  copies them pairwise into the item id and item count arrays. The name is actively misleading and I
  left a `WRONG NAME` comment on both addresses rather than renaming them, because other agents are
  in the database right now. Suggested names: `g_ffxConfStartItemIds` and
  `g_ffxConfStartItemCounts`.
- **`g_ffxChrArray` is `0x23C44E4`, not `0x13C44E4`.** The task brief said 0x13C44E4. The IDB has it
  at 0x23C44E4 with the count at 0x23C44E0, which matches `Rva::ChrArray = 0x01FC44E4` already in
  `addresses\Character.h`. The Workshop constant is right, the brief had a typo.
- The notes' "`+0x3ECC` WORD[256] and `+0x40CC` BYTE[256], consumers untraced" is **the item
  inventory**. See section 4.
- The notes' "+59 status byte" on a character record is the **available sphere level count**.
- The notes' `conf` block description is fine but incomplete: its first dword IS gil.

**New.**

- The three active party members, the battle party order array, the whole inventory, gil, the
  key-item flags, the per-character ability bitmaps, the equipment entry layout including the
  auto-ability slots, the character-index-to-chr-id table, the single inventory add function, and
  the chest reward commit path.

---

## 1. The party

### The three active members

`g_ffxSaveData + 0x3D58`, three bytes, `0x11307E8`, `0x11307E9`, `0x11307EA`. Each is a **character
record index** 0..17, and `0xFF` means the slot is empty. IDA types the first two as one WORD, which
is why the existing symbol is `g_ffxFieldPartyChars01` plus a separate `g_ffxFieldPartyChar2`.

Two accessors agree on it:

- `FFX_SaveData__getFieldParty 0x7852F0` - `int __cdecl (int *s0, int *s1, int *s2)`, each pointer
  optional, zero-extends the byte.
- `FFX_SaveData_GetFieldPartyArray 0x785270` (was `sub_785270`) - returns the pointer and writes
  **3** to an out count.

11 callers, including six ATEL script syscalls, `FFX_Atel_GetPartyMemberActor 0x86A5E0` and the
battle debug HUD.

### The battle party order

`g_ffxBattlePartyOrder 0x112C895`, BYTE[7], `0xFF` padded. **Not inside the save block** - it is
battle work RAM, 0x1FB bytes below `g_ffxSaveData`. `[0..2]` are the three active battle members and
`[3..6]` the reserves.

`FFX_Btl_SetupUnitRoster 0x79C110` (was `sub_79C110`) is the authority on how the two connect, and
it is the single most load-bearing loop for co-op ownership:

```c
for (j = 0; j < 3; ++j) {
    c = g_ffxFieldParty[j];                 // saveData+0x3D58
    if (c != 255) {
        a = FFX_Battle_GetActor(c);
        a[16]   = 1;                        // unit exists
        a[3528] = 1;                        // front row
        a[1278] = j;                        // <-- the active battle slot, 0..2
    }
    g_ffxBattlePartyOrder[j] = c;
}
memset(&g_ffxBattlePartyOrder[3], 0xFF, 4);
```

Writeback is `FFX_Btl_CommitPartyToField 0x786930` (was `sub_786930`), which copies
`g_ffxBattlePartyOrder[0..2]` back into `saveData+0x3D58`, so a party change made inside battle
survives into the field.

`FFX_Btl_SwapPartyArrangement 0x7ADE10` pushes and pops the whole arrangement against a saved copy
at `0x112C89C` (7 bytes) plus `0x112C8A3` and `0x112C8B4` (17 bytes each, the aeon order). A
scripted battle that forces a party uses this.

**For the co-op model**, the ownership table's backing store is exactly these three bytes, and the
per-actor slot number the engine itself uses is battle actor + 1278.

### Is there a wider roster?

There is no separate "who is in the party" list. Membership is a **flag bit in each character
record**: record+0x2C bit 0. `FFX_SaveData_IsCharInParty 0x785380` (was `sub_785380`) is
`record[0x2C] & 1`, and `FFX_Btl_SetupUnitRoster` uses it to decide which of actors 0..17 exist.
Three more bits on the same byte have readers:

| test | function | bit |
|---|---|---|
| in party | `0x785380` | 0x01 |
| permanent, cannot be removed | `0x7853C0` | 0x02 |
| unknown | `0x785360` | 0x04 |
| selectable, set in lockstep with bit 0 | `0x7853A0` | 0x10 |

`FFX_SaveData_SetCharInParty 0x7869B0` (was `sub_7869B0`) sets bits 0 and 4 together, refuses to
clear when bit 1 (permanent) is set, and keeps the 17-byte aeon order array at +0x3D5B in step.

## 2. Which character is which

**Answered, from a table rather than from guessing.** `g_ffxCharIndexToChrId 0xC423A0` (was
`dword_C423A0`), DWORD[18]:

| record index | character | chr id |
|---|---|---|
| 0 | Tidus | 1 |
| 1 | Yuna | 2 |
| 2 | Auron | 3 |
| 3 | Kimahri | 4 |
| 4 | Wakka | 5 |
| 5 | Lulu | 6 |
| 6 | Rikku | 7, or **41** when `saveData+0xD1` is non-zero |
| 7 | Seymour | 8 |
| 8..17 | the ten aeons | 0x3001 0x3002 0x3003 0x3004 **0x3006** 0x3007 0x3008 0x3009 0x300A 0x300B |

So **chr id = record index + 1 for the eight playable characters**, which is what the project
suspected but could not show. The project's known spawnable set `1..8, 41, 45, 101..108, 307, 901,
908` now reads as: 1..8 the eight characters, 41 Rikku's alternate, 101..108 the high-detail
variants of the same eight (the +100 offset lines up exactly), and 45, 307, 901, 908 still
unidentified.

The only reader is `FFX_Btl_ResolveUnitChrId 0x79A440` (was `sub_79A440`), which turns a battle
unit's type id at actor+14 into the chr id at actor+4:

- high nibble 0: a character or aeon, chr id = `g_ffxCharIndexToChrId[id & 0xFFF]`, with the Rikku
  override.
- high nibble 1: a monster, chr id = `0x1000 + (id & 0xFFF)`.
- anything else: chr id 0.

The aeons sit in the 0x3000 chr id space because they load out of the monster archives rather than
the party archives.

Three independent confirmations of the character order:

1. `g_ffxDebugCharNames 0xC3432C` - `"Tidus(0)"`, `"Yuna(1)"`, `"Auron(2)"`, `"Kimari(3)"`,
   `"Wakka(4)"`, `"Lulu(5)"`, `"Rikk(6)"`, `"Seymour(7)"`. Read by
   `FFX_DebugPage_battleCharParams_formatLine`.
2. `g_ffxCharNamesJp 0xC53728` (was `off_C53728`) - the same eight as Shift-JIS strings, read by the
   battle debug HUD at 0x88098C.
3. The aeon tail. `FFX_Btl_LoadPlayerData` and `sub_79BB50` special-case indices **15, 16, 17** as a
   group (the Magus Sisters) and index **14** on its own (Yojimbo), and `FFX_Btl_SetupUnitRoster`
   special-cases actor 16. That is exactly the standard FFX aeon order Valefor, Ifrit, Ixion, Shiva,
   Bahamut, Anima, Yojimbo, Cindy, Sandy, Mindy.

Record index 7 (Seymour) is skipped by `FFX_Btl_LoadPlayerData`'s ability-mask loop and by
`FFX_SaveData_LockEquippedGear 0x7AD5E0` (`a1 < 7`), so for equipment and ability purposes the
playable set is 0..6.

The player-facing names are a separate thing: `g_ffxCharNames 0x1132DDC`, 18 x 20 bytes, where
bytes 0..17 are the name text, byte 18 is the language it was written in and byte 19 is a "player
renamed this" flag. `FFX_SaveData__reloadCharNames 0x787100` refills them from the `ply_save`
kernel table.

## 3. Per-character stats

`g_ffxCharRecords 0x113205C`, **18** records, stride **0x94 = 148** bytes, so
`0x113205C`..`0x1132AC4`. The end bound is the loop condition in `FFX_InitNewSaveData`
(`while ((int)v24 < (int)&word_1132AC4)`) and in `FFX_Btl_CommitActorsToSave`, and 18 x 148 = 2664 =
0x1132AC4 - 0x113205C exactly.

`FFX_SaveData__getCharRecord 0x785330` is `index <= 0x11 ? base + 148*index : NULL`.

| off | size | field | proof |
|---|---|---|---|
| 0x04 | dword | base max HP | `FFX_Debug_MaxHpMpAndStats 0x784B00` writes 99999, `getCharBaseStats` reads |
| 0x08 | dword | base max MP | same, 9999 |
| 0x0C..0x13 | 8 bytes | base stats: Str, Def, Mag, MagDef, Agi, Luck, Eva, Acc | `getCharBaseStats 0x785B60`, and the debug fill writes two dwords of 0xFF |
| 0x14 | dword | total AP earned | `FFX_SaveData_AddAp 0x784610` |
| 0x18 | dword | AP toward the next sphere level | same |
| 0x1C | dword | current HP | `getCharCurrentStats 0x787230`, `FFX_Btl_CommitActorsToSave` |
| 0x20 | dword | current MP | same |
| 0x24 | dword | max HP including bonuses | `FFX_Btl_LoadCharRecordIntoActor 0x79C5F0` -> actor[357] |
| 0x28 | dword | max MP including bonuses | same -> actor[358] |
| 0x2C | byte | party flags, see section 1 | `0x785360`, `0x785380`, `0x7853A0`, `0x7853C0` |
| 0x2D | byte | weapon equip slot id, 0xFF = none | `FFX_SaveData__setCharEquip 0x7AB970` |
| 0x2E | byte | armour equip slot id, 0xFF = none | same |
| 0x2F..0x36 | 8 bytes | effective stats, same order as 0x0C | `getCharCurrentStats`, `0x79C5F0` |
| 0x38..0x3A | 3 bytes | copied back from actor+1467..1469 at end of battle, overdrive related | `FFX_Btl_CommitActorsToSave 0x785FC0` |
| 0x3B | byte | **available sphere levels (S.Lv)**, 0..99 | `0x7853E0` reads, `0x786EF0` spends, `0x784610` earns |
| 0x3C | byte | spent sphere levels, 0..101 | `0x786EF0`, and `0x784E90` uses 0x3B + 0x3C as the total |
| 0x3D | byte | revive / death countdown carried out of battle | `FFX_Btl_CommitActorsToSave` |
| 0x3E..0x49 | 6 words | per-character ability flags for ability ids 0x3000..0x305F | `maybe_FFX_SaveData_TestEventFlag 0x785020`, stride 74 words = 148 bytes |
| 0x4E | byte | bit 1 polled across the party by `FFX_SaveData_AllPartyMembersOk 0x785B10` | |
| 0x50, 0x54, 0x58, 0x5C | 4 dwords | per-character battle counters, clamped 999999999 | `FFX_SaveData_BumpCharBattleCounter 0x785A00` |

0x00..0x03, 0x37, 0x4A..0x4D, 0x4F, and 0x60..0x93 have no reader I identified. That is 0x34 bytes
of the 0x94 unaccounted for.

Two readers to use rather than reimplement:

- `FFX_SaveData__getCharBaseStats 0x785B60` - `char * __cdecl (unsigned index, int *out)`. Fills a
  10-dword struct: `out[0..7]` the eight stats, `out[8]` max HP, `out[9]` max MP, with the equipped
  gear bonus from `g_ffxEquipStatBonus` already folded in (HP x 50, MP x 5, stats +1 each).
- `FFX_SaveData__getCharCurrentStats 0x787230` - `int __cdecl (int index, void *out)`. Fills a
  20-byte struct: `out[0]` sphere levels, `out+4` current HP, `out+8` current MP, `out+12..19` the
  eight effective stats. **It reads the battle actor instead of the record whenever
  `FFX_Battle_IsActive()`**, which is a process-wide flag. See section 7.

The battle-side copies, for completeness: `FFX_Btl_LoadCharRecordIntoActor 0x79C5F0` is the record
to actor direction and `FFX_Btl_CommitActorsToSave 0x785FC0` is the actor to record direction, and
the second one is where a whole battle's result lands in the save block in one call.

There is a second, wider per-character ability bitmap at **`saveData+0x6034`**, 18 characters x 22
words = 44 bytes each, covering ability indices 0..351 against the 320 ability rows.
`FFX_SaveData_TestCharAbility 0x785140` and `FFX_SaveData_SetCharAbility 0x785E00`. It ends at
`0x1132AC4 + 0x318 = 0x1132DDC`, which is exactly `g_ffxCharNames`, so that block is proven to be
contiguous and fully accounted for. What distinguishes it from the 6 words inside the record I did
not establish - most likely one is "learned on the sphere grid" and the other "currently usable",
but I am not claiming which.

Ability ids 0x3060 and above are not per-character at all, they live in a shared 32-byte bitmap at
`saveData+0x3D6C`, which is `maybe_FFX_SaveData_TestEventFlag`'s other branch.

## 4. Inventory and gil

### Gil

`saveData+0x3D48`, DWORD, clamped 0..999,999,999. It is the first dword of the `conf` kernel block.

- `FFX_SaveData_GetGil 0x784E80` (was `sub_784E80`)
- `FFX_SaveData_SetGil 0x785C20` (was `sub_785C20`) - clamps, and a negative argument stores 0
- `FFX_SaveData_SpendGil 0x7859A0` (was `sub_7859A0`) - returns 0 and deducts when affordable, -1
  and no change otherwise. **A negative cost adds gil**, which is how chests pay out.

The proof is the debug HUD at `0x8809C2`: it pushes the string `"GIL:"`, then calls `0x784E80` and
sprintf's the result with `"%d"`.

### Consumable items

Two parallel arrays, each physically **256** entries, of which every gameplay loop uses the first
**112**:

- `g_ffxItemIds saveData+0x3ECC` = `0x113095C`, WORD[256]. A 0x2000-based item id, or 255 (0xFF) for
  an empty slot.
- `g_ffxItemCounts saveData+0x40CC` = `0x1130B5C`, BYTE[256]. Held count 0..99. A count of 0 means
  the slot is free even if the id is still set.

The 256 comes from `FFX_InitNewSaveData`, which clears `for (; v37 < 256; ++v37)`, and from the fact
that +0x3ECC + 512 = +0x40CC and +0x40CC + 256 = +0x41CC, the next field, so the arrays butt up
against each other with nothing between. The 112 comes from every runtime loop:
`FFX_SaveData_AddItem`, `FFX_SaveData_GetItemCount`, `FFX_SaveData_RebuildItemLists`, the two
accessors that report their own length, and the debug fill and clear.

**The single add function is `FFX_SaveData_AddItem 0x790550`** (was `sub_790550`),
`int __cdecl (int itemId, int delta)`:

```c
if ((itemId & 0xFFFFF000) != 0x2000 || delta == 0) return -1;
// pass 1: find itemId with a non-zero count, remembering the first free slot
//         count = clamp(count + delta, 0, 99); if (count == 0) id = 255;
// pass 2: not held and delta > 0 -> first free slot gets id and clamp(delta, 0, 99)
// side effects: set the bit in the gained mask (+0x41CC) or the lost mask (+0x41EC),
//               then FFX_SaveData_RebuildItemLists
return 0;   // or -1 when the array is full
```

Nothing else in the binary writes those two arrays except `FFX_InitNewSaveData` and the three debug
helpers (`FFX_Debug_FullItem 0x7849D0`, `FFX_Debug_OneOfEveryItem 0x784C20`,
`FFX_Debug_ClearInventory 0x784CD0`). So **0x790550 is the commit point** a co-op chest has to reach
exactly once per machine.

Supporting cast:

| function | was | what |
|---|---|---|
| `FFX_SaveData_GetItemCount 0x7904B0` | `sub_7904B0` | held count of one id, minus 1 if it equals `g_ffxItemInUse` |
| `FFX_SaveData_GetItemIdArray 0x790530` | `sub_790530` | pointer plus count 112 |
| `FFX_SaveData_GetItemCountArray 0x790510` | `sub_790510` | pointer plus count 112 |
| `FFX_SaveData_GetItemGainedMask 0x7904A0` | `sub_7904A0` | 256-bit mask at +0x41CC |
| `FFX_SaveData_GetItemLostMask 0x790500` | `sub_790500` | 256-bit mask at +0x41EC |
| `FFX_SaveData_MarkItemDelta 0x790670` | `sub_790670` | sets one bit in either mask |
| `FFX_SaveData_RebuildItemLists 0x7906A0` | `sub_7906A0` | rebuilds the battle list at `0x112C6B6` and the field list at `0x112C796`, 112 words each, 0xFF padded |
| `FFX_SaveData_SetItemInUse 0x790790` | `sub_790790` | writes `g_ffxItemInUse 0x112C948` |

### Key items

A **128-bit flag array** at `saveData+0x448C` = `0x1130F1C`, no counts. Indexed by `(id & 0xFFF)`
which must be below 0x80, and the id space observed at the call sites is 0xA000-based.

`FFX_SaveData_TestKeyItemFlag 0x790800`, `FFX_SaveData_SetKeyItemFlag 0x790930`,
`FFX_SaveData_ClearKeyItemFlag 0x7907A0`, all previously `sub_*`.

### Monster capture and the bestiary

The three blocks between the item masks and the key item flags are the Monster Arena and bestiary
state, which is worth naming because the co-op model has to decide whether capture counts are shared.

- `g_ffxMonsterCaptureCounts saveData+0x420C` = `0x1130C9C`, BYTE[512], value clamped 0..10. That
  cap of 10 is exactly the Monster Arena's capture requirement per species.
  `FFX_SaveData_GetCaptureCount 0x790AF0`, `FFX_SaveData_AddCaptureCount 0x790B90`, and
  `FFX_SaveData_TryCapture 0x790B30` which picks message id 0x300A (captured), 0x300B (already have
  ten) or 0x300C (cannot capture).

  **CORRECTED: the index is a capture species index 0..138, not a monster id.** This entry used to
  say `(monsterId & 0xFFF)`. The mask is right and the name was not, which matters because the two
  values live side by side on the battle unit and a patch that writes by monster id hits the wrong
  byte for nearly every fiend. The accessors do mask with `& 0xFFF`, but look at what the only caller
  passes, in `FFX_Btl_OnUnitDefeated`:

  ```
  movzx eax, word ptr [esi+0Eh]     ; the TYPE ID, for the bestiary bit
  call  FFX_SaveData_MarkMonsterBit
  movzx eax, word ptr [esi+6D6h]    ; the CAPTURE SPECIES INDEX, for the count
  cmp   byte ptr [esi+0DD0h], 0     ; the per-monster "can be captured" flag
  jz    short skip
  push  eax
  push  edi
  call  FFX_SaveData_TryCapture
  ```

  Two different fields two instructions apart. The species index comes out of the monster's own
  shipped file, and `reversing/MONSTER_ARENA.md` has the 139-row species table and the arena roster
  at `[104..138]`.
- `g_ffxMonsterSeenMask saveData+0x440C` = `0x1130E9C`, 512 bits. `FFX_Btl_SetupUnitRoster` sets a
  bit here for every enemy type it spawns, so this is the bestiary "encountered" list.
- `g_ffxMonsterDefeatedMask saveData+0x444C` = `0x1130EDC`, 512 bits, same bit math. **Settled:**
  this is the defeated list. `FFX_Btl_OnUnitDefeated` marks the victim's type id here at `0x78C80C`,
  when a monster actually dies. The earlier text said which was which had not been established.

### The chest reward path

Two ATEL script syscalls commit a treasure, and both go through one global staging buffer:

- `FFX_AtelSys_Core_347_start 0x85A8A0` - the normal chest, with the "You got X" message window.
- `FFX_AtelSys_Core_423_start 0x857B70` - the silent version.

Both do `FFX_Treasure_DecodeToStaging(treasureId)` (`0x798FD0`, was `sub_798FD0`) which decodes one
treasure row into `g_ffxTreasureStaging 0x2310EA0`, then read the reward out of it:

| staging | address | contents |
|---|---|---|
| +0xCC | `0x2310F6C` | gil, handed to `FFX_SaveData_SpendGil` **negated** so it adds |
| +0xD0 | `0x2310F70` | non-zero when there is a consumable |
| +0xD1 | `0x2310F71` | non-zero when there is a key item |
| +0xD2 | `0x2310F72` | how many equipment pieces |
| +0xD4 | `0x2310F74` | item id |
| +0xE4 | `0x2310F84` | item quantity |
| +0xEC | `0x2310F8C` | key item id |
| +0xEE | `0x2310F8E` | WORD[] of equipment slot ids |

and commit with `FFX_SaveData_AddItem(itemId, qty)`, `0x88E790(keyItemId)`,
`FFX_SaveData_AddEquipEntry` per staged piece, and `FFX_SaveData_SpendGil(-gil)`. There is exactly
one staging buffer for the whole process.

## 5. Equipment

### The array

`g_ffxEquipmentArray saveData+0x449C` = `0x1130F2C`, **200** entries of **22** bytes. 200 x 22 =
4400 = 0x1130, and +0x449C + 0x1130 = +0x55CC, which is `g_ffxCharRecords`. Contiguous, exact.

Entry layout, every field with a reader:

| off | size | field |
|---|---|---|
| 0x00 | word | display or name id, derived from the ability set by `FFX_Equip_ComputeNameId 0x7A0CF0` |
| 0x02 | byte | in use. 0 means the slot is free. `FFX_SaveData_AddEquipEntry` sets 1, `FFX_SaveData_RemoveEquipEntry` sets 0 |
| 0x03 | byte | flags. bit 0x02 = locked because a party member has it equipped (`FFX_SaveData_LockEquippedGear 0x7AD5E0`), bit 0x08 = a special marker searched for by `sub_7AD640`, `sub_7AD6E0` and `sub_7AD710` |
| 0x04 | byte | which character the piece is **for**, 0..6 |
| 0x05 | byte | kind. 0 = weapon, 1 = armour |
| 0x06 | byte | which character currently has it **equipped**, 0xFF = nobody |
| 0x08 | byte | goes to battle actor+1473 |
| 0x09 | byte | goes to battle actor+1479 |
| 0x0A | byte | added into battle actor+1496 |
| 0x0C | word | per-character model or name id, written by `sub_7A0C50` |
| 0x0E..0x15 | 4 words | **the auto-ability list**, 0x8000-based ability ids. 0 or 0xFF means an empty slot |

0x07 and 0x0B have no reader I found.

The auto-ability slots are proven twice over. `FFX_Equip_HasAbility 0x7A0C20` is literally
`for (i = entry+14; *i != abilityId; ++i) if (++n >= 4) return 0; return 1;`, and
`FFX_Btl_LoadCharRecordIntoActor 0x79C5F0` walks exactly four words from entry+14, looks each one up
in the ability kernel table with `id & 0xFFF`, and ORs its effects into the actor. Four words at
+14 fills the entry to 22 bytes with nothing left over.

`entry+4` versus `entry+6` is worth keeping straight. +4 is the character the weapon model belongs
to and never changes - FFX weapons are per-character, a Brotherhood is always Tidus's. +6 is who
has it on right now. `FFX_SaveData_RebuildEquipOwners 0x7ABE30` proves +6 by clearing all 200 of
them to 0xFF and then rewriting them from each record's +0x2D and +0x2E.

### Slot ids

A character record names equipment by a 16-bit slot id, not by pointer.
`FFX_SaveData__getEquipEntry 0x7ABBD0` decodes it:

- `0xFF` gives NULL, nothing equipped.
- high nibble 7 selects an 8-entry scratch array at `0x2310F9E`.
- high nibble 11 selects an 8-entry scratch array at `0x2310DDE`.
- anything else selects the main 200-entry save-block array, index `id & 0xFFF` clamped below 200.

New entries are handed out as `0x5000 + index` by `FFX_SaveData_AddEquipEntry 0x7AB910`, which drops
into high nibble 5 and therefore the main array.

### The stat bonus cache

`g_ffxEquipStatBonus 0x1135E00`, **8** entries of 28 bytes, so only the playable characters.
`FFX_SaveData__getEquipStatBonus 0x7987F0` indexes it, `FFX_SaveData__getCharBaseStats` folds it in
as HP x 50, MP x 5 and +1 per stat byte. It is **outside the save block** (0x1135E00 > 0x1133350),
so it is a derived cache, not state. `FFX_SaveData_ClearEquipStatBonus 0x798820` zeroes it.

## 6. What is contiguous

**Good news: almost all of it is one range.** Every piece of persistent game state named above
lives inside `g_ffxSaveData`, `0x112CA90` .. `0x1133350`, 26,816 bytes. Laid out in order:

```
+0x0000                 header, map id +0xB8, playtime +0xBC, LoveParam +0x2C
+0x0BEC  word           scenario / progress
+0x3D0C  0x1C           party_data kernel block
+0x3D28  0x20           kernel table 31
+0x3D48  0x84           conf kernel block   <- gil at +0x3D48, start items at +0x3D8C and +0x3D9C
+0x3D58  3              THE THREE ACTIVE PARTY MEMBERS
+0x3D5B  17             aeon order
+0x3D6C  32             shared ability bitmap (ability ids 0x3060 and up)
+0x3ECC  512            item ids,    WORD[256], 112 used
+0x40CC  256            item counts, BYTE[256], 112 used
+0x41CC  32             item gained mask
+0x41EC  32             item lost mask
+0x420C  512            monster capture counts, BYTE[512], 0..10
+0x440C  64             monster seen mask, 512 bits
+0x444C  64             second monster mask, 512 bits
+0x448C  16             key item flags, 128 bits
+0x449C  4400           equipment array, 200 x 22
+0x55CC  2664           character records, 18 x 148
+0x6034  792            per-character ability bitmap, 18 x 44
+0x634C  360            character names, 18 x 20
+0x64B4  ...            tail, unidentified, ends +0x68C0
```

Where the blocks touch, the arithmetic closes exactly: 0x449C + 0x1130 = 0x55CC, 0x55CC + 0xA68 =
0x6034, 0x6034 + 0x318 = 0x634C, 0x634C + 0x168 = 0x64B4. So the whole span 0x3ECC..0x64B4 is one
unbroken run of the data a desync detector cares about.

**So a hash can be a handful of memcmp-sized ranges, not a hundred reads.** The things to exclude:

1. **Playtime, `+0xBC`, 4 bytes.** It advances every frame
   (`FFX_SaveData__setPlaytime 0x787610`). Hashing the whole block without excluding this would
   report a divergence constantly.
2. The map id at `+0xB8` is worth including, but a peer mid-transition will differ for a frame or
   two, so it belongs in its own bucket rather than mixed with slow-moving state.
3. The two item change masks at `+0x41CC` and `+0x41EC` are UI state, not game state. They diverge
   harmlessly whenever a menu is open on one machine and not the other, so bucket them separately.

Outside the block and **deliberately not hashed**: `g_ffxBattlePartyOrder 0x112C895` and the rest of
the battle work RAM, the battle actor arrays, and the derived `g_ffxEquipStatBonus 0x1135E00`. These
are all reconstructible from the save block, so hashing them would report divergences that fix
themselves.

Bucketed hashing is the useful shape, because "the hash changed" is far less actionable than "the
equipment array changed". The buckets the header implements, each one contiguous:

| bucket | range | bytes |
|---|---|---|
| Header | +0x0000 .. +0x00B8 | 184 |
| MapId | +0x00B8 .. +0x00BC | 4 |
| Progress | +0x00C0 .. +0x3D0C | 15,436 |
| PartyGilFlags | +0x3D0C .. +0x3ECC | 448 |
| Inventory | +0x3ECC .. +0x41CC | 768 |
| ItemMasks | +0x41CC .. +0x420C | 64 |
| Monsters | +0x420C .. +0x448C | 640 |
| KeyItems | +0x448C .. +0x449C | 16 |
| Equipment | +0x449C .. +0x55CC | 4,400 |
| Characters | +0x55CC .. +0x6034 | 2,664 |
| Abilities | +0x6034 .. +0x634C | 792 |
| Names | +0x634C .. +0x64B4 | 360 |
| Tail | +0x64B4 .. +0x68C0 | 1,036 |

Playtime at +0xBC is the only hole, and it is deliberately skipped. That is 13 ranges over ~26 KB.
FNV-1a over 26 KB is in the tens of microseconds on a 32-bit build, so running it every frame is
affordable and running it once a second is obviously fine.

What I did **not** establish: whether anything else inside the block ticks per frame. Playtime is
the one I can prove from the code. Confirming the rest wants a runtime experiment: hash each bucket
for a few hundred idle frames and see which ones move. `ffx::HashGameState` returns the per-bucket
hashes precisely so that experiment is a one-liner.

## 7. Single-player assumptions on these paths

Each of these is marked in the IDB with a comment starting `CO-OP HAZARD:`.

1. **`g_ffxItemInUse 0x112C948`** - one global staged item id for the whole process.
   `FFX_SaveData_GetItemCount 0x7904B0` subtracts one for it with no player or character index, so
   two players staging an item at the same time see each other's reservation.
2. **`g_ffxTreasureStaging 0x2310EA0`** - one global decode buffer for a chest reward. Two chests
   resolving in the same frame clobber each other. Any co-op chest replication has to serialise
   through the decode or snapshot the staging block.
3. **`FFX_Battle_IsActive()` in `FFX_SaveData__getCharCurrentStats 0x787230`** - a single
   process-wide flag that switches the stats source between the save record and the battle actor for
   **every** character at once. With one player in battle and another in the field there is no
   per-player answer to "what is this character's current HP". This is the biggest structural
   problem in my area and it is a design constraint, not a bug to patch.
4. **The item gained and lost masks at `+0x41CC` and `+0x41EC`** - single global "what changed since
   the menu last looked" bitmaps with no per-player split, so a remote player's pickup flashes on
   the local player's item list.
5. **`FFX_Atel_FindActorByPartyChar 0x86A470`** memoises its last answer in a one-entry cache
   (`dword_13270E4`, `dword_13270D8`, `word_13270DC`, `word_13270E0`). It is keyed on
   `(atelCtx+26, index)` so it is correct, but it is not reentrant and interleaved callers thrash
   it.
6. The equipment scratch arrays at `0x2310F9E` and `0x2310DDE` (8 entries each, reached through
   equipment slot id high nibbles 7 and 11) are single global staging areas for gear in flight.

Things that are **not** hazards, worth stating so nobody re-checks them:

- `g_ffxEquipStatBonus` is 8 entries, one per character, indexed properly.
- `g_ffxBattlePartyOrder` is a single global, but that is correct: it **is** the ownership table.
- `FFX_SaveData_AddItem`, `FFX_SaveData_AddEquipEntry`, `FFX_SaveData_SetGil` and
  `FFX_SaveData_SpendGil` all take their arguments and consult no "current" global. They are safe
  to call from a network handler.

## 8. Other names recorded in the IDB from this pass

`FFX_KernelTable_GetRow 0x7AB870` (was `sub_7AB870`) is the row lookup every save-data and
battle-data reader uses, so it is worth knowing. The table header is a DWORD count at +0 then that
many 12-byte range descriptors from +8, each `{ WORD lo, WORD hi, WORD stride, WORD stringOffset,
DWORD dataOffset }`. The first range whose `[lo,hi]` contains the index wins, and the row is
`table + dataOffset + (index - lo) * stride`. The out parameter gets `table + dataOffset +
stringOffset`, which is where that range's strings start.

Also named: `FFX_SaveData_AddAp 0x784610`, `FFX_SaveData_SpendSphereLevels 0x786EF0`,
`FFX_SaveData_ApNeededForNextLevel 0x784E90`, `FFX_Debug_MaxHpMpAndStats 0x784B00`,
`FFX_Debug_FullSphereLevels 0x784AE0`, `FFX_SaveData_GetCharName 0x784FB0`,
`FFX_SaveData_GetRikkuAltOutfit 0x86A7E0`.

## 9. Wiring, for whoever owns the shared files

The game-state work deliberately did not touch `include\ffx\Addresses.h`, `include\ffx\Api.h`,
`src\ffx\VerifyLayout.cpp` or `include\ffx\Ffx.h`. Four lines are wanted:

1. `include\ffx\Addresses.h`, after the existing includes:
   `#include "ffx/addresses/GameState.h"`
2. `src\ffx\VerifyLayout.cpp`, in `addressAreas`:
   `{ "game state", &Rva::GameStateRvaList },`
3. `include\ffx\Ffx.h`, with the other area headers:
   `#include "ffx/GameState.h"`
4. Nothing in `Api.h`. `GameState.cpp` resolves its own function pointers, so the game-state API
   works whether or not `BindApi` has run, and `ffx::Api` stays a character-and-camera thing.

For `VerifyLayout.cpp`'s `calledFunctions` list, the entries worth adding are the ones the library
actually calls through a pointer:

```
{ "SaveDataAddItem",             Rva::SaveDataAddItem             },
{ "SaveDataGetItemCount",        Rva::SaveDataGetItemCount        },
{ "SaveDataSetGil",              Rva::SaveDataSetGil              },
{ "SaveDataSpendGil",            Rva::SaveDataSpendGil            },
{ "SaveDataTestKeyItemFlag",     Rva::SaveDataTestKeyItemFlag     },
{ "SaveDataSetKeyItemFlag",      Rva::SaveDataSetKeyItemFlag      },
{ "SaveDataGetEquipEntry",       Rva::SaveDataGetEquipEntry       },
{ "SaveDataSetCharEquip",        Rva::SaveDataSetCharEquip        },
{ "SaveDataAddEquipEntry",       Rva::SaveDataAddEquipEntry       },
{ "SaveDataCountEquipEntries",   Rva::SaveDataCountEquipEntries   },
{ "SaveDataGetCharBaseStats",    Rva::SaveDataGetCharBaseStats    },
{ "SaveDataGetCharCurrentStats", Rva::SaveDataGetCharCurrentStats },
{ "SaveDataGetCaptureCount",     Rva::SaveDataGetCaptureCount     },
{ "SaveDataAddCaptureCount",     Rva::SaveDataAddCaptureCount     },
```

All fourteen were checked byte for byte against `workshop::LooksLikePrologue` and all fourteen pass.

**One trap.** Do NOT add `Rva::BtlCommitPartyToField` to `calledFunctions`. Its first bytes are
`66 A1 95 C8 12 01`, a `mov ax, [imm32]` with the operand size prefix, and `LooksLikePrologue` does
not accept a `0x66` prefix. The address is correct but the check would reject it and refuse to run
the plugin. It is in the address list, which only bounds checks, and the comment in
`addresses\GameState.h` says so at the declaration. Widening `LooksLikePrologue` to accept `0x66`
would also be a reasonable fix, but that is a shared file and not mine to change.
