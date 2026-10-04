#pragma once

#include <windows.h>

#include "ffx/addresses/Encounter.h"

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
	// THE ENCOUNTER TABLE, enumerated from memory
	// ---------------------------------------------------------------------------
	//
	// This is where EVERY battle in the game is listed, the developers' test and debug
	// fights included, and none of it has to be baked. One 4096-byte file, loaded once at
	// battle init, laid out as scenes -> zones -> formation entries:
	//
	//     scene      one per map that has any battle data. EncounterSceneCount of them,
	//                and the scene carries the 8 character battle scene base name
	//     zone       a region of that map with its own encounter rate. Rate 0 means no
	//                RANDOM battles there, which says nothing about scripted ones
	//     formation  one fight. Its encounter id plus a weight for the random roll
	//
	// A scripted battle is named by (mapId << 16) | encounterId, so walking all three
	// levels yields every battle the game can start. That is what RequestScriptedBattle
	// searches internally, so an id that comes out of here is one it will accept.
	//
	// EMPTY UNTIL THE FIRST BATTLE INIT has loaded btl.bin. Every call fails safe before
	// that, so a zero count means "not loaded yet", not "no battles exist".
	//
	// Nothing here is cached. It is a handful of pointer adds per call, and caching would
	// mean deciding when a file the engine reloads has changed.

	struct EncounterScene
	{
		int sceneIndex;
		int mapId;
		char name[Rva::EncMapEntrySceneNameBytes + 1]; // terminated by this struct
		int zoneCount;
	};

	struct EncounterZone
	{
		int zoneIndex;
		int rate;        // 0 means no random battles in this zone
		int weightTotal; // the modulo total the random roll uses, see the caveat below
		int formationCount;
	};

	// THE WEIGHTS ARE REPORTED, NOT RELIED ON. The engine reads a formation entry's
	// weight as the high nibble of the byte after the encounter id, but an older note
	// in the IDB records the shipped btl.bin storing those weights raw rather than
	// shifted up, which would make the accumulator always 0. Nothing here needs the
	// weight: the encounter id beside it is what starts a fight.

	// How many scenes the loaded table declares, or 0 before it loads.
	int EncounterSceneCount();

	bool EncounterSceneAt(int sceneIndex, EncounterScene* out);

	// The engine's own lookup, which is a linear search. -1 when this map has no battle
	// data at all, which is the cleanest "nothing happens here". Going through the
	// engine means a caller gets the same answer a battle request would.
	//
	// MAP ID 0 ALWAYS RETURNS -1. That search tests "mapId > 0" before it looks at
	// anything, so a scene declaring map 0 could never be found through it.
	int EncounterSceneForMap(int mapId);

	bool EncounterZoneAt(int sceneIndex, int zoneIndex, EncounterZone* out);

	// One fight. outEncounterId is the low byte of a scripted battle id and outWeight is
	// its share of the random roll, 0 for a formation the roll can never pick.
	bool EncounterFormationAt(int sceneIndex, int zoneIndex, int slot, int* outEncounterId,
	    int* outWeight);

	// How many formation entries the whole table holds, which is the battle count. Walks
	// it, so call it once rather than per frame.
	int EncounterFormationTotal();

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	// Logs the accumulators, every gate with whether it is currently blocking, the pending
	// request, and the hook stats. One call says why a battle is or is not coming.
	void LogEncounterState();

	// One line per scene, with its zones and formation counts. The quick way to see what
	// the enumeration above can actually reach.
	void LogEncounterTable();

	// ---------------------------------------------------------------------------
	// NAMING A FIGHT BY ITS MONSTERS, before it starts
	// ---------------------------------------------------------------------------
	//
	// The encounter table says which fight, not what is in it. The monsters are in the
	// fight's own battle field file, asset kind 14, one per (scene, zone, slot), and the
	// link between the two is the scene entry's +4 asset index base:
	//
	//     fieldAssetIndex = sceneEntry[+4] + (formations in zones before this one) + slot
	//     path            = AssetResolvePath(AssetKindBaseTable[14] + fieldAssetIndex)
	//     monsterSection  = file + *(u32 *)(file + 12)
	//     8 u16 at monsterSection + 12, 0xFFFF empty, low 12 bits the kernel monster id
	//
	// 862 of the 863 fights in the shipped btl.bin resolve to a file that is there and
	// parses, and 856 have at least one enemy. The one that does not is scene 0 slot 0,
	// whose path is the developers' own host0:/home/$USER$/battle/jp/btl/output.bin.
	//
	// THE ROSTER IS COMPLETE AND CANNOT CHANGE MID BATTLE. These 8 ids include units that
	// only appear partway through, which is why Penance's arms and Yu Yevon's pagodas are
	// already in the file.
	//
	// ONE ARCHIVE READ PER CALL, a VBF open plus an inflate of 2 to 30 KB. Fine for the
	// selected row in a panel, far too slow per frame and for all 863 at once unless you
	// mean it. CACHE WHAT YOU ASK FOR.

	const int kEncounterMonsterSlots = 8;

	struct EncounterMonsters
	{
		int count;                       // how many of the 8 slots are filled
		int ids[kEncounterMonsterSlots]; // kernel monster ids, the KernelMonsters space
	};

	// Which battle field file a fight uses. -1 when the table is not loaded or the
	// (scene, zone, slot) does not name a fight. Seven scenes share another scene's base
	// and so legitimately return an index inside that scene's run.
	int EncounterFieldAssetIndex(int sceneIndex, int zoneIndex, int slot);

	// The archive path for that file, already converted from the engine's "host0:/" form
	// to the "ffx_ps2/..." form ffx::ReadDataFile wants. outBytes wants at least 272.
	bool EncounterFieldPath(int sceneIndex, int zoneIndex, int slot, char* out, int outBytes);

	// The enemy roster for a fight. Reads the archive, so see the caching note above.
	// False when the file is not there or does not parse. A fight with no enemies comes
	// back true with count 0, which is a real answer and not a failure.
	bool EncounterMonstersAt(int sceneIndex, int zoneIndex, int slot, EncounterMonsters* out);

	// The roster of the fight that is RUNNING, straight out of the live section pointer
	// with no archive read at all. False outside a battle.
	bool LiveEncounterMonsters(EncounterMonsters* out);

} // namespace ffx
