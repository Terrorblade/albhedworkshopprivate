#pragma once

#include <windows.h>

// Random encounters, readable and steerable.
//
// Two things a co-op mod needs from this subsystem, and they are very different sizes.
//
// READING is free. The accumulators, the gates and the pending-battle request are all
// plain globals, so a desync detector can watch them and a panel can show them with no
// intervention at all.
//
// STEERING needs exactly one detour, and only because of one fact: the distance the check
// accumulates is the distance the BOUND player moved, computed by the caller from the ATEL
// context position cache. A second character is invisible to it. But the distance arrives
// as a function PARAMETER rather than being read inside, so substituting it is a two-line
// hook with no save and restore of engine state. That is much cheaper than the per-player
// trigger pass, which had to shadow per-actor state. See reversing\RANDOM_ENCOUNTER.md.
//
// WHY NOT JUST CALL THE CHECK ONCE PER PLAYER: the accumulators are single globals, so N
// calls accumulate N times the distance and the encounter rate scales with the player
// count. Folding the per-player distances into one number is the only way to keep the
// pacing the shipped game has.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Reading
	// ---------------------------------------------------------------------------

	// Everything the encounter check keeps between calls, in one read.
	struct EncounterState
	{
		float distRemain;       // since the last roll. Needs to pass 10.0 for a roll to happen
		float distTotal;        // since the last battle. The rising chance comes off this
		int steps;              // distTotal / 10, which is what the dev window calls steps
		bool encountersEnabled; // the No Encounters ARMOUR ABILITY, not a debug switch
		bool debugEncountersOn; // the PS2 debug menu switch
		int boosterRate;        // 0 off, 1 normal, 2 high. Scales the simulation
		bool valid;
	};

	bool ReadEncounterState(EncounterState* out);

	// The deferred battle request the check writes on a hit. FFX_Btl_MainStep polls this
	// later in the same frame, so a mod can see a battle coming one subsystem early.
	struct PendingBattle
	{
		int kind;       // 1 means a random encounter is pending
		int sceneIndex; // from the HIGH word of the scene-and-map dword
		int zoneIndex;
		int formationIndex;
		bool pending;
		bool valid;
	};

	bool ReadPendingBattle(PendingBattle* out);

	// ---------------------------------------------------------------------------
	// Steering
	// ---------------------------------------------------------------------------

	// Called with the distance the engine was about to accumulate. Return the distance it
	// should accumulate instead. Return the value unchanged to leave the engine alone.
	//
	// Runs on the game thread, inside FFX_MainStep, once per simulation sub-step while a
	// field map is running. Keep it cheap and do not call back into the engine from it.
	typedef float (*EncounterDistanceFn)(float engineDistance);

	// Installs the detour the first time it is called. Pass null to stop steering, which
	// leaves the detour in place and makes it a pass-through, because unpatching live code
	// is a race against the game thread for no benefit.
	//
	// Returns false when the detour could not be installed, which means the prologue bytes
	// did not match and NOTHING was patched.
	bool SetEncounterDistanceHook(EncounterDistanceFn hook);

	bool EncounterHookInstalled();

	// How many times the hook has been consulted, and what it last saw and returned. For a
	// panel line, and for telling "my hook is not installed" apart from "my hook is
	// installed and the engine is not calling it because no field map is running".
	struct EncounterHookStats
	{
		DWORD calls;
		float lastEngineDistance;
		float lastReturnedDistance;
	};

	void ReadEncounterHookStats(EncounterHookStats* out);

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	// Logs the accumulators, every gate with whether it is currently blocking, the pending
	// request, and the hook stats. One call says why a battle is or is not coming.
	void LogEncounterState();

} // namespace ffx
