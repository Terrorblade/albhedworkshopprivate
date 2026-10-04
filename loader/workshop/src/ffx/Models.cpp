#include "ffx/Models.h"

#include "ffx/Character.h"
#include "ffx/GameState.h"
#include "ffx/Layout.h"
#include "ffx/addresses/Character.h"
#include "ffx/addresses/GameState.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <stdio.h>

namespace ffx
{

	using namespace workshop;

	namespace
	{

		// The letters and directories, which the engine keeps in two functions. Held
		// here as data because a picker needs 892 labels and calling across for each one
		// buys nothing.
		const char kLetters[kModelCategories] = { 'c', 'm', 'n', 's', 'w', 'f', 'k' };
		const char* kDirs[kModelCategories] = { "pc", "mon", "npc", "sum", "wep", "obj", "skl" };

		// No shipped table has a number above 999, and the name format is "%03d", so a
		// number past that could not match a path even if it were listed.
		const int kSaneCount = 4096;

		PickerList g_models;

		template <class T>
		T Resolve(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		const short* CategoryTable(int category)
		{
			if (category < 0 || category >= kModelCategories)
				return nullptr;

			void** tables = (void**)ModuleAddress(Rva::ChrRomIndexTables);
			if (!Readable(tables, sizeof(void*) * kModelCategories))
				return nullptr;

			const short* t = (const short*)tables[category];
			if (!Readable(t, 2))
				return nullptr;

			const int count = (int)t[0];
			if (count <= 0 || count > kSaneCount)
				return nullptr;
			if (!Readable(t, 2 + 2 * (size_t)count))
				return nullptr;
			return t;
		}

		typedef int(__cdecl* FindRomEntryFn)(int chrId);
		typedef int(__cdecl* GetSizeFn)(int romIndex);
		typedef void*(__cdecl* LoadChrDataFn)(int chrId);
		typedef void(__cdecl* NoArgFn)(void);
		typedef void(__cdecl* LoadMotionSyncFn)(int chrId, int set);
		typedef Character*(__cdecl* AllocateFn)(int chrId);
		typedef void(__cdecl* DisposeIfLiveFn)(Character* chr);
		typedef Character*(__cdecl* GetPlayerChrFn)(void);
		typedef void(__cdecl* SetPlayerChrFn)(Character* chr);
		typedef int(__cdecl* SetByte184Fn)(Character* chr, char value);
		typedef Character*(__cdecl* SetPartyIndexFn)(Character* chr, int index);
		typedef int(__cdecl* SetPosFn)(Character* chr, float x, float y, float z);
		typedef Character*(__cdecl* GetPosFn)(Character* chr, float* x, float* y, float* z);
		typedef int(__cdecl* SetRotAndMoveDirFn)(Character* chr, float radians);
		typedef void(__cdecl* WalkmeshMoveFn)(Character* chr);
		typedef void(__cdecl* SetLocomotionModeFn)(Character* chr, int mode);
		typedef int(__cdecl* MotSetByModeIndexFn)(Character* chr, int mode, int index);
		typedef void(__cdecl* MotSetPendingLoopCountFn)(Character* chr, int count);

		// The pool's own free-slot test, done here rather than through ffx::Character so
		// this file works whether or not BindApi has run.
		bool PoolHeadroom()
		{
			Character** array = (Character**)ModuleAddress(Rva::ChrArray);
			const int* count = (const int*)ModuleAddress(Rva::ChrCount);
			if (!Readable(array, sizeof(void*)) || !Readable(count, 4))
				return false;

			Character* base = *array;
			const int slots = *count;
			if (!base || slots <= 0)
				return false;

			for (int i = 0; i < slots; ++i)
			{
				const BYTE* entry = (const BYTE*)base + Chr::Stride * (DWORD)i;
				if (!Readable(entry, Chr::InUse + 1))
					return false;
				if (*(volatile const BYTE*)(entry + Chr::InUse) == 0)
					return true;
			}
			return false;
		}

		// Free records in the 40-entry CHRDATA table. A record is free when its first
		// dword is -1, and the count comes out of the two table bounds rather than being
		// baked, because End minus base is exactly 40 * 300.
		int ChrDataHeadroom()
		{
			const BYTE* base = (const BYTE*)ModuleAddress(Rva::ChrDataTable);
			const BYTE* end = (const BYTE*)ModuleAddress(Rva::ChrDataTableEnd);
			if (end <= base)
				return 0;

			const size_t bytes = (size_t)(end - base);
			if (!Readable(base, bytes))
				return 0;

			const int records = (int)(bytes / Rva::ChrDataRecordBytes);
			int free = 0;
			for (int i = 0; i < records; ++i)
			{
				const int* id = (const int*)(base + (size_t)i * Rva::ChrDataRecordBytes
				    + Rva::ChrDataIdOffForFreeTest);
				if (*id == -1)
					++free;
			}
			return free;
		}

		// CHRDATA.m_isFallback. Set when the model's asset did not load and the engine
		// quietly handed back Tidus instead, which is the c046 case.
		bool BodyIsFallback(Character* chr)
		{
			if (!chr)
				return false;

			const BYTE* data = (const BYTE*)DwordAt(chr, Chr::CharacterData);
			if (!Readable(data, Rva::ChrDataIsFallbackOff + 1))
				return false;
			return data[Rva::ChrDataIsFallbackOff] != 0;
		}

		// Appends the human name for the eighteen character indices whose model this is,
		// so "s007" also reads "Anima". Everything else keeps the engine's own label,
		// because a monster's model number is a different id space from its bestiary
		// entry and inventing a name there would be a guess.
		const char* FriendlyName(int chrId)
		{
			for (BYTE i = 0; i < (BYTE)kCharCount; ++i)
			{
				if ((int)ChrIdForCharacter(i, false) != chrId)
					continue;

				const char* name = UnitDisplayName(i);
				if (name && name[0])
					return name;
				return CharacterName(i);
			}
			return nullptr;
		}

	} // namespace

