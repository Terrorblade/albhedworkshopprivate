#include "ffx/Minigames.h"

#include "ffx/Atel.h"
#include "ffx/Battle.h"
#include "ffx/GameLists.h"
#include "ffx/GameState.h"
#include "ffx/WorldState.h"
#include "ffx/addresses/Atel.h"
#include "ffx/addresses/Battle.h"
#include "ffx/addresses/Encounter.h"
#include "ffx/addresses/Minigames.h"
#include "ffx/addresses/WorldState.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <string.h>

namespace ffx
{

	using namespace workshop;

	namespace
	{

		template <class T>
		T Resolve(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		typedef int(__cdecl* SelectContextFn)(int index);
		typedef int(__cdecl* RestoreContextFn)(int saved);
		typedef int(__cdecl* GetActorFn)(int actorId);
		typedef char*(__cdecl* ResolveVarFn)(void* actor, unsigned int descriptor);
		typedef int(__cdecl* StartThreadFn)(int caller, int actor, int kind, int channel,
		    int entry);
		typedef int(__cdecl* FireWithEntryFn)(int caller, int actor, int kind, int entry);
		typedef int(__cdecl* RequestScriptedBattleFn)(int battleId, char a2, char a3);
		typedef int(__cdecl* NoArgIntFn)(void);
		typedef void(__cdecl* NoArgFn)(void);
		typedef int(__cdecl* SetIntFn)(int value);
		typedef WORD(__cdecl* GetMapIdFn)(void);

		// ---------------------------------------------------------------------------
		// Which minigame
		// ---------------------------------------------------------------------------

		struct PackageMatch
		{
			const char* tail; // matched as a prefix of the part after the slash
			Minigame kind;
		};

		// Longest first within a group, so "bltz02" wins over "bltz" for the hub.
		const PackageMatch kPackages[] = {
			{ "bltz02", MinigameBlitzballHub },
			{ "bltz", MinigameBlitzball },
			{ "nagi07", MinigameMonsterArena },
			{ "nagi", MinigameChocoboCalm },
			{ "dbg_nagi", MinigameChocoboCalm },
			{ "lmyt", MinigameChocoboRemiem },
			{ "kami", MinigameLightningDodge },
			{ "200thunder_kami", MinigameLightningDodge },
			{ "mcfr01", MinigameButterflies },
			{ "swin", MinigameJechtShot },
			{ "bika", MinigameCactuarHunt },
			{ "bvyt", MinigameViaPurifico },
			{ "hiku2", MinigameAirshipSearch },
		};
		const int kPackageCount = (int)(sizeof(kPackages) / sizeof(kPackages[0]));

		const char* kMinigameNames[MinigameCount] = {
			"none",
			"Blitzball match",
			"Blitzball team screens",
			"Chocobo race, Calm Lands",
			"Chocobo race, Remiem Temple",
			"Lightning dodge, Thunder Plains",
			"Butterfly hunt, Macalania",
			"Jecht Shot practice",
			"Cactuar hunt, Bikanel",
			"Via Purifico",
			"Monster Arena",
			"Airship search",
		};

		const int kEventNameBytes = 256;

		// ---------------------------------------------------------------------------
		// The ATEL plumbing
		// ---------------------------------------------------------------------------

		int SelectCtx0()
		{
			return Resolve<SelectContextFn>(Rva::AtelSelectContext)(0);
		}

		void RestoreCtx(int saved)
		{
			Resolve<RestoreContextFn>(Rva::AtelRestoreContext)(saved);
		}

		// The loaded package image, or NULL.
		BYTE* ReadPackageBase()
		{
			BYTE** slot = (BYTE**)ModuleAddress(Rva::EvPackageBase);
			if (!Readable(slot, sizeof(void*)))
				return nullptr;
			BYTE* image = *slot;
			if (!LooksLikePointer((DWORD)(UINT_PTR)image) || !Readable(image, 24))
				return nullptr;
			// The magic FFX_Ev_LoadEventPackage spins forever on. If it is not there the
			// pointer is not a package and nothing below should trust it.
			if (image[0] != 'E' || image[1] != 'V')
				return nullptr;
			return image;
		}

		// ---------------------------------------------------------------------------
		// THE PACKAGE CACHE, and the reason it exists.
		//
		// Every answer below is six or seven pointer hops with a Readable check on each,
		// and Readable is a VirtualQuery. A raw variable browser asks for every
		// descriptor in the package once per frame, which is a few thousand rows, so
		// doing the walk per row costs tens of milliseconds. So it is done once and
		// keyed on (image pointer, event id).
		//
		// Both halves of that key are needed. The engine frees the image and reallocates
		// on every map change, and the allocator hands back the same address often
		// enough that the pointer alone is not a change detector. The event id is
		// written by FFX_Ev_LoadEventPackage itself, so it moves on every load. Warping
		// to the same event twice keeps both, and that is fine, because then the package
		// content is identical and the cached offsets still describe it.
		// ---------------------------------------------------------------------------
		struct PackageCache
		{
			BYTE* image;
			int eventId;
			BYTE* block;
			const DWORD* table; // the shared variable descriptor table
			int varCount;
			int actorCount;
		};

		PackageCache g_cache = { nullptr, -1, nullptr, nullptr, 0, 0 };

		// The ATEL block inside the package, which is pkgBase + *(u32 *)(pkgBase + 4).
		// Verified in FFX_Ev_LoadEventPackage, which computes exactly that and passes it
		// to the context init, where it lands at ctx+0x48. Cross-checked against that
		// copy here, because the two come from the same value and a mismatch means
		// something else moved.
		BYTE* ResolveBlock(BYTE* image)
		{
			const DWORD offset = *(const DWORD*)(image + 4);
			if (offset == 0 || offset > 0x04000000)
				return nullptr;

			BYTE* block = image + offset;
			if (!Readable(block, 0x40))
				return nullptr;

			BYTE* ctx = AtelContext0();
			if (ctx)
			{
				BYTE* fromCtx = *(BYTE**)(ctx + Rva::AtelCtxBlockBase);
				if (fromCtx != block)
					return nullptr;
			}
			return block;
		}

		// The actor definition record inside the package image, out of the offset array
		// at block+0x38. This is the same record the engine's resolver reaches through
		// actor+0, and going at it from the package means it answers for an actor whose
		// live pool entry has not been built yet.
		BYTE* ResolveActorDef(BYTE* block, int actorId)
		{
			const int count = (int)*(const WORD*)(block + Rva::AtelBlockActorCount);
			if (actorId < 0 || actorId >= count)
				return nullptr;

			const DWORD* offsets = (const DWORD*)(block + Rva::AtelBlockActorOffsets);
			if (!Readable(offsets, sizeof(DWORD) * (size_t)count))
				return nullptr;

			const DWORD offset = offsets[actorId];
			if (offset == 0 || offset > 0x04000000)
				return nullptr;

			BYTE* def = block + offset;
			return Readable(def, Rva::AtelActorDefBytes) ? def : nullptr;
		}

		// Every actor in a package shares one descriptor table in all five packages that
		// were checked, so actor 0 answers for the package. The count comes from the gap
		// between the table and the int pool, which is the only way to get it in
		// process.
		const DWORD* ResolveVarTable(BYTE* block, int* outCount)
		{
			BYTE* def = ResolveActorDef(block, 0);
			if (!def)
				return nullptr;

			const DWORD tableOff = *(const DWORD*)(def + Rva::AtelActorDefVarTable);
			const DWORD poolOff = *(const DWORD*)(def + Rva::AtelActorDefIntPool);
			if (tableOff == 0 || poolOff <= tableOff)
				return nullptr;

			const DWORD bytes = poolOff - tableOff;
			const int count = (int)(bytes / (DWORD)Rva::AtelVarDescriptorBytes);
			if (count <= 0 || count > 65536)
				return nullptr;

			const DWORD* table = (const DWORD*)(block + tableOff);
			if (!Readable(table, bytes))
				return nullptr;

			*outCount = count;
			return table;
		}

		const PackageCache& Package()
		{
			BYTE* image = ReadPackageBase();
			const int eventId = CurrentEventId();

			if (image == g_cache.image && eventId == g_cache.eventId)
				return g_cache;

			g_cache.image = image;
			g_cache.eventId = eventId;
			g_cache.block = nullptr;
			g_cache.table = nullptr;
			g_cache.varCount = 0;
			g_cache.actorCount = 0;

			if (!image)
				return g_cache;

			BYTE* block = ResolveBlock(image);
			if (!block)
				return g_cache;

			g_cache.block = block;
			g_cache.actorCount = (int)*(const WORD*)(block + Rva::AtelBlockActorCount);
			g_cache.table = ResolveVarTable(block, &g_cache.varCount);
			return g_cache;
		}

		BYTE* AtelBlock()
		{
			return Package().block;
		}

		int BlockActorCount()
		{
			const PackageCache& p = Package();
			return p.block ? p.actorCount : -1;
		}

		BYTE* ActorDef(int actorId)
		{
			const PackageCache& p = Package();
			if (!p.block)
				return nullptr;
			return ResolveActorDef(p.block, actorId);
		}

		// The live pool actor, which is what the engine's resolver takes. Selects
		// context 0 for the lookup the same way every other ATEL getter does.
		void* LiveActor(int actorId)
		{
			const int count = BlockActorCount();
			if (actorId < 0 || count < 0 || actorId >= count)
				return nullptr;

			const int saved = SelectCtx0();
			const int raw = Resolve<GetActorFn>(Rva::AtelGetActor)(actorId);
			RestoreCtx(saved);

			if (!LooksLikePointer((DWORD)(unsigned)raw))
				return nullptr;
			void* actor = (void*)(UINT_PTR)(unsigned)raw;
			// 48 bytes is the smallest stride in the pool. Class 5 wants 0x48 plus the
			// offset, and that is checked where it is used.
			return Readable(actor, 48) ? actor : nullptr;
		}

		int g_varActor = 0;

		// The armed "fire this script once that map is loaded" request.
		int g_armEvent = -1;
		int g_armActor = -1;
		int g_armEntry = -1;

		// ---------------------------------------------------------------------------
		// Descriptors
		// ---------------------------------------------------------------------------

		DWORD MakeDescriptor(int type, int storageClass, DWORD offset)
		{
			return ((DWORD)type << Rva::AtelDescTypeShift)
			    | ((DWORD)storageClass << Rva::AtelDescClassShift)
			    | (offset & Rva::AtelDescOffsetMask);
		}

		const DWORD* DescriptorTable(int* outCount)
		{
			const PackageCache& p = Package();
			if (!p.table)
				return nullptr;
			if (outCount)
				*outCount = p.varCount;
			return p.table;
		}

		double ReadTyped(const void* p, int type)
		{
			switch (type)
			{
			case ScriptVarU8: return (double)*(const BYTE*)p;
			case ScriptVarS8: return (double)*(const char*)p;
			case ScriptVarU16: return (double)*(const WORD*)p;
			case ScriptVarS16: return (double)*(const short*)p;
			case ScriptVarU32: return (double)*(const DWORD*)p;
			case ScriptVarS32: return (double)*(const int*)p;
			case ScriptVarF32: return (double)*(const float*)p;
			default: return 0.0;
			}
		}

		double Clamp(double v, double lo, double hi)
		{
			if (v < lo)
				return lo;
			if (v > hi)
				return hi;
			return v;
		}

		void WriteTyped(void* p, int type, double value)
		{
			switch (type)
			{
			case ScriptVarU8: *(BYTE*)p = (BYTE)Clamp(value, 0, 255); break;
			case ScriptVarS8: *(char*)p = (char)Clamp(value, -128, 127); break;
			case ScriptVarU16: *(WORD*)p = (WORD)Clamp(value, 0, 65535); break;
			case ScriptVarS16: *(short*)p = (short)Clamp(value, -32768, 32767); break;
			case ScriptVarU32:
				*(DWORD*)p = (DWORD)Clamp(value, 0, 4294967295.0);
				break;
			case ScriptVarS32:
				*(int*)p = (int)Clamp(value, -2147483648.0, 2147483647.0);
				break;
			case ScriptVarF32: *(float*)p = (float)value; break;
			default: break;
			}
		}

		// ---------------------------------------------------------------------------
		// The named field tables.
		//
		// Everything here came out of the shipped bytecode rather than a guess, and
		// the note says so where a row is inferred. Class 0 rows are in the save file.
		// ---------------------------------------------------------------------------

		const MinigameField kBlitzFields[] = {
			{ "home score", "In the save block, so it sticks. Verified from six goal sites",
			    kScriptClassSave, -1, 0x1452, ScriptVarU8, 0, 255 },
			{ "away score", "The other six goal sites", kScriptClassSave, -1, 0x1453,
			    ScriptVarU8, 0, 255 },
			{ "half or period flag", "The name is a GUESS, from a correlation only",
			    kScriptClassSave, -1, 0x1454, ScriptVarU8, 0, 255 },
			{ "match clock, seconds",
			    "The displayed MM:SS. Set it to the half length minus one to blow the "
			    "whistle",
			    kScriptClassPackage, -1, 0x144, ScriptVarS32, 0, 100000 },
			{ "half length, seconds", "Ships as 300, which is 5:00", kScriptClassPackage,
			    -1, 0x148, ScriptVarS32, 0, 100000 },
			{ "who just scored", "1 home, 2 away", kScriptClassPackage, -1, 0x128F,
			    ScriptVarU8, 0, 255 },
		};

		const MinigameField kChocoboFields[] = {
			{ "clock tenths", "The live stopwatch, counting up to a 2:00 hard stop",
			    kScriptClassPackage, -1, 0xB0, ScriptVarU8, 0, 9 },
			{ "clock seconds", "", kScriptClassPackage, -1, 0xB1, ScriptVarU8, 0, 59 },
			{ "clock minutes", "At 2 the race ends as a timeout", kScriptClassPackage, -1,
			    0xB2, ScriptVarU8, 0, 9 },
			{ "which course", "1 to 4. Picks the result branch", kScriptClassPackage, -1,
			    0xBC, ScriptVarU8, 1, 4 },
			{ "race state", "0 running, 1 timed out at 2:00, 2 finished",
			    kScriptClassPackage, -1, 0xDF, ScriptVarU8, 0, 2 },
			{ "player balloons", "Each one is -3 s on the result tally",
			    kScriptClassPackage, -1, 0xD3, ScriptVarU8, 0, 255 },
			{ "player birds", "Each one is +3 s on the tally", kScriptClassPackage, -1,
			    0xD5, ScriptVarU8, 0, 255 },
			{ "trainer balloons", "", kScriptClassPackage, -1, 0xD4, ScriptVarU8, 0, 255 },
			{ "trainer birds", "", kScriptClassPackage, -1, 0xD6, ScriptVarU8, 0, 255 },
			{ "player final minutes", "What the result screen prints",
			    kScriptClassPackage, -1, 0xB9, ScriptVarU8, 0, 255 },
			{ "player final seconds", "", kScriptClassPackage, -1, 0xBA, ScriptVarU8, 0,
			    255 },
			{ "player final tenths", "", kScriptClassPackage, -1, 0xBB, ScriptVarU8, 0,
			    255 },
			{ "trainer final minutes", "Beat him by editing his time",
			    kScriptClassPackage, -1, 0xB6, ScriptVarU8, 0, 255 },
			{ "trainer final seconds", "", kScriptClassPackage, -1, 0xB7, ScriptVarU8, 0,
			    255 },
			{ "trainer final tenths", "", kScriptClassPackage, -1, 0xB8, ScriptVarU8, 0,
			    255 },
			{ "best time, course 1 minutes", "SAVED. Four courses of min, sec, tenths",
			    kScriptClassSave, -1, 0xA8, ScriptVarU8, 0, 255 },
			{ "best time, course 1 seconds", "", kScriptClassSave, -1, 0xA9, ScriptVarU8,
			    0, 255 },
			{ "best time, course 1 tenths", "", kScriptClassSave, -1, 0xAA, ScriptVarU8, 0,
			    255 },
			{ "best time, course 2 minutes", "SAVED", kScriptClassSave, -1, 0xAB,
			    ScriptVarU8, 0, 255 },
			{ "best time, course 2 seconds", "", kScriptClassSave, -1, 0xAC, ScriptVarU8,
			    0, 255 },
			{ "best time, course 2 tenths", "", kScriptClassSave, -1, 0xAD, ScriptVarU8, 0,
			    255 },
			{ "best time, course 3 minutes", "SAVED", kScriptClassSave, -1, 0xAE,
			    ScriptVarU8, 0, 255 },
			{ "best time, course 3 seconds", "", kScriptClassSave, -1, 0xAF, ScriptVarU8,
			    0, 255 },
			{ "best time, course 3 tenths", "", kScriptClassSave, -1, 0xB0, ScriptVarU8, 0,
			    255 },
			{ "best time, course 4 minutes", "SAVED. The fourth course was once missed",
			    kScriptClassSave, -1, 0xB1, ScriptVarU8, 0, 255 },
			{ "best time, course 4 seconds", "", kScriptClassSave, -1, 0xB2, ScriptVarU8,
			    0, 255 },
			{ "best time, course 4 tenths", "", kScriptClassSave, -1, 0xB3, ScriptVarU8, 0,
			    255 },
		};

		const MinigameField kLightningFields[] = {
			{ "dodge window", "Set to 45 by a Circle press and counted down. Hold it "
			                  "non-zero and every bolt is dodged",
			    kScriptClassPackage, -1, 0x7, ScriptVarU8, 0, 255 },
			{ "current streak", "The live counter. Jump straight to 199",
			    kScriptClassPackage, -1, 0x12, ScriptVarU16, 0, 65535 },
			{ "best streak", "SAVED. What the 5/10/20/50/100/150/200 reward switch reads",
			    kScriptClassSave, -1, 0x214, ScriptVarU16, 0, 65535 },
			{ "bolts seen", "SAVED", kScriptClassSave, -1, 0x210, ScriptVarU16, 0, 65535 },
			{ "bolts dodged", "SAVED", kScriptClassSave, -1, 0x212, ScriptVarU16, 0,
			    65535 },
			{ "reward bits",
			    "SAVED. 0x01 to 0x20 are the streak tiers, 0x40 and 0x80 the 30 and 80 "
			    "total tiers",
			    kScriptClassSave, -1, 0x208, ScriptVarU8, 0, 255 },
		};

		const MinigameField kButterflyFields[] = {
			{ "countdown seconds",
			    "Actor 37. Starts at 39 for the 40 s course and 29 for the 30 s one",
			    kScriptClassActor3, 37, 0x0, ScriptVarS32, 0, 100000 },
			{ "countdown tenths", "Actor 37, a 10 over 60 cascade",
			    kScriptClassActor3, 37, 0x4, ScriptVarS32, 0, 100000 },
			{ "gauge",
			    "Actor 38. Starts at 128, a blue is +3 and a red is -3, and the reward "
			    "needs 89 or more. The caught count and the red penalty are this one "
			    "variable, there is no separate counter",
			    kScriptClassActor3, 38, 0x16, ScriptVarU8, 0, 255 },
		};

		const MinigameField kJechtFields[] = {
			{ "score", "", kScriptClassPackage, -1, 0x2, ScriptVarU8, 0, 255 },
			{ "attempt budget",
			    "The practice loop runs while this is under 300, so 0 is unlimited "
			    "attempts",
			    kScriptClassPackage, -1, 0x1C, ScriptVarS32, 0, 100000 },
		};

		struct FieldTable
		{
			const MinigameField* rows;
			int count;
		};

		FieldTable TableFor(Minigame which)
		{
			FieldTable t = { nullptr, 0 };
			switch (which)
			{
			case MinigameBlitzball:
				t.rows = kBlitzFields;
				t.count = (int)(sizeof(kBlitzFields) / sizeof(kBlitzFields[0]));
				break;
			case MinigameChocoboCalm:
				t.rows = kChocoboFields;
				t.count = (int)(sizeof(kChocoboFields) / sizeof(kChocoboFields[0]));
				break;
			case MinigameLightningDodge:
				t.rows = kLightningFields;
				t.count = (int)(sizeof(kLightningFields) / sizeof(kLightningFields[0]));
				break;
			case MinigameButterflies:
				t.rows = kButterflyFields;
				t.count = (int)(sizeof(kButterflyFields) / sizeof(kButterflyFields[0]));
				break;
			case MinigameJechtShot:
				t.rows = kJechtFields;
				t.count = (int)(sizeof(kJechtFields) / sizeof(kJechtFields[0]));
				break;
			default: break;
			}
			return t;
		}

		// ---------------------------------------------------------------------------
		// The launch table.
		//
		// Where a row names an actor and an entry, warping alone drops you on the map
		// beside the NPC rather than in the minigame. Whether firing the entry cold
		// produces a playable minigame or a half-initialised one is the one thing in
		// here that the research could not settle, so the rows say so.
		// ---------------------------------------------------------------------------
		const MinigameLaunch kLaunches[] = {
			{ "Blitzball, the hub", "Go to the hub rather than straight to the match, "
			                        "because the match needs the roster state the hub "
			                        "builds",
			    MinigameBlitzballHub, 347, 0, -1, -1 },
			{ "Blitzball, a match directly",
			    "Only sane after the hub has run once this session",
			    MinigameBlitzball, 62, 0, -1, -1 },
			{ "Chocobo race, Calm Lands",
			    "The script fire is the race clock actor. Untested cold",
			    MinigameChocoboCalm, 223, 0, 82, 7 },
			{ "Chocobo race, Remiem Temple",
			    "Warp only, the script entry was never pinned down. Interact in game",
			    MinigameChocoboRemiem, 290, 0, -1, -1 },
			{ "Monster Arena", "The shop and the creature list. The fights are below",
			    MinigameMonsterArena, 307, 0, -1, -1 },
			{ "Lightning dodge, Thunder Plains",
			    "Starts itself. The strike scheduler and the dodge window are both "
			    "auto-started by the map",
			    MinigameLightningDodge, 140, 0, -1, -1 },
			{ "Lightning dodge, the 200 reward map",
			    "Turn on the Thunder Plains treasure swap FIRST, it is a package swap at "
			    "load time",
			    MinigameLightningDodge, 256, 0, -1, -1 },
			{ "Butterfly hunt, Macalania",
			    "The script fire is the game body on actor 38. Untested cold",
			    MinigameButterflies, 241, 0, 38, 9 },
			{ "Jecht Shot practice", "The script fire is actor 32. Untested cold",
			    MinigameJechtShot, 302, 0, 32, 8 },
			{ "Cactuar hunt, Bikanel",
			    "No clock and no single start script, it is a find-and-reach puzzle. Warp "
			    "and walk",
			    MinigameCactuarHunt, 129, 0, -1, -1 },
			{ "Via Purifico", "Also find-and-reach", MinigameViaPurifico, 207, 0, -1, -1 },
			{ "Airship search", "Cid's destination search on the bridge",
			    MinigameAirshipSearch, 382, 0, -1, -1 },
		};
		const int kLaunchCount = (int)(sizeof(kLaunches) / sizeof(kLaunches[0]));

		// ---------------------------------------------------------------------------
		// The Monster Arena's fights, as packed battle ids. These come from the btl:002
		// call sites in nagi0700's bytecode, which is why they are baked.
		// ---------------------------------------------------------------------------
		struct ArenaFight
		{
			int mapId;
			int encounterId;
			const char* label;
		};

		const ArenaFight kArenaFights[] = {
			{ 601, 105, "arena 601, fight 105" },
			{ 603, 76, "arena 603, fight 76" },
			{ 603, 77, "arena 603, fight 77" },
			{ 603, 79, "arena 603, fight 79" },
			{ 603, 80, "arena 603, fight 80" },
			{ 603, 81, "arena 603, fight 81" },
			{ 603, 82, "arena 603, fight 82" },
			{ 603, 83, "arena 603, fight 83" },
			{ 603, 92, "arena 603, fight 92" },
			{ 603, 93, "arena 603, fight 93" },
			{ 603, 94, "arena 603, fight 94" },
			{ 603, 95, "arena 603, fight 95" },
			{ 603, 96, "arena 603, fight 96" },
			{ 603, 97, "arena 603, fight 97" },
			{ 603, 98, "arena 603, fight 98" },
			{ 603, 99, "arena 603, fight 99" },
			{ 604, 0, "arena 604, creature 0" },
			{ 604, 1, "arena 604, creature 1" },
			{ 604, 2, "arena 604, creature 2" },
			{ 604, 3, "arena 604, creature 3" },
			{ 604, 4, "arena 604, creature 4" },
			{ 604, 5, "arena 604, creature 5" },
			{ 604, 6, "arena 604, creature 6" },
			{ 604, 7, "arena 604, creature 7" },
			{ 604, 8, "arena 604, creature 8" },
			{ 604, 9, "arena 604, creature 9" },
			{ 604, 10, "arena 604, creature 10" },
			{ 604, 11, "arena 604, creature 11" },
			{ 604, 12, "arena 604, creature 12" },
			{ 604, 13, "arena 604, creature 13" },
			{ 604, 14, "arena 604, creature 14" },
			{ 604, 15, "arena 604, creature 15" },
			{ 604, 16, "arena 604, creature 16" },
			{ 604, 17, "arena 604, creature 17" },
			{ 604, 18, "arena 604, creature 18" },
			{ 310, 0, "butterfly penalty 0" },
			{ 310, 1, "butterfly penalty 1" },
			{ 310, 2, "butterfly penalty 2" },
			{ 310, 3, "butterfly penalty 3" },
			{ 310, 4, "butterfly penalty 4" },
			{ 310, 5, "butterfly penalty 5" },
		};
		const int kArenaFightCount = (int)(sizeof(kArenaFights) / sizeof(kArenaFights[0]));

		// ---------------------------------------------------------------------------
		// Byte flags
		// ---------------------------------------------------------------------------

		BYTE* FlagByte(DWORD rva)
		{
			BYTE* p = (BYTE*)ModuleAddress(rva);
			return Readable(p, 1) ? p : nullptr;
		}

		bool ReadFlagByte(DWORD rva)
		{
			const BYTE* p = FlagByte(rva);
			return p && *p != 0;
		}

		bool WriteFlagByte(DWORD rva, bool on)
		{
			BYTE* p = FlagByte(rva);
			if (!p)
				return false;
			*p = on ? 1 : 0;
			return true;
		}

		DWORD* FlagDword(DWORD rva)
		{
			DWORD* p = (DWORD*)ModuleAddress(rva);
			return Readable(p, 4) ? p : nullptr;
		}

	} // namespace

