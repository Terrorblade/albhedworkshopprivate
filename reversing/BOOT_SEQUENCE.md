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

**1.** CRT init entry `sub_AE0510` calls `sub_42EF40(dword_C8FB78)`, the `FFXApplication`
constructor, on the static object at `0xC8FB78`, and registers `sub_AF9BA0` with `atexit`.
`sub_42EF40` runs `Phyre__PApplication__ctor`, which writes `g_ffxApplication 0xCC9CD8 =
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
`settings[44]` (-> `sub_58DE30`), `settings[52]` (-> `sub_58E000` in `onInit`),
`settings+56`.

**5.** `CreateMutexW(L"9B8F7468-6DE0-4C5A-9209-89D363F8FCC9")`. `ERROR_ALREADY_EXISTS` shows
string 30773 and exits. One instance per machine, which a two instance local co-op test has
to patch.

**6.** `GetCommandLineW` -> `CommandLineToArgvW` -> narrow argv. This is the real process
command line (`DEBUG_MODE.md` has the proof). Usage string, when `argc > 1`, through
`Phyre__PApplication__parseCommandLine`: `-resolution= -msaa= -vsync -host= -script=`.

**7.** `g_ffxTargetFrameTime = g_phyreFrameRateDivider / 59.94005994`. The limiter target.

**8.** `vtbl+4` (`FFXApplication__vf01_releaseDynamicMeshes`) on the application, then the
settings are copied into the app: +620 width, +624 height, +644 = `settings+56`, +648 =
`settings[40] != 0`, +664 = `settings[8] == 1`, and `g_phyreBorderlessWindow = settings[8]
== 2`. `sub_58DE30(settings[44])`.

**9.** `FFX_SteamApiInit 0x623470`. Sets `g_ffxSteamApiReady`, which the save system reads at
step 21.

**10.** `Phyre__PApplication__run(argv, argc)`. Everything from section 3 to section 9 happens
inside this call. When it returns the message pump below is already over.

After `run`: the `PeekMessageA` pump with the `Sleep` limiter driven by
`Phyre_Time_NowSeconds() - g_phyreTimeOrigin` (`MAIN_LOOP.md` section 1), then teardown:
`app+292` vtbl+8, `sub_5BC3F0` (render thread flush), the log line "SKS: terminate result",
`Phyre_Framework_Terminate`, `SteamAPI_Shutdown`, free the settings.

## 3. Phyre__PApplication__run 0x629A40 and Phyre_Framework_Init 0x430DD0

**11.** `sub_431170(1)` -> `sub_44CE10`: `dword_C0B950 = 1`. This is the Phyre worker thread
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
| 13f | `sub_44CD30` | the default thread pool: `dword_C0B950 > 1 ? sub_57D100 (PThreadPoolWorkStealing(dword_C0B950, dword_C0B95C)) : sub_57CF50 (PThreadPoolImmediate)`, stored in `dword_C91BA8`. Because step 11 set the count to 1, the retail game builds `PThreadPoolImmediate`. No `PhyreEngineWorkerThread` is ever created. |
| 13g | `sub_439510` (basic types), PTypedObject EnsureInit, `sub_43E440`, `sub_43BE70` | more descriptors |
| 13h | `_atexit(Phyre_Framework_Terminate)`, `byte_C901A0 = 1` | done |

A stray helper at 0x430D40 (no IDA function) also runs `Phyre_Time_Init` and writes the origin,
with no callers found. It is not part of the boot.

**14.** `sub_629AB0`: `Phyre__PWindowWin32Base__create` (the window, `MAIN_LOOP.md` section 1
and `RENDER_DEVICE.md` for the swap chain that follows), the hosted window message
`PHYREMSG_SENDHWND`, then `Phyre__PApplication__onInit(g_ffxApplication, argc, argv)`, and
`onExit` if that fails.

**15.** (Reserved for the worker pool that is never created. `sub_44CDE0()` reads
`dword_C0B950` again inside `onInit` and skips `sub_5BBC30`, the per worker `PRendererForJobs`
constructor, for the same reason.)

## 4. Phyre__PApplication__onInit 0x62A600

**16.** `app+10 = Phyre_Time_NowSeconds() - g_phyreTimeOrigin` (start offset), the frame
callback `Phyre__PApplication__frameTick 0x627940` is installed at `app+616`, `app+15 =
g_phyreFixedTimeStepArg`.

**17.** Optional `-script` proxy (0x28 byte `PApplicationScriptProxy`, `sub_62BB60`, callback
`sub_62C6F0`), the input log (fixed timestep 0.016659999, "INFO: Set fixed timestep") and the
input replay. None of these is active in a normal retail launch.

**18.** `sub_463D50`, then the D3D context once: `sub_4FEE70(&g_ffxPhyreD3DContext)`.
`FFX_Phyre_InitDisplay` (`GetClientRect` + `FFX_Phyre_CreateDeviceAndSwapChain`), "Error
initializing Phyre Render Interface" on failure. From here `RENDER_DEVICE.md` section 2 is the
reference, this doc does not repeat it. Then `sub_58E000(settings[52])` and
`sub_443A40(app+177)`.

**19.** The Phyre render thread: `sub_614410()` returns the render thread singleton at
`unk_CC96F8` (lazily constructed by `sub_614010`), and `sub_615140` starts it. Two semaphores
(16,16) and (1,1), 16 ring slot events, and a thread named `PhyreEngineRenderThread`, stack
0x10000, Phyre priority mode 1, procedure `sub_614710` -> `sub_616010`. Error string "Error
initializing Phyre Render Thread with error". `this+12 = 1` marks it running. This thread
exists in retail and `IDXGISwapChain::Present` runs on it. See `THREADS_AND_CLOCKS.md`
part 1, thread T1.

**20.** `Phyre__PInput__createDirectInput8`.

**21.** vtbl+8 `FFXApplication__initApplication(argc, argv)`. Section 5.

**22.** Two Phyre scratch allocations: `sub_42FB80(2621440, 16)` -> `app+181` with
`sub_5B8DA0(.., 0x200000, 0x80000)`, and `sub_42FB80(0x80000, 16)` -> `app+182` with
`sub_5CD010`. Then `sub_629840`.

**23.** `sub_44CDE0()` == 1 so `sub_5BBC30` (`PRendererForJobs` per worker) is skipped.
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
`data\FFX_Data.vbf` (failure -> MessageBox string 30774), `sub_6DAFC0` (the language manager
at `dword_CDED58`), the title id string PCSG00219 / PCSE00293 / PCSB00395 / PCSH00042 by
region, `sub_6F9B60`. Log line "SKS: fiosInitialize finished".

**28.** Log line "SKS: graphicInitialize finished". Yes, this is printed BEFORE
`FFX_GraphicInitialize` runs (the log order is wrong in the shipped binary). Do not use that
line as a boot milestone.

**29.** `sub_646910`: the `ffxsjistbl_*.bin` text encoding tables.

**30.** `FFX_SaveSys_Init 0x647FE0`: `sub_6F1C10`, `sub_6F0330` (the save folder),
`sub_6F1430` only when `g_ffxSteamApiReady` (an 0x50 byte Steam Cloud object at
`dword_CE81A8`), `FFX_SaveUtil_AllocState` (3 bytes at `g_ffxSaveUtilState`), `sub_6F2070`.

**31.** `FFX_SaveFile_RegisterStagingBuffer 0x647FB0`: the 26880 byte save staging buffer.

**32.** `FFX_GraphicInitialize 0x6419D0`. Section 6, steps 33 to 79.

**80.** `FFX_Input__get` (constructs the `FFX_Input` singleton on first call, `FFX_Input__ctor`,
`INPUT_LAYER.md`) then `FFX_Input__buildActionMap`.

**81.** If `!FFX_SaveUtil_FrameHeld()`: `sub_679FC0` -> `sub_6FA0B0`: the `FmodManager` object
(0x50 bytes at `dword_CE9010`, constructed by `sub_6F9DE0`, which creates the `FmodManager`
thread, `THREADS_AND_CLOCKS.md` T4), and `byte_C8FAE8 = 1`. Then `sub_67A960` (`FFX_Alloc` +
`sub_6FA8B0`).

## 6. FFX_GraphicInitialize 0x6419D0, the singleton pass

Log line "SKS: graphicInitialize begin!" first and "SKS: graphicInitialize end!" last. Every
entry below is a guarded create (`if (global == 0) global = ctor(FFX_Alloc(size))`) unless it
says otherwise, and the guard means a second call is a no-op. Sizes are the `FFX_Alloc`
argument. "unnamed" means the IDB has no name and the class could not be identified from
strings or vtables in this pass.

| step | ctor / create fn | size | global | identity, dependencies |
|---|---|---|---|---|
| 33 | `sub_6DC800` | 656 (Phyre `sub_42FB80`, align 4) | `dword_CDEDE8` | the memory report manager: prints "RAM=%s - VRAM=%s - HOST=%s - SRAM=%s - FMOD=%s". Also sets `dword_C31E98 = 1`. |
| 34 | `FFX_Alloc(0x238)` -> `sub_6DB670` | 0x238 | `dword_CDEDE4` | **JobSchedule**. The ctor builds the 8 slot job ring (`sub_6DB560`: 8 x 48 byte slots, semaphores at +107, +119, +120, +121). |
| 35 | `sub_6DBA20(dword_CDEDE4)` | | | **starts the four JobWorker threads**: two semaphores (8,8), 8 free slots, `sub_703A50(this+20*i, 0, 0x20000)` x4, priority 0. These are the threads `FFX_MainInit` runs on (step 86) and the threads `FFX_Ch_DispatchUpdateJob` uses every step. `THREADS_AND_CLOCKS.md` T5. Depends on 34. |
| 36 | `sub_6E56A0` | 1 | `dword_CE63C4` | unnamed one byte object |
| 37 | `sub_6DEB90` | 0x10 + `CreateMutexW` | `dword_CDEDF0` | unnamed, a mutex guarded counter (`sub_6DE720(0)` fills +4) |
| 38 | `sub_69F440` | 8 | `g_classCharacterTable` | the class character table |
| 39 | `sub_69A060` -> `sub_699AB0` | 0x6420 | `dword_CCCA4C` | unnamed, 25632 bytes |
| 40 | `sub_68DB00` | 0x44 | `dword_CCC860` | the input thread manager (`AsyncControllerTaskManager`), creates the `FFX_InputThread__main` thread. `THREADS_AND_CLOCKS.md` T6. |
| 41 | `AsyncLoadManager__create` -> `AsyncLoadManager__ctor` | 0x4C | `g_asyncLoadManager` | mutex, the 200 entry `LoadClusterRequest` pool `dword_CCC890`, the 150 entry `RegistTaskRequest` pool `dword_CCC894` with its 0x12C0 arena, then `AsyncLoadManager_startWorkerThread`. `THREADS_AND_CLOCKS.md` T7. |
| 42 | `sub_69F4C0` -> `sub_69E140` | 0x50 | `dword_CCCA6C` | the pool set "data pool/vfxdg pool/segment pool/name pool" |
| 43 | `sub_692A90` -> `sub_692020` | 0x1240C | `dword_CCC8A0` | unnamed, 74764 bytes |
| 44 | `sub_6ABE40` | 0x28 | `dword_CCCA90` | unnamed |
| 45 | `if (dword_CCCA98 == 0) FFX_Alloc(0x208)` -> `sub_6AD0D0` | 0x208 | `dword_CCCA98` | unnamed, ctor zeroes two words and calls `sub_6ADC90` |
| 46 | `sub_6A6CD0` -> `sub_6A63F0` | 0x234 | `dword_CCCA84` | unnamed |
| 47 | `sub_6A6D50` -> `sub_6A6610` | 0xFC | `dword_CCCA88` | unnamed |
| 48 | `sub_6B60C0` -> `ViewerManager__ctor` | 0xB8 | `g_debugInfoState` | **ViewerManager** (`DEBUG_MODE.md`) |
| 49 | `sub_692B10` | 0x190 | `dword_CCC89C` | **PostProcessTweakUtil** |
| 50 | `DebugMenuManager__createSingleton` | 8 | `dword_CCCAB0` | **DebugMenuManager**, mode 10 |
| 51 | `AutoTestManager__createSingleton` | 300 | `g_autoTestManager 0xCCCB00` | **AutoTestManager**. The ctor creates the stdin "Read command" thread. `THREADS_AND_CLOCKS.md` T8. |
| 52 | `sub_6B00A0` | 0x84 | `dword_CCCAA8` | **TextureAnimationManager** |
| 53 | `sub_6C1420` -> `sub_6BF4A0` | 0x9C | `dword_CDEB80` | unnamed |
| 54 | `sub_6C2190` -> `sub_6C2010` | 0x80 | `dword_CDEB88` | unnamed |
| 55 | `sub_6C34D0` | 0x88 | `dword_CDEBA0` | **BlurEffect** |
| 56 | `sub_6C49A0` | 0x70 | `dword_CDEBFC` | **BlurEffectEx**, needs 55 |
| 57 | `sub_6E76C0` -> `sub_6E6330` | 0x168 | `dword_CE6DC0` | unnamed |
| 58 | `sub_6C5030` -> `sub_6C4EC0` | 0x8C | `dword_CDEC04` | unnamed |
| 59 | `sub_6C5790` -> `sub_6C5670` | 0x5B4 | `dword_CDEC1C` | unnamed |
| 60 | `sub_6C9500` -> `sub_6C7D80` | 0x4068 | `dword_CDEC28` | unnamed, 16488 bytes |
| 61 | `sub_6D4D30` -> `sub_6D4BA0` | 0x78 | `dword_CDEC98` | unnamed |
| 62 | `sub_6D7130` -> `FFX_Fmv_Manager_Construct` | 0x750 | `g_ffxFmvPlayerManager` | **PhyFMVPlayerManager** (`FMV_SYNC.md`). The two video threads are created per movie, not here. |
| 63 | `sub_6B5EB0` -> `sub_6B55D0` | 0xF4 | `g_ffxDebugPageCharSwitching` | debug page |
| 64 | `sub_6B6040` -> `sub_6B57D0` | 0x214 | `g_ffxDebugPageMapSwitching` | debug page |
| 65 | `sub_6B5F30` | 0xF0 | (CharTexAnim page) | debug page |
| 66 | `sub_6B5E30` | 0xE0 | (CharAnim page) | debug page |
| 67 | `sub_6B5CB0` -> `sub_6B52A0` | 0x120 | `g_ffxDebugPageBasicInfo` | debug page |
| 68 | `sub_6B5DB0` | 0x1E4 | (BattleInfo page) | debug page |
| 69 | `sub_6B5D30` | 0x11C | (BattleCharParams page) | debug page |
| 70 | `sub_6B5FC0` | 0x108 | (GameParams page) | debug page |
| 71 | `sub_6E3240` | 0xB8 | `dword_CE5170` | unnamed |
| 72 | `FFX_Alloc(0x44)` -> `sub_6E45E0` | 0x44 | `dword_CE517C` | unnamed, a 17 float record zeroed by the ctor |
| 73 | `sub_6E5490` | 1 | `dword_CE5194` | unnamed one byte object |
| 74 | `FFX_Alloc(0x10DE8)` -> `sub_64DC00` | 0x10DE8 (69096) | `g_ffxGfxCtx` | **the FFX graphics context**, the object every present and draw path takes as `this` |
| 75 | `sub_6656E0(g_ffxGfxCtx)` | | | post-construction setup of 74: `FFX_Gfx_FitViewTo16by9`, `sub_6DC8A0`, `sub_6DDBF0`, `Phyre__getApplication`, then `sub_692BE0/693BE0/6985B0/6983F0/692BF0/6938D0/664440/6E4770/6935F0`. Depends on 33, 43, 74. |
| 76 | `FFX_Btl_OpenMonsterArchives` | | | opens the monster archives |
| 77 | `sub_686FF0(0x100000, 16)` | 1 MB | `dword_CCB458` | a 1 MB arena |
| 78 | `sub_6EBD30(dword_CCB458, 0x100000, 1)` | | `dword_CCB454` | the allocator over that arena. Its header cookie comes from `sub_6EA050`, which reads `GetTickCount` (`THREADS_AND_CLOCKS.md` part 2, row C1). `dword_CCB45C = 1`. |
| 79 | log "SKS: graphicInitialize end!" | | | |

Ordering constraints a recreation must keep: 35 after 34 (the ring before the workers), 56
after 55, 75 after 74 and 33, 78 after 77, and 40 and 41 both before any load request is made
(step 82 is the first). Nothing in this pass touches the PS2 side globals, those are all
`FFX_MainInit`.

## 7. FFXApplication__initScene 0x42F8E0 -> FFX_BootSequence 0x6416C0

`initScene` is a thin wrapper, `FFX_BootSequence` is the body. It runs on the main thread,
inside `onInit`, and this is where boot forks onto a worker thread.

**82.** `AsyncLoadManager__enqueue(sub_636430)`: the first async load. `sub_636430` registers
the dynamic mesh classes, calls `sub_66F010(g_ffxGfxCtx)`, `FFX_Fmv_ParseVideoList`, and
builds the keyboard and pad icon object at `g_ffxGfxCtx+69088`.

**83.** `sub_6E96C0`: the "Loc_Texture_Saveload_Attention_Before_Game" texture (string 505),
the autosave warning. `sub_A26240` and `sub_A262E0(app+292, w, h)`: the Iggy (Scaleform
style) UI runtime. `app+928 = 0`. `sub_6E4FF0(3.0, 0, 0.0)`.

**84.** Not `_ECalm`: `sub_6579D0` loads `OpeningScreen.swf` (its "Finish" callback is
`sub_652400`, which sets `byte_CCB9C2`). Then the loop `sub_660400`: `Phyre__PInput__update`,
`sub_66B2E0(g_ffxGfxCtx)` (present one frame then `Sleep(0x21)`), and once
`AsyncLoadManager__isIdle` is true:

**85.** `sub_633670(ecx = dword_CDEDE4, FFX_ParseCommandLineAndInitDebug_tramp 0x636410,
&v17, argc, argv, 0)`. `sub_633670` is the JobSchedule poster. The disassembly at 0x641830
and 0x64193A shows `mov ecx, dword_CDEDE4` before the call, so the receiver is the
JobSchedule of step 34, and the trampoline runs on one of the four `JobWorker` threads of
step 35. `DEBUG_MODE.md` calls this a callback. It is a job on another thread, see the
corrections list in the hand-back.

**86.** On the JobWorker thread: `FFX_ParseCommandLineAndInitDebug 0x822490`. Sets
`g_ffxDebugMode` from `argv[1] == "debug" / "DEBUG"`, calls `sub_83C200` (the scratch arena
`dword_1301960 = sub_686FF0(&unk_1100000, 16)`), `FFX_InitSubsystems1(argc, argv)`,
`FFX_MainInit`, returns 1 into `v17`. Section 8.

Meanwhile the main thread keeps presenting the opening swf. `sub_65B750`, `app+928 = 1`.
The `_ECalm` path instead shows 30 frames through `sub_66B1E0`.

**87.** Wait for `FFX_SaveUtil` ready, wait for `AsyncLoadManager__isIdle` (abort byte
`byte_CCC7BC`), `app+929 = 1`, `app+928 = 0`, and a late post of step 85 if the opening loop
did not get to it. Then `while (!sub_6DBF30(dword_CDEDE4, 0)) sub_66B1E0(g_ffxGfxCtx);`
which presents frames until all 8 job ring slots are free, which is when `FFX_MainInit` has
returned. `app+929 = 0`.

**88.** `v17 == 1 ? FFX_EscMenu_LoadMovie(g_ffxGfxCtx, ...) : return 9`. Despite the name
this loads `pausemenu.swf` and `escmenu.swf`, the pause overlay. `FFX_BootSequence` returns,
`initScene` returns, `onInit` returns, `run` enters the pump.

## 8. FFX_InitSubsystems1 0x820310 and FFX_MainInit 0x820860, on the JobWorker thread

`FFX_InitSubsystems1` is tiny: `maybe_FFX_Player__setPadLocked(0)`, `byte_12FB8FA = -1`,
`g_ffxDbgUseVfxCameraMatrices = 0`, `byte_12FB8F8 = 0`, and in debug mode with `argc > 1`
it parses `argv[1]` through `sub_821D30`. It constructs nothing. The name in PROGRESS 1.2.8
ordering (`FFX_MainInit -> FFX_InitSubsystems1`) is backwards, `InitSubsystems1` runs first.

`FFX_MainInit` is the PS2 side `main()` prologue. In order, every call that does something:

| step | call | effect |
|---|---|---|
| 89 | `FFX_Frame_SetRateDivider(2)` | 30 Hz step until the title script changes it |
| 90 | `sub_728750` | `FFX_Malloc(0x2C6B8)`, the yonishi VFX primitive buckets (`FFX_Vfx_ResetPrimBuildQueue` zeroes their counts each step) |
| 91 | `sub_72AC30` | small VFX table init |
| 92 | `sub_62F700(1)`, `sub_62F6E0(2/0/1)` -> `dword_23CBBF4/BFC/BF8`, `sub_62FB80(0)` | PS2 stubs, all return 0 |
| 93 | `FFX_Ps2_ReadStatStub`, `FFX_SetMovieScreenMode(0)`, `FFX_Gs_SetupFrameEnv(1)`, `FFX_Gs_SetupFrameEnv(0)` | the GS display environment blocks at `unk_12FB900` / `unk_12FB928` |
| 94 | `FFX_SyncData_LoadSyncList(0)` | `FFX_SyncData__loadSyncList(g_ffxSyncDataMgr)` |
| 95 | `sub_67A500` (-> `sub_6FA280(1)`), `sub_67A600` | FMOD side enable and the sound bank prime |
| 96 | `sub_840910`, `sub_77C870`, `sub_76EFF0`, `sub_781D00` | table inits (field, chr render, SG stub, battle table) |
| 97 | `tklib__f88D710 0x88D710` | the tklib init. Inside, in order: `sub_88E570`, `sub_8886F0` (**starts the vsync emulation thread**, `THREADS_AND_CLOCKS.md` T9), `FFX_Pad__resetAllPorts`, `FFX_Map_RequestResumeFromCheckpoint`, `maybe_g_ffxAtelStepGate = 1`, the 64 byte movie stream descriptor at `unk_1338880`, `sub_88E880`, `sub_76E630` (registers `sub_76E5F0` as the hdd callback, `sub_888740`), the asset loader loads (sphere grid tables through `FFX_GetAssetLoader`, 24/4, 24/0, 24/5, 38/16, 25/0), `TOInit`, **`FFX_Atel_Init 0x86D6D0`** (the 16 ATEL system libraries, and `sub_888790(2, sub_86CA20)` registers the play time counter as vsync callback slot 2), the 64 entry 9/10 decay table at `word_23CBF60`, the sound path `/ffx/proj/sound/us`, `sub_886550`, `sub_8872A0(0)`, then `sub_81E3B0`, `sub_887560`, `sub_81D470/sub_81D3C0`. |
| 98 | `sub_83B5F0` | `sub_83FC50`, `g_ffxPs2HwRegSink = 131`, `off_C498DC = FFX_MemAlloc(0x100000)` (the 1 MB PS2 work buffer), `sub_82CD50`, `sub_841440`, `sub_839CA0(1)`, `sub_826A40`, `sub_83D140`, `sub_830140(0)` |
| 99 | `FFX_Ch_Init 0x826EA0` | chr.c init, prints "sizeof(CHR)=%x" with 0x880, clears the 0x2EE0 byte table at 0x23C8D00 and fills 40 x 300 byte entries with -1. `CHR_RESIDENCY.md`. |
| 100 | `sub_844EB0`, `sub_909030` (`sub_90C400(&g_ffxMapWork, 0, 12288, 9216)`), `sub_7C0540(512, 416)` | map work area and the 512x416 PS2 frame size |
| 101 | `FFX_Came_Init 0x7BD5D0` | 3 camera slots at 0x1137540 (stride 3488), 3 screens at 0x1137000 (stride 448), slot 0 active |
| 102 | `FFX_LoadEventIdTable` | debug mode only |
| 103 | `FFX_Map_ClearActiveWalkmesh`, `FFX_Player__releaseControl` | |
| 104 | `FFX_Btl_Init 0x781700` | logs "* BTL INIT", four `FFX_MemAlloc`s sized from `g_ffxEncTableSize` and `word_112A972/4/6`, installs the battle loader callbacks, and reaches **`FFX_InitNewSaveData 0x786E15` -> `FFX_Rand_SeedValueFromSystemClock 0x798950` -> `FFX_Rand_SeedAllStreams 0x798890`**. This is the wall clock RNG seed of the game RNG, at boot, on the JobWorker thread. `THREADS_AND_CLOCKS.md` part 2 row R1. |
| 105 | `sub_761D60` | sasa / SG debug protocol init (`sub_76B2D0(&unk_E524A8, 57362, ...)`, "InitProtocolDriver PROTO_PARTMAST failed"), `g_ffxSgDebugEnabled = 1`, `sub_7169D0(dword_10D4CFC, 0, &unk_FD24F0, 0x100000)` |
| 106 | `sub_7FF360` | the cosmetic RNG seeds: `srand(0x3F4CF27B)` and `g_ffxEffectRandState = -1.4980391f`, once (guard `dword_12E9C60`). Constants, no clock. Row R2. |
| 107 | `maybe_FFX_Yonishi__feedPadStateBothPorts`, `sub_782740`, `sub_826230` (`FFX_MemAlloc(0x1000)`), `maybe_FFX_Player__setSubStepOverride(6)` | |
| 108 | `sub_83ADD0(1)`, `FFX_SetMapDrawEnabled(1)`, `FFX_SetChrDrawEnabled(1)`, `sub_8203E0(1)`, `FFX_SetChrMotionUpdateEnabled(1)`, `FFX_SetCameraAlwaysStep(1)`, `FFX_SetDebugGuiDrawEnabled(1)`, `FFX_SetMesWinDrawEnabled(1)`, `sub_83F700(1)`, `FFX_SetEffectsPassEnabled(1)`, `FFX_SetMapFPPassEnabled(1)`, `FFX_SetMapVisible(1)`, `FFX_SetChrVisible(1)`, `sub_831940(1)`, `sub_821CB0(0)`, `FFX_SetScriptEffectsPassOff(0)` | the enable flags, all on |
| 109 | `dword_12FBC30 = g_ffxGameClock60Hz`, `g_ffxMainStepCounter = 0` | counters zeroed |
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
the per viewport render, `sub_5BC3F0` (post a marker to the render thread and wait for it,
which is where `Present` happens, `THREADS_AND_CLOCKS.md` T1), vtbl+32 `render`
(`FFXApplication__render` -> `FFX_Frame_PresentScene` -> `sub_668930`, `sub_71E810` (waits for
the JobSchedule ring to drain, so the chr update jobs of this step are done before submit),
`sub_65A620`, then `FFX_VsyncEmu_SignalPresent`), vtbl+40 `endFrame`.

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
until `sub_6DBF30` reports the ring drained. A recreation can run `FFX_MainInit` on the step
thread before the first step, which is simpler and observably identical, because nothing in
the title depends on which thread built the tables. The one thing that must survive is the
seed order: the game RNG is seeded (from the clock in the original, from the session seed in
the recreation) before `sub_7FF360` seeds the cosmetic RNGs, and both before the first step.

## 12. Unresolved

- The identities of the unnamed singletons at steps 36, 37, 39, 43 to 47, 53, 54, 57 to 61,
  71 to 73. Each is a guarded create with a known size and global, nothing in the ctor named
  it. Steps 53 to 61 sit in the post process and effect module by address, so they are most
  likely further effect objects, which is a guess and marked as one.
- Which of the eight debug pages (63 to 70) store to which global for 65, 66, 68, 69, 70. The
  three that IDA already names are listed.
- Whether `sub_6E4FF0(3.0, 0, 0.0)` at step 83 is a fade or a timer. Not traced.
- `sub_629840` at step 22. Not traced.
