#pragma once

#include <windows.h>

// Making the in-game menu work for two people, with exactly one of them driving.
//
// The Triangle menu is party, equipment, abilities, items, config and the Sphere Grid.
// It runs INSIDE FFX_MainStep, which is the whole reason this is tractable: the menu is
// part of the simulation step the lockstep gate already controls, so both machines step
// it together for free and the only questions left are who drives and what the menu
// sees.
//
// ## WHO DRIVES, which is three rules in priority order
//
//   1. The host override, if a host has taken control with ctrl+F4.
//   2. While the SPHERE GRID is the active screen, the owner of the character being
//      edited drives. Control therefore changes hands as the editing player L1/R1s
//      between characters, which is what was asked for.
//   3. Otherwise whoever opened the menu drives, for as long as it is up.
//
// Every one of those is derived from REPLICATED state and nothing local. The override
// is a flag set by an ordered command, the grid character is a byte in the menu work
// block that both machines wrote from the same input, and the opener's identity rides
// in on the open command as its issuer. Two machines picking different drivers is the
// one failure in this subsystem with no way back, so there is no path here that reads
// anything only one machine knows.
//
// Ownership itself is asked of coop/Ownership.h. There is one table.
//
// ## WHAT GOES ON THE WIRE, AND WHAT DOES NOT
//
// On the wire: the open, as a command. The host override, as a command. That is all.
//
// Not on the wire, because they are already replicated:
//
//   THE INPUT. The owner's buttons are already in the lockstep ring, because every
//   peer submits its whole button mask every step regardless of what is on screen. So
//   the menu's input is read back out of the ring rather than sent again.
//
//   THE CLOSE. There is no close function to intercept. FFX_MenuSys_StepFrame calls
//   FFX_MenuSys_AnythingStillRunning 0x8AA610 at the end of every menu frame and
//   clears g_ffxMenuSysRunning when it comes back zero, which happens when the last
//   module has stopped itself. Module 1 stops itself when it sees the cancel button in
//   the pad block, and the pad block is what this file replicates. So both machines
//   close on the same step without being told to. The OPEN is different only because
//   the Triangle test lives in FFX_MainStep reading g_ffxPlayerPadPressed, before the
//   menu system exists, and that global is not replicated yet.
//
//   THE AUTO-REPEAT. Derived from the step counter on both machines rather than
//   transmitted. See the long note on that in MenuSync.cpp, it was a real decision.
//
// ## HOW THE OPEN IS INTERCEPTED, AND WHY NOT WITH A DETOUR
//
// FFX_MenuSys_RequestOpen 0x821750 cannot carry a 5-byte jmp detour. Its prologue is
// 55 8B EC 56 E8 A7 69 E2 FF, which is push ebp / mov ebp,esp / push esi and then a
// REL32 CALL at +4, so any five bytes stolen from the front either split that call or
// relocate a rel32 to somewhere meaningless. workshop/Detour.h refuses both on
// purpose.
//
// So the open is intercepted at the global instead. FFX_MenuSys_PollOpenAndStep makes
// the request in one branch and CONSUMES it in the other, never both in one call, and
// when the menu is not running only one of its three call sites in FFX_MainStep runs
// per step. So a pending mode written during step N is not acted on until step N+1,
// and writing -1 back from the lockstep gate at the top of step N+1 cancels it with a
// whole step to spare. That turned out to be better than a detour anyway: it catches
// every path that sets the pending mode, not just the one function.
//
// ## THE ONE HOLE THIS DOES NOT CLOSE
//
// The Sphere Grid reads the raw pad for the stick, twice, through
// FFX_MenuSys_ReadAnalogByte, which bypasses the 192-byte pad block entirely. See the
// grid section in MenuSync.cpp. It is neutralised rather than replicated, and the
// reason is written down there.

namespace pilgrimage
{

	// Installs the pad hook. Once at startup, not per session: the callback leaves the
	// block alone when no session is running, so a solo game is byte for byte the
	// shipped game with this in.
	bool InstallMenuSync();
	bool MenuSyncInstalled();

	void StartMenuSync();
	void StopMenuSync();
	bool MenuSyncActive();

	// Call once per simulation step from the lockstep GATE, before the game steps,
	// beside PrepareDialogueInput.
	//
	// The gate rather than the after-step observer, and this is not a style choice.
	// FFX_MenuSys_SamplePad runs inside FFX_MainStep, so anything decided after the
	// step would arrive a step late. And the edge and the repeat counters are worked
	// out HERE, once per step, rather than inside the callback: the sampler can fire
	// more than once in a presented frame, and deriving a button edge from how many
	// times a machine happened to sample would make the menu depend on the frame rate,
	// which is the one thing this whole layer exists to prevent.
	//
	// Safe to call more than once for the same step. A refused step calls the gate
	// again for the same step number, and recomputing would consume the edge twice.
	void PrepareMenuInput();

	// The host override toggle, raised by ctrl+F4 and drained on the game thread.
	//
	// It goes out as an ordered command rather than flipping a local flag, because a
	// local flip would mean the two machines disagreed about the owner for the length
	// of a round trip, and acting on different owners is unrecoverable. Host only: a
	// client calling this is refused with a log line.
	void RequestMenuControlOverride();

	// Is the host holding control right now. Both machines agree on this, because it
	// only ever changes when a command lands.
	bool MenuControlOverrideHeld();

	// Which peer is driving the menu, or -1 when no menu is up.
	int MenuDriver();

	// ---------------------------------------------------------------------------
	// The desync detector's contribution.
	//
	// The menu is unusually cheap to check: a divergence reproduces, it happens while
	// nothing else in the game is moving, and the hash says which half drifted. These
	// go into the spare slots of the existing checksum message, past the 13 save-block
	// buckets, so there is no protocol change and a mismatch is reported by the machinery
	// that already exists.
	// ---------------------------------------------------------------------------
	const int kMenuHashRegions = 2;

	// Fills up to maxRegions hashes and returns how many it wrote. Returns 0 when no
	// menu is up, which is the normal case and costs nothing.
	int MenuHashRegions(DWORD* out, int maxRegions);

	// True while a menu is up, so the caller can drop the checksum interval to every
	// step. A menu desync is worth catching on the step it happens rather than up to a
	// second later, and while a menu is up nothing else is generating traffic.
	bool MenuWantsPerStepChecksum();

	const char* MenuSyncStatus();
	void LogMenuSync();

} // namespace pilgrimage
