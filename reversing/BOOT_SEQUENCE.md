# Boot sequence: from the CRT to the first FFX_MainStep

Written 2026-10-12 for Project Zanarkand item 1.2.8. Everything here was read from the FFX.exe
IDB (VA = RVA + 0x400000). Hex-Rays refused two load bearing functions,
`FFX_GraphicInitialize 0x6419D0` and `Phyre_Framework_Init 0x430DD0`, so those two were taken
from the disassembly instruction by instruction. The decompiles and disassembly dumps live in
the agent scratch directory under `agent-research/boot_*`, `boot2_*`, `gi_*`, `pfi_*` and
`maininit_*`.

The graphics device itself (D3D11 device, swap chain, Present, vsync, resize) is in
`RENDER_DEVICE.md` and is not re-derived here. Where the boot chain reaches it, this doc says so
and stops.

Companion docs: `MAIN_LOOP.md` (the frame once booted), `THREADS_AND_CLOCKS.md` (every thread
and every wall clock reader, written in the same pass), `DEBUG_MODE.md` (the `debug` argv
switch), `INPUT_LAYER.md`, `FMV_SYNC.md`.

## 0. The shape in one paragraph

The CRT constructs the single `FFXApplication` object statically. `WinMain` loads
`GameSetting.ini`, takes the single instance mutex, initialises Steam and hands control to
Phyre's `PApplication::run`. `run` initialises the Phyre framework (reflection tables, the
clock, the thread pool, which is sized to one and therefore inline), creates the window, and
calls `onInit`. `onInit` creates the D3D11 device, starts the Phyre render thread, then calls
the two virtual slots that are FFX's: `initApplication` (file system, save system, the whole
`FFX_GraphicInitialize` singleton pass, input, FMOD) and `initScene` (`FFX_BootSequence`).
`FFX_BootSequence` plays `OpeningScreen.swf` on the main thread while it posts
`FFX_ParseCommandLineAndInitDebug` as a job to a `JobWorker` thread. That job runs
`FFX_InitSubsystems1` and `FFX_MainInit`, which builds every PS2 side subsystem (chr, camera,
battle, ATEL, the RNG seeds) and raises map 23, the title. The main thread waits for the job
ring to drain, loads the pause menu swf, and returns from `onInit`. The first
`Phyre__PApplication__frameTick` then runs `FFXApplication::animate` and the first
`FFX_MainStep`.

The rest of this doc is that paragraph with addresses, sizes and globals, in execution order.
Numbers in bold in the margin are one running sequence so a recreation can follow it.

## 1. Static construction, before WinMain

**1.** CRT init entry `FFXApplication__staticInit 0xAE0510` calls `FFXApplication__ctor(g_ffxApplicationStorage)`, the `FFXApplication`
constructor, on the static object at `0xC8FB78`, and registers `sub_AF9BA0` with `atexit`.
`FFXApplication__ctor 0x42EF40` runs `Phyre__PApplication__ctor`, which writes `g_ffxApplication 0xCC9CD8 =
this`, then sets `FFXApplication::vftable 0xB0D998`, calls `sub_62CF50(101)`, and seeds the
window defaults `app+620 = 1280`, `app+624 = 720`, `app+652 = 2`. The vtable slots that matter
later: +0 dtor, +4 `FFXApplication__vf01_releaseDynamicMeshes`, +8 `initApplication`,
+C `initScene`, +10 `animate`, +14 `exitScene`, +18 `exitApplication`, +1C `update`,
+20 `render`, +24 `resize`, +28 `endFrame`, +2C `PApplication__vf11`.

There is exactly one application object and it is static. No other FFX singleton is
constructed before `WinMain`.

## 2. WinMain 0x62EE50

In order:

**2.** `SetUnhandledExceptionFilter(TopLevelExceptionFilter)`.

