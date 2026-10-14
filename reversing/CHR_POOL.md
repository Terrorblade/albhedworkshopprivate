# The CHR pool and the per step CHR passes

Written 2026-10-13 for Project Zanarkand PROGRESS 10.2 (the pool) and 10.6 (the pass order). Addresses are
VAs at the preferred base `0x400000`, RVAs where a hook needs them are VA - `0x400000`. Every claim is from
IDA, Hex-Rays checked against the disassembly where marked [V], inferred where marked [I]. The IDB was read
only for this pass, nothing renamed or saved.

The recreation's port is `src/engine/chr/ChrPool` and `src/engine/chr/ChrStepPasses` in
`E:\Git\projectzanarkand`, with `src/engine/app/ChrPassCallees` routing `FFX_MainStep`'s callees to it.

---

## 0. The short version

- The pool is `g_ffxChrArray 0x23C44E4` and `g_ffxChrCount 0x23C44E0`, `2 x count` CHRs of `0x880`, the
  upper half the scratch slots. Being in the lower half with `m_inUse` set is the whole registration.
- The slot search is the lowest index whose `m_inUse` is 0, every time. No free list, no round robin. [V]
- The observable size is `max(ctx +0x0C - ctx +0x18 - ctx +0x16 - ctx +0x14 + spare + 8, 36)`, then
  `max(that, 10)`, decided per map by `maybe_FFX_Map_SetupChars 0x875510`. [V]
- A spawn with every slot taken still loads its CHRDATA first, then `memset`s `0x880` bytes at null and
  faults. A spawn whose id has no data takes the `c001` fallback, unless `FFX_Ch_SetNoFallbackOnce` armed
  the one-shot, in which case it returns null cleanly. A spawn whose fallback is missing too faults on
  `m_isFallback`. [V]
- `FFX_Ch_Dispose` clears the player binding and the Tidus cache together when either names the disposed
  CHR. Disposing whatever the pad drives forgets Tidus, and disposing Tidus unbinds the pad. [V]
- `FFX_Ch_OrphanChildren` scans `count` entries starting at the disposed CHR, not at the array base. A
  child stored below its parent keeps a dangling parent, which a later spawn into the parent's slot then
  inherits. [V]
- Per step the order is ClearFlags1StepBits, UpdateMotionAll per sub-step, DispatchInBatches, StepAll per
  drawn screen with UpdateCameLenAndZClip inside it per CHR, then ShadowPassAll. [V]
- **`FFX_Ch_ShadowPassAll` is dead in retail.** Its one caller passes 0 and it returns on 0. [V]
- PROGRESS 10.6 lists `UpdateCameLenAndZClip` after `ShadowPassAll`. The binary runs it inside
  `FFX_Ch_StepAll`, before the shadow pass. The recreation's test asserts the binary's order.

---

## 1. The pool state

| Global | VA | RVA | Writers | Meaning |
|---|---|---|---|---|
| `g_ffxChrArray` | 0x23C44E4 | 0x1FC44E4 | `FFX_Ch_Init` (null), `FFX_Ch_AllocChrArray`, `FFX_Ch_DisposeAll` (null) | base of `2 x count` CHRs |
| `g_ffxChrCount` | 0x23C44E0 | 0x1FC44E0 | the same three | the lower half's count |
| `g_ffxTidusChr` | 0x12FBC60 | 0xEFBC60 | `FFX_Ch_BindChrData` (set), `FFX_Ch_Dispose` (clear), `FFX_ChCache_FreeSlot 0x840EC0` (clear, see 8) | the last CHR bound to `c001` or `c101` |
| `g_ffxControlledChr` | 0x1300788 | 0xF00788 | `FFX_Ch_SetPlayerChr 0x82DAD0` | the pad's CHR, with the four `g_ffxPlayerDirRamp*` dwords at `0x130078C..0x1300798` zeroed by every set |
| `g_ffxChrNoFallbackOnce` | 0x12FBD98 | 0xEFBD98 | `FFX_Ch_SetNoFallbackOnce 0x8285D0` (1), `FFX_Ch_Allocate` and `FFX_Ch_AllocChrArray` (0) | refuse the `c001` fallback once |
| `g_ffxChrIterCursor` | 0x12FBD9C | 0xEFBD9C | `FFX_Ch_NextLiveChr`, the reset at `0x826DB0` | the resumable live iterator |
| `g_ffxChrRoundRobinCursor` | 0x13009E8 | 0xF009E8 | `FFX_Ch_AllocChrArray` (0), `FFX_Ch_TimeSlicedFlags1Bit10` | a CHR pointer, see 5.4 |
| `g_ffxSuppressNextSetPos` | 0x12FFAD8 | 0xEFFAD8 | `FFX_Ch_SuppressNextSetPos 0x82ACC0` (1), `FFX_Ch_SetPos` (0) | eats the next `FFX_Ch_SetPos`, including the one inside every spawn |
| `g_ffxChrAllocWriteOnlyFlag` | 0x13019B4 | | `FFX_Ch_SetAllocWriteOnlyFlag`, called with 0 by `FFX_Ch_Allocate` and from `sub_83C200` | nothing reads it [V xrefs], dropped |

