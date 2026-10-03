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

	// Must be called before any other Log call. Sets up the file lock and picks the
	// file name, for example L"pilgrimage_together.log".
	void OpenLog(const wchar_t* fileName);

	void Log(const char* format, ...);

	// The short line a control panel displays. Written by the game thread, read by a
	// UI thread without a lock, which is safe because the buffer's last byte is a
	// permanent NUL. See the note in the implementation.
	void SetStatus(const char* format, ...);
	const char* Status();

} // namespace workshop
