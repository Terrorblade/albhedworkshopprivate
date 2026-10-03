#include "world/TriggerPass.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "clones/CloneRoster.h"
#include "ffx/Api.h"
#include "ffx/Atel.h"
#include "ffx/Character.h"
#include "workshop/Log.h"
#include "world/EncounterSync.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		struct TriggerPlayer
		{
			bool active;
			bool havePrevious;
			float current[3];
			float previous[3];
			float facing;
			bool examineRequested;
			int examineEventKind;
			int examineWinner; // what the last pass picked, kAtelActorIdNone for none
			int firedCount;    // how many events this player has committed, for the log
		};

		TriggerPlayer g_players[MaxTriggerPlayers];

		// Each player's own copy of the per-actor trigger bits. Four bytes per actor per player, and
		// the only reason a remote player's enter and leave edges are its own.
		DWORD g_shadow[MaxTriggerPlayers][MaxShadowActors];

		// The host's bits, lifted off the actors at the start of a pass and put back at the end. The
		// engine's own step left them there, so they are slot 0's state by construction.
		DWORD g_hostBits[MaxShadowActors];

		int g_lastGeneration = -1;
		int g_lastActorCount = -1;

		bool g_armed = false;
		bool g_followClone = false;

		// Set after a map change so the first pass on the new map adopts the engine's starting bits
		// instead of reporting every trigger the player already stands inside as a fresh enter.
		bool g_seedPending = true;

		int g_lastPassActors = 0;
		int g_lastPassPlayers = 0;
		char g_status[192] = "trigger pass: not run yet";

		// Actor ids are reused across maps, so a rebuilt pool makes every shadow bit meaningless.
		// The generation word plus the actor count is the cheapest signal that happened.
		bool WorldChanged(int actorCount)
		{
			const int generation = AtelGeneration();
			if (generation != g_lastGeneration || actorCount != g_lastActorCount)
			{
				g_lastGeneration = generation;
				g_lastActorCount = actorCount;
				return true;
			}

			return false;
		}

		void ForgetShadows()
		{
			memset(g_shadow, 0, sizeof(g_shadow));
			g_seedPending = true;

			for (int i = 0; i < MaxTriggerPlayers; ++i)
			{
				g_players[i].havePrevious = false;
				g_players[i].examineWinner = kAtelActorIdNone;
			}
		}

		// Give every player the engine's current view, so nobody sees a spurious edge on the first
		// step after a load. A player standing inside a box when the map comes up should be inside,
		// not entering.
		void SeedShadowsFromHost(int actorCount)
		{
			for (int p = 0; p < MaxTriggerPlayers; ++p)
			{
				for (int i = 0; i < actorCount; ++i)
					g_shadow[p][i] = g_hostBits[i];
			}

			g_seedPending = false;
		}

		// The engine's own commit, reproduced. FFX_Atel_StepFrame grants the talk bonus and then
		// fires the event, in that order, and gates the whole thing on a player being bound.
		void CommitExamine(TriggerPlayer& player, int slot)
		{
			const int winner = player.examineWinner;
			if (winner == kAtelActorIdNone)
				return;

			if (!g_armed)
			{
				Log("trigger pass: slot %d would examine actor %d, but the pass is not armed", slot, winner);
				return;
			}

			// The engine refuses a second script on a busy channel anyway, but saying so here is
			// more useful than a silent no-op when somebody is watching the log.
			if (ActorEventChannelBusy(winner, player.examineEventKind))
			{
				Log("trigger pass: slot %d picked actor %d but its channel is busy, so the engine would refuse", slot, winner);
				return;
			}

			GrantTalkBonusOnce(winner, player.examineEventKind);
			const int started = FireActorEvent(winner, player.examineEventKind);
			if (started)
				++player.firedCount;

			Log("trigger pass: slot %d examined actor %d, script %s", slot, winner, started ? "STARTED" : "refused");
		}

		// One player's full pass over every actor that has a trigger step.
		void RunOnePlayer(int slot, int actorCount, int dtMs)
		{
			TriggerPlayer& player = g_players[slot];

			if (!SetAtelPassPlayer(player.current, player.havePrevious ? player.previous : nullptr, player.facing))
			{
				// No bound player means no actor pointer to hand the steppers, which is the same
				// condition FFX_Atel_StepActor refuses on. Nothing to do and nothing wrong.
				return;
			}

			// The engine arms a scan only when nothing is blocking and the global permission is set.
			// A pass that ignored either would let a remote player examine during a sequence where
			// the host cannot.
			const bool mayExamine = player.examineRequested && InteractionBlockedKind() == 0 && ExamineAllowed();

			if (mayExamine)
				ArmAtelPassExamine(player.examineEventKind);
			else
				ClearAtelPassExamine();

			for (int id = 0; id < actorCount; ++id)
			{
				if (ActorTriggerStepKind(id) == kAtelTriggerStepNone)
					continue;

				// Swap in this player's remembered edges, step, and take them back out. Without
				// this the actor only ever remembers one player and every edge is wrong.
				SetActorTriggerState(id, g_shadow[slot][id]);
				StepActorTriggers(id, dtMs);

				const DWORD after = ActorTriggerState(id);
				if (after != 0xFFFFFFFFu)
					g_shadow[slot][id] = after;
			}

			player.examineWinner = mayExamine ? AtelPassExamineWinner() : kAtelActorIdNone;
			player.examineRequested = false;

			CommitExamine(player, slot);
		}

		// Keep slot 1 pinned to the active clone. This is how the pass gets tested without a second
		// machine: spawn a clone, walk it into a trigger, and watch what fires.
		void FollowCloneIntoSlot1()
		{
			Character* clone = ActiveClone();
			if (!IsLive(clone) || !Game.GetPos)
			{
				ClearTriggerPlayer(1);
				return;
			}

			float pos[3] = { 0.0f, 0.0f, 0.0f };
			if (!Game.GetPos(clone, &pos[0], &pos[1], &pos[2]))
			{
				ClearTriggerPlayer(1);
				return;
			}

			// The clone's facing, so its examine cone points where it is looking rather than always
			// north. This is a plain field read, there is no bound accessor for it.
			SetTriggerPlayer(1, pos, CharacterFacing(clone));
		}

	} // namespace

	void ResetTriggerPass()
	{
		memset(g_players, 0, sizeof(g_players));
		ForgetShadows();

		g_lastGeneration = -1;
		g_lastActorCount = -1;
		g_lastPassActors = 0;
		g_lastPassPlayers = 0;

		for (int i = 0; i < MaxTriggerPlayers; ++i)
			g_players[i].examineWinner = kAtelActorIdNone;

		strcpy_s(g_status, sizeof(g_status), "trigger pass: reset");
	}

	void SetTriggerPlayer(int slot, const float* pos, float facing)
	{
		if (slot <= 0 || slot >= MaxTriggerPlayers || !pos)
			return;

		TriggerPlayer& player = g_players[slot];

		if (player.active)
		{
			// Last step's current becomes this step's previous, which is what the swept line
			// crossing test needs. Reading it off the character instead would give the engine's
			// own idea of previous, which for a clone is not maintained.
			memcpy(player.previous, player.current, sizeof(player.previous));
			player.havePrevious = true;

			// How far this player moved, which is what the encounter accumulator wants. The
			// engine measures the bound player the same way, as a 3D Euclidean distance
			// between the two halves of its own position cache, so this matches rather than
			// approximates it.
			const float dx = pos[0] - player.current[0];
			const float dy = pos[1] - player.current[1];
			const float dz = pos[2] - player.current[2];
			const float moved = sqrtf(dx * dx + dy * dy + dz * dz);
			ReportRemoteStepDistance(slot, moved);
		}
		else
		{
			memcpy(player.previous, pos, sizeof(player.previous));
			player.havePrevious = false;
		}

		memcpy(player.current, pos, sizeof(player.current));
		player.facing = facing;
		player.active = true;
	}

	void ClearTriggerPlayer(int slot)
	{
		if (slot <= 0 || slot >= MaxTriggerPlayers)
			return;

		g_players[slot].active = false;
		g_players[slot].havePrevious = false;
		g_players[slot].examineWinner = kAtelActorIdNone;
		g_players[slot].examineRequested = false;
	}

	void RequestTriggerExamine(int slot, int eventKind)
	{
		if (slot <= 0 || slot >= MaxTriggerPlayers)
			return;
		if (eventKind != kAtelEventExamine && eventKind != kAtelEventExamineAlt)
			return;

		g_players[slot].examineRequested = true;
		g_players[slot].examineEventKind = eventKind;
	}

	void RunRemoteTriggerPass(int dtMs)
	{
		if (g_followClone)
			FollowCloneIntoSlot1();

		if (!AtelReady())
			return;

		const int actorCount = ActorCount();
		if (actorCount <= 0)
			return;

		if (actorCount > MaxShadowActors)
		{
			// Refusing is right. Truncating would silently stop giving the far actors their own
			// edges, which would look like an intermittent bug rather than a capacity problem.
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "trigger pass: REFUSED, %d actors exceeds the %d the shadow table holds", actorCount, MaxShadowActors);
			return;
		}

		if (WorldChanged(actorCount))
			ForgetShadows();

		int wanted = 0;
		for (int i = 1; i < MaxTriggerPlayers; ++i)
		{
			if (g_players[i].active)
				++wanted;
		}

		// Lift the host's bits even with nobody else active, because that is also how the seed
		// stays current, and it costs one read per actor.
		for (int id = 0; id < actorCount; ++id)
			g_hostBits[id] = ActorTriggerState(id);

		if (g_seedPending)
			SeedShadowsFromHost(actorCount);

		g_lastPassActors = actorCount;
		g_lastPassPlayers = wanted;

		if (wanted == 0)
		{
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "trigger pass: idle, %d actors, no remote players", actorCount);
			return;
		}

		AtelPassState saved;
		if (!SaveAtelPass(&saved))
		{
			strcpy_s(g_status, sizeof(g_status), "trigger pass: could not save the context, so nothing ran");
			return;
		}

		for (int slot = 1; slot < MaxTriggerPlayers; ++slot)
		{
			if (g_players[slot].active)
				RunOnePlayer(slot, actorCount, dtMs);
		}

		// Put the host's edges back before anything else can read them. The engine's next step
		// has to see its own view, not the last remote player's.
		for (int id = 0; id < actorCount; ++id)
			SetActorTriggerState(id, g_hostBits[id]);

		RestoreAtelPass(&saved);

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "trigger pass: %d actors, %d remote, %s", actorCount, wanted, g_armed ? "ARMED" : "observing");
	}

	void SetTriggerPassArmed(bool armed)
	{
		g_armed = armed;
		Log("trigger pass: %s", armed ? "ARMED, a remote player's examine will now fire for real" : "observing only, nothing will be committed");
	}

	bool TriggerPassArmed()
	{
		return g_armed;
	}

	void SetTriggerPassFollowClone(bool follow)
	{
		g_followClone = follow;
		if (!follow)
			ClearTriggerPlayer(1);

		Log("trigger pass: slot 1 %s", follow ? "now follows the active clone" : "no longer follows the clone");
	}

	bool TriggerPassFollowClone()
	{
		return g_followClone;
	}

	const char* TriggerPassStatus()
	{
		return g_status;
	}

	void LogTriggerPass()
	{
		Log("=== trigger pass ===");
		Log("%s", g_status);
		Log("armed %d, follow clone %d, atel generation %d, last pass %d actors %d remote",
		    g_armed ? 1 : 0, g_followClone ? 1 : 0, AtelGeneration(), g_lastPassActors, g_lastPassPlayers);
		Log("engine gates: interaction blocked kind %d, examine allowed %d, bound player actor %d",
		    InteractionBlockedKind(), ExamineAllowed() ? 1 : 0, BoundPlayerActorId());

		for (int i = 1; i < MaxTriggerPlayers; ++i)
		{
			const TriggerPlayer& p = g_players[i];
			if (!p.active)
			{
				Log("  slot %d: inactive", i);
				continue;
			}

			Log("  slot %d: at %.1f %.1f %.1f facing %.2f, previous %s, last winner %d, fired %d",
			    i, p.current[0], p.current[1], p.current[2], p.facing,
			    p.havePrevious ? "tracked" : "not yet", p.examineWinner, p.firedCount);
		}

		// Worth printing because it is the number that decides whether this is cheap. Four bytes
		// an actor a player, and nothing is patched.
		Log("shadow cost: %d bytes for %d players over %d actors",
		    (int)sizeof(g_shadow), MaxTriggerPlayers, MaxShadowActors);
	}

} // namespace pilgrimage
