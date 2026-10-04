# The save block for a cheat UI: stats, equipment, auto-abilities, items, gil, sphere grid

**Why this document exists.** The cheat plugin has to let the user edit party stats, equipment and
its auto-abilities, the sphere grid, items and gil, and every option in the UI has to be built from
game data rather than typed in. So this document is organised around two things: the write path for
each editable thing, and the enumerable list behind it. A write path with no list is only half an
answer, so the enumeration table comes first.

Everything here was read out of `FFX.exe` in IDA or parsed out of the shipped data files with
`tools/vbf.py`. Where something is inferred rather than proved, it says so.

Addresses are RVAs. VA = RVA + 0x400000. `SaveData` is RVA `0x00D2CA90` (VA `0x112CA90`) and
`SaveData+N` offsets are relative to that.

## 0. The one thing to understand first: the kernel tables

Every list the UI needs, except the character roster, is a **kernel table**. These are small binary
files shipped inside `data/FFX_Data.vbf` under
`ffx_ps2/ffx/master/new_<lang>pc/battle/kernel/<name>.bin`, read at startup by
`FFX_Btl_ReadKernelBin 0x382DF0` (which builds the path
`host0:/ffx/master/new_uspc/battle/kernel/%s.bin` for English) and left resident behind a global
pointer. **The plugin should read the resident copy through that pointer, not re-read the file**,
because the resident copy is what the game itself resolves ids against.

All of them share one format and one accessor, `FFX_KernelTable_GetRow 0x3AB870`:

```
file +0  WORD   range count
     +2  6      padding
     +8         that many 12-byte range descriptors:
                   +0 WORD lo        first id in the range
                   +2 WORD hi        last id
                   +4 WORD stride    row size
                   +6 WORD strOff    string-block offset, relative to dataOff
                   +8 DWORD dataOff  row block offset, relative to the file base

row(i)     = file + dataOff + (i - lo) * stride
stringBase = file + dataOff + strOff
```

Every shipped table has exactly **one** range with `lo = 0`, so in practice `row(i) = base + 20 + i *
stride` and the string block starts at `base + 20 + strOff`. The range walk still matters because
that is how the engine does it, and `GetRow` falls back to the first descriptor's `dataOff` for an
out-of-range id rather than returning null.

A row names its display string with a **WORD offset into the string block**, not a pointer. So

```c
const char* name = stringBase + *(const uint16_t*)(row + nameFieldOffset);
```

### The string encoding

Kernel-table strings are **not ASCII**. They are a glyph-index encoding: `glyphIndex = byte - 0x30`
and the glyph order is ASCII with the digits moved to the front and `@` dropped.

| byte | meaning |
|---|---|
| 0x00 | end of string |
| 0x03, 0x09, 0x0A, 0x0B, 0x12, 0x13 | control code, each followed by one parameter byte. 0x0A is the colour code, used as `0A B1` to open a highlight and `0A 41` to close it |
| 0x30..0x39 | `'0'`..`'9'` |
| 0x3A..0x49 | `' '`..`'/'` (ASCII 0x20..0x2F), so 0x3A is space, 0x3B `!`, 0x3F `%`, 0x40 `&`, 0x41 `'`, 0x45 `+`, 0x46 `,`, 0x47 `-`, 0x48 `.`, 0x49 `/` |
| 0x4A..0x4F | `':'`..`'?'` (ASCII 0x3A..0x3F) |
| 0x50..0x69 | `'A'`..`'Z'` |
| 0x6A..0x6F | `'['`..`` '`' `` (inferred from the pattern, no sample found) |
| 0x70..0x89 | `'a'`..`'z'` |
| 0x8A and up | extended glyphs: accented letters and icons. `name_txt.bin` rows 36..46 enumerate 0xA3..0xCF as the accented block, and 0x9C is the arrow glyph used in "Overdrive -> AP" |

The whole table decodes `item.bin`, `command.bin`, `a_ability.bin`, `panel.bin`, `important.bin`
and `w_name.bin` with **zero** unknown bytes outside the extended block, which is how it was
verified. A 256-entry lookup in the plugin is the right implementation.

### The tables that exist

`FFX_Btl_LoadKernelTable 0x381D40` is a switch over six ids. The others load from their own
functions.

| table | loaded by | pointer global (RVA) | size global (RVA) | rows | stride | id class |
|---|---|---|---|---|---|---|
| `command` | `0x381D40` case 0 | `0x00D2A92C` | `0x00D2A968` WORD | 320 | 96 | `0x3000` + row |
| `ply_rom` | `0x381D40` case 1 | `0x00D2A938` | `0x00D2A96A` WORD | 20 | 44 | row = character index |
| `a_ability` | `0x381D40` case 2 | `0x00D2A944` | `0x00D2A970` WORD | 134 | 108 | `0x8000` + row |
| `item` | `0x381D40` case 3 | `0x00D2A940` | `0x00D2A96E` WORD | 112 | 96 | `0x2000` + row |
| `monmagic1` | `0x381D40` case 4 | `0x00D2A930` | - | - | - | `0x4000` + row |
| `monmagic2` | `0x381D40` case 5 | `0x00D2A934` | - | - | - | `0x6000` + row |
| `important` | `0x3908C0` | `0x00D334C0` | `0x00D334BC` DWORD | 64 | 20 | `0xA000` + row |
| `w_name` | `0x3A2600` | `0x00D363B4` | `0x00D363B0` DWORD | 170 | 72 | `0x5000` + row |
| `ply_save` | `0x386B00`, `0x387100` | not kept resident | 20 | 148 | row = character index |
| `panel` | `0x654810` / `0x654AB0` | `0x016860E0` | - | 127 | 24 | row = sphere grid node kind |
| `sphere` | `0x654810` / `0x654AB0` | `0x016860E4` | - | 50 | 16 | row = grid sphere item effect |
| the `*_txt` set | `0x38FEF0` | per table | - | - | - | UI strings |

**Caveat on `panel` and `sphere`.** `0x016860E0` and `0x016860E4` are null until the Sphere Grid
screen (menu module 19) has been entered once, because `0x654810` is what allocates and fills them.
`0x654810` is self-contained - it allocates, reads both files and copies them in - so the plugin can
call it itself to populate the two pointers without opening the menu. Not yet tested at runtime.

`0x00D2A92C`, `0x00D2A938` and friends are `0xFFFFFFFF` in the static image. They are filled by
`FFX_Btl_Init 0x381700`, which runs once at startup, so they are valid by the time any menu exists.

## 1. THE ENUMERATION TABLE

This is the table the UI is built from. Every row is a list the user picks from, and every row says
where the list comes from, how long it is, and how to get a display string out of an entry. Nothing
here needs an id typed by hand.

Shorthand used below:

* `KT(p)` means `FFX_KernelTable_GetRow(index, *(short**)(base + p), &outStringBase)` with `p` an
  RVA of a pointer global and `base` the module base.
* `STR(row, off)` means `outStringBase + *(uint16_t*)(row + off)`, then decode with the charmap in
  section 0.
* `rowCount(table)` means `FFX_KernelTable_RowCount 0x3AB8F0 (table, 0)`, which returns
  `hi - lo + 1` of the first range descriptor. Use it instead of a hardcoded count.

| list | table | stride | count / count source | id from row index | display string |
|---|---|---|---|---|---|
| **Characters (roster)** | `CharNames` RVA `0x00D32DDC` = `SaveData+0x634C` | 20 | **18, hardcode it.** `FFX_SaveData_GetCharName 0x384FB0` accepts 0..30 and will read past the array, so the UI must bound 0..17 itself | index is the id | the 20-byte record is `[0..17]` charmap text, `[18]` the language it was typed in, `[19]` a player-renamed flag. Decode `[0..17]` |
| **Items** | `KT(0x00D2A940)` (`item.bin`) | 96 | `rowCount` = 112 | `0x2000 + index` | name `STR(row, 0)`, description `STR(row, 8)`. `+4` and `+12` are the alternate-language variants, selected by `(*(int*)0x00D3079C >> 3) & 1` |
| **Key items** | `KT(0x00D334C0)` (`important.bin`) | 20 | `rowCount` = 64, of which 0..51 are real and 34 is blank | `0xA000 + index` | name `STR(row, 0)`, description `STR(row, 8)`. Or call `FFX_KeyItem_GetNameString 0x390860 (id)`, which returns the pointer directly |
| **Auto-abilities, full id space** | `KT(0x00D2A944)` (`a_ability.bin`) | 108 | `rowCount` = 134, of which 0..128 are real and 129..133 are "Extra 1".."Extra 5" placeholders | `0x8000 + index` | name `STR(row, 0)`, description `STR(row, 8)` |
| **Auto-abilities, the legal customisable set with its cost** | the 8-byte table at RVA `0x00D2A964`. Row 0 and the count both come from `FFX_Equip_GetCustomizeTable 0x390A10 (int *outCount)` | 8 | `outCount` = 125 | the row CARRIES the id | row `+0` WORD kind (**1 = weapon, 2 = armour**), `+2` WORD auto-ability id, `+4` WORD item id, `+6` BYTE quantity. Name the ability through the `a_ability` row for `id & 0xFFF` and the item through the `item` row |
| **Abilities a character can know (commands)** | `KT(0x00D2A92C)` (`command.bin`) | 96 | `rowCount` = 320 | `0x3000 + index` | name `STR(row, 0)`, description `STR(row, 8)`. Or `FFX_Btl_GetCommandNameString 0x4C1A20 (id)` for the name and `0x4C19E0` for the description |
| **Any ability record by id, whatever its class** | - | - | - | - | `FFX_Btl_GetAbilityNameString 0x4B8D70 (int id)` returns a `char*` for a `0x2xxx` item, a `0x3xxx` command, a `0x4xxx` or `0x6xxx` monster ability or an `0x8xxx` auto-ability. **This is the one name function to prefer** |
| **Equipment display names** | `KT(0x00D363B4)` (`w_name.bin`) | 72 | `rowCount` = 170. Rows 0..73 are weapon names (`0x5000`..`0x5049`), rows 74..169 armour (`0x504A`..`0x50A9`) | `0x5000 + index` | **per character.** The name for character `c` in 0..6 is `STR(row, 4*c)`, the alternate variant `STR(row, 28 + 4*c)`, and `+56 + 2*c` is the model / icon id. Or call `FFX_Equip_GetNameString 0x3A0C50 (nameId, charIndex, variant, uint16_t *outIcon)`, which returns the `char*` |
| **Equipment the player owns** | `EquipmentArray` RVA `0x00D30F2C` = `SaveData+0x449C` | 22 | **200 slots, hardcode it.** In use when `entry[2] != 0`. `FFX_SaveData_CountEquipEntries 0x3ABC60` gives the in-use and free counts | slot id is `0x5000 + index`, and the bare low byte works too | `FFX_SaveData_GetEquipNameString 0x3ABDF0 (slotId)` returns the `char*` straight from the entry's own name id and owner. For a kind label use `entry[5]`, 0 weapon and non-zero armour |
| **Equipment templates, the pre-made pieces the game hands out** | the 22-byte table at RVA `0x00D35FFC`, byte size at RVA `0x00D35FF8` | 22 | `rowCount` on the table | index is the id the treasure table uses | it has no name of its own. Build an entry from it as in section 4.4, call `FFX_Equip_ComputeNameId 0x3A0CF0`, then name that |
| **Equipment kinds** | no table | - | 2 | - | one byte, `entry[5]`, 0 = weapon and non-zero = armour. The words "Weapon" and "Armor" are UI text in `menu_txt`, so hardcode two labels |
| **Sphere grid node slots the save holds** | `SphereGridNodes` RVA `0x00D2EC7C` = `SaveData+0x21EC` | 2 | **1024 slots.** That is the bound `FFX_SphereGrid_RecomputeDerived 0x654860` scans. The whole blob is 4896 bytes | the slot index is the id | low byte is the node kind, `0xFF` meaning cleared or no node. High byte is a bitmask of which of characters 0..6 have activated it. Name the kind through the `panel` row |
| **Sphere grid node kinds** | `KT(0x016860E0)` (`panel.bin`) | 24 | `rowCount` = 127 | index is the id | name `STR(row, 0)`. `+16` WORD stat mask, `+18` WORD granted ability id (`0x3xxx`), `+20` BYTE amount. Section 6 has the mask bits |
| **Grid sphere item effects** | `KT(0x016860E4)` (`sphere.bin`) | 16 | `rowCount` = 50, of which 0..29 are real | index is the id | effect text `STR(row, 0)`. This is the "Activates Strength, Defense, or HP node." help line, not a name |
| **Monsters, for completeness** | `KT(0x00D2A930)` and `KT(0x00D2A934)` | 92 | `rowCount` = 300 and 247 | `0x4000 + index` and `0x6000 + index` | `STR(row, 0)` |

Two caveats that will bite if ignored:

1. **`0x016860E0` and `0x016860E4` are null until the Sphere Grid screen has run once.** Call
   `FFX_SphereGrid_LoadPanelTables 0x654810` first if they are null. It is self-contained, it
   allocates and fills both. That call has not been tested at runtime yet.
2. **`FFX_SaveData_GetCharName 0x384FB0` is unsafe above index 17.** Its guard is
   `FFX_Btl_IsAllyUnit`, which is only `index <= 30`, but `CharNames` has 18 entries, so 18..30 read
   into the save-block CRC and past it. Bound the picker yourself.

