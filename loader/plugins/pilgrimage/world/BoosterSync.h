#pragma once

#include <windows.h>

// Keeping the HD Remaster booster settings identical on both machines.
//
// This closes a real bug rather than adding a feature. The four booster values are plain
// runtime globals, NOT part of the 26,816 byte save block, so the world transfer does not
// carry them. Two players with different booster settings are two players running
// different rules, and the simulations come apart.
//
// THE WORST ONE, and the reason this is worth doing before anything cosmetic:
// g_boosterEncounterRate is read once, in FFX_Field_StepRandomEncounter. A value of 0
// forces the walked distance to zero and clears the enable flag, and 2 multiplies the
// distance by ten. A successful roll then draws FFX_Rand_Stream(0) and then stream 1. So a
// mismatch does not merely give one player more fights, it makes one machine CONSUME RNG
// THAT THE OTHER DOES NOT, and from that point every random draw in the game is offset
// against the other machine. There is no recovering from that short of a fresh world
// transfer, and the symptom turns up far from the cause.
//
// WHY VALUES AND NOT KEYPRESSES. Replaying the press looked like the clean answer, and it
// is not safe. For Invincible and EncounterRate the engine only commits the new value if a
// toast message can be shown, and that check bottoms out in whether an Iggy movie handle
// has loaded yet. So the commit depends on local UI load state, which means the same press
// on two machines can produce two different outcomes. Auto battle commits unconditionally
// and would be fine, but one path that works and three that do not is not a mechanism.
//
// WHO DECIDES. Either player may press the key, because taking the keys away from one of
// them would be worse than the problem. The change is sent as a REQUEST, the host orders
// it, and both machines apply it on the step the host picked. Between a local press and
// the host's answer the local value is held back to what the host last said, so the
// pressing player sees their change take effect a few steps later rather than instantly.
// That is the right trade: it is a settings change, not a movement input, and nobody
// notices 60 ms on a menu toggle.

namespace pilgrimage
{

	// Call once when a session becomes active, and again with Stop when it goes away.
	// Starting takes the current local values as the baseline, which on the host is what
	// gets pushed out and on a client is overwritten by the host's first command.
	void StartBoosterSync();
	void StopBoosterSync();
	bool BoosterSyncActive();

	// Call once per simulation step. Notices a local change and asks for it, applies any
	// command that lands on this step, and holds the globals at the agreed values so a
	// local keypress cannot quietly take one machine out of step with the other.
	void StepBoosterSync();

	// Force a push of the current values, host only. Used when somebody joins, so the new
	// peer does not have to wait for a change it can never see.
	bool PushBoosters();

	const char* BoosterSyncStatus();
	void LogBoosterSync();

} // namespace pilgrimage
