// The sphere grid tab.
//
// A node is "activated" for a character when one bit is set in the save block, so
// everything here is a bit flip plus ffx::RecomputeDerivedStats, which is the call
// the game itself makes after the player spends a sphere. There is no cost model to
// respect and no order to walk in.
//
// The node picker is built from the blob rather than from a list anybody typed, and
// it rebuilds itself when the number of nodes changes, which is what happens when a
// save with a different grid is loaded.

#include "ui/Tabs.h"
#include "ui/CheatPanel.h"

#include "ffx/GameLists.h"
#include "ffx/GameState.h"
#include "ffx/SphereGrid.h"
#include "ffx/WorldState.h"
#include "workshop/OverlayWidgets.h"
#include "workshop/PickerList.h"

#include "imgui.h"

namespace cheats
{

	using namespace workshop;

	namespace
	{

		PickerList g_nodes;
		PickerState g_nodePick;
		int g_nodeSlot = -1;

		PickerState g_gridCharPick;
		int g_gridChar = ffx::kCharTidus;

		const char* kGridNames[] = { "Standard", "Original", "Expert" };

		const char* GridName(int id)
		{
			if (id < 0 || id >= (int)(sizeof(kGridNames) / sizeof(kGridNames[0])))
				return "unknown";
			return kGridNames[id];
		}

		// Whether the empty slots are in the list. Off by default, because on a normal
		// grid most of the 1024 are empty and nothing can be activated in one. On,
		// because an empty slot is still a real slot and seeing the gap is the whole
		// point if something is going to fill it.
		bool g_showEmptySlots = false;
		bool g_listedEmpty = false;

		void RebuildNodeList()
		{
			g_nodes.Reset(g_showEmptySlots ? "sphere grid node array, all 1024 slots"
			                               : "sphere grid node array");
			g_listedEmpty = g_showEmptySlots;
			if (!ffx::SphereGridReadable())
				return;

			for (int slot = 0; slot < ffx::kGridNodeSlotsScanned; ++slot)
			{
				const int kind = ffx::GridNodeKind(slot);
				if (kind < 0)
					continue;

				if (kind == ffx::kGridNodeEmpty)
				{
					// "empty" goes at the front where a filter will find it, and the
					// slot number is still offered because that is the thing an editor
					// would be looking for.
					if (g_showEmptySlots)
						g_nodes.AddFormatted(slot, "empty  slot %d", slot);
					continue;
				}

				// The mask is in the label on purpose. It is the only way to see at a
				// glance which nodes a character is missing without clicking each one.
				g_nodes.AddFormatted(slot, "slot %d, panel kind %d, mask 0x%02X", slot,
				    kind, ffx::GridNodeMask(slot));
			}
			g_nodes.SetLive(!g_nodes.Empty());
		}

		// One rescan when the node count stops matching, which covers loading a save
		// on a different grid. Counting is 1024 byte reads, so it is cheap enough to
		// do per frame and it means there is no stale-list button to remember.
		void KeepNodeListFresh()
		{
			if (g_listedEmpty != g_showEmptySlots)
			{
				RebuildNodeList();
				return;
			}

			// With the empties in, the count the list should have is every readable
			// slot rather than just the filled ones, so the node count alone is not the
			// staleness test any more.
			if (!g_showEmptySlots && g_nodes.Count() != ffx::GridNodeCount())
				RebuildNodeList();
		}

		void DrawNodeInspector()
		{
			KeepNodeListFresh();

			if (g_nodes.Empty())
			{
				ImGui::TextDisabled("the node array has no nodes in it, which means the "
				                    "grid has not been built for this save yet");
				return;
			}

			PickerById("node", g_nodes.Items(), g_nodes.Count(), &g_nodePick, &g_nodeSlot);
			if (g_nodeSlot < 0)
				return;

			ImGui::Text("panel kind %d, activation mask 0x%02X",
			    ffx::GridNodeKind(g_nodeSlot), ffx::GridNodeMask(g_nodeSlot));

			bool wrote = false;
			for (BYTE i = 0; i < (BYTE)ffx::kGridCharacters; ++i)
			{
				bool on = ffx::GridNodeActivated(g_nodeSlot, i);
				if (ImGui::Checkbox(ffx::CharacterName(i), &on))
					wrote = ffx::SetGridNodeActivated(g_nodeSlot, i, on) || wrote;

				if (i % 4 != 3 && i + 1 < (BYTE)ffx::kGridCharacters)
					ImGui::SameLine(0.0f, 20.0f);
			}

			if (wrote)
			{
				ffx::RecomputeDerivedStats();
				RebuildNodeList();
			}
		}

	} // namespace

