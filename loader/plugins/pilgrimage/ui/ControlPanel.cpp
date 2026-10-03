#include "ui/ControlPanel.h"

#include <stdio.h>
#include <windows.h>

#include "ModState.h"
#include "clones/CloneRoster.h"
#include "clones/CloneSpawner.h"
#include "diag/HashProbe.h"
#include "diag/InteractProbe.h"
#include "menu/EscMenuRows.h"
#include "net/LockstepLink.h"
#include "world/Arrival.h"
#include "battle/BattleSync.h"
#include "world/BoosterSync.h"
#include "world/DialogueSync.h"
#include "world/WorldSync.h"
#include "workshop/Log.h"
#include "ffx/HideFlags.h"
#include "ffx/Walkmesh.h"
#include "hooks/VisibilityDetour.h"
#include "net/NetLink.h"
#include "workshop/Settings.h"

namespace pilgrimage
{

	// The game library. Anything unqualified below that looks like FFX
	// knowledge, or like mod plumbing, comes from one of these two.
	using namespace ffx;
	using namespace workshop;

	namespace
	{

		enum ControlId
		{
			IdReadout = 1000,

			IdSpawn,
			IdDespawn,
			IdDespawnAll,
			IdNextClone,
			IdDumpDiff,
			IdCullOverride,
			IdForceVisible,

			IdHost,
			IdJoin,
			IdDisconnect,
			IdUseSteam,
			IdSteamId,
			IdCopyMyId,

			IdHashProbe,
			IdLockstepEnforce,

			IdLookInteract,
			IdFireExamine,
			IdEscMenuRow,

			IdChrIdDown,
			IdChrIdUp,
			IdPartyIndex,

			IdYawMinus90,
			IdYawMinus15,
			IdYawPlus15,
			IdYawPlus90,
			IdYawZero,
			IdWalkDown,
			IdWalkUp,
			IdRunDown,
			IdRunUp,

			IdCheckInput,
			IdCheckFocus,
			IdCheckCameraRelative,
			IdInputSource,
			IdPadDown,
			IdPadUp,
		};

		const UINT_PTR RefreshTimerId = 1;
		const UINT RefreshMs = 100;

		const char* const InputSourceLabels[InputSource::Count] = {
			"source: auto", "source: keys", "source: pad"
		};

		HWND panelWindow = NULL;
		HWND readoutLabel = NULL;
		HWND steamIdEdit = NULL;
		HANDLE uiThread = NULL;
		DWORD uiThreadId = 0;

		// ---------------------------------------------------------------------------
		// Child control construction. A running y cursor keeps the layout readable as a
		// column of rows rather than a wall of coordinates.
		// ---------------------------------------------------------------------------

		int layoutY = 0;

		HWND MakeChild(HWND parent, const char* className, const char* text, DWORD style,
		    int x, int y, int width, int height, int id)
		{
			return CreateWindowExA(0, className, text, WS_CHILD | WS_VISIBLE | style,
			    x, y, width, height, parent, (HMENU)(UINT_PTR)id,
			    (HINSTANCE)GetModuleHandleW(NULL), NULL);
		}

		void AddLabel(HWND parent, const char* text, int x, int width)
		{
			MakeChild(parent, "STATIC", text, 0, x, layoutY + 5, width, 18, 0);
		}

		void AddButton(HWND parent, const char* text, int x, int width, int id)
		{
			MakeChild(parent, "BUTTON", text, BS_PUSHBUTTON, x, layoutY, width, 24, id);
		}

