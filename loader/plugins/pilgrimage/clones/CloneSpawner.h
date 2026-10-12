#pragma once

#include <windows.h>

// Spawning and despawning clones. Everything here must run on the game thread.

namespace pilgrimage
{

	// The c-series ids that ship a field motion set, from tools/pcmodels.py. Only
	// these are worth spawning: an id with no field motions would slide along in a
	// T-pose.
	//
	// Which id is which character is no longer a guess. The game keeps the mapping
	// in a table, g_ffxCharIndexToChrId, so chr id = character index + 1: Tidus 1,
	// Yuna 2, Auron 3, Kimahri 4, Wakka 5, Lulu 6, Rikku 7, Seymour 8. 101 to 108
	// are the same eight at high detail, and 41 is Rikku in her alternate outfit.
	// 45, 307, 901 and 908 are in the list because they have motion sets, and
	// nobody has identified them yet.
	extern const int SpawnableChrIds[];
	extern const int SpawnableChrIdCount;

	// Who a chr id is, for the log and the control panel, for example "Yuna" or
	// "Wakka (high detail)". Never NULL, so it is safe to print. Returns "unknown"
	// for an id with no entry in the game's table, and falls back to the id's number
	// when no game is loaded, since the name table lives in the save data.
	const char* ChrIdName(int chrId);

	// Spawn one body of chrId at an exact position, and return the roster entry it
	// took, or -1. Does not touch which body the F9 keys drive.
	//
	// why is a short label for the log, for example "spawned clone" or "co-op body
	// for peer 1". Never NULL in practice, and defaulted if it is.
	//
	// THE POSITION IS THE CALLER'S PROBLEM AND THAT IS THE POINT. A replicated spawn
	// has to put the body in the same place on both machines, which it can only do
	// if the numbers came from one of them rather than each working its own out.
	LONG SpawnBodyAt(int chrId, float x, float y, float z, float yaw, const char* why);

	// Which id the next spawn will use, from settings.spawnIdIndex.
	int SelectedChrId();

	// Move settings.spawnIdIndex by one, wrapping. Negative step walks backwards.
	void CycleSpawnId(int step);

	bool SpawnClone();

	// Despawns the clone holding the input focus.
	void DespawnActiveClone();

	void DespawnAllClones();

} // namespace pilgrimage
