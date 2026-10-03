# The main loop, the frame step and the clock

How FFX.exe advances one frame, what it advances, and where a lockstep gate can go.

All addresses in this document are VAs. The project convention is RVA + 0x400000 = VA, so subtract
0x400000 to get the RVA that lives in `loader\workshop\include\ffx\addresses\MainLoop.h`.

Everything below was read out of the IDB by decompiling and by checking every xref to each global.
Where something is inference rather than something read in the code, it says so.

The library side of this is `ffx/addresses/MainLoop.h`, `ffx/MainLoop.h` and `src/ffx/MainLoop.cpp`.

## The headline

Four answers matter more than the rest.

1. **The frame limiter is in `WinMain`, not in the engine.** `WinMain 0x62EE50` runs its own
   `PeekMessageA` pump and `Sleep`s to hold one frame at `g_phyreFrameRateDivider * (1/59.94)`
   seconds. One thread. No render thread. No vsync wait of the engine's own.
2. **The game already ships a complete simulation stall, and it is one byte.**
   `FFXApplication + 0x3AD`. All four FFXApplication vtable bodies honour it: update, animate, render
   and endFrame each take a different branch. That is the shipped pause, and it is the place to put a
   lockstep gate.
3. **The game already ships a simulate-without-presenting switch.** `g_ffxIsCatchUpStep 0x12FB7D0`
   and `g_ffxIsCatchUpStepRender 0x12FB7D8`, set on every iteration of the catch-up loop except the
   last, read by roughly sixty sites. The HD Remaster fast forward is built on it, and a lockstep
   catch-up gets to reuse it by writing `g_ffxPendingSteps 0x12FB808`.
4. **An engine-maintained per-step counter does exist**, contrary to the design doc's assumption.
   `g_ffxMainStepCounter 0x23CBBF0` is the game's own frame number. It is not unconditional, so a mod
   still wants its own counter, but it is the right cross-check and it covers field as well as
   battle.

## 1. The call chain, top to bottom

```
WinMain 0x62EE50
  PeekMessageA pump, and the Sleep frame limiter lives right here
    -> *(fn **)(g_ffxApplication + 0x268)             set to frameTick by onInit
       Phyre__PApplication__frameTick 0x627940
         -> vtbl+0x24  FFXApplication::resize 0x42F9A0      first frame only, and on resize
         -> Phyre__PInputMapper__latchDeviceStates 0x628320  all 18 pad slots
         -> vtbl+0x1C  FFXApplication::update 0x42F770
         -> vtbl+0x10  FFXApplication::animate 0x42F520      <- the Workshop hook point
                         -> FFX_MainStepLoop 0x822840
                              -> FFX_MainStep 0x820AE0, once per pending step
                                   -> FFX_Time_ScaleDtForBooster 0x6F7340
                                   -> FFX_StepPacing 0x821E80
                                   -> the sub-step loop
                                   -> the scene display list build
                         -> sub_42FA00 0x42FA00               the play-time float
                         -> FFX_GameTick 0x639300             booster hotkeys and overlays
         -> per-viewport vtbl+0xC
         -> vtbl+0x20  FFXApplication::render 0x42F930
         -> vtbl+0x28  FFXApplication::endFrame 0x42F4F0
```

**`animate` runs before `render`.** An animate hook that returns early still gets a rendered and
presented frame. That is the single fact the whole gate design rests on.

### The WinMain pump, written out

```c
g_ffxTargetFrameTime = (double)g_phyreFrameRateDivider / 59.94005994005994;   // seeded once
while (Msg.message != WM_QUIT) {
    if (PeekMessageA(&Msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&Msg); DispatchMessageA(&Msg); }
    else {
        now    = Phyre_Time_NowSeconds() - dbl_C90278;
        period = (double)g_phyreFrameRateDivider * 0.01668333333333333;   // divider / 59.94
        slack  = now - g_ffxTargetFrameTime - period;
        v      = (slack <= period) ? slack : period;
        if (v < 0.0) {
            Sleep((DWORD)(v * -1000.0));                 // THE FRAME LIMITER
        } else {
            g_ffxTargetFrameTime = now - v;
            if (Phyre__PInput__updateAndCheck() != 0
                || *(BYTE *)(g_ffxApplication + 0x39) != 0
                || (*(int (__thiscall **)(int))(g_ffxApplication + 0x268))(g_ffxApplication) != 0)
                PostMessageA(hwnd, WM_QUIT, 0, 0);
        }
    }
}
```

`g_phyreFrameRateDivider 0xC30E88` is **2** in `.data`, so the game targets 29.97 Hz.
`Phyre__PApplication__setFrameRateDivider 0x625090` clamps it to 1 or 2 and maps 0 to 1. The call
sites of the FFX thunk `FFX_Frame_SetRateDivider 0x6430E0` are:

