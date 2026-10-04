// Minigames: starting one, and editing the one that is running.
//
// There is no per-minigame entry point in this game. Every field map, cutscene
// and minigame is one ATEL event package named by a single integer, so launching
// is a warp plus, for some of them, firing the actor script entry the NPC
// dialogue would normally have fired. The rows that do that say so, and whether
// firing one cold gives a playable minigame or a half-set-up one is the one thing
// here that nobody has tested in a running game yet. The engine's reentrancy
// guards mean a wrong fire is a quiet no-op or a stuck script rather than a
// crash, so it is cheap to find out.
//
// THE EDIT SIDE SPLITS IN TWO, and the split matters more than it looks. Class 0
// variables are inside the save file, so an edit there follows the player home.
// Everything else lives in the loaded package image, which is freed and
// reallocated on every map change, so an edit there is gone on the next load. The
// tables below mark which is which, and nothing caches a resolved pointer.

#include "ui/Tabs.h"
#include "ui/CheatPanel.h"

#include "ffx/Atel.h"
#include "ffx/Battle.h"
#include "ffx/Encounter.h"
#include "ffx/GameLists.h"
#include "ffx/KernelTables.h"
#include "ffx/GameState.h"
#include "ffx/Minigames.h"
#include "workshop/Log.h"
#include "workshop/Overlay.h"
#include "workshop/OverlayWidgets.h"
#include "workshop/PickerList.h"

#include "imgui.h"

namespace cheats
{

	using namespace workshop;

	namespace
	{

		const ImVec4 kRed = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		const ImVec4 kAmber = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
		const ImVec4 kGreen = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);

		// The arena fight list, which is a CURATED SUBSET with hand written names, not
		// the authority. ffx::BattleList is the authority: it walks btl.bin in memory
		// and has every fight in the game including the developers' test battles. This
		// one survives because "Nemesis" is a more useful row than "zz1 map 604 enc 18",
		// and because it works before the first battle has loaded the encounter table.
		PickerList g_arena;

		void BuildArenaList()
		{
			g_arena.Reset("nagi0700 bytecode, baked");
			const int count = ffx::ArenaFightCount();
			for (int i = 0; i < count; ++i)
			{
				int mapId = 0;
				int encId = 0;
				const char* label = nullptr;
				if (ffx::ArenaFightAt(i, &mapId, &encId, &label) && label)
					g_arena.AddFormatted(i, "%s", label);
			}
			g_arena.SetLive(false);
		}

		// One editable row for a named field. Returns true when the value was written.
		bool DrawFieldRow(const ffx::MinigameField* field)
		{
			double value = 0;
			const bool live = ffx::ReadMinigameField(field, &value);

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::Text("%s", field->name);

			ImGui::TableNextColumn();
			bool wrote = false;
			if (!live)
			{
				ImGui::TextDisabled("not resolvable");
			}
			else if (field->type == ffx::ScriptVarF32)
			{
				float f = (float)value;
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::InputFloat("##v", &f, 0.0f, 0.0f, "%.3f",
				        ImGuiInputTextFlags_EnterReturnsTrue))
					wrote = ffx::WriteMinigameField(field, (double)f);
			}
			else
			{
				int v = (int)value;
				const int lo = (int)field->lo;
				const int hi = (int)field->hi;
				if (IntRow("##v", &v, lo, hi))
					wrote = ffx::WriteMinigameField(field, (double)v);
			}

			ImGui::TableNextColumn();
			if (ffx::ScriptVarClassPersistent(field->storageClass))
				ImGui::TextColored(kAmber, "save file");
			else
				ImGui::TextDisabled("%s", ffx::ScriptVarClassName(field->storageClass));

			ImGui::TableNextColumn();
			if (field->note && field->note[0])
				ImGui::TextWrapped("%s", field->note);
			else
				ImGui::TextDisabled("-");

