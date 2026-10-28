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
| 6 | `FFX_Atel_CalcActorPoolSize(atel)` 0x86A290 | then `FFX_Tk_AllocSectorRounded 0x88E650` rounds up to 2048. The pool is not cleared [V] |
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
- `FFX_Atel_ScheduleActor(id, 10)`. [V]

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
- What `FFX_Atel_ScheduleActor 0x8653F0`, the last call of the constructor, does on a fresh actor (it walks the
  `+0x80` list, which is empty then).
- Whether anything reads `g_ffxAtelActorClampCount` to change behaviour.

## 11. The interpreter (2026-10-13, PROGRESS 11.2)

Traced read only in IDA (nothing renamed, nothing saved) and ported to the recreation as
`src/engine/atel/Interpreter.h/.cpp`, `Syscall.h` and `AtelHash.h/.cpp`, tested by
`tests/engine/AtelInterpreterTest.cpp` (hand assembled blocks) and `tests/engine/AtelTitleTest.cpp`
(event 23 from the install). Offsets are the original's. [V] unless marked.

### 11.1 Where it runs

`FFX_Atel_StepOnce 0x88D3D0` runs the map change block first, then `FFX_Atel_StepFieldAll 0x872BD0`,
which reaches `FFX_Atel_StepFieldContexts 0x8666D0` -> `FFX_Atel_StepContextRange 0x8688F0 (0, 6)`.
The range is half open, so contexts 0..5. Context 6 (the menu) is stepped elsewhere.

`StepContextRange`:

- calls `FFX_Atel__samplePadsBothPorts 0x871D70` first,
- for each context whose `+0x08` is 2 or less, sets `g_ffxAtelCtx 0x1326B28` and
  `g_ffxAtelSteppingContextIndex 0x1326B30`. If `flags0 & 1` it calls the `+0x4C` callback, then bumps
  `+0x1F4` when `flags0 & 8`. A `+0x08` of 1 or 2 becomes 3 afterwards,
- restores the selected context and calls `FFX_Atel_MesWinPullStateAndGatePlayer 0x871F30`.

The field's callback is `FFX_Atel_ContextStepCallback3x 0x867740`. With `flags0 & 8` it runs
`StepFrame(1, 0)`, `StepFrame(0, 0)` and `StepFrame(0, 1)`, otherwise `InitFrame(1)`. Then the boot task
(`FFX_BootTask_QueueBusy 0x875000`, `FFX_BootTask_RunHead 0x8750B0`). The default callback
`FFX_Atel_ContextStepCallback 0x867710` is `StepFrame(1, 1)` or `InitFrame(0)`.

**InitFrame `0x8677A0`.** Runs every large actor whose `+0x34` bit `0x10` (ended once) is still clear,
once, in id order. When every actor has ended once it sets `flags0 & 8`, schedules every actor, calls the
event effect init (`op_event_effect_init 0x7FCC20` and `0x872B70`) and ORs `0x10` into `+0x210`.
Otherwise it bumps `+0x20C`. So a context boots over as many frames as its slowest entry 0 needs.

**StepFrame `0x867950`.** A first pass clears `+0x1E4`, remaps the pads (contexts 0 and 1 only), arms
the examine scan on a `0x20` or `0x80` press, writes `+0x1E6` = 1 when the press was not `0x20`, then
runs every large actor with hooks. A later pass runs only the actors whose `+0x36` bits 7..8 are set,
without hooks. After the run it fires the examine event (`FFX_Atel_GrantTalkBonusOnce 0x865650`, then
`FFX_Atel_FireActorEvent 0x8764F0` with `+0x1E6` as the event kind), then schedules each actor and moves
the pass counter bits 9..10 into 7..8. The last pass clears `+0x36` bit `0x1000`. The whole thing loops
while the OR of the RunScript results, with `0x8000` removed, has a bit outside `0xC000` set
(`while ((result & 0xFFFF3FFF) != 0)`). A `0x8000` from any actor sets `+0x08` to 3, the debugger stop.

### 11.2 The thread state of a large actor

| offset | what |
|---|---|
| `+0x2C` u16 | the entry the current request started |
| `+0x32` u8 | the level, which picks the channel |
| `+0x34` u16 | `0x02` alive, `0x04` runs in battle, `0x08` schedulable (written at every RunScript exit), `0x10` ended once, `0x80` steps motion, bits 10..12 the run state (1 running, 2 waiting, 3 ended, 4 yield, 5 runaway) |
| `+0x36` u16 | `0x20` a library 6 call ran, `0x40` runs in battle, bits 7..8 and 9..10 the extra pass counters, `0x1000` grants them to started threads, `0x8000` runs in a gated context |
| `+0x44` ptr | the event entry table, opcode `0x79` |
| `+0x48` | 4 s32 then 10 f32 registers, class 5 |
| `+0x80`, `+0x88`, `+0x90` | running list head, free list head, current request |
| `+0x94`, `+0x98` | speed (always 1.0, only the constructor writes it) and its carry |
| `+0xAB` u8 | the event mask `FireActorEvent` tests |
| `+0xC4` | s32 sp, then 19 u32 values at `+0xC8`, 4 unwritten bytes at `+0x114`, 19 u8 tags at `+0x118` (1 int, 2 float) |
| `+0x12C` | 9 channels of 76 bytes (2 for types 5 and 6) |
| `+0x3D8` | 16 request nodes of 24 bytes (8 at `+0x1C4` for types 5 and 6) |

A channel: `+0x00` three u32 return offsets (code relative), `+0x10` three u16 frame actors, `+0x18` the
PC pointer, `+0x1D` s8 depth, `+0x1E` syscall phase, `+0x1F` wait state, `+0x20` cond, `+0x24` selector,
`+0x28` float R, `+0x2C` 16 bytes of syscall work (the wait words alias it, `+0x2C` entry, `+0x2E`
actor, `+0x30` level), `+0x3C` local index, `+0x3E` block, `+0x40` context.

A request node: `+0x00` next, `+0x04` prev (a node or the head slot), `+0x08` entry, `+0x0A` caller
(`0xFFFF` none), `+0x0C` target, `+0x0E` control (level in the low nibble, state in bits 4..6, 1 queued
and 2 started, `0x80` created since the last schedule), `+0x0F` kind (0 plain, 1 the caller waits for the
start, 2 for the end, 3 a notification), `+0x10` local, `+0x12` block, `+0x14` context.

The channel is `FFX_Atel_GetActorThreadChannel 0x872650`: the level clamped to `[0, 8]`, or `[0, 1]`
for types 5 and 6.

### 11.3 `FFX_Atel_RunScript 0x8641E0`

The whole function holds a mutex (`0x6F2DE0` create, `0x6F2ED0` lock, `0x6F2F50` unlock, the handle in
`g_ffxAtelRunScriptMutex`). Not ported, the recreation steps ATEL on one thread.

1. **Gate.** Return 0 when `!(+0x34 & 2)`, or `!(+0x34 & 4) && !(+0x36 & 0x40)` with a battle pending
   or active, or context `+0x03 & 3` is 1, or it is 2 and `+0x36` bit 15 is clear.
2. The channel of the current level is fixed for the whole call.
3. **Speed carry.** `scaled = speed * g_ffxAtelScriptSpeedScale`, `sum = scaled + carry`, the continue
   flag starts as `scaled >= 1` and if set `sum -= 1`, `carry = sum`, and bit 0 of the result is
   `sum >= 1`. The scale is 1.0 (its setter `0x871020` has no caller), so this is a constant.
4. **Waits.** Wait 1 looks for a notification on the actor's own running list, the **last** kind 3 node
   whose caller, entry and level match the wait words (`0x8635C0`). Found, it is freed and the wait
   cleared, otherwise the call exits. Wait 2 exits while the waited actor's running list has a non kind 3
   node with that entry. A blocked exit writes `+0x34` bit 3 as `sp == 0`.
