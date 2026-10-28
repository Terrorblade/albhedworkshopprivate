# CHRDATA: the character data table and the .chr image

PROGRESS 10.3 in Project Zanarkand (the CHRDATA table and .chr loading without in-place relocation,
PORTING_RULES A1). Written 2026-10-26 against the IDB of `FFX.exe` (32-bit, VA base `0x400000`). IDA
was read only for this pass. Every name below that is not already in the IDB is a proposal, collected
in section 11.

Marks: **[V]** verified in Hex-Rays and, where floats or branch senses matter, in the disassembly.
**[I]** inferred. **[C]** counted over the shipped files with `tools/chrfile.py`.

Related docs: `CHR_FORMAT.md` (the container, which this extends and corrects in section 10),
`CHR_RESIDENCY.md` (the cache and the ROM read that hand `FFX_Ch_BlkAllocate` its blob), `CHR_POOL.md`
(`FFX_Ch_Allocate`, the caller), `CHARACTER_MOTION.md` (the readers of the section 9 fields),
`ffx_types.h` (the `CHRDATA` struct).

The port lives in `src/engine/chr/ChrImage.{h,cpp}` and `src/engine/chr/ChrData.{h,cpp}` of
projectzanarkand, tested by `tests/engine/ChrDataTest.cpp`.

---

## 0. The short version

- `g_ffxChrDataTable 0x23C8D00` holds 40 `CHRDATA` records of 300 bytes, up to
  `g_ffxChrDataTableEnd 0x23CBBE0`. A free record has `m_id == -1`. [V]
- Lookup and allocation both take the **lowest** matching record. There is no free list. [V]
- A 41st distinct id faults: `FFX_Ch_BlkAllocate 0x825760` finds no free record and zeroes 300 bytes
  at null. Nothing checks for it. [V]
- The `.chr` blob is relocated **in place** by four walkers (section 4), then
  `FFX_Ch_InitChrDataFromBlob 0x825EE0` copies the header and section 9 into the record (section 3).
  The relocation also zeroes two kinds of CLUT colour key, so a loaded blob differs from the file in
  more than its pointers. [V]
- `FFX_Ch_RelocateChrData 0x8347A0` is not the initial relocation. It is a rebase after a blob moves,
  and nothing live calls it (section 5). [V]
- `FFX_Ch_LoadAnmFile 0x8283C0` is reached only from the SG debug window and reads a file that does
  not ship (section 8). [V]
- 822 of the 865 shipped `.chr` files go through a side-table relocation that matches a literal
  transcription of the four walkers at two load addresses, byte for byte, with the image unchanged.
  The other 43 are ones **the original itself corrupts or misreads**: 34 make
  `FFX_Ch_RelocateModelSection` write outside the blob, and 9 have section 9 read section 0's stamped
  address as a float. No shipped load path was found that reaches any of them (section 9.1). [V][C]
  (Corrected 2026-10-27. This line used to say all 865 match. That was written before the test ran.)

---

## 1. The table

| VA | name | what it does |
|---|---|---|
| `0x826EA0` | `FFX_Ch_Init` | clears the 0x2EE0 bytes (`sub_62F4E0`) and sets every `m_id` to -1 [V] |
| `0x825EA0` | `FFX_Ch_FindChrData(id)` | the lowest record with `m_id == id`, else null [V] |
| `0x825760` | `FFX_Ch_BlkAllocate(id, blob)` | the lowest record with `m_id == -1`, see below [V] |
| `0x8258D0` | `FFX_Ch_DataFree(rec)` | section 6 |
| `0x824F00` | `FFX_Ch_DataDispose(id, assert)` | section 6, magic host API slot 24 |
| `0x8259F0` | `FFX_Ch_DataDisposeForce(id)` | section 6 |
| `0x8259C0` | `FFX_Ch_DataDisposeAll` | section 6, called from `FFX_Ch_EventjumpDispose` |

Since a free record's `m_id` is -1, `FFX_Ch_FindChrData(-1)` returns the lowest free record, and
`FFX_Ch_LoadChrData(-1)` returns it as if it were loaded. No caller passes -1. [V]

`FFX_Ch_BlkAllocate 0x825760`, in order: [V]

1. Scan for `m_id == -1`. With none the pointer stays null and the `memset(rec, 0, 0x12C)` faults.
2. Zero the record, `m_flags |= 1`, `m_id = id`.
3. `FFX_Ch_InitChrDataFromBlob(rec, blob)`.
4. If there is effect data (`m_effectDataSrc`, section 10), allocate and copy it. The size is
   `blob - src + maybe_FFX_Ch_BlobTailSize(blob)`, where the tail size is the heap block size of the
   blob (`0x83C050` -> `sub_687050` -> `sub_6FBA90` -> `sub_943480`). So the copy runs from section 10
   to the end of the **heap block**, past the end of the file into the allocator's rounding.
   `FFX_MemAllocs` then `memcpy` into `m_effectData +0x124`, size at `+0x128`.

