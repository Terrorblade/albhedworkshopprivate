#pragma once

#include "workshop/SteamAbi.h"

// Who the local player is, and who they could play with.
//
// This exists to solve one concrete problem. Steam P2P is addressed by SteamID,
// so a client has to know the host's before it can send anything. Without this,
// the host has to go and read their own SteamID off their Steam profile page and
// pass it on by hand, which works but is a poor first impression.
//
// Everything here is read-only and safe to call every frame, though there is no
// reason to: an identity does not change while the game is running. Nothing here
// writes to Steam, and SetPersonaName is deliberately not exposed.
//
// ## Call LoggedOn first
//
// Steam's lifetime belongs to the game, which calls SteamAPI_Init on its own
// schedule. Before that happens these return nothing useful, and that is a
// normal early-startup state rather than an error. Every function here fails
// safe: zero, false, or an empty string, never a garbage SteamID that a player
// might paste to a friend.
//
// ## String lifetimes
//
// Steam's own string returns are documented as being freed or reallocated later,
// so none of them escape this header. Every name comes back copied into a buffer
// the caller owns.

namespace workshop
{

	// True once Steam is up and the local identity is real. Everything else here
	// depends on this, so it is the one to check before showing any of it.
	bool SteamLoggedOn();

	// The local player's SteamID, or 0 when Steam is not ready. This is the 17 digit
	// number a host gives their friends.
	uint64 LocalSteamId();

	// The same thing as text, which is the form a player copies and pastes. Writes
	// an empty string when Steam is not ready. Returns out.
	const char* LocalSteamIdText(char* out, int count);

	// The local player's Steam display name, copied into out. Empty when Steam is
	// not ready. Returns out.
	const char* LocalPersonaName(char* out, int count);

	// ---------------------------------------------------------------------------
	// The friend list, for offering a picker instead of a pasted number.
	//
	// Worth knowing before building that UI: a SteamID alone does not make someone
	// reachable. A friend who is not running the game cannot be connected to, so a
	// picker that lists everyone will mostly list people who will not answer.
	// Filtering by who is in game needs GetFriendGamePlayed, which is NOT verified
	// in SteamAbi.h and must not be called until it is.
	// ---------------------------------------------------------------------------

	// How many regular friends Steam knows about, or 0 when Steam is not ready or
	// the call fails. A negative result from Steam is an error, not a count, and is
	// reported here as 0.
	int SteamFriendCount();

	// The friend at index, which must be below SteamFriendCount(). Writes the id and
	// copies the name. Returns false when Steam is not ready or the index is out of
	// range, in which case nothing is written.
	//
	// Pass indices from a single pass over a stable count. The list can change
	// between calls, so re-reading the count before a pass is the safe habit.
	bool SteamFriendAt(int index, uint64* outId, char* outName, int nameCount);

} // namespace workshop