	char ModelCategoryLetter(int category)
	{
		if (category < 0 || category >= kModelCategories)
			return '-';
		return kLetters[category];
	}

	const char* ModelCategoryDir(int category)
	{
		if (category < 0 || category >= kModelCategories)
			return "";
		return kDirs[category];
	}

	int ChrIdCategory(int chrId)
	{
		return (chrId >> 12) & 0xF;
	}

	int ChrIdNumber(int chrId)
	{
		return chrId & 0xFFF;
	}

	int MakeChrId(int category, int number)
	{
		return ((category & 0xF) << 12) | (number & 0xFFF);
	}

	bool ModelName(int chrId, char* out, int outBytes)
	{
		if (!out || outBytes < 5)
			return false;

		const int category = ChrIdCategory(chrId);
		if (category < 0 || category >= kModelCategories)
			return false;

		_snprintf(out, (size_t)outBytes - 1, "%c%03d", kLetters[category],
		    ChrIdNumber(chrId));
		out[outBytes - 1] = 0;
		return true;
	}

	int ModelMotionMode(int category)
	{
		// mon and sum. FFX_Ch_DebugSpawnByName branches exactly here, and almost every
		// monster has zero mode 0 clips and a couple of hundred mode 1 ones, so mode 0
		// for a monster is a model that stands still.
		return (category == 1 || category == 3) ? 1 : 0;
	}

	bool ModelTablesReady()
	{
		for (int cat = 0; cat < kModelCategories; ++cat)
		{
			if (CategoryTable(cat))
				return true;
		}
		return false;
	}

	int ModelCount(int category)
	{
		const short* t = CategoryTable(category);
		return t ? (int)t[0] : 0;
	}

	int ModelNumberAt(int category, int index)
	{
		const short* t = CategoryTable(category);
		if (!t || index < 0 || index >= (int)t[0])
			return -1;
		return (int)(unsigned short)t[1 + index];
	}

	int ModelTotal()
	{
		int total = 0;
		for (int cat = 0; cat < kModelCategories; ++cat)
			total += ModelCount(cat);
		return total;
	}

	bool ModelLoadable(int chrId)
	{
		const int category = ChrIdCategory(chrId);
		if (category < 0 || category >= kModelCategories)
			return false;

		// The engine's own gate. It returns -1 for an id that is not in the table, and
		// every model load gives up on -1.
		return Resolve<FindRomEntryFn>(Rva::ChFindRomEntry)(chrId) >= 0;
	}

