#pragma once

#include <windows.h>

// The reason codes RequestWorldFromHost takes are wire values, so they live in the
// protocol rather than here. Pulled in so a caller gets the function and its argument
// names from one include.
#include "workshop/Protocol.h"

// Getting a joining player into the host's world.
//
// A client that connects has its own save state, which is almost certainly a different
// game: a different party, a different inventory, a different 8,192 bytes of story flags.
// Under lockstep that is not a cosmetic difference, it means the two machines are
// simulating different worlds and nothing after that point is meaningful. So the very
// first thing a join has to do, before any input is exchanged, is make the two worlds
// identical.
//
// The good news, from reversing\WORLD_STATE.md: the save block is 26,816 bytes of pure
// data with no pointers, the game transplants it with a plain memcpy in four separate
// places including one script opcode whose whole body is the memcpy, and everything
// derived from it is rebuilt by a single call. So this is a file transfer followed by a
// memcpy, not a serialisation problem.
//
// THE SEQUENCE
//
//   client                          host
//   ------                          ----
//   WorldRequest(joining)    ->
//                            <-     WorldSnapshot chunk 0 of 27   (reliable, in order)
//                            <-     ... chunk 26
//   install, resume to the
//   host's map, hash it
//   WorldApplied(hash)       ->
//                                   compare hashes, log agreement or divergence
//
// WHAT THIS DOES NOT DO. The save block identifies a DOORWAY, a (map id, entry point)
// pair, not a position. So the client arrives at the last door the host walked through,
// not standing next to the host. Closing that gap is the character layer's job.

namespace pilgrimage
{

	// Registers the message sink. Call once when a session becomes active, and again with
	// Stop when it goes away.
	void StartWorldSync();
	void StopWorldSync();
	bool WorldSyncActive();

	// "Is it safe to start simulating alongside the other end." The host is ready as soon as
	// it is running, because its world is the reference. A client is ready only once a
	// snapshot has actually been installed.
	//
	// This exists so the lockstep clock can be held back. Starting the clock before the
	// world arrives would mean the two machines exchange input for a few frames while
	// simulating different games, and the transplant then lands on top of whatever that
	// produced. Exchanging input is only meaningful once both ends agree what world the
	// input is being applied to.
	bool WorldSyncReady();

	// The host's lockstep step number at the instant it snapshotted the world, as
	// received and installed by this client. False when there is nothing valid, which
	// is the case on a host and on a client that has not installed a snapshot.
	//
	// THIS IS WHAT A CLIENT MUST START ITS LOCKSTEP CLOCK AT. The step number is not a
	// local counter, it is a shared label, and every ordered command stamps one machine
	// and is matched on the other. The host holds its own simulation from the moment it
	// takes the snapshot until the APPLIED reply comes back, so the number it sent is
	// still the step it is about to run when the client gets here. Seeding from this
	// machine's own g_ffxMainStepCounter instead, which is what used to happen, left the
	// two clocks offset by an arbitrary amount for the life of the session.
	bool AppliedHostStep(uint32_t* out);

	// Client side. Ask the host for its world. Harmless to call when already waiting, which
	// is why a stalled transfer can be retried by just asking again.
	bool RequestWorldFromHost(int reason);

	// Host side. Push the world at one peer, or at everybody. Returns how many peers were
	// started, so zero means nobody was reachable rather than nothing was wrong.
	int SendWorldToPeer(int peer);
	int SendWorldToAll();

	// Called once per frame from the frame hook. The host uses it to pace chunks out rather
	// than putting 27 KB on the wire in one frame, and the client uses it to apply a
	// completed transfer at a safe point rather than from inside the receive path.
	void ServiceWorldSync();

	const char* WorldSyncStatus();
	void LogWorldSync();

} // namespace pilgrimage
