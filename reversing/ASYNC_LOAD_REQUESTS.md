# AsyncLoadManager request kinds, producers and completions

Written 2026-10-13 for Project Zanarkand item 1.4.5. Every address is an IDA VA in FFX.exe
(image base 0x400000). Subtract 0x400000 for the RVA a kit hook wants. `[V]` means read in
Hex-Rays or the disassembly this session, `[I]` means inferred from names, data or shape and not
read end to end. The IDB was read only, so every name below that starts `sub_` is still unnamed
there. Proposed names are in section 7.

## 0. The short answer, lockstep hazards first

The worker thread does more than read bytes. Two kinds of request run their whole body, and their
completion callback, on the AsyncLoadManager worker. Several of those bodies write state the step
thread reads to make decisions. These are the off step writes that matter:

| # | what the worker writes | who writes it | step reader | why it matters | status |
|---|---|---|---|---|---|
| H1 | `dword_112CA68` (propose `g_ffxBtlMapLoadPending`) cleared to 0 | `sub_782FD0` from `sub_65A6C0`, the last map cluster's callback, on the worker | `FFX_Btl_StepSubPhase` case 34 waits for 0, then sets phase 10 and calls `FFX_Btl_RollPreemptiveOrAmbush(32)` and `sub_797A40` in the same step | **battle start step depends on disk speed**, and the ambush roll consumes battle RNG on that step | [V] |
| H2 | gfx byte +4501 (map scene loading) set 1, or cleared 0 with +4500 set, on a failed header | `FFX_Gfx_LoadMapAssetHeader 0x65CBF0` on the worker | `FFX_Gfx_IsMapSceneLoading 0x641BF0` from `FFX_MainStep_IsFadeBlocking 0x81FCD0` | fade gate and map change timing | [V] |
| H3 | gfx +36 and the texture slot tables +60, +84, +96 for magic TexLists | `sub_664550` and `sub_666880` on the worker | `FFX_MainStep_PollMagicPrxLoad 0x822250` via `sub_6420D0` -> `sub_6522B0`, which clears +36 when every slot is in, then starts the magic PRX | the step a spell's effect begins, for load mode 2 and 3 ids. Mode 1 ids (622..628, 348, 631, 635) start before this wait (`cmp esi, 1` at 0x8222B4). The TexList is only requested after H4 completes, so the two waits add up. See `MAGIC_ASSETS.md` | [V] |
| H4 | `dword_CE83C4` 2 -> 3, `dword_CE83C0` (HMODULE), and through the stashed callback `g_ffxMagicFileHinst`, `g_ffxMagicGetEffectOverlayTable`, `g_ffxMagicInitMagicPRX` | `FFX_LoadDll_AsyncWorker 0x6F9CC0` and `MagicFile__onDllLoaded` on the worker | `sub_6F9B70` (== 3), `sub_6793B0`, `sub_6793C0` in `FFX_MainStep_PollMagicPrxLoad`, and `sub_9DA280` | same as H3, the magic DLL gate | [V] |
| H5 | ClassCharacter +4, +20, +24, +28, the gfx character table, `g_ffxBattleChrLoadsInFlight 0xC421DC` decrement, the pending record +264/+268 | `ClassCharacter__loadFromFile`, `FFX_Chr_OnSubAssetLoaded 0x65AE40`, `FFX_Chr_FinishCharacterLoad 0x65A760`, `FFX_Ch_OnModelLoaded 0x82A140` | `FFX_Chr_IsClassCharacterLoading 0x82A100`, the battle load wait on the in flight counter | battle and event wait loops end on a disk dependent step. Already written up in `CLASS_CHARACTER.md` | [V] |
| H6 | `dword_12FB8F4`, an unlocked `++` | `FFX_Tex_OnAsyncLoaded 0x666740` | `FFX_DreamFilter_Update 0x8223A0` | `== 5` starts `sub_63DF70`. Visual as far as read | [V] write, [I] no sim effect |
| S1 | `dword_23112C4`, `dword_23112C0` (battle field text bin and ftc) | `FFX_Btl_OpenFieldArchive 0x784500` as a RegistTask | `FFX_Btl_SetupFieldPointers` at sub phase 7 | safe while the queue stays single worker FIFO, because the field ROM read that sets phase 7 is queued after it | [V] |
| S2 | SyncData parse result and mgr+32 = 1 | `sub_6FAF90` as a RegistTask | `FFX_SyncData__readNextTimingEntry 0x6FB160`, `__selectTimingTrack 0x6FB380`, `__lookupVoiceSync 0x6FB410` | safe. Every reader busy waits on +32, so the cost is a stall, never a different answer | [V] |

