#include "ffx/MainLoop.h"

#include <windows.h>

// Addresses.h for Rva::Application, which Character.h owns, and the area file
// directly so this compiles whether or not Addresses.h has been wired up yet.
#include "ffx/Addresses.h"
#include "ffx/addresses/MainLoop.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{
	namespace
	{

		using workshop::Log;
		using workshop::ModuleAddress;
		using workshop::Readable;

		// ---------------------------------------------------------------------------
		// Typed access to one global, with the Readable check that keeps a bad read off
		// the frame path. Every accessor in this file goes through one of these.
		// ---------------------------------------------------------------------------
		template <typename T>
		T* At(DWORD rva)
		{
			T* p = (T*)ModuleAddress(rva);
			return Readable(p, sizeof(T)) ? p : NULL;
		}

		template <typename T>
		bool ReadGlobal(DWORD rva, T* out)
		{
			const T* p = At<T>(rva);
			if (!p || !out)
				return false;
			*out = *p;
			return true;
		}

		// A field inside the application object, where the object pointer itself has to
		// be checked before the field can be.
		template <typename T>
		T* Field(void* object, DWORD offset)
		{
			if (!object)
				return NULL;
			T* p = (T*)((BYTE*)object + offset);
			return Readable(p, sizeof(T)) ? p : NULL;
		}

		// ---------------------------------------------------------------------------
		// Calling into the game. Two of the answers here are only available by calling
		// a game function, so the prologue is checked once and the verdict cached.
		// Anything that fails the check degrades to a documented fallback rather than
		// faulting.
		// ---------------------------------------------------------------------------
		typedef int(__cdecl* IntNoArgsFn)(void);

		IntNoArgsFn ResolveCallable(DWORD rva, int* cachedVerdict)
		{
			if (*cachedVerdict == 0)
			{
				*cachedVerdict = workshop::LooksLikeFunctionStart(rva) ? 1 : -1;
				if (*cachedVerdict < 0)
				{
					Log("main loop: RVA 0x%08X does not look callable, falling back", rva);
				}
			}
			if (*cachedVerdict < 0)
				return NULL;
			// The UINT_PTR hop is the project's idiom for a data-to-function cast.
			return (IntNoArgsFn)(UINT_PTR)ModuleAddress(rva);
		}

		int fadeGateVerdict = 0;
		int subStepGateVerdict = 0;

		// ---------------------------------------------------------------------------
		// Our own step counter and the gate. Written by the detour on the frame thread,
		// read from anywhere, so both go through the interlocked helpers.
		// ---------------------------------------------------------------------------
		volatile LONG localStepCount = 0;
		volatile LONG stalledStepCount = 0;

		StepGateFn stepGate = NULL;
		StepObserverFn stepObserver = NULL;

		// ---------------------------------------------------------------------------
		// The detour on FFX_MainStep.
		//
		// Signature: int __cdecl (float dt). The Hex-Rays prototype claims __usercall
		// with ebx and esi arguments, which is noise. The single call site in
		// FFX_MainStepLoop pushes one float and does add esp, 4, and neither ebx nor esi
		// is read before being written inside the function, which was checked
		// instruction by instruction.
		// ---------------------------------------------------------------------------
		DETOUR_DECLARE(MainStep, int, (float dt));

		// Verified in IDA at 0x820AE0. Nine bytes, three instructions, no branch and no
		// absolute address among them, so they relocate to the trampoline unchanged and
		// the expected-bytes check is stable across ASLR.
		DETOUR_PROLOGUE(MainStep) = {
			0x55,                              // push ebp
			0x8B, 0xEC,                        // mov  ebp, esp
			0x81, 0xEC, 0xA4, 0x00, 0x00, 0x00 // sub  esp, 0A4h
		};

		int __cdecl MainStepHook(float dt)
		{
			if (stepGate && !stepGate())
			{
				// Refusing a step has to clear the pending count as well, or the
				// do/while in FFX_MainStepLoop spins on a value nothing decrements.
				int* pending = At<int>(Rva::PendingSteps);
				if (pending)
					*pending = 0;
				InterlockedIncrement(&stalledStepCount);
				return 0; // FFX_MainStepLoop discards the return value
			}

			const int result = DETOUR_ORIGINAL(MainStep)(dt);
			InterlockedIncrement(&localStepCount);
			if (stepObserver)
				stepObserver(dt);
			return result;
		}

		// ---------------------------------------------------------------------------
		// The hold byte, resolved and checked.
		// ---------------------------------------------------------------------------
		BYTE* HoldByte(void* application)
		{
			return Field<BYTE>(application, FfxApp::HoldSimulation);
		}

		// The frame rate divider the game was last asked for, so UncapFrameRate can put
		// it back instead of guessing.
		int dividerBeforeUncap = 0;

	} // namespace

	// ---------------------------------------------------------------------------
	// The application pointer
	// ---------------------------------------------------------------------------

	BYTE* Application()
	{
		BYTE** slot = At<BYTE*>(Rva::Application);
		if (!slot)
			return NULL;
		BYTE* app = *slot;
		// The largest offset anything here touches, so one check covers them all.
		if (!Readable(app, PApp::FrameCallback + sizeof(void*)))
			return NULL;
		return app;
	}

	// ---------------------------------------------------------------------------
	// Clocks
	// ---------------------------------------------------------------------------

	bool PhyreFrameTickCount(DWORD* out)
	{
		return ReadGlobal<DWORD>(Rva::PhyreFrameTickCount, out);
	}
	bool MainStepCounter(DWORD* out)
	{
		return ReadGlobal<DWORD>(Rva::MainStepCounter, out);
	}
	bool GameClock60Hz(DWORD* out)
	{
		return ReadGlobal<DWORD>(Rva::GameClock60Hz, out);
	}
	bool StepScale(float* out)
	{
		return ReadGlobal<float>(Rva::StepScaleFloat, out);
	}

	bool FrameDelta(float* out)
	{
		const float* p = Field<float>(Application(), PApp::FrameDelta);
		if (!p || !out)
			return false;
		*out = *p;
		return true;
	}

	DWORD StepCount()
	{
		return (DWORD)localStepCount;
	}
	DWORD StalledStepCount()
	{
		return (DWORD)stalledStepCount;
	}

	bool ReadFrameClocks(FrameClocks* out)
	{
		if (!out)
			return false;

		BYTE* app = Application();
		if (!app)
			return false;

		FrameClocks c;
		c.phyreFrameTick = 0;
		c.phyreAppFrame = 0;
		c.mainStepCounter = 0;
		c.gameClock60Hz = 0;
		c.localStepCount = (DWORD)localStepCount;
		c.frameDelta = 0.0f;
		c.fixedTimeStep = 0.0f;
		c.measuredFps = 0.0f;
		c.stepScale = 0.0f;
		c.frameRateDivider = 0;
		c.pendingSteps = 0;
		c.pacingMode = 0;
		c.isCatchUpStep = false;

		PhyreFrameTickCount(&c.phyreFrameTick);
		MainStepCounter(&c.mainStepCounter);
		GameClock60Hz(&c.gameClock60Hz);
		StepScale(&c.stepScale);
		ReadGlobal<int>(Rva::PendingSteps, &c.pendingSteps);
		ReadGlobal<int>(Rva::PacingMode, &c.pacingMode);
		ReadGlobal<int>(Rva::FrameRateDivider, &c.frameRateDivider);

		int catchUp = 0;
		if (ReadGlobal<int>(Rva::IsCatchUpStep, &catchUp))
			c.isCatchUpStep = (catchUp != 0);

		const DWORD* frameNumber = Field<DWORD>(app, PApp::FrameNumber);
		const float* delta = Field<float>(app, PApp::FrameDelta);
		const float* fixedStep = Field<float>(app, PApp::FixedTimeStep);
		const float* fps = Field<float>(app, PApp::MeasuredFps);
		if (frameNumber)
			c.phyreAppFrame = *frameNumber;
		if (delta)
			c.frameDelta = *delta;
		if (fixedStep)
			c.fixedTimeStep = *fixedStep;
		if (fps)
			c.measuredFps = *fps;

		*out = c;
		return true;
	}

	// ---------------------------------------------------------------------------
	// Pause
	// ---------------------------------------------------------------------------

	bool EscMenuIsOpen()
	{
		// The game's own test, reimplemented rather than called, so this stays a
		// pure read and works from any thread.
		BYTE** slot = At<BYTE*>(Rva::EscMenu);
		if (!slot)
			return false;
		BYTE* menu = *slot;
		if (!Readable(menu, EscMenuLayout::OpenIndex + sizeof(int)))
			return false;
		if (*(menu + EscMenuLayout::OpenFlag) != 0)
			return true;
		return *(const int*)(menu + EscMenuLayout::OpenIndex) >= 0;
	}

	bool ReadPauseState(PauseState* out, bool includeFadeGate)
	{
		if (!out)
			return false;

		PauseState s;
		s.escMenuOpen = EscMenuIsOpen();
		s.holdByteSet = false;
		s.menuSysRunning = false;
		s.menuSysRunningAlt = false;
		s.menuOpenPending = false;
		s.saveUiBusy = false;
		s.fadeBlocking = false;
		s.simulationWillStep = false;
		s.gameplayWillStep = false;

		int value = 0;
		if (ReadGlobal<int>(Rva::MenuSysRunning, &value))
			s.menuSysRunning = (value != 0);
		if (ReadGlobal<int>(Rva::MenuSysRunningAlt, &value))
			s.menuSysRunningAlt = (value != 0);
		if (ReadGlobal<int>(Rva::MenuOpenPending, &value))
			s.menuOpenPending = (value != 0);
		if (ReadGlobal<int>(Rva::SaveUiState, &value))
			s.saveUiBusy = (value != 0);

		const BYTE* hold = HoldByte(Application());
		if (hold)
			s.holdByteSet = (*hold != 0);

		if (includeFadeGate)
		{
			IntNoArgsFn fadeGate = ResolveCallable(Rva::MainStepIsFadeBlocking, &fadeGateVerdict);
			if (fadeGate)
				s.fadeBlocking = (fadeGate() != 0);
		}

		// The hold byte is what animate actually branches on, and animate sets it
		// from the esc menu, so either one means FFX_MainStep will not be called.
		s.simulationWillStep = !s.holdByteSet && !s.escMenuOpen;

		// The gameplay block inside FFX_MainStep has four more gates on it.
		s.gameplayWillStep = s.simulationWillStep && !s.menuSysRunning && !s.menuSysRunningAlt && !s.menuOpenPending && !s.saveUiBusy && !s.fadeBlocking;

		*out = s;
		return true;
	}

	bool IsSimulationPaused()
	{
		PauseState s;
		if (!ReadPauseState(&s, false))
			return false;
		return !s.simulationWillStep;
	}

	// ---------------------------------------------------------------------------
	// The simulation hold
	// ---------------------------------------------------------------------------

	bool HoldSimulation(void* application)
	{
		BYTE* hold = HoldByte(application);
		if (!hold)
			return false;
		*hold = 1;
		return true;
	}

	bool ReleaseSimulationHold(void* application)
	{
		BYTE* hold = HoldByte(application);
		if (!hold)
			return false;
		// Exactly what animate and endFrame do, so a genuine pause keeps the byte.
		*hold = EscMenuIsOpen() ? (BYTE)1 : (BYTE)0;
		return true;
	}

	bool IsSimulationHeld(void* application, bool* out)
	{
		const BYTE* hold = HoldByte(application);
		if (!hold || !out)
			return false;
		*out = (*hold != 0);
		return true;
	}

	// ---------------------------------------------------------------------------
	// Catch-up and fast forward
	// ---------------------------------------------------------------------------

	bool RequestCatchUpSteps(int steps)
	{
		if (steps < 1 || steps > maxReasonableCatchUpSteps)
		{
			Log("main loop: refusing a catch-up request of %d steps, the range is 1 to %d",
			    steps, maxReasonableCatchUpSteps);
			return false;
		}
		int* pending = At<int>(Rva::PendingSteps);
		if (!pending)
			return false;
		*pending = steps;
		return true;
	}

	bool PendingSteps(int* out)
	{
		return ReadGlobal<int>(Rva::PendingSteps, out);
	}

	bool IsCatchUpStep()
	{
		int value = 0;
		if (!ReadGlobal<int>(Rva::IsCatchUpStep, &value))
			return false;
		return value != 0;
	}

	bool BoosterSpeedIndex(int* out)
	{
		return ReadGlobal<int>(Rva::BoosterSpeedIndex, out);
	}

	bool SetBoosterSpeedIndex(int index)
	{
		if (index < 0 || index > 2)
		{
			Log("main loop: booster speed index %d is out of the 0 to 2 range", index);
			return false;
		}
		int* slot = At<int>(Rva::BoosterSpeedIndex);
		if (!slot)
			return false;
		*slot = index;
		return true;
	}

	float BoosterSpeedMultiplier()
	{
		int index = 0;
		if (!BoosterSpeedIndex(&index))
			return 1.0f;
		if (index <= 0 || index > 2)
			return 1.0f; // index 0 takes the game's early-out

		const float* table = (const float*)ModuleAddress(Rva::BoosterSpeedMultipliers);
		if (!Readable(table, 3 * sizeof(float)))
			return 1.0f;
		return table[index];
	}

	int SubStepCount()
	{
		IntNoArgsFn getter = ResolveCallable(Rva::PlayerGetSubStepCount, &subStepGateVerdict);
		if (getter)
			return getter();

		// Fallback: the stored count, which omits the override and the turbo flag.
		int stored = 0;
		return ReadGlobal<int>(Rva::PlayerSubStepCount, &stored) ? stored : 1;
	}

	// ---------------------------------------------------------------------------
	// Frame limiting
	// ---------------------------------------------------------------------------

	int FrameRateDivider()
	{
		int value = 0;
		return ReadGlobal<int>(Rva::FrameRateDivider, &value) ? value : 0;
	}

	bool SetFrameRateDivider(int divider)
	{
		// The game's own setter clamps to 1 or 2 and maps 0 to 1. Reimplemented
		// rather than called, so this needs no prologue check and no game call.
		if (divider <= 0)
			divider = 1;
		if (divider > 2)
			divider = 2;

		int* slot = At<int>(Rva::FrameRateDivider);
		if (!slot)
			return false;
		*slot = divider;
		return true;
	}

	bool UncapFrameRate(bool uncapped)
	{
		int* slot = At<int>(Rva::FrameRateDivider);
		if (!slot)
			return false;

		if (uncapped)
		{
			if (*slot != 0)
				dividerBeforeUncap = *slot;
			*slot = 0; // the Sleep period becomes zero, which the setter would refuse
		}
		else
		{
			*slot = (dividerBeforeUncap >= 1 && dividerBeforeUncap <= 2) ? dividerBeforeUncap : 2;
		}
		return true;
	}

	// ---------------------------------------------------------------------------
	// The fixed timestep
	// ---------------------------------------------------------------------------

	float FixedTimeStep()
	{
		const float* p = Field<float>(Application(), PApp::FixedTimeStep);
		return p ? *p : 0.0f;
	}

	bool SetFixedTimeStep(float seconds)
	{
		if (seconds < 0.0f)
			return false;
		float* p = Field<float>(Application(), PApp::FixedTimeStep);
		if (!p)
			return false;
		*p = seconds;
		Log("main loop: fixed timestep set to %.9f s (0 hands the delta back to the clock)",
		    (double)seconds);
		return true;
	}

	bool FixedTimeStepFromCommandLine(float* out)
	{
		return ReadGlobal<float>(Rva::FixedTimeStepArg, out);
	}

	bool IsPacingFromWallClock()
	{
		if (FixedTimeStep() <= 0.0f)
			return true; // frameTick is reading the clock

		int mode = 0;
		if (ReadGlobal<int>(Rva::PacingMode, &mode) && mode == 1)
			return true; // timing track
		return false;
	}

	// ---------------------------------------------------------------------------
	// The per-step hook
	// ---------------------------------------------------------------------------

	bool HookMainStep(StepGateFn gate, StepObserverFn after)
	{
		stepGate = gate;
		stepObserver = after;

		if (DETOUR_INSTALLED(MainStep))
			return true;
		return DETOUR_INSTALL(MainStep, Rva::MainStep);
	}

	bool MainStepHookInstalled()
	{
		return DETOUR_INSTALLED(MainStep);
	}

	MainStepFn OriginalMainStep()
	{
		return DETOUR_INSTALLED(MainStep) ? (MainStepFn)(UINT_PTR)MainStepDetour.trampoline : NULL;
	}

	// ---------------------------------------------------------------------------
	// Diagnostics
	// ---------------------------------------------------------------------------

	void LogMainLoopState()
	{
		FrameClocks c;
		if (!ReadFrameClocks(&c))
		{
			Log("main loop: the application pointer is not usable yet, nothing to report");
			return;
		}

		Log("main loop: phyre frame %lu  app frame %lu  engine step %lu  ours %lu  stalled %lu",
		    (unsigned long)c.phyreFrameTick, (unsigned long)c.phyreAppFrame,
		    (unsigned long)c.mainStepCounter, (unsigned long)c.localStepCount,
		    (unsigned long)stalledStepCount);
		Log("main loop: dt %.6f s  fixed %.6f s  %.1f fps measured  divider %d (%s)",
		    (double)c.frameDelta, (double)c.fixedTimeStep, (double)c.measuredFps,
		    c.frameRateDivider,
		    c.frameRateDivider == 1 ? "59.94 Hz" : c.frameRateDivider == 2 ? "29.97 Hz"
		                                                                   : "uncapped");
		Log("main loop: pending steps %d  pacing mode %d (%s)  catch-up %s  step scale %.4f",
		    c.pendingSteps, c.pacingMode,
		    c.pacingMode == 1 ? "timing track, WALL CLOCK" : "accumulator",
		    c.isCatchUpStep ? "yes" : "no", (double)c.stepScale);
		Log("main loop: game clock %lu ticks (%.2f s)  sub-steps %d  booster %.1fx  "
		    "pacing from wall clock: %s",
		    (unsigned long)c.gameClock60Hz, c.gameClock60Hz / 60.0,
		    SubStepCount(), (double)BoosterSpeedMultiplier(),
		    IsPacingFromWallClock() ? "YES" : "no");

		PauseState s;
		if (ReadPauseState(&s, false))
		{
			Log("main loop: esc %d hold %d menu %d menuAlt %d menuPending %d saveUi %d  "
			    "sim will step %d  gameplay will step %d",
			    (int)s.escMenuOpen, (int)s.holdByteSet, (int)s.menuSysRunning,
			    (int)s.menuSysRunningAlt, (int)s.menuOpenPending, (int)s.saveUiBusy,
			    (int)s.simulationWillStep, (int)s.gameplayWillStep);
		}
		Log("main loop: step hook %s", MainStepHookInstalled() ? "installed" : "not installed");
	}

} // namespace ffx
