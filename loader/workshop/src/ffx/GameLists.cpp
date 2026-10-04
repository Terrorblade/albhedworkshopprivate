#include "ffx/GameLists.h"

#include "ffx/GameState.h"
#include "ffx/KernelTables.h"
#include "ffx/addresses/WorldState.h"
#include "ffx/addresses/Minigames.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <stdio.h>
#include <string.h>

namespace ffx
{

	using namespace workshop;

	namespace
	{

		PickerList g_events;
		PickerList g_characters;
		PickerList g_partyCharacters;
		PickerList g_aeons;
		PickerList g_equipment;
		PickerList g_inventory;
		PickerList g_weaponAbilities;
		PickerList g_armourAbilities;

		// The row stride and the name width come from addresses/WorldState.h, because
		// the name is not reliably terminated and a local copy of 12 would be a second
		// place to get that wrong.
		const int kEventRowBytes = Rva::EventIdNameRowStride;
		const int kEventNameBytes = Rva::EventIdNameMaxChars;

		// Nothing in this game has anywhere near this many events, so a count past it
		// means the pointer is garbage rather than that the table is large.
		const int kEventCountSane = 4096;

		template <class T>
		bool ReadGlobal(DWORD rva, T* out)
		{
			const T* p = (const T*)ModuleAddress(rva);
			if (!Readable(p, sizeof(T)))
				return false;
			*out = *p;
			return true;
		}

		const BYTE* EventTableBase()
		{
			DWORD held = 0;
			if (!ReadGlobal(Rva::EventIdNameTable, &held))
				return nullptr;

			// The file image holds 0xFFFFFFFF here, and a never-filled table keeps it.
			if (held == 0 || held == 0xFFFFFFFFu)
				return nullptr;

			const BYTE* base = (const BYTE*)(UINT_PTR)held;
			if (!Readable(base, kEventRowBytes))
				return nullptr;
			return base;
		}

		// THE IDS THAT HANG THE GAME. Every one of these names a package that is not in
		// the archive, and FFX_Ev_LoadEventPackage answers a bad package magic with
		// while(1) and no break, so the simulation thread is gone and the process has to
		// be killed. Nothing about them looks wrong from inside the process.
		//
		// Derived twice, two different ways, because getting it wrong produces no error.
		// Listing every .ebp in FFX_Data.vbf gives 397 distinct stems. Reading
		// eventid.bin the way FFX_LoadEventIdTable reads it, 402 records of
		// {u32 nameOffset, u32 nameLen} from file offset 0 with the string blob at 3216
		// and the event id equal to the record index, gives 400 named rows of which
		// these 24 name no shipped file. Rows 101 and 111 are nameless. Separately,
		// walking the engine's asset path table found 348 ids with a path, and the 18 of
		// those with no file are all inside this set, so the two measurements agree.
		//
		// The six that are in this list but not in the path-table set are ids with no
		// path at all. They are kept because the eventid.bin fallback list does not
		// consult the path table, and an id denied twice costs nothing.
		const int kEventDenyList[] = {
			40, 186, 216, 228, 229, 231, 232, 233,
			246, 251, 262, 300, 304, 342, 350, 353,
			357, 358, 360, 369, 373, 379, 400, 401,
		};

		bool InDenyList(int eventId)
		{
			for (int i = 0; i < (int)(sizeof(kEventDenyList) / sizeof(kEventDenyList[0])); ++i)
				if (kEventDenyList[i] == eventId)
					return true;
			return false;
		}

		// The engine's own label for a package, built the way FFX_Ev_LoadEventPackage
		// builds g_ffxCurrentEventName: take what follows "/event/obj/" and chop at the
		// last slash. A full path reads
		// "host0:/ffx/master/jppc/event/obj/bl/bltz0000/bltz0000.ebp" and the label is
		// "bl/bltz0000". The two letter prefix is the area group, which is the useful
		// thing to filter on.
		bool LabelFromPath(const char* path, char* out, int outBytes)
		{
			if (!path || !out || outBytes < 2)
				return false;

			const char* marker = strstr(path, "/event/obj/");
			if (!marker)
				return false;

			const char* start = marker + 11;

			// Chop at the last slash, which removes the repeated "/<name>.ebp" tail.
			const char* lastSlash = nullptr;
			for (const char* p = start; *p; ++p)
				if (*p == '/')
					lastSlash = p;
			if (!lastSlash)
				return false;

			int length = (int)(lastSlash - start);
			if (length <= 0)
				return false;
			if (length > outBytes - 1)
				length = outBytes - 1;

			for (int i = 0; i < length; ++i)
				out[i] = start[i];
			out[length] = 0;
			return true;
		}