| caller | value | meaning |
| --- | --- | --- |
| `FFX_MainInit 0x820865` | 2 | 29.97 Hz for gameplay |
| `FFX_MenuSys_StepFrame 0x8A9DAC` | 1 | **59.94 Hz, set right after `g_ffxMenuSysRunning = 1`** |
| `FFX_MenuSys_StepFrame 0x8A9FE8` | 2 | back to 29.97 when the menu closes |
| `sub_A54720 0xA5473D` | 1 | Iggy menu teardown |
| `FFX_Menu_Build 0xA56002` | 2 | |

So the in-game menu runs the **whole main loop**, simulation included, at 59.94 Hz. Writing 0 to
`g_phyreFrameRateDivider` directly, bypassing the clamping setter, makes `period` zero and uncaps the
frame rate. That is a crude fast forward and it also speeds up the simulation, because a shorter
delta still asks `FFX_StepPacing` for one step. See section 5 for the better lever.

### Phyre__PApplication__frameTick 0x627940, in order

```c
if (app+0x3C > 0.0) { app+0x24 = app+0x3C; app+0x20 += app+0x3C; }   // the fixed timestep
else { t = Phyre_Time_NowSeconds() - dbl_C90278 - app+0x28;
       app+0x24 = t - app+0x20; app+0x20 = t; }
app+0x120 = app+0x20;
if (app+0x39) return 9;                      // quit requested
if (app+0x34 == 0) { app+0x24 = 0.0; if (vtbl+0x24(app)) return err; }   // first frame, dt forced 0
++g_phyreFrameTickCount;                     // 0x12FB8C4
++app+0x34;
latchDeviceStates(app+0x2B8);
if (vtbl+0x1C(app)) return err;              // update
... framerate measurement into app+0x30, every 2 seconds, Phyre_TtyPrintf only ...
if (vtbl+0x10(app)) { "App failed during animate"; return err; }        // ANIMATE
... resize / msaa / gamma / resolution / vsync dirty-flag servicing ...
... per-viewport vtbl+0xC ...
if (vtbl+0x20(app)) return err;              // render
vtbl+0x28(app);                              // endFrame
```

`app+0x34` is Phyre's own frame number. It does **not** get decremented on the normal path.
`Phyre__InputLog_CmdFrames 0x623660` does `--app+0x34` after calling frameTick, but that function is
not the normal frame path: it is one of Phyre's input-log script command handlers, registered by
`sub_62BB60` under the name `"Frames"` alongside `Timestep`, `Framerate`, `MinFramerate`,
`RenderFrame`, `Keypress`, `Buttonpress`, `Expect`, `Echo` and `Quit`. Worth knowing because it is
direct evidence that the engine authors shipped a deterministic replay harness, which is the same
thing lockstep needs.

### FFXApplication::animate 0x42F520, in order

1. `SteamAPI_RunCallbacks()` if `byte_CC9CF5`.
2. `if (sub_648FF0() == 1) return 0;` an early-out that skips the whole frame.
3. Title/boot mode one-shot.
4. The hold-byte block, section 6.
5. `FFX_MainStepLoop(*(float *)(this + 0x24))`, the Phyre frame delta.
6. `sub_42FA00(this)`, the FMV-aware play-time float at `+0x3B0`, mirrored to `+0x120`, wrapping at
   2400.0 s. It accumulates the raw frame delta, so it is wall-clock derived.
7. `FFX_GameTick 0x639300` -> `FFX_Frame_UpdateBoostersAndOverlays`, the booster hotkeys and the HUD
   overlays.
8. `return PApplication__vf04(this)`, which is `return 0;` and does nothing else.

Steps 5, 6 and 7 are all inside the hold-byte check. **Step 8 doing nothing means returning 0 early
from an animate hook loses nothing the engine needed.**

### FFX_MainStepLoop 0x822840

`int __cdecl (float dt)`. The Hex-Rays `__usercall` with `ebx` and `esi` arguments is noise: the
single call site at `0x42F5B6` pushes one float and does `add esp, 4`.

```c
do {
    if (g_ffxThreadedPadMode) FFX_Input__consumeThreadedSample();
    sub_63FD80(); sub_71ECD0(); sub_63DAB0();
    FFX_MainStep(dt);
} while (g_ffxPendingSteps != 0);
return sub_71EDB0(0);
```

`dt` is byte-identical on every iteration, and `FFX_MainStep` is what decrements
`g_ffxPendingSteps`.

## 2. FFX_MainStep 0x820AE0 is simulation AND display list in one function

