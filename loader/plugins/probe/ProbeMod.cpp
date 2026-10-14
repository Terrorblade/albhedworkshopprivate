// Al Bhed Probe: records per step fixtures from the running original, so Project Zanarkand
// can check its ports against the real thing rather than against a reading of the
// disassembly. The first piece of Zanarkand PROGRESS 18.1, the oracle harness.
//
// Build with ..\..\build_probe.bat, which also installs the DLL into
// <game dir>\AlBhedWorkshop\plugins\. The format is reversing\PROBE_FIXTURES.md and the
// reader is projectzanarkand\tools\probe_fixture.py.
//
// FOUR RECIPES, each a pure observer on one engine function:
//
//   pacing   FFX_StepPacing            Zanarkand 3.4.3, the dt and pending step sequence
//   camera   FFX_Came_ActivateScreen   Zanarkand 6.3.1, CAMERA_BUILD.md section 10
//   motion   FFX_Ch_UpdateMotionAll    Zanarkand 10.5, CHARACTER_MOTION.md section 10.4
//   joints   FFX_Mot_AdvanceFrame      Zanarkand 10.4, CHARACTER_MOTION.md section 11.12,
//                                      off by default (probe.joints) because it is heavy
//
// Every hook calls the original unchanged and returns its result unchanged, and every one
// chains with any other plugin's detour or call site patch on the same function. The probe never writes game
// memory. It can run next to Pilgrimage Together and the cheats plugin, and with no co-op
// session at all.
//
// HOW A CAPTURE STARTS AND STOPS. probe.capture in <game dir>\AlBhedWorkshop\albhed_probe.txt,
// or the hotkey (Scroll Lock by default, probe.hotkey). The file is watched, so editing it
// while the game runs takes effect within half a second. Output goes to
// %LOCALAPPDATA%\AlBhedWorkshop\probe unless probe.output_dir says otherwise, never into
// the game folder.
//
// THREADS. The hooks run on the game's step thread and only copy into memory. A worker in
// the kit (workshop/RecordFile.h) does every disk write. The control thread here polls the
// hotkey and the settings file and opens and closes capture sessions. See capture/Capture.h
// for the one rule between them.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wchar.h>

#include "ProbeSettings.h"
#include "capture/Capture.h"
#include "ffx/VerifyLayout.h"
#include "workshop/CrashHandler.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "workshop/RecordFile.h"

#if !defined(_M_IX86)
#error "AlBhedWorkshop plugins must be 32-bit x86."
#endif

namespace probe
{

	using namespace workshop;

	namespace
	{

		const DWORD PollMs = 50;
		const DWORD SettingsPollMs = 500;

		bool g_hotkeyWasDown = false;

		const wchar_t* FileNameOf(const wchar_t* path)
		{
			const wchar_t* name = path;
			for (const wchar_t* p = path; *p; ++p)
				if (*p == L'\\' || *p == L'/')
					name = p + 1;
			return name;
		}

		// Edge detected here rather than with GetAsyncKeyState's low bit, which belongs to
		// whoever reads it first, and the game and the other plugins read it too.
		bool HotkeyPressed()
		{
			const int key = (int)settings.hotkey;
			if (key <= 0 || key > 255)
			{
				g_hotkeyWasDown = false;
				return false;
			}
			const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
			const bool pressed = down && !g_hotkeyWasDown;
			g_hotkeyWasDown = down;
			return pressed;
		}

