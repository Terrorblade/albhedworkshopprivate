#include "ffx/MotionPlayback.h"

#include <string.h>
#include <windows.h>

#include "ffx/Addresses.h"
#include "ffx/CameraBuild.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{
	namespace
	{

		using workshop::CallSitePatch;
		using workshop::Log;
		using workshop::ModuleAddress;
		using workshop::PatchCallSite;
		using workshop::Readable;

		template <typename T>
		T Global(DWORD rva)
		{
			return *(const volatile T*)ModuleAddress(rva);
		}

		const DWORD AtelContextCount = 7;
		const DWORD AtelContextBytes = 568;
		const DWORD AtelContextArrayBytes = AtelContextCount * AtelContextBytes;

		struct GlobalSpan
		{
			DWORD rva;
			DWORD bytes;
		};

		// Checked on the first read and never again. Module globals cannot stop being
		// readable. 0 not asked, 1 yes, -1 no.
		const GlobalSpan playbackSpans[] = {
			{ Rva::ControlledChr, 4 },
			{ Rva::LiveSceneId, 4 },
			{ Rva::StoryProgress, 2 },
			{ Rva::BtlSubPhase, 1 },
			{ Rva::MotKeepSpeed, 1 },
			{ Rva::EvCurrentEventId, 4 },
			{ Rva::StepScale88, 4 },
			{ Rva::MotHalfSpeed, 4 },
			{ Rva::AtelContextPtr, 4 },
			{ Rva::AtelContextArray, AtelContextArrayBytes },
		};
		int playbackVerdict = 0;

		bool PlaybackGlobalsReadable()
		{
			if (playbackVerdict == 0)
			{
				playbackVerdict = 1;
				for (size_t i = 0; i < sizeof(playbackSpans) / sizeof(playbackSpans[0]); ++i)
				{
					if (!Readable(ModuleAddress(playbackSpans[i].rva), playbackSpans[i].bytes))
					{
						Log("motion playback: global at RVA 0x%08X is not readable, the "
						    "playback readers stay off",
						    playbackSpans[i].rva);
						playbackVerdict = -1;
						break;
					}
				}
			}
			return playbackVerdict > 0;
		}

		AdvanceFrameObserverFn advanceObserver = NULL;
		StepSequenceObserverFn sequenceObserver = NULL;
		ComposerQueryObserverFn composerObserver = NULL;

		CallSitePatch advanceSite = { 0, false, NULL, false };
		CallSitePatch sequenceSite = { 0, false, NULL, false };
		CallSitePatch stationarySite = { 0, false, NULL, false };
		CallSitePatch nearCameraSite = { 0, false, NULL, false };

		typedef void(__cdecl* AdvanceFrameFn)(BYTE* chr, int applyBlend);
		typedef int(__cdecl* StepSequenceFn)(BYTE* chr);
		typedef BYTE(__cdecl* ComposerQueryFn)(BYTE* chr);

		// Each replacement calls site.previous, never the function by address, so a plugin
		// that patched the same site first still runs. See workshop/Detour.h.

		void __cdecl AdvanceFrameHook(BYTE* chr, int applyBlend)
		{
			AdvanceFrameObserverFn watch = advanceObserver;
			if (watch)
				watch(chr, applyBlend, false);
			((AdvanceFrameFn)advanceSite.previous)(chr, applyBlend);
			if (watch)
				watch(chr, applyBlend, true);
		}

		int __cdecl StepSequenceHook(BYTE* chr)
		{
			const int result = ((StepSequenceFn)sequenceSite.previous)(chr);
			StepSequenceObserverFn watch = sequenceObserver;
			if (watch)
				watch(chr, result);
			return result;
		}

		// The callers test al only (test al, al and movsx edi, al), so a BYTE return is the
		// whole contract.
		BYTE __cdecl IsNearlyStationaryHook(BYTE* chr)
		{
			const BYTE result = ((ComposerQueryFn)stationarySite.previous)(chr);
			ComposerQueryObserverFn watch = composerObserver;
			if (watch)
				watch(chr, ComposerQueryStationary, result);
			return result;
		}

		BYTE __cdecl IsNearCameraHook(BYTE* chr)
		{
			const BYTE result = ((ComposerQueryFn)nearCameraSite.previous)(chr);
			ComposerQueryObserverFn watch = composerObserver;
			if (watch)
				watch(chr, ComposerQueryNearCamera, result);
			return result;
		}

	} // namespace

	bool ReadMotionPlaybackGlobals(MotionPlaybackGlobals* out)
	{
		if (!out)
			return false;
		ZeroMemory(out, sizeof(*out));
		out->camSlotIndex = -1;
		if (!PlaybackGlobalsReadable())
			return false;

		out->controlledChr = Global<const BYTE*>(Rva::ControlledChr);
		out->liveSceneId = Global<int>(Rva::LiveSceneId);
		out->storyProgress = Global<WORD>(Rva::StoryProgress);
		out->btlSubPhase = Global<BYTE>(Rva::BtlSubPhase);
		out->motKeepSpeed = Global<BYTE>(Rva::MotKeepSpeed);
		out->currentEventId = Global<int>(Rva::EvCurrentEventId);
		out->stepScale88 = Global<DWORD>(Rva::StepScale88);
		out->motHalfSpeed = Global<int>(Rva::MotHalfSpeed);

		// A range check against the context array instead of a VirtualQuery, which keeps
		// this free of syscalls. The array is module memory and was validated above.
		const BYTE* context = Global<const BYTE*>(Rva::AtelContextPtr);
		const BYTE* first = (const BYTE*)ModuleAddress(Rva::AtelContextArray);
		if (context >= first && context < first + AtelContextArrayBytes)
		{
			out->atelContextByte = *context;
			out->atelContextValid = true;
		}

		const BYTE* slots = CameSlotsBase();
		const int slot = CameActiveSlotIndex();
		if (slots && slot >= 0)
		{
			const BYTE* s = slots + slot * CameBuild::SlotBytes;
			out->camSlot = s;
			out->camSlotIndex = slot;
			memcpy(out->camEye, s + MotPlay::SlotEye, sizeof(out->camEye));
			out->camPitchSelect = s[MotPlay::SlotPitchSelect];
			memcpy(&out->camPitchWhenSet, s + MotPlay::SlotPitchWhenSet, 4);
			memcpy(&out->camPitchWhenClear, s + MotPlay::SlotPitchWhenClear, 4);
			out->camValid = true;
		}
		return true;
	}

	bool HookAdvanceFrame(AdvanceFrameObserverFn observer)
	{
		advanceObserver = observer;
		if (advanceSite.patched)
			return true;
		return PatchCallSite(advanceSite, Rva::MotAdvanceFrameCallSite, Rva::MotAdvanceFrame,
		    (void*)&AdvanceFrameHook, "motion advance frame");
	}

	bool HookStepSequence(StepSequenceObserverFn observer)
	{
		sequenceObserver = observer;
		if (sequenceSite.patched)
			return true;
		return PatchCallSite(sequenceSite, Rva::MotStepSequenceCallSite, Rva::MotStepSequence,
		    (void*)&StepSequenceHook, "motion step sequence");
	}

	bool HookComposerQueries(ComposerQueryObserverFn observer)
	{
		composerObserver = observer;
		if (!stationarySite.patched)
			PatchCallSite(stationarySite, Rva::ChIsNearlyStationaryCallSite, Rva::ChIsNearlyStationary,
			    (void*)&IsNearlyStationaryHook, "joint composer stationary test");
		if (!nearCameraSite.patched)
			PatchCallSite(nearCameraSite, Rva::ChIsNearCameraCallSite, Rva::ChIsNearCameraForSmoothing,
			    (void*)&IsNearCameraHook, "joint composer near camera test");
		return stationarySite.patched && nearCameraSite.patched;
	}

} // namespace ffx