This is the most important structural fact and it is bad news for anyone hoping to gate "simulate"
separately from "draw" at a fine grain. `FFX_MainStep` is the ported PS2 main loop body, and on the
PS2 the frame was one function.

`int __cdecl (float dt)`, same convention argument as above. Prologue
`55 8B EC 81 EC A4 00 00 00`, nine bytes, three instructions, no branch and no absolute address, so
it is a clean detour target.

In order:

1. `dt = FFX_Time_ScaleDtForBooster(dt)` and **the result is written back over its own argument at
   `0x820B01`**. See section 5.
2. Phyre housekeeping: `sub_71E460(0)`, `sub_63DAC0()`, `sub_6427F0()`, `sub_645DF0(dt)`.
3. `if (sub_6411E0() == 2)` the FMV path, which draws the movie and returns. No simulation.
4. `FFX_Pad__updateAll()`.
5. `FFX_StepPacing(dt)`, unless `g_ffxMenuSysRunningAlt` is set, in which case only
   `FFX_Pad__commitAllPorts()` runs.
6. The catch-up decision:
   ```c
   if (g_ffxPendingSteps <= 1) { g_ffxIsCatchUpStep = 0; ...Render = 0; ...Unread = 0;
                                 g_ffxPendingSteps = 0; dword_1340830 = g_ffxMenuSysRunning;
                                 sub_645A40(); }
   else                        { g_ffxIsCatchUpStep = 1; ...Render = 1; ...Unread = 1;
                                 --g_ffxPendingSteps; }
   ```
7. `FFX_Chr_ProcessPendingAttachments()`, `sub_6442D0(dt)`, save-UI polling, the debug reset combo,
   menu-system stepping.
8. The gameplay block, guarded by `!g_ffxMenuSysRunning && !g_ffxMenuSysRunningAlt &&
   !dword_134082C && FFX_SaveUi_IsIdle() && !FFX_MainStep_IsFadeBlocking()`. Inside it:
   `FFX_Player__readPad`, `FFX_MenuSys_PollOpenAndStep`, `FFX_MainStep_PublishStepScale 0x8206C0`,
   then the sub-step loop:
   ```c
   for (i = 0; i < FFX_Player__getSubStepCount(); ++i) {
       FFX_Player__stepControl();
       FFX_Btl_MainStep();
       sub_88D3D0(); sub_8AB910();
       FFX_Magic_CallOverlayStep(0);
       FFX_Came_StepAll();
       sub_91A8C0();
       if (byte_12FB8FF) FFX_Ch_UpdateMotionAll(0.033373334);   // the hardcoded 29.97 Hz step
       FFX_PlayerCam_Step();
   }
   ```
9. Then, in the same function, the display list build: `FFX_PlayerCam_Publish`, the two-pass
   `FFX_Came_ActivateScreen` multi-screen loop, `FFX_Ch_StepAll` which is the character draw
   dispatch, `DmaSync`, the effects pass, the 2D pass, `FFX_DrawDebugOverlay`.
10. `++g_ffxMainStepCounter` at `0x821617`.

**The separation the engine actually provides is the `g_ffxIsCatchUpStep` flag, not a call-graph
split.** The submission of what step 9 built happens later, in `FFXApplication::render`. See
section 6c, because that split is what makes a stall land safely or not.

## 3. Frame and step counters, the full list

The design doc said no clean step counter exists. That is half right. Seven counters exist and they
are all worth knowing.

| global | VA | advances | readers | verdict |
| --- | --- | --- | --- | --- |
| `g_phyreFrameTickCount` | `0x12FB8C4` | once per `frameTick`, unconditionally, before `animate` | **zero**, the `inc` is the only xref in the binary | a free, tamper-proof **presented-frame** counter. Not a step counter. |
| `app+0x34` | `*(DWORD *)(*(void **)0xCC9CD8 + 0x34)` | same place, same rate | the first-frame test, and the input-log replay handler | same information, reachable through the app pointer the Workshop already has |
| `g_ffxMainStepCounter` | `0x23CBBF0` | once per `FFX_MainStep`, gameplay path only | many: `FFX_Ch_MoveChrMemory`, `FFX_Mot_RunEvents`, `DmaSync`, `SndKickFoot`, `sgMenuExec`, plus three `test byte ptr ..., 1` parity sites | **the game's own frame number.** Zeroed by `FFX_MainInit`. Covers field and battle. Stalls during FMV and the system menu. **Read it, never write it**, because the parity tests drive a double-buffer flip. |
| `g_ffxGameClock60Hz` | `0x12FB7F0` | `+= 2` per simulation step, in `FFX_StepPacing` | `FFX_MainStep_PublishStepScale`, `sub_908F70` | the in-game clock in 1/60 s ticks. Gated on no menu, no pending menu open, save UI idle. |
| `g_ffxGameClock60HzUnread` | `0x12FB7F4` | identical, beside the above | **zero** | one xref total, the `add`. Dead twin. |
| `g_ffxEffectSubStepCounter` | `0x23328D4` | per sub-step | 11 | documented by an earlier pass. Stalls when `byte_12FB8FC`/`byte_12FB8FD` are set. |
| `g_ffxBattleFrameCounter` | `0x112C900` | per battle step | 4, one of them a script opcode | battle only, and resettable. |

