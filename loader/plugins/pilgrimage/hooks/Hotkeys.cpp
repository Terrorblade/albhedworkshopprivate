#include "hooks/Hotkeys.h"

#include <windows.h>

#include "ModState.h"
#include "workshop/Log.h"
#include "hooks/VisibilityDetour.h"
#include "ui/ControlPanel.h"

namespace pilgrimage
{

	// The mod plumbing: logging and the status line.
	using namespace workshop;

	namespace
	{

		// Edge detection, so a held key fires once. Tracked here rather than with
		// GetAsyncKeyState's low bit, because that bit is cleared by whoever reads it
		// first and the game is polling too.
		struct KeyEdge
		{
			int virtualKey;
			bool wasDown;
		};

		KeyEdge keys[] = {
			{ VK_F2, false },
			{ VK_F3, false },
			{ VK_F4, false },
			{ VK_F5, false },
			{ VK_F6, false },
			{ VK_F7, false },
			{ VK_F8, false },
			{ VK_F9, false },
			{ VK_F10, false },
			{ VK_F11, false },
			{ VK_F12, false },
		};

		enum KeyIndex
		{
			KeyF2,
			KeyF3,
			KeyF4,
			KeyF5,
			KeyF6,
			KeyF7,
			KeyF8,
			KeyF9,
			KeyF10,
			KeyF11,
			KeyF12,
			KeyCount
		};

		bool ShiftHeld()
		{
			return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
		}

		void ToggleForceVisible()
		{
			if (!VisibilityDetourInstalled())
			{
				SetStatus("force-visible needs the detour, which did not install");
				return;
			}
			InterlockedExchange(&settings.forceVisible, !settings.forceVisible);
			SetStatus("force visible %s", settings.forceVisible ? "ON" : "off");
			Log("force visible %s, our clones only. Forced frames so far: %ld",
			    settings.forceVisible ? "ON" : "off", counters.visibilityForcedFrames);
		}

	} // namespace

	void PollHotkeys()
	{
		bool pressed[KeyCount];
		for (int i = 0; i < KeyCount; ++i)
		{
			const bool down = (GetAsyncKeyState(keys[i].virtualKey) & 0x8000) != 0;
			pressed[i] = down && !keys[i].wasDown;
			keys[i].wasDown = down;
		}

		// F2 hosts, F3 joins, shift+F3 disconnects. Both open a socket, so they go
		// through the request flags rather than being done here.
		if (pressed[KeyF2])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.toggleFixedTimeStep, 1);
			else
				InterlockedExchange(&requests.startHosting, 1);
		}
		if (pressed[KeyF3])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.stopNetworking, 1);
			else
				InterlockedExchange(&requests.startJoining, 1);
		}

		// F4 hands the arrow keys to the next clone. Safe to read without checking
		// alt, because alt+F4 would have closed the window before we got here.
		if (pressed[KeyF4])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.toggleLockstepEnforce, 1);
			else
				InterlockedExchange(&requests.cycleClone, 1);
		}
		if (pressed[KeyF5])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.armEscMenuRow, 1);
			else
				ToggleForceVisible();
		}
		if (pressed[KeyF6])
		{
			if (ShiftHeld())
			{
				InterlockedExchange(&requests.fireNearestExamine, 1);
			}
			else
			{
				InterlockedExchange(&settings.disableCull, !settings.disableCull);
				SetStatus("cull override %s", settings.disableCull ? "ON" : "off");
			}
		}
		if (pressed[KeyF7])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.cycleSpawnIdBack, 1);
			else
				InterlockedExchange(&requests.cycleSpawnIdForward, 1);
		}
		if (pressed[KeyF12])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.toggleTriggerFollow, 1);
			else
				InterlockedExchange(&requests.lookAtInteractables, 1);
		}
		if (pressed[KeyF8])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.startHashProbe, 1);
			else
				InterlockedExchange(&requests.dumpDiff, 1);
		}
		if (pressed[KeyF9])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.toggleTriggerArmed, 1);
			else
				InterlockedExchange(&requests.spawn, 1);
		}
		if (pressed[KeyF10])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.despawnAll, 1);
			else
				InterlockedExchange(&requests.despawn, 1);
		}
		if (pressed[KeyF11])
		{
			if (ShiftHeld())
				InterlockedExchange(&requests.logTriggerPass, 1);
			else
				ToggleControlPanel();
		}
	}

	const char* HotkeySummary()
	{
		return "F9 spawn, F10 despawn, shift+F10 despawn all, F4 next clone, "
		       "arrow keys to move, shift to run, F11 control window, "
		       "F7 cycles the spawn id, F8 dumps a field diff, "
		       "shift+F8 runs the save block stability probe, "
		       "F6 cull override, F5 force visible, "
		       "F2 host, F3 join, shift+F3 disconnect, "
		       "shift+F2 toggles the engine fixed timestep, "
		       "shift+F4 lets the lockstep gate refuse a step, "
		       "F12 lists what is interactable nearby, shift+F6 fires an examine "
		       "on the nearest one, shift+F5 arms the esc menu row test";
	}

} // namespace pilgrimage