		// Reads the game's own asset path table, which is the list of every map and
		// event. Unlike the eventid.bin name table this is always populated, because the
		// engine needs it to load anything at all.
		//
		// MUST RUN ON THE GAME THREAD. It calls two engine functions and it changes the
		// asset loader's selected kind, which the engine's own loads read, so the
		// previous selection is saved and put back.
		void BuildEventsFromPathTable()
		{
			g_events.Reset("asset path table, kind 12");

			typedef int(__cdecl * SelectKindFn)(int kind);
			typedef char*(__cdecl * GetPathFn)(int index);

			SelectKindFn selectKind = (SelectKindFn)ModuleAddress(Rva::AssetSelectKind);
			GetPathFn getPath = (GetPathFn)ModuleAddress(Rva::AssetGetPathForIndex);
			if (!Readable((void*)selectKind, 1) || !Readable((void*)getPath, 1))
				return;

			// The table is all 0xFFFFFFFF until the engine fills it, so enumerating
			// before that would call into nothing.
			const DWORD* slots = (const DWORD*)ModuleAddress(Rva::AssetLoaderTable);
			if (!Readable(slots, 4 * 10) || slots[8] == 0xFFFFFFFFu)
				return;

			DWORD* kindBase = (DWORD*)ModuleAddress(Rva::AssetCurrentKindBase);
			DWORD* kind = (DWORD*)ModuleAddress(Rva::AssetCurrentKind);
			if (!Readable(kindBase, 4) || !Readable(kind, 4))
				return;

			const DWORD heldBase = *kindBase;
			const DWORD heldKind = *kind;

			int skippedNoPath = 0;
			int skippedDenied = 0;

			if (selectKind(Rva::AssetKindEventObj) >= 0)
			{
				for (int id = 0; id < Rva::EventIdCount; ++id)
				{
					// Sub index 0 of the id's 18 path slots is the .ebp itself.
					const char* path = getPath(Rva::AssetEventStride * id);
					if (!path || !Readable(path, 1) || !path[0])
					{
						++skippedNoPath;
						continue;
					}

					// A path with no shipped file is the dangerous case, not a harmless
					// one: FFX_Ev_LoadEventPackage checks the package magic and spins in
					// while(1) on a mismatch, with no break and no return. So an id that
					// cannot be loaded must never reach a picker.
					if (InDenyList(id))
					{
						++skippedDenied;
						continue;
					}

					char label[48];
					if (LabelFromPath(path, label, (int)sizeof(label)))
						g_events.Add(id, label);
					else
						g_events.AddFormatted(id, "event %d", id);
				}
			}

			*kindBase = heldBase;
			*kind = heldKind;

			g_events.SetLive(!g_events.Empty());
			if (!g_events.Empty())
				Log("lists: %d loadable events, %d ids have no path, %d ship no package",
				    g_events.Count(), skippedNoPath, skippedDenied);
		}

		void BuildEvents()
		{
			// The path table first, because it is always populated and its label is the
			// engine's own "bl/bltz0000" rather than a bare name.
			BuildEventsFromPathTable();
			if (!g_events.Empty())
				return;

			// Fall back to the eventid.bin name table. Only useful if something has
			// called LoadEventTable, and it offers no loadability filter, so every entry
			// is marked with a warning the UI can show.
			g_events.Reset("event id name table, UNFILTERED");

			const int count = EventTableCount();
			if (count <= 0)
				return;

			const BYTE* base = EventTableBase();
			if (!base || !Readable(base, (size_t)count * kEventRowBytes))
				return;

			for (int row = 0; row < count; ++row)
			{
				const BYTE* p = base + (size_t)row * kEventRowBytes;
				const int id = (int)(*(const DWORD*)(p + kEventNameBytes));
				if (InDenyList(id))
					continue;

				// AddFixedName screens the name on printable ASCII, which is what drops
				// the two binary rows, and stops at 12 bytes, which is what survives the
				// id write having eaten a 12 character name's terminator.
				g_events.AddFixedName(id, (const char*)p, kEventNameBytes);
			}

			g_events.SetLive(!g_events.Empty());
		}