### 1.1 `FFX_Ch_Init 0x826EA0` [V]

Called once from `FFX_MainInit`. Clears the 40 x 300 byte CHRDATA table at `g_ffxChrDataTable 0x23C8D00`
to `m_id = -1`, `FFX_Ch_LoadRomIndexTables`, `g_ffxChrArray = null`, `g_ffxChrCount = 0`,
`g_ffxChrProtoCount = 0`, `maybe_FFX_Ch_StepInit`, the debug `.tbl` reads (debug mode only, the files do
not ship), `FFX_Ch_DisposeAll` on the empty pool, `sub_8360B0` (two light matrices), and
`FFX_PlayerCam_ResetDistance`. It does not touch `g_ffxTidusChr`, `g_ffxControlledChr`, the one-shot or the
iterator.

### 1.2 The size rule [V]

`maybe_FFX_Map_SetupChars 0x875510` computes, from ATEL context 0's u16 words:

```
n = ctx[+0x0C] - ctx[+0x18] - ctx[+0x16] - ctx[+0x14]      as ints, may go negative
n = FFX_Atel_GetSpareChrCount(ebp) + n + 8                 the u32 at RES+4
if (n < 36) n = 36
FFX_Ch_EventjumpInit(n) -> if (n < 10) n = 10 -> ... -> FFX_Ch_AllocChrArray(n)
```

`FFX_Atel_ResetContext` seeds `+0x0C` from the `.ebp` header word 27 and `+0x14`, `+0x16`, `+0x18` from
words 10, 11 and 14 [V]. What those words count is not settled [I]. The floor of 10 can never apply from
this path. `FFX_Ch_AllocChrArray` has no other caller [V].

### 1.3 `FFX_Ch_AllocChrArray 0x825670` [V]

`g_ffxChrCount = n`, `g_ffxChrNoFallbackOnce = 0`, `g_ffxChrArray = FFX_MemAlloc(n * 0x1100)`, then for
the lower half only `m_inUse = 0`, `m_name = null`, `m_instance = null`. The upper half is left as the
allocator filled it. `g_ffxChrRoundRobinCursor = 0`. The previous array is not freed here: every caller
path runs `FFX_Ch_EventjumpDispose` first.

---

## 2. `FFX_Ch_Allocate 0x824F90(chrId)` [V]

Also the magic host API entry 0, so effect DLLs spawn through it.

1. `FFX_Ch_SetAllocWriteOnlyFlag(0)`, a write nothing reads.
2. The slot: the first `i` in `0 .. count - 1` with `m_inUse == 0`. None leaves the pointer null.
3. `FFX_Ch_LoadChrData(chrId)`. When null: if the one-shot is set, clear it and **return null**, no slot
   touched. Otherwise `FFX_Ch_LoadChrData(1)` (`c001`) and `m_isFallback = 1` on it, through null if
   `c001` is missing too.
4. `g_ffxChrNoFallbackOnce = 0`.
5. `memset(slot, 0, 0x880)`, **through null when the pool is full**. Everything below writes the slot.
6. `m_inUse = 1`, `m_data`, `FFX_Ch_BindChrData` (`m_id` as a word copy of CHRDATA `m_id`, `m_name`, the
   Tidus cache when the name is `c001` or `c101`, the part mesh pointers when `m_parts` is set, which it is
   not yet).
7. `+0x7E4 m_eventCursor = -1`, `+0x190 m_objId = -1`, `+0x1B8 m_parentUid = -1`.
8. `FFX_Ch_SetPos(0, 0, 0)`: position, `m_prevPos* = m_pos*` (w included), `m_flags1 |= 0x2000000`,
   `m_walkmeshTri = -1`. All skipped, and the flag eaten, when `g_ffxSuppressNextSetPos` is set, so
   such a spawn starts with `m_walkmeshTri = 0`, not -1.
9. `m_groundMode = 1`. Every CHRANIMF at `+0x330..+0x383` ends at `{1.0, 1.0, mode 0}`: `m_anim360`, the
   shade colour (`FFX_Ch_SetShadeColorAnim`, the largest channel 1.0 under its 1.3 clamp), the effect
   colour targets (`m_effectR/G/B = 1` as well), `m_animShadeCoeff` through `FFX_Ch_SetShadeCoeff`,
   which recurses into every live CHR whose `m_parent` is this slot (see 3.2), `m_animShadeA`,
   `m_anim378`.
10. `m_speedMulA = 256`, `m_clipZ = 5000` (`FFX_Ch_SetClipZ(50)` scales by 100), `m_f500 = 1.0`, the
    uniform scale from CHRDATA `m_defaultScale`, `m_f4F4`, `m_f4F0`, `m_collisionRadiusScale`, `m_f4EC`
    from CHRDATA `+0x2C`, `+0x28`, `+0x1C`, `+0x24` (each setter takes CHRDATA for a negative argument),
    `+0x17C = 0.2`.