## 2. The character roster

18 records, indices 0..17, stride 148, at `CharRecords` RVA `0x00D3205C` = `SaveData+0x55CC`.
`FFX_SaveData_GetCharRecord 0x385330` is `index <= 0x11 ? base + 148 * index : NULL`.

| index | who | what the UI needs to know |
|---|---|---|
| 0 | Tidus | |
| 1 | Yuna | the ten aeons' stats are derived from hers, see section 3 |
| 2 | Auron | |
| 3 | Kimahri | |
| 4 | Wakka | |
| 5 | Lulu | `RecomputeCharDerived` also mirrors her ability ids `0x3041`..`0x3053` into `0x3078`..`0x308A` |
| 6 | Rikku | ships as `"????"` in `ply_save.bin`, the story replaces it. Also the only character whose chr id changes, to 41 instead of 7, when `SaveData+0xD1` is non-zero |
| 7 | Seymour | has a full record, a name and two starting equipment slots, but `w_name.bin` only carries 7 per-character name columns, 0..6, so Seymour cannot be the `forChar` of a normal equipment piece |
| 8..17 | Valefor, Ifrit, Ixion, Shiva, Bahamut, Anima, Yojimbo, Cindy, Sandy, Mindy | stats are recomputed from Yuna's every time, see section 3. Index 11, Shiva, also ships as `"????"` |

**There is no Jecht index and no eighteenth playable character.** `ply_save.bin` has 20 rows but 18
and 19 have empty names and are never copied into the block, because the copy loop in
`FFX_InitNewSaveData` stops at `CharAbility`, which is record 18. `CharIndexToChrId 0x008423A0` is
18 entries as well.

Names live in the block, not in a table. `CharNames` RVA `0x00D32DDC` = `SaveData+0x634C`, 18
records of 20 bytes, charmap encoded. `FFX_SaveData_ReloadCharNames 0x387100` refills them from
`ply_save.bin` row `+0`, and `FFX_SaveData_ReencodeCharNames 0x387470` re-encodes them for the
running language after a save load. Writing a name is an 18-byte charmap write plus `[19] = 1` to
mark it player-renamed.

The renameable defaults, for a "reset name" button, are `name_txt.bin` rows 13..23: Tidus, Valefor,
Ifrit, Ixion, Shiva, Bahamut, Anima, Yojimbo, Cindy, Sandy, Mindy.

## 3. The character record and the stat layout

One record is 148 = 0x94 bytes. `CharRecords` RVA `0x00D3205C` = `SaveData+0x55CC`, 18 of them,
ending at `SaveData+0x6034`.

Everything in the table below was read out of `FFX_SaveData_RecomputeCharDerived 0x3860F0`,
`FFX_SaveData_GetCharBaseStats 0x385B60`, `FFX_SaveData_AddAp 0x384610` and
`FFX_SaveData_SpendSphereLevels 0x386EF0`, and cross-checked against `ply_save.bin`, which is a
row-for-row 148-byte template of this record (`FFX_InitNewSaveData` does a literal
`qmemcpy(record, row, 0x94)`).

| off | size | signedness | field | base or derived |
|---|---|---|---|---|
| 0x00 | 2 | - | in `ply_save.bin` this is the name string offset. Nothing in the engine reads it out of the live record | neither |
| 0x02 | 2 | - | no reader found | neither |
| 0x04 | 4 | signed int | **base max HP** | BASE, write this |
| 0x08 | 4 | signed int | **base max MP** | BASE, write this |
| 0x0C | 1 | unsigned | **base Strength** | BASE |
| 0x0D | 1 | unsigned | **base Defense** | BASE |
| 0x0E | 1 | unsigned | **base Magic** | BASE |
| 0x0F | 1 | unsigned | **base Magic Defense** | BASE |
| 0x10 | 1 | unsigned | **base Agility** | BASE |
| 0x11 | 1 | unsigned | **base Luck** | BASE |
| 0x12 | 1 | unsigned | **base Evasion** | BASE |
| 0x13 | 1 | unsigned | **base Accuracy** | BASE |
| 0x14 | 4 | signed int | total AP earned, clamped 0..999,999,999 | BASE |
| 0x18 | 4 | signed int | AP banked toward the next sphere level, clamped 0..999,999,999 | BASE |
| 0x1C | 4 | signed int | **current HP**, re-clamped to 0..record+0x24 on every recompute | derived-ish, see below |
| 0x20 | 4 | signed int | **current MP**, re-clamped to 0..record+0x28 | derived-ish |
| 0x24 | 4 | signed int | **effective max HP** | DERIVED, do not write |
| 0x28 | 4 | signed int | **effective max MP** | DERIVED, do not write |
| 0x2C | 1 | bitfield | party flags. Bit 0 is "in the party". Use `FFX_SaveData_SetCharInParty 0x3869B0`, not a raw write | state |
| 0x2D | 1 | unsigned | **weapon equipment slot, the low byte of the slot id, 0xFF = none** | state |
| 0x2E | 1 | unsigned | **armour equipment slot, same convention** | state |
| 0x2F | 1 | unsigned | effective Strength | DERIVED |
| 0x30 | 1 | unsigned | effective Defense | DERIVED |
| 0x31 | 1 | unsigned | effective Magic | DERIVED |
| 0x32 | 1 | unsigned | effective Magic Defense | DERIVED |
| 0x33 | 1 | unsigned | effective Agility | DERIVED |
| 0x34 | 1 | unsigned | effective Luck | DERIVED |
| 0x35 | 1 | unsigned | effective Evasion | DERIVED |
| 0x36 | 1 | unsigned | effective Accuracy | DERIVED |
| 0x37 | 1 | - | 25 for every row in the template. No reader found | - |
| 0x38..0x3A | 3 | - | copied back from battle actor+1467..1469 at the end of a battle, overdrive related. Template values are per-character: 2 for Tidus / Yuna / Wakka / Lulu, 12 Auron, 6 Kimahri, 25 Rikku, 0 Seymour and the aeons | state |
| 0x3B | 1 | unsigned | **available sphere levels (S.Lv)**. `AddAp` stops granting at 99 and clamps to 0..99 | state |
| 0x3C | 1 | unsigned | spent sphere levels, clamped 0..101 by `SpendSphereLevels` | state |
| 0x3D | 1 | - | revive / death countdown carried out of battle | state |
| 0x3E..0x49 | 6 words | bitfield | per-character ability flags for ability ids `0x3000`..`0x305F`. `FFX_SaveData_TestAbilityFlag 0x385020` reads bit `(id & 0xF)` of word `(id & 0xFFF) / 16`, `FFX_SaveData_SetAbilityFlag 0x385C50` writes it. **`RecomputeCharDerived` ORs the sphere-grid bonus cache into these and never clears them**, so a value written here sticks until the next new game or save load | mixed |
| 0x4A | 2 | bitfield | **auto-ability flags word 0.** CLEARED and rebuilt by every recompute from the equipped weapon's and armour's four auto-abilities. `a_ability` row `+98` is the source | DERIVED, do not write |
| 0x4C | 2 | bitfield | **auto-ability flags word 1.** Same. **Bit 0x200 is Break HP Limit and bit 0x400 is Break MP Limit**, and they are what pick the 99999 / 9999 clamps | DERIVED |
| 0x4E | 2 | bitfield | **auto-ability flags word 2.** Same. Bit 1 is No Encounters, which `RecomputeCharDerived` ANDs into the global `EncountersEnabled` for party members | DERIVED |
| 0x50, 0x54, 0x58, 0x5C | 4 dwords | signed int | per-character battle counters, clamped to 999,999,999 by `FFX_SaveData_BumpCharBattleCounter 0x385A00` | state |
| 0x60..0x81 | 17 words | - | per-character, non-zero for 0..6 and 0xFFFF for the aeons. Tidus is 150, 300, 0, 80, 75, 100, 250, 100, 100, 50, 120, 120, 600, 600, 120, 170, 60. **No reader identified.** The shape and the per-character spread look like Overdrive-mode learn thresholds, which is a guess, not a finding | - |
| 0x82..0x93 | 18 | - | mostly 0xFF for 0x82..0x87 on the playable characters and 0xFF across 0x60..0x85 for the aeons. **No reader identified** | - |

**A correction to the IDB.** The comment already on `FFX_SaveData_RecomputeCharDerived` says the
Break HP Limit test is `record+0x40 bit 0x200`. It is **`record+0x4C`**. The decompiler expression is
`*((_WORD *)v1 + 38) & 0x200`, and WORD index 38 is byte offset 76 = 0x4C. Cross-checked the other
way as well: `a_ability` row 23, Break HP Limit, has `0x0200` in its `+100` field, and `+100` is the
field that gets ORed into `record+0x4C`.

### Which one a cheat must write

**Write the BASE fields: `+0x04`, `+0x08` and `+0x0C..+0x13`. Then call
`FFX_SaveData_RecomputeAllCharDerived 0x386900`.** That is the only combination that survives, because

* `+0x24`, `+0x28` and `+0x2F..+0x36` are recomputed from the base fields plus the equipment bonus
  plus the sphere-grid bonus cache on every recompute, and a recompute happens on every save load,
  every equipment change and every sphere-grid node activation. Writing them directly looks right
  until the next menu action.
* `+0x4A`, `+0x4C` and `+0x4E` are explicitly zeroed at the top of the per-character recompute and
  rebuilt from the equipped gear, so writing them is pointless. To grant Break HP Limit, put ability
  `0x8017` in an equipped armour's ability slot.

`FFX_SaveData_RecomputeAllCharDerived 0x386900` is `void (void)`. It sets the global
`EncountersEnabled` to 1 and then calls `FFX_SaveData_RecomputeCharDerived 0x3860F0` for all 18
records. It clobbers exactly the DERIVED rows above plus the four bytes at
`CharDerivedBytes 0x1F11240` (outside the block) and the `EncountersEnabled` global. It does **not**
touch base stats, AP, sphere levels, equipment slots or the ability bitmap. So it is safe to call
after any base-stat write.

If the grid or the equipment also changed, call `FFX_SphereGrid_RecomputeDerived 0x654860` instead.
It zeroes the equip-stat-bonus cache, rebuilds it from the grid and the auto-abilities, and then
calls `RecomputeAllCharDerived` itself. That is the single function to run after transplanting a
whole block.

### The caps the game enforces

| thing | cap | where |
|---|---|---|
| base max HP / MP | storage is a signed 32-bit int, no clamp on write | - |
| effective max HP | `clamp(0, 9999)`, or `clamp(0, 99999)` with `record+0x4C & 0x200` | `0x3860F0` |
| effective max MP | `clamp(0, 999)`, or `clamp(0, 9999)` with `record+0x4C & 0x400` | `0x3860F0` |
| base stats | one unsigned byte each, so 0..255 by storage | - |
| effective stats | `clamp(1, 255)`, except Evasion which is `clamp(0, 255)` | `0x3860F0` |
| current HP | `clamp(0, effective max HP)` | `0x3860F0` |
| current MP | `clamp(0, effective max MP)` | `0x3860F0` |
| total AP and banked AP | `clamp(0, 999999999)` | `0x384610` |
| available S.Lv | `AddAp` refuses to grant past 99 and clamps to `clamp(0, 99)` | `0x384610` |
| spent S.Lv | `clamp(0, 101)` | `0x386EF0` |
| gil | `clamp(0, 999999999)`, and a negative argument stores 0 | `0x385C20` |
| item count per slot | `clamp(0, 99)` | `0x390550` |

So the practical ceiling without Break HP Limit is 9999 HP and 999 MP no matter what you write into
the base fields, and the practical stat ceiling is 255 because the derived byte cannot hold more.

### Where the equipment and grid bonuses come from

`FFX_SaveData_GetCharBaseStats 0x385B60` is `char * (unsigned index, int *out)` and fills ten
dwords: `out[0..7]` the eight stats in the `+0x0C` order, `out[8]` max HP, `out[9]` max MP. It folds
in the equip-stat-bonus cache, which is `EquipStatBonus 0x00D35E00`, 8 entries of 28 bytes, indexed
by `FFX_SaveData_GetEquipStatBonus 0x3987F0` and **outside the save block**:

```
out[8] += 50 * *(int*)(bonus + 0)      HP, in units of 50
out[9] +=  5 * *(int*)(bonus + 4)      MP, in units of 5
out[0..7] += bonus[8 .. 15]            one byte per stat, +1 each
bonus[16 .. 27]                        6 words of ability flags, ORed into record+0x3E..0x49
```

That cache is built entirely from the sphere grid by `FFX_SphereGrid_RecomputeDerived 0x654860`, so
"the grid gave me this much Strength" lives there, not in the record.

The auto-ability percentage pass is separate and happens inside `RecomputeCharDerived`. For every
non-empty auto-ability slot on the equipped weapon and armour it reads the `a_ability` row's `+85`
BYTE percentage and `+86` WORD bucket mask and accumulates into 14 buckets, then applies
`value += pct * value / 100`. The buckets are

| mask bit | bucket | effect |
|---|---|---|
| 0x0001..0x0080 | the eight stats in the `+0x0C` order | no shipped auto-ability uses these |
| 0x0100 | max HP | `HP +5/10/20/30%`, ability ids `0x8072`..`0x8075` |
| 0x0200 | max MP | `MP +5/10/20/30%`, ids `0x8076`..`0x8079` |
| 0x0400 | attack power | `Strength +3/5/10/20%`, ids `0x8062`..`0x8065` |
| 0x0800 | magic power | `Magic +3/5/10/20%`, ids `0x8066`..`0x8069` |
| 0x1000 | physical defence | `Defense +3/5/10/20%`, ids `0x806A`..`0x806D` |
| 0x2000 | magical defence | `Magic Def +3/5/10/20%`, ids `0x806E`..`0x8071` |

