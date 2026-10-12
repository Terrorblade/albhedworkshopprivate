#include "ui/OverlayPanel.h"

#include <ctype.h>
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
#include "world/CoopBody.h"
#include "menu/EscMenuRows.h"
#include "menu/MenuSync.h"
#include "menu/PauseSync.h"
#include "net/LockstepLink.h"
#include "net/SessionPhase.h"
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

			// DIRECTLY UNDER THE NETWORK LINE, because "connected" and "playing together"
			// are different things and the gap between them is where somebody connecting
			// from the title screen spends their time. The reason is printed rather than
			// just the phase, since "lobby" tells a player nothing and "waiting for the
			// host to load a save" tells them whether to sit still.
			RowF("session", "%s - %s", SessionPhaseName(CurrentSessionPhase()),
			    SessionPhaseReason());
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
			RowF("bodies", "%s", CoopBodySummary());
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

		// Defined below, next to the clone tab it shares its list with.
		void DrawMyCharacter();

		void DrawNetwork()
		{
			static char steamId[24] = { 0 };
			static char hostAddress[64] = { 0 };
			static bool fieldsLoaded = false;
			if (!fieldsLoaded)
			{
				strncpy_s(steamId, sizeof(steamId), settings.hostSteamId, _TRUNCATE);
				strncpy_s(hostAddress, sizeof(hostAddress), settings.hostAddress, _TRUNCATE);
				fieldsLoaded = true;
			}

			ImGui::TextUnformatted(NetworkingSummary());

			// EXACTLY ONE OF YOU HOSTS. Spelled out because two people both pressed Host
			// and then spent four minutes wondering why neither of them had joined, and
			// the refusal that explains it was in a log file at the time.
			ImGui::TextUnformatted("One of you presses Host, the other presses Join. "
			                       "Both hosting connects nobody.");
			ImGui::Separator();

			DrawMyCharacter();
			ImGui::Separator();

			Request("Host", &requests.startHosting, 90.0f);
			ImGui::SameLine();
			Request("Join", &requests.startJoining, 90.0f);
			ImGui::SameLine();
			Request("Disconnect", &requests.stopNetworking, 110.0f);

			bool useSteam = settings.useSteam != 0;
			if (ImGui::Checkbox("Steam P2P (off is plain UDP to the address below)", &useSteam))
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

			if (useSteam)
			{
				ImGui::TextUnformatted(telemetry.localSteamId[0]
				        ? "Give them the id below. Steam needs no address and no port."
				        : "Steam has not signed in yet.");
			}
			else
			{
				// Written the same way as the SteamID field, on Enter or on losing
				// focus, because writing a setting writes the file.
				ImGui::SetNextItemWidth(220.0f);
				ImGui::InputText("host address (UDP)", hostAddress, sizeof(hostAddress));
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					const Setting* setting = FindSetting("net.host_address");
					if (setting)
						WriteSettingText(*setting, hostAddress);
					else
						strncpy_s(settings.hostAddress, sizeof(settings.hostAddress),
						    hostAddress, _TRUNCATE);
					SetStatus("host address set to %s", hostAddress);
				}

				const bool loopback = strcmp(settings.hostAddress, "127.0.0.1") == 0
				    || strcmp(settings.hostAddress, "localhost") == 0
				    || settings.hostAddress[0] == 0;
				ImGui::TextUnformatted(loopback
				        ? "127.0.0.1 IS THIS PC. Only reaches another copy of the game "
				          "here. For a friend, put their address in, or use Steam."
				        : "Their machine has to be reachable on this address and port, "
				          "which usually means a port forward. Steam needs neither.");
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

		// A case insensitive "does name contain needle". By hand because the CRT has
		// no portable strcasestr and a 21 entry list does not justify pulling one in.
		bool ContainsNoCase(const char* name, const char* needle)
		{
			if (!name || !needle || !*needle)
				return true;

			for (const char* start = name; *start; ++start)
			{
				const char* a = start;
				const char* b = needle;
				while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b))
				{
					++a;
					++b;
				}
				if (!*b)
					return true;
			}
			return false;
		}

		// ---------------------------------------------------------------------------
		// WHO I PLAY AS. The one answer each player gives before connecting.
		//
		// Built from SpawnableChrIds and ChrIdName rather than from a hardcoded list of
		// names, so it says whatever the game's own name table says, in whatever
		// language the game is in, and a new id added to the kit's list turns up here
		// with no change to this file.
		//
		// Every entry is a filter box away rather than a scroll away because 21 of them
		// is already more than fits comfortably, and the list is sorted the way the kit
		// declares it, which puts the party in id order at the top.
		//
		// This writes the setting through the registry rather than the field, because
		// the registry is what makes it survive a restart.
		// ---------------------------------------------------------------------------
		void DrawMyCharacter()
		{
			static char filter[32] = "";

			ImGui::Text("I play as: %s", ChrIdName(SelectedChrId()));

			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputTextWithHint("##chrfilter", "type to filter the list", filter,
			    sizeof(filter));

			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("my character", ChrIdName(SelectedChrId())))
			{
				for (int i = 0; i < SpawnableChrIdCount; ++i)
				{
					const char* name = ChrIdName(SpawnableChrIds[i]);

					// A case insensitive contains, done by hand. The alternative is
					// _stricmp on a lowered copy, and a 21 entry list does not need it.
					if (filter[0] && !ContainsNoCase(name, filter))
						continue;

					const bool selected = (i == (int)settings.spawnIdIndex);
					if (ImGui::Selectable(name, selected))
					{
						const Setting* setting = FindSetting("spawn.chr_id_index");
						if (setting)
							WriteSetting(*setting, (float)i);
						else
							InterlockedExchange(&settings.spawnIdIndex, i);
						SetStatus("you play as %s", ChrIdName(SpawnableChrIds[i]));
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			// Said plainly because it is the question everybody asks next, and because
			// the answer is not what a party based co-op mod would do.
			ImGui::TextUnformatted("Each of you picks your own. It is not shared, so you "
			                       "do not have to match.");
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

		// SHARE THE LOADER'S ImGui, do not start one. See the matching comment in the
		// cheat plugin: two modules each calling InstallOverlay is two contexts and two
		// Present hooks, which is what this replaces. A false is the ordinary "no frame
		// yet" answer and not a reason to give up, because the host points this
		// module's ImGui at its context right before it calls the panel below.
		if (!AdoptOverlayHost())
			Log("overlay: not up yet, which is normal this early. %s",
			    OverlayHostAdoptStatus());

		g_panel = RegisterOverlayPanel("Pilgrimage Together", &Draw);
		if (g_panel == 0)
			return false;

		// NO SetOverlayToggleKey(0) ANY MORE, and no F11 in the hotkey table either.
		// The toggle key belongs to the overlay owner now, so it is one setting for
		// the whole process: taking it away here would have disabled F11 for every
		// other plugin as well, which is not this plugin's call to make. The overlay's
		// own default is F11 and that is the same key the hotkey table used to bind,
		// so nothing changes for the player.
		//
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
