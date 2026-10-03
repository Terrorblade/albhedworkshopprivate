// AlBhedWorkshop clone mod: spawn extra playable characters into FINAL FANTASY X and
// drive one of them from a second input device.
//
// Build with ..\build_pilgrimage.bat and drop the DLL into <game dir>\AlBhedWorkshop\plugins\.
// See hooks/Hotkeys.h for the keys.
//
// WHAT THIS PLUGIN IS, VERSUS WHAT THE LIBRARY IS
//   Everything this plugin knows about FFX.exe comes from workshop, which is linked
//   in: addresses, structure layouts, the resolved API, the per-frame hook point,
//   the walkmesh binding, the render probes. None of that is duplicated here, and
//   a second plugin gets all of it by linking the same library. See
//   ..\..\workshop\include\ffx\Ffx.h.
//
//   What is left in this plugin is only its own behaviour: which clones exist, how
//   one is made, how input drives it, and the control panel.
//
// WHAT THIS IS BUILT ON
//   reversing\PHASE1_CLONE.md is the research this implements. The short version
//   is that movement in this game is already fully per-character: everything below
//   the input layer takes a character pointer and reads no player global. So
//   movement is not reimplemented. A second character is allocated, its speed and
//   heading are written every frame, and the engine turns it, animates it, moves
//   it, collides it and clamps it to the ground for free.
//
//   Two things do not come free. The walkmesh binding, which is in the library at
//   ffx/Walkmesh.h because every plugin that places a character needs it, and the
//   one-slot Tidus cache, which is in clones/CloneSpawner.cpp because only
//   spawning hits it. Do not remove either.
//
// HOW THIS PLUGIN IS LAID OUT
//   clones/     which clones exist, how one is made, and how input drives it
//   diag/       dumps and watches, for when a spawn looks wrong
//   hooks/      the per-frame body and the hotkeys
//   ui/         the control panel, on its own thread
//   ModState.h  the state those share, split by which thread owns it

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wchar.h>

#include "ModState.h"
#include "ffx/Api.h"
#include "ffx/Input.h"
#include "ffx/MainLoop.h"
#include "ffx/Pad.h"
#include "ffx/VerifyLayout.h"
#include "battle/BattleSync.h"
#include "hooks/FrameHook.h"
#include "net/LockstepLink.h"
#include "world/EncounterSync.h"
#include "world/RemotePlayers.h"
#include "world/DialogueSync.h"
#include "world/TriggerPass.h"
#include "hooks/Hotkeys.h"
#include "hooks/VisibilityDetour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "ui/ControlPanel.h"

