# ClassCharacter: the Phyre half of a character model

Written 2026-10-12 for Project Zanarkand item 4.6 (`ClassCharacter` bundles and
`ClassCharacterTable`). Read only from the FFX.exe IDB (VA = RVA + 0x400000). The archive facts
were checked with Python over the shipped FFX_Data.vbf, and the port reproduces them in
`tests/engine/ClassCharacterTest.cpp`.

Companion docs: `CHR_RESIDENCY.md` (the cache record and FFX_Ch_RomRead, which is where this
starts), `CHR_FORMAT.md` (the .chr blob the other half loads), `PHYRE_FORMAT.md` (clusters),
`FFX_GAME_NOTES.md` (the CHR struct and +0x830).

---

## 1. The object

`ClassCharacter`, vtable `??_7ClassCharacter@@6B@` 0xB47E0C, 36 bytes, ctor `ClassCharacter__ctor`
0x69E030, dtor 0x69E830.

| off | what | who writes it |
|---|---|---|
| +0 | vtable | ctor |
| +4 | `PCluster*` of the `.dae.phyre` row | sub_65A760, or loadFromFile on a gfx cache hit |
| +8 / +12 / +16 | vector of the 32 byte instance records (begin, end, capacity) | sub_6A0D00, under the mutex `hHandle` 0xCCCA78 |
| +20 | per mesh instance, an int array of joint numbers (the remap) | sub_6A2390 |
| +24 | **mesh count**: how many PMeshInstance objects the model cluster holds | sub_6A2390 |
| +28 | ready byte, set on success and on every failure path that is not a crash | worker, see 3 |
| +32 | alternateRenderSet byte | FFX_LoadCharacterData |

+32 is set when the asset id is 4347, 4348, 4349 or 4352, or 800 < id < 900. sub_66F7D0 then
builds the mesh instance against the gfx context's set at +4660 in place of +4580.

`FFX_GAME_NOTES.md` calls +0x14 "an array of Phyre asset handles". It is the per mesh joint remap
table and +0x18 is the count of it.

## 2. The load chain

1. `FFX_Ch_RomRead` 0x829EF0 reads the .chr into the cache record and calls `FFX_Ch_LoadModel`
   0x82A1C0 with the ROM path, the record's +24 (cdf) and +28 (cmf) slots and the chr id. Its
   result goes into record +16, and +20 is zeroed. `FFX_ChCache_GetModelSlots` 0x840680 zeroes
   +16..+32 just before, so nothing reads +16 before that.
2. `FFX_Ch_LoadModel` unifies `/FFX_Data/GameData/PS3Data` for the root, then runs
   `FFX_Ch_BuildModelRelPath` 0x829E60 on the ROM path from its first `chr`.
   `chr/pc/c001/mdl/c001.chr` becomes `chr/pc/c001/c001.ah` (cut after the dot of `.chr`, append
   `ah`, drop the first `mdl/`). It then writes `ahwin32` over the last two characters of the
   joined path. The result for c001 is `<root>/FFX_Data/GameData/PS3Data/chr/pc/c001/c001.ahwin32`.
   A ROM path with no `chr` in it leaves the relative part empty and the overwrite lands on the
   root itself (`.../PS3Datahwin32`).
3. `FFX_LoadCharacterData` 0x63D140:
   - an empty path returns null
   - sub_635A70 refuses some ids (id 16487 always, ids 5, 6, 4251, 20481, 20482 when the low 16
     bits of story progress are 46, and any id below 0x4000 after masking with 0xFFFFF000 when
     g_ffxCurrentEventId is 382 and story is not 46)
   - `ClassCharacterTable__find` 0x6A2110 returns an existing entry
   - otherwise `new ClassCharacter`, set +32, `FFX_EnqueueCharacterLoad` 0x6642F0
   - `FFX_EnqueueCharacterLoad` always returns 0, so the "VIRTUOS Error: Can't find character
     data" branch after it is dead
   - it also calls sub_782F80, which raises `dword_C421DC` when it is not -1 (section 7)
4. If `FFX_Ch_LoadModel` was given both slots it takes the first record of
   `g_ffxPendingChrLoads` 0x12FC598 whose state +264 is 0 (50 records, see 6), fills it and
   queues `FFX_Ch_OnModelLoaded` 0x82A140 behind the character job with sub_6442F0.
5. On the AsyncLoadManager worker, `FFX_AsyncLoad__characterJob` 0x657850 runs
   `ClassCharacter__loadFromFile` 0x663F20:
   - it probes the gfx context's cache at `g_ffxGfxCtx+0x11D8` (a map keyed by the header path).
     A hit sets +4 to the cached model, +28 = 1, inserts into the table, runs sub_6A2390, and does
     **not** lower `dword_C421DC`
   - if the `PStreamReaderFile` does not open it sets +28 = 1 and returns 5
   - otherwise sub_6904C0 builds a 0x44 byte asset header object and sub_6F4900 parses the file
     into it (section 4). One `FFX_AsyncLoad__charSubAssetJob` 0x6663D0 per file row goes on a
     child list that the worker runs right after the parent job
