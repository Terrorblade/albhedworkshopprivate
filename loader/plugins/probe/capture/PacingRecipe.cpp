// Recipe 1, step pacing, for Project Zanarkand PROGRESS 3.4.3.
//
// One record per FFX_StepPacing call, which is one per simulation step except while the
// in-game menu holds MenuSysRunningAlt. The record carries the dt the call got, the frame
// delta animate got, every mode 0 input and output, the timing track state on both sides
// for mode 1, and FFX_StepPacing_AllowCatchUp's answer when the call could have asked it.
// That is everything zan::app::StepPacing::Step needs to be fed and checked against.
//
// This hook also keeps the step cursor, so it runs its bookkeeping on every call whether
// or not anything is being recorded.

#include "capture/Capture.h"

#include "ProbeSettings.h"
#include "ffx/Cutscene.h"
#include "ffx/StepPacing.h"
#include "workshop/Log.h"

namespace probe
{
	namespace
	{

		PacingRecord* g_open = NULL; // reserved on entry, committed on exit

		void CopyVars(PacingVarsRecord* out, const ffx::StepPacingVars& in)
		{
			out->pendingSteps = in.pendingSteps;
			out->catchUpExtraSteps = in.catchUpExtraSteps;
			out->pacingMode = in.pacingMode;
			out->frac60 = in.frac60;
			out->frac30 = in.frac30;
			out->clock60Hz = in.clock60Hz;
			out->menuSysRunning = in.menuSysRunning;
			out->menuSysRunningAlt = in.menuSysRunningAlt;
			out->menuOpenPending = in.menuOpenPending;
			out->saveUiState = in.saveUiState;
		}

		void CopyTrack(TimingTrackRecord* out, const ffx::TimingTrackVars& in)
		{
			out->nextTime = in.nextTime;
			out->tolerance = in.tolerance;
			out->dueTime = in.dueTime;
			out->elapsed = in.elapsed;
			out->startUs = in.startUs;
			out->pauseUsPending = in.pauseUsPending;
			out->pauseUsTotal = in.pauseUsTotal;
			out->firstStep = in.firstStep;
		}

		void __cdecl OnStepPacing(float dt, bool afterCall)
		{
			if (!afterCall)
			{
				ffx::PacingFrameInputs frame;
				const bool frameOk = ffx::ReadPacingFrameInputs(&frame);

				// The cursor first, recording or not.
				++cursor.pacingOrdinal;
				if (cursor.pacingOrdinal != 1 && frame.phyreFrameTick == cursor.lastFrameTick)
					++cursor.pacingCallInFrame;
				else
					cursor.pacingCallInFrame = 0;
				cursor.lastFrameTick = frame.phyreFrameTick;
				cursor.cameraCallInStep = 0;
				cursor.motionCallInStep = 0;
				NoteStepForLimit();

				g_open = NULL;
				if (!settings.pacing || !BeginRecord())
					return;
				workshop::RecordFile* file = FileFor(KindPacing);
				PacingRecord* r = file ? (PacingRecord*)workshop::ReserveRecord(file) : NULL;
				if (!r)
				{
					EndRecord();
					return;
				}

				FillPrefix(&r->prefix, KindPacing, cursor.pacingCallInFrame, 0);
				if (frameOk)
					r->prefix.flags |= PacingFlagFrame;
				r->dt = dt;
				r->frameDelta = frame.frameDelta;
				r->fixedTimeStep = frame.fixedTimeStep;
				r->appFrameNumber = frame.appFrameNumber;
				r->frameRateDivider = frame.frameRateDivider;
				r->boosterEnabled = frame.boosterEnabled;
				r->boosterSpeedIndex = frame.boosterSpeedIndex;
				r->subStepCount = frame.subStepCount;

				ffx::StepPacingVars vars;
				const bool varsOk = ffx::ReadStepPacingVars(&vars);
				if (varsOk)
					CopyVars(&r->entry, vars);
				ffx::TimingTrackVars track;
				if (ffx::ReadTimingTrackVars(&track))
					CopyTrack(&r->trackEntry, track);

				// Asked only when the engine could ask it, which is a pending count of zero on
				// entry. It is a pure query and FFX_StepPacing reaches it with nothing in
				// between that it reads, see the note in reversing/PROBE_FIXTURES.md.
				r->allowCatchUp = AllowCatchUpNotAsked;
				bool allowed = false;
				if (varsOk && vars.pendingSteps == 0 && ffx::QueryCatchUpAllowed(&allowed))
					r->allowCatchUp = allowed ? 1 : 0;

				g_open = r;
				return;
			}

			PacingRecord* r = g_open;
			if (!r)
				return;
			g_open = NULL;

			ffx::StepPacingVars vars;
			ffx::TimingTrackVars track;
			const bool varsOk = ffx::ReadStepPacingVars(&vars);
			const bool trackOk = ffx::ReadTimingTrackVars(&track);
			if (varsOk)
			{
				CopyVars(&r->exit, vars);
				r->prefix.flags |= PacingFlagVars;
			}
			if (trackOk)
			{
				CopyTrack(&r->trackExit, track);
				r->prefix.flags |= PacingFlagTrack;
			}
			workshop::CommitRecord(FileFor(KindPacing));
			EndRecord();
		}

	} // namespace

	bool InstallPacingRecipe()
	{
		const bool ok = ffx::HookStepPacing(&OnStepPacing);
		workshop::Log("pacing recipe: %s", ok ? "hooked FFX_StepPacing" : "COULD NOT HOOK FFX_StepPacing");
		return ok;
	}

} // namespace probe
