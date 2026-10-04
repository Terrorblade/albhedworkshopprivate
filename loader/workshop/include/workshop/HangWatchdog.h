#pragma once

#include <windows.h>

// A FREEZE WITH A CALL STACK.
//
// The crash handler covers the case where the game dies. This covers the other
// one, where it stops dead and nothing is thrown, which on this project has
// already cost a whole debugging session with no evidence at all. A watchdog
// thread watches a counter that the game thread bumps on every simulation step.
// When that counter stops moving it suspends the game thread, reads its context,
// and writes the exact same report the crash handler writes, into the same file.
//
// It samples the hung thread MORE THAN ONCE, on purpose. One sample cannot tell a
// spin apart from something slow but progressing. Two samples with the same EIP
// can, and that is usually the whole answer.
//
// Known freeze mode on FFX, for reference: FFX_Ev_LoadEventPackage spins forever
// on an event id whose package does not ship, and only 330 of the 402 ids do.
//
// Usage, which mirrors the crash handler:
//
//     workshop::StartHangWatchdog();        // once, in Startup, any module
//     workshop::NoteHangWatchdogStep();     // from a step handler
//
// Like the crash handler, the state is shared across every module in the process
// through a named section, so the first plugin loaded owns the thread and every
// other plugin's step notes still feed the same counter.

namespace workshop
{

	// First caller in the process gets the watchdog thread, everyone else gets true
	// and no second thread. Safe from DllMain: it creates a thread and touches
	// nothing that needs the loader lock.
	bool StartHangWatchdog();

	// CALL THIS FROM A STEP HANDLER, nowhere else. It is what defines "the game is
	// still running", and it also records which thread the game thread is, which is
	// the thread the report will be about. An interlocked increment and two stores.
	void NoteHangWatchdogStep();

	// How long the game thread may go without a step before it counts as hung.
	// Default 10000 ms. A real map load on this engine takes a couple of seconds, so
	// going much below that will report loads as freezes.
	void SetHangThresholdMs(DWORD ms);
	DWORD HangThresholdMs();

	bool HangWatchdogRunning();

	// How many hang reports have been written this process. Non zero means the log
	// has something in it worth reading.
	DWORD HangReportCount();

	// True while the watchdog currently believes the game thread is stuck. Useful for
	// showing it in a panel.
	bool GameThreadLooksHung();

	// Which thread last called NoteHangWatchdogStep, so the simulation thread. 0
	// before the first step. Pass it to workshop::WriteThreadReportNow to get the
	// game's own call stack rather than the Present thread's.
	DWORD GameThreadId();

} // namespace workshop
