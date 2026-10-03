#include "world/EncounterSync.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Encounter.h"
#include "workshop/Log.h"
#include "world/TriggerPass.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		EncounterPolicy g_policy = EncounterPolicy::Max;
		bool g_installed = false;

		// Distance each remote player has walked that the engine has not been told about
		// yet. Consumed by the hook and zeroed, so a step that the engine never reaches the
		// check on does not silently throw the movement away, and a step it reaches twice
		// does not count it twice.
		float g_pending[MaxTriggerPlayers];

		// For the log, because "the hook ran and decided to change nothing" and "the hook
		// never ran" look identical from the outside otherwise.
		float g_lastEngine = 0.0f;
		float g_lastRemote = 0.0f;
		float g_lastResult = 0.0f;
		DWORD g_substitutions = 0;

		char g_status[192] = "encounter sync: not installed";

		float ConsumeRemote()
		{
			float maxOne = 0.0f;
			float sum = 0.0f;

			for (int i = 1; i < MaxTriggerPlayers; ++i)
			{
				const float d = g_pending[i];
				g_pending[i] = 0.0f;

				if (d > maxOne)
					maxOne = d;

				sum += d;
			}

			switch (g_policy)
			{
			case EncounterPolicy::Sum:
				return sum;
			case EncounterPolicy::Max:
				return maxOne;
			case EncounterPolicy::HostOnly:
			default:
				return 0.0f;
			}
		}

		float EncounterDistanceHook(float engineDistance)
		{
			const float remote = ConsumeRemote();

			float result = engineDistance;
			if (g_policy == EncounterPolicy::Sum)
			{
				result = engineDistance + remote;
			}
			else if (g_policy == EncounterPolicy::Max)
			{
				// The engine already accounts for the bound player, so Max means "take the
				// bigger of the two" rather than "add the bigger of the two".
				if (remote > engineDistance)
					result = remote;
			}

			g_lastEngine = engineDistance;
			g_lastRemote = remote;
			g_lastResult = result;
			if (result != engineDistance)
				++g_substitutions;

			return result;
		}

	} // namespace

	bool InstallEncounterSync()
	{
		if (g_installed)
			return true;

		memset(g_pending, 0, sizeof(g_pending));

		if (!SetEncounterDistanceHook(&EncounterDistanceHook))
		{
			strcpy_s(g_status, sizeof(g_status),
			    "encounter sync: the detour refused to install, encounters are untouched");
			Log("encounter sync: could not install, so a remote player's walking will not "
			    "count toward a battle. Nothing was patched.");
			return false;
		}

		g_installed = true;
		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "encounter sync: installed, policy %s",
		    EncounterPolicyName(g_policy));
		Log("encounter sync: installed on the encounter check, policy %s",
		    EncounterPolicyName(g_policy));
		return true;
	}

	bool EncounterSyncInstalled()
	{
		return g_installed;
	}

	void SetEncounterPolicy(EncounterPolicy policy)
	{
		g_policy = policy;
		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "encounter sync: %s, policy %s",
		    g_installed ? "installed" : "NOT installed", EncounterPolicyName(policy));
		Log("encounter sync: policy is now %s", EncounterPolicyName(policy));
	}

	EncounterPolicy CurrentEncounterPolicy()
	{
		return g_policy;
	}

	const char* EncounterPolicyName(EncounterPolicy policy)
	{
		switch (policy)
		{
		case EncounterPolicy::HostOnly:
			return "host only, remote walking does not count";
		case EncounterPolicy::Max:
			return "max, the party rolls once per 10 units whoever walked them";
		case EncounterPolicy::Sum:
			return "sum, every player's walking adds up";
		default:
			return "unknown";
		}
	}

	void ReportRemoteStepDistance(int slot, float distance)
	{
		if (slot <= 0 || slot >= MaxTriggerPlayers)
			return;
		if (distance <= 0.0f)
			return;

		// A teleport, a map change or a respawn produces one enormous step. Letting that
		// through would hand the engine hundreds of units at once and start a battle the
		// instant anybody loads in, so it is dropped rather than clamped. Clamping would
		// still award the cap, which is just a smaller version of the same wrong thing.
		const float TeleportThreshold = 50.0f;
		if (distance > TeleportThreshold)
			return;

		g_pending[slot] += distance;
	}

	const char* EncounterSyncStatus()
	{
		return g_status;
	}

	void LogEncounterSync()
	{
		Log("=== encounter sync ===");
		Log("%s", g_status);
		Log("policy %s", EncounterPolicyName(g_policy));

		if (!g_installed)
		{
			Log("not installed, so the engine is accumulating only the bound player's "
			    "distance exactly as the shipped game does");
			return;
		}

		EncounterHookStats stats;
		ReadEncounterHookStats(&stats);
		Log("hook consulted %u times, substituted a different distance %u of those",
		    (unsigned)stats.calls, (unsigned)g_substitutions);
		Log("last call: engine %.3f, remote %.3f, returned %.3f", g_lastEngine, g_lastRemote,
		    g_lastResult);

		float pendingTotal = 0.0f;
		for (int i = 1; i < MaxTriggerPlayers; ++i)
			pendingTotal += g_pending[i];

		Log("%.3f units of remote movement are waiting to be counted", pendingTotal);

		if (stats.calls == 0)
			Log("zero calls means the engine never reached the check, so no field map is "
			    "running. That is a different problem from the hook being wrong.");

		// The engine side of the same picture, so one keypress answers "why no battle".
		LogEncounterState();
	}

} // namespace pilgrimage
