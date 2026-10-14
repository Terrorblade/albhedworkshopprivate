# ATEL contexts, the actor pool and the event package load

Written 2026-10-13 for PROGRESS 11.1 of the recreation (`E:\Git\projectzanarkand`, `src/engine/atel`).
Read with `EBP_FORMAT.md` (the container and the bytecode), `MAP_TRANSITIONS.md` section 3 (where in a
map change this happens), `MONSTER_AI.md` sections 2 and 3 (the battle contexts) and the ATEL section of
`FFX_GAME_NOTES.md` (the actor's runtime fields). Everything marked [V] was read in IDA, [D] is a census
over the shipped data with `../tools/ebp.py` or the recreation's `tools/mon_ai.py`, [I] is inferred.

Nothing in the IDB was renamed for this. Proposed names are in section 9.

## 1. Short version

- The field package is asset kind 12, row `18 * eventId`. Kind 12 has 7,236 rows, so 402 event ids.
  348 rows are non-empty, 330 of those ship. The boot scenes 393..399 have empty rows and their own
  host0 paths, and all seven ship. [V] [D]
- The loader reads five fixed header slots (`+0x04`, `+0x08`, `+0x0C`, `+0x14`, and never `+0x10`),
  not the `-1` terminated list. In the 32 legacy packages the slot it reads as the default message table
  (`+0x14`) is the end of data sentinel, so that table points one past the image. Ten of those are
  reachable by event id (0, 2, 230, 240, 321, 323, 340, 352, 354, 359). [V] [D]
- The ATEL header word `+0x08` is the **end** of the entry record table (`FFX_Atel_GetEntryRecord`
  clamps against it), which is why it is also where the short name string starts. `+0x18` is the
  **system actor** id, the actor the doorway arrival script runs on and the camera syscalls compare
  against, `0xFFFF` for none. [V]
- The actor pool's segment decides the slot size, the definition's type decides what the constructor
  writes. In all 397 packages each type sits in exactly one segment: type 1 in the 2904 byte segment,
  type 4 in 1368, types 2 and 3 in 1464, types 5 and 6 in 744, type 0 in the 48 byte small segment. [D]
- One actor constructor (`FFX_Atel_ConstructActor`) serves all three context setups. Large actors get the full
  runtime record and two init threads (channel 7 entry 0, channel 0 entry 1). Small actors get only the
  first 48 bytes. [V]
- Script variables of classes 2, 3, 4 and 6 live **inside the loaded package**. The constructor clears
  each large actor's class 3 region in the image. So the image is not read only in the original, and a
  port keeping A1 needs a mutable copy of those regions. [V] [D]
- The multi-block form lays global actor ids out block by block (all large actors of every block, then
  all small ones), but `FFX_Atel_GetActor` lays slots out by segment over the summed counts. The two
  agree only when the blocks share one segment. Every one of the 361 monster blocks has only type 2
  actors in the 1464 segment, so the monster context is safe. A mixed block list would overrun. [V] [D]

## 2. The load, in the order the engine reads things

`FFX_Ev_LoadEventPackage 0x872EF0`, the parts that touch the package (the rest is `MAP_TRANSITIONS.md`
section 3.1):

| step | read | notes |
|---|---|---|
| 1 | `FFX_Ev_ReadEventPackageImage(12, 18 * id)` 0x88D2C0 | asset loader slot 4 selects kind 12, slot 5 gives the size (0 for a missing file), `FFX_MemAlloc` rounds up to 2048, the **first dword is zeroed**, slot 0 reads, slot 2 pumps synchronously. A boot scene (393..399) takes its size from `FFX_Ev_GetBootPackageSize 0x67AAE0` instead [V] |
| 2 | the event name | `FFX_Asset_SelectKindAndGetPath(12, 18 * id)` 0x8AB7D0, or `FFX_Ev_GetBootPackagePath 0x67AAC0` (`off_C334B8`, `host0:/ffx/master/jppc/event/obj/sc/sceneN/sceneN.ebp`) for the boot scenes [V] |
| 3 | `image[0] == 'E' && image[1] == 'V'` | else `while (1);` [V] |
| 4 | `image + *(image + 4)` | the ATEL block. 0x40 in all 397 [D] |
| 5 | `FFX_Atel_GetResFlags`, `FFX_Atel_GetEntryMapNo(atel, 0)` | RES `+0x08`, entry record 0 `+0x00` (or the RES `+0x3C` override table) [V] |
| 6 | `FFX_Atel_CalcActorPoolSize(atel)` 0x86A290 | then `FFX_Ev_AllocActorPool 0x88E650` rounds up to 2048. The pool is not cleared [V] |
| 7 | `image + *(image + 8)` | the Japanese table, the context's first message table when `FFX_LoadLocalizedBin 0x88CAC0` returns null. When the .bin exists, `g_ffxEvBuiltinMsgTable 0x23CBD60 = image + *(image + 8)` unconditionally, even for a 0 slot [V] |
| 8 | `image + *(image + 20)` | the default table, 0 when the slot is 0. Becomes context `+0x38` [V] |
| 9 | `maybe_FFX_Map_SetupChars 0x875510` | section 6.1 |
| 10 | `image + *(image + 12)` | the SeSep block for `FFX_Atel_StartEventSe 0x866430` [V] |

`image + *(image + 16)`, the font slot, is never read. [V]

**The legacy slot.** 32 packages have four sections and the end of data sentinel, so their `+0x14` is
the end of data (equal to the file size in all 32). The loader still takes it as the default message
table, and `maybe_FFX_Map_SetupChars` keeps it at context `+0x38` because it is not 0. Its readers
(`sub_86B170 0x86B170`, Core 566 under the international environment) would read past the image.
`bltz0001` (event 340) is a multiple of 2048 long, so for it the read is past the allocation too. [V] [D]

## 3. The ATEL header, by reader

Offsets from the block base. Rows marked new are corrections or additions to `EBP_FORMAT.md`.

| off | reader | meaning |
|---|---|---|
| `+0x00` | none found | nonzero in every package |
| `+0x04` | `FFX_Atel_GetEntryRecord 0x86BFE0`, `FFX_Atel_GetEntryMapNo 0x86BD90`, `FFX_Atel_StartEntryArrivalScript 0x86ED80` | the entry records, 32 bytes each. 0 in 7 packages |
| `+0x08` | **new**: the same three, as the end bound | `off = base + 32 * entryPoint; if (off >= end) off = base`. Also the short name string, which follows the table. 390 of 390 spans are a multiple of 32 |
| `+0x10` | none in the load | block size |
| `+0x14`, `+0x16`, `+0x1C` | `FFX_Atel_ResetContext 0x86EE10` (context `+0x14`, `+0x16`, `+0x18`), `FFX_Atel_CalcActorPoolSize`, and in the multi-block form `FFX_Atel_GetCount1464 0x86BCB0`, `FFX_Atel_GetCount1368 0x86A9E0`, `FFX_Atel_GetCount744 0x86B020` | the 1464, 1368 and 744 byte segment counts |
| `+0x18` | **new**: `FFX_Atel_StartEntryArrivalScript` (thread target), `FFX_Atel_GetSystemActorId 0x86B5D0` (read through context 0's block by 60-odd Came syscalls and Core 138, which pass `actorId == systemActor` to `FFX_Came_GetHandleForClass`) | the system actor id, `0xFFFF` none. A type 1 actor in 383 packages, `0xFFFF` in 14 [D] |
| `+0x1A` | none found | 0 in 383, `0xFFFF` in 11, 1 in 2, 4 in 1 [D] |
| `+0x1E` | none found | `0x8010` or 0 |
| `+0x20` | `FFX_Atel_ResolveVarAddress 0x86C2E0` class 6 | |
| `+0x24`, `+0x28` | none found | |
| `+0x2C` | the RES getters (`FFX_Atel_GetSpareChrCount 0x86AA20` and its neighbours at 0x86AA70..0x86AD20) | 0 in 5 packages, every getter returns 0 then |
| `+0x30` | the interpreter | code base |
| `+0x34`, `+0x36` | `FFX_Atel_GetActorCount 0x86C190`, `FFX_Atel_GetLargeActorCount 0x86A650` | |
| `+0x38` | `FFX_Atel_GetActorDef 0x86BB80` | definition offsets. The records follow the array directly, 52 apart, in all 397 [D] |

Definition `+0x10` is the **class 3 storage size** (the constructor's `memset` length and
`FFX_Atel_GetActorClass3SliceBytes`'s slice size). [V]

## 4. The pool

`FFX_Atel_CalcActorPoolSize 0x86A290`:

```
744*h[+0x1C] + 1368*h[+0x16] + 1464*h[+0x14] + 2904*(h[+0x36] - h[+0x16] - h[+0x1C] - h[+0x14]) + 48*(h[+0x34] - h[+0x36])
```

`FFX_Atel_GetActor 0x86A830` lays the slots out in this order, counts from the context:

| segment | slot | count | types (census) [D] | constructor needs [V] |
|---|---|---|---|---|
| 0 | 2904 | `ctx+0x0C - ctx+0x18 - ctx+0x16 - ctx+0x14` | 1 x6707 | type 1 writes up to `+0xB54` |
| 1 | 1368 | `ctx+0x16` | 4 x1944 | 9 thread channels and the list nodes end below `+0x558` |
| 2 | 1464 | `ctx+0x14` | 2 x648, 3 x486 | the motion state at `+0x558` |
| 3 | 744 | `ctx+0x18` | 5 x1382, 6 x116 | 2 channels, nodes, motion state at `+0x284`, `+0x2E4` |
| 4 | 48 | `ctx+0x10 - ctx+0x0C` | 0 x441 | identity only |

So the segment of an id is the only thing the slot size depends on, and the type only has to fit it.
The compiler folded the last branch into `696 * n744 + 48 * k`, which is `744 * n744 + 48 * (k - n744)`.

**The cache.** `GetActor` keeps one slot: `g_ffxAtelActorCachePtr 0x13270E4`, keyed by
`g_ffxAtelActorCacheCtx 0x13270DC` and `g_ffxAtelActorCacheId 0x13270E0`. The "generation" is context
`+0x1A`, which `FFX_Atel_ResetContext` sets to the **context index**, so the key is (context, requested
id). The cached id is the requested one, before the clamp. [V]

**The clamp.** An id below 0 reads actor 0 and clears `g_ffxAtelActorClampCount 0x13270D8`, an id at
or past context `+0x10` reads actor 0 and adds one to it. Both happen on a cache miss only, so a run of
the same bad id counts once. The counter is cleared by the stack and variable error paths of the
interpreter and by `FFX_Atel_InstallContextCallbacks` when the context has no pool. No reader that
changes behaviour was found. [V]

**The kind.** `FFX_Atel_GetActorKind 0x86A7B0` returns 7 for an id outside `[0, ctx+0x0C)`, so every
small actor reads as kind 7. [V]

## 5. The actor constructor, `FFX_Atel_ConstructActor 0x86CB00`

`(int params[8], int clearClass3)`. The parameter block, as all three setups fill it:

| param | single block (field and static) | multi-block |
|---|---|---|
| 0 | 1 large, 0 small | same |
| 1 | the actor id | the global id |
| 2 | the context index (0 for the field) | the context index |
| 3 | 0 | the block index |
| 4 | the id | the index inside the block |
| 5 | the definition | same |
| 6 | the block base | the block's base |
| 7 | `a5` of the setup, 0 in retail | the block's extra (0 from `FFX_BtlEv_AddScriptBlock`) |

`clearClass3` is 1 from the single block setups and 0 from the multi-block one.

Every actor: `+0x00 = definition`, `+0x04 = block base`, `+0x08 = param 7`, `+0x0C..+0x2B = 0`,
`+0x2E = param 1` (the id). That is all a small actor gets, the rest of its 48 bytes are whatever the
allocator left. [V]

Large actors only, in order: `+0xB8 = 0`, `+0xC4 = 0` (the stack pointer), `+0xAE = param2 & 0xF |
param3 << 4`, the registers `+0x48..+0x7C` cleared, `FFX_Atel_InitActorThreadRecords 0x86D300` (each channel's thread
record gets the context, block and local index at `+0x3C..+0x40`, a type 1 actor's records also get
pointers to its MOVE and ROT blocks), `+0x94 = 1.0`, the flag words `+0x34`, `+0x36`, `+0x38`,
`+0x32 = 0x040E0000`, `+0x98 = 0`, `+0xB4 = -10000.0`, `+0xAD = 0`, `+0xAF = 15`, `+0xAA = 0`,
`+0x40 = -1`, `+0x44 = 0`, the motion state block for types 1, 2, 3, 5, 6 (`+0x284` for 5 and 6,
`+0x558` otherwise), type 1's extra fields up to `+0xB54`, `+0xAB` by type (-9 for 1, -1 for 2, 3, 5, 6,
0 for 4), `FFX_Atel_InitActorListNodes 0x86D510` (the list nodes), then:

- when `clearClass3`: `memset(block + def[+0x2C], 0, def[+0x10])` if `def[+0x2C]` is nonzero and the
  size positive. In the multi-block form `+0xB8` would win, but it is still 0 at that point.
- `FFX_Atel_CreateThread` on channel 7 for entry 0 and on channel 0 for entry 1, unless the id is
  `0xFFFF`.
- `sub_8653F0(id, 10)`. [V]

Types 5 and 6 have **two** thread channels, not nine (`FFX_Atel_InitActorThreadRecords` clamps the channel to 1 for them). [V]

## 6. The three setups

### 6.1 The field, `maybe_FFX_Map_SetupChars 0x875510`, context 0

1. The dev harness preload (`sub_885B00` per entry of `a6`), null in retail.
2. `FFX_Atel_ResetContext(0, atel, pool, poolSize)` with the **unrounded** pool size.
3. `+0x34 = messages`, `+0x38 = default ?: messages`, `+0x50 = 0` (no boot hook),
   `+0x4C = FFX_Atel_ContextStepCallback3x 0x867740`, `FFX_Atel_InstallContextCallbacks(0)`,
   `FFX_Atel_SelectContext(0)`.
4. Ids `[0, large)` through the constructor as large, `[large, total)` as small, each followed by
   `FFX_Atel_SetActorDevResources 0x86D670`, which copies the dev harness table into `+0x0C..` and does nothing in retail.
5. The CHR pool size from context `+0x0C`, `+0x18`, `+0x16`, `+0x14` and RES `+4` (`CHR_POOL.md`).
6. `FFX_Atel_StartEntryArrivalScript`, then `FFX_Atel_CallContextBootHook`, which does nothing because
   step 3 cleared the hook. The field's init scripts run from the threads the constructor made. [V]

### 6.2 The static single block, `FFX_Atel_SetupStaticContext 0x8759B0`, context 6

Its one caller is `FFX_Menu_SetupAtelContext 0x8BE3A0` from `FFX_Module0_ExecBoot`, for the menu context over a
static block at `unk_1576F70` with the pool at `unk_1574F70`. The same as 6.1 except: the pool size
passed to the reset is -1, only `+0x34` is set (so `+0x38` keeps whatever it held), the step slot is
left for `InstallContextCallbacks` to fill with `FFX_Atel_ContextStepCallback 0x867710`, and the boot
hook stays, so `FFX_Atel_BootAllActors 0x8683E0` runs every actor's entry 0 at the end. [V]

### 6.3 The multi-block form, `FFX_Atel_SetupMultiBlockContext 0x8756D0`

Called only from `FFX_BtlEv_BuildScriptContext 0x798390`, which maps group 0, 1, 2 to context 2, 3, 4
with step callbacks `FFX_BtlEv_StepBattleContext 0x7972E0`, `FFX_BtlEv_StepMonsterContext 0x7973B0`,
`FFX_BtlEv_StepContext4 0x797350`. Per group (84 bytes at `dword_1134470 + 84 * group`) it holds up to
eight block pointers (`FFX_BtlEv_AddScriptBlock 0x797A40` refuses a ninth), a parallel extra list (all
0), the pool size summed with `FFX_Atel_CalcActorPoolSize` and the private class 3 size summed with
`FFX_Atel_CalcBlockClass3Bytes 0x86C0C0`. Groups 0 and 1 allocate both with `FFX_MemAlloc`. Group 2 uses a static 2048 byte
buffer at `unk_1135570` and raises `FFX_Btl_InternalError` when the two sizes, each rounded to 16,
exceed it. [V]

The setup:

1. `FFX_Atel_ResetContext(ctx, nullptr, pool, -1)`, which leaves every count 0, including `+0x12`.
2. `+0x34 = 0`, `+0x54 = FFX_Atel_ResolveClass3Private 0x8756B0` (class 3 resolves to `actor[+0xB8] + offset`).
3. Sums over the blocks: `+0x0C` large, `+0x10` total, `+0x14`, `+0x16`, `+0x18` the segment counts,
   stored as u16. `+0x4C` the step callback, `+0x1F8` the block list, `+0x1FC` the extra list, `+0x30`
   the battle work area `unk_1134570` (the class 1 base). Then `InstallContextCallbacks`,
   `SelectContext`.
4. Block by block, every large actor through the constructor with `clearClass3 = 0`, then
   `FFX_Atel_BindActorClass3Slice 0x870870(id, slice)`, which sets `+0xB8 = slice` and clears `def[+0x10]` bytes there, and
   the slice advances by `FFX_Atel_GetActorClass3SliceBytes 0x86C160`, the size rounded up to 16.
5. Block by block, every small actor. Their `+0xB8` is never written, so a class 3 access from a small
   actor in a battle context reads an uninitialised pointer. No shipped monster block has a small actor.
6. `FFX_BtlEv_BindWorkersToUnits 0x7976A0`, `FFX_Atel_CallContextBootHook` (the reset's
   `FFX_Atel_BootAllActors`), `FFX_Atel_SelectContext(0)`. [V]

**The overrun.** Step 4 numbers the actors block by block, but `GetActor` cuts the pool by segment over
the summed counts. With blocks `[type 4]` and `[type 1]`, the sums give one character slot and one 1368
slot, id 0 (the type 4) gets the 2904 slot and id 1 (the character) the 1368 one, and its constructor
writes 2904 bytes from there, past the pool. The monster census makes this moot for context 3: 361
blocks, 898 actors, every one type 2 in the 1464 segment (`(0, 0, n, 0, 0)` segment shapes only). [D]
Contexts 2 and 4 were not censused (section 10).

## 7. The context reset, `FFX_Atel_ResetContext 0x86EE10`

`g_ffxAtelCtxArray 0x1325BA0`, 568 bytes each, seven contexts. Every write, by offset:

| off | value | notes |
|---|---|---|
| `+0x000` | word `0x9880` | byte 0 = 0x80, the warp gate. `InstallContextCallbacks` ORs 0x01 (active) |
| `+0x002` | `(old & 0x10) \| 0xC8` | bit 0x10 survives |
| `+0x003` | 0x30 | |
| `+0x004` | dword `0x00020004` | |
| `+0x008` | byte 0 | |
| `+0x00A` | `0xFFFF` | `FFX_Atel_GetPlayerActorId 0x86C1A0` |
| `+0x00C` | `h[+0x36]` | 0 without a block |
| `+0x010` | `h[+0x34]` | |
| `+0x012` | `h[+0x36]` again | 0 without a block |
| `+0x014`, `+0x016`, `+0x018` | `h[+0x14]`, `h[+0x16]`, `h[+0x1C]` | |
| `+0x01A` | the context index | the actor cache key |
| `+0x01C` | the pool | |
| `+0x020` | 0 | `InstallContextCallbacks` puts `FFX_Atel_GetActor` here |
| `+0x024` | the size argument | unrounded for the field, -1 for the others |
| `+0x028`, `+0x02C`, `+0x030`, `+0x034` | 0 | `+0x2C` becomes `&saveData[+0x1EC]` on install, `+0x34` the first message table |
| `+0x044` | 0 | `FFX_Atel_SetCtxField44 0x8715E0` writes it |
| `+0x048` | the block | |
| `+0x04C` | 0 | install puts `FFX_Atel_ContextStepCallback` here if the setup did not |
| `+0x050` | `FFX_Atel_BootAllActors` | |
| `+0x054` | 0 | the class 3 resolver |
| `+0x058..+0x060` | 0 | install: `FFX_Atel_PushActorPosToChr 0x866800`, `FFX_Atel_StepActor 0x8666E0`, `FFX_Atel_HookReturnTrue 0x86C4C0` (returns 1) |
| `+0x064 + 16i`, i < 4 | dword -1 | the `brk` records, only the first dword |
| `+0x0A4 + 8i`, i < 32 | word 0 | only the first word |
| `+0x1A4 + 8i`, i < 8 | dword 0 | only the first dword |
| `+0x1E4` | byte 0 | the examine scan armed flag (`FFX_Atel_ArmExamineScan 0x8655B0`) |
| `+0x1E7` | 0xFF | |
| `+0x1E8` | `0xFFFF` | |
| `+0x1EA` | 0 | |
| `+0x1EC`, `+0x1F0` | 0.0 | |
| `+0x1F4..+0x204` | 0 | `+0x1F4` is also cleared by `FFX_Atel_DeactivateContext 0x86ED40` |
| `+0x208` | 0.0 | |
| `+0x20C` | 0 | |
| `+0x210` | `(old & ~0x7F) \| 0x49` | `FFX_Map_ArmWarpGate 0x8729C0` ORs 0x02 |
| `+0x214` | -1 | |
| `+0x218..+0x220` | 0.0 | |
| `+0x224` | 1.0 | then 0.0, 0.0, 0.0 |
| `+0x234` | 1.0 | |

Then `g_ffxAtelActorCachePtr = 0`. Left alone and so carried across a reset: `+0x00E`, `+0x038`, `+0x03C`,
`+0x040`, `+0x1E5`, `+0x1E6` and the unwritten parts of the three record arrays. [V]

`FFX_Atel_InstallContextCallbacks 0x871190`: byte 0 |= 1, `g_ffxAtelActorClampCount` cleared when
`+0x1C` (the pool) is 0, and each of `+0x20`, `+0x2C`, `+0x4C`, `+0x58`, `+0x5C`, `+0x60` filled only when
0. [V]

## 8. What lives in the image, and what that means for a port

`FFX_Atel_ResolveVarAddress` resolves classes 2, 3, 4 and 6 into the package, so script writes land in
the loaded image. Measured over the 397 packages [D]:

- class 2 and class 4 bases are the same for every actor of a package,
- class 3 regions (definition `+0x2C`, `+0x10` bytes) are per large actor, all inside the block, and
  never overlap. No small actor has one,
- every variable base sits after the code section, and the span from the lowest base to the first
  entry table is at most 157,832 bytes (bltz0002), to the end of the block at most 781,504 bytes.

The package is freed and re-read on every map change (`maybe_FFX_Map_Unload 0x872D20` frees
`g_ffxEvPackageBase 0x23CBD70` and `g_ffxAtelActorPool 0x23CBD78`), so all of those variables reset per
map. The recreation keeps the image immutable (PORTING_RULES A1) and gives the interpreter a mutable
copy of the variable regions in the arena (K14). Sizing that copy is open (section 10).

## 9. Proposed IDB names

| VA | current | proposed | why |
|---|---|---|---|
| 0x86CB00 | `FFX_Atel_ConstructActor` | `FFX_Atel_ConstructActor` | the one actor constructor, all three setups call it |
| 0x86D300 | `FFX_Atel_InitActorThreadRecords` | `FFX_Atel_InitActorThreadRecords` | writes context, block and local index into every channel's thread record, and type 1's MOVE and ROT pointers |
| 0x86D510 | `FFX_Atel_InitActorListNodes` | `FFX_Atel_InitActorListNodes` | links the per-actor list nodes into the list at `+0x88` |
| 0x86D670 | `FFX_Atel_SetActorDevResources` | `FFX_Atel_SetActorDevResources` | copies the dev harness table into `+0x0C..+0x2B`, null table in retail |
| 0x8759B0 | `FFX_Atel_SetupStaticContext` | `FFX_Atel_SetupStaticContext` | the single block setup for context 6 |
| 0x8BE3A0 | `FFX_Menu_SetupAtelContext` | `FFX_Menu_SetupAtelContext` | its caller, context 6 over the static menu block |
| 0x870870 | `FFX_Atel_BindActorClass3Slice` | `FFX_Atel_BindActorClass3Slice` | `+0xB8 = slice`, cleared |
| 0x86C160 | `FFX_Atel_GetActorClass3SliceBytes` | `FFX_Atel_GetActorClass3SliceBytes` | definition `+0x10` rounded to 16 |
| 0x86C0C0 | `FFX_Atel_CalcBlockClass3Bytes` | `FFX_Atel_CalcBlockClass3Bytes` | the sum of the above over a block's large actors |
| 0x8756B0 | `FFX_Atel_ResolveClass3Private` | `FFX_Atel_ResolveClass3Private` | the context `+0x54` hook |
| 0x86BCB0 | `FFX_Atel_GetCount1464` | `FFX_Atel_GetCount1464` | header `+0x14` |
| 0x86A9E0 | `FFX_Atel_GetCount1368` | `FFX_Atel_GetCount1368` | header `+0x16` |
| 0x86B020 | `FFX_Atel_GetCount744` | `FFX_Atel_GetCount744` | header `+0x1C` |
| 0x86C510 | `FFX_Atel_GetChrActorCount` | `FFX_Atel_GetChrActorCount` | `ctx+0x0C - +0x18 - +0x16 - +0x14` |
| 0x86B5D0 | `FFX_Atel_GetSystemActorId` | `FFX_Atel_GetSystemActorId` | context 0's block `+0x18` |
| 0x86A5D0 | `FFX_Atel_GetActorIdField` | `FFX_Atel_GetActorIdField` | `actor + 0x2E` |
| 0x86A420 | `FFX_Atel_GetActorContextIndex` | `FFX_Atel_GetActorContextIndex` | `actor[+0xAE] & 0xF` |
| 0x67AAE0 | `FFX_Ev_GetBootPackageSize` | `FFX_Ev_GetBootPackageSize` | the boot scene size, cached in `dword_C33484` |
| 0x13270DC | `g_ffxAtelActorCacheCtx` | `g_ffxAtelActorCacheCtx` | it holds the context index, not a generation |
| 0x13270D8 | `g_ffxAtelActorClampCount` | keep, add a comment | it is also cleared by every stack error path, so it reads as an error counter |

## 10. Open

- The readers of header `+0x00`, `+0x1A`, `+0x1E`, `+0x24`, `+0x28`, definition `+0x01..+0x07` and
  `+0x0C`. A breakpoint on reads of the block header during a map load would settle them.
- The extents of the class 2, 4 and 6 regions. Nothing in the header sizes them. 11.2 either copies the
  span from the lowest base to the first entry table (157,832 bytes at most) or finds the sizes.
- Whether contexts 2 and 4 ever get a block list that mixes segments (section 6.3). Their blocks are
  `dword_112A994[0]` and `[8]` for context 2 and `dword_1135DF8` for context 4.
- How the asset loader's slot 0 reads a boot scene, whose kind 12 row is empty. Only `_ECalm` reaches it.
- What `sub_8653F0 0x8653F0`, the last call of the constructor, does on a fresh actor (it walks the
  `+0x80` list, which is empty then).
- Whether anything reads `g_ffxAtelActorClampCount` to change behaviour.
