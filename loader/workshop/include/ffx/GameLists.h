#pragma once

// The game's own tables, enumerated into workshop::PickerList so a UI never has
// to hardcode an option or make the user type an id.
//
//     ffx::RefreshGameLists();
//     const workshop::PickerList& ev = ffx::EventList();
//     workshop::Picker("event", ev.Items(), ev.Count(), &state, &eventId);
//
// Every list is cached and only rebuilt by RefreshGameLists, because most of them
// read the save block and a list that changed under an open combo would move the
// row the mouse is over. Call Refresh when a panel opens, not every frame.
//
// A list whose table was not readable comes back empty with Live() false, so a
// caller can say "no game loaded" rather than drawing nothing.

#include "workshop/PickerList.h"

namespace ffx
{

	// Rebuilds all of them. Safe with no game loaded, in which case the save-block
	// lists come back empty.
	void RefreshGameLists();

	// ---------------------------------------------------------------------------
	// Events, which are also maps
	// ---------------------------------------------------------------------------

	// Every event package, id and internal name such as "bltz0001". In this game a
	// field map IS an event package, so this one list answers both "warp me
	// somewhere" and "start this event": the id is what FFX_Map_WarpTo and
	// FFX_Map_RequestChange take, and it is what EvCurrentEventId holds.
	//
	// EMPTY UNTIL SOMETHING CALLS LoadEventTable. See below.
	const workshop::PickerList& EventList();

	// ---------------------------------------------------------------------------
	// Characters
	// ---------------------------------------------------------------------------

	// All 18 character records. 0..7 are the playable characters and 8..17 the ten
	// aeons. The label is the player-facing name when a game is loaded, which is
	// what the rename screen would have changed, with the stable English name as
	// the fallback. An in-party character is marked.
	const workshop::PickerList& CharacterList();

	// Just 0..7, for "put this character in the party".
	const workshop::PickerList& PartyCharacterList();

	// Just 8..17.
	const workshop::PickerList& AeonList();

	// ---------------------------------------------------------------------------
	// Save block contents
	// ---------------------------------------------------------------------------

	// The 200-entry equipment array, in-use entries only. The id is the slot id the
	// game's own equip setter wants, not the array index. Empty with no game.
	const workshop::PickerList& EquipmentList();

	// The inventory slots that hold something. The id is the item id. Empty with no
	// game.
	const workshop::PickerList& InventoryList();

	// The auto-abilities that are legal on a weapon, and on an armour, read from the
	// game's own customise recipe table. The id is the 0x8000 based ability id.
	//
	// This is the list to offer, not KernelList(KernelAutoAbilities). The kernel
	// table has all 134 including five "Extra" placeholders and it does not say which
	// side a given ability belongs on, and kaizou.bin is the only table in the game
	// that does. The nine without a recipe do work in a slot, they just only appear
	// on shipped gear, so a panel that wants them can still fall back to the kernel
	// list behind a "show everything" toggle.
	const workshop::PickerList& WeaponAbilityList();
	const workshop::PickerList& ArmourAbilityList();

	// One line per list, to the log. For finding out which table is not readable
	// without adding a panel for it.
	void LogGameLists();

	// ---------------------------------------------------------------------------
	// The event id table, which starts empty and can be filled
	//
	// FFX_LoadEventIdTable reads /ffx/proj/event/header/eventid.bin and builds a
	// 16-byte-per-row table: the name at +0, the row index written over +12 as the
	// id. Both of its callers are conditional, FFX_MainInit only in debug mode and
	// AutoTestManager only on the "EnableAutoTest" command, so on a normal boot the
	// table is empty.
	//
	// It is empty, not unavailable. The read goes through Sg_PcRead, whose "host0:"
	// prefix looks like a devkit loose-file path but is not: FFX_File_OpenHost0
	// strips "host0:/" and prepends "/ffx_ps2/", which is exactly where the file
	// lives inside the shipped archive. So the read succeeds, and one call to
	// LoadEventTable gets all 402 names.
	//
	// Two traps for anything that reads the table once it IS filled. A row's name
	// field is only 12 bytes because the id write lands at +12, and one real entry
	// (id 251, "test_yanagi2") is exactly 12 characters, so its terminator is
	// overwritten. Never trust a NUL, stop at 12. And two rows, ids 101 and 111,
	// hold two binary bytes rather than a name, so screen rows on printable ASCII.
	// ---------------------------------------------------------------------------

	// Whether warping to this event id is safe. THIS IS NOT A NICETY.
	// FFX_Ev_LoadEventPackage checks the package magic and then spins in while(1) with
	// no break and no return, so loading an id whose package does not ship hard locks
	// the simulation thread and the only way out is killing the process.
	//
	// Of the 402 ids, 348 have an asset path and 18 of those ship no file, which leaves
	// 330. Both halves are checked here, the path at runtime and the 18 from a baked
	// list, because a missing file cannot be detected from inside the process without a
	// file existence call per id.
	//
	// Anything that takes an id from a human, rather than from EventList, must call this
	// first.
	bool EventIdLoadable(int eventId);

	// Calls the game's own loader to fill the table, and returns whether it is
	// filled afterwards. Free to call repeatedly, the engine has a one-shot guard
	// and checks it before doing anything.
	//
	// CALL THIS FROM THE GAME THREAD. It allocates through the engine's allocator
	// and reads the archive, and neither has been shown to be safe from the render
	// thread. Filling it also makes the game's own "JumpMap <name>" console command
	// and the debug GUI's map jump work, which they do not on a stock boot.
	bool LoadEventTable();

	// False when the table has not been filled, which is the state on a normal
	// boot. Checks the engine's own once-only guard and sanity checks the count, so
	// a pointer left at 0xFFFFFFFF reads as not loaded rather than as a huge table.
	bool EventTableLoaded();

	// How many rows the engine's table holds, 0 when it is not loaded. 402 on this
	// build once it is.
	int EventTableCount();

	// Row accessor. outName should be at least 13 bytes. Returns false for an out
	// of range row, an unloaded table, or a row whose name is not printable.
	bool EventTableRow(int row, int* outEventId, char* outName, int outBytes);

	// The internal name for an event id, or NULL. Copies into a caller buffer for
	// the terminator reason above.
	bool EventNameForId(int eventId, char* outName, int outBytes);

	// The event the game is on right now, out of EvCurrentEventId. This is the
	// value FFX_Ev_LoadEventPackage was last given, and the one
	// FFX_Map_GetCurrentMapName looks up. Zero is a real id ("event00"), so there
	// is no "unknown" return, check GameLoaded first if that matters.
	int CurrentEventId();

} // namespace ffx
