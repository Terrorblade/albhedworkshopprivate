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

#include "workshop/PickerCache.h"

namespace ffx
{

	// Rebuilds all of them. Safe with no game loaded, in which case the save-block
	// lists come back empty.
	void RefreshGameLists();

	// JUST THE SAVE DERIVED ONES: characters, party, aeons, equipment, inventory.
	// Those five change as the player plays, so they are the lists the export cache
	// deliberately does not hold and this is what rebuilds them after a cache load.
	void RefreshSaveDerivedLists();

	// ---------------------------------------------------------------------------
	// The per-id package measurement, for the export cache.
	//
	// This is the expensive thing and the dangerous thing at once. Producing it
	// makes the engine open and close 402 files, and it is also what
	// EventIdLoadable answers from, which is what stops a warp picking an id whose
	// package does not ship. FFX_Ev_LoadEventPackage answers a missing package with
	// while(1), so getting this wrong hangs the game with no way out.
	//
	// So it is cached with the lists rather than measured every boot. See
	// ffx/ListExport.h.
	// ---------------------------------------------------------------------------
	int EventProbeCount();
	bool EventProbeRow(int eventId, unsigned* outBytes, bool* outHasPath, char* outLabel, int labelBytes);
	bool SetEventProbeRow(int eventId, unsigned bytes, bool hasPath, const char* label);

	// Says the probe table is trustworthy without having run the probe, which is
	// what loading it from the cache means. "how" is kept for the log only.
	void MarkEventPackagesProbed(const char* how);

	// ---------------------------------------------------------------------------
	// Events, which are also maps
	// ---------------------------------------------------------------------------

	// Every event package, id and internal name such as "bltz0001". In this game a
	// field map IS an event package, so this one list answers both "warp me
	// somewhere" and "start this event": the id is what FFX_Map_WarpTo and
	// FFX_Map_RequestChange take, and it is what EvCurrentEventId holds.
	//
	// LOADABLE IDS ONLY, so it is safe to warp to anything in it. See
	// EventIdLoadable below for why that matters.
	const workshop::PickerList& EventList();

	// EVERY id in the space, loadable or not, which is the list that shows the
	// developers' test and sample packages: test01 through test39, sample01,
	// testbattle, testpub1 to 4, testfont, loopdemo, scene1 to 9, startmap0 and the
	// rest. A row that cannot be loaded carries "[no package]" or "[no path]" at the
	// front of its label, where a filter will find it.
	//
	// NOTHING MAY WARP FROM THIS LIST WITHOUT CALLING EventIdLoadable FIRST. The
	// marker in the label is for the person reading it, it is not a guard.
	//
	// The name comes from eventid.bin when that table has been filled, because
	// "testbattle" is a name only there. Call LoadEventTable before
	// RefreshGameLists if you want names rather than path stems.
	const workshop::PickerList& AllEventList();

	// ---------------------------------------------------------------------------
	// Measuring which packages actually ship, instead of trusting a baked list
	//
	// FFX_Asset_GetSizeForIndex resolves an asset path and then OPENS THE FILE to
	// measure it, returning 0 when it cannot. That makes it an existence test, and
	// the only one that will notice a package an editor has ADDED or removed. Once
	// it has run, both event lists and EventIdLoadable use it in preference to the
	// baked deny list, in both directions.
	// ---------------------------------------------------------------------------

	// Takes the measurement and rebuilds the two event lists from it. One file open
	// per id, 402 of them, so this is a one-shot: calling it again does nothing.
	//
	// CALL THIS FROM THE GAME THREAD. It goes through the engine's file layer. It
	// does nothing before the asset loader table is filled, which is the state
	// during DllMain, so an early call is harmless but also useless.
	void ProbeEventPackages();

	// Measures again, for after something has added a package. Same thread rule.
	void ReprobeEventPackages();

	// Whether the measurement has been taken. False means loadability is coming from
	// the baked deny list.
	bool EventPackagesProbed();

