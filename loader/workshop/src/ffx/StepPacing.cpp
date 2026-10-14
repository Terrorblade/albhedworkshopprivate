#include "ffx/StepPacing.h"

#include <windows.h>

#include "ffx/Addresses.h"
#include "ffx/MainLoop.h"
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

		template <typename T>
		T Global(DWORD rva)
		{
			return *(const volatile T*)ModuleAddress(rva);
		}

		struct GlobalSpan
		{
			DWORD rva;
			DWORD bytes;
		};

		// Module globals cannot stop being readable once they are, so the check runs on the
		// first call and the answer is kept. 0 not asked yet, 1 yes, -1 no.
		bool SpansReadableOnce(const GlobalSpan* spans, int count, int* verdict, const char* what)
		{
			if (*verdict == 0)
			{
				*verdict = 1;
				for (int i = 0; i < count; ++i)
				{
					if (!Readable(ModuleAddress(spans[i].rva), spans[i].bytes))
					{
						Log("step pacing: %s global at RVA 0x%08X is not readable, so that "
						    "reader stays off",
						    what, spans[i].rva);
						*verdict = -1;
						break;
					}
				}
			}
			return *verdict > 0;
		}

		const GlobalSpan pacingSpans[] = {
			{ Rva::PendingSteps, 4 },
			{ Rva::CatchUpExtraSteps, 4 },
			{ Rva::PacingMode, 4 },
			{ Rva::PacingFrac60, 4 },
			{ Rva::PacingFrac30, 4 },
			{ Rva::GameClock60Hz, 4 },
			{ Rva::MenuSysRunning, 4 },
			{ Rva::MenuSysRunningAlt, 4 },
			{ Rva::MenuOpenPending, 4 },
			{ Rva::SaveUiState, 4 },
		};
		int pacingVerdict = 0;

		const GlobalSpan trackSpans[] = {
			{ Rva::TimingTrackNextTime, 4 },
			{ Rva::TimingTrackTolerance, 4 },
			{ Rva::TimingTrackDueTime, 4 },
			{ Rva::TimingTrackElapsed, 4 },
			{ Rva::TimingTrackStartMs, 8 },
			{ Rva::TimingTrackPauseMsPending, 8 },
			{ Rva::TimingTrackPauseMsTotal, 8 },
			{ Rva::TimingTrackFirstStep, 4 },
		};
		int trackVerdict = 0;

		const GlobalSpan frameSpans[] = {
			{ Rva::Application, 4 },
			{ Rva::PhyreFrameTickCount, 4 },
			{ Rva::MainStepCounter, 4 },
			{ Rva::FrameRateDivider, 4 },
			{ Rva::BoosterEnabled, 4 },
			{ Rva::BoosterSpeedIndex, 4 },
			{ Rva::PlayerSubStepCount, 4 },
		};
		int frameVerdict = 0;

		// The application singleton, checked once per distinct pointer value. It is built
		// before any plugin loads and never moves, so in practice this is one check per
		// process.
		const BYTE* checkedApp = NULL;
		bool checkedAppOk = false;

		const BYTE* ApplicationChecked()
		{
			const BYTE* app = Global<const BYTE*>(Rva::Application);
			if (app != checkedApp)
			{
				checkedApp = app;
				checkedAppOk = app && Readable(app, PApp::FixedTimeStep + sizeof(float));
			}
			return checkedAppOk ? app : NULL;
		}

		StepPacingObserverFn observer = NULL;

		DETOUR_DECLARE(StepPacing, int, (float dt));

		// Verified in IDA at 0x821E80. Six bytes, push ebp, mov ebp esp, sub esp 18h, no
		// relative operand among them.
		DETOUR_PROLOGUE(StepPacing) = {
			0x55,            // push ebp
			0x8B, 0xEC,      // mov  ebp, esp
			0x83, 0xEC, 0x18 // sub  esp, 18h
		};

		int __cdecl StepPacingHook(float dt)
		{
			StepPacingObserverFn watch = observer;
			if (watch)
				watch(dt, false);
			const int result = DETOUR_ORIGINAL(StepPacing)(dt);
			if (watch)
				watch(dt, true);
			return result;
		}

	} // namespace

	bool ReadStepPacingVars(StepPacingVars* out)
	{
		if (!out || !SpansReadableOnce(pacingSpans, sizeof(pacingSpans) / sizeof(pacingSpans[0]),
		                &pacingVerdict, "pacing"))
			return false;
		out->pendingSteps = Global<int>(Rva::PendingSteps);
		out->catchUpExtraSteps = Global<int>(Rva::CatchUpExtraSteps);
		out->pacingMode = Global<int>(Rva::PacingMode);
		out->frac60 = Global<float>(Rva::PacingFrac60);
		out->frac30 = Global<float>(Rva::PacingFrac30);
		out->clock60Hz = Global<DWORD>(Rva::GameClock60Hz);
		out->menuSysRunning = Global<int>(Rva::MenuSysRunning);
		out->menuSysRunningAlt = Global<int>(Rva::MenuSysRunningAlt);
		out->menuOpenPending = Global<int>(Rva::MenuOpenPending);
		out->saveUiState = Global<int>(Rva::SaveUiState);
		return true;
	}

	bool ReadTimingTrackVars(TimingTrackVars* out)
	{
		if (!out || !SpansReadableOnce(trackSpans, sizeof(trackSpans) / sizeof(trackSpans[0]),
		                &trackVerdict, "timing track"))
			return false;
		out->nextTime = Global<float>(Rva::TimingTrackNextTime);
		out->tolerance = Global<float>(Rva::TimingTrackTolerance);
		out->dueTime = Global<float>(Rva::TimingTrackDueTime);
		out->elapsed = Global<float>(Rva::TimingTrackElapsed);
		out->startUs = Global<LONGLONG>(Rva::TimingTrackStartMs);
		out->pauseUsPending = Global<LONGLONG>(Rva::TimingTrackPauseMsPending);
		out->pauseUsTotal = Global<LONGLONG>(Rva::TimingTrackPauseMsTotal);
		out->firstStep = Global<int>(Rva::TimingTrackFirstStep);
		return true;
	}

	bool ReadPacingFrameInputs(PacingFrameInputs* out)
	{
		if (!out)
			return false;
		ZeroMemory(out, sizeof(*out));
		if (!SpansReadableOnce(frameSpans, sizeof(frameSpans) / sizeof(frameSpans[0]),
		        &frameVerdict, "frame"))
			return false;

		out->phyreFrameTick = Global<DWORD>(Rva::PhyreFrameTickCount);
		out->mainStepCounter = Global<DWORD>(Rva::MainStepCounter);
		out->frameRateDivider = Global<int>(Rva::FrameRateDivider);
		out->boosterEnabled = Global<int>(Rva::BoosterEnabled);
		out->boosterSpeedIndex = Global<int>(Rva::BoosterSpeedIndex);
		out->subStepCount = Global<int>(Rva::PlayerSubStepCount);

		const BYTE* app = ApplicationChecked();
		if (!app)
			return false;
		out->appFrameNumber = *(const volatile DWORD*)(app + PApp::FrameNumber);
		out->frameDelta = *(const volatile float*)(app + PApp::FrameDelta);
		out->fixedTimeStep = *(const volatile float*)(app + PApp::FixedTimeStep);
		return true;
	}

	bool HookStepPacing(StepPacingObserverFn fn)
	{
		observer = fn;
		if (DETOUR_INSTALLED(StepPacing))
			return true;
		return DETOUR_INSTALL(StepPacing, Rva::StepPacing);
	}

	bool StepPacingHookInstalled()
	{
		return DETOUR_INSTALLED(StepPacing);
	}

} // namespace ffx
