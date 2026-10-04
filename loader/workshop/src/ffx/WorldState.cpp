#include "ffx/WorldState.h"

#include "ffx/GameLists.h"

#include <string.h>

#include "ffx/Addresses.h"
#include "ffx/GameState.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		typedef unsigned char(__cdecl* ReencodeCharNamesFn)(void);
		typedef void(__cdecl* RecomputeDerivedFn)(void);
		typedef void(__cdecl* MapRequestChangeFn)(int mapId, char entryPoint);
		typedef void(__cdecl* MapResumeFn)(void);
		typedef void(__cdecl* MapSetTransitionFramesFn)(int frames);
		typedef void(__cdecl* MapWarpToFn)(int mapId, char entryPoint);
		typedef void(__cdecl* ClearEquipStatBonusFn)(void);
		typedef void(__cdecl* SetSceneAndSubFn)(void);
		typedef unsigned char(__cdecl* SaveFileInstallFn)(void* dest, int saveImage);

		template <class T>
		T Resolve(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		// The block base, guarded. Null until a save slot has been set up, which is the
		// state at the title screen and the state a panel refresh can hit.
		BYTE* Block()
		{
			BYTE* base = (BYTE*)ModuleAddress(Rva::SaveData);
			if (!Readable(base, SaveBlock::Size))
				return NULL;

			return base;
		}

		inline BYTE Rd8(const BYTE* p)
		{
			return *(volatile const BYTE*)p;
		}
		inline WORD Rd16(const BYTE* p)
		{
			return *(volatile const WORD*)p;
		}
		inline DWORD Rd32(const BYTE* p)
		{
			return *(volatile const DWORD*)p;
		}

		bool ReadFlag(DWORD rva, DWORD* out)
		{
			const BYTE* p = (const BYTE*)ModuleAddress(rva);
			if (!Readable(p, 4))
				return false;

			*out = Rd32(p);
			return true;
		}

		// The two map ids that are not map ids. Passing either to RequestMapChange would do
		// something other than load a map, so the wrapper refuses rather than surprising
		// its caller.
		//
		// The leave-field id is read from the game by LeaveFieldMapId rather than spelled
		// here. This constant is the fallback for when that read fails, and 399 is what
		// every build seen so far holds.
		const int LeaveFieldPseudoMapIdFallback = 399;
		const int TitleScreenMapId = 23;

	} // namespace

	DWORD SaveBlockSize()
	{
		return SaveBlock::Size;
	}

	bool SnapshotSaveBlock(void* out, DWORD bytes)
	{
		if (!out || bytes < SaveBlock::Size)
			return false;

		const BYTE* base = Block();
		if (!base)
			return false;

		memcpy(out, base, SaveBlock::Size);
		return true;
	}

	TransplantOptions DefaultTransplantOptions()
	{
		TransplantOptions o;
		o.keepLocalPlaytime = true;
		o.reencodeNames = false;
		o.recomputeDerived = true;
		o.setJustLoadedFlag = true;
		return o;
	}

	bool TransplantSaveBlock(const void* bytes, DWORD size, const TransplantOptions& options,
	    TransplantResult* resultOut)
	{
		if (resultOut)
			memset(resultOut, 0, sizeof(*resultOut));

		// A short buffer is refused rather than partially applied. A half-installed world
		// state is worse than no install, because the script work area at +0x1EC has to be
		// all-or-nothing: the scenario word alone is read by 331 script descriptors, and a
		// mismatch there does not glitch, it produces a different game.
		if (!bytes || size != SaveBlock::Size)
		{
			workshop::Log("world state: refusing a transplant of %u bytes, the block is "
			              "exactly %u and a partial install would be worse than none",
			    (unsigned)size, (unsigned)SaveBlock::Size);
			return false;
		}

		BYTE* base = Block();
		if (!base)
		{
			workshop::Log("world state: the save block is not readable yet, so there is "
			              "nothing to transplant over");
			return false;
		}

		// Playtime ticks every frame, so the incoming copy is stale the moment it was
		// taken. Keeping the local value stops one machine's clock jumping backwards.
		DWORD playtime = 0;
		if (options.keepLocalPlaytime && Readable(base + SaveBlock::Playtime, 4))
			playtime = Rd32(base + SaveBlock::Playtime);

		memcpy(base, bytes, SaveBlock::Size);

		if (options.keepLocalPlaytime && Readable(base + SaveBlock::Playtime, 4))
			*(volatile DWORD*)(base + SaveBlock::Playtime) = playtime;

		if (options.setJustLoadedFlag && Readable(base + SaveBlock::JustLoadedFlag, 1))
			*(volatile BYTE*)(base + SaveBlock::JustLoadedFlag) = 1;

		// Pure text, and only needed across a language boundary. Skipping it on a
		// same-language pair is what the shipped load path effectively does too.
		if (options.reencodeNames)
			Resolve<ReencodeCharNamesFn>(Rva::SaveDataReencodeCharNames)();

		// THE re-derive. Rebuilds the equip stat bonus cache from the sphere grid and then
		// every character's derived stats, effective stats, ability masks and HP/MP clamps.
		// The game's own save load does this. Skip it and the party keeps the stats of
		// whoever used to be in this slot.
		if (options.recomputeDerived)
			Resolve<RecomputeDerivedFn>(Rva::SphereGridRecomputeDerived)();

		if (resultOut)
		{
			resultOut->installed = true;
			resultOut->reencodedNames = options.reencodeNames;
			resultOut->recomputedDerived = options.recomputeDerived;
			resultOut->setJustLoadedFlag = options.setJustLoadedFlag;
			resultOut->bytesCopied = SaveBlock::Size;
			resultOut->playtimePreserved = playtime;

			WorldLocation loc;
			if (ReadWorldLocation(&loc))
			{
				resultOut->mapIdAfter = loc.mapId;
				resultOut->entryPointAfter = loc.entryPoint;
				resultOut->checkpointMapAfter = loc.checkpointMapId;
				resultOut->checkpointEntryAfter = loc.checkpointEntryPoint;
			}
		}

		return true;
	}

	bool ReadWorldLocation(WorldLocation* out)
	{
		if (!out)
			return false;

		memset(out, 0, sizeof(*out));

		const BYTE* base = Block();
		if (!base)
			return false;

		out->mapId = (int)Rd16(base + SaveBlock::LiveMapId);
		out->previousMapId = (int)Rd16(base + SaveBlock::LivePreviousMapId);
		out->entryPoint = (int)Rd8(base + SaveBlock::LiveEntryPoint);
		out->entrySubId = (int)Rd8(base + SaveBlock::LiveEntrySubId);

		// Low word is the entry point, high word is the map. Not the other way round, which
		// is the mistake the old CurrentMapId name encouraged.
		out->checkpointEntryPoint = (int)Rd16(base + SaveBlock::CheckpointMapAndEntry);
		out->checkpointMapId = (int)Rd16(base + SaveBlock::CheckpointMapAndEntry + 2);

		out->scenario = (int)Rd16(base + SaveBlock::Scenario);

		DWORD pending = 0;
		DWORD loaded = 0;
		if (ReadFlag(Rva::MapChangePending, &pending))
			out->changePending = pending != 0;
		if (ReadFlag(Rva::SceneLoaded, &loaded))
			out->sceneLoaded = loaded != 0;

		out->valid = true;
		return true;
	}

	bool RequestMapChange(int mapId, int entryPoint)
	{
		if (mapId < 0)
		{
			// A negative id is the engine's own "go to the checkpoint" encoding. A caller
			// that wants that should say so, because it ignores the arguments entirely.
			workshop::Log("world state: refusing map change to %d. A negative id means go "
			              "to the checkpoint, so call RequestResumeFromCheckpoint instead.",
			    mapId);
			return false;
		}
		if (mapId == LeaveFieldMapId())
		{
			workshop::Log("world state: refusing map change to %d. That is the leave-field "
			              "pseudo map, it does not load anything. Call RequestLeaveField if "
			              "that is what you meant.",
			    mapId);
			return false;
		}
		if (mapId == TitleScreenMapId)
		{
			workshop::Log("world state: refusing map change to %d, the title screen. It "
			              "runs extra teardown and is not a map load.",
			    mapId);
			return false;
		}
		if (entryPoint < 0 || entryPoint > 255)
			return false;

		if (!Block())
			return false;

		Resolve<MapRequestChangeFn>(Rva::MapRequestChange)(mapId, (char)entryPoint);
		return true;
	}

	bool RequestResumeFromCheckpoint()
	{
		const BYTE* base = Block();
		if (!base)
			return false;

		// The resume consumes and zeroes the checkpoint, so a zero map there means there is
		// nothing to resume to and the call would move the player to map 0.
		const WORD checkpointMap = Rd16(base + SaveBlock::CheckpointMapAndEntry + 2);
		if (checkpointMap == 0)
		{
			workshop::Log("world state: no checkpoint to resume to, the map half of +0xB8 "
			              "is zero. It is consumed on use, so this is normal after one "
			              "resume has already happened.");
			return false;
		}

		Resolve<MapResumeFn>(Rva::MapRequestResumeFromCheckpoint)();
		return true;
	}

	bool RequestLoadLiveLocation()
	{
		const BYTE* base = Block();
		if (!base)
			return false;

		const WORD mapId = Rd16(base + SaveBlock::LiveMapId);
		if (mapId == 0)
		{
			workshop::Log("world state: the live map id is zero, so the block names nowhere "
			              "to load and the pending flag would send us to map 0");
			return false;
		}

		// One store, the same store both shipped request functions finish with. Everything
		// those two do before it is field shuffling this case must not have.
		//
		// FFX_Atel_StepOnce picks the flag up inside FFX_MainStep on a later simulation
		// step, counts the transition frames down, and then runs the real scene load. So the
		// load lands on the simulation thread at a step boundary, which is the whole reason
		// this is safe to do from a lockstep peer at all.
		DWORD* pending = (DWORD*)ModuleAddress(Rva::MapChangePending);
		if (!Readable(pending, 4))
			return false;

		*(volatile DWORD*)pending = 1;

		workshop::Log("world state: asked for a load of the live location, map %d entry %d, "
		              "without disturbing the previous-map history",
		    (int)mapId, (int)Rd8(base + SaveBlock::LiveEntryPoint));
		return true;
	}

	bool SetMapTransitionFrames(int frames)
	{
		if (frames < 0)
			return false;

		Resolve<MapSetTransitionFramesFn>(Rva::MapSetTransitionFrames)(frames);
		return true;
	}

	int LeaveFieldMapId()
	{
		DWORD value = 0;
		if (!ReadFlag(Rva::QuitPseudoMapId, &value))
			return LeaveFieldPseudoMapIdFallback;

		// -1 is the engine's "the resume path refuses and raises the leave-field handshake
		// instead" state, not a map id. Anything outside a plausible map id is treated the
		// same way, so a caller never compares against garbage.
		const int id = (int)value;
		if (id <= 0 || id > 0xFFFF)
			return LeaveFieldPseudoMapIdFallback;

		return id;
	}

	bool RequestLeaveField()
	{
		if (!Block())
			return false;

		// Straight past RequestMapChange's refusal, because here the reserved id is the
		// point. The engine raises its leave-field handshake and FFX_MainStep picks it up.
		Resolve<MapRequestChangeFn>(Rva::MapRequestChange)(LeaveFieldMapId(), 0);
		return true;
	}

	bool ReadMapChangeState(MapChangeState* out)
	{
		if (!out)
			return false;

		memset(out, 0, sizeof(*out));

		DWORD pending = 0;
		DWORD frames = 0;
		DWORD flag = 0;
		DWORD loaded = 0;

		if (!ReadFlag(Rva::MapChangePending, &pending))
			return false;

		out->pending = pending != 0;
		if (ReadFlag(Rva::MapChangeDelayFrames, &frames))
			out->delayFrames = (int)frames;
		if (ReadFlag(Rva::MapChangeDelayFlag, &flag))
			out->delayFlag = flag != 0;
		if (ReadFlag(Rva::SceneLoaded, &loaded))
			out->sceneLoaded = loaded != 0;

		out->valid = true;
		return true;
	}

	bool IsDebugMode()
	{
		DWORD value = 0;
		if (!ReadFlag(Rva::DebugMode, &value))
			return false;

		return value != 0;
	}

	bool CanWarpTo()
	{
		// Debug mode is the override, and it is checked first because it makes the context
		// bit irrelevant.
		if (IsDebugMode())
			return true;

		// Otherwise bit 0x80 of the ATEL context's byte 0 has to be set, because the gate
		// is "if that byte is not negative, return". The context pointer lives in the Atel
		// area, so this reads the slot rather than pulling that header in: it is one
		// indirection and duplicating the whole accessor here would be worse.
		const DWORD* slot = (const DWORD*)ModuleAddress(Rva::AtelContextPtr);
		if (!Readable(slot, 4))
			return false;

		const BYTE* ctx = (const BYTE*)(*(volatile const DWORD*)slot);
		if (!Readable(ctx, 1))
			return false;

		return (Rd8(ctx) & 0x80) != 0;
	}

	bool WarpTo(int mapId, int entryPoint)
	{
		if (mapId < 0 || mapId > 0xFFFF || entryPoint < 0 || entryPoint > 255)
			return false;

		if (!Block())
			return false;

		// Refuse rather than call into a silent no-op. FFX_Map_WarpTo returns void and
		// gives no sign it bailed, so a caller that got here on a closed gate would see
		// nothing happen and have nothing to inspect.
		if (!CanWarpTo())
		{
			workshop::Log("world state: refusing a warp to map %d. FFX_Map_WarpTo needs bit "
			              "0x80 of the ATEL context byte 0 set, or debug mode on, and "
			              "neither holds. It would have returned without doing anything.",
			    mapId);
			return false;
		}

		// And say that it consumed the bit, because the next call will be refused and the
		// reason will not be obvious from the second log line on its own.
		Resolve<MapWarpToFn>(Rva::MapWarpTo)(mapId, (char)entryPoint);
		workshop::Log("world state: warped to map %d entry %d. That consumed the context "
		              "bit, so a second warp in this context will be refused.",
		    mapId, entryPoint);
		return true;
	}

	bool ArmWarpGate()
	{
		typedef void(__cdecl * ArmFn)(void);
		ArmFn arm = Resolve<ArmFn>(Rva::MapArmWarpGate);
		if (!Readable((void*)arm, 1))
			return false;
		arm();
		return true;
	}

	bool WarpWithSavedFade(int eventId, int entryPoint)
	{
		if (entryPoint < 0 || entryPoint > 255)
			return false;

		// The loadability check comes before anything else, because getting it wrong does
		// not produce an error, it produces an unkillable spin inside the load.
		if (!EventIdLoadable(eventId))
		{
			workshop::Log("world state: refusing a warp to event %d. It has no shipped "
			              "package, and loading one of those spins the simulation thread in "
			              "while(1) with no way out but killing the process.",
			    eventId);
			return false;
		}

		if (!Block())
			return false;

		typedef void(__cdecl * WarpSavedFn)(int eventId, char entryPoint, int flag);
		WarpSavedFn warp = Resolve<WarpSavedFn>(Rva::MapWarpToWithSavedFade);
		if (!Readable((void*)warp, 1))
			return false;

		// Arm it ourselves rather than refusing, which is what the engine's own callers
		// do. The gate is consumed by the warp, so this is per call.
		ArmWarpGate();

		warp(eventId, (char)entryPoint, 0);
		workshop::Log("world state: warped to event %d entry %d", eventId, entryPoint);
		return true;
	}

	bool RecomputeDerivedStats()
	{
		if (!Block())
			return false;

		Resolve<RecomputeDerivedFn>(Rva::SphereGridRecomputeDerived)();
		return true;
	}

	bool ClearEquipStatBonusCache()
	{
		if (!Block())
			return false;

		Resolve<ClearEquipStatBonusFn>(Rva::SaveDataClearEquipStatBonus)();
		return true;
	}

	bool ReencodeCharacterNames()
	{
		if (!Block())
			return false;

		Resolve<ReencodeCharNamesFn>(Rva::SaveDataReencodeCharNames)();
		return true;
	}

	bool ApplySceneAndSubFromHistory()
	{
		if (!Block())
			return false;

		Resolve<SetSceneAndSubFn>(Rva::SaveDataSetSceneAndSub)();
		return true;
	}

	bool InstallSaveFileImage(const void* image, bool* engineAcceptedOut)
	{
		if (engineAcceptedOut)
			*engineAcceptedOut = false;

		if (!image)
			return false;

		BYTE* base = Block();
		if (!base)
			return false;

		// The engine's own signature is (dest, saveImage) and it copies from saveImage + 64,
		// so the 64-byte header is skipped by the callee and the caller passes the image
		// base. Nothing here validates that header, because the engine's return value is
		// how it says it did not like the image.
		const unsigned char accepted =
		    Resolve<SaveFileInstallFn>(Rva::SaveFileInstallBlock)(base, (int)(INT_PTR)image);

		if (engineAcceptedOut)
			*engineAcceptedOut = accepted != 0;

		// The copy already happened by the time a rejection is reported, so the derived
		// state is rebuilt either way. Leaving it stale would be worse than rebuilding it
		// from a block the engine disliked.
		Resolve<RecomputeDerivedFn>(Rva::SphereGridRecomputeDerived)();
		return accepted != 0;
	}

	void LogWorldLocation()
	{
		WorldLocation loc;
		if (!ReadWorldLocation(&loc))
		{
			workshop::Log("world state: the save block is not readable yet, so no save slot "
			              "has been set up");
			return;
		}

		workshop::Log("=== world location ===");
		workshop::Log("live: map %d entry %d sub %d, previous map %d", loc.mapId,
		    loc.entryPoint, loc.entrySubId, loc.previousMapId);
		workshop::Log("checkpoint: map %d entry %d%s", loc.checkpointMapId,
		    loc.checkpointEntryPoint,
		    loc.checkpointMapId == 0 ? "  (zero, so already consumed)" : "");
		workshop::Log("scenario word %d, which 331 script descriptors read", loc.scenario);
		workshop::Log("map change pending %d, scene loaded %d", loc.changePending ? 1 : 0,
		    loc.sceneLoaded ? 1 : 0);

		// Worth saying every time, because it is the one thing about this subsystem that
		// surprises people: the block names a door, not a place.
		workshop::Log("remember the block identifies a DOORWAY, not a position. A joining "
		              "client arrives at this entry point, not next to the host.");
	}

} // namespace ffx
