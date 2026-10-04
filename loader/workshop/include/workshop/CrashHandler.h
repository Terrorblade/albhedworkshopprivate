#pragma once

// A crash report with a call stack, for when the game dies and the normal log just
// stops. Every frame comes out as module+RVA plus the address that module's own IDB
// uses, so a line pastes straight into IDA with no arithmetic.
//
//     workshop::InstallCrashHandler();                  // first thing in Startup
//     workshop::CrashContext("probing event %d", id);   // a breadcrumb, any thread
//     workshop::ReassertCrashHandler();                 // once per step
//
// Reports land in <game dir>\AlBhedWorkshop\albhed_crash.log.
//
// THIS FILE DEPENDS ON NOTHING ELSE IN THE LIBRARY, on purpose, two ways over. A crash
// handler that needs the rest of the kit to be healthy is no use when the kit is what
// broke, and it must not touch workshop::Log, whose critical section may be the thing
// the dead thread is holding. So it formats by hand, writes with CreateFileW directly,
// and allocates nothing. That also means it links into the dinput8 proxy, which has no
// other part of this library, so coverage starts before the first plugin loads.
//
// It takes no loader lock either. The module list comes from a PEB walk rather than
// GetModuleHandleEx, because a fault inside a DllMain happens with the loader lock
// already held and calling back into the loader there is a deadlock.

namespace workshop
{

	// Installs the unhandled exception filter. Safe from DllMain. Call it as early as
	// possible, before anything that can fault.
	//
	// FIRST CALLER IN THE PROCESS WINS. Two plugins and the proxy each link their own
	// copy of this, so the state that decides ownership lives in a named shared section
	// rather than in a static. Later callers return false and leave the owner alone,
	// which is what stops one crash producing three identical reports.
	bool InstallCrashHandler();

	// Reads the preferred image base of every loaded module off the disk, so the report
	// can give an address in the form that module's IDB uses.
	//
	// SEPARATE FROM INSTALL BECAUSE IT OPENS FILES, and install has to be callable from
	// DllMain where nothing should touch the disk. Call this from any ordinary thread
	// once startup is past the loader lock. The proxy calls it on its bootstrap thread
	// before it loads any plugin, which is what makes FFX.exe's own preferred base known
	// from the earliest point a plugin can fault.
	//
	// Skipping it costs only the IDA column, which then reads "ida unknown". Module and
	// RVA are always there. A per step ReassertCrashHandler does this work too, so a
	// module that loads later is picked up on the next step.
	void WarmCrashHandler();

	// Cheap, meant for a per-step handler. Two jobs.
	//
	// The game installs its own top level filter during startup, after ours, and that
	// replaces rather than chains. This puts ours back and remembers the game's so the
	// report still ends with the game's own handling. It also records the calling thread
	// as the game thread, so a report can say whether the fault was on it.
	//
	// Any module may call it. If the filter it displaces belongs to another Al Bhed
	// module, that one is put straight back, so the chain never holds two of ours.
	void ReassertCrashHandler();

	// A breadcrumb. The report prints the last 16 of these with the thread that left
	// them, which is the difference between "it died at FFX.exe+0x36C53B" and "it died
	// at FFX.exe+0x36C53B while selecting the event asset kind".
	//
	// Keep them short and cheap. This is the one call here that uses the CRT, because it
	// runs in ordinary code rather than inside the handler.
	void CrashContext(const char* format, ...);

	// For a scope that should drop its breadcrumb on the way out as well, so the log
	// shows the call returned instead of leaving a stale "doing X" as the last word.
	struct CrashScope
	{
		explicit CrashScope(const char* what);
		~CrashScope();

	private:
		CrashScope(const CrashScope&);
		CrashScope& operator=(const CrashScope&);

		char held[96];
	};

	bool CrashHandlerInstalled();

	// The module that owns the filter, for a startup log line. "" when nobody does.
	const char* CrashHandlerOwner();

	// Writes a report right now, for the current thread, without needing a crash. This
	// is how to check the handler works and that the frames resolve, rather than finding
	// out the first time it matters. Returns false if the file could not be written.
	bool WriteCrashReportNow(const char* why);

	// The same report, about ANOTHER thread. Suspends it, reads its context, writes
	// the report, resumes it. Pass 0 or this thread's own id and it is just
	// WriteCrashReportNow.
	//
	// This is how a freeze gets a call stack. The hang watchdog calls it with the
	// game thread's id once the game thread has stopped stepping.
	// unsigned long rather than DWORD, because this header deliberately includes
	// nothing at all. They are the same type on win32.
	bool WriteThreadReportNow(unsigned long threadId, const char* why);

} // namespace workshop
