#include "ffx/EventPlaces.h"

#include "ffx/DataFile.h"
#include "ffx/KernelTables.h"
#include "workshop/Log.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

namespace ffx
{

	using namespace workshop;

	namespace
	{

		// ---------------------------------------------------------------------------
		// macrodic.dcp, exactly as FFX_MenuRes_GetMapNameTable reads it
		//
		// A DIRECTORY OF SIXTEEN u32 AT FILE OFFSET 0, and the place names are entry
		// 11. FFX_MenuRes_LoadMacroDic walks 0 to 0x3C in steps of 4 and rewrites each
		// non-zero entry in place into a real pointer, and the table getter is three
		// instructions ending in "mov eax, [eax+2Ch]", which is entry 11.
		//
		// Entries 0 to 5 are zero in every shipped copy, which is why counting only the
		// live offsets makes this look like "section 5". It is the same u32 either way,
		// at file offset 0x2C, but entry 11 is what the engine calls it.
		//
		// A section's first u16 is 4 times its row count, so 1396 means 349 rows. Then
		// come the rows, four bytes each, as TWO u16 offsets relative to the section
		// base. FFX_SaveData_MapNameIdToString computes the string offset as
		//
		//     base + 4 * id + 2 * alt        alt = (g_ffxSaveDataOptionFlags >> 3) & 1
		//
		// so the array starts at base + 0 and record 0's first offset IS that count
		// field. That works because the first string sits immediately after the array,
		// which is 4 * rows bytes long, so the two numbers are necessarily equal.
		//
		// A NOTE ON A WRONG READING, because it looks convincing. Treat the array as
		// starting at base + 2 and the pair stops being (primary, alternate) and starts
		// looking like (start, end), since every record's second offset then equals the
		// next record's first. That is just primary == alt shifted by one slot. It holds
		// for all 349 rows of new_uspc and for none of jppc, where the two really do
		// differ, so the English table forgives the mistake and the Japanese one does
		// not. The primary offset is the one to read.
		// ---------------------------------------------------------------------------
		const char* kMacroDicPath = "ffx_ps2/ffx/master/new_uspc/menu/macrodic.dcp";
		const int kDcpDirectoryEntries = 16;
		const int kPlaceDirectoryEntry = 11;
		const int kDcpRecordBytes = 4;

		// ---------------------------------------------------------------------------
		// The atel block of an event package
		// ---------------------------------------------------------------------------
		const int kAtelPlaceField = 0x1E;  // u16, see the header
		const int kAtelPlaceTable = 0x28;  // u32, offset from the atel base
		const int kPlaceCountMask = 0x7FFF;
		const int kPlaceTableFlag = 0x8000;

		// ---------------------------------------------------------------------------
		// The script bytecode
		//
		// One byte per instruction with the high bit clear, three with it set, and then
		// the next two bytes are a little endian operand. That is the whole decoder.
		//
		// Which means a plain byte scan for the pattern is not an approximation. Checked
		// against a real instruction walk over all 397 packages: scanning only the code
		// span, scanning to the end of the atel block, and scanning the entire file all
		// produce the identical set of ids with not one extra, so this scans whole files
		// and never has to parse an actor table to find where the code ends.
		// ---------------------------------------------------------------------------
		const BYTE kOpPushImm = 0x80 | 0x2E;
		const BYTE kOpSyscall = 0x80 | 0x58;
		const BYTE kOpSyscallF = 0x80 | 0x35;
		const int kSyscallSetActorModel = 0x0001; // core:1, FFX_AtelOp_SetActorModel
		const int kPatternBytes = 6;              // pushimm imm16, syscall imm16

		// How long to wait between packages, so a read of the archive from here never
		// sits in front of more than one of the game's own. 397 of these adds about ten
		// seconds to a sweep that reads for twenty two, which is the right trade when
		// the sweep is something the player asked for and is waiting on.
		const DWORD kPaceMs = 25;

