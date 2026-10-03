#include "diag/InteractProbe.h"

#include <stdio.h>
#include <string.h>

#include "clones/CloneRoster.h"
#include "ffx/Api.h"
#include "ffx/Atel.h"
#include "ffx/Character.h"
#include "workshop/Log.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// How far to look. A chest's own examine radius is small, but the probe wants to
		// SEE things that are a bit too far as well, so the log can say "there is a chest
		// 4 units away and you are not close enough" rather than "nothing found".
		const float LookRadius = 600.0f;

		const int MaxFound = 24;

		char status[160] = "interact probe: not used yet";

		const char* EventKindName(int kind)
		{
			switch (kind)
			{
			case kAtelEventExamine:
				return "examine";
			case kAtelEventExamineAlt:
				return "examine alt";
			case kAtelEventCollide:
				return "collide";
			case kAtelEventProximity:
				return "proximity";
			case kAtelEventTouch:
				return "touch";
			case kAtelEventEnter:
				return "enter";
			case kAtelEventLeave:
				return "leave";
			case kAtelEventUnreachable:
				return "unreachable";
			default:
				return "?";
			}
		}

		const char* ActorKindName(int kind)
		{
			switch (kind)
			{
			case 1:
				return "character";
			case 2:
				return "line trigger";
			case 3:
				return "box trigger";
			case 4:
				return "kind 4, never stepped";
			case 5:
				return "path trigger";
			case 6:
				return "volume trigger";
			default:
				return "unknown kind";
			}
		}

		// Where to probe from. The active clone when there is one, because firing from a
		// second character's position is the entire point of the experiment, and the
		// player otherwise so the probe is still useful with nothing spawned.
		//
		// Returns false and says why when neither is available.
		bool ProbeOrigin(float* xyz, const char** whoOut)
		{
			Character* clone = ActiveClone();
			if (IsLive(clone) && Game.GetPos)
			{
				if (Game.GetPos(clone, &xyz[0], &xyz[1], &xyz[2]))
				{
					if (whoOut)
						*whoOut = "the active clone";
					return true;
				}
			}

			Character* player = LivePlayerCharacter();
			if (player && Game.GetPos)
			{
				if (Game.GetPos(player, &xyz[0], &xyz[1], &xyz[2]))
				{
					if (whoOut)
						*whoOut = "the player";
					return true;
				}
			}

			if (whoOut)
				*whoOut = "nobody";
			return false;
		}

		// Why an examine would do nothing on this actor, in the engine's own terms. Empty
		// string when it would work.
		const char* WhyNot(int actorId)
		{
			static char reason[96];
			reason[0] = 0;

			const int kind = ActorKind(actorId);
			if (EventChannel(kind, kAtelEventExamine) == kAtelChannelNone)
			{
				_snprintf_s(reason, sizeof(reason), _TRUNCATE,
				    "actor kind %d has no examine channel", kind);
				return reason;
			}
			if (EventScriptEntry(actorId, kAtelEventExamine) == kAtelScriptEntryNone)
			{
				strcpy_s(reason, sizeof(reason), "no script entry for examine");
				return reason;
			}
			const BYTE mask = ActorEventMask(actorId);
			if ((mask & (1u << kAtelEventExamine)) == 0)
			{
				_snprintf_s(reason, sizeof(reason), _TRUNCATE,
				    "examine bit clear in the actor's enable mask 0x%02X", mask);
				return reason;
			}
			return reason;
		}

		float DistanceTo(int actorId, const float* from)
		{
			float pos[3];
			if (!ActorPosition(actorId, pos))
				return -1.0f;
			const float dx = pos[0] - from[0];
			const float dy = pos[1] - from[1];
			const float dz = pos[2] - from[2];
			const float d2 = dx * dx + dy * dy + dz * dz;

			// No need for a square root to rank things, but the log is for a human, so
			// one per actor is worth it.
			if (d2 <= 0.0f)
				return 0.0f;
			float guess = d2;
			for (int i = 0; i < 12; ++i)
				guess = 0.5f * (guess + d2 / guess);
			return guess;
		}

	} // namespace

	void LookAtInteractables()
	{
		if (!AtelReady())
		{
			Log("interact probe: no atel world yet, so load a save and stand on a map");
			strcpy_s(status, sizeof(status), "interact probe: no atel world");
			return;
		}

		float origin[3] = { 0.0f, 0.0f, 0.0f };
		const char* who = "nobody";
		if (!ProbeOrigin(origin, &who))
		{
			Log("interact probe: nothing to probe from, no clone and no player");
			strcpy_s(status, sizeof(status), "interact probe: no origin");
			return;
		}

		int ids[MaxFound];
		const int found = FindActorsNear(origin, LookRadius, ids, MaxFound);

		Log("=== interactables within %.0f of %s at %.1f %.1f %.1f ===",
		    LookRadius, who, origin[0], origin[1], origin[2]);
		Log("%d actors live, bound player actor is %d, interaction blocked kind %d",
		    ActorCount(), BoundPlayerActorId(), InteractionBlockedKind());

		if (found == 0)
		{
			Log("  nothing in range. Either there is genuinely nothing here, or the "
			    "radius is too small for this map's scale.");
			strcpy_s(status, sizeof(status), "interact probe: nothing in range");
			return;
		}

		int eligible = 0;
		for (int i = 0; i < found; ++i)
		{
			const int id = ids[i];
			const char* reason = WhyNot(id);
			const bool can = (reason[0] == 0);
			if (can)
				++eligible;

			Log("  actor %-5d %-16s dist %7.1f  examine: %s%s%s",
			    id, ActorKindName(ActorKind(id)), DistanceTo(id, origin),
			    can ? "YES" : "no",
			    can ? (ActorEventChannelBusy(id, kAtelEventExamine)
			                  ? ", but a script is already running on its channel"
			                  : "")
			        : ", ",
			    can ? "" : reason);
		}

		// The engine's own single-slot answer, for comparison. If it disagrees with
		// the probe that is interesting rather than wrong: the engine applies a
		// facing cone and a per-kind radius that the probe deliberately does not.
		const int engineChoice = NearestExaminableActorId();
		Log("  the engine's own arbitration currently points at actor %d", engineChoice);

		Log("%d of %d would respond to an examine. Press the fire key to try the "
		    "nearest one.",
		    eligible, found);

		_snprintf_s(status, sizeof(status), _TRUNCATE,
		    "interact probe: %d near %s, %d eligible", found, who, eligible);
	}

	void FireNearestExamine()
	{
		if (!AtelReady())
		{
			Log("interact probe: no atel world yet");
			strcpy_s(status, sizeof(status), "interact probe: no atel world");
			return;
		}

		const int blocked = InteractionBlockedKind();
		if (blocked != 0)
		{
			// Firing into an open dialogue is something the engine's own path cannot
			// do, so doing it here would be testing a case that never happens.
			Log("interact probe: a message window is up (blocked kind %d), so not "
			    "firing. Close it and try again.",
			    blocked);
			strcpy_s(status, sizeof(status), "interact probe: dialogue is open");
			return;
		}

		float origin[3] = { 0.0f, 0.0f, 0.0f };
		const char* who = "nobody";
		if (!ProbeOrigin(origin, &who))
		{
			Log("interact probe: nothing to probe from");
			strcpy_s(status, sizeof(status), "interact probe: no origin");
			return;
		}

		int ids[MaxFound];
		const int found =
		    FindActorsNearForEvent(origin, LookRadius, kAtelEventExamine, ids, MaxFound);

		if (found == 0)
		{
			Log("interact probe: nothing near %s would respond to an examine. Run the "
			    "look key first to see what is around and why each one refused.",
			    who);
			strcpy_s(status, sizeof(status), "interact probe: nothing eligible");
			return;
		}

		// Nearest first, so index 0 is the one a player would expect.
		const int target = ids[0];

		if (ActorEventChannelBusy(target, kAtelEventExamine))
		{
			Log("interact probe: actor %d already has a script running on its examine "
			    "channel, so the engine would refuse this anyway. That refusal is the "
			    "reason a duplicated network command cannot double grant an item.",
			    target);
			_snprintf_s(status, sizeof(status), _TRUNCATE,
			    "interact probe: actor %d busy", target);
			return;
		}

		Log("=== firing examine on actor %d (%s) from %s, %.1f away ===",
		    target, ActorKindName(ActorKind(target)), who, DistanceTo(target, origin));
		Log("The bound player actor is %d, so if this works it worked WITHOUT being "
		    "the bound player, which is the whole question.",
		    BoundPlayerActorId());

		const int started = FireActorEvent(target, kAtelEventExamine);

		if (started)
		{
			Log("interact probe: the engine started a script. Look at the screen: a "
			    "chest should be opening, or an NPC talking.");
			_snprintf_s(status, sizeof(status), _TRUNCATE,
			    "interact probe: fired on %d, script STARTED", target);
		}
		else
		{
			// A zero return means one of the three gates refused after all, or the
			// thread start itself refused. ActorCanFireEvent said yes, so a refusal
			// here is genuinely informative and worth reporting as such.
			Log("interact probe: the engine refused to start anything, even though all "
			    "three gates said yes. That is worth knowing: the refusal is inside "
			    "the thread start, not in the event tables.");
			_snprintf_s(status, sizeof(status), _TRUNCATE,
			    "interact probe: fired on %d, REFUSED", target);
		}
	}

	const char* InteractProbeStatus()
	{
		return status;
	}

} // namespace pilgrimage
