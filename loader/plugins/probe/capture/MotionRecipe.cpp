// Recipe 3, character motion, for Project Zanarkand PROGRESS 10.5.
//
// reversing/CHARACTER_MOTION.md section 10.4. FFX_Ch_UpdateMotionAll is hooked at entry
// and at return, and on each side every in use CHR gets one record: the step identity,
// the globals the step reads, and the CHR fields listed in MotionChrSpans. A side with no
// CHR in use still gets one record, flagged MotionFlagNoChr, so the globals are never
// missing for a step.
//
// Cost: about 650 bytes a CHR a side. Twenty CHRs at one sub step a frame is about
// 0.8 MB a second. The pool span is validated once per map load, not per step, and the
// copy itself sits inside __try so a CHR freed under us costs a flag, not the game.

#include "capture/Capture.h"

#include <string.h>

#include "ProbeSettings.h"
#include "ffx/MotionStep.h"
#include "workshop/Log.h"

namespace probe
{
	namespace
	{

		// The step thread's copy of the globals for the side being recorded, filled once per
		// side and copied into every record of that side.
		MotionRecord g_template;

		// Plain C on purpose: a function with __try cannot hold C++ objects that unwind.
		// Returns the ChrFlag bits it learned, ChrFlagFaulted if the copy did not finish.
		WORD CopyChr(BYTE* out, const BYTE* chr, short* anmFrameCount)
		{
			WORD flags = 0;
			__try
			{
				BYTE* at = out;
				for (int i = 0; i < MotionChrSpanCount; ++i)
				{
					memcpy(at, chr + MotionChrSpans[i].offset, MotionChrSpans[i].bytes);
					at += MotionChrSpans[i].bytes;
				}

				const void* seqPc = *(const void* const*)(chr + 0x714);
				const BYTE* anmHeader = *(const BYTE* const*)(chr + 0x800);
				if (seqPc)
					flags |= ChrFlagSeqPc;
				if (anmHeader)
				{
					flags |= ChrFlagAnmHeader;
					// One s16 behind a pointer the engine itself reads every sub step. A
					// VirtualQuery per CHR per step is exactly the stall the kit forbids, so the
					// __try is the guard here.
					*anmFrameCount = *(const short*)anmHeader;
					flags |= ChrFlagAnmCount;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				flags |= ChrFlagFaulted;
			}
			return flags;
		}

		void FillTemplate(float dt, BYTE phase)
		{
			ZeroMemory(&g_template, sizeof(g_template));
			g_template.dt = dt;
			g_template.prefix.phase = phase;

			ffx::MotionGlobals g;
			if (!ffx::ReadMotionGlobals(&g))
				return;
			g_template.prefix.flags |= MotionFlagGlobals;
			g_template.chrArray = (DWORD)(UINT_PTR)g.chrArray;
			g_template.chrCount = g.chrCount;
			g_template.controlledChr = (DWORD)(UINT_PTR)g.controlledChr;
			g_template.walkmeshScale = g.walkmeshScale;
			g_template.walkmeshTriCount = g.walkmeshTriCount;
			g_template.waterLevel = g.waterLevel;
			g_template.waterLevelMax = g.waterLevelMax;
			memcpy(g_template.groundRemap, g.groundRemap, sizeof(g_template.groundRemap));
			g_template.debugNoClip = g.debugNoClip;
			g_template.stepScale88 = g.stepScale88;
			g_template.motHalfSpeed = g.motHalfSpeed;
			g_template.chrUpdateFrozen = g.chrUpdateFrozen;
			g_template.motKeepSpeed = g.motKeepSpeed;
			g_template.motionUpdateEnabled = g.motionUpdateEnabled;
			g_template.currentEventId = g.currentEventId;
			g_template.liveSceneId = g.liveSceneId;
			memcpy(g_template.currentEventName, g.currentEventName, sizeof(g_template.currentEventName));
		}

		void RecordSide(float dt, BYTE phase, WORD call)
		{
			workshop::RecordFile* file = FileFor(KindMotion);
			if (!file)
				return;

			FillTemplate(dt, phase);

			const BYTE* pool = NULL;
			int slots = 0;
			const bool poolOk = ffx::ChrPoolForReading(&pool, &slots);
			if (poolOk)
				g_template.prefix.flags |= MotionFlagPool;

			// Count first, so every record of the side carries the same inUseCount.
			WORD inUse = 0;
			if (poolOk)
				for (int i = 0; i < slots; ++i)
					if (pool[(DWORD)i * ChrStride + 2] != 0)
						++inUse;

			if (inUse == 0)
			{
				MotionRecord* r = (MotionRecord*)workshop::ReserveRecord(file);
				if (!r)
					return;
				memcpy(r, &g_template, sizeof(*r));
				FillPrefix(&r->prefix, KindMotion, call, phase);
				r->prefix.flags |= MotionFlagNoChr;
				r->slot = NoSlot;
				workshop::CommitRecord(file);
				return;
			}

			const DWORD controlled = g_template.controlledChr;
			WORD ordinal = 0;
			for (int i = 0; i < slots; ++i)
			{
				const BYTE* chr = pool + (DWORD)i * ChrStride;
				if (chr[2] == 0)
					continue;

				MotionRecord* r = (MotionRecord*)workshop::ReserveRecord(file);
				if (!r)
					return; // the budget is spent, the drop is counted, the rest would drop too

				// The template's zeroed CHR area is copied too and then overwritten, which is
				// simpler than copying around it and costs 488 bytes.
				memcpy(r, &g_template, sizeof(*r));
				FillPrefix(&r->prefix, KindMotion, call, phase);
				r->slot = (WORD)i;
				r->inUseCount = inUse;
				r->ordinal = ordinal++;

				short anm = 0;
				WORD flags = CopyChr(r->chr, chr, &anm);
				if ((DWORD)(UINT_PTR)chr == controlled)
					flags |= ChrFlagControlled;
				r->chrFlags = flags;
				r->anmFrameCount = anm;
				workshop::CommitRecord(file);
			}
		}

		WORD g_call = 0;
		bool g_holding = false; // BeginRecord succeeded on entry, so both sides get recorded

		// The gate is held from entry to return, like the other two recipes, so a capture
		// that starts or stops mid call never leaves one side without the other.
		void __cdecl OnUpdateMotionAll(float dt, bool afterCall)
		{
			if (!afterCall)
			{
				g_call = cursor.motionCallInStep++;
				g_holding = settings.motion && BeginRecord();
				if (g_holding)
					RecordSide(dt, 0, g_call);
				return;
			}

			if (!g_holding)
				return;
			g_holding = false;
			RecordSide(dt, 1, g_call);
			EndRecord();
		}

	} // namespace

