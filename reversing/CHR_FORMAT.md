# The `.chr` character container

The PS2-era character file, shipped intact in `FFX_Data.vbf`. Reader: `../tools/chrfile.py`.
Recovered from `FFX_Ch_RelocateChrBlob 0x8256F0`, which walks the whole header, and validated by
parsing **all 865 `.chr` files with zero errors**.

## Where they live

```
ffx_ps2/ffx/master/jppc/chr/<cat>/<name>/mdl/<name>.chr
ffx_ps2/ffx/master/jppc/chr/<cat>/<name>/mot/resident{0,1,2,3}.mgrp
ffx_data/gamedata/ps3data/chr/<name>/mdl/d3d11/<name>.dae.phyre     the HD geometry
ffx_data/gamedata/ps3data/chr/<name>/tex/d3d11/<name>.dds.phyre     the HD textures
```

**Only `jppc` has a `chr/` tree.** Eleven `master/*pc` roots exist but the character data is not
localised, so there is no per-language fan-out here. The localised roots carry `battle/kernel`,
`menu` and `event` text.

`<name>` is `%c%03d` of (category letter, decimal number), from `FFX_Ch_IdToModelName 0x838100`.
Categories, from `FFX_Ch_LetterToCategory 0x829D80`:

| letter | cat | meaning | `.chr` files |
|---|---|---|---|
| `c` | 0 | playable characters | 35 |
| `m` | 1 | monsters | 341 |
| `n` | 2 | NPCs | 224 |
| `s` | 3 | summons | 29 |
| `w` | 4 | weapons | 79 |
| `f` | 5 | objects | 111 |
| `k` | 6 | props and scenery | 40 |

**chr id = `(category << 12) | decimalNumber`**, and the number is decimal, not hex. So `c001` is
id 1 and `c041` is id 41 (`0x29`).

`<cat>.tbl` is the ROM index: `u16 count` followed by `u16 ids[count]`, byte exact for all 7
(`2 + 2*count == filesize`). That is precisely what `FFX_Ch_FindRomEntry 0x82A640` reads. It is the
authoritative "what the game can load" list, and it does not always agree with the directory
listing: `pc.tbl` names 46 (no `c046` exists) and omits 307 (`c307.chr` exists but is unreachable).

### The 35 playable models

`c001` to `c008` are the eight field models, `c101` to `c108` the high-detail event models for the
same eight. Then alternates: `c041 c043 c044 c045 c051` are costume variants of the `c00N` series,
`c121 c122` of `c043`/`c044`, and `c307 c901`-`c908 c921 c922 c999` of the `c1NN` series.
`c307.chr` is a byte-identical copy of `c101.chr`.

The grouping is not guesswork, it falls out of cross-model motion references: `c041` borrows all
four of `c007`'s motion banks and `c045` borrows `c003`'s.

14 more directories (`c801`-`c807`, `c811`-`c817`) have Phyre model and texture assets but **no
`.chr`, no `.mgrp`, and no `pc.tbl` entry**, so they cannot be spawned as CHRs at all.

**Tidus is `c001`, id 1.** The engine says so itself: `FFX_Ch_BindChrData 0x826070` does
`strcmp(chrdata->m_name, "c001")` and `strcmp(..., "c101")` and caches the CHR in
`g_ffxTidusChr 0x12FBC60` on either match.

### Two traps

`chr/pc/a` and `a.bak` are leftover developer `ls -l` output ("206208 c001/mdl/c001.chr") with stale
sizes from an older build. Ignore them.

The 865 `.wd` files are **sound wave dictionaries** under `ffx_ps2/ffx/proj/sound/`. None are under
`chr/`. The 865-to-865 count match with `.chr` is a coincidence, and I initially misread it as a
pairing. Same for `.ply`, `.rsd`, `.ma2`, `.omd`, `.an2` and `.anm`, which all live under
`ffx_ps2/ffx/yonishi_data/` effect directories.

## Layout

Relocation, from `FFX_Ch_RelocateChrBlob`:

