#include "ffx/Encounter.h"

#include <string.h>

#include "ffx/Addresses.h"
#include "workshop/Detour.h"
#include "workshop/Log.h"
#include "workshop/HostModule.h"

namespace ffx
{

	using workshop::ModuleAddress;
	using workshop::Readable;

	namespace
	{

		inline BYTE Rd8(const BYTE* p)
		{
			return *(volatile const BYTE*)p;
		}
		inline DWORD Rd32(const BYTE* p)
		{
			return *(volatile const DWORD*)p;
		}
		inline float RdF(const BYTE* p)
		{
			return *(volatile const float*)p;
		}

		// One guarded read per global, because every one of these is in uninitialised data
		// until a battle has been set up once, and a title-screen panel refresh must not
		// fault.
		bool ReadF(DWORD rva, float* out)
		{
			const BYTE* p = (const BYTE*)ModuleAddress(rva);
			if (!Readable(p, 4))
				return false;

			*out = RdF(p);
			return true;
		}

		bool ReadB(DWORD rva, BYTE* out)
		{
			const BYTE* p = (const BYTE*)ModuleAddress(rva);
			if (!Readable(p, 1))
				return false;

			*out = Rd8(p);
			return true;
		}

		bool ReadD(DWORD rva, DWORD* out)
		{
			const BYTE* p = (const BYTE*)ModuleAddress(rva);
			if (!Readable(p, 4))
				return false;

			*out = Rd32(p);
			return true;
		}

		// ---------------------------------------------------------------------------
		// The one detour.
		//
		// Target FFX_Field_StepRandomEncounter, whose prologue is
		//     55        push ebp
		//     8B EC     mov  ebp, esp
		//     83 EC 10  sub  esp, 10h
		// Six bytes, no branch and no absolute among them, and byte six starts a new
		// instruction (53, push ebx), so the stolen range lands on a boundary.
		// ---------------------------------------------------------------------------
		DETOUR_DECLARE(EncounterCheck, int, (int sceneId, int encZone, float dist));

		DETOUR_PROLOGUE(EncounterCheck) = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10 };

		EncounterDistanceFn g_hook = NULL;
		EncounterHookStats g_stats = { 0, 0.0f, 0.0f };