	// ---------------------------------------------------------------------------
	// Which minigame
	// ---------------------------------------------------------------------------

	bool CurrentPackageName(char* out, int outBytes)
	{
		if (!out || outBytes <= 0)
			return false;
		out[0] = 0;

		const char* name = (const char*)ModuleAddress(Rva::EvCurrentEventName);
		if (!Readable(name, kEventNameBytes))
			return false;

		int n = 0;
		while (n < kEventNameBytes - 1 && name[n] != 0)
			++n;
		if (n == 0)
			return false;

		// Printable only. The buffer is a char[256] in .data, so before the first load
		// it holds whatever the image shipped with.
		for (int i = 0; i < n; ++i)
			if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7E)
				return false;

		int copy = n;
		if (copy > outBytes - 1)
			copy = outBytes - 1;
		memcpy(out, name, (size_t)copy);
		out[copy] = 0;
		return true;
	}

	Minigame CurrentMinigame()
	{
		char name[64];
		if (!CurrentPackageName(name, (int)sizeof(name)))
			return MinigameNone;

		// The part after the last slash, because the two-letter group prefix comes from
		// the archive layout rather than from the exe.
		const char* tail = name;
		for (const char* p = name; *p; ++p)
			if (*p == '/')
				tail = p + 1;

		for (int i = 0; i < kPackageCount; ++i)
			if (strncmp(tail, kPackages[i].tail, strlen(kPackages[i].tail)) == 0)
				return kPackages[i].kind;

		return MinigameNone;
	}

