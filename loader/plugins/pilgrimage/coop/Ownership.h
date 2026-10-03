#pragma once

// Who controls which character. One table, one policy, every subsystem asks here.
//
// This started inside the battle layer, because battle was the first thing that needed
// it. It is here now because it turned out not to be a battle question at all: the
// sphere grid needs the same answer, the equipment screen needs it, and anything later
// that acts on behalf of one character will need it too. Two tables that are supposed
// to agree is a bug waiting for a quiet afternoon, so there is one.
//
// THE RULE, in order:
//
//   1. An explicit binding, if that peer is actually in the session.
//   2. Otherwise the character's slot in the active party, matched to the peer id.
//      Slot 0 is the host, slot 1 the first client, slot 2 the second.
//   3. Otherwise the host.
//
// WHY THE DEFAULT IS DERIVED RATHER THAN AGREED. It is a pure function of (local peer
// id, active party order), and both of those are already identical on both machines:
// the peer id is assigned once by the host in the welcome, and the party order is part
// of the save block that lockstep keeps in step. So two machines reach the same answer
// with no message passing, which means there is no window where they disagree because
// the answer was in flight. That matters more than it sounds. Two machines acting on
// different ownership is the one failure in this subsystem that cannot be patched up
// afterwards.
//
// WHY RULE 3 EXISTS. With two players there are still three active slots, so somebody
// has to own slot 2. The same goes for an aeon, a reserve member, or any character
// nobody has been bound to. Handing those to the host is not a claim about who ought to
// control them, it is the only answer both machines reach without another message.
//
// THE SEAM. SetCharacterOwner is the explicit binding, and it is what "each player
// picks their character" will be built on. Read the warning on it before calling it: a
// binding applied on one machine and not the other is exactly the split-brain failure
// above, so it has to be driven from an ordered command rather than a local keypress.

namespace pilgrimage
{

	// What the lookups return when they cannot answer at all. That happens with no
	// session, or before the host has assigned this machine a peer id.
	const int kNoOwner = -1;

	// Forget every explicit binding and go back to the default. Called when a session
	// starts, so a second session does not inherit the first one's choices.
	void ResetOwnership();

	// THE SEAM. Bind a peer to a character index explicitly, beating the default. Pass
	// -1 as the character to clear that peer's binding.
	//
	// The binding is EXCLUSIVE: taking a character takes it off whoever had it. That is
	// done rather than refused, because asking for a swap is the normal case and a
	// refusal would leave the table half changed.
	//
	// DRIVE THIS FROM AN ORDERED COMMAND, not from a keypress. See the file header.
	// Returns false for a peer or character index out of range.
	bool SetCharacterOwner(int peer, int charIndex);

	// The character this peer is explicitly bound to, or -1 when it is on the default.
	// Note this is NOT "the character this peer controls", because the default answer
	// depends on the live party order. For that, ask OwnerOfCharacter the other way
	// round.
	int OwnerCharacter(int peer);

	// The peer who controls this character, or kNoOwner. charIndex is a save block
	// character index, 0..7.
	int OwnerOfCharacter(int charIndex);

	// Whether this machine controls this character. The question every gate actually
	// asks.
	//
	// True when there is no session at all, because a solo game has to behave exactly
	// as the shipped game does and every character is the local player's.
	bool LocalOwnsCharacter(int charIndex);

	// Which character this machine controls by default, or -1. For a status line, where
	// "you are driving Wakka" beats a table.
	int LocalOwnedCharacter();

	// Is this peer in the session right now.
	//
	// Exposed because more than one caller needs it and the subtlety is worth stating
	// once: Session::PeerReachable is false for ourselves by design, so the local peer
	// has to be added back. Both machines compute this from their own peer table and
	// those agree except during a join or a drop, which is the one window where the two
	// can briefly disagree about who owns slot 2.
	bool PeerInSession(int peer);

	// This machine's peer id, or -1 before the host has assigned one.
	int LocalPeerIndex();

	const char* OwnershipStatus();
	void LogOwnership();

} // namespace pilgrimage
