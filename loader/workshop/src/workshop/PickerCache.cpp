#include "workshop/PickerCache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "workshop/Log.h"

namespace workshop
{

	namespace
	{

		// 4096 rows of a label up to a couple of hundred bytes, plus headers. One
		// megabyte is comfortably over the worst case and is a single read.
		const int kMaxFileBytes = 1024 * 1024;

		// Creates every directory in the path except the file name itself.
		// CreateDirectoryW only makes one level, and the data folder may not exist.
		void EnsureDirectories(const wchar_t* path)
		{
			wchar_t work[MAX_PATH];
			int n = 0;
			for (; n < MAX_PATH - 1 && path[n]; ++n)
				work[n] = path[n];
			if (n >= MAX_PATH - 1)
				return; // too long to be one of ours, and truncating would be worse
			work[n] = 0;

			for (int i = 0; work[i]; ++i)
			{
				if (work[i] != L'\\' && work[i] != L'/')
					continue;

				const wchar_t held = work[i];
				work[i] = 0;
				// A drive root like "G:" fails and that is fine, keep going.
				if (work[0] && work[1] == L':' && work[2] == 0)
				{
					work[i] = held;
					continue;
				}
				CreateDirectoryW(work, NULL);
				work[i] = held;
			}
		}

		bool WriteAll(HANDLE file, const char* text, int bytes)
		{
			int done = 0;
			while (done < bytes)
			{
				DWORD wrote = 0;
				if (!WriteFile(file, text + done, (DWORD)(bytes - done), &wrote, NULL) || wrote == 0)
					return false;
				done += (int)wrote;
			}
			return true;
		}

		// Reads the whole file into buf and zero terminates it. Returns the length, or
		// -1 when the file is missing or too big to be one of ours.
		int ReadWholeFile(const wchar_t* path, char* buf, int bufBytes)
		{
			HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
			    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file == INVALID_HANDLE_VALUE)
				return -1;

			LARGE_INTEGER size;
			size.QuadPart = 0;
			if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0
			    || size.QuadPart >= (LONGLONG)(bufBytes - 1))
			{
				CloseHandle(file);
				return -1;
			}

			DWORD got = 0;
			const BOOL ok = ReadFile(file, buf, (DWORD)size.QuadPart, &got, NULL);
			CloseHandle(file);
			if (!ok)
				return -1;

