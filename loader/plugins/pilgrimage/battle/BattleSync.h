#pragma once

// A second player choosing and committing battle actions.
//
// ONE MENU, REPLICATED RECORDS. That is the whole design, and it is forced on us
// rather than chosen. The battle command menu's staging record is a single 72 byte
// global that is rebuilt from the menu page stack EVERY FRAME, and the page stack,
// the page depth, the menu owner and the target cursor are all one slot as well. Two
// cursors on that is not a race that shows up occasionally, it is one player's
// half-built command being stamped with the other player's unit, every frame, by
// design. There is no second copy to fall back on. See the single-instance list in
// workshop/include/ffx/addresses/Battle.h.
//
// So nothing here tries to run two menus. Whoever owns the acting unit drives the
// SHIPPED menu on their own machine, with their own pad and their own cursor. At the
// instant it would commit, the 72 bytes are captured, the local commit is dropped,
// and the record goes out on the ordered command channel. Every machine then calls
// FFX_Btl_CommitCommand with those bytes on the step the host picked. The machine
// that does not own the unit never opens a menu for it at all.
//
// WHY THAT WORKS AT ALL, which is the one fact the whole feature rests on: the
// commit gates are the queue's 62 slot capacity and variant <= 1. There is no CTB
// readiness test, no MP test, no status test and no "this unit already acted" flag.
// A record replayed six steps later is accepted exactly as the menu's own would have
// been. The legality checks live earlier, in the menu that is not running on the
// other machine, and later in the executor, which both machines reach with the same
// state because that is what lockstep is for.
//
// WHAT THE OTHER MACHINE DOES WHILE IT WAITS. Nothing, and that falls out of the
// engine rather than being arranged. FFX_Btl_SendMenu claims the turn queue entry
// before it decides between the menu and the AI, and the top of its loop returns the
// moment the head is already claimed. So a suppressed menu leaves a claimed turn
// sitting there and the engine's own early return holds the battle. Both machines
// are in that state anyway, because the owner is also sitting on a claimed turn from
// the moment it confirmed until the ordered copy comes back.
//
// And the CTB clock is frozen on both of them for the whole of it. FFX_Btl_CtbTick
// returns immediately while the turn queue count is non zero, so no second turn can
// be handed out while one is outstanding. Nothing in the menu module writes either
// queue count or any of the flags FFX_Btl_IsCtbTickAllowed reads, so "a menu is open
// here" is invisible to the simulation. That is the fact the whole suppression rests
// on, and it was checked in the disassembly rather than inferred.
//
// WHY THE RECORD AND NOT THE KEYPRESSES. The same reason BoosterSync sends values
// and not presses, only more so. Replaying the pad would mean replaying it into a
// menu whose page stack, cursor position, candidate target list and staged record
// are all single slot and all driven by the local player at the same time. The
// record is the output of all of that, and it is the only thing both machines need
// to agree on.
//
// WHAT THIS DELIBERATELY DOES NOT TOUCH. The AI script runner, and the three forced
// action paths for confuse, berserk and provoke. All four commit through the same
// FFX_Btl_CommitCommand, and all four are the engine deciding for itself from state
// that is already in step, so both machines reach the same decision on the same step
// on their own. Replicating them would commit each of those actions twice. That is
// the reason the capture is on the five player call sites and not on CommitCommand
// itself, which has ten callers.

namespace pilgrimage
{

	// Installs the capture and suppression hooks. Non-fatal: without it the battle
	// behaves exactly as the shipped game does, which is right for one player and a
	// divergence for two. Install once at startup and leave it in, like every other
	// hook here, because the callbacks do nothing until a session starts.
	bool InstallBattleSync();
	bool BattleSyncInstalled();

	// Call when a session becomes active, and Stop when it goes away. Until Start,
	// every hook passes straight through and the game is the shipped game.
	void StartBattleSync();
	void StopBattleSync();
	bool BattleSyncActive();

	// Once per simulation step. Applies any battle command the host ordered for this
	// step, on every machine including the one that chose it.
	void StepBattleSync();

	const char* BattleSyncStatus();
	void LogBattleSync();

} // namespace pilgrimage
