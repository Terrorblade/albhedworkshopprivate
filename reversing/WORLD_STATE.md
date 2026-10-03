# World state: can a client be handed the host's save block?

The question this answers: can a joining player be given the host's entire 26,816-byte save state at
runtime and end up in a consistent world, and what is the game's own path for doing that.

The short answer is yes, and the game already does it in three places with nothing more than a
memcpy. The parts that need care are two re-derive calls, one flag byte, and a map request. The long
answer is below.

Everything here was read out of the IDB. Each claim is marked **confirmed** when the binary or the
shipped data proves it and **inferred** when it is my reading. Addresses are VAs at the preferred
base, so RVA = VA - 0x400000. Save-block offsets are written `+0x...` and are relative to
`g_ffxSaveData 0x112CA90`.

Read `GAME_STATE.md` first for the second half of the block (party, inventory, equipment, character
records). This document covers the first half, which `GAME_STATE.md` left as "header" and "Progress,
15,436 bytes, unidentified", plus everything outside the block that depends on it.

## Premise corrections, up front

Three of the brief's premises are wrong, and the corrections are worth more than the confirmations.

1. **"FFX tracks story and world progress in a large bit array."** It does not. There *is* a
   dedicated 640-bit event-flag array with test, set and clear syscalls, and the shipped event
   scripts use it **three times in total, all three of them sets, across all 397 event packages**.
   The real world state is 8,192 bytes of **typed script variables** (mostly `u8`) at
   `+0x1EC`, addressed by compiled byte offsets with no accessor function anywhere. See section 7.
   This matters for the mod: there is nothing to hook, and nothing to diff per-bit. The only
   sensible unit of replication is the byte range.

2. **"`+0xB8` is the current map id."** It is the **checkpoint** location, not the live one, and it
   is two fields packed in a dword: the high word `+0xBA` is the map id and the low word `+0xB8` is
   the entry-point index. The live map id is the word at `+0x00` and the live entry point is the
   byte at `+0x0C`. `GAME_STATE.md` and `FFX_GAME_NOTES.md` both say "current map id +0xB8", and
   `GameState.h`'s hash buckets treat `+0xB8..+0xBC` as "MapId". The address is right, the meaning
   is not. I left a `NAME IS MISLEADING` comment on `g_ffxCurrentMapId 0x112CB48` rather than
   renaming it, because other agents are in the database. Suggested name
   `g_ffxSaveCheckpointLoc`.

3. **"look for a compression step."** There is none. The save file is 26,880 plain bytes written
   with `fwrite`. zlib is in the binary but it belongs to the VBF archive reader, not to saves.

One of the brief's premises is right in a way worth stating plainly: **the naive "memcpy the host's
26,816 bytes over the client's" is literally what the game does**, in three separate places, and one
of them is a script opcode.

---

## 1. The game's own save-file load path

### The file

**Confirmed.** `<dataroot>/FFX_Data/GameData/PS3Data/saves/%02d.SAV`, plain `fopen` / `fread` /
`fwrite`, **26,880 = 0x6900 bytes**, no compression and no encryption.

```
+0x0000  0x40 bytes   header
+0x0040  0x68C0 bytes byte-for-byte copy of the live block at g_ffxSaveData
```

The 0x40 header, from `FFX_SaveFile_Serialize 0x8B3E60` (was `sub_8B3E60`):

| off | size | field |
|---|---|---|
| 0x00 | dword | monotonic save counter, `g_ffxSaveCounter 0x186627C`, clamped at 0x80000000 |
| 0x04 | byte | save kind, passed in by the caller |
| 0x05..0x07 | 3 bytes | the three active party members, copied from block `+0x3D58`, 0xFF padded |
| 0x0C | byte | language the save was written in |
| 0x10 | dword | playtime, copied from block `+0xBC` |
| 0x14 | dword | gil |
| 0x18 | word | map-name id for the slot description |
| **0x1A** | **word** | **CRC-16** |
| 0x1C | word | **version, hardcoded 256**. Nothing in the binary reads it back. |
| 0x1E | byte | Rikku alternate outfit, block `+0xD1` |
| 0x1F | byte | `sub_7851D0()`, which is a `return 0` stub |
| 0x20.. | string | character name 0 (Tidus, as renamed by the player) |

The slot list is cached in `g_ffxSaveSlotHeaders 0x159EC70`, 99 entries of the same 64 bytes,
indexed by `FFX_SaveFile_GetSlotHeader 0x8B4E40`.

### The checksum

**Confirmed.** `FFX_Crc16Ccitt 0x8B1450` (was `sub_8B1450`) is CRC-16/CCITT: a 256-entry table built
on the stack from polynomial 0x1021, initial value 0xFFFF, result one's-complemented. Six call
sites, all save integrity.

The CRC is stored **twice**: at header `+0x1A` and at **block `+0x64B4`** (`g_ffxSaveBlockCrc`). The
block's own slot is zeroed before the CRC is computed, so it covers itself as zeros.

**Confirmed, and it is a genuine inconsistency in the shipped binary: the two save layers use
different CRC lengths.**

| site | length |
|---|---|
| `FFX_SaveFile_Serialize 0x8B3E60` (in-game save) | 26,816 = 0x68C0, the whole block |
| `sub_8B15D0` at 0x8B3040 (module 15/16 load verify) | 26,816 = 0x68C0 |
| `FFX_SaveFile_WriteSlot 0x646DF0` (the PC .SAV writer) | 25,784 = 0x64B8 |
| `FFX_SaveFile_VerifyCrc 0x647D70` | 25,784 = 0x64B8 |
| `FFX_SaveFile_StampCrc 0x648F20` | 25,784 = 0x64B8 |

0x64B8 is exactly `+0x64B4 + 4`, i.e. "up to and including the checksum slot". 0x68C0 is the whole
block. **Inferred:** the PS2/PS3 code used 0x64B8 because the block used to end there, the block grew
to 0x68C0, and only the newer in-game save path was updated. The practical consequence is that the
PC path re-stamps the CRC on write (`FFX_SaveFile_WriteSlot` zeroes the slot and recomputes before
every `fwrite`), so whatever the in-game serializer put there is overwritten.

`FFX_SaveFile_VerifyCrc` skips the entire check when `maybe_g_ffxSaveCrcSkip 0xCCB9A4` is non-zero.
I did not trace who sets it.

### The install

**Confirmed, and this is the heart of the answer.**

```c
// FFX_SaveFile_InstallBlock 0x8B54A0  (was sub_8B54A0)
unsigned char FFX_SaveFile_InstallBlock(void *dest, int saveImage)
{
    memcpy(dest, (const void *)(saveImage + 64), 0x68C0u);
    return FFX_SaveData_ReencodeCharNames();
}
```

That is the whole install step. A memcpy and a text re-encode. No pointer fixup, no unpacking, no
per-field copy, no table rebuild.

`FFX_SaveData_ReencodeCharNames 0x787470` (was `sub_787470`) walks the 18 x 20-byte name records at
`+0x634C`, re-encoding each name for the running language (record+18 is the language the name was
typed in, record+19 is the player-renamed flag), and patches two localised string buffers when the
scenario word has passed 0x5A and 0x604. Pure text, no gameplay state. **Two machines running the
same language can skip it entirely.**

### The three transplant sites

**Confirmed.** Every place in the binary that installs a whole 0x68C0 block:

| site | what it does | re-derives |
|---|---|---|
| `FFX_SaveFile_CommitLoad 0x8B4EC0` (PC Load Game) | `FFX_SaveFile_InstallBlock` + the sequence below | yes |
| `sub_8B15D0` (PS3-era modules 15 and 16) | CRC verify, then the same install | yes |
| `FFX_Debug_ApplyViewerSave 0x8B55E0` | memcpy, then warp | **no** |
| `FFX_AtelSys_Save_087_resi 0x8781F0` | `memcpy(&g_ffxSaveData, arg + 64, 0x68C0)` and nothing else | **no** |

The last one deserves a sentence of its own. **The game ships a script opcode that performs the
exact transplant the mod wants.** Library 12 function 87. Its body is a pop, a stubbed printf, the
memcpy, and `return 1`. I scanned all 397 shipped `.ebp` packages for it: it is used **once**, by
`ffx_ps2/ffx/master/jppc/event/obj/te/test22/test22.ebp`, a developer test map, alongside library 12
functions 84, 85 and 86. So it is a dev facility, not a shipped mechanic, but it is live code in the
retail exe and it proves the block is self-contained.

`FFX_Debug_ApplyViewerSave` is the closest thing to a worked example of what the mod needs:

