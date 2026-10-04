#pragma once

// The sphere grid, which is 2560 bytes of the save block and one bit per node per
// character.
//
//     ffx::ActivateAllNodes(ffx::kCharTidus);
//     ffx::RecomputeDerivedStats();          // REQUIRED, see below
//
// Layout, at SaveData+0x21EC:
//
//   +0x0000  1280 node slots of 2 bytes
//              [0] the panel kind, 0xFF when the slot holds no node
//              [1] a bitmask of which of characters 0..6 have activated it
//   +0x0A00  1280 link bytes, a per-character trail mask, cosmetic only
//   +0x0F00  7 WORDs, each character's current node index
//   +0x0F18  BYTE grid id, +0x0F19 BYTE zoom level
//
// Setting an activation bit IS the whole of "activated". Everything a node grants,
// every stat point and every ability, is recomputed from those bits, so an edit here
// does nothing visible until ffx::RecomputeDerivedStats runs. That function is the
// game's own and the game calls it for exactly this reason.
//
// ONLY SLOTS 0..1023 COUNT. The engine's recompute scans 1024 of the 1280, so a bit
// set in the last 256 is read by nothing. ActivateAllNodes respects that.
//
// NOT WHILE THE GRID SCREEN IS UP. Opening it copies the blob into the menu's working
// set and closing it copies back, so the menu wins and an edit made underneath is
// thrown away. Every writer here refuses while menu module 19 is active, and
// SphereGridMenuOpen is how a panel asks before offering the button.
//
// Characters 0..6 only. Seymour is index 7 and the aeons are 8..17, and none of them
// has a grid, so the mask has seven meaningful bits.
//
// Research is in reversing/CHEAT_SAVEDATA.md.

#include <windows.h>

namespace ffx
{

	const int kGridNodeSlots = 1280;
	const int kGridNodeSlotsScanned = 1024; // what RecomputeDerivedStats actually reads
	const int kGridNodeEmpty = 0xFF;
	const int kGridCharacters = 7;

	// False when there is no game, so every call below is safe at the title screen.
	bool SphereGridReadable();

	// True while the grid screen owns the data. Writing is refused in that state.
	bool SphereGridMenuOpen();

	// The panel kind in a slot, or kGridNodeEmpty. Minus one for a bad index or no
	// game, which is distinct from empty on purpose.
	int GridNodeKind(int slot);

	// The activation bitmask for a slot, characters 0..6 in bits 0..6. Minus one on
	// failure.
	int GridNodeMask(int slot);

	bool GridNodeActivated(int slot, BYTE charIndex);
	bool SetGridNodeActivated(int slot, BYTE charIndex, bool on);

	// How many slots hold a node at all, and how many this character has activated.
	// Both count only the scanned range.
	int GridNodeCount();
	int GridActivatedCount(BYTE charIndex);

	// Sets or clears this character's bit on every slot that holds a node. Returns how
	// many slots changed. Call RecomputeDerivedStats afterwards or nothing happens.
	int ActivateAllNodes(BYTE charIndex);
	int ClearAllNodes(BYTE charIndex);

	// Where this character's cursor is sitting, slot index. Minus one on failure. Worth
	// reading rather than writing: moving somebody to a node they have not reached is
	// what the activation bits are for.
	int GridCursorNode(BYTE charIndex);

	// Which grid the save is on, 0 Standard, 1 Original, 2 Expert. Read from bits 14
	// and 15 of the save's option flags, which is what the game itself branches on, not
	// from the blob's own copy at +0x0F18. Minus one on failure.
	int GridId();

	void LogSphereGrid();

} // namespace ffx
