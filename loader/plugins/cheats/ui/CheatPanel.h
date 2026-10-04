#pragma once

// The cheat panel, drawn on the workshop's ImGui overlay. F11 shows the overlay.
//
//     cheats::StartCheatPanel();

namespace cheats
{

	// Installs the overlay if nobody has yet and registers the panel. False means
	// the panel is unavailable, and the log says why.
	bool StartCheatPanel();

	// Rebuilds every game-data list. Not every frame, because a list that changes
	// under an open combo moves the row the mouse is over.
	//
	// ONLY FROM THE GAME THREAD. The lists call the engine's file layer, and the
	// event probe opens 402 files. Anything in the UI wants RequestCheatListRefresh
	// instead, because a panel draws on whoever calls Present.
	void RefreshCheatLists();

	// Arms a rebuild for the next simulation step. This is what every button in the
	// UI calls. Cheap, idempotent, and safe from any thread.
	void RequestCheatListRefresh();

	// Tries the export cache, once per process. True when every list came off disk
	// and the engine was never asked for any of them.
	//
	// CALL THIS BEFORE ANYTHING PROBES. The event probe makes the engine open and
	// close 402 files, so trying the cache afterwards would pay the whole cost and
	// then throw the answer away.
	bool TryExportedCheatLists();

	// Arms a fresh event package measurement, for after something has added one.
	// 402 file opens, so it happens on a step rather than under the button.
	void RequestEventReprobe();

	// Does an armed rebuild, and takes the event package measurement the first time
	// through. CALL THIS FROM A STEP HANDLER AND NOWHERE ELSE.
	void PumpCheatListRefresh();

} // namespace cheats
