# Progression side games: Al Bhed primers, the sphere grid, the Celestial weapons

Three things that are not timed and are not reflex games, but that the player chases across the
whole game and that change the simulation when they land. For each one: where the state lives, what
clock it runs on, whether it draws RNG, who reads and writes it, and what a co-op session has to
agree on. This is the companion to `MINIGAMES_TIMED.md`, which did the same for six timed games.

**Addresses in this document are VAs**, IDA at image base 0x400000, like most docs in this folder.
`MINIGAMES_TIMED.md` uses RVAs, so subtract 0x400000 before comparing a number against it. Save
block offsets are offsets into `g_ffxSaveData 0x112CA90`, written `+0x...`, and for a class-0
script variable the block offset is the variable offset plus 0x1EC.

Everything here came out of the disassembly, out of the shipped `.ebp` bytecode read with
`tools/ebp.py`, out of `takara.bin` and `buki_get.bin` read as tables, or out of a mix. Claims are
marked [V] when I read the code or the bytes that prove them, and [I] when they are an inference.
I also tripped over the Jecht Spheres on the way, and they are in section 5 because they are the
same kind of thing and nobody had pinned them down.

While doing this I found that `MINIGAMES_TIMED.md` section 5 had the butterfly hunt wrong. Actor 38
entry 9 of `mcfr0100` is not the butterfly game, it is the Celestial Mirror upgrade. Section 4.6 here
has the proof and section 7 lists every doc I corrected.

## Summary, read this first

| System | State | Logic | Clock | RNG | Verdict |
|---|---|---|---|---|---|
| Al Bhed primers | `+0x3D10`, 26 bits, plus key items `0xA004..0xA01D` | 38 script `core:269` sites, one C writer | none | none | **replicates, except the primer import, which reads the local disk** |
| Sphere grid | `+0x21EC..+0x3105` node array, links, current nodes, grid id. S.Lv in each character record | menu module 19, C code at `0xA44000..0xA5D000` | menu frames at 59.94 Hz, frame counted animations | **none** | **replicates if the grid's input or its results are host-ordered, plus one derived cache to rebuild** |
| Celestial weapons | `+0xC39` found bits, `+0xC6C` crest bits, `+0xC6D` sigil bits, the weapon's equipment entry, crest and sigil key items | script `core:347` and `core:533`, one C table writer | none | the battle damage roll, unit stream | **replicates for free** |

Five things matter more than the rest.

1. **The Al Bhed mask has exactly one writer, `FFX_SaveData_LearnAlBhedPrimer 0x906B30`.** Every
   grant reaches it through the key item giver `sub_88E790` (proposed `FFX_SaveData_GiveKeyItem`),
   which sets the key item flag and then, for ids `0xA004..0xA01D`, the matching mask bit. [V]
2. **The mask is simulation visible, not only text.** Every primer pickup package reads it back
   with `core:270` and branches on it, and the renderer reads it too. The renderer side is
   presentation. The script side is not. [V]
3. **The primer import path reads the local machine's save files.** Five packages and one block in
   `FFX_MainStep` turn primers found in other save files on this disk into grants. Two peers will not
   have the same files. This is the one real hazard in the whole document. [V]
4. **The sphere grid has no RNG and reaches no clock.** I took the closure of every function in
   `0xA44000..0xA5D000` through direct calls and function address references, 1,229 functions, and
   none of them reaches an `FFX_Rand_*`, the CRT `rand` or any clock source. Its animations count
   menu frames. [V within that method]
5. **The Celestial weapon state is all script driven and all in the save block or the equipment
   table.** The upgrade is a table copy into the weapon's four ability words, and in battle three
   damage formulas, `0x11`, `0x12` and `0x13`, read the attacker's live HP or MP. [V]

What the mod still has to do:

- **Host-relay or suppress the primer import.** `core:453`, `core:565` and the `FFX_MainStep` scan
  block at `0x820C2D` all depend on the local disk. Section 2.4.
- **Decide how the sphere grid travels.** It is a single-instance menu working on `g_ffxMenuWork`.
  Either replicate its input frame by frame, or replicate the committed results. Section 3.6 has
  the commit points, which are few.
- **Rebuild the stat cache after any transplant.** `EquipStatBonus 0x1135E00` is outside the save
  block and only `FFX_SphereGrid_RecomputeDerived 0xA54860` rebuilds it.
- **Nothing for the Celestial weapons** beyond what every other script flag needs. One C function
  reads the found bits during input, section 4.7, and it reads the save block, so it agrees when the
  block agrees.

## 1. What the three have in common

None of them is a timer. All three advance only when a script event fires or when the player
presses a button in a menu. That makes them easier than the six timed games, because there is no
clock to keep in step. The questions that remain are where every simulation visible bit lives, and
whether any of it comes from outside the simulation.

Two of the three come from outside in one place each:

- the primer import reads save files on the local disk (section 2.4)
- the sphere grid is a menu, and a menu is driven by one player's pad and one single-slot working
  copy (section 3)

## 2. Al Bhed primers

### 2.1 State

| what | where | width | notes |
|---|---|---|---|
| primer mask | `g_ffxAlBhedPrimerMask 0x11307A0` = `+0x3D10` | u32, 26 bits used | bit n = primer n, volume n+1 |
| key items | key item bits for `0xA004 + n` in the key item array at `+0x448C` | 1 bit each | set together with the mask bit |
| imported mask | `dword_1325AC4` | u32 | outside the block, local disk, section 2.4 |
| script view of the import | `dword_1325B8C` | u32 | outside the block, `core:565` returns it |
| staged grant | `dword_1325B90` armed, `dword_1325B94` mask | u32 x 2 | outside the block, consumed by `core:564` |
| debug override | `dword_18763E0` | s32 | process local, text only, section 2.5 |

### 2.2 The one writer [V]

`FFX_SaveData_LearnAlBhedPrimer 0x906B30 (int n)`:

- `n == -1` clears the mask
- `0 <= n <= 25` sets bit n, unlocks trophy 1 (call at `0x906B5B`), and unlocks trophy 28 when the
  mask reaches `0x3FFFFFF`
- `n == 255` sets all 26 bits and unlocks trophy 28

