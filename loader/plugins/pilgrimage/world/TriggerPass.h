#pragma once

#include <windows.h>

// Making a second player's movement trigger the world the way the first player's does.
//
// FFX runs its whole trigger set once per simulation step, against one player. The three
// steppers (proximity, line, box) already take the player actor as a parameter, so they can
// be called for somebody who is not the bound player without patching anything. What they
// cannot do unaided is remember a separate inside-or-outside state per player, because that
// state is two bit fields on the ACTOR, not on the player. See ffx/Atel.h for the masks.
//
// So this runs one extra pass per remote player, and gives each of them its own shadow copy
// of those bits. The result is that a remote player walking into a trigger fires enter, and
// walking out fires leave, with the host's own edges completely unaffected.
//
// Why this needs no network command of its own: under lockstep both machines simulate the
// same inputs and therefore hold the same positions, so both run an identical pass and both
// fire the same events on the same step. Interaction replicates for free. The command
// channel is for things that are not derivable from input, like a join.

namespace pilgrimage
{

	// One per active battle slot, which is the mod's player cap. Slot 0 is the host's own
	// character and is NOT passed over, because the engine's own pass already is slot 0.
	const int MaxTriggerPlayers = 3;

	// How many actors a shadow table covers. A field map ran about 36 actors, so this is a wide
	// margin, and the pass refuses rather than truncates if a map ever exceeds it.
	const int MaxShadowActors = 512;

	void ResetTriggerPass();

	// Tell the pass where a player is this step. The previous position is remembered from the
	// last call, which is what lets a swept line crossing be detected rather than missed.
	// Call once per step per remote player, before RunRemoteTriggerPass.
	void SetTriggerPlayer(int slot, const float* pos, float facing);

	void ClearTriggerPlayer(int slot);

	// Ask for an examine on this player's behalf on this step, which is that player's confirm
	// button arriving through the replicated input. Cleared by the pass once consumed.
	void RequestTriggerExamine(int slot, int eventKind);

	// Run the extra passes. Call once per simulation step, after the engine's own step, so the
	// scripts this starts are picked up by the next step exactly as the engine's own would be.
	void RunRemoteTriggerPass(int dtMs);

	// Whether the pass actually fires the events it finds. Off means it computes and logs the
	// winner without committing, which is the safe way to see what it would do.
	void SetTriggerPassArmed(bool armed);
	bool TriggerPassArmed();

	// Drive slot 1 from the active clone's position every step. This is the solo test harness:
	// it needs no network, so the whole pass can be proved with one spawned character.
	void SetTriggerPassFollowClone(bool follow);
	bool TriggerPassFollowClone();

	const char* TriggerPassStatus();
	void LogTriggerPass();

} // namespace pilgrimage