11. `m_flags1 |= 0x100000`, slots 0, 1, 2 = 0, 1, 2 and slots 16..19 from `g_ffxChrSlotDefaults 0xC49680`
    = 0, 1, 2, 3, `m_nextHokan = 1`, `m_flags1 |= 0x800`, `m_runThreshold = 18.0`, `m_flags1 |= 0x400 |
    0x40000`, `m_groundAttrs = 0xFFFE0000`, `m_flags1 |= 0x80000`, `+0x1B4 = 1.0` (attach scale),
    `m_flags1 |= 0x40000000 | 0x400000`, `m_flags3 |= 1`, `m_flags2 &= ~8`, `+0x420 = 6.0`,
    `m_flags2 |= 0x100`, `m_f110 = 1.0`.
12. `FFX_ChCache_SetByte7(1, id, 1)`, `m_flags2 |= 0x400`, `FFX_Ch_BuildSkeletonInstance`, `sub_83D0A0`
    (the `+0x6DC` texture animation block for CHRDATA with a texture animation section), `sub_8313C0`, `sub_831390`, `FFX_Ch_RegisterShadowRegistryEntry` (the
    effect registry entry, `m_i408`), `m_i408 = 1`, `FFX_ChCache_SetByte7(1, id, 0)`, `m_flags2 &= ~0x400`.
13. `m_b183` by category: 1 for 0..3, 0 for 4..6, 3 for 0xF, which also sets `m_flags1 |= 0x40`.
14. `+0x4D8 = 1234.0`, `FFX_Ch_BuildSkinMatrices`, `FFX_Ch_UpdateBonePositions`, `sub_834E60` (bone 9
    matrix), `FFX_Ch_MarkDirty` (`m_flags1 |= 0x10` unless bit 1, and the part vertex buffers),
    `sub_824620` (`+0x864..0x86C`, `+0x868 = 11245.0` for id 101), `sub_824920` (`+0x81C`, `+0x810`,
    `+0x812` by id).
15. `m_flags1 |= 2` for the 98 ids of `word_C494D8`, `+0x814` and `+0x818` from the tables at
    `0xC495B0`, `m_flags2 |= 0x80` for ids `0x1088`, `0x1089`, 4236, `m_flags2 |= 0x200` for ids 4262,
    4274..4276, 4382, 4385..4387, 12292, 12297..12299.
16. The part bone buffers when `m_flags1 & 0x200000`, `+0x83C`, the u16 store of 1 at `+0x870`
    (`m_jointComposerMode = 1`, `m_forceSmoothComposer = 0`), `m_instance2 = 0`, then
    `FFX_Chr_AttachModelInstance` for `m_instance` and `m_instance2` from the CHR cache.
17. `m_b87C = 1` when magic 275 is loaded and the id is 12292, else 0.

`FFX_Ch_AllocateWithObjId 0x825650` stores `m_objId` through the result unchecked, so it faults on a null
result too, including the clean `NoData` one.

The recreation ports steps 1 to 9, 11, 13 and 15 except the `word_C494D8` and `0xC495B0` tables, as far
as the fields exist in its `MotionChr`, and step 12's skeleton build when the CHRDATA source has a
skeleton. The rest waits for item 10.1's full CHR.

### 2.1 What a failing spawn looks like

| Case | Original | Recreation |
|---|---|---|
| No data, one-shot armed | returns null, one-shot cleared, nothing else touched | `NoData`, the same |
| No data, no one-shot | `c001` loaded and marked `m_isFallback`, spawned as `c001` (id 1, the Tidus cache) | the same, `fallback` set |
| Pool full (or no pool) | CHRDATA loaded (fallback marked if it came to that), one-shot cleared, then a null `memset`: access violation | `PoolFull` after the same loads and clears, the pool untouched |
| No data and no `c001` | null store of `m_isFallback`: access violation | `FallbackMissing` |
| CHRDATA table full (40 records) | `FFX_Ch_BlkAllocate 0x825760` zeroes 300 bytes at null: access violation | the CHRDATA source reports it as no data. Item 10.3 decides |

The co-op kit already refuses a spawn into a full pool (`COOP_DESIGN.md`), which is why a capture can
never show the faulting case.

---

## 3. Dispose

### 3.1 `FFX_Ch_Dispose 0x8266F0(chr)` [V]

Magic host API entry 384.

1. `FFX_Ch_IsLiveChr 0x8261B0`: the pointer must be a lower half slot with `m_inUse`. Anything else,
   null, a scratch slot, a dead slot, returns at once.
2. `FFX_Ch_OrphanChildren` (3.2).
3. A worker 0 wait when magic 275 is loaded, `FFX_Ch_ReleaseTexListACopy 0x83C810` (the `+0x704` texture list copy), `FFX_Ch_ReleaseShadowRegistryEntry` (the
   effect registry entry) unless CHRDATA flag 2.