		// ---------------------------------------------------------------------------
		// Storage. All of it static, because this runs once and the labels it builds
		// have to outlive the call.
		// ---------------------------------------------------------------------------
		const int kMaxPlaces = 512;      // 349 ship
		const int kMaxAreas = 192;       // the distinct text in front of a " - "
		const int kTextArenaBytes = 24 * 1024;
		const int kLabelArenaBytes = 48 * 1024;
		const int kMaxUses = 8192;       // one per (model, package), about 3000 ship
		const int kMaxModels = 1024;     // 414 ship
		const int kMaxPerPackage = 256;  // distinct models placed by one package

		struct Use
		{
			WORD chrId;
			WORD area;  // index into g_areas
			WORD place;  // index into g_places, for the single place case
			WORD sites; // how many core:1 calls in that package used this model
		};

		struct Model
		{
			int chrId;
			const char* label;
		};

		char g_textArena[kTextArenaBytes];
		int g_textUsed = 0;

		char g_labelArena[kLabelArenaBytes];
		int g_labelUsed = 0;

		const char* g_places[kMaxPlaces];
		WORD g_placeArea[kMaxPlaces];
		int g_placeCount = 0;

		const char* g_areas[kMaxAreas];
		int g_areaCount = 0;

		Use g_uses[kMaxUses];
		int g_useCount = 0;

		Model g_models[kMaxModels];
		int g_modelCount = 0;

		// One winner does the work, and ready is what every reader waits on. Set last
		// and behind a barrier, so a reader that sees ready also sees the tables.
		volatile LONG g_claimed = 0;
		volatile LONG g_ready = 0;
		int g_loadedModels = 0;
		HANDLE g_thread = nullptr;

		WORD Rd16(const BYTE* p)
		{
			return (WORD)(p[0] | ((WORD)p[1] << 8));
		}

		DWORD Rd32(const BYTE* p)
		{
			return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16)
			    | ((DWORD)p[3] << 24);
		}

		const char* KeepText(char* arena, int arenaBytes, int* used, const char* text)
		{
			const int need = (int)strlen(text) + 1;
			if (*used + need > arenaBytes)
				return nullptr;

			char* at = arena + *used;
			memcpy(at, text, (size_t)need);
			*used += need;
			return at;
		}

		// The part of a place name in front of the " - ", so "Luca - Cafe" and
		// "Luca - Number 2 Dock" are both Luca. A name with no separator is its own
		// area, which is how "Besaid Village" and "Guadosalam" behave.
		int AreaForName(const char* name)
		{
			char key[96];
			const char* dash = strstr(name, " - ");
			const int len = dash ? (int)(dash - name) : (int)strlen(name);
			if (len <= 0 || len >= (int)sizeof(key))
				return -1;
			memcpy(key, name, (size_t)len);
			key[len] = 0;

			for (int i = 0; i < g_areaCount; ++i)
			{
				if (strcmp(g_areas[i], key) == 0)
					return i;
			}
			if (g_areaCount >= kMaxAreas)
				return -1;

			const char* kept = KeepText(g_textArena, kTextArenaBytes, &g_textUsed, key);
			if (!kept)
				return -1;
			g_areas[g_areaCount] = kept;
			return g_areaCount++;
		}

		// ---------------------------------------------------------------------------
		// Step one: the place name table.
		// ---------------------------------------------------------------------------
		bool LoadPlaceNames()
		{
			int bytes = 0;
			BYTE* file = (BYTE*)ReadDataFile(kMacroDicPath, &bytes);
			if (!file)
			{
				Log("event places: %s did not read, so there are no place names and every "
				    "model keeps its bare number.",
				    kMacroDicPath);
				return false;
			}

			bool ok = false;
			if (bytes > 4 * kDcpDirectoryEntries)
			{
				const DWORD base = Rd32(file + 4 * kPlaceDirectoryEntry);
				if (base > 0 && (int)base + 2 <= bytes)
				{
					const int sectionBytes = (int)Rd16(file + base);
					const int rows = sectionBytes / kDcpRecordBytes;
					for (int r = 0; r < rows && r < kMaxPlaces; ++r)
					{
						// The primary offset. The alternate two bytes further on is the
						// same string in every row of the English table and a different
						// one in Japanese, and the engine picks between them on a save
						// option flag this does not read.
						const int at = (int)base + kDcpRecordBytes * r;
						if (at + 2 > bytes)
							break;

						const int off = (int)base + (int)Rd16(file + at);
						char text[96];
						if (off >= bytes
						    || DecodeKernelText(file + off, bytes - off, text,
						           (int)sizeof(text))
						        <= 0)
						{
							g_places[r] = nullptr;
							g_placeArea[r] = (WORD)0xFFFF;
							g_placeCount = r + 1;
							continue;
						}

						g_places[r] = KeepText(g_textArena, kTextArenaBytes, &g_textUsed,
						    text);
						const int area = g_places[r] ? AreaForName(g_places[r]) : -1;
						g_placeArea[r] = (WORD)(area < 0 ? 0xFFFF : area);
						g_placeCount = r + 1;
					}
					ok = g_placeCount > 0;
				}
			}

			FreeDataFile(file);
			if (!ok)
				Log("event places: %s read but directory entry %d did not parse.",
				    kMacroDicPath, kPlaceDirectoryEntry);
			return ok;
		}

