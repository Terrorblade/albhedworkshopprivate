#include "world/MinigameSync.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Input.h"
#include "ffx/StepClock.h"
#include "ffx/Random.h"
#include "ffx/addresses/MainLoop.h"
#include "ffx/addresses/Battle.h"
#include "ffx/addresses/Minigames.h"
#include "net/NetLink.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "workshop/Protocol.h"
#include "workshop/Session.h"

namespace pilgrimage
{

	using namespace workshop;
	using ffx::RandomState;

	namespace
	{

		// One simulation step. 29.97 Hz, the same figure the engine hands
		// FFX_Ch_UpdateMotionAll as a hardcoded constant.
		const float StepSeconds = 0.033373334f;

		bool g_installed = false;
		bool g_running = false;
		int g_publishes = 0;
		int g_applies = 0;
		bool g_flagsCleared = false;
		char g_status[160] = "idle";

		BYTE* FlagPtr(DWORD rva)
		{
			BYTE* p = (BYTE*)ModuleAddress(rva);
			return Readable(p, 1) ? p : nullptr;
		}

		// Raw bits, not a float compare. These are hash inputs, so an exact bit
		// difference is exactly what we want to catch.
		DWORD WordAt(DWORD rva)
		{
			const DWORD* p = (const DWORD*)ModuleAddress(rva);
			return Readable(p, 4) ? *p : 0;
		}

		// Both machines force these to 0. Picking the host's value instead would mean
		// a client that happened to have one set would load a different package for
		// the same map, which is the failure this exists to stop.
		void ClearPackageSwapFlags()
		{
			const DWORD flags[3] = { ffx::Rva::ChocoboGameDebugEnable,
				ffx::Rva::ThunderPlainTreasureEnable, ffx::Rva::FullNagi0700Enable };
			int cleared = 0;
			for (int i = 0; i < 3; ++i)
			{
				BYTE* p = FlagPtr(flags[i]);
				if (!p)
					continue;
				if (*p != 0)
				{
					*p = 0;
					++cleared;
				}
			}
			g_flagsCleared = true;
			if (cleared > 0)
				Log("minigame: cleared %d package swap flag(s). Those swap the bytecode "
				    "for a map id, so a mismatch runs different scripts",
				    cleared);
		}

		uint32_t PackedFlags()
		{
			const DWORD flags[4] = { ffx::Rva::ChocoboGameDebugEnable,
				ffx::Rva::ThunderPlainTreasureEnable, ffx::Rva::FullNagi0700Enable,
				ffx::Rva::ScriptWaitGate };
			uint32_t v = 0;
			for (int i = 0; i < 4; ++i)
			{
				BYTE* p = FlagPtr(flags[i]);
				v |= (uint32_t)((p ? *p : 0) & 0xFF) << (i * 8);
			}
			return v;
		}

		typedef int(__cdecl* SubStepCountFn)(void);

		int SubStepCount()
		{
			SubStepCountFn fn = (SubStepCountFn)ModuleAddress(ffx::Rva::PlayerGetSubStepCount);
			if (!Readable((void*)fn, 1))
				return -1;
			return fn();
		}

		// ---------------------------------------------------------------------------

		class MinigameSink : public MessageSink
		{
		public:
			virtual bool OnSessionMessage(const MessageHeader& header,
			    const unsigned char* payload, int payloadLength)
			{
				if (header.kind != MessageRandomState)
					return false;
				if (payloadLength < (int)sizeof(RandomStatePayload))
				{
					Log("minigame: random state message too short, %d bytes", payloadLength);
					return true;
				}

				const RandomStatePayload* in = (const RandomStatePayload*)payload;

				// A size change means the two builds disagree about the RNG, and a
				// partial apply is worse than none.
				if (in->streamBytes != (uint32_t)RandomStreamBytes)
				{
					Log("minigame: refusing random state, peer says %u stream bytes and we "
					    "expect %d",
					    (unsigned)in->streamBytes, RandomStreamBytes);
					return true;
				}

				RandomState state;
				memset(&state, 0, sizeof(state));
				memcpy(state.stream, in->stream, RandomStreamBytes);
				state.battle = (DWORD)in->battle;
				state.effect = (DWORD)in->effect;
				state.valid = true;

				if (!ffx::WriteRandomState(state))
				{
					Log("minigame: random state write refused, globals not writable");
					return true;
				}

				++g_applies;
				Log("minigame: applied random state from step %u, reason %u, hash now %08X",
				    (unsigned)in->step, (unsigned)in->reason,
				    (unsigned)ffx::RandomStateHash());
				return true;
			}
		};

		MinigameSink g_sink;

	}

	bool InstallMinigameSync()
	{
		if (g_installed)
			return true;

		if (!ffx::RandomStateAvailable())
		{
			Log("minigame: RNG globals not readable, minigame sync is off");
			return false;
		}

		// Not fatal, but this is the one with the widest reach. It covers held button
		// repeat, the native menu's cursor repeat, the message window sampler that
		// feeds the BATTLE MENU's cursor repeat, and the 0.30 / 0.45 second input
		// lockout every battle menu page opens with. See ffx/StepClock.h.
		if (!ffx::InstallStepClock())
			Log("minigame: input timing is still on the wall clock, so held button "
			    "repeat and the battle menu's input lockout will differ between machines");

		g_installed = true;
		return true;
	}

	bool MinigameSyncInstalled()
	{
		return g_installed;
	}