5. **The loop.** Run state 1. Every fetch counts, and past `0x10000` the continue flag drops and the run
   state becomes 5. In mode 0 the loop leaves when the continue flag is clear **and the stack is empty**.
   So an end with a value left on the stack keeps fetching the same end opcode, and the original hangs
   there. The port stops at `0x20000` with a `Hang` fault. In mode 1 (single step) it leaves on the first
   clear continue flag, and a nonempty stack clears bit 3.
6. **Advance** is `pc += ((b | 0x40) >> 6)`, which is 1 or 3, and the phase is cleared. The jumps, calls,
   returns, ends, yield, brk, a blocking syscall and the default case do not advance.
7. **Exit.** `+0x34` bit 3 is the last instruction's "reschedulable", which a poll result with bit 2
   clears. Then the `+0x5C` StepActor hook on the channel of the new level.

The result: bit 0 the carry, `0x8000` a brk stopped the context, `0x4000` single step was still going.

### 11.4 Opcode facts the table in EBP_FORMAT.md gets wrong or leaves out

- `x` is pushed first, `y` is on top. `08`, `09`, `0C` and `0D` compare **unsigned**, `0A`, `0B`, `0E`
  and `0F` signed. `08` and `0A` are `x > y`, `0C` and `0E` are `x >= y`, `09` and `0B` are
  `!(x >= y)`, `0D` and `0F` are `!(x > y)`. On floats the comparisons are ordered, so a NaN makes `09`,
  `0B`, `0D` and `0F` true. No shipped script uses the unsigned four. [V] [D]
- `06`, `07` and `14..17` use the float form unless **both** top tags are int.
  `FFX_Atel_TopTwoAreInts 0x86D9E0` reads `tags[sp-1]` and `tags[sp-2]` whatever sp is, so with fewer
  than two values it reads the 4 bytes at `+0x114`, which nothing writes.
- `17` and `18` use `idiv`, so a zero divisor (or `INT_MIN / -1`) traps in the original. The port pushes
  0 and raises `DivideByZero`. `13` is `sar`. `1A` negates through x87, so a NaN keeps its sign.
- A pop below an empty stack reads 0. The twentieth push is dropped. A float popped as an int truncates
  (`0x80000000` for NaN or out of range).
- `1F` and `22` read by the descriptor's type, type 6 pushes a float and the rest an int. A type 4 (u32)
  read as a float goes through double, so a negative s32 reads as itself plus 2^32. Stores pop the value
  first and then, for the indexed forms, the index, which is clamped to `[0, count - 1]`. The saturating
  forms clamp to the type's range. `27` pushes the descriptor plus `index * elementSize`, the index
  clamped the same way.
- `2B` copies the top **as an int**.
- `30`, `31`, `32`, `55`, `56`, `57`: a jump inside a call frame uses the label table of the frame's
  actor (`FFX_Atel_JumpToLabel 0x8726F0`), not the running actor's.
- `33` at depth 3 does nothing and advances. `34` at depth 0 does nothing and advances.
- `35` leaves the result on the stack, `58` pops it into R. See 11.7.
- `36..38` and `45..53` pop entry, target and level. Group 0 also reports to
  `FFX_SyncData_SelectTimingTrack 0x67A9B0`. Call the group base (`45`, `4A`, `4F`) variant 0. Variants
  0 and 3 use `FFX_Atel_StartThread 0x86E990`, variants 1 and 4 `FFX_Atel_StartThreadDeferred 0x86EC40`,
  which is `FFX_Atel_StartThreadIfEntryNotRunning 0x86EC80` with the arguments packed, and the plain
  form (`36`, `37`, `38`) and variant 2 use `FFX_Atel_StartThreadIfChannelFree 0x86EBE0` (refuses when a
  non notification node holds that level). Variants 2, 3 and 4 also ask the `+0x60` hook, which is
  `FFX_Atel_HookReturnTrue 0x86C4C0`. An entry of 0 starts nothing. Groups 1 and 2 set wait 1 when the
  start succeeded, and stop the script for the frame either way.
- `3C` and `3E` end with no level (`FFX_Atel_EndScript 0x8691D0` with -1), `3D` and `3F` pop a
  level and kill the requests above it. `3E` and `3F` then point the channel of the new level at entry 1,
  overriding whatever the schedule just started there. None of them advance, so an actor with nothing
  left to run executes its end again every frame.
- `40` brk: a record at `ctx+0x64` on this PC either runs the byte it replaced (when it stopped once
  already, or in single step), or stops: result `0x8000`, the record's `+0x0F` set, single step for the
  rest of the call. With no record it stalls the actor forever, no advance and the run state cleared.
- `54` yield is `EndScript(level 0)`. It frees the current request and every request above level 0 that
  was **not created this frame**, then schedules. No advance.
- `75` pushes `g_ffxAtelRegisterDescriptors[operand]`. Entries 0..13 are class 5 descriptors of the 14 registers, 14 is
  -1, and index 15 is `g_ffxAtelRunScriptMutex`, the RunScript mutex handle, a heap value at run time. No shipped
  script uses `75`. The port raises `UnstableSystemValue` for 15 and up.
- `77` waits (wait 2) only when the target is running that entry. `78` does the same through the party.
- `79` (`0x870D30`) pops value, index and a reference. A fresh table gets the type's defaults
  (`0x876390`) first, then `table[min(index, 7)] = value`. A negative index is not clamped.
- `7A` (`0x86FAB0`) only does anything for types 5 and 6, with the table at `actor+0x2E4` that only
  `0x86D190` sets. It pushes the result.
- `1B` and `41..44` have no case. The default leaves the continue flag set and does not advance, so it
  spins to the runaway count.

### 11.5 The scheduler

- `FFX_Atel_CreateThread 0x86EA00` takes a free node (the free list starts as `[last .. 0]`), writes
  entry, caller, target, kind, the level (clamped to 1 for types 5 and 6) and state 1 with the new bit,
  and inserts it before the first node of a lower level, so the running list is sorted by level
  descending and FIFO within a level. A notification (kind 3) is appended at the tail. In context 0 a
  caller whose `+0x36` has `0x1000` gives the target `0x200`.
- `0x8653F0` (schedule) takes a threshold. Below 0 means the current level, above 9 means "any". If the
  head is not a notification and its level is above the threshold, a queued head is started
  (`0x8690A0`: entry, level, the channel PC on its entry, depth and phase 0) and its kind 1 caller is told
  (`0x871650`), otherwise the level is just set to the head's. The head becomes current, and the new bit
  is cleared on every node.
- `FFX_Atel_EndScript 0x8691D0`: a kind 2 current request notifies its caller, the current request
  is freed, a level of 0 or more kills the requests above it (`0x872860`, mode 2 keeps the new ones, and
  a queued kind 1 or any kind 2 tells its caller), then schedule with threshold 10, then the optional
  entry 1 reset, then `+0x34 |= 0x10`.
- `FFX_Atel_FireActorEvent 0x8764F0` always works on context 0. The event's channel comes from
  `FFX_Atel_GetEventChannel 0x876860` by type, the entry from the table `79` set or the type's default,
  and it starts only when the event's bit is in `+0xAB`.

### 11.6 The variable window census [D]