		// Which place a package is in, as a row in g_places, or -1 for none.
		//
		// The table form wins by majority rather than by taking entry zero, because
		// fourteen packages really do carry more than one place and the majority is the
		// one the package is about.
		int PlaceForPackage(const BYTE* file, int bytes)
		{
			if (bytes < 0x40)
				return -1;

			// The container header is u32 section offsets from +4 until a -1, and the
			// atel block is the first of them.
			DWORD atel = 0;
			for (int i = 1; i < 16 && 4 * i + 4 <= bytes; ++i)
			{
				const DWORD v = Rd32(file + 4 * i);
				if (v == 0xFFFFFFFF)
					break;
				if (i == 1)
					atel = v;
			}
			if (atel == 0 || (int)atel + kAtelPlaceTable + 4 > bytes)
				return -1;

			const WORD field = Rd16(file + atel + kAtelPlaceField);
			if ((field & kPlaceTableFlag) == 0)
			{
				// The plain form. Every package that uses it holds 0, which is "no
				// place", but it is read properly rather than assumed to be 0.
				const int row = (int)field;
				return (row > 0 && row < g_placeCount && g_places[row]) ? row : -1;
			}

			const int count = field & kPlaceCountMask;
			const DWORD table = Rd32(file + atel + kAtelPlaceTable);
			if (count <= 0 || table == 0)
				return -1;

			// ENTRY ZERO, and the count is only here to bound the read.
			//
			// The index into this table is the "dic" attribute of the ground the player
			// stands on, which FFX_Map_GroundAttrGetDic produces as (attr >> 11) & 3.
			// Two bits, so only entries 0 to 3 are reachable, and every shipped package
			// has count 16 with entries 4 to 15 holding a copy of entry 0. The engine
			// also falls back to entry 0 whenever there is no ground, in
			// FFX_SaveData_SetMapNameIdFromGroundDic's "idx = (dic >= count) ? 0 : dic".
			//
			// So entry 0 is the package's default place, and 333 of the 347 packages
			// that have a table spell the same place in all sixteen slots anyway. What
			// this gives up is telling apart the up to four ground regions of the other
			// fourteen, which a model placed by script cannot be attributed to.
			const int at = (int)atel + (int)table;
			if (at + 2 > bytes)
				return -1;

			const int row = (int)Rd16(file + at);
			return (row > 0 && row < g_placeCount && g_places[row]) ? row : -1;
		}

		// ---------------------------------------------------------------------------
		// Step two: who is placed by this package.
		// ---------------------------------------------------------------------------
		void SweepPackage(const BYTE* file, int bytes, int place)
		{
			if (place < 0 || bytes < kPatternBytes)
				return;

			const int area = (int)g_placeArea[place];
			if (area == 0xFFFF)
				return;

			WORD ids[kMaxPerPackage];
			WORD hits[kMaxPerPackage];
			int found = 0;

			const int last = bytes - kPatternBytes;
			for (int i = 0; i <= last; ++i)
			{
				if (file[i] != kOpPushImm)
					continue;
				if (file[i + 3] != kOpSyscall && file[i + 3] != kOpSyscallF)
					continue;
				if ((int)Rd16(file + i + 4) != kSyscallSetActorModel)
					continue;

				const WORD chrId = Rd16(file + i + 1);
				int slot = -1;
				for (int k = 0; k < found; ++k)
				{
					if (ids[k] == chrId)
					{
						slot = k;
						break;
					}
				}
				if (slot >= 0)
				{
					++hits[slot];
				}
				else if (found < kMaxPerPackage)
				{
					ids[found] = chrId;
					hits[found] = 1;
					++found;
				}
			}

			for (int k = 0; k < found && g_useCount < kMaxUses; ++k)
			{
				Use& u = g_uses[g_useCount++];
				u.chrId = ids[k];
				u.area = (WORD)area;
				u.place = (WORD)place;
				u.sites = hits[k];
			}
		}

