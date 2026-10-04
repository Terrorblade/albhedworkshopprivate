#pragma once

#include <windows.h>
#include <stdint.h>

// Minigame determinism.
//
// The script minigames are ATEL bytecode stepped inside FFX_MainStep, so the
// lockstep gate already runs them in step. Three things it does not cover:
//
//  - The RNG state sits outside the save block, so the world transfer misses it.
//  - Three debug flags swap which bytecode package a map loads, so two machines
//    can run different scripts on the same map id with nothing in the save block
//    to show it.
//  - Pad auto repeat runs off the wall clock, and one shipped scene reads it.

namespace pilgrimage
{

	bool InstallMinigameSync();
	bool MinigameSyncInstalled();

	// Session start clears the package swap flags and, on the host, publishes the
	// RNG state. Call after the session is active.
	void StartMinigameSync();
	void StopMinigameSync();

	// Once per lockstep step. Feeds the pad auto repeat timer a step derived time.
	void ServiceMinigameStep(uint32_t step);

	// Host side. Reads the live RNG state and sends it. reason is a
	// workshop::RandomStateReason.
	bool PublishRandomState(int reason);
	bool PublishRandomStateTo(int peer, int reason);

	// Two regions. 0 is the RNG state, the package swap flags and the sub step
	// count. 1 is the overdrive minigame timer and Lulu's keyboard latches.
	const int kMinigameHashRegions = 2;
	int MinigameHashRegions(DWORD* out, int maxRegions);

	// How many times the host has published, and the client has applied.
	int RandomStatePublishCount();
	int RandomStateApplyCount();

	const char* MinigameSyncStatus();
	void LogMinigameSync();

}