**Bits 0x0400 through 0x2000 land in stack slots that `RecomputeCharDerived` never reads back.** The
array it accumulates into is the ten-dword output of `GetCharBaseStats` plus four scratch dwords, and
only indices 0..9 are copied out. So the `+X%` attack and defence auto-abilities do not change the
record at all and whatever applies them must be in the battle damage path. Practical consequence for
the UI: it cannot preview those four families as a stat change, because the game does not model them
that way either. HP% and MP% do change `record+0x24` and `+0x28`.

### The ten aeons, indices 8..17

For `index >= 8` the recompute **ignores the aeon's own stat bytes as a base** and derives the stats
from Yuna's (`GetCharBaseStats(1, ...)`), scaled by coefficients in the aeon's `ply_rom.bin` row
(bytes `+24` through `+41`), then floored by a row of the asset-class-13 index-14 table chosen by
`SaveData+0x3D14 / 30` capped at 20, which is the aeon power counter the "Aeon's Soul" key item
feeds. The aeon's own `+0x04`, `+0x08` and `+0x0C..+0x13` are then **added on top** as a bonus.

So editing an aeon's base fields does work, it is additive rather than absolute, and it will not
survive a change to Yuna's stats in the sense that the total moves with her.

### The AP curve

`FFX_SaveData_ApNeededForNextLevel 0x384E90` is `unsigned (int charIndex)`:

```
total = record[0x3B] + record[0x3C];
row   = KernelTableGetRow(charIndex, plyRomTable);   // plyRomTable = *(short**)0x00D2A938
if (total > 100) return *(uint32_t*)(row + 20);      // 22000 for every playable character
return (total*total     * row[18]) / 10
     + (total*total*total * row[17]) / 100
     + (total + 1)      * row[19];
```

For every playable character `ply_rom.bin` has `row[17] = 2`, `row[18] = 0`, `row[19] = 5` and the
fixed cost `22000`. `FFX_SaveData_AddAp 0x384610` adds to `+0x14` and `+0x18` and then converts
`+0x18` into sphere levels a level at a time, which is the only AP-to-S.Lv path in the binary.

## 4. Equipment

`EquipmentArray` RVA `0x00D30F2C` = `SaveData+0x449C`. **200 entries of 22 bytes**, verified three
ways: `0x449C + 200 * 22 = 0x449C + 0x1130 = 0x55CC`, which is exactly where the character records
start; `FFX_SaveData_GetEquipEntry 0x3ABBD0` rejects `(slotId & 0xFFF) >= 0xC8`; and
`FFX_SaveData_AddEquipEntry 0x3AB910` walks until the pointer reaches `CharRecords`.

There are two 8-entry scratch arrays outside the block for the menu's preview rows, selected by the
slot id's top nibble: nibble 7 gives `0x1F10F9E` and nibble 11 gives `0x1F10DDE`. Anything else,
including the usual nibble 5, selects the main array at `index = slotId & 0xFFF`. A slot id of
exactly `0xFF` means "no equipment" and returns NULL.

### 4.1 The 22-byte entry

| off | size | field |
|---|---|---|
| 0x00 | 2 | **name id**, `0x5000`-based, a row index into `w_name.bin`. Derived from the ability set by `FFX_Equip_ComputeNameId 0x3A0CF0`, so it is not independent data |
| 0x02 | 1 | **in use.** 0 means the slot is free. `AddEquipEntry` sets 1, `RemoveEquipEntry` clears it |
| 0x03 | 1 | **flags.** Bit 0x02 = cannot be customised. Bit 0x04 = this character's Celestial Weapon, which forces name id `0x5000`. Bit 0x08 = Celestial Armour, name id `0x5001`. `FFX_Menu_CustomizeIsEntryEligible 0x4D5750` refuses 0x02 always and 0x04 / 0x08 unless forced |
| 0x04 | 1 | **for character**, 0..6. Selects the per-character column in `w_name.bin` and the 3D model. Not the same thing as the owner |
| 0x05 | 1 | **kind.** 0 = weapon, non-zero = armour. This is the byte `ComputeNameId` branches on |
| 0x06 | 1 | **owner**, the character record index currently equipping it, `0xFF` = unowned. Maintained by `FFX_SaveData_SetCharEquip 0x3AB970` and rebuilt wholesale by `FFX_SaveData_RebuildEquipOwners 0x3ABE30` |
| 0x07 | 1 | **no reader and no writer found.** The new-game fill copies it verbatim out of the template table, the treasure builder does not write it at all |
| 0x08 | 1 | copied from equipment-template row `+4`. No reader found |
| 0x09 | 1 | copied from template `+5`. No reader found |
| 0x0A | 1 | copied from template `+6`. No reader found |
| 0x0B | 1 | **auto-ability slot count**, 1..4. `FFX_Equip_AddAbilityToEntry 0x4D5680` will not write past it, and `FFX_Menu_CustomizeIsEntryEligible` uses it to test for a free slot |
| 0x0C | 2 | **model / icon id**, written by `FFX_Equip_RefreshEntryNameId 0x3993B0` from `w_name` row `+56 + 2 * forChar`. Observed values are `0x40xx` |
| 0x0E | 2 | auto-ability slot 0 |
| 0x10 | 2 | auto-ability slot 1 |
| 0x12 | 2 | auto-ability slot 2 |
| 0x14 | 2 | auto-ability slot 3 |

**Four ability slots, fixed, and both 0 and 0xFF mean empty.** Every reader uses the same test,
`*slot != 0 && *slot != 255`, and every loop runs exactly four iterations:
`FFX_Equip_HasAbility 0x3A0C20`, `RecomputeCharDerived`, `FFX_Menu_StatusBuildAbilityList` and the
customise draw helper `0x4D63F0` all do it. `entry[0x0B]` is how many of the four the player is
allowed to use, not how many exist.

Three fields in the entry are unaccounted for, `+0x07` through `+0x0A`. They are copied around and
never read by anything I found. A cheat should preserve them rather than zero them.

### 4.2 How a character names a piece

`record[0x2D]` and `record[0x2E]` hold the slot as **one byte**, so the stored value is the array
index 0..199, with `0xFF` for none. `FFX_SaveData_SetCharEquip 0x3AB970` takes a `short`, masks it
with `0xFFF` and stores the low byte, so passing either `0x5000 + index` or a bare `index` works.

### 4.3 Attaching a piece to a character

**Use `FFX_SaveData_SetCharEquip 0x3AB970`**, signature
`int __cdecl (unsigned char charIndex, int which, short slotId)` with `which == 0` for the weapon
and non-zero for the armour. It sets the outgoing entry's `+6` back to `0xFF`, the incoming entry's
`+6` to `charIndex`, and then writes the record byte. Writing `record[0x2D]` by hand leaves two
stale owner bytes behind and the Equip screen will show the wrong thing.

It does **not** rebuild the stat bonus cache, so follow it with
`FFX_SaveData_RecomputeCharDerived 0x3860F0` or `FFX_SaveData_RecomputeAllCharDerived 0x386900`.
The menu's own wrapper for that is `0x4C3070`, which recomputes and then re-clamps current HP and MP.

### 4.4 Creating a piece from nothing

`FFX_SaveData_AddEquipEntry 0x3AB910` is `int __cdecl (EquipEntry *src)`. It finds the first entry
with `+2 == 0`, copies all 22 bytes in, forces `+2 = 1` and `+6 = 0xFF`, and returns the new slot id
`0x5000 + index`, or 0 when the array is full. So the caller supplies a fully formed 22-byte struct.

The engine's own recipe for filling that struct is in the treasure decoder `0x399420` case 5, which
is the canonical answer. Translated:

```c
EquipEntry e = {0};
e.inUse        = 1;            // +0x02
e.flags        = tmpl[0];      // +0x03
e.forChar      = tmpl[1];      // +0x04, 0..6
e.kind         = tmpl[2];      // +0x05, 0 weapon / non-zero armour
e.owner        = 0xFF;         // +0x06
e.b8           = tmpl[4];      // +0x08
e.b9           = tmpl[5];      // +0x09
e.b10          = tmpl[6];      // +0x0A
int used = 0;
for (int i = 0; i < 4; ++i) {
    uint16_t a = *(uint16_t*)(tmpl + 8 + 2*i);
    if (a == 0) a = 0xFF;      // 0 is normalised to 0xFF
    e.ability[i] = a;
    if (a != 0xFF) ++used;
}
e.slotCount    = max(tmpl[7], used);                 // +0x0B
e.nameId       = FFX_Equip_ComputeNameId(&e);        // +0x00
FFX_Equip_GetNameString(e.nameId, e.forChar, 0, &e.icon);   // fills +0x0C
int slotId = FFX_SaveData_AddEquipEntry(&e);
```

`tmpl` is a row of the 22-byte template table at RVA `0x00D35FFC`. If the UI is building a piece
from scratch instead, `flags = 0`, `+0x08..+0x0A = 0` and `slotCount` chosen by the user is fine,
and the only fields that must be right are `forChar`, `kind`, the four ability words and
`slotCount`.

`FFX_SaveData_RemoveEquipEntry 0x3ABCA0` is the opposite, `int (int slotId)`: it clears `+2` and
unequips the piece from whoever holds it.

### 4.5 Setting the auto-abilities

Two functions, and the order matters.

```c
FFX_Equip_AddAbilityToEntry(entry, abilityId);   // 0x4D5680
FFX_Equip_RefreshEntryNameId(entry);             // 0x3993B0
```

`FFX_Equip_AddAbilityToEntry 0x4D5680` is `char __cdecl (int entry, short abilityId)`. It walks the
four words from `+0x0E`, stops at the first 0 or 0xFF, and writes there, but it gives up if it has
already passed `entry[0x0B]` slots. So it only ever appends, it cannot replace a slot and it cannot
exceed the slot count. It calls `FFX_SaveData_RecomputeAllCharDerived` on the way out, which means
calling it is enough to refresh the derived stats. **To clear or replace a slot the cheat has to
write the word directly**, because no engine function removes an auto-ability.

`FFX_Equip_RefreshEntryNameId 0x3993B0` is `int __cdecl (int entry)`. It recomputes
`FFX_Equip_ComputeNameId(entry)` into `+0x00` and refreshes the icon word at `+0x0C`. **Call it
after any ability change**, otherwise the piece keeps the name it had, which in FFX is wrong by
definition because the name *is* a function of the ability set.

`FFX_Equip_ComputeNameId 0x3A0CF0` is a long decision tree over `FFX_Equip_HasAbility` tests. It
branches first on `entry[0x05]`: the weapon side returns name ids 20480..20550 (`0x5000`..`0x5046`)
and the armour side 20554..20638 (`0x504A`..`0x509E`). That split lines up exactly with `w_name.bin`
rows 0..73 being weapons and 74..169 armour, which is the cross-check that settles both.

`FFX_Equip_HasAbility 0x3A0C20` is `int __cdecl (EquipEntry *entry, short abilityId)`, a linear scan
of the four words. Use it rather than reimplementing the 0 / 0xFF terminator rule.

The engine's full customise commit, from `FFX_Menu_CustomizeStep 0x4D5830` state 13, also charges
the item:

```c
row = FFX_Equip_GetCustomizeTable(&count) + 8 * listIndex;   // 0x390A10
FFX_Equip_AddAbilityToEntry(entry, *(uint16_t*)(row + 2));
FFX_Equip_RefreshEntryNameId(entry);
FFX_SaveData_AddItem(*(uint16_t*)(row + 4), -(int)row[6]);   // 0x390550
```

A cheat does not have to charge the item, but the customise table is still the right source for
"which auto-abilities are legal on a weapon versus an armour", because it is the only table in the
game that says so. 125 rows, `+0` = 1 for weapon and 2 for armour.

## 5. Items and the inventory

### 5.1 The list

112 items, ids `0x2000`..`0x206F`, from the resident `item.bin` table at RVA `0x00D2A940`. Row `+0`
is the name offset and `+8` the description offset, both into the string block. Appendix A has the
full decoded list, which matches the known FFX item order exactly, from Potion at `0x2000` to
Winning Formula at `0x206F`. That match is the verification that both the table layout and the
charmap are right.

Key items are a separate id space, `0xA000`-based, 64 rows in `important.bin` at RVA `0x00D334C0`,
of which 0..51 are real. They are flags with no count.

### 5.2 The parallel arrays

```
ItemIds    RVA 0x00D3095C = SaveData+0x3ECC   WORD[256], 0x2000-based id, 255 = empty slot
ItemCounts RVA 0x00D30B5C = SaveData+0x40CC   BYTE[256], 0..99
```

Physically 256 slots each, but **every gameplay loop in the binary bounds at 112**, and both
`FFX_SaveData_GetItemIdArray 0x390530` and `FFX_SaveData_GetItemCountArray 0x390510` write 112 into
their out-count. `FFX_InitNewSaveData` does fill all 256 (ids 255, counts 0), so slots 112..255 exist
and are simply never visited. **Treat 112 as the capacity.**

