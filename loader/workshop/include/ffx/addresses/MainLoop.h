#pragma once

#include <windows.h>

// The main loop, the frame step and the clock.
//
// Everything that decides WHEN the simulation advances, HOW FAR it advances and
// WHETHER it advances at all. The co-op design is lockstep, so this is the area
// a lockstep gate attaches to.
//
// Addresses are RVAs, which is the IDA VA minus 0x00400000. Signatures were read
// out of the IDB from the call sites rather than from the Hex-Rays prototypes,
// because the decompiler reports FFX_MainStep and FFX_MainStepLoop as __usercall
// with ebx and esi arguments and that is noise. Both are plain
// int __cdecl (float dt): the single call site pushes one float and does
// add esp, 4, and neither function reads ebx or esi before writing it.
//
// The derivation, the call chain and the evidence are in reversing\MAIN_LOOP.md.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The top of the loop.
		//
		// There is NO render thread and no vsync wait of the engine's own. WinMain runs
		// its own PeekMessageA pump and paces the game with Sleep, to
		// g_phyreFrameRateDivider * (1/59.94) seconds per frame. Everything below runs
		// on that one thread.
		//
		//   WinMain pump
		//     -> *(fn **)(application + 0x268)       which onInit set to PhyreFrameTick
		//        -> FFXApplication::update    vtable +0x1C
		//        -> FFXApplication::animate   vtable +0x10   <- the Workshop hook point
		//           -> FFX_MainStepLoop(dt)
		//              -> FFX_MainStep(dt) once per pending step
		//                 -> FFX_StepPacing(dt)
		//        -> FFXApplication::render    vtable +0x20
		//        -> FFXApplication::endFrame  vtable +0x28
		//
		// ANIMATE RUNS BEFORE RENDER, so an animate hook that returns early still gets a
		// rendered and presented frame.
		// ---------------------------------------------------------------------------
		const DWORD WinMain = 0x0022EE50;        // the real frame loop and the Sleep limiter
		const DWORD PhyreFrameTick = 0x00227940; // int __thiscall (PApplication *)

		// ---------------------------------------------------------------------------
		// The FFX frame, in call order. All int __cdecl (float dt).
		// ---------------------------------------------------------------------------
		const DWORD MainStepLoop = 0x00422840; // do { ...; MainStep(dt) } while (PendingSteps)
		const DWORD MainStep = 0x00420AE0;     // one simulation step AND the scene display list
		const DWORD StepPacing = 0x00421E80;   // turns dt into PendingSteps

		// Non-dt pieces of the same frame, worth having by name.
		const DWORD AppPlayTimeStep = 0x0002FA00;       // void __thiscall (FFXApplication *)
		const DWORD GameTick = 0x00239300;              // int __cdecl (void), booster hotkeys and overlays
		const DWORD StepScalePublish = 0x004206C0;      // int __cdecl (void), fills StepScale88
		const DWORD TimeScaleDtForBooster = 0x002F7340; // double __cdecl (float dt), the fast forward
		// Wall clock seconds since app start. 30 callers, 20 of which reach the
		// simulation step. Most are battle menu UI pulsing, but Lulu's Fury overdrive
		// is timed off it, see Rva::BtlOdLuluFuryStickMinigame.
		const DWORD TimeAppElapsedSeconds = 0x00241410;  // double __cdecl (void)
		const DWORD TimeNowMicroseconds = 0x00241420;    // u64 __cdecl (void), MICROseconds

		// The other three FFXApplication vtable bodies, because all four honour the
		// simulation hold byte and a gate has to know what each one does.
		const DWORD FFXApplicationUpdate = 0x0002F770;
		const DWORD FFXApplicationRender = 0x0002F930;
		const DWORD FFXApplicationEndFrame = 0x0002F4F0;

		// ---------------------------------------------------------------------------
		// The catch-up loop state.
		//
		// PendingSteps is how many times FFX_MainStep still has to run this presented
		// frame. FFX_StepPacing recomputes it ONLY when it is already zero, so a value
		// written from outside before animate runs survives untouched and the loop runs
		// exactly that many simulation steps. That is the catch-up primitive.
		//
		// It is NOT a stall primitive. FFX_MainStepLoop is a do/while, so the body runs
		// at least once however small the value is.
		// ---------------------------------------------------------------------------
		const DWORD PendingSteps = 0x00EFB808;      // int
		const DWORD CatchUpExtraSteps = 0x00EFB814; // int, pacing mode 1 only
		const DWORD PacingMode = 0x00EFB8A0;        // int, 0 accumulator, 1 timing track

		// ---------------------------------------------------------------------------
		// The engine's own simulate-without-presenting switch.
		//
		// FFX_MainStep sets both of these to 1 on every catch-up iteration except the
		// last. Roughly sixty call sites read IsCatchUpStep, every one of them shaped
		// as if (flag == 0) do the presentation work, and the render-side character code
		// reads IsCatchUpStepRender to skip skinning and render-state work. This is what
		// the HD Remaster fast forward runs on.
		//
		// A third twin at 0x00EFB7D4 is written in the same three places and read
		// nowhere in the binary, so it is deliberately not exposed here.
		// ---------------------------------------------------------------------------
		const DWORD IsCatchUpStep = 0x00EFB7D0;       // int
		const DWORD IsCatchUpStepRender = 0x00EFB7D8; // int

		// ---------------------------------------------------------------------------
		// Counters. See ffx/MainLoop.h for which one to use when.
		// ---------------------------------------------------------------------------

		// Incremented once per PhyreFrameTick, unconditionally, before animate. The inc
		// is the ONLY xref in the whole binary, so nothing reads it and nothing can
		// disturb it. A free and tamper-proof presented-frame counter.
		const DWORD PhyreFrameTickCount = 0x00EFB8C4; // DWORD

		// The game's own frame number. Zeroed by FFX_MainInit, incremented once per
		// FFX_MainStep but only on the gameplay path, so it stalls during an FMV or the
		// system menu. Widely read, including three test byte ptr, 1 parity sites that
		// drive a double-buffer flip, so READ IT, NEVER WRITE IT.
		const DWORD MainStepCounter = 0x01FCBBF0; // DWORD

		// The in-game clock in 1/60 s ticks. FFX_StepPacing adds 2 per simulation step,
		// gated on no menu running and the save UI idle.
		const DWORD GameClock60Hz = 0x00EFB7F0; // DWORD

		// How long this simulation step was. 0x100 in 8.8 fixed point, or 1.0 as a
		// float, means one nominal 30 Hz step. Derived from the GameClock60Hz delta, so
		// it is integer-derived and therefore deterministic. FFX_Mot_AdvanceFrame scales
		// motion playback by it.
		const DWORD StepScale88 = 0x01FCBBEC;    // DWORD, 8.8 fixed point
		const DWORD StepScaleFloat = 0x01FCBBE8; // float

		// 0.033373333513736725, one 29.97 Hz frame. The constant FFX_MainStep hands to
		// FFX_Ch_UpdateMotionAll, which is why field motion is already fixed step.
		const DWORD MotionFixedStepSeconds = 0x00759158; // const float

		// ---------------------------------------------------------------------------
		// Frame limiting.
		//
		// FrameRateDivider is 2 in .data, so the game targets 29.97 Hz. The setter
		// clamps to 1 or 2 and maps 0 to 1. FFX_MainInit asks for 2, FFX_MenuSys_StepFrame
		// asks for 1 the moment it sets g_ffxMenuSysRunning, and for 2 again when the
		// menu closes, so THE IN-GAME MENU RUNS THE WHOLE LOOP AT 59.94 Hz.
		//
		// A direct write of 0, bypassing the clamping setter, makes the Sleep period
		// zero and uncaps the frame rate.
		// ---------------------------------------------------------------------------
		const DWORD FrameRateDivider = 0x00830E88;         // int
		const DWORD SetFrameRateDividerFn = 0x00225090;    // int __cdecl (unsigned), clamps to 1..2
		const DWORD FfxSetFrameRateDividerFn = 0x002430E0; // jmp thunk to the above
		const DWORD TargetFrameTime = 0x008C9D00;          // double, the WinMain pacing anchor
		const DWORD ProcessTimeOrigin = 0x00890278;        // double, subtracted from the raw clock

		// ---------------------------------------------------------------------------
		// The fixed timestep, which is the determinism lever.
		//
		// PhyreFrameTick uses application + 0x3C as the delta and never reads the clock
		// when it is positive. onInit copies FixedTimeStepArg into it once, from the
		// -timestep=<seconds> command line argument, and nothing caches it afterwards,
		// so a runtime write to application + 0x3C takes effect on the very next frame.
		//
		// VERIFIED IN CODE, NEVER TESTED IN A RUNNING GAME.
		// ---------------------------------------------------------------------------
		const DWORD FixedTimeStepArg = 0x008C9D10; // float, filled by the command line parser

		// ---------------------------------------------------------------------------
		// Pause, which is four separate things.
		// ---------------------------------------------------------------------------

		// The real pause. EscMenuIsOpen is BOOL __cdecl (void) and is a cheap pointer
		// test on the singleton. FFXApplication caches the answer in the hold byte at
		// application + 0x3AD and all four vtable bodies honour it.
		const DWORD EscMenuIsOpen = 0x0023DA80; // BOOL __cdecl (void)
		const DWORD EscMenu = 0x008CC870;       // void *, the singleton

		// The in-game menu. These do NOT stop FFX_MainStep, they stop the gameplay block
		// inside it and they stop GameClock60Hz. MenuSysRunningAlt additionally makes
		// FFX_MainStep skip FFX_StepPacing entirely, so PendingSteps is never recomputed
		// while it is set.
		const DWORD MenuSysRunning = 0x00F40824;    // int
		const DWORD MenuSysRunningAlt = 0x00F40828; // int

		// A menu open requested but not yet serviced. Gates the gameplay block for
		// exactly one step, then FFX_MainStep clears it unconditionally.
		const DWORD MenuOpenPending = 0x00F4082C; // int

		// The save UI state machine. Zero means idle. Non-zero gates the gameplay block.
		const DWORD SaveUiState = 0x008CB994; // int

		// The fade and transition gate on the gameplay block, which is a function rather
		// than a flag. BOOL __cdecl (void), non-zero means the gameplay block is skipped.
		// Call it from the frame thread only, it walks the fade state.
		const DWORD MainStepIsFadeBlocking = 0x0041FCD0;

		// ---------------------------------------------------------------------------
		// Fast forward, the simulation side. The settings screen that exposes the toggle
		// is somebody else's area.
		//
		// FFX_Time_ScaleDtForBooster multiplies dt by Multipliers[SpeedIndex] and
		// FFX_MainStep writes the result back over its own argument, so a bigger dt
		// means a bigger PendingSteps and more simulation steps per presented frame.
		// Multipliers is { 1.0, 2.0, 4.0 } and index 0 takes an early-out, so the
		// shipped speeds are 2x and 4x.
		//
		// BOTH MACHINES MUST AGREE ON BoosterSpeedIndex OR LOCKSTEP DIVERGES.
		// ---------------------------------------------------------------------------
		const DWORD BoosterEnabled = 0x008E82BC;          // int, master on/off
		const DWORD BoosterSpeedIndex = 0x008E82B4;       // int, 0 1 or 2
		const DWORD BoosterSpeedMultipliers = 0x00838E1C; // const float[3]
		const DWORD BoosterSpeedApplied = 0x008E82B8;     // int, edge latch

		// A third, independent multiplier: how many times the INNER sub-step loop runs
		// inside one FFX_MainStep. It repeats player control, battle, camera and motion
		// without repeating the render block. Also exported to the shipped magic plugin
		// DLLs as host API entry 253, so an effect DLL can read it.
		const DWORD PlayerGetSubStepCount = 0x0042D7E0; // int __cdecl (void)
		const DWORD PlayerSubStepCount = 0x008496D4;    // int

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check. VerifyLayout walks this so
		// a typo is caught at startup rather than by a fault later.
		//
		// Keep it in step with the constants. If you add an address and forget this
		// list, nothing breaks today and something breaks confusingly in a year.
		// ---------------------------------------------------------------------------
		inline const DWORD* MainLoopRvaList(int* count)
		{
			static const DWORD list[] = {
				WinMain,
				PhyreFrameTick,
				MainStepLoop,
				MainStep,
				StepPacing,
				AppPlayTimeStep,
				GameTick,
				StepScalePublish,
				TimeScaleDtForBooster,
				TimeAppElapsedSeconds,
				TimeNowMicroseconds,
				FFXApplicationUpdate,
				FFXApplicationRender,
				FFXApplicationEndFrame,
				PendingSteps,
				CatchUpExtraSteps,
				PacingMode,
				IsCatchUpStep,
				IsCatchUpStepRender,
				PhyreFrameTickCount,
				MainStepCounter,
				GameClock60Hz,
				StepScale88,
				StepScaleFloat,
				MotionFixedStepSeconds,
				FrameRateDivider,
				SetFrameRateDividerFn,
				FfxSetFrameRateDividerFn,
				TargetFrameTime,
				ProcessTimeOrigin,
				FixedTimeStepArg,
				EscMenuIsOpen,
				EscMenu,
				MenuSysRunning,
				MenuSysRunningAlt,
				MenuOpenPending,
				SaveUiState,
				MainStepIsFadeBlocking,
				BoosterEnabled,
				BoosterSpeedIndex,
				BoosterSpeedMultipliers,
				BoosterSpeedApplied,
				PlayerGetSubStepCount,
				PlayerSubStepCount,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