	bool MotionSpansConsistent()
	{
		int sum = 0;
		int previousEnd = 0;
		for (int i = 0; i < MotionChrSpanCount; ++i)
		{
			if (MotionChrSpans[i].offset < previousEnd || MotionChrSpans[i].offset + MotionChrSpans[i].bytes > (int)ChrStride)
			{
				workshop::Log("motion recipe: span %d at +0x%03X overlaps the one before it or runs "
				              "off the CHR, motion recording is off",
				    i, MotionChrSpans[i].offset);
				return false;
			}
			previousEnd = MotionChrSpans[i].offset + MotionChrSpans[i].bytes;
			sum += MotionChrSpans[i].bytes;
		}
		if (sum != MotionChrBytes)
		{
			workshop::Log("motion recipe: the spans add up to %d bytes and the record holds %d, "
			              "motion recording is off",
			    sum, MotionChrBytes);
			return false;
		}
		return true;
	}

	DWORD MotionExtensionBytes(void* out, DWORD capacity)
	{
		MotionExtension e;
		e.chrStride = ChrStride;
		e.spanCount = (DWORD)MotionChrSpanCount;
		memcpy(e.spans, MotionChrSpans, sizeof(e.spans));
		if (capacity < sizeof(e))
			return 0;
		memcpy(out, &e, sizeof(e));
		return sizeof(e);
	}

	bool InstallMotionRecipe()
	{
		const bool ok = ffx::HookUpdateMotionAll(&OnUpdateMotionAll);
		workshop::Log("motion recipe: %s",
		    ok ? "hooked FFX_Ch_UpdateMotionAll" : "COULD NOT HOOK FFX_Ch_UpdateMotionAll");
		return ok;
	}

} // namespace probe