**Recommendation, and this is what `ffx/MainLoop.h` implements.** Keep your own unconditional counter,
because that is the only counter that cannot stall. Use `g_ffxMainStepCounter` as the desync
cross-check, because it is the engine's own view of how many simulation steps have happened and it
covers field and battle. Use `g_phyreFrameTickCount` when you want "presented frames" rather than
"simulation steps", for example to tell "the process is alive" from "the simulation is advancing".

## 4. Where the frame delta goes, and every variable-delta hole

`dt` reaches `animate` as `app+0x24`, which `frameTick` computes from `app+0x3C` when that is
positive and from the clock otherwise. `app+0x3C` is loaded once by
`Phyre__PApplication__onInit 0x62A665` from `g_phyreFixedTimeStepArg 0xCC9D10`, the `-timestep=`
value. **Confirmed in code. Still never tested in a running game**, which is research item 2 in the
design doc and it stays open. Nothing caches the field, so a runtime write to `app+0x3C` takes effect
on the next frame, which is a far easier experiment than a relaunch.

From `FFX_MainStep`, `dt` reaches exactly four places:

| consumer | what it does with dt | determinism verdict |
| --- | --- | --- |
| `FFX_Time_ScaleDtForBooster 0x6F7340` | multiplies by `g_boosterSpeedMultipliers[g_boosterSpeedIndex]` | deterministic **if both machines agree on `g_boosterSpeedIndex 0xCE82B4`**. Must be synced. |
| `sub_645DF0` | Phyre presentation housekeeping, does not use the float | not a hole |
| `sub_6442D0 0x6442D0` | `sub_69A1A0(); sub_69C010(dt);` | presentation |
| `FFX_StepPacing 0x821E80` | turns dt into `g_ffxPendingSteps` | **deterministic given a constant dt.** The iteration count is a pure function of dt plus the two carried fractions. |

What the simulation itself integrates with is **not** dt:

- `FFX_Ch_UpdateMotionAll` gets the hardcoded `0.033373334` from `flt_B59158`. Already confirmed.
- `FFX_Mot_AdvanceFrame` at `0x838DF3` multiplies by `g_ffxStepScale88 0x23CBBEC`, which is derived
  from the **integer** 60 Hz tick delta, not from the clock. `FFX_MainStep_PublishStepScale` computes
  `(g_ffxGameClock60Hz - previous) << 7`, so a nominal step gives exactly `0x100` and
  `g_ffxStepScaleFloat 0x23CBBE8` gives exactly `1.0`. It is also forced to `0x100` whenever
  `FFX_Player__getSubStepCount() != 1`. **Integer-derived and therefore deterministic.**

### The pacing accumulator, written out

```c
v8 = (g_ffxPacingFrac60 + dt) * 60.0;
if (dt > 0.30000001) { dt = 0.30000001; v8 = (g_ffxPacingFrac60 + 0.3) * 60.0; }
if (g_ffxPendingSteps == 0) {                   // ONLY recomputes when already zero
    if (g_ffxPacingMode == 1) { ... the timing-track path, see below ... }
    else if (v8 >= 2.0 && FFX_StepPacing_AllowCatchUp()) {
        g_ffxPacingFrac60 = (v8 - (int)v8) / 60.0;
        g_ffxPendingSteps = (int)((g_ffxPacingFrac30 + dt) * 30.0);
        g_ffxPacingFrac30 = ((g_ffxPacingFrac30 + dt) * 30.0 - g_ffxPendingSteps) / 30.0;
    } else { g_ffxPendingSteps = 1; g_ffxPacingFrac60 = 0.0; g_ffxPacingFrac30 = 0.0; }
}
if (!menus && FFX_SaveUi_IsIdle()) { g_ffxGameClock60Hz += 2; g_ffxGameClock60HzUnread += 2; }
FFX_Pad__commitAllPorts();
```

Two things fall out of this and both matter.

- **`if (g_ffxPendingSteps == 0)` is the whole reason the catch-up primitive works.** A value written
  from outside before `animate` runs is not recomputed and not overwritten.