ROM reads (asset loader slot 0) are different. Their callbacks run on the step thread inside
`FFX_AsyncLoad_DispatchCompletions 0x6427F0`, which is the fourth statement of `FFX_MainStep`
(0x820B10). That is a step boundary, but the step on which a completion appears still depends on
how fast the worker got through the read. So every row in section 3 is a "which step" hazard even
though it is on the right thread. [V]

**Port rule that falls out of this.** A port can keep the IO thread, but no completion may write
simulation state off the step thread, and every "load done" edge the step reads (H1 to H5 and
every section 3 callback) has to become a step ordered event that all peers apply on the same
step. The cheapest version is the one `AsyncLoadManager.h` in the port already models: deliver
completions in request order on the step thread, and have the lockstep layer hold a step until
every peer reports the same completion. [I]

## 1. The manager itself

`g_asyncLoadManager 0xCCC88C`. One worker thread, `AsyncLoadManager__workerThread 0x691360`. [V]

| offset | what |
|---|---|
| +8 | work semaphore |
| +12 | pass done semaphore |
| +16 | mutex around the queues |
| +0x14 | running flag (`0x637840` is a thunk that returns it) |
| +32, +44 | the double buffered request queues |
| +56 | which of the two is being filled |
| +60, +64, +68 | child list bookkeeping |

The worker swaps the queues under the mutex, then for each request calls vtable slot 1
(execute) and slot 2 (release). Slot 0 is the deleting destructor. A request can own a child
list (`AsyncLoadManager__createChildList 0x690F70`), and the children run on the worker straight
after their parent, before the next request in the outer queue. [V]

There are exactly two request classes. [V]

| class | size | pool | count | execute |
|---|---|---|---|---|
| `RegistTaskRequest` | 32 | `dword_CCC894` | 150 | `RegistTaskRequest__execute 0x691350`: `fn(args)`, fn at +4, args at +8 |
| `LoadClusterRequest` | 148 | `dword_CCC890` | 200 | `sub_69A1A0()`, `r = FFX_ClusterCache_Load(path +4)`, then `cb(r, ctx)` with cb at +8 and ctx at +12 |

Both run their body **and** their callback on the worker. Nothing about either class marshals
back to the step thread. The only marshalling in the binary is the libmscd layer in section 3,
which is built on top of RegistTask. [V]

Enqueue functions:

| function | VA | builds |
|---|---|---|
| `AsyncLoadManager__enqueue` | 0x6918B0 | RegistTask (`allocRequest 0x6910C0`) |
| `AsyncLoadManager__enqueueWithContext` | 0x691660 | LoadCluster (`allocClusterRequest 0x690FF0`, `LoadClusterRequest__set 0x691550`, `appendRequest 0x690E60`) |
| `AsyncLoadManager__isIdle` | 0x691630 | poll used at boot |

## 2. RegistTask producers

Every caller of `AsyncLoadManager__enqueue`, found by a full rel32 scan of .text plus an
absolute reference scan. [V]

