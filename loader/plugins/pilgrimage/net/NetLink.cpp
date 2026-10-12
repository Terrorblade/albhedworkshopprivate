#include "net/NetLink.h"

#include "coop/Ownership.h"

#include "ffx/GameState.h"
#include "net/LockstepLink.h"
#include "net/SessionPhase.h"
#include "battle/BattleSync.h"
#include "menu/CoopConfig.h"
#include "menu/MenuSync.h"
#include "menu/PauseSync.h"
#include "world/BoosterSync.h"
#include "world/DialogueSync.h"
#include "world/FmvSync.h"
#include "world/HeldObjects.h"
#include "world/WorldSync.h"

#include <stdio.h>
#include <string.h>

#include "ModState.h"
#include "workshop/Log.h"
#include "workshop/Session.h"
#include "workshop/Settings.h"
#include "workshop/SteamTransport.h"
#include "ffx/MainLoop.h"
#include "workshop/SteamUser.h"
#include "workshop/UdpTransport.h"

namespace pilgrimage
{

	using namespace workshop;

	namespace
	{

		// Both backends are built, and which one runs is a setting rather than a build
		// option, so switching is a click instead of a rebuild. Steam is the shipping
		// path. UDP loopback is how the layers above get tested without needing a second
		// Steam account and a second machine.
		//
		// Static rather than allocated, so there is nothing to leak and nothing to
		// null-check on the frame path. Both exist, only one is ever started.
		UdpTransport udpLink;
		SteamTransport steamLink;
		Transport* active = NULL;

		Session session;
		char summary[200] = "networking off";

		// Changes whenever the mod is rebuilt, which is exactly what the handshake wants:
		// two machines on different builds should refuse each other rather than desync in
		// a way nobody can explain. Hashing the compiler's own date and time strings is
		// enough and costs nothing at runtime.
		uint32_t BuildId()
		{
			const char* stamp = __DATE__ " " __TIME__;
			uint32_t hash = 2166136261u;
			for (const char* p = stamp; *p; ++p)
			{
				hash ^= (unsigned char)*p;
				hash *= 16777619u;
			}
			return hash;
		}

		// The joiner binds 0 so the OS picks a free port. Two instances on one machine
		// therefore never collide, and the host learns the joiner's address from its
		// first message rather than needing it configured. Steam needs no equivalent.
		const unsigned short JoinerPort = 0;

		bool UsingSteam()
		{
			return settings.useSteam != 0;
		}

		// The address a UDP join will dial, never empty. An empty setting means the
		// file was hand edited, and falling back to the default beats dialling "".
		const char* HostAddress()
		{
			return settings.hostAddress[0] ? settings.hostAddress : "127.0.0.1";
		}

		// Is that address this machine. Deliberately a text match on the two spellings
		// that actually turn up rather than a routing table lookup: the point is to
		// warn a human who did not realise, not to be exhaustive.
		bool HostAddressIsThisMachine()
		{
			const char* a = HostAddress();
			return strcmp(a, "127.0.0.1") == 0 || strcmp(a, "localhost") == 0
			    || strcmp(a, "::1") == 0;
		}

		void RefreshSharedHash()
		{
			// Rechecked every frame rather than only at startup, because a shared setting
			// can be changed from the control panel while a session is up, and noticing
			// exactly that is the point of the hash.
			session.SetSharedSettingsHash(SharedSettingsHash());
		}

		// Brings up whichever transport the setting asks for. acceptUnknown is the host's
		// mode in both backends: a sender we have not seen becomes a peer.
		Transport* StartTransport(bool acceptUnknown)
		{
			if (UsingSteam())
			{
				if (!steamLink.Start(acceptUnknown))
					return NULL;
				return &steamLink;
			}
			if (!udpLink.Start(acceptUnknown ? (unsigned short)settings.hostPort : JoinerPort,
			        acceptUnknown))
				return NULL;
			return &udpLink;
		}

	} // namespace