Both callers are inside `FFX_Ch_LoadChrData`.

---

## 2. The load path, `FFX_Ch_LoadChrData 0x825A40`

1. `FFX_Ch_FindChrData(id)`. A hit is returned at once, no read.
2. `id & 0xF000 == 0xF000`: the viewer branch. `FFX_Ch_ReadFileDev 0x83AE30` of
   `/ffx/proj2/chr/prot/... .chr`, then the `.mdl` through `FFX_Ch_BlkAllocateMdl 0x825860`, then
   `sub_837FA0` for the sysmot and relation `.tbl` files. `m_flags` gets 2 (and 4 with its own
   `.tex`). Nothing on this branch ships. [V]
3. Debug mode (the `g_ffxDebug...` gate) needs the debug `.tbl` counts and also fills `m_path`. [V]
4. The retail branch: `FFX_ChCache_GetData(1, id)`, else `FFX_Ch_RomRead` and `FFX_Ch_RomPump(0)`,
   else null. If the state is still 1 it pumps again. Then `FFX_ChCache_Lock`, `FFX_Ch_BlkAllocate`,
   `m_sourceTag = ""` (`byte_B8FBEB`), `m_path[0] = 0` outside debug mode,
   `m_name = FFX_Ch_IdToModelName(id)`, `m_name2 = FFX_Ch_IdToCategoryDir(id)`. [V]

The residency half (steps 4 up to the lock) is already ported as `ChrResidency::LoadChrDataBlob`, see
`CHR_RESIDENCY.md`. On a full table the blob is loaded and locked before the fault, and the port keeps
that order.

`FFX_Ch_RomRead 0x829EF0` allocates `((size + 143) & ~0x7F) - 16`, which is why the three short `skl`
files in section 9 survive. [V]

---

## 3. `FFX_Ch_InitChrDataFromBlob 0x825EE0`

It calls `FFX_Ch_RelocateChrBlob 0x8256F0` first, then copies. [V]

Header copies, from the relocated header:

| CHRDATA | from | |
|---|---|---|
| `+0x06C m_blobBase` | the blob | |
| `+0x004 m_partTable` | `blob+16`, section 0 | |
| `+0x014 m_texData` | `blob+24`, section 1 | |
| `+0x018 m_texDataAux` | `blob+28`, section 1's aux | no reader found |
| `+0x110 m_motRelCount` | `blob+52` as u16, section 4's aux | read as s16 |
| `+0x114 m_motRelTabs` | `blob+48`, section 4 | |
| `+0x120 m_effectDataSrc` | `blob+96`, section 10 | only when the count is above 10, else 0 |

Defaults, written in this order (`0x825F37..0x825FA8`):

| field | default | constant |
|---|---|---|
| `+0x1C`, `+0x20` | 1.0 | |
| `+0x42` | 2 | |
| `+0x43` | 1 | |
| `+0x24` | 1.0 | |
| `+0x40` | 0 | |
| `+0x60` | `0x7F808080` (bytes 80 80 80 7F) | |
| `+0x2C` | 8.0 | `flt_B92314` |
| `+0x64`, `+0x65` | 0 | |
| `+0x30`, `+0x34`, `+0x38` | 1.0 | |
| `+0x44`, `+0x48`, `+0x4C` | 0 | |
| `+0x50`, `+0x58` | 1.0 | |
| `+0x5C` | 2.0 | `flt_B92268` |
| `+0x54` | 0 | |
| `+0x3C` | 1000.0 | `flt_B1F2CC` |
| `+0x28` | not written, the memset's 0 | |

An npc (`id & 0xF000 == 0x2000`) then gets `+0x43 = 0` and `+0x40 = 1`. [V]

When the section count is above 9, section 9 is copied (`0x825FB5..0x826064`). Every float goes
through an x87 `fld`/`fstp` pair, so a signalling NaN in the file is stored quieted (bit 22 set). Only
the revision dword is a `mov`. In instruction order: [V]

