#pragma once

// The cheat panel, drawn on the workshop's ImGui overlay. F11 shows the overlay.
//
//     cheats::StartCheatPanel();

namespace cheats
{

	// Installs the overlay if nobody has yet and registers the panel. False means
	// the panel is unavailable, and the log says why.
	bool StartCheatPanel();

	// Rebuilds every game-data list. The panel calls this when it opens and when
	// the Refresh button is pressed, not every frame, because a list that changes
	// under an open combo moves the row the mouse is over.
	void RefreshCheatLists();

} // namespace cheats