	bool StartHosting()
	{
		if (NetworkingActive())
		{
			Log("net: a session is already open, so hosting was not started. "
			    "Disconnect first with shift+F3.");
			SetStatus("ALREADY IN A SESSION, not hosting. shift+F3 first.");
			return false;
		}

		active = StartTransport(true);
		if (!active)
			return false;

		RefreshSharedHash();
		if (!session.Start(active, RoleHost, BuildId(), "host"))
		{
			active->Stop();
			active = NULL;
			return false;
		}

		if (UsingSteam())
		{
			Log("net: hosting over Steam, build %08X. Clients need your SteamID, which "
			    "is the 17 digit number on your Steam profile page.",
			    BuildId());
			SetStatus("HOSTING over Steam. They need your SteamID %s. They press Join, "
			          "not Host.",
			    telemetry.localSteamId[0] ? telemetry.localSteamId : "(not signed in yet)");
		}
		else
		{
			Log("net: hosting over UDP on port %ld, build %08X",
			    settings.hostPort, BuildId());
			SetStatus("HOSTING over UDP on port %ld. They need this machine's address "
			          "and that port. They press Join, not Host.",
			    settings.hostPort);
		}
		return true;
	}

	bool StartJoining()
	{
		if (NetworkingActive())
		{
			// NOT NECESSARILY CONNECTED. This only says the transport is open, which it
			// is from the moment a join starts, long before the host has answered. The
			// old wording here read "already connected", which told someone whose join
			// was stuck that they were connected when they plainly were not.
			Log("net: a session is already open, so joining was not started. It may "
			    "still be waiting for the host to answer. Disconnect first with "
			    "shift+F3 to start over.");

			// ON SCREEN, not only in the log. This refusal used to be log only, and two
			// people hit it: one of them was hosting, pressed Join twice, saw absolutely
			// nothing happen either time, and had no way to find out why. Exactly one of
			// you hosts and the other joins.
			SetStatus("ALREADY IN A SESSION, not joining. If you pressed Host, that IS "
			          "the session. Only one of you hosts. shift+F3 first.");
			return false;
		}

		// Check the thing we need before opening anything, so a missing SteamID does
		// not leave a socket or a Steam session behind.
		if (UsingSteam() && !settings.hostSteamId[0])
		{
			Log("net: no host SteamID set. Put the host's 17 digit SteamID in the "
			    "control panel before joining.");
			SetStatus("NO HOST STEAMID. Put the host's 17 digit id in the field below "
			          "before joining.");
			return false;
		}

		active = StartTransport(false);
		if (!active)
			return false;

		int peer = InvalidPeer;
		if (UsingSteam())
		{
			peer = steamLink.AddPeerFromText(settings.hostSteamId);
		}
		else
		{
			peer = udpLink.AddPeer(HostAddress(), (unsigned short)settings.hostPort);
		}
		if (peer == InvalidPeer)
		{
			Log("net: could not add the host as a peer, so there is nobody to join");
			SetStatus("COULD NOT ADD THE HOST. Check the %s in the field below parses.",
			    UsingSteam() ? "SteamID" : "address");
			active->Stop();
			active = NULL;
			return false;
		}

		RefreshSharedHash();
		if (!session.Start(active, RoleClient, BuildId(), "client"))
		{
			active->Stop();
			active = NULL;
			return false;
		}

		if (UsingSteam())
		{
			Log("net: joining SteamID %s, build %08X", settings.hostSteamId, BuildId());
			SetStatus("JOINING SteamID %s. Waiting for them to answer.",
			    settings.hostSteamId);
		}
		else
		{
			Log("net: joining %s:%ld from local port %u, build %08X", HostAddress(),
			    settings.hostPort, udpLink.LocalPort(), BuildId());

			// SAY IT, because it is invisible otherwise and it is the whole reason a
			// UDP attempt between two machines silently sends into nothing.
			if (HostAddressIsThisMachine())
			{
				Log("net: %s IS THIS MACHINE. A UDP join to it only reaches another copy "
				    "of the game on this same PC. If the host is a different computer, "
				    "put their address in net.host_address, or use Steam P2P, which "
				    "needs no address at all.",
				    HostAddress());
				SetStatus("JOINING %s:%ld, WHICH IS THIS PC. Only reaches another copy "
				          "on this machine. Use Steam, or set the host's address.",
				    HostAddress(), settings.hostPort);
			}
			else
			{
				SetStatus("JOINING %s:%ld over UDP. Waiting for them to answer.",
				    HostAddress(), settings.hostPort);
			}
		}
		return true;
	}

