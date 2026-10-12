# Threads and clocks: every thread the game makes and every wall clock it reads

Written 2026-10-12 for Project Zanarkand items 1.1.9 (threads) and 1.1.10 (wall clock readers).
Read only from the FFX.exe IDB (VA = RVA + 0x400000). The decompiles behind every row are in the
agent scratch directory under `agent-research/thr_*`, `thr2_*`, `thr3_*`, `rt_*`, `rt2_*`,
`vs_*`, `vs2_*`, `clk_*`, `clk2_*`, `clk3_*`, `nofunc_*`. Where a site sits in code IDA has not
made a function of, the row says `nofunc` and gives the instruction address.

Classification rule for part 2, from the brief: a reader is **simulation** if the per step hash
can see its value or it changes which step does what, and **presentation** if only the eye can
see it. The port decision is one of: **step clock** (derive from the step counter and the fixed
step period), **display clock** (keep a real clock, but it may only feed draw), **remove**,
**keep** (a real clock on a thread the simulation never reads), **session seed** (for RNG).

Companion docs: `BOOT_SEQUENCE.md` (where each thread is created in the boot), `MAIN_LOOP.md`,
`INPUT_LAYER.md` (the threaded pad mode), `FMV_SYNC.md` (the movie clocks in depth),
`RENDER_DEVICE.md` (Present), `CUTSCENE.md` (the voice syscalls).

---

# Part 1. Threads

## 1.1 The four ways this binary makes a thread

| primitive | VA | what it does |
|---|---|---|
| Phyre thread | `sub_4300B0` | `CreateThread(0, desc+4 stack, desc+0 fn, desc+8 arg, 0)`, names it with the `RaiseException(0x406D1388)` debugger convention (name from desc+16, or `Phyre__GetMainThreadName()` = "PhyreEngineThread"), then desc+12 priority mode: 0 -> `SetThreadPriority(1)` above normal, 2 -> `SetThreadPriority(-1)` below normal, anything else leaves it normal. |
| Sys::Thread | `sub_679CB0` | `CreateThread` with `CREATE_SUSPENDED` unless a3 == 1, stack from a2, name a5. The procedure `sub_679DB0` calls the object's vtable slot 1 (run) and then `ExitThread`. Vtable `.?AVThread@Sys@@`. |
| FFX_Thread_Create | `0x6F2E10` | `FFX_Alloc(0x18)` crossThreadImpl (vtable 0xB4BBCC), stores fn at +16 and arg at +20, default name "UnknownThread", creates it through Sys::Thread suspended. `crossThreadImpl__run` is the slot 1 body that calls fn(arg). `FFX_Thread_Start 0x6F2FF0` = `ResumeThread`, `FFX_Thread_SetPriority 0x6F3050` = `SetThreadPriority(handle, n)`. `FFX_Sema_Create(n)` = `CreateSemaphoreW(n, n)`. |
| CRT | `_beginthreadex` (libvpx), raw `CreateThread` (one library helper) | see T12 and T13 |

`CreateThreadpoolWork` and friends are imported but no caller was found. `SuspendThread` /
`GetThreadContext` / `ResumeThread` at 0x6885F0.. are the crash stack walker, not creation.

## 1.2 The thread census

Every thread the process can have, including the ones it cannot. "Publishes to the step thread"
is the mechanism by which the thread's results reach the simulation, which is what the port has
to preserve or replace.

### T0. Main thread

- Created by: the OS. Phyre profile name "MainThread" (`sub_60ED70("MainThread", 0x2000)` in
  `Phyre__PApplication__onInit`).
- Role: `WinMain` pump and limiter, `Phyre__PApplication__frameTick`, `FFXApplication::update`,
  `animate` (`FFX_MainStepLoop` -> `FFX_MainStep`, the whole simulation), the PS2 style display
  list build, the swf UI, and the submission of the Phyre render command list. Everything that
  reads input or writes save data in retail runs here, with the three exceptions marked HAZARD
  below (T4 boot, T5 voice timers, T9 play time).

### T1. PhyreEngineRenderThread

- Created by: `sub_615140` (from `Phyre__PApplication__onInit 0x62A600` via `sub_614410`, which
  returns the render thread singleton at `unk_CC96F8`, lazily built by `sub_614010`). Phyre
  primitive. Name "PhyreEngineRenderThread", stack 0x10000, priority mode 1 (normal). Start
  routine `sub_614710` -> `sub_616010` (profile name "RenderThread"). Error string "Error
  initializing Phyre Render Thread with error". `this+12 = 1` once running.
- Body `sub_616010`: a 16 slot ring at `this+20` (5 dwords a slot: +4 list begin, +5 list end,
  +6 slot event, +8 yield flag), consumer index `this+84`, producer index `this+85`. Each
  iteration waits the slot event (`sub_4302A0`), reads `this+12`: 1 -> `sub_615550(begin, end)`
  executes the command list, 2 -> exit, 0 -> "Render thread running without initialize() being
  called." `sub_615550` is the command executor: `sub_6148D0` issues the dynamic mesh jobs
  ("issued job count overflow. Dynamic mesh render error will occur"), `sub_614C60` is the
  command switch (cases 4.. for viewport, scissor, clear, ..), and **case 0x614D89 calls
  `Phyre_RenderD3D11_Present 0x593760`**. So `IDXGISwapChain::Present` runs on this thread, not
  on the step thread. The thunk `j_Phyre_RenderD3D11_Present 0x6148B0` has no callers.
- Producers: `sub_615210` (`PRendererBase__vf02`, which is slot 2 of the PRenderer vtable at
  0xB363F8 and of the PRendererBase vtable at 0xB363E0) posts (begin, end, flag) into the next
  slot and sets its event. `sub_6152A0` (`PRendererForJobs__vf02`) is the per worker variant and
  is never used (see T2).
- Flush `sub_615EA0`: when `this+12 == 1`, takes an event from the 16 event pool (`sub_614B70`,
  interlocked bitmask at `this+99`), posts a one entry list of kind 44 whose callback
  `sub_614700` sets that event, waits on it, returns the event (`sub_615AC0`). Callers of the
  flush: `sub_5BC3F0` (called by `Phyre__PApplication__frameTick 0x627B4A` right after the per
  viewport render and before vtbl+32 `render`, by `WinMain` teardown, `onExit`, `sub_624DA0`,
  `sub_62C480`, `sub_65D240`, `sub_670B20`, `sub_671300`, `sub_6D5050`, `sub_A3BA50`,
  `sub_A3D240`, `sub_A3D410`, `sub_A3D720`), `PRenderer__vf01`, `sub_638F90` (before
  `FFX_MenuSys_CreateTexLoad`), `sub_638FB0`, `sub_615F40`, and a dead thunk at nofunc
  0x642C10.
- Publishes to the step thread: nothing. The step thread joins it every frame through the
  flush, so the two run as a one frame deep producer and consumer with a hard join.
