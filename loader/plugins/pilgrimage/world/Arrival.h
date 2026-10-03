#pragma once

#include <windows.h>

#include "workshop/Protocol.h"

// Landing a joining player next to the host instead of at a doorway.
//
// WorldSync hands over the save block, which makes the two worlds identical in every
// respect but one: the block names a DOORWAY, a (map id, entry point) pair, not a
// position. So a joiner that installs the block and loads arrives at the last door the
// host walked through, which can be most of a map away from where the host is actually
// standing. WorldSync.h says that gap is the character layer's job. This is it.
//
// THE SHAPE OF THE PROBLEM, which is not "send a position".
//
// Under lockstep both machines simulate the same world, so the party characters are the
// SAME characters on both. If the joiner moves them and the host does not, that is a
// divergence in the very state the two machines are about to start agreeing on. There is
// no step at which both could do an identical placement either, because the joiner's map
// is still loading while the host's is not.
//
// What makes it work is that the host is FROZEN for the whole transfer. WorldSync holds
// the simulation on both ends and releases only when the joiner replies APPLIED. So the
// host has not moved since it read the positions, the joiner places its characters
// exactly there, and when the clock starts the two machines genuinely agree. The
// placement is not a sync, it happens once, before there is a shared step to diverge on.
//
// Two consequences of that worth stating, because they drove the design:
//
//  - EVERY CHARACTER, not just the leader. Followers trail the leader by a few metres
//    and those offsets are world state. Sending the leader's position and fanning the
//    others out from it would put the two machines in different places, which is the
//    exact thing this is for.
//
//  - THE APPLIED REPLY HAS TO WAIT for the placement, not just for the transplant. If
//    the joiner confirms as soon as the bytes land, the host unfreezes and walks off
//    while the joiner's map is still loading, and the anchor is stale before it is used.
//    So WorldSync asks ArrivalComplete() before it replies.
//
// WHAT IT CANNOT DO. The simulation has to run for a map to load, because the load is
// consumed inside FFX_MainStep, so the joiner's own hold is dropped the moment the
// transplant is in. Those few free-running frames replay the map entry the host already
// did. That is inherent to joining by loading a map rather than by copying a running
// one, and it is the same bargain WorldSync was already making.

namespace pilgrimage
{

	void StartArrival();
	void StopArrival();

	// ---------------------------------------------------------------------------
	// Host side
	// ---------------------------------------------------------------------------

	// Read where everybody is standing, right now, into an anchor to send.
	//
	// Returns false when there is no world to read, which is the same condition that
	// stops a snapshot being taken, so a caller that already has a snapshot will not see
	// it fail.
	bool CaptureArrivalAnchor(uint32_t snapshotId, uint32_t hostStep,
	    workshop::WorldAnchorPayload* out);

	// ---------------------------------------------------------------------------
	// Client side
	// ---------------------------------------------------------------------------

	// Remember an anchor that arrived. Kept rather than used immediately, because it
	// arrives before the map it describes has loaded.
	void NoteArrivalAnchor(const workshop::WorldAnchorPayload& anchor);

	// "The world is installed and the load has been asked for, now get me there." Called
	// by WorldSync at the end of its install.
	void BeginArrival(uint32_t snapshotId);

	// True while waiting for the map to finish loading so the placement can happen.
	bool ArrivalPending();

	// True once there is nothing left to wait for. That covers four outcomes and the
	// caller does not need to tell them apart: placed, nothing to place, no anchor
	// arrived, or gave up waiting. All four mean the host can safely unfreeze.
	bool ArrivalComplete();

	// Once per frame. Does nothing unless an arrival is pending.
	void ServiceArrival();

	const char* ArrivalStatus();
	void LogArrival();

} // namespace pilgrimage
