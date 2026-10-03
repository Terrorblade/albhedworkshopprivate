#pragma once

#include <windows.h>

// Can a character other than the bound player fire a world trigger? One keypress
// to find out.
//
// This is the experiment the whole "the client opens a chest" feature rests on.
// The ATEL research says the mechanism is one call,
// ffx::FireActorEvent(actorId, kAtelEventExamine), and that nothing about it
// needs the bound player flipped or any byte patched, because the engine's own
// deliberate fire entry point already takes the actor id as a parameter.
//
// That is read out of the disassembly and it is a strong reading. It is still a
// reading. The failure modes it cannot rule out are all runtime ones: a script
// started from a frame hook rather than from the trigger stepper might see a
// half-built world, the chest's script might depend on cursor state the examine
// arbitration would have set, or the actor ids a mod finds by position might not
// be the ones a chest actually uses.
//
// So: stand next to a chest, press the key, and look.
//
// ## What each key does
//
// LookAtInteractables   lists every ATEL actor near a position with its kind, its
//                       distance, whether an examine event would do anything, and
//                       why not when it would not. Costs nothing and changes
//                       nothing, so it is the one to press first.
//
// FireNearestExamine    fires the examine event on the nearest actor that passes
//                       all three engine gates. This is the real test.
//
// Both read the ACTIVE CLONE's position when there is one, falling back to the
// player's. That is the point: firing from the clone's position is the second
// character case, and if the chest opens, the feature works.
//
// Game thread only. FireNearestExamine starts a script, so it has to be.

namespace pilgrimage
{

	// Lists what is interactable near the probe position. Read only.
	void LookAtInteractables();

	// Fires the examine event on the nearest actor that would accept it. Logs what it
	// chose and what the engine returned, and says plainly when nothing was eligible.
	void FireNearestExamine();

	// A line for the control panel: what the probe last did and what came back.
	const char* InteractProbeStatus();

} // namespace pilgrimage
