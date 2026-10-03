#pragma once

#include <windows.h>

#include "ffx/Api.h"

// Which clones exist, and which one the input drives.
//
// The roster holds CHR pool INDICES, not pointers, because the pool is freed and
// reallocated on every map transition. An entry of -1 is empty. See Character.h.
//
// Everything here is bookkeeping and touches the game only to resolve a slot to a
// character, so it is safe to read from either thread. Mutating it is the game
// thread's job.

namespace pilgrimage
{

	// How many clones can be live at once. A field map had 36 CHR slots with 9 in use,
	// so 8 is comfortable, and PoolHasFreeSlot is still checked before every
	// allocation because FFX_Ch_Allocate memsets through a null pointer when the pool
	// really is full.
	const LONG MaxClones = 8;

	void ResetRoster();

	// The first empty entry, or -1 when every entry is taken.
	LONG FirstFreeEntry();

	LONG LiveCloneCount();

	// The pool slot held by an entry, or -1 if that entry is empty.
	LONG SlotOfEntry(LONG entry);

	void ClaimEntry(LONG entry, LONG poolSlot);
	void ReleaseEntry(LONG entry);

	// Which entry the input drives, and the character it resolves to.
	LONG ActiveEntry();
	ffx::Character* ActiveClone();
	void SetActiveEntry(LONG entry);

	// Hand the input to the next live clone. Wraps. Does nothing useful with fewer
	// than two clones, and says so.
	void CycleActiveClone();

	// Is this character one of ours? The visibility detour must never touch a
	// character the game owns, and with several clones live a pointer compare against
	// one slot is not enough.
	bool IsManagedClone(ffx::Character* chr);

	// Drops any entry whose character is no longer live, which is what a map
	// transition looks like from here. Returns how many are still live, and writes
	// each live one's character into outLive when that is not null (it must have room
	// for MaxClones entries).
	LONG PruneDeadClones(ffx::Character** outLive);

} // namespace pilgrimage