		void AddCheckbox(HWND parent, const char* text, int width, int id, bool checked)
		{
			HWND box = MakeChild(parent, "BUTTON", text, BS_AUTOCHECKBOX, 10, layoutY, width, 20, id);
			SendMessageA(box, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
		}

		void NextRow(int gap)
		{
			layoutY += gap;
		}

		void BuildControls(HWND parent)
		{
			layoutY = 8;
			readoutLabel = MakeChild(parent, "STATIC", "", 0, 10, layoutY, 380, 314, IdReadout);
			NextRow(322);

			AddButton(parent, "Spawn (F9)", 10, 110, IdSpawn);
			AddButton(parent, "Despawn (F10)", 128, 110, IdDespawn);
			AddButton(parent, "Diff (F8)", 246, 110, IdDumpDiff);
			NextRow(30);

			AddButton(parent, "Despawn all", 10, 110, IdDespawnAll);
			AddButton(parent, "Next clone (F4)", 128, 130, IdNextClone);
			NextRow(30);

			AddButton(parent, "no-cull override (F6)", 10, 180, IdCullOverride);
			AddButton(parent, "force visible (F5)", 196, 160, IdForceVisible);
			NextRow(30);

			// Networking. Both starts open a socket, so like everything else here they
			// only raise a flag for the game thread.
			AddButton(parent, "Host (F2)", 10, 100, IdHost);
			AddButton(parent, "Join (F3)", 118, 100, IdJoin);
			AddButton(parent, "Disconnect", 226, 110, IdDisconnect);
			NextRow(28);

			AddCheckbox(parent, "use Steam P2P (off means UDP on loopback)", 330,
			    IdUseSteam, settings.useSteam != 0);
			NextRow(26);

			AddLabel(parent, "host SteamID", 10, 90);
			steamIdEdit = MakeChild(parent, "EDIT", settings.hostSteamId, WS_BORDER,
			    104, layoutY + 2, 232, 20, IdSteamId);
			NextRow(28);

			AddButton(parent, "copy my SteamID to clipboard", 10, 230, IdCopyMyId);
			AddButton(parent, "hash probe", 250, 140, IdHashProbe);
			NextRow(28);

			AddCheckbox(parent, "lockstep gate may refuse a step (shift+F4)", 330,
			    IdLockstepEnforce, LockstepEnforced());
			NextRow(28);

			// The two experiments. Both are one press and each one decides a feature, so
			// they get real buttons rather than being hotkey only.
			AddButton(parent, "what is nearby (F12)", 10, 150, IdLookInteract);
			AddButton(parent, "examine it (shift+F6)", 168, 150, IdFireExamine);
			NextRow(28);

			AddButton(parent, "esc menu row test (shift+F5)", 10, 230, IdEscMenuRow);
			NextRow(34);

			AddLabel(parent, "chr id", 10, 46);
			AddButton(parent, "-", 76, 30, IdChrIdDown);
			AddButton(parent, "+", 110, 30, IdChrIdUp);
			AddLabel(parent, "party idx", 160, 60);
			AddButton(parent, "cycle", 226, 64, IdPartyIndex);
			NextRow(32);

			AddLabel(parent, "yaw bias", 10, 60);
			AddButton(parent, "-90", 76, 46, IdYawMinus90);
			AddButton(parent, "-15", 126, 46, IdYawMinus15);
			AddButton(parent, "+15", 176, 46, IdYawPlus15);
			AddButton(parent, "+90", 226, 46, IdYawPlus90);
			AddButton(parent, "0", 276, 36, IdYawZero);
			NextRow(32);

			AddLabel(parent, "walk", 10, 40);
			AddButton(parent, "-", 76, 30, IdWalkDown);
			AddButton(parent, "+", 110, 30, IdWalkUp);
			AddLabel(parent, "run", 160, 40);
			AddButton(parent, "-", 226, 30, IdRunDown);
			AddButton(parent, "+", 260, 30, IdRunUp);
			NextRow(32);

			AddCheckbox(parent, "arrow keys drive the clone", 240, IdCheckInput,
			    settings.inputEnabled != 0);
			NextRow(24);
			AddCheckbox(parent, "only while the game has focus", 240, IdCheckFocus,
			    settings.requireForeground != 0);
			NextRow(24);
			AddCheckbox(parent, "camera-relative (off = world axes)", 280,
			    IdCheckCameraRelative, settings.cameraRelative != 0);
			NextRow(28);

			AddLabel(parent, "input", 10, 40);
			AddButton(parent, InputSourceLabels[InputSource::Auto], 76, 120, IdInputSource);
			AddLabel(parent, "pad slot", 206, 56);
			AddButton(parent, "-", 266, 30, IdPadDown);
			AddButton(parent, "+", 300, 30, IdPadUp);
		}

		// ---------------------------------------------------------------------------
		// The readout
		// ---------------------------------------------------------------------------

		const char* GaitName()
		{
			if (telemetry.speed <= 0.0f)
				return "idle";
			return (telemetry.speed > 18.0f) ? "run" : "walk";
		}

		void RefreshReadout()
		{
			if (!readoutLabel)
				return;

			char text[1200];
			_snprintf_s(text, sizeof(text), _TRUNCATE,
			    "network   : %s\r\n"
			    "transport : %s   port %ld   (F2 host, F3 join, shift+F3 off)\r\n"
			    "you       : %s %s\r\n"
			    "clones    : %ld of %ld live, driving #%ld, last id %ld   (F4 cycles)\r\n"
			    "next spawn: chr id %d, %s   (F7 cycles, shift+F7 back)\r\n"
			    "status    : %s\r\n"
			    "probe     : %s   (shift+F8 starts it)\r\n"
			    "lockstep  : %s\r\n"
			    "world     : %s\r\n"
			    "arrival   : %s\r\n"
			    "boosters  : %s\r\n"
			    "battle    : %s\r\n"
			    "dialogue  : %s\r\n"
			    "interact  : %s\r\n"
			    "menu row  : %s\r\n"
			    "pool      : %ld live of %ld slots\r\n"
			    "mesh      : %s   sub-meshes: %ld   player has: %ld\r\n"
			    "hide      : 0x%02lX  %s\r\n"
			    "cull      : %s   (F6 toggles the engine override)\r\n"
			    "force vis : %s   cleared on %ld frames   (F5)\r\n"
			    "alpha     : %.3f   cameLen %.0f / clipZ %.0f\r\n"
			    "linked    : %ld of %ld sub-meshes   instance shown byte: %ld\r\n"
			    "walkmesh  : tri %ld   binds %ld   offset %.1f\r\n"
			    "DRAW GATE : %s\r\n"
			    "party idx : %ld\r\n"
			    "flags     : flags1 0x%08lX  flags2 0x%08lX\r\n"
			    "position  : %.1f  %.1f  %.1f\r\n"
			    "speed     : %.2f  (%s)\r\n"
			    "heading   : %.3f rad   facing %.3f rad\r\n"
			    "camera    : %s yaw %.3f rad   input: %s\r\n"
			    "pad       : slot %ld %s, %ld bound   driving with: %s\r\n"
			    "yaw bias  : %+.0f deg\r\n"
			    "walk/run  : %.1f / %.1f\r\n"
			    "frames    : %ld      spawn attempts: %ld",
			    NetworkingSummary(),
			    NetworkingBackendName(), settings.hostPort,
			    telemetry.personaName[0] ? telemetry.personaName : "(Steam not ready)",
			    telemetry.localSteamId,
			    LiveCloneCount(), MaxClones, ActiveEntry() + 1, telemetry.spawnedChrId,
			    SelectedChrId(), ChrIdName(SelectedChrId()),
			    Status(),
			    HashProbeStatus(),
			    LockstepSummary(),
			    WorldSyncStatus(),
			    ArrivalStatus(),
			    BoosterSyncStatus(),
			    BattleSyncStatus(),
			    DialogueSyncStatus(),
			    InteractProbeStatus(),
			    EscMenuRowTestStatus(),
			    telemetry.poolLive, telemetry.poolTotal,
			    telemetry.instanceAttached ? "attached" : "NOT YET",
			    telemetry.subMeshCount, telemetry.playerSubMeshCount,
			    (unsigned long)telemetry.hideFlags, HideFlagNames((BYTE)telemetry.hideFlags),
			    counters.cullOverrideHeld ? "OVERRIDDEN, everything visible" : "normal",
			    settings.forceVisible ? "ON, our clones only" : "off",
			    counters.visibilityForcedFrames,
			    telemetry.shadeAlpha, telemetry.cameLength, telemetry.clipZ,
			    telemetry.subMeshesLinked, telemetry.subMeshCount, telemetry.instanceShownByte,
			    telemetry.walkmeshTriangle, WalkmeshBindCount(), settings.spawnOffset,
			    telemetry.drawGate,
			    telemetry.partyIndex,
			    (unsigned long)telemetry.flags1, (unsigned long)telemetry.flags2,
			    telemetry.posX, telemetry.posY, telemetry.posZ,
			    telemetry.speed, GaitName(),
			    telemetry.moveDirection, telemetry.facing,
			    telemetry.cameraYawValid ? "ok " : "N/A", telemetry.cameraYaw,
			    settings.cameraRelative ? "camera-relative" : "world",
			    settings.padSlot, telemetry.padBound ? "connected" : "absent",
			    telemetry.padsPresent, telemetry.usingPad ? "gamepad" : "keyboard",
			    settings.yawBiasDegrees,
			    settings.walkSpeed, settings.runSpeed,
			    counters.frames, counters.spawnAttempts);

			SetWindowTextA(readoutLabel, text);
		}

		// ---------------------------------------------------------------------------
		// Commands. Every one of these either raises a request for the game thread or
		// writes a tuning scalar. None of them calls into FFX.
		// ---------------------------------------------------------------------------

		// Puts the local SteamID on the clipboard, so a host can paste it to friends
		// instead of hunting for their profile page. Reads it from telemetry rather than
		// from Steam, because the game thread owns every call into Steam.
		void CopyMySteamIdToClipboard(HWND window)
		{
			if (!telemetry.localSteamId[0])
			{
				SetStatus("Steam has not signed in yet, so there is no id to copy");
				return;
			}

			const size_t length = strlen(telemetry.localSteamId);
			HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, length + 1);
			if (!block)
				return;

			char* text = (char*)GlobalLock(block);
			if (!text)
			{
				GlobalFree(block);
				return;
			}
			memcpy(text, telemetry.localSteamId, length + 1);
			GlobalUnlock(block);

			if (!OpenClipboard(window))
			{
				// Another process can hold the clipboard, and failing to get it is not
				// worth more than a status line. Free the block or it leaks.
				GlobalFree(block);
				SetStatus("could not open the clipboard, something else is holding it");
				return;
			}
			EmptyClipboard();
			if (SetClipboardData(CF_TEXT, block) == NULL)
				GlobalFree(block);
			CloseClipboard();

			SetStatus("copied %s to the clipboard", telemetry.localSteamId);
		}