Over all 397 packages with `../tools/ebp.py`: the lowest nonzero variable base (header `+0x20`, every
definition's `+0x28`, `+0x2C`, `+0x30`) equals the class 4 base and equals the end of the code in every
package, and no class 3 region ends past the block. The largest window (block size minus code end) is
781,504 bytes (bltz0002), the sum over all packages 10,878,800. Class 3 regions interleave with the entry
and label tables. So the recreation copies `[lowest base, block end)` per block and reads everything
below it from the image. A write below that start would be the original writing into its own code, and
is a fault in the port.

Descriptor classes in shipped data: class 0 3,281, class 3 8,921, class 4 1,023, class 6 5,815. No class
1, 2, 5 or 7 descriptors. The variable table and both pools are shared by all actors in every package.

### 11.7 Syscalls

An operand's bits 12..15 pick one of the 16 libraries in `g_ffxAtelSysFuncLibs 0x1328558`, bits 0..11
the entry (16 bytes: start, poll, float result, int result). `FFX_Atel_SysFuncStart 0x8777A0` runs once
when the phase is 0 (and sets it to 1). A library 6 call there sets `+0x36 |= 0x20` when `0x10` is
clear. `FFX_Atel_SysFuncPoll 0x877730` runs every pass while the phase is 1: bit 1 done, bit 2 clears
the reschedulable bit at exit, bit 4 keeps the script running this frame. A null slot polls 5.
`FFX_Atel_SysFuncResult 0x8777F0` pushes the float getter's value if there is one, else the int
getter's, else int 0. Libraries 2, 3, 10, 14 and 15 are registered by
`FFX_Atel_RegisterSysFuncLib 0x877880` with `g_ffxAtelSysFuncLibEmpty 0xC52B60`, eight entries of a
no-op start `0x877710` and a poll `0x877720` that returns 0, so a call there **never finishes**. No
shipped script calls them. Shipped scripts use libraries 0, 1, 4, 5, 6, 7, 8, 11 and 12. [V] [D]

### 11.8 Mode and the debugger

Mode is context `+0x08`, 1 is single step. A brk stop sets it to 3, and a context at 3 is skipped by
`StepContextRange`. Nothing found in retail sets 1 or 2.

### 11.9 No RNG

The call graph from `RunScript`, `StepFrame`, `InitFrame`, both context step callbacks, the StepActor
and PushActorPos hooks and the scheduler reaches no random number function (checked against
`FFX_Rand_Stream`, `FFX_Btl_Rand`, `rand` and the CRT copies) except through the syscall dispatch. The
draws a script causes are its syscalls' (PROGRESS 11.3).

### 11.10 What the original never initialises, and open points

The actor pool is not cleared (`FFX_Tk_AllocSectorRounded 0x88E650`), and the constructor does not write a
channel's cond, selector and syscall work, the stack slots above sp, the call frames above the depth,
the 4 bytes at `+0x114`, the words of a notification node, or the top 3 bits of `+0x38`. `+0xAB` is
written for types 1 to 6 only. The hash leaves all of them out. The port zeroes them, which can change
behaviour only for a compare or arithmetic opcode on an empty or one deep stack (the `+0x114` read).

Open:

- **The multi-block request words.** In the multi-block form `CreateThread` takes the node's local,
  block and context from its arguments, and none of the opcode paths fill those in (stack garbage at the
  call). The port uses the target's channel 0 words, which is what the single block form reads. Only the
  battle contexts are affected.
- The private class 3 storage of a multi-block context in the capture recipe (11.12) needs its base,
  presumably the lowest `+0xB8` of the large actors. Not checked in IDA. The rest of the recipe's globals and the `GetActor` arithmetic were rechecked
  read only on a scratch copy of the IDB on 2026-10-13.
- `0x872B70` clears `dword_1327138` and `dword_132713C`. Their readers are not traced.
- Whether anything outside the debugger ever sets `+0x08` to 1 or 2.

### 11.11 The static walk of event 23

A throwaway Python walk over `../tools/ebp.py`'s disassembly of te/test20/test20.ebp, from entry 0 of
each large actor, following `jmp`, `callactor` and `ret`, and stopping at the first conditional jump,
end, wait, blocking runscript, brk or revisited PC. It records the syscall ids met:

| actor | type | syscalls | stops at |
|---|---|---|---|
| 0 | 1 | `4:32`, `0:66`, `0:67` | `3C` |
| 1 | 1 | none | `3C` |
| 2 | 4 | `0:577`, `12:7`, `12:24`, `0:137` | `56` |
| 3 | 4 | none | `3C` |
| 4 | 4 | `1:0` | `3C` |

8 in all. The digest is FNV-1a 64 over, per actor, u16 id, u16 count, then each u16 id, little endian:
`0xfbfe444170e3812b`. `AtelTitleTest` builds the same summary from the interpreter's trace of the first
step and compares both numbers. Nothing from the package is checked in.

### 11.12 Capture recipe for the per-step ATEL hash

For the co-op probe plugin (`loader/plugins/probe`) to add later, not implemented here. RVA = VA - 0x400000.

**Where.** Detour the return of `FFX_Atel_StepOnce` (VA 0x88D3D0, RVA 0x48D3D0) and hash after it
returns, once per simulation step, on the step thread. Use the probe's own step counter, the one it
already prints with the save block hash.

**Globals.**

| what | VA | RVA |
|---|---|---|
| `g_ffxAtelCtxArray`, 7 contexts of 568 bytes | 0x1325BA0 | 0xF25BA0 |
| `g_ffxAtelSysFuncLibs` (not hashed, for checking a null slot) | 0x1328558 | 0xF28558 |
| `g_ffxEvPackageBase` | 0x23CBD70 | 0x1FCBD70 |
| `g_ffxAtelActorPool` | 0x23CBD78 | 0x1FCBD78 |
| `g_ffxAtelScriptWork` (class 0, save block `+0x1EC`) | 0x112CC7C | 0xD2CC7C |

Context `c` is at `0x1325BA0 + 568 * c`. Its pool is `+0x1C`, its block (single block form) `+0x48`, the
large count `+0x0C`, the total `+0x10`, the segment counts `+0x14`, `+0x16`, `+0x18`, the block list
`+0x1F8` (multi block). Actor `id` is at the pool plus the `FFX_Atel_GetActor 0x86A830` arithmetic of
section 4: the 2904 byte segment of `+0x0C - +0x18 - +0x16 - +0x14` actors, then 1368 x `+0x16`, 1464 x
`+0x14`, 744 x `+0x18`, then the 48 byte small slots. Call nothing, read memory only. A context counts as
installed when `flags0 & 1` is set and `+0x1C` is non-null.

**The three streams**, each FNV-1a 32 from the offset basis, folded low byte first into the combined
value exactly as `zan::diag::HashSaveBlock` folds its regions. All values little endian, in this order.
`zan::atel::AtelRegionStream` in `src/engine/atel/AtelHash.cpp` is the reference.

1. `atelContexts`, for c in 0..6: u8 c, u8 `flags0 & 1`. If active: u8 `+0x00`, `+0x01`, `+0x02`,
   `+0x03`, u8 `+0x08`, u16 `+0x0A`, u8 `+0x1E4`, u8 `+0x1E6`, u16 `+0x1E8`, u32 `+0x1F4`, u32 `+0x20C`,
   u32 `+0x210`.
2. `atelThreads`, for each installed context: u8 c, u16 large count, then per large actor:
   - u16 id, u8 block index (0 for a single block context, else the index of actor `+0x04` in the
     `+0x1F8` list), u32 definition offset (actor `+0x00` minus actor `+0x04`),
   - u16 `+0x2C`, u8 `+0x32`, u16 `+0x34`, u16 `+0x36`, u32 bits of `+0x94`, u32 bits of `+0x98`, the
     56 bytes at `+0x48`, u8 `+0xAB`,
   - the event table in 8 bytes, u8 region, u8 block, u16 actor, u32 offset. Region 0 is null (all zero),
     1 inside the script work (offset from 0x112CC7C), 2 the battle work, 3 inside a block (the block
     index, offset from its base), 4 the private class 3 storage, 5 an actor's registers (the actor id,
     offset from its `+0x48`). Block is written only for 3 and actor only for 5, zero otherwise,
   - u8 sp (clamped to 0..19), the sp values as u32, then the sp tags as u8,
   - u8 n, the running list length (follow `+0x80` and each node's `+0x00`, at most 16), then per node
     u16 entry, u16 caller, u16 target, u8 control, u8 kind, and when the kind is not 3 also u16 local,
     u16 block, u16 context. Then u8 the position of `+0x90` in that list, `0xFF` when null or absent,
   - per channel (9, or 2 for types 5 and 6): u8 PC block, u32 PC minus that block's base, u8 depth,
     then per frame below the depth u32 return and u16 frame actor, u8 phase, u8 wait, then for wait 1
     or 2 the u16 entry and actor words, and for wait 1 also the u16 level word, u32 bits of R, u16
     local, u16 block, u16 context.
3. `atelVariables`, for each installed context and block: u8 c, u8 block, u32 window start, u32 length,
   then the bytes of the block from the window start to its end. The window start is the lowest nonzero
   of header `+0x20` and every definition's `+0x28`, `+0x2C`, `+0x30`, or the block size when all are
   zero (`zan::atel::VariableWindowStart`). A multi-block context then adds u8 c, u8 `0xFF`, u32 0, u32
   length and its private class 3 storage (open, 11.10).

The probe prints one line per step in the recreation's format,
`atel: step=N combined=0x... atelContexts=0x... atelThreads=0x... atelVariables=0x...`
(`zan::atel::FormatAtelLine`), so the two logs diff directly.

**Before trusting a match.** The recreation's title test reaches step 120 with `combined=0xE800F4BE`
under a stub syscall layer, which the original will not match, because its real syscalls move state.
The first useful comparison is the contexts region over the boot frames of event 23, then the threads
region once 11.3 lands the libraries the title calls (35 distinct ids in 120 steps). A line that matches
from the first step on an all zero state proves nothing, so check that the stream had `flags0 & 1` set.

### 11.13 IDB names

Applied and saved 2026-10-26 through idalib after checks of the scheduler (0x8653F0), FindNotification
returning the last match (0x8635C0) and no RNG under `FFX_Atel_RunScript 0x8641E0`. 0x872650 was
`FFX_Atel_GetActorMotionChannel` in the IDB and is now `FFX_Atel_GetActorThreadChannel`, since the record
is the per level script thread that also carries the MOVE and ROT pointers. The two role only rows stay
unnamed.

| VA | proposed | why |
|---|---|---|
| 0x8653F0 | `FFX_Atel_ScheduleActor` | picks the head request by a level threshold, starts it or rebinds |
| 0x8690A0 | `FFX_Atel_StartRequestOnChannel` | entry, level and the channel PC from a node |
| 0x871650 | `FFX_Atel_NotifyCallerStarted` | a kind 1 node posts a kind 3 to its caller |
| 0x862C60 | `FFX_Atel_RebindChannelBlock` | rebinds only when the channel's context has a block list |
| 0x862CC0 | `FFX_Atel_BindActorToBlock` | definition, block base and extra of a block's local actor |
| 0x8639E0 | `FFX_Atel_ResetChannel` | depth, PC on entry 0, phase, wait, R |
| 0x863750 | `FFX_Atel_ResetChannelMotionCmds` | the MOVE and ROT reset of a type 1 channel |
| 0x872860 | `FFX_Atel_KillRequestsAbove` | frees the nodes above a level, by mode |
| 0x8635C0 | `FFX_Atel_FindNotification` | the last matching kind 3 node |
| 0x8654C0 | `FFX_Atel_FreeRequest` | unlink and push on the free list |
| 0x863570 | `FFX_Atel_IsEntryRunning` | any non kind 3 node with that entry |
| 0x86DC50 | `FFX_Atel_ListAppend` | |
| 0x86DC10 | `FFX_Atel_ListInsertAfter` | |
| 0x86DC30 | `FFX_Atel_ListInsertBefore` | |
| 0x870D30 | `FFX_Atel_Op_SetEventEntry` | opcode `79` |
| 0x86FAB0 | `FFX_Atel_Op_BindTriggerLink` | opcode `7A` |
| 0x869DA0 | `FFX_Atel_DecodeAt` | the brk path's opcode and operand read |
| 0x8641C0 | `FFX_Atel_RunScriptById` | |
| 0x86C3F0 | `FFX_Atel_GetActorLevel` | `+0x32` |
| 0x876390 | `FFX_Atel_GetDefaultEventTable` | the type's eight default entries |
| 0x6F2DE0, 0x6F2ED0, 0x6F2F50 | `FFX_Mutex_Create`, `FFX_Mutex_Lock`, `FFX_Mutex_Unlock` | the RunScript lock |
| 0x8691D0 | `FFX_Atel_EndScript` | drop the `maybe_` |
| 0x872650 | `FFX_Atel_GetActorThreadChannel` | already named, confirmed |
| 0xC52704 | `g_ffxAtelRegisterDescriptors` | opcode `75`'s table |
| 0xC52740 | `g_ffxAtelRunScriptMutex` | |
| 0x877710, 0x877720 | `FFX_AtelSys_EmptyStart`, `FFX_AtelSys_EmptyPollBlocks` | the empty library entries |
| 0x872B70 | (role) clears `dword_1327138` and `dword_132713C` after a boot | |
| 0x86D190 | (role) the setter of `actor+0x2E4` that `7A` reads | |

## 12. Actor types 5 and 6 (2026-10-26, PROGRESS 1.8.12)

Read only through idalib on a scratch copy of the IDB (nothing renamed, nothing saved) and censused over
the 397 packages with `../tools/ebp.py`. Marks as in the rest of the file. Handler names below are the
positional `FFX_AtelSys_Core_NNN` ones the IDB already carries, the meanings are new.

### 12.1 Short version

- **Type 5 is a crossing line trigger, type 6 an enter and leave volume.** They are the light forms of
  type 2 (line trigger) and type 3 (box trigger). Same shape fields, set by the same syscalls, but a 744
  byte slot with 2 thread channels and 8 request nodes, no examine or proximity events, and a target actor
  of their own in place of the bound player. [V]
- Type 5 fires only event 4 (the target's step crossed the line) and records which side it came from.
  Type 6 fires only events 5 (enter) and 6 (leave). [V]
- In shipped data type 5 is mostly the field's **camera switch line**: 936 of 1,382 bind a camera handle
  with core:2, 1,049 read the crossing side with core:139, 981 call the camera library. The rest relay
  the crossing to another actor's script. Type 6 starts event scenes and map effects when the player walks
  into a box (49 take or give back player control, 40 call mapfx, 59 bind a camera). [D]
- No CHR and no motion. The CHR pool is sized from the type 1 segment only (`maybe_FFX_Map_SetupChars`,
  `ctx+0x0C - +0x18 - +0x16 - +0x14` plus the spare count plus 8, at least 36), `FFX_Atel_StepActor`
  runs the MOVE and ROT steppers for type 1 only, and only type 1 channels get MOVE and ROT records
  (`FFX_Atel_InitActorThreadRecords`). No shipped type 5 or 6 calls core:1 (`FFX_AtelOp_SetActorModel`),
  which itself has no type test. [V] [D]

### 12.2 Census [D]

| | type 5 | type 6 |
|---|---|---|
| actors | 1,382 | 116 |
| packages | 188 | 43, 37 of them also hold type 5 |
| most in one package | 45 omeg0000, 40 nagi0000 and dbg_nagi0000 | 29 dome0700 |
| segment | 744 in all 397: `n744` equals the count of types 5 and 6 in every package | same |
| ids | always large, always exactly the 744 id range `[chr + n1368 + n1464, large)` | same |
| entry counts | 3 x1,061, 4 x90, 5 x223, 6 x7, 7 x1 | 4 x105, 5 x1, 6 x10 |
| entry 0 sets the shape | core:84 in 1,349 | core:87 in 115 |
| entry 1 starts with `core:95` (park) | 1,372 | 107 |

Type 6 packages: bika0300, bjyt0000, bjyt0200, bjyt0400, bjyt0600, bjyt1000, bsil0000, bsil0100,
bsmm0000, bsyt0100, bvyt0900, bvyt1300, dbg_nagi0000, djyt0600, djyt0900, dome0700, endg0200, endg0300,
endg0400, kami0000, kino0000, klyt1000, lchb0000, lchb1400, luca0100, luca0600, mcyt0500, mcyt0700,
mihn0000, mihn0600, msmm0000, mtgz0600, mtgz0900, mtgz1100, nagi0000, nagi0600, omeg0000, scene1,
slik1000, swin0000, swin0200, test29, testpub1. Type 5 is in 188 packages over 41 area prefixes, most in
bs (15), mc (13), lc (12), te (11).

Syscalls by actors that call them at least once (code attributed to the actor of the nearest entry
point at or below it):

| feature | type 5 | type 6 |
|---|---|---|
| core:84 set line | 1,363 | - |
| core:122 extend line | 1,369 | - |
| core:87 set box centre | - | 116 |
| core:88 or core:89 box extents | - | 116 |
| core:90 box rotation | - | 17 |
| core:85 half height | 333 | 25 |
| core:86 or core:92 arm or disarm | 399 | 55 |
| core:139 crossing side | 1,049 | 0 |
| core:2 bind a camera handle | 936 | 59 |
| any came:* | 981 | 60 |
| starts another actor's script (`36..38`, `45..53`) | 414 | 52 |
| core:93 or core:94 player control on or off | 103 | 49 |
| mapfx:* | 22 | 40 |
| core:154 set the target actor | 52 | 9 |

The heaviest calls by count and package: type 5 came:41 1,756 in 106, core:84 and core:122 1,440 in
187, core:95 1,390 in 188, core:86 1,269 in 114, came:45 1,066 in 124, core:139 1,050 in 135, came:39
1,044 in 122, came:59 (`MsCameraSetScrDpt`) 1,024 in 124, core:2 936 in 126. Type 6 core:0
(`FFX_AtelOp_WaitFrames`) 187 in 10, core:89 166 in 35, came:41 148 in 24, mapfx:2 134 in 5, core:93
(`FFX_AtelOp_PlayerControlOn`) 123 in 11, core:95 119 in 43, core:87 118 in 43.

- Every one of the 99 core:154 calls pushes an immediate id, and every id names a type 1 actor. So a
  retargeted trigger watches a scripted character instead of the player.
- core:240 and opcode `7A` occur in no shipped script, for any actor type. The link table at `+0x2E4`
  (12.5) is therefore always null in retail.
- core:139 is also called by 31 type 2 actors, core:2 by 375 type 1 actors. Neither is a type 5 feature
  as such.

### 12.3 What reads the type [V]

Found by an asm scan of `.text` for `sub x, 5` or `add x, 0FBh` followed by `cmp x, 1`, and for
`cmp x, 5` within 8 instructions of `cmp x, 6`, then read in Hex-Rays. 57 functions have the range form,
all in the ATEL code except one unrelated texture function. They fall in three groups.

1. **Layout only.** The `(type - 5) <= 1` selects: the shape block at `+0x284` instead of `+0x558`
   (`FFX_Atel_GetActorMotionState 0x86C2B0` and some 40 inlined copies in the move, rotate, position
   and trigger code), the channel clamp to `[0, 1]` (`FFX_Atel_GetActorThreadChannel 0x872650`, the
   exit hook of `FFX_Atel_RunScript`, `FFX_Atel_InitActorThreadRecords 0x86D300`, the level in
   `FFX_Atel_CreateThread 0x86EA00`, `0x863FE0`, `0x86DB80`), and the request nodes
   (`FFX_Atel_InitActorListNodes 0x86D510`: 8 nodes at `+0x1C4` and `+0x2E4 = 0`, against 16 at `+0x3D8`).
2. **Behaviour.** `FFX_Atel_StepActor 0x8666E0` (case 5 to `0x868020`, case 6 to `0x867C10`, with no
   bound player gate), `FFX_Atel_GetEventChannel 0x876860`, `FFX_Atel_GetEventScriptEntry 0x8763E0` and
   `FFX_Atel_GetDefaultEventTable 0x876390`, the constructor's shape block defaults and `+0xAB = 0xFF`,
   the trigger syscalls of 12.6, and the link table group (`0x86D190`, `0x86D110`, `0x86D1C0`, `0x8632B0`,
   `0x86FAB0`).
3. **Not type tests.** The `== 4 || == 5` in `FFX_Atel_IntegrateActorPos 0x8629C0` and
   `FFX_Atel_StepMoveCmd 0x868990` reads the attachment byte `+0xAA`, and the `case 5, 6` in
   `FFX_Atel_StepMoveCmd`, `FFX_Atel_ApplyMoveToChr 0x870540` and `FFX_Atel_ApplyRotToChr 0x870DC0`
   switch on a move command mode.

`FFX_Atel_StepFrame 0x867950` and `FFX_Atel_InitFrame 0x8677A0` read no type, so types 5 and 6 boot and
step like every other large actor.

### 12.4 The 744 byte slot [V]

| off | what |
|---|---|
| `+0x000..+0x12B` | the common large actor header (11.2) |
| `+0x12C` | 2 channels of 76 bytes |
| `+0x1C4` | 8 request nodes of 24 bytes, on the free list `+0x88` |
| `+0x284` | the shape block. The constructor writes up to `+0x2D3` |
| `+0x2D4..+0x2E3` | not written by the constructor |
| `+0x2E4` | the link table pointer, 0 from `InitActorListNodes`, set only by core:240 |

The shape block, offsets from its base, the same fields types 2 and 3 have at `+0x558`:

| off | line (2, 5) | box (3, 6) | default |
|---|---|---|---|
| `+0x00` | point A | centre | 0 |
| `+0x10` | read only when the actor is another trigger's target, see 12.8 | same | 0 |
| `+0x1C` | half height, 0 or less means no Y test | same | -1.0 |
| `+0x28` u16 | target actor id, types 5 and 6 only (core:154), `0xFFFF` the context's player pair | same | `0xFFFF` |
| `+0x30` | point B | X half extent | 0 |
| `+0x34` | | rotation about Y (core:90) | 0 |
| `+0x38` | | Z half extent | 0 |
| `+0x48` | radius (type 2 range test) | radius around the box (volume fallback) | 1.0 |

Flags: `+0x34` bit `0x80` armed (the `FFX_Atel_StepActor` gate), `0x40` inside, `0x100` fully inside
the box. `+0x36` bit `0x01` retest even if the target did not move (every shape setter sets it), bits 1..2
the side of the last crossing.

### 12.5 How they run [V]

1. **Created** by `FFX_Atel_ConstructActor` as large actors of the 744 segment. In shipped data only in
   event packages, so only in field context 0 (none in the 361 monster blocks) [D]. The constructor's
   entry 0 thread asks for channel 7 and `CreateThread` clamps it to 1, entry 1 goes on channel 0.
2. **Shaped** by entry 0: core:84 and core:122 for a line, core:87 and core:88 or core:89 (and core:90)
   for a box. Each of core:84 and core:87 also arms the trigger. Optionally core:85, core:154, core:2,
   and core:86 or core:92 to disarm until a story flag allows it. Entry 1 parks on core:95.
3. **Stepped** each frame. `StepFrame` runs the actor, `RunScript`'s exit calls the context `+0x5C` hook
   `FFX_Atel_StepActor` on the current channel, and an armed type 5 goes to
   `FFX_Atel_StepCrossingTrigger 0x868020`, an armed type 6 to `FFX_Atel_StepVolumeTrigger 0x867C10`.
   Types 1, 2 and 3 are skipped when `ctx+0x0A` is `0xFFFF`, types 5 and 6 are not.
4. **Type 5** (`FFX_Atel_TestCrossingForTarget 0x8680A0`). The moving segment is the target's
   `+0x10 -> +0x00`, or `ctx+0x228 -> ctx+0x218` for `0xFFFF`, and the test runs only when it moved in X
   or Z or the retest bit is set. The same crossing test (`0x889BB0`) and distance test (`0x889E90`) as
   `FFX_Atel_StepLineTrigger`, gated by the half height. On a hit `+0x36` bits 1..2 become 1 for a
   positive side and 0 otherwise, then `FFX_Atel_FireActorEvent(0xFFFF, self, 4)`. After the own target,
   `0x868020` walks the 8 link records at `+0x2E4` (u16 target, u16 entry, u8 side, 3 meaning unset), and
   each hit stores its side in the record and fires event 4 with the record's entry through
   `FFX_Atel_FireActorEventWithEntry 0x8766D0`. Dead in retail (12.2).
5. **Type 6.** Rotates both points of the target's step into the box frame and builds outcodes against
   the extents and half height. `0x100` is set when the current point is inside the extents, `0x40`
   (inside) when it is inside them or within the radius of the box edge. Outside to inside fires event 5,
   inside to outside event 6, and a step that passed straight through in one frame fires 5 then 6. All
   through `FFX_Atel_FireActorEventSync 0x876590`, which does not start an entry that is already running.
6. **Events.** `FFX_Atel_GetEventChannel` gives channel 1 for events 0, 1, 3, 4, 5 and 6 of both types,
   none for 2 and 7. The default entries are 2 for every event of type 5, and 2 for type 6 except leave,
   which is entry 3 (`g_ffxAtelEventScript_Kind5 0xC52AF8`, `_Kind6 0xC52B08`). That matches the entry
   counts, 3 for a type 5 (init, park, crossed) and 4 for a type 6 (init, park, entered, left). [V] [D]
   Events 0, 1 and 3 have a channel but no engine path fires them for types 5 and 6, since only the type
   1, 2 and 3 steppers feed the nearest examinable slot `ctx+0x1E8` and fire event 3. [I], the
   `FireActorEvent` callers outside the steppers were not all traced.
7. **The side.** core:139 (`0x85D410`) returns the side byte of the link whose entry is the current
   request's (`0x8632B0`), else `+0x36` bits 1..2, with 3 read as -1. The camera lines use it with one
   actor per direction on the same line: azit0300 actors 39 and 40 share their six line constants, entry
   2 of 39 sets its camera only when core:139 is 0 and entry 2 of 40 only when it is not. [V] [D]
8. **The camera handle.** core:2 (`0x85E920`) releases what the actor had (core:413 by `+0xAA`), sets
   `+0xAA = 2` and `+0xA8` to its argument, and through `0x7BA9A0 -> 0x7BAA50` caches
   `FFX_Came_GetHandleForClass(5, flag, n)` at `actor+0x3C`, the flag being its argument or "this is the
   system actor". For types 2, 3, 5 and 6 it also writes `+0x34` bits 13..14 = 2.

### 12.6 The trigger syscalls [V]

| syscall | handler | types | does |
|---|---|---|---|
| core:84 | `0x85F370` | 2, 5 | pops two points, the one pushed first is A. A at `+0x00`, B at `+0x30`, half height -1, arms, retest |
| core:122 | `0x85C130` | 2, 5 | moves A back along the line by the value pushed first and B forward by the one pushed last, retest |
| core:85 | `0x85FA20` | 2, 3, 5, 6 | half height, retest |
| core:86, core:92 | `0x85FBF0`, `0x8600C0` | 2, 3, 5, 6 | arm when nonzero, disarm when zero, retest. The two bodies are identical |
| core:306 | `0x85FE30` | 2, 3, 5, 6 | the same on another actor by id |
| core:87 | `0x8603E0` | 3, 6 | box centre, arms, retest |
| core:89 | `0x855CD0` | 3, 6 | X half extent, Z half extent, half height |
| core:88 | `0x855F80` | 3, 6 | core:89 with half height -1 |
| core:90 | `0x856170` | 3, 6 | rotation, retest |
| core:91, core:133 | `0x856600`, `0x856460` | no test | radius `+0x48`, retest. A type 4 has no block, so this writes near address 0 |
| core:127 | `0x85C320` | 2, 3, 5, 6 | `+0x34` bits 13..14 |
| core:139 | `0x85D410` | any | the crossing side, -1 for none |
| core:154 | `0x860390` | 5, 6 | target actor id `+0x28`, returns it. On other types it only returns it |
| core:240 | `0x8576A0` | 5, 6 | resolves a variable and hands it to `0x86D190`: `+0x2E4` = it, 8 links cleared (`0x86D110`), and 16 more request nodes from table `+0x40` on put on the free list (`0x86D1C0`). So the table is 448 bytes |
| op `7A` | `0x86FAB0` | 5, 6 | binds (actor, entry) into the link already holding that actor, else the first free one |

### 12.7 Identification

Type 5 is a crossing line trigger and type 6 an enter and leave volume, on this evidence:

- `FFX_Atel_StepActor` dispatches them to steppers that call the same geometry as the type 2 and type 3
  steppers on the same shape fields, and fire the subset of their events listed above. [V]
- The shape syscalls accept exactly 2 and 5 for a line (core:84, core:122) and 3 and 6 for a box
  (core:87 to core:90), and the shipped scripts of nearly every type 5 and type 6 actor set their shape
  in entry 0 (1,349 of 1,382 and 115 of 116). [V] [D]
- The position getters `0x86A2F0` (midpoint) and `0x86A390` (point B) treat 2 and 5 alike as lines and
  everything else as a point. [V]
- What they are used for is a census fact, not an engine one: camera switching for most type 5, event
  scenes and effects for type 6. Nothing in the engine ties either type to the camera. [D]

The existing IDB name `FFX_Atel_StepCrossingTrigger` (from `INTERACTION_PATH.md`) is a misnomer. There is no
path, the shape is one straight segment, the same as type 2's.

IDB names, applied and saved 2026-10-26 after a check of the step dispatch and the events each type fires
(the current column is what the IDB had before):

| VA | current | proposed | why |
|---|---|---|---|
| 0x868020 | `FFX_Atel_StepCrossingTrigger` | `FFX_Atel_StepCrossingTrigger` | the type 5 stepper, own target then the 8 links |
| 0x8680A0 | `FFX_Atel_TestCrossingForTarget` | `FFX_Atel_TestCrossingForTarget` | one target's step against the line, event 4 |
| 0x8632B0 | `sub_8632B0` | `FFX_Atel_GetLinkCrossingSide` | side byte of the link for the current request's entry |
| 0x86D190 | `sub_86D190` | `FFX_Atel_SetTriggerLinkTable` | core:240's worker, `+0x2E4` |
| 0x86D110 | `sub_86D110` | `FFX_Atel_ClearTriggerLinks` | 8 links to `0xFFFF`, side 3 |
| 0x86D1C0 | `sub_86D1C0` | `FFX_Atel_AddLinkTableRequestNodes` | 16 nodes from the table onto `+0x88` |
| 0x86FAB0 | `FFX_Atel_Op_BindTriggerLink` | `FFX_Atel_Op_BindTriggerLink` | opcode `7A`, what the slot is |
| 0x86A2F0 | `sub_86A2F0` | `FFX_Atel_GetActorCentre` | line midpoint, else the position |
| 0x86A390 | `sub_86A390` | `FFX_Atel_GetActorEndPoint` | line point B, else the position |
| 0x86A720 | `sub_86A720` | `FFX_Atel_GetActorStartPoint` | point A, the position for every type |
| 0x7BAA50 | `sub_7BAA50` | `FFX_Atel_BindActorCameraHandle` | `FFX_Came_GetHandleForClass(5, ..)` cached at `+0x3C` |
| 0x86AE50 | `sub_86AE50` | `FFX_Atel_GetActorCameraHandleSlot` | `actor + 0x3C` |
| 0x85F370 | `FFX_AtelSys_Core_084_resi` | `FFX_AtelSys_Core_084_SetTriggerLine` | 12.6 |
| 0x85C130 | `FFX_AtelSys_Core_122_resi` | `FFX_AtelSys_Core_122_ExtendTriggerLine` | 12.6 |
| 0x85FA20 | `FFX_AtelSys_Core_085_resi` | `FFX_AtelSys_Core_085_SetTriggerHalfHeight` | 12.6 |
| 0x85FBF0 | `FFX_AtelSys_Core_086_resi` | `FFX_AtelSys_Core_086_SetTriggerArmed` | 12.6 |
| 0x8600C0 | `FFX_AtelSys_Core_092_resi` | `FFX_AtelSys_Core_092_SetTriggerArmed` | 12.6, identical body |
| 0x85FE30 | `FFX_AtelSys_Core_306_resi` | `FFX_AtelSys_Core_306_SetActorTriggerArmed` | 12.6 |
| 0x8603E0 | `FFX_AtelSys_Core_087_resi` | `FFX_AtelSys_Core_087_SetTriggerBoxCentre` | 12.6 |
| 0x855CD0 | `FFX_AtelSys_Core_089_resi` | `FFX_AtelSys_Core_089_SetTriggerBoxExtents` | 12.6 |
| 0x856170 | `FFX_AtelSys_Core_090_resi` | `FFX_AtelSys_Core_090_SetTriggerBoxRotation` | 12.6 |
| 0x85D410 | `FFX_AtelSys_Core_139_resi` | `FFX_AtelSys_Core_139_GetCrossingSide` | 12.5 item 7 |
| 0x860390 | `FFX_AtelSys_Core_154_resi` | `FFX_AtelSys_Core_154_SetTriggerTarget` | 12.6 |
| 0x8576A0 | `FFX_AtelSys_Core_240_resi` | `FFX_AtelSys_Core_240_SetTriggerLinkTable` | 12.6 |

### 12.8 Open

- What reads `+0x34` bits 13..14, which core:2 and core:127 write for the trigger types.
- Whether any syscall fires events 0, 1 or 3 at a type 5 or 6 actor.
- Which of shape `+0x00` and `+0x10` of a target is its current position. The steppers pair `+0x10`
  with `ctx+0x228` and `+0x00` with `ctx+0x218`, `INTERACTION_PATH.md` section 1 names `+0x00` the
  previous one and its trigger hazard note names `ctx+0x228` the previous one, and the two cannot both
  hold.
- Definition bytes `+0x01..+0x07` and `+0x0C..+0x13` of types 5 and 6 were not checked against a reader.

## 13. The syscall dispatch (2026-10-26, PROGRESS 11.3)

Traced read only through idalib on a scratch copy of the IDB (nothing renamed, nothing saved) and ported
as `src/engine/atel/SyscallTables.h/.cpp` over the boundary in `Syscall.h`, tested by
`tests/engine/AtelSyscallTest.cpp` and `AtelTitleTest`. This extends 11.7, which stays correct. [V]
unless marked.

### 13.1 The three calls

RunScript's case for `35` and `58` (0x864CFB) sets the run state to 1, clears the continue flag, and
switches on the channel's phase byte `+0x1E`: 0 sets it to 1 and calls the start, then falls into the
poll, 1 goes straight to the poll, anything else stays on the instruction. The arguments are the
actor (`ebx`), the channel's work area (`edi+0x2C`) and the stack (`esi`, `actor+0xC4`, set at 0x86425E):

| function | slot | called as |
|---|---|---|
| `FFX_Atel_SysFuncStart 0x8777A0` | `+0` | `start(actor, work, stack)`, after the library 6 bit (`+0x36 |= 0x20` when `0x10` is clear) |
| `FFX_Atel_SysFuncPoll 0x877730` | `+4` | `poll(actor, work)`, **no stack**. A null slot returns 5 |
| `FFX_Atel_SysFuncResult 0x8777F0` | `+8`, `+12` | `getter(actor, work, stack)`, only when the poll's bit 1 is set |

Each of them indexes the table again from the id: `g_ffxAtelSysFuncLibs[id >> 12] + 16 * (id & 0xFFF)`.
The library is `id >> 12` without a mask, which is 0..15 because the operand is 16 bits. **No call
checks the function against the table size.** There is no per library count anywhere in the dispatch.

The result rule: the float slot first. It is called as a function returning in `st0`, stored with
`fstp dword` (so rounded to float), reloaded and passed to `FFX_Atel_PushFloat` (tag 2). Only when the
float slot is null is the int slot called, and its `eax` goes to `FFX_Atel_PushInt` (tag 1). Neither
gives `PushInt(0)`. After the push the phase goes back to 0, and `58` pops the top with
`FFX_Atel_PopFloat` and stores it to `R` with `fstp`, so an int result reaches R converted. `35` leaves
it on the stack. Both pushes drop a value on a full stack (19 slots), and that path also writes
`g_ffxAtelActorClampCount = 0`, which the port does not model (the 11.2 note says the same of pops).

The poll bits are 11.7's: 1 done, 2 clears the reschedulable bit at exit, 4 sets the continue flag. The
reschedulable local is set to 1 at every fetch (0x864404) and the poll ANDs it to 0 on bit 2 (0x864D5D),
so it is the last instruction's, as 11.3 says. The 11.2 port already carries the phase across steps and
the bits, and the dispatch port does not touch them. One consequence worth knowing when writing a
handler: a `35` result stays on the stack, and RunScript leaves with the continue flag clear only once
the stack is empty, so the instruction that consumes the result runs in the same frame even when the
poll did not set bit 4 (`AtelSyscallPhasesCarryAcrossSteps`).

### 13.2 The tables

`FFX_Atel_Init 0x86D6D0` first registers all 16 slots with null, which
`FFX_Atel_RegisterSysFuncLib 0x877880` turns into `g_ffxAtelSysFuncLibEmpty 0xC52B60`, then registers the
eleven real ones in the order 1, 0, 12, 6, 9, 5, 4, 11, 7, 8, 13. Sizes and addresses are
FFX_GAME_NOTES.md's, except that Movie has 16 rows, not 146 (`ATEL_MOVIE_SAVE_LIBRARIES.md` 1.3), so
1,516 rows in all. Row shapes, which of the four slots are non-null, counted over every row:

| lib | rows | int only | float only | start+poll+int | start+poll | poll only | all four | other | null |
|---|---|---|---|---|---|---|---|---|---|
| 0 Core | 616 | 500 | 76 | 18 | 15 | 6 | | | 1 (435) |
| 1 Sys | 30 | 12 | 18 | | | | | | |
| 4 Sg | 71 | 63 | 5 | 1 | 1 | | | | 1 (2) |
| 5 Ch | 145 | 126 | 6 | 7 | 3 | 1 | | | 2 (18, 28) |
| 6 Came | 138 | 129 | 2 | | 3 | 4 | | | |
| 7 Btl | 296 | 252 | 17 | 2 | 14 | 10 | | 1 poll+int | |
| 8 MapFx | 108 | 93 | 12 | | 3 | | | | |
| 9 Test | 1 | 1 | | | | | | | |
| 11 Movie | 16 | 9 | | 7 | | | | | |
| 12 Save | 94 | 90 | 2 | | 1 | | | | 1 (10) |
| 13 AbMap | 1 | 1 | | | | | | | |

So no row has both getters. A null row is the null slot: no start, poll 5 (done, keep running), int 0.
A row with only some slots gets the null behaviour for the missing ones, slot by slot.

`tools/atel_syscall_shapes.py` in the recreation prints the per row shape from the IDB, one hex digit
per row, and `SyscallTables.cpp` carries its output.

**Every non-null slot points at code.** An earlier count gave Movie 146 rows with odd slots past row 15
(`0x1`, `0x2, 0x30, 0x2F00, 0x38FC`, `0x100`, a bare `ret` called `FFX_AtelSys_BareRet`). Those were never
rows. Movie ends at 0xC40F30, and what follows is the sa_menu pointer table `off_C40F30`, then menu
module pointers, menu globals and two more sa_menu tables (`ATEL_MOVIE_SAVE_LIBRARIES.md` 1.3 and 5). The
bare `ret` at 0x773B90 is a do nothing entry of one of those tables, not a syscall slot. Nothing bounds
the index, so a movie:16..145 call in the original would still read those bytes as {start, poll, float,
int}, which is why the recreation keeps 146 as Movie's scan extent. No shipped script calls Movie past 13
(ebp.py's LIB_INFO).

Some slots point at code IDA has not made a function: Core 214, 449 and 507 int (0x85DE80, 0x85A420,
0x85AF10). Four polls are `return 5` stubs IDA's signatures misname as `__RTC_NumErrors_*`
(Core 261, Ch 1, Ch 16, Movie 0), and two int getters misnamed `_wcstoul` and `_wcstoul_0` (Btl 111,
Came 70) forward to a four argument function with a trailing 1.

### 13.3 Past the end of a table

Because nothing checks the index, a function past the end reads the bytes after its table. Only two
tables are followed by another library's rows on a 16 byte boundary:

- the empty table: `0xC52B60 + 8 * 16 = 0xC52BE0`, Sys. So `lib2:8..37` (and 3, 10, 14, 15) run Sys rows
  0..29. Past that is the `"atelsaveram000"` string.
- Sg: `0xC88D88 + 71 * 16 = 0xC891F8`, Ch. So `sg:71..215` run Ch rows 0..144. Past that is a string.

Everything else past an end is strings, integer tables or other globals: Core is followed by the table at
0xC526D0 (`0xFFFFFFFF, 1, ...`, a start there calls 0xFFFFFFFF), Sys by the `atelsaveram` string and
then Save misaligned by 8 bytes, Ch by a string, Came by `g_ffxCameMainNearLatch`, Btl by the id table at
0xC438A8, MapFx by `dword_C5E350`, Test by data, Movie by `off_C40F30` (the sa_menu getter table, so
movie:16 and up would call menu functions with ATEL arguments, and further on hit menu globals), Save by
the event header path, AbMap by `off_C85EC0`. A call there executes data, or the wrong code, in the
original. No shipped script does any of this, the
highest function each library is called with is inside its table (ebp.py LIB_INFO).

### 13.4 The port

- `SyscallLibraryRows`, `SyscallLibraryVa` and `ResolveSyscall` in `Syscall.h`: Row, Neighbour (the two
  cases above) or OutOfRange. The dispatch raises `SyscallOutOfRange` (through `SyscallFrame::Fault`) at
  the start of an OutOfRange call. The interpreter still raises the 11.2 `EmptyLibraryCall` for every call
  into an empty library, a Neighbour one included.
- `SyscallRow` is four plain function pointers, a name and a ported flag. A null pointer behaves as the
  original's null slot, so the dispatch (`SyscallDispatch::Start`, `Poll`, `Result`) is the three
  functions above, line for line.
- `SyscallTables` holds every row back to back, the defaults built at compile time: every row null, the
  12 rows the original leaves null marked ported (they are already exact, and seven of them, Movie 52,
  53, 80, 81, 82, 92 and 138, are zero words of the sa_menu table inside Movie's 146 row scan extent, not
  real rows), and the five empty libraries
  pointing at one shared block of 8 rows of `EmptyStart` and `EmptyPollBlocks`. `Register(library,
  function, row)` is what 11.4 to 11.9 call, it refuses a function past the table and the empty
  libraries. `ShapeMismatches()` lists ported rows whose slots differ from the original's, so a port that
  invents or drops a getter shows up in a test.
- A row that is not ported runs as the null slot, and the dispatch tells its sink `(library, function)`
  once per id, with the id as the script wrote it (`sg:71`, not `ch:0`). An OutOfRange id runs as the
  null slot too, with the fault. The interpreter keeps taking a `SyscallHandler`, so the 11.2 tests and
  their scripted handler are unchanged, and `NullSyscalls` is gone (the dispatch over default tables is
  the same thing plus the original's own null and empty rows).
- Names: `FormatSyscall` prints ebp.py's `core:66` form, with the name from `SYSCALL_NAMES` in ebp.py
  where it has one (54 names, names only).
- No RNG: the dispatch draws nothing. Any draw is a handler's (core:169 is `FFX_Rand_Stream(2)`, MONSTER_AI.md).

The title (event 23, 120 steps, `AtelTitleTest`) makes 4,910 calls to 35 distinct ids, all inside their
tables, none ported yet and none an original null row, so all 35 are reported once: core 0 (WaitFrames),
66, 67, 68, 95, 107, 137, 178, 179, 180, 187, 188, 202, 203, 226, 231, 250, 251, 259, 267, 369, 370, 392,
496, 505, 535, 577, 595, 601, sys 0, sg 5, 32, save 7, 24, 81. The ATEL hash after 120 steps is still
`combined=0xE800F4BE`, the 11.12 figure, so moving the title onto the dispatch changed nothing.

### 13.5 IDB names

Applied and saved 2026-10-26 after checks of the three wrappers and that each stub sits in exactly one
row (the four `loc_` targets were made functions first). `sa_menu_init` is not split out yet.

Not applied.

| VA | proposed | why |
|---|---|---|
| 0x859DF0 | `FFX_AtelSys_Core_261_poll` | `return 5`, misnamed `__RTC_NumErrors_808` |
| 0xA79A10 | `FFX_AtelSys_Ch_001_poll` | `return 5`, misnamed `__RTC_NumErrors_956` |
| 0xA79970 | `FFX_AtelSys_Ch_016_poll` | `return 5`, misnamed `__RTC_NumErrors_955`, between `chReadSystemMGRP` and its finish |
| 0x76E980 | `FFX_AtelSys_Movie_000_poll` | `return 5`, misnamed `__RTC_NumErrors_800` |
| 0x7B9D20 | `FFX_AtelSys_Came_070_resi` | misnamed `_wcstoul_0` |
| 0x7A36C0 | `FFX_AtelSys_Btl_111_resi` | misnamed `_wcstoul` |
| 0x85DE80, 0x85A420, 0x85AF10 | `FFX_AtelSys_Core_214_resi`, `_449_resi`, `_507_resi` | make functions first, they are `loc_` |

The Movie 16 start (0x7722A0), `sa_menu_init` (0x76F3B0) and `FFX_AtelSys_BareRet` (0x773B90) rows that
used to be here are gone. They are sa_menu functions, not syscall handlers, and their proposed names are in
`ATEL_MOVIE_SAVE_LIBRARIES.md` section 5.

### 13.6 Open

- `g_ffxAtelActorClampCount` is cleared by a push onto a full stack, including a result push. Readers
  not traced (11.2 open item).
- What the misnamed `_wcstoul` pair and the `loc_` slots do is 11.4 to 11.9's.
