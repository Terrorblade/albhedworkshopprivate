// Exercises the crash handler with no game involved. Two reports come out of a run:
// one asked for by hand, one from a real access violation, so the on-demand path and
// the filter path are both covered along with the PEB walk and the frame walk.

#include "workshop/CrashHandler.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

// Three nested frames, so the frame pointer walk and the stack scan have something
// to find and the order can be checked by eye.
__declspec(noinline) void Innermost(int mode)
{
	workshop::CrashContext("test: Innermost, mode %d", mode);

	if (mode == 0)
	{
		if (!workshop::WriteCrashReportNow("the on demand path"))
			printf("WriteCrashReportNow FAILED\n");
		else
			printf("on demand report written\n");
		return;
	}

	// A real one. The filter should catch this, write the second report, then hand on
	// to the default handler and let the process die.
	printf("faulting on purpose now\n");
	volatile int* p = (volatile int*)0x00000010;
	*p = 0x41424344;
}

__declspec(noinline) void Middle(int mode)
{
	workshop::CrashScope scope("test: Middle");
	Innermost(mode);
}

__declspec(noinline) void Outer(int mode)
{
	workshop::CrashContext("test: Outer");
	Middle(mode);
}

// ---------------------------------------------------------------------------
// The foreign thread path, which is what the hang watchdog uses.
//
// A worker goes into a spin inside three nested frames, main reports it, and the
// report has to come out with the WORKER's frames in it rather than main's. That
// is the whole point: the report is written by one thread about another, so the
// stack bounds have to come from the subject's stack pointer and not from the
// writer's TEB.
//
// WHAT TO LOOK FOR: the thread line should name the worker and say the watchdog
// read it from another thread, and the call chain should contain Spinner,
// SpinMiddle and SpinOuter. If both chains come out empty the bounds are wrong.
// ---------------------------------------------------------------------------

volatile LONG g_spinning = 0;
volatile LONG g_stop = 0;

__declspec(noinline) void Spinner()
{
	workshop::CrashContext("test: Spinner is about to spin");
	InterlockedExchange(&g_spinning, 1);
	while (g_stop == 0)
	{
		// Not a tight burn, the point is only that it never leaves this frame.
		Sleep(1);
	}
}

__declspec(noinline) void SpinMiddle()
{
	workshop::CrashScope scope("test: SpinMiddle");
	Spinner();
}

__declspec(noinline) void SpinOuter()
{
	workshop::CrashContext("test: SpinOuter");
	SpinMiddle();
}

DWORD WINAPI SpinThread(LPVOID)
{
	SpinOuter();
	return 0;
}

void RunHangTest()
{
	DWORD id = 0;
	HANDLE t = CreateThread(NULL, 0, SpinThread, NULL, 0, &id);
	if (!t)
	{
		printf("could not start the spin thread\n");
		return;
	}

	// Wait for it to actually be in the spin, so the report is about the frame we
	// mean rather than about CreateThread's startup path.
	for (int i = 0; i < 500 && g_spinning == 0; ++i)
		Sleep(10);
	Sleep(50);

	printf("spin thread %lu is spinning, reporting it from thread %lu\n",
	    id, GetCurrentThreadId());

	if (!workshop::WriteThreadReportNow(id, "the foreign thread path, as the hang watchdog uses it"))
		printf("WriteThreadReportNow FAILED\n");
	else
		printf("foreign thread report written\n");

	InterlockedExchange(&g_stop, 1);
	WaitForSingleObject(t, 5000);
	CloseHandle(t);
}

int main(int argc, char** argv)
{
	// So the deliberate fault does not stop on a Windows error dialog.
	SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);

	const bool took = workshop::InstallCrashHandler();
	printf("InstallCrashHandler: %s, installed=%d, owner=%s\n", took ? "took it" : "someone else had it",
	    workshop::CrashHandlerInstalled() ? 1 : 0, workshop::CrashHandlerOwner());

	// What the proxy does on its bootstrap thread, off the loader lock.
	workshop::WarmCrashHandler();

	workshop::CrashContext("test: main started with %d args", argc);

	// "hang" reports another thread, anything else crashes, nothing does the on
	// demand report on this thread.
	if (argc > 1 && strcmp(argv[1], "hang") == 0)
	{
		RunHangTest();
		printf("main returning normally\n");
		return 0;
	}

	const int mode = (argc > 1) ? 1 : 0;
	Outer(mode);

	printf("main returning normally\n");
	(void)argv;
	return 0;
}
