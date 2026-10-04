#include "ffx/ListExport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ffx/GameLists.h"
#include "ffx/KernelTables.h"
#include "ffx/Models.h"
#include "workshop/Log.h"
#include "workshop/PickerCache.h"

namespace ffx
{

	using workshop::Log;

	namespace
	{

		const int kFormatVersion = 1;

		// Six game lists, one model list, five kernel tables.
		const int kMaxLists = 24;

		// PickerList::Reset keeps the source string BY POINTER rather than copying it,
		// so a list loaded from a file needs somewhere stable for its source line to
		// live. These are it.
		char g_sources[kMaxLists][96];

		char g_status[256] = "not checked yet";

		int Collect(workshop::CacheableList* out, int max)
		{
			int n = 0;
			n += CacheableGameLists(out + n, max - n);
			n += CacheableModelLists(out + n, max - n);
			n += CacheableKernelLists(out + n, max - n);
			return n;
		}

		// <game dir>, from the running exe rather than the current directory, which the
		// game changes.
		bool GameDir(wchar_t* out, int max)
		{
			if (GetModuleFileNameW(NULL, out, (DWORD)max) == 0)
				return false;

			int cut = -1;
			for (int i = 0; out[i]; ++i)
				if (out[i] == L'\\' || out[i] == L'/')
					cut = i;
			if (cut < 0)
				return false;
			out[cut] = 0;
			return true;
		}

		bool CacheFilePath(const char* name, wchar_t* out, int max)
		{
			wchar_t dir[MAX_PATH];
			if (!GameDir(dir, MAX_PATH))
				return false;

			// The list names are ASCII literals in this file, so a plain widen is right.
			wchar_t wide[64];
			int i = 0;
			for (; i < 63 && name[i]; ++i)
				wide[i] = (wchar_t)(unsigned char)name[i];
			wide[i] = 0;

			return _snwprintf_s(out, (size_t)max, _TRUNCATE,
			           L"%s\\AlBhedWorkshop\\data\\%s.tsv", dir, wide)
			    > 0;
		}

		bool ManifestPath(wchar_t* out, int max)
		{
			wchar_t dir[MAX_PATH];
			if (!GameDir(dir, MAX_PATH))
				return false;
			return _snwprintf_s(out, (size_t)max, _TRUNCATE,
			           L"%s\\AlBhedWorkshop\\data\\manifest.tsv", dir)
			    > 0;
		}

		// ---------------------------------------------------------------------------
		// THE STALENESS KEY.
		//
		// A VBF's last sixteen bytes are an MD5 of its entire header, which is every
		// file name, size and offset in the archive. The game computes and stores it
		// itself, so it is not something invented here, and tools/vbf.py reads the same
		// value as VbfArchive.stored_md5.
		//
		// FFX_Data.vbf is 20.7 GB. Hashing it would be absurd and is not necessary:
		// changing any file in it changes its header, which changes this.
		// ---------------------------------------------------------------------------
		bool ArchiveKey(char* out, int outBytes)
		{
			wchar_t dir[MAX_PATH];
			if (!GameDir(dir, MAX_PATH))
				return false;

			wchar_t path[MAX_PATH];
			if (_snwprintf_s(path, _countof(path), _TRUNCATE, L"%s\\data\\FFX_Data.vbf", dir) <= 0)
				return false;

			HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file == INVALID_HANDLE_VALUE)
				return false;

			LARGE_INTEGER size;
			size.QuadPart = 0;
			bool ok = GetFileSizeEx(file, &size) != 0 && size.QuadPart > 16;

			BYTE md5[16] = { 0 };
			if (ok)
			{
				LARGE_INTEGER at;
				at.QuadPart = -16;
				ok = SetFilePointerEx(file, at, NULL, FILE_END) != 0;
			}
			if (ok)
			{
				DWORD got = 0;
				ok = ReadFile(file, md5, 16, &got, NULL) != 0 && got == 16;
			}
			CloseHandle(file);
			if (!ok)
				return false;

			// The options that change list CONTENT go in the key as well. The archive
			// has not changed but what we put in the list has, and a cache that
			// ignored that would serve rows built under the other setting.
			const int n = _snprintf_s(out, (size_t)outBytes, _TRUNCATE,
			    "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X"
			    ".%I64u.mon%d",
			    md5[0], md5[1], md5[2], md5[3], md5[4], md5[5], md5[6], md5[7],
			    md5[8], md5[9], md5[10], md5[11], md5[12], md5[13], md5[14], md5[15],
			    (unsigned __int64)size.QuadPart, BattleMonsterNames() ? 1 : 0);
			return n > 0;
		}