The three data references to `0x11307A0` from code that writes are all inside this function, at
`0x906B3B`, `0x906B55` and `0x906B84`. It has two callers:

- `FFX_AtelSys_Save_039_resi 0x878D20`, a dev command gated on `FFX_IsDevHarness`, which clears the
  mask and the primer key items
- `sub_88E790`, the key item giver. It calls `FFX_SaveData_SetKeyItemFlag(id)` and then, when
  `id - 0xA004` is 0..25, `LearnAlBhedPrimer(id - 0xA004)`. I propose `FFX_SaveData_GiveKeyItem`.

`sub_88E790` is called from `core:423` (`0x857BC0`), `core:347` (`0x85A9A4`), `core:269`
(`0x85ACAE`), `core:351` (`0x85B662`), the import commit `sub_86A040` (`0x86A074`), the dev command
`Save_040` (`0x878D77`) and one function-less block at `0x8A9807`. So any key item grant of a primer
id sets the mask, whichever syscall made it.

### 2.3 How each primer is gained [V]

All 26 are given by `core:269(n)` from an event script. None is a treasure chest: no `takara.bin`
row of kind 10 has an id in `0xA004..0xA01D`. There are 38 `core:269` calls in the shipped
packages, including the debug package `test29` (n = 4, 13, 18) and `dbg_nagi0000`.

| n | package, actor |
|---|---|
| 0 | `bika0000` a10, `cdsp0000` a28 |
| 1 | `bsvr0200` a29 |
| 2 | `bika0000` a11, `slik0800` a13 |
| 3 | `ptkl0800` a7 |
| 4 | `bika0100` a12, `swin0300` a7, `swin0900` a7 |
| 5 | `lchb1400` a21 |
| 6 | `lchb1100` a9 |
| 7 | `mihn0300` a19 |
| 8 | `mihn0500` a22 |
| 9 | `kino0500` a29 |
| 10 | `kino0400` a21 |
| 11 | `genk1200` a19 |
| 12 | `guad0400` a14 |
| 13 | `bika0100` a13, `kami0100` a16 |
| 14 | `mcfr0400` a15 |
| 15 | `maca0000` a23 |
| 16 | `bika0200` a14 |
| 17 | `bika0200` a15 |
| 18 | `bika0400` a20 |
| 19 | `azit0600` a12 |
| 20 | `azit0300` a15 |
| 21 | `bvyt0400` a9 |
| 22 | `nagi0000` a71 |
| 23 | `lmyt0000` a34 |
| 24 | `nagi0500` a24 |
| 25 | `omeg0000` a29 |

Each pickup package also reads the mask once with `core:270`, so the script decides what to show or
whether to offer the pickup at all from the mask. That read is the reason the mask is simulation
visible.

### 2.4 The import path, the hazard [V]

The game can pull primers out of other save files on the same disk. Two routes, both local.

**The script route.** Five packages, `azit0700`, `bjyt0000`, `hiku0500`, `maca0100` and
`mihn0300`, run this sequence:

1. `core:605(0)` clears `dword_1325AC4`
2. `core:453(0, 0)`, `RunMemCardCheck`, starts save UI states 21..27, which run
   `FFX_SaveFile_BeginPrimerImport 0x6EFEB0` and `FFX_SaveFile_ImportPrimerFromSlot 0x6EFD10`. The
   second reads a save file with `fread`, checks no CRC, ORs that file's primer mask into the
   operation buffer's mask at `+0x3D50` (block `+0x3D10` behind the 0x40-byte header) and stores
   the result in both `dword_1325AC4` and `dword_1325B8C` (`0x6EFD93..0x6EFDAB`). A function-less
   copy at `0x647976` does the same with a `g_albhed:%d` printf and no caller
3. `core:565` returns `dword_1325B8C` through `sub_8729D0`, and the script tests it bit by bit, 26
   `core:270` reads per package
4. `core:564`, `FFX_AtelSys_Core_564_CommitStagedPrimerGrants_resi 0x85FFF0`, calls `sub_86A040`,
   which, when `dword_1325B90` is set, calls `sub_88E790(0xA004 + i)` for each bit i < 26 of
   `dword_1325B94` that the mask does not have yet, then clears both

`memochek` and `test29` also use `core:453`.

**The C route.** `FFX_MainStep 0x820C2D..0x820C49`: when `FFX_SaveFile_TakeScanDoneFlag()` returns 1,
it calls `FFX_AlBhed_StagePrimerGrantMask 0x86E820 (1, FFX_AlBhed_GetImportedPrimerMask())`.
`0x86E820` writes `dword_1325B90` and `dword_1325B94`, and `0x647C90` returns `dword_1325AC4`. So a
finished save scan stages the imported mask, and the next `core:564` commits it.

Both routes take their input from files on the local disk. Two peers with different save folders
get different masks, different `core:565` results, and so different script branches. **The mod
has to run the import on the host only and relay the resulting grant**, or disable it in a
session. The staged words are outside the save block, so a block hash will not see the difference
until the commit lands.

### 2.5 What the mask changes in the renderer [V, one I]

`sub_906780` (proposed `FFX_AlBhed_TranslateGlyph`) decides per glyph whether to draw the Al Bhed
cipher glyph or the original:

- it reads the mask, unless the debug override `dword_18763E0` says otherwise (-1 none known, 1 all
  known). Only `FFX_Debug_CycleAlBhedDictionary 0x906740` writes the override, from the orphan
  debug page. `sub_906730` reads it
- language 0 with international env 0: glyphs `0x5F..0xAE` take their primer bit from
  `byte_187626E[4*g]` and their cipher glyph from `byte_187626D[4*g]`
- languages 9 and 10 take a multi-byte path with bit `(code - 94) % 26`
- other languages: `A..Z` is `0x50..0x69` and `a..z` is `0x70..0x89`, bit from `byte_18762AA[4*g]`,
  cipher glyph from `byte_18762A9[4*g]`
- returns 2 when the primer is known (draw the original), 1 when substituted, 0 when neither