			buf[got] = 0;
			return (int)got;
		}

		// Advances past the current line and returns the start of the next, or NULL at
		// the end. Zero terminates the line in place, so the caller gets a C string.
		char* TakeLine(char** cursor)
		{
			char* line = *cursor;
			if (!line || !*line)
				return NULL;

			char* p = line;
			while (*p && *p != '\n' && *p != '\r')
				++p;

			if (*p)
			{
				const char held = *p;
				*p = 0;
				++p;
				// Tolerate CRLF even though we only ever write LF, because a text editor
				// or a git checkout with the wrong setting may have changed it.
				if (held == '\r' && *p == '\n')
					++p;
			}
			*cursor = p;
			return line;
		}

	} // namespace

	bool WritePickerList(const wchar_t* path, const char* listName, const PickerList& list)
	{
		EnsureDirectories(path);

		HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		    FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			Log("list cache: could not write %S, GetLastError=%lu", path, GetLastError());
			return false;
		}

		char head[512];
		const int headLen = _snprintf_s(head, sizeof(head), _TRUNCATE,
		    "# Al Bhed Workshop list export, do not hand edit the counts\n"
		    "# list\t%s\n"
		    "# source\t%s\n"
		    "# rows\t%d\n",
		    listName ? listName : "?",
		    list.Source() ? list.Source() : "",
		    list.Count());

		bool ok = headLen > 0 && WriteAll(file, head, headLen);

		const PickerItem* items = list.Items();
		for (int i = 0; ok && i < list.Count(); ++i)
		{
			// The label is the only free text in the file, so it is the only thing
			// that can break the format. A tab would invent a column and a newline
			// would invent a row. Game names have never contained either, this is so
			// a surprise costs a label rather than the whole file.
			char label[384];
			const char* src = items[i].label ? items[i].label : "";
			int w = 0;
			for (; w < (int)sizeof(label) - 1 && src[w]; ++w)
			{
				const char c = src[w];
				label[w] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
			}
			label[w] = 0;

			char row[512];
			const int n = _snprintf_s(row, sizeof(row), _TRUNCATE, "%d\t%s\n",
			    items[i].id, label);
			if (n <= 0)
				continue;

			ok = WriteAll(file, row, n);
		}

		CloseHandle(file);
		if (!ok)
			Log("list cache: the write of %S failed partway", path);
		return ok;
	}

	bool ReadPickerList(const wchar_t* path, PickerList* out, char* sourceBuf, int sourceBytes)
	{
		if (!out || !sourceBuf || sourceBytes <= 0)
			return false;

		// One buffer, reused across every list, because this runs one list at a time
		// and a megabyte of stack is not available.
		static char buf[kMaxFileBytes];

		const int length = ReadWholeFile(path, buf, kMaxFileBytes);
		if (length <= 0)
			return false;

		// ONE PASS OVER EVERY LINE, header and data together.
		//
		// THIS USED TO READ THE HEADER IN ITS OWN LOOP AND THEN REWIND THE CURSOR to
		// hand the first data line back, and that read exactly one row out of 330.
		// TakeLine zero terminates each line in place, so by the time the cursor was
		// rewound the newline after that line had already been overwritten with a
		// zero, and the next read stopped there. The file was fine, the reader was not.
		//
		// The row count is always written before the data, so the list can be reset
		// lazily on the first data line. That is also what keeps a file that is not
		// ours from writing anything into the list, which the caller depends on because
		// its fallback is to rebuild from the engine and it has to start clean.
		char* cursor = buf;
		int claimedRows = -1;
		int rows = 0;
		bool started = false;
		sourceBuf[0] = 0;

		for (;;)
		{
			char* line = TakeLine(&cursor);
			if (!line)
				break;

			if (line[0] == '#')
			{
				if (strncmp(line, "# source\t", 9) == 0)
					_snprintf_s(sourceBuf, (size_t)sourceBytes, _TRUNCATE, "%s", line + 9);
				else if (strncmp(line, "# rows\t", 7) == 0)
					claimedRows = atoi(line + 7);
				continue;
			}

			if (!line[0])
				continue;

			char* tab = strchr(line, '\t');
			if (!tab)
				continue;
			*tab = 0;

			if (!started)
			{
				// A data row before a row count means this is not our format, and
				// nothing has been written into the list yet.
				if (claimedRows < 0)
					return false;
				out->Reset(sourceBuf[0] ? sourceBuf : "exported list");
				started = true;
			}

			if (!out->Add(atoi(line), tab + 1))
				break; // full, and PickerList has already recorded the overflow
			++rows;
		}

		if (claimedRows < 0)
			return false; // no row count anywhere, so not our format

		if (rows != claimedRows)
		{
			Log("list cache: %S claims %d rows but has %d, so it is being ignored",
			    path, claimedRows, rows);
			if (started)
				out->Reset(NULL);
			return false;
		}

		// A file with a zero row count never went through the Reset above, and
		// SetLive on a list nobody reset would mark whatever was in it as good.
		if (!started)
			out->Reset(sourceBuf[0] ? sourceBuf : "exported list");

		out->SetLive(true);
		return true;
	}

	int PickerListFileRows(const wchar_t* path)
	{
		static char buf[4096];
		HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
			return -1;

		DWORD got = 0;
		const BOOL ok = ReadFile(file, buf, sizeof(buf) - 1, &got, NULL);
		CloseHandle(file);
		if (!ok || got == 0)
			return -1;
		buf[got] = 0;

		const char* at = strstr(buf, "# rows\t");
		return at ? atoi(at + 7) : -1;
	}

} // namespace workshop
