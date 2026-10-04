#pragma once

#include <windows.h>

// THE LISTS, ON DISK, SO A BOOT NEVER ASKS THE ENGINE FOR THEM.
//
// Twelve of the kit's lists are fixed by the game's archive: every map and event,
// every fight, every model, and the five kernel name tables. Building them by
// walking the engine cost two things worth getting rid of:
//
//   - Time. The event list alone made the engine open and close 402 files through
//     FFX_Asset_GetSizeForIndex to find out which packages ship.
//   - Stability. Those 402 open and close pairs run outside the engine's own flow
//     with the global asset kind swapped out, and the crash that keeps turning up
//     in FFX_Free on a corrupt block header has no frames of ours on the stack,
//     which is what delayed heap corruption looks like.
//
// So they are exported once and read back after that. The save derived lists, the
// characters, party, aeons, equipment and inventory, are NOT cached: they change
// as the player plays and they are small.
//
// ## What makes a cache stale
//
// A VBF stores an MD5 of its whole header, every name, size and offset in the
// archive, in its last sixteen bytes. FFX_Data.vbf is 20.7 GB and that key costs
// one seek to read, so it is checked on every boot. Add a mod that changes the
// archive and every cached list rebuilds by itself.
//
// Options that change what goes IN a list are part of the key too, because the
// archive has not changed but the content has. Right now that is the battle
// list's monster names.
//
//     ffx::LoadExportedLists();      // on a step, before building anything
//     ffx::WriteExportedLists();     // after a build, when the load missed
//
// Files land in <game dir>\AlBhedWorkshop\data. They are tab separated text and
// they are meant to be read, diffed and committed.

namespace ffx
{

	// Fills every cacheable list from disk. False when there is no usable cache, and
	// then the caller builds from the engine as before. Never half fills a list.
	bool LoadExportedLists();

	// Writes them all out. Call it after a successful engine build.
	//
	// Skips a list that is empty or not live, because writing an empty file would
	// cache a failure and the next boot would trust it.
	bool WriteExportedLists();

	// True when the files on disk match this archive and these options. Cheap, it
	// reads a manifest and sixteen bytes of the archive.
	bool ExportedListsFresh();

	// For a panel or a log line: where the cache lives and what it holds.
	const char* ExportedListsStatus();

	// Deletes the cache so the next build writes a new one. For the panel's
	// "rebuild from the game" button.
	void DiscardExportedLists();

} // namespace ffx