	void RefreshModelList()
	{
		g_models.Reset("chr rom index tables");

		for (int cat = 0; cat < kModelCategories; ++cat)
		{
			const int count = ModelCount(cat);
			for (int i = 0; i < count; ++i)
			{
				const int number = ModelNumberAt(cat, i);
				if (number < 0)
					continue;

				const int chrId = MakeChrId(cat, number);
				const char* friendly = FriendlyName(chrId);
				if (friendly)
					g_models.AddFormatted(chrId, "%c%03d  %s (%s)", kLetters[cat], number,
					    friendly, kDirs[cat]);
				else
					g_models.AddFormatted(chrId, "%c%03d  (%s)", kLetters[cat], number,
					    kDirs[cat]);
			}
		}

		g_models.SetLive(!g_models.Empty());
	}

	const workshop::PickerList& ModelList()
	{
		if (g_models.Empty() && ModelTablesReady())
			RefreshModelList();
		return g_models;
	}

	const char* SwapResultText(SwapResult result)
	{
		switch (result)
		{
		case SwapOk:
			return "done";
		case SwapBadId:
			return "the engine does not list that model, so nothing can load it";
		case SwapNoBody:
			return "there is no player character right now";
		case SwapPoolFull:
			return "the character pool is full, and allocating into a full pool crashes";
		case SwapNoDataSlot:
			return "all 40 of the engine's model data records are in use, and claiming a "
			       "41st crashes. A map change frees them.";
		case SwapNoAsset:
			return "that model is in the engine's index but ships no file";
		case SwapFailed:
			return "the allocation came back null";
		}
		return "?";
	}

	Character* PlayerBody()
	{
		Character* player = Resolve<GetPlayerChrFn>(Rva::ChGetPlayerChr)();
		return IsLive(player) ? player : nullptr;
	}

	int PlayerModelId()
	{
		Character* player = PlayerBody();
		if (!player)
			return 0;
		return (int)(unsigned short)ShortAt(player, Chr::Id);
	}