The empty-slot marker is `255`, not `0`, and not `0xFFFF`. `0x20FF` is not a real item id so there
is no ambiguity, but a cheat that writes 0 into an id slot creates a slot the engine will treat as
item id 0.

### 5.3 Packed or sparse

**Packed.** `FFX_SaveData_AddItem 0x390550` searches for an existing slot with the id, and if it
does not find one it takes the first slot whose id is 255. When a count reaches zero it frees the
slot. It does not compact, so a hole left by a freed slot is reused by the next add, which means the
array is "packed up to the first 255" only loosely. What matters for a cheat:

* **Writing a sparse slot works** as far as `GetItemCount` and the two display lists are concerned,
  because both scan all 112 slots looking for the id rather than stopping at the first 255.
* **But `AddItem` will stop at the first free slot it finds**, so leaving holes is harmless.
* **Two slots with the same id is the one thing to avoid.** `AddItem` and
  `FFX_SaveData_GetItemCount 0x3904B0` both stop at the first match, so the second copy becomes
  unreachable and un-spendable.

The safe cheat path is `FFX_SaveData_AddItem 0x390550`, `int __cdecl (int itemId, int delta)`. It
requires `(itemId & 0xFFFFF000) == 0x2000` and `delta != 0`, clamps the resulting count to 0..99,
frees the slot at zero, sets the gained or lost mask bit and rebuilds both menu display lists.
Returns 0 on success and -1 on a rejected argument. To set a count to an exact value, read it with
`GetItemCount` and pass the difference.

A direct array write is fine too but then `FFX_SaveData_RebuildItemLists 0x3906A0` has to run, or the
Items menu will show a stale list.

`ItemInUse 0x00D2C948` is one process-wide "staged item" id that `GetItemCount` subtracts one for.
If the UI reads a count while a menu has an item staged it will read one low.

## 6. The sphere grid

### 6.1 What the save block holds

`SphereGridNodes` RVA `0x00D2EC7C` = `SaveData+0x21EC`, reached by
`FFX_SaveData_GetSphereGridNodes 0x384F40` which is a one-line `return &g_ffxSphereGridNodes`.
The whole run is **4896 bytes**, the size `FFX_SphereGrid_InitNodes 0x653DE0` memsets. Three
functions touch it and between them they account for all of it except a tail:

| offset in blob | size | what |
|---|---|---|
| `+0x0000` | 2560 | **the node array**, 1280 slots of 2 bytes. `[2i]` = panel kind, `[2i+1]` = activation mask |
| `+0x0A00` | 1280 | **the link array**, one byte per grid line, a per-character mask |
| `+0x0F00` | 14 | **7 WORDs, each character's current node index.** Character order 0..6 |
| `+0x0F0E` | 10 | not written by any of the three |
| `+0x0F18` | 1 | grid id, 0 / 1 / 2. Drives the camera x offset, 0 / -3640 / -7281 |
| `+0x0F19` | 1 | zoom level 0..3, mapping to scale 1.0 / 0.5 / 0.25 / 0.125 |
| `+0x0F1A` | 1030 | **no reader and no writer found.** Slack |

**Node slot `[2i]` is the panel kind**, a row index into `panel.bin`, and `0xFF` means the node is
empty, which is what a Clear Sphere leaves behind. `FFX_SaveData_GetSphereGridNodes`'s menu mirror
turns `0xFF` into `0xFFFF`, so the two spellings of "nothing here" are `0xFF` in the save and
`0xFFFF` in the menu.

**Node slot `[2i+1]` is the activation mask**, bit `c` set meaning character `c` in 0..6 has
activated this node. That single bit is the whole record of activation. There is no separate
"activated" list and no per-character node list. Everything the character gains from the grid is
recomputed from these bits, which is what makes the grid cheap to cheat and expensive to get wrong.

Two warnings about the bounds:

1. **`FFX_SphereGrid_RecomputeDerived 0x654860` only scans slots 0..1023**, not 1280. Setting a bit
   in slot 1024 or above does nothing to a character's stats, but the drawing code uses the node
   count from the layout file and will still show it.
2. **`InitNodes` memsets the blob to zero and then writes only as many kinds as the layout file
   lists.** Slots past the real node count keep kind 0, and panel row 0 is "Lv. 3 Lock", whose mask
   is `0x8000`, so it adds nothing to any stat. That is luck rather than design. A cheat that sets
   all 1024 masks to `0x7F` is safe for stats today, but **bound the loop to the real node count**,
   which is the WORD at `+4` of the layout asset, or `*(short*)(g_ffxMenuWork + 2)` once the menu
   has run.

The **link array** is the per-character trail, one byte per line on the grid, bit `c` for character
`c`. The count comes from the layout file too, the WORD at `+6`. It is cosmetic as far as stats go,
`RecomputeDerived` never reads it. `FFX_Menu_SphereGridActivateNode 0x656160` clears a character's
bit on every link flagged in its menu record, so the exact set-and-clear rule is not fully pinned
down, and I did not find the function that sets a bit. A cheat that activates nodes without touching
this array leaves the trail lines grey. The nodes themselves still light up, because those are drawn
from the node mask.

### 6.2 Activating a node from a cheat

For one character `c` and node `i`:

```c
uint8_t *nodes = FFX_SaveData_GetSphereGridNodes();   // 0x384F40
if (nodes[2*i] != 0xFF)
    nodes[2*i + 1] |= (1 << c);
FFX_SphereGrid_RecomputeDerived();                    // 0x654860, rebuilds everything
```

`FFX_SphereGrid_RecomputeDerived` is the one call that has to follow. It clears the per-character
bonus cache, rescans all 1024 node slots for all 7 characters, re-totals the ten buckets, re-applies
every ability a grid node grants, and then calls `FFX_SaveData_RecomputeAllCharDerived 0x386900`
itself. Nothing else needs calling. Writing a node bit without it leaves the character's stats at
their old values until the next save load or the next visit to the grid screen.

To put a specific panel into an empty node, write `nodes[2*i]` to the panel row index first. That is
exactly what a sphere item does.

### 6.3 Sphere levels

```
FFX_SaveData_GetSphereLevels   0x3853E0   int (unsigned char charIndex)
FFX_SaveData_SpendSphereLevels 0x386EF0   int (unsigned char charIndex, int count)
```

The getter is one byte, `record[0x3B]`, the available S.Lv count. No bounds check on the index.

The setter is the only thing in the binary that moves sphere levels. It reads `record[0x3B]`,
**returns -1 without writing anything when `available - count` would go negative**, otherwise
subtracts `count` from `record[0x3B]` and adds it to `record[0x3C]`, the lifetime spent counter,
clamped to 0..101 by `FFX_ClampInt`. Note the clamp is on the **spent** counter only, so a cheat
that hands out levels should write `record[0x3B]` directly rather than trying to go through this.

`record[0x3B]` is a byte, so the storage ceiling is 255 available levels. Its only legitimate writer
is the AP level-up path in `FFX_SaveData_AddAp 0x384610`, and that one stops at 99, so anything above
99 only arrives from a cheat.

The single caller of SpendSphereLevels is `FFX_Menu_SphereGridActivateNode 0x656160`, which charges
the character at `g_ffxMenuWork + 71100` the cost at `g_ffxMenuWork + 71196`.

### 6.4 The node kind table

`panel.bin`, 127 rows of 24 bytes, pointer global RVA `0x016860E0`, accessed with
`FFX_KernelTable_GetRow`. Eight WORD string offsets at `+0` through `+0x0E` for the display
variants, use `+0` for the name. Then:

| off | size | meaning |
|---|---|---|
| 0x10 | 2 | stat mask, below |
| 0x12 | 2 | granted ability id, `0x3000`-based, 0 for none |
| 0x14 | 1 | amount |
| 0x16 | 1 | category, for the menu's grouping |

The mask bits, read straight off `RecomputeDerived`:

| bit | stat | how the amount is applied |
|---|---|---|
| 0x0001 | Strength | `+amount`, total clamped to 255 |
| 0x0002 | Defense | `+amount`, clamped 255 |
| 0x0004 | Magic | `+amount`, clamped 255 |
| 0x0008 | Magic Def | `+amount`, clamped 255 |
| 0x0010 | Agility | `+amount`, clamped 255 |
| 0x0020 | Luck | `+amount`, clamped 255 |
| 0x0040 | Evasion | `+amount`, clamped 255 |
| 0x0080 | Accuracy | `+amount`, clamped 255 |
| 0x0100 | max HP | `+amount`, kept as a DWORD, multiplied by 50 when folded in |
| 0x0200 | max MP | `+amount`, kept as a DWORD, multiplied by 5 when folded in |
| 0x8000 | lock | a Lv.1 to Lv.4 lock, grants nothing |

Bit 0x0400 is the "grants an ability" marker as far as the menu is concerned, but `RecomputeDerived`
does not test it. It calls `FFX_SaveData_SetEquipStatBonusAbility 0x398840` with `+0x12`
unconditionally, and that function ignores anything that is not `0x3xxx`. So a node grants an
ability if and only if `+0x12` is a `0x3xxx` id.

Spot checks against the real game: row 2 is "Strength +1" with mask `0x0001` and amount 1, row 0 is
"Lv. 3 Lock" with mask `0x8000`, row 1 is "Empty Node" with mask 0. "HP +200" has mask `0x0100` and
amount 4, and 4 x 50 = 200. "MP +40" has mask `0x0200` and amount 8, and 8 x 5 = 40. That is the
multiplier confirmed from both directions.

The per-character output is `EquipStatBonus` RVA `0x00D35E00`, **28 bytes per character for
characters 0..7**, from `FFX_SaveData_GetEquipStatBonus 0x3987F0`:

```
+0x00 DWORD  HP bonus, x50 when folded into the record
+0x04 DWORD  MP bonus, x5
+0x08 BYTE   Strength
+0x09 BYTE   Defense
+0x0A BYTE   Magic
+0x0B BYTE   Magic Def
+0x0C BYTE   Agility
+0x0D BYTE   Luck
+0x0E BYTE   Evasion
+0x0F BYTE   Accuracy
+0x10 WORD[6] ability bitmap, ids 0x3000..0x305F, word (id&0xFFF)/16 bit (id&0xF)
```

This cache is **outside the save block**, so a co-op transplant of the block has to rebuild it, and
`FFX_SphereGrid_RecomputeDerived` is the call that does it.

### 6.5 Where the grid itself is

Not in `.text`, and not in the kernel tables. The topology comes out of the **asset loader, class
37**, and which index depends on the grid the save is using:

```
gridType = (*(int*)0x00D3079C >> 14) & 3;      // SaveData+0x3D0C, bits 14 and 15
```

| gridType | layout asset | node kinds asset | what it is |
|---|---|---|---|
| 0 | class 37 index 9 | class 37 index 17 | Standard |
| 1 | class 37 index 10 | class 37 index 18 | the second grid |
| 2 | class 37 index 11 | class 37 index 19 | the third grid |

**The layout asset, read by `FFX_SphereGrid_LoadLayout 0x645570`:**

```
+0  BYTE  magic, must be 49
+2  WORD  countA     ->  countA records of 16 bytes
+4  WORD  nodeCount  ->  nodeCount records of 12 bytes
+6  WORD  linkCount  ->  linkCount records of 8 bytes
```

The 12-byte node record carries the screen position as two shorts at `+0` and `+2` and the panel
kind as a WORD at `+6`. The 8-byte link record is `WORD nodeA, WORD nodeB` plus four more bytes.
**That link list is the grid's adjacency**, and the loader validates it, dropping any link where
`nodeA == nodeB` or either index is past `nodeCount`.

**The node kinds asset, read by `FFX_SphereGrid_InitNodes 0x653DE0` and the trophy check
`0x647210`:**

```
+0  WORD  magic, must be 49
+2  WORD  nodeCount
+4  BYTE[nodeCount]  the panel row for each node, 0xFF for an empty node
```

The buffer the engine reads it into is 1144 bytes, so that is the hard ceiling on the node count.
This is the file `InitNodes` copies into the save blob's low bytes on a new game, which is why the
save holds the kinds at all: they are a mutable copy that Clear Spheres and attribute spheres edit.

The starting state is in `.rdata` and it is per grid:

| what | address | contents |
|---|---|---|
| start node per character, Standard | `0x00886C00` | 0, 341, 540, 637, 56, 224, 448 |
| start node per character, grid 1 | `0x00886C10` | same seven |
| start node per character, grid 2 | `0x00886C20` | 43, 9, 38, 13, 24, 19, 80 |
| pre-activated nodes, Standard | `0x00886C5C` | 7 pointers to `0xFFFF`-terminated WORD lists. Tidus none, Yuna 335 336, Auron 541, Kimahri 636, Wakka 57, Lulu 220..223, Rikku 449 450 |
| pre-activated nodes, grid 1 | `0x00886CA4` | same |
| pre-activated nodes, grid 2 | `0x00886CEC` | Tidus none, Yuna 0 8, Auron 39, Kimahri 13, Wakka 31, Lulu 14..17, Rikku 40 41 |

`InitNodes` also loads 70 files from `/ffx_ps2/ffx/eiichi_abmap_data`, names at `0x00885EF0`. **69 of
the 70 are art**, `omd` models, `an2` animations and two `OMD` grade textures. The only data file is
`par/dna.pdt`, 67796 bytes, and it gets handed to the model scene builder, so it is the 3D board
rather than the logical grid. **`tools/` has no reader for `pdt`**, and nothing in the cheat needs
one, because the logical grid is the class 37 layout asset above.

