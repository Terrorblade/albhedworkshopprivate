#pragma once

#include <windows.h>

// Giving each remote player a body in this world.
//
// This is the join between the lockstep clock and the game. Once per simulation step it
// takes every remote peer's replicated input for that step, drives that peer's character
// with it, and hands the resulting position to the trigger pass so that character fires
// the world the way the bound player does.
//
// The whole thing is deliberately symmetric. Every machine does exactly this for every
// peer including itself, from the same bytes, so every machine ends up with the same
// characters in the same places. Nothing here sends anything and nothing here is
// authoritative. If two machines disagree after this runs, the input they ran differed,
// and that is the desync detector's problem rather than this file's.
//
// The peer to character mapping is by slot, not by pointer, because the CHR pool is freed
// and reallocated on every map transition. Peer 0 is the host and owns the bound player,
// which the engine still drives itself. See the note in RemotePlayers.cpp about why that
// is the remaining hole.

namespace pilgrimage
{

	void ResetRemotePlayers();

	// Bind a peer to a clone roster entry, so that peer's input drives that character.
	// Pass -1 as the entry to unbind. Peer 0 is rejected: the host's own character is the
	// bound player and the engine drives it.
	void BindRemotePlayer(int peer, LONG rosterEntry);

	LONG RemotePlayerEntry(int peer);

	// Called once per simulation step from the step hook, before the trigger pass. Reads
	// the clock's input for the current step and applies it.
	void StepRemotePlayers();

	// How many peers currently have a body. Zero means this is running solo, which is the
	// normal state until somebody joins.
	int RemotePlayerCount();

	const char* RemotePlayerStatus();
	void LogRemotePlayers();

} // namespace pilgrimage