```c
delta = self - dword[0];  dword[0] = self;           // idempotent
for (i = 0; i < dword[1]; i++)
    if (dword[4 + 2*i]) dword[4 + 2*i] += delta;     // each section offset -> pointer
```

### Header

| off | type | field | status |
|---|---|---|---|
| `0x00` | u32 | relocation anchor, **0 on disk** in all 865, so stored offsets are file-relative | confirmed |
| `0x04` | u32 | section count: 11 in 856 files, 10 in 9 | confirmed, it is the relocator's loop bound |
| `0x08` | u32 | version: 2 when count is 11, 1 when count is 10 | **inferred.** No reader found, the perfect correlation is the only evidence |
| `0x0C` | u32 | 0 in all 865, no reader | unknown |
| `0x10 + 8i` | u32, u32 | `offset, aux` per section | confirmed |

Header size is `0x10 + 8*sectionCount`, then **`0x77` fill** to the first section. 802 files have
pad and all of it is `0x77`, 63 fit exactly, none disagree.

### Sections

The index is fixed, because the relocator switches on the loop counter.

| i | contents | what `aux` means | status |
|---|---|---|---|
| 0 | PS2 model and skeleton blob -> `CHRDATA.m_partTable` | always 0, unused | confirmed |
| 1 | PS2 texture and CLUT blob -> `m_texData`, aux -> `CHRDATA+0x18` | small count 1..6, **meaning unknown** | confirmed |
| 2 | bone point table, 16-byte records | record count | confirmed |
| 3 | **never used**, offset 0 in all 865, no reader | - | confirmed |
| 4 | pointer array -> `CHRDATA+0x114`, aux -> `+0x110` | pointer count, stride 4 | confirmed, contents not decoded |
| 5 | motion id table, mode 0 **Field** | dword count | confirmed |
| 6 | motion id table, mode 1 **FieldBattle** | dword count | confirmed |
| 7 | motion id table, mode 2 **Swim** | dword count | confirmed |
| 8 | motion id table, mode 3 **SwimBattle** | dword count | confirmed |
| 9 | parameter block, always present | byte size (72 current, 16/28/32/36/56 on older revisions) | confirmed |
| 10 | monster effect data -> `m_effectDataSrc` | byte size, exact in 286 of 286 | confirmed |

The mode mapping is not a guess. `g_ffxMotModeToSetSlot 0xC49784` is literally `BYTE[4] {5,6,7,8}`,
and `FFX_Mot_SetByModeIndex 0x837D00` computes `blobBase + 8*slot + 16`, which is the section's own
offset field, then indexes it as `dword[motionIndex]`. The four `mot/resident{0..3}.mgrp` banks and
`g_ffxDebugCharModeNames 0xC34318` (`"Field" "FieldBattle" "Swim" "SwimBattle"`) are the independent
cross-check, and the counts line up exactly: `c001.chr` sections 5/6/7/8 hold 5 / 216 / 4 / 215 ids
against the motion counts of `resident0..3.mgrp`.

**So the motion ids are in the `.chr`, not the `.mgrp`.** Tidus idle, walk and run are
`0x00011040`, `0x00011094`, `0x00011093`, which `../tools/mgrp.py` confirms from the other side as
motions 1040, 1094 and 1093 in `resident0.mgrp`. See `MGRP_FORMAT.md`.

### Section 0, the model and skeleton

Offsets are relative to the **section** base, because that is what the relocator uses as its delta.

| off | field | `c001` | status |
|---|---|---|---|
| `0x04` u16 | revision | 5409 | confirmed, picks the mesh stride |
| `0x06` u16 | mesh count, 56-byte `CHRPART` each | 2 | confirmed |
| `0x08` u16 | count of the `+0x14` table | 84 | confirmed |
| `0x0A` u16 | **joint count**, 352 + 396 bytes each | 136 | confirmed |
| `0x0C` u16 | count of the `+0x18` dword table | 1 | confirmed |
| `0x0E` u16 | - | 2 | unknown |
| `0x10` / `0x14` / `0x18` | mesh table / stride-12 table / dword table | | confirmed relocated |
| `0x1C` | **joint bind-pose table**, stride 20 | | confirmed |
| `0x20` u16 / `0x24` | count / stride-12 table | 84 | confirmed |
| `0x2C` u16 | collision volume count, 96 bytes each | 0 | confirmed |

