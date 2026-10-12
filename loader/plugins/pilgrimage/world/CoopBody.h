#pragma once

#include <windows.h>

// A JOINING PLAYER'S BODY: one spawned character per peer, on both machines.
//
// ## What this is for
//
// FFX has one player. Everything that means "the player" in the engine resolves
// through a single pointer, g_ffxControlledChr, and the party members walking
// behind you are not controllable bodies, they are followers with no input of their
// own. So a second person joining has nothing to be.
//
// This gives them something to be: a character spawned out of the CHR pool, of
// whatever character they picked in the panel, standing next to the party. It is
// not a party member. It has no battle slot, no sphere grid and no gear, and that
// is on purpose, because the point is to have a body that can walk around the world
// and interact with it rather than to restructure the party system.
//
// ## How two machines agree on it
//
// The spawn is an ORDERED COMMAND, kCommandSpawnBody, carrying the peer, the
// character id and the exact position. Both machines run it on the same step with
// the same numbers, so both end up with the same body in the same place.
//
// What they do NOT agree on is the CHR pool slot it landed in, and they do not have
// to. A body's identity across machines is "peer N's body", held in the clone
// roster's peer table, and each machine resolves that to its own local pointer. See
// BindPeerBody in clones/CloneRoster.h.
//
// ## The camera
//
// Nothing here points a camera at anything. It does not need to: FFX_PlayerCam_Step
// at RVA 0x0043F2E0 reads g_ffxControlledChr every step through FFX_Player__getPos
// and has no follow target of its own, so the camera goes wherever the player
// binding goes. PlayerDrive already moves that binding to whatever body the local
// peer owns, so a peer whose body is a spawned character gets the camera with it,
// with no camera code at all.
//
// THE ONE CAVEAT, and it is measured rather than assumed: the follow camera is a
// FALLBACK. FFX_PlayerCam_Publish opens a Came handle with class 0, which
// FFX_Came_OpenHandle rejects, so its output only reaches the camera slot when no
// map, event, battle or viewer handle is open on screen 0. While a map script holds
// a camera, that script decides the view and the binding does not. LogCoopBodies
// prints which camera is actually driving, so a session says whether this is
// happening rather than leaving it a theory.

namespace pilgrimage
{

	// Call once at plugin start.
	void ResetCoopBodies();

	// Call once per simulation step from the lockstep layer, inside the session.
	// Drains kCommandSpawnBody for this step, and asks for a body when this machine
	// needs one and has not got one.
	void StepCoopBodies();

	// Forget everything. Does NOT despawn: a dropped session is not a reason to
	// delete a character out of the pool, and the next PruneDeadClones gets it.
	void StopCoopBodies();

	// One line for the panel.
	const char* CoopBodySummary();

	// Everything, for ctrl+F11.
	void LogCoopBodies();

} // namespace pilgrimage