- At 59.94 Hz, `v8` is 1.0, below the 2.0 threshold, so the else branch fires and
  `g_ffxPendingSteps = 1` **every frame**. In other words the divider does not just change the
  presentation rate, it changes the simulation rate. At divider 1 the game simulates at 59.94 Hz. At
  divider 2 the accumulator path gives exactly one step per frame at 29.97 Hz, and more only when a
  frame overran. That is consistent with the game shipping at 29.97 and switching to 59.94 only for
  menus.

### Pacing mode 1, and it is narrower than it first looked

`g_ffxPacingMode 0x12FB8A0 == 1` derives its step count from `FFX_Time_NowMilliseconds` and, when it
is more than 100 ms ahead, busy-waits in a `for (v3 = 0; v3 < 10000; ++v3)` spin. Mode 0 never
touches the clock. `ffx::IsPacingFromWallClock()` samples this plus the missing-fixed-timestep case.

**CORRECTED by the cutscene pass, in three ways. See `reversing\CUTSCENE.md` section 2.**

First, mode 1 is not *set* and left set. `FFX_StepPacing_LoadTimingTrack 0x821D60` is its only
writer, and `FFX_StepPacing` calls it unconditionally on every frame where `g_ffxPendingSteps` hit
zero. So the mode is **re-derived every frame** and means exactly one thing at that instant: a
SyncData timing track is selected and has entries left.

Second, it is not the FMV path, it is the **voiced cutscene** path. A track is armed by
`FFX_SyncData_SelectTimingTrack 0x67A9B0`, whose single caller in the binary is
`FFX_Atel_CallActorScript0 0x867230`, and the sibling tables in the same file are read by the ATEL
voice syscalls 213, 214 and 215 (`VoiceStandbyInit!`, `VoiceStartInit!`, `VoiceStartResult!`). **A
cutscene with no voiced line never leaves mode 0.**

Third, and this is the part that changes the plan: **mode 1 does not make a step's CONTENTS depend on
the clock.** While it is active, `FFX_MainStep_PublishStepScale` takes the per-step motion scale out
of `g_ffxTimingTrackStepTicks`, which is authored file data, instead of from the clock delta. Both
machines consume the same entries in the same order and get the same scale. Only the number of steps
per presented frame differs, and lockstep already owns that through `g_ffxPendingSteps`.

So mode 1 is a pacing difference, not a divergence, and it has a shipped remedy. The engine's own
pause menu has the identical problem, so there is a pending-pause value,
`g_ffxTimingTrackPauseMsPending 0xCCC868`, which `FFX_StepPacing` subtracts from the track's elapsed
time. **A lockstep gate that stalls during mode 1 must write its stall duration there**, or the track
comes back behind and burns catch-up steps that the other machine does not take. That is
`ffx::AddTimingTrackPauseMs()`, and `plugins\pilgrimage\net\LockstepLink.cpp` calls it when it
releases a hold.

The other wall-clock readers were enumerated by an earlier pass: nine Blitzball sites through
`FFX_Input__getTimeSeconds 0x630C40`, plus `sub_6F0670`'s `GetTickCount` ramp. I did not re-derive
those. `sub_42FA00`'s play-time float at `FFXApplication+0x3B0` is a further accumulator of raw real
time, but it only feeds `+0x120` and I found no gameplay consumer.

### The 0.3 second clamp

`FFX_StepPacing` clamps dt to `0.30000001` **before** using it, so a long hitch caps the catch-up at
9 steps in mode 0 and silently discards the rest of the time. If one machine ever hitches past 300 ms
and the other does not, that is a divergence. A lockstep build should not be relying on this path at
all, but it is worth knowing it exists.

## 5. Fast forward and catch-up

### The shipped fast forward

`FFX_Time_ScaleDtForBooster 0x6F7340`, called as the very first thing in `FFX_MainStep` with the
result written back over the dt argument at `0x820B01`:

```c
if (!g_boosterEnabled) return dt;
... eligibility tests ...
if (ineligible || g_boosterSpeedIndex == 0) {
    if (g_boosterSpeedApplied) { iggy "OnDisable"; maybe_FFX_Snd_SetBoosterSpeed(0); }
    g_boosterSpeedApplied = 0; return dt;
}
if (!g_boosterSpeedApplied) { iggy "OnEnable"; maybe_FFX_Snd_SetBoosterSpeed(1); }
g_boosterSpeedApplied = 1;
return g_boosterSpeedMultipliers[g_boosterSpeedIndex] * dt;
```

| global | VA | meaning |
| --- | --- | --- |
| `g_boosterEnabled` | `0xCE82BC` | master on/off for the HD Remaster booster features |
| `g_boosterSpeedIndex` | `0xCE82B4` | 0, 1 or 2 |
| `g_boosterSpeedMultipliers` | `0xC38E1C` | **`{ 1.0, 2.0, 4.0 }`**, so the shipped speeds are 2x and 4x |
| `g_boosterSpeedApplied` | `0xCE82B8` | edge latch, so the audio notify fires on transitions |
| `g_boosterSpeedIggyIcon` | `0xCE82C0` | drives the Iggy "OnEnable"/"OnDisable" overlay |