| CHRDATA | section 9 | |
|---|---|---|
| `+0x54` | `+0` | revision, `mov` |
| `+0x30` | `+12` | |
| `+0x3C` | `1000.0 / +0x30` | `dbl_B0DBA0`, `fdivr`, stored as a float |
| `+0x28` | `+20` | |
| `+0x2C` | `+8` | |
| `+0x1C` | `+4` | |
| `+0x20` | `+16` | |
| `+0x24` | `+24` | |
| `+0x34` | `+28` | `m_defaultScale` |
| `+0x38` | `+32` | `m_modelScale` |
| `+0x40`, `+0x42`, `+0x43` | `+36`, `+38`, `+39` | bytes. `+37` is never read, `+0x41` never written |
| `+0x44`, `+0x48`, `+0x4C`, `+0x50` | `+40`, `+44`, `+48`, `+52` | |
| | | `cmp dword ptr [edx], 1624h` / `jl`: the rest only when the revision is >= 5668, **signed** |
| `+0x58`, `+0x5C` | `+56`, `+60` | |
| `+0x60..+0x63` | `+64..+67` | four byte moves |
| `+0x64`, `+0x65` | `+68`, `+69` | bytes |

The section 9 reads are not bounded by the declared size. Section 9 below.

---

## 4. The relocation, every fix-up

Each walker rewrites a stored offset into a pointer by adding a delta. "always" means the add happens
even when the stored value is 0, so a zero offset becomes a pointer to the base. "if nonzero" means a
zero stays null. All [V] from the decompile and checked against the disassembly where a branch sense
mattered.

### 4.1 `FFX_Ch_RelocateChrBlob 0x8256F0`, the header

- `delta = blob - *blob`. Nothing at all happens when it is 0 (an already relocated blob).
- `*blob = blob`.
- For `i < blob[1]`, the count **re-read every pass**: a nonzero descriptor offset `blob[4 + 2i]` gets
  `+= delta`. Then, on the loop counter:
  - `i == 0`: `FFX_Ch_RelocateModelSection(blob[4])`
  - `i == 1`: `FFX_Ch_RelocateTexSection(blob[6])`
  - `i == 4`: `FFX_Mot_RelocateIdTable(&blob[12], delta)`

### 4.2 `FFX_Ch_RelocateModelSection 0x827590`, section 0

`delta = section - *section` (the section address, since the stored value is 0). Nothing when 0.

| slot | rule |
|---|---|
| `+16` mesh table, `+28` joint table | always |
| `+0` | stamped with the section address |
| `+20` (`+0x14` table) | if nonzero |
| `+36` (`+0x24` table) | if nonzero |
| `+24` (`+0x18` table) | always |
| `+48` (`+0x30`) | if nonzero |

Per mesh, `u16 +6` of them, entry from `FFX_Ch_MeshEntryAt 0x828BE0` (stride 40 when the revision is
>= 4884 or < 2097, else 24):

| slot | rule |
|---|---|
| mesh `+8`, `+12`, `+16` | always |
| the `+16` table: `u16 mesh+6` entries, stride 12, each `+8` | always |
| mesh `+28` | always |
| the `+28` table: `u16 mesh+24` entries, stride 36: `+12`, `+8` | always |
| the same entries' `+20`, `+32` | if nonzero |

Then three tables of the header:

| table | count | stride | slot | rule |
|---|---|---|---|---|
| `+24` | `u16 +12` | 4 | each dword | always |
| `+20` | `u16 +8` | 12 | `+8` | always |
| `+36` | `u16 +32` | 12 | `+8` | if nonzero |

The original reads the nested tables through pointers it has just rewritten. A table pointer the walk
must follow but that is null would be dereferenced at null.

**The mesh loop has no revision gate on its fields.** It takes the stride from `FFX_Ch_MeshEntryAt`,
24 bytes for revisions 2097 to 4883, but still reads the mesh's `u16 +0x18` count and `+0x1C` table.
In a 24 byte entry those are the next entry's `+0` and `+4`, and for the last mesh whatever follows the
table. The disassembly at `0x8275E4..0x82768F` matches the decompile exactly, there is no branch on the
revision anywhere in the function. So on these files the original walks garbage and adds the load
address to dwords far outside the blob (6.8 MB past it for `k002`, 5 MB before it for `k015`). 34
shipped files do this, see section 9.1. [V][C]

### 4.3 `FFX_Ch_RelocateTexSection 0x83CBA0`, section 1

Only when the revision `u16 +16` is >= `0x1029` and `*section != section`. `delta` is the section
address.

| slot | rule |
|---|---|
| `+20`, `+24`, `+28` | always |
| `+0` | stamped |
| `+32` | if nonzero and the revision is >= `0x1126` |
| the `+24` CLUT table: `u16 +8` entries, stride 8, each `+4` | always |
| the `+28` table: `u16 +12` entries, stride 16, each `+12` | if nonzero |

