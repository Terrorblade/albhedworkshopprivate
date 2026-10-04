#include "ui/CheatPanel.h"
#include "ui/Tabs.h"

#include "ffx/GameLists.h"
#include "ffx/ListExport.h"
#include "ffx/GameState.h"
#include "ffx/GfxContext.h"
#include "ffx/KernelTables.h"
#include "ffx/Models.h"
#include "workshop/Log.h"
#include "workshop/Overlay.h"
#include "workshop/OverlayWidgets.h"

#include "imgui.h"

namespace cheats
{

	using namespace workshop;

	namespace
	{

		int g_panel = 0;

		// Armed by the UI, consumed by the plugin's step handler. One bool written by
		// one thread and cleared by another is enough here: the only failure is a
		// rebuild landing a step later than it could have, and on x86 a naturally
		// aligned bool neither tears nor gets reordered past the step boundary.
		volatile bool g_refreshWanted = false;
		volatile bool g_reprobeWanted = false;

		void Draw(void*)
		{
			// NO NoteOverlayGameThread HERE. It used to be called from this function,
			// which made OverlayOnGameThread trivially true for every panel whatever
			// thread was presenting, so the check was worthless. The plugin's step
			// handler notes the thread now, which is the only place that knows.
			//
			// Everything in a tab is either a read of a static buffer or a write the
			// engine re-derives. Anything heavier is armed here and done on a step.

			if (ImGui::Button("refresh lists"))
				RequestCheatListRefresh();
			ImGui::SameLine();
			ImGui::TextDisabled("options are read from the game, type in a picker to filter");

			ImGui::Separator();

			if (!ImGui::BeginTabBar("##cheattabs", ImGuiTabBarFlags_FittingPolicyScroll))
				return;

			struct Tab
			{
				const char* title;
				void (*draw)();
			};

			// Order is "most used first", not the order they were built.
			static const Tab tabs[] = {
			    { "Game", &DrawGameTab },
			    { "Party", &DrawPartyTab },
			    { "Characters", &DrawCharactersTab },
			    { "Equipment", &DrawEquipmentTab },
			    { "Inventory", &DrawInventoryTab },
			    { "Warp", &DrawWarpTab },
			    { "Battle", &DrawBattleTab },
			    { "Sphere grid", &DrawSphereGridTab },
			    { "Minigames", &DrawMinigamesTab },
			    { "Models", &DrawModelsTab },
			    { "Debug menu", &DrawDebugTab },
			};

			for (int i = 0; i < (int)(sizeof(tabs) / sizeof(tabs[0])); ++i)
			{
				if (!ImGui::BeginTabItem(tabs[i].title))
					continue;
				tabs[i].draw();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

	} // namespace

	void RefreshCheatLists()
	{
		// ---------------------------------------------------------------------------
		// THE CACHE FIRST, AND THE ENGINE ONLY IF IT MISSES.
		//
		// Twelve of these lists cannot change unless the game's archive does, and
		// building them from the engine is what made a boot open and close 402 files
		// and walk tens of thousands of memory spans. The cache is keyed on the MD5
		// the archive stores of its own header, so a changed archive rebuilds by
		// itself. See ffx/ListExport.h.
		// ---------------------------------------------------------------------------
		if (ffx::LoadExportedLists())
		{
			// The five save derived lists are deliberately not cached, because they
			// change as the player plays. They are small and they read the save block,
			// which is a module global, so they cost almost nothing.
			ffx::RefreshSaveDerivedLists();
			return;
		}

		// The name tables first, because the equipment and inventory labels are built
		// from them. Each table is read at most once, so this is nearly free after the
		// first success, and it does nothing at all before the game's file system is up,
		// which is the state during DllMain.
		ffx::LoadKernelTables();
		ffx::RefreshGameLists();

		// The model list is the engine's ROM index rather than the save block, so it is
		// good from boot and never changes, but it borrows the character names for the
		// eighteen models that have one and those come from the save.
		ffx::RefreshModelList();

		// Only now, with everything built from the engine, is there something worth
		// keeping. WriteExportedLists refuses to write an empty or unlive list, so a
		// build that happened too early does not get cached as a failure.
		ffx::WriteExportedLists();
	}

	void RequestCheatListRefresh()
	{
		g_refreshWanted = true;
	}

	bool TryExportedCheatLists()
	{
		static bool tried = false;
		static bool worked = false;
		if (tried)
			return worked;
		tried = true;

		// Reads files with CreateFileW, so it needs nothing from the engine and can
		// run on the very first step.
		if (!ffx::LoadExportedLists())
			return false;

		ffx::RefreshSaveDerivedLists();
		worked = true;
		g_refreshWanted = false;
		return true;
	}

	void RequestEventReprobe()
	{
		g_reprobeWanted = true;
	}

	void PumpCheatListRefresh()
	{
		// FIRST, BEFORE THE PROBE. A cache hit means the 402 engine file opens never
		// happen, so this cannot go after the block below.
		if (TryExportedCheatLists())
		{
			if (!g_refreshWanted)
				return;
		}

		if (g_reprobeWanted)
		{
			g_reprobeWanted = false;
			ffx::ReprobeEventPackages();
			g_refreshWanted = true;
		}

		// The measurement, once, on the first step that gets here. It opens a file per
		// event id so it is not something to do on a map change, and it is what makes
		// the event lists notice content that was added rather than trusting a list
		// baked into this build.
		if (!ffx::EventPackagesProbed())
		{
			// The names first. eventid.bin is what calls an event "testbattle" rather
			// than "ba/btl0000", and a stock boot never reads it.
			ffx::LoadEventTable();
			ffx::ProbeEventPackages();

			// ONLY WHEN IT WORKED. The probe needs the asset loader, which is not up for
			// the whole boot and title sequence, and it is retried on every step until it
			// is. Arming the rebuild unconditionally here would rebuild every picker,
			// the 863-row battle list included, on every one of those steps.
			if (ffx::EventPackagesProbed())
				g_refreshWanted = true;
		}

		if (!g_refreshWanted)
			return;

		g_refreshWanted = false;
		RefreshCheatLists();
	}

	bool StartCheatPanel()
	{
#ifdef ALBHED_NO_OVERLAY
		// ---------------------------------------------------------------------------
		// BISECT BUILD: THE OVERLAY IS NEVER STARTED.
		//
		// Built with /DALBHED_NO_OVERLAY, which build_cheats.bat adds when
		// ALBHED_NO_OVERLAY is set in the environment. No Present hook, no ImGui
		// context, no panel. Everything else is untouched, so the layout check, the
		// lists, the export cache and every table dump still run and still log.
		//
		// The point is to split an intermittent crash in two. ImGui is still linked
		// into this build, it is simply never called, and code that is never called
		// cannot corrupt a heap. So a crash that still happens here is not the
		// overlay's doing, and one that stops happening is.
		// ---------------------------------------------------------------------------
		Log("cheats: BUILT WITHOUT THE OVERLAY, so there is no panel and no ImGui. "
		    "This is a bisect build. The lists and the table dumps below still work.");
		RequestCheatListRefresh();
		return true;
#else
		// WHERE THE SWAPCHAIN IS, BEFORE THE OVERLAY GOES LOOKING FOR IT. The overlay
		// knows no game addresses, so without this it has nothing to hook. It used to
		// make a swapchain of its own and that crashed the boot, because a second
		// swapchain makes the Steam overlay hook Present twice and recurse until the
		// stack is gone.
		ffx::PointOverlayAtSwapChain();

		if (!InstallOverlay())
		{
			Log("cheats: the overlay would not install, so there is no panel");
			return false;
		}

		// NO LIST BUILDING HERE. This runs from DllMain, under the loader lock, long
		// before the engine's file system exists, and every list reads either the
		// archive or the asset loader. The step handler builds them on the first
		// simulation step instead, which is the earliest honest moment.
		//
		// This was a crash, not a theoretical worry: the asset kind selector faults if
		// its resident tables are not up, and DllMain is inside that window.
		RequestCheatListRefresh();

		g_panel = RegisterOverlayPanel("Cheats", &Draw);
		if (!g_panel)
		{
			Log("cheats: the overlay panel table is full");
			return false;
		}

		SetOverlayPanelOpen(g_panel, true);
		return true;
#endif // ALBHED_NO_OVERLAY
	}

	// ---------------------------------------------------------------------------
	// The two helpers every tab uses
	// ---------------------------------------------------------------------------

	void PendingNote(const char* subject, const char* needs)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%s is still being mapped.",
		    subject);
		ImGui::TextWrapped("%s", needs);
	}

	bool RequireGame()
	{
		if (ffx::GameLoaded())
			return true;

		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
		    "No game loaded, so there is nothing to edit here.");
		ImGui::TextDisabled("Load a save or start a new game, then press refresh lists.");
		return false;
	}

} // namespace cheats