It is called only when the text object's `+0x10 == 0x4000`, from `sub_8B6CB0` (`0x8B6D4D`),
`FFX_MesWin_MeasureTokenRun` (`0x8B9743`), `FFX_MesWin_LayoutMessageAndFindChoices` (`0x8B9AF4`),
`FFX_MesWin_DrawText` (`0x8DEB7F`, which also picks colour code `0x56` or `0x60` from the return),
`DrawTextAlt` (`0x8DF002`) and `DrawTextGs` (`0x8DFFE1`). `+0x10 = 0x4000` is set only by
`FFX_MesWin_ApplyAttrToTextObject 0x8B8BB0`, from attribute bit `0x4000`, called from `sub_8B0000`,
which sets the frame style and the attribute together. No message attribute in any `.ebp` carries
`0x4000`, so the bit comes from a window style argument. [I which syscall passes it]

The forward call closure of `FFX_MesWin_StepAll` (14 functions) and of `FFX_Atel_StepOnce` (2,473
functions) does not reach `sub_906780` through direct calls. The layout path is reached from the menu
entry and the draw path. So the renderer use is presentation. A glyph swap can change a token's
measured width, so line breaks could differ between a peer who knows a primer and one who does not,
but both peers have the same mask when the block agrees, so this is not a hazard in practice. [I]

The dictionary data is `Albheddic.bin`, loaded at text init, and `Albhed.bin` exists only for Korean
and Chinese, loaded by `FFX_Btl_LoadAlbhedLocalized 0x790340` (`ASSET_PATHS.md`).

### 2.6 Readers [V]

| reader | where | what |
|---|---|---|
| `FFX_SaveData_GetAlBhedPrimerMask 0x906B10` | `core:270` at `0x85AF50`, the debug page `sub_87F4A0` at `0x87F55B` ("Book%02d On/Off"), a function-less copy at `0x87F61F` | returns the mask |
| `sub_86A040` | `0x86A053` | the import commit, skips bits already known |
| `sub_906780` | `0x90678A` | the renderer, section 2.5 |

### 2.7 Clock, RNG and verdict

No clock and no RNG. A grant is a script event stepped by ATEL, and the renderer runs on the present
side. **Replicates for free, except the import**, which must be host-only. Trophies 1 and 28 fire on
each machine that runs the grant, which is the same per-machine side effect as every other trophy.

## 3. The sphere grid as a game system

### 3.1 State

| what | where | notes |
|---|---|---|
| node array | `g_ffxSphereGridNodes 0x112EC7C` = `+0x21EC`, 1280 x {kind, mask} | kind = `panel.bin` row, mask bit c = character c activated it |
| link array | `+0x2BEC`, one byte per line | per-character trail, cosmetic for stats |
| current node | `+0x30EC`, u16 x 7 | each character's position |
| grid id, zoom | `+0x3104`, `+0x3105` | camera only |
| grid type | bits 14..15 of `g_ffxSaveDataOptionFlags 0x113079C` = `+0x3D0C` | 0 template, 1 Standard, 2 Expert. `INTERNATIONAL_CONTENT.md` 7 |
| sphere levels | character record `+0x3B` available, `+0x3C` spent | record at `+0x55CC + 148*c` |
| AP | character record `+0x14` total, `+0x18` toward the next level | |
| sphere items | the item array | consumed by use, section 3.3 |
| stat cache | `EquipStatBonus 0x1135E00`, 28 bytes x 8 | **outside the block**, derived |

`CHEAT_SAVEDATA.md` section 6 has the byte layouts. What it did not have is the flow between the
menu and the block, which is this section.

### 3.2 The menu works on a copy [V]

The grid is menu module 19. `FFX_Menu_Build 0xA54B40` builds it and calls
`FFX_SphereGrid_SaveToMenu 0xA49590`, which copies the save block into `g_ffxMenuWork`:

| save | menu work | |
|---|---|---|
| node kind | word at `+2062 + 40*i`, `0xFF` becomes -1 | 40-byte node record at `+2056` |
| node mask | byte at `+2089 + 40*i` | |
| link byte | byte at `+43028 + 20*j` | 20-byte link record |
| current node of c | word at `+69836 + 80*c` | |
| grid id, zoom | `+71115`, `+71116` | |

`FFX_SphereGrid_MenuToSave 0xA5BB70` copies the same fields back. It is called from:

- `FFX_SphereGrid_ActivateNodeForChar 0xA48910`, at once, every activation
- `sub_A47D50`, the node kind change, when its frame counter reaches 20
- `sub_A56060`, the commit branch of the quit path, followed by `RecomputeDerived` and `sub_8E27E0`
- two function-less tails at `0xA45560` and `0xA547F0`, each followed by `RecomputeDerived`

So the block is updated on every activation and every kind change, and the current nodes reach the
block only at the next of those or at quit. Between commits a character's position lives only in
`g_ffxMenuWork`.

### 3.3 The actions [V for the code, I for which item is which effect]

**Move and its cost.** The cursor moves a character node by node while `g_ffxMenuWork + 71196`
accumulates a cost. `0xA56160`, now named `FFX_Menu_SphereGridActivateNode`, is the confirm or
cancel of that move, reached through the handler pointer at `0xA4825F`:

- `a3 == 0`, confirm: sound `0x80000052`, `FFX_SaveData_SpendSphereLevels 0x786EF0 (menu+71100,
  menu+71196)`, which is the only S.Lv spend in the binary
- `a3 == 1`, cancel: clears the character's bit on every link record flagged 8, puts the
  character's node back from `menu+71216`, redraws, and sets the handler `sub_A59E80`
- both clear the links' transient flags and zero the cost

So it does not activate a node. It charges a move. I propose the name
`FFX_SphereGrid_ConfirmOrCancelMove`. `MENU_SYSTEM.md` and `CHEAT_SAVEDATA.md` are updated.

**Using a sphere.** `sub_A452D0`, the item page step, on confirm (pad bit `0x20`) calls
`sub_A5BCA0 (char = menu+71100, node = menu+70396, item)`:

1. the node record's byte `+34` must have one of bits `0x03` set (`menu + 2090 + 40*node`)
2. `sub_A493B0` must pass: the item count is not zero, and `sub_A49440` says the sphere fits the
   node. The sphere row is `sphere.bin` row `item.bin[item] + 93`. For effect byte `+13 == 0` the
   test is "the character has not activated this node" and "`panel.bin` mask `+16` AND sphere mask
   `+10` is not zero, and the panel is not a lock". For other effects `sub_A5D120` decides
