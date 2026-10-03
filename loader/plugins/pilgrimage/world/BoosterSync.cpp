#include "world/BoosterSync.h"

#include <stdio.h>
#include <string.h>

#include "ffx/EscMenu.h"
#include "ffx/MainLoop.h"
#include "net/Commands.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "workshop/Session.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		bool g_started = false;

		// What the two machines have AGREED on, which is not the same thing as what the
		// globals currently hold. The difference between those two is the whole mechanism:
		// a local keypress moves the globals, this does not move until the host says so,
		// and StepBoosterSync pushes the globals back until it does.
		BoosterCommand g_agreed;
		bool g_haveAgreed = false;

		// The last values we asked the host about, so one keypress produces one request
		// rather than one per step until the answer arrives.
		BoosterCommand g_asked;
		bool g_askPending = false;

		// Which peers were reachable last step, as a bit per peer. The host pushes
		// whenever this changes, which is what gets the values to a joiner.
		//
		// Driven off the peer set rather than done once at startup because a push needs the
		// lockstep clock, and the clock is started AFTER this file is told the session went
		// active, in the same frame. A single push at startup therefore fails silently,
		// which is exactly the kind of bug that only shows up as "the other player had
		// encounters off and I did not".
		uint8_t g_lastReachable = 0;

		LONG g_corrections = 0; // how many times a local change was held back
		char g_status[200] = "boosters: not syncing";

		bool ReadLive(BoosterCommand* out)
		{
			memset(out, 0, sizeof(*out));

			// Every one of these can fail before the engine has set the globals up, and a
			// partial read would be worse than no read: pushing three real values and one
			// zero would turn invincibility off on the other machine for no reason.
			int speed = 0, rate = 0, invincible = 0, autoBattle = 0;
			if (!BoosterSpeedIndex(&speed))
				return false;
			if (!BoosterEncounterRate(&rate))
				return false;
			if (!BoosterInvincible(&invincible))
				return false;
			if (!BoosterAutoBattle(&autoBattle))
				return false;

			out->speedIndex = (int32_t)speed;
			out->encounterRate = (int32_t)rate;
			out->invincible = (int32_t)invincible;
			out->autoBattle = (int32_t)autoBattle;
			return true;
		}

		bool Same(const BoosterCommand& a, const BoosterCommand& b)
		{
			return a.speedIndex == b.speedIndex && a.encounterRate == b.encounterRate &&
			       a.invincible == b.invincible && a.autoBattle == b.autoBattle;
		}

		// Writes all four, and reports whether anything actually moved so the log can say
		// "corrected" rather than printing every step.
		bool ApplyValues(const BoosterCommand& want)
		{
			BoosterCommand live;
			if (!ReadLive(&live))
				return false;

			if (Same(live, want))
				return false;

			// Encounter rate first, because it is the one that moves the RNG and therefore
			// the one worth having right even if a later write fails.
			if (live.encounterRate != want.encounterRate)
				SetBoosterEncounterRate((int)want.encounterRate);
			if (live.speedIndex != want.speedIndex)
				SetBoosterSpeedIndex((int)want.speedIndex);
			if (live.invincible != want.invincible)
				SetBoosterInvincible((int)want.invincible);
			if (live.autoBattle != want.autoBattle)
				SetBoosterAutoBattle((int)want.autoBattle);

			return true;
		}

		uint8_t ReachableMask(Session* session)
		{
			uint8_t mask = 0;
			for (int peer = 0; peer < MaxPlayers; ++peer)
				if (session->PeerReachable((uint8_t)peer))
					mask |= (uint8_t)(1u << peer);

			return mask;
		}

		void Describe(const BoosterCommand& b, char* out, int bytes)
		{
			_snprintf_s(out, (size_t)bytes, _TRUNCATE, "speed %d, encounters %d, invincible "
			                                           "%d, auto battle %d",
			    (int)b.speedIndex, (int)b.encounterRate, (int)b.invincible,
			    (int)b.autoBattle);
		}

	} // namespace

	void StartBoosterSync()
	{
		g_started = true;
		g_askPending = false;
		g_corrections = 0;
		g_lastReachable = 0;

		// Take the local values as the starting point. On the host that is the truth and
		// PushBoosters sends it. On a client it is a placeholder that the host's first
		// command replaces, and until then the client is no worse off than it was before
		// this file existed.
		g_haveAgreed = ReadLive(&g_agreed);

		if (!g_haveAgreed)
		{
			strcpy_s(g_status, sizeof(g_status),
			    "boosters: the globals are not readable yet");
			return;
		}

		char line[120];
		Describe(g_agreed, line, sizeof(line));

		Session* session = ActiveSession();
		if (session && session->IsHost())
		{
			// Not pushed here. The clock does not exist yet at this point in the frame, so
			// a push would fail. StepBoosterSync does it when it sees a peer turn up.
			Log("boosters: hosting with %s. That goes out as soon as somebody is reachable.",
			    line);
		}
		else
		{
			Log("boosters: joined with %s locally, waiting for the host to say what they "
			    "actually are",
			    line);
		}

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "boosters: %s", line);
	}

	void StopBoosterSync()
	{
		g_started = false;
		g_haveAgreed = false;
		g_askPending = false;
		strcpy_s(g_status, sizeof(g_status), "boosters: not syncing");
	}

	bool BoosterSyncActive()
	{
		return g_started;
	}

	bool PushBoosters()
	{
		Lockstep* clock = ActiveLockstep();
		Session* session = ActiveSession();
		if (!clock || !session || !session->IsHost())
			return false;

		BoosterCommand live;
		if (!ReadLive(&live))
			return false;

		return clock->RequestCommand((uint8_t)kCommandBoosters, &live, (int)sizeof(live));
	}

	void StepBoosterSync()
	{
		if (!g_started)
			return;

		Lockstep* clock = ActiveLockstep();
		if (!clock)
			return;

		Session* session = ActiveSession();
		if (!session)
			return;

		// 0. Somebody turned up, or went away. On the host that is the cue to say what the
		//    settings are, because a joiner has no other way to find out: these four are
		//    runtime globals and the world transfer does not carry them.
		const uint8_t reachable = ReachableMask(session);
		if (reachable != g_lastReachable)
		{
			g_lastReachable = reachable;
			if (session->IsHost() && reachable != 0)
			{
				if (PushBoosters())
					Log("boosters: the peer set changed, pushing the current settings");
			}
		}

		// 1. Anything the host ordered for THIS step. Done before the local comparison so a
		//    command that just landed becomes the agreed value immediately, rather than
		//    looking like a local change for one step and triggering a pointless request.
		const Command* commands[8];
		const int count = clock->CommandsForStep(commands, 8);
		for (int i = 0; i < count; ++i)
		{
			if (commands[i]->kind != (uint8_t)kCommandBoosters)
				continue;
			if (commands[i]->length < (int)sizeof(BoosterCommand))
				continue;

			BoosterCommand ordered;
			memcpy(&ordered, commands[i]->data, sizeof(ordered));

			const bool changed = !g_haveAgreed || !Same(ordered, g_agreed);
			g_agreed = ordered;
			g_haveAgreed = true;
			g_askPending = false;

			if (changed)
			{
				char line[120];
				Describe(g_agreed, line, sizeof(line));
				Log("boosters: peer %u set %s, applied on step %u",
				    (unsigned)commands[i]->issuer, line, (unsigned)commands[i]->step);
				_snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "boosters: %s", line);
			}
		}

		if (!g_haveAgreed)
		{
			// Still waiting to be told. Try again to read a baseline, because the globals
			// may not have existed when the session started.
			g_haveAgreed = ReadLive(&g_agreed);
			return;
		}

		// 2. Did somebody press a booster key on this machine.
		BoosterCommand live;
		if (!ReadLive(&live))
			return;

		if (Same(live, g_agreed))
		{
			g_askPending = false;
			return;
		}

		// 3. Ask for it, once. RequestCommand on the host issues immediately and on a
		//    client sends an ask, so the same call is right either way and this file does
		//    not need to know which it is.
		if (!g_askPending || !Same(live, g_asked))
		{
			char line[120];
			Describe(live, line, sizeof(line));

			if (clock->RequestCommand((uint8_t)kCommandBoosters, &live, (int)sizeof(live)))
			{
				g_asked = live;
				g_askPending = true;
				Log("boosters: asking for %s", line);
			}
		}

		// 4. And hold the globals at the agreed values until the answer comes back.
		//
		//    This is the part that makes the mechanism safe rather than merely hopeful.
		//    Without it the pressing machine would run under the new rules for the few
		//    steps the request is in flight, and for the encounter rate specifically those
		//    few steps are enough to draw from the RNG stream that the other machine does
		//    not. The press still takes effect, a moment later, on both machines at once.
		if (ApplyValues(g_agreed))
			InterlockedIncrement(&g_corrections);
	}

	const char* BoosterSyncStatus()
	{
		return g_status;
	}

	void LogBoosterSync()
	{
		Log("=== boosters ===");
		Log("%s", g_status);

		if (!g_started)
		{
			Log("not syncing, so each machine is on its own settings. That is fine alone "
			    "and is a divergence in a session.");
			return;
		}

		BoosterCommand live;
		char line[120];

		if (ReadLive(&live))
		{
			Describe(live, line, sizeof(line));
			Log("live:   %s", line);
		}
		else
		{
			Log("live:   not readable");
		}

		if (g_haveAgreed)
		{
			Describe(g_agreed, line, sizeof(line));
			Log("agreed: %s", line);
		}
		else
		{
			Log("agreed: nothing yet, the host has not said");
		}

		Log("%ld local changes held back so far%s", g_corrections,
		    g_askPending ? ", and a request is in flight" : "");
		Log("none of these four live in the save block, so a world transfer does not carry "
		    "them and this is the only thing keeping them together");
	}

} // namespace pilgrimage
