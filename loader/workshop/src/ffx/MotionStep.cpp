#include "ffx/MotionStep.h"

#include <string.h>
#include <windows.h>

#include "ffx/Addresses.h"
#include "ffx/Layout.h"
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

		// The engine reads base[code] for code 48..63 only, see the warning on
		// Rva::GroundTypeRemapBiasedBase.
		const int RemapFirstCode = 48;
		const int RemapCodeCount = 16;

		// Generous on purpose. The original allocates 36 plus 8, and a count far above that
		// is a torn read or a pool that is not built yet, not a bigger pool.
		const int MaxPlausibleChrCount = 256;

		struct GlobalSpan
		{
			DWORD rva;
			DWORD bytes;
		};

		const GlobalSpan motionSpans[] = {
			{ Rva::ChrArray, 4 },
			{ Rva::ChrCount, 4 },
			{ Rva::ControlledChr, 4 },
			{ Rva::WalkmeshScale, 4 },
			{ Rva::WalkmeshTriCount, 4 },
			{ Rva::WaterLevel, 4 },
			{ Rva::WaterLevelMax, 4 },
			{ Rva::GroundTypeRemapBiasedBase + RemapFirstCode * 4, RemapCodeCount * 4 },
			{ Rva::PlayerDebugNoClip, 4 },
			{ Rva::StepScale88, 4 },
			{ Rva::MotHalfSpeed, 4 },
			{ Rva::MotionKillSwitch, 1 },
			{ Rva::MotKeepSpeed, 1 },
			{ Rva::MotionUpdateEnabled, 1 },
			{ Rva::EvCurrentEventId, 4 },
			{ Rva::LiveSceneId, 4 },
			{ Rva::EvCurrentEventName, 16 },
		};
		int motionVerdict = 0;

		bool MotionGlobalsReadable()
		{
			if (motionVerdict == 0)
			{
				motionVerdict = 1;
				for (size_t i = 0; i < sizeof(motionSpans) / sizeof(motionSpans[0]); ++i)
				{
					if (!Readable(ModuleAddress(motionSpans[i].rva), motionSpans[i].bytes))
					{
						Log("motion step: global at RVA 0x%08X is not readable, the motion "
						    "readers stay off",
						    motionSpans[i].rva);
						motionVerdict = -1;
						break;
					}
				}
			}
			return motionVerdict > 0;
		}

		// The last pool span that was checked, and the answer.
		const BYTE* checkedBase = NULL;
		int checkedCount = -1;
		bool checkedOk = false;

		MotionStepObserverFn observer = NULL;

		DETOUR_DECLARE(UpdateMotionAll, int, (float dt));

		// Verified in IDA at 0x832E10. push ebp, mov ebp esp, sub esp 10h. The call to
		// FFX_Dbg_IsChrUpdateFrozen right after is a rel32 and is deliberately left alone.
		DETOUR_PROLOGUE(UpdateMotionAll) = {
			0x55,            // push ebp
			0x8B, 0xEC,      // mov  ebp, esp
			0x83, 0xEC, 0x10 // sub  esp, 10h
		};

		int __cdecl UpdateMotionAllHook(float dt)
		{
			MotionStepObserverFn watch = observer;
			if (watch)
				watch(dt, false);
			const int result = DETOUR_ORIGINAL(UpdateMotionAll)(dt);
			if (watch)
				watch(dt, true);
			return result;
		}

	} // namespace

	bool ReadMotionGlobals(MotionGlobals* out)
	{
		if (!out)
			return false;
		ZeroMemory(out, sizeof(*out));
		if (!MotionGlobalsReadable())
			return false;

		out->chrArray = Global<const BYTE*>(Rva::ChrArray);
		out->chrCount = Global<int>(Rva::ChrCount);
		out->controlledChr = Global<const BYTE*>(Rva::ControlledChr);
		out->walkmeshScale = Global<float>(Rva::WalkmeshScale);
		out->walkmeshTriCount = Global<int>(Rva::WalkmeshTriCount);
		out->waterLevel = Global<float>(Rva::WaterLevel);
		out->waterLevelMax = Global<float>(Rva::WaterLevelMax);
		memcpy(out->groundRemap,
		    ModuleAddress(Rva::GroundTypeRemapBiasedBase + RemapFirstCode * 4),
		    sizeof(out->groundRemap));
		out->debugNoClip = Global<int>(Rva::PlayerDebugNoClip);
		out->stepScale88 = Global<DWORD>(Rva::StepScale88);
		out->motHalfSpeed = Global<int>(Rva::MotHalfSpeed);
		out->chrUpdateFrozen = Global<BYTE>(Rva::MotionKillSwitch);
		out->motKeepSpeed = Global<BYTE>(Rva::MotKeepSpeed);
		out->motionUpdateEnabled = Global<BYTE>(Rva::MotionUpdateEnabled);
		out->currentEventId = Global<int>(Rva::EvCurrentEventId);
		out->liveSceneId = Global<int>(Rva::LiveSceneId);
		memcpy(out->currentEventName, ModuleAddress(Rva::EvCurrentEventName),
		    sizeof(out->currentEventName));
		return true;
	}

	bool ChrPoolForReading(const BYTE** base, int* count)
	{
		if (!base || !count)
			return false;
		*base = NULL;
		*count = 0;
		if (!MotionGlobalsReadable())
			return false;

		const BYTE* pool = Global<const BYTE*>(Rva::ChrArray);
		const int slots = Global<int>(Rva::ChrCount);
		if (pool != checkedBase || slots != checkedCount)
		{
			checkedBase = pool;
			checkedCount = slots;
			checkedOk = pool && slots > 0 && slots <= MaxPlausibleChrCount && Readable(pool, (SIZE_T)slots * Chr::Stride);
		}
		if (!checkedOk)
			return false;
		*base = pool;
		*count = slots;
		return true;
	}

	bool HookUpdateMotionAll(MotionStepObserverFn fn)
	{
		observer = fn;
		if (DETOUR_INSTALLED(UpdateMotionAll))
			return true;
		return DETOUR_INSTALL(UpdateMotionAll, Rva::ChUpdateMotionAll);
	}

	bool UpdateMotionAllHookInstalled()
	{
		return DETOUR_INSTALLED(UpdateMotionAll);
	}

} // namespace ffx