	SwapResult SwapPlayerModel(int chrId)
	{
		// The engine's own pre-flight, in the order the engine does it. FindRomEntry
		// answers "is this in the index at all" and GetSize answers "does the file
		// actually ship", which is the c046 case. They have to be back to back because
		// FindRomEntry selects the asset kind as a side effect.
		const int romIndex = Resolve<FindRomEntryFn>(Rva::ChFindRomEntry)(chrId);
		if (romIndex < 0)
			return SwapBadId;

		GetSizeFn* getSizeSlot = (GetSizeFn*)ModuleAddress(Rva::RomDevGetSize);
		if (Readable(getSizeSlot, sizeof(void*)) && *getSizeSlot
		    && Readable((void*)*getSizeSlot, 1))
		{
			if ((*getSizeSlot)(romIndex) == 0)
				return SwapNoAsset;
		}

		Character* old = PlayerBody();
		if (!old)
			return SwapNoBody;

		if (!PoolHeadroom())
			return SwapPoolFull;

		// Everything read off the old body has to be read before anything is allocated,
		// because an allocation can move what the pool slots hold.
		float x = 0.0f, y = 0.0f, z = 0.0f;
		Resolve<GetPosFn>(Rva::ChGetPos)(old, &x, &y, &z);
		const float yaw = FloatAt(old, Chr::Facing);
		const int partyIndex = (int)DwordAt(old, Chr::PartyIndex);
		const char byte184 = (char)ByteAt(old, Chr::Byte184);

		const int category = ChrIdCategory(chrId);
		const int mode = ModelMotionMode(category);

		// The CHRDATA table has to be checked as well as the CHR pool, and for the same
		// reason: ChBlkAllocate walks 40 records for a free one and, when there is none,
		// leaves its result pointer null and then memsets 300 bytes through it.
		if (ChrDataHeadroom() <= 0)
			return SwapNoDataSlot;

		// Make it resident. ONE CALL, and it blocks until the read and its completion
		// callback are done, inside this frame. ChLoadChrData creates the cache entry,
		// starts the read and pumps it, which is why the shipped ChDebugSpawnByName does
		// nothing about residency at all. No RomRead, no polling, no retry next frame.
		if (!Resolve<LoadChrDataFn>(Rva::ChLoadChrData)(chrId))
		{
			Log("models: ChLoadChrData(0x%04X) came back null, so the model did not load",
			    chrId);
			return SwapNoAsset;
		}

		// Ask for a hard failure rather than a silent Tidus. Without this, a model that
		// will not load comes back as a second c001 with m_isFallback set, which looks
		// exactly like success. One shot, Allocate clears it either way.
		Resolve<NoArgFn>(Rva::ChSetNoFallbackOnce)();

		Character* neo = Resolve<AllocateFn>(Rva::ChAllocate)(chrId);
		if (!neo)
			return SwapFailed;

		// Also self sufficient, it pumps its own read, so there is nothing to poll.
		Resolve<LoadMotionSyncFn>(Rva::ChLoadMotionSetSync)(chrId, mode);

		// Belt and braces. SetNoFallbackOnce should have turned this case into a null
		// above, so reaching here means the flag did not take, which is worth knowing.
		if (BodyIsFallback(neo))
		{
			Log("models: 0x%04X allocated with the fallback byte set even though the no "
			    "fallback flag was armed. Disposing it rather than handing back a Tidus.",
			    chrId);
			Resolve<DisposeIfLiveFn>(Rva::ChDisposeIfLive)(neo);
			return SwapNoAsset;
		}

		// BEFORE THE DISPOSE, AND THIS IS THE WHOLE TRICK. ChBindChrData writes the
		// one-slot Tidus cache for any body named c001 or c101 and for nothing else, so
		// after swapping to anything else the cache still points at the old body. Then
		// Dispose runs its guard, sees the cache pointing at what it is destroying, and
		// nulls the player binding that was just moved. Repointing it first is what
		// stops that.
		Character** tidusCache = (Character**)ModuleAddress(Rva::TidusChr);
		if (Readable(tidusCache, sizeof(void*)))
			*tidusCache = neo;

		Resolve<SetByte184Fn>(Rva::ChSetByte184)(neo, byte184);
		Resolve<SetPartyIndexFn>(Rva::ChSetPartyIndex)(neo, partyIndex);
		Resolve<SetPosFn>(Rva::ChSetPos)(neo, x, y, z);
		Resolve<SetRotAndMoveDirFn>(Rva::ChSetRotAndMoveDir)(neo, yaw);

		// Monsters and aeons want the field-battle locomotion set, copied from the
		// shipped debug spawner. Without it the body is correct and motionless.
		if (mode == 1)
		{
			Resolve<SetLocomotionModeFn>(Rva::ChSetLocomotionMode)(neo, 1);
			Resolve<MotSetByModeIndexFn>(Rva::MotSetByModeIndex)(neo, 1, 16);
			Resolve<MotSetPendingLoopCountFn>(Rva::MotSetPendingLoopCount)(neo, 0);
		}

		// Ground mode 1 writes the Y position from m_groundHeight every frame, and
		// m_groundHeight stays 0 until a triangle is bound, so skipping this leaves the
		// body at Y zero forever rather than where it was put.
		WriteFloat(neo, Chr::VelocityX, 0.0f);
		WriteFloat(neo, Chr::VelocityZ, 0.0f);
		Resolve<WalkmeshMoveFn>(Rva::ChWalkmeshMove)(neo);

		Resolve<SetPlayerChrFn>(Rva::ChSetPlayerChr)(neo);
		Resolve<DisposeIfLiveFn>(Rva::ChDisposeIfLive)(old);

		char name[8];
		ModelName(chrId, name, (int)sizeof(name));
		Log("models: player body swapped to %s (0x%04X), motion mode %d, at (%.1f %.1f "
		    "%.1f) yaw %.3f. It lasts until the next map, the ATEL script re-authors the "
		    "player actor's model on a transition.",
		    name, chrId, mode, (double)x, (double)y, (double)z, (double)yaw);
		return SwapOk;
	}

	int ModelDataSlotsFree()
	{
		return ChrDataHeadroom();
	}

	void LogModelTables()
	{
		if (!ModelTablesReady())
		{
			Log("models: the rom index tables are not loaded");
			return;
		}

		for (int cat = 0; cat < kModelCategories; ++cat)
			Log("models: %-3s %c, %d listed, motion mode %d", kDirs[cat], kLetters[cat],
			    ModelCount(cat), ModelMotionMode(cat));
		Log("models: %d in total, %d of the engine's model data records free, %s",
		    ModelTotal(), ModelDataSlotsFree(), ModelList().Describe());
	}

} // namespace ffx