	const char* MinigameName(Minigame which)
	{
		if (which < 0 || which >= MinigameCount)
			return "out of range";
		return kMinigameNames[which];
	}

	bool PackageLoaded()
	{
		return ReadPackageBase() != nullptr;
	}

	int LiveMapId()
	{
		if (!GameLoaded())
			return -1;
		return (int)Resolve<GetMapIdFn>(Rva::SaveDataGetMapId)();
	}

	// ---------------------------------------------------------------------------
	// Launching
	// ---------------------------------------------------------------------------

	int MinigameLaunchCount()
	{
		return kLaunchCount;
	}

	const MinigameLaunch* MinigameLaunchAt(int index)
	{
		if (index < 0 || index >= kLaunchCount)
			return nullptr;
		return &kLaunches[index];
	}

	bool LaunchMinigame(const MinigameLaunch* launch)
	{
		if (!launch)
			return false;

		// The hard-lock guard. An event id whose package does not ship does not fail,
		// FFX_Ev_LoadEventPackage spins in while(1) and the process has to be killed.
		if (!EventIdLoadable(launch->eventId))
		{
			Log("minigames: refusing event %d, it ships no package", launch->eventId);
			return false;
		}

		if (launch->actorId >= 0 && launch->scriptEntry >= 0)
			ArmScriptFire(launch->eventId, launch->actorId, launch->scriptEntry);
		else
			CancelScriptFire();

		if (!RequestMapChange(launch->eventId, launch->entryPoint))
		{
			CancelScriptFire();
			return false;
		}
		return true;
	}