```c
if (FFX_Debug_LoadSaveForViewer(id, &g_ffxSaveFileStaging, 0x6900) == 0) {
    memcpy(FFX_GetSaveData(), &g_ffxSaveFileStagingBlock, 0x68C0);
    FFX_Map_WarpTo(HIWORD(g_ffxCurrentMapId), (unsigned short)g_ffxCurrentMapId);
}
```

Note that the destination map comes out of the block that was just transplanted. That is the whole
late-joiner flow in two statements.

### What `FFX_SaveFile_CommitLoad 0x8B4EC0` does beyond the memcpy

**Confirmed.** This is the answer to "what else has to be re-derived", in call order:

| step | call | effect |
|---|---|---|
| 1 | `FFX_SaveFile_InstallBlock(g_ffxSaveData, g_ffxSaveFileStaging)` | the memcpy plus the name re-encode |
| 2 | `FFX_Menu_ApplySaveConfigBit5EC 0x8CC170` | pushes block `+0x5EC` bit 0 into the engine via `sub_679FE0` |
| 3 | `sub_8DE080(8200)` then `sub_8AAEB0` | selects a menu page, cosmetic |
| 4 | `sub_8724F0 0x8724F0`, `sub_8724A0 0x8724A0` | apply the sound and display config bits held in `+0x3D0C` |
| 5 | `sub_8AF4F0(1, "scene3" / "scene33" / "scene34")` | drops three menu scenes |
| **6** | **`FFX_SphereGrid_RecomputeDerived 0xA54860`** (through the thunk `0x8A97E0`) | **the real re-derive, see section 3** |
| 7 | `TOSwapInternationalEnvExec 0x8B0500` | only when the saved language differs from the running one |
| 8 | `FFX_Module_Suspend(16)` | closes the Load screen |

**And note what is not there.** No map warp. No `FFX_SaveData_RebuildItemLists`. No
`FFX_SaveData_RebuildEquipOwners`. No `FFX_Btl_SetupUnitRoster`. The warp is a separate deferred
request (section 6), the equipment owner bytes are inside the block already, the item use-lists are
rebuilt lazily, and the battle roster is rebuilt at battle start.

The PC path reaches `FFX_SaveFile_CommitLoad` from `FFX_SaveUi_Step 0x81FF80` state 4, gated on
`sub_822480()` which is a `return 2` stub, so it always fires. **The PC load path never checks the
CRC.** Only the PS3-era module 15/16 screens and `FFX_SaveFile_VerifyCrc` do.

---

## 2. Is the 26,816-byte block self-contained?

**Confirmed: yes, completely. It holds no pointers and can go on the wire as-is.** Four independent
checks, any one of which would be suggestive and which together are conclusive.

1. **It is BSS.** `ida_bytes.is_loaded(0x112CA90)` is false and every one of the 6,704 dwords in the
   range reads back as uninitialised. The block has no initialised image on disk, so no static
   initialiser can have placed a pointer in it.
2. **No relocations.** `ida_fixup` reports zero fixups anywhere in `0x112CA90..0x1133350`. If the
   linker had put an address in there, a relocation would exist, because the exe is ASLR-relocated
   (`DYNAMIC_BASE`, full `.reloc`).
3. **No instruction stores an address into it.** I scanned every `mov [mem], imm32` in all 130
   functions that call `FFX_GetSaveData` and in all 141 functions that reference the block directly,
   filtering for immediates in the image range. Six hits, all benign: two write a stack local, two
   write the constant `0x01010101` into a battle actor, one writes `g_ffxAtelCtx`, and one is the
   decompiler trap below.
4. **It round-trips through a file.** The block is `fwrite`-ten and `fread`-back byte for byte under
   ASLR with no fixup pass. A stored pointer would be a stale address from another process image
   base on the very next load.

**One decompiler trap to be aware of.** `FFX_Debug_ApplyViewerSave` decompiles as
`g_ffxCurrentMapId = (int)&unk_F60000;`. That is the immediate `0x00F60000`, which is
`(246 << 16) | 0`, i.e. map 246 entry point 0. IDA renders any immediate that lands in a segment as
`&unk_...`. It is not a pointer. If you grep decompilations for `&unk_` inside save-block writes,
this is the only hit and it is a false positive.

**One field that is not plain data in the useful sense:** `+0x64B4`, the CRC. It is written only
into the file copy, never into the live block, so a live block holds whatever CRC it was loaded with
and a new-game block holds zero. It is dead at runtime but it is inside the 26,816 bytes, so a
byte-for-byte comparison between two peers will see it. If a peer started from a save file and the
other received a transplant, they agree. If one started a new game and the other loaded, they will
differ by two bytes forever. Put it in its own hash bucket or skip it.

---

## 3. Derived state that lives outside the block

**Confirmed.** The complete list of state computed from the block but stored elsewhere, with the
function that rebuilds each.

| global | size | what it is | rebuilt by |
|---|---|---|---|
| `g_ffxEquipStatBonus 0x1135E00` | 8 x 28 | per-character sphere-grid stat totals and learned-ability bitmap | `FFX_SphereGrid_RecomputeDerived 0xA54860` |
| `g_ffxCharDerivedBytes 0x2311240` | 18 x 4 | per-character derived bytes | `FFX_SaveData_RecomputeCharDerived 0x7860F0` |
| `g_ffxEncountersEnabled 0x112A9D7` | 1 | master "random encounters on" flag | `FFX_SaveData_RecomputeAllCharDerived 0x786900`, then ANDed per party member |
| battle item use-lists `0x112C6B6` and `0x112C796` | 112 words each | the item lists the battle and field menus show | `FFX_SaveData_RebuildItemLists 0x7906A0`, called by `FFX_SaveData_AddItem` and by `FFX_Btl_BeginBattle 0x781020` |
| `g_ffxBattlePartyOrder 0x112C895` | 7 bytes | the battle lineup | `FFX_Btl_SetupUnitRoster 0x79C110` at battle start |
| battle actor arrays `0x11334D4`, `0x113446C` | 31 x 0xF90, 62 x 912 | only valid in battle | `FFX_Btl_LoadCharRecordIntoActor 0x79C5F0` |
| `g_ffxMenuCharList 0x1841C14` and the menu work buffer | | the menu's own copies | menu module start |

### The one call that matters: `FFX_SphereGrid_RecomputeDerived 0xA54860`

**Confirmed.** Was `sub_A54860`. This is what the save load runs, and it is the single call a co-op
client has to make after a transplant.

```c
FFX_SaveData_ClearEquipStatBonus();            // zero g_ffxEquipStatBonus
for (char = 0; char < 7; ++char) {
    for (node = 0; node < 1024; ++node)        // g_ffxSphereGridNodes, block +0x21EC
        if (nodeWord[node].highByte & (1 << char)) {
            row = FFX_KernelTable_GetRow(nodeWord[node].lowByte, panelTable);
            // add row[20] into the ten stat buckets selected by the bitmask at row+16
            FFX_SaveData_SetEquipStatBonusAbility(char, row[18]);
        }
    // store the ten clamped totals into g_ffxEquipStatBonus[char]
}
return FFX_SaveData_RecomputeAllCharDerived();
```

Seven callers: the save load (twice through `sub_8B15D0`, once through `FFX_SaveFile_CommitLoad`),
`FFX_SphereGrid_InitNodes` on a new game, and three sites inside the Sphere Grid screen.

`FFX_SaveData_RecomputeAllCharDerived 0x786900` then runs
`FFX_SaveData_RecomputeCharDerived 0x7860F0` for all 18 character records. That one writes, per
record:

- `+0x24` max HP including bonuses, clamped to 9999 or 99999 depending on a flag word bit
- `+0x28` max MP including bonuses, clamped to 999 or 9999
- `+0x2F..+0x36` the eight effective stats, clamped
- `+0x3E..+0x48` ability flag words ORed in from the equip stat bonus cache
- `+0x4A..+0x4E` ORed from the four auto-abilities of the equipped weapon and armour
- `+0x1C` and `+0x20` current HP and MP re-clamped to the new maxima
- `g_ffxCharDerivedBytes[4 * index]` and `g_ffxEncountersEnabled`, both outside the block

For the ten aeons (index 8 and up) the stats are derived from Yuna's through a kernel table row and
the aeon power counter at `+0x3D14`. For Lulu (index 5) it also mirrors ability ids 0x3041..0x3053
onto 0x3078..0x308A.

**A quirk worth knowing.** `FFX_Btl_Init 0x781700` calls `FFX_SaveData_ClearEquipStatBonus` and
never refills it. So the battle system zeroes the grid bonus cache and relies on something else to
rebuild it. In the shipped game that something else is the Sphere Grid screen or the next save load.
I did not establish whether this is a bug in the retail build or whether a path I did not find
refills it. **Inferred, flagged as a known unknown.**