- Port: keep a render thread or fold it into the frame, either is observably identical, the
  simulation never reads anything from it. `MAIN_LOOP.md` line 19 ("One thread. No render
  thread.") is wrong and is listed in the corrections.

### T2. PhyreEngineWorkerThread (never created)

- Would be created by: `sub_44CE70` ("PhyreEngineWorkerThread", format "WorkerThread%d") from
  `PThreadPoolWorkStealing__vf02 0x57D581`, start routine `sub_44CE30`.
- Why it never exists: `Phyre__PApplication__run 0x629A40` calls `sub_431170(1)` ->
  `sub_44CE10`, `dword_C0B950 = 1`. `Phyre_Framework_Init` then calls `sub_44CD30`, which picks
  `PThreadPoolImmediate` (`sub_57CF50`) when `dword_C0B950 <= 1` and stores it in
  `dword_C91BA8`. `PThreadPoolImmediate` runs every job inline on the caller. `onInit` also
  checks `sub_44CDE0()` (same global) and skips `sub_5BBC30`, the per worker renderer. The
  job classes `PANIThreadPoolJob`, `ANIThreadPoolJobs`, `PRendererThreadPoolJob` (`sub_5B3DF0`)
  therefore all run inline on the main thread.
- Port: nothing to port. PORTING_RULES E3 and `MAGIC_DLL.md` line 297 attribute the JobWorker
  threads to this pool, which is wrong (corrections list).

### T3. PhyreEngineBulletThread (dead)

- `sub_5FFC50`, name "PhyreEngineBulletThread", stack 0x20000. Zero callers (the only other
  reference to the name string is at nofunc 0x5FFE11, which is also unreachable). Dead code.

### T4. JobWorker x4 (the FFX job pool)

- Created by: `sub_6DBA20` (`BOOT_SEQUENCE.md` step 35) on the JobSchedule object
  `dword_CDEDE4` (0x238 bytes, ctor `sub_6DB670`, vtable `.?AVJobWorker@JobSchedule@@` for the
  workers at `this+20*i`). `sub_703A50(worker, 0x20000)` -> Sys::Thread
  `sub_679CB0(worker, 0x20000, 0, 0, "JobWorker")` suspended, `SetThreadPriority(handle, 0)`
  normal, `ResumeThread`. Four of them.
- Body `JobWorker__vf01 0x703A20`: forever: `sub_6DB8A0` pops a job (wait the job count
  semaphore [140], take the lock [139], read the 8 slot job ring at +512 through index [136],
  release [139]), `sub_703910` runs it by kind (+4: 0 -> fn(), 1 -> fn(+20), 2 -> fn(+20,+24),
  3 -> fn(+20,+24,+28)), `sub_6DB800` returns the 48 byte slot to the free ring (lock [127],
  free ring at +464, index [125], count [126], release the free count semaphore [141]).
- Posters: `sub_633670` (boot only, `FFX_BootSequence` 0x64184D and 0x641957, posts
  `FFX_ParseCommandLineAndInitDebug_tramp` with argc and argv) and `sub_71E720 0x71E720` (kind 3:
  takes a free slot, fills fn, a3, and pointers to a4 and a5, pushes it on the job ring). Drain
  tests: `sub_6DBF30(ring, 0)` true when all 8 slots are free (boot waits on it), `sub_6DBF50`
  (the per frame wait behind `sub_71E810`).
- Users in the running game: `FFX_Ch_DispatchUpdateJob 0xA44B90` posts `FFX_Ch_UpdateRenderJob`
  per character per step (this is where `FFX_Ch_UpdateBlink 0x8344F0` and the `sub_77C8D0` ->
  `sub_77F360` / `sub_77FF00` noise run, on a worker), and `FFX_Vfx_FlushPrimBuildQueue
  0x71EDB0` (from `FFX_MainStepLoop` after the step loop) posts 6 `sub_71ED10` jobs when the
  queued VFX prim cost is >= 500 or the count >= 50, and runs them inline otherwise.
- Publishes to the step thread: results are written in place (CHR render fields, VFX prim
  buffers, and at boot every global `FFX_MainInit` touches). The join is `sub_71E810` in
  `FFX_Frame_PresentScene 0x642BB0` (before the display list is submitted) and `sub_6DBF30` in
  `FFX_BootSequence`.
- HAZARD (boot): `FFX_MainInit` runs here, not on the step thread, including the wall clock RNG
  seed. `BOOT_SEQUENCE.md` section 11.
- HAZARD (RNG): MSVC `rand()` keeps per thread state, so each worker has its own unseeded CRT
  stream (initial state 1) and which worker takes which character is scheduling dependent. The
  blink and sway results are render only.
- Port: a pool or inline, the recreation's choice. The step hash must be taken after the join
  or must exclude the fields the jobs write. The worker side `rand()` stays a cosmetic stream.

### T5. FmodManager

- Created by: `sub_6F9DE0` (the FmodManager ctor, 0x50 bytes at `dword_CE9010`, from
  `sub_6FA0B0` <- `sub_679FC0` <- `FFXApplication__initApplication`, `BOOT_SEQUENCE.md` step
  81). A second copy of the same creation sits at nofunc 0x6FA130 (unreachable). Two mutexes
  (+16, +20), two manual reset events (+24 kick, +28 sync done), base time at +32 from
  `FFX_Time_NowMilliseconds` (really microseconds, see row C9). `FFX_Thread_Create(desc{fn
  sub_6FA1B0, stack 0x10000}, "FmodManager")`, `FFX_Thread_Start`, then
  `FFX_Thread_SetPriority(obj, 300)`. 300 is not a valid `SetThreadPriority` value, the call
  fails and the thread stays at normal priority.
- Body `sub_6FA1B0` -> loop `sub_6FA700`: `elapsed = NowMicroseconds/1000 - base`, if less than
  33.333 ms wait the kick event for the remainder, `ResetEvent`, `FfxFmod_DrainCommandQueue`,
  else `FfxFmod__f7070D0(this+8)` (the per tick voice and stream service) and advance the base
  by whole 33.333 ms periods, then drain. So the audio thread ticks at 30 Hz and drains the
  command ring on every wake.
- Publishes to the step thread: the step thread posts through `FfxFmod_QueueCommand 0x6FA300`
  into the 150 entry ring at 0xCE8458 (write index +40, read index +44) and sets the kick event
  for commands 8 and 58 during boot scenes. Synchronous commands put their args at +48/+52 and
  wait on the sync event +28 for the result at +72. The reverse direction is the voice state:
  `sub_70B750` (on this thread, from `FfxFmod__f7070D0`) stamps a voice start at +112 and the
  elapsed time at +120/+124 against the duration at +108, and calls `FmodVoice__f70A590` when
  the line is over. The ATEL voice syscalls 213, 214 and 215 (`CUTSCENE.md` section 2.6) poll
  the voice object for completion, which is how a wall clock on this thread decides which step
  a cutscene script advances on. The exact field the syscalls read back was not traced in this
  pass.
- Port: keep an audio thread with its own real clock (row C9 to C11), but the step must never
  read a wall clock derived audio field for a decision. "Voice done" has to become a step
  ordered event (the host decides the step), see the decision table.

### T6. AsyncControllerTaskManager (the input thread)

- Created by: `sub_68C830` from the ctor `sub_68DB00` (0x44 bytes at `dword_CCC860`,
  `BOOT_SEQUENCE.md` step 40). `CreateMutexW`, two `FFX_Input__getTimeSeconds` stamps,
  `FFX_Thread_Create(desc{FFX_InputThread__main, stack 0x8000}, "AsyncControllerTaskManager")`,
  `FFX_Thread_Start`. No priority call.
- Body `FFX_InputThread__main 0x68D740`: while the exit flag `dword_CCC860+60` is clear, if
  `g_ffxThreadedPadMode 0x133C930` is set, poll `FFX_Input__pollThreaded 0x630CB0` once every
  0.01667 s by `FFX_Input__getTimeSeconds`, otherwise `Sleep(16)` through `sub_6F2D20`.
- Publishes to the step thread: the threaded sample queue, popped by
  `FFX_Input__consumeThreadedSample 0x630DE0` at the top of `FFX_MainStepLoop` (`INPUT_LAYER.md`
  "threaded mode"). The mode is set to 1 at 0x891C7E and cleared at 0x891FD8 inside
  `FFX_BtlOd_LuluFuryStickMinigame 0x891B80`, so in retail it is only on during Lulu's Fury
  stick minigame.
- Port: remove. Sample the pad once per step on the step thread (the lockstep layer owns the
  sample anyway). The fury minigame's rotation counting then has to count per step (row C4).

### T7. AsyncLoadManager

- Created by: `AsyncLoadManager__startWorkerThread 0x691180` from `AsyncLoadManager__ctor`
  (`BOOT_SEQUENCE.md` step 41). `FFX_Sema_Create(1)` x2 at +8 (work) and +12 (done),
  `FFX_Thread_Create(desc{AsyncLoadManager__workerThread, stack 0x8000, 999 in the unused desc
  field}, "AsyncLoadManager")`, `FFX_Thread_Start`, `FFX_Thread_SetPriority(obj, 1)` above
  normal. (The IDB comment says "priority 999", the 999 is a desc field that nothing reads, the
  real priority is 1.)
- Body `AsyncLoadManager__workerThread 0x691360`: wait the work semaphore, swap the double
  buffered request queue under the mutex at +16, run each request's vtable slot 1 (execute) then
  slot 2 (release), `FFX_Sema_Release(+12)`.
- Publishes to the step thread: the request objects (200 `LoadClusterRequest` and 150
  `RegistTaskRequest` from the pools at `dword_CCC890` / `dword_CCC894`) are completed in place,
  and the step thread polls `AsyncLoadManager__isIdle` (boot) or the request state
  (`CHR_RESIDENCY.md`, map loads). Completion is therefore consumed at a poll point on the step
  thread, which is a step boundary already.
- Port: keep an IO thread. The lockstep layer must make "load complete" a step ordered event,
  otherwise two peers with different disks consume the completion on different steps.

### T8. Read command (the AutoTest stdin reader)

- Created by: `AutoTestManager__ctor 0x6BCA90` (`BOOT_SEQUENCE.md` step 51), in retail, with no
  flag. `FFX_Thread_Create(desc{AutoTestManager__stdinThread, stack 0x8000}, "Read command")`,
  `FFX_Thread_Start`. No priority call.
- Body `AutoTestManager__stdinThread 0x6BD1E0`: `std::getline(std::cin)` forever. The line
  "EnableAutoTest" runs `AutoTestManager__execCommand` directly on this thread (which calls
  `FFX_LoadEventIdTable` and `sub_42F690`), anything else is queued into the vector at
  `this+0x118`.
- Publishes to the step thread: nothing in retail. `AutoTestManager__drainCommandQueue 0x6BCFF0`
  has no callers (orphaned, `DEBUG_MODE.md`). With a console attached and "EnableAutoTest"
  typed, `execCommand` touches game state from this thread, which is a debug only hazard.
- Port: remove.

### T9. VsyncEmulationThread

- Created by: `FFX_VsyncEmu_StartThread 0x679E60`, called from `sub_888680(fn)` <- `sub_8886F0`
  <- `tklib__f88D710 0x88D728` <- `FFX_MainInit`, that is on a JobWorker thread during boot
  (`BOOT_SEQUENCE.md` step 97). `VsyncThreadImpl` (0x20 bytes, `g_ffxVsyncEmuThread`, vtable
  `.?AVVsyncThreadImpl@@`): +4 handle, +16 callback (= `sub_887D60`), +24 run flag, +28
  semaphore (1,1). Sys::Thread `sub_679CB0(obj, 0x8000, 0, 0, "VsyncEmulationThread")`
  suspended, `SetThreadPriority(handle, 1)` above normal, `ResumeThread`.
- Body is vtable slot 1, misnamed `VsyncThreadImpl__stop 0x679C60`: `while (run) { callback();
  WaitForSingleObject(sema, 16 ms); }` then `TerminateThread` on its own handle. The semaphore is
  released by `FFX_VsyncEmu_SignalPresent 0x679F80` from `FFXApplication__render 0x42F968` right
  after `FFX_Frame_PresentScene`, so the callback fires once per presented frame and at least
  once every 16 ms. It is the PC stand in for the PS2 VSync interrupt handler chain.
- Callback `sub_887D60`: if `dword_13301F0` (enabled by `sub_887F40` -> `sub_887E80(1)`), call
  the slot 0 handler `dword_133026C[0]`, and when the disable count `dword_13301E0` is 0 also
  slots 1 and 2. Slots are set by `sub_888790(slot, fn)`: slot 0 starts as `nullsub_127`
  (`sub_887E30`), slot 1 is never set, **slot 2 = `sub_86CA20`**, registered by `FFX_Atel_Init
  0x86D7C8`.
- HAZARD: `sub_86CA20` is the play time counter. It reads `FFX_Time_AppElapsedSeconds`, and
  when more than 1.001 s has passed since `flt_1325B84` it adds the whole seconds to
  `saveData[47]` (capped at 3599999 = 999:59:59) and moves the mark. **It writes the save block
  from this thread, from the wall clock, concurrently with the step.** Row C5.
- Stop paths: `sub_888830` (`sub_822750` thunk, callers not found) and `sub_888760` (nofunc
  0x88E996) both reach `sub_888680(0)` -> `FFX_VsyncEmu_StopThread` (clears the run flag, the
  body then `TerminateThread`s itself). `sub_6F2AE0/BB0/CC0` are PS2 intr handler stubs that
  return 0, `dword_1330268` holds their fake handle.
- Not a thread, same module: `hdd_thread_main 0x7795xx` is registered through `sub_888740` into
  `dword_1330254` (by `sub_76E630` in tklib init and `FFX_AtelSys_Movie_144_resi`), and its
  would be scheduler loop at nofunc 0x8885C0 (`FFX_Sema_Wait(dword_1330250[idx])` then call
  `dword_1330254[idx]`) has no xrefs, so it never runs. `sub_888820` (`FFX_Sema_Release
  (dword_1330250)`) is called by the ROM device read path (`FFX_RomDev_ExecuteQueuedRead`,
  `sub_A432A0`, `sub_778550`, `sub_778610`, `sub_7797A0`, `sub_76E1C0`, Movie 144) and releases
  a semaphore nobody waits on. Dead on PC.
- Port: remove the thread. Advance `saveData[47]` from the step counter (one second per 60
  simulation steps at the 60 Hz base, or per 30 at divider 2, whichever the recreation's step
  period is). It is save data and it is in the hash, so it has to be step derived.

### T10. PVideoPlaybackWin32::_ReadingThread (per movie)

- Created by: `PVideoPlaybackWin32___ReadingThread 0xA27CF0` is the START function (not the
  body), called from `sub_6D8F30` / `sub_6D8FC0` on the player at `g_ffxFmvPlayerManager+56` when
  a movie opens. Phyre primitive with desc{proc `sub_A27880`, stack 0 (OS default), arg this,
  name "PVideoPlaybackWin32::_ReadingThread"}, handle at `this+192`, event at `this+684`.
- Role: reads the mkv container from disk into the demux buffer ahead of the decoder.
- Publishes to the step thread: nothing directly, it feeds T11.

### T11. PVideoPlaybackWin32::_VideoDecodingThread (per movie)

- Created by: `PVideoPlaybackWin32___VideoDecodingThread 0xA27F20` is the START function: it
  stamps `dword_1A84C80 = GetTickCount()` (row C2), parses the mkv header, allocates 0x400000
  through `sub_A3DDF0`, and creates the thread with the Phyre primitive, proc `sub_A27B90`, name
  "PVideoPlaybackWin32::_VideoDecodingThread". Callers `sub_66FB70`, `sub_6D8FC0`, `sub_6DA9B0`
  and nofunc 0x666904.
- Role: VP8 (libvpx) video and Vorbis audio decode, frame timing against its own clock.
- Publishes to the step thread: the decoded frame ring and the frame index that
  `FFX_Fmv_DrawFrame` draws and that `FFX_StepPacing`'s pacing mode 1 (the FMV timing track)
  steps against. `FMV_SYNC.md` has the full treatment and the decision (frames are consumed by
  step count, not by wall time).
- Port: keep decode off the step thread, consume by step count.

### T12. libvpx decode workers (conditional, probably never)

- `sub_40C350` (libvpx `vp8_decoder_create_threads`): `_beginthreadex(0, 0, StartAddress, ..)`
  for `min(cfg.threads, 8, mb_rows) - 1` threads, only when that is > 1. `cfg.threads` is
  `pbi+15704`, copied in `sub_40AE10` (`vpx_codec_dec_init`) from the `vpx_codec_dec_cfg_t` at
  `a2+16` that the caller `sub_409C70` passes. Where Phyre fills that cfg, and therefore whether
  the retail build ever asks for more than one thread, was not resolved. libvpx's default is one
  thread and no workers.
- Publishes: internal to the decoder. No port decision needed beyond T11.

### T13. Unidentified library housekeeping thread

- `sub_948C10` -> `CreateThread(0, 0, sub_948C30, fn, 0, 0)` where `sub_948C30` just calls fn.
  Called from `sub_947630` (<- `sub_9428B0`, a lazy init reached from the static initialiser
  `sub_AF1290` and from nofuncs at 0x94261C, 0x9427FD, 0x942967) with fn = `sub_947E30`: `while
  (byte_C613AC) { WaitForSingleObject(off_C613B0[1821], 1000 ms); sub_9478E0(off_C613B0, 0);
  ++off_C613B0[1819]; }`. A 1 Hz tick on a 7 KB table at `off_C613B0`, no strings, in the
  0x942000..0x948000 region between Lua and the CRT. Not identified. It never touches FFX
  state that this pass could see.
- Port: ignore until identified. Unresolved.

### T14. Threads inside DLLs (not visible in the exe)

Steam (`SteamAPI_Init` in `FFX_SteamApiInit 0x623470`), FMOD's mixer and stream threads (the
"FMOD=%s" memory report line is FFX's, the threads are fmod's), the D3D11 runtime, DXGI and the
driver. None publishes to the step thread except through the FFX wrappers already listed (T5
for FMOD, `RENDER_DEVICE.md` for D3D11, and Steam callbacks pumped from the main thread).