| # | producer (call site) | task body on the worker | what the body writes | consumer on the step thread | status |
|---|---|---|---|---|---|
| A1 | `FFX_BootSequence` 0x6416EB -> `FFX_Boot_FirstAsyncLoad 0x636430` | `FFX_Fmv_ParseVideoList 0x6D82F0`, the VFX shader manifest, `sub_657A50` and its LoadCluster children | FMV map at mgr+1768, shader tables | boot polls `AsyncLoadManager__isIdle`. Boot only | [V] |
| A2 | `FFX_RomDev_PostReadTask 0x642950` (thunk `0x637860`) from `FFX_RomDev_EnqueueRead` 0x76BDB5 | `FFX_RomDev_ExecuteQueuedRead 0xA43740` | the read buffer, then `FFX_AsyncLoad_PostCompletion 0x642980` queues the callback | section 3 | [V] |
| A3 | same thunk from `FFX_LoadDll_Impl 0x6F9BC0` | `FFX_LoadDll_AsyncWorker 0x6F9CC0`: `LoadLibraryA`, `dword_CE83C4` 2 -> 3, `dword_CE83C0` = HMODULE, then the callback stashed at path+132, `MagicFile__onDllLoaded` | H4 | `FFX_MainStep_PollMagicPrxLoad 0x822250` | [V] |
| A4 | same thunk from `FFX_Btl_StepSubPhase` 0x7830E9, case 5 | `FFX_Btl_OpenFieldArchive 0x784500` | S1 | `FFX_Btl_SetupFieldPointers`, phase 7 | [V] |
| A5 | `FFX_AsyncLoad_EnqueueCallTask 0x6442F0` from `FFX_Ch_LoadModel` 0x82A35D | `FFX_Ch_OnModelLoaded 0x82A140` | clears the pending record +264/+268 | `FFX_Chr_IsClassCharacterLoading 0x82A100` | [V] |
| A6 | `FFX_Gfx_BeginMapLoad 0x65CE90` (sets gfx+4501 = 1 and +216 = 0 on the step first) | `FFX_Gfx_MapLoadTask 0x657820` -> `FFX_Gfx_LoadMapAssetHeader 0x65CBF0` | gfx +0 (`lchb08`), +68910 (`lchb12`), +63940, +63941 (`grid00`), `dword_CCB9C4` (event 355 or 356), `byte_CCB9C0`, +2364, +216, +4500, +4501, and `sub_6F6DC0(46/72/32/70)` keyed by gfx+64244 map ids 7968, 544, 3076, 2668. Then one LoadCluster child per header row (section 2b) | H2, then H1 through the last child | [V] |
| A7 | `FFX_EnqueueCharacterLoad 0x6642F0` | `FFX_AsyncLoad__characterJob 0x657850` -> `ClassCharacter__loadFromFile 0x663F20` | H5. See `CLASS_CHARACTER.md` | `FFX_Chr_IsClassCharacterLoading` and the in flight counter | [V] |
| A8 | `sub_664D10` 0x664D69 | `sub_657880` -> `sub_664550` (TexList loader, variant A) | per row LoadCluster with `sub_666880`, then this+36 = 1 at the end | H3 | [V] |
| A9 | `sub_665F60` 0x665FB9 | `sub_6578B0` -> `sub_665770` (TexList loader) | per row LoadCluster with `sub_666880`, which writes gfx+96[i] = 1, +60[i], +84[i] and registers the slot through `sub_656AE0` into the gfx+72 table | texture lookups at render, plus H3 for magic | [V] |
| A10 | `FFX_SyncData__requestForMap 0x6FB080` | `sub_6FB130` -> `sub_6FAF90` | S2 | busy waiting readers | [V] |

Who calls A8 and A9:

- `sub_664D80` (magic TexList) is reached from `sub_6420B0`, which is called by `sub_9DA280` and
  `MagicFile__load` 0x9DA665. It sets gfx+36 = 0 on the step first. When
  `FFX_Magic_GetPrxLoadMode` (`g_ffxMagicPrxLoadMode`) is 0 the same work runs synchronously through
  `sub_665770` on the step thread instead. [V]
- `sub_665F60` kind 4 comes from `sub_663F00` <- `sub_639310` <- the ATEL calls
  `FFX_AtelSys_Core_283 sub_8748A0` and `FFX_AtelSys_Core_282 sub_874B70`, with the path from
  `sub_874C50` (the event TexList, see `ASSET_PATHS.md` 5.7). [V]
- `sub_665F60` kind 1 comes from `sub_664F60` (the field `fp` TexList, guarded by gfx+37),
  reached from `sub_6454A0` <- `sub_908000` and `FFX_Map_LoadMapOutSync`. [V]
- `sub_665F60` kind 3 (`menu/titl` TexList at `0x665FD0`) is only reachable from the orphan thunk
  `0x645E20`. Dead in retail. [V]

## 2b. LoadCluster producers

Every caller of `AsyncLoadManager__enqueueWithContext`. The callback runs on the worker. [V]

