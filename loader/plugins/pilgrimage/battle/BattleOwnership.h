#pragma once

// Who is allowed to choose a battle action for which unit.
//
// This is the smallest table in the mod and the one with the most consequences, so
// it gets its own file. One wrong answer here is not a cosmetic glitch: if both
// machines think they own a unit, both open a menu and both commit, and the action
// happens twice. If neither thinks it owns a unit, nobody opens a menu, nobody
// commits, and the battle hangs with a claimed turn that nothing will ever retire.
// There is no third outcome, so the rule has to be a function that cannot disagree
// with itself across two machines.
//
// THE RULE, as it stands today: peer N owns active battle slot N.
//
// Slot 0 is the host, slot 1 is the first client, slot 2 is the second. The slot is
// read from the battle party roster, so it is whatever the game itself thinks the
// three active members are rather than anything the mod decides. Three slots, three
// players, which is the cap this feature has whatever MaxPlayers says.
//
// WHY IT IS DERIVED AND NOT AGREED. The mapping is a pure function of (local peer
// id, battle party roster). Both of those are already identical on both machines:
// the peer id is assigned once by the host in the welcome, and the roster is part of
// the state lockstep keeps in step. So the two machines reach the same answer
// without a message, and there is no window in which they disagree because the
// answer was in flight. That is worth more than it sounds, because the one thing
// that cannot be fixed later is two machines acting on different ownership.
//
// THE EXPLICIT BINDING IS LIVE, and this note used to say it had no caller. It does.
//
// The table itself moved to coop/Ownership.h, because "who drives this character" is
// not a battle question: the sphere grid and the equipment screen ask the same thing,
// and two tables meant to agree are a bug waiting for a quiet afternoon. So the co-op
// Config rows the player sets (menu/CoopConfig.cpp, through SetCharacterOwner) are the
// SAME binding this file's rule 1 reads. Assign a character to a peer there and that
// peer's machine is the only one that opens a battle menu for them.
//
// WHAT IS STILL OUTSTANDING is the ordering. A binding that is set on one machine and
// not the other is the worst failure this subsystem has, and today the Config path sets
// it locally rather than through an ordered command. The cost is bounded: the two
// machines can disagree for the turns between one player changing the setting and the
// other doing the same, and in that window either both suppress the menu or neither
// does. Neither is a divergence in simulated state, but both are confusing to play.
// The fix is a command kind of its own, applied on the same step on both machines, not
// a cleverer predicate.
//
// ONE MORE THING THE DEFAULT HAS TO HANDLE. With two players there are three active
// slots, so somebody has to drive slot 2, and anything that is not an active party
// member at all (an aeon, a unit the debug Mon Input flag hands a menu to) needs an
// owner as well. Those all fall to the host. That is not an opinion about who should
// control them, it is the only answer that is the same on both machines without
// another message.

namespace pilgrimage
{

	// Three, because there are three active battle slots. Not MaxPlayers.
	const int kBattleOwnerSlots = 3;

	// What BattleOwnerOfUnit returns when it cannot answer at all, which only happens
	// with no session or before the host has assigned us a peer id.
	const int kNoBattleOwner = -1;

	// Forgets every explicit binding and goes back to "peer N owns slot N". Called
	// when a session starts, so a second session does not inherit the first one's
	// choices.
	void ResetBattleOwnership();

	// THE SEAM. Binds a peer to a character index explicitly, beating the default.
	// Pass a character index of -1 to clear that peer's binding.
	//
	// Read the note at the top of this file before calling it: a binding that is set
	// on one machine and not the other is the worst failure this subsystem has, so
	// this wants to be driven from an ORDERED command and not from a local keypress.
	// Returns false for a peer or character index out of range.
	bool SetBattleOwner(int peer, int charIndex);

	// The character this peer has been bound to explicitly, or -1 when it is on the
	// default. Not "the character this peer is driving": for that, ask
	// BattleOwnerOfUnit, because the default answer depends on the live roster.
	int BattleOwnerCharacter(int peer);

	// The peer who may choose actions for this unit, or kNoBattleOwner.
	//
	// unitIndex is a BATTLE UNIT index, which for the seven playable characters is
	// the same number as the save block character index. See ffx/Battle.h for why
	// those three numbers are kept apart.
	int BattleOwnerOfUnit(int unitIndex);

	// Whether this machine is the one that drives this unit's menu. The question
	// every hook in BattleSync actually asks.
	//
	// True when there is no session at all, because a solo game has to behave exactly
	// as the shipped game does and every unit is the local player's.
	bool LocalOwnsUnit(int unitIndex);

	// Which unit this machine owns, or -1. For the status line and the log, where
	// "you are driving Wakka" is more use than a table.
	int LocalOwnedUnit();

	const char* BattleOwnershipStatus();
	void LogBattleOwnership();

} // namespace pilgrimage