**The colour keys.** Right after each CLUT pointer is rewritten, the four bytes it points at are
tested. `(00, FF, 00, x)` (green) or `(FF, 00, 00, x)` (red) are zeroed, all four bytes. The two tests
run one after the other on the same entry. 276 keys are zeroed over the shipped files. [V][C]

Below revision `0x1029` nothing in the section is touched, not even its stamp, although the section
descriptor in the header is still rewritten. 9 files are like that (section 9). [V][C]

### 4.4 `FFX_Mot_RelocateIdTable 0x8377A0`, section 4

`a1 = &blob[12]`, the section 4 descriptor. `a1[1]` (the aux word) entries at `a1[0]`, each dword
`+= delta`, the **file** delta. So the motreltab offsets are file relative, unlike sections 0 and 1.
[V]

### 4.5 How many

For c001 the header part is 1 image stamp and 8 descriptors (sections 0, 1, 2, 5, 6, 7, 8, 9), then
the model and texture walkers, 302 fix-ups in all. Over the 822 files that load there are 123804
fix-ups and 276 colour keys (2026-10-27, from the notes `C001LoadsUnrelocatedAndEveryAccessorMatchesChrfile`
and `ChrEveryShippedFileLoadsUnrelocated` print).

---

## 5. `FFX_Ch_RelocateChrData 0x8347A0` is a dead rebase

What it does: find the record with `m_blobBase == old`, `FFX_Ch_InitChrDataFromBlob(rec, new)`, then
for every live CHR with `m_data == rec`: `FFX_Ch_BindChrData`, `sub_83CDD0`, `sub_77C900` when
`m_texAnimProgram` is set, and `m_jointRemap += new - old`. [V]

It runs after a blob has been copied, so `FFX_Ch_RelocateChrBlob` sees a stale `*blob` and rebases by
the move. Its callers:

- `FFX_ChCache_CompactBlobs_dead 0x840C00`, no callers.
- A loop at `0x835F10` with no xrefs: copy every live record's blob, free the old one, call
  `FFX_Ch_RelocateChrData`. Proposed `FFX_Ch_CompactChrDataBlobs_dead`.

So blobs never move in retail, and the port has no rebase. [V]

---

## 6. Release

`FFX_Ch_DataFree 0x8258D0(rec)`: [V]

1. With `m_flags & 1`: `FFX_Ch_MotionSetDispose 0x836830(id, mode)` for modes 0, 1, 2, 3 (each a tail
   call of `FFX_Ch_ReleaseResource(0, mode, id)`), then `FFX_ChCache_Unlock(1, id)`, then free
   `m_effectData`.
2. Otherwise free `m_partTable`, `m_texData`, `+0x10C` and `+0x108`.
3. With `m_flags & 1` and `& 2`: free the blob, and `m_texData` too with `& 4`.
4. Free `+0x11C`, `+0x68`, `+0x100`, set `+0x100 = 0` and `m_id = -1`. Every other field is left as
   it was.

`FFX_Ch_DataDispose 0x824F00(id, assert)`: if `FFX_Ch_FindById(id)` finds a live CHR, assert (when
asked) and return -1 with nothing freed. Else `FFX_Ch_FindChrData`, and if there is a record
`FFX_Ch_DataFree` then `FFX_ChCache_FreeAllForId(m_id)`. Returns 0 either way. [V]

`FFX_Ch_DataDisposeForce 0x8259F0(id)`: the same without the in use test. `FFX_Ch_DataDisposeAll
0x8259C0`: `FFX_Ch_DataFree` on every live record, no cache free. [V]

---

## 7. Readers

Of the record:

| VA | name | reads |
|---|---|---|
| `0x82A9B0` | `FFX_Ch_GetEffectData` | `+0x124` |
| `0x82AB60` | `FFX_Ch_GetDataF30` | `+0x30` |
| `0x82AD70` | `FFX_Ch_GetDataCollisionRadiusScale` | `+0x1C` |
| `0x82AD90` | `FFX_Ch_GetDataF20` | `+0x20` |
| `0x826A60` | `sub_826A60` | returns `m_data + 184`, which is `m_path` |
| `0x82B0F0` | `FFX_Ch_SetF4F4` | `+0x2C` for the -1 argument |
| `0x82B200` | `FFX_Ch_SetF4F0` | `+0x28` |
| `0x82B8C0` | `FFX_Ch_SetF4EC` | `+0x24` |
| `0x82B880` | `FFX_Ch_SetCollisionRadiusScale` | `+0x1C` |
| `0x825006` | inside `FFX_Ch_Allocate` | `+0x34` read, `+0xF8 m_isFallback` written |
| `0x826070` | `FFX_Ch_BindChrData` | `m_id` word, `m_name`, the Tidus cache for c001 and c101, `m_skeleton = m_partTable`, part meshes |
| `0x826AF0` | `FFX_Ch_FindInstanceByEffectAddr` | `+0x124`, `+0x128` |