| producer (site) | callback | what the callback writes | when | status |
|---|---|---|---|---|
| `FFX_Gfx_LoadMapAssetHeader` 0x65CE6E, one per header row | `FFX_Gfx_OnMapClusterLoaded 0x666630` -> `FFX_Gfx_AdoptMapCluster 0x65AF60`. On the last row `sub_65A6C0` | `PClusterArray__linkImports`, then gfx+4500 = 1 when both sections are in, else gfx+216 = 0, and in both branches `sub_782FD0` clears `dword_112CA68` | every map load | [V], H1 |
| `ClassCharacter__loadFromFile` 0x66427B | `FFX_AsyncLoad__charSubAssetJob 0x6663D0` -> `FFX_Chr_OnSubAssetLoaded 0x65AE40`, last row `FFX_Chr_FinishCharacterLoad 0x65A760` | the fp table at chrWork+64716, `g_ffxBattleChrLoadsInFlight` decrement | every character | [V], H5 |
| `sub_664550` 0x664B73 and `sub_665770` 0x665DD5 | `sub_666880` | texture slot tables | TexLists | [V], H3 |
| `sub_664440` 0x6644D5 | `sub_666610` | gfx+64164 (CutBuffer shader cluster) | boot, via `sub_6656E0` <- `FFX_GraphicInitialize` 0x641B3D | [V] |
| `FFX_Tex_EnqueueAsyncLoad 0x6651D0` | `FFX_Tex_OnAsyncLoaded 0x666740` | linkImports, `++dword_12FB8F4` when `dword_12FB8F0` | menu, help, font, copyright, cloud save textures (`sub_67C6F0`, `sub_67C7A0`, `sub_67C8C0`, `FFX_SysTex_RequestCopyright`, `sub_67CB30`, `sub_67CBA0`, `sub_67CBD0`, `sub_67CE50`, `sub_67D180`, `sub_67D370`, `sub_67D650`, `sub_67D670`, `sub_67D690`, `sub_67DD70`, `FFX_SysTex_RequestCloudSaveTex`) | [V], H6 |
| `sub_693D20` 0x693D4D | `sub_694C20` | post process shader table `off_C34114`, 14 entries, `sub_693D70` at index 13 | boot via `sub_693BE0` <- `sub_6656E0` | [V] |
| `FFX_Vfx_ParseShaderProperty 0x6C0F30` | `sub_6C13E0` | VFX shader slots | boot manifest | [V] |
| `sub_6C1520` 0x6C1738 and 0x6C1821 | `sub_6C1280`, `sub_6C1320` | dynamic geometry materials | render objects, `sub_6A4240` vtable 0xB47E58 | [V] |
| `sub_6C2710`, `sub_6C38B0`, `sub_6C5360` | `sub_6C28B0`, `sub_6C3950`, `sub_6C53E0` | distortion and blend frame shaders | boot, all from `sub_657A50` | [V] |
| `FFX_Fmv_CreatePlayer 0x6DA160` (`Video.cgfx`) | `sub_6DA460` | player+864, `sub_6D86E0` | each FMV, from `FFX_Fmv_StartVideo` 0x6416AA | [V] |
| `FFX_DebugText_LoadFont` 0x6F639B (`off_C38E10`, PhyreText tuffy) | `FFX_DebugText_OnAssetLoaded 0x6F63F0` | debug font | debug only | [V] |

Orphans, no code or data reference anywhere in .text (rel32 and absolute scans): `0x664520`
(callback `sub_6666A0`), `0x6DA300` (an FMV shader duplicate), `0x666310`, `0x666350`,
`0x6666C0`, `0x666700`, `0x666780`, `0x6667C0`, `0x666800`, `0x666840`, `0x6D2D30`, `0x69A950`,
`0x6DA900`, `0x645E20` (and so the titl TexList at `0x665FD0`), the credits loader `0x6DA550` and
the MetaMenu timestamp reader `0x6D98E0`. [V] Its `/MetaMenu/...` paths are keys in `FFX_Data.vbf`'s
own `metamenu/` tree, not in `metamenu.vbf` (`METAMENU.md` section 4).

## 3. ROM reads (asset loader slot 0) and the step thread completion

The libmscd layer, the only path that moves a completion back to the step thread. [V]

```
step:    g_ffxAssetLoader[0](index, buf, cb, arg)       = FFX_RomDev_EnqueueRead
         -> FFX_RomDev_PostReadTask 0x642950
         -> AsyncLoadManager__enqueue(0x637860, FFX_RomDev_ExecuteQueuedRead, slot)
worker:  FFX_RomDev_ExecuteQueuedRead 0xA43740
         opens g_ffxRomReadQueuePaths[slot] with FFX_File_OpenHost0, reads, closes
         if entry+0x20 (cb) != 0: FFX_AsyncLoad_PostCompletion 0x642980
step:    FFX_AsyncLoad_DispatchCompletions 0x6427F0
         (statement 4 of FFX_MainStep 0x820B10, plus FFX_RomDev_Pump 0x76C490 and
          FFX_RomDev_PumpBlocking 0x76E660)
         -> FFX_RomDev_InvokeReadCallback -> entry+0x20(...)
```

Callbacks registered through slot 0, found by walking every `FFX_GetAssetLoader 0x76D0D0`
caller. All run on the step thread. [V]

