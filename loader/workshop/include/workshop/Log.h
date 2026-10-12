#pragma once

// Logging, and the one-line status string a control panel can show.
//
// Log writes to the debugger and appends to <game dir>\AlBhedWorkshop\<name>, where the
// name is whatever OpenLog was given. Each plugin DLL links its own copy of this
// and so gets its own file, which is what you want when two plugins are loaded.
//
// Called from the game thread on the frame path, so it stays cheap and never
// allocates.

namespace workshop
{

	// Must be called before any other Log call. Sets up the file lock, picks the file
	// name, for example L"pilgrimage_together.log", and TRUNCATES that file.
	//
	// ONE BOOT, ONE LOG. Log appends, so without this every run piled on top of the
	// last one and the first question about any log became "which run am I reading".
	// Timestamps are wall clock with no date, so two runs an hour apart are genuinely
	// ambiguous.
	//
	// Each module passes its own file name, so each clears only its own file and the
	// three never race. Two modules passing the SAME name would have the second wipe
	// the first, so do not do that.
	//
	// THE CRASH LOG IS NOT THIS FILE and is deliberately handled differently: the
	// loader renames it to albhed_crash.prev.log instead of truncating it, because you
	// restart the game specifically to read a crash report and clearing it on the way
	// in would destroy the thing you came back for. See RollCrashLog.
	void OpenLog(const wchar_t* fileName);

	void Log(const char* format, ...);

	// The short line a control panel displays. Written by the game thread, read by a
	// UI thread without a lock, which is safe because the buffer's last byte is a
	// permanent NUL. See the note in the implementation.
	void SetStatus(const char* format, ...);
	const char* Status();

} // namespace workshop