		void AddCharacterTo(PickerList& list, BYTE index)
		{
			// The player-facing name first, because that is what the rename screen
			// changed and what the player will be looking for. CharacterName never
			// returns NULL and does not read the game, so it is the fallback.
			char shown[24];
			if (!CharacterDisplayName(index, shown, (int)sizeof(shown)) || !shown[0])
			{
				const char* stable = CharacterName(index);
				_snprintf(shown, sizeof(shown) - 1, "%s", stable ? stable : "?");
				shown[sizeof(shown) - 1] = 0;
			}

			const bool inParty = CharacterInParty(index);
			const int slot = ActiveSlotOfCharacter(index);

			if (slot >= 0)
				list.AddFormatted((int)index, "%s  [active %d]", shown, slot + 1);
			else if (inParty)
				list.AddFormatted((int)index, "%s  [in party]", shown);
			else
				list.Add((int)index, shown);
		}

		void BuildCharacters()
		{
			g_characters.Reset("character records");
			g_partyCharacters.Reset("character records 0..7");
			g_aeons.Reset("character records 8..17");

			for (BYTE i = 0; i < (BYTE)kCharCount; ++i)
			{
				AddCharacterTo(g_characters, i);
				if (i < (BYTE)kCharFirstAeon)
					AddCharacterTo(g_partyCharacters, i);
				else
					AddCharacterTo(g_aeons, i);
			}

			// Live even with no game, because CharacterName does not read the game and
			// the 18 indices are a property of the build rather than of the save.
			g_characters.SetLive(true);
			g_partyCharacters.SetLive(true);
			g_aeons.SetLive(true);
		}

		void BuildEquipment()
		{
			g_equipment.Reset("equipment array");

			if (!GameLoaded())
				return;

			for (int i = 0; i < kEquipSlots; ++i)
			{
				EquipEntryData* e = EquipEntryAt(i);
				if (!e || !EquipEntryInUse(e))
					continue;

				const BYTE kind = EquipEntryKind(e);
				const BYTE owner = EquipEntryOwner(e);
				const BYTE forChar = EquipEntryForChar(e);
				const int abilities = EquipEntryAbilityCount(e);

				// The game's own name for the piece. It has to come from the engine
				// rather than from w_name.bin row zero, because a row holds one name
				// per character and the one that applies is the piece's own forChar.
				// Reading offset zero gets Tidus's name for everybody's gear.
				const WORD slotId = EquipSlotId(i);
				const char* real = EquipName(slotId);
				const char* kindText = kind == 0 ? "weapon" : "armour";
				const char* label = real ? real : kindText;
				const char* whose = CharacterName(forChar);

				if (owner != (BYTE)kCharNone)
				{
					const char* wearer = CharacterName(owner);
					g_equipment.AddFormatted((int)slotId,
					    "%s  (%s, %d ability%s, worn by %s)", label, whose ? whose : "?",
					    abilities, abilities == 1 ? "" : "s", wearer ? wearer : "?");
				}
				else
				{
					g_equipment.AddFormatted((int)slotId,
					    "%s  (%s, %d ability%s)", label, whose ? whose : "?", abilities,
					    abilities == 1 ? "" : "s");
				}
			}

			g_equipment.SetLive(true);
		}