	bool ArmScriptFire(int eventId, int actorId, int scriptEntry)
	{
		if (eventId < 0 || actorId < 0 || scriptEntry < 0)
			return false;
		g_armEvent = eventId;
		g_armActor = actorId;
		g_armEntry = scriptEntry;
		return true;
	}

	bool ScriptFireArmed(int* outEventId, int* outActorId, int* outScriptEntry)
	{
		if (g_armEvent < 0)
			return false;
		if (outEventId)
			*outEventId = g_armEvent;
		if (outActorId)
			*outActorId = g_armActor;
		if (outScriptEntry)
			*outScriptEntry = g_armEntry;
		return true;
	}

	void CancelScriptFire()
	{
		g_armEvent = -1;
		g_armActor = -1;
		g_armEntry = -1;
	}

	bool PumpMinigameLaunch()
	{
		if (g_armEvent < 0)
			return false;

		// Wait for the package the warp asked for, not just for any package. The event
		// id global is written by FFX_Ev_LoadEventPackage itself, so it only matches
		// once the load has happened.
		if (CurrentEventId() != g_armEvent || !PackageLoaded())
			return false;

		const int actorId = g_armActor;
		const int entry = g_armEntry;
		CancelScriptFire();

		const int entries = ActorScriptEntryCount(actorId);
		if (entries <= 0 || entry >= entries)
		{
			Log("minigames: actor %d has %d script entries, so entry %d was not fired",
			    actorId, entries, entry);
			return false;
		}

		const int started = StartActorScript(kAtelNoCaller, actorId, 1, entry);
		Log("minigames: event %d loaded, fired actor %d entry %d, thread %s", g_armEvent,
		    actorId, entry, started ? "started" : "refused");
		return started != 0;
	}