		// Copies the SteamID box into the setting. Called from the UI thread only, since
		// it reads a window. Writing the setting through the registry rather than
		// straight into the array is what makes the value get saved to disk.
		void CaptureSteamId()
		{
			if (!steamIdEdit)
				return;

			char text[24] = { 0 };
			GetWindowTextA(steamIdEdit, text, sizeof(text));

			const Setting* setting = FindSetting("net.host_steam_id");
			if (setting)
				WriteSettingText(*setting, text);
			else
				strncpy_s(settings.hostSteamId, sizeof(settings.hostSteamId), text, _TRUNCATE);
		}

		void CyclePartyIndex()
		{
			// 255 is "not a party member", which is what a clone gets. The others are here
			// so the party index can be ruled in or out as something that gates behaviour,
			// without a rebuild.
			static const LONG choices[] = { 255, 0, 1, 2, 7 };
			const int count = (int)(sizeof(choices) / sizeof(choices[0]));

			LONG next = choices[0];
			for (int i = 0; i < count; ++i)
				if (choices[i] == settings.spawnPartyIndex)
				{
					next = choices[(i + 1) % count];
					break;
				}
			InterlockedExchange(&settings.spawnPartyIndex, next);
		}

		void CycleInputSource(HWND parent)
		{
			const LONG next = (settings.inputSource + 1) % InputSource::Count;
			InterlockedExchange(&settings.inputSource, next);
			SetWindowTextA(GetDlgItem(parent, IdInputSource), InputSourceLabels[next]);
		}