---

## 4. What identifies "where in the world" the save is

**Confirmed.** The save block's first 16 bytes are a map-transition record, and the location is
`(map id, entry-point index)`, never coordinates.

| off | size | field | accessor |
|---|---|---|---|
| +0x00 | word | **live map id** | `FFX_SaveData_GetMapId 0x88D660` |
| +0x02 | word | previous map id | `FFX_SaveData_GetPrevMapId 0x88D600` |
| +0x04 | word | scene id | `FFX_SaveData_GetSceneId 0x88D690` |
| +0x06 | word | previous scene id | `FFX_SaveData_GetPrevSceneId 0x88D630` |
| +0x08 | word | scene sub id | `FFX_SaveData_GetSceneSubId 0x88D680` |
| +0x0A | word | previous scene sub id | `FFX_SaveData_GetPrevSceneSubId 0x88D620` |
| +0x0C | byte | **live entry-point index** | `FFX_SaveData_GetEntryPoint 0x88D670` |
| +0x0D | byte | previous entry point | `FFX_SaveData_GetPrevEntryPoint 0x88D610` |
| +0x0E | word | map-name id for the save-slot description | `FFX_SaveData_GetMapNameId 0x86C410` |
| +0x11 | byte | fade-in frames | `FFX_SaveData_SetFadeInFrames 0x8700C0` |
| +0x12 | byte | fade-out frames | `FFX_SaveData_SetFadeOutFrames 0x8700D0` |
| +0x13 | byte | field flags, bit 1 = encounters allowed | `FFX_Atel_GetFieldStepAllowsEncounter 0x86A9D0` |
| +0x2A | byte | **"this block came from a save file"**, see below | `FFX_SaveData_MarkLoadedFromFile 0x88EA50` |
| +0xB4, +0xB6 | 2 words | per-map pair, written once per map by ATEL Core syscall 366 | `sub_873AE0`, `sub_873AF0` |
| +0xB8 | word | **checkpoint entry-point index** | together `g_ffxCurrentMapId 0x112CB48` |
| +0xBA | word | **checkpoint map id** | |
| +0xBC | dword | playtime | `FFX_SaveData__setPlaytime 0x787610` |
| +0xC0..+0xCF | 16 | location history, `g_ffxLocationHistory` | `FFX_SaveData_SnapshotLocationHistory 0x88E800` |
| +0xBEC | word | **scenario / story progress counter** | `FFX_SaveData_GetScenarioWord 0x86C470` |

### How the entry point becomes a position

**Confirmed.** The byte at `+0x0C` is an index into the loaded event package's warp table, at
`atel + *(atel + 4) + 32 * index`, read by `sub_86BFE0`, `sub_86C020` and `sub_86ED80`. The XYZ for
a map entry lives in the `.ebp` file, not in the save. **The save identifies a location to within a
named doorway, not to within a metre.** A late joiner placed by the engine lands on the same door the
host last walked through, which is usually not where the host is standing now. Putting a joiner at
the host's exact position is a separate problem and belongs to the CHR layer the Phase 1 work owns.

### The location history and the `+0x2A` flag

**Confirmed, and this is the most useful small discovery in this document.**

`FFX_SaveData_SnapshotLocationHistory 0x88E800` copies the block's first 14 bytes into `+0xC0`:

```
word +0xC0 <- word +0x00 (map)     word +0xC2 <- word +0x02 (prev map)
word +0xC4 <- word +0x04 (scene)   word +0xC6 <- word +0x06 (prev scene)
word +0xC8 <- word +0x08           word +0xCA <- word +0x0A
word +0xCC <- byte +0x0C (entry)   word +0xCE <- byte +0x0D (prev entry)
```

`FFX_SaveData_MarkLoadedFromFile 0x88EA50` sets `byte +0x2A = 1`. `FFX_SaveFile_Serialize` calls it
**on the copy that goes into the file**, so every save file and therefore every freshly loaded block
has it set. The only consumer is `FFX_SaveData_SetSceneAndSub 0x88EAF0`: while the byte is set, the
"previous scene" fields come from the `+0xC0` history instead of from the live words, and then it
clears the byte.

So the game has its own **"I was just transplanted, fix up my transition state once"** flag. A mod
that memcpys a block in at runtime should set `+0x2A = 1` for exactly the same reason, and will get
the same correct behaviour for free.

### Where the checkpoint is written

**Confirmed.** Two writers.

- `maybe_FFX_Scene_Init 0x88E070` writes `+0xB8 = byte +0x0C` and `+0xBA = word +0x00` on **every**
  real field map entry, i.e. every map that is not 23, 282, 194, 348, 349, 368, 255, 209, 212, 270,
  320, 119 or 247 and not title or boot mode. It also calls `FFX_SaveData_SnapshotLocationHistory`
  and `FFX_SaveFile_StageAndDescribe 0x8B50B0`, which refreshes the whole save image in
  `g_ffxSaveFileStaging`.
- ATEL Core syscall 367 (`FFX_AtelSys_Core_367_resi 0x85C430`) writes `+0xB8` from a script-supplied
  entry point and `+0xBA` from the current map, then snapshots the history. **634 uses across 104 of
  the 397 shipped event packages.** This is the save-sphere and story-checkpoint writer. Syscall 368
  does the same with both values from the script and is used zero times.

**So `+0xB8`/`+0xBA` are never more than one map behind the player.** A transplanted block always
carries a usable "put me here" answer.

---

## 5. Per-map transient state

**Confirmed, and the answer is cleaner than expected: there is no separate per-map state block that
needs replicating, because everything per-map is reconstructed from the archive on every map load.**

Two allocations hold all of it:

| global | what | lifetime |
|---|---|---|
| `g_ffxEvPackageBase` | the loaded event (`.ebp`) package image | freed by `maybe_FFX_Map_Unload 0x872D20`, re-read from the archive by `FFX_Ev_LoadEventPackage 0x872EF0` |
| `g_ffxAtelActorPool` | the per-map ATEL actor pool, size from `FFX_Atel_CalcActorPoolSize 0x86BD40` | same |

The ATEL variable storage classes map onto them, and the numbers below are measured over all 397
shipped packages:

| class | resolves to | descriptors | reads | writes | lifetime |
|---|---|---|---|---|---|
| 0 | `*(ctx+0x2C) + off` = **`g_ffxSaveData + 0x1EC`** | 3,278 | 104,548 | 59,332 | **persistent, in the save block** |
| 1 | `*(ctx+0x30) + off` | 0 | 0 | 0 | never set to anything but zero anywhere in the binary |
| 2 | `atel + off + actorDef[0x28]` | 0 | 0 | 0 | per map |
| 3 | ctx callback, else `atel + off + actorDef[0x2C]` | 8,921 | 94,351 | 34,412 | per map |
| 4 | `atel + off + actorDef[0x30]` | 1,023 | 79,807 | 1,797 | per map |
| 5 | `actor + 0x48 + off` | n/a, opcode-addressed | | | per actor instance |
| 6 | `atel + off + *(atel+0x20)` | 5,815 | 1,685,960 | 575,939 | per map |

Classes 2, 3, 4 and 6 all land inside the package image and class 5 inside the actor pool. **Only
class 0 survives a map change.** The decisive detail is `sub_88D2C0`, which
`FFX_Ev_LoadEventPackage` uses to load the package: it always `FFX_MemAlloc`s a fresh buffer and
always reads the asset. There is no cache, so returning to a map you have already visited gives you
the packaged initial values again.

**So chests already opened, doors, switches, NPC flags and anything else that has to survive leaving
and re-entering a map must be class-0 variables, i.e. they are inside the save block.** The chest
syscalls `FFX_AtelSys_Core_347_start 0x85A8A0` (849 uses in 134 packages) and
`FFX_AtelSys_Core_423_start 0x857B70` (7 uses) do not record "opened" themselves. The script sets a
class-0 variable afterwards. **Inferred** from the division of labour, but it is forced: there is
nowhere else for the state to live.

**A battle is not a map change.** `g_ffxMapChangePending` has exactly four references in the whole
binary, and the only writers are `FFX_Map_RequestChange` and
`FFX_Map_RequestResumeFromCheckpoint`. Neither is on the battle path. So the field package and the
actor pool survive an encounter and the field is still there when you come back. **Confirmed.**

`maybe_FFX_Map_Unload 0x872D20` touches only three save-block fields on its way out: it zeroes the
dword at `+0xE4`, zeroes the word at `+0xE8`, and clears the map-name override at `+0xD6`. **It does
not clear the script work area.**

---

## 6. The smallest set of writes that moves a player to a given map