Instruction-level reads of the section 9 fields:

| where | reads |
|---|---|
| `FFX_Ch_UpdateBlink` | `+0x43` at `0x834653`, `+0x48` at `0x834697`, `+0x4C` at `0x8346A6`, `+0x42` at `0x8346BB` |
| `FFX_Ch_UpdateLookAt` | `+0x40` at `0x834D82` |
| `FFX_Ch_BuildSkinMatrices` | `+0x30`, `+0x38`, `+0x3C` (`0x8327DB..0x832A90`) |
| `FFX_Ch_BuildJointTransforms` | `+0x38` at `0x838F88` |
| `FFX_Ch_EvalAnmClip` | `+0x38` at `0x838A98` |
| `FFX_Ch_AttachToParentOffset` | `+0x38` at `0x832725` |
| `FFX_Ch_StepAll` | `+0x60..+0x63` as bytes at `0x82FA20`, `0x82FA4B`, `0x82FA8C`, `0x82FAAF`. Alpha above 0 gates `RGB * shadeA / 128`. `+0x58` at `0x82FC99`, `+0x5C` at `0x82FCA2` |

Of the image:

- `FFX_Ch_LookupBonePoint 0x833A70(chr, bone, *node, *offset)`: `*node = 0`, `*offset = {0,0,0,1}`.
  Returns 0 when `m_blobBase` is null or the count (`blob+36`, section 2's aux) is 0. The table is at
  `blob+32`. The first record with `(w0 & 0x3FFF) == bone` sets `*node = (s16)w1`, then on
  `w0 & 0xC000`: 0 returns 0 with the offset left zero, `0x8000` returns 2, `0xC000` returns 0, and
  `0x4000` computes `scale = 10000.0 / f30 / f30 * modelScale` (`fld f30`, `fdivr dbl_B30720`,
  `fdiv f30`, `fmul modelScale`, `fstp` a float at `0x833B1E`), sets the offset to the record's
  floats times the scale, and returns 1. [V]
- `sub_833BF0`: the node of the first match, kind ignored, no null check (ATEL `Ch_083`). [V]
- `FFX_Ch_GetBoneNodeIndex 0x835660` and `sub_8356B0` (host API 621) wrap the lookup. [V]
- `FFX_Ch_UpdateBonePositions 0x8350F0` reads `blob[8]`, `blob[9]`, `f30` and `modelScale`. [V]
- `FFX_Mot_SetByModeIndex 0x837D00` (host API 37): with `m_flags & 1`, the dword at
  `*(blob + 16 + 8 * g_ffxMotModeToSetSlot[mode])` indexed by the motion index, falling back to mode
  0. Otherwise `m_sysMotTbl +264`. `FFX_Mot_SetByModeIndexSearch 0x837E10` is the debug twin. [V]
- `FFX_Ch_SetMotionKey 0x837AC0`: with `m_flags & 1`, the motreltab search over the s16
  `m_motRelCount` and `m_motRelTabs`, matching an s16 model id. Otherwise `m_relationTbl +0x10C`. [V]
- Section 1:
  - `sub_83CD20` (host API 585): a CLUT entry `{word0, +4 pointer}`.
  - `sub_83CD50`: a `+28` entry's `u16 +8`, `u16 +10` and the `+12` pointer.
  - `sub_83CD90` (host API 586): `u16 +8`, the CLUT count.
  - `sub_83CDB0`: `u16 +12`.
  - `sub_833E60` (from `FFX_BtlExplode_BuildFragments`): over the `u16 +4` entries of the `+20` table
    (stride 16), byte 0 indexes the `+28` table. It gives the largest `+8` word, the sum of the `+10`
    words and the running sums before each.
  - `sub_83CDD0`: the texture animation block, gated on revision >= `0x1029`.
  - `sub_83C710`, `sub_83CA20`: CLUT copies.

---

## 8. `FFX_Ch_LoadAnmFile 0x8283C0`

The only caller is `sub_84C1E0`, and its only caller is `FFX_SG_DebugWin_OpenChrAndTakeControl`. It
reads `/ffx/proj2/chr/data/... .anm` through `FFX_Ch_ReadFileDev`, which does not ship. It checks
`m_isFallback`, sets the anm fields at `+0xFC`, `+0x100` and `+0x104`, and runs the motreltab search
with `id = FFX_Ch_LetterToCategory(name[0]) << 12 | atoi(name + 1)`. Not ported. [V]

---

## 9. The shipped files, and the digest

Counted with `tools/chrfile.py` over `FFX_Data.vbf`: [C]

- 865 `.chr`. Section counts: 856 with 11, 9 with 10. None below 5.
- Categories: mon 341, npc 224, obj 111, pc 39, skl 40, sum 30, wep 80.
- No file has a nonzero stored base in the header, section 0 or section 1.
- 3 have no section 0: `skl` `k201`, `k303`, `k403`. Tiny files with only sections 5 and 9.
- 43 have no section 1.
- Model revisions: 5409 x819, 4119 x25, 2097 x11, 4884 x5, 0 x2.
- Texture revisions: `0x1126` x805, `0x1029` x8, 0 x9. The nine at 0 (`_c001`, `__c003`, `k001`,
  `k002`, `k004`, `k005`, `k012`, `k014`, `k015`) are left wholly unrelocated.
- 276 colour keys zeroed. 286 files have section 10.
- Section 9 runs past the end of the file in `k201` (8 bytes), `k303` (8) and `k403` (24). 34 files
  declare a section 9 smaller than what is read.
- 6 files are not named after their directory.

c001 (263648 bytes): 11 sections, version 2. Section 0 at `0x8D0`, 1 at `0x2A180` (aux 2), 2 at `0x70`
(19 bone points), 3, 4 and 10 absent, 5 to 8 at `0x1A0`, `0x1B4`, `0x514`, `0x524` (5, 216, 4, 215
ids), 9 at `0x880` (72 bytes). Model revision 5409, 2 meshes, 136 joints, `+0x30` equal to the joint
table. Section 9: revision 5668, `+0x1C` 4, `+0x2C` 18, `+0x30` 1, `+0x20` 6, `+0x28` 18, `+0x24` 5,
`+0x34` 1, `+0x38` 10, bytes 0, 2, 1, `+0x4C` -0.242152, `+0x50` 1, `+0x58` 1, `+0x5C` 2, `+0x60`
`0x7F808080`, `+0x64` 0, `+0x65` 1, so `+0x3C` 1000. Bone point for bone 1 is kind `0x4000`, node 61.

**The digest.** `ChrEveryShippedFileLoadsUnrelocated` hashes the port's accessors and record fields
with FNV-1a 64 (offset `0xcbf29ce484222325`, prime `0x100000001b3`), and a throwaway script hashed the
same serialisation from `chrfile.py`. Over all 865 the script gives `effccfe8ffae8c7f`. The test only
digests the files that load, so with the 43 of section 9.1 left out the script gives
`5fd02ff40b0b0196`, and the port gives the same value independently (2026-10-27). The counts over those
822 are: 822 with 11 sections and none with 10 (all nine version 1 files are refused), 3 with no
section 0, 19 with no section 1, 1 texture section below `0x1029` (`k004`), 286 with section 10, 3
section 9 reads past the end, 276 colour keys. Files in byte order of their archive path, little
endian throughout, per file:

1. The archive path bytes, then a 0 byte.
2. u32 section count, u32 version, u32 header `+0x0C`.
3. Per section below the count: u32 offset (0 when absent), u32 aux.
4. u8 has section 0. If so: u16 revision, mesh count, `+0x08`, joint count, `+0x0C`, `+0x0E`, u32 the
   mesh table and the joint table as file offsets, u16 `+0x20`, u32 `+0x28`, u16 collision count, then
   each joint's ten s16.
5. u32 section 2's aux. If section 2 is present, per record: u16 bone id, u16 kind, u16 node, then the
   three offset floats as raw u32 bits.
6. Per mode 0 to 3: u8 present, then if present u32 count and each id as u32.
7. u8 has section 9 (count above 9 and present). If so, as the record holds them: u32 revision, the
   eight floats `+4..+32` in block order as bits (quieted if a signalling NaN), the three bytes as u32,
   the four floats `+40..+52` as bits, then if the revision is >= 5668 the floats `+56`, `+60` as bits,
   `+64` as a raw u32, the bytes `+68`, `+69` as u32. A field past the end of the file is 0. Last the
   bits of `float(1000 / f30)`.
8. u8 has section 10 (count above 10 and present). If so u32 offset, u32 size to the end of the file.

### 9.1 The 43 files the original corrupts or misreads

Found 2026-10-27 when the test first ran. A literal Python transcription of the four walkers (from the
decompile, checked against the disassembly of `FFX_Ch_RelocateModelSection`) was run on every file in a
buffer of the size `FFX_Ch_RomRead` allocates, logging every access outside it. [V][C]

**The model walk leaves the blob, 34 files.** The mesh loop of section 4.2 on the 24 byte stride, or a
garbage header. `_c001` and `__c003` (model revision 0, `+0x24`/`+0x28` read `0x807F7F7F`, collision
count 65535), `c051`, `_c102` (in both `c102/` and `c902/`), `s001_nasi`, and 28 `skl` files: `k001`
`k002` `k003` `k005` `k006` `k007` `k012` `k014` `k015` `k016` `k017` `k101` `k103` `k104` `k112`
`k113` `k114` `k202` `k204` `k212` `k214` `k302` `k401` `k402` `k501` `k503` `k504` `k505`. Of the 38
shipped models on the short stride or at revision 0, `k004` survives by luck (its single mesh's
borrowed `+0x18` count is 0), and `m999`, `f128` and `_w064` stay in bounds but fall in the next group.

**Section 9 reads a rewritten slot, 9 files.** The declared block is shorter than the 56 bytes
`FFX_Ch_InitChrDataFromBlob` always reads, and section 0 starts right behind it. At +48 (`m177`
`k301` `k502` `k998`, 36 byte blocks at revision 5159) the original copies section 0's stamped address
into `CHRDATA +0x4C`, which `FFX_Ch_UpdateBlink` reads. At +32 (`m999` `f128` `c999` `_w064` at 28
bytes, revision 4112, and `k999` at 32 bytes, revision 5152) it also copies it into `+0x38
m_modelScale`, and the rewritten mesh and `+0x14` table pointers into `+0x4C` and `+0x50`. The result
depends on the heap address. The 56 byte blocks of revision 5649 or 5665 that sit 64 bytes before
section 0 (19 files, among them `m089`, `n034` and `c902`..`c908`) are fine, because the original reads
`+56..+69` only from revision 5668.

**Nothing shipped reaches them**, as far as these checks go:

- `_c001`, `__c003`, `_c102`, `_w064` and `s001_nasi` are not names `FFX_Ch_IdToModelName 0x838100`
  (`"%c%03d"`) can build, and the ROM path is that name.
- Category 6 (`skl`) is in `skl.tbl` and has Phyre models, but no shipped `.ebp` loads a `0x6xxx` id
  through `core:1`, `core:308`, `core:515` or `ch:78` (the only syscalls consuming a `0x6xxx` constant
  are `core:157` and `sg:21`, neither a load). `FFX_Btl_ResolveUnitChrId 0x79A440` gives category 0 or
  1 only. The 87 calls of magic host API slots 0 and 4 in the 581 FFX magic DLLs push constant ids of
  categories 1, 3 and 4 only (32 of them had a constant to read).
- `c051` and `c999` are in `pc.tbl`, but no event script spawns them and `g_ffxCharIndexToChrId
  0xC423A0` does not hold them.
- `m177` is in no story or random battle file. The only roster naming monster 177 is `zzzz00_177`,
  the developers' one fight per monster list. `m999` is in no battle file and no event.
- `f128` is spawned only by `test25.ebp`.

The debug paths (`FFX_Ch_DebugSpawnByName`, the SG windows) and a plugin's own `FFX_Ch_Allocate` can
still load any of them, and **would corrupt the heap** on the 34. The Al Bhed Workshop model browser
builds its list from the ROM tables, so it offers all 28 `skl` models and `c051`.

The port refuses all 43 by design (`ChrImage::Parse`), and the test names them.

---

## 10. Corrections

- `ffx_types.h` says `CHRDATA +0x060 m_f60` has "default +inf (0x7F800000). No reader found". The
  default is `0x7F808080`, four bytes 80 80 80 7F, and `FFX_Ch_StepAll` reads it as an RGBA colour
  (section 7). Proposed `m_specColor`, with `+0x58` and `+0x5C` its intensity and power. [V]
- `CHR_FORMAT.md` gives the relocation as the header loop only. It also rewrites sections 0, 1 and 4,
  and section 1's walker **changes data**: green and red colour keys are zeroed. A relocated blob is not
  the file plus pointers. [V]
- `CHR_FORMAT.md` lists section 4 as "contents not decoded, no consumer traced". The consumer is the
  motreltab search of `FFX_Ch_SetMotionKey 0x837AC0` and `FFX_Ch_LoadAnmFile`, and its entries are
  file-relative, unlike sections 0 and 1. [V]
- `CHR_FORMAT.md` says the section 9 floats `+40..+52` are copied as raw dwords. They go through
  `fld`/`fstp`, which matters only for a signalling NaN, and none ships. [V]
- `CHR_FORMAT.md` says kind `0x8000` is "a plain node" and kind 0 "a plain joint position". By return
  value: 0 returns 0 with the node written, `0x8000` returns 2, `0xC000` (not listed there) returns 0.
  [V]

---

## 11. Proposed names

Applied to the live IDB on 2026-10-26, with two changes: `0x6FBA90` is `FFX_Mem_HeapBlockSize` (it sits
under `FFX_Mem_BlockSize 0x687050`, so they cannot share a name), and the CHRDATA member is `m_specColor`.
`0x835F10` was made a function first. ffx_types.h is updated to match.

| VA | proposed | why |
|---|---|---|
| `0x826A60` | `FFX_Ch_GetDataPath` | returns `m_data + 184`, `m_path` |
| `0x833BF0` | `FFX_Ch_GetBonePointNode` | the node of the first bone point match, kind ignored |
| `0x83CD20` | `FFX_Ch_GetTexClut` | a CLUT entry of section 1 |
| `0x83CD50` | `FFX_Ch_GetTexPage` | a `+28` entry of section 1 |
| `0x83CD90` | `FFX_Ch_GetTexClutCount` | `u16 +8` |
| `0x83CDB0` | `FFX_Ch_GetTexPageCount` | `u16 +12` |
| `0x833E60` | `FFX_Ch_GetTexStripLayout` | the strip sums for the battle explode |
| `0x835F10` | `FFX_Ch_CompactChrDataBlobs_dead` | the orphaned blob compaction loop |
| `0x83C050` | `FFX_Ch_BlobBlockSize` | was `maybe_FFX_Ch_BlobTailSize`, it is the heap block size |
| `0x687050`, `0x6FBA90` | `FFX_Mem_BlockSize` | the size query chain under it |
| `0x84C1E0` | `FFX_SG_DebugWin_LoadChrAnm` | the only caller of `FFX_Ch_LoadAnmFile` |
| `CHRDATA +0x60` | `m_specColor` | section 10 |

---

## 12. The port (projectzanarkand, PROGRESS 10.3)

`ChrImage` runs the four walkers as a reading pass and records a side table: every slot the original
rewrites with the file offset its pointer names, and every colour key it zeroes. The image is the
file, shared and never written. Accessors resolve pointers through the table and read everything else
through a view where a zeroed key reads 0. `RelocatedAt(base)` rebuilds the original's buffer at any
32-bit base, as a diagnostic.

`ChrDataTable` is the 40 records in the original's order, with offsets for pointers and the image,
the motion view and the effect copy held beside each record at its index. `ChrPool::Allocate` takes it
as its `ChrDataSource`.

Deviations, each written at its site:

- A full table returns `ChrDataTableFull` (and `ChrAllocateStatus::DataTableFull` from the pool) where
  the original faults. The blob is still loaded and locked first. The one-shot no-fallback flag is not
  consumed, as the original faults before reading it.
- `ChrImage::Parse` refuses: a nonzero stored base (header, section 0 or section 1), fewer than 5
  sections, a walker read past the end, a table the walk must follow left null with entries, a fix-up
  landing on a slot already rewritten, a CLUT key overlapping a rewritten slot, and a section 9 that is
  missing or whose read bytes (+0..+55, to +69 from revision 5668) include a rewritten slot. 43 shipped
  files are refused, each one the original corrupts or misreads (section 9.1). A refused image leaves
  the record free.
- Section 9 fields past the end of the file read 0, where the original reads the allocator's padding.
- The effect copy ends at the end of the file, not the end of the heap block.
- `LookupBonePoint` with a count but no section 2 finds nothing, where the original reads at null.
- The viewer branch, debug mode, `FFX_Ch_BlkAllocateMdl`, `FFX_Ch_LoadAnmFile` and
  `FFX_Ch_RelocateChrData` are not ported (sections 2, 5, 8).
- `DataFree` drops the image, motion view and effect copy, while the record keeps its stale fields
  as the original does.

---

## 13. Open

- What the section 0 `+0x14`, `+0x18`, `+0x24` and `+0x30` tables hold, and the section 1 tables
  beyond what the readers in section 7 use. The relocation is complete without knowing.
- `+0x41` and section 9 `+37`: never copied, so either padding or a field the PS2 build used.
- `m_texDataAux +0x18`: still no reader.
- How far past the file the original's effect copy reads for each file depends on the allocator's
  rounding, so a byte-exact effect buffer would need the heap block size rule (`FFX_Mem_BlockSize`).
  Nothing found reads the tail.
