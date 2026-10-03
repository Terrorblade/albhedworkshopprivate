#pragma once

// The co-op settings, as native rows on the game's own in-game Config screen.
//
// Until now the only way to change who drives which character was a debug hotkey, and
// the host menu override was ctrl+F4. Hotkeys are fine for the person who wrote them
// and invisible to everybody else, so the settings that a second player actually needs
// live here instead, on the screen where a player already goes to change settings.
//
// ## WHAT THE ROWS ARE
//
//   Co-op                 a header. Not selectable, nothing to change, it is there so
//                         the rows below it do not read as part of the game's own list.
//   <name in slot 0>      who plays that character: Host, Player 2 or Player 3
//   <name in slot 1>      the same
//   <name in slot 2>      the same
//   Host drives menus     on or off. The same thing ctrl+F4 does.
//
// Input delay is deliberately not here. It is a per-machine latency knob that needs no
// agreement, and the lockstep enforce toggle is a debug control that belongs on its
// hotkey.
//
// ## WHY THIS WORKS AT ALL, in one paragraph
//
// Module 10, the Config screen, draws its rows out of two writable globals: a pointer
// to an array of row-object pointers, and a row count that is an ordinary int. So the
// mod builds a longer array in its own memory, copies the game's eight pointers into
// the front of it, appends five of its own, and points the globals at that. The game
// then draws, navigates and actions our rows with its own code. Nothing of the game's
// is modified: not the eight row objects, not the four arrays they live in. The worst
// failure available is a missing row. See ffx/MenuSystem.h for the row layout and the
// three things about it that bite.
//
// The labels come from a detour on the kernel string lookup, answering only for a
// reserved id range and passing everything else through. That is what makes the labels
// dynamic: the row for slot 0 says "Tidus" because the string is built at draw time,
// and the help line at the bottom of the screen says who that character belongs to.
// The glyph encoding that makes this possible is documented in ffx/MenuSystem.h.
//
// ## THE PART THAT MATTERS: EVERY CHANGE IS AN ORDERED COMMAND
//
// A row's setter never writes ownership. It asks.
//
// Both machines run module 10 from the same replicated pad block, so both reach the
// same setter on the same step with the same new value, and a local write would
// *usually* be fine. Usually is not good enough here. Two machines acting on different
// ownership is the one failure in this subsystem that cannot be recovered from, and the
// cases where "usually" fails are real: one machine's rows did not install, the string
// override was refused on one side, a hole in the pad replication. So the change goes
// out as kCommandCharOwner, the host stamps a step, and both machines apply it on that
// step or neither does.
//
// Two consequences worth knowing before reading the code:
//
//   ONLY THE MENU DRIVER ASKS. The setter runs on every machine, so if every machine
//   asked, one press would produce two or three identical commands. MenuSync::MenuDriver
//   is the same answer on both machines, so gating the ask on "am I the driver" is safe
//   and costs nothing.
//
//   THE ROWS ARE A VIEW, NOT THE STATE. Every row's current value is recomputed from
//   the authoritative state at the top of every simulation step. A press moves the row
//   for one step, the ask goes out, and the row snaps back until the command lands.
//   That is the same trade BoosterSync makes and for the same reason: it is a settings
//   change, not a movement input, and nobody notices sixty milliseconds on a menu
//   toggle.
//
// ## THE ONE THING THE SELECTABLE FLAG MAY NOT DEPEND ON
//
// Up and Down skip any row whose selectable flag is not 1, and that navigation is
// driven by replicated input. So if the two machines disagree about which rows are
// selectable, their cursors land on different rows and the next Left or Right changes
// two different settings. Every selectable flag here is therefore derived from state
// both machines already share, which in practice means the active party order. In
// particular the host override row is selectable on a CLIENT too, even though a client
// cannot change it: refusing the press with a log line is correct, making the row
// unselectable on one machine only is not.
//
// ## SOLO PLAY
//
// No session, no rows. The Config screen is byte for byte the shipped game's, and the
// table is put back the moment a session ends. The restore is deferred while the Config
// screen is actually up, because shortening the array under a cursor that is sitting
// past the new end is an out of bounds read the game does not guard against.

namespace pilgrimage
{

	// Installs the per-step tick and the UI string override. Once at startup, not per
	// session: nothing is injected until a session starts, so a solo game is
	// unaffected with this in.
	//
	// Returns false when either hook was refused, and in that case no rows will ever
	// be injected. That is a missing feature, not a hazard.
	// Once per simulation step, from the lockstep gate's FFX_MainStep pre-hook, and
	// once per frame from the frame hook.
	//
	// BOTH CALLS ARE WANTED. The gate call is the one that matters for correctness,
	// because an ordered ownership binding may only be consumed on the exact step it
	// was stamped for and the frame path misses one on every catch-up step. The frame
	// call exists because the gate does not run at all when the clock is not running,
	// and a session that ends with the Config screen up still has to have the game's
	// own row table put back. Everything step-sensitive is guarded on the step number
	// having changed, so the extra call costs a status rebuild.
	void ServiceCoopConfigStep();

	bool InstallCoopConfig();
	bool CoopConfigInstalled();

	void StartCoopConfig();
	void StopCoopConfig();
	bool CoopConfigActive();

	// Are our rows on the Config screen right now.
	bool CoopConfigRowsInjected();

	const char* CoopConfigStatus();
	void LogCoopConfig();

} // namespace pilgrimage