The gap that remains: **I could not resolve a class-37 index to a filename.** The asset loader
(`g_ffxAssetLoader` VA `0x2310C40`, getter `FFX_GetAssetLoader 0x36D0D0`) resolves an index through
the runtime cdidx, `ffx_ps2/ffx/proj/prog/cdidx/cdrom.mdg`, 131072 bytes, and I did not crack that
format. So **the grid layout can be read at runtime through the asset loader but not offline from
the vbf.** For a plugin that is no obstacle, because the engine has already parsed it into the menu
work area by the time the UI runs.

### 6.6 The parsed copy, if the UI wants positions

`g_ffxMenuWork` RVA `0x01F05834`, valid once menu module 19 has built the grid screen once.

```
+0x0002  short  nodeCount
+0x0004  short  linkCount
+0x0808  node records, 40 bytes each:  +0 short x, +2 short y, +6 WORD panel kind
                                       (0xFFFF for empty), +32 WORD whose high byte
                                       is the activation mask
+0xA808  link records, 20 bytes each:  +0 WORD nodeA, +2 WORD nodeB,
                                       +8 WORD cellA, +10 WORD cellB,
                                       +12 BYTE per-character trail mask,
                                       +14 BYTE transient flags
+0x110CC WORD at +0x110CC + 80*c is character c's current node index
```

`FFX_SphereGrid_SaveToMenu 0x649590` copies the save blob into this, and
`FFX_SphereGrid_MenuToSave 0x65BB70` copies it back. **If the UI edits the save blob while the grid
screen is up, the menu copy wins on the way out.** Edit the blob when the grid screen is not open,
or edit both.

### 6.7 What is not settled about the grid

* Which function sets a bit in the link trail array. Only the clear is accounted for.
* The 10 bytes at blob `+0x0F0E` and the 1030 at `+0x0F1A`.
* `0x016860E0` and `0x016860E4`, the `panel` and `sphere` table pointers, are null until the grid
  screen has run. `FFX_SphereGrid_LoadPanelTables 0x654810` looks self-contained and should fill
  both, but **I have not tested calling it from outside the menu**.

## 7. Gil, and the two ability id spaces

### 7.1 Gil

```
FFX_SaveData_GetGil   0x384E80   int (void)              returns SaveData+0x3D48
FFX_SaveData_SetGil   0x385C20   int (int amount)        clamps to 0..999999999
FFX_SaveData_SpendGil 0x3859A0   int (int cost)          shops. A negative cost adds
```

The value is a plain DWORD at `SaveData+0x3D48`, RVA `0x00D307D8`. The setter is the cap: a negative
argument stores 0, anything above 999,999,999 stores 999,999,999. `SetGil` returns its argument
rather than the stored value, so do not read the return as a confirmation.

All three already exist in `GameState.h`.

### 7.2 The two id spaces are different, and this matters for the UI

**Known abilities, what a character can use in battle, are `0x3xxx`.** The table is `command.bin`,
320 rows of 96 bytes, pointer global RVA `0x00D2A92C`. Storage is the per-character bitmap
`CharAbility` RVA `0x00D32AC4` = `SaveData+0x6034`, **44 bytes per character for 18 characters**,
treated as 22 WORDs:

```c
// FFX_SaveData_TestCharAbility 0x385140
if ((id & 0xFFFFF000) != 0x3000) return 0;
return  *(uint16_t*)(CharAbility + 44*c + 2*((id & 0xFFF) / 16)) & (1 << (id & 0xF));
```

22 words covers ids `0x3000`..`0x315F`, and `command.bin` has 320 rows ending at `0x313F`, so the
bitmap is exactly big enough with a little room. Set with
`FFX_SaveData_SetCharAbility 0x385E00 (charIndex, abilityId, on)`.

**Auto-abilities, what sits in an equipment slot, are `0x8xxx`.** The table is `a_ability.bin`, 134
rows of 108 bytes, pointer global RVA `0x00D2A944`. Storage is the four WORDs in the equipment
entry. There is no bitmap, there is no per-character list, and an auto-ability exists only as long as
the piece of gear does.

So: **two tables, two id spaces, two storage mechanisms, and no overlap.** A UI that offers one list
for both would be wrong. The id's top nibble is the discriminator, and
`FFX_Btl_GetAbilityNameString 0x4B8D70` is the one name function that handles both, plus items and
monster abilities.

There is a third, smaller space that is easy to confuse with the first: the grid-granted ability
bitmap at `EquipStatBonus + 28*c + 0x10`, 6 WORDs covering `0x3000`..`0x305F`. That is a derived
cache, not storage. `RecomputeDerived` rebuilds it from the node masks on every call.

### 7.3 AP

`record[0x14]` DWORD is total AP earned, `record[0x18]` DWORD is AP toward the next sphere level.
Both are clamped to 999,999,999. `FFX_SaveData_ApNeededForNextLevel 0x384E90` is the curve and
`FFX_SaveData_AddAp 0x384610 (charIndex, ap)` is the only function that converts AP into
`record[0x3B]`. It spends `record[0x18]` one level at a time and **stops at `record[0x3B]` = 99**, so
it cannot be used to go past 99 available levels. A cheat that wants levels should either call AddAp
with a large number, which rolls both counters forward correctly, or write `record[0x3B]` and leave
AP alone. Writing both by hand is the way to get an inconsistent record.

## 8. THE ADDRESS TABLE

Everything this run established. RVAs, so the IDA VA is the RVA plus 0x400000. The "in" column says
which header already declares it, blank meaning it is new and went into
`loader/workshop/include/ffx/addresses/GameState.h` with this run.

### 8.1 The kernel table primitive and the name functions

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| `0x003AB870` | function | `KernelTableGetRow` | `char *__cdecl (int index, short *table, int *outStringBase)` | already in GameState.h. The one row accessor every table goes through |
| `0x003AB8F0` | function | `KernelTableRowCount` | `int __cdecl (int table, int rangeIndex)` | `hi - lo + 1` of descriptor `rangeIndex`. Pass 0. Use instead of a hardcoded count |
| `0x00382DF0` | function | `BtlReadKernelBin` | `unsigned __cdecl (const char *name, void *dest)` | reads `battle/kernel/<name>.bin`, returns the byte size |
| `0x00381D40` | function | `BtlLoadKernelTables` | `int __cdecl (int which)` | the six-case loader for command, ply_rom, a_ability, item, monmagic1, monmagic2 |
| `0x004B8D70` | function | `BtlGetAbilityNameString` | `int __cdecl (int id)`, a `char *` in practice | **the universal name function.** Handles 0x2xxx, 0x3xxx, 0x4xxx, 0x6xxx and 0x8xxx |
| `0x004C1A20` | function | `MenuGetAbilityName` | `const char *__cdecl (short id)` | in Battle.h |
| `0x004C19E0` | function | `MenuGetAbilityHelp` | `const char *__cdecl (short id)` | in Battle.h |
| `0x00390860` | function | `KeyItemGetNameString` | `int __cdecl (short id)`, a `char *` | `0xAxxx` key items |
| `0x003A0C50` | function | `EquipGetNameString` | `int __cdecl (short nameId, int charIndex, int variant, WORD *outIcon)` | per-character equipment name, also hands back the icon id |
| `0x003ABDF0` | function | `SaveDataGetEquipNameString` | `int __cdecl (int slotId)`, a `char *` | the same but from an owned slot id, which is what a list UI wants |

### 8.2 The resident table pointers

All are `short *` pointer globals suitable for `KernelTableGetRow`.

| RVA | kind | suggested name | rows x stride | note |
|---|---|---|---|---|
| `0x00D2A92C` | ptr | `BtlPlayerAbilityTable` | 320 x 96 | `command.bin`, ids `0x3000+`. In Battle.h |
| `0x00D2A930` | ptr | `KernelTableMonMagic1` | 300 x 92 | ids `0x4000+` |
| `0x00D2A934` | ptr | `KernelTableMonMagic2` | 247 x 92 | ids `0x6000+` |
| `0x00D2A938` | ptr | `KernelTablePlyRom` | 20 x 44 | per-character constants, the aeon derivation coefficients |
| `0x00D2A940` | ptr | `KernelTableItem` | 112 x 96 | `item.bin`, ids `0x2000+` |
| `0x00D2A944` | ptr | `BtlAbilityEffectTable` | 134 x 108 | `a_ability.bin`, ids `0x8000+`. In Battle.h |
| `0x00D2A964` | ptr | `EquipCustomizeTable` | 125 x 8 | `kaizou.bin`, the legal customise recipes |
| `0x00D334C0` | ptr | `KernelTableImportant` | 64 x 20 | `important.bin`, key items, ids `0xA000+` |
| `0x00D363B4` | ptr | `KernelTableWName` | 170 x 72 | `w_name.bin`, equipment names, ids `0x5000+` |
| `0x016860E0` | ptr | `KernelTablePanel` | 127 x 24 | `panel.bin`, sphere grid node kinds. **null until the grid screen has run** |
| `0x016860E4` | ptr | `KernelTableSphere` | 50 x 16 | `sphere.bin`, grid sphere effect text. Same caveat |

### 8.3 Equipment

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| `0x00D30F2C` | data | `EquipmentArray` | `EquipEntry[200]`, 22 bytes each | already in GameState.h. `SaveData+0x449C` |
| `0x003ABBD0` | function | `SaveDataGetEquipEntry` | `EquipEntry *__cdecl (int slotId)` | already there. Rejects index >= 200 |
| `0x003AB910` | function | `SaveDataAddEquipEntry` | `int __cdecl (EquipEntry *src)` | already there. Returns `0x5000 + index`, or 0 when full |
| `0x003ABCA0` | function | `SaveDataRemoveEquipEntry` | `int __cdecl (int slotId)` | already there |
| `0x003AB970` | function | `SaveDataSetCharEquip` | `int __cdecl (unsigned char charIndex, int which, short slotId)` | already there. **The attach path.** `which` 0 weapon, else armour |
| `0x003A0C20` | function | `EquipHasAbility` | `int __cdecl (EquipEntry *entry, short abilityId)` | already there |
| `0x003A0CF0` | function | `EquipComputeNameId` | `int __cdecl (EquipEntry *entry)` | the name is a function of the ability set. 0x5000..0x5046 weapon, 0x504A..0x509E armour |
| `0x003993B0` | function | `EquipRefreshEntryNameId` | `int __cdecl (EquipEntry *entry)` | in MenuSystem.h. Writes entry+0 and entry+0x0C |
| `0x004D5680` | function | `EquipAddAbilityToEntry` | `char __cdecl (EquipEntry *entry, short abilityId)` | in MenuSystem.h. Appends only, respects entry+0x0B, recomputes derived |
| `0x004BF720` | function | `EquipHasFreeAbilitySlot` | `BOOL __cdecl (EquipEntry *entry)` | tests the word at `entry + 2*entry[0x0B] + 12` for 0 or 0xFF |
| `0x004D5750` | function | `MenuCustomizeIsEntryEligible` | `int __cdecl (EquipEntry *entry, int force)` | the game's own "can this be customised" rule |
| `0x00390A10` | function | `EquipGetCustomizeTable` | `char *__cdecl (int *outCount)` | row 0 of `kaizou.bin` plus the row count. **This is the enumerable auto-ability list** |
| `0x00D35FFC` | data | `EquipTemplateTable` | `BYTE[22][]` | the pre-made pieces. Asset class 13 index 26 |
| `0x00D35FF8` | data | `EquipTemplateTableSize` | `DWORD` | byte size of the above |
| `0x00D35FF4` | data | `TreasureTable` | `BYTE[4][498]` | `takara.bin` equivalent, asset class 13 index 25 |
| `0x00D35FF0` | data | `TreasureTableSize` | `DWORD` | byte size |
| `0x00399420` | function | `TreasureGiveReward` | `void __cdecl (int kind, int arg)` | case 5 is the canonical build-an-equipment-entry recipe, section 4.4 |
| `0x00399020` | function | `LoadTreasureTables` | `int (void)` | fills the four globals above from asset class 13 |