4. `m_inUse = 0`, `FFX_ChrInstance_SetHidden(m_instance, 0)`.
5. **`if (g_ffxControlledChr == chr || g_ffxTidusChr == chr) { FFX_Ch_SetPlayerChr(null); g_ffxTidusChr = null; }`**
6. The frees: part bone buffers (`FFX_Ch_FreePartBoneBuffers`), the part mesh copies, `m_pMeshPtrs`, the movie texture
   when `m_i6FC`, `m_joints`, `m_parts`, `m_channels`, `m_p6DC`, `m_collisionVolumes`, then
   `g_ffxChrMemoryMoveWanted = 1` (`FFX_Ch_RequestMemoryMove`), both render instances released and nulled, the cloned
   ClassCharacter freed and nulled.

Nothing else in the slot is cleared. A dead slot keeps its id, position, flags and dangling pointers until
the next spawn `memset`s it.

The recreation takes the player bindings as a parameter (PORTING_RULES K7). Each binding is cleared when
it holds the disposed CHR or the disposed CHR is the Tidus cache, and the Tidus cache is cleared when any
binding or the cache held it. With one binding that is step 5 exactly. With three it is a guess at the
co-op intent, see 9.

### 3.2 `FFX_Ch_OrphanChildren 0x826150` [V, disassembly 0x826154..0x82619D]

```
for (p = chr; p != chr + g_ffxChrCount; ++p)       // starts at chr, not at g_ffxChrArray
    if (p->m_parent == chr) p->m_parent = null;
```

So it covers slots `chr .. count - 1` and then scratch slots `0 .. chr - 1` (entries `count .. count + chr - 1`).
A child at a lower index than its parent is never orphaned. Its `m_parent` points at the dead slot, and
the next CHR spawned there becomes its parent: `FFX_Ch_SetShadeCoeffChildren 0x82B7A0` inside that spawn
already reaches it (2, step 9), and every pass that follows `m_parent` (DispatchInBatches' child pass,
the attach) treats it as attached. How often scripts attach a child below its parent is open, see 9.

### 3.3 `FFX_Ch_DisposeAll 0x8268E0` [V]

When the array is not null: `FFX_Ch_Dispose` on every live slot in index order, then `FFX_MemFree` of the
whole block (the scratch half with it), `g_ffxChrArray = null`, `g_ffxChrCount = 0`. Callers:
`FFX_Ch_Init` and `FFX_Ch_EventjumpDispose 0x83B420`, which is the map teardown (`MAP_TRANSITIONS.md` 2.3).

### 3.4 The lookups [V]

- `FFX_Ch_FindById 0x8261F0(id)`: the lowest live slot with `(u16)m_id == id`. The id is an int, so a
  negative id or one above `0xFFFF` finds nothing. Its only code caller is `FFX_Ch_DataDispose`.
- `FFX_Ch_NextLiveChr 0x826D70`: from `g_ffxChrIterCursor`, the next live slot, the cursor left one past
  it. Reset only by the three instruction function at `0x826DB0`. Neither `AllocChrArray` nor `DisposeAll`
  resets it. Callers are effect code (`0x729F19`, `sub_729F90`, `sub_92E1E0`, `sub_932570`) and the magic
  host table at `0xC65A3C`.
- `FFX_Ch_CountLive 0x8285E0`: the debug GUI CHR lists only.
- `FFX_Ch_IsLiveChr 0x8261B0`: 0 for a live lower half slot, -1 otherwise.

---

## 4. The scratch slots

`FFX_Ch_CopyToScratchSlot 0x828B30(chr)` is `qmemcpy(&chr[g_ffxChrCount], chr, 0x880)` and returns the
copy. No check of any kind. Its one caller is `FFX_Field_FreezeActorsToScratch 0x866240`, see
`MAP_TRANSITIONS.md` section 4 for when.

`FFX_Ch_CopyState 0x828620(dst, src)` [V], called only from `FFX_Atel_RestoreActorChrState 0x86E5B0`,
copies back:

- `+0x0C..+0x3B` position, previous position, `m_vec2C`
- `+0x5C..+0x8B` scale, `+0x68`, offset, `+0x78`, the ground normal with its w
- `+0xBC..+0xCB` the effect colour and `+0xC8`
- `+0x158..+0x17F` minus `m_speed`: rotation, `+0x15C..+0x167`, `m_moveDir`, `m_groundHeight`,
  `m_runThreshold`, `m_clipZ`, `+0x17C`. Not `m_cameLen` (`+0x178`)
- `+0x180..+0x188` except `m_b184`
- `m_flags1` bits `0xE07C09C7`: 1, 2, 4, 0x40, 0x80, 0x100, 0x800, 0x40000, 0x80000, 0x100000, 0x200000,
  0x400000, 0x20000000, 0x40000000 and 0x80000000 (the last through the closing `& 0x7FFFFFFF`)
- `m_flags2` bits `0x38E`: 2, 4, 8, 0x80, 0x100, 0x200
- `+0x1A0..+0x1BB`: the parent joint, the attach offset, the attach scale, `m_parentUid`
- `m_i408`, the `0xDC` byte look at block `+0x40C..+0x4E7` (which ends with `m_collisionRadiusScale`),
  `+0x4EC..+0x503`, `+0x6D8`, `m_speedMulA`, `+0x814..+0x81F`, the seven CHRANIMF, the 32 motion slots