6. Each sub-asset job loads its row's cluster and hands it to sub_65AE40, which stores it. For a
   row `sub_6F46B0` says is a model row the cluster goes to the job's model slot and the
   `m_bounds->m_meshInstance` dword (+28) of its PMeshInstances is cleared (0x65AF07), which takes
   the templates out of the render. See CHR_CLUSTER.md 3.1.
7. The last row's job runs sub_65A760:
   - sub_623580 links the imports across the bundle (sub_441F20, sub_441620). On failure it sets
     +28 = 1 with +4 left null
   - sub_64BDE0 inserts the bundle in the gfx cache at +0x11D8
   - sub_6F6A90 fixes up the samplers
   - texture animation through `textureAnimationChar.ovp` and sub_6B05D0
   - then +4 = model, sub_6A28B0 inserts into the table, sub_6A2390, +28 = 1, sub_782FA0 lowers
     `dword_C421DC`
8. `FFX_Ch_OnModelLoaded` runs next on the worker. It takes the record's path minus its last seven
   characters plus `cdf` and `cmf`, reads each with sub_63D260 (exists check, then a whole file
   read into an `FFX_Alloc` block, null when absent) into the cache record's +24 and +28, then
   clears the pending record's +264 and +268.

So every write a step thread reader polls (+4, +20, +24, +28, the table, the gfx cache, the
counter, the pending record) happens on the worker in the original.

## 3. Readiness and the step thread

`FFX_Chr_IsClassCharacterLoading` 0x82A100 scans the 50 pending records for state != 0 and
+268 == the ClassCharacter. The attach code waits on `+28 == 0 || IsClassCharacterLoading`.
Because +28 is written before the pending record is cleared, there is a window where the model is
ready and still "loading". The port keeps that window (it is observable through the attach list)
but moves both writes to completions, so they land at `DispatchCompletions` on the step thread in
request order and never between two reads of one step.

The gfx cache probe in loadFromFile happens at job start on the worker, which is after every
earlier job has finished. The port does it in the completion, which runs after every earlier
completion. Same order, same answer.

## 4. The asset header (.ahwin32)

A C header Phyre's build wrote. sub_6F4900 reads it in 0x3FF byte chunks with `fgets` style
reads, appending each chunk up to its first NUL. Only the block between
`#ifdef PHYRE_RENDERING_PLATFORM_D3D11` and the next `#endif` counts.

- `const Phyre::PUInt32 <name> = <n>;` lines are symbols. The name starts at offset 21 and ends at
  ` = `. Each declaration is stored when the **next** one is seen, so the last one in the block
  (always `g_fileNamesCount`) is never stored. Values get the current file count added, which is 0
  for a fresh header.
- `vfxtex_start` and `vfxtex_end`: end defaults to start. When both are present both keys are
  erased, file rows in `[start, end]` are skipped, and every symbol above end is renumbered to
  `start + v - end - 1`.
- File rows are the quoted lines after ` = {`, with a trailing CR handled. A duplicate row is
  skipped. A row ending in `.dae.phyre` is a model row (`sub_6F46B0`).

c001.ahwin32 lists 38 files: rows 0..22 the animated textures, 23 `textureAnimationChar.ags`,
24 `c001.dae.phyre`, 25 `c001.dds`, 26..37 the `PhyreChrLitShader.fx` variants. Symbol `c001`
is 24. Over all 872 character headers in the archive each lists exactly one `.dae.phyre` and every
row exists. Of the 1324 `.ahwin32` files (maps too) 307 carry a vfxtex range, and all of them are
CRLF.

## 5. Mesh count and the joint remap (sub_6A2390)

- +24 = the number of objects in the model cluster's instance lists whose class derives from
  `PMeshInstance` (descriptor `dword_CA7228`), in list order.
- +20[i] for mesh instance i is an int array with `m_mesh->m_defaultPose.count` (PMesh +24)
  entries. Entry j is `atoi(m_matrixNames[j] + k)`, where k is one past the first `_` of
  `m_matrixNames[1]` (k = 0 when the count is 1). Past the end of `m_matrixNames` it reads
  whatever follows.
- `FFX_ChrInstance_UploadBoneMatrices` 0x63BE60 indexes the CHR's joint palette with +20.

For c001 the cluster has one PMeshInstance list with one object, so **c001's mesh count is 1**.
Its PMesh has 79 entries in both arrays, the names are `SkeletonNode_<n>` in list 41, and the
remap starts `0, 1, 2, 74, 3, 75, 5, 76, 6, 77, 90, 102, 7, 70` and ends with 47. The
PHASE1_CLONE runtime log printed `meshes=1` for Tidus, which is the instance record count at
CHR+0x830 and agrees.

