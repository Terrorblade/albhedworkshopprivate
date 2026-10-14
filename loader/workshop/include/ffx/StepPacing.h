#pragma once

#include <windows.h>

// FFX_StepPacing 0x821E80, read and observed.
//
// StepPacing turns the frame delta into g_ffxPendingSteps, the number of FFX_MainStep
// calls this presented frame still owes. It runs once per FFX_MainStep, from the top of
// the step after the pad update, and it is skipped entirely while MenuSysRunningAlt is
// set. It only recomputes the pending count when that count is already zero, so on a
// catch-up frame the first call decides and the others only advance the clock.
//
// Everything mode 0 decides is a function of the dt argument, the two carried fractions,
// the 0.3 s clamp and FFX_StepPacing_AllowCatchUp. Mode 1 (a voiced timing track) adds
// the wall clock, which is why a recording also carries the track state. The function
// itself is in reversing\MAIN_LOOP.md, and the port it is checked against is Project
// Zanarkand's src/engine/app/StepPacing.
//
// THE DT IT SEES IS NOT THE FRAME DELTA. FFX_MainStep multiplies its own argument by the
// booster speed before passing it on, so with fast forward on the argument is 2x or 4x
// application + 0x24. Record both and compare.
//
// Every read here is of a module global, validated once and then read plainly, which is
// what makes it cheap enough to call twice per step.

namespace ffx
{

	// The mode 0 state and the gates, in the order FFX_StepPacing reads them.
	struct StepPacingVars
	{
		int pendingSteps;      // g_ffxPendingSteps
		int catchUpExtraSteps; // g_ffxCatchUpExtraSteps, mode 1 only
		int pacingMode;        // 0 accumulator, 1 timing track
		float frac60;          // g_ffxPacingFrac60
		float frac30;          // g_ffxPacingFrac30
		DWORD clock60Hz;       // g_ffxGameClock60Hz, +2 per step outside menus
		int menuSysRunning;
		int menuSysRunningAlt;
		int menuOpenPending;
		int saveUiState; // 0 is idle, which is all FFX_SaveUi_IsIdle tests
	};
	bool ReadStepPacingVars(StepPacingVars* out);

	// The mode 1 state. Written only by FFX_StepPacing and FFX_StepPacing_LoadTimingTrack.
	struct TimingTrackVars
	{
		float nextTime;  // milliseconds, the entry just read
		float tolerance; // milliseconds of slack
		float dueTime;   // when this step was due
		float elapsed;   // since the track started
		LONGLONG startUs;
		LONGLONG pauseUsPending;
		LONGLONG pauseUsTotal;
		int firstStep; // 1 until the first step of a track has run
	};
	bool ReadTimingTrackVars(TimingTrackVars* out);

	// The frame around a pacing call: which presented frame it is, what the application
	// handed animate as dt, and the multipliers that stand between that and the argument.
	struct PacingFrameInputs
	{
		DWORD phyreFrameTick;  // g_phyreFrameTickCount, every presented frame
		DWORD appFrameNumber;  // application + 0x34
		DWORD mainStepCounter; // g_ffxMainStepCounter, stalls in menus and movies
		float frameDelta;      // application + 0x24, the dt FFX_MainStepLoop got
		float fixedTimeStep;   // application + 0x3C, 0 means the clock is read
		int frameRateDivider;  // 1 is 59.94 Hz, 2 is 29.97 Hz
		int boosterEnabled;
		int boosterSpeedIndex;
		int subStepCount; // the raw PlayerSubStepCount global, not the getter's answer
	};

	// Reads the application fields through a pointer validated once, and again only when
	// the singleton changes, so unlike ReadFrameClocks this makes no syscall per call.
	// Fields it could not read are left 0 and the result is false.
	bool ReadPacingFrameInputs(PacingFrameInputs* out);

	// ---------------------------------------------------------------------------
	// The observer hook. A detour on FFX_StepPacing that calls the observer before the
	// original with afterCall false, runs the original, then calls it again with afterCall
	// true. The observer cannot change anything, the original always runs, and the return
	// value goes back unchanged.
	//
	// Chains behind another plugin's detour on the same function, see workshop/Detour.h.
	// One observer per module. Installing again replaces the observer and keeps the hook.
	// ---------------------------------------------------------------------------
	typedef void(__cdecl* StepPacingObserverFn)(float dt, bool afterCall);
	bool HookStepPacing(StepPacingObserverFn observer);
	bool StepPacingHookInstalled();

} // namespace ffx
