#include "workshop/HangWatchdog.h"

#include <stdio.h>

#include "workshop/CrashHandler.h"
#include "workshop/Log.h"

namespace workshop
{

	namespace
	{

		const DWORD kMagic = 0x57484241; // "ABHW"

		// Shared across every module in the process, same reason and same mechanism as
		// the crash handler's block. Two plugins both call Start, one gets the thread,
		// and both of their step notes land on one counter.
		struct Shared
		{
			DWORD magic;
			LONG owner;       // base of the module running the thread, 0 if nobody
			LONG steps;       // bumped by the game thread
			DWORD gameThread; // whoever last bumped it
			DWORD thresholdMs;
			DWORD reports;
			LONG hung; // 1 while the watchdog thinks it is stuck
		};

		HANDLE g_section = NULL;
		Shared* g_shared = NULL;

		Shared* State()
		{
			if (g_shared)
				return g_shared;

			// OpenFileMapping first would be a race against another module creating it,
			// so create unconditionally. CreateFileMapping on an existing name opens it,
			// which is exactly what is wanted here.
			if (!g_section)
			{
				g_section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
				    0, sizeof(Shared), L"Local\\AlBhedWorkshopHangState");
			}
			if (!g_section)
				return NULL;

			g_shared = (Shared*)MapViewOfFile(g_section, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
			if (!g_shared)
				return NULL;

			// A fresh section is zeroed by the kernel, so this only ever runs once and
			// only for the module that got there first.
			if (g_shared->magic != kMagic)
			{
				g_shared->thresholdMs = 10000;
				g_shared->magic = kMagic;
			}
			return g_shared;
		}

		DWORD SelfBase()
		{
			HMODULE self = NULL;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			        (LPCWSTR)&SelfBase, &self))
			{
				return 0;
			}
			return (DWORD)(UINT_PTR)self;
		}

		DWORD WINAPI WatchThread(LPVOID)
		{
			Shared* s = State();
			if (!s)
				return 0;

			LONG lastSteps = 0;
			DWORD lastMoved = GetTickCount();
			int reportsThisEpisode = 0;
			DWORD lastReport = 0;
			bool everStepped = false;

			for (;;)
			{
				Sleep(250);

				const LONG steps = s->steps;
				const DWORD threadId = s->gameThread;
				const DWORD now = GetTickCount();

				if (!threadId)
					continue; // no step has run yet, so there is nothing to watch

				if (!everStepped)
				{
					everStepped = true;
					Log("hang watchdog: watching thread %lu, a freeze counts after %lu ms",
					    threadId, s->thresholdMs);
				}

				if (steps != lastSteps)
				{
					if (reportsThisEpisode > 0)
					{
						Log("hang watchdog: thread %lu started stepping again after %lu ms "
						    "and %d report(s). It was a long stall, not a deadlock.",
						    threadId, now - lastMoved, reportsThisEpisode);
					}
					lastSteps = steps;
					lastMoved = now;
					reportsThisEpisode = 0;
					lastReport = 0;
					InterlockedExchange(&s->hung, 0);
					continue;
				}

				// GetTickCount wraps every 49 days. The subtraction is correct across
				// the wrap as long as it stays in unsigned arithmetic, which it does.
				const DWORD stalled = now - lastMoved;
				if (stalled < s->thresholdMs)
					continue;

				InterlockedExchange(&s->hung, 1);

				// SIX AT MOST, AND SPREAD OUT. The first one is the evidence. The second
				// is what proves it is a spin rather than something slow, because the
				// same EIP twice twenty seconds apart cannot be progress. After that it
				// is just filling the log, so it tapers off and stops.
				if (reportsThisEpisode >= 6)
					continue;
				if (reportsThisEpisode > 0 && (now - lastReport) < 20000)
					continue;

				char why[160];
				_snprintf_s(why, sizeof(why), _TRUNCATE,
				    "THE GAME THREAD HAS NOT STEPPED FOR %lu ms. Sample %d of up to 6.",
				    stalled, reportsThisEpisode + 1);

				const bool ok = WriteThreadReportNow(threadId, why);
				++reportsThisEpisode;
				if (ok)
					++s->reports;

				Log("hang watchdog: thread %lu has been stuck for %lu ms. %s",
				    threadId, stalled,
				    ok ? "A report with its call stack went into albhed_crash.log."
				       : "The report could not be written, another one may have been in "
				         "flight.");

				lastReport = now;
			}
		}

	} // namespace

	bool StartHangWatchdog()
	{
		Shared* s = State();
		if (!s)
			return false;

		const DWORD self = SelfBase();
		const DWORD claim = self ? self : 1;

		// One thread per process. Whoever wins the swap runs it.
		if (InterlockedCompareExchange(&s->owner, (LONG)claim, 0) != 0)
			return true; // somebody else already owns it, which is fine

		HANDLE thread = CreateThread(NULL, 0, WatchThread, NULL, 0, NULL);
		if (!thread)
		{
			InterlockedExchange(&s->owner, 0);
			return false;
		}
		CloseHandle(thread);
		return true;
	}

	void NoteHangWatchdogStep()
	{
		Shared* s = State();
		if (!s)
			return;
		InterlockedIncrement(&s->steps);
		s->gameThread = GetCurrentThreadId();
	}

	void SetHangThresholdMs(DWORD ms)
	{
		Shared* s = State();
		if (!s)
			return;
		// Below a second this would report every hitch, and FFX legitimately takes a
		// couple of seconds to load a map.
		s->thresholdMs = ms < 1000 ? 1000 : ms;
	}

	DWORD HangThresholdMs()
	{
		Shared* s = State();
		return s ? s->thresholdMs : 0;
	}

	bool HangWatchdogRunning()
	{
		Shared* s = State();
		return s && s->owner != 0;
	}

	DWORD HangReportCount()
	{
		Shared* s = State();
		return s ? s->reports : 0;
	}

	bool GameThreadLooksHung()
	{
		Shared* s = State();
		return s && s->hung != 0;
	}

	DWORD GameThreadId()
	{
		Shared* s = State();
		return s ? s->gameThread : 0;
	}

} // namespace workshop