			return wrote;
		}

		void DrawNamedFields(ffx::Minigame which)
		{
			const int count = ffx::MinigameFieldCount(which);
			if (count == 0)
			{
				ImGui::TextDisabled("No named fields for this one. The Cactuar hunt and "
				                    "Via Purifico genuinely have none, their only state "
				                    "is a handful of world flags, and that is a "
				                    "measurement rather than a gap. For anything else, "
				                    "the raw browser below is the fallback.");
				return;
			}

			if (!ImGui::BeginTable("##fields", 4,
			        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
			            | ImGuiTableFlags_SizingStretchProp))
				return;

			ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthStretch, 1.4f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.2f);
			ImGui::TableSetupColumn("lives in", ImGuiTableColumnFlags_WidthStretch, 0.7f);
			ImGui::TableSetupColumn("note", ImGuiTableColumnFlags_WidthStretch, 2.6f);
			ImGui::TableHeadersRow();

			for (int i = 0; i < count; ++i)
			{
				const ffx::MinigameField* field = ffx::MinigameFieldAt(which, i);
				if (!field)
					continue;
				ImGui::PushID(i);
				DrawFieldRow(field);
				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		// The raw descriptor browser. A package declares a few thousand of these, so
		// the rows are clipped and there is a class filter, because class 6 alone is
		// 5815 descriptors across the shipped set.
		void DrawVariableBrowser()
		{
			const int total = ffx::ScriptVarCount();
			if (total <= 0)
			{
				ImGui::TextDisabled("No descriptor table. That is the state with no event "
				                    "package loaded, which includes the title screen.");
				return;
			}

			int actor = ffx::ScriptVarActor();
			const int actors = ffx::PackageActorCount();
			ImGui::Text("%d descriptors, %d actors in this package", total, actors);

			if (IntRow("actor for the per-actor classes", &actor, 0,
			        actors > 0 ? actors - 1 : 0))
				ffx::SetScriptVarActor(actor);
			ImGui::TextDisabled("classes 2, 3, 4 and 5 are PER ACTOR, so the same "
			                    "descriptor names a different byte for a different "
			                    "actor. Classes 0 and 6 ignore this.");

			static bool showClass[ffx::kScriptClassCount]
			    = { true, false, false, true, false, false, true };
			for (int c = 0; c < ffx::kScriptClassCount; ++c)
			{
				if (c)
					ImGui::SameLine();
				ImGui::PushID(c);
				ImGui::Checkbox(ffx::ScriptVarClassName(c), &showClass[c]);
				ImGui::PopID();
			}
			ImGui::TextDisabled("class 1 is never pointed anywhere in the whole binary, "
			                    "so it is off and reads as not resolvable.");

			// The filtered index list, rebuilt each frame so a changed filter or a new
			// package takes effect with no button.
			static const int kMaxRows = 8192;
			static int rows[kMaxRows];
			int rowCount = 0;
			for (int i = 0; i < total && rowCount < kMaxRows; ++i)
			{
				ffx::ScriptVar var;
				if (!ffx::ScriptVarAt(i, &var))
					continue;
				if (var.storageClass < 0 || var.storageClass >= ffx::kScriptClassCount)
					continue;
				if (!showClass[var.storageClass])
					continue;
				rows[rowCount++] = i;
			}

			ImGui::Text("%d shown", rowCount);

			if (!ImGui::BeginTable("##vars", 6,
			        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
			            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
			        ImVec2(0.0f, 320.0f)))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("var");
			ImGui::TableSetupColumn("class");
			ImGui::TableSetupColumn("type");
			ImGui::TableSetupColumn("offset");
			ImGui::TableSetupColumn("n");
			ImGui::TableSetupColumn("value");
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(rowCount);
			while (clipper.Step())
			{
				for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
				{
					ffx::ScriptVar var;
					if (!ffx::ScriptVarAt(rows[r], &var))
						continue;

					ImGui::PushID(rows[r]);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text("%d", var.index);

					ImGui::TableNextColumn();
					if (ffx::ScriptVarClassPersistent(var.storageClass))
						ImGui::TextColored(kAmber, "%s",
						    ffx::ScriptVarClassName(var.storageClass));
					else
						ImGui::Text("%s", ffx::ScriptVarClassName(var.storageClass));

					ImGui::TableNextColumn();
					ImGui::Text("%s", ffx::ScriptVarTypeName(var.type));

					ImGui::TableNextColumn();
					ImGui::Text("0x%06X", (unsigned)var.offset);

					ImGui::TableNextColumn();
					ImGui::Text("%d", var.elements);

					ImGui::TableNextColumn();
					double value = 0;
					if (!ffx::ReadScriptVar(&var, 0, &value))
					{
						ImGui::TextDisabled("-");
					}
					else if (var.type == ffx::ScriptVarF32)
					{
						float f = (float)value;
						ImGui::SetNextItemWidth(110.0f);
						if (ImGui::InputFloat("##rv", &f, 0.0f, 0.0f, "%.3f",
						        ImGuiInputTextFlags_EnterReturnsTrue))
							ffx::WriteScriptVar(&var, 0, (double)f);
					}
					else
					{
						int v = (int)value;
						ImGui::SetNextItemWidth(110.0f);
						if (ImGui::InputInt("##rv", &v, 1, 10,
						        ImGuiInputTextFlags_EnterReturnsTrue))
							ffx::WriteScriptVar(&var, 0, (double)v);
					}

					ImGui::PopID();
				}
			}

			ImGui::EndTable();
			ImGui::TextDisabled("element 0 only. An array's later elements are reachable "
			                    "through ffx::ReadScriptVar with an element index, they "
			                    "are just not worth a row each here.");
		}

	} // namespace

	void DrawMinigamesTab()
	{
		// ---------------------------------------------------------------
		// What is running
		// ---------------------------------------------------------------
		char package[64];
		const bool havePackage = ffx::CurrentPackageName(package, (int)sizeof(package));
		const ffx::Minigame which = ffx::CurrentMinigame();
		const bool loaded = ffx::PackageLoaded();

		ImGui::Text("package %s, map %d, event %d", havePackage ? package : "unreadable",
		    ffx::LiveMapId(), ffx::CurrentEventId());

		if (which == ffx::MinigameNone)
			ImGui::TextDisabled("not a minigame package");
		else
			ImGui::TextColored(kGreen, "%s", ffx::MinigameName(which));

		if (havePackage && !loaded)
			ImGui::TextColored(kAmber,
			    "there is no package image loaded, so that name is stale. Nothing clears "
			    "the engine's buffer on a map unload.");

		if (ffx::BattleRunning())
			ImGui::TextColored(kAmber,
			    "a battle is running, and a battle does not reload the event package, so "
			    "the name above is the field map underneath. For an overdrive minigame "
			    "read the phase below instead.");

		const int odPhase = ffx::OverdriveMinigamePhase();
		ImGui::TextDisabled("overdrive minigame phase %d (0 idle, 1 armed, 2 running). "
		                    "Its live state is on the Battle tab, it is native globals "
		                    "rather than script variables.",
		    odPhase);

		int armedEvent = 0;
		int armedActor = 0;
		int armedEntry = 0;
		if (ffx::ScriptFireArmed(&armedEvent, &armedActor, &armedEntry))
		{
			ImGui::TextColored(kAmber, "armed: actor %d entry %d will run once event %d "
			                           "is loaded",
			    armedActor, armedEntry, armedEvent);
			ImGui::SameLine();
			if (ImGui::SmallButton("cancel"))
				ffx::CancelScriptFire();
		}

		ImGui::Separator();

		// ---------------------------------------------------------------
		// Launching
		// ---------------------------------------------------------------
		if (ImGui::CollapsingHeader("Start one", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!RequireGame())
			{
				ImGui::TextDisabled("the launcher writes live save data, so it needs a "
				                    "game");
			}
			else
			{
				ImGui::TextDisabled("every row is a map warp. Where it also names a "
				                    "script entry, the warp alone would just drop you on "
				                    "the map next to the NPC, so the fire is armed and "
				                    "happens on the step the package lands.");

				if (ImGui::BeginTable("##launch", 3,
				        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
				            | ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("minigame", ImGuiTableColumnFlags_WidthStretch,
					    1.6f);
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f);
					ImGui::TableSetupColumn("note", ImGuiTableColumnFlags_WidthStretch,
					    2.6f);
					ImGui::TableHeadersRow();

					const int count = ffx::MinigameLaunchCount();
					for (int i = 0; i < count; ++i)
					{
						const ffx::MinigameLaunch* row = ffx::MinigameLaunchAt(i);
						if (!row)
							continue;

						const bool safe = ffx::EventIdLoadable(row->eventId);

						ImGui::PushID(i);
						ImGui::TableNextRow();

						ImGui::TableNextColumn();
						if (row->kind == which)
							ImGui::TextColored(kGreen, "%s", row->label);
						else
							ImGui::Text("%s", row->label);
						ImGui::TextDisabled("event %d, entry %d", row->eventId,
						    row->entryPoint);

						ImGui::TableNextColumn();
						ImGui::BeginDisabled(!safe);
						if (ImGui::Button("go"))
						{
							if (!ffx::LaunchMinigame(row))
								Log("cheats: launching %s was refused", row->label);
						}
						ImGui::EndDisabled();
						if (!safe)
						{
							ImGui::SameLine();
							ImGui::TextColored(kRed, "no package");
						}
						else if (row->scriptEntry >= 0)
						{
							ImGui::SameLine();
							ImGui::TextDisabled("+script");
						}

						ImGui::TableNextColumn();
						ImGui::TextWrapped("%s", row->note);

						ImGui::PopID();
					}
					ImGui::EndTable();
				}

				ImGui::TextDisabled("for any other map or cutscene, the Warp tab has the "
				                    "whole 402 id list.");
			}
		}

		// ---------------------------------------------------------------
		// The flags, which have to be set before the warp
		// ---------------------------------------------------------------
		if (ImGui::CollapsingHeader("Unlocks and package swaps"))
		{
			ImGui::TextColored(kAmber, "the first two swap which bytecode a map id "
			                           "loads, so set them BEFORE warping, not after.");

			bool chocobo = ffx::ChocoboDebugPackage();
			if (ImGui::Checkbox("chocobo race debug package", &chocobo))
				ffx::SetChocoboDebugPackage(chocobo);
			ImGui::SameLine();
			ImGui::TextDisabled("nagi0000 becomes dbg_nagi0000");

			bool thunder = ffx::ThunderPlainTreasure();
			if (ImGui::Checkbox("Thunder Plains treasure", &thunder))
				ffx::SetThunderPlainTreasure(thunder);
			ImGui::SameLine();
			ImGui::TextDisabled("kami0400 becomes the 200-dodge reward map");

			bool arena = ffx::FullMonsterArena();
			if (ImGui::Checkbox("Monster Arena fully unlocked", &arena))
				ffx::SetFullMonsterArena(arena);
			ImGui::SameLine();
			ImGui::TextDisabled("the game's own label calls this Full Arena "
			                    "Localization, which is not what it does");

			bool blitzCheat = ffx::BlitzCheatEnabled();
			if (ImGui::Checkbox("Blitzball pad cheats", &blitzCheat))
				ffx::SetBlitzCheatEnabled(blitzCheat);
			ImGui::SameLine();
			ImGui::TextDisabled("L1 and Up home +1, Down away +1, Left resets the clock, "
			                    "Right ends the half");

			bool odFull = ffx::OverdriveAlwaysFull();
			if (ImGui::Checkbox("overdrive gauges always full", &odFull))
				ffx::SetOverdriveAlwaysFull(odFull);
			ImGui::SameLine();
			ImGui::TextDisabled("the cheapest way into the four battle overdrive "
			                    "minigames");

			ImGui::BeginDisabled(!ffx::GameLoaded());
			if (ImGui::Button("unlock every Blitzball tech"))
			{
				ffx::BlitzUnlockEverything();
				Log("cheats: the Blitzball save block was filled. It does not start a "
				    "match, use the hub row above for that");
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("the game's own, all 60 players. It does not start a "
			                    "match");
		}

		// ---------------------------------------------------------------
		// The one that is running
		// ---------------------------------------------------------------
		if (ImGui::CollapsingHeader("Edit the running minigame",
		        ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!loaded)
				ImGui::TextColored(kAmber, "no package is loaded, so every row below "
				                           "reads as not resolvable.");
			else if (which == ffx::MinigameNone)
				ImGui::TextDisabled("this package is not one of the known minigames. The "
				                    "raw browser below still works on it.");

			ImGui::TextColored(kAmber, "a row marked \"save file\" is serialised byte for "
			                           "byte, so a bad write there follows the player "
			                           "home. Everything else is freed on the next map "
			                           "change.");

			DrawNamedFields(which);
		}

		// ---------------------------------------------------------------
		// Battles
		// ---------------------------------------------------------------
		if (ImGui::CollapsingHeader("Battles, every fight the game can start"))
		{
			const bool allowed = ffx::ScriptedBattleAllowed();

			// THE LIST IS WALKED OUT OF MEMORY, scenes then zones then formation
			// entries, out of the btl.bin the engine already loaded. Nothing here is
			// baked, which is the point: a fight added to that file turns up in this
			// picker with no change to this plugin.
			const PickerList& fights = ffx::BattleList();

			if (fights.Empty())
			{
				ImGui::TextColored(kAmber, "the encounter table has not been read yet");
				ImGui::TextWrapped("btl.bin is loaded at the first battle init, so this "
				                   "list fills in once one fight has started. It is not "
				                   "an error at the title screen. Get into a battle, then "
				                   "press refresh lists.");
			}
			else
			{
				static PickerState fightPick;
				static int battleId = -1;

				// The id in this list IS the battle id, so the first draw has to land on
				// a real row rather than on 0, which is map 0 encounter 0.
				if (battleId < 0)
					battleId = fights.Items()[0].id;

				PickerById("fight", fights.Items(), fights.Count(), &fightPick, &battleId);
				ImGui::TextDisabled("%s, %d scene%s", fights.Describe(),
				    ffx::EncounterSceneCount(),
				    ffx::EncounterSceneCount() == 1 ? "" : "s");

				if (fights.Overflowed())
					ImGui::TextColored(kAmber, "the list overflowed, so some fights are "
					                           "missing from it");

				const int mapId = (battleId >> 16) & 0xFFFF;
				const int encId = battleId & 0xFFFF;
				ImGui::TextDisabled("battle id 0x%08X, which is (map %d << 16) | "
				                    "encounter %d",
				    (unsigned)battleId, mapId, encId);
				ImGui::TextDisabled("a [scripted] row sits in a zone whose encounter rate "
				                    "is 0, so nothing walks into it. That is the arena, "
				                    "the penalty fights and the test battles.");

				// WHAT IS ACTUALLY IN THE FIGHT, read out of its own battle field file.
				// One archive read, so it happens when the selection changes and not per
				// frame. The roster in that file is complete: units that only turn up
				// partway through, Penance's arms and Yu Yevon's pagodas, are in it from
				// the start.
				static int shownFor = -1;
				static ffx::EncounterMonsters shownSet = { 0, { 0 } };
				static bool shownOk = false;

				// ON THE GAME THREAD ONLY. The read goes through the engine's file
				// layer, and a panel draws on whoever calls Present. The plugin's step
				// handler is what makes this answer mean anything.
				const bool canRead = OverlayOnGameThread();

				if (canRead && shownFor != battleId)
				{
					shownFor = battleId;
					shownOk = false;

					int scene = 0;
					int zone = 0;
					int slot = 0;
					if (ffx::BattleListLocate(battleId, &scene, &zone, &slot))
						shownOk = ffx::EncounterMonstersAt(scene, zone, slot, &shownSet);
				}

				if (!canRead)
					ImGui::TextDisabled("the panel is not drawing on the game thread, so "
					                    "this fight's own file is not being read. The "
					                    "enemy list needs that thread.");
				else if (shownOk && shownSet.count > 0)
				{
					ImGui::Text("enemies:");
					for (int i = 0; i < shownSet.count; ++i)
					{
						const char* name
						    = ffx::KernelName(ffx::KernelMonsters, shownSet.ids[i]);
						ImGui::SameLine();
						ImGui::Text("%s%s", name && name[0] ? name : "?",
						    i + 1 < shownSet.count ? "," : "");
					}
				}
				else if (shownOk)
					ImGui::TextDisabled("no enemies in this one. Seven of the 863 are "
					                    "like that and they are all test rows.");
				else
					ImGui::TextDisabled("could not read this fight's battle field file. "
					                    "Scene 0 slot 0 is expected to fail, its path is "
					                    "the developers' own home directory.");

				ImGui::BeginDisabled(!allowed);
				if (ImGui::Button("start this fight"))
				{
					if (!ffx::RequestScriptedBattle(mapId, encId))
						Log("cheats: battle (map %d, encounter %d) was not taken. The "
						    "engine's own function returns -1 either way, so this read "
						    "the pending kind and it did not become 2",
						    mapId, encId);
				}
				ImGui::EndDisabled();
			}

			if (!allowed)
			{
				ImGui::TextColored(kAmber, "battles are disabled or one is already in a "
				                           "phase other than 0, so nothing here will "
				                           "start");
			}

			// FILTERING BY MONSTER, which is how a person actually looks for a fight.
			// It costs one archive read per fight to put the names in the labels, 863
			// of them, so it is a deliberate button rather than the default.
			bool named = ffx::BattleMonsterNames();
			if (ImGui::Checkbox("name every fight by its monsters", &named))
			{
				ffx::SetBattleMonsterNames(named);
				RequestCheatListRefresh();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("reads one archive file per fight, 863 of them, once. "
			                    "Then you can type a monster name to find its fight.");

			// Free during a battle, because the engine already has the section pointer.
			ffx::EncounterMonsters live;
			if (ffx::LiveEncounterMonsters(&live) && live.count > 0)
			{
				ImGui::Separator();
				ImGui::Text("the fight running right now:");
				for (int i = 0; i < live.count; ++i)
				{
					const char* name = ffx::KernelName(ffx::KernelMonsters, live.ids[i]);
					ImGui::SameLine();
					ImGui::Text("%s%s", name && name[0] ? name : "?",
					    i + 1 < live.count ? "," : "");
				}
			}

			if (ImGui::SmallButton("log the whole encounter table"))
				ffx::LogEncounterTable();
			ImGui::SameLine();
			ImGui::TextDisabled("one line per scene with its map, name, zone count and "
			                    "every encounter id in it");

			// ---------------------------------------------------------
			ImGui::Separator();
			ImGui::TextDisabled("the arena and penalty fights with real names. Same ids, "
			                    "read out of nagi0700's bytecode, so this one works "
			                    "before the first battle.");

			if (g_arena.Empty())
				BuildArenaList();

			static PickerState arenaPick;
			static int arenaIndex = 0;
			PickerById("named fight", g_arena.Items(), g_arena.Count(), &arenaPick,
			    &arenaIndex);

			int arenaMap = 0;
			int arenaEnc = 0;
			const char* label = nullptr;
			const bool have = ffx::ArenaFightAt(arenaIndex, &arenaMap, &arenaEnc, &label);
			if (have)
				ImGui::TextDisabled("map %d encounter %d", arenaMap, arenaEnc);

			ImGui::BeginDisabled(!have || !allowed);
			if (ImGui::Button("start the named fight"))
			{
				if (!ffx::RequestScriptedBattle(arenaMap, arenaEnc))
					Log("cheats: arena battle (map %d, encounter %d) was not taken",
					    arenaMap, arenaEnc);
			}
			ImGui::EndDisabled();

			// ---------------------------------------------------------
			ImGui::Separator();
			ImGui::TextDisabled("any pair by hand, same packing. An encounter id above "
			                    "255 can never match: the resolver masks the low word of "
			                    "the battle id but compares it against a byte.");

			static int manualMap = 604;
			static int manualEnc = 0;
			IntRow("map id", &manualMap, 0, 65535);
			IntRow("encounter id", &manualEnc, 0, 255);
			ImGui::BeginDisabled(!allowed);
			if (ImGui::Button("start"))
			{
				if (!ffx::RequestScriptedBattle(manualMap, manualEnc))
					Log("cheats: battle (map %d, encounter %d) was not taken", manualMap,
					    manualEnc);
			}
			ImGui::EndDisabled();
		}

		// ---------------------------------------------------------------
		// Firing a script entry by hand
		// ---------------------------------------------------------------
		if (ImGui::CollapsingHeader("Run an actor script entry"))
		{
			ImGui::TextWrapped(
			    "This is what actually starts a field minigame once its map is loaded, "
			    "and it is also how a cutscene or a piece of dialogue runs without "
			    "loading a whole package. The engine bounds checks neither the entry "
			    "index nor the channel, so both are clamped here against the actor's own "
			    "entry table.");

			const int actors = ffx::PackageActorCount();
			ImGui::Text("%d actors in this package", actors);

			static int fireActor = 0;
			static int fireEntry = 0;
			static int fireChannel = 1;
			IntRow("actor", &fireActor, 0, actors > 0 ? actors - 1 : 0);

			const int entries = ffx::ActorScriptEntryCount(fireActor);
			if (entries > 0)
				ImGui::TextDisabled("that actor has %d script entries", entries);
			else
				ImGui::TextColored(kAmber, "that actor's definition is not readable");

			IntRow("entry", &fireEntry, 0, entries > 0 ? entries - 1 : 0);
			IntRow("channel", &fireChannel, 0, 8);
			ImGui::TextDisabled("channel 1 is what the engine's own arrival script uses");

			ImGui::BeginDisabled(entries <= 0);
			if (ImGui::Button("run it now"))
			{
				const int started = ffx::StartActorScript(0xFFFF, fireActor, fireChannel,
				    fireEntry);
				Log("cheats: actor %d entry %d on channel %d, thread %s", fireActor,
				    fireEntry, fireChannel, started ? "started" : "refused");
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("no dedupe, so a second press stacks a second thread");

			ImGui::BeginDisabled(entries <= 0);
			if (ImGui::Button("as an examine event"))
			{
				const int started = ffx::FireActorEventWithEntry(0xFFFF, fireActor,
				    ffx::kAtelEventExamine, fireEntry);
				Log("cheats: examine on actor %d entry %d, %s", fireActor, fireEntry,
				    started ? "started" : "refused by the channel or the event mask");
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("keeps the engine's channel and event-mask checks, which "
			                    "is the safer of the two");

			ImGui::Separator();
			static int armEvent = 223;
			IntRow("arm for event", &armEvent, 0, 401);
			if (ImGui::Button("arm"))
			{
				if (!ffx::ArmScriptFire(armEvent, fireActor, fireEntry))
					Log("cheats: that arm request was refused");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("runs the actor and entry above once that event is "
			                    "loaded, which is what the launcher rows do");
		}

		// ---------------------------------------------------------------
		// The raw browser
		// ---------------------------------------------------------------
		if (ImGui::CollapsingHeader("All script variables, raw"))
		{
			ImGui::TextWrapped(
			    "Every variable the loaded package declares, typed and resolved through "
			    "the engine's own resolver. This is the fallback for a minigame with no "
			    "named rows, and for working out what an unnamed slot is: park on a "
			    "class and watch which one moves.");
			DrawVariableBrowser();
		}
	}

} // namespace cheats