		void BuildInventory()
		{
			g_inventory.Reset("inventory slots");

			if (!GameLoaded())
				return;

			for (int slot = 0; slot < kItemSlots; ++slot)
			{
				const WORD id = ItemIdAt(slot);
				if (id == (WORD)kItemIdNone)
					continue;

				const int held = (int)ItemCountAt(slot);
				const char* name = KernelName(KernelItems, KernelIdFromTagged((int)id));
				if (name)
					g_inventory.AddFormatted((int)id, "%s  x%d", name, held);
				else
					g_inventory.AddFormatted((int)id, "item 0x%04X  x%d", id, held);
			}

			g_inventory.SetLive(true);
		}

		// The legal auto-abilities per equipment kind, from kaizou.bin. One ability can
		// have more than one recipe, different materials for the same result, so this
		// keeps the first and drops the repeats.
		void BuildAbilityRecipes()
		{
			g_weaponAbilities.Reset("customise recipe table, weapon side");
			g_armourAbilities.Reset("customise recipe table, armour side");

			const int count = EquipRecipeCount();
			if (count <= 0)
				return;

			for (int i = 0; i < count; ++i)
			{
				EquipRecipe row;
				if (!EquipRecipeAt(i, &row))
					continue;

				if (row.kind != 1 && row.kind != 2)
					continue;

				PickerList& into = row.kind == 1 ? g_weaponAbilities : g_armourAbilities;

				bool already = false;
				for (int n = 0; n < into.Count(); ++n)
					if (into.Items()[n].id == row.abilityId)
					{
						already = true;
						break;
					}
				if (already)
					continue;

				const char* name = KernelName(KernelAutoAbilities,
				    KernelIdFromTagged(row.abilityId));
				if (name)
					into.Add(row.abilityId, name);
				else
					into.AddFormatted(row.abilityId, "ability 0x%04X", row.abilityId);
			}

			g_weaponAbilities.SetLive(!g_weaponAbilities.Empty());
			g_armourAbilities.SetLive(!g_armourAbilities.Empty());
		}

	} // namespace

	void RefreshGameLists()
	{
		BuildEvents();
		BuildCharacters();
		BuildEquipment();
		BuildInventory();
		BuildAbilityRecipes();
	}

	const PickerList& EventList() { return g_events; }
	const PickerList& CharacterList() { return g_characters; }
	const PickerList& PartyCharacterList() { return g_partyCharacters; }
	const PickerList& AeonList() { return g_aeons; }
	const PickerList& EquipmentList() { return g_equipment; }
	const PickerList& InventoryList() { return g_inventory; }
	const PickerList& WeaponAbilityList() { return g_weaponAbilities; }
	const PickerList& ArmourAbilityList() { return g_armourAbilities; }

	void LogGameLists()
	{
		Log("lists: %s", g_events.Describe());
		Log("lists: %s", g_characters.Describe());
		Log("lists: %s", g_partyCharacters.Describe());
		Log("lists: %s", g_aeons.Describe());
		Log("lists: %s", g_weaponAbilities.Describe());
		Log("lists: %s", g_armourAbilities.Describe());
		Log("lists: %s", g_equipment.Describe());
		Log("lists: %s", g_inventory.Describe());

		if (!EventTableLoaded())
			Log("lists: the event id table is not loaded, which is normal in retail. "
			    "Nothing can be warped to by name until something fills it. See "
			    "ffx/addresses/WorldState.h.");
	}

