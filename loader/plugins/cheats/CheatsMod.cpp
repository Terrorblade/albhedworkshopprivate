// Al Bhed Workshop cheat plugin for FINAL FANTASY X HD Remaster.
//
// Build with ..\build_cheats.bat and drop the DLL into <game dir>\AlBhedWorkshop\plugins\.
// Press F11 in game for the overlay, then the Cheats panel.
//
// WHAT THIS IS FOR, BESIDES CHEATING
//   This is the second plugin on the hub, and that is the point of it. Everything
//   it knows about FFX.exe comes from the linked workshop library, exactly as
//   Pilgrimage Together's does, and the two share no code. If a feature here
//   needed something the library did not have, the fix was to add it to the
//   library rather than to this plugin, which is why ffx/GameLists.h and
//   workshop/PickerList.h exist.
//
// THE ONE DESIGN RULE
//   No option is typed in. Every list of things, every map, character, piece of
//   equipment, item, model, is read out of the game's own tables at runtime and
//   offered as a filterable picker. Typing is only ever a filter, never the way
//   you name a thing. workshop::Picker is that widget and ffx::GameLists is where
//   the lists come from.
//
//   The exception, and it is marked as one wherever it happens, is a subsystem
//   whose name table has not been found yet. Those fall back to a numeric row and
//   say so, rather than pretending the id is a sensible thing to ask a person for.
//
// LAYOUT
//   ui/CheatPanel.cpp     the panel shell and the tab bar
//   ui/TabsSaveData.cpp   gil, party, stats, equipment, inventory
//   ui/TabsWorld.cpp      warp, events, and reaching the unreferenced packages
//   ui/TabsBattle.cpp     the dev battle flags and overdrive modes
//   ui/TabsSphereGrid.cpp node activation
//   ui/TabsModels.cpp     the model list and the player model swap
//   ui/TabsDebug.cpp      the game's own debug menu, and restoring its input
//   ui/TabsMinigames.cpp  launching a minigame and editing the running one

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wchar.h>

#include "ffx/Api.h"
#include "ffx/AssetPaths.h"
#include "ffx/GameLists.h"
#include "ffx/EngineHeap.h"
#include "ffx/DebugMenu.h"
#include "ffx/KernelTables.h"
#include "ffx/Minigames.h"
#include "ffx/Models.h"
#include "ffx/Hub.h"
#include "ffx/VerifyLayout.h"
#include "ui/CheatPanel.h"
#include "workshop/HostModule.h"
#include "workshop/Events.h"
#include "workshop/Log.h"
#include "workshop/CrashHandler.h"
#include "workshop/HangWatchdog.h"
#include "workshop/Overlay.h"

#if !defined(_M_IX86)
#error "AlBhedWorkshop plugins must be 32-bit x86."
#endif

