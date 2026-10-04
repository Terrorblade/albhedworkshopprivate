#include "ui/OverlayPanel.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "imgui.h"

#include "ModState.h"
#include "battle/BattleSync.h"
#include "clones/CloneRoster.h"
#include "clones/CloneSpawner.h"
#include "diag/HashProbe.h"
#include "diag/InteractProbe.h"
#include "ffx/GfxContext.h"
#include "ffx/HideFlags.h"
#include "ffx/Hub.h"
#include "ffx/Walkmesh.h"
#include "hooks/VisibilityDetour.h"
#include "menu/CoopConfig.h"
#include "menu/EscMenuRows.h"
#include "menu/MenuSync.h"
#include "menu/PauseSync.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "workshop/Events.h"
#include "workshop/Log.h"
#include "workshop/Overlay.h"
#include "workshop/Settings.h"
#include "world/Arrival.h"
#include "world/BoosterSync.h"
#include "world/DialogueSync.h"
#include "world/FmvSync.h"
#include "world/HeldObjects.h"
#include "world/MinigameSync.h"
#include "world/PlayerDrive.h"
#include "world/WorldSync.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		int g_panel = 0;

		const char* const InputSourceLabels[InputSource::Count] = {
			"auto", "keyboard", "pad"
		};

		// ---------------------------------------------------------------------------
		// Small wrappers, so a volatile field can meet an ImGui widget that wants a
		// plain pointer.
		// ---------------------------------------------------------------------------

		void Toggle(const char* label, volatile LONG* field)
		{
			bool on = *field != 0;
			if (ImGui::Checkbox(label, &on))
				InterlockedExchange(field, on ? 1 : 0);
		}

		void Request(const char* label, volatile LONG* flag, float width = 0.0f)
		{
			if (ImGui::Button(label, ImVec2(width, 0)))
				InterlockedExchange(flag, 1);
		}

		void Row(const char* label, const char* value)
		{
			ImGui::TextUnformatted(label);
			ImGui::SameLine(110.0f);
			ImGui::TextUnformatted(value ? value : "(null)");
		}

		void RowF(const char* label, const char* format, ...)
		{
			char text[512];
			va_list args;
			va_start(args, format);
			_vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
			va_end(args);
			Row(label, text);
		}

		const char* GaitName()
		{
			if (telemetry.speed <= 0.0f)
				return "idle";
			return (telemetry.speed > 18.0f) ? "run" : "walk";
		}

		// ---------------------------------------------------------------------------

		void DrawStatus()
		{
			RowF("status", "%s", Status());
			RowF("you", "%s %s",
			    telemetry.personaName[0] ? telemetry.personaName : "(Steam not ready)",
			    telemetry.localSteamId);
			RowF("network", "%s", NetworkingSummary());
			RowF("transport", "%s, port %ld", NetworkingBackendName(), settings.hostPort);
			RowF("lockstep", "%s", LockstepSummary());
			RowF("clones", "%ld of %ld live, driving #%ld, last id %ld",
			    LiveCloneCount(), (LONG)MaxClones, ActiveEntry() + 1, telemetry.spawnedChrId);
			RowF("next spawn", "chr id %d, %s", SelectedChrId(), ChrIdName(SelectedChrId()));
			RowF("pool", "%ld live of %ld slots", telemetry.poolLive, telemetry.poolTotal);
			RowF("frames", "%ld, %ld spawn attempts", counters.frames, counters.spawnAttempts);
		}

		void DrawSync()
		{
			// Several of these call an engine function to read a live value, so they
			// only run when Present turns out to be the game's own thread.
			if (!OverlayOnGameThread())
			{
				ImGui::TextWrapped("Present runs on thread %lu, which is not the thread "
				                   "stepping the game. These read engine state, so they are "
				                   "held back rather than called from the wrong thread.",
				    GetCurrentThreadId());
				return;
			}

			RowF("drive", "%s", PlayerDriveStatus());
			RowF("minigame", "%s", MinigameSyncStatus());
			RowF("world", "%s", WorldSyncStatus());
			RowF("arrival", "%s", ArrivalStatus());
			RowF("boosters", "%s", BoosterSyncStatus());
			RowF("battle", "%s", BattleSyncStatus());
			RowF("dialogue", "%s", DialogueSyncStatus());
			RowF("fmv", "%s", FmvSyncStatus());
			RowF("held", "%s", HeldObjectsStatus());
			RowF("menu", "%s", MenuSyncStatus());
			RowF("co-op cfg", "%s", CoopConfigStatus());
			RowF("pause", "%s", PauseSyncStatus());
			RowF("probe", "%s", HashProbeStatus());
			RowF("interact", "%s", InteractProbeStatus());
			RowF("menu row", "%s", EscMenuRowTestStatus());
		}

		void DrawNetwork()
		{
			static char steamId[24] = { 0 };
			static bool steamIdLoaded = false;
			if (!steamIdLoaded)
			{
				strncpy_s(steamId, sizeof(steamId), settings.hostSteamId, _TRUNCATE);
				steamIdLoaded = true;
			}

			ImGui::TextUnformatted(NetworkingSummary());
			ImGui::Separator();

			Request("Host", &requests.startHosting, 90.0f);
			ImGui::SameLine();
			Request("Join", &requests.startJoining, 90.0f);
			ImGui::SameLine();
			Request("Disconnect", &requests.stopNetworking, 110.0f);

			bool useSteam = settings.useSteam != 0;
			if (ImGui::Checkbox("Steam P2P (off is UDP on loopback)", &useSteam))
			{
				InterlockedExchange(&settings.useSteam, useSteam ? 1 : 0);
				SetStatus("transport: %s", NetworkingBackendName());
			}

			// Written on Enter or on losing focus rather than per keystroke, because
			// writing the setting writes the file.
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputText("host SteamID", steamId, sizeof(steamId),
			    ImGuiInputTextFlags_CharsDecimal);
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				const Setting* setting = FindSetting("net.host_steam_id");
				if (setting)
					WriteSettingText(*setting, steamId);
				else
					strncpy_s(settings.hostSteamId, sizeof(settings.hostSteamId), steamId, _TRUNCATE);
				SetStatus("host SteamID set to %s", steamId);
			}

			if (ImGui::Button("copy my SteamID"))
			{
				if (!telemetry.localSteamId[0])
					SetStatus("Steam has not signed in yet, so there is no id to copy");
				else
					ImGui::SetClipboardText(telemetry.localSteamId);
			}
			ImGui::SameLine();
			Request("hash probe", &requests.startHashProbe, 110.0f);

			ImGui::Separator();
			bool enforce = LockstepEnforced();
			if (ImGui::Checkbox("the lockstep gate may refuse a step", &enforce))
				InterlockedExchange(&requests.toggleLockstepEnforce, 1);
			Request("resync the world from the host", &requests.requestWorldResync);
			Request("host takes or gives back menu control", &requests.toggleMenuOverride);
		}

		void DrawClones()
		{
			Request("Spawn", &requests.spawn, 90.0f);
			ImGui::SameLine();
			Request("Despawn", &requests.despawn, 90.0f);
			ImGui::SameLine();
			Request("Despawn all", &requests.despawnAll, 110.0f);

			Request("Next clone", &requests.cycleClone, 110.0f);
			ImGui::SameLine();
			Request("Field diff", &requests.dumpDiff, 110.0f);

			ImGui::Separator();
			ImGui::Text("spawn as chr id %d, %s", SelectedChrId(), ChrIdName(SelectedChrId()));
			if (ImGui::Button("previous##chrid"))
				InterlockedExchange(&requests.cycleSpawnIdBack, 1);
			ImGui::SameLine();
			if (ImGui::Button("next##chrid"))
				InterlockedExchange(&requests.cycleSpawnIdForward, 1);

			// 255 is "not a party member", which is what a clone gets. The others are
			// here so the party index can be ruled in or out as something that gates
			// behaviour, without a rebuild.
			static const LONG partyChoices[] = { 255, 0, 1, 2, 7 };
			ImGui::SameLine();
			if (ImGui::Button("cycle party index"))
			{
				const int count = (int)(sizeof(partyChoices) / sizeof(partyChoices[0]));
				LONG next = partyChoices[0];
				for (int i = 0; i < count; ++i)
					if (partyChoices[i] == settings.spawnPartyIndex)
					{
						next = partyChoices[(i + 1) % count];
						break;
					}
				InterlockedExchange(&settings.spawnPartyIndex, next);
			}
			ImGui::SameLine();
			ImGui::Text("= %ld", settings.spawnPartyIndex);

			float offset = settings.spawnOffset;
			if (ImGui::SliderFloat("spawn offset", &offset, 0.0f, 200.0f, "%.0f units"))
				settings.spawnOffset = offset;
			Toggle("re-bind the walkmesh when it drops", &settings.autoBindWalkmesh);

			ImGui::Separator();
			Toggle("hold the engine cull override on", &settings.disableCull);
			if (VisibilityDetourInstalled())
				Toggle("force our clones visible", &settings.forceVisible);
			else
				ImGui::TextDisabled("force visible needs the visibility detour");
		}

		void DrawDrive()
		{
			Toggle("arrow keys drive the clone", &settings.inputEnabled);
			Toggle("only while the game has focus", &settings.requireForeground);
			Toggle("camera-relative (off is world axes)", &settings.cameraRelative);

			const LONG source = settings.inputSource;
			ImGui::Text("input source: %s",
			    InputSourceLabels[(source >= 0 && source < InputSource::Count) ? source : 0]);
			ImGui::SameLine();
			if (ImGui::Button("cycle##inputsource"))
				InterlockedExchange(&settings.inputSource, (source + 1) % InputSource::Count);

			int padSlot = (int)settings.padSlot;
			if (ImGui::SliderInt("pad slot", &padSlot, 0, 17))
				InterlockedExchange(&settings.padSlot, (LONG)padSlot);

			ImGui::Separator();
			float yaw = settings.yawBiasDegrees;
			if (ImGui::SliderFloat("yaw bias", &yaw, -180.0f, 180.0f, "%.0f deg"))
				settings.yawBiasDegrees = yaw;
			if (ImGui::Button("zero the yaw bias"))
				settings.yawBiasDegrees = 0.0f;

			float walk = settings.walkSpeed;
			if (ImGui::SliderFloat("walk speed", &walk, 1.0f, 60.0f, "%.1f"))
				settings.walkSpeed = walk;
			float run = settings.runSpeed;
			if (ImGui::SliderFloat("run speed", &run, 1.0f, 90.0f, "%.1f"))
				settings.runSpeed = run;

			ImGui::Separator();
			RowF("position", "%.1f  %.1f  %.1f", telemetry.posX, telemetry.posY, telemetry.posZ);
			RowF("speed", "%.2f (%s)", telemetry.speed, GaitName());
			RowF("heading", "%.3f rad, facing %.3f rad", telemetry.moveDirection, telemetry.facing);
			RowF("camera", "%s yaw %.3f rad, input is %s",
			    telemetry.cameraYawValid ? "ok" : "N/A", telemetry.cameraYaw,
			    settings.cameraRelative ? "camera-relative" : "world");
			RowF("pad", "slot %ld %s, %ld bound, driving with %s",
			    settings.padSlot, telemetry.padBound ? "connected" : "absent",
			    telemetry.padsPresent, telemetry.usingPad ? "gamepad" : "keyboard");
		}

		void DrawRender()
		{
			RowF("mesh", "%s, %ld sub-meshes, player has %ld",
			    telemetry.instanceAttached ? "attached" : "NOT YET",
			    telemetry.subMeshCount, telemetry.playerSubMeshCount);
			RowF("linked", "%ld of %ld, instance shown byte %ld",
			    telemetry.subMeshesLinked, telemetry.subMeshCount, telemetry.instanceShownByte);
			RowF("hide", "0x%02lX  %s", (unsigned long)telemetry.hideFlags,
			    HideFlagNames((BYTE)telemetry.hideFlags));
			RowF("cull", "%s", counters.cullOverrideHeld ? "overridden, everything visible" : "normal");
			RowF("force vis", "%s, cleared on %ld frames",
			    settings.forceVisible ? "on, our clones only" : "off",
			    counters.visibilityForcedFrames);
			RowF("alpha", "%.3f, cameLen %.0f, clipZ %.0f",
			    telemetry.shadeAlpha, telemetry.cameLength, telemetry.clipZ);
			RowF("walkmesh", "tri %ld, %ld binds, offset %.1f",
			    telemetry.walkmeshTriangle, WalkmeshBindCount(), settings.spawnOffset);
			RowF("party idx", "%ld", telemetry.partyIndex);
			RowF("flags", "flags1 0x%08lX  flags2 0x%08lX",
			    (unsigned long)telemetry.flags1, (unsigned long)telemetry.flags2);
			ImGui::Separator();
			ImGui::TextWrapped("draw gate: %s", telemetry.drawGate);
		}

		void DrawHub()
		{
			RowF("hub", "%s", HubStatus());
			RowF("overlay", "%s", OverlayStatus());
			ImGui::Separator();

			// Off shows every declared event, including the ones nothing has raised yet,
			// which is how you check the wiring exists before trying to trigger it.
			static bool inUseOnly = true;
			ImGui::Checkbox("only events in use", &inUseOnly);

			if (ImGui::BeginTable("events", 3,
			        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("event");
				ImGui::TableSetupColumn("subscribers");
				ImGui::TableSetupColumn("raised");
				ImGui::TableHeadersRow();
				for (int i = 0; i < EventCount; ++i)
				{
					const EventId id = (EventId)i;
					const int subs = SubscriberCount(id);
					const uint32_t raised = PublishCount(id);
					const bool idle = subs == 0 && raised == 0;
					if (idle && inUseOnly)
						continue;
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (idle)
						ImGui::TextDisabled("%s", EventName(id));
					else
						ImGui::TextUnformatted(EventName(id));
					ImGui::TableNextColumn();
					ImGui::Text("%d", subs);
					ImGui::TableNextColumn();
					ImGui::Text("%lu", (unsigned long)raised);
				}
				ImGui::EndTable();
			}
		}

		void DrawProbes()
		{
			ImGui::TextWrapped("Each of these settles one question, so they stay as buttons "
			                   "rather than being folded into a feature.");
			ImGui::Separator();

			Request("what is nearby", &requests.lookAtInteractables, 160.0f);
			ImGui::SameLine();
			Request("examine the nearest", &requests.fireNearestExamine, 180.0f);

			if (ImGui::Button("arm the esc menu row test", ImVec2(230.0f, 0)))
			{
				InterlockedExchange(&requests.armEscMenuRow, 1);
				SetStatus("esc menu row armed, now press Escape");
			}

			Request("start the save block hash probe", &requests.startHashProbe, 260.0f);
			Request("toggle the engine fixed timestep", &requests.toggleFixedTimeStep, 260.0f);

			ImGui::Separator();
			// These raise a request that the game thread consumes and clears, so they
			// are buttons. A checkbox bound to one would flip straight back.
			Request("toggle: trigger pass follows the active clone", &requests.toggleTriggerFollow, 320.0f);
			Request("toggle: trigger pass may commit events", &requests.toggleTriggerArmed, 320.0f);
			Request("log the trigger pass", &requests.logTriggerPass, 200.0f);
			Request("log the world sync", &requests.logWorldSync, 200.0f);
		}

		// ---------------------------------------------------------------------------

		void Draw(void*)
		{
			if (ImGui::BeginTabBar("pilgrimage"))
			{
				if (ImGui::BeginTabItem("Status"))
				{
					DrawStatus();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Sync"))
				{
					DrawSync();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Network"))
				{
					DrawNetwork();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Clones"))
				{
					DrawClones();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Drive"))
				{
					DrawDrive();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Render"))
				{
					DrawRender();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Probes"))
				{
					DrawProbes();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Hub"))
				{
					DrawHub();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}

			// Cheap when nothing changed, it compares a revision counter first. Saving
			// here rather than at shutdown means a tweak survives the game being killed.
			if (OverlayOnGameThread())
				SaveModSettingsIfChanged();
		}

	} // namespace

	bool StartOverlayPanel()
	{
		if (g_panel != 0)
			return true;

		// WHERE THE SWAPCHAIN IS, BEFORE THE OVERLAY GOES LOOKING FOR IT. The overlay
		// knows no game addresses, so without this it has nothing to hook. It used to
		// make a swapchain of its own and that crashed the boot, because a second
		// swapchain makes the Steam overlay hook Present twice and recurse until the
		// stack is gone.
		ffx::PointOverlayAtSwapChain();

		if (!InstallOverlay())
			return false;

		g_panel = RegisterOverlayPanel("Pilgrimage Together", &Draw);
		if (g_panel == 0)
			return false;

		// The hotkey table owns F11, so the overlay must not also claim it or a
		// press would toggle twice. Other plugins can keep the default.
		SetOverlayToggleKey(0);

		// Hidden until asked for, so a launch looks like the shipped game.
		SetOverlayPanelOpen(g_panel, true);
		SetOverlayVisible(false);
		return true;
	}

	bool OverlayPanelStarted()
	{
		return g_panel != 0;
	}

	void ToggleOverlayPanel()
	{
		if (g_panel == 0)
			return;
		SetOverlayPanelOpen(g_panel, true);
		ToggleOverlay();
	}

} // namespace pilgrimage
