#include "net/NetLink.h"

#include "net/LockstepLink.h"
#include "battle/BattleSync.h"
#include "menu/CoopConfig.h"
#include "menu/MenuSync.h"
#include "menu/PauseSync.h"
#include "world/BoosterSync.h"
#include "world/DialogueSync.h"
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
			Log("net: already connected, disconnect first");
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
			Log("net: hosting over Steam, build %08X. Clients need your SteamID, which "
			    "is the 17 digit number on your Steam profile page.",
			    BuildId());
		else
			Log("net: hosting over UDP on port %ld, build %08X",
			    settings.hostPort, BuildId());
		return true;
	}

	bool StartJoining()
	{
		if (NetworkingActive())
		{
			Log("net: already connected, disconnect first");
			return false;
		}

		// Check the thing we need before opening anything, so a missing SteamID does
		// not leave a socket or a Steam session behind.
		if (UsingSteam() && !settings.hostSteamId[0])
		{
			Log("net: no host SteamID set. Put the host's 17 digit SteamID in the "
			    "control panel before joining.");
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
			peer = udpLink.AddPeer("127.0.0.1", (unsigned short)settings.hostPort);
		}
		if (peer == InvalidPeer)
		{
			Log("net: could not add the host as a peer, so there is nobody to join");
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
			Log("net: joining SteamID %s, build %08X", settings.hostSteamId, BuildId());
		else
			Log("net: joining 127.0.0.1:%ld from local port %u, build %08X",
			    settings.hostPort, udpLink.LocalPort(), BuildId());
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
		StopWorldSync();
		StopBoosterSync();
		StopBattleSync();
		StopDialogueSync();

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
			StartWorldSync();
			StartBoosterSync();
			StartBattleSync();
			StartDialogueSync();
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
			StopWorldSync();
			StopBoosterSync();
			StopBattleSync();
			StopDialogueSync();

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
		if (after == SessionActive && !LockstepRunning() && WorldSyncReady())
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
		return UsingSteam() ? "steam" : "udp loopback";
	}

} // namespace pilgrimage