	void StartMinigameSync()
	{
		if (!g_installed)
			return;

		ClearPackageSwapFlags();

		Session* session = ActiveSession();
		if (session && session->AddMessageSink(&g_sink))
		{
			if (session->Role() == RoleHost)
				PublishRandomState(RandomStateJoin);
		}

		g_running = true;
	}

	void StopMinigameSync()
	{
		Session* session = ActiveSession();
		if (session)
			session->RemoveMessageSink(&g_sink);
		g_running = false;
	}

	void ServiceMinigameStep(uint32_t step)
	{
		if (!g_installed)
			return;

		// A step derived clock for the pad auto repeat timer. It only ever reads
		// differences, so the absolute value does not matter, only that both machines
		// compute it from the same step.
		ffx::SetStepClockTime((float)step * StepSeconds);
	}

	bool PublishRandomState(int reason)
	{
		return PublishRandomStateTo((int)BroadcastPeer, reason);
	}

	bool PublishRandomStateTo(int peer, int reason)
	{
		Session* session = ActiveSession();
		if (!session || session->Role() != RoleHost)
			return false;

		RandomState state;
		if (!ffx::ReadRandomState(&state))
			return false;

		RandomStatePayload out;
		memset(&out, 0, sizeof(out));
		out.step = 0;
		out.reason = (uint32_t)reason;
		out.streamBytes = (uint32_t)RandomStreamBytes;
		out.battle = (uint32_t)state.battle;
		out.effect = (uint32_t)state.effect;
		memcpy(out.stream, state.stream, RandomStreamBytes);

		bool ok;
		if (peer == (int)BroadcastPeer)
			ok = session->Broadcast(MessageRandomState, &out, (int)sizeof(out), SendReliable);
		else
			ok = session->Send((uint8_t)peer, MessageRandomState, &out, (int)sizeof(out),
			    SendReliable);

		if (ok)
		{
			++g_publishes;
			Log("minigame: published random state, reason %d, hash %08X", reason,
			    (unsigned)ffx::RandomStateHash());
		}
		return ok;
	}

	int MinigameHashRegions(DWORD* out, int maxRegions)
	{
		if (!out || maxRegions < kMinigameHashRegions || !g_installed)
			return 0;

		// The sub step count goes in because it scales every script timer in the
		// game, so a disagreement makes every minigame tick at a different rate.
		const int sub = SubStepCount();
		DWORD h = (DWORD)ffx::RandomStateHash();
		h ^= PackedFlags() * 2654435761u;
		h ^= (DWORD)((sub < 0 ? 0xFF : sub) + 1) * 40503u;
		out[0] = h;

		// Region 1, the overdrive minigames. Separate from region 0 so a mismatch
		// says which family drifted instead of just that something did.
		//
		// The timer trio is step derived (budget - stepCounter / fps), so it SHOULD
		// agree, which is what makes it worth hashing. Lulu's two keyboard latches are
		// in because they were the one genuinely wall-clock thing in the family until
		// StepClock took over their 0.5001 s window, and ThreadedPadMode is in because
		// Lulu's minigame sets it and it changes how often the pad ring advances.
		const DWORD overdrive[] = {
			ffx::Rva::BtlOdStepCounter,
			ffx::Rva::BtlOdTimeBudget,
			ffx::Rva::BtlOdTimeRemaining,
			ffx::Rva::BtlOdMinigamePhase,
			ffx::Rva::BtlOdLuluKeyA,
			ffx::Rva::BtlOdLuluKeyB,
		};
		DWORD o = 0x9E3779B9u;
		for (int i = 0; i < (int)(sizeof(overdrive) / sizeof(overdrive[0])); ++i)
			o = (o * 16777619u) ^ WordAt(overdrive[i]);
		o = (o * 16777619u) ^ (DWORD)ffx::ThreadedPadMode();
		out[1] = o;

		return kMinigameHashRegions;
	}

	int RandomStatePublishCount()
	{
		return g_publishes;
	}

	int RandomStateApplyCount()
	{
		return g_applies;
	}

	const char* MinigameSyncStatus()
	{
		if (!g_installed)
			return "off";
		_snprintf(g_status, sizeof(g_status) - 1,
		    "%s rng %08X sub %d pub %d app %d repeat %s", g_running ? "on" : "idle",
		    (unsigned)ffx::RandomStateHash(), SubStepCount(), g_publishes, g_applies,
		    ffx::StepClockInstalled() ? "step" : "clock");
		g_status[sizeof(g_status) - 1] = 0;
		return g_status;
	}

	void LogMinigameSync()
	{
		Log("minigame: %s", MinigameSyncStatus());
		ffx::LogRandomState();
		ffx::LogStepClock();
		Log("minigame: flags chocobo=%d thunder=%d nagi0700=%d waitGate=%d cleared=%d",
		    FlagPtr(ffx::Rva::ChocoboGameDebugEnable) ? *FlagPtr(ffx::Rva::ChocoboGameDebugEnable) : -1,
		    FlagPtr(ffx::Rva::ThunderPlainTreasureEnable) ? *FlagPtr(ffx::Rva::ThunderPlainTreasureEnable) : -1,
		    FlagPtr(ffx::Rva::FullNagi0700Enable) ? *FlagPtr(ffx::Rva::FullNagi0700Enable) : -1,
		    FlagPtr(ffx::Rva::ScriptWaitGate) ? *FlagPtr(ffx::Rva::ScriptWaitGate) : -1,
		    g_flagsCleared ? 1 : 0);
	}

}