	bool EventIdLoadable(int eventId)
	{
		if (eventId < 0 || eventId >= Rva::EventIdCount)
			return false;
		if (InDenyList(eventId))
			return false;

		// Prefer the list, because it was built with the same two tests and costs
		// nothing to search.
		if (!g_events.Empty())
		{
			for (int i = 0; i < g_events.Count(); ++i)
				if (g_events.Items()[i].id == eventId)
					return true;
			// The list is authoritative once built, so an id that is not in it has no
			// path.
			return false;
		}

		// No list yet, so ask the path table directly. Same save and restore as the
		// enumeration, for the same reason.
		typedef int(__cdecl * SelectKindFn)(int kind);
		typedef char*(__cdecl * GetPathFn)(int index);

		SelectKindFn selectKind = (SelectKindFn)ModuleAddress(Rva::AssetSelectKind);
		GetPathFn getPath = (GetPathFn)ModuleAddress(Rva::AssetGetPathForIndex);
		if (!Readable((void*)selectKind, 1) || !Readable((void*)getPath, 1))
			return false;

		const DWORD* slots = (const DWORD*)ModuleAddress(Rva::AssetLoaderTable);
		if (!Readable(slots, 4 * 10) || slots[8] == 0xFFFFFFFFu)
			return false;

		DWORD* kindBase = (DWORD*)ModuleAddress(Rva::AssetCurrentKindBase);
		DWORD* kind = (DWORD*)ModuleAddress(Rva::AssetCurrentKind);
		if (!Readable(kindBase, 4) || !Readable(kind, 4))
			return false;

		const DWORD heldBase = *kindBase;
		const DWORD heldKind = *kind;

		bool ok = false;
		if (selectKind(Rva::AssetKindEventObj) >= 0)
		{
			const char* path = getPath(Rva::AssetEventStride * eventId);
			ok = path && Readable(path, 1) && path[0] != 0;
		}

		*kindBase = heldBase;
		*kind = heldKind;
		return ok;
	}

	bool LoadEventTable()
	{
		if (EventTableLoaded())
			return true;

		// No arguments. The decompiler shows a usercall taking an edi argument that
		// it passes to the allocator as a tag, but the disassembly disagrees: edi is
		// set inside the function as the row counter, and FFX_MemAlloc is called with
		// one pushed argument. So this is a plain cdecl with no parameters.
		typedef void(__cdecl * LoadFn)(void);
		LoadFn load = (LoadFn)ModuleAddress(Rva::LoadEventIdTable);
		if (!Readable((void*)load, 1))
			return false;

		load();

		// The engine writes the count last, after every row, so a non-zero count
		// means the rows behind it are complete.
		const bool filled = EventTableLoaded();
		if (filled)
			Log("lists: the event id table filled, %d entries", EventTableCount());
		else
			Log("lists: the event id table did not fill. The read resolves into the "
			    "archive, so this means the file is missing from it.");
		return filled;
	}

	bool EventTableLoaded()
	{
		DWORD flag = 0;
		if (!ReadGlobal(Rva::EventIdTableLoaded, &flag) || flag == 0)
			return false;

		// The flag alone is not enough. Trust it only when the pointer and the count
		// also read as plausible.
		return EventTableBase() != nullptr && EventTableCount() > 0;
	}

	int EventTableCount()
	{
		int count = 0;
		if (!ReadGlobal(Rva::EventIdNameCount, &count))
			return 0;
		if (count <= 0 || count > kEventCountSane)
			return 0;
		return count;
	}

	bool EventTableRow(int row, int* outEventId, char* outName, int outBytes)
	{
		if (row < 0 || row >= EventTableCount())
			return false;

		const BYTE* base = EventTableBase();
		if (!base)
			return false;

		const BYTE* p = base + (size_t)row * kEventRowBytes;
		if (!Readable(p, kEventRowBytes))
			return false;

		if (!PrintableName((const char*)p, kEventNameBytes))
			return false;

		if (outEventId)
			*outEventId = (int)(*(const DWORD*)(p + kEventNameBytes));

		if (outName && outBytes > 0)
		{
			int i = 0;
			for (; i < kEventNameBytes && i < outBytes - 1 && p[i]; ++i)
				outName[i] = (char)p[i];
			outName[i] = 0;
		}
		return true;
	}

	bool EventNameForId(int eventId, char* outName, int outBytes)
	{
		const int count = EventTableCount();
		for (int row = 0; row < count; ++row)
		{
			int id = 0;
			char name[16];
			if (!EventTableRow(row, &id, name, (int)sizeof(name)))
				continue;
			if (id != eventId)
				continue;

			if (outName && outBytes > 0)
			{
				_snprintf(outName, (size_t)outBytes - 1, "%s", name);
				outName[outBytes - 1] = 0;
			}
			return true;
		}
		return false;
	}

	int CurrentEventId()
	{
		int id = 0;
		if (!ReadGlobal(Rva::EvCurrentEventId, &id))
			return 0;
		return id;
	}

} // namespace ffx
