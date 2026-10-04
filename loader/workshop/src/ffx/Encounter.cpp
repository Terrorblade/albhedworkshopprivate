#include "ffx/Encounter.h"

#include "ffx/AssetPaths.h"
#include "ffx/DataFile.h"
#include "ffx/addresses/WorldState.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Addresses.h"
#include "workshop/Detour.h"
#include "workshop/Log.h"
#include "workshop/HostModule.h"

namespace ffx
{

	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		inline BYTE Rd8(const BYTE* p)
		{
			return *(volatile const BYTE*)p;
		}
		inline DWORD Rd32(const BYTE* p)
		{
			return *(volatile const DWORD*)p;
		}
		inline float RdF(const BYTE* p)
		{
			return *(volatile const float*)p;
		}

		// One guarded read per global, because every one of these is in uninitialised data
		// until a battle has been set up once, and a title-screen panel refresh must not
		// fault.
		bool ReadF(DWORD rva, float* out)
		{
			const BYTE* p = (const BYTE*)ModuleAddress(rva);
			if (!Readable(p, 4))
				return false;

			*out = RdF(p);
			return true;
		}

		bool ReadB(DWORD rva, BYTE* out)
		{
			const BYTE* p = (const BYTE*)ModuleAddress(rva);
			if (!Readable(p, 1))
				return false;

			*out = Rd8(p);
			return true;
		}

		bool ReadD(DWORD rva, DWORD* out)
		{
			const BYTE* p = (const BYTE*)ModuleAddress(rva);
			if (!Readable(p, 4))
				return false;

			*out = Rd32(p);
			return true;
		}

		// ---------------------------------------------------------------------------
		// The one detour.
		//
		// Target FFX_Field_StepRandomEncounter, whose prologue is
		//     55        push ebp
		//     8B EC     mov  ebp, esp
		//     83 EC 10  sub  esp, 10h
		// Six bytes, no branch and no absolute among them, and byte six starts a new
		// instruction (53, push ebx), so the stolen range lands on a boundary.
		// ---------------------------------------------------------------------------
		DETOUR_DECLARE(EncounterCheck, int, (int sceneId, int encZone, float dist));

