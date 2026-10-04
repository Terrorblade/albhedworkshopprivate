// Warping, which in this game is the same thing as starting an event.
//
// A field map IS an event package here, so one id space covers "take me to Luca"
// and "play that cutscene". ffx::EventList is that list.
//
// Two ways in, and both are offered because they are not interchangeable.
// WarpWithSavedFade is what the game itself uses: its only callers are the four
// ATEL opcodes behind every door, save point and airship destination in FFX, and
// it arms its own gate. RequestMapChange instead raises the deferred flag that
// FFX_Atel_StepOnce picks up a simulation step later, which is the one lockstep
// wants because both peers agree on the step it lands on.
//
// THE ONE THING THAT MUST NOT BE GOT WRONG. Not every one of the 402 event ids
// has a shipped package, and loading one that does not is not a failure:
// FFX_Ev_LoadEventPackage checks the package magic and spins in while(1) with no
// break and no return, so the simulation thread is gone and the process has to be
// killed. Every id offered here is checked with ffx::EventIdLoadable before either
// button is enabled, and after the first step that check is a MEASUREMENT, because
// the kit has asked the engine to open each package and tell it the size.
//
// "show everything" switches to the unfiltered list, which is the one that has the
// developers' test and sample packages in it. The rows that cannot be loaded stay
// in that list with their reason in the label, because an editor that adds a
// package wants to see the id it is filling, and the buttons stay disabled for
// them exactly as they would for a typed id.

#include "ui/Tabs.h"
#include "ui/CheatPanel.h"

#include "ffx/AssetPaths.h"
#include "ffx/GameLists.h"
#include "ffx/GameState.h"
#include "ffx/WorldState.h"
#include "workshop/Log.h"
#include "workshop/Overlay.h"
#include "workshop/OverlayWidgets.h"

#include "imgui.h"

#include <string.h>

namespace cheats
{

	using namespace workshop;

	void DrawWarpTab()
	{
		const int current = ffx::CurrentEventId();
		char name[16];
		if (ffx::EventNameForId(current, name, (int)sizeof(name)))
			ImGui::Text("here: event %d (%s)", current, name);
		else
			ImGui::Text("here: event %d", current);

		ffx::MapChangeState state;
		if (ffx::ReadMapChangeState(&state) && state.valid)
			ImGui::TextDisabled("pending %s, %d step%s to wait, scene %s",
			    state.pending ? "YES" : "no", state.delayFrames,
			    state.delayFrames == 1 ? "" : "s",
			    state.sceneLoaded ? "loaded" : "not loaded");

		ImGui::Separator();

		static bool showEverything = false;
		ImGui::Checkbox("show everything", &showEverything);
		ImGui::SameLine();
		ImGui::TextDisabled("the test and sample packages too, loadable or not");

		const PickerList& events
		    = showEverything ? ffx::AllEventList() : ffx::EventList();
		static PickerState eventPick;
		static int target = 0;

		if (events.Count() > 0)
		{
			PickerById("destination", events.Items(), events.Count(), &eventPick, &target);
			ImGui::TextDisabled("%s", events.Describe());

			if (ffx::EventPackagesProbed())
			{
				const unsigned bytes = ffx::EventPackageBytes(target);
				if (bytes)
					ImGui::TextDisabled("event %d, package measures %u bytes", target,
					    bytes);
				else
					ImGui::TextDisabled("event %d, no package in the archive", target);
			}
			else
				ImGui::TextDisabled("package sizes have not been measured yet, so "
				                    "loadability is coming from a list baked into this "
				                    "build. One simulation step fixes that.");
		}
		else
		{
			// The path table is filled by the engine before anything can load, so an
			// empty list here means the panel opened before graphics init.
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
			    "The event list has not been read yet.");
			ImGui::TextWrapped(
			    "It comes from the game's own asset path table, which is filled during "
			    "graphics init, so this clears once the game is past the loading screen. "
			    "Press refresh lists.");
			IntRow("destination id", &target, 0, 401);
		}