- last, `m_parent = m_parentUid == -1 ? null : FFX_Atel_GetActorChrById 0x86B3A0(m_parentUid)`

The recreation keeps the scratch as its own array of `count` entries and restores the same subset, with
the actor lookup passed in.

---

## 5. The per step passes (PROGRESS 10.6)

### 5.1 The order inside `FFX_MainStep 0x820AE0` [V]

| Call site | Function | Runs | Gate |
|---|---|---|---|
| 0x820FDF | `FFX_Ch_ClearFlags1StepBits 0x832DB0` | once, before the sub-step loop | gameplay branch |
| 0x82107D | `FFX_Ch_UpdateMotionAll 0x832E10(0.033373334)` | once per sub-step | `g_ffxChrMotionUpdateEnabled` |
| 0x8210DA | `FFX_Ch_DispatchInBatches 0x833390` | once, after the loop | the same |
| 0x8212F8, 0x821325 | `FFX_Ch_StepAll 0x82ED50` | once per drawn screen, the site by `g_ffxMapAlphaPassFirst` | not in a movie frame, `g_ffxChrDrawEnabled`, `g_ffxChrVisible`, and on screens after the first only without a second camera |
| 0x82F256 | `FFX_Ch_UpdateCameLenAndZClip 0x82E220(chr)` | inside StepAll's third walk, per CHR | `m_inUse` and not `m_flags2 & 0x20`. Hidden CHRs included |
| 0x821465 | `FFX_Ch_ShadowPassAll 0x830180(0)` | effects pass k == 0 | map draw on, map visible, `FFX_Map_HasBgSection` |

`engine/app/MainStep` in the recreation reproduces these call sites and gates, and its test
(`ChrPassOrderMatchesFfxMainStep`) drives the ported passes through it.

Corrections: PROGRESS 10.6 orders `UpdateCameLenAndZClip` after `ShadowPassAll`, the binary runs it inside
StepAll. `VISUAL_CENSUS.md` gives the shadow pass "shadow mode 1", the argument is 0. `FFX_GAME_NOTES.md`'s
table is right.

### 5.2 What each pass reads and writes

**`FFX_Ch_ClearFlags1StepBits`** [V]: `m_flags1 &= 0xF8FFFFFF` on every live CHR. Reads nothing else.

**`FFX_Ch_UpdateMotionAll`**: `CHARACTER_MOTION.md` sections 1 to 4 and 10. For this section: it joins
the previous call's render jobs at `0x833217`, right before pass 4 (`FFX_Mot_AdvanceFrame`), and posts
`FFX_Ch_UpdateRenderJob(chrs, 0, n)` for the parentless CHRs at the end (`0x83332D`, `0x833374`). [V]

**`FFX_Ch_DispatchInBatches`** [V]: `FFX_Ch_JoinUpdateJobs`, then the parented CHRs that pass the gate
(live, not hidden unless `m_b181`, not frozen) posted as `FFX_Ch_UpdateRenderJob(chrs, 1, n)`, a batch
each time the skeleton joint counts (`m_skeleton +10`) reach 200, a second join, then two event walks,
roots first then children: a CHR with `m_flags1 & 0x1000000` runs `FFX_Mot_RunEvents` and clears
`0x1000`, otherwise sets `0x1000`. Then `FFX_Ch_MarkEventsDispatched 0x832540` latches `g_ffxChrBit8OverrideToken = 1`,
which `FFX_Ch_BuildJointOverrideMatrices` reads and clears, and `dword_13009C0` takes the
`g_ffxPs2HwRegSink` delta (a profile counter).

**`FFX_Ch_StepAll`** [V for the walks]: reads `FFX_SaveData_GetFlagsC89`, the level hash, the magic id,
the story progress, the ATEL context bit 2, the battle state, the camera matrices.

- Walk 1 (`0x82EE97`), live and unhidden: `m_flags2 &= ~0x40`, skip on `0x20`, `m_flags2 &= ~0x10`, the
  world transform and bone matrices to the render instances. At the top of every iteration it pushes
  `g_ffxTidusChr`'s position to `sub_641080` when the cache is live.
- Walk 2, live, unhidden, not `0x20`, not `m_flags1 & 0x2000`, `m_animShadeA.cur != 0`:
  `m_flags2 |= 0x50`.
- `FFX_Ch_TimeSlicedFlags1Bit10 0x8336A0` (5.4), `FFX_ChrShadow_ResetBlobBuffer`, `sub_644AB0`.
- Walk 3 (`0x82F224`), live: `m_flags2 &= ~0x40`, skip on `0x20`, `FFX_Ch_UpdateBoneMatrices`,
  **`FFX_Ch_UpdateCameLenAndZClip`**, `sub_831480`, the per level and per id `m_b183` overrides (levels
  7044, 9465, 3993, 9417, story 922, parent 8314, ids 20489..20566, 16425), two magic hide hacks that
  write `m_hideFlags` (ids 4225..4372 under `dword_1940AC4`, id 12311 under `dword_1940AB8`), the blob
  shadow, `FFX_ChrInstance_SetHidden` by the hide bits, the material colour (skipped on catch-up steps).
