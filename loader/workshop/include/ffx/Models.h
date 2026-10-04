#pragma once

// The model list, and swapping the player's model for one of them.
//
//     ffx::RefreshModelList();
//     const workshop::PickerList& models = ffx::ModelList();
//     ffx::SwapResult r = ffx::SwapPlayerModel(0x1042);   // m066
//     if (r != ffx::SwapOk) workshop::Log("%s", ffx::SwapResultText(r));
//
// A model id is one nibble of category and twelve bits of number:
//
//     chrId = ((category & 0xF) << 12) | (number & 0xFFF)
//
//   0 c pc    playable characters     4 w wep  weapons
//   1 m mon   monsters                5 f obj  objects
//   2 n npc   NPCs                    6 k skl  skeletons
//   3 s sum   summons, the aeons
//
// Categories 7 to 14 do not exist, the letter comes back as '-' and the path does not
// resolve, so a picker clamps to 0..6. Category 15 is the prototype asset viewer and
// its table is empty in retail.
//
// THE LIST COMES FROM THE ENGINE, not from the archive. Rva::ChrRomIndexTables is a
// void *[7] of {s16 count; s16 number[count]}, loaded by FFX_Ch_Init with no debug
// gate, and ChFindRomEntry scans it on every single model load. So it is not a list of
// what shipped, it is the engine's own statement of what it is willing to load. 892
// entries. The two directions it disagrees with the archive both matter: c046 is listed
// but ships nothing, which shows up as SwapNoAsset rather than a crash, and c307 ships
// a full model but is not listed, so it cannot be loaded at all.
//
// A swap lasts until the next map or cutscene. The ATEL script sets the player actor's
// model from the map's own authoring, so a transition puts Tidus back. Re-apply.
//
// Research is in reversing/CHEAT_MODELS.md sections 1 to 3.

#include <windows.h>
#include "ffx/Api.h" // for the Character forward declaration
#include "workshop/PickerList.h"

namespace ffx
{

	const int kModelCategories = 7;

	// 'c', 'm', 'n', 's', 'w', 'f', 'k', or '-' out of range.
	char ModelCategoryLetter(int category);

	// "pc", "mon", "npc", "sum", "wep", "obj", "skl", or "" out of range.
	const char* ModelCategoryDir(int category);

	int ChrIdCategory(int chrId);
	int ChrIdNumber(int chrId);
	int MakeChrId(int category, int number);

	// The engine's four-character name, "c001" or "m042". Formatted here rather than
	// through FFX_Ch_IdToModelName, which sprintfs into one shared static buffer and
	// so cannot be used to build a list. False on a bad id or a short buffer.
	bool ModelName(int chrId, char* out, int outBytes);

	// Which motion mode a category wants. Monsters and aeons keep their clips in mode
	// 1, everything else in mode 0, and getting this wrong is the single most likely
	// way to end up with a model that stands still.
	int ModelMotionMode(int category);

	// True once FFX_Ch_Init has run, which is before any map.
	bool ModelTablesReady();

	// How many models the engine lists for a category, and one entry's number. The
	// number is not the index, the tables are sparse.
	int ModelCount(int category);
	int ModelNumberAt(int category, int index);
	int ModelTotal();

	// True when this id is in the ROM index, which is the gate every model load
	// passes. Asks the engine rather than re-walking the table.
	bool ModelLoadable(int chrId);

	// Every listed model, as picker options. Ids are chrIds and labels are "c001
	// (pc)" with the character or aeon name appended for the eighteen that have one.
	// Built once, RefreshModelList rebuilds it.
	const workshop::PickerList& ModelList();
	void RefreshModelList();

	// ---------------------------------------------------------------------------
	// The swap
	// ---------------------------------------------------------------------------

	enum SwapResult
	{
		SwapOk = 0,
		SwapBadId,        // not in the engine's ROM index, so nothing can load it
		SwapNoBody,       // there is no player character to replace
		SwapPoolFull,     // FFX_Ch_Allocate null-derefs on a full pool, so this refuses
		SwapNoDataSlot,   // all 40 CHRDATA records are taken, and the engine's claimer
		                  // memsets through a null pointer when that happens
		SwapNoAsset,      // in the index but ships no file, which is the c046 case
		SwapFailed        // the allocation itself came back null
	};

	const char* SwapResultText(SwapResult result);

	// The player's live body, and the model it is wearing.
	Character* PlayerBody();
	int PlayerModelId();

	// Replaces the player's body with a fresh one of chrId, carrying over the
	// position, the facing, the party slot and the player binding, then retires the
	// old body. Returns SwapOk only when the new body is live and is actually the
	// model asked for.
	//
	// ANY of the 892 listed models works from anywhere, with no residency dance. The
	// engine's own ChLoadChrData creates the cache entry, starts the read and blocks on
	// its pump until the data has landed, all inside this call, which is why the
	// shipped debug spawner does nothing about residency either. Nothing has to be
	// retried next frame.
	//
	// The mesh appears one to three frames later, because the Phyre model load is
	// async and ChrProcessPendingAttachments creates the render instance. The engine's
	// own spawn path has exactly the same latency.
	//
	// GAME THREAD ONLY. It allocates, pumps the read queue with no lock, walks the
	// CHRDATA table unguarded and disposes.
	SwapResult SwapPlayerModel(int chrId);

	// Free records in the engine's 40-entry CHRDATA table, which is the limit a model
	// browser actually runs into. Zero means the next swap would crash, so
	// SwapPlayerModel refuses there. A map transition frees them.
	int ModelDataSlotsFree();

	void LogModelTables();

} // namespace ffx
