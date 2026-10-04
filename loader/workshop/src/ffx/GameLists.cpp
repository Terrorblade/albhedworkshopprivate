#include "ffx/GameLists.h"
#include "ffx/EngineHeap.h"

#include "ffx/AssetPaths.h"
#include "ffx/Encounter.h"
#include "ffx/GameState.h"
#include "ffx/KernelTables.h"
#include "ffx/addresses/GameState.h"
#include "ffx/addresses/WorldState.h"
#include "ffx/addresses/Minigames.h"
#include "workshop/CrashHandler.h"
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
		PickerList g_keyItems;
		PickerList g_weaponAbilities;
		PickerList g_armourAbilities;
		PickerList g_allEvents;
		PickerList g_battles;

		// Where each battle row came from, parallel to g_battles. The picker only
		// carries an int, and the battle field file is keyed by (scene, zone, slot)
		// rather than by the (map, encounter) the battle id packs, so the rest of the
		// row has to live somewhere.
		struct BattleWhere
		{
			int battleId;
			short scene;
			short zone;
			short slot;
		};

		// Dimensioned off the picker's own cap, so a row that made it into the list
		// always has a home here.
		BattleWhere g_battleWhere[PickerList::MaxItems];
		int g_battleWhereCount = 0;

		bool g_battleMonsterNames = false;

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

		// ---------------------------------------------------------------------------
		// ONE VALIDATED VIEW OF THE WHOLE TABLE, NOT ONE CHECK PER ROW.
		//
		// THIS IS WHAT CAUSED A TEN SECOND FREEZE. workshop::Readable is a
		// VirtualQuery, which is a syscall that takes the address space lock, and the
		// old EventTableRow called it twice per row. EventNameForId scanned every row
		// to find one id, and BuildAllEvents called that for all 402 ids. 402 x 402
		// rows x two syscalls is about half a million VirtualQuery calls per list
		// rebuild. The hang watchdog caught the game thread sitting inside
		// NtQueryVirtualMemory with that exact call chain under it.
		//
		// Checking the whole span once is also a STRONGER check than checking each row,
		// not a weaker one: it proves the entire table is in one committed region.
		struct TableView
		{
			const BYTE* base;
			int rows;
		};

		bool EventNameTableView(TableView* out)
		{
			DWORD held = 0;
			if (!ReadGlobal(Rva::EventIdNameTable, &held))
				return false;

			// Zero before the engine fills it, because this global is past the end of
			// initialized .data and the loader zero fills that tail.
			if (held == 0 || held == 0xFFFFFFFFu)
				return false;

			int rows = 0;
			if (!ReadGlobal(Rva::EventIdNameCount, &rows))
				return false;
			if (rows <= 0 || rows > kEventCountSane)
				return false;

			const BYTE* base = (const BYTE*)(UINT_PTR)held;

			// Memoized on the identity of the table, so a rebuild that walks every row
			// pays for one VirtualQuery rather than one per row. The engine reallocates
			// this on a map change, and a different base or count re-validates.
			static const BYTE* checkedBase = nullptr;
			static int checkedRows = 0;
			static bool checkedOk = false;

			if (base != checkedBase || rows != checkedRows)
			{
				checkedOk = Readable(base, (SIZE_T)rows * kEventRowBytes);
				checkedBase = base;
				checkedRows = rows;
			}
			if (!checkedOk)
				return false;

			out->base = base;
			out->rows = rows;
			return true;
		}

		const BYTE* EventTableBase()
		{
			DWORD held = 0;
			if (!ReadGlobal(Rva::EventIdNameTable, &held))
				return nullptr;

			// Zero before the engine fills it, because this global is past the end of
			// initialized .data and the loader zero fills that tail. 0xFFFFFFFF is what
			// IDA reports for a byte the file does not contain, so it is worth rejecting
			// as well, but 0 is the one that actually turns up.
			if (held == 0 || held == 0xFFFFFFFFu)
				return nullptr;

			const BYTE* base = (const BYTE*)(UINT_PTR)held;
			if (!Readable(base, kEventRowBytes))
				return nullptr;
			return base;
		}

		// THE IDS THAT HANG THE GAME, as of the shipped archive. Every one of these
		// names a package that is not in it, and FFX_Ev_LoadEventPackage answers a bad
		// package magic with while(1) and no break, so the simulation thread is gone and
		// the process has to be killed. Nothing about them looks wrong from inside the
		// process.
		//
		// THIS IS NO LONGER THE AUTHORITY. ProbeEventPackages below asks the engine to
		// open each package and measure it, which is a real existence test, and that
		// answer wins once it has been taken. What this list is for now is the window
		// before the first simulation step, and being a cross-check: a disagreement
		// between the two is logged, because a disagreement means the archive is not the
		// one this list was derived from, which is exactly what happens when somebody
		// adds content.
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

		// ---------------------------------------------------------------------------
		// The measured event probe
		//
		// The baked deny list above was the best answer available without running the
		// game. This is the better one, and it is the reason the lists can be trusted
		// after somebody adds content: FFX_Asset_GetSizeForIndex resolves the path and
		// then OPENS THE FILE to measure it, returning 0 when it cannot. So a non-zero
		// answer means the .ebp is in the archive right now, which is the only test
		// that notices a package that was ADDED and the only one that notices one going
		// away.
		//
		// ONE FILE OPEN PER ID, 402 of them, so it runs once and the answers are kept.
		// Kind 12 is not in that function's early-out kind set, so for events it always
		// takes the open path. Everything both event lists need is collected in the one
		// pass, because a pass per list would mean opening every package twice.
		// ---------------------------------------------------------------------------

		struct EventProbe
		{
			unsigned bytes; // what the package measures, 0 when it is not in the archive
			bool hasPath;   // the asset path table has an entry for this id
			char label[48]; // the engine's own "bl/bltz0000", empty when there is no path
		};

		EventProbe g_eventProbe[Rva::EventIdCount];
		bool g_eventProbed = false;

		// The asset loader's selected kind, saved and put back by the destructor.
		// SELECTING A KIND IS A WRITE TO SHARED STATE: the selector stores the kind base
		// and the kind in loader slots 8 and 9, and the engine's own loads read them, so
		// leaving a different kind selected breaks the next thing the game loads. There
		// are four places that need this now, which is why it is a type rather than four
		// copies of the same five lines.
		//
		// ok is false, and nothing was touched, when the loader table has not been
		// filled yet. That is the state during DllMain and at the very start of boot.
		struct EventKindScope
		{
			EventKindScope()
			    : ok(false), kindBase(nullptr), kind(nullptr), heldBase(0), heldKind(0)
			{
				typedef int(__cdecl * SelectKindFn)(int kind);
				SelectKindFn selectKind = (SelectKindFn)ModuleAddress(Rva::AssetSelectKind);
				if (!Readable((void*)selectKind, 1))
					return;

				// THIS USED TO TEST "slots[8] == 0xFFFFFFFF" AND THAT CRASHED THE GAME ON
				// BOOT. Two things were wrong with it. Slot 8 is what the selector
				// WRITES, not what it reads, so it answers the wrong question. And the
				// asset loader table lives past the end of initialized .data, so the
				// loader zero fills it and slot 8 reads 0 at process start, never
				// 0xFFFFFFFF. The test could not fire, the call went through with slot 11
				// holding 0, and "movsx eax, word ptr [eax+ecx*2]" at RVA 0x0036C53B took
				// an access violation. DllMain is exactly where that happens.
				// ffx::AssetLoaderReady checks the slots that get dereferenced, for 0.
				if (!AssetLoaderReady())
					return;

				// The breadcrumb goes immediately before the engine call, not after the
				// guard, because the whole point is to name the call that was in flight
				// if this faults again. This exact line is the one that crashed the boot.
				CrashContext("ffx: calling FFX_Asset_SelectKind for the event kind");

				DWORD* base = (DWORD*)ModuleAddress(Rva::AssetCurrentKindBase);
				DWORD* sel = (DWORD*)ModuleAddress(Rva::AssetCurrentKind);
				if (!Readable(base, 4) || !Readable(sel, 4))
					return;

				heldBase = *base;
				heldKind = *sel;
				kindBase = base;
				kind = sel;

				ok = selectKind(Rva::AssetKindEventObj) >= 0;
			}

			~EventKindScope()
			{
				if (kindBase && kind)
				{
					*kindBase = heldBase;
					*kind = heldKind;
				}
			}

			bool ok;

		private:
			EventKindScope(const EventKindScope&);
			EventKindScope& operator=(const EventKindScope&);

			DWORD* kindBase;
			DWORD* kind;
			DWORD heldBase;
			DWORD heldKind;
		};

		// MUST RUN ON THE GAME THREAD, it calls the engine's file layer 402 times.
		bool ProbeEvents()
		{
			typedef char*(__cdecl * GetPathFn)(int index);
			typedef unsigned(__cdecl * GetSizeFn)(int index);

			GetPathFn getPath = (GetPathFn)ModuleAddress(Rva::AssetGetPathForIndex);
			GetSizeFn getSize = (GetSizeFn)ModuleAddress(Rva::AssetGetSizeForIndex);
			if (!Readable((void*)getPath, 1) || !Readable((void*)getSize, 1))
				return false;

			EventKindScope scope;
			if (!scope.ok)
				return false;

			CrashContext("ffx: probing all %d event packages", Rva::EventIdCount);

			for (int id = 0; id < Rva::EventIdCount; ++id)
			{
				EventProbe& e = g_eventProbe[id];
				e.bytes = 0;
				e.hasPath = false;
				e.label[0] = 0;

				// Sub index 0 of the id's 18 path slots is the .ebp itself.
				const char* path = getPath(Rva::AssetEventStride * id);
				if (!path || !Readable(path, 1) || !path[0])
					continue;

				e.hasPath = true;
				LabelFromPath(path, e.label, (int)sizeof(e.label));

				// A BREADCRUMB PER ID, which does flood the 16 slot ring. That is the
				// point: if this loop is where the game stops, the ring holds the last
				// sixteen ids and the newest one names exactly which package did it.
				// The outer context is in the normal log anyway.
				//
				// FFX_Ev_LoadEventPackage spins forever on a package that does not
				// ship, and only 330 of these 402 ids do, so a hang in here is the
				// single most likely one in the whole kit.
				CrashContext("ffx: event id %d, size of %s", id, e.label);

				// Only asked for an id that has a path, because the size getter would
				// resolve the same empty path and attempt a pointless open.
				e.bytes = getSize(Rva::AssetEventStride * id);
			}

			g_eventProbed = true;

			// The DISAGREEMENT is the interesting half, not the agreement. A baked deny
			// list that no longer matches the archive is exactly the signal that
			// something has added or removed a package, which is what this measurement
			// exists for, so it gets said out loud rather than quietly ignored.
			int withPath = 0;
			int shipped = 0;
			int deniedButPresent = 0;
			int allowedButMissing = 0;
			for (int id = 0; id < Rva::EventIdCount; ++id)
			{
				const EventProbe& e = g_eventProbe[id];
				if (e.hasPath)
					++withPath;
				if (e.bytes)
					++shipped;
				if (e.bytes && InDenyList(id))
					++deniedButPresent;
				if (!e.bytes && e.hasPath && !InDenyList(id))
					++allowedButMissing;
			}

			Log("lists: probed %d event ids, %d have a path, %d ship a package",
			    Rva::EventIdCount, withPath, shipped);
			if (deniedButPresent || allowedButMissing)
				Log("lists: the baked deny list no longer matches the archive. %d denied "
				    "ids are present and %d allowed ids are missing. THE MEASUREMENT WINS, "
				    "and kEventDenyList in workshop/src/ffx/GameLists.cpp wants updating.",
				    deniedButPresent, allowedButMissing);
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

			typedef char*(__cdecl * GetPathFn)(int index);

			GetPathFn getPath = (GetPathFn)ModuleAddress(Rva::AssetGetPathForIndex);
			if (!Readable((void*)getPath, 1))
				return;

			EventKindScope scope;
			if (!scope.ok)
				return;

			int skippedNoPath = 0;
			int skippedDenied = 0;

			for (int id = 0; id < Rva::EventIdCount; ++id)
			{
				// Sub index 0 of the id's 18 path slots is the .ebp itself.
				const char* path = getPath(Rva::AssetEventStride * id);
				if (!path || !Readable(path, 1) || !path[0])
				{
					++skippedNoPath;
					continue;
				}

				// A path with no shipped file is the dangerous case, not a harmless one:
				// FFX_Ev_LoadEventPackage checks the package magic and spins in while(1)
				// on a mismatch, with no break and no return. So an id that cannot be
				// loaded must never reach a picker. This route has no measurement to go
				// on, so it has to trust the baked list.
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

			g_events.SetLive(!g_events.Empty());
			if (!g_events.Empty())
				Log("lists: %d loadable events, %d ids have no path, %d ship no package",
				    g_events.Count(), skippedNoPath, skippedDenied);
		}

		// The loadable-only list, from the measurement when there is one. This is the
		// list every existing caller uses and the one that has to stay safe, because a
		// bad id hard locks the simulation thread.
		void BuildEventsFromProbe()
		{
			g_events.Reset("asset path table, package sizes measured");

			for (int id = 0; id < Rva::EventIdCount; ++id)
			{
				const EventProbe& e = g_eventProbe[id];
				if (!e.hasPath || !e.bytes)
					continue;

				if (e.label[0])
					g_events.Add(id, e.label);
				else
					g_events.AddFormatted(id, "event %d", id);
			}

			g_events.SetLive(!g_events.Empty());
			if (!g_events.Empty())
				Log("lists: %d loadable events, measured", g_events.Count());
		}

		// EVERY id, including the ones that cannot be loaded, which is what makes the
		// developers' test and sample packages visible. The name comes from eventid.bin
		// when that table has been filled, because "test01" and "testbattle" are only
		// names there, the path table gives a group and a stem instead.
		//
		// NOTHING MAY WARP FROM THIS LIST WITHOUT EventIdLoadable. Every row that cannot
		// be loaded says so in its label, but a label is not a guard.
		void BuildAllEvents()
		{
			g_allEvents.Reset(g_eventProbed ? "all event ids, measured"
			                                : "all event ids, unmeasured");

			const bool named = EventTableLoaded();

			for (int id = 0; id < Rva::EventIdCount; ++id)
			{
				const EventProbe& e = g_eventProbe[id];

				// The eventid.bin name first, then the path label, then the bare id. All
				// three happen in a stock boot: the name table is empty until something
				// fills it, and 54 ids have no path at all.
				char name[16];
				const bool haveName = named && EventNameForId(id, name, (int)sizeof(name))
				    && name[0] != 0;

				// A package that is not there is the dangerous row, so the marker goes
				// where a filter will find it rather than at the end of the line.
				const char* mark = "";
				if (g_eventProbed)
					mark = e.bytes ? "" : (e.hasPath ? "[no package] " : "[no path] ");
				else if (!e.hasPath)
					mark = "[unmeasured] ";

				if (haveName && e.label[0])
					g_allEvents.AddFormatted(id, "%s%-12s %s", mark, name, e.label);
				else if (haveName)
					g_allEvents.AddFormatted(id, "%s%-12s id %d", mark, name, id);
				else if (e.label[0])
					g_allEvents.AddFormatted(id, "%s%s", mark, e.label);
				else
					g_allEvents.AddFormatted(id, "%sevent %d", mark, id);
			}

			g_allEvents.SetLive(!g_allEvents.Empty());
		}

		void BuildEvents()
		{
			// The measurement first, when it has been taken. It is strictly better
			// evidence than either baked list and it is the only one that notices new
			// content, which is the whole point of taking it.
			if (g_eventProbed)
			{
				BuildEventsFromProbe();
				BuildAllEvents();
				if (!g_events.Empty())
					return;
			}

			// Then the path table, which is always populated and whose label is the
			// engine's own "bl/bltz0000" rather than a bare name.
			BuildEventsFromPathTable();
			BuildAllEvents();
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
		// EVERY ONE OF THE 64 ROWS, named or not. The game's own menu only ever shows
		// the ones the player holds, so this is the first place the full set is visible.
		void BuildKeyItems()
		{
			g_keyItems.Reset("important.bin, 0xA000 based");

			// The table pointer is a kernel slot, so it is null until the battle kernel
			// has loaded. Asking for a name before that resolves a string offset against
			// nothing.
			const DWORD* table = (const DWORD*)ModuleAddress(Rva::KernelTableImportant);
			if (!Readable(table, 4) || !LooksLikePointer(*table))
				return;

			typedef int(__cdecl * KeyItemNameFn)(short id);
			KeyItemNameFn getName = (KeyItemNameFn)ModuleAddress(Rva::KeyItemGetNameString);
			if (!Readable((void*)getName, 1))
				return;

			for (int i = 0; i < Rva::KeyItemCount; ++i)
			{
				const int id = Rva::KeyItemIdBase + i;

				// Kernel encoding, not ASCII. The digits are moved to the front of the
				// character set, so reading this as a C string gives mojibake.
				const unsigned char* raw
				    = (const unsigned char*)(UINT_PTR)getName((short)id);

				char text[64];
				int written = 0;
				if (raw && Readable(raw, 1))
					written = DecodeKernelText(raw, 63, text, (int)sizeof(text));

				if (written > 0)
					g_keyItems.Add(id, text);
				else
					g_keyItems.AddFormatted(id, "unnamed %d", i);
			}

			g_keyItems.SetLive(!g_keyItems.Empty());
		}

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

		// EVERY FIGHT THE GAME CAN START, walked out of btl.bin in memory rather than
		// listed here. The walk is scenes -> zones -> formation entries and it is all in
		// ffx/Encounter.h. This replaced a hand written table of 42 arena and penalty
		// fights, which was both incomplete and wrong the moment anything changed.
		//
		// EMPTY UNTIL THE FIRST BATTLE. btl.bin is read at the first battle init, so at
		// the title screen there is nothing to walk and that is not an error.
		//
		// The picker id is the BATTLE ID, (mapId << 16) | encounterId, which is what
		// FFX_Btl_RequestScriptedBattle takes, so a selection feeds it directly with no
		// second lookup.
		// "Sahagin x3" or "Dingo x1, Water Flan x1", out of the fight's own roster.
		// Returns false when nothing could be named, which is a real answer for the
		// seven fights that have no enemies at all.
		bool MonsterLabel(int scene, int zone, int slot, char* out, int outBytes)
		{
			if (!out || outBytes < 2)
				return false;
			out[0] = 0;

			EncounterMonsters set;
			if (!EncounterMonstersAt(scene, zone, slot, &set) || set.count <= 0)
				return false;

			// Counted in place, keeping first-seen order, because "Sahagin x3" is how
			// the game itself would describe that fight and three separate rows is not.
			int seen[kEncounterMonsterSlots];
			int times[kEncounterMonsterSlots];
			int distinct = 0;

			for (int i = 0; i < set.count; ++i)
			{
				int at = -1;
				for (int d = 0; d < distinct; ++d)
					if (seen[d] == set.ids[i])
					{
						at = d;
						break;
					}

				if (at >= 0)
					++times[at];
				else
				{
					seen[distinct] = set.ids[i];
					times[distinct] = 1;
					++distinct;
				}
			}

			int used = 0;
			for (int d = 0; d < distinct; ++d)
			{
				const int room = outBytes - used;
				if (room < 8)
					break;

				const char* name = KernelName(KernelMonsters, seen[d]);

				// Monster ids 0 and 365 are placeholder kernel rows with no real name,
				// so the id is the only true thing to say about them.
				char numbered[24];
				if (!name || !name[0])
				{
					_snprintf_s(numbered, sizeof(numbered), _TRUNCATE, "monster %d",
					    seen[d]);
					name = numbered;
				}

				used += _snprintf_s(out + used, (size_t)room, _TRUNCATE, "%s%s x%d",
				    used ? ", " : "", name, times[d]);
			}

			return out[0] != 0;
		}

		void BuildBattles()
		{
			g_battles.Reset(g_battleMonsterNames ? "btl.bin, monsters named"
			                                     : "btl.bin, walked in memory");
			g_battleWhereCount = 0;

			const int scenes = EncounterSceneCount();
			if (scenes <= 0)
				return;

			for (int scene = 0; scene < scenes; ++scene)
			{
				EncounterScene s;
				if (!EncounterSceneAt(scene, &s))
					continue;

				for (int zone = 0; zone < s.zoneCount; ++zone)
				{
					EncounterZone z;
					if (!EncounterZoneAt(scene, zone, &z))
						continue;

					for (int slot = 0; slot < z.formationCount; ++slot)
					{
						int encId = 0;
						if (!EncounterFormationAt(scene, zone, slot, &encId, nullptr))
							continue;

						const int battleId = (s.mapId << 16) | (encId & 0xFFFF);

						// The same encounter can sit in more than one zone of a scene,
						// and two rows that start the identical fight are just noise in
						// a picker.
						bool already = false;
						for (int n = 0; n < g_battles.Count(); ++n)
							if (g_battles.Items()[n].id == battleId)
							{
								already = true;
								break;
							}
						if (already)
							continue;

						// Rate 0 means the zone has no random battles, so every fight in
						// it is one a script starts. That is where the developers' test
						// fights and the arena live, and it is worth being able to filter
						// on.
						const char* mark = z.rate == 0 ? "  [scripted]" : "";

						// The monsters first when they were asked for, because that is
						// what a person types to find a fight. The scene name and the
						// numbers stay on the row so the other way of looking still
						// works.
						char who[160];
						bool named = false;
						if (g_battleMonsterNames)
							named = MonsterLabel(scene, zone, slot, who, (int)sizeof(who));

						if (named)
							g_battles.AddFormatted(battleId, "%s  (%-8s map %d enc %d)%s",
							    who, s.name[0] ? s.name : "(unnamed)", s.mapId, encId,
							    mark);
						else
							g_battles.AddFormatted(battleId, "%-8s map %d enc %d%s",
							    s.name[0] ? s.name : "(unnamed)", s.mapId, encId, mark);

						// Only after the add, so a row dropped by the picker's cap does
						// not get an entry here that nothing can reach.
						if (g_battles.Count() > g_battleWhereCount
						    && g_battleWhereCount < PickerList::MaxItems)
						{
							BattleWhere& w = g_battleWhere[g_battleWhereCount++];
							w.battleId = battleId;
							w.scene = (short)scene;
							w.zone = (short)zone;
							w.slot = (short)slot;
						}
					}
				}
			}

			g_battles.SetLive(!g_battles.Empty());
		}

	} // namespace

	void ProbeEventPackages()
	{
		if (g_eventProbed)
			return;

		// ONCE A SECOND, NOT ONCE A STEP.
		//
		// This is called from a step handler and retried until it works, which can be
		// the whole boot and title sequence. Every attempt swaps the asset loader's
		// SELECTED KIND, which is a global the engine's own loads read, and puts it
		// back afterwards. Doing that thirty times a second next to the engine's own
		// loading is asking for the engine to resolve a path against our kind instead
		// of its own, and a bad path on this engine does not fail, it spins forever.
		//
		// A second of latency on a measurement that only matters once costs nothing.
		static DWORD lastTry = 0;
		const DWORD now = GetTickCount();
		if (lastTry != 0 && (now - lastTry) < 1000)
			return;
		lastTry = now;

		if (ProbeEvents())
		{
			BuildEventsFromProbe();
			BuildAllEvents();
		}
	}

	void ReprobeEventPackages()
	{
		// For after an editor has added a package. The probe is a one-shot only so that
		// 402 file opens do not happen on every map change, not because the answer can
		// never change.
		g_eventProbed = false;
		ProbeEventPackages();
	}

	bool EventPackagesProbed() { return g_eventProbed; }

	unsigned EventPackageBytes(int eventId)
	{
		if (eventId < 0 || eventId >= Rva::EventIdCount || !g_eventProbed)
			return 0;
		return g_eventProbe[eventId].bytes;
	}

	void RefreshGameLists()
	{
		// One breadcrumb per build, so a crash report says which list was being built
		// rather than just naming this function. They cost a formatted copy into a
		// shared ring, which against reading the archive is nothing.
		CrashContext("ffx: building the event list");
		BuildEvents();
		CrashContext("ffx: building the battle list");
		BuildBattles();
		CrashContext("ffx: building the character list");
		BuildCharacters();
		CrashContext("ffx: building the equipment list");
		BuildEquipment();
		CrashContext("ffx: building the inventory list");
		BuildInventory();
		CrashContext("ffx: building the key item list");
		BuildKeyItems();
		CrashContext("ffx: building the ability recipe list");
		BuildAbilityRecipes();
		CrashContext("ffx: the game lists are built");
	}

	void RefreshSaveDerivedLists()
	{
		CrashContext("ffx: building the character list");
		BuildCharacters();
		CrashContext("ffx: building the equipment list");
		BuildEquipment();
		CrashContext("ffx: building the inventory list");
		BuildInventory();
		CrashContext("ffx: the save derived lists are built");
	}

	int EventProbeCount() { return Rva::EventIdCount; }

	bool EventProbeRow(int eventId, unsigned* outBytes, bool* outHasPath, char* outLabel, int labelBytes)
	{
		if (eventId < 0 || eventId >= Rva::EventIdCount)
			return false;

		const EventProbe& e = g_eventProbe[eventId];
		if (outBytes)
			*outBytes = e.bytes;
		if (outHasPath)
			*outHasPath = e.hasPath;
		if (outLabel && labelBytes > 0)
		{
			int i = 0;
			for (; i < labelBytes - 1 && i < (int)sizeof(e.label) && e.label[i]; ++i)
				outLabel[i] = e.label[i];
			outLabel[i] = 0;
		}
		return true;
	}

	bool SetEventProbeRow(int eventId, unsigned bytes, bool hasPath, const char* label)
	{
		if (eventId < 0 || eventId >= Rva::EventIdCount)
			return false;

		EventProbe& e = g_eventProbe[eventId];
		e.bytes = bytes;
		e.hasPath = hasPath;
		e.label[0] = 0;
		if (label)
		{
			int i = 0;
			for (; i < (int)sizeof(e.label) - 1 && label[i]; ++i)
				e.label[i] = label[i];
			e.label[i] = 0;
		}
		return true;
	}

	void MarkEventPackagesProbed(const char* how)
	{
		g_eventProbed = true;
		Log("lists: the event package measurement is trusted, %s", how ? how : "no reason given");
	}

	const PickerList& EventList() { return g_events; }
	const PickerList& AllEventList() { return g_allEvents; }
	const PickerList& BattleList() { return g_battles; }

	bool BattleListLocate(int battleId, int* outScene, int* outZone, int* outSlot)
	{
		for (int i = 0; i < g_battleWhereCount; ++i)
		{
			if (g_battleWhere[i].battleId != battleId)
				continue;

			if (outScene) *outScene = (int)g_battleWhere[i].scene;
			if (outZone) *outZone = (int)g_battleWhere[i].zone;
			if (outSlot) *outSlot = (int)g_battleWhere[i].slot;
			return true;
		}
		return false;
	}

	bool BattleMonsterNames() { return g_battleMonsterNames; }

	void SetBattleMonsterNames(bool on) { g_battleMonsterNames = on; }
	const PickerList& CharacterList() { return g_characters; }
	const PickerList& PartyCharacterList() { return g_partyCharacters; }
	const PickerList& AeonList() { return g_aeons; }
	const PickerList& EquipmentList() { return g_equipment; }
	const PickerList& InventoryList() { return g_inventory; }
	const PickerList& KeyItemList() { return g_keyItems; }
	const PickerList& WeaponAbilityList() { return g_weaponAbilities; }
	const PickerList& ArmourAbilityList() { return g_armourAbilities; }

	void LogGameLists()
	{
		Log("lists: %s", g_events.Describe());
		Log("lists: %s", g_allEvents.Describe());
		Log("lists: %s", g_battles.Describe());
		Log("lists: %s", g_characters.Describe());
		Log("lists: %s", g_partyCharacters.Describe());
		Log("lists: %s", g_aeons.Describe());
		Log("lists: %s", g_weaponAbilities.Describe());
		Log("lists: %s", g_armourAbilities.Describe());
		Log("lists: %s", g_equipment.Describe());
		Log("lists: %s", g_inventory.Describe());
		Log("lists: %s", g_keyItems.Describe());

		if (!EventTableLoaded())
			Log("lists: the event id table is not loaded, which is normal in retail. "
			    "Until something fills it the test and sample packages show as a path "
			    "stem rather than by name. See ffx/addresses/WorldState.h.");

		if (!g_eventProbed)
			Log("lists: the event packages have not been measured, so loadability is "
			    "coming from the baked deny list. Call ffx::ProbeEventPackages from the "
			    "game thread.");

		if (g_battles.Empty())
			Log("lists: no battles. btl.bin is read at the first battle init, so this is "
			    "the state until one battle has started and it is not an error at the "
			    "title screen.");
		else
			Log("lists: %d distinct fights across %d battle scenes", g_battles.Count(),
			    EncounterSceneCount());
	}

	bool EventIdLoadable(int eventId)
	{
		if (eventId < 0 || eventId >= Rva::EventIdCount)
			return false;

		// THE MEASUREMENT WINS when it has been taken, in both directions. It opened the
		// file, so it is better evidence than the baked list, and it is the only answer
		// that can be right about a package somebody has just added.
		if (g_eventProbed)
			return g_eventProbe[eventId].hasPath && g_eventProbe[eventId].bytes != 0;

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

		// No list yet, so ask the path table directly.
		typedef char*(__cdecl * GetPathFn)(int index);

		GetPathFn getPath = (GetPathFn)ModuleAddress(Rva::AssetGetPathForIndex);
		if (!Readable((void*)getPath, 1))
			return false;

		EventKindScope scope;
		if (!scope.ok)
			return false;

		const char* path = getPath(Rva::AssetEventStride * eventId);
		return path && Readable(path, 1) && path[0] != 0;
	}

	int CacheableGameLists(workshop::CacheableList* out, int max)
	{
		const workshop::CacheableList all[] = {
			{ "events", &g_events },
			{ "all-events", &g_allEvents },
			{ "battles", &g_battles },
			{ "key-items", &g_keyItems },
			{ "weapon-abilities", &g_weaponAbilities },
			{ "armour-abilities", &g_armourAbilities },
		};

		const int count = (int)(sizeof(all) / sizeof(all[0]));
		int wrote = 0;
		for (int i = 0; i < count && wrote < max; ++i)
			out[wrote++] = all[i];
		return wrote;
	}

	bool LoadEventTable()
	{
		if (EventTableLoaded())
			return true;

		// ---------------------------------------------------------------------------
		// ONCE A SECOND, AND SIXTY TIMES AT MOST.
		//
		// THIS USED TO RUN ON EVERY STEP UNTIL IT WORKED, which is thirty times a
		// second for the whole boot and title sequence, and that is not a harmless
		// retry. FFX_LoadEventIdTable calls FFX_MemAlloc. Calling an engine allocator
		// in a loop, before the file layer is up, with no way to know whether the
		// previous call got halfway, is exactly the shape that leaks a block or frees
		// a stale one. There is an unexplained crash on this project inside
		// FFX_Free -> FFX_MemFreeLocked on a corrupt block header with none of our
		// frames on the stack, and this is a candidate for it.
		//
		// The cap matters as much as the interval. "Retry forever" against an engine
		// function that allocates is not a retry, it is a slow leak, and the honest
		// answer after a minute is that the file is not there.
		// ---------------------------------------------------------------------------
		static DWORD lastTry = 0;
		static int tries = 0;
		static bool gaveUp = false;

		if (gaveUp)
			return false;

		const DWORD now = GetTickCount();
		if (lastTry != 0 && (now - lastTry) < 1000)
			return false;
		lastTry = now;

		// No arguments. The decompiler shows a usercall taking an edi argument that
		// it passes to the allocator as a tag, but the disassembly disagrees: edi is
		// set inside the function as the row counter, and FFX_MemAlloc is called with
		// one pushed argument. So this is a plain cdecl with no parameters.
		typedef void(__cdecl * LoadFn)(void);
		LoadFn load = (LoadFn)ModuleAddress(Rva::LoadEventIdTable);
		if (!Readable((void*)load, 1))
			return false;

		++tries;
		CrashContext("ffx: calling FFX_LoadEventIdTable, attempt %d", tries);
		load();

		// ---------------------------------------------------------------------------
		// THE CALL ABOVE CORRUPTS THE ENGINE HEAP, AND THIS PUTS IT BACK.
		//
		// Settled 2026-10-04 by walking the engine heap before and after. This is not
		// a precaution, it is a measured repair of a specific four byte write.
		//
		// FFX_LoadEventIdTable reads eventid.bin through FFX_Ch_ReadFileDev, which is
		// Sg_PcRead followed by a stamp pass at RVA 0x0043C4E0. Sg_PcRead returns a
		// raw FFX_MemAlloc payload, and the stamp pass then does
		//
		//     *(u32 *)(payload - 4) ^= ((*(u32 *)(payload - 4) ^ key) & 0x3FFFFF0)
		//
		// on it. payload - 4 is the allocator's own tag field, so a tag of 2, meaning
		// in use, becomes 0x03FFFFE2. Nothing faults at the time. Later, when the
		// engine frees the block NEXT to the file buffer, the allocator reads this tag,
		// decides it is a free list node and dereferences it, and the process dies in
		// sub_9435A0 with not one of our frames on the stack. That was an intermittent
		// boot crash on this project for days, about one run in three, and never
		// reproducible without this plugin because nothing in a retail build calls
		// Sg_PcRead at all.
		//
		// The repair is exact rather than a workaround. The tag's correct value IS 2,
		// and Sg_PcRead's file buffer is never freed, so marking the block in use is
		// the truth and leaks nothing that was not already leaked.
		//
		// See addresses/Memory.h for the heap layout and ffx/EngineHeap.h for the walk.
		// ---------------------------------------------------------------------------
		CrashContext("ffx: repairing the heap after FFX_LoadEventIdTable");
		const int repaired = RepairEngineHeap();
		if (repaired > 0)
			Log("lists: FFX_LoadEventIdTable damaged %d engine heap block header(s) "
			    "through Sg_PcRead's stamp pass, and they have been put back. Without "
			    "this the game dies later inside FFX_Free.",
			    repaired);

		// The engine writes the count last, after every row, so a non-zero count
		// means the rows behind it are complete.
		const bool filled = EventTableLoaded();
		if (filled)
		{
			Log("lists: the event id table filled on attempt %d, %d entries",
			    tries, EventTableCount());
			return true;
		}

		if (tries >= 60)
		{
			gaveUp = true;
			Log("lists: the event id table never filled in %d attempts over a minute, so "
			    "giving up on it. Events will be named by path rather than by name. The "
			    "read resolves into the archive, so eventid.bin is missing from it.",
			    tries);
		}
		else if (tries == 1)
		{
			// ONCE, NOT PER ATTEMPT. The first failure is expected, because the file
			// layer is not up when the first step runs. Saying so sixty times would
			// bury everything else in the log.
			Log("lists: the event id table did not fill on the first attempt, which is "
			    "normal this early. Retrying once a second for up to a minute.");
		}
		return false;
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
		TableView t;
		if (!EventNameTableView(&t))
			return false;
		if (row < 0 || row >= t.rows)
			return false;

		// NO Readable HERE. The view has already proved the whole table is committed,
		// and this used to be a syscall per row on a path that walks every row.
		const BYTE* p = t.base + (size_t)row * kEventRowBytes;

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
		if (eventId < 0 || eventId >= Rva::EventIdCount)
			return false;

		TableView t;
		if (!EventNameTableView(&t))
			return false;

		// AN INDEX, NOT A SCAN. BuildAllEvents asks for all 402 ids one after the
		// other, and this used to walk the whole table for each of them. Built once
		// per table instance, and the engine reallocating the table rebuilds it.
		static const BYTE* indexedBase = nullptr;
		static int indexedRows = 0;
		static short rowForId[Rva::EventIdCount];

		if (indexedBase != t.base || indexedRows != t.rows)
		{
			for (int i = 0; i < Rva::EventIdCount; ++i)
				rowForId[i] = -1;

			for (int row = 0; row < t.rows; ++row)
			{
				const BYTE* p = t.base + (size_t)row * kEventRowBytes;
				if (!PrintableName((const char*)p, kEventNameBytes))
					continue;

				const int id = (int)(*(const DWORD*)(p + kEventNameBytes));
				if (id < 0 || id >= Rva::EventIdCount)
					continue;

				// FIRST ROW WINS, which is what the old scan did, so a duplicate id
				// resolves to the same name it always did.
				if (rowForId[id] < 0)
					rowForId[id] = (short)row;
			}

			indexedBase = t.base;
			indexedRows = t.rows;
		}

		const int row = rowForId[eventId];
		if (row < 0)
			return false;

		const BYTE* p = t.base + (size_t)row * kEventRowBytes;
		if (outName && outBytes > 0)
		{
			int i = 0;
			for (; i < kEventNameBytes && i < outBytes - 1 && p[i]; ++i)
				outName[i] = (char)p[i];
			outName[i] = 0;
		}
		return true;
	}

	int CurrentEventId()
	{
		int id = 0;
		if (!ReadGlobal(Rva::EvCurrentEventId, &id))
			return 0;
		return id;
	}

} // namespace ffx