	// What the id's .ebp measures, rounded up to 16 by the engine. 0 means the file
	// is not in the archive, or that nothing has measured yet.
	unsigned EventPackageBytes(int eventId);

	// ---------------------------------------------------------------------------
	// Battles
	// ---------------------------------------------------------------------------

	// EVERY FIGHT THE GAME CAN START, walked out of btl.bin in memory: scenes, then
	// zones, then formation entries. See ffx/Encounter.h for the walk itself.
	//
	// The id is the BATTLE ID, (mapId << 16) | encounterId, which is exactly what
	// ffx::RequestScriptedBattle takes, so a selection feeds it with no second
	// lookup.
	//
	// EMPTY UNTIL THE FIRST BATTLE. btl.bin is read at the first battle init, so
	// there is nothing to walk at the title screen and that is not an error. A zone
	// whose encounter rate is 0 has no random battles, so its fights are the ones a
	// script starts, and those rows are marked "[scripted]". That is where the
	// monster arena, the penalty fights and the developers' test battles are.
	const workshop::PickerList& BattleList();

	// Where a battle id sits in the encounter table. For anything that needs more than
	// the id, which is everything that wants the fight's own battle field file, because
	// that is keyed by (scene, zone, slot) rather than by map and encounter. False for
	// an id that is not in BattleList.
	bool BattleListLocate(int battleId, int* outScene, int* outZone, int* outSlot);

	// Whether BattleList's labels NAME THE MONSTERS in each fight, so "Sahagin x3"
	// rather than "bjyt02 map 32 enc 0". That is what makes the picker filterable by
	// monster, which is how a person actually looks for a fight.
	//
	// OFF BY DEFAULT, because turning it on reads one file out of the archive per
	// fight, 863 of them, each a VBF open plus an inflate. It is a few seconds of
	// one-off work, not something to do on every boot.
	//
	// Setting it takes effect at the next RefreshGameLists, which must be on the game
	// thread like any other list rebuild.
	bool BattleMonsterNames();
	void SetBattleMonsterNames(bool on);

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

	// ALL 64 KEY ITEM SLOTS, which is the ones with a name and the ones without. The
	// id is the 0xA000 based id that ffx::HasKeyItem and ffx::SetKeyItem take.
	//
	// About 52 are real. The rest are declared rows that nothing named, and they are
	// listed as "unnamed <n>" because an unnamed row is still a flag the save file
	// carries and still something an editor could fill.
	//
	// Empty until the battle kernel has loaded, because the name function resolves a
	// string offset against a table pointer that is null before then.
	const workshop::PickerList& KeyItemList();

	// The auto-abilities that are legal on a weapon, and on an armour, read from the
	// game's own customise recipe table. The id is the 0x8000 based ability id.
	//
	// This is the list to offer, not KernelList(KernelAutoAbilities). The kernel
	// table has all 134 including five "Extra" placeholders and it does not say which
	// side a given ability belongs on, and kaizou.bin is the only table in the game
	// that does. The nine without a recipe do work in a slot, they just only appear
	// on shipped gear, so a panel that wants them can still fall back to the kernel
	// list behind a "show everything" toggle.
	// THE ARCHIVE DERIVED LISTS, for workshop::WritePickerList and friends. These
	// are the ones whose content cannot change unless the game's archive does, so
	// they can be exported once and read back on every later boot. The character,
	// party, aeon, equipment and inventory lists are deliberately NOT here: they
	// come out of the save block and have to be rebuilt.
	int CacheableGameLists(workshop::CacheableList* out, int max);

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
	// 330 on the shipped archive.
	//
	// After ProbeEventPackages has run this is a MEASUREMENT: the engine opened the
	// file. Before it has, it is the path table plus a baked list of the 24 ids known
	// to ship nothing. The measurement wins when both exist, including when it
	// disagrees, because a baked list cannot know about content that was added.
	//
	// Anything that takes an id from a human, rather than from EventList, must call this
	// first. That includes everything taken from AllEventList.
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
