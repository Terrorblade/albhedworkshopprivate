// The model tab.
//
// The list is the engine's own ROM index, so every row in it is a model the engine
// says it will load. That is a stronger statement than "the asset ships", and it is
// why the picker is built from the table rather than from the archive.
//
// Typing in the picker filters on the label, and the label carries the category
// directory, so "mon" narrows it to monsters and "s0" to the aeons.

#include "ui/Tabs.h"
#include "ui/CheatPanel.h"

#include "ffx/GameState.h"
#include "ffx/Models.h"
#include "workshop/Log.h"
#include "workshop/OverlayWidgets.h"

#include "imgui.h"

namespace cheats
{

	using namespace workshop;

	namespace
	{

		PickerState g_modelPick;
		int g_modelId = 0;

		// The last thing the swap said, kept so the answer stays on screen rather than
		// flashing for one frame.
		ffx::SwapResult g_lastResult = ffx::SwapOk;
		bool g_everTried = false;

	} // namespace

	void DrawModelsTab()
	{
		const PickerList& models = ffx::ModelList();

		if (!ffx::ModelTablesReady())
		{
			ImGui::TextDisabled("the engine's model tables are not loaded yet. They fill "
			                    "during boot, before any map, so this clears by itself.");
			return;
		}

		char current[8] = { 0 };
		const int playerModel = ffx::PlayerModelId();
		ffx::ModelName(playerModel, current, (int)sizeof(current));

		if (ffx::PlayerBody())
			ImGui::Text("the player is wearing %s (0x%04X)", current[0] ? current : "?",
			    playerModel);
		else
			ImGui::TextDisabled("there is no player character right now");

		const int slots = ffx::ModelDataSlotsFree();
		ImGui::Text("%d models listed across %d categories, %d of the engine's 40 model "
		            "data records free",
		    ffx::ModelTotal(), ffx::kModelCategories, slots);

		if (slots <= 0)
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
			    "no free model data record, so a swap is refused. Claiming a 41st is a "
			    "write through a null pointer in the engine, not a failed allocation. "
			    "A map change frees them.");

		ImGui::Separator();

		PickerById("model", models.Items(), models.Count(), &g_modelPick, &g_modelId);

		char wanted[8] = { 0 };
		ffx::ModelName(g_modelId, wanted, (int)sizeof(wanted));
		const int category = ffx::ChrIdCategory(g_modelId);

		ImGui::Text("category %d %s, motion mode %d", category,
		    ffx::ModelCategoryDir(category), ffx::ModelMotionMode(category));

		const bool ready = ffx::PlayerBody() != nullptr && ffx::ModelLoadable(g_modelId)
		    && slots > 0;

		ImGui::BeginDisabled(!ready);
		if (ImGui::Button("wear this model"))
		{
			g_lastResult = ffx::SwapPlayerModel(g_modelId);
			g_everTried = true;
			Log("cheats: swap to %s returned %d, %s", wanted[0] ? wanted : "?",
			    (int)g_lastResult, ffx::SwapResultText(g_lastResult));
		}
		ImGui::EndDisabled();

		if (!ffx::ModelLoadable(g_modelId))
		{
			ImGui::SameLine();
			ImGui::TextDisabled("the engine does not list that id");
		}

		if (g_everTried)
		{
			const bool ok = g_lastResult == ffx::SwapOk;
			ImGui::TextColored(
			    ok ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
			    "%s", ffx::SwapResultText(g_lastResult));
		}

		ImGui::Separator();

		ImGui::TextWrapped(
		    "Two things worth knowing before they surprise you. A swap lasts until the "
		    "next map or cutscene, because the event script re-authors the player "
		    "actor's model from the map's own data on a transition, so it has to be "
		    "re-applied after one. And the new body's mesh appears one to three frames "
		    "later rather than instantly, because the model load is asynchronous. The "
		    "engine's own spawn path has the same lag.");

		ImGui::TextWrapped("Any model in the list works from anywhere. The engine's own "
		                   "loader creates the cache entry and blocks until the read has "
		                   "landed, so there is no 'that model is not on this map'.");

		if (ImGui::CollapsingHeader("what the engine lists per category"))
		{
			if (ImGui::BeginTable("##modelcats", 4,
			        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("category");
				ImGui::TableSetupColumn("letter");
				ImGui::TableSetupColumn("listed");
				ImGui::TableSetupColumn("motion mode");
				ImGui::TableHeadersRow();

				for (int cat = 0; cat < ffx::kModelCategories; ++cat)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%d %s", cat, ffx::ModelCategoryDir(cat));
					ImGui::TableNextColumn();
					ImGui::Text("%c", ffx::ModelCategoryLetter(cat));
					ImGui::TableNextColumn();
					ImGui::Text("%d", ffx::ModelCount(cat));
					ImGui::TableNextColumn();
					ImGui::Text("%d", ffx::ModelMotionMode(cat));
				}
				ImGui::EndTable();
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("%s", models.Describe());
	}

} // namespace cheats
