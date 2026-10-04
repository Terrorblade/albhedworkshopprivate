#pragma once

// One function per tab of the cheat panel. Split by what they touch rather than
// by screen, so a piece of research lands in exactly one file.
//
//   TabsSaveData.cpp    gil, party, stats, equipment, inventory
//   TabsWorld.cpp       warp, events
//   TabsBattle.cpp      the dev battle debug flags and overdrive modes
//   TabsSphereGrid.cpp  node activation
//   TabsModels.cpp      the model list and the player model swap
//   TabsDebug.cpp       the shipped debug menu, and restoring its input
//   TabsMinigames.cpp   launching one, and editing the running one
//
// Every tab assumes it is on the render thread, so nothing here calls an engine
// function that is not a plain read of a static buffer. The two that do call the
// game, the party setter and the warp, say so at the call.

namespace cheats
{

	// TabsSaveData.cpp
	void DrawGameTab();
	void DrawPartyTab();
	void DrawCharactersTab();
	void DrawEquipmentTab();
	void DrawInventoryTab();

	// TabsWorld.cpp
	void DrawWarpTab();

	// TabsBattle.cpp
	void DrawBattleTab();

	// TabsSphereGrid.cpp
	void DrawSphereGridTab();

	// TabsModels.cpp
	void DrawModelsTab();

	// TabsDebug.cpp
	void DrawDebugTab();

	// TabsMinigames.cpp
	void DrawMinigamesTab();

	// Shared helper: a line saying a subsystem is still being mapped, and what is
	// missing. Keeps the honest "not wired yet" wording in one place.
	void PendingNote(const char* subject, const char* needs);

	// Shared helper: the red "no game loaded" line every save-block tab wants.
	// Returns true when there IS a game, so a tab reads
	// "if (!RequireGame()) return;".
	bool RequireGame();

} // namespace cheats
