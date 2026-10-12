#pragma once

// WHERE A MODEL IS USED, because FFX does not name its NPCs.
//
// There is no NPC name table in the game. All four chrId to text functions in the
// binary are sprintf on the number, npc.tbl is exactly 480 bytes for 239 entries with
// no room for a string, and the one table that carries model names per category,
// /ffx/proj2/chr/common/<dir>.tbl, is not in the archive. So "n030" is the only name
// the game has for n030, and the best a picker can offer instead is the places the
// game actually puts it.
//
//     ffx::LoadEventPlaces();                            // once, blocks on disk
//     const char* where = ffx::ModelPlaceLabel(0x201E);  // "Besaid, Luca +2"
//
// HOW IT IS WORKED OUT, all measured against the 397 shipped packages:
//
//   A model is placed by script, not by a field. FFX_AtelOp_SetActorModel, syscall
//   core:1, pops a chr id and stores it on the actor, so the authored ids are the
//   operands of the pushimm in front of every core:1 call. 5689 of the 5690 call
//   sites have one, the exception being a pushvar in the dev package test02.ebp.
//
//   A package's place name is at atel+0x1E. With the high bit set the low 15 bits
//   are a count and atel+0x28 points at that many u16 ids, one per ground region.
//   347 packages are that form with a count of 16, and the other 50 hold 0, which
//   means no place. 333 of the 347 spell the same place all 16 times, and all 347
//   have one place as a strict majority, so a package has one place for this purpose.
//
//   The names are macrodic.dcp section 5, 349 rows, which is also what the engine
//   reads through FFX_SaveData_MapNameIdToString.
//
// WHAT IT WILL NOT DO IS GUESS. An NPC placed in sixteen places is a crowd model and
// not a Mushroom Rock NPC, so the label lists the areas it is used in rather than
// picking a winner. 37 of the 216 NPCs with place data are used in one area, 54 in
// two, and the long tail goes to fourteen.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// DO NOT RUN THIS AT BOOT. ASK THE PLAYER FIRST.
	//
	// The sweep reads 397 files and about 58 MB through the engine's file layer, and
	// every way of fitting that into a boot tripped the hang watchdog. All measured
	// 2026-10-04 on a cold cache:
	//
	//   synchronous on the game thread          12.9 s of sweep, a 10 s report
	//   worker at below normal priority         Windows keeps I/O priority separate
	//   worker in background I/O mode           slower at 25.5 s, still a report
	//   worker paced 200 ms between packages    133 s of sweep, a 30 s report
	//
	// The last line is the one that settles it: asleep for four fifths of its life and
	// it produced the worst stall of the four. This was never a fight over disk
	// bandwidth. FFX stops stepping by itself while it loads, and anything else
	// touching the archive stretches those loads until a gap crosses ten seconds.
	//
	// So the cheat plugin puts it behind a button and writes the result into the export
	// cache, which makes it once per archive rather than once per boot. See
	// RequestPlaceSweep in the plugin's ui/CheatPanel.h for the shape to copy.
	// ---------------------------------------------------------------------------

	// Kicks the sweep off on a worker thread and returns at once. Call it as many
	// times as you like, only the first one starts anything.
	//
	// ffx::ReadDataFile is documented safe from any thread and really is, so the thread
	// is sound. It also waits for the engine's file system to come up, so calling this
	// early works. Early is still the wrong time, for the reasons above.
	void StartEventPlaces();

	// True once the sweep has finished, whether it worked or not. Poll it on a step and
	// act when it flips. ModelPlaceLabel returns null until this is true.
	bool EventPlacesReady();

	// The synchronous version, which is what the worker thread calls. Returns the
	// number of models it found a place for, so 0 means nothing worked.
	//
	// ON THE GAME THREAD THIS FREEZES THE GAME FOR OVER TEN SECONDS. It is public
	// because a tool or a test wants it, not because a mod should call it. A second
	// caller does not repeat the work and does not block, it gets 0 until the first one
	// has finished.
	int LoadEventPlaces();

	bool EventPlacesLoaded();

	// Where this model is used, the area it is placed in most often first, or null
	// when no package places it. The full place name when there is only one, so a
	// model used in exactly one spot reads "Luca - Cafe" rather than "Luca".
	//
	// Points into this module's own storage, which lives as long as the process.
	const char* ModelPlaceLabel(int chrId);

	// The place name table on its own, 349 rows out of macrodic.dcp section 5. Row 0
	// is "None". Null for a row that is not there.
	int EventPlaceCount();
	const char* EventPlaceName(int row);

	void LogEventPlaces();

} // namespace ffx