namespace cheats
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

		// Rebuild the game-data lists on the edges that invalidate them. A map change
		// moves the current event, and a battle or a menu closing changes the party and
		// the inventory, so a list built at the title screen would be stale by the time
		// anybody opened the panel.
		void OnHubEvent(const Event&, void*)
		{
			RequestCheatListRefresh();
		}

		// The one-shot that gets the archive-backed name tables read. It cannot happen
		// in DllMain, because the game's VBF manager does not exist until
		// FFX_InitFileSystem has run, so the first simulation step is the earliest
		// honest moment. After it fires the subscription is dropped.
		int g_firstStep = 0;

		void OnFirstStep(const Event&, void*)
		{
			// The event name table, then the package measurement, then the lists, in
			// that order. LoadEventTable is what gives the test and sample packages
			// their real names, and ProbeEventPackages is what proves which ones are
			// actually in the archive instead of trusting a list baked into this build.
			// Both go through the engine's file layer, which is why they are here and
			// not in DllMain.
			// THE EXPORT CACHE FIRST. When it hits, every archive derived list is
			// already filled and the event package table is already trusted, so the
			// probe below sees EventPackagesProbed and does nothing. That is the whole
			// point: no 402 engine file opens, and no asset kind swapped out from
			// under the engine while it loads.
			// ===================================================================
			// HEAP CHECKS, SCAFFOLDING FOR ONE HUNT. REMOVE WHEN IT IS OVER.
			//
			// There is an intermittent crash, about one boot in three with this
			// plugin loaded and never without it, inside the engine's allocator
			// coalescing across a block whose header is wrong, with no frame of ours
			// on the stack. Each of these walks the whole engine heap and validates
			// every block header, so the first one that fails names the phase that
			// did the damage. A full walk is far too expensive to keep in a boot
			// path once the answer is in.
			// ===================================================================
			CrashContext("cheats: first step, trying the exported lists");
			const bool fromCache = cheats::TryExportedCheatLists();

			// ONLY WHEN THE CACHE MISSED. FFX_LoadEventIdTable is the one engine call
			// in this whole startup that damages the engine heap, because the dev file
			// reader behind it writes over the allocator's tag field. ffx::LoadEventTable
			// repairs that straight away, but the better answer is not to call it: a
			// cache hit already has every event name, so there is nothing to read.
			//
			// See the comment on the repair in ffx/GameLists.cpp for the mechanism.
			if (!fromCache)
			{
				CrashContext("cheats: first step, reading the event name table");
				ffx::LoadEventTable();
				ffx::ProbeEventPackages();
			}

			// STAY SUBSCRIBED UNTIL THERE IS SOMETHING WORTH SAYING. The first
			// simulation step runs during the splash, long before the asset loader or
			// the archive exist, so logging on the literal first step dumps six empty
			// tables and then unsubscribes, wasting the one shot. The probe succeeding
			// is the honest signal that the file layer is up, and both calls above are
			// idempotent so retrying them costs a guard check.
			//
			// The budget is so a build where the loader never comes up still logs once
			// and says so, instead of retrying for the life of the process. 2000 steps
			// is about 67 seconds at the simulation's 29.97 Hz.
			static int waited = 0;
			if (!ffx::EventPackagesProbed() && ++waited < 2000)
				return;

			RefreshCheatLists();

			if (!ffx::EventPackagesProbed())
				Log("the asset loader did not come up within %d steps, so the tables "
				    "below hold only what was readable without it. The refresh button "
				    "in the panel picks the rest up once a game is loaded.",
				    waited);

			CrashContext("cheats: logging the tables");
			ffx::LogKernelTables();
			ffx::LogGameLists();
			ffx::LogModelTables();
			ffx::LogDebugMenu();
			ffx::LogMinigames();
			ffx::LogAssetPaths();

			// ONE CHECK, AT THE END, AND IT IS WORTH ITS COST ONCE.
			//
			// A full walk of every block in the engine heap, which is tens of thousands
			// of them. It stays because it is the only thing that catches this class of
			// damage before the allocator trips over it seconds later with a useless
			// stack, and because the damage is the game's own: a mod cannot avoid
			// touching dev-only code paths forever. If it ever says the heap will
			// fault, read ffx/EngineHeap.h and the repair comment in ffx/GameLists.cpp.
			ffx::LogEngineHeap("at the end of the cheat plugin's startup");

			if (g_firstStep)
			{
				Unsubscribe(g_firstStep);
				g_firstStep = 0;
			}
		}

		// Everything that has to happen on the simulation thread, once per step.
		//
		// NoteOverlayGameThread goes here and nowhere else. It is what lets a panel ask
		// OverlayOnGameThread and get a true answer, and calling it from inside a draw
		// callback, which is what this plugin used to do, makes the answer always yes
		// and the check pointless.
		//
		// The launch pump is the minigame launcher's deferred half: a launch row that
		// names a script entry cannot fire it at the time of the warp, because the
		// package is not loaded yet, so the kit holds the request and this hands it a
		// step to land on.
		void OnGameStep(const Event&, void*)
		{
			workshop::NoteOverlayGameThread();

			// The game installs its own top level filter during startup, which replaces
			// rather than chains, so ours has to be put back. This also records the game
			// thread id so a report can say whether the fault was on it.
			workshop::ReassertCrashHandler();

			// WHAT TURNS A FREEZE INTO A CALL STACK. The watchdog thread watches this
			// counter, and when it stops moving it suspends this thread and reports its
			// stack the same way the crash handler does.
			workshop::NoteHangWatchdogStep();

			PumpCheatListRefresh();
			ffx::PumpMinigameLaunch();
		}

		// Subscribe is per event id, so this is three calls. Returns how many took.
		int SubscribeToRefreshEdges()
		{
			static const EventId wanted[] = {
			    EventMapChanged,
			    EventBattleEnd,
			    EventMenuClosed,
			};

			int taken = 0;
			for (int i = 0; i < (int)(sizeof(wanted) / sizeof(wanted[0])); ++i)
				if (Subscribe(wanted[i], &OnHubEvent))
					++taken;
			return taken;
		}

		// The startup sequence, in the order the kit requires it: log, bind the module,
		// verify the build, bind the API, then hook. Returns false when something went
		// wrong badly enough that nothing should be hooked, and the log says why.
		bool Startup()
		{
			// FIRST, BEFORE ANYTHING THAT CAN FAULT. It writes its own file and needs no
			// log, no module binding and no engine, so there is nothing to put ahead of
			// it. If the proxy already installed one, this returns false and leaves it
			// alone, which is the normal case.
			const bool tookCrashHandler = InstallCrashHandler();

			// Starts the thread that watches for a freeze. Only one runs per process,
			// whichever plugin asks first, and every plugin's step notes feed it.
			const bool tookWatchdog = StartHangWatchdog();
			CrashContext("cheats: Startup");

			OpenLog(L"albhed_cheats.log");
			BindHostModule();

			wchar_t exePath[MAX_PATH] = { 0 };
			GetModuleFileNameW(NULL, exePath, MAX_PATH);
			Log("=== AlBhedWorkshop cheats ===");
			Log("host       : %S", exePath);
			Log("module base: 0x%08X (preferred 0x00400000, ASLR slide %+d)",
			    (unsigned)(UINT_PTR)ModuleBase(), (int)((INT_PTR)ModuleBase() - 0x00400000));

			if (_wcsicmp(FileNameOf(exePath), L"FFX.exe") != 0)
			{
				Log("host '%S' is not FFX.exe, doing nothing", FileNameOf(exePath));
				return false;
			}

			Log("crash handler: %s. Reports go to albhed_crash.log with every frame as "
			    "module+RVA and the address to paste into IDA.",
			    tookCrashHandler ? "installed by this plugin"
			                     : (CrashHandlerInstalled()
			                               ? "already installed by another Al Bhed module"
			                               : "COULD NOT BE INSTALLED"));

			Log("hang watchdog: %s, threshold %lu ms. A freeze writes the same report a "
			    "crash does, for the thread that stopped stepping.",
			    tookWatchdog ? (HangWatchdogRunning() ? "running" : "not running")
			                 : "COULD NOT START",
			    HangThresholdMs());

			if (!VerifyLayout())
			{
				Log("LAYOUT CHECK FAILED. This is not the analysed build, so nothing will "
				    "be hooked. Re-derive the addresses in workshop/include/ffx/ from the "
				    "IDB.");
				return false;
			}
			Log("layout check passed");

			BindApi();
			LogApiState();

			// The hub, for the list-invalidating edges. Not fatal: without it the lists
			// go stale after a map change and the refresh button is the only way back.
			if (!StartHub())
				Log("the hub would not start, so the lists will not refresh themselves "
				    "after a map change. Use the refresh button in the panel.");
			else
			{
				g_firstStep = Subscribe(EventStep, &OnFirstStep);
				if (!g_firstStep)
					Log("could not subscribe to the first step, so the item and ability "
					    "name tables will not be read until the refresh button is "
					    "pressed");

				if (!Subscribe(EventStep, &OnGameStep))
					Log("could not subscribe the per-step handler, so the lists will "
					    "never be measured, the game-thread check will always say no, "
					    "and a launch row that fires a script entry will warp and then "
					    "do nothing");

				const int taken = SubscribeToRefreshEdges();
				if (taken != 3)
					Log("only %d of the 3 refresh edges subscribed, so some stale lists "
					    "will need the refresh button",
					    taken);
			}

			if (!StartCheatPanel())
			{
				Log("no panel, so this plugin can do nothing. Giving up.");
				return false;
			}

			Log("ready. F11 for the overlay, then the Cheats panel.");
			return true;
		}

	} // namespace
} // namespace cheats

// A plugin DLL attaches after the exe's CRT startup, so the engine already exists.
// DllMain runs under the loader lock, so no LoadLibrary and no waiting on other
// threads. VirtualProtect, a dword store and CreateThread are all fine.
//
// There is deliberately no DLL_PROCESS_DETACH handler. This plugin is loaded for
// the life of the process and it installs no detour that would have to come back
// out, every write it makes is to game state the engine owns anyway.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;

	DisableThreadLibraryCalls(module);
	cheats::Startup();
	return TRUE;
}