3. `FFX_SaveData_AddItem(item, -1)` must return 0, so the sphere is spent first
4. effect 0 activates through `FFX_SphereGrid_ActivateNodeForChar`. Any other effect goes to
   `sub_A5CF30`

`sub_A5CF30` by effect byte:

| effect | what the code does | which spheres, [I] from the counts |
|---|---|---|
| 0 | activate the node for the character | Power, Mana, Speed, Ability, Fortune |
| 1..4 | change the node to kind 1, Empty Node | Lv. 1 to Lv. 4 Key Spheres |
| 5, 6, 7, 16..22 | change the node to kind 25, 35, 36, 5, 9, 13, 17, 21, 29, 33 | the ten attribute spheres |
| 8..11, 24 | activate the node for the character, with sound | Special, Skill, White, Black, Master |
| 12..15 | warp the character to the node | Return, Friend, Teleport, Warp |
| 23 | change the node to kind 1 and clear all seven characters' bits | Clear Sphere |

**Activating a node.** `FFX_SphereGrid_ActivateNodeForChar 0xA48910 (char, node)` sets
`menu+2089+40*node |= 1 << char`, records the node at `menu+71336`, starts the board effect
(4 on Expert, 0 otherwise), then `MenuToSave`, `FFX_SphereGrid_RecomputeDerived`,
`FFX_SphereGrid_CheckGridTrophies 0xA47210` (trophies 23 and 29) and `sub_A5AA30` for the learned
ability popup.

**Changing a node kind.** `sub_A48740 (newKind, node)` starts the effect (5 on Expert, 3
otherwise), stores the kind at `menu+71244` and the node at `menu+71246`, and installs `sub_A47D50`.
`sub_A47D50` runs once per menu frame. It counts `menu+71248` from 0 and at 20 it calls `MenuToSave`
and `RecomputeDerived`, and only then writes the new kind into the node record. So for 20 frames the
item is already gone and the node still has its old kind.

**Warping.** `sub_A48A80 (char, node)` stores the target at `menu+71220` and the character at
`menu+71224` and installs `sub_A47F00`. That fades out over 40 frames, moves over 12 frames
(`+1/12` per frame), writes the character's current node, and fades back in over 40 frames. The
fade phases wait on `word_1A8607E == 0`, the board effect's busy word.

**Where sphere levels come from.** `FFX_SaveData_AddAp 0x784610`, called only by
`FFX_BtlResult_AddAp 0x8BA390` at battle results, adds AP to `+0x14` and `+0x18` and turns `+0x18`
into levels one at a time while `+0x3B < 99`. The debug fill is `FFX_Debug_FullSphereLevels
0x784AE0`.

### 3.4 What the stats are built from [V]

`FFX_SphereGrid_RecomputeDerived 0xA54860` clears `EquipStatBonus`, walks nodes 0..1023 for
characters 0..6, adds `panel.bin` `+20` into the buckets selected by `+16`, clamps at 255, sets the
ability `+18`, and calls `FFX_SaveData_RecomputeAllCharDerived`. The node mask is the only input
besides the panel table, so two blocks with the same node array give the same stats.

Its callers: `FFX_SaveData_CommitLoad` (through `j_ 0x8A97E0`), `FFX_SphereGrid_InitNodes`
(`0xA54547`), `sub_A47D50`, `FFX_SphereGrid_ActivateNodeForChar`, `sub_A56060`, and the two tails at
`0xA45565` and `0xA547F5`.

### 3.5 Clock and RNG [V]

**The clock.** The grid is a menu. `FFX_MenuSys_StepFrame` sets the rate divider to 1 at `0x8A9DAC`
and back to 2 at `0x8A9FE8`, so the grid runs at 59.94 Hz with one simulation step per frame
(`MAIN_LOOP.md`). Every animation in the grid is frame counted: the kind change is 20 frames, the warp
is 40 + 12 + 40, and the pad repeat in `FFX_Menu_SphereGridReadPad 0xA57520` is 6 frames then every
3, read from the sticky held pad at `+0x18` (`MENU_SYSTEM.md`).

**RNG.** I listed all 248 functions in `0xA44000..0xA5D000`, took the closure through direct calls
and function address references, 1,229 functions, and checked it against every `FFX_Rand_*`, the CRT
`rand` and every clock source in the binary. None is reached. The grid has no randomness.

### 3.6 What a step by step recreation must reproduce, and the co-op hazards

A recreation has to reproduce:

- the three commit points in section 3.2, because between them the block and the menu disagree
- the order inside a sphere use: item spent, then the effect, then for a kind change a 20-frame delay
  before the node kind and the stats change
- the S.Lv spend on move confirm, and the restore on cancel
- `RecomputeDerived` after every commit, and the 1024-node bound
- the frame counted pad repeat, since menu input is per frame

Co-op hazards:

1. **The grid is one menu over one working copy.** `g_ffxMenuWork` holds one current character at
   `+71100` and one cursor. Two players cannot be in it at once. The mod either replicates the whole
   menu's input frame by frame, at the 59.94 Hz menu rate, or it lets one player run the grid
   locally and sends the committed results as a host-ordered command. The results are small: the
   node array, the link array, the seven current nodes, two S.Lv bytes per character and the sphere
   item counts.
2. **A desync check during the grid sees intermediate states.** The current nodes are only in the
   menu work until the next commit, and a kind change spends the item 20 frames before the node
   changes. Hash at commit points, not every step.
3. **`EquipStatBonus 0x1135E00` is outside the block.** A transplant must call
   `FFX_SphereGrid_RecomputeDerived`, which `FFX_SaveData_CommitLoad` already does.
4. **The warp waits on `word_1A8607E`, the board effect's busy word.** I did not find what clears it.
   If the effect is stepped from the present thread, the warp's commit frame could differ between
   machines when input is replicated frame by frame. It does not matter if results are replicated.

## 4. The Celestial weapons

### 4.1 State