		// ---------------------------------------------------------------------------
		// Step three: one label per model.
		// ---------------------------------------------------------------------------
		const int kLabelBudget = 56;

		void BuildLabel(int chrId)
		{
			// Gather this model's areas with their placement counts. Weighted by how
			// many times the model is actually placed, so a model dropped forty times
			// in Besaid and once in Luca reads Besaid first.
			WORD area[64];
			int weight[64];
			int areas = 0;
			int onlyPlace = -1;
			bool onePlace = true;

			for (int i = 0; i < g_useCount; ++i)
			{
				if (g_uses[i].chrId != (WORD)chrId)
					continue;

				// BY NAME, NOT BY ROW. The 349 row table repeats itself a lot, rows 1
				// to 6 all being "Zanarkand - Stadium", so comparing row numbers would
				// call a model multi place for being used in two rows that say the
				// identical thing and then fall back to the vaguer area label.
				const int here = (int)g_uses[i].place;
				if (onlyPlace < 0)
					onlyPlace = here;
				else if (onlyPlace != here
				    && !(g_places[onlyPlace] && g_places[here]
				        && strcmp(g_places[onlyPlace], g_places[here]) == 0))
					onePlace = false;

				int slot = -1;
				for (int k = 0; k < areas; ++k)
				{
					if (area[k] == g_uses[i].area)
					{
						slot = k;
						break;
					}
				}
				if (slot >= 0)
					weight[slot] += (int)g_uses[i].sites;
				else if (areas < 64)
				{
					area[areas] = g_uses[i].area;
					weight[areas] = (int)g_uses[i].sites;
					++areas;
				}
			}
			if (areas <= 0 || g_modelCount >= kMaxModels)
				return;

			char text[160];

			// One place and one place only, so say exactly where rather than the area it
			// is in. This is what turns n142 into "Luca - Cafe".
			if (onePlace && onlyPlace >= 0 && g_places[onlyPlace])
			{
				_snprintf_s(text, sizeof(text), _TRUNCATE, "%s", g_places[onlyPlace]);
			}
			else
			{
				// Selection sort on the area weights. 64 entries at worst and this runs
				// once per model, so the simple thing is the right thing.
				for (int a = 0; a < areas - 1; ++a)
				{
					int best = a;
					for (int b = a + 1; b < areas; ++b)
					{
						if (weight[b] > weight[best])
							best = b;
					}
					if (best == a)
						continue;
					const WORD ta = area[a];
					const int tw = weight[a];
					area[a] = area[best];
					weight[a] = weight[best];
					area[best] = ta;
					weight[best] = tw;
				}

				int used = 0;
				int listed = 0;
				text[0] = 0;
				for (int a = 0; a < areas; ++a)
				{
					const char* name = g_areas[area[a]];
					const int add = (int)strlen(name) + (used > 0 ? 2 : 0);
					if (listed > 0 && used + add > kLabelBudget)
						break;
					if (used > 0)
					{
						memcpy(text + used, ", ", 2);
						used += 2;
					}
					const int n = (int)strlen(name);
					memcpy(text + used, name, (size_t)n);
					used += n;
					text[used] = 0;
					++listed;
				}

				// The ones that did not fit, counted rather than dropped silently. A
				// crowd model reading "Besaid, Luca +9" is telling the truth about
				// itself in a way that naming one area would not.
				if (listed < areas)
					_snprintf_s(text + used, sizeof(text) - used, _TRUNCATE, " +%d",
					    areas - listed);
			}

			const char* kept = KeepText(g_labelArena, kLabelArenaBytes, &g_labelUsed,
			    text);
			if (!kept)
				return;

			g_models[g_modelCount].chrId = chrId;
			g_models[g_modelCount].label = kept;
			++g_modelCount;
		}