### 8.4 The sphere grid

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| `0x00D2EC7C` | data | `SphereGridNodes` | `BYTE[4896]` | `SaveData+0x21EC`. Node array, link array, 7 positions, grid id, zoom |
| `0x00384F40` | function | `SaveDataGetSphereGridNodes` | `BYTE *__cdecl (void)` | one line, returns the above |
| `0x00654860` | function | `SphereGridRecomputeDerived` | `char __cdecl (void)` | in WorldState.h. **The one call after any grid or equipment edit** |
| `0x00653DE0` | function | `SphereGridInitNodes` | `char __cdecl (short *nodes)` | new game only. memsets 4896 and fills from the layout assets |
| `0x00654810` | function | `SphereGridLoadPanelTables` | `int __cdecl (void)` | allocates and fills both `0x016860E0` and `0x016860E4`. Call once, only if null |
| `0x00645570` | function | `SphereGridLoadLayout` | `int __cdecl (void)` | parses asset class 37 index 9/10/11 into the menu work area |
| `0x00649590` | function | `SphereGridSaveToMenu` | `int __cdecl (void)` | save blob into the menu's working copy |
| `0x0065BB70` | function | `SphereGridMenuToSave` | `int __cdecl (void)` | the menu's working copy back into the save blob |
| `0x00656160` | function | `MenuSphereGridActivateNode` | `int __cdecl (int, int, int mode)` | in MenuSystem.h. mode 0 activates and charges S.Lv, mode 1 moves |
| `0x003853E0` | function | `SaveDataGetSphereLevels` | `int __cdecl (unsigned char charIndex)` | already in GameState.h. `record[0x3B]` |
| `0x00386EF0` | function | `SaveDataSpendSphereLevels` | `int __cdecl (unsigned char charIndex, int count)` | already there. Returns -1 and writes nothing when short |
| `0x003987F0` | function | `SaveDataGetEquipStatBonus` | `char *__cdecl (unsigned index)` | already there. 28 bytes per character, index 0..7 |
| `0x00D35E00` | data | `EquipStatBonus` | `BYTE[8][28]` | already there. Derived, outside the save block |
| `0x00398840` | function | `SaveDataSetEquipStatBonusAbility` | `short __cdecl (unsigned charIndex, int abilityId)` | ORs a bit into the `+0x10` ability bitmap. Ignores anything not `0x3xxx` |
| `0x00398820` | function | `SaveDataClearEquipStatBonus` | `int __cdecl (void)` | in WorldState.h |
| `0x00D3079C` | data | `SaveDataOptionFlags` | `DWORD` | `SaveData+0x3D0C`. Bits 14..15 pick the grid, bit 3 picks the alternate name strings |
| `0x00886C00` | rdata | `SphereGridStartNodes` | `WORD[7]` | start node per character, Standard grid. `+0x10` and `+0x20` are the other two grids |
| `0x00886C5C` | rdata | `SphereGridStartActivated` | `WORD *[7]` | `0xFFFF`-terminated pre-activated node lists. `+0x48` and `+0x90` for the other grids |
| `0x00885EF0` | rdata | `SphereGridAssetNames` | `char *[70]` | the `eiichi_abmap_data` file names. 69 art, one data file, `par/dna.pdt` |
| `0x01F05834` | data | `MenuWork` | `void *` | in MenuSystem.h. The parsed grid lives at `+0x808` and `+0xA808`, see 6.6 |

### 8.5 Character records, names, items, gil

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| `0x00D3205C` | data | `CharRecords` | `BYTE[18][148]` | already in GameState.h |
| `0x00385330` | function | `SaveDataGetCharRecord` | `BYTE *__cdecl (int index)` | already there. NULL above 17 |
| `0x003860F0` | function | `SaveDataRecomputeCharDerived` | `int __cdecl (unsigned char charIndex)` | **the authority on the record layout.** One character |
| `0x00386900` | function | `SaveDataRecomputeAllCharDerived` | `int __cdecl (void)` | in MenuSystem.h. All 18 |
| `0x004C3070` | function | `MenuRecomputeCharAndClamp` | `char *__cdecl (int charIndex)` | recompute plus a re-clamp of current HP and MP. What the menu uses |
| `0x00D32DDC` | data | `CharNames` | `BYTE[18][20]` | already there. `SaveData+0x634C`, charmap text |
| `0x00387100` | function | `SaveDataReloadCharNames` | `int (void)` | refills all 18 from `ply_save.bin`. The reset-name path |
| `0x00387470` | function | `SaveDataReencodeCharNames` | `unsigned char __cdecl (void)` | in WorldState.h |
| `0x00384E90` | function | `SaveDataApNeededForNextLevel` | `unsigned __cdecl (int charIndex)` | the AP curve |
| `0x00384610` | function | `SaveDataAddAp` | `int __cdecl (unsigned char charIndex, int ap)` | already there. The only AP to S.Lv conversion |
| `0x003906A0` | function | `SaveDataRebuildItemLists` | `int (void)` | rebuilds the two menu display lists. Needed after a direct array write |
| `0x00390550` | function | `SaveDataAddItem` | `int __cdecl (int itemId, int delta)` | already there. The safe item path |
| `0x00384E80` | function | `SaveDataGetGil` | `int __cdecl (void)` | already there |
| `0x00385C20` | function | `SaveDataSetGil` | `int __cdecl (int amount)` | already there. Clamp 0..999999999 |

### 8.6 What went into GameState.h

The new constants, all added to `GameStateRvaList` as well: `KernelTableRowCount`,
`BtlReadKernelBin`, `BtlLoadKernelTables`, `BtlGetAbilityNameString`, `KeyItemGetNameString`,
`EquipGetNameString`, `SaveDataGetEquipNameString`, `KernelTableMonMagic1`, `KernelTableMonMagic2`,
`KernelTablePlyRom`, `KernelTableItem`, `EquipCustomizeTable`, `KernelTableImportant`,
`KernelTableWName`, `KernelTablePanel`, `KernelTableSphere`, `EquipComputeNameId`,
`EquipHasFreeAbilitySlot`, `MenuCustomizeIsEntryEligible`, `EquipGetCustomizeTable`,
`EquipTemplateTable`, `EquipTemplateTableSize`, `TreasureTable`, `TreasureTableSize`,
`TreasureGiveReward`, `LoadTreasureTables`, `SphereGridNodes`, `SaveDataGetSphereGridNodes`,
`SphereGridInitNodes`, `SphereGridLoadPanelTables`, `SphereGridLoadLayout`, `SphereGridSaveToMenu`,
`SphereGridMenuToSave`, `SaveDataSetEquipStatBonusAbility`, `SaveDataOptionFlags`,
`SphereGridStartNodes`, `SphereGridStartActivated`, `SphereGridAssetNames`,
`SaveDataRecomputeCharDerived`, `MenuRecomputeCharAndClamp`, `SaveDataReloadCharNames`,
`SaveDataApNeededForNextLevel`, `SaveDataRebuildItemLists`.

Left where they already live, with a pointer comment in GameState.h instead of a second
declaration: `MenuGetAbilityName`, `MenuGetAbilityHelp`, `BtlPlayerAbilityTable`,
`BtlAbilityEffectTable` (Battle.h), `EquipRefreshEntryNameId`, `EquipAddAbilityToEntry`,
`MenuSphereGridActivateNode`, `SaveDataRecomputeAllCharDerived`, `MenuWork` (MenuSystem.h),
`SphereGridRecomputeDerived`, `SaveDataClearEquipStatBonus`, `SaveDataReencodeCharNames`
(WorldState.h).

## 9. Verified, inferred, unsettled

**Verified in the disassembly, and in several cases cross-checked against the shipped data:**

* The kernel table header format, the row accessor and the row count accessor.
* The charmap. Proof is that `item.bin` decodes to the exact FFX item list in the exact order, and
  `a_ability.bin` row 23 decodes to "Break HP Limit" with a Wings to Discovery x30 recipe, which is
  what the game charges.
* Every resident table pointer in 8.2, each from the loader that writes it.
* The 148-byte character record layout and the base versus derived split, read off
  `SaveDataRecomputeCharDerived`.
* The 22-byte equipment entry, 21 of 22 bytes, and the four ability slots with their 0 and 0xFF
  terminators, from four independent readers that all agree.
* The entry construction recipe, from the treasure decoder that the game itself uses.
* The sphere grid blob layout, from the three functions that touch it plus the init.
* The panel table mask bits and the x50 and x5 multipliers, confirmed from both directions.
* Sphere levels at `record[0x3B]` and `record[0x3C]`, and the exact failure mode of
  `SpendSphereLevels`.
* Gil at `SaveData+0x3D48` with a 999,999,999 cap.
* That the known-ability space (`0x3xxx`, `command.bin`, 320 rows, a 44-byte per-character bitmap)
  and the auto-ability space (`0x8xxx`, `a_ability.bin`, 134 rows, equipment slots) are separate.
* `FFX_SphereGrid_RecomputeDerived` is RVA `0x00654860`. An earlier note of mine said `0x006548D0`,
  which is an address inside the function, not its entry. `WorldState.h` already had it right.

**Inferred, meaning the mechanism is clear but I did not watch it run:**

* That 200 is the equipment capacity. Three independent arguments agree (the array ends exactly
  where `CharRecords` begins, the getter rejects 200 and above, the add walks to `CharRecords`) but
  no single instruction says "200".
* That 112 is the practical item capacity, not 256. The arrays are physically 256 and the new-game
  fill writes all 256, but every loop and both array getters stop at 112.
* That `SphereGridLoadPanelTables 0x00654810` is safe to call from a plugin when the pointers are
  null. It is self-contained and it only needs the file system, but **calling it twice leaks two
  allocations** and I have not run it.
* The charmap entries for bytes `0x6A`..`0x6F`, which should be `[`, backslash, `]`, `^`, `_` and
  backtick. No shipped string in any table I decoded uses them, so the order is extrapolated from
  the ASCII run either side.
* That the 1030-byte tail of the sphere grid blob and the 10 bytes at `+0x0F0E` are unused. No
  reader and no writer, which is not the same as proof.

**Not settled, and worth saying plainly:**

* **Equipment entry byte `+0x07`, and bytes `+0x08` through `+0x0A`.** Copied from the template on
  creation and never read by anything I found. Preserve them, do not invent values.
* **Character record bytes `0x00`..`0x03`, `0x37`, and `0x60`..`0x93`.** No readers found. The
  `0x60`..`0x81` WORD block smells like Overdrive mode thresholds from its shape, but that is a
  guess and I am flagging it as one.
* **The asset class index to filename map.** The resolver is the runtime cdidx,
  `ffx_ps2/ffx/proj/prog/cdidx/cdrom.mdg`, 131072 bytes, and I did not crack it. Consequence: the
  grid layout and the equipment template table can be read at runtime through the engine but not
  offline out of the vbf.
* **Which function sets a bit in the sphere grid link trail array.** Only the clear is accounted for,
  so a cheat that wants the trail lines coloured has no verified recipe.
* **Whether `kaizou.bin` is complete as the legal auto-ability set.** It is the only table that says
  weapon versus armour, 125 rows against 134 auto-abilities, and the nine without a recipe only
  appear on shipped gear. A cheat can still write them into a slot and they work. What I cannot say
  is whether any of the nine misbehave in a slot the game never puts them in.
* The `panel.bin` string offsets at `+0x02` through `+0x0E`, seven more per row. They are display
  variants of the name and the UI only needs `+0x00`, but which variant is which is unestablished.

## Appendix A. Items, ids 0x2000..0x206F

112 rows of `item.bin`. Ability-slot ids and item ids are different spaces, see section 7.2.

```
2000 Potion                  2001 Hi-Potion               2002 X-Potion
2003 Mega-Potion             2004 Ether                   2005 Turbo Ether
2006 Phoenix Down            2007 Mega Phoenix            2008 Elixir
2009 Megalixir               200A Antidote                200B Soft
200C Eye Drops               200D Echo Screen             200E Holy Water
200F Remedy                  2010 Power Distiller         2011 Mana Distiller
2012 Speed Distiller         2013 Ability Distiller       2014 Al Bhed Potion
2015 Healing Water           2016 Tetra Elemental         2017 Antarctic Wind
2018 Arctic Wind             2019 Ice Gem                 201A Bomb Fragment
201B Bomb Core               201C Fire Gem                201D Electro Marble
201E Lightning Marble        201F Lightning Gem           2020 Fish Scale
2021 Dragon Scale            2022 Water Gem               2023 Grenade
2024 Frag Grenade            2025 Sleeping Powder         2026 Dream Powder
2027 Silence Grenade         2028 Smoke Bomb              2029 Shadow Gem
202A Shining Gem             202B Blessed Gem             202C Supreme Gem
202D Poison Fang             202E Silver Hourglass        202F Gold Hourglass
2030 Candle of Life          2031 Petrify Grenade         2032 Farplane Shadow
2033 Farplane Wind           2034 Designer Wallet         2035 Dark Matter
2036 Chocobo Feather         2037 Chocobo Wing            2038 Lunar Curtain
2039 Light Curtain           203A Star Curtain            203B Healing Spring
203C Mana Spring             203D Stamina Spring          203E Soul Spring
203F Purifying Salt          2040 Stamina Tablet          2041 Mana Tablet
2042 Twin Stars              2043 Stamina Tonic           2044 Mana Tonic
2045 Three Stars             2046 Power Sphere            2047 Mana Sphere
2048 Speed Sphere            2049 Ability Sphere          204A Fortune Sphere
204B Attribute Sphere        204C Special Sphere          204D Skill Sphere
204E Wht Magic Sphere        204F Blk Magic Sphere        2050 Master Sphere
2051 Lv. 1 Key Sphere        2052 Lv. 2 Key Sphere        2053 Lv. 3 Key Sphere
2054 Lv. 4 Key Sphere        2055 HP Sphere               2056 MP Sphere
2057 Strength Sphere         2058 Defense Sphere          2059 Magic Sphere
205A Magic Def Sphere        205B Agility Sphere          205C Evasion Sphere
205D Accuracy Sphere         205E Luck Sphere             205F Clear Sphere
2060 Return Sphere           2061 Friend Sphere           2062 Teleport Sphere
2063 Warp Sphere             2064 Map                     2065 Rename Card
2066 Musk                    2067 Hypello Potion          2068 Shining Thorn
2069 Pendulum                206A Amulet                  206B Door to Tomorrow
206C Wings to Discovery      206D Gambler's Spirit        206E Underdog's Secret
206F Winning Formula
```

## Appendix B. Auto-abilities, ids 0x8000..0x8085