| what | where | width | notes |
|---|---|---|---|
| weapons found | `+0xC39`, class 0 var at `0xA4D`, current name `maybe_g_ffxSaveWeaponMasterBits 0x112D6C9` | u8, 7 bits | 0x01 Tidus, 0x02 Yuna, 0x04 Wakka, 0x08 Auron, 0x10 Kimahri, 0x20 Lulu, 0x40 Rikku. 0x7F = trophy 27 |
| crest applied | `+0xC6C`, class 0 var at `0xA80`, `0x112D6FC` | u8, 7 bits | bit `1 << charId`, 1 Tidus .. 64 Rikku |
| sigil applied | `+0xC6D`, class 0 var at `0xA81`, `0x112D6FD` | u8, 7 bits | same bits |
| the weapon | an entry in the equipment array at `+0x449C` | 22 bytes | flag `0x04` at `+0x03` marks a celestial weapon |
| crests, sigils, mirrors | key item bits | 1 bit each | table in 4.3 |
| chest opened | the treasure flags | 1 bit each | ordinary treasure rows |

Note the two different bit orders. `+0xC39` uses the order the scripts set it in, Wakka before
Auron, and `+0xC6C` and `+0xC6D` use `1 << charId`, Auron before Wakka.

### 4.2 The weapons [V]

Every weapon is a treasure of kind 5, which is equipment built from a `buki_get.bin` template. All
seven templates have flags `0x04`, one ability `0x8014` (No AP), 4 slots, power `0x10` and crit 3:

| char | treasure | buki_get | formula | given at | `+0xC39` bit |
|---|---|---|---|---|---|
| Tidus | 114 | 37 | 0x11 | `nagi0000` a96 e2 | 0x01 |
| Yuna | 113 | 36 | 0x12 | `nagi0700` a3 e2 | 0x02 |
| Wakka | 93 | 26 | 0x11 | `luca0400` a32 e7 | 0x04 |
| Auron | 99 | 30 | 0x13 | `kino0000` a47 e2 | 0x08 |
| Kimahri | 188 | 56 | 0x11 | `kami0000` a17 e2 | 0x10 |
| Lulu | 5 | 3 | 0x12 | `bjyt0200` a8 e6 | 0x20 |
| Rikku | 214 | 61 | 0x11 | `kino0100` a11 e2 | 0x40 |

Each site does `core:347(1, treasure)` and then `+0xC39 |= bit`. The debug package `test23` actor 24
sets each bit and gives the next weapon, which confirms the mapping a second way. Auron's needs the
Rusty Sword, key item `0xA021`, treasure 118 at `nagi0400` a7 e2, tested at `kino0000` a46 and taken
at `kino0000` a25 e8. `luca0400` a24 e5 also clears bit 0x04 and swaps `0xA002` and `0xA003`, which I
did not explain.

So `+0xC39` is settled as the seven found bits, and trophy 27 (`FFX_MainStep 0x820F1A..0x820F52`)
fires when all seven are set.

### 4.3 Crests, sigils and mirrors [V, one I]

All are key items given as treasure kind 10.

| item | id | treasure | given at |
|---|---|---|---|
| Sun Crest | A023 | 267 | `dome0600` a15 e2 |
| Sun Sigil | A024 | 274 | `nagi0000` a72 e12, the chocobo race, four sites |
| Moon Crest | A025 | 268 | `bsil0000` a15, `bsil0100` a17 |
| Moon Sigil | A026 | 275 | `lmyt0100` a16 e2 |
| Mars Crest | A027 | 269 | `mihn0400` a18 |
| Mars Sigil | A028 | 276 | `nagi0700` a2 e2 |
| Saturn Crest | A02A | 270 | `mtgz0200` a9 |
| Saturn Sigil | A02B | 277 | `mcfr0100` a27 e2, `mcfr0200` a17 e2, the butterfly hunt |
| Jupiter Crest | A02C | 271 | `lchb1300` a31 |
| Jupiter Sigil | A02D | 244 | no immediate give. It is an element of a prize array in `bltz0200` at `0x14F48`, Blitzball [I] |
| Venus Crest | A02E | 272 | `ikai0600` a14, `ikai0800` a18 |
| Venus Sigil | A02F | 278 | `kami0400` a10 e2, and the `200thunder_kami0400` variant |
| Mercury Crest | A030 | 273 | `bika0300` a16 |
| Mercury Sigil | A031 | 279 | `bika0300` a35 |
| Cloudy Mirror | A002 | 176 | `lmyt0000` a37 e6 |
| Celestial Mirror | A003 | 111 | `mcfr0100` a9 e2 and a38 e2, which takes A002 with `core:353` |

The Mirror is tested with `core:352` at every weapon pickup site and at `bjyt0200`, `kami0000`,
`kino0000`, `kino0100`, `luca0400`, `nagi0000` a96 and `nagi0700` a3.

### 4.4 The upgrade is a table copy [V]

`core:533`, `FFX_AtelSys_Core_533_SetEquipAbilitiesFromTable_resi 0x85D170`, pops a row and a kind.
A kind of 18 or more returns 0, the row is clamped to 1..2, and it calls
`FFX_Equip_SetAbilitySlotsFromTable 0x8C3170 (kind, row)`. That switches on the kind for a base,
takes the 8-byte row at `base + 8*row`, writes its four u16 into the destination's `+0x0E..+0x15`,
and calls `j_FFX_SaveData_RecomputeAllCharDerived`.

The destination is `sub_8C30E0 (kind)`, proposed `FFX_Equip_FindCelestialWeapon`. It asks
`sub_7ABD10 (kind, -1, 0, &n)` for the character's equipment list and returns the first entry with
`flags & 4` and `forChar == kind`. No celestial template has `forChar >= 7`, so kinds 7..17 find
nothing and the call does nothing. `sub_7ABD10` (proposed `FFX_SaveData_ListEquipForChar`) also lists
free slots, which does not matter here because a free slot has no `0x04` flag.