		// The key the manifest on disk was written with, or an empty string.
		bool ReadManifestKey(char* out, int outBytes)
		{
			out[0] = 0;

			wchar_t path[MAX_PATH];
			if (!ManifestPath(path, MAX_PATH))
				return false;

			HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
			    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file == INVALID_HANDLE_VALUE)
				return false;

			char buf[2048];
			DWORD got = 0;
			const BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &got, NULL);
			CloseHandle(file);
			if (!ok || got == 0)
				return false;
			buf[got] = 0;

			// The "key\t<value>" line, whether it is the first line or a later one.
			const char* value = NULL;
			if (strncmp(buf, "key\t", 4) == 0)
			{
				value = buf + 4;
			}
			else
			{
				const char* at = strstr(buf, "\nkey\t");
				if (at)
					value = at + 5;
			}
			if (!value)
				return false;

			int i = 0;
			for (; i < outBytes - 1 && value[i] && value[i] != '\n' && value[i] != '\r'; ++i)
				out[i] = value[i];
			out[i] = 0;

			// A manifest from an older format is not usable even if the key matches.
			const char* fmt = strstr(buf, "format\t");
			if (!fmt || atoi(fmt + 7) != kFormatVersion)
			{
				out[0] = 0;
				return false;
			}
			return out[0] != 0;
		}

		bool WriteManifest(const char* key, const workshop::CacheableList* lists, int count)
		{
			wchar_t path[MAX_PATH];
			if (!ManifestPath(path, MAX_PATH))
				return false;

			HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			    FILE_ATTRIBUTE_NORMAL, NULL);
			if (file == INVALID_HANDLE_VALUE)
			{
				Log("list cache: could not write the manifest, GetLastError=%lu", GetLastError());
				return false;
			}

			char text[4096];
			int n = _snprintf_s(text, sizeof(text), _TRUNCATE,
			    "# Al Bhed Workshop list export.\n"
			    "#\n"
			    "# These are the lists the game cannot change without changing its\n"
			    "# archive, so they are built once and read back after that. The key is\n"
			    "# the MD5 FFX_Data.vbf stores of its own header, which covers every\n"
			    "# file in it, plus the archive size and the options that affect list\n"
			    "# content. Any change to it rebuilds every list on the next boot.\n"
			    "#\n"
			    "# Delete this file to force a rebuild from the game.\n"
			    "format\t%d\n"
			    "key\t%s\n",
			    kFormatVersion, key);

			for (int i = 0; i < count && n > 0 && n < (int)sizeof(text) - 128; ++i)
			{
				n += _snprintf_s(text + n, sizeof(text) - (size_t)n, _TRUNCATE,
				    "list\t%s\t%d\n", lists[i].name, lists[i].list->Count());
			}

			DWORD wrote = 0;
			const BOOL ok = WriteFile(file, text, (DWORD)n, &wrote, NULL);
			CloseHandle(file);
			return ok != 0 && wrote == (DWORD)n;
		}

		// ---------------------------------------------------------------------------
		// The event package measurement, as its own file.
		//
		// It is not a PickerList, it is one row per event id carrying what the package
		// measures and whether the asset path table has an entry. It gets its own file
		// because it is BOTH the expensive half of a boot, 402 engine file opens, and
		// the half that matters for safety: EventIdLoadable answers from it, and
		// loading an id whose package does not ship puts the game in a while(1) with
		// no way out.
		//
		//     id  bytes  haspath  label
		//     0   114688 1        ev/evtitle
		// ---------------------------------------------------------------------------
		bool ProbePath(wchar_t* out, int max)
		{
			wchar_t dir[MAX_PATH];
			if (!GameDir(dir, MAX_PATH))
				return false;
			return _snwprintf_s(out, (size_t)max, _TRUNCATE,
			           L"%s\\AlBhedWorkshop\\data\\event-packages.tsv", dir)
			    > 0;
		}

		bool WriteProbeTable()
		{
			wchar_t path[MAX_PATH];
			if (!ProbePath(path, MAX_PATH))
				return false;

			HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			    FILE_ATTRIBUTE_NORMAL, NULL);
			if (file == INVALID_HANDLE_VALUE)
				return false;

			const int count = EventProbeCount();

			char text[48 * 1024];
			int n = _snprintf_s(text, sizeof(text), _TRUNCATE,
			    "# Al Bhed Workshop event package measurement\n"
			    "# Produced by asking the engine to open each package, which is the only\n"
			    "# honest existence test. bytes 0 means the id has no shipped package and\n"
			    "# loading it hangs the game in FFX_Ev_LoadEventPackage.\n"
			    "# rows\t%d\n"
			    "# id\tbytes\thaspath\tlabel\n",
			    count);

			for (int id = 0; id < count && n > 0 && n < (int)sizeof(text) - 160; ++id)
			{
				unsigned bytes = 0;
				bool hasPath = false;
				char label[64];
				if (!EventProbeRow(id, &bytes, &hasPath, label, (int)sizeof(label)))
					continue;

				n += _snprintf_s(text + n, sizeof(text) - (size_t)n, _TRUNCATE,
				    "%d\t%u\t%d\t%s\n", id, bytes, hasPath ? 1 : 0, label);
			}

			DWORD wrote = 0;
			const BOOL ok = WriteFile(file, text, (DWORD)n, &wrote, NULL);
			CloseHandle(file);
			return ok != 0 && wrote == (DWORD)n;
		}

		bool ReadProbeTable()
		{
			wchar_t path[MAX_PATH];
			if (!ProbePath(path, MAX_PATH))
				return false;

			HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
			    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file == INVALID_HANDLE_VALUE)
				return false;

			static char buf[48 * 1024];
			DWORD got = 0;
			const BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &got, NULL);
			CloseHandle(file);
			if (!ok || got == 0)
				return false;
			buf[got] = 0;

			const char* rowsAt = strstr(buf, "# rows\t");
			if (!rowsAt)
				return false;
			const int claimed = atoi(rowsAt + 7);
			if (claimed != EventProbeCount())
			{
				Log("list cache: event-packages.tsv has %d rows but this build expects "
				    "%d, so it is being ignored",
				    claimed, EventProbeCount());
				return false;
			}

			// Parsed into a scratch table first. A half applied probe table is the one
			// thing that must never happen, because a missing row reads as "ships" and
			// a warp to it hangs the game.
			struct Row
			{
				unsigned bytes;
				bool hasPath;
				bool seen;
				char label[64];
			};
			static Row rows[1024];
			if (claimed > (int)(sizeof(rows) / sizeof(rows[0])))
				return false;
			memset(rows, 0, sizeof(rows));

			char* at = buf;
			int parsed = 0;
			while (*at)
			{
				char* line = at;
				while (*at && *at != '\n' && *at != '\r')
					++at;
				if (*at)
				{
					const char held = *at;
					*at = 0;
					++at;
					if (held == '\r' && *at == '\n')
						++at;
				}

				if (!line[0] || line[0] == '#')
					continue;

				// id, bytes, haspath, label
				char* f1 = strchr(line, '\t');
				if (!f1)
					continue;
				*f1++ = 0;
				char* f2 = strchr(f1, '\t');
				if (!f2)
					continue;
				*f2++ = 0;
				char* f3 = strchr(f2, '\t');
				if (!f3)
					continue;
				*f3++ = 0;

				const int id = atoi(line);
				if (id < 0 || id >= claimed)
					continue;

				rows[id].bytes = (unsigned)strtoul(f1, NULL, 10);
				rows[id].hasPath = atoi(f2) != 0;
				rows[id].seen = true;
				int i = 0;
				for (; i < (int)sizeof(rows[id].label) - 1 && f3[i]; ++i)
					rows[id].label[i] = f3[i];
				rows[id].label[i] = 0;
				++parsed;
			}

			if (parsed != claimed)
			{
				Log("list cache: event-packages.tsv claims %d rows and parsed %d, so it "
				    "is being ignored rather than half applied",
				    claimed, parsed);
				return false;
			}
			for (int id = 0; id < claimed; ++id)
			{
				if (!rows[id].seen)
				{
					Log("list cache: event-packages.tsv has no row for id %d, so it is "
					    "being ignored. A missing row would read as shipped and a warp "
					    "to it would hang the game.",
					    id);
					return false;
				}
			}

			// ONLY NOW is it applied.
			for (int id = 0; id < claimed; ++id)
				SetEventProbeRow(id, rows[id].bytes, rows[id].hasPath, rows[id].label);

			MarkEventPackagesProbed("read from the export cache, not measured this boot");
			return true;
		}

	} // namespace

	bool ExportedListsFresh()
	{
		char want[128];
		if (!ArchiveKey(want, sizeof(want)))
		{
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "the archive could not be read, so the cache cannot be trusted");
			return false;
		}

		char have[128];
		if (!ReadManifestKey(have, sizeof(have)))
		{
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "no usable manifest, so the lists will be built from the game");
			return false;
		}

		if (strcmp(want, have) != 0)
		{
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "the archive or the options changed, so the lists will be rebuilt");
			return false;
		}
		return true;
	}

	bool LoadExportedLists()
	{
		if (!ExportedListsFresh())
			return false;

		// THE PROBE TABLE FIRST. Without it the lists would load but EventIdLoadable
		// would fall back to the deny list baked into this build, and the whole point
		// of the measurement is that it beats that list.
		if (!ReadProbeTable())
		{
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "the event package table is missing or unusable, rebuilding");
			return false;
		}

		workshop::CacheableList lists[kMaxLists];
		const int count = Collect(lists, kMaxLists);

		// ALL OR NOTHING. A partial load would leave some lists from the cache and
		// some empty, and the caller has no way to ask for just the missing ones.
		// Loading into the real lists and then bailing is fine, because the fallback
		// is a full rebuild which resets every one of them anyway.
		int loaded = 0;
		int rows = 0;
		for (int i = 0; i < count; ++i)
		{
			wchar_t path[MAX_PATH];
			if (!CacheFilePath(lists[i].name, path, MAX_PATH))
				break;

			if (!workshop::ReadPickerList(path, lists[i].list, g_sources[i],
			        (int)sizeof(g_sources[i])))
			{
				Log("list cache: %s is missing or unreadable, so everything is being "
				    "rebuilt from the game",
				    lists[i].name);
				break;
			}
			++loaded;
			rows += lists[i].list->Count();
		}

		if (loaded != count)
		{
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "%d of %d files loaded before one failed, rebuilding", loaded, count);
			return false;
		}

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "%d lists, %d rows, read from disk with no engine calls", loaded, rows);
		Log("list cache: %s", g_status);
		return true;
	}

	bool WriteExportedLists()
	{
		char key[128];
		if (!ArchiveKey(key, sizeof(key)))
		{
			Log("list cache: the archive key could not be read, so nothing was written");
			return false;
		}

		workshop::CacheableList lists[kMaxLists];
		const int count = Collect(lists, kMaxLists);

		int wrote = 0;
		int rows = 0;
		for (int i = 0; i < count; ++i)
		{
			const workshop::PickerList& list = *lists[i].list;

			// NEVER CACHE A FAILURE. An empty or unlive list means the engine was not
			// ready, and writing it would make the next boot trust the emptiness and
			// never ask again.
			if (list.Empty() || !list.Live())
			{
				Log("list cache: %s is %s, so the cache was not written. It will be "
				    "tried again on the next boot.",
				    lists[i].name, list.Empty() ? "empty" : "not live");
				return false;
			}

			wchar_t path[MAX_PATH];
			if (!CacheFilePath(lists[i].name, path, MAX_PATH))
				return false;
			if (!workshop::WritePickerList(path, lists[i].name, list))
				return false;

			++wrote;
			rows += list.Count();
		}

		if (!WriteProbeTable())
		{
			Log("list cache: the event package table could not be written, so no "
			    "manifest was written either and the cache stays unused");
			return false;
		}

		// The manifest goes LAST, because it is what makes the cache usable. If any
		// list write failed above, there is no manifest and the whole cache is
		// ignored rather than half trusted.
		if (!WriteManifest(key, lists, count))
			return false;

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "%d lists, %d rows, written to disk", wrote, rows);
		Log("list cache: %s. The next boot reads these instead of asking the engine.",
		    g_status);
		return true;
	}

	const char* ExportedListsStatus()
	{
		return g_status;
	}

	void DiscardExportedLists()
	{
		wchar_t path[MAX_PATH];
		if (ManifestPath(path, MAX_PATH))
			DeleteFileW(path);

		workshop::CacheableList lists[kMaxLists];
		const int count = Collect(lists, kMaxLists);
		for (int i = 0; i < count; ++i)
		{
			if (CacheFilePath(lists[i].name, path, MAX_PATH))
				DeleteFileW(path);
		}
		if (ProbePath(path, MAX_PATH))
			DeleteFileW(path);

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "discarded, will rebuild");
		Log("list cache: discarded. The lists will be rebuilt from the game and "
		    "written again.");
	}

} // namespace ffx