| callback | what it does | status |
|---|---|---|
| `sub_782FE0` | `g_ffxBtlSubPhase = 7` (battle field read done) | [V] |
| `sub_783990` | `FFX_Btl_BindMonsterArchiveSections`, `FFX_Btl_LoadMonsterData` | [V] |
| `loc_783DD0` | battle foot data | [V] |
| `sub_783FC0` | weapon models, `g_ffxBtlSubPhase` 18 or 19, `byte_112BEA2` | [V] |
| `sub_78FED0` | `byte_11333F0` | [V] |
| `sub_79CC80` | `byte_112C9CF = 1`, `FFX_Mot_RegisterSystemMgrp` | [V] |
| `sub_7B0500` | actor +3581 = 0 | [V] |

Many reads pass callback 0 and then block in `FFX_RomDev_PumpBlocking` or poll. Direct slot 0
users with no callback include `FFX_Ch_RomRead`, `FFX_Ch_RomReadMotionSet`,
`FFX_Ch_LoadRomIndexTables`, `FFX_Ch_RomReadEventMotionGroup`, `FFX_SaMenu_Draw 0x76F850` and
`sa_menu_start 0x770430` (sa_menu code, not Movie syscalls as older names had them,
`ATEL_MOVIE_SAVE_LIBRARIES.md` section 5), `sub_76CFF0` and the site at 0x77D694. A blocking read is deterministic in result and
only costs wall time. A polled one has the same "which step" hazard as a callback. [V]

## 4. Kinds that are not on this manager

- Sound. No FMOD code (0x70xxxx) calls either enqueue. FMOD streams on its own threads, see
  `THREADS_AND_CLOCKS.md`. [V] for the absence of calls.
- The FMV decoder. It has its own threads. Only the `Video.cgfx` shader goes through the manager
  (2b). [V]
- `FFX_LoadLocalizedBin`, `FFX_LoadLocalizedFtc`, `FFX_LoadLocalizedMenuBin`,
  `FFX_Btl_ReadKernelBin` and the other `FFX_File_Open` readers are synchronous on whatever
  thread calls them. They are on the worker only when a RegistTask calls them (A4). [V]

## 5. Threads, in one place

| step | worker | render |
|---|---|---|
| every enqueue, the ROM read callbacks, `FFX_Gfx_BeginMapLoad`'s +4501 = 1 | RegistTask bodies, LoadCluster loads and callbacks, the magic DLL load | `sub_658AE0` clears +4500 and +4501 at 0x658C89 and installs the scene (`VISUAL_CENSUS.md`) |

## 6. Corrections

- `THREADS_AND_CLOCKS.md` T7 says the request objects "are completed in place, and the step
  thread polls". That is true for the boot and the character pending record, and wrong in
  general. LoadCluster callbacks and RegistTask bodies write game state directly on the worker,
  `dword_112CA68` (H1), the magic texture flags (H3) and the magic DLL exports (H4) among them. A
  dated note is appended there.
- `VISUAL_CENSUS.md` lists the render clear at 0x658C89 as the place +4501 goes back to 0. It
  also goes to 0 on the worker, in `FFX_Gfx_LoadMapAssetHeader`, when `sub_667BB0` reports the
  header produced nothing to load. A dated note is appended there.

## 7. Proposed IDB names

