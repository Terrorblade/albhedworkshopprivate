#pragma once

#include <windows.h>

namespace ffx
{
	struct Character;
}

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
// and reallocated on every map transition.
//
// WHAT MOVED OUT OF HERE. This file used to do the movement drive as well, turning a
// replicated heading into a speed and a direction through clones/CloneDriver.h. It no
// longer does, because that was the wrong way to move a player: it was a second
// implementation of the engine's own driver, so the character this machine owned and the
// same character on the other machine were computed by two different pieces of code and
// drifted apart by construction. world/PlayerDrive.cpp now runs every player character,
// this machine's included, through FFX_Player__stepControl itself.
//
// What is left here is the bookkeeping that the drive used to carry along with it: where
// each remote body ended up, so the trigger pass can fire the world for it, and the
// examine press edge. Both still belong on the frame path, because both are about the
// position AFTER the engine integrated motion.
//
// The old drive is still in the file as a fallback, and it runs only when PlayerDrive
// could not patch its call site. A character moving slightly wrong is better than a
// character not moving.

namespace pilgrimage
{

	void ResetRemotePlayers();

	// Stop driving every remote body and let go of their trigger slots, without
	// tearing the session down. Called when a BATTLE starts, because the field relay
	// has nothing to say in a battle and a stale trigger slot would keep describing a
	// character standing somewhere that no longer exists.
	//
	// Separate from ResetRemotePlayers because this one also clears the trigger slots.
	// A reset only forgets the per-peer edge and coast state.
	void ParkRemotePlayers();

	// ---------------------------------------------------------------------------
	// THIS IS NOT HOW A REMOTE PLAYER GETS A BODY. READ THIS BEFORE USING IT.
	//
	// A remote player's body is the PARTY CHARACTER that peer owns, resolved through
	// coop/Ownership.h and ffx::ChrForPartyCharacter. world/PlayerDrive.cpp does that
	// for every peer, the local one included, because the ownership table is the only
	// answer both machines can compute identically. clones/ is the solo test harness
	// that proved a second body could be driven at all, and it is not the shipped
	// shape.
	//
	// This function binds a peer to a CLONE ROSTER ENTRY. Nothing calls it. It is the
	// survivor of the refactor described above, which moved the drive out of this file,
	// and it is kept only because the clone harness still exists behind F9.
	//
	// Its old comment claimed "peer 0 is rejected: the host's own character is the
	// bound player and the engine drives it", which was wrong twice over. On a CLIENT
	// peer 0 is the host and is remote. And the function rejects the LOCAL peer, not
	// peer 0. RemotePlayers.cpp already fixed that for the trigger slots and this
	// header was not updated, which is the sort of thing that costs an afternoon.
	//
	// Pass -1 as the entry to unbind.
	// ---------------------------------------------------------------------------
	// BindRemotePlayer used to live here and is gone. Binding a peer to a body is
	// BindPeerBody in clones/CloneRoster.h now, because three places have to agree
	// about whose body is which and the table belongs where all three can see it.

	LONG RemotePlayerEntry(int peer);

	// The live character bound to a peer, or null when it has no body or the pool has
	// been reallocated out from under its roster entry.
	//
	// Exposed for world/PlayerDrive.cpp, which needs the body but has no business
	// knowing about roster entries or pool slots. Returns null for the local peer, which
	// is not bound here at all: PlayerDrive reads that one from the engine's own
	// g_ffxControlledChr.
	ffx::Character* RemotePlayerCharacter(int peer);

	// Called once per simulation step from the step hook, before the trigger pass. Reads
	// the clock's input for the current step and applies it.
	void StepRemotePlayers();

	// How many peers currently have a body. Zero means this is running solo, which is the
	// normal state until somebody joins.
	int RemotePlayerCount();

	const char* RemotePlayerStatus();
	void LogRemotePlayers();

} // namespace pilgrimage
