#pragma once

#include <windows.h>

// Which parts of the save block sit still, measured rather than assumed.
//
// A lockstep desync detector hashes game state and compares it between machines.
// That only works over regions that change when the simulation changes and stay
// put otherwise. A region that ticks on its own produces a mismatch every frame
// and the detector becomes noise that gets switched off.
//
// The game-state research proved playtime ticks per frame and excluded it, but
// could only prove that one. The rest is a reasonable guess. This settles it the
// cheap way: hash every bucket for a few hundred frames while the game sits
// still, and report which ones moved and how often.
//
// Run it standing in a safe spot with nothing happening. Anything that moves
// while the player is not acting either belongs out of the detector's combined
// hash, or is something worth understanding before trusting the detector at all.
//
// Game thread only. Reads save data, so it goes where the frame hook runs.

namespace pilgrimage
{

	// Starts a run. Any run already going is abandoned. Does nothing and says so if
	// no game is loaded, since there is no save block to read yet.
	void BeginHashProbe(LONG frame, int frames);

	// Called once per frame from the frame hook. Cheap and returns immediately when
	// no probe is running.
	void StepHashProbe(LONG frame);

	bool HashProbeRunning();

	// A one-line status for the control panel, for example "hash probe: 140/300".
	// Never NULL.
	const char* HashProbeStatus();

} // namespace pilgrimage