One row each. The IDB was not touched.

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x112CA68 | `dword_112CA68` | `g_ffxBtlMapLoadPending` | set 1 in `FFX_Btl_StepSubPhase` case 8 before `FFX_Map_LoadMapAsync`, waited on in case 34, cleared by the map completion |
| 0x782FD0 | `sub_782FD0` | `FFX_Btl_MapLoadEnd` | one statement, clears the above, called from the last map cluster callback |
| 0x65A6C0 | `sub_65A6C0` | `FFX_Gfx_FinishMapClusterSet` | linkImports, sets +4500 or clears +216, calls the above |
| 0x659290 | `sub_659290` | `FFX_Gfx_BuildMapHeaderPath` | builds `/FFX_Data/GameData/PS3Data/<dir>[/2d]/<last>.ahwin32` |
| 0x667BB0 | `sub_667BB0` | `FFX_Gfx_ParseMapAssetHeader` | called by `FFX_Gfx_LoadMapAssetHeader`, non zero means nothing to enqueue |
| 0x643490 | `sub_643490` | `FFX_Gfx_SetBattleMapFlag` | copies gfx+4502 into +4503, then +4502 = (a1 != 0). Called with 1 before the battle map load |
| 0x664D80 | `sub_664D80` | `FFX_Magic_EnqueueTexList` | builds `magic/<name>/tex[_xx]/TexList.txt` and calls `sub_664D10` |
| 0x664D10 | `sub_664D10` | `FFX_AsyncLoad_EnqueueTexListTaskA` | RegistTask with `sub_657880` |
| 0x657880 | `sub_657880` | `FFX_AsyncLoad__texListJobA` | worker body, calls `sub_664550` |
| 0x664550 | `sub_664550` | `FFX_TexList_LoadAndEnqueueA` | parses a TexList, one LoadCluster per row, sets this+36 |
| 0x665F60 | `sub_665F60` | `FFX_AsyncLoad_EnqueueTexListTask` | RegistTask with `sub_6578B0`, kind argument 1, 3 or 4 |
| 0x6578B0 | `sub_6578B0` | `FFX_AsyncLoad__texListJob` | worker body, calls `sub_665770` |
| 0x665770 | `sub_665770` | `FFX_TexList_LoadAndEnqueue` | parses a TexList, one LoadCluster per row |
| 0x666880 | `sub_666880` | `FFX_Tex_OnTexListRowLoaded` | LoadCluster callback, writes +96[i], +60[i], +84[i] |
| 0x656AE0 | `sub_656AE0` | `FFX_Tex_RegisterSlot` | registers a loaded texture into the gfx+72 table |
| 0x6522B0 | `sub_6522B0` | `FFX_Magic_AreTexturesLoaded` | scans +96[], clears +36 when all are in |
| 0x6420B0 | `sub_6420B0` | `FFX_Magic_RequestTexList` | thunk to `sub_664D80` on g_ffxGfxCtx |
| 0x664F60 | `sub_664F60` | `FFX_Map_EnqueueFpTexList` | `%s%sfp/tex/TexList.txt`, guarded by gfx+37 |
| 0x663F00 | `sub_663F00` | `FFX_Ev_EnqueueTexList` | kind 4 call of `sub_665F60` |
| 0x874C50 | `sub_874C50` | `FFX_Ev_BuildTexListPath` | the event TexList builder, see `ASSET_PATHS.md` |
| 0x6FB130 | `sub_6FB130` | `FFX_SyncData__loadJob` | RegistTask body for SyncData |
| 0x6FAF90 | `sub_6FAF90` | `FFX_SyncData__parseMapData` | parses, then mgr+32 = 1 |
| 0x666610 | `sub_666610` | `FFX_Gfx_OnCutBufferLoaded` | writes gfx+64164 |
| 0x664440 | `sub_664440` | `FFX_Gfx_LoadCutBufferShader` | enqueues it |
| 0x694C20 | `sub_694C20` | `FFX_PostFx_OnShaderLoaded` | callback over `off_C34114` |
| 0x693D20 | `sub_693D20` | `FFX_PostFx_EnqueueShader` | enqueues it |
| 0x6C13E0 | `sub_6C13E0` | `FFX_Vfx_OnShaderLoaded` | callback from `FFX_Vfx_ParseShaderProperty` |
| 0x6DA460 | `sub_6DA460` | `FFX_Fmv_OnVideoShaderLoaded` | callback from `FFX_Fmv_CreatePlayer` |
| 0x6F9B70 | `sub_6F9B70` | `FFX_LoadDll_IsDone` | returns `dword_CE83C4 == 3` |
| 0x6F9B80 | `sub_6F9B80` | `FFX_LoadDll_IsFailed` | returns `dword_CE83C4 == 3 && dword_CE83C0 == 0`, done with no module |
| 0x6F9BA0 | `sub_6F9BA0` | `FFX_LoadDll_IsBusy` | returns `dword_CE83C4` is 1 or 2 |

## 8. Still open

- Whether any H1 to H6 write can land in the middle of a step on real hardware. The worker runs
  at above normal priority, so yes in principle. Nobody measured how often.
- `sub_797A40`, called right after the ambush roll in case 34. Its RNG use was not read.
- What `sub_63DF70` (dream filter, H6) changes. Read as visual only.

---

**Note, 2026-10-13** (BATTLE_START_AND_END.md 2.3). H1 does not always apply. Phase 7 goes
straight to phase 10 when the battle map equals the current scene id, skipping the map load (phases
8, 9, 34), so the advantage roll then lands on the step the battle field file completed. The monster
file chain (phase 13) is a further disk dependent point. The CTB counter draws run inside the last
monster file's completion, in `FFX_AsyncLoad_DispatchCompletions`. [V]