Eligibility rules out: battle phase 1 with `sub_6F6FF0` set, `g_ffxMenuSysRunning` non-zero,
`dword_CE7310` non-zero, `sub_641060()` non-zero, `byte_C5A068` zero, `dword_112A008` non-zero, and
`sub_6F6FA0(maybe_g_ffxGameModeId)`. The hotkey and UI side is in
`FFX_Frame_UpdateBoostersAndOverlays 0x657140` and is another agent's area.

**So fast forward is not a separate code path.** It multiplies dt, `FFX_StepPacing` turns the bigger
dt into a bigger `g_ffxPendingSteps`, `FFX_MainStepLoop` runs `FFX_MainStep` that many times, and the
game skips presentation work on all but the last.

### The lockstep catch-up primitive

**Write `g_ffxPendingSteps 0x12FB808 = N` before the original `animate` runs.** `FFX_StepPacing` only
recomputes that value when it is already zero, so N survives and the loop runs exactly N simulation
steps inside one presented frame with `g_ffxIsCatchUpStep` set on the first N-1.

**This is inference from reading the control flow, not something I ran.** It is a small and very
falsifiable experiment: set it to 2 for one frame and the game should visibly jump forward one extra
step, with `g_ffxMainStepCounter` advancing by 2 instead of 1. `ffx::RequestCatchUpSteps` does the
write and `ffx::ReadFrameClocks` reports the counter, so the experiment is two lines.

### Three stacking multipliers, so nobody is confused later

- `g_boosterSpeedIndex` scales dt, so `FFX_StepPacing` asks for more steps. 2x or 4x.
- `g_ffxPendingSteps` is how many `FFX_MainStep` calls this presented frame.
- `FFX_Player__getSubStepCount 0x82D7E0` is how many times the **inner** sub-step loop runs inside one
  `FFX_MainStep`. It returns `g_ffxPlayerSubStepCount 0xC496D4`, or `0x13007B2` when `byte_13007B1`
  is set, OR-ed with `maybe_FFX_Player__getTurboFlag()`. It repeats control, battle, camera and
  motion **without** repeating the display list build. It is also exported to the shipped magic
  plugin DLLs as host API entry 253, so an effect DLL can read it.

## 6. Pause, and the hold byte

There are four separate things in the frame path that can be called pause, and they gate different
amounts.

### 6a. The hold byte: FFXApplication + 0x3AD

This is the answer to "where can the simulation be stalled safely". It is one byte and **all four
FFXApplication vtable bodies honour it**:

| slot | VA | behaviour when the byte is set |
| --- | --- | --- |
| `+0x1C` update | `0x42F770` | skips `FFX_Input__updateFromFFXApp(dt)` |
| `+0x10` animate | `0x42F520` | polls menu input, then **skips `FFX_MainStepLoop`, the play-time step and `FFX_GameTick`** |
| `+0x20` render | `0x42F930` | takes `FFX_Frame_PresentPausedOverlay 0x642BE0` instead of `FFX_Frame_PresentScene 0x642BB0` |
| `+0x28` endFrame | `0x42F4F0` | does nothing at all |

The animate structure, written out:

```c
if (app+0x3AD) {
    FFX_Input__pollForMenu();
    if (app+0x3AD && !FFX_EscMenu__isOpen()) sub_642CE0();     // a gfx-context pair, unidentified
    app+0x3AD = FFX_EscMenu__isOpen();
    if (app+0x3AD) goto tail;                                   // the entire game step is skipped
}
FFX_MainStepLoop(dt); sub_42FA00(this); FFX_GameTick();
tail:
return PApplication__vf04(this);                                // which is "return 0;"
```

`FFX_EscMenu__isOpen 0x63DA80` reads the singleton `g_ffxEscMenu 0xCCC870`:
`+0x70 != 0 || *(int *)(ptr + 0xE4) >= 0`.

**Animate and endFrame both re-latch the byte from `FFX_EscMenu__isOpen()`.** So you cannot hold it
at 1 from outside without owning the animate vtable slot. The Workshop already owns it, which makes
this trivially reachable.

### 6b. Why that is genuinely safe, and what it costs

Safe, because:

- `frameTick` still runs `latchDeviceStates`, the Phyre update, the per-viewport render, `render` and
  `endFrame`, so the window keeps presenting and Windows keeps seeing a responsive app.
- `FFX_MainStep` does not run at all, so no simulation, no `g_ffxMainStepCounter`, no
  `g_ffxGameClock60Hz`, no RNG draws.