Kind to base: 0 `0xC86D00`, 1 `0xC86D10`, 2 `0xC86D60`, 3 `0xC86D20`, 4 `0xC86D30`, 5 `0xC86D40`, 6
`0xC86D50`, 7 `0xC86D70`, 8..12 `0xC86D80..0xC86DC0`, 13 `0xC86DE0`, 14 `0xC86DD0`, 15..17
`0xC86DF0..0xC86E10`.

| row | for | abilities |
|---|---|---|
| 1, crest | kinds 0..7 | `0x8014` No AP, `0x800E` Double Overdrive |
| 2, sigil | Tidus | `0x8019` Break Damage Limit, `0x800F` Triple Overdrive, `0x8004` Evade & Counter, `0x8005` Magic Counter |
| 2 | Yuna | 8019, 800F, `0x8012` Double AP, `0x800D` One MP Cost |
| 2 | Auron | 8019, 800F, `0x8001` First Strike, `0x8003` Counterattack |
| 2 | Kimahri | 8019, 800F, 8012, 8004 |
| 2 | Wakka | 8019, 800F, 8012, 8004 |
| 2 | Lulu | 8019, 800F, `0x8006` Magic Booster, 800D |
| 2 | Rikku | 8019, 800F, 8012, `0x801A` Gillionaire |

The rows for kinds 8 and up hold `0x807B`, `0x800B` and `0x8019`, and are dead because those kinds
find no weapon.

### 4.5 Where the upgrade runs [V]

`mcfr0100` actor 38 entry 9, the Macalania spring. `core:315` puts up a menu and the choice goes into
var55, 0..6. The switch at `label_38_16 0x129B9` jumps to one of seven blocks, at `0x7E48`, `0x96C5`,
`0xAF42`, `0xC7BF`, `0xE03C`, `0xF8B9` and `0x11136`. Each block:

1. sets var53 to the character of an actor (actors 0, 1, 5, 6, 2, 4, 3 for choices 0..6, so var53
   ends up equal to the choice)
2. tests the found bit in var2, which is `+0xC39`
3. tests the crest key item (Sun, Moon, Mars, Saturn, Jupiter, Venus, Mercury)
4. tests var3 (`+0xC6C`) and var4 (`+0xC6D`) for the character's bit
5. applies the crest with `core:533(char, 1)` and sets the var3 bit, for example at `0x8654`, or
   the sigil with `core:533(char, 2)` and sets the var4 bit, for example at `0x8698`

The block is a macro expansion, so it carries dead code: 7 live and 7 dead copies of each
(kind, row) pair, and the pairs with kinds 8, 9, 10, 11 and 14 at row 1 are no-ops. That is where the
"266 calls" count comes from. Entry 0 of the same actor tests `A002 && !A003 && var7 & 16` to decide
whether the mirror spot is live. `nagi0600` a13 e8 calls `core:533(14, 1)` six times, which does
nothing because kind 14 finds no weapon.

### 4.6 The butterfly correction [V]

`MINIGAMES_TIMED.md` read entry 9 as the butterfly game body and var57 as a score gauge. The
bytecode says otherwise:

```
007D7E  var57 = 128
007D8A  while (var57 >= 89) { sg:9(0, 0, 0, var57); var57 -= 1; WaitFrames(1) }
```

That is a 40-frame fade before the menu. The 28 `+= 3` and `-= 3` sites are a fade out and a fade
back in, one pair per character block, 14 live and 14 dead. For example `0x892F..0x8968` loops
`sg:9(0, 0, 0, var57); var57 -= 3; WaitFrames(1)` while `var57 > 1`, and `0x8A6D..0x8A96` loops
`+= 3` while `var57 <= 89`. None of it counts butterflies. The butterfly reward is the Saturn Sigil,
treasure 277, given at `mcfr0100` a27 e2 and `mcfr0200` a17 e2. I did not find where the hunt keeps
its catch count. The hunt's clock in actor 37 and its five stream-2 draws are unaffected, so the
"replicates for free" verdict for the hunt stands.

### 4.7 What the weapon changes in battle [V]

`FFX_Btl_LoadCharRecordIntoActor 0x79C5F0` defaults the actor's damage formula `+0x5C1` to 1
(`0x79C771`), power `+0x5C7` to `0x10` and crit `+0x5D8` to 0. Then it copies the weapon entry's
`+0x08` into `+0x5C1` (`0x79C867`), `+0x09` into `+0x5C7` (`0x79C871`) and adds both equipped entries'
`+0x0A` into `+0x5D8` (`0x79C87A`).

`FFX_Btl_ComputeEffectForTarget 0x78E630` takes the formula and power from the actor when the
command record's `+28` has flag `0x40000`, "uses the weapon" (`0x78E75F`, `0x78E770`), and from the
command's `+0x28` and `+0x2A` otherwise. `sub_78CD30` makes the same choice at `0x78CD68`.

`FFX_Btl_DamageFormula 0x789BF0`, jump table at `0x789CCB`:

| formula | weapons | what it does |
|---|---|---|
| `0x11` | Tidus, Wakka, Kimahri, Rikku | `((Str + bonus)^3 / 32 + 30) * (100 * curHP / maxHP + 10) / 110`, reads `+0x5D0` at `0x78A01F` and max HP `+0x594` |
| `0x12` | Yuna, Lulu | the same shape on MP, `+0x5D4` at `0x78A07C` and `+0x598` |
| `0x13` | Auron | `(130 - 100 * curHP / maxHP) / 60` scaling, `0x78A0D9`, the `0x82` at `0x78A0EA` |

All three ignore the target's defence. The random factor is
`(FFX_Rand_Stream(FFX_Btl_RandStreamForUnit(unit, 0)) & 0x1F) + 240` when the variance argument is
set. Battle replicates the action, not the input, so this roll is the same on both peers as long as
the unit streams agree.

