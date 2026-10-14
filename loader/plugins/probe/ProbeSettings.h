#pragma once

#include <windows.h>

// The probe's settings, in <game dir>\AlBhedWorkshop\albhed_probe.txt.
//
// Nothing here needs a co-op session, a peer or the overlay. The file is written with the
// defaults on the first run, and it is read again whenever it changes on disk while the
// game runs, so a harness can start and stop a capture by editing one line:
//
//   probe.capture=1      start now, or at boot if it is already 1 when the game starts
//   probe.capture=0      stop and close the files
//
// The hotkey does the same from the keyboard and writes the new value back to the file.

namespace probe
{

	struct Settings
	{
		volatile long capture; // the switch. 1 records, 0 does not.
		volatile long pacing;  // which recipes a capture writes
		volatile long camera;
		volatile long motion;
		volatile long joints;          // off by default, a joints record is about 66 KB
		volatile long jointsChr;       // JointsFilterControlled, JointsFilterEvery, or an m_id to match
		volatile long jointsMax;       // joints each record has room for, read when a capture starts
		volatile long hotkey;          // a virtual key code that toggles capture, 0 for none
		volatile long maxSteps;        // stop after this many pacing calls, 0 for no limit
		volatile long budgetMegabytes; // memory one file may hold waiting for the disk
		char outputDir[260];           // empty means %LOCALAPPDATA%\AlBhedWorkshop\probe
	};

	extern Settings settings;

	// probe.joints_chr. 0..65535 picks CHRs whose u16 m_id equals it, so 1 is Tidus.
	const long JointsFilterControlled = -1; // g_ffxControlledChr only, the default
	const long JointsFilterEvery = -2;      // every CHR AdvanceFrame is called for

	extern const wchar_t* const SettingsFileName;

	// Defaults, then the registry, then the file on top, creating it when there is none.
	void LoadProbeSettings();

	// Writes the file, and remembers its new timestamp so the watcher does not read our
	// own write back as an edit.
	void SaveProbeSettings();

	// True, once, when the file on disk has changed since the last load or save. The
	// caller then calls ReloadProbeSettings. Cheap enough to poll twice a second.
	bool ProbeSettingsFileChanged();
	void ReloadProbeSettings();

} // namespace probe