	void DrawSphereGridTab()
	{
		if (!RequireGame())
			return;

		if (!ffx::SphereGridReadable())
		{
			ImGui::TextDisabled("the grid blob is not readable right now");
			return;
		}

		ImGui::Text("grid %d, %s. %d of %d slots hold a node.", ffx::GridId(),
		    GridName(ffx::GridId()), ffx::GridNodeCount(), ffx::kGridNodeSlotsScanned);

		if (ImGui::Checkbox("show empty slots", &g_showEmptySlots))
			RebuildNodeList();
		ImGui::SameLine();
		ImGui::TextDisabled("nothing can be activated in an empty slot, but the slot is "
		                    "real and the gap is worth seeing");

		const bool menuUp = ffx::SphereGridMenuOpen();
		if (menuUp)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
			    "The grid screen is open, so editing is off. It holds its own copy and "
			    "writes it back on close, which would throw away anything done here.");

		ImGui::Separator();

		// Per character, what the kit can read. Activated versus total is the useful
		// number, the sphere levels are what the player spends to get there.
		if (ImGui::BeginTable("##gridchars", 5,
		        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		            | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("character");
			ImGui::TableSetupColumn("activated");
			ImGui::TableSetupColumn("S.Lv");
			ImGui::TableSetupColumn("spent");
			ImGui::TableSetupColumn("cursor");
			ImGui::TableHeadersRow();

			for (BYTE i = 0; i < (BYTE)ffx::kGridCharacters; ++i)
			{
				ffx::CharRecordData* rec = ffx::CharacterRecord(i);

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", ffx::CharacterName(i));
				ImGui::TableNextColumn();
				ImGui::Text("%d of %d", ffx::GridActivatedCount(i), ffx::GridNodeCount());
				ImGui::TableNextColumn();
				ImGui::Text("%d", rec ? (int)ffx::RecordSphereLevels(rec) : 0);
				ImGui::TableNextColumn();
				ImGui::Text("%d", rec ? (int)ffx::RecordSphereSpent(rec) : 0);
				ImGui::TableNextColumn();
				ImGui::Text("%d", ffx::GridCursorNode(i));
			}
			ImGui::EndTable();
		}

		ImGui::Separator();

		ImGui::BeginDisabled(menuUp);

		const PickerList& people = ffx::CharacterList();
		PickerById("character", people.Items(), people.Count(), &g_gridCharPick,
		    &g_gridChar);

		const bool gridded = g_gridChar >= 0 && g_gridChar < ffx::kGridCharacters;
		if (!gridded)
			ImGui::TextDisabled("only characters 0 to 6 have a grid, so the buttons below "
			                    "are for somebody else");

		ImGui::BeginDisabled(!gridded);

		if (ImGui::Button("activate every node"))
		{
			ffx::ActivateAllNodes((BYTE)g_gridChar);
			ffx::RecomputeDerivedStats();
			RebuildNodeList();
		}
		ImGui::SameLine();
		if (ImGui::Button("clear every node"))
		{
			ffx::ClearAllNodes((BYTE)g_gridChar);
			ffx::RecomputeDerivedStats();
			RebuildNodeList();
		}

		ImGui::EndDisabled();

		ImGui::SameLine(0.0f, 24.0f);
		if (ImGui::Button("activate everything for all seven"))
		{
			for (BYTE i = 0; i < (BYTE)ffx::kGridCharacters; ++i)
				ffx::ActivateAllNodes(i);
			ffx::RecomputeDerivedStats();
			RebuildNodeList();
		}

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Stats and abilities both come out of this, so expect "
			                  "everyone to end up with every spell as well.");

		ImGui::Separator();

		if (ImGui::CollapsingHeader("one node at a time"))
			DrawNodeInspector();

		ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextDisabled("%s", g_nodes.Describe());
	}

} // namespace cheats
