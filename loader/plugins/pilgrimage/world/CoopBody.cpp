#include "world/CoopBody.h"

#include <stdio.h>
#include <string.h>

#include "ModState.h"
#include "clones/CloneRoster.h"
#include "clones/CloneSpawner.h"
#include "ffx/Atel.h"
#include "ffx/Camera.h"
#include "ffx/Character.h"
#include "ffx/GameState.h"
#include "ffx/WorldState.h"
#include "ffx/Layout.h"
#include "net/Commands.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "world/WorldSync.h"
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "workshop/Session.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// How often to ask again when this machine still has no body. 60 steps is
		// about two seconds, which is slow enough not to flood the command channel
		// and quick enough that nobody is standing there wondering.
		const uint32_t kRetryEverySteps = 60;

		// The same two one-liners every module in here keeps, because ActiveSession can
		// hand back NULL at any point and a -1 peer has to be a value rather than a
		// crash.
		int LocalPeerId()
		{
			Session* session = ActiveSession();
			if (!session)
				return -1;

			return (int)session->LocalPeer();
		}

		bool IsSessionHost()
		{
			Session* session = ActiveSession();
			return session && session->IsHost();
		}

		// How many times to ask before giving up and saying so once. A spawn that
		// fails ten times in a row is failing for a reason that asking again will not
		// fix, most likely a full CHR pool, and twenty seconds of trying is enough to
		// prove that.
		const int kMaxAttempts = 10;

		bool g_started = false;

		// The step we last asked on, and how many times. Reset on a new session and
		// whenever a body actually appears.
		uint32_t g_lastAskStep = 0;
		bool g_everAsked = false;
		int g_attempts = 0;
		bool g_gaveUp = false;

		// Counters for the panel and the log.
		volatile LONG g_bodiesSpawned = 0;
		volatile LONG g_spawnsRefused = 0;
		volatile LONG g_spawnsIgnored = 0;

		char g_status[160] = "co-op bodies: off";

		// ---------------------------------------------------------------------------
		// THE MAP THE BODIES BELONG TO, and this is not bookkeeping, it is the thing
		// that stops a player driving a shopkeeper.
		//
		// A body is a CHR pool slot, and the pool is freed and reallocated on every map
		// transition. The roster already drops an entry whose character is no longer
		// live, which sounds like enough and is not: by the time anybody looks, the
		// NEXT map has filled that slot with one of its own NPCs, and that NPC is
		// perfectly live. So the liveness test passes, the peer stays bound, and their
		// input starts driving whatever the new map put there.
		//
		// A map id is the honest test. When it changes, every body is gone by
		// definition, whatever the pool happens to look like.
		//
		// -1 means "not read yet", which is different from any real map id.
		int g_bodyMapId = -1;

		int LiveMapId()
		{
			// NO GAME MEANS NO MAP, said here because WorldLocation::valid does not
			// mean that. It means the fields were read, and the save block is readable
			// from process start, so at the title screen this comes back valid with
			// every field zero. Without this the whole lobby reads as "we are all
			// standing together on map 0" and the first real map looks like a map
			// change, which drops bindings that were never made.
			if (!ffx::GameLoaded())
				return -1;

			ffx::WorldLocation where;
			if (!ffx::ReadWorldLocation(&where) || !where.valid)
				return -1;
			return where.mapId;
		}

		// Drop every binding when the map underneath them changed. Runs on the STEP
		// path, not the frame path, so both machines do it on the same step and neither
		// is briefly holding a body the other has already let go of.
		void ForgetBodiesOnMapChange()
		{
			const int map = LiveMapId();
			if (map < 0)
				return; // between maps, so there is nothing to compare against yet

			if (map == g_bodyMapId)
				return;

			const int had = PeerBodyCount();
			const int was = g_bodyMapId;
			g_bodyMapId = map;

			if (was < 0 || had == 0)
				return; // first map of the session, or nothing to drop

			ClearPeerBodies();

			// The attempt budget is about THIS map, so a new map gets a fresh one. A
			// player who could not get a body in Besaid should not be refused one in
			// Luca because of it.
			g_attempts = 0;
			g_gaveUp = false;
			g_everAsked = false;

			Log("co-op body: the map changed from %d to %d, so all %d body bindings are "
			    "dropped. The characters behind them went with the old map's CHR pool, "
			    "and a pool slot gets reused, so holding on to one would mean somebody "
			    "driving whichever NPC the new map put in that slot. Everybody asks for "
			    "a new body.",
			    was, map, had);
		}

		// ---------------------------------------------------------------------------
		// IS THERE A GAME TO PUT A BODY IN, asked properly rather than inferred.
		//
		// This used to be implicit: AskForBody looked for the party leader and gave up
		// quietly when there was not one. That is the correct behaviour and a terrible
		// explanation. Somebody connected at the title screen got no body, no body
		// spawn in the log, and no line anywhere saying why, which reads exactly like
		// the feature being broken.
		//
		// Three separate things have to be true and they fail at different moments:
		//
		//   GameLoaded    there is a save block with a real character in it. False at
		//                 the title screen, and false for a client that has connected
		//                 but not yet been sent the host's world.
		//   PoolIsReady   the CHR pool exists, so FFX_Ch_Allocate has somewhere to
		//                 allocate from. False between maps.
		//   PartyLeader   the leader's actor has been authored into this map. False for
		//                 the first frames of a map load, after the other two are true.
		//
		// Checking all three and saying which one is missing turns "nothing happened"
		// into "waiting for the host to load a game", which is the difference between a
		// bug report and a player waiting patiently.
		// ---------------------------------------------------------------------------
		const char* WhyNotPlayable()
		{
			if (!ffx::GameLoaded())
				return "no game is loaded yet";

			// AND BOTH MACHINES HAVE TO AGREE ON WHICH GAME. This one is easy to leave
			// out, because StepCoopBodies is called BEFORE the clock-running test in
			// OnStep, deliberately, so that a body can be spawned solo for testing with
			// no second machine involved. The price of that choice is no clock gate
			// underneath, so the world test has to be made here.
			//
			// It bites hardest in exactly the case this all exists for. A joining client
			// transplants the host's block, so GameLoaded above goes true immediately,
			// several hundred steps before its own map has come up and the arrival has
			// settled. Spawning into that window puts a body in the OLD map's CHR pool,
			// which the map load then throws away.
			if (!WorldSyncReady())
				return "the world has not been agreed with the host yet";

			if (!PoolIsReady())
				return "the CHR pool is not up, so this is between maps";
			return NULL;
		}

		// ---------------------------------------------------------------------------
		// WHERE A BODY GOES, and the answer has to be the same on both machines.
		//
		// The PARTY LEADER, which is active party slot 0, not "the player". Those are
		// different characters here on purpose: each machine has already pointed
		// g_ffxControlledChr at whatever body its own peer drives, so asking the
		// engine for "the player" would give two different answers and put the body
		// in two different places.
		//
		// The leader is the same character on both machines as long as the worlds
		// agree, which the world sync guarantees before the clock starts, and it is
		// the natural place to appear anyway since that is where the party is.
		// ---------------------------------------------------------------------------
		Character* PartyLeader()
		{
			const int charIndex = (int)ffx::ActivePartyMember(0);
			if (charIndex < 0 || charIndex > 7)
				return NULL;
			return (Character*)ffx::ChrForPartyCharacter(charIndex);
		}

		// ---------------------------------------------------------------------------
		// Asking for a body.
		// ---------------------------------------------------------------------------
		void AskForBody(Lockstep* clock, int local)
		{
			Character* leader = PartyLeader();
			if (!leader)
			{
				// No leader means no map, or a map that has not finished authoring its
				// actors. Not an error, just not yet, so this does not count as an
				// attempt. The retry timer is left alone so the next step tries again.
				return;
			}

			float x = 0.0f, y = 0.0f, z = 0.0f;
			Game.GetPos(leader, &x, &y, &z);
			const float yaw = FloatAt(leader, Chr::Facing);

			// Beside the leader rather than inside them, fanned out by peer so two
			// joiners do not land in the same spot. The same arithmetic runs on both
			// machines but the RESULT travels in the command, so there is no chance of
			// the two disagreeing about it.
			const float offset = settings.spawnOffset * (float)(local + 1);

			SpawnBodyCommand payload;
			memset(&payload, 0, sizeof(payload));
			payload.peer = (uint8_t)local;
			payload.chrId = (uint16_t)SelectedChrId();
			payload.x = x + offset;
			payload.y = y;
			payload.z = z;
			payload.yaw = yaw;

			if (!clock->RequestCommand((uint8_t)kCommandSpawnBody, &payload, (int)sizeof(payload)))
			{
				// The session cannot carry it, so nothing happened anywhere. Try again
				// on the next tick rather than counting it against the attempt budget,
				// since this is a transport problem and not a spawn problem.
				Log("co-op body: could not ask for a body for peer %d, the session would "
				    "not take the command. Trying again in %u steps.",
				    local, (unsigned)kRetryEverySteps);
				return;
			}

			++g_attempts;
			g_lastAskStep = clock->CurrentStep();
			g_everAsked = true;

			Log("co-op body: asked for peer %d to get %s at (%.1f %.1f %.1f) yaw %.3f. "
			    "Attempt %d of %d. Both machines spawn it on the step the host stamps.",
			    local, ChrIdName((int)payload.chrId), payload.x, payload.y, payload.z,
			    payload.yaw, g_attempts, kMaxAttempts);
		}

		// ---------------------------------------------------------------------------
		// Running one.
		// ---------------------------------------------------------------------------
		void ApplySpawnBody(const Command& command)
		{
			if (command.length < (int)sizeof(SpawnBodyCommand))
			{
				Log("co-op body: a spawn command from peer %u is %d bytes and the payload "
				    "is %d, so the two builds do not agree on the protocol",
				    (unsigned)command.issuer, command.length, (int)sizeof(SpawnBodyCommand));
				return;
			}

			SpawnBodyCommand payload;
			memcpy(&payload, command.data, sizeof(payload));

			const int peer = (int)payload.peer;
			if (peer < 0 || peer >= MaxPlayers)
			{
				Log("co-op body: a spawn command names peer %d, which is not a peer in a "
				    "session of %d",
				    peer, MaxPlayers);
				return;
			}

			// ALREADY HAS ONE. Idempotent on purpose: a command can arrive twice when a
			// retry crosses with the answer to the first ask, and spawning a second
			// body for the same peer would leave an orphan standing in the world
			// forever with nothing driving it.
			if (PeerBodyCharacter(peer))
			{
				InterlockedIncrement(&g_spawnsIgnored);
				Log("co-op body: ignoring a spawn for peer %d on step %u, that peer "
				    "already has a body. This is a crossed retry rather than a fault.",
				    peer, (unsigned)command.step);
				return;
			}

			char why[64];
			_snprintf_s(why, sizeof(why), _TRUNCATE, "co-op body for peer %d", peer);

			const LONG entry = SpawnBodyAt((int)payload.chrId, payload.x, payload.y,
			    payload.z, payload.yaw, why);
			if (entry < 0)
			{
				// Loud, because this is a DIVERGENCE and not a local inconvenience. The
				// other machine ran the same command on the same step and very likely
				// got a body out of it, so from here one world has a character the
				// other does not.
				InterlockedIncrement(&g_spawnsRefused);
				Log("co-op body: THE SPAWN FOR PEER %d FAILED ON THIS MACHINE on step %u. "
				    "The other machine ran the same command and probably has the body, so "
				    "the two worlds now differ by one character. The reason is on the line "
				    "above. ctrl+F3 asks the host for the world again.",
				    peer, (unsigned)command.step);
				return;
			}

			BindPeerBody(peer, entry);
			InterlockedIncrement(&g_bodiesSpawned);

			// The attempt budget belongs to this machine's own body, so it only resets
			// when that is the one that appeared.
			const int local = LocalPeerId();
			if (local >= 0 && peer == local)
			{
				g_attempts = 0;
				g_gaveUp = false;
			}

			Log("co-op body: peer %d now has a body, entry %ld, and the player binding "
			    "will follow on the next drive pass. On that machine the camera follows "
			    "the binding, so this is also where their view goes.",
			    peer, entry);
		}

		void ApplyCommandsForStep(Lockstep* clock)
		{
			// Two is generous. A body is asked for once per peer per map, and three
			// peers all asking on one step is already the worst case.
			const Command* commands[4];
			const int count = clock->CommandsForStep(commands, 4);

			for (int i = 0; i < count; ++i)
				if (commands[i]->kind == (uint8_t)kCommandSpawnBody)
					ApplySpawnBody(*commands[i]);
		}

	} // namespace

	void ResetCoopBodies()
	{
		g_started = true;
		g_lastAskStep = 0;
		g_everAsked = false;
		g_attempts = 0;
		g_gaveUp = false;
		g_bodiesSpawned = 0;
		g_spawnsRefused = 0;
		g_spawnsIgnored = 0;
		g_bodyMapId = -1;
		strcpy_s(g_status, sizeof(g_status), "co-op bodies: waiting for a session");
	}

	void StopCoopBodies()
	{
		g_started = false;
		ClearPeerBodies();
		strcpy_s(g_status, sizeof(g_status), "co-op bodies: off");
	}

	void StepCoopBodies()
	{
		if (!g_started)
			return;

		Lockstep* clock = ActiveLockstep();
		if (!clock)
			return;

		// BEFORE THE COMMANDS. A spawn ordered for this step has to land in a table
		// that has already let go of the last map's bodies, or the duplicate check
		// sees a stale binding and ignores a spawn this machine genuinely needs.
		ForgetBodiesOnMapChange();

		// Commands next, so a body asked for a few steps ago exists before the code
		// below decides whether this machine still needs one.
		ApplyCommandsForStep(clock);

		const int local = LocalPeerId();
		if (local < 0)
			return;

		Character* mine = PeerBodyCharacter(local);

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "co-op bodies: %d live, mine %s, %ld spawned, %ld refused", PeerBodyCount(),
		    mine ? "yes" : "no", g_bodiesSpawned, g_spawnsRefused);

		if (mine)
			return;

		// NOT PLAYABLE YET, which at the title screen is the normal state rather than a
		// failure. Said out loud in the status line so somebody waiting knows they are
		// waiting for the host and not looking at a broken mod.
		const char* missing = WhyNotPlayable();
		if (missing)
		{
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "co-op bodies: waiting, %s", missing);
			return;
		}

		// ---------------------------------------------------------------------------
		// THE HOST DOES NOT GET A SPAWNED BODY, and this is the one asymmetry in the
		// whole file.
		//
		// The host is already playing somebody: the party leader, bound to
		// g_ffxControlledChr by the shipped game before any of this ran. Spawning a
		// second body for them would mean two characters on screen with one of them
		// unattended, and the camera picking whichever the mod bound last.
		//
		// A joining peer is the one with nothing to be, so a joining peer is the one
		// who gets a body.
		// ---------------------------------------------------------------------------
		if (IsSessionHost())
			return;

		if (g_gaveUp)
			return;

		if (g_attempts >= kMaxAttempts)
		{
			g_gaveUp = true;
			Log("co-op body: giving up after %d attempts. This machine has no body and "
			    "will not ask again, because something is refusing the spawn rather than "
			    "being slow, and the reason is in the lines above. The usual cause is a "
			    "full CHR pool. Disconnect and rejoin to try again.",
			    kMaxAttempts);
			return;
		}

		const uint32_t step = clock->CurrentStep();
		if (g_everAsked && step > g_lastAskStep && step - g_lastAskStep < kRetryEverySteps)
			return;

		// A clock that has only just started has no agreed step history, and a command
		// stamped in the first few steps of a session can land before the other
		// machine's queue is listening. Waiting for the lead to pass costs nothing.
		if (!g_everAsked && step < (uint32_t)DefaultCommandLead)
			return;

		AskForBody(clock, local);
	}

	const char* CoopBodySummary()
	{
		return g_status;
	}

	void LogCoopBodies()
	{
		// ---------------------------------------------------------------------------
		// WHO OWNS THE VIEW, and this is the line that decides whether the whole
		// "the camera follows the binding" story is true during play or only in
		// theory.
		//
		// The free-follow camera reads g_ffxControlledChr and has no target of its
		// own, so while IT is driving, moving the binding moves the view. But it is
		// the lowest thing in the camera priority system: any map or event handle
		// outranks it, and then a script decides the view and the binding changes
		// only what you control.
		//
		// Nobody can tell which is happening by looking at the screen, so it gets
		// asked rather than assumed. If this says a map camera is driving while
		// somebody walks around a field, pointing the binding at a body is not going
		// to move their camera and something else is needed.
		// ---------------------------------------------------------------------------
		const DWORD handle = ffx::ActiveCameraHandle(0);
		Log("camera: screen 0 is driven by %s.%s", 
		    ffx::FollowCameraIsDriving(0)
		        ? "THE FREE-FOLLOW CAMERA, so the player binding controls the view and "
		          "pointing it at a co-op body moves that player's view"
		        : "A SCRIPT CAMERA, so the player binding does NOT control the view right "
		          "now and moving it changes only who you control",
		    ffx::CameraMatrixOverrideActive()
		        ? " The matrix override is ON as well, which beats both of them, so this is "
		          "an FMV or a pre-rendered background."
		        : "");
		Log("  active handle 0x%08X, class %s, look-at mode \"%s\", eye mode \"%s\"%s",
		    (unsigned)handle, ffx::CameraHandleClassName(handle),
		    ffx::ActiveCameraModeName(0, false), ffx::ActiveCameraModeName(0, true),
		    handle ? "" : ". A zero handle means nothing has claimed the camera");

		Log("co-op bodies: %d live of %d peers, %ld spawned, %ld refused, %ld ignored "
		    "as duplicates, %d attempts for this machine%s",
		    PeerBodyCount(), MaxPlayers, g_bodiesSpawned, g_spawnsRefused, g_spawnsIgnored,
		    g_attempts, g_gaveUp ? ", GAVE UP" : "");

		Log("  bodies belong to map %d, and the live map is %d%s", g_bodyMapId, LiveMapId(),
		    (g_bodyMapId >= 0 && LiveMapId() >= 0 && g_bodyMapId != LiveMapId())
		        ? ". THESE DISAGREE, so the bindings are about to be dropped"
		        : "");

		for (int peer = 0; peer < MaxPlayers; ++peer)
		{
			const LONG entry = PeerBodyEntry(peer);
			if (entry < 0)
			{
				Log("  peer %d: no body%s", peer,
				    (peer == 0) ? " (the host plays the party leader, which is correct)"
				                : "");
				continue;
			}

			Character* chr = PeerBodyCharacter(peer);
			Log("  peer %d: entry %ld, pool slot %ld, character 0x%08X, %s", peer, entry,
			    SlotOfEntry(entry), (unsigned)(UINT_PTR)chr,
			    chr ? "live" : "THE ENTRY IS BOUND BUT THE CHARACTER IS NOT LIVE");
		}
	}

} // namespace pilgrimage
