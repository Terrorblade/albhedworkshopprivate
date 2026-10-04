#include "ui/CheatPanel.h"
#include "ui/Tabs.h"

#include "ffx/GameLists.h"
#include "ffx/GameState.h"
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

		void Draw(void*)
		{
			// The engine is only safe to touch from the thread that presents, and the
			// workshop answers that for us. Everything here is either a read of a
			// static buffer or a write the engine re-derives, but a tab that calls an
			// engine function checks this first.
			NoteOverlayGameThread();

			if (ImGui::Button("refresh lists"))
				RefreshCheatLists();
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
	}

	bool StartCheatPanel()
	{
		if (!InstallOverlay())
		{
			Log("cheats: the overlay would not install, so there is no panel");
			return false;
		}

		// Built once up front so the first frame has something in its combos. The
		// save-block lists come back empty at the title screen, which is correct, and
		// the refresh button or opening a tab picks them up once a game is loaded.
		RefreshCheatLists();
		ffx::LogGameLists();

		g_panel = RegisterOverlayPanel("Cheats", &Draw);
		if (!g_panel)
		{
			Log("cheats: the overlay panel table is full");
			return false;
		}

		SetOverlayPanelOpen(g_panel, true);
		return true;
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
