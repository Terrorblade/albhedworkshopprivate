#pragma once

#include <windows.h>

// Can the mod put its own rows in the game's own settings menu? One keypress to
// find out.
//
// The Esc menu research established everything on the native side. Each page is
// built by a native function that calls an ActionScript function inside
// escmenu.swf once per row, with the row index as the first argument. Nothing in
// native code holds a maximum. No native code reads a row count back, and it
// cannot, because none of the 41 imported Iggy entry points returns an array
// length. So the native half is count-blind and the indices are whatever we say.
//
// What that does NOT prove is the one thing that decides the whole approach:
// whether escmenu.swf's addVideoPageItem APPENDS a row or fills one of a fixed
// set of pre-placed row instances. That is one line of ActionScript inside an
// Iggy-compiled asset and nobody has read it.
//
// The evidence says append. The Video page emits 11 or 12 rows depending on
// whether the display adapter reported a mode list, and in the 11-row case it
// passes indices 1 to 11 and skips 0, so the movie tolerates a gap. The pages
// range from 3 rows to 23 and all of them scroll. That is good evidence and it is
// still evidence rather than proof.
//
// So: press the key, open the Esc menu, go to Video, scroll to the bottom.
//
// A 13th row saying "Pilgrimage Together" means the plan works and the mod's
// settings can live in the game's own menu. No 13th row means fall back to the
// Special Feature page, which is a plain text list and the least likely of all of
// them to have a fixed slot count, and failing that keep the Win32 window.
//
// Game thread only. It calls into Iggy.

namespace pilgrimage
{

	// Arms the experiment. The row cannot be added now, because the page is built
	// once when the menu singleton is constructed, so this sets a flag and the next
	// page build adds the row. Logs what it is waiting for.
	void ArmEscMenuRowTest();

	// Called once per frame from the frame hook. Watches for the Video page having
	// been built and adds the extra row the first time it sees it. Cheap, and returns
	// immediately when the test is not armed.
	void ServiceEscMenuRowTest();

	// Adds the row right now, for the case where the menu has already been opened
	// once and the singleton exists. Reports what happened.
	void AddEscMenuRowNow();

	// A line for the control panel.
	const char* EscMenuRowTestStatus();

} // namespace pilgrimage