		// Waits for the engine's file system, then does the sweep.
		//
		// The poll is the honest shape here. There is no event the engine signals when
		// FFX_InitFileSystem is done, and the alternative was to make the game thread
		// tell us, which puts the ordering problem back on the caller. A tenth of a
		// second of latency on a thirteen second job is not worth a hook.
		DWORD WINAPI SweepThread(LPVOID)
		{
			// ---------------------------------------------------------------------------
			// BACKGROUND MODE, WHICH IS ABOUT THE DISK AND NOT THE CPU.
			//
			// Windows keeps I/O priority separate from thread priority, so
			// THREAD_PRIORITY_BELOW_NORMAL does nothing for a job whose cost is the
			// disk. This one is entirely disk: 58 MB of reads scattered through a
			// 20.7 GB archive, which measured 4.5 MB/s unthrottled, so it is seek bound.
			//
			// BE HONEST ABOUT WHAT THIS DID AND DID NOT FIX. It did not stop the hang
			// watchdog firing. That was caused by the sweep being started from three
			// places, one of which ran during the plugin's own startup, and the fix was
			// to leave exactly one starter in CheatsMod.cpp after the last file heavy
			// thing it does. This stays because it is still the right setting for a
			// background job with no deadline, and the only thing it costs is the
			// sweep's own latency, 12.9 seconds unthrottled against about 17 with it.
			// ---------------------------------------------------------------------------
			SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);

			const int kWaitSteps = 1200; // two minutes, then give up rather than spin
			for (int i = 0; i < kWaitSteps && !DataFileSystemReady(); ++i)
				Sleep(100);