		DWORD WINAPI ControlThread(LPVOID)
		{
			DWORD lastSettingsPoll = GetTickCount();
			for (;;)
			{
				Sleep(PollMs);

				if (HotkeyPressed())
				{
					InterlockedExchange(&settings.capture, settings.capture ? 0 : 1);
					Log("hotkey: recording %s", settings.capture ? "requested" : "stop requested");
					SaveProbeSettings();
				}

				if (GetTickCount() - lastSettingsPoll >= SettingsPollMs)
				{
					lastSettingsPoll = GetTickCount();
					if (ProbeSettingsFileChanged())
					{
						Log("settings: %S changed on disk, reading it again", SettingsFileName);
						ReloadProbeSettings();
					}
				}

				if (TakeLimitStopRequest() && CaptureRunning())
				{
					StopCapture("probe.max_steps reached");
					// In memory only. Leaving the file at 1 lets the next boot capture again,
					// and the capture does not restart now because the value here is 0.
					InterlockedExchange(&settings.capture, 0);
				}

				if (settings.capture && !CaptureRunning())
				{
					if (!StartCapture())
					{
						// Do not retry fifty times a second. An edit to the file or the
						// hotkey asks again.
						InterlockedExchange(&settings.capture, 0);
						SetStatus("probe: could not start, see albhed_probe.log");
					}
				}
				else if (!settings.capture && CaptureRunning())
				{
					StopCapture("asked to");
				}
			}
		}

		bool Startup()
		{
			// Whoever loads first owns the process crash handler. Not the hang watchdog: a
			// watchdog needs a step heartbeat and this plugin is not the one to give it.
			const bool tookCrashHandler = InstallCrashHandler();

			OpenLog(L"albhed_probe.log");
			BindHostModule();

			wchar_t exePath[MAX_PATH] = { 0 };
			GetModuleFileNameW(NULL, exePath, MAX_PATH);
			Log("=== Al Bhed Probe, fixture recorder ===");
			Log("host       : %S", exePath);
			Log("module base: 0x%08X", (unsigned)(UINT_PTR)ModuleBase());
			Log("crash handler: %s", tookCrashHandler ? "installed by this plugin"
			                                          : "left to another Al Bhed module");

			if (_wcsicmp(FileNameOf(exePath), L"FFX.exe") != 0)
			{
				Log("host '%S' is not FFX.exe, doing nothing", FileNameOf(exePath));
				return false;
			}
			if (!ffx::VerifyLayout())
			{
				Log("LAYOUT CHECK FAILED. Not the analysed build, so nothing will be hooked.");
				return false;
			}
			Log("layout check passed, FFX.exe TimeDateStamp 0x%08X SizeOfImage 0x%08X CheckSum "
			    "0x%08X",
			    ModuleTimeDateStamp(), ModuleImageSize(), ModuleCheckSum());

			LoadProbeSettings();

			// Installed once, unconditionally, and gated at runtime, so turning a recipe on
			// mid session needs no patching. Each failure is logged by the recipe.
			const bool pacing = InstallPacingRecipe();
			const bool camera = InstallCameraRecipe();
			const bool motion = InstallMotionRecipe();
			const bool joints = InstallJointsRecipe();
			if (!pacing)
				Log("without the pacing hook the step ordinal never advances, so camera and "
				    "motion records will all carry ordinal 0 and the step limit cannot fire");
			if (!pacing && !camera && !motion && !joints)
			{
				Log("no hook went in, nothing to record");
				return false;
			}

			HANDLE thread = CreateThread(NULL, 0, &ControlThread, NULL, 0, NULL);
			if (!thread)
			{
				Log("could not start the control thread, error %lu", GetLastError());
				return false;
			}
			CloseHandle(thread);

			Log("ready. Recording is %s. Toggle with virtual key 0x%02lX, or set probe.capture "
			    "in %S. Output: %s",
			    settings.capture ? "ON from boot" : "off", settings.hotkey, SettingsFileName,
			    settings.outputDir[0] ? settings.outputDir : "%LOCALAPPDATA%\\AlBhedWorkshop\\probe");
			SetStatus("probe: idle");
			return true;
		}

	} // namespace
} // namespace probe

// Attaches after the exe's CRT startup, like every plugin, which is fine because all three
// hooks are on per step functions. DllMain holds the loader lock, so the control thread is
// created here but does nothing that needs the lock until DllMain has returned.
//
// DETACH. When the process is exiting, lpReserved is non-null and every other thread is
// already gone, so whatever the writer had not reached is written here synchronously and
// the files stay whole. A FreeLibrary detach never happens, plugins stay loaded.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);
		probe::Startup();
	}
	else if (reason == DLL_PROCESS_DETACH && reserved != NULL)
	{
		workshop::FlushRecordFilesAtProcessExit();
	}
	return TRUE;
}