**Confirmed. It is a deferred request, which is the good case, and it is five stores plus a flag.**

```c
// FFX_Map_RequestChange 0x88EA60   (was sub_88EA60)
void FFX_Map_RequestChange(int mapId, char entryPoint)
{
    saveData.byte[0x0D] = saveData.byte[0x0C];   // previous entry point
    saveData.word[0x02] = saveData.word[0x00];   // previous map id
    saveData.word[0x00] = mapId;
    saveData.byte[0x0C] = entryPoint;
    g_ffxMapChangePending = 1;                   // 0x133084C
}
```

Nothing loads here. One simulation step later `FFX_Atel_StepOnce 0x88D3D0`, which runs inside
`FFX_MainStep`, sees the flag, counts down `g_ffxMapChangeDelayFrames 0x1330850`, zeroes
`g_ffxMapChangeDelayFlag 0x1330854` on the step after that, and then calls
`maybe_FFX_Scene_Init 0x88E070`, which does the real work and clears the flag. **The load therefore
happens on the simulation thread, inside the step, at a step boundary both peers can agree on. That
is exactly the shape the lockstep design wants.**

Three special cases in `FFX_Map_RequestChange`:

- `mapId == g_ffxQuitPseudoMapId 0xC33480` (a constant 0x18F = 399) only raises `dword_1327108 = 1`,
  which `FFX_MainStep` picks up. It means "leave the field", not "load map 399".
- `mapId == 23` (the title) also runs `sub_645310` and `nullsub_62`.
- **A negative `mapId` means "go to the checkpoint"**: the map comes from `+0xBA` and the entry point
  from `+0xB8`.

### The layers above it

| VA | name | what |
|---|---|---|
| `0x88EB50` | `FFX_Map_SetTransitionFrames` | sets both delay counters, i.e. "wait n simulation steps before loading" |
| `0x86FEC0` | `FFX_Map_WarpTo` | the full warp: unbind the player CHR, request the change, set the screen fade rate, set the transition frames |
| `0x86FF40` | `FFX_Map_WarpToWithSavedFade` | the same, taking the fade length from `+0x11`/`+0x12` |
| `0x88DD60` | `FFX_Map_RequestResumeFromCheckpoint` | **"put me where the save block says I am"**, see below |

### The debug warp the brief hoped for exists

**Confirmed.** Three of them, and all three go through `FFX_Map_WarpTo`.

1. **The SG developer GUI's map jump.** `maybe_SG_DebugGui_MapJumpItemProc 0x854DF0` reads the
   selected map out of the list widget, stores it in `g_ffxDebugMapJumpTarget 0x1325928`, and calls
   `FFX_Map_WarpTo(target, 0)`. `maybe_SG_DebugGui_MiscItemProc 0x854C60` item 46 repeats the jump
   with the stored target. This is the PS2 debug menu map jump `FFX_GAME_NOTES.md` pointed at.
2. **`FFX_AutoTest_JumpMap 0x908100`**, the handler for the `AutoTestManager` stdin command
   `JumpMap <mapname>`. It substring-searches a 16-byte-stride map-name table at `dword_25D5888`
   (count `dword_1934EC8`, map id at row+12) and calls `FFX_Map_WarpTo(mapId, 0)`. The literal name
   `grid00` maps to map 0. `FFX_GAME_NOTES.md` already noted that `AutoTestManager` is ungated in
   retail, so **this one is reachable today by attaching stdin, with no patching at all.**
3. **`FFX_Debug_ApplyViewerSave 0x8B55E0`**, covered in section 1.

**One trap in `FFX_Map_WarpTo`, and it matters.** The function begins

```c
if (*(char *)g_ffxAtelCtx >= 0 && !FFX_IsDebugMode()) return;
```

and then clears that same bit (`*(BYTE *)g_ffxAtelCtx &= ~0x80`). So the gate wants bit 0x80 of the
ATEL context's byte 0 set, and the call consumes it. A second call in the same context is silently
refused unless `g_ffxDebugMode 0x133C910` is 1. **`FFX_Map_RequestChange` has no gate of any kind**,
so a mod should call that directly and set the fade itself.

### The resume primitive, already written and orphaned