- Walk 4, only when walks 1 or 3 counted a CHR: live, unhidden, not `m_flags1 & 0x1000`, alpha non-zero,
  not `0x20`: `m_flags1 |= 0x1000`, the joint override matrices for magic 437 and 438, the job list for
  CHRs with `m_flags1 & 0x200000` and a cloned ClassCharacter, the texture animation call (not on
  catch-up), `FFX_Ch_AccumulateLights`, the shadow registry bit, the specular upload. Then the job list
  is posted to worker 0 as `FFX_Ch_JointOverrideJob 0x82E390`, ids 3 and 103 first.

**`FFX_Ch_UpdateCameLenAndZClip`** [V]: clears hide bits `0x08 | 0x20` and `m_flags1 & 0x10000000`,
`m_cameLen = |m_bodyCentre - FFX_Came_GetPos(1)|`. Returns 1 if any other hide bit is set, 0 without
`m_flags1 & 0x800`, sets `0x08` and returns 1 when `FFX_Chr_InstanceAnyMeshVisible` says no sub-mesh is in
the frustum, else sets `0x20` past `m_clipZ` unless the second camera is active, with half the distance on
level 9465 under save flag `0xC89` bit 4 and not bit 7. The frustum test reads Phyre's cull state, which
is why the recreation still stubs it (PORTING_RULES F2, `VISUAL_CENSUS.md` row "view cull").

**`FFX_Ch_ShadowPassAll`** [V, 0x830183, and the `push esi` at 0x821464 before the call at 0x821465]: `if (arg == 0) return;`. The caller
pushes `esi`, the effects pass index, which is 0 on the only path that reaches the call (`0x82142C test
esi, esi / jnz` skips it otherwise). So the walk over `m_flags2 & 0x10` CHRs never runs in retail. The
recreation keeps the pass, with the argument, as a no-op.

### 5.3 Worker jobs

| Job | Posted by | Thread | Joined | Writes |
|---|---|---|---|---|
| `FFX_Ch_UpdateRenderJob 0x833530` mode 0 | `FFX_Ch_UpdateMotionAll`, end | Phyre pool via `JobSchedule__postJob3` | the next UpdateMotionAll's pass 4, or DispatchInBatches' first join | blink (`rand`, worker state), look at, skin matrices and bone positions (`m_bodyCentre`) when `m_flags1 & 0x3000000`, shade from lights, `m_flags1 & 0x200` = is the controlled CHR, `+0x878` stage marker |
| the same, mode 1 | `FFX_Ch_DispatchInBatches` | the same | its own second join | the same for parented CHRs, plus the shade constants |
| `FFX_Ch_JointOverrideJob 0x82E390` | `FFX_Ch_StepAll` walk 4, through `FFX_Ch_PostJointOverrideJob` -> `JobSchedule__postJob2OnWorker` | worker 0 | `FFX_Job_WaitWorker0Idle` at the end of the step, and inside StepAll before it regrows the job list | joint override matrices, **`FFX_Ch_StepSecondaryPhysics 0x822DE0`**, which draws `rand()` on worker 0 and is the only clear of `m_flags3 & 4` |

Correction: the `m_flags3 & 4` clear is in the joint override job StepAll posts, not in
`FFX_Ch_UpdateRenderJob`. Both run between one step's motion and the next, so the probe caveat in
`PROBE_FIXTURES.md` section 8 stands.

The worker `rand()` (checked 2026-10-13): StepSecondaryPhysics calls the imported MSVCR110 `rand` at
0x82395B as `rand() % 1000` and scales a jitter vector by it. That CRT keeps the seed in per thread data,
so the draw advances worker 0's own sequence, never the step thread's (which `FFX_Time` drains once per
sub-step). It is reproducible only while worker 0 is the one thread that runs this job and nothing else
on it draws. Whether the jittered result reaches a hash visible field is open [I].

Open [I]: between the mode 0 dispatch at the end of one UpdateMotionAll and its join, the step thread runs
`FFX_PlayerCam_Step` and, with two sub-steps, the whole next sub-step's player control, battle, ATEL,
message windows and camera. Whether any of them reads `m_bodyCentre`, the look at block or `m_flags1 &
0x200` while a job writes it has not been checked.

### 5.4 `FFX_Ch_TimeSlicedFlags1Bit10 0x8336A0` [V]

The budget is `(500 - g_ffxProfSectionTicksPrev[0]) / 40` CHRs per step, read through the misnamed
`FFX_Prof_GetPrevSectionTicks 0x820580`. Section 0's ticks are the sum of `g_ffxPs2HwRegSink` at every
`FFX_Prof_MarkSection` of the previous step, and that sink is where the ported PS2 code writes its
hardware register values (`0x30000000`, `0x38000000` and so on, 219 references). So the budget is not a
clock but it is fed by whatever register stand-in the render and effect paths last wrote, and a large
value makes it negative, which skips the pass. From the cursor it sets `m_flags1 |= 0x10` (unless bit 1)
on CHRs with `m_flags1 & 6` and `m_flags2 & 0x40`. Not ported. Whether the result is stable across two
machines is open.