	void StopNetworking()
	{
		if (session.State() == SessionIdle && !active)
			return;

		EndLockstep(); // before the goodbye, so nothing is left holding the game

		// Every subsystem, explicitly, here. StepNetworking's "was active, now is not"
		// branch never runs on this path: that function returns early once the state is
		// Idle and session.Stop() below makes it Idle in the same frame. So this is the
		// only place these get stopped when the user disconnects on purpose, and the
		// list has to stay in step with the one in StepNetworking.
		//
		// For the pause hold that is not a tidiness point. A hold reason left raised has
		// ServiceLockstep re-asserting the stall byte every frame with no clock left to
		// ever lower it, which is the game frozen for good. StopPauseSync drops it, and
		// StopMenuSync hands the local stick back to the sphere grid.
		//
		// Menu and pause go LAST for that reason, after anything that might still raise
		// a hold.
		ResetSessionPhase();
		StopWorldSync();
		StopBoosterSync();
		StopBattleSync();
		StopDialogueSync();

		// The FMV barrier has to be let go here or a script parked waiting for a peer
		// that is no longer there never unparks, because the only thing that releases
		// it is a command from that peer. StopFmvSync is what lets the wait through.
		StopFmvSync();

		// Only the command half stops. The carry census keeps running with no session,
		// because it is read-only and is the thing that proves a carried object is a
		// bone parent, which is worth being able to check solo.
		StopHeldObjects();

		// NOT StopCoopConfig. Those rows are settings and they stay available with no
		// session, so that someone can set up who plays whom before a join and so this
		// is testable alone. Retiring them here would take the screen away the moment a
		// link dropped, which is exactly when a player wants to look at it.
		StopMenuSync();
		StopPauseSync();

		session.Stop(); // sends a goodbye first, so the other end does not wait
		if (active)
		{
			active->Stop();
			active = NULL;
		}
		strcpy_s(summary, sizeof(summary), "networking off");
	}

	bool NetworkingActive()
	{
		const SessionState state = session.State();
		return state == SessionListening || state == SessionJoining || state == SessionActive;
	}

	void PublishLocalIdentity()
	{
		// Already answered. An identity does not change while the game runs, so one
		// successful read is the whole job.
		if (telemetry.localSteamId[0])
			return;

		if (!SteamLoggedOn())
		{
			telemetry.steamLoggedOn = 0;
			return;
		}
		telemetry.steamLoggedOn = 1;

		LocalSteamIdText(telemetry.localSteamId, sizeof(telemetry.localSteamId));
		LocalPersonaName(telemetry.personaName, sizeof(telemetry.personaName));

		if (telemetry.localSteamId[0])
			Log("steam: signed in as '%s', SteamID %s. That number is what a joiner "
			    "needs.",
			    telemetry.personaName, telemetry.localSteamId);
	}

