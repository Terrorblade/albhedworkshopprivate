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
// THE ONE THING THAT MUST NOT BE GOT WRONG. 72 of the 402 event ids have no
// shipped package, and loading one does not fail: FFX_Ev_LoadEventPackage checks
// the package magic and spins in while(1) with no break and no return, so the
// simulation thread is gone and the process has to be killed. The picker only ever
// offers loadable ids, and a typed id is checked with ffx::EventIdLoadable before
// either button is enabled.

#include "ui/Tabs.h"
#include "ui/CheatPanel.h"

#include "ffx/GameLists.h"
#include "ffx/GameState.h"
#include "ffx/WorldState.h"
#include "workshop/Log.h"
#include "workshop/OverlayWidgets.h"

#include "imgui.h"

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

		const PickerList& events = ffx::EventList();
		static PickerState eventPick;
		static int target = 0;

		if (events.Count() > 0)
		{
			PickerById("destination", events.Items(), events.Count(), &eventPick, &target);
			ImGui::TextDisabled("%s", events.Describe());
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
		ImGui::TextDisabled("not needed for this tab. It makes the game's own debug map "
		                    "jump and its JumpMap console command work, which they do "
		                    "not on a stock boot.");

		ImGui::Separator();
		ImGui::TextDisabled("Running one script or one piece of dialogue, rather than "
		                    "loading a whole package, is the \"run an actor script "
		                    "entry\" section of the Minigames tab. An event id loads a "
		                    "package and what runs inside it is ATEL bytecode, so firing "
		                    "an entry is the other half of warping.");
	}

} // namespace cheats