---

## 6. Capture recipe for the PROGRESS 10.2 done-when [I, not yet run]

The done-when wants a spawn sequence recorded from the game to yield the same slot indices here. The
co-op kit's probe plugin (`loader/plugins/probe`, PROBE_FIXTURES.md) is where this belongs, as a fifth
kind. Proposed: kind 5, `chrpool`, one record per pool call, with the 20 byte prefix of section 1.2 of
`PROBE_FIXTURES.md` (phase 0 entry, 1 return).

Hook entry and return, chaining with any other hook on the same bytes:

| Function | VA | RVA | Argument |
|---|---|---|---|
| `FFX_Ch_AllocChrArray` | 0x825670 | 0x425670 | `[esp+4]` count |
| `FFX_Ch_Allocate` | 0x824F90 | 0x424F90 | `[esp+4]` chrId, returns `CHR*` in eax |
| `FFX_Ch_Dispose` | 0x8266F0 | 0x4266F0 | `[esp+4]` `CHR*` |
| `FFX_Ch_DisposeAll` | 0x8268E0 | 0x4268E0 | none (the IDB's `esi` argument is a Hex-Rays artefact) |
| `FFX_Ch_CopyToScratchSlot` | 0x828B30 | 0x428B30 | `[esp+4]` `CHR*` |
| `FFX_Ch_CopyState` | 0x828620 | 0x428620 | `[esp+4]` dst, `[esp+8]` src |
| `FFX_Ch_SetNoFallbackOnce` | 0x8285D0 | 0x4285D0 | none, entry only |
| `FFX_Ch_SuppressNextSetPos` | 0x82ACC0 | 0x42ACC0 | none, entry only |
| `FFX_Ch_SetPlayerChr` | 0x82DAD0 | 0x42DAD0 | `[esp+4]` `CHR*`, entry only |

Per record, after the prefix:

| Field | Source |
|---|---|
| u8 op | 1 AllocChrArray, 2 Allocate, 3 Dispose, 4 DisposeAll, 5 CopyToScratchSlot, 6 CopyState, 7 SetNoFallbackOnce, 8 SuppressNextSetPos, 9 SetPlayerChr |
| i32 arg | the id or count, or the slot index of a `CHR*` argument as `(p - g_ffxChrArray) / 0x880` (-1 for null, the raw offset for anything outside the lower half) |
| i32 arg2 | CopyState's source the same way, measured from `g_ffxChrArray + count * 0x880` when it is a scratch slot |
| i32 result | Allocate's return as a slot index, -1 for null |
| i32 count | `g_ffxChrCount` RVA `0x1FC44E0` |
| u32 array | `g_ffxChrArray` RVA `0x1FC44E4`, only to see rebuilds, never compared by value |
| i32 tidus | `g_ffxTidusChr` RVA `0xEFBC60` as a slot index |
| i32 controlled | `g_ffxControlledChr` RVA `0xF00788` as a slot index |
| u8 noFallbackOnce | `g_ffxChrNoFallbackOnce` RVA `0xEFBD98` |
| u8 suppressSetPos | `g_ffxSuppressNextSetPos` RVA `0xEFFAD8` |
| u8 fallback | on Allocate's return: CHRDATA `+0xF8 m_isFallback` of the new CHR's `m_data` (`CHR+0x1C4`) |
| u8 pad | 0 |
| i32 eventId | `g_ffxCurrentEventId` RVA `0xEFBC40` |
| u8[16] inUse | bit i = `m_inUse` (`CHR+0x002`) of slot i, slots 0..127 |
| i16[128] ids | `m_id` (`CHR+0x000`) of slots 0..127, so a replay can check the dead slots' stale ids too |
| i32[128] parents | `m_parent` (`CHR+0x19C`) of slots 0..127 as slot indices, for the orphan rule |

That is 20 + 572 bytes. The pool is 36 to about 60 slots in practice, 128 is headroom. Read every CHR
inside `__try` and validate the array span once per rebuild, never per row (`workshop::Readable` is a
syscall).

Replay in the recreation: one `ChrPool`, a CHRDATA stand in that answers each id with a record and
reports the recorded `fallback` flag, and the ops applied in order from the entry records. After each
return record compare the slot index, `count`, the in-use bitmap, the ids of live slots, `tidus`,
`controlled` and the one-shots. The failing spawn the original survives (`NoData` under the one-shot) is
capturable. The faulting ones are not, so they stay hand built tests.

A good session: boot, load a save in Besaid village, walk into the temple and out (two rebuilds), start
and win a random battle on the beach (the scratch save and restore, `CopyToScratchSlot` per field actor,
then `CopyState`), and a cutscene that spawns and disposes NPCs, for example the Besaid aurochs
blitzball scene. Two runs from the same save should give identical op streams, a check worth making
before the first comparison.

---

## 7. What the recreation deviates on, on purpose

- The three faulting spawns return a status instead of faulting.
- The scratch is a separate array (D2), deep copied. The original's shallow copy shares the joint and
  channel blocks with the live CHR, which the battle path frees right after. `FFX_Ch_CopyState` never
  reads them, so nothing observes the difference.
- `FFX_Ch_CopyToScratchSlot` refuses a slot outside the pool where the original writes out of bounds.
- `AllocChrArray` zeroes the slots it does not clear, where the original leaves allocator bytes. Nothing
  reads a dead slot before the spawn `memset`.
- `g_ffxChrAllocWriteOnlyFlag`, `g_ffxChrMemoryMoveWanted` and the debug prints are dropped.

---

## 8. Oddities found on the way

- `FFX_ChCache_FreeSlot 0x840EC0` clears `g_ffxTidusChr` when the cache slot's `+0x0C` pointer equals it
  (`sub ecx, g_ffxTidusChr / neg / sbb / and`). That compares a cache entry's data pointer with a CHR
  pointer, which should never match [I]. Recorded, not ported.
- A fallback spawn becomes the Tidus cache, because the fallback is `c001`.
- `FFX_Ch_StepAll` walk 1 pushes the Tidus cache's position to `sub_641080` once per pool entry, not once
  per call.

---

## 9. Open questions

1. What the ATEL context words `+0x0C`, `+0x14`, `+0x16`, `+0x18` count (`.ebp` header words 27, 10, 11, 14).
2. Whether any shipped script attaches a child CHR in a lower slot than its parent and then disposes the
   parent, which is when the orphan rule shows.
3. Co-op: should disposing the Tidus cache unbind every player, as the solo rule does for the one pad,
   or only player 0. The recreation clears every binding for now.
4. The races of 5.3, and the stability of the 5.4 budget.

---

## 10. Proposed IDB names

Not applied, the IDB was read only for this pass.

| VA | Current | Proposed | Why |
|---|---|---|---|
| 0x826DB0 | `FFX_Ch_ResetLiveChrIter` | `FFX_Ch_ResetLiveChrIter` | its whole body is `g_ffxChrIterCursor = 0`, the reset of `FFX_Ch_NextLiveChr` |
| 0x83C4D0 | `FFX_Ch_SetAllocWriteOnlyFlag` | `FFX_Ch_SetAllocFlag13019B4` | writes `g_ffxChrAllocWriteOnlyFlag`, which nothing reads, called with 0 at the top of `FFX_Ch_Allocate` |
| 0x13019B4 | `g_ffxChrAllocWriteOnlyFlag` | `g_ffxChrAllocWriteOnlyFlag` | the same |
| 0x826260 | `FFX_Ch_FreePartBoneBuffers` | `FFX_Ch_FreePartBoneBuffers` | frees each part's `+0x2C` bone buffers and their two sub blocks, from `FFX_Ch_Dispose` |
| 0x827250 | `FFX_Ch_RequestMemoryMove` | `FFX_Ch_RequestMemoryMove` | its body is `g_ffxChrMemoryMoveWanted = 1` |
| 0x83C810 | `FFX_Ch_ReleaseTexListACopy` (applied) | | frees `m_texListACopy` (CHR+0x704) and its render side, from `FFX_Ch_Dispose` and the magic host table entry 346. Corrected 2026-10-13: +0x6DC..+0x708 is texture animation, not cloth (CHARACTER_MOTION.md section 12) |
| 0x7FC650 | `FFX_Ch_ReleaseShadowRegistryEntry` | `FFX_Ch_ReleaseEffectRegistryEntry` | finds the CHR in `g_ffxChrShadowRegistry` and releases its effect unit |
| 0x7FC780 | `FFX_Ch_RegisterShadowRegistryEntry` | `FFX_Ch_RegisterEffectRegistryEntry` | its counterpart, called from `FFX_Ch_Allocate` |
| 0xA44B40 | `FFX_Ch_PostJointOverrideJob` | `FFX_Ch_PostJointOverrideJob` | posts `FFX_Ch_JointOverrideJob` to worker 0 |
| 0x82E200 | `sub_82E200` | `FFX_Ch_IsWakkaModel` | `m_id == 3 \|\| m_id == 103`, the ids StepAll posts first [I for the character] |
| 0x820580 | `?_Get_future_error_what@std@@YAPBDH@Z_2` | `FFX_Prof_GetPrevSectionTicks` | returns `g_ffxProfSectionTicksPrev[a1]`, a misidentified CRT match |
| 0x824620 | `sub_824620` | `FFX_Ch_InitBlock864` | zeroes `+0x864..+0x86C`, `+0x868 = 11245.0` for id 101 |
| 0x824920 | `sub_824920` | `FFX_Ch_InitBlinkParams` | `+0x81C`, `+0x810`, `+0x812` by party id [I for blink] |
