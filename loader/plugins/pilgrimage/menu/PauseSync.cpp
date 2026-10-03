#include "menu/PauseSync.h"

#include <stdio.h>
#include <string.h>

#include "coop/Ownership.h"
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

		// A bit per peer. The local bit is re-read from the engine every frame, the
		// remote bits are set by an ordered command and cleared by watching that peer's
		// input start flowing again. See the header for why those two halves are
		// different mechanisms.
		int g_pausedMask = 0;

		// What this machine last told everybody about itself, so one Esc keypress
		// produces one command rather than one per frame.
		bool g_announcedPaused = false;
		bool g_haveAnnounced = false;

		// The peer's input lead at the moment their pause landed. When it climbs past
		// this, their FFX_MainStep is running again.
		int g_leadAtPause[MaxPlayers];

		// Are we the ones holding the game for a remote pause. Tracked so the reason is
		// raised and dropped exactly once and never left up.
		bool g_holding = false;

		LONG g_localPauses = 0;
		LONG g_remotePauses = 0;
		LONG g_sendFailures = 0;

		char g_status[220] = "pause: not syncing";

		int LocalPeerId()
		{
			Session* session = ActiveSession();
			if (!session)
				return -1;

			const uint8_t local = session->LocalPeer();
			return (local == PeerUnassigned) ? -1 : (int)local;
		}

		// A player's name and peer id, for the log and the readout. Over the internet a
		// held or overridden machine has to be told WHO did it by name, or it reads as a
		// dropped connection or a broken mod.
		//
		// Three rotating buffers rather than one static, because two PeerLabel calls in
		// one Log would otherwise both point at the same bytes and print the same name,
		// which turns "the driver changed from A to B" into "from B to B". Three is
		// enough for every call site here and the rotation is per call, not per peer.
		const char* PeerLabel(int peer)
		{
			static char labels[3][48];
			static int next = 0;

			char* label = labels[next];
			next = (next + 1) % 3;

			Session* session = ActiveSession();
			const PeerInfo* info = (session && peer >= 0) ? session->Peer(peer) : NULL;

			if (info && info->name[0])
				_snprintf_s(label, 48, _TRUNCATE, "%s (peer %d)", info->name, peer);
			else
				_snprintf_s(label, 48, _TRUNCATE, "peer %d", peer);
			return label;
		}

		int RemoteMask()
		{
			const int local = LocalPeerId();
			if (local < 0)
				return g_pausedMask;
			return g_pausedMask & ~(1 << local);
		}

		void RefreshStatus()
		{
			const int remote = RemoteMask();

			if (remote != 0)
			{
				for (int peer = 0; peer < MaxPlayers; ++peer)
				{
					if ((remote & (1 << peer)) == 0)
						continue;
					_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
					    "pause: HELD, %s has the pause menu open", PeerLabel(peer));
					return;
				}
			}

			if (EscMenuIsOpen())
			{
				_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
				    "pause: your pause menu is open, so the others are held too");
				return;
			}

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "pause: running, %ld local and %ld remote pauses so far", g_localPauses,
			    g_remotePauses);
		}

		// Tell everybody what this machine is doing. On the host RequestCommand issues
		// immediately and on a client it sends an ask, so the same call is right either
		// way.
		void Announce(bool paused)
		{
			Lockstep* clock = ActiveLockstep();
			if (!clock)
			{
				// No clock means no session worth announcing to, or a session that has not
				// started stepping yet. Either way nothing is waiting on us.
				g_announcedPaused = paused;
				g_haveAnnounced = true;
				return;
			}

			PauseCommand payload;
			memset(&payload, 0, sizeof(payload));
			payload.paused = (uint8_t)(paused ? 1 : 0);

			if (!clock->RequestCommand((uint8_t)kCommandPause, &payload, (int)sizeof(payload)))
			{
				InterlockedIncrement(&g_sendFailures);

				// Not retried on the next frame, because a retry loop would fire a command
				// per frame for as long as the link is down. The consequence of losing a
				// PAUSE announcement is that the other machine falls back to the gate
				// stall it would have had anyway, and the consequence of losing a RESUME
				// is nothing at all, because the release is driven by watching input flow
				// rather than by this message.
				Log("pause: could not announce %s. The other machine will fall back to the "
				    "plain lockstep stall, which freezes it without saying why.",
				    paused ? "a pause" : "a resume");
			}

			g_announcedPaused = paused;
			g_haveAnnounced = true;
		}

	} // namespace

	void StartPauseSync()
	{
		g_started = true;
		g_pausedMask = 0;
		g_haveAnnounced = false;
		g_announcedPaused = false;
		g_localPauses = 0;
		g_remotePauses = 0;
		g_sendFailures = 0;

		for (int i = 0; i < MaxPlayers; ++i)
			g_leadAtPause[i] = 0;

		if (g_holding)
		{
			ReleaseSimulationFor(kHoldPauseMenu);
			g_holding = false;
		}

		strcpy_s(g_status, sizeof(g_status), "pause: running");
		Log("pause: syncing. One player opening the Esc menu now holds everybody, and the "
		    "held machines are told who did it.");
	}

	void StopPauseSync()
	{
		g_started = false;
		g_pausedMask = 0;

		// Never leave the game frozen because the link went away while somebody was
		// paused. This is the one thing in this file that absolutely must not be missed.
		if (g_holding)
		{
			ReleaseSimulationFor(kHoldPauseMenu);
			g_holding = false;
		}

		strcpy_s(g_status, sizeof(g_status), "pause: not syncing");
	}

	bool PauseSyncActive()
	{
		return g_started;
	}

	void ApplyPauseCommands()
	{
		if (!g_started)
			return;

		Lockstep* clock = ActiveLockstep();
		if (!clock)
			return;

		const Command* commands[4];
		const int count = clock->CommandsForStep(commands, 4);

		for (int i = 0; i < count; ++i)
		{
			if (commands[i]->kind != (uint8_t)kCommandPause)
				continue;
			if (commands[i]->length < (int)sizeof(PauseCommand))
			{
				Log("pause: a command from peer %u is %d bytes and the payload is %d, so "
				    "the two builds do not agree on the protocol",
				    (unsigned)commands[i]->issuer, commands[i]->length,
				    (int)sizeof(PauseCommand));
				continue;
			}

			PauseCommand payload;
			memcpy(&payload, commands[i]->data, sizeof(payload));

			const int peer = (int)commands[i]->issuer;
			if (peer < 0 || peer >= MaxPlayers)
				continue;

			// Our own announcement coming back. The local bit is owned by the engine
			// read in StepPauseSync, so ignore it here rather than have two writers of
			// one bit.
			if (peer == LocalPeerId())
				continue;

			const int bit = 1 << peer;
			const bool wasPaused = (g_pausedMask & bit) != 0;

			if (payload.paused != 0)
			{
				g_pausedMask |= bit;

				if (!wasPaused)
				{
					// The baseline for the release watch, captured once. The gate asks
					// for the same step again after refusing it, so re-capturing every
					// call would keep moving the thing we are comparing against.
					g_leadAtPause[peer] = clock->PeerInputLead((uint8_t)peer);

					InterlockedIncrement(&g_remotePauses);
					Log("pause: %s opened their pause menu on step %u. YOUR GAME IS HELD "
					    "until they close it. This is not a dropped connection.",
					    PeerLabel(peer), (unsigned)commands[i]->step);
				}
			}
			else
			{
				// A resume that actually reached a step. That only happens when nobody
				// was held, which is the case where the pause and the resume both
				// happened inside the command lead. Handled rather than assumed, because
				// otherwise the bit would be left set and only the input watch would
				// clear it.
				g_pausedMask &= ~bit;
				if (wasPaused)
					Log("pause: %s closed their pause menu on step %u", PeerLabel(peer),
					    (unsigned)commands[i]->step);
			}
		}
	}

	void StepPauseSync()
	{
		if (!g_started)
		{
			if (g_holding)
			{
				ReleaseSimulationFor(kHoldPauseMenu);
				g_holding = false;
			}
			return;
		}

		const int local = LocalPeerId();
		Lockstep* clock = ActiveLockstep();

		// 1. Our own pause, straight out of the engine. Read every frame rather than
		//    edge-tracked into a variable, so there is no state to get stuck: if the Esc
		//    menu is shut, this bit is clear, full stop.
		const bool localPaused = EscMenuIsOpen();
		if (local >= 0)
		{
			const int bit = 1 << local;
			if (localPaused)
				g_pausedMask |= bit;
			else
				g_pausedMask &= ~bit;
		}

		// 2. Announce a change, once. The ask goes out even while our own simulation is
		//    stopped, because the session pump lives in the animate hook and the animate
		//    hook still runs when the original animate has skipped the step loop.
		if (!g_haveAnnounced || localPaused != g_announcedPaused)
		{
			if (localPaused)
			{
				InterlockedIncrement(&g_localPauses);
				Log("pause: you opened the pause menu, holding the other machines too");
			}
			else if (g_haveAnnounced)
			{
				Log("pause: you closed the pause menu, letting everybody go");
			}
			Announce(localPaused);
		}

		// 3. Drop a remote peer's bit when there is real evidence their simulation is
		//    running again, or when they have gone. No timer, see the header.
		if (clock)
		{
			for (int peer = 0; peer < MaxPlayers; ++peer)
			{
				if (peer == local)
					continue;

				const int bit = 1 << peer;
				if ((g_pausedMask & bit) == 0)
					continue;

				if (!PeerInSession(peer))
				{
					g_pausedMask &= ~bit;
					Log("pause: %s left while paused, so the hold is being dropped",
					    PeerLabel(peer));
					continue;
				}

				// PeerInputLead is (their highest stamped step) minus (our current step).
				// While they are paused neither half moves, so any increase is them
				// stepping again. This is the release, and it is an observation rather
				// than a message, which is what makes it immune to the fact that a
				// command cannot be consumed while every clock is stopped.
				const int lead = clock->PeerInputLead((uint8_t)peer);
				if (lead > g_leadAtPause[peer])
				{
					g_pausedMask &= ~bit;
					Log("pause: %s is simulating again, releasing the hold", PeerLabel(peer));
				}
			}
		}
		else
		{
			// No clock, so no peer can be stepping and no release can be observed.
			// Holding the game in that state would freeze it with nothing able to lift
			// the freeze, so the remote bits go.
			g_pausedMask = localPaused && local >= 0 ? (1 << local) : 0;
		}

		// 4. Raise or drop the hold, which is the only thing this function actually
		//    changes about the game.
		//
		//    ONLY FOR A REMOTE PAUSE. Our own Esc menu is already holding us through the
		//    engine's own byte, which animate re-latches from the menu state every frame,
		//    and adding a second writer of the same byte for the same condition would
		//    mean our release racing the engine's latch for no benefit. The one owner of
		//    that byte is ServiceLockstep and the reason bitmask is how anything asks it.
		const bool wantHold = RemoteMask() != 0;
		if (wantHold != g_holding)
		{
			g_holding = wantHold;
			if (wantHold)
				HoldSimulationFor(kHoldPauseMenu);
			else
				ReleaseSimulationFor(kHoldPauseMenu);
		}

		RefreshStatus();
	}

	bool AnyonePaused()
	{
		return g_pausedMask != 0;
	}

	int PausedPeerMask()
	{
		return g_pausedMask;
	}

	const char* PauseSyncStatus()
	{
		return g_status;
	}

	void LogPauseSync()
	{
		Log("=== pause ===");
		Log("%s", g_status);

		if (!g_started)
		{
			Log("not syncing, so one player opening the Esc menu stops only their own "
			    "machine. With the lockstep gate enforcing that starves the other one "
			    "about 100 ms later with no explanation, and with the gate only measuring "
			    "it lets the other one run away alone, which is worse.");
			return;
		}

		Log("esc menu here: %s, paused peer mask 0x%X, holding for a remote pause: %s",
		    EscMenuIsOpen() ? "OPEN" : "shut", (unsigned)g_pausedMask,
		    g_holding ? "yes" : "no");

		Lockstep* clock = ActiveLockstep();
		if (clock)
		{
			for (int peer = 0; peer < MaxPlayers; ++peer)
			{
				if ((g_pausedMask & (1 << peer)) == 0)
					continue;
				Log("  %s: lead %d now, %d when they paused", PeerLabel(peer),
				    clock->PeerInputLead((uint8_t)peer), g_leadAtPause[peer]);
			}
		}
		else
		{
			Log("no lockstep clock, so nothing can be observed resuming and the remote "
			    "bits are not trusted");
		}

		Log("%ld local pauses, %ld remote pauses, %ld announcements that could not be sent",
		    g_localPauses, g_remotePauses, g_sendFailures);
		Log("the raise is an ordered command and the release is a frame-path observation. "
		    "That asymmetry is deliberate: a resume delivered as a command could never be "
		    "consumed, because consuming one needs a step and every clock is stopped.");
	}

} // namespace pilgrimage