		int __cdecl EncounterCheckHook(int sceneId, int encZone, float dist)
		{
			float substituted = dist;

			// A null hook is a pass-through rather than an uninstall. Patching live code
			// off again would be a race against the game thread for no benefit, and the
			// cost of the pass-through is one branch per sub-step.
			EncounterDistanceFn hook = g_hook;
			if (hook)
				substituted = hook(dist);

			++g_stats.calls;
			g_stats.lastEngineDistance = dist;
			g_stats.lastReturnedDistance = substituted;

			return DETOUR_ORIGINAL(EncounterCheck)(sceneId, encZone, substituted);
		}

	} // namespace

	bool ReadEncounterState(EncounterState* out)
	{
		if (!out)
			return false;

		memset(out, 0, sizeof(*out));

		if (!ReadF(Rva::EncDistRemain, &out->distRemain))
			return false;
		if (!ReadF(Rva::EncDistTotal, &out->distTotal))
			return false;

		// steps is what the chance curve is actually built on, so deriving it here keeps
		// every caller from repeating the divisor and getting it wrong.
		out->steps = (int)(out->distTotal / 10.0f);

		BYTE enabled = 0;
		BYTE debugOn = 0;
		if (ReadB(Rva::EncountersEnabled, &enabled))
			out->encountersEnabled = enabled != 0;
		if (ReadB(Rva::DebugEncountersOn, &debugOn))
			out->debugEncountersOn = debugOn != 0;

		DWORD rate = 1;
		if (ReadD(Rva::BoosterEncounterRate, &rate))
			out->boosterRate = (int)rate;

		out->valid = true;
		return true;
	}

	bool ReadPendingBattle(PendingBattle* out)
	{
		if (!out)
			return false;

		memset(out, 0, sizeof(*out));

		BYTE kind = 0;
		if (!ReadB(Rva::BattlePendingKind, &kind))
			return false;

		out->kind = (int)kind;
		out->pending = kind != 0;

		// The scene index is in the HIGH word. The low word belongs to something else and
		// is deliberately not reported, because guessing at it here would put a wrong
		// number in a log that somebody later trusts.
		DWORD sceneAndMap = 0;
		if (ReadD(Rva::BattleSceneAndMap, &sceneAndMap))
			out->sceneIndex = (int)((sceneAndMap >> 16) & 0xFFFF);

		BYTE zone = 0;
		BYTE formation = 0;
		if (ReadB(Rva::BattleZoneIndex, &zone))
			out->zoneIndex = (int)zone;
		if (ReadB(Rva::BattleFormationIndex, &formation))
			out->formationIndex = (int)formation;

		out->valid = true;
		return true;
	}

	bool SetEncounterDistanceHook(EncounterDistanceFn hook)
	{
		if (!DETOUR_INSTALLED(EncounterCheck))
		{
			if (!DETOUR_INSTALL(EncounterCheck, Rva::FieldStepRandomEncounter))
			{
				// The prologue did not match, so nothing was patched. Saying so plainly
				// matters: the caller must not go on believing it is steering encounters.
				workshop::Log("encounter: the distance detour did NOT install, so nothing "
				              "is steering the encounter rate");
				return false;
			}
		}

		g_hook = hook;
		return true;
	}

	bool EncounterHookInstalled()
	{
		return DETOUR_INSTALLED(EncounterCheck);
	}

	void ReadEncounterHookStats(EncounterHookStats* out)
	{
		if (out)
			*out = g_stats;
	}

	void LogEncounterState()
	{
		EncounterState state;
		if (!ReadEncounterState(&state))
		{
			workshop::Log("encounter: cannot read the accumulators yet, so no field map has "
			              "been set up");
			return;
		}

		workshop::Log("=== random encounters ===");
		workshop::Log("distance: remain %.2f of the 10.0 a roll needs, total %.2f which is "
		              "%d steps",
		    state.distRemain, state.distTotal, state.steps);

		// Each gate with whether it is currently the one stopping a battle, because "no
		// encounters are happening" has about six possible causes and guessing between
		// them wastes a test run.
		workshop::Log("gates: No Encounters armour ability %s, debug encounters %s, booster "
		              "rate %d (%s)",
		    state.encountersEnabled ? "not active" : "ACTIVE, blocking",
		    state.debugEncountersOn ? "on" : "off", state.boosterRate,
		    state.boosterRate == 0   ? "off, distance is zeroed"
		    : state.boosterRate == 2 ? "high, distance multiplied by 10"
		                             : "normal");

		PendingBattle battle;
		if (ReadPendingBattle(&battle) && battle.pending)
			workshop::Log("A BATTLE IS PENDING: kind %d, scene %d, zone %d, formation %d",
			    battle.kind, battle.sceneIndex, battle.zoneIndex, battle.formationIndex);
		else
			workshop::Log("no battle pending");

		if (!EncounterHookInstalled())
		{
			workshop::Log("the distance detour is NOT installed, so remote movement cannot "
			              "contribute");
			return;
		}

		workshop::Log("distance detour installed, consulted %u times, last saw %.3f and "
		              "returned %.3f%s",
		    (unsigned)g_stats.calls, g_stats.lastEngineDistance,
		    g_stats.lastReturnedDistance,
		    g_hook ? "" : " (no hook set, so it is a pass-through)");

		if (g_stats.calls == 0)
			workshop::Log("zero calls means the engine is not reaching the check at all, "
			              "which is a different problem from the hook being wrong. The "
			              "caller is FFX_Atel_StepFieldFrame, so no field map is running.");
	}

} // namespace ffx
