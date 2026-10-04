#pragma once

#include <windows.h>

// Makes the engine's input and menu timing deterministic by feeding the call
// sites that pace gameplay a time derived from the simulation step instead of
// the wall clock.
//
//     ffx::InstallStepClock();           // once, at startup
//     ffx::SetStepClockTime(step * 0.033373334f);   // once per simulation step
//
// Until SetStepClockTime is called the hooks pass the real clock through, so a
// solo game behaves exactly as the shipped game does.
//
// WHAT IT PATCHES, and why each one matters:
//
//   pad auto repeat         held button repeat, read by one shipped scene
//   MenuSys pad sample      the native menu's cursor repeat
//   MesWin pad sample       writes MenuPadWord10, which the BATTLE MENU reads
//   battle menu page open   stamps a page's start time
//   battle menu page step   the 0.30 s / 0.45 s input lockout on every page
//   lulu fury now / mark    the 0.5001 s window that clears Lulu's KEYBOARD latches
//
// All seven are call site patches, not detours, so the clock functions stay
// callable by address and their other 30-odd callers are untouched.

namespace ffx
{

	// Patches every site it can. Returns false only if none of them took.
	bool InstallStepClock();
	bool StepClockInstalled();

	// How many of the seven sites are patched, and the total.
	int StepClockSitesPatched();
	int StepClockSiteCount();

	// Once per simulation step, from the step path. Any value works as long as
	// both peers compute it from the same step, because every consumer reads
	// differences rather than the absolute value.
	void SetStepClockTime(float seconds);
	float StepClockTime();

	// True once SetStepClockTime has been called, which is when the hooks stop
	// passing the real clock through.
	bool StepClockFeeding();

	// Stops feeding, so the hooks hand back the wall clock again. For ending a
	// session without unpatching anything.
	void ReleaseStepClock();

	const char* StepClockStatus();
	void LogStepClock();

} // namespace ffx