134 rows of `a_ability.bin`. The cost column is the `kaizou.bin` customise recipe, W for a
weapon and A for an armour. A row with no cost cannot be customised onto gear by the player,
it can only arrive on a shipped piece, but it still works if a cheat writes it into a slot.

| id | name | customise |
|---|---|---|
| 8000 | Sensor | W Ability Sphere x2 |
| 8001 | First Strike | W Return Sphere x1 |
| 8002 | Initiative | W Chocobo Feather x6 |
| 8003 | Counterattack | W Friend Sphere x1 |
| 8004 | Evade & Counter | W Teleport Sphere x1 |
| 8005 | Magic Counter | W Shining Gem x16 |
| 8006 | Magic Booster | W Turbo Ether x30 |
| 8007 | Alchemy | W Healing Water x4 |
| 8008 | Auto-Potion | A Stamina Tablet x4 |
| 8009 | Auto-Med | A Remedy x20 |
| 800A | Auto-Phoenix | A Mega Phoenix x20 |
| 800B | Piercing | W Lv. 2 Key Sphere x1 |
| 800C | Half MP Cost | W Twin Stars x20 |
| 800D | One MP Cost | W Three Stars x20 |
| 800E | Double Overdrive | W Underdog's Secret x30 |
| 800F | Triple Overdrive | W Winning Formula x30 |
| 8010 | SOS Overdrive | W Gambler's Spirit x20 |
| 8011 | Overdrive <9C> AP | W Door to Tomorrow x10 |
| 8012 | Double AP | W Megalixir x20 |
| 8013 | Triple AP | W Wings to Discovery x50 |
| 8014 | No AP | - |
| 8015 | Pickpocket | A Amulet x30 |
| 8016 | Master Thief | A Pendulum x30 |
| 8017 | Break HP Limit | A Wings to Discovery x30 |
| 8018 | Break MP Limit | A Three Stars x30 |
| 8019 | Break Damage Limit | W Dark Matter x60 |
| 801A | Gillionaire | W Designer Wallet x30 |
| 801B | HP Stroll | A Stamina Tablet x2 |
| 801C | MP Stroll | A Mana Tablet x2 |
| 801D | No Encounters | A Purifying Salt x30 |
| 801E | Firestrike | W Bomb Fragment x4 |
| 801F | Fire Ward | A Bomb Fragment x4 |
| 8020 | Fireproof | A Bomb Core x8 |
| 8021 | Fire Eater | A Fire Gem x20 |
| 8022 | Icestrike | W Antarctic Wind x4 |
| 8023 | Ice Ward | A Antarctic Wind x4 |
| 8024 | Iceproof | A Arctic Wind x8 |
| 8025 | Ice Eater | A Ice Gem x20 |
| 8026 | Lightningstrike | W Electro Marble x4 |
| 8027 | Lightning Ward | A Electro Marble x4 |
| 8028 | Lightningproof | A Lightning Marble x8 |
| 8029 | Lightning Eater | A Lightning Gem x20 |
| 802A | Waterstrike | W Fish Scale x4 |
| 802B | Water Ward | A Fish Scale x4 |
| 802C | Waterproof | A Dragon Scale x8 |
| 802D | Water Eater | A Water Gem x20 |
| 802E | Deathstrike | W Farplane Wind x60 |
| 802F | Deathtouch | W Farplane Shadow x30 |
| 8030 | Deathproof | A Farplane Wind x60 |
| 8031 | Death Ward | A Farplane Shadow x15 |
| 8032 | Zombiestrike | W Candle of Life x30 |
| 8033 | Zombietouch | W Holy Water x70 |
| 8034 | Zombieproof | A Candle of Life x10 |
| 8035 | Zombie Ward | A Holy Water x30 |
| 8036 | Stonestrike | W Petrify Grenade x60 |
| 8037 | Stonetouch | W Petrify Grenade x10 |
| 8038 | Stoneproof | A Petrify Grenade x20 |
| 8039 | Stone Ward | A Soft x30 |
| 803A | Poisonstrike | W Poison Fang x24 |
| 803B | Poisontouch | W Antidote x99 |
| 803C | Poisonproof | A Poison Fang x12 |
| 803D | Poison Ward | A Antidote x40 |
| 803E | Sleepstrike | W Dream Powder x16 |
| 803F | Sleeptouch | W Sleeping Powder x10 |
| 8040 | Sleepproof | A Dream Powder x8 |
| 8041 | Sleep Ward | A Sleeping Powder x6 |
| 8042 | Silencestrike | W Silence Grenade x20 |
| 8043 | Silencetouch | W Echo Screen x60 |
| 8044 | Silenceproof | A Silence Grenade x10 |
| 8045 | Silence Ward | A Echo Screen x30 |
| 8046 | Darkstrike | W Smoke Bomb x20 |
| 8047 | Darktouch | W Eye Drops x60 |
| 8048 | Darkproof | A Smoke Bomb x10 |
| 8049 | Dark Ward | A Eye Drops x40 |
| 804A | Slowstrike | W Gold Hourglass x30 |
| 804B | Slowtouch | W Silver Hourglass x16 |
| 804C | Slowproof | A Gold Hourglass x20 |
| 804D | Slow Ward | A Silver Hourglass x10 |
| 804E | Confuseproof | A Musk x48 |
| 804F | Confuse Ward | A Musk x16 |
| 8050 | Berserkproof | A Hypello Potion x32 |
| 8051 | Berserk Ward | A Hypello Potion x8 |
| 8052 | Curseproof | A Tetra Elemental x12 |
| 8053 | Curse Ward | - |
| 8054 | Auto-Shell | A Lunar Curtain x80 |
| 8055 | Auto-Protect | A Light Curtain x70 |
| 8056 | Auto-Haste | A Chocobo Wing x80 |
| 8057 | Auto-Regen | A Healing Spring x80 |
| 8058 | Auto-Reflect | A Star Curtain x40 |
| 8059 | SOS Shell | A Lunar Curtain x8 |
| 805A | SOS Protect | A Light Curtain x8 |
| 805B | SOS Haste | A Chocobo Feather x20 |
| 805C | SOS Regen | A Healing Spring x12 |
| 805D | SOS Reflect | A Star Curtain x8 |
| 805E | SOS NulTide | A Dragon Scale x1 |
| 805F | SOS NulFrost | A Arctic Wind x1 |
| 8060 | SOS NulShock | A Lightning Marble x1 |
| 8061 | SOS NulBlaze | A Bomb Core x1 |
| 8062 | Strength +3% | W Power Sphere x3 |
| 8063 | Strength +5% | W Stamina Spring x2 |
| 8064 | Strength +10% | W Skill Sphere x1 |
| 8065 | Strength +20% | W Supreme Gem x4 |
| 8066 | Magic +3% | W Mana Sphere x3 |
| 8067 | Magic +5% | W Mana Spring x2 |
| 8068 | Magic +10% | W Blk Magic Sphere x1 |
| 8069 | Magic +20% | W Supreme Gem x4 |
| 806A | Defense +3% | A Power Sphere x3 |
| 806B | Defense +5% | A Stamina Spring x2 |
| 806C | Defense +10% | A Special Sphere x1 |
| 806D | Defense +20% | A Blessed Gem x4 |
| 806E | Magic Def +3% | A Mana Sphere x3 |
| 806F | Magic Def +5% | A Mana Spring x2 |
| 8070 | Magic Def +10% | A Wht Magic Sphere x1 |
| 8071 | Magic Def +20% | A Blessed Gem x4 |
| 8072 | HP +5% | A X-Potion x1 |
| 8073 | HP +10% | A Soul Spring x3 |
| 8074 | HP +20% | A Elixir x5 |
| 8075 | HP +30% | A Stamina Tonic x1 |
| 8076 | MP +5% | A Ether x1 |
| 8077 | MP +10% | A Soul Spring x3 |
| 8078 | MP +20% | A Elixir x5 |
| 8079 | MP +30% | A Mana Tonic x1 |
| 807A | Capture | - |
| 807B |  | - |
| 807C | Distill Power | W Power Sphere x2 |
| 807D | Distill Mana | W Mana Sphere x2 |
| 807E | Distill Speed | W Speed Sphere x2 |
| 807F | Distill Ability | W Ability Sphere x2 |
| 8080 | Ribbon | A Dark Matter x99 |
| 8081 | Extra 1 | - |
| 8082 | Extra 2 | - |
| 8083 | Extra 3 | - |
| 8084 | Extra 4 | - |
| 8085 | Extra 5 | - |

## Appendix C. Sphere grid node kinds, `panel.bin` rows 0..126

`mask` is the WORD at row+0x10, `amt` the byte at row+0x14, `gives` the ability id at row+0x12.

| row | name | mask | amt | gives |
|---|---|---|---|---|
| 0 | Lv. 3 Lock | 0x8000 | 0 | - |
| 1 | Empty Node | 0x0000 | 0 | - |
| 2 | Strength +1 | 0x0001 | 1 | - |
| 3 | Strength +2 | 0x0001 | 2 | - |
| 4 | Strength +3 | 0x0001 | 3 | - |
| 5 | Strength +4 | 0x0001 | 4 | - |
| 6 | Defense +1 | 0x0002 | 1 | - |
| 7 | Defense +2 | 0x0002 | 2 | - |
| 8 | Defense +3 | 0x0002 | 3 | - |
| 9 | Defense +4 | 0x0002 | 4 | - |
| 10 | Magic +1 | 0x0004 | 1 | - |
| 11 | Magic +2 | 0x0004 | 2 | - |
| 12 | Magic +3 | 0x0004 | 3 | - |
| 13 | Magic +4 | 0x0004 | 4 | - |
| 14 | Magic Defense +1 | 0x0008 | 1 | - |
| 15 | Magic Defense +2 | 0x0008 | 2 | - |
| 16 | Magic Defense +3 | 0x0008 | 3 | - |
| 17 | Magic Defense +4 | 0x0008 | 4 | - |
| 18 | Agility +1 | 0x0010 | 1 | - |
| 19 | Agility +2 | 0x0010 | 2 | - |
| 20 | Agility +3 | 0x0010 | 3 | - |
| 21 | Agility +4 | 0x0010 | 4 | - |
| 22 | Luck +1 | 0x0020 | 1 | - |
| 23 | Luck +2 | 0x0020 | 2 | - |
| 24 | Luck +3 | 0x0020 | 3 | - |
| 25 | Luck +4 | 0x0020 | 4 | - |
| 26 | Evasion +1 | 0x0040 | 1 | - |
| 27 | Evasion +2 | 0x0040 | 2 | - |
| 28 | Evasion +3 | 0x0040 | 3 | - |
| 29 | Evasion +4 | 0x0040 | 4 | - |
| 30 | Accuracy +1 | 0x0080 | 1 | - |
| 31 | Accuracy +2 | 0x0080 | 2 | - |
| 32 | Accuracy +3 | 0x0080 | 3 | - |
| 33 | Accuracy +4 | 0x0080 | 4 | - |
| 34 | HP +200 | 0x0100 | 4 | - |
| 35 | HP +300 | 0x0100 | 6 | - |
| 36 | MP +40 | 0x0200 | 8 | - |
| 37 | MP +20 | 0x0200 | 4 | - |
| 38 | MP +10 | 0x0200 | 2 | - |
| 39 | Lv. 1 Lock | 0x8000 | 0 | - |
| 40 | Lv. 2 Lock | 0x8000 | 0 | - |
| 41 | Lv. 4 Lock | 0x8000 | 0 | - |
| 42 | Delay Attack | 0x0400 | 0 | Delay Attack |
| 43 | Delay Buster | 0x0400 | 0 | Delay Buster |
| 44 | Sleep Attack | 0x0400 | 0 | Sleep Attack |
| 45 | Silence Attack | 0x0400 | 0 | Silence Attack |
| 46 | Dark Attack | 0x0400 | 0 | Dark Attack |
| 47 | Zombie Attack | 0x0400 | 0 | Zombie Attack |
| 48 | Sleep Buster | 0x0400 | 0 | Sleep Buster |
| 49 | Silence Buster | 0x0400 | 0 | Silence Buster |
| 50 | Dark Buster | 0x0400 | 0 | Dark Buster |
| 51 | Triple Foul | 0x0400 | 0 | Triple Foul |
| 52 | Power Break | 0x0400 | 0 | Power Break |
| 53 | Magic Break | 0x0400 | 0 | Magic Break |
| 54 | Armor Break | 0x0400 | 0 | Armor Break |
| 55 | Mental Break | 0x0400 | 0 | Mental Break |
| 56 | Mug | 0x0400 | 0 | Mug |
| 57 | Quick Hit | 0x0400 | 0 | Quick Hit |
| 58 | Steal | 0x0400 | 0 | Steal |
| 59 | Use | 0x0400 | 0 | Use |
| 60 | Flee | 0x0400 | 0 | Flee |
| 61 | Pray | 0x0400 | 0 | Pray |
| 62 | Cheer | 0x0400 | 0 | Cheer |
| 63 | Focus | 0x0400 | 0 | Focus |
| 64 | Reflex | 0x0400 | 0 | Reflex |
| 65 | Aim | 0x0400 | 0 | Aim |
| 66 | Luck | 0x0400 | 0 | Luck |
| 67 | Jinx | 0x0400 | 0 | Jinx |
| 68 | Lancet | 0x0400 | 0 | Lancet |
| 69 | Guard | 0x0400 | 0 | Guard |
| 70 | Sentinel | 0x0400 | 0 | Sentinel |
| 71 | Spare Change | 0x0400 | 0 | Spare Change |
| 72 | Threaten | 0x0400 | 0 | Threaten |
| 73 | Provoke | 0x0400 | 0 | Provoke |
| 74 | Entrust | 0x0400 | 0 | Entrust |
| 75 | Copycat | 0x0400 | 0 | Copycat |
| 76 | Doublecast | 0x0400 | 0 | Doublecast |
| 77 | Bribe | 0x0400 | 0 | Bribe |
| 78 | Cure | 0x0400 | 0 | Cure |
| 79 | Cura | 0x0400 | 0 | Cura |
| 80 | Curaga | 0x0400 | 0 | Curaga |
| 81 | NulFrost | 0x0400 | 0 | NulFrost |
| 82 | NulBlaze | 0x0400 | 0 | NulBlaze |
| 83 | NulShock | 0x0400 | 0 | NulShock |
| 84 | NulTide | 0x0400 | 0 | NulTide |
| 85 | Scan | 0x0400 | 0 | Scan |
| 86 | Esuna | 0x0400 | 0 | Esuna |
| 87 | Life | 0x0400 | 0 | Life |
| 88 | Full-Life | 0x0400 | 0 | Full-Life |
| 89 | Haste | 0x0400 | 0 | Haste |
| 90 | Hastega | 0x0400 | 0 | Hastega |
| 91 | Slow | 0x0400 | 0 | Slow |
| 92 | Slowga | 0x0400 | 0 | Slowga |
| 93 | Shell | 0x0400 | 0 | Shell |
| 94 | Protect | 0x0400 | 0 | Protect |
| 95 | Reflect | 0x0400 | 0 | Reflect |
| 96 | Dispel | 0x0400 | 0 | Dispel |
| 97 | Regen | 0x0400 | 0 | Regen |
| 98 | Holy | 0x0400 | 0 | Holy |
| 99 | Auto-Life | 0x0400 | 0 | Auto-Life |
| 100 | Blizzard | 0x0400 | 0 | Blizzard |
| 101 | Fire | 0x0400 | 0 | Fire |
| 102 | Thunder | 0x0400 | 0 | Thunder |
| 103 | Water | 0x0400 | 0 | Water |
| 104 | Fira | 0x0400 | 0 | Fira |
| 105 | Blizzara | 0x0400 | 0 | Blizzara |
| 106 | Thundara | 0x0400 | 0 | Thundara |
| 107 | Watera | 0x0400 | 0 | Watera |
| 108 | Firaga | 0x0400 | 0 | Firaga |
| 109 | Blizzaga | 0x0400 | 0 | Blizzaga |
| 110 | Thundaga | 0x0400 | 0 | Thundaga |
| 111 | Waterga | 0x0400 | 0 | Waterga |
| 112 | Bio | 0x0400 | 0 | Bio |
| 113 | Demi | 0x0400 | 0 | Demi |
| 114 | Death | 0x0400 | 0 | Death |
| 115 | Drain | 0x0400 | 0 | Drain |
| 116 | Osmose | 0x0400 | 0 | Osmose |
| 117 | Flare | 0x0400 | 0 | Flare |
| 118 | Ultima | 0x0400 | 0 | Ultima |
| 119 | Pilfer Gil | 0x0400 | 0 | Pilfer Gil |
| 120 | Full Break | 0x0400 | 0 | Full Break |
| 121 | Extract Power | 0x0400 | 0 | Extract Power |
| 122 | Extract Mana | 0x0400 | 0 | Extract Mana |
| 123 | Extract Speed | 0x0400 | 0 | Extract Speed |
| 124 | Extract Ability | 0x0400 | 0 | Extract Ability |
| 125 | Nab Gil | 0x0400 | 0 | Nab Gil |
| 126 | Quick Pockets | 0x0400 | 0 | Quick Pockets |