	// ---------------------------------------------------------------------------
	// Running a script entry directly
	// ---------------------------------------------------------------------------

	int ActorScriptEntryCount(int actorId)
	{
		const BYTE* def = ActorDef(actorId);
		if (!def)
			return -1;
		return (int)*(const WORD*)(def + Rva::AtelActorDefEntryCount);
	}

	int StartActorScript(int callerActorId, int actorId, int channel, int scriptEntry)
	{
		if (actorId < 0 || scriptEntry < 0)
			return 0;
		if (channel < 0 || channel > 8)
			return 0;

		// The engine bounds checks neither the entry index nor the channel, and it
		// indexes the actor's entry table with the raw value, so the bound is here.
		const int entries = ActorScriptEntryCount(actorId);
		if (entries <= 0 || scriptEntry >= entries)
			return 0;

		const int saved = SelectCtx0();
		const int started = Resolve<StartThreadFn>(Rva::AtelStartThread)(callerActorId,
		    actorId, 0, channel, scriptEntry);
		RestoreCtx(saved);
		return started;
	}

	int FireActorEventWithEntry(int callerActorId, int actorId, int eventKind,
	    int scriptEntry)
	{
		if (actorId < 0)
			return 0;
		if (eventKind < 0 || eventKind >= kAtelEventKindCount)
			return 0;

		// A negative entry means "let the engine pick", which the engine itself
		// handles, and an out of range positive one falls back to entry record 0 inside
		// the engine. So only the negative case is passed through untouched.
		if (scriptEntry >= 0)
		{
			const int entries = ActorScriptEntryCount(actorId);
			if (entries <= 0 || scriptEntry >= entries)
				return 0;
		}

		// It selects context 0 itself and restores, so there is nothing to wrap.
		return Resolve<FireWithEntryFn>(Rva::AtelFireActorEventWithEntry)(callerActorId,
		    actorId, eventKind, scriptEntry);
	}

