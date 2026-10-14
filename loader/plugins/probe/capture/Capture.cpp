#include "capture/Capture.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ProbeSettings.h"
#include "ffx/MainLoop.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace probe
{

	using workshop::Log;
	using workshop::RecordFile;

	StepCursor cursor = { 0, 0, 0, 0, 0, 0 };

	namespace
	{

		// Set by the control thread, read by the step thread.
		volatile LONG g_recording = 0;
		volatile LONG g_inside = 0;
		volatile LONG g_limitStop = 0;

		// Written only while g_recording is 0 and nobody is inside.
		RecordFile* g_files[KindCount] = { NULL, NULL, NULL, NULL, NULL };
		DWORD g_seq[KindCount] = { 0, 0, 0, 0, 0 };
		DWORD g_jointCapacity = 0; // the open joints file's, 0 when there is none

		bool g_running = false; // control thread only
		DWORD g_sessionIndex = 0;
		wchar_t g_sessionStem[MAX_PATH];

		const char* KindName(int kind)
		{
			switch (kind)
			{
			case KindPacing:
				return "pacing";
			case KindCamera:
				return "camera";
			case KindMotion:
				return "motion";
			case KindJoints:
				return "joints";
			}
			return "unknown";
		}

		// CreateDirectoryW one level at a time, because it will not make parents.
		bool MakeDirectories(const wchar_t* path)
		{
			wchar_t partial[MAX_PATH];
			const size_t length = wcslen(path);
			if (length == 0 || length >= MAX_PATH)
				return false;
			for (size_t i = 0; i <= length; ++i)
			{
				const wchar_t c = path[i];
				partial[i] = c;
				if ((c == L'\\' || c == L'/' || c == 0) && i > 0 && path[i - 1] != L':')
				{
					partial[i] = 0;
					if (!CreateDirectoryW(partial, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
					{
						Log("capture: could not create the folder %S, error %lu", partial,
						    GetLastError());
						return false;
					}
					partial[i] = c;
				}
			}
			return true;
		}

		// The folder from probe.output_dir, which is UTF-8 because that is what a text
		// editor saves, or %LOCALAPPDATA%\AlBhedWorkshop\probe. Never the game folder.
		bool OutputFolder(wchar_t* out, size_t count)
		{
			if (settings.outputDir[0])
			{
				if (!MultiByteToWideChar(CP_UTF8, 0, settings.outputDir, -1, out, (int)count))
					return false;
			}
			else
			{
				wchar_t local[MAX_PATH];
				const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
				if (n == 0 || n >= MAX_PATH)
				{
					Log("capture: LOCALAPPDATA is not set, so set probe.output_dir");
					return false;
				}
				_snwprintf_s(out, count, _TRUNCATE, L"%s\\AlBhedWorkshop\\probe", local);
			}

			// Trailing separators would make an empty last component.
			size_t length = wcslen(out);
			while (length > 0 && (out[length - 1] == L'\\' || out[length - 1] == L'/'))
				out[--length] = 0;
			return length > 0;
		}

		RecordFile* OpenKind(FixtureKind kind, DWORD recordBytes, const void* extension,
		    DWORD extensionBytes, const FILETIME& start)
		{
			BYTE header[512];
			if (sizeof(FileHeader) + extensionBytes > sizeof(header))
				return NULL;
			ZeroMemory(header, sizeof(header));

			FileHeader* h = (FileHeader*)header;
			memcpy(h->magic, FormatMagic, sizeof(h->magic));
			h->version = FormatVersion;
			h->kind = (WORD)kind;
			h->headerBytes = (DWORD)sizeof(FileHeader) + extensionBytes;
			h->recordBytes = recordBytes;
			h->exeTimeDateStamp = workshop::ModuleTimeDateStamp();
			h->exeSizeOfImage = workshop::ModuleImageSize();
			h->exeCheckSum = workshop::ModuleCheckSum();
			h->exeModuleBase = (DWORD)(UINT_PTR)workshop::ModuleBase();
			h->sessionIndex = g_sessionIndex;
			h->startFileTime = ((unsigned long long)start.dwHighDateTime << 32) | start.dwLowDateTime;
			h->extensionBytes = extensionBytes;
			if (extensionBytes)
				memcpy(header + sizeof(FileHeader), extension, extensionBytes);

			wchar_t path[MAX_PATH];
			_snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s_%S.bin", g_sessionStem, KindName(kind));

			const DWORD budget = (DWORD)settings.budgetMegabytes * 1024u * 1024u;
			RecordFile* file = workshop::OpenRecordFile(path, header, h->headerBytes, recordBytes, budget);
			if (file)
				Log("capture: %s -> %S (%lu byte records)", KindName(kind), path, recordBytes);
			return file;
		}

		void CloseAll()
		{
			for (int kind = 1; kind < KindCount; ++kind)
			{
				RecordFile* file = g_files[kind];
				if (!file)
					continue;
				workshop::RecordFileStats stats;
				workshop::ReadRecordFileStats(file, &stats);
				workshop::CloseRecordFile(file);
				g_files[kind] = NULL;

				// Read before the close, so bytesWritten can trail the last chunk. The
				// record count is exact and is what a reader will find.
				Log("capture: %s closed, %llu records, %llu dropped, %lu write errors",
				    KindName(kind), stats.recordsCommitted, stats.recordsDropped,
				    stats.writeErrors);
			}
		}

	} // namespace

	// ---------------------------------------------------------------------------
	// Step thread
	// ---------------------------------------------------------------------------

	bool BeginRecord()
	{
		if (!g_recording)
			return false;
		InterlockedIncrement(&g_inside);
		if (!g_recording)
		{
			InterlockedDecrement(&g_inside);
			return false;
		}
		return true;
	}

	void EndRecord()
	{
		InterlockedDecrement(&g_inside);
	}

	RecordFile* FileFor(FixtureKind kind)
	{
		return (kind >= 1 && kind < KindCount) ? g_files[kind] : NULL;
	}

	DWORD NextSeq(FixtureKind kind)
	{
		return (kind >= 1 && kind < KindCount) ? g_seq[kind]++ : 0;
	}

	void FillPrefix(RecordPrefix* prefix, FixtureKind kind, WORD callIndex, BYTE phase)
	{
		DWORD tick = 0, step = 0;
		ffx::PhyreFrameTickCount(&tick);
		ffx::MainStepCounter(&step);
		prefix->seq = NextSeq(kind);
		prefix->phyreFrameTick = tick;
		prefix->mainStepCounter = step;
		prefix->pacingOrdinal = cursor.pacingOrdinal;
		prefix->callIndex = callIndex;
		prefix->phase = phase;
	}

	void NoteStepForLimit()
	{
		if (!g_recording || settings.maxSteps <= 0)
			return;
		// Called before this step's BeginRecord, so the step that crosses the limit is the
		// first one not recorded and the file holds exactly maxSteps pacing records when
		// the capture started between steps. Clearing the flag here, on the step thread,
		// is what makes it exact. The control thread only closes the files afterwards.
		if (cursor.pacingOrdinal - cursor.sessionFirstOrdinal > (DWORD)settings.maxSteps)
		{
			InterlockedExchange(&g_recording, 0);
			InterlockedExchange(&g_limitStop, 1);
		}
	}

	// ---------------------------------------------------------------------------
	// Control thread
	// ---------------------------------------------------------------------------

	bool StartCapture()
	{
		if (g_running)
			return true;

		wchar_t folder[MAX_PATH];
		if (!OutputFolder(folder, MAX_PATH) || !MakeDirectories(folder))
		{
			Log("capture: no usable output folder, not starting");
			return false;
		}

		SYSTEMTIME local;
		GetLocalTime(&local);
		FILETIME start;
		GetSystemTimeAsFileTime(&start);
		_snwprintf_s(g_sessionStem, MAX_PATH, _TRUNCATE, L"%s\\probe_%04u%02u%02u_%02u%02u%02u_%lu",
		    folder, local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute,
		    local.wSecond, g_sessionIndex);

		BYTE extension[256];
		if (settings.pacing)
			g_files[KindPacing] = OpenKind(KindPacing, sizeof(PacingRecord), NULL, 0, start);
		if (settings.camera)
		{
			const DWORD n = CameraExtensionBytes(extension, sizeof(extension));
			g_files[KindCamera] = OpenKind(KindCamera, sizeof(CameraRecord), extension, n, start);
		}
		if (settings.motion && MotionSpansConsistent())
		{
			const DWORD n = MotionExtensionBytes(extension, sizeof(extension));
			g_files[KindMotion] = OpenKind(KindMotion, sizeof(MotionRecord), extension, n, start);
		}

		g_jointCapacity = 0;
		if (settings.joints && JointsSpansConsistent())
		{
			// Read once here, so the record size cannot change under an open file.
			const DWORD capacity = (DWORD)settings.jointsMax;
			const DWORD n = JointsExtensionBytes(extension, sizeof(extension), capacity);
			g_files[KindJoints] = OpenKind(KindJoints, JointsRecordBytes(capacity), extension, n, start);
			if (g_files[KindJoints])
				g_jointCapacity = capacity;
		}

		if (!g_files[KindPacing] && !g_files[KindCamera] && !g_files[KindMotion] && !g_files[KindJoints])
		{
			Log("capture: no file could be opened, or every recipe is off, not starting");
			return false;
		}

		for (int i = 0; i < KindCount; ++i)
			g_seq[i] = 0;
		// Racy by one step at most, and only for the limit, which counts from here.
		cursor.sessionFirstOrdinal = cursor.pacingOrdinal;
		InterlockedExchange(&g_limitStop, 0);

		g_running = true;
		InterlockedExchange(&g_recording, 1);
		Log("capture: session %lu started at step ordinal %lu%s", g_sessionIndex,
		    cursor.pacingOrdinal, settings.maxSteps > 0 ? ", with a step limit" : "");
		workshop::SetStatus("probe: recording session %lu", g_sessionIndex);
		return true;
	}

	void StopCapture(const char* why)
	{
		if (!g_running)
			return;

		InterlockedExchange(&g_recording, 0);

		// Wait for any record in flight. A hook holds the counter across one engine call,
		// so this is milliseconds. If the game thread is hung inside one, closing the files
		// under it would be a crash, so leave them open and say so.
		const DWORD start = GetTickCount();
		while (g_inside > 0)
		{
			if (GetTickCount() - start > 5000)
			{
				Log("capture: the step thread has been inside a record for 5 s, leaving the "
				    "files open rather than closing them under it");
				return;
			}
			Sleep(1);
		}

		Log("capture: session %lu stopping (%s) after %lu steps", g_sessionIndex, why,
		    cursor.pacingOrdinal - cursor.sessionFirstOrdinal);
		CloseAll();
		g_running = false;
		++g_sessionIndex;
		workshop::SetStatus("probe: idle, %lu session%s recorded", g_sessionIndex,
		    g_sessionIndex == 1 ? "" : "s");
	}

	DWORD JointCapacity()
	{
		return g_jointCapacity;
	}

	bool CaptureRunning()
	{
		return g_running;
	}

	bool TakeLimitStopRequest()
	{
		return InterlockedExchange(&g_limitStop, 0) != 0;
	}

} // namespace probe