## Appendix D. Key items, ids 0xA000..0xA03F

```
A000 Withered Bouquet            A001 Flint
A002 Cloudy Mirror               A003 Celestial Mirror
A004 Al Bhed Primer I            A005 Al Bhed Primer II
A006 Al Bhed Primer III          A007 Al Bhed Primer IV
A008 Al Bhed Primer V            A009 Al Bhed Primer VI
A00A Al Bhed Primer VII          A00B Al Bhed Primer VIII
A00C Al Bhed Primer IX           A00D Al Bhed Primer X
A00E Al Bhed Primer XI           A00F Al Bhed Primer XII
A010 Al Bhed Primer XIII         A011 Al Bhed Primer XIV
A012 Al Bhed Primer XV           A013 Al Bhed Primer XVI
A014 Al Bhed Primer XVII         A015 Al Bhed Primer XVIII
A016 Al Bhed Primer XIX          A017 Al Bhed Primer XX
A018 Al Bhed Primer XXI          A019 Al Bhed Primer XXII
A01A Al Bhed Primer XXIII        A01B Al Bhed Primer XXIV
A01C Al Bhed Primer XXV          A01D Al Bhed Primer XXVI
A01E Summoner's Soul             A01F Aeon's Soul
A020 Jecht's Sphere              A021 Rusty Sword
A022 (blank)                     A023 Sun Crest
A024 Sun Sigil                   A025 Moon Crest
A026 Moon Sigil                  A027 Mars Crest
A028 Mars Sigil                  A029 Mark of Conquest
A02A Saturn Crest                A02B Saturn Sigil
A02C Jupiter Crest               A02D Jupiter Sigil
A02E Venus Crest                 A02F Venus Sigil
A030 Mercury Crest               A031 Mercury Sigil
A032 Blossom Crown               A033 Flower Scepter
A034 (blank)                     A035 (blank)
A036 (blank)                     A037 (blank)
A038 (blank)                     A039 (blank)
A03A (blank)                     A03B (blank)
A03C (blank)                     A03D (blank)
A03E (blank)                     A03F (blank)
```

## Appendix E. Known abilities, ids 0x3000..0x313F

320 rows of `command.bin`. These are what SaveDataSetCharAbility turns on. The ones a sphere
grid node grants are a subset, see appendix C.

```
3000 Attack                    3001 Item                      3002 Switch
3003 Escape                    3004 Weapon                    3005 Armor
3006 Delay Attack              3007 Delay Buster              3008 Sleep Attack
3009 Silence Attack            300A Dark Attack               300B Zombie Attack
300C Sleep Buster              300D Silence Buster            300E Dark Buster
300F Triple Foul               3010 Power Break               3011 Magic Break
3012 Armor Break               3013 Mental Break              3014 Mug
3015 Quick Hit                 3016 Steal                     3017 Use
3018 Flee                      3019 Pray                      301A Cheer
301B Aim                       301C Focus                     301D Reflex
301E Luck                      301F Jinx                      3020 Lancet
3021 Defend                    3022 Guard                     3023 Sentinel
3024 Spare Change              3025 Threaten                  3026 Provoke
3027 Entrust                   3028 Copycat                   3029 Doublecast
302A Bribe                     302B Cure                      302C Cura
302D Curaga                    302E NulFrost                  302F NulBlaze
3030 NulShock                  3031 NulTide                   3032 Scan
3033 Esuna                     3034 Life                      3035 Full-Life
3036 Haste                     3037 Hastega                   3038 Slow
3039 Slowga                    303A Shell                     303B Protect
303C Reflect                   303D Dispel                    303E Regen
303F Holy                      3040 Auto-Life                 3041 Blizzard
3042 Fire                      3043 Thunder                   3044 Water
3045 Fira                      3046 Blizzara                  3047 Thundara
3048 Watera                    3049 Firaga                    304A Blizzaga
304B Thundaga                  304C Waterga                   304D Bio
304E Demi                      304F Death                     3050 Drain
3051 Osmose                    3052 Flare                     3053 Ultima
3054 Shield                    3055 Boost                     3056 Dismiss
3057 Dismiss                   3058 Pilfer Gil                3059 Full Break
305A Extract Power             305B Extract Mana              305C Extract Speed
305D Extract Ability           305E Nab Gil                   305F Quick Pockets
3060 Spiral Cut                3061 Slice & Dice              3062 Energy Rain
3063 Blitz Ace                 3064 Shooting Star             3065 Dragon Fang
3066 Banishing Blade           3067 Tornado                   3068 Jump
3069 Fire Breath               306A Seed Cannon               306B Self-Destruct
306C Thrust Kick               306D Stone Breath              306E Aqua Breath
306F Doom                      3070 White Wind                3071 Bad Breath
3072 Mighty Guard              3073 Nova                      3074 Element Reels
3075 Attack Reels              3076 Status Reels              3077 Aurochs Reels
3078 Blizzard Fury             3079 Fire Fury                 307A Thunder Fury
307B Water Fury                307C Fira Fury                 307D Blizzara Fury
307E Thundara Fury             307F Watera Fury               3080 Firaga Fury
3081 Blizzaga Fury             3082 Thundaga Fury             3083 Waterga Fury
3084 Bio Fury                  3085 Demi Fury                 3086 Death Fury
3087 Drain Fury                3088 Osmose Fury               3089 Flare Fury
308A Ultima Fury               308B Grenade                   308C Frag Grenade
308D Pineapple                 308E Potato Masher             308F Cluster Bomb
3090 Tallboy                   3091 Blaster Mine              3092 Hazardous Shell
3093 Calamity Bomb             3094 Chaos Grenade             3095 Heat Blaster
3096 Firestorm                 3097 Burning Soul              3098 Brimstone
3099 Abaddon Flame             309A Snow Flurry               309B Icefall
309C Winter Storm              309D Black Ice                 309E Krysta
309F Thunderbolt               30A0 Rolling Thunder           30A1 Lightning Bolt
30A2 Electroshock              30A3 Thunderblast              30A4 Waterfall
30A5 Flash Flood               30A6 Tidal Wave                30A7 Aqua Toxin
30A8 Dark Rain                 30A9 Nega Burst                30AA Black Hole
30AB Sunburst                  30AC Ultra Potion              30AD Panacea
30AE Ultra Cure                30AF Mega Phoenix              30B0 Final Phoenix
30B1 Elixir                    30B2 Megalixir                 30B3 Super Elixir
30B4 Final Elixir              30B5 NulAll                    30B6 Mega NulAll
30B7 Hyper NulAll              30B8 Ultra NulAll              30B9 Mighty Wall
30BA Mighty G                  30BB Super Mighty G            30BC Hyper Mighty G
30BD Vitality                  30BE Mega Vitality             30BF Hyper Vitality
30C0 Mana                      30C1 Mega Mana                 30C2 Hyper Mana
30C3 Freedom                   30C4 Freedom X                 30C5 Quartet of 9
30C6 Trio of 9999              30C7 Hero Drink                30C8 Miracle Drink
30C9 Hot Spurs                 30CA Eccentrick                30CB Attack
30CC Sonic Wings               30CD Energy Blast              30CE Energy Ray
30CF Attack                    30D0 Meteor Strike             30D1 Hellfire
30D2 Attack                    30D3 Aerospark                 30D4 Thor's Hammer
30D5 Attack                    30D6 Heavenly Strike           30D7 Diamond Dust
30D8 Attack                    30D9 Impulse                   30DA Mega Flare
30DB Attack                    30DC Pain                      30DD Oblivion
30DE Daigoro                   30DF Kozuka                    30E0 Wakizashi
30E1 Wakizashi                 30E2 Zanmato                   30E3 Requiem
30E4 Attack                    30E5 Camisade                  30E6 Attack
30E7 Razzia                    30E8 Attack                    30E9 Passado
30EA Delta Attack              30EB -                         30EC -
30ED -                         30EE -                         30EF Fire Shot
30F0 Fire Shot                 30F1 Ice Shot                  30F2 Ice Shot
30F3 Water Shot                30F4 Water Shot                30F5 Thunder Shot
30F6 Thunder Shot              30F7 Havoc Shot                30F8 Havoc Shot
30F9 Time Shot                 30FA Time Shot                 30FB Break Shot
30FC Break Shot                30FD Aurochs Shot              30FE Power Shot
30FF Magus Sisters             3100 Summon Aeon               3101 Pincer Attack
3102 Use crane                 3103 Move                      3104 Stand by
3105 Talk                      3106 Move in                   3107 Pull back
3108 Cancel                    3109 Struggle                  310A -
310B -                         310C -                         310D -
310E -                         310F -                         3110 -
3111 -                         3112 -                         3113 Skill
3114 Special                   3115 Blk Magic                 3116 Wht Magic
3117 Summon                    3118 Grand Summon              3119 Swordplay
311A Ronso Rage                311B Bushido                   311C Slots
311D Fury                      311E Mix                       311F Auto-Life
3120 Death                     3121 Gil                       3122 Gil
3123 Pay                       3124 Do as you will.           3125 One more time.
3126 Fight!                    3127 Go, go!                   3128 Help each other!
3129 Combine powers!           312A Defense!                  312B Are you all right?
312C Taking a break...         312D NulAll                    312E Attack Reels
312F Open lock                 3130 Extra 24                  3131 Extra 25
3132 Extra 26                  3133 Extra 27                  3134 Extra 28
3135 Extra 29                  3136 Extra 30                  3137 Extra 31
3138 Extra 32                  3139 Extra 33                  313A Extra 34
313B Extra 35                  313C Extra 36                  313D Extra 37
313E Extra 38                  313F Extra 39
```