	// ---------------------------------------------------------------------------
	// Scripted battles
	// ---------------------------------------------------------------------------

	bool ScriptedBattleAllowed()
	{
		const DWORD* disabled = FlagDword(Rva::BattleDisabled);
		if (!disabled || *disabled == 1)
			return false;
		return Resolve<NoArgIntFn>(Rva::BtlGetPhase)() == 0;
	}

	bool RequestScriptedBattle(int mapId, int encounterId)
	{
		if (mapId < 0 || mapId > 0xFFFF || encounterId < 0 || encounterId > 0xFFFF)
			return false;
		if (!ScriptedBattleAllowed())
			return false;

		BYTE* pending = FlagByte(Rva::BattlePendingKind);
		if (!pending)
			return false;

		const int battleId = (mapId << 16) | encounterId;
		Resolve<RequestScriptedBattleFn>(Rva::BtlRequestScriptedBattle)(battleId, 1, 0);

		// The engine's function returns -1 whether it took the request or threw it
		// away, so the only honest test is whether the pending kind became 2.
		return *pending == 2;
	}

	int ArenaFightCount()
	{
		return kArenaFightCount;
	}

	bool ArenaFightAt(int index, int* outMapId, int* outEncounterId,
	    const char** outLabel)
	{
		if (index < 0 || index >= kArenaFightCount)
			return false;
		if (outMapId)
			*outMapId = kArenaFights[index].mapId;
		if (outEncounterId)
			*outEncounterId = kArenaFights[index].encounterId;
		if (outLabel)
			*outLabel = kArenaFights[index].label;
		return true;
	}

	// ---------------------------------------------------------------------------
	// Script variables
	// ---------------------------------------------------------------------------

	bool ScriptVarsReady()
	{
		return DescriptorTable(nullptr) != nullptr;
	}

	int ScriptVarCount()
	{
		int count = 0;
		if (!DescriptorTable(&count))
			return -1;
		return count;
	}

