#include "ffx/Hub.h"

#include <stdio.h>

#include "ffx/AnimateHook.h"
#include "ffx/Addresses.h"
#include "ffx/Battle.h"
#include "ffx/Cutscene.h"
#include "ffx/WorldState.h"
#include "workshop/Detour.h"
#include "workshop/Events.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using namespace workshop;

	namespace
	{

		const int MaxStepGates = 8;

		StepGateFn g_gates[MaxStepGates];
		int g_gateCount = 0;

		bool g_running = false;
		bool g_animateHooked = false;
		bool g_stepHooked = false;
		bool g_exitHooked = false;
		bool g_subStepHooked = false;

		// Edge state for the polled events. -1 and false are "not yet known", so the
		// first frame establishes a baseline rather than raising a change.
		bool g_haveBaseline = false;
		int g_lastMapId = -1;
		bool g_lastBattle = false;
		bool g_lastPaused = false;
		bool g_lastEscMenu = false;
		bool g_lastFmv = false;

		unsigned long g_frames = 0;
		unsigned long g_steps = 0;
		unsigned long g_subSteps = 0;
		unsigned long g_held = 0;
		char g_status[200] = "not started";

		DWORD CurrentStep()
		{
			DWORD step = 0;
			return MainStepCounter(&step) ? step : 0;
		}

		int LiveMapId()
		{
			WorldLocation where;
			if (!ReadWorldLocation(&where) || !where.valid)
				return -1;
			return where.mapId;
		}

		void PollEdges(DWORD step)
		{
			const int mapId = LiveMapId();
			const bool battle = BattleRunning();
			const bool paused = IsSimulationPaused();
			const bool escMenu = EscMenuIsOpen();
			const bool fmv = FmvInProgress();

			if (!g_haveBaseline)
			{
				g_lastMapId = mapId;
				g_lastBattle = battle;
				g_lastPaused = paused;
				g_lastEscMenu = escMenu;
				g_lastFmv = fmv;
				g_haveBaseline = true;
				return;
			}

			// A map id of -1 means the read failed rather than that the map changed, so
			// it is not an edge either way.
			if (mapId >= 0 && mapId != g_lastMapId)
			{
				g_lastMapId = mapId;
				Publish(EventMapChanged, step, (uint32_t)mapId);
			}

			if (battle != g_lastBattle)
			{
				g_lastBattle = battle;
				Publish(battle ? EventBattleBegin : EventBattleEnd, step);
			}

			if (paused != g_lastPaused)
			{
				g_lastPaused = paused;
				Publish(paused ? EventPaused : EventResumed, step);
			}

			if (escMenu != g_lastEscMenu)
			{
				g_lastEscMenu = escMenu;
				Publish(escMenu ? EventMenuOpened : EventMenuClosed, step, 1);
			}

			// FmvInProgress, not FmvPlaying, because the decoder can still be draining
			// after the manager has dropped its playing byte.
			if (fmv != g_lastFmv)
			{
				g_lastFmv = fmv;
				Publish(fmv ? EventFmvBegin : EventFmvEnd, step);
			}
		}

		int __fastcall HubAnimate(void* self, void* unusedEdx)
		{
			// The engine runs first, so a subscriber is looking at a frame the engine has
			// already stepped, and a fault in a subscriber cannot stop the game from
			// having run. The result is returned unchanged, which is not optional: Phyre
			// treats a non-zero return as fatal.
			const int result = OriginalAnimate()(self, unusedEdx);

			++g_frames;
			const DWORD step = CurrentStep();

			// self is the FFXApplication, which is where the stall byte and the frame
			// clocks live, so it rides along rather than being re-derived.
			Event frame;
			frame.id = EventFrame;
			frame.step = step;
			frame.value = 0;
			frame.pointer = self;
			Publish(frame);

			PollEdges(step);
			return result;
		}

		// Same convention as animate, and the same vtable, slot 6.
		typedef int(__fastcall* ExitAppFn)(void* self, void* unusedEdx);
		ExitAppFn g_originalExitApp = NULL;

		int __fastcall HubExitApplication(void* self, void* unusedEdx)
		{
			// BEFORE the original, unlike animate. Once exitApplication has run, graphics
			// are finalised and the sync data is destroyed, so a subscriber that wanted to
			// read engine state on the way out would be reading freed memory.
			Event bye;
			bye.id = EventShutdown;
			bye.step = CurrentStep();
			bye.value = 0;
			bye.pointer = self;
			Publish(bye);

			if (!g_originalExitApp)
				return 0;
			return g_originalExitApp(self, unusedEdx);
		}

		bool HookExitApplication()
		{
			void* slot = ModuleAddress(Rva::ExitApplicationVtableSlot);

			// The slot has to hold what we expect before we touch it. A mismatch means a
			// different build or another mod got there first, and either way overwriting
			// it blind would lose their hook.
			if (!Readable(slot, 4))
				return false;
			const DWORD held = *(const DWORD*)slot;
			const DWORD expect = (DWORD)(UINT_PTR)ModuleAddress(Rva::ExitApplicationExpected);
			if (held != expect)
			{
				Log("hub: the exitApplication slot holds 0x%08X, not 0x%08X, so it is left "
				    "alone and EventShutdown will not be raised",
				    held, expect);
				return false;
			}

			DWORD previous = 0;
			if (!WriteProtectedDword(slot, (DWORD)(UINT_PTR)&HubExitApplication, &previous))
				return false;
			g_originalExitApp = (ExitAppFn)(UINT_PTR)previous;
			return true;
		}

		bool __cdecl HubGate()
		{
			// Every gate is asked even once one has held, because a gate is also where a
			// plugin does its per-step bookkeeping and skipping it would starve the one
			// that is waiting.
			bool allow = true;
			for (int i = 0; i < g_gateCount; ++i)
				if (g_gates[i] && !g_gates[i]())
					allow = false;
			if (!allow)
				++g_held;
			return allow;
		}

		void __cdecl HubAfterStep(float)
		{
			++g_steps;
			Publish(EventStep, CurrentStep());
		}

		// FFX_Player__stepControl, the first call in FFX_MainStep's inner sub-step loop.
		// It returns a CHR* and takes nothing.
		typedef void*(__cdecl* StepControlFn)(void);
		CallSitePatch g_subStepSite = { 0, false };

		void* __cdecl HubStepControl()
		{
			// BEFORE the original, because the whole point of a sub-step event is to let a
			// plugin read or replace the latched pad before the movement driver consumes
			// it. value is the running total, which is also the ATEL script clock.
			++g_subSteps;
			Publish(EventSubStep, CurrentStep(), (uint32_t)g_subSteps);

			StepControlFn original = (StepControlFn)ModuleAddress(Rva::PlayerStepControl);
			if (!Readable((void*)original, 1))
				return NULL;
			return original();
		}

	} // namespace

	bool StartHub()
	{
		if (g_running)
			return true;

		g_animateHooked = HookAnimate(&HubAnimate);
		if (!g_animateHooked)
		{
			Log("hub: the animate slot would not take a hook, so there is no frame event");
			return false;
		}

		g_stepHooked = HookMainStep(&HubGate, &HubAfterStep);
		if (!g_stepHooked)
			Log("hub: no step hook, so EventStep will not be raised and no gate can hold "
			    "the simulation. The frame event still works.");

		g_exitHooked = HookExitApplication();

		// One call site in the whole binary, so this costs nothing anybody else uses.
		g_subStepHooked = PatchCallSite(g_subStepSite, Rva::PlayerStepControlCallSite,
		    Rva::PlayerStepControl, (void*)&HubStepControl, "sub step");

		g_running = true;
		Log("hub: running, frame%s step events, sub step %s, shutdown %s",
		    g_stepHooked ? " and" : " but no",
		    g_subStepHooked ? "armed" : "unavailable",
		    g_exitHooked ? "armed" : "unavailable");
		return true;
	}

	bool HubRunning()
	{
		return g_running;
	}

	bool AddStepGate(StepGateFn gate)
	{
		if (!gate)
			return false;
		for (int i = 0; i < g_gateCount; ++i)
			if (g_gates[i] == gate)
				return true;
		if (g_gateCount >= MaxStepGates)
		{
			Log("hub: no room for another step gate, %d is the limit", MaxStepGates);
			return false;
		}
		g_gates[g_gateCount++] = gate;
		return true;
	}

	void RemoveStepGate(StepGateFn gate)
	{
		for (int i = 0; i < g_gateCount; ++i)
			if (g_gates[i] == gate)
			{
				for (int j = i; j + 1 < g_gateCount; ++j)
					g_gates[j] = g_gates[j + 1];
				g_gates[--g_gateCount] = nullptr;
				return;
			}
	}

	int StepGateCount()
	{
		return g_gateCount;
	}

	const char* HubStatus()
	{
		if (!g_running)
			return "not started";
		_snprintf(g_status, sizeof(g_status) - 1,
		    "%lu frames, %lu steps, %lu sub, %lu held, %d gate%s, map %d%s%s",
		    g_frames, g_steps, g_subSteps, g_held, g_gateCount, g_gateCount == 1 ? "" : "s",
		    g_lastMapId, g_lastBattle ? ", in battle" : "",
		    g_lastPaused ? ", paused" : "");
		g_status[sizeof(g_status) - 1] = 0;
		return g_status;
	}

	unsigned long HubSubStepCount()
	{
		return g_subSteps;
	}

	void LogHub()
	{
		Log("hub     : %s", HubStatus());
		LogEvents();
	}

} // namespace ffx
