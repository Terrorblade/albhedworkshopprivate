#pragma once

#include <windows.h>

#include "ffx/Api.h"
#include "workshop/Session.h"
#include "workshop/Session.h"

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
	// for MaxClones entries). Also unbinds any peer whose body went with it, so a
	// peer never points at an entry that has been handed to somebody else.
	LONG PruneDeadClones(ffx::Character** outLive);

	// ---------------------------------------------------------------------------
	// WHOSE BODY IS WHICH, and this is the thing that makes a spawned character a
	// PLAYER rather than scenery.
	//
	// It lives here, in the roster, rather than in whichever module happened to need
	// it first, because three different places have to agree: the drive pass that
	// feeds a body its input, the trigger pass that fires that body's triggers, and
	// the camera, which follows whatever the local peer's body is. The comment in
	// RemotePlayers.cpp that warns "two resolvers would eventually disagree and the
	// symptom would be a character walking in one place and interacting in another"
	// was right, and the way to honour it is one table in one place.
	//
	// A peer with no bound body is the normal case. The host plays the party leader
	// the game already gave it, and only a joining peer gets a spawned body, so peer
	// 0 is usually unbound and that is not a fault.
	//
	// The LOCAL peer can and must be bound. An earlier version of this refused that,
	// on the reasoning that the local player already has a body the engine drives.
	// That is exactly backwards for a joining player: their body IS the spawned one,
	// and the engine drives it because the mod points g_ffxControlledChr at it.
	// ---------------------------------------------------------------------------

	// Point a peer at a roster entry, or at -1 to unbind. Binding a peer that is
	// already bound replaces the binding and says so.
	void BindPeerBody(int peer, LONG rosterEntry);

	// The roster entry bound to a peer, or -1.
	LONG PeerBodyEntry(int peer);

	// The live character bound to a peer, or NULL when that peer has no body, the
	// entry has been released, or the character behind it is dead.
	ffx::Character* PeerBodyCharacter(int peer);

	// Which peer a character is the body of, or -1. The reverse lookup, for the
	// walkmesh and trigger passes that start from a character.
	int PeerOfBody(ffx::Character* chr);

	// How many peers have a live body right now.
	int PeerBodyCount();

	// Forget every binding. The entries themselves are left alone, since dropping a
	// session is not a reason to despawn anything.
	void ClearPeerBodies();

} // namespace pilgrimage
