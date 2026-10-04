// The game's own debug menu tab.
//
// The menu is not a reconstruction. Every page draws itself every frame already, from
// FFX_Frame_PresentScene, with no debug gate. What shipped broken is two missing
// calls: nothing ever opens a page, and all eight per-page input handlers are
// orphaned. This tab makes both calls, which is why there is a pump checkbox. The
// input handler has to run every frame or the menu draws and ignores you.
//
// Three of the eight pages test the debug flag as well as their own enabled byte, so
// the flag is not optional for those, and MapSwitching, the warp page, is one of them.

#include "ui/Tabs.h"
#include "ui/CheatPanel.h"

#include "ffx/DebugMenu.h"
#include "ffx/GameState.h"
#include "ffx/WorldState.h"
#include "workshop/CrashHandler.h"
#include "workshop/HangWatchdog.h"
#include "workshop/Events.h"
#include "workshop/Log.h"
#include "workshop/OverlayWidgets.h"

#include "imgui.h"

namespace cheats
{

	using namespace workshop;

	namespace
	{

		// The subscription that restores the input layer. Held so the checkbox can drop
		// it again, and so nothing pumps input for a menu nobody opened.
		int g_pumpSub = 0;

		void OnStepPumpInput(const Event&, void*)
		{
			ffx::PumpDebugMenuInput();
		}

		bool PumpRunning()
		{
			return g_pumpSub != 0;
		}

		void SetPump(bool on)
		{
			if (on == PumpRunning())
				return;

			if (on)
			{
				g_pumpSub = Subscribe(EventStep, &OnStepPumpInput);
				Log("cheats: debug menu input pump %s",
				    g_pumpSub ? "running on the step event" : "could not subscribe");
			}
			else
			{
				Unsubscribe(g_pumpSub);
				g_pumpSub = 0;
				Log("cheats: debug menu input pump stopped");
			}
		}

	} // namespace

