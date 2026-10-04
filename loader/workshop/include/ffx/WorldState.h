#pragma once

#include <windows.h>

// Handing one machine's whole world to another, and putting it on the right map.
//
// See reversing\WORLD_STATE.md. The three facts that make this cheap, each proved there:
// the save block is pure data with no pointers so it goes on the wire as-is, the game
// itself transplants it with a plain memcpy in four separate places, and everything
// derived from it is rebuilt by one call.
//
// WHAT THIS IS NOT. The save block identifies a DOORWAY, a (map id, entry point) pair, not
// a position. The spawn XYZ comes from the map's own warp table. So a joining client lands
// at the last door the host walked through, not next to the host. Putting the character in
// the right place afterwards is the character layer's job, not this one's.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// Snapshot and transplant
	// ---------------------------------------------------------------------------

	// The exact number of bytes a snapshot is, so a caller never has to spell 0x68C0.
	DWORD SaveBlockSize();

	// Copies the live block out. The buffer must hold SaveBlockSize() bytes. Returns false
	// when the block is not readable yet, which is the state at the title screen.
	bool SnapshotSaveBlock(void* out, DWORD bytes);

	// What a transplant may be told to leave alone.
	struct TransplantOptions
	{
		// Playtime at +0xBC ticks every frame, so a snapshot of it is stale the instant it
		// is taken and copying it makes one machine's clock jump. Default true.
		bool keepLocalPlaytime;

		// The 18 name records are encoded for the language they were typed in. Re-encoding
		// is pure text and costs nothing in gameplay state, but it is only NEEDED when the
		// two machines run different languages. Default false.
		bool reencodeNames;

		// Rebuild the derived stats. This is the step the game's own save load makes and
		// FFX_Debug_ApplyViewerSave skips, which is why the latter is not a safe model.
		// There is almost no reason to turn this off. Default true.
		bool recomputeDerived;

		// Set the game's own byte +0x2A "loaded from file" flag, which
		// FFX_SaveData_SetSceneAndSub consumes once to take the previous-scene fields from
		// the +0xC0 history instead of from stale live values. One byte, removes a class of
		// transition bug. Default true.
		bool setJustLoadedFlag;
	};

	// Sensible defaults, as described above. Use this and then change what you mean.
	TransplantOptions DefaultTransplantOptions();

	// What a transplant actually did, so a caller can log it rather than guess.
	struct TransplantResult
	{
		bool installed;
		bool reencodedNames;
		bool recomputedDerived;
		bool setJustLoadedFlag;
		DWORD bytesCopied;
		DWORD playtimePreserved;
		int mapIdAfter;
		int entryPointAfter;
		int checkpointMapAfter;
		int checkpointEntryAfter;
	};

	// Installs a snapshot over the live block.
	//
	// GAME THREAD ONLY, and only at a point where the simulation is not mid-step. This
	// replaces the entire world: the party, the inventory, the sphere grids, the 8,192-byte
	// script work area that holds every chest and door and story flag. Calling it while a
	// script is running is asking for the script to finish against a different world than
	// it started in.
	//
	// Does NOT move the player. Call RequestResumeFromCheckpoint or RequestMapChange after.
	bool TransplantSaveBlock(const void* bytes, DWORD size, const TransplantOptions& options,
	    TransplantResult* resultOut);

	// ---------------------------------------------------------------------------
	// Where in the world
	// ---------------------------------------------------------------------------

	struct WorldLocation
	{
		int mapId;                // live, word +0x00
		int previousMapId;        // live, word +0x02
		int entryPoint;           // live, byte +0x0C
		int entrySubId;           // live, byte +0x0D
		int checkpointMapId;      // +0xBA, zeroed when consumed
		int checkpointEntryPoint; // +0xB8
		int scenario;             // +0xBEC, the word 331 script descriptors read
		bool changePending;       // the deferred request has been raised and not consumed
		bool sceneLoaded;
		bool valid;
	};

	bool ReadWorldLocation(WorldLocation* out);

	// Raise the deferred map change. Five stores and a flag, consumed by FFX_Atel_StepOnce
	// inside FFX_MainStep one simulation step later. That delay is a feature: the load
	// lands on a step boundary both peers can agree on.
	//
	// Deliberately wraps FFX_Map_RequestChange and NOT FFX_Map_WarpTo, because the latter
	// has a gate it consumes and would silently refuse a second call.
	//
	// Refuses mapId 399 (the leave-field pseudo map), mapId 23 (the title) and negative
	// values, because each means something other than "load this map" and a caller that
	// passes one by accident should hear about it rather than quietly leave the field.
	bool RequestMapChange(int mapId, int entryPoint);

	// "Put me where the save block says I am." Unpacks the checkpoint at +0xB8/+0xBA into
	// the live fields and raises the same deferred request. This is the post-transplant
	// step a joining client wants, and the game already contains it.
	//
	// Returns false when there is no checkpoint to resume to, which is what a zero at +0xBA
	// means, because the resume consumes and zeroes it.
	bool RequestResumeFromCheckpoint();

	// "Load the map the live fields already name." This is the one a transplant wants, and
	// it is deliberately neither of the two above.
	//
	// After a transplant the live map id at +0x00 and the live entry point at +0x0C ALREADY
	// hold the host's current location, because they came over in the block. What is
	// missing is only the load, so this raises the pending flag and touches nothing else.
	//
	// Why not RequestResumeFromCheckpoint: it reads +0xB8/+0xBA and writes them OVER the
	// live fields, so it would move the client to the host's last save sphere instead of to
	// where the host is standing.
	//
	// Why not RequestMapChange with the live values read back: it shuffles current into
	// previous first, so previous map and previous entry point would end up equal to the
	// current ones. Those two fields are readable by scripts through
	// FFX_SaveData_GetPrevEntryPoint, so a client whose history differs from the host's is a
	// divergence in state the simulation can observe. Under lockstep that is the kind of
	// difference that surfaces much later as an unexplained desync.
	//
	// Returns false when there is no block or the live map id is zero, which means the block
	// names nowhere to go.
	bool RequestLoadLiveLocation();

	// How many simulation steps to wait before the load actually happens. The warp path
	// sets this, and a bare RequestMapChange does not, so set it if a fade is wanted.
	bool SetMapTransitionFrames(int frames);

	// "Leave the field." Not a map load. The engine reserves one map id for this, reads as
	// 399, and passing it to RequestMapChange raises a different flag that FFX_MainStep
	// picks up. RequestMapChange refuses that id on purpose so a caller cannot trip it by
	// accident, which is why this is a separate function.
	bool RequestLeaveField();

	// The reserved id itself, read from the game rather than hardcoded. Returns 399 on
	// every build seen so far. Zero means it could not be read.
	int LeaveFieldMapId();

	// Where a deferred map change has got to. All pure reads, safe from any thread.
	struct MapChangeState
	{
		bool pending;    // FFX_Atel_StepOnce polls this inside FFX_MainStep
		int delayFrames; // simulation steps still to wait before the load
		bool delayFlag;  // cleared the step after the countdown reaches zero
		bool sceneLoaded;
		bool valid;
	};

	bool ReadMapChangeState(MapChangeState* out);

	// ---------------------------------------------------------------------------
	// The warp, and why it is awkward
	// ---------------------------------------------------------------------------

	// FFX_Map_WarpTo is the full warp: unbind the player, request the change, set the fade
	// rate and the transition frames. It is wrapped here rather than left out, because the
	// alternative is every caller rediscovering its gate.
	//
	// The gate: it opens with "if the ATEL context's byte 0 is NOT negative and we are not
	// in debug mode, return", and then CLEARS that same bit 0x80. So it needs the bit set,
	// it consumes it, and a second call in the same context is silently refused with no
	// log and no return value to check.
	//
	// CanWarpTo reports whether the gate would pass right now, so a caller can find out
	// instead of guessing. WarpTo refuses and says why rather than calling into a silent
	// no-op. For ordinary map movement use RequestMapChange and set the fade separately,
	// which is what the mod does.
	bool CanWarpTo();
	bool WarpTo(int mapId, int entryPoint);

	// Opens the gate WarpTo needs, which turns out to be three instructions rather than
	// something to work around. FFX_Map_ArmWarpGate is one of only two setters of that
	// bit in the whole binary, and the engine's own warp path calls it immediately
	// before warping. So "the gate is shut" is a precondition, not a wall.
	bool ArmWarpGate();

	// The warp the GAME uses for a door, a save point or an airship destination. Its
	// only callers are four ATEL script opcodes, which is every place in FFX that
	// moves the player somewhere, so this is the one to prefer over WarpTo.
	//
	// Arms the gate first, so unlike WarpTo it does not have to be refused.
	//
	// REFUSES AN EVENT ID THAT CANNOT BE LOADED, and that refusal is the point of it.
	// FFX_Ev_LoadEventPackage checks the package magic and then spins in while(1) with
	// no break and no return, so a bad id hard locks the simulation thread. See
	// ffx::EventIdLoadable in ffx/GameLists.h.
	bool WarpWithSavedFade(int eventId, int entryPoint);

	// Whether the game's own debug mode is on. Read only. Turning it on has effects well
	// beyond letting a warp through, so the kit does not offer a setter.
	bool IsDebugMode();

	// ---------------------------------------------------------------------------
	// Pieces of the save load, exposed on their own
	//
	// TransplantSaveBlock calls the ones it needs. They are here as well because they are
	// each useful to anything that edits save state, not only to a world transfer. A mod
	// that rewrites the sphere grid or swaps equipment needs the re-derive just as much as
	// a co-op join does.
	// ---------------------------------------------------------------------------

	// Rebuilds g_ffxEquipStatBonus from the sphere grid, then every character's derived
	// stats, effective stats, ability masks and HP/MP clamps. Call this after ANY edit to
	// the sphere grid, to equipment, or to the save block, or the party keeps the stats it
	// had before the edit. The game's own save load calls it. FFX_Debug_ApplyViewerSave
	// does not, which is exactly why that one is not a safe model to copy.
	bool RecomputeDerivedStats();

	// Zeroes the equip stat bonus cache without rebuilding it. FFX_Btl_Init calls this and
	// nothing on the battle path visibly refills it, which WORLD_STATE.md records as
	// unsettled. If you call this, call RecomputeDerivedStats afterwards.
	bool ClearEquipStatBonusCache();

	// Re-encodes the 18 name records at +0x634C for the running language. PURE TEXT, no
	// gameplay state, so two machines on the same language never need it.
	bool ReencodeCharacterNames();

	// Consumes the byte +0x2A "loaded from file" flag, taking the previous-scene fields
	// from the +0xC0 history instead of from stale live values. The engine calls this
	// itself on the next transition, so a transplant only has to SET the byte and let the
	// engine do this part. Exposed for the case where a caller wants it applied now.
	bool ApplySceneAndSubFromHistory();

	// Installs a save FILE image, meaning a 64-byte header followed by the 0x68C0 block,
	// and re-encodes the names. This is the game's own file load path. A mod holding a bare
	// block wants TransplantSaveBlock instead, which is why that one exists.
	//
	// Returns what the engine returned, so false means the engine rejected the image.
	bool InstallSaveFileImage(const void* image, bool* engineAcceptedOut);

	// The ATEL script opcode whose entire body is "memcpy(&g_ffxSaveData, arg + 64,
	// 0x68C0); return 1", library 12 function 87. NOT WRAPPED, deliberately. It is a
	// strictly worse InstallSaveFileImage, used by exactly one shipped developer test map,
	// and its only value is as proof that the block is self-contained. See
	// Rva::AtelSysSave087.

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	// Logs the live location, the checkpoint, the scenario word and the pending state. One
	// call answers "where does this machine think it is".
	void LogWorldLocation();

} // namespace ffx