#if !defined(_M_IX86)
#error "AlBhedWorkshop plugins must be 32-bit x86."
#endif

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// Just the filename, so the host check does not care where the game is installed.
		const wchar_t* FileNameOf(const wchar_t* path)
		{
			const wchar_t* name = path;
			for (const wchar_t* p = path; *p; ++p)
				if (*p == L'\\' || *p == L'/')
					name = p + 1;
			return name;
		}

		// The startup sequence, in the order it has to happen. This is the contract from
		// ffx/Ffx.h: log, bind the module, verify the build, bind the API, then hook.
		// Returns false when something went wrong badly enough that nothing should be
		// hooked, in which case the log already says why.
		bool Startup()
		{
			OpenLog(L"pilgrimage_together.log");
			BindHostModule();

			// Defaults first, then the table that describes them, then the saved file on
			// top. Loading goes through the registry, so the records have to exist by then.
			ApplyDefaultSettings();
			RegisterModSettings();
			LoadModSettings();

			wchar_t exePath[MAX_PATH] = { 0 };
			GetModuleFileNameW(NULL, exePath, MAX_PATH);
			Log("=== AlBhedWorkshop clone mod ===");
			Log("host       : %S", exePath);
			Log("module base: 0x%08X (preferred 0x00400000, ASLR slide %+d)",
			    (unsigned)(UINT_PTR)ModuleBase(), (int)((INT_PTR)ModuleBase() - 0x00400000));

			if (_wcsicmp(FileNameOf(exePath), L"FFX.exe") != 0)
			{
				Log("host '%S' is not FFX.exe, doing nothing", FileNameOf(exePath));
				return false;
			}

			if (!VerifyLayout())
			{
				Log("LAYOUT CHECK FAILED. This is not the analysed build, so nothing will be "
				    "hooked. Re-derive the addresses in workshop/include/ffx/Addresses.h from "
				    "the IDB.");
				return false;
			}
			Log("layout check passed");

			BindApi();
			LogApiState();

			if (!InstallFrameHook())
			{
				Log("could not install the frame hook, giving up");
				return false;
			}

			// The step hook has to be in place before a session starts, and it costs one
			// call per simulation step when nothing is connected, so it goes in
			// unconditionally.
			InstallLockstep();

			// The remote player table and the per-player trigger shadows start empty. Both
			// are pure bookkeeping until somebody joins, so this is just making sure a
			// reload does not inherit the last session's bindings.
			ResetRemotePlayers();
			ResetTriggerPass();

			// Not fatal. Without it a remote player's walking does not count toward a
			// random battle, and the engine behaves exactly as the shipped game does.
			if (!InstallEncounterSync())
				Log("remote movement will not contribute to random encounters");

			// Installed unconditionally and harmless when nothing is connected, because
			// the callback leaves the pad block alone unless a session is active. Without
			// it a message box runs off whichever hardware pad is plugged into the machine
			// showing it, which diverges the moment anybody talks to anything.
			if (!InstallDialogueSync())
				Log("message boxes will not be driven by replicated input, so talking to "
				    "anything in a session is a divergence");

			// Installed unconditionally, same reasoning as the dialogue hook: both
			// callbacks pass straight through until a session starts, so a solo game is
			// byte for byte the shipped game with these in.
			//
			// Not fatal, and the consequence splits in two. Without the capture sites a
			// battle action is committed on this machine alone. Without the open hook
			// both machines open a menu for the same unit, and since the staging record
			// is one global, the two cursors corrupt each other's command. Either way a
			// battle in a session is a divergence, which InstallBattleSync says out loud
			// per hook.
			if (!InstallBattleSync())
				Log("battle commands will not be replicated, so the first battle in a "
				    "session will diverge");

			// Not fatal. Only the force-visible diagnostic depends on it.
			if (!InstallVisibilityDetour())
				Log("the force-visible diagnostic (F5) is unavailable without that detour");

			StartControlPanel();
			LogPadSlots();

			// The input research left one unknown that only a running game can settle,
			// and it decides whether input injection at animate time works at all. Non
			// zero moves the pad latch inside animate, which is after the point we would
			// be writing. Logged first thing so the answer is in every log we ever read.
			Log("threaded pad mode = %lu (%s)", ThreadedPadMode(),
			    ThreadedPadMode() ? "LATCH IS INSIDE ANIMATE, injection order needs rechecking"
			                      : "latch is before animate, which is what injection assumes");
			LogInputState();

			// The frame loop, including the two things only a running game can answer:
			// whether the fixed timestep argument is in effect, and whether this build
			// is pacing from the wall clock, which is the one determinism hole lockstep
			// has to know about.
			LogMainLoopState();
			Log("clone input will use pad slot %ld", settings.padSlot);
			Log("ready. %s", HotkeySummary());
			return true;
		}

	} // namespace
} // namespace pilgrimage

// A plugin DLL attaches AFTER the exe's CRT startup, so FFXApplication already
// exists. That is fine, because patching a vtable slot is time-independent: the
// slot lives in .rdata at a fixed RVA and the next frame picks it up. Do not add a
// hook on a one-shot startup function here, it is a race you will lose.
//
// DllMain runs under the loader lock, so no LoadLibrary and no waiting on other
// threads. VirtualProtect, a dword store and CreateThread are all fine.
//
// There is deliberately no DLL_PROCESS_DETACH handler. This mod is loaded for the
// life of the process, and the one piece of engine state it modifies, the cull
// override, is restored whenever that override is switched back off.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;

	DisableThreadLibraryCalls(module);
	pilgrimage::Startup();
	return TRUE;
}