	void StepNetworking(LONG frame)
	{
		// ---------------------------------------------------------------------------
		// A TEST SETTING, so a boot test can exercise the host path without a keypress.
		//
		// Hosting is the only thing that registers the Steam callbacks, and those are
		// the one place the kit hands Steam an object to call back into. An automated
		// boot test that never hosts never touches that code, which is how a host that
		// could not be joined shipped: the transport looked fine because nothing in the
		// test ever asked it to be joined.
		//
		// Put net.host_on_boot = 1 in pilgrimage_together.txt and this presses F2 for
		// you. A setting and not an environment variable on purpose, because testing
		// Steam at all means launching through Steam, and Steam starts the game with
		// its own environment rather than the one the test runner had.
		//
		// Retried every two seconds rather than attempted once, because Steam comes up
		// on the game's schedule and measured about ten seconds after boot here, so a
		// single early attempt just logs "Steam is not up yet" and gives up. Hosting
		// alone with no joiner is harmless, so this is safe to leave on in a test loop.
		// ---------------------------------------------------------------------------
		static bool hostOnBootAnnounced = false;
		static bool hostOnBootDone = false;
		if (settings.hostOnBoot != 0 && !hostOnBootDone)
		{
			if (!hostOnBootAnnounced)
			{
				hostOnBootAnnounced = true;
				Log("net: net.host_on_boot is set, so hosting will start on its own "
				    "shortly. That is a test setting, not something a player does.");
			}
			if (frame > 60 && (frame % 60) == 0 && StartHosting())
				hostOnBootDone = true;
		}

		if (session.State() == SessionIdle)
			return;

		RefreshSharedHash();

		// The game's own simulation step counter, g_ffxMainStepCounter. This used to
		// be the render frame count as a stand-in, which was wrong in the way that
		// matters: a dropped or repeated presented frame must not move the
		// simulation clock. The engine turns out to keep exactly the counter we
		// need, incremented once per FFX_MainStep, covering field and battle, so
		// lockstep can use it rather than inventing one.
		//
		// Read only. Three double-buffer parity sites in the game test its low bit,
		// so writing it would corrupt rendering.
		DWORD step = 0;
		if (ffx::MainStepCounter(&step))
			session.SetLocalStep((uint32_t)step);
		else
			// Before FFX_MainInit the counter is not there yet. The render frame is a
			// fine stand-in during startup, where nothing is simulating anyway.
			session.SetLocalStep((uint32_t)frame);

		const SessionState before = session.State();
		session.Step();
		const SessionState after = session.State();

		// World sync comes up first, so a joining client's request for the world is on
		// the wire before anything else happens.
		if (after == SessionActive && before != SessionActive)
		{
			// ---------------------------------------------------------------------------
			// OWNERSHIP IS THE HOST'S, so only a client throws its table away here.
			//
			// This used to reset on both machines, which quietly destroyed the one
			// workflow the Config rows exist for. A host sets up who plays whom before
			// anybody joins, somebody joins, and the join wiped it. The note in
			// menu/CoopConfig.cpp promising those bindings would "already be in the
			// table when the join lands" was describing something that did not happen.
			//
			// A client still clears, for the reason the old comment gave, which is still
			// right for a client: a second session must not inherit the first one's
			// bindings. It loses nothing by clearing, because the host publishes the
			// whole table as kCommandCharOwnerTable as soon as this machine's clock is
			// running and able to receive a command at all.
			//
			// The host's table is therefore sticky across sessions. That is deliberate:
			// it is the host's configuration, and "peer 1 plays Yuna" is a setting, not
			// a fact about one particular guest. A host who wants it different changes
			// it on the Config screen.
			// ---------------------------------------------------------------------------
			if (session.IsHost())
				Log("ownership: this is the host, so its table stands as the authority "
				    "for the session and goes out to everybody else. %s",
				    OwnershipStatus());
			else
				ResetOwnership();

			// So the first phase line of this session gets written. Without it a second
			// session that opens in the same phase the last one closed in says nothing,
			// and the log loses the one line that explains what everyone is waiting for.
			ResetSessionPhase();

			StartWorldSync();
			StartBoosterSync();
			StartBattleSync();
			StartDialogueSync();
			StartFmvSync();
			StartHeldObjects();
			StartMenuSync();
			StartPauseSync();

			// After StartMenuSync, because the Config rows read MenuDriver to work out
			// which machine should send an ask and MenuControlOverrideHeld to show the
			// override row's state. Both come from MenuSync.
			StartCoopConfig();
		}
		if (after != SessionActive && before == SessionActive)
		{
			EndLockstep();
			ResetSessionPhase();
			StopWorldSync();
			StopBoosterSync();
			StopBattleSync();
			StopDialogueSync();

			// Same reason as in StopNetworking: a parked movie wait is released by a
			// peer's command and there are no peers left to send one.
			StopFmvSync();

			StopHeldObjects();

			// NOT StopCoopConfig, same reason as in StopNetworking. The rows are
			// settings and they outlive the session.

			// Menu and pause last, because StopPauseSync is what drops the hold reason
			// and nothing after it should be able to raise one again. Leaving the game
			// frozen because a link went away mid-pause is the worst outcome here.
			StopMenuSync();
			StopPauseSync();
		}

		// The clock runs only while there is a session to run it against, AND only once
		// both ends are simulating the same world. On the host that is immediately. On a
		// client it is after the snapshot has been installed, which is why this is a
		// per-frame check and not a one-shot on the transition: the clock starts on
		// whichever frame the world turns up.
		//
		// THIS GOES BEFORE ServiceWorldSync, deliberately. The host stamps its snapshot
		// with clock->CurrentStep(), and that is the number the joiner starts its own
		// clock at, so a snapshot taken while the host's clock was still unstarted would
		// carry step 0 and seed the joiner at 1 against a host in the thousands. The
		// host's WorldSyncReady is true from its first active frame, so ordering the
		// start first means the clock is always up before anything can read a step off
		// it. On a client this costs one frame, because the frame the install finishes
		// on had WorldSyncReady false at the top, and one frame is free here.
		// The phase, before anything reads it. Logs only on a change, so this is one
		// line per transition and not one per frame.
		if (after == SessionActive)
			ServiceSessionPhase();

		// ---------------------------------------------------------------------------
		// AND A GAME HAS TO EXIST, which used to go without saying and then stopped.
		//
		// Allowing people to connect from the TITLE SCREEN broke the old condition
		// silently. On the host WorldSyncReady is true from its first active frame,
		// whether or not there is a game, so a host sitting at the main menu started a
		// lockstep clock over an empty world, captured a save block that was all
		// zeroes, and sent those zeroes to the joiner, which transplanted them over its
		// own. Nothing refused, nothing logged.
		//
		// GameLoaded is the honest test rather than a map id comparison: the save block
		// is static .data, readable and all zero before a game exists, so it checks two
		// things a zeroed block cannot fake. See ffx/GameState.h.
		//
		// NOT gated on the full PhasePlaying, and this is deliberate. Playing also
		// requires the CHR pool, which on a joiner is not up until its map has
		// finished loading, and the host's transfer hold is released the moment the
		// snapshot is INSTALLED rather than when the map comes up. So waiting for the
		// pool would start the joiner's clock several hundred steps behind a host that
		// had already resumed. The clock wants the earliest honest moment, which is a
		// real world installed, and the pool is the body layer's problem.
		// ---------------------------------------------------------------------------
		if (after == SessionActive && !LockstepRunning() && WorldSyncReady()
		    && ffx::GameLoaded())
			BeginLockstep();

		// Pace the transfer and, on a client, install a completed one.
		if (after == SessionActive)
			ServiceWorldSync();

		// The session line, then the link quality underneath it when Steam can tell
		// us. GetP2PSessionState is documented as debug-only, so it goes on screen
		// and never into a decision.
		const int used = (int)strlen(session.Summary(summary, sizeof(summary)));
		if (active == &steamLink && session.State() == SessionActive)
		{
			char link[80];
			for (int i = 0; i < MaxPlayers; ++i)
			{
				const PeerInfo* peer = session.Peer(i);
				if (!peer || !peer->active || peer->isLocal)
					continue;
				if (steamLink.DescribeLink(i, link, sizeof(link)))
					_snprintf_s(summary + used, sizeof(summary) - used, _TRUNCATE,
					    "  [%s]", link);
				break;
			}
		}
	}

	const char* NetworkingSummary()
	{
		return summary;
	}

	workshop::Session* ActiveSession()
	{
		return (session.State() == SessionIdle) ? NULL : &session;
	}

	const char* NetworkingBackendName()
	{
		// Not "udp loopback" any more. It was loopback only because the address was
		// hardcoded, and that is a setting now, so saying loopback here would be
		// telling somebody who set a real address that it cannot work.
		if (UsingSteam())
			return "steam";
		return HostAddressIsThisMachine() ? "udp on this PC only" : "udp";
	}

} // namespace pilgrimage