			LoadEventPlaces();
			return 0;
		}

	} // namespace

	int LoadEventPlaces()
	{
		// WHOEVER GETS HERE FIRST DOES IT, and nobody else waits. A second caller
		// blocking would put the thirteen seconds back on whatever thread it is on,
		// which is the whole thing this avoids.
		if (InterlockedCompareExchange(&g_claimed, 1, 0) != 0)
			return g_ready ? g_loadedModels : 0;

		const DWORD startedAt = GetTickCount();
		if (!DataFileSystemReady())
		{
			Log("event places: the engine's file system is not up yet, so nothing was "
			    "read. Start this on a step or from the worker, not from DllMain.");
			MemoryBarrier();
			InterlockedExchange(&g_ready, 1);
			return 0;
		}

		if (!LoadPlaceNames())
		{
			MemoryBarrier();
			InterlockedExchange(&g_ready, 1);
			return 0;
		}

		// Every .ebp in the archive. All 397 live under one language root and nothing
		// else uses the extension, so the extension is the whole filter.
		const int total = DataFileCount();
		int packages = 0;
		int placed = 0;
		for (int i = 0; i < total; ++i)
		{
			DataFileEntry e;
			if (!DataFileEntryAt(i, &e) || !e.path)
				continue;

			const size_t len = strlen(e.path);
			if (len < 5 || strcmp(e.path + len - 4, ".ebp") != 0)
				continue;

			++packages;

			// A gap between packages, so a read from here never sits in front of more
			// than one of the game's own.
			//
			// PACING IS NOT WHAT MAKES THIS SAFE, and it is worth saying so because it
			// looks like it should be. Pacing was tried as the fix for the boot freeze
			// and it made things worse: at 200 ms between packages the sweep slept for
			// four fifths of its life and still produced the longest stall of any
			// attempt, 30 seconds. The game stops stepping by itself while it loads, and
			// touching the archive at all stretches those loads. What makes this safe is
			// that it only runs when the player asks for it. See ui/CheatPanel.h.
			Sleep(kPaceMs);

			int bytes = 0;
			BYTE* file = (BYTE*)ReadDataFile(e.path, &bytes);
			if (!file)
				continue;

			const int place = PlaceForPackage(file, bytes);
			if (place >= 0)
			{
				++placed;
				SweepPackage(file, bytes, place);
			}
			FreeDataFile(file);
		}

		// One label per distinct model, built from the uses collected above.
		for (int i = 0; i < g_useCount; ++i)
		{
			bool already = false;
			for (int k = 0; k < i && !already; ++k)
				already = g_uses[k].chrId == g_uses[i].chrId;
			if (!already)
				BuildLabel((int)g_uses[i].chrId);
		}

		g_loadedModels = g_modelCount;
		MemoryBarrier();
		InterlockedExchange(&g_ready, 1);

		// THE ELAPSED TIME IS IN THE LOG ON PURPOSE. It reads about 58 MB and it
		// measured thirteen seconds, which is why it is on a worker thread rather than
		// the game thread. If a log ever shows this line twice in a session then
		// something is calling it off the cold path.
		Log("event places: %d place names, %d areas. Swept %d packages, %d of them with "
		    "a place, and %d placements gave %d models a label. Took %lu ms.",
		    g_placeCount, g_areaCount, packages, placed, g_useCount, g_modelCount,
		    GetTickCount() - startedAt);
		if (g_useCount >= kMaxUses || g_modelCount >= kMaxModels)
			Log("event places: a table filled up, so some models are missing a label. "
			    "uses %d/%d, models %d/%d.",
			    g_useCount, kMaxUses, g_modelCount, kMaxModels);
		return g_loadedModels;
	}

	bool EventPlacesLoaded()
	{
		return g_ready != 0 && g_modelCount > 0;
	}

	bool EventPlacesReady()
	{
		return g_ready != 0;
	}

	void StartEventPlaces()
	{
		if (g_claimed != 0)
			return;

		// A DIAGNOSTIC SWITCH, because this sweep is the only thing in the kit that
		// keeps the disk busy for twenty seconds and "is it the sweep or is it the
		// game's own boot" is otherwise unanswerable. Set ALBHED_NO_PLACE_SWEEP to get
		// a cold boot with everything else the same and no place labels.
		if (GetEnvironmentVariableA("ALBHED_NO_PLACE_SWEEP", nullptr, 0) != 0)
		{
			Log("event places: ALBHED_NO_PLACE_SWEEP is set, so the sweep is off and "
			    "every npc keeps its bare number. This is a diagnostic, not a setting.");
			InterlockedExchange(&g_claimed, 1);
			MemoryBarrier();
			InterlockedExchange(&g_ready, 1);
			return;
		}

		// Not detached, because a DLL that unloads while this is still reading files
		// would be a crash with no stack. The handle is kept so a future teardown can
		// wait on it, and leaking one handle for the life of the process is the cheaper
		// of the two mistakes available here.
		g_thread = CreateThread(nullptr, 0, &SweepThread, nullptr, 0, nullptr);
		if (g_thread)
			return;

		// No thread, so nothing is ever going to finish. Say so and let the ready flag
		// go up with no data, which leaves every model with its bare number instead of
		// leaving a caller polling for something that will never arrive.
		Log("event places: CreateThread failed with %lu, so model place labels are off "
		    "for this session. Every npc keeps its bare number.",
		    GetLastError());
		InterlockedExchange(&g_claimed, 1);
		MemoryBarrier();
		InterlockedExchange(&g_ready, 1);
	}

	const char* ModelPlaceLabel(int chrId)
	{
		if (g_ready == 0)
			return nullptr;

		for (int i = 0; i < g_modelCount; ++i)
		{
			if (g_models[i].chrId == chrId)
				return g_models[i].label;
		}
		return nullptr;
	}

	int EventPlaceCount()
	{
		return g_placeCount;
	}

	const char* EventPlaceName(int row)
	{
		if (row < 0 || row >= g_placeCount)
			return nullptr;
		return g_places[row];
	}

	void LogEventPlaces()
	{
		Log("event places: %s, %d place names, %d areas, %d models labelled, arenas "
		    "%d/%d text and %d/%d label",
		    g_ready ? "ready" : (g_claimed ? "still sweeping" : "not started"),
		    g_placeCount, g_areaCount,
		    g_modelCount, g_textUsed, kTextArenaBytes, g_labelUsed, kLabelArenaBytes);
	}

} // namespace ffx