### Summary for PORTING_RULES E3

Retail FFX.exe runs the simulation on the main thread and has exactly these other threads of
its own: `PhyreEngineRenderThread` (Present and the Phyre command list, joined every frame),
four `JobWorker` (character render update jobs, VFX prim builds, and the whole of `FFX_MainInit`
at boot, joined before submit), `FmodManager` (30 Hz audio service, command ring in, voice
timers out), `AsyncControllerTaskManager` (pad polling, only active in Lulu's fury minigame),
`AsyncLoadManager` (file loads, polled), `Read command` (stdin, inert in retail),
`VsyncEmulationThread` (16 ms tick, runs the play time counter into the save block), and per
movie `PVideoPlaybackWin32::_ReadingThread` and `_VideoDecodingThread`. The Phyre worker pool
is sized to one and is inline, the Bullet thread is dead code, and libvpx's workers are off
unless Phyre asks for them.

---

# Part 2. Clocks and RNG

## 2.1 The clock stack

```
timeBeginPeriod(5), QueryPerformanceFrequency, QueryPerformanceCounter, timeGetTime      Phyre_Time_Init 0x42FE10 (boot, once)
GetProcAddress("GetSystemTimePreciseAsFileTime") -> g_phyrePreciseSystemTimeFn 0xC9004C      "
Phyre_Time_NowSeconds 0x42FD80 = precise ? FILETIME/1e7 : QPC * g_phyreSecondsPerCount       the one real clock
g_phyreTimeOrigin 0xC90278 = NowSeconds() at Phyre_Framework_Init 0x430F0D                   the app origin
sub_42F690 = NowSeconds() - origin                                                           app elapsed, double
FFX_Time_AppElapsedSeconds 0x641410 = (float) sub_42F690()                                   the FFX game side clock
FFX_Input__getTimeSeconds 0x630C60 = NowSeconds() - origin (float)                           the input side clock
FFX_Time_NowMilliseconds 0x641420 = (int64)((NowSeconds() - origin) * 1000000.0)             MICROSECONDS, see C9
```

`FFX_Time_NowMilliseconds` multiplies by `dbl_B3ECC0 = 1000000.0` (disassembly at 0x64142B),
not 1000. Every consumer divides by 1000.0 to get milliseconds (`sub_6FA700`, nofunc 0x6FA210,
`FFX_StepPacing` to get its ms track time) or compares against microsecond constants
(`FfxFmod__f7070D0`: `> 0x80E8` = 33000 us = 33 ms). The IDB name and comment are wrong, and
so are the names `g_ffxTimingTrackStartMs`, `g_ffxTimingTrackPauseMsPending` and
`g_ffxTimingTrackPauseMsTotal`, which hold microseconds. Proposed renames are in the hand-back.

`dword_C90044` (the `timeGetTime` sample) and the three QPC samples `dbl_C90058`,
`dword_C90048`, `dbl_C90050` are written by `Phyre_Time_Init` and read by nothing else. The
raw QPC helper at nofunc 0x42FD60 has no callers. The Phyre `PTimer::GetTime` reflected method
`Phyre__GetTime 0x42F330` exists only for Phyre scripts (`PTimer__GetClassDescriptor`).

## 2.2 Raw OS clock readers

| row | VA | function | feeds | class | port |
|---|---|---|---|---|---|
| C1 | 0x6EA100 | `sub_6EA050` (allocator init, from `sub_6EBD30` and friends, boot) | `dword_CE6EC4 = (GetTickCount() ^ 0x55555550) & ~0xF | 8`, the header cookie of the 1 MB arena allocator (`BOOT_SEQUENCE.md` step 78) | presentation (heap header bytes only, but a raw memory hash of that arena would see it) | remove, use a constant cookie |
| C2 | 0xA27F69 | `PVideoPlaybackWin32___VideoDecodingThread` | `dword_1A84C80`, the decode clock base of the movie, read by nothing else in the exe (the decoder uses it internally) | presentation (decode pacing) | keep on the decode thread, frames are consumed by step count (`FMV_SYNC.md`) |
| C3a | 0x6F0528 | `sub_6F0510` (save file write, <- `sub_6F1C40` <- `sub_647E10`) | `dword_CE730C = GetTickCount()` at the moment a save is written | simulation adjacent (it is the base of C3b) | remove with C3b |
| C3b | 0x6F0674 | `sub_6F0670` (<- `FFX_MainStep_StepSaveScanAndCachePlayerPos 0x648480`, every step) | `(GetTickCount() - dword_CE730C) / 2000.0` clamped to 0..1, a 2 s ramp. When it reaches 1.0 the step calls `sub_641680`, which is a nullsub | simulation by position (it is on the step), effect nil in retail | remove (the gated call is empty) |
| C4a | 0x42FE2F, 0x42FE67, 0x42FE92 | `Phyre_Time_Init` | the stored samples above, read by nothing | none | remove |
| C4b | 0x42FDD9 and `g_phyrePreciseSystemTimeFn` at 0x42FD83 | `Phyre_Time_NowSeconds` | everything in 2.3 to 2.6 | see the consumers | the recreation keeps one real clock for display and pacing, the step never reads it directly |
| C4c | 0x62F5C8 | `FFX_Time_ReadSystemTimeFields 0x62F5B0` (`GetSystemTime`) | 8 byte buffer -> `FFX_Rand_SeedValueFromSystemClock` | simulation (row R1) | session seed |
| C4d | 0x6F0A95 | `sub_6F0A70` (`GetTimeZoneInformation` + `SystemTimeToTzSpecificLocalTime`, <- `sub_6F0950` save scanner states) | the local time string of a save file's timestamp for the load menu | presentation | keep (display only) |
| C4e | 0x6DCB0C..0x6DE184 (18 sites, nofunc) | the debug memory info logger ("MapLoad", "%s_MapUnload", "DebugMemoryInfo clearred", `_time64`) | debug log timestamps. No callers into the region were found | none (dead debug code) | remove |
| C4f | 0x950952, 0x95F4FB, 0x95F785, 0x950A94, 0x95FD14 | Lua: `lstate_makeseed` (`lua_newstate`), `lua_os_date`, `lua_os_time`, `sub_950A90`, `sub_95FD10` | Phyre's PScripting Lua state (`PScripting_CreateLuaStateForPScript`, `PScripting_RunStandaloneScriptFile`, the `-script` proxy). No game logic runs in Lua | presentation (tooling) | remove with the script proxy, or keep for tools |
| C4g | 0x94A258, 0x94A270, 0x94A27D | `___security_init_cookie` (`GetSystemTimeAsFileTime`, `GetTickCount64`, QPC) | the CRT /GS cookie at process start | none | n/a (CRT) |

`GetLocalTime` is not imported. `GetTickCount64` and `GetSystemTimeAsFileTime` have no caller
outside the CRT cookie.

## 2.3 Phyre_Time_NowSeconds readers (13)

| row | VA | function | feeds | class | port |
|---|---|---|---|---|---|
| P1 | 0x627989 | `Phyre__PApplication__frameTick` | `frameDelta = now - origin - app+10`, into `app+0x24`, which `FFX_StepPacing` clamps to 0.3 s and turns into `g_ffxPendingSteps` (how many `FFX_MainStep`s run this frame) | **simulation** (it decides the step count per frame) | step clock. In lockstep the layer decides the step count, the real clock only paces presentation |
| P2 | 0x62F2EC | `_WinMain` | the `Sleep` limiter | presentation | display clock |
| P3 | 0x62A64D | `Phyre__PApplication__onInit` | `app+10`, the start offset P1 subtracts | presentation (part of P1) | with P1 |
| P4 | 0x628E19 | `sub_628DF0` (<- `onInit`, `sub_66B1E0` boot frame) | resets `app+9` dt and `app+8` total | presentation | with P1 |
| P5 | 0x62574E | `sub_625720` (<- `sub_62BB60`, the `-script` proxy) | script mode frame timing | none in retail | remove |
| P6 | 0x430F08 | `Phyre_Framework_Init` | `g_phyreTimeOrigin` | the origin | keep (one origin) |
| P7 | 0x430D49 (nofunc) | boot helper, no callers | `Phyre_Time_Init` + origin again | none | remove |
| P8 | 0x570517, 0x570525 (nofunc, between `PSpriteAnimationInfoInstance_ClassDescForType__vf00` and `sub_5705E0`) | a Phyre sprite animation timer helper with a double at `this+0x18`, no callers found | unknown, probably Phyre sprite animation, not used by FFX | unresolved, presentation if anything |
| P9 | 0x42F330 | `Phyre__GetTime` (PTimer reflected method) | Phyre scripts | none in retail | remove |
| P10 | 0x42F697 | `sub_42F690` | `FFX_Time_AppElapsedSeconds` (2.4), `AutoTestManager__execCommand` (JumpMap start stamp, debug), `AutoTestManager__drainCommandQueue` (dead) | see 2.4 | see 2.4 |
| P11 | 0x630C64 | `FFX_Input__getTimeSeconds` | 2.5 | see 2.5 | see 2.5 |
| P12 | 0x641420 | `FFX_Time_NowMilliseconds` (microseconds) | 2.6 | see 2.6 | see 2.6 |
| P13 | 0x42FD83 | the precise path inside `Phyre_Time_NowSeconds` itself | | | |

## 2.4 FFX_Time_AppElapsedSeconds 0x641410 readers (31 functions, 65 sites)

MAIN_LOOP.md says 30 callers of which 20 reach the simulation. The count is 31 functions, and
the simulation side is smaller but sharper than "mostly battle menu UI pulsing": the battle
menu page timers gate input acceptance, the play time counter writes save data from another
thread, and the fury minigame measures its window in real time. The pure draw pulses are the
majority.

| row | VA(s) | function | feeds | class | port |
|---|---|---|---|---|---|
| A1 | 0x86CA33 | `sub_86CA20` (vsync thread callback slot 2, T9) | `saveData[47]` play time, whole seconds since `flt_1325B84` | **simulation** (save data, in the hash, written off thread) | step clock, on the step thread |
| A2 | 0x89AEF6, 0x89AF39 | `FFX_BtlMenu_Step 0x89AE20`, page state 2 | page record `+0xDC` = now, `+0xE0` = 0 | **simulation**, see A3 | step clock |
| A3 | 0x8A2DBB.., 0x8A369B.., 0x8A49D8.., 0x8A688C, 0x8A6A7B.., 0x8A7548, 0x8A760C.., 0x8A7DDC.. (23 sites) | the battle menu page callbacks installed by `FFX_BtlMenu_InstallPageCallbacks`: `sub_8A2DA0`, `sub_8A3680`, `sub_8A49B0`, `sub_8A6870`, `sub_8A6A60`, `sub_8A7520`, `sub_8A75F0`, `sub_8A7DC0`, and `sub_89F370` (6 callers among them) | `+0xE0 = now - +0xDC`, then `if (+0xE0 >= 0.3)` / `>= 0.45` / `>= 0.5` advance the page (comparisons in `sub_89F370`, `sub_8A6A60`, `sub_8A75F0`, `sub_8A7DC0`, `sub_8A87A0`, `sub_8A8A40`, `sub_8A8A70`, `sub_8A8AC0`, `sub_8A8AF0`, `sub_8A8B20`, `sub_8A8B50`, `sub_8A8B90`), and slide fractions `(t-0.2)/0.2`, `/0.3` for the draw | **simulation**: the open animation of a battle menu page decides on which step the page starts accepting input. A press on step N is taken on one machine and dropped on the other | step clock: count steps since the page opened, 0.3 s = 9 steps at the 30 Hz divider |
| A4 | 0x891BB6, 0x891BD6 | `FFX_BtlOd_LuluFuryStickMinigame 0x891B80` | `g_ffxBtlOdLuluClockNow`, `g_ffxBtlOdLuluClockMark`, the real time window the stick rotations are counted in (the IDB comment notes these are not the overdrive time limit, which is `g_ffxBtlOdTimeRemaining`) | **simulation** (the rotation count is the overdrive result) | step clock, and with T6 removed the rotations are sampled per step |
| A5 | 0x891565 | `sub_891480` (<- `FFX_BtlMenu_Step`) | `flt_133C944` mark, read by A6 | presentation | display clock |
| A6 | 0x893A50, 0x894542, 0x8945B9 | `sub_893A30` (<- `FFX_BtlMenu_DrawRoot`) | `(t - flt_133C944) / 0.2` slide of the root menu | presentation | display clock |
| A7 | 0x892956 | `sub_892910` (<- `sub_892CB0`) | `cos(t*3)*32+96` glow colour when a gauge is full | presentation | display clock |
| A8 | 0x896105 | `sub_8960E0` (<- `sub_8933C0`, `sub_896450`) | same glow formula | presentation | display clock |
| A9 | 0x89BCAC, 0x89BD17 | `sub_89BC80` (<- `sub_895390`) | battle menu draw timing | presentation | display clock |
| A10 | 0x8C05BD | `sub_8C0580` (menu draw helper, 10+ callers) | `cos(t*8)*3` pixel wobble | presentation | display clock |
| A11 | 0x8C069F | `sub_8C0670` (menu draw helper, `FFX_MesWin_DrawChoiceCursorAndArm`, `FFX_Menu_ConfigDrawRow`, ..) | `cos(t*8)*3` wobble | presentation | display clock |
| A12 | 0x8C49D1 | `sub_8C4770` (<- `sub_8C3D00`) | `frac(t) > 0.5` cursor blink | presentation | display clock |
| A13 | 0x8D36AB | `FFX_Menu_DrawGaugeBarClockPulsed 0x8D3690` | `cos(t*3)*32+96` glow | presentation (IDB comment agrees) | display clock |
| A14 | 0x8E7D98, 0x8E7DB2 | `sub_8E7D30` (<- `sub_89F370` x8, `sub_8A3680`, ..) | UV scroll | presentation | display clock |
| A15 | 0x8E8B0A.. (6 sites) | `sub_8E8A10` (<- `sub_8ED380`, `sub_8FE0B0`) | `cos(t*3)*10+118` into `+28`, a colour | presentation | display clock |
| A16 | 0x8F41B8 | `sub_8F41B0` (<- `sub_893A30`, `sub_89BC80`, `sub_8A0590`, `sub_8A0BA0`, `sub_8A2DA0`, `sub_8A32B0`, `sub_8F34C0`) | a pulsed quad draw | presentation | display clock |
| A17 | 0x8FEB36 | `sub_8FEB00` (<- `FFX_BtlMenu_DrawRoot`, `sub_8B3860`, `sub_8B4A50`, `sub_8BD280`, `sub_8BD670`) | `cos(t*8)*3` wobble | presentation | display clock |
| A18 | 0x6E854D, 0x6E8591, 0x6E85B3 | `sub_6E8540` (<- `FFX_Fmv_DrawFrame`) | movie fade in and out alpha | presentation | display clock |
| A19 | 0x6E78A7 | `sub_6E78A0` (<- `FFX_Fmv_PollSkipButtons`, on the START edge) | prompt object `+4` = now, `+20 = 1`, `+21 = 1`: the skip prompt appears | presentation (the skip itself is the SQUARE edge, not timed, per the IDB comment on `FFX_Fmv_PollSkipButtons`) | display clock |
| A20 | 0x6E78CC | `sub_6E78C0` (<- `FFX_Fmv_PollSkipButtons`, on the skip) | prompt `+8` = now, hide animation | presentation | display clock |
| A21 | 0x774F6E, 0x774F9C | `sub_774CB0` (<- `FFX_AtelSys_Movie_136_start`) | `cos(t*8)*2` added to a text position before `sub_63EEE0` | presentation | display clock |
| A22 | 0x68B538 | `sub_68B470` (<- `sub_68A3A0` <- `FFX_Input__updateFromFFXApp`) | an input record `+16` timestamp in the PC input bridge, state 2 | presentation unless a later reader compares it. Not traced further, `INPUT_LAYER.md` territory | display clock, verify |
| A23 | 0x68B268 (nofunc) | same module as A22 | unknown | unresolved | verify with A22 |

## 2.5 FFX_Input__getTimeSeconds 0x630C60 readers (9 functions, 10 sites)

| row | VA(s) | function | feeds | class | port |
|---|---|---|---|---|---|
| I1 | 0x630FF2 | `FFX_Input__ctor` | the initial hold timer stamp `this+34` | part of I2 | step clock |
| I2 | 0x6330E7 | `FFX_Input__updateHoldTimers` (<- `FFX_Input__poll`) | per button hold seconds `this+2..`, accumulated from real elapsed time | **simulation** if any reader of the hold seconds decides something (`INPUT_LAYER.md` lists the hold readers), presentation otherwise | step clock: hold = steps held |
| I3 | 0x6312CC | `FFX_Input__clearHoldTimers` | resets the stamp | with I2 | with I2 |
| I4 | 0x68C92B, 0x68C933 | `sub_68C830` (T6 creation) | thread start stamps | presentation | remove with T6 |
| I5 | 0x68D77B | `FFX_InputThread__main` | the 0.01667 s poll cadence | presentation (sampling) | remove with T6 |
| I6 | 0x68DC37 (nofunc) | input thread module | unknown | unresolved | remove with T6 |
| I7 | 0x889989 | `FFX_Pad__stepAutoRepeatMask 0x889980` (<- `FFX_Pad__stepAutoRepeatAllGroups`) | per group auto repeat remaining time decremented by real elapsed seconds, then the repeat bit in the emulated pad | **simulation** (the IDB comment calls it a co-op determinism hazard: the repeat press lands on different steps) | step clock, repeat in steps, and the lockstep input record carries the derived repeat |
| I8 | 0x8B7CE4 | `FFX_MesWin_SamplePadPort0 0x8B7CD0` (<- `FFX_MesWin_StepAll`) | the message window repeat mask | **simulation** | step clock |
| I9 | 0x8BE50C | `FFX_MenuSys_SamplePad 0x8BE500` (<- `FFX_MenuSys_StepFrame`) | the menu repeat mask at `g_ffxMenuPadBlock+0x26` | **simulation** | step clock |

## 2.6 FFX_Time_NowMilliseconds 0x641420 readers (microseconds, 11 functions, 16 sites)

| row | VA(s) | function | feeds | class | port |
|---|---|---|---|---|---|
| C9 | 0x6F9E60 | `sub_6F9DE0` (T5 ctor) | the audio thread base time `+32` | presentation (audio thread pacing) | keep, audio thread |
| C10 | 0x6FA709 | `sub_6FA700` (T5 loop) | the 33.333 ms tick | presentation | keep, audio thread |
| C11 | 0x7070DB | `FfxFmod__f7070D0` (T5 tick, also `sub_6FA880`, `sub_707BD0`) | `qword_CEC168` stream service stamp, serviced when > 33000 us | presentation | keep, audio thread |
| C12 | 0x70B25F | `FmodVoice__f70B160` (<- `FfxFmod__f7070D0`) | voice start stamp `+112` | **simulation** through C14 | see C14 |
| C13 | 0x70B641 | `FmodVoice__f70B5C0` (<- `sub_707540`) | a voice stamp | **simulation** through C14 | see C14 |
| C14 | 0x70B773, 0x70B78C | `sub_70B750` (<- `FfxFmod__f7070D0`) | `+120/+124` elapsed vs the duration at `+108`, and `FmodVoice__f70A590` when the line is over, which is what the ATEL voice syscalls 213 to 215 wait on | **simulation** (which step a voiced cutscene line is "done" on) | the host decides: "voice done" becomes a step ordered event. The field the syscall reads is the unresolved item |
| C15 | 0x6FA216 (nofunc) | `/ 1000.0` helper | no callers | none | remove |
| C16 | 0x821F07, 0x821F5B, 0x822000, 0x82207B | `FFX_StepPacing 0x821E80`, pacing mode 1 | `g_ffxTimingTrackStartMs` (us), the FMV timing track elapsed `g_ffxTimingTrackElapsed = us/1000`, compared against the track's due times to set `g_ffxPendingSteps`, with a spin wait when early | **simulation** (step count per frame during a movie) | step clock. `FMV_SYNC.md` has the decision |
| C17 | 0x8AB3D0 | `FFX_Cutscene_EnterPauseOverlay` | `qword_12FB820` pause start | **simulation** through C18 | step clock |
| C18 | 0x8AB2EE | `FFX_Cutscene_StepPauseOverlay` | `g_ffxTimingTrackStartMs += now - qword_12FB820` on resume, so the paused real time is removed from the track | **simulation** (shifts C16) | step clock (pause is a run of frames with zero steps, nothing to compensate) |
| C19 | 0x6903A1, 0x6903C1 | `FFEscMenu__scalar_deleting_dtor 0x68FFB0` (the Esc menu step, misnamed) | `this+240` open stamp, `g_ffxTimingTrackPauseMsPending = now - this+240` on close, consumed by C16 | **simulation** (shifts C16) | step clock |

## 2.7 RNG

| row | VA | function | feeds | class | port |
|---|---|---|---|---|---|
| R1 | 0x79895A -> 0x798894 -> 0x786E15 | `FFX_Rand_SeedValueFromSystemClock 0x798950` <- `FFX_Rand_SeedAllStreams 0x798890` <- `FFX_InitNewSaveData 0x786B00` (<- `FFX_Btl_Init` at boot on a JobWorker, `FFX_AtelSys_Core_573_BeginNewGame` at New Game, `SG_DebugWin_BattleConsoleProc`) | the only seed of the game RNG: `g_ffxBattleRandState 0xC42200` and the 68 dword `g_ffxRandStreamState 0x1135EE0`, from `GetSystemTime` fields xor folded, times `seed + 1` | **simulation**, the only entropy in the game RNG | session seed from the host, applied at the same two points (boot and New Game) |
| R2 | 0x7FF39F and 0x7FF3xx | `sub_7FF360` (<- `FFX_MainInit 0x82097B`, once) | `srand(0x3F4CF27B)` for the step thread's CRT stream and `g_ffxEffectRandState 0xC0A000 = -1.4980391f` for the effect float RNG | deterministic constants | keep |
| R3 | 54 sites in 29 functions at 0x72F4B0..0x732390 and 0x757490..0x7582E0 | `FFX_Rand_EffectFloat1to2 0x72F420` readers, the VFX operators, many of them Magic plugin host API entries (62..65, 108, 119, 130, 156, 158, 174, 191, 193, 573, 591) | particle and effect parameters from `g_ffxEffectRandState` | presentation, deterministic given call order on the step thread | cosmetic stream, excluded from the hash |
| R4 | 155 sites in 44 functions | `srand(g_ffxEffectRandState & 0x7FFFFF)` in 41 VFX operators (0x72F4B0..0x733E40, 0x757490..0x762460, `sub_9DBB40` host API 241), Hex-Rays renders the mask as `((unsigned int)&loc_7FFFFD + 2)` | reseeds the calling thread's CRT `rand()` from the effect state before drawing particle noise | presentation | cosmetic stream, keep the semantics so the look matches |
| R5 | 0x82395B | `sub_822DE0` (<- `sub_82E390`, per CHR per step on the step thread) | `rand() % 1000` as a float into a position passed to `sub_824E60` | **simulation** (step thread CRT stream, the phase depends on every other step thread `rand`/`srand` call) | move to the game RNG or a dedicated per step stream and hash it. Identify what `sub_824E60` does with it (unresolved) |
| R6 | 0x7FF72C | `maybe_FFX_Yonishi__feedPadStateBothPorts` (per sub step from `FFX_MainStep 0x82141D`) | `rand()` with the result discarded (a PS2 libc side effect kept by the compiler) | simulation adjacent (advances the shared step thread stream that R5 reads) | remove, or keep in the same step thread stream as R5 so the phase matches the original |
| R7 | 0x834542, 0x834583, 0x834592, 0x83452B | `FFX_Ch_UpdateBlink 0x8344F0` (<- `FFX_Ch_UpdateRenderJob`, on a JobWorker) | eye blink and head sway on bone 13, `CHR+0x4D4..0x4E3` | presentation (worker thread stream, scheduling dependent) | cosmetic stream, never hashed |
| R8 | 0x77F54C, 0x77F5AE, 0x77F6F9, 0x780122, 0x7801B3, 0x780313 | `sub_77F360`, `sub_77FF00` (<- `sub_77C8D0` <- `FFX_Ch_UpdateRenderJob`, on a JobWorker) | render job noise (`rand() % 90`, byte scaled) | presentation | cosmetic stream |
| R9 | 0x63E075, 0x63E09B, 0x63E301, 0x63E32D, plus nofuncs 0x63E522, 0x63E528, 0x63E54E, 0x63E737, 0x63E763 | `sub_63DF70` (<- `sub_822580`, `sub_822620` <- `FFX_DreamFilter_Update 0x8223A0` <- `FFX_MainStep`) | the PC dream filter noise and dirt offsets | presentation (full screen filter), on the step thread so it advances the shared stream | cosmetic stream |
| R10 | 0x6F8139 | `sub_6F8110` (<- `sub_6F8C50` <- `sub_6F7C10` <- `sub_6C35D0`, BlurEffect radial lines) | vertex noise | presentation | cosmetic stream |
| R11 | 0x71DCE4, 0x71DD05, 0x71DD26, 0x71DD28 | `sub_71DBD0` (<- `sub_7116D0` via host API table 0xC65A58, nofunc 0x711651) | effect noise for plugins | presentation | cosmetic stream |
| R12 | 0x75053B, 0x751EDB, 0x7541A9 | `sub_750510`, `sub_751EB0`, `sub_754180` (host API 153, 191, 613) | `return rand()` exported to the magic DLLs | presentation, unless a DLL feeds it into battle state (`MAGIC_DLL.md`, verify per DLL) | cosmetic stream |
| R13 | 0x810E9B | `sub_810E20` (<- `sub_8124B0` via table 0xC49318) | effect rand + `FFX_Rand_Scale` | presentation | cosmetic stream |
| R14 | 0x921172, 0x924E80, 0x92971C, 0x92E4F8, 0x93724B, nofuncs 0x92E438, 0x92E704 | `sub_9210E0`, `sub_924DF0`, `sub_9296D0`, `sub_92E4F0` (host API 469), `sub_937190` (<- `FFX_RcBg_Step`, `sub_936A00`), the rcBg background effects, and `rand_0` in the rcBg table at 0xC653F8 | sky, rain and particle backgrounds | presentation (on the step thread) | cosmetic stream |
| R15 | 0x411E9C, 0x412621, 0x4126A2, 0xA7F6FF, 0xA8B05E | `sub_411E80`, `sub_4124B0`, `sub_412690`, `sub_A7F6FA`, `sub_A8B059` (<- `sub_40D2C0`, `sub_412E00`, libvpx postproc) | the VP8 postprocessor noise and dither tables | presentation (movie decode) | keep in the decoder |
| R16 | 0x953EE7, 0x95405E, 0x954067 | `lua_math_random`, `lua_math_randomseed` | Phyre PScripting Lua, `-script` only | none in retail | remove with the script proxy |

Where the step thread's CRT stream stands at any moment is a function of R2 and every R4, R5,
R6, R9, R11 to R14 call made on that thread since, in order. All of those are deterministic
given the same step sequence, so the stream itself is reproducible across peers. What is not
reproducible is R7 and R8 on the workers, and those are render only.

## 2.8 The decision table

| decision | rows |
|---|---|
| step clock | P1, A1, A2, A3, A4, I1, I2, I3, I7, I8, I9, C16, C17, C18, C19 |
| display clock | P2, P3, P4, A5 to A22 |
| keep (another thread's own clock, the step never reads it) | C2, C9, C10, C11, R15, P6 |
| host ordered event instead of a clock | C12, C13, C14 (voice done), T7 load complete |
| remove | C1, C3a, C3b, C4a, C4e, P5, P7, P9, C15, I4, I5, I6, R16, T6, T8, T9, T13 |
| session seed | R1 |
| cosmetic stream, excluded from the hash | R3, R4, R7 to R14 |
| keep as is | R2 |
| unresolved | P8, A23, R5's consumer `sub_824E60`, the voice field C14's syscall reads, the vpx thread count (T12), T13's identity |

## 2.9 Unresolved, collected

- Which field of the FmodVoice object the ATEL voice syscalls 213, 214 and 215 read to decide
  "voice done" (C14). `CUTSCENE.md` section 2.6 has the syscall side.
- What `sub_824E60` does with the random position from `sub_822DE0` (R5).
- The sprite animation timer at nofunc 0x570500 (P8) and the input bridge stamp at 0x68B268
  (A23).
- Where Phyre fills `vpx_codec_dec_cfg_t.threads` (T12).
- The identity of the 1 Hz library thread `sub_947E30` (T13).
- Whether any hold seconds reader of I2 decides anything (the `INPUT_LAYER.md` hold readers).
