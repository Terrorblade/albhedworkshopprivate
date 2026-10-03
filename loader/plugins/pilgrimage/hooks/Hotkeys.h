#pragma once

// In-game keys. Polled from the frame hook on the game thread.
//
//   F4            hand the arrow keys to the next clone
//   F5            force-visible toggle, through the detour
//   F6            hold the engine's cull override on
//   F7            cycle which chr id F9 spawns, shift walks backwards
//   F8            dump a character field diff against the real player
//   F9            spawn a clone
//   F10           despawn the one you are driving, shift+F10 despawns all
//   F11           show or hide the control window
//   arrow keys    walk the clone that holds the input focus, shift to run

namespace pilgrimage
{

	void PollHotkeys();

	// The key list as one string, for the startup banner and the control panel.
	const char* HotkeySummary();

} // namespace pilgrimage