	bool ScriptVarAt(int index, ScriptVar* out)
	{
		if (!out)
			return false;

		int count = 0;
		const DWORD* table = DescriptorTable(&count);
		if (!table || index < 0 || index >= count)
			return false;

		const DWORD desc = table[index * 2];
		out->index = index;
		out->type = (int)(desc >> Rva::AtelDescTypeShift);
		out->storageClass = (int)((desc >> Rva::AtelDescClassShift) & 7);
		out->offset = desc & Rva::AtelDescOffsetMask;
		out->flagBit = (desc & 0x01000000u) != 0;
		// The element count is the u16 at descriptor+4, which is the second dword's low
		// half. It is the bound the engine's own indexed form clamps to.
		out->elements = (int)(table[index * 2 + 1] & 0xFFFFu);
		if (out->elements <= 0)
			out->elements = 1;
		return true;
	}

	int ScriptVarActor()
	{
		return g_varActor;
	}

	bool SetScriptVarActor(int actorId)
	{
		const int count = BlockActorCount();
		if (actorId < 0 || count < 0 || actorId >= count)
			return false;
		g_varActor = actorId;
		return true;
	}

	int PackageActorCount()
	{
		return BlockActorCount();
	}

	void* ScriptVarAddress(int storageClass, DWORD offset, int actorId)
	{
		if (storageClass < 0 || storageClass >= kScriptClassCount)
			return nullptr;
		if (storageClass == kScriptClassUnused)
			return nullptr; // the base is never set to anything but 0
		if ((offset & ~Rva::AtelDescOffsetMask) != 0)
			return nullptr;

		// CLASS 0 IS DONE HERE RATHER THAN THROUGH THE ENGINE, deliberately. The
		// resolver needs a live actor for every other class, and demanding one for
		// class 0 too would mean the persistent halves, the chocobo best times and the
		// lightning dodge counters, could only be read while standing on that map. They
		// are in the save block, so they are readable from anywhere.
		//
		// The arithmetic is the resolver's own: base = *(u32 *)(ctx + 0x2C), which is
		// g_ffxSaveData + 0x1EC. The static RVA is both the fallback for no context and
		// a cross-check on the context's copy, and a mismatch means the save block
		// moved, which it never does.
		if (storageClass == kScriptClassSave)
		{
			if (offset >= (DWORD)Rva::AtelScriptWorkBytes)
				return nullptr;

			BYTE* fallback = (BYTE*)ModuleAddress(Rva::AtelScriptWork);
			BYTE* base = fallback;

			BYTE* ctx0 = AtelContext0();
			if (ctx0 && Readable(ctx0 + Rva::AtelCtxClass0Base, sizeof(void*)))
			{
				BYTE* fromCtx = *(BYTE**)(ctx0 + Rva::AtelCtxClass0Base);
				if (fromCtx && fromCtx != fallback)
					return nullptr;
				if (fromCtx)
					base = fromCtx;
			}

			BYTE* p = base + offset;
			return Readable(p, 4) ? (void*)p : nullptr;
		}

		if (actorId < 0)
			actorId = g_varActor;

		void* actor = LiveActor(actorId);
		if (!actor)
			return nullptr;

		if (!AtelContext0())
			return nullptr;

		// A synthetic descriptor. The resolver only reads the class bits and the low 24
		// bits, so the type field can be anything.
		const DWORD desc = MakeDescriptor(0, storageClass, offset);

		const int saved = SelectCtx0();
		char* p = Resolve<ResolveVarFn>(Rva::AtelResolveVarAddress)(actor, desc);
		RestoreCtx(saved);

		if (!LooksLikePointer((DWORD)(UINT_PTR)p))
			return nullptr;
		// Four bytes covers the widest type. Nothing here reads an array past its
		// first element without checking again.
		return Readable(p, 4) ? (void*)p : nullptr;
	}

	bool ReadScriptVar(const ScriptVar* var, int element, double* out)
	{
		if (!var || !out)
			return false;
		if (var->type < 0 || var->type >= ScriptVarTypeCount)
			return false;
		if (element < 0 || element >= var->elements)
			return false;

		const int stride = ScriptVarTypeBytes(var->type);
		BYTE* base = (BYTE*)ScriptVarAddress(var->storageClass, var->offset, -1);
		if (!base)
			return false;

		BYTE* p = base + (size_t)element * (size_t)stride;
		if (!Readable(p, (SIZE_T)stride))
			return false;

		*out = ReadTyped(p, var->type);
		return true;
	}

	bool WriteScriptVar(const ScriptVar* var, int element, double value)
	{
		if (!var)
			return false;
		if (var->type < 0 || var->type >= ScriptVarTypeCount)
			return false;
		if (element < 0 || element >= var->elements)
			return false;

		const int stride = ScriptVarTypeBytes(var->type);
		BYTE* base = (BYTE*)ScriptVarAddress(var->storageClass, var->offset, -1);
		if (!base)
			return false;

		BYTE* p = base + (size_t)element * (size_t)stride;
		if (!Readable(p, (SIZE_T)stride))
			return false;

		WriteTyped(p, var->type, value);
		return true;
	}

	const char* ScriptVarTypeName(int type)
	{
		switch (type)
		{
		case ScriptVarU8: return "u8";
		case ScriptVarS8: return "s8";
		case ScriptVarU16: return "u16";
		case ScriptVarS16: return "s16";
		case ScriptVarU32: return "u32";
		case ScriptVarS32: return "s32";
		case ScriptVarF32: return "f32";
		default: return "?";
		}
	}

	const char* ScriptVarClassName(int storageClass)
	{
		switch (storageClass)
		{
		case kScriptClassSave: return "save block";
		case kScriptClassUnused: return "unused";
		case kScriptClassActor2: return "actor 2";
		case kScriptClassActor3: return "actor 3";
		case kScriptClassActor4: return "actor 4";
		case kScriptClassRegs: return "registers";
		case kScriptClassPackage: return "package";
		default: return "?";
		}
	}

	int ScriptVarTypeBytes(int type)
	{
		switch (type)
		{
		case ScriptVarU8:
		case ScriptVarS8: return 1;
		case ScriptVarU16:
		case ScriptVarS16: return 2;
		case ScriptVarU32:
		case ScriptVarS32:
		case ScriptVarF32: return 4;
		default: return 1;
		}
	}

