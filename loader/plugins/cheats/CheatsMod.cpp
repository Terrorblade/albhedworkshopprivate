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
//   ui/TabsWorld.cpp      warp and events
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
#include "ffx/GameLists.h"
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
			RefreshCheatLists();
		}

		// The one-shot that gets the archive-backed name tables read. It cannot happen
		// in DllMain, because the game's VBF manager does not exist until
		// FFX_InitFileSystem has run, so the first simulation step is the earliest
		// honest moment. After it fires the subscription is dropped.
		int g_firstStep = 0;

		void OnFirstStep(const Event&, void*)
		{
			RefreshCheatLists();
			ffx::LogKernelTables();
			ffx::LogGameLists();
			ffx::LogModelTables();
			ffx::LogDebugMenu();
			ffx::LogMinigames();

			if (g_firstStep)
			{
				Unsubscribe(g_firstStep);
				g_firstStep = 0;
			}
		}

		// The minigame launcher's deferred half. A launch row that names a script entry
		// cannot fire it at the time of the warp, because the package is not loaded
		// yet, so the kit holds the request and this hands it a step to land on.
		void OnStepPumpLaunch(const Event&, void*)
		{
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

				if (!Subscribe(EventStep, &OnStepPumpLaunch))
					Log("could not subscribe the minigame launch pump, so a launch row "
					    "that fires a script entry will warp and then do nothing");

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