- `g_phyreFrameTickCount` keeps counting, which is why it is the counter for "the process is alive".
- **Nothing in the frame path latches on being called exactly once.** The three
  `g_ffxIsCatchUpStep` twins are rewritten from scratch at the top of every `FFX_MainStep`, and
  `FFX_StepPacing`'s accumulators carry fractions rather than depending on a call count.
- It is shipped code. The pause menu does exactly this.

The cost is the scene redraw. The display list is built inside `FFX_MainStep`, and the paused render
branch `FFX_Frame_PresentPausedOverlay` is `sub_66B130(g_ffxGfxCtx)` plus `sub_A267F0(...)`, the Iggy
UI draw, with no scene submission. So the screen holds the last presented frame with the overlay path
on top. For a lockstep wait of a few milliseconds that is invisible. For a wait of seconds, draw your
own "waiting for peer" overlay, because the game is not drawing anything new underneath it.

**Audio.** I did not trace audio to its thread. The argument that audio survives a stall is that the
shipped pause menu produces exactly this state and audio is fine in the retail game. That is
inference from shipped behaviour, not from reading a sound thread, and
`maybe_FFX_Snd_SetBoosterSpeed 0x821C60` going through `sub_6FA280(57)` / `sub_6FA2A0(58, x)` /
`sub_6FA2C0(0, x, 2)` is where to start if someone wants to settle it.

### 6c. The narrower gate, and the one configuration the game never produces

Detouring `FFX_MainStep` and returning without calling the original is one level below animate, so
animate, the play-time step, `FFX_GameTick`, the Phyre update and the render path all keep running.
That sounds strictly better, and for a one or two frame stall it is.

**But it produces a configuration the shipped game never does:** `FFX_Frame_PresentScene` submitting
a display list that `FFX_MainStep` did not rebuild. `FFX_Frame_PresentScene 0x642BB0` is
`sub_668930(g_ffxGfxCtx)`, `sub_71E810()`, `sub_65A620(g_ffxGfxCtx)`. Whether resubmitting a stale
list is harmless or walks a half-released buffer is **not something I established**, and it is the
largest remaining risk in this area. The shipped pause sidesteps it entirely by switching the render
branch.

So: use the `FFX_MainStep` detour for the per-step counter and for one-or-two-step gating, and use the
hold byte for anything longer. `ffx/MainLoop.h` provides both and says this in the header.

Returning early from `FFX_MainStep` also skips `FFX_Pad__updateAll()` and `FFX_Pad__commitAllPorts()`,
because both sit ahead of the gameplay block. For lockstep that is desirable, a stalled machine
should not consume local input, but the game's pad state is then frozen too.

### 6c2. Does anything latch on being called exactly once

This was the specific worry, so it got checked rather than assumed. Every per-step counter in the
`FFX_MainStep` path was found and classified.

**Nothing breaks.** The three `g_ffxIsCatchUpStep` twins are rewritten from scratch at the top of
every step rather than toggled, and `FFX_StepPacing`'s accumulators carry a fraction rather than
depending on a call count, so neither drifts when a step is skipped.

**Four things simply do not advance**, which is the correct behaviour for a pause and is what the
shipped pause already produces:

| counter | site | what stalling it costs |
| --- | --- | --- |
| `dword_C421E8`, `dword_C421E4` | `FFX_MainStep_PollMagicPrxLoad 0x822250` | an in-flight magic PRX load is polled one step later. It waits, it does not fail. |
| `dword_C494CC` | `sub_81FF80` | the save-data enumeration timeout (`>= 200`) takes longer in wall-clock terms |
| `dword_12FB7C0` | `FFX_MainStep` title path | the title logo timer (`> 304`) takes longer |
| `g_ffxMainStepCounter` | `0x821617` | the double-buffer parity does not flip, which is consistent because nothing was drawn |

`g_ffxGameClock60Hz` also stops, which is the point: the in-game clock should not run while the
simulation is frozen.

### 6d. The in-game menu

`g_ffxMenuSysRunning 0x1340824` and `g_ffxMenuSysRunningAlt 0x1340828` do not stop `FFX_MainStep`.
They stop the gameplay block inside it and they stop `g_ffxGameClock60Hz`. `g_ffxMenuSysRunningAlt`
additionally makes `FFX_MainStep` skip `FFX_StepPacing` entirely, so **while the alt menu is up
`g_ffxPendingSteps` is never recomputed**, only decremented, so it drains to zero and stays there.

### 6e. A pending menu open, and the save UI

`dword_134082C` is tested by `FFX_MenuSys_RequestOpen 0x8217AB` and cleared in `FFX_MainStep` through
`dword_134082C = sub_8202D0() == 0 ? dword_134082C : 0;` where `sub_8202D0` is a constant
`return 1`, so it is unconditionally cleared every step. It gates the gameplay block for exactly one
step.