		// Whatever route the id came from, say whether it can actually be loaded. This
		// is the guard that matters: an id with no shipped package does not fail, it
		// spins the simulation thread in while(1) and the only way out is killing the
		// game.
		const bool loadable = ffx::EventIdLoadable(target);
		if (!loadable)
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
			    "event %d ships no package. Warping to it would hang the game, so the "
			    "buttons below are disabled.",
			    target);

		static int entryPoint = 0;
		IntRow("entry point", &entryPoint, 0, 63);
		ImGui::TextDisabled("which arrival point on the map. 0 is the usual one.");

		ImGui::Separator();

		if (!loadable)
			ImGui::BeginDisabled();

		// The one the game uses itself. Its only callers are the four ATEL opcodes
		// behind every door, save point and airship destination in FFX, and it arms
		// its own gate, so it is the one to reach for.
		if (ImGui::Button("go"))
			ffx::WarpWithSavedFade(target, entryPoint);
		ImGui::SameLine();
		ImGui::TextDisabled("the warp every door in the game uses");

		// The deferred one, which lands on a step boundary. That is what makes it the
		// right one under lockstep, so it stays offered even though "go" is better for
		// a single player.
		if (ImGui::Button("request map change"))
		{
			if (!ffx::RequestMapChange(target, entryPoint))
				Log("cheats: event %d was refused. 399 is the leave-field pseudo id and "
				    "23 is the title, and both mean something other than 'load this'",
				    target);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("deferred to a step boundary, no fade");

		if (!loadable)
			ImGui::EndDisabled();

		ImGui::Separator();
		ImGui::TextDisabled("the other two the game does itself");

		if (ImGui::Button("resume from checkpoint"))
		{
			if (!ffx::RequestResumeFromCheckpoint())
				Log("cheats: there is no checkpoint to resume to. the resume consumes it, "
				    "so this only works once per save");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("to the last save sphere");

		if (ImGui::Button("reload this map"))
		{
			if (!ffx::RequestLoadLiveLocation())
				Log("cheats: the live location names nowhere to go");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("loads whatever the live fields already say");

		if (ImGui::Button("leave field"))
			ffx::RequestLeaveField();
		ImGui::SameLine();
		ImGui::TextDisabled("not a map load, id %d", ffx::LeaveFieldMapId());

		ImGui::Separator();
		static int fade = 30;
		IntRow("transition steps", &fade, 0, 120);
		ImGui::SameLine();
		if (ImGui::SmallButton("set"))
			ffx::SetMapTransitionFrames(fade);
		ImGui::TextDisabled("a bare request sets no fade, the full warp sets 30 or 0");

		ImGui::Separator();
		if (ImGui::Button("load the event name table"))
			ffx::LoadEventTable();
		ImGui::SameLine();
		ImGui::TextDisabled("done for you on the first step. It is also what makes the "
		                    "game's own debug map jump and its JumpMap console command "
		                    "work, which they do not on a stock boot.");

		if (ImGui::Button("measure the packages again"))
			RequestEventReprobe();
		ImGui::SameLine();
		ImGui::TextDisabled("402 file opens. For after something has added a package, "
		                    "which is the case the measurement exists for.");

		// -------------------------------------------------------------------
		// Reaching the packages no id names
		// -------------------------------------------------------------------
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Make the test packages loadable"))
		{
			ImGui::TextWrapped(
			    "67 event packages ship inside the archive that no id points at, and "
			    "they are the developers' own: test01 through test40, sample01 and 02, "
			    "testbattle, testpub1 to 4, testfont, soundtest, zooo0000. 54 of the 402 "
			    "ids have no path, so most of them can be given one.");
			ImGui::TextWrapped(
			    "Nothing here is typed in or baked. The name comes from eventid.bin, the "
			    "path shape is the archive's own convention, and whether the file is "
			    "really there is a question put to the archive.");

			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
			    "NONE OF THIS HAS EVER BEEN RUN. The path table edit was derived from "
			    "the disassembly and has not been exercised in a live game, and the "
			    "packages themselves are unfinished by definition.");

			// Everything below reads the archive and writes the engine's path table.
			const bool canEdit = OverlayOnGameThread();
			if (!canEdit)
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
				    "The panel is not drawing on the game thread, so this section is "
				    "disabled. It reads the archive and moves bytes the engine reads.");

			ImGui::BeginDisabled(!canEdit);

			// The summary costs 54 archive probes, so it is taken on demand and kept.
			static bool haveSummary = false;
			static int emptySlots = 0;
			static int fillable = 0;
			static int bootScenes = 0;

			if (canEdit && !haveSummary)
			{
				emptySlots = 0;
				fillable = 0;
				bootScenes = 0;
				for (int id = 0; id < 402; ++id)
				{
					if (!ffx::EventPackagePathSlotEmpty(id))
						continue;
					++emptySlots;

					if (ffx::EventIdIsBootScene(id))
					{
						++bootScenes;
						continue;
					}

					char derived[160];
					if (ffx::DeriveEventPackagePath(id, derived, (int)sizeof(derived)))
						++fillable;
				}
				haveSummary = true;
			}

			if (!ffx::EventTableLoaded())
				ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
				    "the event name table is not loaded, so no path can be derived. It "
				    "is read on the first simulation step, or by the button above.");
			else
				ImGui::Text("%d ids have no path. %d of them have a package in the "
				            "archive, and %d are the hardcoded boot scenes.",
				    emptySlots, fillable, bootScenes);

			ImGui::TextDisabled("the boot scenes, 393 to 399, are left alone on purpose. "
			                    "The loader sends those to hardcoded paths before it "
			                    "looks at the table, so an entry would be ignored while "
			                    "still making this picker think they are loadable.");

			if (ImGui::Button("give every one of them a path"))
			{
				const int filled = ffx::AddDerivedEventPackagePaths(false);
				Log("cheats: filled %d empty event ids", filled);
				haveSummary = false;
				ffx::ReprobeEventPackages();
				RequestCheatListRefresh();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("recount"))
				haveSummary = false;

			// -------------------------------------------------------------
			ImGui::Separator();
			ImGui::TextDisabled("or just the one selected in the picker above");

			if (ffx::EventPackagePathSlotEmpty(target))
			{
				char derived[160];
				if (ffx::DeriveEventPackagePath(target, derived, (int)sizeof(derived)))
				{
					ImGui::TextWrapped("event %d has no path. The archive does hold "
					                   "\"%s\".",
					    target, derived);

					ImGui::BeginDisabled(ffx::EventIdIsBootScene(target));
					if (ImGui::Button("give event this path"))
					{
						if (ffx::AddEventPackagePath(target, derived))
						{
							haveSummary = false;
							ffx::ReprobeEventPackages();
							RequestCheatListRefresh();
						}
					}
					ImGui::EndDisabled();
				}
				else
					ImGui::TextDisabled("event %d has no path and the archive holds no "
					                    "package for its name either, so there is "
					                    "nothing to point it at.",
					    target);
			}
			else
			{
				// The other half of the orphan set: prefixed variants sitting in the
				// base name's own directory, which need no new id because the base id
				// already resolves.
				const int variants = ffx::EventPackageVariantCount(target);
				if (variants <= 0)
					ImGui::TextDisabled("event %d already has a path and the archive "
					                    "holds no variant of it.",
					    target);
				else
				{
					ImGui::TextWrapped("event %d already resolves, and the archive holds "
					                   "%d variant%s of its package. Those are the "
					                   "blitzball alternates and the debug versions. A "
					                   "substitution points this id at one, which needs "
					                   "no new id and is reversible.",
					    target, variants, variants == 1 ? "" : "s");

					char key[160];
					const bool haveKey = ffx::EventPackageSubstitutionKey(target, key,
					    (int)sizeof(key));

					for (int v = 0; v < variants; ++v)
					{
						char path[192];
						if (!ffx::EventPackageVariantAt(target, v, path,
						        (int)sizeof(path)))
							continue;

						// The value the substitution needs is the same tail as the key,
						// which is everything after "/event/obj/".
						const char* marker = strstr(path, "/event/obj/");
						const char* value = marker ? marker + 11 : nullptr;

						ImGui::PushID(v);
						ImGui::BeginDisabled(!haveKey || !value);
						if (ImGui::SmallButton("use"))
						{
							const int slot = ffx::RegisterAssetSubstitution(key, value);
							if (slot < 0)
								Log("cheats: no substitution slot was free");
							else
								RequestCheatListRefresh();
						}
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::TextDisabled("%s", value ? value : path);
						ImGui::PopID();
					}
				}
			}

			// -------------------------------------------------------------
			ImGui::Separator();
			ImGui::Text("%d of %d substitution slots used", ffx::AssetSubstitutionCount(),
			    ffx::kAssetSubstitutionSlots);

			for (int i = 0; i < ffx::kAssetSubstitutionSlots; ++i)
			{
				const char* k = nullptr;
				const char* v = nullptr;
				if (!ffx::AssetSubstitutionAt(i, &k, &v))
					continue;

				ImGui::PushID(1000 + i);
				if (ImGui::SmallButton("drop"))
					ffx::UnregisterAssetSubstitution(i);
				ImGui::SameLine();
				ImGui::TextDisabled("%d: %s -> %s", i, k, v);
				ImGui::PopID();
			}

			ImGui::TextDisabled("one of those is the game's own, for the localised event "
			                    "directory. Dropping it would break localised text.");

			if (ImGui::Button("log the path table"))
				ffx::LogAssetPaths();
			ImGui::SameLine();
			ImGui::TextDisabled("counts, the kind bases, every empty id, and the "
			                    "substitution slots");

			ImGui::EndDisabled();
		}

		ImGui::Separator();
		ImGui::TextDisabled("Running one script or one piece of dialogue, rather than "
		                    "loading a whole package, is the \"run an actor script "
		                    "entry\" section of the Minigames tab. An event id loads a "
		                    "package and what runs inside it is ATEL bytecode, so firing "
		                    "an entry is the other half of warping.");
	}

} // namespace cheats