Census of the 872 character models by mesh count: 0 meshes 15, 1 mesh 699, 2 meshes 63, 3 meshes
43, the rest higher. The Zanarkand ruins set:

| model | rows | model row | meshes | .cdf |
|---|---|---|---|---|
| c001 | 38 | 24 | 1 | yes |
| c002 | 28 | 13 | 1 | yes |
| c003 | 19 | 3 | 1 | yes |
| c004 | 23 | 3 | 1 | yes |
| c005 | 17 | 7 | 1 | yes |
| c006 | 17 | 5 | 1 | yes |
| c007 | 21 | 7 | 1 | yes |
| f001 | 4 | 0 | 3 | no |
| f149 | 3 | 0 | 1 | no |

None of them ships a `.cmf`, so record +28 stays null.

## 6. Tables

**g_classCharacterTable** 0xCCCA70 is a `std::map<std::string, ClassCharacter*>` keyed by the
header path.

| VA | role |
|---|---|
| sub_69F440 | creates it, from FFX_GraphicInitialize |
| sub_69F580 | frees the map in FFX__graphicFinalize, without deleting any ClassCharacter |
| 0x6A2110 | find by path |
| sub_6A28B0 | insert, nothing when the key is there |
| sub_6A1E80 | contains, by value |
| sub_6A1D10 | erase the first entry holding the value |
| 0x69F6C0 | get the map |

**g_ffxPendingChrLoads** 0x12FC598: 50 records of 272 bytes. +0 path[256], +256 cdf slot,
+260 cmf slot, +264 state, +268 ClassCharacter. The scan in FFX_Ch_LoadModel runs from
0x12FC6A0 to 0x12FFBC0, which is 50 records, not 51. With all 50 open the load still goes ahead
but no record is written and the cdf and cmf slots are never filled.

**Pending attachments** on `g_ffxPendingChrAttachList` 0xCCB6D8 (count 0xCCB6D4), 16 byte records
`{instance slot, ClassCharacter, CHR, callback}`.

- `FFX_Chr_AttachModelInstance` 0x63D370, from FFX_Ch_Allocate. Ready and not loading creates at
  once through `FFX_Chr_CreateInstanceNow` 0x637880, otherwise it appends.
- `FFX_Chr_ProcessPendingAttachments` 0x639DB0, from FFX_MainStep at 0x820C0F, after the
  DispatchCompletions call at 0x820B10. It walks from the back. A record still waiting is swapped
  with the front of the kept range, a ready one is consumed (instance created and the callback
  run if +4 is set, silently dropped if not). Anything a callback appends past the kept count is
  cut off by the final count.
- `FFX_Chr_CancelPendingAttachments` 0x637B30 drops the records of one CHR by moving the last
  record into the hole and stepping past it.
- `ClassCharacter__createInstance` 0x63C540 builds the 32 byte record CHR+0x830 points at:
  +0 count and +4 data of per mesh instances (sub_66F7D0 each), +16 the ClassCharacter, +24 the
  CHR. It names the instance by walking the table for the entry holding this ClassCharacter and
  cutting its key between the last `/` and the last `.`, so c001's instance is `c001`.

## 7. dword_C421DC, the battle load counter

New finding. `dword_C421DC` counts battle character loads in flight. It is -1 outside battle
loading. sub_783020 phases 11 and 14 set it to 0 and phases 12, 15 and 20 wait while it is above
0. sub_782F80 (from FFX_EnqueueCharacterLoad) raises it, sub_782FA0 (from sub_65A760) lowers it
and logs "Something wrong with PS3 battle character loading!!!" if it goes negative.

It is lowered on the worker, so which battle step the wait ends on depends on wall clock. For
lockstep and for the port this has to become a completion. A cache hit in loadFromFile never
lowers it and neither does an open failure, so either one leaves the phase waiting for good if
it happens inside a battle load phase.

## 8. Release (sub_637980)

Reached through the thunk sub_63D9A0 from the cache sweeps on record +16 and +20.

- Not in the table: logs "Virtuos Error: try to unload a nonexistent character or it already has
  been unloaded" and returns. The ClassCharacter is **not** deleted.
- Otherwise it tears down every instance record, sub_6A1200 clears +8..+16, sub_670B20 erases the
  gfx cache entry whose model matches +4 and unloads its clusters, sub_6A1D10 erases the table
  entry, then the virtual delete.

## 9. Edge cases worth knowing

- **Two loads of one path while the first is in flight.** The table only learns of the first in
  sub_65A760, so a second FFX_LoadCharacterData in between makes a second ClassCharacter. Its job
  hits the gfx cache, shares the clusters and does not lower the counter. Its table insert is a
  no-op, so a later release of it is refused and it leaks.