`FFX_SaveUi_IsIdle 0x648100` is `dword_CCB994 == 0`, written only by `sub_6486E0`, four xrefs total.
Gates the gameplay block and the clock advance.

### 6f. For the design doc's "sync pause as the first test of the network layer"

Sync **the hold byte**, not the game's own pause state. Writing `g_ffxEscMenu+0x70` would open the
actual menu UI on both machines, which is a different and worse thing. The recipe is in
`ffx/MainLoop.h` and is four lines in an animate hook.

## 7. Frame limiting and vsync, summarised

- The limiter is `Sleep` in the `WinMain` pump, to `g_phyreFrameRateDivider / 59.94` seconds. See
  section 1.
- `frameTick` has no sleep and no limiter. It only **measures**, reporting through
  `Phyre_TtyPrintf(2, "Framerate = %f fps, %f ms\n", ...)` every 2 seconds using `dword_CCA1B0` as
  the sample count and `flt_CCA1B4` as the accumulated time, and storing the result in `app+0x30`.
- Vsync is a game setting applied through a dirty flag. `frameTick` watches `app+0x14` through
  `app+0x18` and, on `app+0x18`, calls `sub_58E170(g_ffxGameSettings[40] != 0)` unless `app+0x1C` is
  set, in which case it passes 0. `app+0x1D` repeats that outside the dirty group.
  `sub_58E170 0x58E170` stores one byte at `device+0x131C`, so **the chain from the setting to the
  swap interval is only half proven here**: I confirmed which setting feeds it and where the byte
  lands, not what reads the byte. The settings screen is another agent's area.
- The `WinMain` usage string names `-vsync` alongside `-resolution`, `-msaa`, `-host` and `-script`.

## 8. Verdict

**Put the lockstep gate in the `animate` hook and express the stall as the hold byte at
`FFXApplication + 0x3AD`.**

```c
int __fastcall MyAnimate(void *self, void *unusedEdx)
{
    if (!PeerInputReady()) {
        ffx::HoldSimulation(self);
        return 0;
    }
    ffx::ReleaseSimulationHold(self);
    return ffx::OriginalAnimate()(self, unusedEdx);
}
```

**Is stalling safe there?** Yes, and with more confidence than anything else in this area, because it
is the state the shipped pause menu puts the game in and all four vtable bodies already agree on it.
The one thing it costs is the scene redraw, so a stall longer than a few frames needs your own
overlay.

For catch-up, `ffx::RequestCatchUpSteps(n)` writes `g_ffxPendingSteps` before the original animate
runs, and the engine's own fast-forward machinery does the rest.

For the step clock, install `ffx::HookMainStep` and use `ffx::StepCount()`, cross-checked against
`g_ffxMainStepCounter`.

**The biggest unknown left** is whether the shipped `-timestep=` path actually holds together at
runtime, because it is the difference between "the step count is a pure function of a constant" and
"the step count is a function of the wall clock". It has only ever been read in the disassembly. The
experiment is now cheap: `ffx::SetFixedTimeStep(ffx::nominalFixedTimeStep)` writes `app+0x3C` live,
nothing caches it, and `ffx::IsPacingFromWallClock()` reports whether it took. Watch audio sync and
the FMV path.

## 9. What I could not settle

- **`-timestep=` at runtime.** Code path re-confirmed, never run. See above.
- **Whether `FFX_Frame_PresentScene` resubmitting a stale display list is safe.** This is the thing
  that decides whether the narrow `FFX_MainStep` gate is usable for a long stall. The shipped game
  never produces that configuration.
- **Whether audio is really independent of `FFX_MainStep`.** Argued from the shipped pause rather
  than traced to a sound thread.
- **`sub_648FF0`'s early-out in animate.** It reads a byte at `sub_67AC90() + 1` and returning 1 makes
  animate return 0 immediately, skipping even the Steam callbacks. Probably a shutdown or focus-loss
  state. It is the one other thing that can silently skip a whole frame, and a gate living in animate
  should log it.
- **Whether `g_ffxIsCatchUpStep` can be forced on for a non-catch-up step without visual artefacts.**
  All sixty-odd readers are shaped `if (flag == 0) do the work`, which looks safe, and `sub_A54660`
  saves, clears and restores it specifically so the Iggy menu always draws. That nesting is the thing
  to look at before relying on forcing it.
- **Where `maybe_FFX_Player__getTurboFlag` comes from.** It is OR-ed into the sub-step count, so the
  inner loop can run more than the stored number of times.
- **The last hop of the vsync chain**, from the byte `sub_58E170` stores to the actual swap interval.