		DETOUR_PROLOGUE(EncounterCheck) = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10 };

		EncounterDistanceFn g_hook = NULL;
		EncounterHookStats g_stats = { 0, 0.0f, 0.0f };

		int __cdecl EncounterCheckHook(int sceneId, int encZone, float dist)
		{
			float substituted = dist;

			// A null hook is a pass-through rather than an uninstall. Patching live code
			// off again would be a race against the game thread for no benefit, and the
			// cost of the pass-through is one branch per sub-step.
			EncounterDistanceFn hook = g_hook;
			if (hook)
				substituted = hook(dist);

			++g_stats.calls;
			g_stats.lastEngineDistance = dist;
			g_stats.lastReturnedDistance = substituted;

			return DETOUR_ORIGINAL(EncounterCheck)(sceneId, encZone, substituted);
		}

	} // namespace

	bool ReadEncounterState(EncounterState* out)
	{
		if (!out)
			return false;

		memset(out, 0, sizeof(*out));

		if (!ReadF(Rva::EncDistRemain, &out->distRemain))
			return false;
		if (!ReadF(Rva::EncDistTotal, &out->distTotal))
			return false;

		// steps is what the chance curve is actually built on, so deriving it here keeps
		// every caller from repeating the divisor and getting it wrong.
		out->steps = (int)(out->distTotal / 10.0f);

		BYTE enabled = 0;
		BYTE debugOn = 0;
		if (ReadB(Rva::EncountersEnabled, &enabled))
			out->encountersEnabled = enabled != 0;
		if (ReadB(Rva::DebugEncountersOn, &debugOn))
			out->debugEncountersOn = debugOn != 0;

		DWORD rate = 1;
		if (ReadD(Rva::BoosterEncounterRate, &rate))
			out->boosterRate = (int)rate;

		out->valid = true;
		return true;
	}

	bool ReadPendingBattle(PendingBattle* out)
	{
		if (!out)
			return false;

		memset(out, 0, sizeof(*out));

		BYTE kind = 0;
		if (!ReadB(Rva::BattlePendingKind, &kind))
			return false;

		out->kind = (int)kind;
		out->pending = kind != 0;

		// The scene index is in the HIGH word. The low word belongs to something else and
		// is deliberately not reported, because guessing at it here would put a wrong
		// number in a log that somebody later trusts.
		DWORD sceneAndMap = 0;
		if (ReadD(Rva::BattleSceneAndMap, &sceneAndMap))
			out->sceneIndex = (int)((sceneAndMap >> 16) & 0xFFFF);

		BYTE zone = 0;
		BYTE formation = 0;
		if (ReadB(Rva::BattleZoneIndex, &zone))
			out->zoneIndex = (int)zone;
		if (ReadB(Rva::BattleFormationIndex, &formation))
			out->formationIndex = (int)formation;

		out->valid = true;
		return true;
	}

	bool SetEncounterDistanceHook(EncounterDistanceFn hook)
	{
		if (!DETOUR_INSTALLED(EncounterCheck))
		{
			if (!DETOUR_INSTALL(EncounterCheck, Rva::FieldStepRandomEncounter))
			{
				// The prologue did not match, so nothing was patched. Saying so plainly
				// matters: the caller must not go on believing it is steering encounters.
				workshop::Log("encounter: the distance detour did NOT install, so nothing "
				              "is steering the encounter rate");
				return false;
			}
		}

		g_hook = hook;
		return true;
	}

	bool EncounterHookInstalled()
	{
		return DETOUR_INSTALLED(EncounterCheck);
	}

	void ReadEncounterHookStats(EncounterHookStats* out)
	{
		if (out)
			*out = g_stats;
	}

	void LogEncounterState()
	{
		EncounterState state;
		if (!ReadEncounterState(&state))
		{
			workshop::Log("encounter: cannot read the accumulators yet, so no field map has "
			              "been set up");
			return;
		}

		workshop::Log("=== random encounters ===");
		workshop::Log("distance: remain %.2f of the 10.0 a roll needs, total %.2f which is "
		              "%d steps",
		    state.distRemain, state.distTotal, state.steps);

		// Each gate with whether it is currently the one stopping a battle, because "no
		// encounters are happening" has about six possible causes and guessing between
		// them wastes a test run.
		workshop::Log("gates: No Encounters armour ability %s, debug encounters %s, booster "
		              "rate %d (%s)",
		    state.encountersEnabled ? "not active" : "ACTIVE, blocking",
		    state.debugEncountersOn ? "on" : "off", state.boosterRate,
		    state.boosterRate == 0   ? "off, distance is zeroed"
		    : state.boosterRate == 2 ? "high, distance multiplied by 10"
		                             : "normal");

		PendingBattle battle;
		if (ReadPendingBattle(&battle) && battle.pending)
			workshop::Log("A BATTLE IS PENDING: kind %d, scene %d, zone %d, formation %d",
			    battle.kind, battle.sceneIndex, battle.zoneIndex, battle.formationIndex);
		else
			workshop::Log("no battle pending");

		if (!EncounterHookInstalled())
		{
			workshop::Log("the distance detour is NOT installed, so remote movement cannot "
			              "contribute");
			return;
		}

		workshop::Log("distance detour installed, consulted %u times, last saw %.3f and "
		              "returned %.3f%s",
		    (unsigned)g_stats.calls, g_stats.lastEngineDistance,
		    g_stats.lastReturnedDistance,
		    g_hook ? "" : " (no hook set, so it is a pass-through)");

		if (g_stats.calls == 0)
			workshop::Log("zero calls means the engine is not reaching the check at all, "
			              "which is a different problem from the hook being wrong. The "
			              "caller is FFX_Atel_StepFieldFrame, so no field map is running.");
	}

	// ---------------------------------------------------------------------------
	// The encounter table, enumerated from memory
	// ---------------------------------------------------------------------------

	namespace
	{

		typedef int(__cdecl* FindSceneFn)(int mapId);
		typedef int(__cdecl* GetMapEntryFn)(int sceneIndex);
		typedef int(__cdecl* GetZoneTableFn)(int sceneIndex);
		typedef BYTE*(__cdecl* GetZoneRecordFn)(int sceneIndex, int zoneIndex);

		template <class T>
		T At(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		// The scene count, 0 when btl.bin has not been loaded yet. Everything else is
		// gated on this, because the two table pointers are null until then and the
		// engine's own getters clamp an out of range index to 0 rather than refusing,
		// which would hand back garbage that looks like scene 0.
		int SceneCount()
		{
			// A SIGNED WORD, NOT AN INT. The engine writes it with a 16-bit store and
			// reads it back with movsx, and EncTableSize sits in the next two bytes, so
			// a four byte read here gives a count in the tens of millions.
			const short* count = (const short*)ModuleAddress(Rva::EncMapCount);
			if (!Readable(count, 2))
				return 0;

			const int n = (int)*count;
			if (n <= 0 || n > 4096)
				return 0;

			// The blob is one 4096-byte file, so both bases have to be live before any
			// of the getters mean anything.
			const DWORD* mapTable = (const DWORD*)ModuleAddress(Rva::EncMapTable);
			const DWORD* zoneBlob = (const DWORD*)ModuleAddress(Rva::EncZoneBlob);
			if (!Readable(mapTable, 4) || !Readable(zoneBlob, 4))
				return 0;
			if (!workshop::LooksLikePointer(*mapTable)
			    || !workshop::LooksLikePointer(*zoneBlob))
				return 0;

			return n;
		}

		const BYTE* MapEntry(int sceneIndex)
		{
			if (sceneIndex < 0 || sceneIndex >= SceneCount())
				return NULL;

			const BYTE* entry
			    = (const BYTE*)(UINT_PTR)At<GetMapEntryFn>(Rva::BtlGetEncounterMapEntry)(
			        sceneIndex);
			if (!Readable(entry, Rva::EncMapEntryBytes))
				return NULL;
			return entry;
		}

		const BYTE* ZoneTable(int sceneIndex)
		{
			if (sceneIndex < 0 || sceneIndex >= SceneCount())
				return NULL;

			const BYTE* table
			    = (const BYTE*)(UINT_PTR)At<GetZoneTableFn>(Rva::BtlGetEncounterZoneTable)(
			        sceneIndex);
			if (!Readable(table, 2))
				return NULL;
			return table;
		}

		int ZoneCountOf(int sceneIndex)
		{
			const BYTE* table = ZoneTable(sceneIndex);
			if (!table)
				return 0;
			return (int)Rd8(table + Rva::EncZoneTableCountOff);
		}

		// The zone record, with the length its own formation count implies. The engine's
		// getter clamps an out of range zone to 0, so the bound is checked here instead,
		// otherwise zone 99 of a one-zone scene would report zone 0's fights.
		const BYTE* ZoneRecord(int sceneIndex, int zoneIndex)
		{
			const int zones = ZoneCountOf(sceneIndex);
			if (zoneIndex < 0 || zones <= 0 || zoneIndex >= zones)
				return NULL;

			const BYTE* rec = At<GetZoneRecordFn>(Rva::BtlGetEncounterZoneRecord)(
			    sceneIndex, zoneIndex);
			if (!Readable(rec, Rva::EncZoneRecFirstEntry))
				return NULL;

			const int formations = (int)Rd8(rec + Rva::EncZoneRecFormations);
			const SIZE_T bytes = (SIZE_T)Rva::EncZoneRecFirstEntry
			    + (SIZE_T)formations * (SIZE_T)Rva::EncZoneRecEntryStride;
			if (!Readable(rec, bytes))
				return NULL;
			return rec;
		}

	} // namespace

	int EncounterSceneCount()
	{
		return SceneCount();
	}

	bool EncounterSceneAt(int sceneIndex, EncounterScene* out)
	{
		if (!out)
			return false;

		const BYTE* entry = MapEntry(sceneIndex);
		if (!entry)
			return false;

		out->sceneIndex = sceneIndex;

		// Signed, because the engine reads it with movsx. Every shipped map id is small
		// and positive, so this is about matching the engine rather than about a value
		// anyone expects to see negative.
		out->mapId = (int)*(const short*)(entry + Rva::EncMapEntryMapId);
		out->zoneCount = ZoneCountOf(sceneIndex);

		// The name is a fixed 8 bytes with no terminator, so it is copied rather than
		// pointed at, and anything unprintable is dropped so a half loaded table reads
		// as unnamed instead of as mojibake.
		int n = 0;
		for (; n < Rva::EncMapEntrySceneNameBytes; ++n)
		{
			const char c = (char)entry[Rva::EncMapEntrySceneName + n];
			if (c == 0)
				break;
			if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E)
			{
				n = 0;
				break;
			}
			out->name[n] = c;
		}
		out->name[n] = 0;
		return true;
	}

	int EncounterSceneForMap(int mapId)
	{
		if (SceneCount() <= 0)
			return -1;

		// The engine's search tests "mapId > 0" before it looks at anything, so map id
		// 0 can never resolve through it. Saying so here beats returning -1 and letting
		// a caller think no scene declares it.
		if (mapId <= 0)
			return -1;
		// The engine's own search, so a caller gets the same answer the battle request
		// will get rather than a second implementation of it.
		return At<FindSceneFn>(Rva::BtlFindEncounterSceneByMapId)(mapId);
	}

	bool EncounterZoneAt(int sceneIndex, int zoneIndex, EncounterZone* out)
	{
		if (!out)
			return false;

		const BYTE* rec = ZoneRecord(sceneIndex, zoneIndex);
		if (!rec)
			return false;

		out->zoneIndex = zoneIndex;
		out->rate = (int)Rd8(rec + Rva::EncZoneRecRate);
		out->weightTotal = (int)Rd8(rec + Rva::EncZoneRecWeightTotal);
		out->formationCount = (int)Rd8(rec + Rva::EncZoneRecFormations);
		return true;
	}

	bool EncounterFormationAt(int sceneIndex, int zoneIndex, int slot, int* outEncounterId,
	    int* outWeight)
	{
		const BYTE* rec = ZoneRecord(sceneIndex, zoneIndex);
		if (!rec)
			return false;

		const int formations = (int)Rd8(rec + Rva::EncZoneRecFormations);
		if (slot < 0 || slot >= formations)
			return false;

		const BYTE* p = rec + Rva::EncZoneRecFirstEntry
		    + (SIZE_T)slot * (SIZE_T)Rva::EncZoneRecEntryStride;

		// The encounter id is ONE BYTE. ResolveBattleId masks the low word of the battle
		// id but compares it against a u8 read, so an id above 255 can never match.
		if (outEncounterId)
			*outEncounterId = (int)Rd8(p);
		if (outWeight)
			*outWeight = (int)(Rd8(p + 1) >> 4);
		return true;
	}

	int EncounterFormationTotal()
	{
		const int scenes = SceneCount();
		int total = 0;
		for (int scene = 0; scene < scenes; ++scene)
		{
			const int zones = ZoneCountOf(scene);
			for (int zone = 0; zone < zones; ++zone)
			{
				EncounterZone z;
				if (EncounterZoneAt(scene, zone, &z))
					total += z.formationCount;
			}
		}
		return total;
	}

	void LogEncounterTable()
	{
		const int scenes = SceneCount();
		if (scenes <= 0)
		{
			workshop::Log("encounter table: not loaded. btl.bin is read at the first "
			              "battle init, so this is the state until one battle has "
			              "started, and it is not an error at the title screen.");
			return;
		}

		workshop::Log("encounter table: %d scenes, %d fights in total", scenes,
		    EncounterFormationTotal());

		for (int scene = 0; scene < scenes; ++scene)
		{
			EncounterScene s;
			if (!EncounterSceneAt(scene, &s))
			{
				workshop::Log("  scene %d is not readable", scene);
				continue;
			}

			char fights[256];
			int used = 0;
			fights[0] = 0;

			for (int zone = 0; zone < s.zoneCount; ++zone)
			{
				EncounterZone z;
				if (!EncounterZoneAt(scene, zone, &z))
					continue;

				for (int slot = 0; slot < z.formationCount; ++slot)
				{
					int encId = 0;
					if (!EncounterFormationAt(scene, zone, slot, &encId, NULL))
						continue;
					const int room = (int)sizeof(fights) - used;
					if (room < 8)
						break;
					used += _snprintf_s(fights + used, (size_t)room, _TRUNCATE, "%s%d",
					    used ? " " : "", encId);
				}
			}

			workshop::Log("  scene %2d map %4d %-8s %d zone%s, fights: %s", s.sceneIndex,
			    s.mapId, s.name[0] ? s.name : "(unnamed)", s.zoneCount,
			    s.zoneCount == 1 ? "" : "s", fights[0] ? fights : "none");
		}
	}

	// ---------------------------------------------------------------------------
	// Naming a fight by its monsters
	// ---------------------------------------------------------------------------

	namespace
	{

		typedef char*(__cdecl* ResolvePathFn)(int pathIndex);

		// The monster ids out of an already-located monster section. Shared by the
		// archive route and the live route, because the 16 bytes are laid out the same
		// either way, which is the whole reason the live pointer is worth having.
		bool ReadMonsterSection(const BYTE* section, EncounterMonsters* out)
		{
			const SIZE_T need = (SIZE_T)Rva::BtlFieldMonsterFirstId
			    + (SIZE_T)Rva::BtlFieldMonsterSlots * (SIZE_T)Rva::BtlFieldMonsterEntryBytes;
			if (!section || !Readable(section, need))
				return false;

			out->count = 0;
			for (int i = 0; i < Rva::BtlFieldMonsterSlots; ++i)
			{
				const BYTE* p = section + Rva::BtlFieldMonsterFirstId
				    + (SIZE_T)i * (SIZE_T)Rva::BtlFieldMonsterEntryBytes;

				const WORD tagged = (WORD)(p[0] | ((WORD)p[1] << 8));
				if (tagged == Rva::BtlFieldMonsterEmpty)
					continue;

				out->ids[out->count++] = (int)(tagged & Rva::BtlFieldMonsterIdMask);
			}
			return true;
		}

		// cdrom.fid's entry for a kind, or -1. A pointer slot, so the pointer is checked
		// before it is indexed.
		int KindPathBase(int kind)
		{
			if (kind < 0 || kind >= 65)
				return -1;

			const DWORD* slot = (const DWORD*)ModuleAddress(Rva::AssetKindBaseTable);
			if (!Readable(slot, 4) || !workshop::LooksLikePointer(*slot))
				return -1;

			const short* table = (const short*)(UINT_PTR)*slot;
			if (!Readable(table + kind, 2))
				return -1;

			return (int)table[kind];
		}

	} // namespace

	int EncounterFieldAssetIndex(int sceneIndex, int zoneIndex, int slot)
	{
		const BYTE* entry = MapEntry(sceneIndex);
		if (!entry)
			return -1;

		// The slot has to name a real fight, otherwise this would hand back the index of
		// whatever happens to sit after this zone's run.
		int encId = 0;
		if (!EncounterFormationAt(sceneIndex, zoneIndex, slot, &encId, NULL))
			return -1;

		const int base = (int)*(const short*)(entry + Rva::EncMapEntryFieldAssetBase);
		if (base < 0)
			return -1;

		// The index runs flat across the scene's zones, so every formation in every
		// earlier zone counts.
		int before = 0;
		for (int z = 0; z < zoneIndex; ++z)
		{
			EncounterZone zone;
			if (!EncounterZoneAt(sceneIndex, z, &zone))
				return -1;
			before += zone.formationCount;
		}

		return base + before + slot;
	}

	bool EncounterFieldPath(int sceneIndex, int zoneIndex, int slot, char* out, int outBytes)
	{
		if (!out || outBytes < 2)
			return false;
		out[0] = 0;

		const int fieldIndex = EncounterFieldAssetIndex(sceneIndex, zoneIndex, slot);
		if (fieldIndex < 0)
			return false;

		// The resolver dereferences the path table after a null test that 0xFFFFFFFF
		// passes, so this is a precondition and not a nicety.
		if (!AssetLoaderReady())
			return false;

		const int kindBase = KindPathBase(Rva::AssetKindBattleField);
		if (kindBase < 0)
			return false;

		// AssetResolvePath, NOT loader slot 22. This one reads the path table and
		// nothing else, so unlike the selector it needs no kind selection and leaves no
		// shared state to put back.
		ResolvePathFn resolve = At<ResolvePathFn>(Rva::AssetResolvePath);
		if (!Readable((void*)resolve, 1))
			return false;

		const char* raw = resolve(kindBase + fieldIndex);
		if (!raw || !Readable(raw, 1) || !raw[0])
			return false;

		// "host0:/ffx/master/..." becomes "ffx_ps2/ffx/master/...". The prefix looks
		// like a devkit loose file path and is not: FFX_File_OpenHost0 strips it and
		// prepends the archive root, so this does the same thing to the same end.
		static const char kHost0[] = "host0:/";
		static const char kRoot[] = "ffx_ps2/";
		const int hostLen = (int)sizeof(kHost0) - 1;

		const char* tail = raw;
		if (strncmp(raw, kHost0, (size_t)hostLen) == 0)
			tail = raw + hostLen;

		// A path that still does not start at the archive root is one of the
		// developers' own, "host0:/home/$USER$/battle/jp/btl/output.bin" being the only
		// one in the shipped table. Passed through as it is, so the caller sees what it
		// actually was rather than a silently mangled version.
		if (tail == raw)
			_snprintf_s(out, (size_t)outBytes, _TRUNCATE, "%s", raw);
		else
			_snprintf_s(out, (size_t)outBytes, _TRUNCATE, "%s%s", kRoot, tail);

		return out[0] != 0;
	}

	bool EncounterMonstersAt(int sceneIndex, int zoneIndex, int slot, EncounterMonsters* out)
	{
		if (!out)
			return false;
		out->count = 0;

		char path[272];
		if (!EncounterFieldPath(sceneIndex, zoneIndex, slot, path, (int)sizeof(path)))
			return false;

		int bytes = 0;
		void* file = ReadDataFile(path, &bytes);
		if (!file)
			return false;

		bool ok = false;
		const BYTE* base = (const BYTE*)file;

		// Four section offsets at +4, and the third of them is the monster section.
		// Every one is relative to the file base.
		if (bytes > Rva::BtlFieldMonsterSectionOff + 4)
		{
			const DWORD at = *(const DWORD*)(base + Rva::BtlFieldMonsterSectionOff);
			const SIZE_T need = (SIZE_T)Rva::BtlFieldMonsterFirstId
			    + (SIZE_T)Rva::BtlFieldMonsterSlots * (SIZE_T)Rva::BtlFieldMonsterEntryBytes;

			if (at < (DWORD)bytes && (SIZE_T)(bytes - at) >= need)
				ok = ReadMonsterSection(base + at, out);
		}

		FreeDataFile(file);
		return ok;
	}

	bool LiveEncounterMonsters(EncounterMonsters* out)
	{
		if (!out)
			return false;
		out->count = 0;

		const DWORD* slot = (const DWORD*)ModuleAddress(Rva::BtlFieldMonsterSection);
		if (!Readable(slot, 4) || !workshop::LooksLikePointer(*slot))
			return false;

		return ReadMonsterSection((const BYTE*)(UINT_PTR)*slot, out);
	}

} // namespace ffx
