#include "ffx/AssetPaths.h"

#include "ffx/DataFile.h"
#include "ffx/GameLists.h"

#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ffx
{

	using namespace workshop;

	namespace
	{

		typedef char*(__cdecl* ResolvePathFn)(int pathIndex);
		typedef int(__cdecl* RegisterSubstFn)(const char* key, const char* value);
		typedef void(__cdecl* UnregisterSubstFn)(int slot);

		template <class T>
		T At(DWORD rva)
		{
			return (T)ModuleAddress(rva);
		}

		// The table itself, or null before it loads. Layout is u32 count, then count+1
		// u32 byte offsets RELATIVE TO THE TABLE BASE, then the string blob.
		BYTE* Table()
		{
			const DWORD* slot = (const DWORD*)ModuleAddress(Rva::AssetPathTable);
			if (!Readable(slot, 4) || !LooksLikePointer(*slot))
				return nullptr;

			BYTE* base = (BYTE*)(UINT_PTR)*slot;
			if (!Readable(base, 8))
				return nullptr;
			return base;
		}

		DWORD* Offsets(BYTE* table)
		{
			return (DWORD*)(table + 4);
		}

		int CountOf(BYTE* table)
		{
			const DWORD n = *(const DWORD*)table;

			// 16,305 on the shipped archive. A count past this is a wrong pointer rather
			// than a big table, and believing it would walk off the end of the block.
			if (n == 0 || n > 1000000u)
				return 0;
			return (int)n;
		}

		// How far into the block the blob reaches, which is where a new string goes.
		DWORD BlobEnd(BYTE* table, int count)
		{
			return Offsets(table)[count];
		}

		// ---------------------------------------------------------------------------
		// The kit's own copy of the table, allocated once with room to spare.
		//
		// Growing it later would mean deciding when the engine has stopped reading the
		// previous block, and there is no way to know that, so the arena is fixed at
		// first use and the original 811 KB is never freed.
		// ---------------------------------------------------------------------------

		const int kPathArenaBytes = 16 * 1024;

		BYTE* g_ours = nullptr;
		int g_oursBytes = 0;
		int g_added = 0;

		// Copies the live table into a kit-owned block with kPathArenaBytes of slack
		// past the blob, and repoints loader slot 12 at it. One aligned dword store, so
		// it is atomic on x86 and needs no lock, but it still has to be the game thread
		// because the engine may be reading the old block.
		bool TakeOwnership()
		{
			if (g_ours)
				return true;

			BYTE* live = Table();
			if (!live)
				return false;

			const int count = CountOf(live);
			if (count <= 0)
				return false;

			const DWORD end = BlobEnd(live, count);
			const int header = 4 + 4 * (count + 1);
			if (end < (DWORD)header)
				return false;

			if (!Readable(live, end))
				return false;

			const int bytes = (int)end + kPathArenaBytes;
			BYTE* mine = (BYTE*)malloc((size_t)bytes);
			if (!mine)
				return false;

			memcpy(mine, live, (size_t)end);
			memset(mine + end, 0, (size_t)kPathArenaBytes);

			DWORD* slot = (DWORD*)ModuleAddress(Rva::AssetPathTable);
			if (!Readable(slot, 4))
			{
				free(mine);
				return false;
			}

			*slot = (DWORD)(UINT_PTR)mine;

			g_ours = mine;
			g_oursBytes = bytes;

			Log("asset paths: took ownership of the path table. %d paths, %u bytes copied, "
			    "%d bytes of room for new ones. The engine's original block is left "
			    "allocated on purpose.",
			    count, (unsigned)end, kPathArenaBytes);
			return true;
		}

		// ---------------------------------------------------------------------------
		// The substitution key and value storage.
		//
		// The engine KEEPS THE POINTERS it is given, so a caller passing a stack buffer
		// would leave the table pointing at dead bytes. Everything is copied here.
		// ---------------------------------------------------------------------------

		const int kSubstTextBytes = 160;

		char g_substKeys[kAssetSubstitutionSlots][kSubstTextBytes];
		char g_substValues[kAssetSubstitutionSlots][kSubstTextBytes];

		const char* const* SubstKeyTable()
		{
			const char* const* keys
			    = (const char* const*)ModuleAddress(Rva::AssetPathSubstKeys);
			return Readable(keys, 4 * kAssetSubstitutionSlots) ? keys : nullptr;
		}

		const char* const* SubstValueTable()
		{
			const char* const* values
			    = (const char* const*)ModuleAddress(Rva::AssetPathSubstValues);
			return Readable(values, 4 * kAssetSubstitutionSlots) ? values : nullptr;
		}

		// The shipped layout for an event package, which every one of the 397 .ebp in
		// the archive follows: the two letter group is the first two characters of the
		// name and the directory is the name itself. The 20 exceptions are all PREFIXED
		// variants, where the directory is the BASE name and only the file carries the
		// prefix, which is what kVariantPrefixes below is for.
		const char* const kEventObjRoot = "ffx/master/jppc/event/obj";

		// The prefixes seen in the archive. "dbg_" and "full_" and "200thunder_" are
		// already reachable through the three debug flags the resolver honours, so they
		// are here for completeness rather than because they are needed.
		const char* const kVariantPrefixes[] = { "cn_", "psv_", "psvcn_", "dbg_", "full_" };
		const int kVariantPrefixCount
		    = (int)(sizeof(kVariantPrefixes) / sizeof(kVariantPrefixes[0]));

		// Builds the archive form, which is what ffx::DataFileExists and ReadDataFile
		// want, as opposed to the "host0:/" form the table stores.
		bool ArchiveFormPath(const char* group, const char* dir, const char* stem,
		    char* out, int outBytes)
		{
			return _snprintf_s(out, (size_t)outBytes, _TRUNCATE,
			           "ffx_ps2/%s/%s/%s/%s.ebp", kEventObjRoot, group, dir, stem)
			    > 0;
		}

		// And the table form, which is what goes into the path table. The first seven
		// bytes are discarded blindly rather than matched, so "host0:/" is a convention
		// and not a requirement, but everything else in the table uses it.
		bool TableFormPath(const char* group, const char* dir, const char* stem, char* out,
		    int outBytes)
		{
			return _snprintf_s(out, (size_t)outBytes, _TRUNCATE,
			           "host0:/%s/%s/%s/%s.ebp", kEventObjRoot, group, dir, stem)
			    > 0;
		}

		// The two things FFX_Ev_LoadEventPackage will do to this string with no null
		// check of its own.
		bool PathHasEventObjShape(const char* path)
		{
			const char* marker = strstr(path, "/event/obj/");
			if (!marker)
				return false;
			return strchr(marker + 11, '/') != nullptr;
		}

	} // namespace

	bool AssetLoaderReady()
	{
		// Slot 11, the s16[65] of per-kind base indices, which is what the selector
		// indexes without checking.
		const DWORD* kinds = (const DWORD*)ModuleAddress(Rva::AssetKindBaseTable);
		if (!Readable(kinds, 4) || !LooksLikePointer(*kinds))
			return false;
		if (!Readable((const void*)(UINT_PTR)*kinds, 2 * 65))
			return false;

		// Slot 12, the path table. The resolver null tests it and then reads the count
		// at +0, and 0xFFFFFFFF passes a null test.
		const DWORD* paths = (const DWORD*)ModuleAddress(Rva::AssetPathTable);
		if (!Readable(paths, 4) || !LooksLikePointer(*paths))
			return false;
		if (!Readable((const void*)(UINT_PTR)*paths, 8))
			return false;

		return true;
	}

	// -------------------------------------------------------------------------------
	// Reading
	// -------------------------------------------------------------------------------

	int AssetPathCount()
	{
		BYTE* table = Table();
		return table ? CountOf(table) : 0;
	}

	bool AssetPathAt(int index, char* out, int outBytes)
	{
		if (!out || outBytes < 2)
			return false;
		out[0] = 0;

		BYTE* table = Table();
		if (!table)
			return false;

		const int count = CountOf(table);
		if (index < 0 || index >= count)
			return false;

		const DWORD* offs = Offsets(table);
		const DWORD from = offs[index];
		const DWORD to = offs[index + 1];

		// A zero length slot is the table's own "no path", and the engine's test is
		// signed, so a slot whose end is before its start reads as empty too.
		if ((int)(to - from) <= 0)
			return false;

		if (!Readable(table + from, 1))
			return false;

		_snprintf_s(out, (size_t)outBytes, _TRUNCATE, "%s", (const char*)(table + from));
		return out[0] != 0;
	}

	int AssetKindBase(int kind)
	{
		if (kind < 0 || kind >= 65)
			return -1;

		const DWORD* slot = (const DWORD*)ModuleAddress(Rva::AssetKindBaseTable);
		if (!Readable(slot, 4) || !LooksLikePointer(*slot))
			return -1;

		const short* table = (const short*)(UINT_PTR)*slot;
		if (!Readable(table + kind, 2))
			return -1;

		return (int)table[kind];
	}

	bool ResolveAssetPath(int kind, int subIndex, char* out, int outBytes)
	{
		if (!out || outBytes < 2)
			return false;
		out[0] = 0;

		if (!AssetLoaderReady())
			return false;

		const int base = AssetKindBase(kind);
		if (base < 0 || subIndex < 0)
			return false;

		// The resolver, not loader slot 22. This one reads the path table and nothing
		// else, so it needs no kind selection and leaves no shared state to put back.
		ResolvePathFn resolve = At<ResolvePathFn>(Rva::AssetResolvePath);
		if (!Readable((void*)resolve, 1))
			return false;

		const char* raw = resolve(base + subIndex);
		if (!raw || !Readable(raw, 1) || !raw[0])
			return false;

		_snprintf_s(out, (size_t)outBytes, _TRUNCATE, "%s", raw);
		return out[0] != 0;
	}

	bool EventPackagePath(int eventId, char* out, int outBytes)
	{
		if (eventId < 0 || eventId >= Rva::EventIdCount)
			return false;
		return ResolveAssetPath(Rva::AssetKindEventObj, Rva::AssetEventStride * eventId,
		    out, outBytes);
	}

	// -------------------------------------------------------------------------------
	// Substitutions
	// -------------------------------------------------------------------------------

	int AssetSubstitutionCount()
	{
		const char* const* keys = SubstKeyTable();
		if (!keys)
			return 0;

		int used = 0;
		for (int i = 0; i < kAssetSubstitutionSlots; ++i)
			if (keys[i])
				++used;
		return used;
	}

	bool AssetSubstitutionAt(int slot, const char** outKey, const char** outValue)
	{
		if (slot < 0 || slot >= kAssetSubstitutionSlots)
			return false;

		const char* const* keys = SubstKeyTable();
		const char* const* values = SubstValueTable();
		if (!keys || !values || !keys[slot])
			return false;

		if (outKey)
			*outKey = Readable(keys[slot], 1) ? keys[slot] : "(unreadable)";
		if (outValue)
			*outValue = (values[slot] && Readable(values[slot], 1)) ? values[slot] : "";
		return true;
	}

	int RegisterAssetSubstitution(const char* key, const char* value)
	{
		if (!key || !key[0] || !value)
			return -1;

		if ((int)strlen(key) >= kSubstTextBytes || (int)strlen(value) >= kSubstTextBytes)
		{
			Log("asset paths: refusing a substitution longer than %d bytes",
			    kSubstTextBytes - 1);
			return -1;
		}

		const char* const* keys = SubstKeyTable();
		if (!keys)
			return -1;

		// THE REFUSAL IS THE POINT. The engine's own register asserts on a full table
		// and then writes keys[16] anyway, and keys[16] is values[0], so a 17th
		// registration silently destroys the first substitution's value. Find the slot
		// here so that never happens.
		int free = -1;
		for (int i = 0; i < kAssetSubstitutionSlots; ++i)
			if (!keys[i])
			{
				free = i;
				break;
			}

		if (free < 0)
		{
			Log("asset paths: all %d substitution slots are taken, refusing. Registering "
			    "anyway would overwrite slot 0's value, because the engine's keys[16] IS "
			    "values[0].",
			    kAssetSubstitutionSlots);
			return -1;
		}

		// Copied, because the engine keeps the pointer and the caller's string may not
		// outlive the call.
		_snprintf_s(g_substKeys[free], kSubstTextBytes, _TRUNCATE, "%s", key);
		_snprintf_s(g_substValues[free], kSubstTextBytes, _TRUNCATE, "%s", value);

		RegisterSubstFn reg = At<RegisterSubstFn>(Rva::AssetRegisterPathSubstitution);
		if (!Readable((void*)reg, 1))
			return -1;

		const int slot = reg(g_substKeys[free], g_substValues[free]);

		Log("asset paths: substitution slot %d, \"%s\" -> \"%s\". Applies to EVERY asset "
		    "kind, not just events.",
		    slot, g_substKeys[free], g_substValues[free]);
		return slot;
	}

	bool UnregisterAssetSubstitution(int slot)
	{
		if (slot < 0 || slot >= kAssetSubstitutionSlots)
			return false;

		UnregisterSubstFn unreg
		    = At<UnregisterSubstFn>(Rva::AssetUnregisterPathSubstitution);
		if (!Readable((void*)unreg, 1))
			return false;

		unreg(slot);
		return true;
	}

	// -------------------------------------------------------------------------------
	// Filling an empty slot
	// -------------------------------------------------------------------------------

	bool EventPackagePathSlotEmpty(int eventId)
	{
		if (eventId < 0 || eventId >= Rva::EventIdCount)
			return false;

		const int base = AssetKindBase(Rva::AssetKindEventObj);
		if (base < 0)
			return false;

		char path[272];
		return !AssetPathAt(base + Rva::AssetEventStride * eventId, path,
		    (int)sizeof(path));
	}

	bool AddEventPackagePath(int eventId, const char* path)
	{
		if (eventId < 0 || eventId >= Rva::EventIdCount || !path || !path[0])
			return false;

		const int len = (int)strlen(path);
		if (len >= Rva::AssetRomReadPathMaxBytes)
		{
			Log("asset paths: refusing a %d byte path for event %d. The read queue holds "
			    "%d bytes per slot and the copy stops at the NUL, so a longer path is "
			    "stored with no terminator and runs into the next slot.",
			    len, eventId, Rva::AssetRomReadPathMaxBytes);
			return false;
		}

		if (!PathHasEventObjShape(path))
		{
			Log("asset paths: refusing \"%s\" for event %d. An event path must contain "
			    "\"/event/obj/\" and at least one \"/\" after it, because "
			    "FFX_Ev_LoadEventPackage does strstr then strrchr with no null check and "
			    "faults otherwise.",
			    path, eventId);
			return false;
		}

		if (!EventPackagePathSlotEmpty(eventId))
		{
			Log("asset paths: event %d already has a path, refusing to overwrite it. "
			    "Redirecting an id that has one is what RegisterAssetSubstitution is for.",
			    eventId);
			return false;
		}

		if (!TakeOwnership())
		{
			Log("asset paths: could not take ownership of the path table, so event %d was "
			    "not given a path. The table is loaded during boot, so this is the state "
			    "until the game's file system is up.",
			    eventId);
			return false;
		}

		const int kindBase = AssetKindBase(Rva::AssetKindEventObj);
		if (kindBase < 0)
			return false;

		const int index = kindBase + Rva::AssetEventStride * eventId;
		const int count = CountOf(g_ours);
		if (index < 0 || index >= count)
			return false;

		DWORD* offs = Offsets(g_ours);
		const DWORD at = offs[index];
		const DWORD end = BlobEnd(g_ours, count);
		const DWORD grow = (DWORD)len + 1;

		if (end + grow > (DWORD)g_oursBytes)
		{
			Log("asset paths: no room left. %d bytes were set aside for new paths and "
			    "they are used up, so event %d was not given one. The arena is fixed at "
			    "first use because growing it would mean deciding when the engine has "
			    "stopped reading the old block.",
			    kPathArenaBytes, eventId);
			return false;
		}

		if (at > end)
			return false;

		// INSERT AND SHIFT, which is the only version that does not disturb a
		// neighbour. The offset boundaries are shared, offs[i+1] being both slot i's end
		// and slot i+1's start, so the two-dword version inflates the PRECEDING slot's
		// length. That is harmless when it held a path and produces a bogus one when it
		// did not, and 40 of the 54 empty event ids are preceded by an empty slot.
		memmove(g_ours + at + grow, g_ours + at, (size_t)(end - at));
		memcpy(g_ours + at, path, (size_t)grow);

		// Every boundary after this slot moves by the same amount, which leaves the
		// offsets monotonic and changes no length except this one.
		for (int j = index + 1; j <= count; ++j)
			offs[j] += grow;

		++g_added;

		Log("asset paths: event %d now resolves to \"%s\". Slot %d, %u bytes of arena "
		    "left. NOTHING HAS LOADED IT YET, so whether an unfinished package boots is "
		    "a separate question.",
		    eventId, path, index,
		    (unsigned)((DWORD)g_oursBytes - BlobEnd(g_ours, count)));
		return true;
	}

	int AddedAssetPathCount()
	{
		return g_added;
	}

	int AddedAssetPathBytesFree()
	{
		if (!g_ours)
			return kPathArenaBytes;

		const int count = CountOf(g_ours);
		if (count <= 0)
			return 0;

		return g_oursBytes - (int)BlobEnd(g_ours, count);
	}

	// -------------------------------------------------------------------------------

	void LogAssetPaths()
	{
		const int count = AssetPathCount();
		if (count <= 0)
		{
			Log("asset paths: the table is not loaded. It is read during boot, so this is "
			    "the state until the file system is up and it is not an error in DllMain.");
			return;
		}

		Log("asset paths: %d paths, %s, %d bytes free for new ones", count,
		    g_ours ? "kit owned" : "the engine's own block", AddedAssetPathBytesFree());

		Log("asset paths: kind %d events base %d, kind %d battle fields base %d",
		    Rva::AssetKindEventObj, AssetKindBase(Rva::AssetKindEventObj),
		    Rva::AssetKindBattleField, AssetKindBase(Rva::AssetKindBattleField));

		// The empty event slots, which is the budget for new content.
		char list[256];
		int used = 0;
		int empty = 0;
		list[0] = 0;

		for (int id = 0; id < Rva::EventIdCount; ++id)
		{
			if (!EventPackagePathSlotEmpty(id))
				continue;

			++empty;
			const int room = (int)sizeof(list) - used;
			if (room > 8)
				used += _snprintf_s(list + used, (size_t)room, _TRUNCATE, "%s%d",
				    used ? " " : "", id);
		}

		Log("asset paths: %d of the %d event ids have no path, which is the budget for "
		    "new packages: %s",
		    empty, Rva::EventIdCount, list[0] ? list : "none");

		const int subst = AssetSubstitutionCount();
		Log("asset paths: %d of %d substitution slots used, %d free", subst,
		    kAssetSubstitutionSlots, kAssetSubstitutionSlots - subst);

		for (int i = 0; i < kAssetSubstitutionSlots; ++i)
		{
			const char* key = nullptr;
			const char* value = nullptr;
			if (AssetSubstitutionAt(i, &key, &value))
				Log("asset paths:   slot %d, \"%s\" -> \"%s\"", i, key, value);
		}
	}

	// -------------------------------------------------------------------------------
	// Deriving the orphan packages
	// -------------------------------------------------------------------------------

	bool EventIdIsBootScene(int eventId)
	{
		// 393 through 399. maybe_FFX_IsTitleOrBootMode sends these to the seven
		// hardcoded boot paths before the table is consulted at all.
		return eventId >= 393 && eventId <= 399;
	}

	bool DeriveEventPackagePath(int eventId, char* out, int outBytes)
	{
		if (!out || outBytes < 2)
			return false;
		out[0] = 0;

		if (eventId < 0 || eventId >= Rva::EventIdCount)
			return false;

		// The name is the whole derivation, and a stock boot never reads the table it
		// comes from.
		char name[16];
		if (!EventNameForId(eventId, name, (int)sizeof(name)) || !name[0] || !name[1])
			return false;

		char group[3];
		group[0] = name[0];
		group[1] = name[1];
		group[2] = 0;

		// Asked of the archive rather than assumed. 46 of the 54 empty ids answer yes
		// and the other 8 are packages that simply are not there.
		char archive[160];
		if (!ArchiveFormPath(group, name, name, archive, (int)sizeof(archive)))
			return false;
		if (!DataFileExists(archive))
			return false;

		return TableFormPath(group, name, name, out, outBytes) && out[0] != 0;
	}

	int AddDerivedEventPackagePaths(bool includeBootScenes)
	{
		if (!EventTableLoaded())
		{
			Log("asset paths: the event name table is not loaded, so no path can be "
			    "derived. Call ffx::LoadEventTable first.");
			return 0;
		}

		int filled = 0;
		int skippedBoot = 0;
		int noPackage = 0;

		for (int id = 0; id < Rva::EventIdCount; ++id)
		{
			if (!EventPackagePathSlotEmpty(id))
				continue;

			if (!includeBootScenes && EventIdIsBootScene(id))
			{
				++skippedBoot;
				continue;
			}

			char path[160];
			if (!DeriveEventPackagePath(id, path, (int)sizeof(path)))
			{
				++noPackage;
				continue;
			}

			if (AddEventPackagePath(id, path))
				++filled;
		}

		Log("asset paths: filled %d empty event ids from the archive. %d had no package "
		    "to point at, %d were the hardcoded boot scenes and were left alone.",
		    filled, noPackage, skippedBoot);

		if (filled > 0)
			Log("asset paths: THOSE IDS HAVE NEVER BEEN LOADED BY ANYTHING. They are the "
			    "developers' unfinished test packages, so a missing walkmesh or a script "
			    "that expects state nothing set up is likely. Measure one with "
			    "ffx::EventPackageBytes before warping to it.");

		return filled;
	}

	bool EventPackageSubstitutionKey(int eventId, char* out, int outBytes)
	{
		if (!out || outBytes < 2)
			return false;
		out[0] = 0;

		// Out of the live table rather than derived, because the key has to match what
		// the resolver will actually be holding.
		char path[272];
		if (!EventPackagePath(eventId, path, (int)sizeof(path)))
			return false;

		// "<group>/<name>/<name>.ebp", which is everything after "/event/obj/". Shorter
		// than that and the substitution would start matching other asset kinds.
		const char* marker = strstr(path, "/event/obj/");
		if (!marker)
			return false;

		_snprintf_s(out, (size_t)outBytes, _TRUNCATE, "%s", marker + 11);
		return out[0] != 0;
	}

	int EventPackageVariantCount(int eventId)
	{
		int found = 0;
		for (int i = 0; i < kVariantPrefixCount; ++i)
		{
			char path[160];
			if (EventPackageVariantAt(eventId, found, path, (int)sizeof(path)))
				++found;
			else
				break;
		}
		return found;
	}

	bool EventPackageVariantAt(int eventId, int index, char* outPath, int outBytes)
	{
		if (!outPath || outBytes < 2 || index < 0)
			return false;
		outPath[0] = 0;

		char name[16];
		if (!EventNameForId(eventId, name, (int)sizeof(name)) || !name[0] || !name[1])
			return false;

		char group[3];
		group[0] = name[0];
		group[1] = name[1];
		group[2] = 0;

		// The variants live in the BASE name's directory with the prefix only on the
		// file, which is the one place the archive breaks its own naming convention.
		int seen = 0;
		for (int i = 0; i < kVariantPrefixCount; ++i)
		{
			char stem[48];
			if (_snprintf_s(stem, sizeof(stem), _TRUNCATE, "%s%s", kVariantPrefixes[i],
			        name)
			    <= 0)
				continue;

			char archive[192];
			if (!ArchiveFormPath(group, name, stem, archive, (int)sizeof(archive)))
				continue;
			if (!DataFileExists(archive))
				continue;

			if (seen == index)
				return TableFormPath(group, name, stem, outPath, outBytes)
				    && outPath[0] != 0;
			++seen;
		}

		return false;
	}

} // namespace ffx