Mesh stride is `rev >= 4884 || rev < 2097 ? 40 : 24` (`FFX_Ch_MeshEntryAt 0x828BE0`).

The bind-pose record is `s16[10]`: parent index, rotation in degrees x100, translation / 1000, scale
/ 4096. All of that comes from the conversion arithmetic in
`FFX_Ch_BuildSkeletonInstance 0x8277F0`, and the decoded table is real anatomy with clean parent
chains and 90-degree joint rotations.

### Section 2, bone points

From `FFX_Ch_LookupBonePoint 0x833A70`, all confirmed. `u16[0]` is `kind << 14 | boneId` where kind
0 is a plain joint position, `0x4000` a joint plus a local offset and `0x8000` a plain node.
`u16[1]` is the joint node index, then three floats of local offset scaled by
`10000 / m_f30^2 * m_modelScale`.

### Section 9, the parameter block

Every field is confirmed, because `FFX_Ch_InitChrDataFromBlob 0x825EE0` copies each one to a named
`CHRDATA` offset: `+0` revision (gate 5668), `+4`/`+16`/`+24` -> `m_f1C`/`m_f20`/`m_f24`, `+8` ->
`m_f2C`, `+12` -> `m_f30` (and `1000/m_f30` -> `CHRDATA+0x3C`), `+20` -> `m_f28`, **`+28` ->
`m_defaultScale`**, **`+32` -> `m_modelScale`**, `+36`/`+38`/`+39` bytes -> `+0x40`/`+0x42`/`+0x43`,
`+40..+52` floats -> `+0x44`/`+0x48`/`+0x4C`/`+0x50`, and when revision >= 5668 also
`+56`/`+60`/`+64` -> `+0x58`/`+0x5C`/`+0x60` and `+68`/`+69` -> `+0x64`/`+0x65`.

## Is the PS2 geometry dead weight?

Probably, but this is inferred from absence rather than proven. Section 0 definitely supplies the
skeleton, the per-mesh `CHRPART` metadata and the bone count. Actual drawing goes through the Phyre
instance, with `FFX_Ch_BuildSkinMatrices 0x832760` ->
`FFX_ChrInstance_UploadBoneMatrices 0x63BE60` as the only bridge. Nothing was found that
dereferences the mesh table entries past offset 24. Checking that directly would settle it.

## Known unknowns

- **Section 1's `aux`** (1..6). Following `FFX_Ch_RelocateTexSection 0x83CBA0`'s use of
  `CHRDATA+0x18` would settle it.
- **Section 4's contents.** The pointers are relocated and reach `CHRDATA+0x114`, but no consumer
  was traced. An xref hunt on reads of `CHRDATA+0x110`/`+0x114` would settle it.
- **Section 0 fields `+0x0E` and `+0x28`, and the purpose of the `+0x14`, `+0x18`, `+0x24` and
  `+0x30` tables.** Decompiling the rest of `FFX_Ch_BuildSkeletonInstance` past the joint loop would
  settle it.
- **Header `+0x0C` and section 3.** Both zero in all 865 with no reader, so probably dead PS2-era
  slots. Nothing in this build can settle it, it would need an earlier PS2 disc to compare.
- **Section 10's internal layout** (monster effect data, up to 96 KB). Not needed for playable
  characters, none of the 35 have it.
- **Which of `c002`-`c008` is which party member.** `g_ffxDebugCharNames 0xC3432C` gives the eight
  names in party order but the model mapping is data-driven, read from ATEL actor+168 on the field
  path. One runtime read of `m_partyIndex` against `CHR+0` on a map with a known party settles it.

Three `skl` stubs (`k201`, `k303`, `k403`) declare a section 9 shorter than the game reads, and only
get away with it because `FFX_Ch_RomRead` over-allocates the blob buffer by rounding up to
`((size + 143) & ~0x7F) - 16`. `chrfile.py verify` flags them.
