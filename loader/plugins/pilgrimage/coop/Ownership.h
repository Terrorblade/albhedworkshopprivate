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
// THE EXPLICIT BINDINGS ARE THE HOST'S, and rule 1 is the only part of this file that
// needs a message at all. Rules 2 and 3 are derived, so they cannot disagree. Rule 1 is
// a CHOICE somebody made in a menu, so it can.
//
// THERE IS EXACTLY ONE WRITER, and everything else follows from that:
//
//   ONLY THE HOST MAY CHANGE A BINDING. A client's Config row refuses the press with a
//   log line, see menu/CoopConfig.cpp. The row stays selectable on both machines
//   because the selectable flag drives replicated navigation, so it is the change that
//   is refused and not the cursor.
//
//   A change is still an ORDERED COMMAND. The host asks with kCommandCharOwner, stamps
//   a step, and every machine applies it on that step or none does. Writing the binding
//   locally even on the host would put the host a few steps ahead of everybody else,
//   which is the split-brain failure above for as long as it lasted.
//
//   The host KEEPS its table when a session starts and a client CLEARS. See
//   ResetOwnership. That is what lets a host set up who plays whom before anybody has
//   joined, which is the normal way this gets used.
//
//   The host PUBLISHES THE WHOLE TABLE as kCommandCharOwnerTable ONCE PER JOINER, the
//   moment it sees that peer's clock running. That is needed because workshop::Lockstep
//   only becomes a message sink in Start(), so a joiner is not listening to the command
//   channel until its own clock runs and every binding ordered before that reached it as
//   nothing. InstallOwnerTable is the receiving end.
//
// Nothing is resent. With one writer no copy can drift, so there is nothing to heal.

namespace pilgrimage
{

	// What the lookups return when they cannot answer at all. That happens with no
	// session, or before the host has assigned this machine a peer id.
	const int kNoOwner = -1;

	// Forget every explicit binding and go back to the default.
	//
	// CALLED BY A CLIENT AT SESSION START, NOT BY THE HOST. The host's table is the
	// authority for the session and is published to everybody else, so wiping it on the
	// host would throw away the one thing a host sets up before anybody joins. A client
	// clears because it is about to be told, and because a second session must not
	// inherit the first one's bindings.
	void ResetOwnership();

	// Replace the WHOLE table at once, from the host's published copy.
	//
	// NOT a loop over SetCharacterOwner, and it must not become one. That setter is
	// exclusive: it takes a character off whoever else held it. So replaying three
	// entries through it would have each entry strip the ones before it and the result
	// would depend on the order they were applied in, which is the opposite of what an
	// absolute state is for.
	//
	// charIndexPerPeer is one entry per peer, -1 meaning no binding. An entry out of
	// range is taken as no binding rather than refusing the whole table, because a
	// table from a build with different rules must not leave this machine with no table
	// at all. A duplicate character is resolved by letting the lower peer keep it,
	// which is an answer every machine reaches identically.
	//
	// Returns true when the table actually changed.
	bool InstallOwnerTable(const int* charIndexPerPeer, int count);

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