- **A header with no file rows** queues no sub-asset job, so sub_65A760 never runs and +28 stays 0
  forever. Any attach on it waits forever. No shipped header does this.
- **A header with no `.dae.phyre` row**, or one whose cluster did not load, leaves the model slot
  null and sub_6A2390 reads through it. No shipped character header does this.
- **Releasing a ClassCharacter while it loads** deletes it under a pending record (+268) and any
  pending attachment that still names it, and the next scan reads freed memory. The port clears
  those references and warns.
- **sub_670B20 matches by model.** A bundle that never got a model (the import link failure path)
  is never erased from the gfx cache.
- **0x82A380** is an older `.ah` loader with no xrefs. Dead.

## 10. Reading it live

Kit addresses are RVAs (VA - 0x400000).

- Tidus's CHR: `*(g_ffxTidusChr)`, VA 0x12FBC60, RVA 0xEFBC60.
- Instance record: `inst = *(CHR + 0x830)`.
- ClassCharacter: `cc = *(inst + 16)`.
- Mesh count: `*(int*)(cc + 24)`, expected 1. The same number is `*(inst + 0) & 0x7FFFFFFF`.
- Alternatively record +16 of the cache entry with type 1 key 1 in `g_ffxChrCacheSlots`
  0x1302F98, or the value under `.../chr/pc/c001/c001.ahwin32` in g_classCharacterTable 0xCCCA70.

This has not been read on a running game for this item. The static derivation is the archive walk
above plus the PHASE1_CLONE `meshes=1` log line.

## 11. Proposed IDB names

| address | name | evidence |
|---|---|---|
| sub_69F440 | ClassCharacterTable__create | allocs the map at 0xCCCA70, called once from FFX_GraphicInitialize |
| sub_69F580 | ClassCharacterTable__destroy | frees it, from FFX__graphicFinalize |
| sub_6A28B0 | ClassCharacterTable__insert | find then insert, from sub_65A760 and loadFromFile |
| sub_6A1E80 | ClassCharacterTable__containsValue | walks the map comparing values, sub_637980's gate |
| sub_6A1D10 | ClassCharacterTable__eraseValue | erases the node holding the value, sub_637980 |
| sub_6A2390 | ClassCharacter__buildMeshTables | writes +20 and +24 from PMeshInstance and m_matrixNames |
| sub_637980 | ClassCharacter__unload | the Virtuos Error string and the virtual delete |
| sub_63D9A0 | ClassCharacter__unloadThunk | jumps to sub_637980, from the cache sweeps |
| sub_6A0D00 | ClassCharacter__addInstance | push to +8 under hHandle 0xCCCA78 |
| sub_6A1200 | ClassCharacter__clearInstances | clears +8..+16 in sub_637980 |
| sub_6904C0 | PAssetHeader__ctor | 0x44 byte object loadFromFile passes to sub_6F4900 |
| sub_6F4900 | PAssetHeader__parse | the D3D11 block, symbols and g_fileNames |
| sub_6F4170 | PAssetHeader__fileCount | size of the row vector |
| sub_6F41B0 | PAssetHeader__fileAt | row by index |
| sub_6F46B0 | PAssetHeader__isModelRow | `.dae.phyre` suffix test |
| sub_6F4350 | PAssetHeader__symbol | map lookup by name |
| sub_65AE40 | FFX_Chr_OnSubAssetLoaded | stores each row's cluster, the model row to the job |
| sub_65A760 | FFX_Chr_FinishCharacterLoad | import link, gfx cache, +4, table, +28, counter |
| sub_63D260 | FFX_File_ReadWholeIfExists | exists check then whole file into FFX_Alloc |
| sub_6442F0 | FFX_AsyncLoad_EnqueueCallTask | queues fn(arg) as a task, used for FFX_Ch_OnModelLoaded |
| sub_782F80 | FFX_Btl_ChrLoadBegin | ++dword_C421DC when not -1 |
| sub_782FA0 | FFX_Btl_ChrLoadEnd | --dword_C421DC and the PS3 battle loading message |
| dword_C421DC | g_ffxBattleChrLoadsInFlight | section 7 |
| sub_635A70 | FFX_Chr_IsLoadSuppressed | the 16487, story 46 and event 382 tests |
| sub_670B20 | FFX_Gfx_UnloadCharacterClusters | erases the +0x11D8 entry by model |
| 0x82A380 | FFX_Ch_LoadModelAh_Orphan | `.ah` loader, no xrefs |

IDB comment corrections: the comment on FFX_Ch_LoadModel says 51 pending slots, it is 50. The
comment on ClassCharacter__loadFromFile names the sub-asset callback, which is
FFX_AsyncLoad__charSubAssetJob 0x6663D0.