(Called "the late-joiner primitive" in an earlier draft. It is not one. It goes to the CHECKPOINT,
not to where the host is, so a late joiner wants the pending flag on its own instead. See "Why step
6 is not resume-from-checkpoint" above.)

**Confirmed.** `FFX_Map_RequestResumeFromCheckpoint 0x88DD60` is:

```c
if (g_ffxQuitPseudoMapId == -1) { dword_1327108 = 1; }
else {
    entry = saveData.word[0xB8];   map = saveData.word[0xBA];
    saveData.word[0xB8] = 0;                      // consume the checkpoint
    saveData.word[0x02] = saveData.word[0xC2];    // previous map, from the history
    saveData.word[0x00] = map;
    saveData.byte[0x0C] = entry;
    saveData.byte[0x0D] = saveData.byte[0xCE];
    g_ffxMapChangePending = 1;
}
g_ffxSceneLoaded = 0;
```

One live caller, `tklib__f88D710` from `FFX_MainInit`, i.e. the boot path. **Plus two orphaned
callers with zero xrefs anywhere in `.text`, `.rdata` or `.data`:**

- `maybe_FFX_Map_ResumeFromCheckpoint_ORPHAN 0x88E9E0`, which is
  `FFX_Pad__resetAllPorts(); nullsub_129(); FFX_Pad__resetAllPorts();
  FFX_Map_RequestResumeFromCheckpoint(); dword_1330848 = 1;`
- `maybe_FFX_Map_ResumeFromCheckpoint_ORPHAN2 0x88EC30`, the same minus one pad reset

and a third orphan, `maybe_FFX_Map_UnloadIfLive_ORPHAN 0x88E9C0`, which is the standalone unload.
Same situation as `SG_DebugGui_Open_ORPHAN 0x84F100`: dead code the linker kept. **The game already
contains "reset the pads and go to the location in the save block" as a callable function, with
nothing calling it.** That is precisely the post-transplant step a joining client needs.

---

## 7. The world state store: `g_ffxSaveData + 0x1EC`, 8,192 bytes

This is the most important section and it is where the brief's premise needed correcting.

### The two bit arrays that exist

**Confirmed.** There really are dedicated flag bit arrays in the block, and they are small and
barely used.

| address | size | API | uses in the shipped scripts |
|---|---|---|---|
| `+0x18` `g_ffxSaveHeaderBits18` | 8 bytes, **64 bits** | test `FFX_SaveData_TestHeaderBit18 0x86B680`, set `FFX_SaveData_SetHeaderBit18 0x8700E0` | no script syscall, C code only |
| `+0x4C` `g_ffxEventFlagBits` | 80 bytes, **640 bits** | ATEL Core 568 test (`0x85EB30`), 569 set (`0x85E8B0`), 305 clear (`0x85E760`) | **568 zero times, 569 three times, 305 zero times** |
| `+0x3DCC` `g_ffxWorldFlagBits3DCC` | 128 bytes, **1024 bits** | test `FFX_SaveData_TestWorldFlag3DCC 0x86B9E0`, set/clear `FFX_SaveData_SetWorldFlag3DCC 0x8701F0`, pointer `0x785250`, plus byte reads from two Movie syscalls | C code and the Movie library |
| `+0x3E4C` `g_ffxWorldFlagBits3E4C` | 128 bytes, 1024 bits | pointer accessor `0x785260` has **zero callers** | nothing |

Each size is pinned by the bound check in its own setter: `a1/32 >= 2` for the first,
`idx >> 5 < 0x14` for the second, `byteIndex >= 128 -> 127` for the third. The three uses of Core 569
are in `guad0000.ebp`, `ikai0600.ebp` and `isho0000.ebp`.

The arithmetic closes on the third one: `+0x3DCC + 0x80 = +0x3E4C` and `+0x3E4C + 0x80 = +0x3ECC`,
which is `g_ffxItemIds`. So those two 128-byte arrays butt up against the inventory with nothing in
between.

### Where the world state actually is

**Confirmed, from the binary and cross-checked against all 397 shipped event packages.**

`FFX_Atel_InstallContextCallbacks 0x8711D0` installs the ATEL variable bases into each context, and
one line is the whole answer:

```c
if (*((_DWORD *)ctx + 11) == 0)                 // ctx + 0x2C
    *((_DWORD *)ctx + 11) = FFX_GetSaveData_thunk() + 492;
```

492 = 0x1EC. `FFX_Atel_ResolveVarAddress 0x86C2E0` resolves variable storage class 0 as
`*(ctx + 0x2C) + offset`. **So every persistent script variable in the game is a plain typed field at
`g_ffxSaveData + 0x1EC + offset`.**

The region is `g_ffxAtelScriptWork`, **`+0x1EC .. +0x21EC`, exactly 0x2000 = 8,192 bytes**, ending
precisely where `g_ffxSphereGridNodes` begins. Four independent confirmations of the base and the
extent:

1. The `+ 492` above. **Confirmed from the binary.**
2. The dev-only syscalls `FFX_AtelSys_Save_000_resi` and `_001_resi` dump and restore
   `8684 = 0x21EC` bytes from the block base to a file called `atelsaveram000520.dat`. That is the
   header plus the script area and nothing beyond. **Confirmed**, though both calls are behind
   `sub_887D10()` which returns a constant 0, so they are dead in retail.
3. `FFX_SaveData_InitPersistentScriptFields 0x7845B0` initialises `+0x11EC` (0x78 bytes) and
   `+0x157E` (0x3C bytes) on a new game. Those are class-0 offsets **0x1000** and **0x1392**, and
   they are the 9th and 11th most referenced class-0 offsets in the shipped scripts, with 55 and 54
   descriptors each. Two independent sources agreeing on two specific offsets.
4. The scenario word. `+0xBEC` is class-0 offset **0x0A00**, and **331 of the 3,278 class-0
   descriptors point at it**, more than three times the next most popular. The known story progress
   counter is the hottest script variable in the game, which is exactly what it should be.

### How big and how busy

**Confirmed**, by parsing the variable descriptor table of all 397 `.ebp` packages with
`tools/ebp.py`:

- 3,278 class-0 descriptors
- declared offsets span **0x0000 .. 0x1A00**, which is 6,656 bytes, comfortably inside the 8,192-byte
  window. The highest byte any shipped script declares is `g_ffxSaveData + 0x1BEC`.
- 3,978 distinct bytes are declared across 373 distinct offsets, in 50 runs separated by gaps
- types: 2,139 `u8`, 544 `s32`, 523 `u16`, 49 `f32`, 23 `s8`. **There is no bit-array API because
  there are no bit arrays here, just small integers.**
- traffic: 104,548 reads and 59,332 writes, counted over the `pushvar` / `pushvar.ix` /
  `pushvarref` and `storevar` / `storevar.ix` opcode sites

The busiest class-0 offsets, which is a reasonable shortlist of "the fields that matter most":

| class-0 offset | block offset | descriptors |
|---|---|---|
| 0x0A00 | +0x0BEC | 331 (the scenario word) |
| 0x0A88 | +0x0C74 | 129 |
| 0x0A68 | +0x0C54 | 109 |
| 0x0A6C | +0x0C58 | 107 |
| 0x0A98 | +0x0C84 | 103 |
| 0x0A4A | +0x0C36 | 100 |
| 0x1266 | +0x1452 | 58 |
| 0x141A | +0x1606 | 56 |
| 0x1000 | +0x11EC | 55 |
| 0x10F0 | +0x12DC | 55 |

The cluster around block `+0xC30..+0xC90` is clearly the main progress block. Three of its bytes
already have C-code readers: `+0xC39` is tested with `& 0x20` by `FFX_MainStep` and
`FFX_AtelSys_Core_077_resi`, `+0xC7F` is read by `maybe_FFX_Scene_Init`, and `+0xC89` by
`sub_787340`.

### Is it ever cleared?

**Confirmed: not during play.** `FFX_SaveData_ClearScriptWorkArea 0x86D4E0` does
`memset(g_ffxSaveData + 0x1EC, 0, 4096)`, which wipes the first half. It has exactly two callers:
`FFX_SaveData_ResetHeaderAndScriptWork 0x88DF40`, whose only caller is `FFX_InitNewSaveData`, and the
SG developer GUI item 81. **It is not on the map-load path.** `maybe_FFX_Map_Unload` does not clear
it, `maybe_FFX_Scene_Init` does not clear it, and `FFX_Ev_LoadEventPackage` calls `sub_86D4C0`, which
is a different function that resets three unrelated globals.

There is a backup/restore pair, `FFX_SaveData_BackupScriptWorkArea 0x87E720` and
`FFX_SaveData_RestoreScriptWorkArea 0x87E6F0`, that swap the first 4 KB with
`g_ffxScriptWorkAreaBackup 0x132D570`. They would be installed as `dword_1327070` and
`dword_1327074` by `maybe_FFX_TkHarness_Init_DEAD 0x8798F0`, which is behind `sub_887D10()` and
therefore never runs. The pointers stay NULL and `FFX_Map_WarpTo`'s indirect call through
`dword_1327070` is a no-op. **Dead in retail**, noted so nobody chases it.

### The rest of the first half, now mapped

Putting section 4 and section 7 together with `GAME_STATE.md`, the "Progress, 15,436 bytes,
unidentified" bucket resolves completely:

```
+0x0000  0x1EC   header: location, fades, flags, LoveParam, the two small bit arrays,
                 checkpoint, playtime, location history, misc per-map words
+0x01EC  0x2000  g_ffxAtelScriptWork   <== THE WORLD STATE. 8,192 bytes of script variables.
+0x21EC  0x1320  g_ffxSphereGridNodes  2,448 words, low byte = panel id,
                                       high byte = per-character activation mask
+0x350C  0x0800  g_ffxSaveBlock350C    4 x 512, handed out by scene id for scenes
                                       582 / 584 / 590 / 591 only (sub_91CE50)
+0x3D0C  ...     party_data, and from here on GAME_STATE.md has it
```

Every boundary closes exactly: `0x1EC + 0x2000 = 0x21EC`, `0x21EC + 0x1320 = 0x350C`,
`0x350C + 0x800 = 0x3D0C`. **Confirmed.**

Two small notes. The sphere grid type is **bits 14 and 15 of the dword at `+0x3D0C`** (0 Standard,
1 and 2 the other two layouts), read by `FFX_SphereGrid_InitNodes` to pick which of three asset sets
to load. And the one config bit the in-game menu owns, `+0x5EC` bit 0, is class-0 offset 0x400,
which falls in a gap no shipped script declares. So the menu is borrowing an unused slot in the
script work area. **Confirmed.**

---

## What this means for co-op

**The verdict: yes, a client can be handed the host's entire save state at runtime and end up in a
consistent world. The block is pure data, it is contiguous, the game transplants it with a memcpy in
three places, and everything derived from it is rebuilt by one function call.**

The complete recipe, with every step justified above:

```
1. Host:   snapshot g_ffxSaveData, 26,816 bytes. Send it.
           Optionally exclude +0xBC (playtime, ticks every frame) and +0x64B4 (a stale CRC).
2. Client: memcpy it over g_ffxSaveData.
3. Client: set byte +0x2A = 1.
           The game's own loaded-from-file flag. FFX_SaveData_SetSceneAndSub consumes it once
           to take the previous-scene fields from the +0xC0 history instead of from stale
           live values. Costs one byte, removes a whole class of transition bug.
4. Client: if the two machines are on different languages, call
           FFX_SaveData_ReencodeCharNames 0x787470. Otherwise skip it.
5. Client: call FFX_SphereGrid_RecomputeDerived 0xA54860.
           This is the step the save load makes and FFX_Debug_ApplyViewerSave skips.
           It rebuilds g_ffxEquipStatBonus and then every character's derived stats,
           effective stats, ability masks and HP/MP clamps.
6. Client: raise g_ffxMapChangePending 0x133084C = 1 and nothing else.
           CORRECTED, see "Why step 6 is not resume-from-checkpoint" below. The live map
           id at +0x00 and the live entry point at +0x0C already hold the host's current
           location, because they arrived inside the block. All that is missing is the
           load, and the pending flag is the only thing either shipped request function
           does that this case needs.
7. Client: place EVERY party character at the host's actual positions afterwards.
           The save only identifies a doorway, not a position. BUILT, see "Step 7, the
           arrival" below. The engine's own teleport is FFX_Atel_SetActorPos 0x870B20
           and it does far more than move an actor, so use it rather than writing the
           position. snapPrev must be 1.
```

### Step 7, the arrival

This is built now, as `plugins/pilgrimage/world/Arrival.cpp` plus protocol message 13. Four
things had to be settled that were not obvious from the recipe.

**Use the engine's teleport, not a position write.** `FFX_Atel_SetActorPos 0x870B20` is
`int __cdecl (void *actor, float x, float y, float z, int snapPrev)` and it does all of this:

- writes the actor's position vector, at pos+0
- for a CHR backed actor, calls `FFX_Ch_SetPos` and then `FFX_Ch_MarkDirty`, so the drawable
  and the actor record agree on the same frame rather than one apart
- when the actor IS the bound player, shifts the context player cache at ctx+536 down into
  ctx+552 and writes the new position into ctx+536. That pair is what every line, box, path
  and volume trigger compares against, so getting it updated is not cosmetic
- refreshes the encounter ground attribute at savedata+16 from `FFX_Map_GroundAttrGetEnc`,
  and the area id at savedata+14 through `sub_870EF0(FFX_Map_GroundAttrGetDic(chr))`, so a
  random battle rolled right after a placement uses the destination's table rather than the
  origin's
- ORs 0x10 into the byte at ctx+2

Writing six floats by hand gets none of that.

**snapPrev MUST be 1 for a placement.** With it 0 the engine keeps the old previous position,
so the swept segment runs from wherever the character used to be to where it now is. Every
line and box trigger along that imaginary line then fires. A joiner arriving in a new map
would trip every trigger between the map origin and its feet. The scripts that pass 0 are
doing a deliberate nudge where they WANT the sweep counted.

**Send every character's position, not the leader's.** The followers trail the leader by a
few metres and those offsets are part of the world state. Sending one position and fanning
the others out from it puts the two machines in different places, which is the exact thing
the step is for. So the anchor message carries up to 8 slots, one per save block character
index, each with x, y, z and the facing read from the CHR's `m_rotY`.

**The APPLIED reply has to wait for the placement, not for the bytes.** The host unfreezes on
that reply. Replying as soon as the transplant is in lets the host walk away while the
joiner's map is still loading, and the anchor goes stale before it can be used. The transfer
then ends with two identical worlds and two parties standing in different places.

The joiner's own hold has to come off BEFORE the reply though, and that is not a
contradiction. The map load is consumed inside `FFX_MainStep`, which the simulation hold
skips, so holding through the load waits forever for something that cannot happen. The
lockstep clock is gated separately on "the world is installed", which stays false until the
placement finishes. So the joiner steps locally through its map load without exchanging input
against a world it has not reached yet.

### The traps in the placement path

`reversing/PLACEMENT.md` is the full audit and the numbered recipe. The short version of what
bites, in the order it bites you. `ffx::PlaceActor` does all of this.

**The destination has to be on the walkmesh, and you can ask before committing.** When
`FFX_Ch_WalkmeshFindTri` fails, `FFX_Ch_WalkmeshMove` does `posX += velX * 10` and
`posZ += velZ * 10` and runs nothing else, leaving `m_groundHeight`, the ground normal and
`m_groundAttrs` stale. With the velocity zeroed that is survivable, with velocity the
character gets flung. It leaves `m_walkmeshTri` at -1 rather than storing a bad index, so it
retries next frame. `ffx::PointIsOnWalkmesh` is the pre-check, and note the engine's own
function takes WALKMESH space, not world space, while the world-space wrapper at `0x83EAE0`
is dead code that divides the triangle index by the scale.

**`m_speed` is the carry-over, not the velocity.** `FFX_Ch_ResolveCollisionsAll` zeroes
`m_velX/Y/Z` every frame anyway, so they are not what walks a placed character away. `m_speed`
at CHR+0x154 is, plus `m_vertVel` at CHR+0x504 for the water mode.

**And a bare `m_speed = 0` does not even stick if the actor has a pending ATEL move command.**
While the kind word at `moveCmd+2` is non-zero, `FFX_Atel_ApplyMoveToChr` re-asserts `m_speed`
from the actor record every frame and walks the character back toward the script's target. So
the placement appears to work for one frame and then unwinds. Zero that word and the same
function forces `m_speed = 0` for you. `FFX_Atel_GetMoveCmd` is RVA 0x46C0A0 and
`ffx::CancelActorMoveCommand` wraps it.

**Copy the engine's Y bias.** `FFX_Atel_PushActorPosToChr` calls
`FFX_Ch_SetPos(chr, x, y - 0.1f, z)`, lifting the character clear of the stated Y so it does
not start inside the floor.

**The networked Y only has to pick the right deck.** `FFX_Ch_Allocate` sets `m_groundMode = 1`,
which makes the motion pass overwrite `posY` with `m_groundHeight` every frame, so the engine
computes the exact height itself. Y is still worth getting roughly right, because
`FindTri` uses it to choose between stacked floors, and since +Y is down, too small a Y means
too high up and that is the safe direction.

**Facing needs both halves.** `m_flags1` bit `0x400` slews `m_rotY` toward `m_moveDir` at 0.314
to 0.524 radians per sub-step, so writing the rotation alone turns the character to where you
asked and then smoothly turns it away. `FFX_Ch_SetRotAndMoveDir` RVA 0x42B1B0 writes both in
one call. And for a script driven actor that is still not enough: `FFX_Atel_StepActor`
overwrites the CHR's speed, move direction and rotation from the actor record every frame
unless `actor+0x34` bit `0x20` is set, which is the player-controlled bit.

**`FFX_Ch_SetPos` deliberately leaves `m_walkmeshTri` at -1**, which is the engine's "needs
relocating" sentinel rather than an error. The recovery normally happens in the next frame's
motion pass, but that pass skips hidden characters, so a hidden one is never relocated. Call
`ffx::BindToWalkmesh` after every placement. The full deadlock is written up in
`workshop/include/ffx/Walkmesh.h`.

**Two things that look like traps and are not**, both checked properly rather than assumed, so
nobody has to spend an afternoon on them again:

- `g_ffxSuppressNextSetPos` at `0x12FFAD8` would make the next `FFX_Ch_SetPos` do nothing, and
  **it cannot be armed**. Its only setter, `FFX_Ch_SuppressNextSetPos 0x82ACC0`, has zero
  callers, and it is not in `g_ffxMagicHostApiTable` either, confirmed by searching the whole
  image for its address. The byte is in uninitialised `.data`, so it is 0 at startup and stays
  0. Placement clears it anyway because the read is free and the failure it would cause is
  invisible.
- `m_flags1` bit `0x02000000`, which `FFX_Ch_SetPos` sets on every write, **has no reader
  anywhere**. Three writers, zero readers, verified by an immediate search across `.text`, a
  byte-pattern sweep of every test and bit-test encoding, and a decompile of all 76 functions
  that touch displacement 0x194. It is wiped by `FFX_Ch_ClearFlags1StepBits 0x832DB0` at the
  top of every frame along with bits 24 and 26. A dead latch, nothing to redo and nothing to
  clear.

### A correction to the actor position offsets

`ffx::AtelPos::X` and `ffx::AtelPos::PrevX` were **the wrong way round** in the kit, and the
fix is in. **pos+0 is the CURRENT position and pos+16 is the PREVIOUS one.** Three functions
agree and none of them is ambiguous:

- `FFX_Atel_PullActorPosFromChr 0x869E40` runs every frame and starts with
  `pos[4..6] = pos[0..2]`, which is "shift current into previous", then fills `pos[0..2]` from
  the live CHR with `FFX_Ch_GetPos`.
- `FFX_Atel_SetActorPos 0x870B20` writes the new position to `pos[0..2]` and only copies it
  down to `pos[4..6]` when it is snapping the previous as well.
- Both of them feed ctx+536, the CURRENT half of the player cache, from `pos[0..2]`.

Getting this backwards does not fault, it reads one frame stale, which is the kind of wrong
that survives a long time. `ffx::ActorPosition()` had been returning the previous position and
the nearby-actor search was reading it. Do not flip it back without reading those three
functions.

A useful corollary from the same reading: in the steady state **the CHR is the authority and
the actor record is the mirror**, because `PullActorPosFromChr` copies the live CHR position
into the actor record every frame. So a CHR-only position write propagates up to the actor by
itself one frame later. The reverse is not automatic, because
`FFX_Atel_PushActorPosToChr 0x866800` only pushes actor -> CHR when the actor's dirty bit at
`actor+0x38` bit 1 is set. `FFX_Atel_SetActorPos` writes both sides, which is a third reason
to prefer it.

### Why step 6 is not resume-from-checkpoint

This correction came out of implementing the recipe. The earlier version of step 6 said to call
`FFX_Map_RequestResumeFromCheckpoint 0x88DD60`, and that is wrong for a join in progress.

Look again at what that function actually does:

```c
entry = saveData.word[0xB8];   map = saveData.word[0xBA];   // the CHECKPOINT
saveData.word[0x00] = map;                                  // over the LIVE map
saveData.byte[0x0C] = entry;                                // over the LIVE entry point
```

It reads the checkpoint and writes it **over** the live fields. The checkpoint is the host's last
save sphere or story checkpoint, which can be a whole region behind where the host is standing. So
calling it after a transplant does not put the joiner where the host is, it puts the joiner at the
host's last save point. For loading a save from disk that is exactly right, which is why the
function exists and why the boot path calls it. For joining a game in progress it is not.

`FFX_Map_RequestChange 0x88EA60` with the live values read back out is also wrong, for a subtler
reason. Its first two stores are:

```c
saveData.byte[0x0D] = saveData.byte[0x0C];   // previous entry point
saveData.word[0x02] = saveData.word[0x00];   // previous map id
```

Passing it the values that are already there makes previous equal current. Those two fields are
readable by scripts through `FFX_SaveData_GetPrevEntryPoint 0x88D610`, so a client whose history
differs from the host's is a divergence in state the simulation can observe. Under lockstep that is
the kind of difference that does not show up as a glitch, it shows up much later as an unexplained
desync.

So the right action is the single store both functions finish with. The mod exposes it as
`ffx::RequestLoadLiveLocation()`, and the comment on that function says the same thing.

Six things that make this easier than it looked:

- **The block has no pointers.** BSS, no relocations, no instruction stores an address into it, and
  it round-trips through a file under ASLR. It goes on the wire as-is.
- **The map change is a deferred request, not a call.** `g_ffxMapChangePending` plus four words, and
  the load happens inside `FFX_MainStep` on the simulation step. That is the lockstep-friendly shape,
  and it means a map change can be a host-ordered command like any other input.
- **Per-map state cannot desync, because it does not persist.** The event package and the actor pool
  are freed and re-read from the archive on every map change, with no cache. Both peers loading the
  same map get the same initial values from the same file.
- **The world state is one contiguous 8,192-byte range** at `+0x1EC`. Chests, doors, switches, story
  progress, every one of them. For a desync detector that is one bucket, and an 8 KB diff when it
  trips will name the exact byte.
- **The derived state is one function call.** `FFX_SphereGrid_RecomputeDerived 0xA54860`.
- **`AutoTestManager`'s `JumpMap` is ungated in retail**, so map warping can be tested today with
  nothing more than stdin attached.

Four things that will bite:

- **The save only identifies a doorway.** `(map id, entry-point index)`. The actual spawn XYZ comes
  from the `.ebp` warp table, so a late joiner arrives at the last door the host used, not next to
  the host. Position replication is a separate job.
- **The scenario word at `+0xBEC` is read by 331 script descriptors.** A transplant that gets it
  wrong does not produce a visible glitch, it produces a different game. Treat `+0x1EC..+0x21EC` as
  all-or-nothing: never merge it field by field.
- **The two CRC lengths disagree** (0x68C0 in the in-game save, 0x64B8 in the PC `.SAV` layer). If
  the mod ever writes a save file it should use the PC layer's own writer rather than stamping a CRC
  itself.
- **`FFX_Btl_Init` zeroes `g_ffxEquipStatBonus` and nothing visible refills it.** After a battle
  initialisation the sphere grid bonus cache is blank until the Sphere Grid screen or a save load
  rebuilds it. Whatever the shipped game does about this, a co-op build that calls
  `FFX_SphereGrid_RecomputeDerived` on both machines at the same simulation step is safe, and one
  that calls it on only one machine is not.

A note for `GameState.h`'s bucket list. The current buckets treat `+0x00C0 .. +0x3D0C` as one
15,436-byte "Progress" bucket. That is correct but coarse. Three sub-buckets would be strictly more
useful and still contiguous:

| bucket | range | bytes | why separate |
|---|---|---|---|
| ScriptWork | +0x01EC .. +0x21EC | 8,192 | the world state. The one that matters. |
| SphereGrid | +0x21EC .. +0x350C | 4,896 | changes only when someone activates a node |
| Minigame | +0x350C .. +0x3D0C | 2,048 | four scenes only, almost always static |

and the header `+0x00C0 .. +0x01EC` (300 bytes) left on its own, because it contains the location
fields and will differ for a frame or two during any transition, exactly like the map id already
does.

---

## What I could not settle

1. **Whether `FFX_Btl_Init`'s `FFX_SaveData_ClearEquipStatBonus` is ever followed by a rebuild.** I
   found the clear and I found the only two rebuilders, and neither is on the battle path. Either
   retail ships with a blank grid bonus cache after the first battle initialisation, or a path I did
   not find refills it. A runtime experiment settles it in a minute: read `g_ffxEquipStatBonus
   0x1135E00` before a battle, after a battle, and after opening the Sphere Grid.
2. **The meaning of individual class-0 offsets.** I can say the world state is 8,192 bytes at
   `+0x1EC`, that 3,978 of those bytes are declared, and which ones are hottest. I cannot say which
   byte is "opened the chest in Besaid Temple". Recovering that means correlating a specific script's
   disassembly with its map, which `tools/ebp.py` can already do per file. The hottest offsets table
   above is the right place to start.
3. **`+0xA8`, `+0xAA`, `+0xB4`, `+0xB6`, `+0xD8`, `+0xDC`, `+0xE0`, `+0xE4`, `+0xE8`, `+0xEA`.** All
   have getters and setters, all are written around scene init, none has an identified purpose.
   `+0xB4`/`+0xB6` are written exactly once per map by ATEL Core syscall 366 in 323 of 397 packages,
   so they are per-map identity of some kind.
4. **Who sets `maybe_g_ffxSaveCrcSkip 0xCCB9A4`**, which turns off the PC save CRC check entirely.
5. **What the four 512-byte records at `+0x350C` hold.** `sub_91CE50` hands them out for scene ids
   582, 584, 590 and 591 and nothing else. The function lives in the 0x91xxxx range, which I did not
   attribute to a module.
6. **Whether the 640-bit `g_ffxEventFlagBits` at `+0x4C` is used by anything other than those three
   script sites.** The C-side has no callers I found beyond the three syscalls. It may be vestigial
   PS2 code, or it may be written by a magic DLL.
7. **What `dword_1327108` does.** It is the "leave the field" handshake between
   `FFX_Map_RequestChange`, `FFX_MainStep` and `sub_8738D0`, and I traced the writes but not the
   meaning.
8. **Whether anything inside `+0x1EC..+0x21EC` ticks every frame the way playtime does.** Playtime at
   `+0xBC` is the one I can prove from the code. A per-bucket hash over a few hundred idle frames,
   which `ffx::HashGameState` already supports, would answer it for every bucket at once. If
   something in the script work area does tick, the desync detector needs to know before it is
   trusted.

---

## Names and comments recorded in the IDB

All of these were applied with `idc.set_name` and carry a repeatable comment holding the evidence.
The database was saved. Nothing with an existing meaningful name was renamed, and
`g_ffxCurrentMapId 0x112CB48` carries a `NAME IS MISLEADING` comment rather than a rename.

**Save file**

| VA | name | was |
|---|---|---|
| 0x8B3E60 | `FFX_SaveFile_Serialize` | `sub_8B3E60` |
| 0x8B1450 | `FFX_Crc16Ccitt` | `sub_8B1450` |
| 0x8B54A0 | `FFX_SaveFile_InstallBlock` | `sub_8B54A0` |
| 0x787470 | `FFX_SaveData_ReencodeCharNames` | `sub_787470` |
| 0x8B4EC0 | `FFX_SaveFile_CommitLoad` | `sub_8B4EC0` |
| 0x81FF80 | `FFX_SaveUi_Step` | `sub_81FF80` |
| 0x8B50B0 | `FFX_SaveFile_StageAndDescribe` | `sub_8B50B0` |
| 0x8B4E40 | `FFX_SaveFile_GetSlotHeader` | `sub_8B4E40` |
| 0x646DF0 | `FFX_SaveFile_WriteSlot` | `sub_646DF0` |
| 0x646B30 | `FFX_SaveFile_ReadSlot` | `sub_646B30` |
| 0x646CA0 | `FFX_SaveFile_ReadSlotNoExt` | `sub_646CA0` |
| 0x647D70 | `FFX_SaveFile_VerifyCrc` | `sub_647D70` |
| 0x648F20 | `FFX_SaveFile_StampCrc` | `sub_648F20` |
| 0x647FB0 | `FFX_SaveFile_RegisterStagingBuffer` | `sub_647FB0` |
| 0x6492C0 | `FFX_SaveFile_ReadSlotToStaging` | `sub_6492C0` |
| 0x1597F70 | `g_ffxSaveFileStaging` | `byte_1597F70` |
| 0x1597FB0 | `g_ffxSaveFileStagingBlock` | `unk_1597FB0` |
| 0x159E464 | `g_ffxSaveFileStagingCrcField` | `dword_159E464` |
| 0x159EC70 | `g_ffxSaveSlotHeaders` | `unk_159EC70` |
| 0x186627C | `g_ffxSaveCounter` | `dword_186627C` |
| 0xCCB9A4 | `maybe_g_ffxSaveCrcSkip` | `dword_CCB9A4` |

**Map change and location**

| VA | name | was |
|---|---|---|
| 0x88EA60 | `FFX_Map_RequestChange` | `sub_88EA60` |
| 0x88EB50 | `FFX_Map_SetTransitionFrames` | `sub_88EB50` |
| 0x86FEC0 | `FFX_Map_WarpTo` | `sub_86FEC0` |
| 0x86FF40 | `FFX_Map_WarpToWithSavedFade` | `sub_86FF40` |
| 0x88DD60 | `FFX_Map_RequestResumeFromCheckpoint` | `sub_88DD60` |
| 0x88E9E0 | `maybe_FFX_Map_ResumeFromCheckpoint_ORPHAN` | not a function |
| 0x88EC30 | `maybe_FFX_Map_ResumeFromCheckpoint_ORPHAN2` | not a function |
| 0x88E9C0 | `maybe_FFX_Map_UnloadIfLive_ORPHAN` | not a function |
| 0x88E800 | `FFX_SaveData_SnapshotLocationHistory` | `sub_88E800` |
| 0x88EA50 | `FFX_SaveData_MarkLoadedFromFile` | `sub_88EA50` |
| 0x88EAF0 | `FFX_SaveData_SetSceneAndSub` | `sub_88EAF0` |
| 0x908100 | `FFX_AutoTest_JumpMap` | `sub_908100` |
| 0x854DF0 | `maybe_SG_DebugGui_MapJumpItemProc` | `sub_854DF0` |
| 0x854C60 | `maybe_SG_DebugGui_MiscItemProc` | `sub_854C60` |
| 0x1325928 | `g_ffxDebugMapJumpTarget` | `dword_1325928` |
| 0x133084C | `g_ffxMapChangePending` | `dword_133084C` |
| 0x1330850 | `g_ffxMapChangeDelayFrames` | `dword_1330850` |
| 0x1330854 | `g_ffxMapChangeDelayFlag` | `dword_1330854` |
| 0x1330858 | `g_ffxSceneLoaded` | `dword_1330858` |
| 0x1330848 | `maybe_g_ffxAtelStepGate` | `dword_1330848` |
| 0xC33480 | `g_ffxQuitPseudoMapId` | `dword_C33480` |
| 0x112CB50 | `g_ffxLocationHistory` | unnamed |

**Save-block header accessors**

| VA | name | field |
|---|---|---|
| 0x88D660 | `FFX_SaveData_GetMapId` | word +0x00 |
| 0x88D600 | `FFX_SaveData_GetPrevMapId` | word +0x02 |
| 0x88D630 | `FFX_SaveData_GetPrevSceneId` | word +0x06 |
| 0x88D680 | `FFX_SaveData_GetSceneSubId` | word +0x08 |
| 0x88D620 | `FFX_SaveData_GetPrevSceneSubId` | word +0x0A |
| 0x88D670 | `FFX_SaveData_GetEntryPoint` | byte +0x0C |
| 0x88D610 | `FFX_SaveData_GetPrevEntryPoint` | byte +0x0D |
| 0x870ED0 | `FFX_SaveData_SetMapNameId` | word +0x0E |
| 0x86C410 | `FFX_SaveData_GetMapNameId` | word +0x0E or +0xD4 |
| 0x870140 | `FFX_SaveData_SetMapNameOverride` | word +0xD4, byte +0xD6 |
| 0x8638B0 | `FFX_SaveData_ClearMapNameOverride` | byte +0xD6 |
| 0x8700C0 | `FFX_SaveData_SetFadeInFrames` | byte +0x11 |
| 0x8700D0 | `FFX_SaveData_SetFadeOutFrames` | byte +0x12 |
| 0x86C470 | `FFX_SaveData_GetScenarioWord` | word +0xBEC |

**World state, flags and derived state**

| VA | name | was |
|---|---|---|
| 0x112CC7C | `g_ffxAtelScriptWork` | unnamed, block +0x1EC, 8,192 bytes |
| 0x112CAA8 | `g_ffxSaveHeaderBits18` | unnamed, block +0x18, 64 bits |
| 0x112CADC | `g_ffxEventFlagBits` | unnamed, block +0x4C, 640 bits |
| 0x113085C | `g_ffxWorldFlagBits3DCC` | `unk_113085C`, block +0x3DCC, 1024 bits |
| 0x11308DC | `g_ffxWorldFlagBits3E4C` | `unk_11308DC`, block +0x3E4C, no readers |
| 0x112EC7C | `g_ffxSphereGridNodes` | `unk_112EC7C`, block +0x21EC, 4,896 bytes |
| 0x112FF9C | `g_ffxSaveBlock350C` | `unk_112FF9C`, block +0x350C, 2,048 bytes |
| 0x1132F44 | `g_ffxSaveBlockCrc` | `byte_1132F44`, block +0x64B4 |
| 0x86B680 | `FFX_SaveData_TestHeaderBit18` | `sub_86B680` |
| 0x8700E0 | `FFX_SaveData_SetHeaderBit18` | `sub_8700E0` |
| 0x86B9E0 | `FFX_SaveData_TestWorldFlag3DCC` | `sub_86B9E0` |
| 0x8701F0 | `FFX_SaveData_SetWorldFlag3DCC` | `sub_8701F0` |
| 0x785250 | `FFX_SaveData_GetWorldFlags3DCC` | `sub_785250` |
| 0x785260 | `FFX_SaveData_GetWorldFlags3E4C` | `sub_785260` |
| 0x784F40 | `FFX_SaveData_GetSphereGridNodes` | `sub_784F40` |
| 0x785400 | `FFX_SaveData_GetBlock350C` | `sub_785400` |
| 0xA53DE0 | `FFX_SphereGrid_InitNodes` | `sub_A53DE0` |
| 0xA54860 | `FFX_SphereGrid_RecomputeDerived` | `sub_A54860` |
| 0x86D4E0 | `FFX_SaveData_ClearScriptWorkArea` | `sub_86D4E0` |
| 0x87E720 | `FFX_SaveData_BackupScriptWorkArea` | `sub_87E720` |
| 0x87E6F0 | `FFX_SaveData_RestoreScriptWorkArea` | `sub_87E6F0` |
| 0x132D570 | `g_ffxScriptWorkAreaBackup` | `unk_132D570` |
| 0x7845B0 | `FFX_SaveData_InitPersistentScriptFields` | `sub_7845B0` |
| 0x88DF40 | `FFX_SaveData_ResetHeaderAndScriptWork` | `sub_88DF40` |
| 0x8798F0 | `maybe_FFX_TkHarness_Init_DEAD` | `sub_8798F0` |
| 0x2311240 | `g_ffxCharDerivedBytes` | `byte_2311240` |

Comments only, no rename: `FFX_Atel_InstallContextCallbacks 0x8711D0` (the class-0 base),
`FFX_Atel_ResolveVarAddress 0x86C2E0` (the per-class lifetime table and the measured traffic),
`FFX_Atel_StepOnce 0x88D3D0` (the deferred map-change consumer), `maybe_FFX_Scene_Init 0x88E070`
(the checkpoint snapshot), `FFX_SaveData_RecomputeCharDerived 0x7860F0` and
`FFX_SaveData_RecomputeAllCharDerived 0x786900` (what they write),
`FFX_Debug_ApplyViewerSave 0x8B55E0` (the worked transplant example and the `&unk_F60000` trap),
`FFX_AtelSys_Save_087_resi 0x8781F0` (the script-callable transplant),
`FFX_AtelSys_Save_071..078` (raw block access by byte offset),
`FFX_AtelSys_Core_366/367/368_resi` (the checkpoint writers),
`FFX_AtelSys_Core_305/568/569_resi` (the event flag bit array and its three uses),
`sub_887D10 0x887D10` (returns a constant 0, so roughly 40 guarded branches are dead),
`g_ffxEvPackageBase` and `g_ffxAtelActorPool` (per-map lifetime),
`g_ffxCurrentMapId 0x112CB48` (the `NAME IS MISLEADING` correction),
`g_ffxSaveData 0x112CA90` (the full header map).

The scripts that produced the shipped-data measurements are throwaway and were run against
`tools/ebp.py`. The three numbers worth being able to regenerate are: class-0 descriptor count and
offset span, the per-class read and write counts, and the syscall usage counts for Core 305 / 568 /
569 and Save 84 / 85 / 86 / 87. Each is a short loop over `Source().list_ebp()` reading
`EbpFile.var_table()` and `EbpFile.instructions()`.
