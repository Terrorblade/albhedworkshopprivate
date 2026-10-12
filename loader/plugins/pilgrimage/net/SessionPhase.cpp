#include "net/SessionPhase.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Character.h"
#include "ffx/GameState.h"
#include "ffx/WorldState.h"
#include "net/NetLink.h"
#include "workshop/Log.h"
#include "workshop/Session.h"
#include "world/WorldSync.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// The last phase a line was written about, so a phase that holds for a minute
		// writes one line and not eighteen hundred.
		SessionPhase g_logged = PhaseOffline;
		bool g_everLogged = false;

		char g_reason[200] = "no session";

	} // namespace

	SessionPhase CurrentSessionPhase()
	{
		Session* session = ActiveSession();
		if (!session || session->State() != SessionActive)
			return PhaseOffline;

		// ---------------------------------------------------------------------------
		// A GAME, FIRST, because without one every other question is meaningless.
		//
		// GameLoaded is the honest test and not a map id comparison: the save block is
		// static .data, so it is readable and all zero at the title screen, and it
		// checks two things a zeroed block cannot fake. See ffx/GameState.h.
		//
		// For a CLIENT this is also the "has the host's world arrived" test, and that
		// is not a coincidence. A client at the title screen has a zeroed block, and
		// the only thing that puts a real one there is the host's snapshot.
		// ---------------------------------------------------------------------------
		if (!GameLoaded())
		{
			// A transfer already running means the host has committed and the bytes are
			// in flight, so this is Loading rather than Lobby even though there is no
			// game here yet. Worth separating: in Lobby there is nothing to wait for
			// but a human, and in Loading there is.
			return WorldSyncActive() ? PhaseLoading : PhaseLobby;
		}

		// A game, but the world has not been agreed yet. On the host WorldSyncReady is
		// true from its first active frame, so this only catches a client mid transfer.
		if (!WorldSyncReady())
			return PhaseLoading;

		// A game and an agreed world, but the map is still coming up. The CHR pool is
		// the thing that is missing, and it is missing for real frames during a load,
		// so anything that allocates has to wait for it.
		if (!PoolIsReady())
			return PhaseLoading;

		return PhasePlaying;
	}

	bool SessionIsPlaying()
	{
		return CurrentSessionPhase() == PhasePlaying;
	}

	const char* SessionPhaseName(SessionPhase phase)
	{
		switch (phase)
		{
			case PhaseLobby:
				return "lobby";
			case PhaseLoading:
				return "loading";
			case PhasePlaying:
				return "playing";
			case PhaseOffline:
			default:
				return "offline";
		}
	}

	const char* SessionPhaseReason()
	{
		Session* session = ActiveSession();
		if (!session || session->State() != SessionActive)
		{
			strcpy_s(g_reason, sizeof(g_reason), "no session");
			return g_reason;
		}

		const bool host = session->IsHost();

		// NAMES WHAT IS BEING WAITED FOR, not what state we are in. "lobby" tells a
		// player nothing. "waiting for the host to start a new game or load a save"
		// tells them whether to do something or sit still.
		if (!GameLoaded())
		{
			if (WorldSyncActive())
				strcpy_s(g_reason, sizeof(g_reason),
				    "the host's world is on its way over, hold on");
			else if (host && TitleScreenActive())
				strcpy_s(g_reason, sizeof(g_reason),
				    "connected, and you are sitting in the main menu. Start a new game or "
				    "load a save, and everyone else follows you into it.");
			else if (host)
				strcpy_s(g_reason, sizeof(g_reason),
				    "connected. Start a new game or load a save, and everyone else "
				    "follows you into it.");
			else
				strcpy_s(g_reason, sizeof(g_reason),
				    "connected. Waiting for the host to start a new game or load a save. "
				    "Do NOT start your own, you will be sent theirs.");
			return g_reason;
		}

		if (!WorldSyncReady())
		{
			strcpy_s(g_reason, sizeof(g_reason),
			    "a game is loaded here but the world has not been agreed yet");
			return g_reason;
		}

		if (!PoolIsReady())
		{
			strcpy_s(g_reason, sizeof(g_reason), "the map is still loading");
			return g_reason;
		}

		strcpy_s(g_reason, sizeof(g_reason), "playing");
		return g_reason;
	}

	void ServiceSessionPhase()
	{
		const SessionPhase phase = CurrentSessionPhase();
		if (g_everLogged && phase == g_logged)
			return;

		const SessionPhase was = g_logged;
		const bool first = !g_everLogged;
		g_logged = phase;
		g_everLogged = true;

		if (first)
			Log("session phase: %s. %s", SessionPhaseName(phase), SessionPhaseReason());
		else
			Log("session phase: %s -> %s. %s", SessionPhaseName(was),
			    SessionPhaseName(phase), SessionPhaseReason());
	}

	void ResetSessionPhase()
	{
		g_logged = PhaseOffline;
		g_everLogged = false;
		strcpy_s(g_reason, sizeof(g_reason), "no session");
	}

} // namespace pilgrimage