**3.** `FFX_CreateDocumentsFolders`, then the `GameSetting.ini` path under
`\Square Enix\FINAL FANTASY X&X-2 HD Remaster\`.

**4.** `FFX_Alloc(0x63C)` -> `FFX_Config_Construct 0x401880` -> `g_ffxGameSettings`, then
`FFX_Config_LoadFile`. 0x63C bytes. The fields read during boot: `settings[0]` UI language
(`sub_62CAD0` -> `SetThreadUILanguage`), `settings+4` height and `settings+6` width (packed
dword), `settings[8]` window mode (1 fullscreen, 2 borderless), `settings[40]` vsync,
`settings[44]` (-> `PRenderInterfaceD3D11__setAnisotropyLevel 0x58DE30`), `settings[52]` (-> `PRenderInterfaceD3D11__setTextureMipSkip 0x58E000` in `onInit`),
`settings+56`.

**5.** `CreateMutexW(L"9B8F7468-6DE0-4C5A-9209-89D363F8FCC9")`. `ERROR_ALREADY_EXISTS` shows
string 30773 and exits. One instance per machine, which a two instance local co-op test has
to patch.

**6.** `GetCommandLineW` -> `CommandLineToArgvW` -> narrow argv. This is the real process
command line (`DEBUG_MODE.md` has the proof). Usage string, when `argc > 1`, through
`Phyre__PApplication__parseCommandLine`: `-resolution= -msaa= -vsync -host= -script=`. The usage
string is not the flag list. The parser knows 13 flags (`-h -help -? -script= -log= -input=
-timestep= -resolution= -msaa= -vsync= -host= -fullscreen= -borderless=`), and `argv[1]` can also
be `debug`, `DEBUG` or `_ECalm`. `COMMAND_LINE.md` (2026-10-27) has every one with its reader.

**7.** `g_ffxTargetFrameTime = g_phyreFrameRateDivider / 59.94005994`. The limiter target.

**8.** `vtbl+4` (`FFXApplication__vf01_releaseDynamicMeshes`) on the application, then the
settings are copied into the app: +620 width, +624 height, +644 = `settings+56`, +648 =
`settings[40] != 0`, +664 = `settings[8] == 1`, and `g_phyreBorderlessWindow = settings[8]
== 2`. `PRenderInterfaceD3D11__setAnisotropyLevel(settings[44])`.

**9.** `FFX_SteamApiInit 0x623470`. Sets `g_ffxSteamApiReady`, which the save system reads at
step 21.

**10.** `Phyre__PApplication__run(argv, argc)`. Everything from section 3 to section 9 happens
inside this call. When it returns the message pump below is already over.

After `run`: the `PeekMessageA` pump with the `Sleep` limiter driven by
`Phyre_Time_NowSeconds() - g_phyreTimeOrigin` (`MAIN_LOOP.md` section 1), then teardown:
`app+292` vtbl+8, `Phyre_RenderThread_FlushGlobal 0x5BC3F0` (render thread flush), the log line "SKS: terminate result",
`Phyre_Framework_Terminate`, `SteamAPI_Shutdown`, free the settings.

## 3. Phyre__PApplication__run 0x629A40 and Phyre_Framework_Init 0x430DD0

**11.** `j_Phyre_SetWorkerThreadCount(1)` -> `Phyre_SetWorkerThreadCount 0x44CE10`: `g_phyreWorkerThreadCount = 1`. This is the Phyre worker thread
count, and it is one. Keep this number in mind for step 15.

**12.** Icon and drag globals, `SetThreadExecutionState(0x80000003)` (no sleep, no screen
off while running).

**13.** `Phyre_Framework_Init 0x430DD0`. Returns 9 on failure. From the disassembly, in order:

| order | call | what it is |
|---|---|---|
| 13a | `sub_467930`, `sub_45D600`, `sub_435610`, `sub_43E000` (PNamespace), `sub_43A1E0`, `sub_446490`, `sub_43B8B0` (isTypeOf), `sub_447DA0` (PClassDescriptor), `sub_43FFE0` (PCluster), `sub_442E30` (PWorld), `sub_44F120` (PEntity), `sub_44D880` (PComponent), `sub_4514A0` (PWorldMatrix), `sub_4537D0` (PCamera), `sub_454070` (PCameraProjection), `sub_456540` (FOV), `sub_4555E0` (PCameraOrthographic), `sub_4454A0` (PString), `sub_45C500` (PAssetReference), `sub_45C890`, `sub_4443F0`, `sub_44B2B0`, `sub_4580C0` (PInstancesComponent), `sub_45AA60` (FindFirstWithName), `sub_45E950` (GenerateRandomFloat01) | the Phyre reflection registrars. Each registers a class descriptor or a scripted function in the Phyre type system. Static tables, no allocation that matters. The names in brackets come from the string each one registers. |
| 13b | `EnsureInit` for PTimer, Vector2/3/4, Quat, Point3, Matrix3, Matrix4, PMatrix4, PMatrix4x3, then `sub_464400` | more type descriptors |
| 13c | `Phyre_Time_Init 0x42FE10` at 0x430EFF | the clock. `g_phyreTimerPeriodRaised = (timeBeginPeriod(5) == 0)`, `Sleep(0)`, `dword_C90044 = timeGetTime()`, `QueryPerformanceFrequency` -> `g_phyreSecondsPerCount = 1/freq`, `QueryPerformanceCounter` -> `dbl_C90058`, `dword_C90048`, `dbl_C90050`, and `g_phyrePreciseSystemTimeFn 0xC9004C = GetProcAddress(kernel32, "GetSystemTimePreciseAsFileTime")`. See `THREADS_AND_CLOCKS.md` part 2. |
| 13d | `g_phyreTimeOrigin 0xC90278 = Phyre_Time_NowSeconds()` at 0x430F0D | the app time origin every FFX clock subtracts |
| 13e | `sub_44CE20(0x40000)`, `sub_44CE00(0x40000)`, `sub_44CDF0(0x800)` | `dword_C0B954/8/C`, the pool sizing knobs |
| 13f | `Phyre_CreateDefaultThreadPool 0x44CD30` | the default thread pool: `g_phyreWorkerThreadCount > 1 ? sub_57D100 (PThreadPoolWorkStealing(g_phyreWorkerThreadCount, dword_C0B95C)) : sub_57CF50 (PThreadPoolImmediate)`, stored in `g_phyreDefaultThreadPool 0xC91BA8`. Because step 11 set the count to 1, the retail game builds `PThreadPoolImmediate`. No `PhyreEngineWorkerThread` is ever created. |
| 13g | `sub_439510` (basic types), PTypedObject EnsureInit, `sub_43E440`, `sub_43BE70` | more descriptors |
| 13h | `_atexit(Phyre_Framework_Terminate)`, `byte_C901A0 = 1` | done |

A stray helper at 0x430D40 (no IDA function) also runs `Phyre_Time_Init` and writes the origin,
with no callers found. It is not part of the boot.

**14.** `Phyre__PApplication__createWindowAndInit 0x629AB0`: `Phyre__PWindowWin32Base__create` (the window, `MAIN_LOOP.md` section 1
and `RENDER_DEVICE.md` for the swap chain that follows), the hosted window message
`PHYREMSG_SENDHWND`, then `Phyre__PApplication__onInit(g_ffxApplication, argc, argv)`, and
`onExit` if that fails.

**15.** (Reserved for the worker pool that is never created. `Phyre_GetWorkerThreadCount()` reads
`g_phyreWorkerThreadCount 0xC0B950` again inside `onInit` and skips `sub_5BBC30`, the per worker `PRendererForJobs`
constructor, for the same reason.)

## 4. Phyre__PApplication__onInit 0x62A600

**16.** `app+10 = Phyre_Time_NowSeconds() - g_phyreTimeOrigin` (start offset), the frame
callback `Phyre__PApplication__frameTick 0x627940` is installed at `app+616`, `app+15 =
g_phyreFixedTimeStepArg`.

**17.** Optional `-script` proxy (0x28 byte `PApplicationScriptProxy`, `Phyre__InputLog_RegisterCommands 0x62BB60`, callback
`Phyre__PApplication__scriptFrameTick 0x62C6F0`), the input log (fixed timestep 0.016659999, "INFO: Set fixed timestep") and the
input replay. None of these is active in a normal retail launch.

**18.** `sub_463D50`, then the D3D context once: `sub_4FEE70(&g_ffxPhyreD3DContext)`.
`FFX_Phyre_InitDisplay` (`GetClientRect` + `FFX_Phyre_CreateDeviceAndSwapChain`), "Error
initializing Phyre Render Interface" on failure. From here `RENDER_DEVICE.md` section 2 is the
reference, this doc does not repeat it. Then `PRenderInterfaceD3D11__setTextureMipSkip(settings[52])` and
`sub_443A40(app+177)`.

**19.** The Phyre render thread: `Phyre_RenderThread_Get()` returns the render thread singleton at
`unk_CC96F8` (lazily constructed by `Phyre_RenderThread_ctor 0x614010`), and `Phyre_RenderThread_Start 0x615140` starts it. Two semaphores
(16,16) and (1,1), 16 ring slot events, and a thread named `PhyreEngineRenderThread`, stack
0x10000, Phyre priority mode 1, procedure `Phyre_RenderThread_Proc 0x614710` -> `Phyre_RenderThread_Main 0x616010`. Error string "Error
initializing Phyre Render Thread with error". `this+12 = 1` marks it running. This thread
exists in retail and `IDXGISwapChain::Present` runs on it. See `THREADS_AND_CLOCKS.md`
part 1, thread T1.

**20.** `Phyre__PInput__createDirectInput8`.

**21.** vtbl+8 `FFXApplication__initApplication(argc, argv)`. Section 5.

**22.** Two Phyre scratch allocations: `PhyreMallocAligned(2621440, 16)` -> `app+181` with
`sub_5B8DA0(.., 0x200000, 0x80000)`, and `PhyreMallocAligned(0x80000, 16)` -> `app+182` with
`sub_5CD010`. Then `sub_629840`.

**23.** `Phyre_GetWorkerThreadCount()` == 1 so `sub_5BBC30` (`PRendererForJobs` per worker) is skipped.
`sub_60ED70("MainThread", 0x2000)` and `sub_60ED40(app+3)` name the profile timeline.

**24.** `sub_628DF0(app)`: the application clock reset (`+9` dt, `+8` total, `+72` mirror).

**25.** `Phyre__PInputMapper__bindMouseAndKeyboards`.

**26.** vtbl+12 `FFXApplication__initScene` -> `FFX_BootSequence`. Section 7.

`onInit` returns after `FFX_BootSequence` returns, so everything up to and including the
title event being resident happens before the first `frameTick`.

## 5. FFXApplication__initApplication 0x42F7F0

argv is stored at `app+932 (0x3A4)`, argc at `app+936 (0x3A8)`. Log line "SKS:
initApplication begin". The window title is "FINAL FANTASY X", or "FINAL FANTASY X ETERNAL
CALM" when `argv[1] == "_ECalm"`. `app+944 = 0`.

**27.** `FFX_InitFileSystem 0x6795B0`: `FFX_fios_setDataRoot("../../..")`,
`FFX_VbfManager__createSingleton` (68 bytes -> `g_ffxVbfManager`), root `../../../`, mount
`data\FFX_Data.vbf` into mount slot 0 (failure -> MessageBox string 30774, then WM_CLOSE to the
main window through `FFX_App_SendCloseToMainWindow 0x42F4D0`, and the boot carries on with no
archive, see "The mount slots" in `VBF_FORMAT.md`), `sub_6DAFC0` (the language manager
at `dword_CDED58`), the title id string PCSG00219 / PCSE00293 / PCSB00395 / PCSH00042 by
region, `sub_6F9B60`. Log line "SKS: fiosInitialize finished".

**28.** Log line "SKS: graphicInitialize finished". Yes, this is printed BEFORE
`FFX_GraphicInitialize` runs (the log order is wrong in the shipped binary). Do not use that
line as a boot milestone.

**29.** `sub_646910`: the `ffxsjistbl_*.bin` text encoding tables.

**30.** `FFX_SaveSys_Init 0x647FE0`: `FFX_SaveMgr_Create 0x6F1C10`, `FFX_SaveFile_EnsureSaveFolder 0x6F0330` (the save folder),
`FFX_Ach_Create 0x6F1430` only when `g_ffxSteamApiReady` (the 0x50 byte `StatsAndAchievements` object at
`g_ffxStatsAndAchievements 0xCE81A8`, the Steam achievement client, not a cloud object, see `STEAM_ACHIEVEMENTS.md`), `FFX_SaveUtil_AllocState` (3 bytes at `g_ffxSaveUtilState`), `FFX_SaveMgr_Get 0x6F2070`.

**31.** `FFX_SaveFile_RegisterStagingBuffer 0x647FB0`: the 26880 byte save staging buffer.

**32.** `FFX_GraphicInitialize 0x6419D0`. Section 6, steps 33 to 79.

**80.** `FFX_Input__get` (constructs the `FFX_Input` singleton on first call, `FFX_Input__ctor`,
`INPUT_LAYER.md`) then `FFX_Input__buildActionMap`.

**81.** If `!FFX_SaveUtil_FrameHeld()`: `sub_679FC0` -> `FmodManager__createSingleton 0x6FA0B0`: the `FmodManager` object
(0x50 bytes at `g_ffxFmodManager 0xCE9010`, constructed by `FmodManager__ctor 0x6F9DE0`, which creates the `FmodManager`
thread, `THREADS_AND_CLOCKS.md` T4), and `byte_C8FAE8 = 1`. Then `FFX_SyncData_CreateAtInit 0x67A960` (`FFX_Alloc` +
`FFX_SyncData__ctor 0x6FA8B0`).

## 6. FFX_GraphicInitialize 0x6419D0, the singleton pass

Log line "SKS: graphicInitialize begin!" first and "SKS: graphicInitialize end!" last. Every
entry below is a guarded create (`if (global == 0) global = ctor(FFX_Alloc(size))`) unless it
says otherwise, and the guard means a second call is a no-op. Sizes are the `FFX_Alloc`
argument.

2026-10-28: every row is now identified and named in the IDB (`tools/apply_singleton_names.py`,
111 names). None of these classes has a vtable or RTTI. Five carry their name in a "not created
yet" or similar string, the rest are named by what reads them. Step 72 is the one create with no
guard. Steps 36 and 73 are one byte objects whose only live calls are no-ops. The names in this
document were brought up to date with the IDB on the same day, the facts did not change except
where a row says so.

| step | ctor / create fn | size | global | identity, dependencies |
|---|---|---|---|---|
| 33 | `FFX_MemReport_CreateSingleton 0x6DC800` | 656 (Phyre `PhyreMallocAligned 0x42FB80`, align 4) | `g_ffxMemReport 0xCDEDE8` | the memory report manager: prints "RAM=%s - VRAM=%s - HOST=%s - SRAM=%s - FMOD=%s". Also sets `g_ffxMemTrackMinSize = 1`. |
| 34 | `FFX_Alloc(0x238)` -> `JobSchedule__ctor 0x6DB670` | 0x238 | `g_jobSchedule 0xCDEDE4` | **JobSchedule**. The ctor builds the 8 slot job ring (`JobSchedule__initRings 0x6DB560`: 8 x 48 byte slots, semaphores at +107, +119, +120, +121). |
| 35 | `JobSchedule__startWorkers(g_jobSchedule)` | | | **starts the four JobWorker threads**: two semaphores (8,8), 8 free slots, `JobWorker__start(this+20*i, 0, 0x20000)` x4, priority 0. These are the threads `FFX_MainInit` runs on (step 86) and the threads `FFX_Ch_DispatchUpdateJob` uses every step. `THREADS_AND_CLOCKS.md` T5. Depends on 34. |
| 36 | `maybe_FFX_HeapFacade_CreateSingleton 0x6E56A0` | 1 | `maybe_g_ffxHeapFacade 0xCE63C4` | 2026-10-28: an inert one byte object, class name a guess. Its only live use is `FFX_FmvMgr_ProcessCommands` passing it as `this` to `maybe_FFX_HeapFacade_OnFmvDynGeoChange 0x6E5740`, which is `retn 4`, right after freeing DynGeoMem set 0 (0x6D8E91) and right before rebuilding it (0x6D8E2E) [V]. The code after the create (0x6E56F0, 0x6E5710, 0x6E5730 forward to `FFX_MemAllocAligned`, 0x6E5760 and 0x6E5780 to `FFX_MemFree`) and the getter 0x635A30 have no reference anywhere [M]. Main thread. Live pointer, does nothing. |
| 37 | `FFX_DynGeoMem_CreateSingleton 0x6DEB90` | 0x10 + `CreateMutexW` + 2 x 5 MB | `g_ffxDynGeoMem 0xCDEDF0` | 2026-10-28: **the DynGeoMem manager** (the IDB comments already called it that). `{+0 current set, +4 set 0, +8 set 1, +12 mutex}`. The create runs `FFX_DynGeoMem_BuildSet(0)`, two segments of `dword_B4A650[0]` = 0x500000 bytes from `FFX_Malloc`, each with a mutex, and selects it [V]. So this step allocates 10 MB. Readers: `FFX_DynGeoMem_RewindCurrentSegment` every `FFX_MainStepLoop`, `FFX_DynGeoMem_AllocModelPart` (71 callers), `FFX_Sprite2D_Draw`, `FFX_Glyph2D_Draw`, `FFX_Vfx_SetModelParts`, `FFX_DynGeoMem_FlipFrameSegment` per present, and the FMV command list, which frees set 0 during a movie and rebuilds it after [V]. Main thread, the chr update jobs fill what the step allocated (`VISUAL_CENSUS.md`) [I]. Live. |
| 38 | `ClassCharacterTable__create 0x69F440` | 8 | `g_classCharacterTable` | the class character table |
| 39 | `FFX_ClusterCache_CreateSingleton 0x69A060` -> `FFX_ClusterCache_ctor 0x699AB0` | 0x6420 | `g_ffxClusterCache 0xCCCA4C` | 2026-10-28: **the cluster cache**, the refcounted, path keyed cache of every loaded `PCluster` (shaders, textures, fonts, character and map clusters) behind `FFX_ClusterCache_Load 0x69B940`. +0 delayed release state (2 at boot), +4 its timer, +16 the map, +24 a mutex [V]. `FFX_ClusterCache_StepDelayRelease 0x69C010` releases after 3 s in state 1 and logs a time out after 12 s in state 0 [V]. `FFX_ClusterCache_Get 0x69A1A0` has 138 call sites. Main thread, and the `AsyncLoadManager` worker through `LoadClusterRequest__execute`, under the mutex [V]. Live. |
| 40 | `AsyncControllerTaskManager__createSingleton 0x68DB00` | 0x44 | `g_ffxAsyncControllerTaskManager 0xCCC860` | the input thread manager (`AsyncControllerTaskManager`), creates the `FFX_InputThread__main` thread. `THREADS_AND_CLOCKS.md` T6. |
| 41 | `AsyncLoadManager__create` -> `AsyncLoadManager__ctor` | 0x4C | `g_asyncLoadManager` | mutex, the 200 entry `LoadClusterRequest` pool `dword_CCC890`, the 150 entry `RegistTaskRequest` pool `dword_CCC894` with its 0x12C0 arena, then `AsyncLoadManager_startWorkerThread`. `THREADS_AND_CLOCKS.md` T7. |
| 42 | `FFX_PoolSet_createSingleton 0x69F4C0` -> `sub_69E140` | 0x50 | `g_ffxPoolSet 0xCCCA6C` | the pool set "data pool/vfxdg pool/segment pool/name pool" |
| 43 | `FFX_PostProcessMgr_CreateSingleton 0x692A90` -> `FFX_PostProcessMgr_ctor 0x692020` | 0x1240C | `g_ffxPostProcessMgr 0xCCC8A0` | 2026-10-28: **the post-process target manager** that `RENDER_PASSES.md` 2 and `SCREENS_AND_FADES.md` already describe by offset (also gfx+4300): the scene targets, two `PCluster`s (+60, +74548), the fade state and the glow and fog values the PostProcess page edits [V]. Readers through `FFX_PostProcessMgr_Get 0x692BE0` (65 sites): the `FFX_ScreenFade_*` API at step, the presenter and every post effect render [V]. Main thread. Live. |
| 44 | `FFX_MaterialParamAnimator_CreateSingleton 0x6ABE40` | 0x28 | `g_ffxMaterialParamAnimator 0xCCCA90` | 2026-10-28: **the material parameter animator** (the name `VISUAL_CENSUS.md` proposed). Four `PArray`s of `{material, weight}` rows and a time accumulator at +32, ctor inline [V]. Step writers: `FFX_Map_StepAnims` -> `sub_914A10` and ATEL Core 417 to 420 -> `sub_90A4D0`, both into `FFX_MaterialParamAnimator_SetWeights 0x6ABFC0` [V]. Applied per present by `FFX_MaterialParamAnimator_ApplyAll 0x6ACD20` as MorphWeight, UVMorphWeight, UvOffsetX/Y, VertexColorMorphWeight [V, VISUAL_CENSUS]. Main thread. Live. |
| 45 | `if (g_renderDelegationHelper == 0) FFX_Alloc(0x208)` -> `RenderDelegationHelper_ctor 0x6AD0D0` | 0x208 | `g_renderDelegationHelper 0xCCCA98` | 2026-10-28: **RenderDelegationHelper**, the phase masked callback registry of `DISPLAY_LIST.md` 6 (the name is in its own CAS failure string). The guarded create is inlined here (0x641A42), the out of line copy `RenderDelegationHelper_CreateSingleton_dead 0x6357F0` and its getter 0x635A20 have no reference anywhere [M]. Registrations at step, phase runners at present and in `FFX_Frame_UpdateBoostersAndOverlays`. Main thread. Live. |
| 46 | `FFX_ChrLight_CreateSingleton 0x6A6CD0` -> `FFX_ChrLight_ctor 0x6A63F0` | 0x234 | `g_ffxChrLight 0xCCCA84` | 2026-10-28: already named before this pass, the row said unnamed. **The character light manager**, `CHR_CLUSTER.md` 5. Main thread. Live. |
| 47 | `CharacterLightingTweakUtil__createSingleton 0x6A6D50` -> `CharacterLightingTweakUtil__ctor 0x6A6610` | 0xFC | `g_characterLightingTweakUtil 0xCCCA88` | 2026-10-28: **the CharacterLighting debug page**, DebugMenuManager mode 0, sibling of PostProcessTweakUtil. Its draw `CharacterLightingTweakUtil__draw 0x6A9F00` prints "CharacterLighting: %.2f fps" [V]. The page never opens (`DEBUG_MODE.md`), but the object is not idle: `CharacterLightingTweakUtil__initLabels 0x6A91E0` runs on every boot from `sub_657A50` in `FFX_Boot_FirstAsyncLoad`, and because `FFX_DebugText_LoadFont` always returns 0 (`xor eax, eax` at 0x6F63AB) it sets +0 = 1 at 0x6A93AB [V]. The ctor already set +201 = 1 (word 0x100 at +200, 0x6A6640) [V]. So in retail `FFX_ChrLight_(Un)RegisterMeshInstance` keep its 100 row mesh table at +248, the shade coefficient setters write rows, `FFX_Gfx_ApplyCharacterLights` mirrors the four light intensities into +204..+216, and `FFX_ChrLight_SetLightColour` pushes the mirror back into `g_ffxChrLight` through `sub_6AA260` when they differ by more than 0.01 [V]. Since the only writers keep both in step, that push is a no-op in practice [I]. Main thread. Live bookkeeping, dead page. |
| 48 | `ViewerManager__createSingleton 0x6B60C0` -> `ViewerManager__ctor` | 0xB8 | `g_debugInfoState` | **ViewerManager** (`DEBUG_MODE.md`) |
| 49 | `PostProcessTweakUtil__createSingleton 0x692B10` | 0x190 | `g_postProcessTweakUtil 0xCCC89C` | **PostProcessTweakUtil** |
| 50 | `DebugMenuManager__createSingleton` | 8 | `g_debugMenuManager 0xCCCAB0` | **DebugMenuManager**, mode 10 |
| 51 | `AutoTestManager__createSingleton` | 300 | `g_autoTestManager 0xCCCB00` | **AutoTestManager**. The ctor creates the stdin "Read command" thread. `THREADS_AND_CLOCKS.md` T8. |
| 52 | `TextureAnimationManager__createSingleton 0x6B00A0` | 0x84 | `g_textureAnimationManager 0xCCCAA8` | **TextureAnimationManager** |
| 53 | `FFX_VfxShaderTable_CreateSingleton 0x6C1420` -> `FFX_VfxShaderTable_ctor 0x6BF4A0` | 0x9C | `g_ffxVfxShaderTable 0xCDEB80` | 2026-10-28: **the VFX shader table**, three `PMap`s. Filled by `FFX_Vfx_LoadShaderManifest 0x6C0FC0` from `FFX_Boot_FirstAsyncLoad` on the `AsyncLoadManager` worker [V], read by `FFX_VfxShaderTable_Find 0x6C06E0` when VFX dynamic geometry builds its materials (`sub_6A4240`, `sub_6A4800`, `sub_6A48D0`) [V]. The calls from `FFX_Map_RequestParticleTextures` and `sub_642090` only reach no-ops (`nullsub_204`, `nullsub_67`) [V]. Main thread after boot. Live. Not a post effect, so the section 12 guess was wrong for this one. |
| 54 | `Distortion__createSingleton 0x6C2190` -> `Distortion__ctor 0x6C2010` | 0x80 | `g_ffxDistortion 0xCDEB88` | 2026-10-28: **Distortion**, named by "Distortion singleton is not created yet." in `FFX_Distortion_GetSingleton 0x6C2290` [V]. The screen wave grid, `DYNAMIC_MESHES.md` 4. Step writers (strength, enable, skip) through `sub_642F30`, `sub_642F50`, `FFX_Distortion_SetSkipNextDraw`, render in `FFX_Frame_RenderScene` [V, VISUAL_CENSUS]. Main thread. Live. |
| 55 | `BlurEffect__createSingleton 0x6C34D0` | 0x88 | `g_blurEffect 0xCDEBA0` | **BlurEffect** |
| 56 | `BlurEffectEx__createSingleton 0x6C49A0` | 0x70 | `g_blurEffectEx 0xCDEBFC` | **BlurEffectEx**, needs 55 |
| 57 | `FFX_ButtonPrompt_CreateSingleton 0x6E76C0` -> `FFX_ButtonPrompt_ctor 0x6E6330` | 0x168 | `g_ffxButtonPrompt 0xCE6DC0` | 2026-10-28: **the PC button prompt substitution table** that `FFX_ButtonPrompt_Substitute 0x6E69E0` runs from `FFX_Sprite2D_Draw` (`MENU_DRAW_PATH.md` 5.1) [V]. The ctor fills the key and pad cell maps, +352/+356 is a prompt scale that `FFX_MesWin_ApplyControlCode` and `FFX_BtlOd_DrawAuronSeq` set and reset (`FFX_ButtonPrompt_SetScale 0x642EC0`, `FFX_ButtonPrompt_ResetScale 0x642C60`) [V]. Step time draw paths, main thread. Live. Not an effect. |
| 58 | `MotionBlur__createSingleton 0x6C5030` -> `MotionBlur__ctor 0x6C4EC0` | 0x8C | `g_ffxMotionBlur 0xCDEC04` | 2026-10-28: **MotionBlur**, named by "MotionBlur singleton is not created yet." in `MotionBlur__getSingleton 0x6C5130` [V]. BlendFrame.cgfx. Step request from `FFX_Sg_SetMotionBlur` -> `sub_643730`, render consumes +88/+89 [V, VISUAL_CENSUS]. Main thread. Live. |
| 59 | `FFX_SpecularPass_CreateSingleton 0x6C5790` -> `FFX_SpecularPass_ctor 0x6C5670` | 0x5B4 | `g_ffxSpecularPass 0xCDEC1C` | 2026-10-28: **the specular character pass**, four 356 byte slots at +16 (pass command 4, `DISPLAY_LIST.md`). Writers `FFX_Gfx_QueueSpecularPass*` and `FFX_Gfx_SetSpecularMapParams`, readers `FFX_GfxCtx_ExecPassCommand` and `FFX_Frame_RenderScene` through `FFX_SpecularPass_Get 0x6C57E0` [V]. Main thread. Live. |
| 60 | `ShadowManager__createSingleton 0x6C9500` -> `ShadowManager__ctor 0x6C7D80` | 0x4068 | `g_shadowManager 0xCDEC28` | 2026-10-28: **ShadowManager**, named by its render target strings "ShadowManager_TempShadowMaptarget" and friends (`sub_6D18B0`) [V]. The character shadow atlas of `DYNAMIC_MESHES.md` 7. Resources built from `FFX_Boot_FirstAsyncLoad` on the async worker (`sub_6CE100`), then `ShadowManager__get 0x6D2390` (43 sites) at step and present, ATEL Core 606 included [V]. Nothing frees it [M]. Main thread after boot. Live. |
| 61 | `BrokenScreenEffect__createSingleton 0x6D4D30` -> `BrokenScreenEffect__ctor 0x6D4BA0` | 0x78 | `g_brokenScreenEffect 0xCDEC98` | 2026-10-28: **BrokenScreenEffect**, named by "BrokenScreenEffect singleton is not created yet." in `BrokenScreenEffect__getSingleton 0x6D4DE0` [V]. The glass shard effect, `DYNAMIC_MESHES.md` 6. Step setters `sub_639360`..`sub_639540`, render at 0x66898B [V, VISUAL_CENSUS]. Main thread. Live. |
| 62 | `PhyFMVPlayerManager__createSingleton 0x6D7130` -> `FFX_Fmv_Manager_Construct` | 0x750 | `g_ffxFmvPlayerManager` | **PhyFMVPlayerManager** (`FMV_SYNC.md`). The two video threads are created per movie, not here. |
| 63 | `sub_6B5EB0` -> `sub_6B55D0` | 0xF4 | `g_ffxDebugPageCharSwitching` | debug page |
| 64 | `sub_6B6040` -> `sub_6B57D0` | 0x214 | `g_ffxDebugPageMapSwitching` | debug page |
| 65 | `sub_6B5F30` | 0xF0 | (CharTexAnim page) | debug page |
| 66 | `sub_6B5E30` | 0xE0 | (CharAnim page) | debug page |
| 67 | `sub_6B5CB0` -> `sub_6B52A0` | 0x120 | `g_ffxDebugPageBasicInfo` | debug page |
| 68 | `sub_6B5DB0` | 0x1E4 | (BattleInfo page) | debug page |
| 69 | `sub_6B5D30` | 0x11C | (BattleCharParams page) | debug page |
| 70 | `sub_6B5FC0` | 0x108 | (GameParams page) | debug page |
| 71 | `FFX_FogMgr_CreateSingleton 0x6E3240` | 0xB8 | `g_ffxFogMgr 0xCE5170` | 2026-10-28: **the fog manager** (`FFX_FogMgr__get 0x6E32C0`, `TEXTURE_ANIMATION_AND_POST_TWEAKS.md`). Ctor inline. Step writers `FFX_Fog_SetColor/Curve/Far/Limit/Near` from MapFx, install at render by `FFX_Gfx_InstallFogFromGlobalCamera` [V]. Main thread. Live. |
| 72 | `FFX_Alloc(0x44)` -> `FFX_LoadingIndicator_ctor 0x6E45E0` | 0x44 | `g_ffxLoadingIndicator 0xCE517C` | 2026-10-28: **the loading indicator**, the spinner `FFX_LoadingIndicator_Step` already drives. `FFX_LoadingIndicator_LoadTextures 0x6E4770` loads `menu/loading/ld00`..`ld19` and `loadingbg` at step 75 [V]. +0 drawing, +1 requested, +4 spinner clock, +8 delay timer, +16 state 0..3, +20 alpha [V]. **Not guarded**, unlike every other row: no compare before the `FFX_Alloc` at 0x641AEE, so a second call would leak the first [V]. Shown by `FFX_LoadingIndicator_SetVisible 0x643500` (ATEL Core 607, `app+928`), drawn by `FFX_Boot_PresentFrame` and `FFX_Frame_RenderScene` [V]. Main thread. Live. |
| 73 | `FFX_FrameTimeMonitor_CreateSingleton 0x6E5490` | 1 | `g_ffxFrameTimeMonitor 0xCE5194` | 2026-10-28: an inert one byte object. Its one live call is `FFX_FrameTimeMonitor_Init_nop 0x6E55E0` (`retn`) from `FFX_Boot_FirstAsyncLoad` 0x6364E0, and teardown calls `FFX_FrameTimeMonitor_Destroy_nop 0x6E54B0`, so the byte is never freed [V]. The neighbouring code that would sample frame times through renderer callbacks and set a verdict `dword_CE5198` (-1 over 26 and 40 ms, 1 under 14 ms) has no reference anywhere, and nothing reads the verdict [M]. Named for that orphan code. Dead in effect. |
| 74 | `FFX_Alloc(0x10DE8)` -> `FFX_GfxCtx_ctor 0x64DC00` | 0x10DE8 (69096) | `g_ffxGfxCtx` | **the FFX graphics context**, the object every present and draw path takes as `this` |
| 75 | `sub_6656E0(g_ffxGfxCtx)` | | | post-construction setup of 74: `FFX_Gfx_FitViewTo16by9`, `sub_6DC8A0`, `sub_6DDBF0`, `Phyre__getApplication`, then `FFX_PostProcessMgr_Get`, `sub_693BE0`, `sub_6985B0`, `sub_6983F0`, `PostProcessTweakUtil__get`, `PostProcessTweakUtil__initLabels`, `sub_664440`, `FFX_LoadingIndicator_LoadTextures`, `sub_6935F0`. Depends on 33, 43, 72, 74. `TEXTURE_ANIMATION_AND_POST_TWEAKS.md` proposes `FFX_Gfx_InitPostProcess` for `sub_6656E0`, not applied in the IDB. |
| 76 | `FFX_Btl_OpenMonsterArchives` | | | opens the monster archives |
| 77 | `FFX_MemAllocAligned(0x100000, 16)` | 1 MB | `g_ffxClothClonePoolBase 0xCCB458` | the 1 MB block of the cloth clone pool (`MEMORY_ARENAS.md` 2.4) |
| 78 | `FFX_Dl_CreateMspaceWithBase(g_ffxClothClonePoolBase, 0x100000, 1)` | | `g_ffxClothClonePool 0xCCB454` | a dlmalloc mspace over that block, with locking (`MEMORY_ARENAS.md` 2.4). Its magic comes from `FFX_Dl_InitMparams 0x6EA050`, which reads `GetTickCount` (`THREADS_AND_CLOCKS.md` part 2, row C1). `dword_CCB45C = 1`. |
| 79 | log "SKS: graphicInitialize end!" | | | |

Ordering constraints a recreation must keep: 35 after 34 (the ring before the workers), 56
after 55, 75 after 74 and 33, 78 after 77, and 40 and 41 both before any load request is made
(step 82 is the first). Nothing in this pass touches the PS2 side globals, those are all
`FFX_MainInit`.

### 6.1 What the recreation needs from the boot singletons, 2026-10-28

Every singleton the boot constructs, sections 1 to 6, sorted by what a recreation has to do with
it. "Engine" means the state is read or written by the step and so belongs under the step hash or
at least in the snapshot. "Renderer" means the recreation's own renderer replaces it and only its
behaviour has to match. "No" means nothing observable depends on it. [I] throughout, the evidence
is in the rows above and the docs they cite.

| verdict | steps |
|---|---|
| engine, port it | 1 application object (as the app layer), 4 settings, 27 VBF manager and language manager, 30 save manager, save util state and `StatsAndAchievements` (PC layer), 31 save staging buffer, 38 `ClassCharacterTable`, 41 `AsyncLoadManager` (as a seam, its completion timing is a lockstep hazard, `ASYNC_LOAD_REQUESTS.md` 0), 46 `g_ffxChrLight`, 62 FMV manager, 76 monster archives, 80 `FFX_Input`, 81 FMOD manager and sync data |
| engine half, renderer half | 39 cluster cache (the asset layer needs a refcounted path cache, its 3 s and 12 s delayed release decides when clusters unload and so feeds load timing), 43 post-process manager (the fade colour, lock and hold mode at +0x1238C..+0x123A8 are read back by `FFX_MainStep`, `READBACK_CENSUS.md` 2.1, the targets are renderer), 44 material animator (the weights are step state for the snapshot, the apply is renderer), 54 Distortion, 58 MotionBlur, 61 BrokenScreenEffect and 71 fog (step requests and values for the snapshot, the draw is renderer), 72 loading indicator (the show requests come from the step, ATEL Core 607 and `FFX_App_ShowLoadingIndicator`, through `app+928`, the spinner is renderer) |
| renderer only | 45 RenderDelegationHelper (becomes the typed display list, keep the phase order), 52 TextureAnimationManager, 53 VFX shader table, 55 BlurEffect, 56 BlurEffectEx, 57 button prompts (16, PC layer), 59 specular pass, 60 ShadowManager (its gradient meshes take `rand()` at init, `DYNAMIC_MESHES.md` 7, the recreation must own that draw), 74 and 75 the graphics context and its setup, 19 the Phyre render thread (D3D12 has its own) |
| ordering contract only | 34 and 35 JobSchedule and the four JobWorkers: the recreation already runs `FFX_MainInit` on the step thread (section 11) and can do the same with the chr update jobs, what has to survive is "every job of this step is done before the present" (`FFX_Frame_WaitJobSchedule`) |
| no | 33 memory report, 36 heap facade (inert), 37 DynGeoMem (the 2 x 5 MB per step vertex scratch is the original's step to render hand off, the snapshot replaces it), 40 input thread (the recreation polls on the step), 42 pool set (VFX geometry pools behind 37), 47 CharacterLighting page (its retail bookkeeping only mirrors `g_ffxChrLight`), 73 frame time monitor (inert), 77 and 78 the cloth clone mspace (an allocator detail) |
| optional, debug surface | 48 ViewerManager, 49 PostProcessTweakUtil, 50 DebugMenuManager, 51 AutoTestManager and its stdin thread, 63 to 70 the debug pages (PROGRESS 3.6 and 6.4.3) |

Two order constraints follow from the identities and were not in the list above: 39 before 75
and before step 82, because `FFX_LoadingIndicator_LoadTextures` at 75 and every boot load go
through the cluster cache, and 72 before 75 for the same call. Both already hold in the original
order.

## 7. FFXApplication__initScene 0x42F8E0 -> FFX_BootSequence 0x6416C0

`initScene` is a thin wrapper, `FFX_BootSequence` is the body. It runs on the main thread,
inside `onInit`, and this is where boot forks onto a worker thread.

**82.** `AsyncLoadManager__enqueue(FFX_Boot_FirstAsyncLoad)`: the first async load. `FFX_Boot_FirstAsyncLoad 0x636430` registers
the dynamic mesh classes, calls `FFX_Gfx_ApplyPostOptions(g_ffxGfxCtx)`, `FFX_Fmv_ParseVideoList`, and
builds the keyboard and pad icon object at `g_ffxGfxCtx+69088`.

**83.** `sub_6E96C0`: the "Loc_Texture_Saveload_Attention_Before_Game" texture (string 505),
the autosave warning. `sub_A26240` and `sub_A262E0(app+292, w, h)`: the Iggy (Scaleform
style) UI runtime. `app+928 = 0`. `FFX_LoadingIndicator_SetParams(g_ffxLoadingIndicator, &3.0, null,
&0.0)` at 0x64175E: the loading indicator's spinner clock +4 = 3.0 and alpha +20 = 0.0, state +16
untouched (the middle argument is a null pointer, 0x641758) [V]. Neither a fade nor a timer of its own.

**84.** Not `_ECalm`: `FFX_Boot_LoadOpeningScreenSwf 0x6579D0` loads `OpeningScreen.swf` (its "Finish" callback is
`sub_652400`, which sets `byte_CCB9C2`). Then the loop `FFX_Boot_PresentOpeningFrame 0x660400`: `Phyre__PInput__update`,
`FFX_Boot_PresentBlankFrame(g_ffxGfxCtx)` (present one frame then `Sleep(0x21)`), and once
`AsyncLoadManager__isIdle` is true:

**85.** `JobSchedule__postJob(ecx = g_jobSchedule, FFX_ParseCommandLineAndInitDebug_tramp 0x636410,
&v17, argc, argv, 0)`. `JobSchedule__postJob 0x633670` is the JobSchedule poster. The disassembly at 0x641830
and 0x64193A shows `mov ecx, g_jobSchedule` before the call, so the receiver is the
JobSchedule of step 34, and the trampoline runs on one of the four `JobWorker` threads of
step 35. `DEBUG_MODE.md` calls this a callback. It is a job on another thread, see the
corrections list in the hand-back.

**86.** On the JobWorker thread: `FFX_ParseCommandLineAndInitDebug 0x822490`. Sets
`g_ffxDebugMode` from `argv[1] == "debug" / "DEBUG"`, calls `FFX_SgMem_Init 0x83C200` (the PS2 SG heap,
`g_ffxSgHeapBlock 0x1301960 = FFX_MemAllocAligned(0x1100000, 16)`, which nothing allocates from, `MEMORY_ARENAS.md` 2.2), `FFX_InitSubsystems1(argc, argv)`,
`FFX_MainInit`, returns 1 into `v17`. Section 8.

Meanwhile the main thread keeps presenting the opening swf. `sub_65B750`, `app+928 = 1`.
The `_ECalm` path instead shows 30 frames through `FFX_Boot_PresentFrame 0x66B1E0`.

**87.** Wait for `FFX_SaveUtil` ready, wait for `AsyncLoadManager__isIdle` (abort byte
`byte_CCC7BC`), `app+929 = 1`, `app+928 = 0`, and a late post of step 85 if the opening loop
did not get to it. Then `while (!JobSchedule__allSlotsFree(g_jobSchedule, 0)) FFX_Boot_PresentFrame(g_ffxGfxCtx);`
which presents frames until all 8 job ring slots are free, which is when `FFX_MainInit` has
returned. `app+929 = 0`.

**88.** `v17 == 1 ? FFX_EscMenu_LoadMovie(g_ffxGfxCtx, ...) : return 9`. Despite the name
this loads `pausemenu.swf` and `escmenu.swf`, the pause overlay. `FFX_BootSequence` returns,
`initScene` returns, `onInit` returns, `run` enters the pump.

## 8. FFX_InitSubsystems1 0x820310 and FFX_MainInit 0x820860, on the JobWorker thread

`FFX_InitSubsystems1` is tiny: `maybe_FFX_Player__setPadLocked(0)`, `byte_12FB8FA = -1`,
`g_ffxDbgUseVfxCameraMatrices = 0`, `g_ffxDbgUseSugimotoUser = 0`, and in debug mode with `argc > 1`
it parses `argv[1]` through `FFX_Dbg_SetUserName 0x821D30`. It constructs nothing. The name in PROGRESS 1.2.8
ordering (`FFX_MainInit -> FFX_InitSubsystems1`) is backwards, `InitSubsystems1` runs first.

`FFX_MainInit` is the PS2 side `main()` prologue. In order, every call that does something:

| step | call | effect |
|---|---|---|
| 89 | `FFX_Frame_SetRateDivider(2)` | 30 Hz step until the title script changes it |
| 90 | `FFX_Vfx_PrimBuildInitBuckets 0x728750` | `FFX_Malloc(0x2C6B8)`, the yonishi VFX primitive buckets (`FFX_Vfx_ResetPrimBuildQueue` zeroes their counts each step) |
| 91 | `MapFx__f72AC30 0x72AC30` | small VFX table init |
| 92 | `sub_62F700(1)`, `sub_62F6E0(2/0/1)` -> `dword_23CBBF4/BFC/BF8`, `sub_62FB80(0)` | PS2 stubs, all return 0 |
| 93 | `FFX_Ps2_ReadStatStub`, `FFX_SetMovieScreenMode(0)`, `FFX_Gs_SetupFrameEnv(1)`, `FFX_Gs_SetupFrameEnv(0)` | the GS display environment blocks at `g_ffxGsDBuff 0x12FB900` / `unk_12FB928` |
| 94 | `FFX_SyncData_LoadSyncList(0)` | `FFX_SyncData__loadSyncList(g_ffxSyncDataMgr)` |
| 95 | `FFX_FmodCmd_InitData 0x67A500` (-> `FfxFmod_QueueCommand0(1)`), `FFX_Snd_ApplyVolumeOptions 0x67A600` | FMOD side enable and the sound bank prime |
| 96 | `FFX_ChCache_Init 0x840910`, `sub_77C870`, `sub_76EFF0`, `sub_781D00` | table inits (field, chr render, SG stub, battle table) |
| 97 | `tklib__f88D710 0x88D710` | the tklib init. Inside, in order: `sub_88E570`, `FFX_VsyncEmu_Install 0x8886F0` (**starts the vsync emulation thread**, `THREADS_AND_CLOCKS.md` T9), `FFX_Pad__resetAllPorts`, `FFX_Map_RequestResumeFromCheckpoint`, `maybe_g_ffxAtelStepGate = 1`, the 64 byte movie stream descriptor at `unk_1338880`, `sub_88E880`, `sub_76E630` (registers `sub_76E5F0` as the hdd callback, `FFX_Ps2Thread_SetHddCallback_dead 0x888740`), the asset loader loads (sphere grid tables through `FFX_GetAssetLoader`, 24/4, 24/0, 24/5, 38/16, 25/0), `TOInit`, **`FFX_Atel_Init 0x86D6D0`** (the 16 ATEL system libraries, and `FFX_VsyncEmu_SetSlotCallback(2, FFX_Save_StepPlayTimeFromWallClock)` registers the play time counter as vsync callback slot 2), the 64 entry 9/10 decay table at `word_23CBF60`, the sound path `/ffx/proj/sound/us`, `FFX_Voice_ResetState 0x886550`, `sub_8872A0(0)`, then `maybe_FFX_Se_LoadProjectFromHeader 0x81E3B0`, `sub_887560`, `sub_81D470/sub_81D3C0`. |
| 98 | `sub_83B5F0` | `sub_83FC50`, `g_ffxPs2HwRegSink = 131`, `g_ffxSharedWorkBufPtr = FFX_MemAlloc(0x100000)` (the 1 MB PS2 work buffer), `sub_82CD50`, `sub_841440`, `j_FFX_Mot_SetKeepSpeed(1)`, `sub_826A40`, `sub_83D140`, `sub_830140(0)` |
| 99 | `FFX_Ch_Init 0x826EA0` | chr.c init, prints "sizeof(CHR)=%x" with 0x880, clears the 0x2EE0 byte table at 0x23C8D00 and fills 40 x 300 byte entries with -1. `CHR_RESIDENCY.md`. |
| 100 | `sub_844EB0`, `sub_909030` (`sub_90C400(&g_ffxMapWork, 0, 12288, 9216)`), `sub_7C0540(512, 416)` | map work area and the 512x416 PS2 frame size |
| 101 | `FFX_Came_Init 0x7BD5D0` | 3 camera slots at 0x1137540 (stride 3488), 3 screens at 0x1137000 (stride 448), slot 0 active |
| 102 | `FFX_LoadEventIdTable` | debug mode only |
| 103 | `FFX_Map_ClearActiveWalkmesh`, `FFX_Player__releaseControl` | |
| 104 | `FFX_Btl_Init 0x781700` | logs "* BTL INIT", four `FFX_MemAlloc`s sized from `g_ffxEncTableSize` and `g_ffxKernelTableSumAssureSize 0x112A972` and `word_112A974/6`, installs the battle loader callbacks, and reaches **`FFX_InitNewSaveData 0x786E15` -> `FFX_Rand_SeedValueFromSystemClock 0x798950` -> `FFX_Rand_SeedAllStreams 0x798890`**. This is the wall clock RNG seed of the game RNG, at boot, on the JobWorker thread. `THREADS_AND_CLOCKS.md` part 2 row R1. |
| 105 | `sub_761D60` | sasa / SG debug protocol init (`sub_76B2D0(&unk_E524A8, 57362, ...)`, "InitProtocolDriver PROTO_PARTMAST failed"), `g_ffxSgDebugEnabled = 1`, `FFX_Ppp_InitHeapForSet(dword_10D4CFC, 0, &g_ffxVfxPppHeapArena, 0x100000)` |
| 106 | `FFX_Rand_SeedCosmeticStreams 0x7FF360` | the cosmetic RNG seeds: `srand(0x3F4CF27B)` and `g_ffxEffectRandState = -1.4980391f`, once (guard `dword_12E9C60`). Constants, no clock. Row R2. |
| 107 | `maybe_FFX_Yonishi__feedPadStateBothPorts`, `sub_782740`, `FFX_Ch_InitMemoryMoveCursor 0x826230` (`FFX_MemAlloc(0x1000)`), `maybe_FFX_Player__setSubStepOverride(6)` | |
| 108 | `sub_83ADD0(1)`, `FFX_SetMapDrawEnabled(1)`, `FFX_SetChrDrawEnabled(1)`, `sub_8203E0(1)`, `FFX_SetChrMotionUpdateEnabled(1)`, `FFX_SetCameraAlwaysStep(1)`, `FFX_SetDebugGuiDrawEnabled(1)`, `FFX_SetMesWinDrawEnabled(1)`, `sub_83F700(1)`, `FFX_SetEffectsPassEnabled(1)`, `FFX_SetMapFPPassEnabled(1)`, `FFX_SetMapVisible(1)`, `FFX_SetChrVisible(1)`, `sub_831940(1)`, `FFX_Dbg_SetChrUpdateFrozen(0)`, `FFX_SetScriptEffectsPassOff(0)` | the enable flags, all on |
| 109 | `g_ffxStepScaleClockAtLast = g_ffxGameClock60Hz`, `g_ffxMainStepCounter = 0` | counters zeroed |
| 110 | `FFX_Map_RequestChange(23, 0)` and `maybe_FFX_Scene_Init(saveData, 23)` | **the title**. Debug mode: `maybe_FFX_Scene_Init(saveData, 0)` and no request. `_ECalm`: `g_ffxFirstBootSceneEventId 0xC33468 == 393` instead of 23. Both the deferred request and its consumer are called here, so the title event is resident before the first step. Section 9. |
| 111 | `sub_83F7A0(256, 208)`, `FFX_PlayerCam_ApplyScreenProj` | half resolution projection constants |
| 112 | `MagicFile__init` | logs "MagicFile: Init()", `MAGIC_DLL.md` |
| 113 | `byte_C85EC4 = 0`, `sub_625C70(&off_C85EC0)` | last |

The save block has NOT been reset by a title flow at this point. `FFX_InitNewSaveData` is
reached here only through `FFX_Btl_Init`, and again from ATEL Core 573 in the title script
(`ffx-title-screen-reads-as-a-valid-world` in memory covers the consequence: the block reads
as all zero and valid before a game).

## 9. The boot scene window and the title

- `g_ffxFirstBootSceneEventId 0xC33468 = 393` (scene1.ebp) and `g_ffxQuitPseudoMapId 0xC33480
  = 399`. `FFX_Ev_IsBootSceneEvent` is `(id - 393) <= 6`, so events 393..399 are the boot scene
  window: the Eternal Calm opening, the logo and warning scenes and the quit pseudo map.
- The retail title is event 23, raised at step 110 by `FFX_Map_RequestChange(23, 0)` and
  consumed synchronously by `maybe_FFX_Scene_Init(saveData, 23)`. The title steps through
  `FFX_MainStep` like any other event (memory `ffx-title-is-just-event-23`). The 23 arm of
  `maybe_FFX_Scene_Init` also latches `g_ffxTitleExitCheckpointSuppress 0xC58E40 = 1`.
- `_ECalm` boots into 393 and shows no `OpeningScreen.swf`.
- `debug` on argv[1] boots into section 0 with no request and loads the event id table.

## 10. The first frames

Once `run` enters the pump, each `Phyre__PApplication__frameTick 0x627940` (`MAIN_LOOP.md`
sections 1 and 2) does: `frameDelta = Phyre_Time_NowSeconds() - g_phyreTimeOrigin - app+10`
(forced 0 on the first frame, with a vtbl+36 resize first), vtbl+28 `update` (input poll when
not threaded), vtbl+16 `animate` -> `FFX_MainStepLoop 0x822840` -> `FFX_MainStep 0x820A00`,
the per viewport render, `Phyre_RenderThread_FlushGlobal 0x5BC3F0` (post a marker to the render thread and wait for it,
which is where `Present` happens, `THREADS_AND_CLOCKS.md` T1), vtbl+32 `render`
(`FFXApplication__render` -> `FFX_Frame_PresentScene` -> `FFX_Frame_RenderScene 0x668930`, `FFX_Frame_WaitJobSchedule 0x71E810` (waits for
the JobSchedule ring to drain, so the chr update jobs of this step are done before submit),
`FFX_GfxCtx_ReleaseDeferredDrawItems 0x65A620`, then `FFX_VsyncEmu_SignalPresent`), vtbl+40 `endFrame`.

What the first `FFX_MainStep` sees: `g_ffxMainStepCounter == 0`, rate divider 2, the title
event resident with its map change already consumed, the save block zero, the game RNG
seeded from the wall clock at step 104, the cosmetic RNGs seeded from constants at step 106,
all enable flags on, and the FMOD, input, async load, vsync emulation and JobWorker threads
already running. `FFX_MainStep_StepSaveScanAndCachePlayerPos 0x648480` starts its 2 s
`GetTickCount` ramp on this step (row C3).

## 11. Which thread runs what, during boot

| part | thread |
|---|---|
| sections 1 to 7 up to step 85, and steps 87, 88 | main thread (the one that later steps the simulation) |
| step 86, that is `FFX_ParseCommandLineAndInitDebug`, `FFX_InitSubsystems1`, all of `FFX_MainInit` | one of the four `JobWorker` threads |
| step 35 onwards | the other three `JobWorker` threads idle on their semaphore |
| step 19 onwards | `PhyreEngineRenderThread`, executing whatever the main thread posts and presenting |
| step 40 onwards | `FFX_InputThread__main` (polls only while `g_ffxThreadedPadMode`) |
| step 41 onwards | `AsyncLoadManager__workerThread`, running step 82's load during step 84 |
| step 51 onwards | the AutoTestManager stdin reader, blocked in `getline` |
| step 81 onwards | `FmodManager` |
| step 97 onwards | the vsync emulation thread |

The consequence for a recreation: `FFX_MainInit` touches every PS2 side global and it does so
on a thread that is not the step thread, while the main thread is presenting. In the original
this is safe only because the main thread's boot loop reads nothing `FFX_MainInit` writes
until `JobSchedule__allSlotsFree 0x6DBF30` reports the ring drained. A recreation can run `FFX_MainInit` on the step
thread before the first step, which is simpler and identical for the tables, because nothing in
the title depends on which thread built them. One exception (2026-10-28, `THREADS_AND_CLOCKS.md`
R2): MSVCR110 keeps the `rand` seed per thread, so the `srand` inside `FFX_Rand_SeedCosmeticStreams`
seeds the boot worker's stream and the step thread's CRT stream starts at the default seed 1. A
recreation owns its streams and must start the step stream where the original does, not where a
moved `FFX_MainInit` would leave it. The one thing that must survive is the
seed order: the game RNG is seeded (from the clock in the original, from the session seed in
the recreation) before `FFX_Rand_SeedCosmeticStreams 0x7FF360` seeds the cosmetic RNGs, and both before the first step.

## 12. Unresolved

- ~~The identities of the unnamed singletons at steps 36, 37, 39, 43 to 47, 53, 54, 57 to 61,
  71 to 73.~~ Resolved 2026-10-28, section 6. The guess that 53 to 61 are all effect objects was
  half right: 54, 58 and 61 are effects, 53 is the VFX shader table, 57 the button prompt table,
  59 the specular pass, 60 the shadow manager. 46 already had a name.
- The class of step 36. Only its no-op method is live, the name `maybe_FFX_HeapFacade` comes from
  the orphan allocator forwarders next to it.
- Which of the eight debug pages (63 to 70) store to which global for 65, 66, 68, 69, 70. The
  three that IDA already names are listed.
- ~~Whether `sub_6E4FF0(3.0, 0, 0.0)` at step 83 is a fade or a timer.~~ Resolved 2026-10-28, step 83.
- `sub_629840` at step 22. Not traced.
