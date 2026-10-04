#pragma once

#include <windows.h>

#include "workshop/PickerList.h"

// A PICKER LIST ON DISK, SO THE ENGINE NEVER HAS TO BE ASKED TWICE.
//
// Building these lists by walking the game costs real money. The event list alone
// made the engine open and close 402 files through
// FFX_Asset_GetSizeForIndex, every boot, to answer a question that cannot change
// unless the archive does. That is also the most likely source of the heap
// corruption that turns up later in FFX_Free, because those 402 open/close pairs
// run outside the engine's normal flow with the global asset kind swapped out.
//
// The format is tab separated text with '#' comments and LF endings:
//
//     # list   events
//     # source asset path table, package sizes measured
//     # rows   330
//     0  title         ev/evtitle
//     1  klm0000       klm/klm0000
//
// Human readable on purpose. It greps, it diffs in a pull request, and it parses
// here in about forty lines with one read buffer and no allocation. If something
// later wants JSON or TypeScript for an editor front end, a script can emit it
// from this rather than this file growing a second format.
//
// Nothing here knows what a list means, only how to write one and read one back.
// The decision about WHICH lists are safe to cache, and what makes a cache stale,
// belongs to the game layer. For FFX see ffx/ListExport.h.

namespace workshop
{

	// ONE CACHEABLE LIST. The name becomes the file name, so keep it short and
	// stable: renaming one orphans its file and forces a rebuild of that list.
	//
	// The pointer is mutable because loading writes into the list. That is the only
	// reason a mutable handle to a list exists anywhere in the kit, which is why
	// each owner hands out its own set rather than exposing the lists themselves.
	struct CacheableList
	{
		const char* name;
		PickerList* list;
	};

	// Writes one list. Creates the directory in the path if it is missing.
	//
	// A label containing a tab, CR or LF would break the format, so those bytes are
	// written as spaces. Game names have never contained one, this is just so a
	// surprise degrades a label instead of corrupting the file.
	bool WritePickerList(const wchar_t* path, const char* listName, const PickerList& list);

	// Fills out from the file. Leaves it untouched and returns false if the file is
	// missing or malformed, so a failed load always falls back to the engine.
	//
	// sourceBuf is where the list's "where this came from" string is kept, because
	// PickerList::Reset holds that by pointer rather than copying it. It has to
	// outlive the list, so pass something static.
	bool ReadPickerList(const wchar_t* path, PickerList* out, char* sourceBuf, int sourceBytes);

	// How many rows a file claims, without parsing it. For a log line.
	int PickerListFileRows(const wchar_t* path);

} // namespace workshop