	bool ScriptVarClassPersistent(int storageClass)
	{
		return storageClass == kScriptClassSave;
	}

	// ---------------------------------------------------------------------------
	// The named fields
	// ---------------------------------------------------------------------------

	int MinigameFieldCount(Minigame which)
	{
		return TableFor(which).count;
	}

	const MinigameField* MinigameFieldAt(Minigame which, int index)
	{
		const FieldTable t = TableFor(which);
		if (index < 0 || index >= t.count)
			return nullptr;
		return &t.rows[index];
	}

	bool ReadMinigameField(const MinigameField* field, double* out)
	{
		if (!field || !out)
			return false;

		const int stride = ScriptVarTypeBytes(field->type);
		BYTE* p = (BYTE*)ScriptVarAddress(field->storageClass, field->offset,
		    field->actorId);
		if (!p || !Readable(p, (SIZE_T)stride))
			return false;

		*out = ReadTyped(p, field->type);
		return true;
	}

	bool WriteMinigameField(const MinigameField* field, double value)
	{
		if (!field)
			return false;

		const int stride = ScriptVarTypeBytes(field->type);
		BYTE* p = (BYTE*)ScriptVarAddress(field->storageClass, field->offset,
		    field->actorId);
		if (!p || !Readable(p, (SIZE_T)stride))
			return false;

		WriteTyped(p, field->type, Clamp(value, field->lo, field->hi));
		return true;
	}

	// ---------------------------------------------------------------------------
	// The cheap levers
	// ---------------------------------------------------------------------------

	bool BlitzCheatEnabled()
	{
		return ReadFlagByte(Rva::BlitzCheatEnabled);
	}

	bool SetBlitzCheatEnabled(bool on)
	{
		return WriteFlagByte(Rva::BlitzCheatEnabled, on);
	}

	bool BlitzUnlockEverything()
	{
		if (!GameLoaded())
			return false;
		Resolve<NoArgFn>(Rva::BlitzDebugFullBlitz)();
		return true;
	}

	bool ChocoboDebugPackage()
	{
		return ReadFlagByte(Rva::ChocoboGameDebugEnable);
	}

	bool SetChocoboDebugPackage(bool on)
	{
		// The byte directly rather than FFX_Debug_StepChocoboGameDebugEnable, which
		// needs four presses to get there.
		return WriteFlagByte(Rva::ChocoboGameDebugEnable, on);
	}

	bool ThunderPlainTreasure()
	{
		const DWORD* p = FlagDword(Rva::ThunderPlainTreasureEnable);
		return p && *p != 0;
	}

	bool SetThunderPlainTreasure(bool on)
	{
		if (!FlagDword(Rva::ThunderPlainTreasureEnable))
			return false;
		Resolve<SetIntFn>(Rva::DebugSetThunderPlainTreasureEnable)(on ? 1 : 0);
		return true;
	}

	bool FullMonsterArena()
	{
		const DWORD* p = FlagDword(Rva::FullNagi0700Enable);
		return p && *p != 0;
	}

	bool SetFullMonsterArena(bool on)
	{
		DWORD* p = FlagDword(Rva::FullNagi0700Enable);
		if (!p)
			return false;
		// The shipped function is a toggle rather than a setter, so it only gets called
		// when the state is actually wrong.
		if ((*p != 0) != on)
			Resolve<NoArgIntFn>(Rva::DebugToggleFullNagi0700)();
		return (*p != 0) == on;
	}

	bool OverdriveAlwaysFull()
	{
		const DWORD* p = FlagDword(Rva::BtlDbgOverdriveAlwaysFull);
		return p && *p != 0;
	}

	bool SetOverdriveAlwaysFull(bool on)
	{
		if (!FlagDword(Rva::BtlDbgOverdriveAlwaysFull))
			return false;
		Resolve<SetIntFn>(Rva::BtlDbgSetOverdriveAlwaysFull)(on ? 1 : 0);
		return true;
	}

	int OverdriveMinigamePhase()
	{
		const DWORD* p = FlagDword(Rva::BtlOdMinigamePhase);
		return p ? (int)*p : -1;
	}

	void LogMinigames()
	{
		char name[64];
		const bool haveName = CurrentPackageName(name, (int)sizeof(name));
		const Minigame which = CurrentMinigame();

		Log("minigames: package %s, matched as %s, map %d, event %d",
		    haveName ? name : "unreadable", MinigameName(which), LiveMapId(),
		    CurrentEventId());

		const int vars = ScriptVarCount();
		const int actors = PackageActorCount();
		Log("minigames: %d script descriptors, %d actors, var actor %d", vars, actors,
		    g_varActor);

		if (vars <= 0)
			Log("minigames: no descriptor table, so the variable browser is empty. That "
			    "is the state with no package loaded");

		const int fields = MinigameFieldCount(which);
		int readable = 0;
		for (int i = 0; i < fields; ++i)
		{
			double v = 0;
			if (ReadMinigameField(MinigameFieldAt(which, i), &v))
				++readable;
		}
		if (fields > 0)
			Log("minigames: %d of %d named fields resolve right now", readable, fields);

		Log("minigames: blitz cheat %d, chocobo debug package %d, thunder treasure %d, "
		    "full arena %d, overdrive always full %d",
		    BlitzCheatEnabled() ? 1 : 0, ChocoboDebugPackage() ? 1 : 0,
		    ThunderPlainTreasure() ? 1 : 0, FullMonsterArena() ? 1 : 0,
		    OverdriveAlwaysFull() ? 1 : 0);

		Log("minigames: scripted battle %s, overdrive minigame phase %d, battle %s",
		    ScriptedBattleAllowed() ? "allowed" : "refused", OverdriveMinigamePhase(),
		    BattleRunning() ? "RUNNING, so the package name above is the field map"
		                    : "not running");
	}

} // namespace ffx
