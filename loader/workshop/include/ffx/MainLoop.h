#pragma once

#include <windows.h>

// The main loop, the frame step and the clock.
//
// What a plugin needs to answer three questions: what frame is it, is the
// simulation running, and can I make it wait. The co-op design is lockstep, so
// the third one is the point of this file.
//
// Every reader here goes through workshop::Readable and returns false rather
// than faulting, because all of it is meant to be callable from the frame path.
//
// ===========================================================================
// WHAT RUNS PER FRAME, IN ORDER
// ===========================================================================
//
//   WinMain's PeekMessageA pump, which also IS the frame limiter: it Sleeps so
//   one frame takes FrameRateDivider() * (1/59.94) seconds. One thread, no
//   render thread, no vsync wait of its own.
//     -> Phyre PApplication::frameTick
//          computes the frame delta, or uses the fixed timestep if one is set
//          bumps PhyreFrameTickCount and the app's own frame number
//          latches all 18 pad slots
//        -> FFXApplication::update    vtable +0x1C
//        -> FFXApplication::animate   vtable +0x10   <- ffx::HookAnimate
//           -> FFX_MainStepLoop(dt)
//              -> FFX_MainStep(dt), once per pending step   <- ffx::HookMainStep
//                 -> FFX_StepPacing(dt), which sets the pending step count
//                 -> the sub-step loop: player control, battle, camera, motion
//                 -> the scene display list build
//           -> the play-time accumulator
//           -> FFX_GameTick, booster hotkeys and overlays
//        -> FFXApplication::render    vtable +0x20
//        -> FFXApplication::endFrame  vtable +0x28
//
// ANIMATE RUNS BEFORE RENDER. An animate hook that returns early still gets a
// rendered and presented frame.
//
// FFX_MainStep IS SIMULATION AND DISPLAY LIST IN ONE FUNCTION. It is the ported
// PS2 main loop body and there is no call-graph boundary between stepping the
// world and building the frame for it. The separation the engine does provide is
// a flag, IsCatchUpStep, not a split in the call tree.
//
// ===========================================================================
// HOW TO STALL THE SIMULATION, AND WHY THIS WAY
// ===========================================================================
//
// The shipped pause menu already stalls the simulation for as long as you like,
// and it does it with ONE BYTE at FFXApplication + 0x3AD. All four vtable bodies
// honour it:
//
//   update    skips the FFX input update
//   animate   skips FFX_MainStepLoop, the play-time step and FFX_GameTick
//   render    takes a present-plus-overlay branch instead of the scene branch
//   endFrame  does nothing at all
//
// So the whole state is coherent and it is shipped code, which is the strongest
// safety argument available. The catch is that animate and endFrame both
// re-latch that byte from the game's own menu state, so holding it requires
// owning the animate vtable slot. The Workshop already owns it.
//
//   int __fastcall MyAnimate(void *self, void *unusedEdx)
//   {
//       if (!MyPeerInputIsReady()) {
//           ffx::HoldSimulation(self);   // byte at +0x3AD, the shipped pause state
//           return 0;                    // skip the game step, exactly as pause does
//       }
//       ffx::ReleaseSimulationHold(self);
//       return ffx::OriginalAnimate()(self, unusedEdx);
//   }
//
// THE ONE THING YOU GIVE UP is the scene redraw, because the scene display list
// is built inside FFX_MainStep. The screen holds the last frame with the overlay
// path on top. For a lockstep wait of a few milliseconds that is invisible. For a
// wait of seconds, draw your own "waiting for peer" overlay, because the game
// will not be drawing anything new underneath it. The booster hotkeys stop too,
// because FFX_GameTick is inside the same branch.
//
// NOTHING IN THE FRAME PATH LATCHES ON BEING CALLED EXACTLY ONCE. That was
// checked rather than assumed: the catch-up flags are rewritten from scratch at
// the top of every step rather than toggled, and the pacing accumulators carry a
// fraction rather than a call count. Four step-counted timers simply stop, the
// magic PRX load poll among them, which costs a delay and never a failure.
//
// DO NOT try to stall by writing PendingSteps to 0. FFX_MainStepLoop is a
// do/while, so the body runs at least once however small the value is. Pending
// steps is a catch-up knob, not a gate.
//
// ===========================================================================
// HOW TO CATCH UP
// ===========================================================================
//
// RequestCatchUpSteps(n) writes the pending step count. This works because
// FFX_StepPacing only recomputes that value when it is already zero, so a value
// written from outside before animate runs survives, and FFX_MainStepLoop then
// runs exactly that many simulation steps inside one presented frame with
// presentation suppressed on all but the last. That is the same mechanism the HD
// Remaster fast forward uses, reached a different way.
//
// Three independent multipliers exist and they stack. Know which one you are
// pulling:
//
//   BoosterSpeedIndex       scales dt, so FFX_StepPacing asks for more steps.
//                           Shipped values are 2x and 4x. MUST MATCH on both
//                           machines or lockstep diverges.
//   PendingSteps            how many FFX_MainStep calls this presented frame.
//   PlayerSubStepCount      how many times the INNER sub-step loop runs inside
//                           one FFX_MainStep. Repeats control, battle, camera
//                           and motion without repeating the display list.
//
// ===========================================================================
// WHICH COUNTER TO USE
// ===========================================================================
//
//   StepCount()           our own. Unconditional, cannot stall, needs
//                         HookMainStep installed. USE THIS as the lockstep
//                         clock.
//   MainStepCounter       the engine's own frame number. Clean per step, covers
//                         field and battle, but stalls during an FMV or the
//                         system menu. USE THIS as the desync cross-check.
//   PhyreFrameTickCount   presented frames. Unconditional and nothing in the
//                         binary reads it. Use it to tell "the process is alive"
//                         from "the simulation is advancing".
//
// The derivation and the evidence are in reversing\MAIN_LOOP.md.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Field offsets. Not addresses, so they live here rather than in
	// addresses/MainLoop.h. Phyre's PApplication is the base of FFXApplication, so
	// the PApp offsets apply to the pointer ffx::Application() returns.
	// ---------------------------------------------------------------------------
	namespace PApp
	{
		const DWORD DirtyResize = 0x14;     // BYTE, frameTick services these five
		const DWORD DirtyMsaa = 0x15;       // BYTE
		const DWORD DirtyGamma = 0x16;      // BYTE
		const DWORD DirtyResolution = 0x17; // BYTE
		const DWORD DirtyVsync = 0x18;      // BYTE
		const DWORD VsyncForcedOff = 0x1C;  // BYTE, when set the vsync setter gets 0
		const DWORD TimeAccumulated = 0x20; // float, seconds since the time origin
		const DWORD FrameDelta = 0x24;      // float, THE dt handed to FFX_MainStepLoop
		const DWORD TimeOffset = 0x28;      // float, subtracted from the raw clock
		const DWORD MeasuredFps = 0x30;     // float, refreshed every 2 seconds
		const DWORD FrameNumber = 0x34;     // int, ++ per frameTick
		const DWORD QuitRequested = 0x39;   // BYTE, frameTick returns 9 when set
		const DWORD FixedTimeStep = 0x3C;   // float, > 0 means the clock is never read
		const DWORD FrameCallback = 0x268;  // fn *, what WinMain calls, == frameTick
	} // namespace PApp

	namespace FfxApp
	{
		// The simulation hold byte. See the long comment above. All four vtable bodies
		// read it, and animate plus endFrame re-latch it from the pause menu state.
		const DWORD HoldSimulation = 0x3AD; // BYTE
		// Play time in seconds, accumulated from the raw frame delta, wrapping at 2400.
		// Mirrored to +0x120. Wall-clock derived, so do not use it as a sync clock.
		const DWORD PlayTimeSeconds = 0x3B0; // float
	} // namespace FfxApp

	namespace EscMenuLayout
	{
		const DWORD OpenFlag = 0x70;  // BYTE,  non-zero means open
		const DWORD OpenIndex = 0xE4; // int,   >= 0 also means open
	} // namespace EscMenuLayout

	// ---------------------------------------------------------------------------
	// The application pointer, Readable-checked. Null if the game has not built it
	// yet, which happens if a plugin asks too early.
	// ---------------------------------------------------------------------------
	BYTE* Application();

	// ---------------------------------------------------------------------------
	// Everything about the current frame, in one read so the values agree with each
	// other. Returns false and leaves out untouched if the application pointer is
	// not usable yet.
	// ---------------------------------------------------------------------------
	struct FrameClocks
	{
		DWORD phyreFrameTick;  // presented frames, unconditional
		DWORD phyreAppFrame;   // the app's own frame number, same rate
		DWORD mainStepCounter; // the engine's per-step frame number, gameplay only
		DWORD gameClock60Hz;   // in-game clock in 1/60 s ticks, menu-gated
		DWORD localStepCount;  // ours, from HookMainStep. 0 if not installed.
		float frameDelta;      // the dt this frame, seconds
		float fixedTimeStep;   // 0 means the delta came from the clock
		float measuredFps;
		float stepScale;      // 1.0 means one nominal 30 Hz step
		int frameRateDivider; // 1 is 59.94 Hz, 2 is 29.97 Hz
		int pendingSteps;     // simulation steps still owed this frame
		int pacingMode;       // 0 accumulator, 1 timing track. See IsPacingFromWallClock.
		bool isCatchUpStep;   // presentation is being suppressed right now
	};
	bool ReadFrameClocks(FrameClocks* out);

	// Individual readers, for when one value is all you want. Each returns false if
	// the address is not readable.
	bool PhyreFrameTickCount(DWORD* out);
	bool MainStepCounter(DWORD* out);
	bool GameClock60Hz(DWORD* out);
	bool StepScale(float* out);
	bool FrameDelta(float* out);

	// Our own step counter, incremented once per allowed FFX_MainStep. Zero until
	// HookMainStep has been installed. This is the lockstep clock.
	DWORD StepCount();

	// How many steps the gate has refused. Useful as a one-line health metric.
	DWORD StalledStepCount();

	// ---------------------------------------------------------------------------
	// Pause, which is four separate things that gate different amounts.
	// ---------------------------------------------------------------------------
	struct PauseState
	{
		bool escMenuOpen;        // the real pause. Stops FFX_MainStep entirely.
		bool holdByteSet;        // FFXApplication + 0x3AD, which is what actually gates
		bool menuSysRunning;     // in-game menu. FFX_MainStep still runs, gameplay does not.
		bool menuSysRunningAlt;  // as above, and FFX_StepPacing is skipped too
		bool menuOpenPending;    // a menu open requested, gates gameplay for one step
		bool saveUiBusy;         // the save UI state machine is not idle
		bool fadeBlocking;       // a fade or transition owns the gameplay block
		bool simulationWillStep; // FFX_MainStep will be called at all
		bool gameplayWillStep;   // the gameplay block inside it will run
	};

	// Reads all of it. fadeBlocking calls into the game, so CALL THIS FROM THE FRAME
	// THREAD ONLY. Pass includeFadeGate = false to skip that one call and leave
	// fadeBlocking false, which makes the function pure reads and safe anywhere.
	bool ReadPauseState(PauseState* out, bool includeFadeGate);

	// The cheap question: is the simulation currently frozen. Pure reads.
	bool IsSimulationPaused();

	// The game's own pause-menu test. A trivial pointer test on the singleton.
	bool EscMenuIsOpen();

	// ---------------------------------------------------------------------------
	// The simulation hold. See the long comment at the top for the recipe, and for
	// why these have to be driven from an animate hook rather than set once.
	//
	// HoldSimulation also returns false if the pointer is unusable, in which case
	// NOTHING was written and the caller must run the frame normally rather than
	// returning early. Check it.
	// ---------------------------------------------------------------------------
	bool HoldSimulation(void* application);
	bool ReleaseSimulationHold(void* application);
	bool IsSimulationHeld(void* application, bool* out);

	// ---------------------------------------------------------------------------
	// Catch-up and fast forward.
	// ---------------------------------------------------------------------------

	// Ask for n simulation steps in the next presented frame, 1 being normal. Write
	// this BEFORE the original animate runs. Values above about 8 have never been
	// asked of this engine, and FFX_StepPacing's own dt clamp caps it near 9, so
	// this refuses anything over maxReasonableCatchUpSteps.
	const int maxReasonableCatchUpSteps = 8;
	bool RequestCatchUpSteps(int steps);
	bool PendingSteps(int* out);

	// True while the engine is suppressing presentation for a catch-up step.
	bool IsCatchUpStep();

	// The shipped fast forward. Index 0 is off, 1 is 2x, 2 is 4x. BOTH MACHINES MUST
	// AGREE ON THE INDEX, so this is a settings value with network scope, not a
	// local preference.
	bool BoosterSpeedIndex(int* out);
	bool SetBoosterSpeedIndex(int index);
	float BoosterSpeedMultiplier();

	// How many times the inner sub-step loop runs inside one FFX_MainStep. Reads the
	// game's own getter, which folds in an override and a turbo flag.
	int SubStepCount();

	// ---------------------------------------------------------------------------
	// Frame limiting.
	//
	// 1 is 59.94 Hz, 2 is 29.97 Hz and is the gameplay default. The game itself
	// switches to 1 while the in-game menu is up.
	//
	// SetFrameRateDivider goes through the game's own setter, which clamps to 1 or 2
	// and maps 0 to 1. UncapFrameRate writes the global directly with 0, which the
	// setter would not allow, and that makes the Sleep period zero. That is a crude
	// way to run faster than real time and it ALSO SPEEDS UP THE SIMULATION, because
	// a shorter frame delta still asks FFX_StepPacing for one step. Prefer
	// RequestCatchUpSteps, which keeps the pacing honest.
	// ---------------------------------------------------------------------------
	int FrameRateDivider();
	bool SetFrameRateDivider(int divider);
	bool UncapFrameRate(bool uncapped);

	// ---------------------------------------------------------------------------
	// The fixed timestep, which is the determinism lever.
	//
	// When application + 0x3C is positive, frameTick uses it as the frame delta and
	// never reads the clock. The command line is -timestep=<seconds>, and the value
	// nothing else touches is 0.0333733, one 29.97 Hz frame. Nothing caches the
	// field, so a runtime write takes effect on the next frame.
	//
	// VERIFIED IN CODE, NEVER TESTED IN A RUNNING GAME. Confirm by watching audio
	// sync and the FMV path before trusting it.
	// ---------------------------------------------------------------------------
	const float nominalFixedTimeStep = 0.033373334f; // one 29.97 Hz frame

	float FixedTimeStep();
	bool SetFixedTimeStep(float seconds); // 0.0 hands the delta back to the clock
	bool FixedTimeStepFromCommandLine(float* out);

	// True while the step count is being derived from the wall clock rather than
	// from a fixed delta. Two ways that happens: no fixed timestep is set, or the
	// pacing mode is the cutscene timing track, which counts steps from the
	// millisecond clock and busy-waits when it is ahead. EITHER ONE IS A LOCKSTEP
	// DIVERGENCE SOURCE, so a desync detector should sample this.
	bool IsPacingFromWallClock();

	// ---------------------------------------------------------------------------
	// The per-step hook: a detour on FFX_MainStep.
	//
	// This is the narrow gate. It is one level below animate, so a refused step
	// leaves animate, the play-time step, FFX_GameTick, the Phyre update and the
	// whole render path running, and only the simulation stops. It is NOT the same
	// as the shipped pause, because the render path stays on the scene branch with
	// a display list that was not rebuilt, which is a configuration the shipped game
	// never produces. See the warning in reversing\MAIN_LOOP.md before using it for
	// a long stall. For a long stall use HoldSimulation from an animate hook.
	//
	// The gate returning false also zeroes the pending step count, so the do/while
	// in FFX_MainStepLoop cannot spin.
	//
	// A refused step also skips the pad read and the pad commit, because both live
	// inside FFX_MainStep ahead of FFX_StepPacing. For lockstep that is what you
	// want, since a stalled machine should not be consuming local input, but it does
	// mean the game's pad state is frozen too and anything of yours that reads it
	// will see the same buttons repeatedly.
	//
	// Both callbacks may be null. Install once at startup and gate at runtime.
	// ---------------------------------------------------------------------------
	typedef int(__cdecl* MainStepFn)(float dt);
	typedef bool(__cdecl* StepGateFn)(void);         // false means do not advance
	typedef void(__cdecl* StepObserverFn)(float dt); // runs after an allowed step

	bool HookMainStep(StepGateFn gate, StepObserverFn after);
	bool MainStepHookInstalled();
	MainStepFn OriginalMainStep();

	// Logs the whole frame picture once. Worth calling at startup to confirm the
	// bindings landed on something sensible, and on a desync to see where the two
	// machines differ.
	void LogMainLoopState();

} // namespace ffx