		void SyncCheckbox(HWND parent, int id, volatile LONG* target)
		{
			const bool checked =
			    SendMessageA(GetDlgItem(parent, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
			InterlockedExchange(target, checked ? 1 : 0);
		}

		void HandleCommand(HWND parent, int id)
		{
			switch (id)
			{
			case IdSpawn:
				InterlockedExchange(&requests.spawn, 1);
				break;
			case IdDespawn:
				InterlockedExchange(&requests.despawn, 1);
				break;
			case IdDespawnAll:
				InterlockedExchange(&requests.despawnAll, 1);
				break;
			case IdNextClone:
				InterlockedExchange(&requests.cycleClone, 1);
				break;
			case IdDumpDiff:
				InterlockedExchange(&requests.dumpDiff, 1);
				break;

			case IdHost:
				CaptureSteamId();
				InterlockedExchange(&requests.startHosting, 1);
				break;

			case IdJoin:
				// Read the box here, on the UI thread that owns the control, and leave
				// the setting for the game thread to pick up. The game thread must never
				// touch a window handle.
				CaptureSteamId();
				InterlockedExchange(&requests.startJoining, 1);
				break;

			case IdDisconnect:
				InterlockedExchange(&requests.stopNetworking, 1);
				break;

			case IdUseSteam:
				InterlockedExchange(&settings.useSteam, !settings.useSteam);
				SetStatus("transport: %s", NetworkingBackendName());
				break;

			case IdCopyMyId:
				CopyMySteamIdToClipboard(parent);
				break;

			case IdHashProbe:
				InterlockedExchange(&requests.startHashProbe, 1);
				SetStatus("hash probe queued, stand still");
				break;

			case IdLockstepEnforce:
				InterlockedExchange(&requests.toggleLockstepEnforce, 1);
				break;

			case IdLookInteract:
				InterlockedExchange(&requests.lookAtInteractables, 1);
				break;

			case IdFireExamine:
				InterlockedExchange(&requests.fireNearestExamine, 1);
				break;

			case IdEscMenuRow:
				InterlockedExchange(&requests.armEscMenuRow, 1);
				SetStatus("esc menu row armed, now press Escape");
				break;

			case IdCullOverride:
				InterlockedExchange(&settings.disableCull, !settings.disableCull);
				break;
			case IdForceVisible:
				if (VisibilityDetourInstalled())
					InterlockedExchange(&settings.forceVisible, !settings.forceVisible);
				break;

			case IdChrIdDown:
				InterlockedExchange(&requests.cycleSpawnIdBack, 1);
				break;
			case IdChrIdUp:
				InterlockedExchange(&requests.cycleSpawnIdForward, 1);
				break;
			case IdPartyIndex:
				CyclePartyIndex();
				break;

			case IdYawMinus90:
				settings.yawBiasDegrees -= 90.0f;
				break;
			case IdYawMinus15:
				settings.yawBiasDegrees -= 15.0f;
				break;
			case IdYawPlus15:
				settings.yawBiasDegrees += 15.0f;
				break;
			case IdYawPlus90:
				settings.yawBiasDegrees += 90.0f;
				break;
			case IdYawZero:
				settings.yawBiasDegrees = 0.0f;
				break;

			case IdWalkDown:
				if (settings.walkSpeed > 1.0f)
					settings.walkSpeed -= 1.0f;
				break;
			case IdWalkUp:
				if (settings.walkSpeed < 60.0f)
					settings.walkSpeed += 1.0f;
				break;
			case IdRunDown:
				if (settings.runSpeed > 1.0f)
					settings.runSpeed -= 2.0f;
				break;
			case IdRunUp:
				if (settings.runSpeed < 90.0f)
					settings.runSpeed += 2.0f;
				break;

			case IdCheckInput:
				SyncCheckbox(parent, IdCheckInput, &settings.inputEnabled);
				break;
			case IdCheckFocus:
				SyncCheckbox(parent, IdCheckFocus, &settings.requireForeground);
				break;
			case IdCheckCameraRelative:
				SyncCheckbox(parent, IdCheckCameraRelative, &settings.cameraRelative);
				break;

			case IdInputSource:
				CycleInputSource(parent);
				break;
			case IdPadDown:
				if (settings.padSlot > 0)
					InterlockedExchange(&settings.padSlot, settings.padSlot - 1);
				break;
			case IdPadUp:
				if (settings.padSlot < 17)
					InterlockedExchange(&settings.padSlot, settings.padSlot + 1);
				break;
			default:
				break;
			}

			// Keep the yaw bias in a readable range so the readout stays legible.
			if (id >= IdYawMinus90 && id <= IdYawZero)
			{
				while (settings.yawBiasDegrees > 180.0f)
					settings.yawBiasDegrees -= 360.0f;
				while (settings.yawBiasDegrees < -180.0f)
					settings.yawBiasDegrees += 360.0f;
			}
		}

		// ---------------------------------------------------------------------------
		// The window
		// ---------------------------------------------------------------------------

		LRESULT CALLBACK PanelWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
		{
			switch (message)
			{
			case WM_CREATE:
				BuildControls(window);
				SetTimer(window, RefreshTimerId, RefreshMs, NULL);
				return 0;

			case WM_TIMER:
				if (wparam == RefreshTimerId)
				{
					RefreshReadout();
					// Cheap when nothing has changed, since it compares a revision
					// counter first. Saving here rather than at shutdown means a tweak
					// survives even if the game is killed rather than closed.
					SaveModSettingsIfChanged();
				}
				return 0;

			case WM_COMMAND:
				HandleCommand(window, LOWORD(wparam));
				RefreshReadout();
				return 0;

			case WM_CLOSE:
				// Hide rather than destroy, so F11 can bring it back.
				ShowWindow(window, SW_HIDE);
				return 0;

			case WM_DESTROY:
				KillTimer(window, RefreshTimerId);
				panelWindow = NULL;
				readoutLabel = NULL;
				PostQuitMessage(0);
				return 0;
			}
			return DefWindowProcA(window, message, wparam, lparam);
		}

		DWORD WINAPI PanelThreadProc(LPVOID)
		{
			WNDCLASSEXA windowClass;
			ZeroMemory(&windowClass, sizeof(windowClass));
			windowClass.cbSize = sizeof(windowClass);
			windowClass.lpfnWndProc = PanelWndProc;
			windowClass.hInstance = (HINSTANCE)GetModuleHandleW(NULL);
			windowClass.hCursor = LoadCursorA(NULL, IDC_ARROW);
			windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
			windowClass.lpszClassName = "AlBhedWorkshopCloneControl";
			if (!RegisterClassExA(&windowClass))
			{
				Log("UI: RegisterClassExA failed, GetLastError=%lu", GetLastError());
				return 1;
			}

			panelWindow = CreateWindowExA(
			    WS_EX_TOOLWINDOW | WS_EX_TOPMOST, windowClass.lpszClassName,
			    "AlBhedWorkshop - clone control",
			    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
			    40, 40, 420, 976, NULL, NULL, windowClass.hInstance, NULL);
			if (!panelWindow)
			{
				Log("UI: CreateWindowExA failed, GetLastError=%lu", GetLastError());
				return 1;
			}

			// The shell's default UI font, rather than the ancient bitmap default.
			HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
			for (HWND child = GetWindow(panelWindow, GW_CHILD); child;
			    child = GetWindow(child, GW_HWNDNEXT))
				SendMessageA(child, WM_SETFONT, (WPARAM)font, TRUE);

			ShowWindow(panelWindow, SW_SHOW);
			Log("UI: control window up on thread %lu", GetCurrentThreadId());

			MSG message;
			while (GetMessageA(&message, NULL, 0, 0) > 0)
			{
				TranslateMessage(&message);
				DispatchMessageA(&message);
			}
			Log("UI: message loop exited");
			return 0;
		}

	} // namespace

	bool StartControlPanel()
	{
		uiThread = CreateThread(NULL, 0, PanelThreadProc, NULL, 0, &uiThreadId);
		if (!uiThread)
		{
			Log("CreateThread for the UI failed, GetLastError=%lu. Hotkeys still work.",
			    GetLastError());
			return false;
		}
		return true;
	}

	void ToggleControlPanel()
	{
		if (!panelWindow)
			return;
		if (IsWindowVisible(panelWindow))
		{
			ShowWindow(panelWindow, SW_HIDE);
		}
		else
		{
			ShowWindow(panelWindow, SW_SHOW);
			SetWindowPos(panelWindow, HWND_TOPMOST, 0, 0, 0, 0,
			    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}

} // namespace pilgrimage