	void DrawDebugTab()
	{
		bool debugOn = ffx::IsDebugMode();
		if (ImGui::Checkbox("debug mode", &debugOn))
			ffx::SetDebugMode(debugOn);

		ImGui::SameLine();
		ImGui::TextDisabled("also unlocks the warp gate, the shipped noclip and the SG GUI");

		if (!ffx::DebugMenuAvailable())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
			    "the DebugMenuManager singleton does not exist, which should not happen "
			    "on a normal boot. FFX_GraphicInitialize builds it.");
			return;
		}

		bool pump = PumpRunning();
		if (ImGui::Checkbox("restore the menu's input", &pump))
			SetPump(pump);

		ImGui::SameLine();
		ImGui::TextDisabled("the orphaned handler, run once per step");

		ImGui::Separator();

		const ffx::DebugPage open = ffx::OpenDebugPageNow();
		if (open == ffx::DebugPageCount)
			ImGui::Text("manager mode %d, no page open", ffx::DebugMenuMode());
		else
			ImGui::Text("manager mode %d, open page is %s", ffx::DebugMenuMode(),
			    ffx::DebugPageName(open));

		if (open != ffx::DebugPageCount && !pump)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
			    "that page is drawing but nothing is reading input for it. Tick the box "
			    "above.");

		if (open != ffx::DebugPageCount && ffx::DebugPageNeedsDebugFlag(open)
		    && !ffx::IsDebugMode())
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
			    "that page's input handler also tests the debug flag, so it will not "
			    "respond until debug mode is on.");

		ImGui::Separator();
		ImGui::TextDisabled("the pages. Arrow keys move and change, and they are Phyre "
		                    "key codes rather than Win32 ones, so the game window needs "
		                    "the focus.");

		if (ImGui::BeginTable("##debugpages", 4,
		        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		            | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("page");
			ImGui::TableSetupColumn("mode");
			ImGui::TableSetupColumn("needs the flag");
			ImGui::TableSetupColumn("");
			ImGui::TableHeadersRow();

			for (int i = 0; i < ffx::DebugPageCount; ++i)
			{
				const ffx::DebugPage page = (ffx::DebugPage)i;
				const int mode = ffx::DebugPageMode(page);

				ImGui::PushID(i);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				if (page == open)
					ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s",
					    ffx::DebugPageName(page));
				else
					ImGui::Text("%s", ffx::DebugPageName(page));

				ImGui::TableNextColumn();
				if (mode >= 0)
					ImGui::Text("%d", mode);
				else
					ImGui::TextDisabled("unsettled");

				ImGui::TableNextColumn();
				ImGui::Text("%s", ffx::DebugPageNeedsDebugFlag(page) ? "yes" : "no");

				ImGui::TableNextColumn();
				ImGui::BeginDisabled(mode < 0);
				if (ImGui::SmallButton("open"))
					ffx::OpenDebugPage(page);
				ImGui::EndDisabled();

				if (page == open)
				{
					ImGui::SameLine();
					if (ImGui::SmallButton("up"))
						ffx::MoveDebugPageCursor(page, 0);
					ImGui::SameLine();
					if (ImGui::SmallButton("down"))
						ffx::MoveDebugPageCursor(page, 1);
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		if (ImGui::Button("close the menu"))
			ffx::CloseDebugMenu();

		ImGui::Separator();
		ImGui::TextDisabled("the cheats the Game parameter config page reaches. These are "
		                    "the game's own, they take no arguments and check nothing.");

		if (!RequireGame())
			return;

		if (ImGui::Button("full gil"))
			ffx::DebugFullGil();
		ImGui::SameLine();
		if (ImGui::Button("99 of every item"))
		{
			ffx::DebugFullItems();
			RequestCheatListRefresh();
		}
		ImGui::SameLine();
		if (ImGui::Button("one of every item"))
		{
			ffx::DebugOneOfEveryItem();
			RequestCheatListRefresh();
		}
		ImGui::SameLine();
		if (ImGui::Button("empty the inventory"))
		{
			ffx::DebugClearInventory();
			RequestCheatListRefresh();
		}

		if (ImGui::Button("98 sphere levels for everybody"))
			ffx::DebugFullSphereLevels();
		ImGui::SameLine();
		if (ImGui::Button("max HP, MP and base stats"))
		{
			ffx::DebugMaxHpMpAndStats();
			ffx::RecomputeDerivedStats();
		}

		ImGui::Separator();

		if (ImGui::CollapsingHeader("the PS2 developer GUI, and its one real breakage"))
		{
			ImGui::TextWrapped(
			    "A second and much larger menu, 95 functions of it, with per-character "
			    "windows, camera editors, a battle console and a map jump. Its opener is "
			    "orphaned the same way the page input is and its body is intact. The one "
			    "thing genuinely stubbed out in it is the pointer device read, which was "
			    "replaced with xor eax eax then ret, so it reports success and fills "
			    "nothing and the cursor never moves. The repair is to write the three "
			    "dwords that read was supposed to fill, which the kit does, rather than "
			    "to patch the stub, which has fifteen callers shared with the save and "
			    "file paths.");

			ImGui::BeginDisabled(!ffx::IsDebugMode());
			if (ImGui::Button("open the SG GUI"))
				ffx::OpenSgDebugGui();
			ImGui::EndDisabled();

			if (!ffx::IsDebugMode())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("needs debug mode, both its pumps test the flag");
			}

			int cx = 0;
			int cy = 0;
			if (ffx::SgDebugGuiCursor(&cx, &cy))
				ImGui::Text("its cursor is at %d, %d", cx, cy);

			ImGui::TextWrapped("Feeding it a real mouse is not wired to this panel yet. "
			                   "ffx::FeedSgDebugGuiMouse is the call, once per frame "
			                   "before the step, and the part that needs a decision "
			                   "rather than more research is who gets the pointer while "
			                   "an ImGui window wants it.");
		}

		// -------------------------------------------------------------------
		// The crash log
		// -------------------------------------------------------------------
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Crash reports"))
		{
			ImGui::TextWrapped(
			    "When the game dies, the normal logs just stop and say nothing about "
			    "where. This writes a report instead, to albhed_crash.log next to the "
			    "other logs: the exception, the registers, the bytes at the faulting "
			    "instruction, what the kit was doing at the time, and a call chain.");
			ImGui::TextWrapped(
			    "Every frame is given as module+RVA and then as the address that "
			    "module's own IDB uses, so a line pastes into IDA with no arithmetic. "
			    "Frames in this project's modules are marked.");

			if (workshop::CrashHandlerInstalled())
			{
				const char* owner = workshop::CrashHandlerOwner();
				ImGui::Text("installed, %s", owner[0] ? "by this plugin" : "by another Al Bhed module");
			}
			else
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
				    "NOT INSTALLED, so a crash will leave nothing behind.");

			ImGui::TextDisabled("the game replaces the top level filter during its own "
			                    "startup, so the kit puts ours back once per step.");

			if (ImGui::Button("write a report now"))
			{
				if (workshop::WriteCrashReportNow("asked for from the cheat panel"))
					Log("cheats: wrote a crash report to albhed_crash.log");
				else
					Log("cheats: the crash report could not be written");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("no crash, just the same report for this thread. Worth "
			                    "doing once to check the frames resolve.");

			ImGui::TextDisabled("note: this writes the report for the thread that draws "
			                    "the overlay, which is the Present thread, so the frames "
			                    "will be D3D and ImGui rather than the game's.");

			ImGui::Separator();

			if (ImGui::Button("report the GAME thread now"))
			{
				const unsigned long game = workshop::GameThreadId();
				if (game == 0)
					Log("cheats: the game thread is not known yet, so there is nothing to report");
				else if (workshop::WriteThreadReportNow(game, "asked for from the cheat panel"))
					Log("cheats: wrote a report for game thread %lu", game);
				else
					Log("cheats: the report for game thread %lu could not be written", game);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("suspends the simulation thread for a moment, reads its "
			                    "stack, resumes it. This is the one that shows game frames.");
		}

		if (ImGui::CollapsingHeader("Freeze watchdog"))
		{
			ImGui::TextWrapped(
			    "A freeze leaves even less behind than a crash does. A watchdog thread "
			    "watches a counter the simulation thread bumps every step, and when that "
			    "stops moving it suspends the thread and writes the same report a crash "
			    "writes. It samples up to six times, twenty seconds apart, because the "
			    "same EIP twice is what proves a spin rather than something merely slow.");

			if (workshop::HangWatchdogRunning())
				ImGui::Text("running, threshold %lu ms", workshop::HangThresholdMs());
			else
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
				    "NOT RUNNING, so a freeze will leave nothing behind.");

			if (workshop::GameThreadLooksHung())
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
				    "THE GAME THREAD IS NOT STEPPING RIGHT NOW.");
			else
				ImGui::TextDisabled("the game thread is stepping");

			ImGui::Text("reports written this run: %lu", workshop::HangReportCount());

			static int threshold = 0;
			if (threshold == 0)
				threshold = (int)workshop::HangThresholdMs();
			if (ImGui::SliderInt("threshold ms", &threshold, 1000, 60000))
				workshop::SetHangThresholdMs((unsigned long)threshold);
			ImGui::TextDisabled("a real map load on this engine takes a couple of seconds, "
			                    "so going much below 5000 reports loads as freezes.");
		}
	}

} // namespace cheats