One C function reads the found bits outside battle. `FFX_AtelSys_Core_077_IsPadPressedRemapped_resi
0x85E820` returns false on event 49 (`bjyt0200`, Lulu's weapon) when bit 0x20 is set and the calling
worker's `+46` is 11. So once Lulu's weapon is found the pickup prompt stops answering. It reads the
save block, so it agrees when the block agrees.

### 4.8 Clock, RNG and verdict

No clock. The only RNG is the battle damage roll, on the unit stream. Everything else is script
events and table copies. The upgrade menu's choice comes from `core:315`, which is ordinary menu
input and has to travel like any other. **Replicates for free.**

## 5. Jecht Spheres, found on the way [V, one I]

`+0xC7F`, class 0 var at `0xA93`, u8, current name `maybe_g_ffxSaveJechtSphereStage 0x112D70F`. It
is a count. Nine packages increment it once each: `bvmm0000` a15 e2 (`0x11D0`), `bvmm0600` a7,
`gemm0100` a5, `kamm0300` a5, `lumm0100` a6, `mimm0200` a7, `msmm0000` a4, `mtmm0000` a5 and
`slmm0200` a15. After the increment each one switches on the count: at 2 it calls
`core:534(1, 2, 102)` and at 9 `core:534(1, 2, 103)`, teaching Auron an ability. The first sphere is
key item `0xA020`, treasure 177, given at `mcfr0300` a11 e3 with `core:423`, and it does not touch
the count. So `+0xC7F` counts the spheres after the first, and 9 means all ten. That 102 and 103 are
Auron's overdrives is [I] from the game. Trophy 20 at 8 then 9 is `STEAM_ACHIEVEMENTS.md` 5.2.

No clock, no RNG, script only.

## 6. Every address I derived, as VAs

Types: `fn` function, `glob` global, `tbl` table, `off` save block offset.

### Al Bhed primers

| VA | Type | Name | Note |
|---|---|---|---|
| 0x11307A0 | glob | `g_ffxAlBhedPrimerMask` | `+0x3D10`, 26 bits |
| 0x906B30 | fn | `FFX_SaveData_LearnAlBhedPrimer` | the only writer, trophies 1 and 28 |
| 0x906B10 | fn | `FFX_SaveData_GetAlBhedPrimerMask` | `core:270` |
| 0x88E790 | fn | `sub_88E790`, proposed `FFX_SaveData_GiveKeyItem` | key item flag, then the primer bit for `0xA004..0xA01D` |
| 0x86A040 | fn | `sub_86A040`, proposed `FFX_AlBhed_CommitStagedPrimerGrants` | `core:564` body |
| 0x85FFF0 | fn | `FFX_AtelSys_Core_564_CommitStagedPrimerGrants_resi` | |
| 0x8729D0 | fn | `sub_8729D0`, proposed `FFX_AlBhed_GetImportedPrimerMaskForScript` | `core:565` |
| 0x86E820 | fn | `FFX_AlBhed_StagePrimerGrantMask` | writes the stage |
| 0x647C90 | fn | `FFX_AlBhed_GetImportedPrimerMask` | returns `dword_1325AC4` |
| 0x6EFEB0 | fn | `FFX_SaveFile_BeginPrimerImport` | save UI states 21..27 |
| 0x6EFD10 | fn | `FFX_SaveFile_ImportPrimerFromSlot` | reads a local file, no CRC |
| 0x820C2D | code | `FFX_MainStep` scan block | stages the imported mask when the scan finishes |
| 0x1325AC4 | glob | `dword_1325AC4`, proposed `g_ffxAlBhedImportedPrimerMask` | `core:605` writes it |
| 0x1325B8C | glob | `dword_1325B8C`, proposed `g_ffxAlBhedImportedPrimerMaskForScript` | |
| 0x1325B90 | glob | `dword_1325B90`, proposed `g_ffxAlBhedPrimerGrantArmed` | |
| 0x1325B94 | glob | `dword_1325B94`, proposed `g_ffxAlBhedPrimerGrantMask` | |
| 0x906780 | fn | `sub_906780`, proposed `FFX_AlBhed_TranslateGlyph` | renderer |
| 0x906730 | fn | `sub_906730`, proposed `FFX_Debug_GetAlBhedDictionaryOverride` | |
| 0x906740 | fn | `FFX_Debug_CycleAlBhedDictionary` | only writer of the override |
| 0x18763E0 | glob | `dword_18763E0`, proposed `g_ffxDebugAlBhedDictionaryOverride` | -1 none, 1 all |
| 0x187626D | tbl | `byte_187626D` / `byte_187626E` | cipher glyph and primer bit, stride 4, language 0 |
| 0x18762A9 | tbl | `byte_18762A9` / `byte_18762AA` | the same for the other languages |
| 0x8B8BB0 | fn | `FFX_MesWin_ApplyAttrToTextObject` | the only writer of text object `+0x10 = 0x4000` |
| 0x87F4A0 | fn | `sub_87F4A0`, proposed `FFX_DebugPage_DrawAlBhedBooks` | debug page |

### Sphere grid

| VA | Type | Name | Note |
|---|---|---|---|
| 0x112EC7C | glob | `g_ffxSphereGridNodes` | `+0x21EC` |
| 0xA49590 | fn | `FFX_SphereGrid_SaveToMenu` | block to menu work, at build |
| 0xA5BB70 | fn | `FFX_SphereGrid_MenuToSave` | menu work to block, the commit |
| 0xA54860 | fn | `FFX_SphereGrid_RecomputeDerived` | rebuilds `EquipStatBonus` |
| 0xA48910 | fn | `FFX_SphereGrid_ActivateNodeForChar` | sets the bit and commits |
| 0xA56160 | fn | `FFX_Menu_SphereGridActivateNode`, proposed rename `FFX_SphereGrid_ConfirmOrCancelMove` | the only S.Lv spend |
| 0x786EF0 | fn | `FFX_SaveData_SpendSphereLevels` | |
| 0x784610 | fn | `FFX_SaveData_AddAp` | the only S.Lv gain |
| 0x8BA390 | fn | `FFX_BtlResult_AddAp` | its only caller |
| 0xA452D0 | fn | `sub_A452D0`, proposed `FFX_SphereGrid_StepUseSpherePage` | item page confirm |
| 0xA5BCA0 | fn | `sub_A5BCA0`, proposed `FFX_SphereGrid_UseSphereOnNode` | spends the item, then the effect |
| 0xA493B0 | fn | `sub_A493B0`, proposed `FFX_SphereGrid_CanUseSphereOnNode` | count and fit |
| 0xA49440 | fn | `sub_A49440`, proposed `FFX_SphereGrid_TestSphereOnNode` | the fit test |
| 0xA5CF30 | fn | `sub_A5CF30`, proposed `FFX_SphereGrid_ApplySphereEffect` | switch on the effect byte |
| 0xA48740 | fn | `sub_A48740`, proposed `FFX_SphereGrid_BeginNodeKindChange` | |
| 0xA47D50 | fn | `sub_A47D50`, proposed `FFX_SphereGrid_StepNodeKindChange` | 20 frames, then commit |
| 0xA458A0 | fn | `sub_A458A0`, proposed `FFX_SphereGrid_ActivateNodeWithSe` | |
| 0xA458C0 | fn | `sub_A458C0`, proposed `FFX_SphereGrid_ClearNodeBitForChar` | |
| 0xA458F0 | fn | `sub_A458F0`, proposed `FFX_SphereGrid_UseWarpSphere` | |
| 0xA48A80 | fn | `sub_A48A80`, proposed `FFX_SphereGrid_BeginWarp` | |
| 0xA47F00 | fn | `sub_A47F00`, proposed `FFX_SphereGrid_StepWarp` | 40 + 12 + 40 frames |
| 0xA56060 | fn | `sub_A56060`, proposed `FFX_SphereGrid_OnQuitConfirm` | commit on quit |
| 0xA47210 | fn | `FFX_SphereGrid_CheckGridTrophies` | trophies 23, 29 |
| 0xA57520 | fn | `FFX_Menu_SphereGridReadPad` | frame counted repeat |
| 0x1135E00 | glob | `EquipStatBonus` | outside the block |

### Celestial weapons

| VA | Type | Name | Note |
|---|---|---|---|
| 0x112D6C9 | off | `maybe_g_ffxSaveWeaponMasterBits`, proposed `g_ffxCelestialWeaponsFound` | `+0xC39` |
| 0x112D6FC | off | no name, proposed `g_ffxCelestialCrestApplied` | `+0xC6C` |
| 0x112D6FD | off | no name, proposed `g_ffxCelestialSigilApplied` | `+0xC6D` |
| 0x85D170 | fn | `FFX_AtelSys_Core_533_SetEquipAbilitiesFromTable_resi` | `core:533` |
| 0x8C3170 | fn | `FFX_Equip_SetAbilitySlotsFromTable` | the table copy |
| 0x8C30E0 | fn | `sub_8C30E0`, proposed `FFX_Equip_FindCelestialWeapon` | `flags & 4` and `forChar == kind` |
| 0x7ABD10 | fn | `sub_7ABD10`, proposed `FFX_SaveData_ListEquipForChar` | |
| 0xC86D00 | tbl | `off_C86D00`, proposed `g_ffxCelestialAbilityRows` | 8-byte rows, bases in 4.4 |
| 0x79C5F0 | fn | `FFX_Btl_LoadCharRecordIntoActor` | weapon formula and power into the actor |
| 0x78E630 | fn | `FFX_Btl_ComputeEffectForTarget` | weapon or command formula |
| 0x789BF0 | fn | `FFX_Btl_DamageFormula` | cases 0x11, 0x12, 0x13 |
| 0x85E820 | fn | `FFX_AtelSys_Core_077_IsPadPressedRemapped_resi` | the C reader of bit 0x20 |
| 0x820F1A | code | `FFX_MainStep` | trophy 27 |
| 0xC494A4 | glob | `g_ffxWeaponMasterAchState` | process local latch |

### Jecht Spheres

| VA | Type | Name | Note |
|---|---|---|---|
| 0x112D70F | off | `maybe_g_ffxSaveJechtSphereStage`, proposed `g_ffxJechtSphereCount` | `+0xC7F` |
| 0x88E070 | fn | `maybe_FFX_Scene_Init` | trophy 20 |
| 0xC58E8A | glob | `g_ffxAchJechtSphereLatch` | process local latch |

## 7. What I corrected in other docs

- `MINIGAMES_TIMED.md` 2.2, 5.2, 5.4, section 10 and 11.1: actor 38 entry 9 is the Celestial Mirror
  upgrade and var57 is a fade level. The butterfly reward is the Saturn Sigil.
- `CHEAT_MODELS.md`, the butterfly hunt row and the gauge table: the same mistake, copied from it.
- `MENU_SYSTEM.md`: `0xA56160` charges a move rather than activating a node, and the 20-byte records
  at `g_ffxMenuWork + 43028` are link records, not node records.
- `CHEAT_SAVEDATA.md` 6.1 and 6.3: no sphere writes `0xFF` into a node. The key and clear effects
  write kind 1, Empty Node. `0xFF` in the block is what `MenuToSave` writes for a menu slot holding
  -1. And the S.Lv spend is the move cost.
- `STEAM_ACHIEVEMENTS.md` 5.1, 5.2 and 7: `+0xC39` and `+0xC7F` are settled.
- `SAVE_BLOCK_MAP.md` 2.2 and `tools/saveblock_fields.py`: the same two rows.
- `ATEL_CORE_LIBRARY.md` rows 269, 270 and 565: the path is settled, tags moved to V.

## 8. What is untested, ranked

1. **Which sphere item has which effect byte.** The code for each effect is read. The names in the
   effect table in 3.3 come from matching the counts against the game's sphere list. Reading
   `sphere.bin` byte `+13` per row next to the item names would settle it.
2. **What clears `word_1A8607E`.** It gates the end of a warp and of a kind change animation. If it
   is stepped from the present thread, frame-by-frame input replication of the grid could commit a
   warp on different frames on two machines. Result replication avoids the question.
3. **Where the butterfly hunt keeps its catch count.** Section 4.6 says where it is not.
4. **How the move cost at `g_ffxMenuWork + 71196` accumulates.** The charge is read, the per-node rule
   is not.
5. **Which window style passes attribute `0x4000`.** It decides which message windows get the Al Bhed
   treatment. Presentation only.
6. **Why `luca0400` a24 e5 clears Wakka's found bit and swaps the two mirrors.** Likely a Blitzball
   reset path, not read.
7. **The Jupiter Sigil's prize logic in `bltz0200`.** I found the id in a prize array and stopped,
   Blitzball belongs to another doc.
