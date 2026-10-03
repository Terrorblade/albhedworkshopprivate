#pragma once

#include <windows.h>

// Making a remote player's walking count toward a random battle.
//
// The engine accumulates the distance the BOUND player moved and rolls once per 10 units.
// A remote player is invisible to it, so without this a client can walk across Besaid and
// never meet anything. The directive was that a remote player's movement should trigger
// events like the main character's does, and random battles are the clearest case of that.
//
// The mechanism is one detour on FFX_Field_StepRandomEncounter, whose distance argument is
// a parameter rather than something it reads, so there is no engine state to save and
// restore. See ffx/Encounter.h and reversing\RANDOM_ENCOUNTER.md.
//
// WHY THERE IS A POLICY CHOICE HERE. The accumulators are single globals, so this cannot
// just call the check once per player: N calls accumulate N times the distance and the
// encounter rate multiplies by the player count. Something has to decide what "the party
// moved" means, and that is a question about how the game should feel rather than a
// question about the binary, so it is a setting and not a constant.
//
// ONE STEP OF LATENCY, on purpose. The detour fires inside FFX_MainStep, while the remote
// distances are measured after it, so a step consumes the distance walked during the
// previous step. Both machines do the identical thing, so it costs nothing in correctness
// and buying it back would mean driving remote bodies before the step gate has decided
// whether the step runs at all.

namespace pilgrimage
{

	enum class EncounterPolicy
	{
		// Shipped behaviour. Only the bound player's walking counts, so a remote player
		// never triggers anything. Here to A/B against, not as a real option.
		HostOnly = 0,

		// The distance is the largest any single player walked. The party covering ten
		// units rolls once, which is the pacing the shipped game has, and a remote player
		// walking on their own still builds toward a battle. The default.
		Max = 1,

		// Every player's distance adds up, so two players walking together meet twice as
		// much as one. Truer to "everybody is making noise", and probably annoying.
		Sum = 2,
	};

	// Installs the detour and starts steering. Safe to call once at startup, and returns
	// false when the detour could not be installed, which means nothing was patched and
	// encounters are untouched.
	bool InstallEncounterSync();

	bool EncounterSyncInstalled();

	void SetEncounterPolicy(EncounterPolicy policy);
	EncounterPolicy CurrentEncounterPolicy();
	const char* EncounterPolicyName(EncounterPolicy policy);

	// Called by the trigger pass each step with how far one remote player moved. Kept
	// separate from the position bookkeeping so the policy lives in one place.
	void ReportRemoteStepDistance(int slot, float distance);

	const char* EncounterSyncStatus();
	void LogEncounterSync();

} // namespace pilgrimage
