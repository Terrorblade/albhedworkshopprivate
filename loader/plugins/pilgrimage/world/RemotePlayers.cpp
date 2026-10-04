#include "world/RemotePlayers.h"

#include <stdio.h>
#include <string.h>

#include "clones/CloneDriver.h"
#include "clones/CloneRoster.h"
#include "ffx/Api.h"
#include "ffx/Atel.h"
#include "ffx/Character.h"
#include "ffx/Input.h"
#include "net/LockstepLink.h"
#include "net/NetLink.h"
#include "workshop/Session.h"
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "workshop/Protocol.h"
#include "world/DialogueSync.h"
#include "world/PlayerDrive.h"
#include "world/TriggerPass.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// WAS WRONG, and the fix is worth spelling out because the mistake is easy to
		// make again.
		//
		// This used to say "peer 0 is the host and owns the bound player, so only peers 1
		// and up get a clone". That confuses two different numbering schemes:
		//
		//   - a PEER ID is session-global. The host is always peer 0, on both machines.
		//   - the BOUND PLAYER is machine-local. It is whichever character this copy of the
		//     game is driving, and the engine's own trigger pass is always that one.
		//
		// On the host those two happen to coincide, which is why the old rule looked right.
		// On a client they do not: peer 0 is the HOST and is remote, and the client's own id
		// is 1 or 2. Under the old rule a client would drive its own character as a clone
		// while its real body was already being driven by the engine, and would never drive
		// the host at all. The host would just stand still and trigger nothing.
		//
		// So the rule is "every peer except the local one", and the local one is asked for
		// rather than assumed.
		const int MaxPeers = MaxTriggerPlayers;

		// Which peer this copy of the game is playing, or -1 when there is no session. Read
		// fresh rather than cached, because a client does not know its id until the host
		// assigns one during the handshake.
		int LocalPeerId()
		{
			Session* session = ActiveSession();
			if (!session)
				return -1;

			return (int)session->LocalPeer();
		}

		// Peers are session-global, trigger slots are machine-local. Slot 0 is always this
		// machine's bound player, because that is what the engine's own pass covers, so the
		// remote peers have to be packed into the slots above it.
		//
		// The mapping is a bijection from "every peer but the local one" onto slots 1 and up:
		//
		//   local 0: peer 1 -> slot 1, peer 2 -> slot 2
		//   local 1: peer 0 -> slot 1, peer 2 -> slot 2
		//   local 2: peer 0 -> slot 1, peer 1 -> slot 2
		//
		// Returns -1 for the local peer itself, which is the caller's signal to skip it.
		int TriggerSlotForPeer(int peer, int localPeer)
		{
			if (peer < 0 || peer >= MaxPeers || localPeer < 0)
				return -1;
			if (peer == localPeer)
				return -1;

			return (peer < localPeer) ? peer + 1 : peer;
		}

		LONG g_entryForPeer[MaxPeers];

		// Last step's button mask per peer, so an examine can fire on the press EDGE. The
		// engine reads g_ffxAtelPadPressed, which is already an edge mask, but
		// InputFrame.buttons is the LEVEL state. Without this difference being handled a
		// held button re-examines the moment each script finishes, which looks like the
		// chest reopening itself.
		DWORD g_lastButtons[MaxPeers];

		// The last input actually received for each peer, and how many steps have been run
		// on it since. Together these are what lets a late packet coast instead of stop.
		InputFrame g_lastFrame[MaxPeers];
		int g_coastSteps[MaxPeers];

		// How long a character may keep moving on input that has not arrived.
		//
		// At 29.97 Hz this is about 130 ms. The reasoning for coasting at all: somebody
		// walking in a straight line is overwhelmingly likely to still be walking in a
		// straight line 33 ms later, so repeating their last input is a far better guess
		// than assuming they stopped dead. Guessing wrong costs a small position error that
		// the next real frame corrects, and because position is never sent, that correction
		// happens by them walking, not by them being moved.
		//
		// Four rather than more because the error grows every step you keep guessing, and a
		// character that coasts for half a second is a character that walked somewhere
		// nobody asked it to.
		const int MaxCoastSteps = 4;

		// How many steps have been coasted in total, for the status line. A number that
		// keeps climbing is the signal that the link cannot keep up, which is worth being
		// able to see rather than having to infer from the characters looking wrong.
		LONG g_coastedTotal = 0;

		int g_lastDriven = 0;
		char g_status[192] = "remote players: none bound";

		// Which button means examine.
		//
		// A CHOICE, not a finding. The engine's own read goes through
		// FFX_Pad__remapButtonMask and tests bits 0x20 and 0x80 of the REMAPPED mask,
		// taking kind 0 for 0x20 and kind 1 otherwise. That remap table has not been read,
		// so which pre-remap buttons those are is unknown. Circle is the sensible guess for
		// a default FFX layout.
		//
		// Kind 1, examine alt, is deliberately NOT wired. Guessing a second button would
		// mean guessing twice, and Cross is already spoken for below as the walk modifier.
		// The kit takes the kind as a parameter, so wiring it later is one line.
		const DWORD ExamineButton = Btn::Circle;

		Character* CharacterForPeer(int peer)
		{
			if (peer < 0 || peer >= MaxPeers)
				return NULL;

			const LONG entry = g_entryForPeer[peer];
			if (entry < 0)
				return NULL;

			const LONG slot = SlotOfEntry(entry);
			if (slot < 0)
				return NULL;

			Character* chr = CharacterFromSlot(slot);
			return IsLive(chr) ? chr : NULL;
		}

		// Apply one peer's replicated input to its character, then tell the trigger pass
		// where that character ended up. slot is the trigger slot, which is NOT the peer id.
		void DriveOnePeer(int peer, int slot, const InputFrame& frame)
		{
			Character* chr = CharacterForPeer(peer);
			if (!chr)
			{
				ClearTriggerPlayer(slot);
				return;
			}

			// THE FALLBACK PATH ONLY. world/PlayerDrive.cpp normally moves this character,
			// through the engine's own driver, at the engine's own point in the step. This
			// branch runs when it could not patch its call site.
			//
			// It is a reimplementation of the driver and it is not a faithful one: the
			// speeds are the mod's constants rather than the engine's table, and the
			// heading is set directly rather than ramped, so there is no acceleration. It
			// will not agree with a machine that is using the real driver. Keeping it is
			// still right, because the alternative when the patch fails is a character
			// that does not move at all.
			if (!PlayerDriveActive())
			{
				// A world heading and a deflection, already resolved against the owner's
				// camera, already deadzoned and already quantised. No camera is read here
				// on purpose, and no atan2 either: the angle came over as an angle.
				const float heading = DequantiseAngle(frame.moveAngle);
				const float magnitude = DequantiseMagnitude(frame.moveMag);

				// Run unless the walk modifier is held, which is exactly what the local
				// pad path decides with out->run = !pad.cross.
				const bool run = (frame.buttons & Btn::Cross) == 0;

				DriveCloneFromHeading(chr, heading, magnitude, run);
			}

			// The position AFTER the drive, because the trigger pass wants where this
			// character is now. The engine integrates motion later in the step, so this is
			// last step's position plus this step's intent, which is the same thing the
			// engine's own trigger pass sees for the bound player.
			float pos[3] = { 0.0f, 0.0f, 0.0f };
			if (!Game.GetPos || !Game.GetPos(chr, &pos[0], &pos[1], &pos[2]))
			{
				ClearTriggerPlayer(slot);
				return;
			}

			SetTriggerPlayer(slot, pos, CharacterFacing(chr));

			// And the examine button, which is the other half of "interact with the world".
			// On the press edge only, because the engine reads an edge mask and a level
			// would re-fire as soon as each script released the channel.
			const DWORD pressed = frame.buttons & ~g_lastButtons[peer];
			g_lastButtons[peer] = frame.buttons;

			if (pressed & ExamineButton)
			{
				RequestTriggerExamine(slot, kAtelEventExamine);

				// And tell the dialogue layer whose box it will be if this examine wins.
				// It may not win: a nearer actor can take the winner slot and the engine
				// can refuse it outright, which is why this is a note with an expiry
				// rather than an assignment.
				NoteDialogueOwner(peer);
			}
		}

	} // namespace

	void ResetRemotePlayers()
	{
		for (int i = 0; i < MaxPeers; ++i)
		{
			g_entryForPeer[i] = -1;
			g_lastButtons[i] = 0;
			g_lastFrame[i] = NeutralInput();
			g_coastSteps[i] = 0;
		}

		g_lastDriven = 0;
		strcpy_s(g_status, sizeof(g_status), "remote players: reset");
	}

	void BindRemotePlayer(int peer, LONG rosterEntry)
	{
		if (peer < 0 || peer >= MaxPeers)
		{
			Log("remote players: refusing to bind peer %d, the session only has peers 0 to "
			    "%d",
			    peer, MaxPeers - 1);
			return;
		}

		// Binding the LOCAL peer a clone is the one genuine mistake here, and it is worth
		// being loud about. That character already has a body the engine drives, so giving
		// it a second one would mean two things writing one character's motion.
		const int local = LocalPeerId();
		if (local >= 0 && peer == local)
		{
			Log("remote players: refusing to bind peer %d, that is US. The local player "
			    "already has a body and the engine drives it.",
			    peer);
			return;
		}

		g_entryForPeer[peer] = rosterEntry;

		// Forget the edge state on a rebind. Carrying it over would let a button that was
		// held when the old body went away count as a fresh press on the new one. Same for
		// the coast: a new body must not start off walking because the old one was.
		g_lastButtons[peer] = 0;
		g_lastFrame[peer] = NeutralInput();
		g_coastSteps[peer] = 0;

		if (rosterEntry < 0)
		{
			const int slot = TriggerSlotForPeer(peer, local);
			if (slot > 0)
				ClearTriggerPlayer(slot);
		}

		Log("remote players: peer %d %s", peer,
		    rosterEntry < 0 ? "unbound" : "bound to roster entry");
	}

	Character* RemotePlayerCharacter(int peer)
	{
		return CharacterForPeer(peer);
	}

	LONG RemotePlayerEntry(int peer)
	{
		if (peer < 0 || peer >= MaxPeers)
			return -1;

		return g_entryForPeer[peer];
	}

	void StepRemotePlayers()
	{
		Lockstep* clock = ActiveLockstep();
		if (!clock)
		{
			// No session. Every peer that had a body loses it, so a character does not keep
			// coasting on the last input it ever received.
			if (g_lastDriven != 0)
			{
				// Slots, not peers. Slot 0 is the bound player and was never ours.
				for (int slot = 1; slot < MaxTriggerPlayers; ++slot)
					ClearTriggerPlayer(slot);

				g_lastDriven = 0;
				strcpy_s(g_status, sizeof(g_status), "remote players: no session");
			}

			return;
		}

		const int local = LocalPeerId();
		if (local < 0)
			return;

		int driven = 0;
		for (int peer = 0; peer < MaxPeers; ++peer)
		{
			const int slot = TriggerSlotForPeer(peer, local);
			if (slot < 0)
				continue; // us. The engine drives this one.
			if (g_entryForPeer[peer] < 0)
				continue;

			// Received, or only the neutral stand-in. InputForStep cannot tell us, which
			// is why HasInputForStep exists.
			if (clock->HasInputForStep((uint8_t)peer))
			{
				g_lastFrame[peer] = *clock->InputForStep((uint8_t)peer);
				g_coastSteps[peer] = 0;
				DriveOnePeer(peer, slot, g_lastFrame[peer]);
				++driven;
				continue;
			}

			// Nothing arrived for this step. Keep going on the last thing they actually
			// sent, for a short while, rather than stopping dead. See MaxCoastSteps.
			if (g_coastSteps[peer] < MaxCoastSteps)
			{
				++g_coastSteps[peer];
				InterlockedIncrement(&g_coastedTotal);

				// The BUTTONS are deliberately not coasted, only the movement. Repeating a
				// held direction is a safe guess. Repeating a button is not: the edge
				// detector in DriveOnePeer would see no new press, so nothing fires twice,
				// but a button that was released during the gap would stay held for up to
				// four steps and that is the difference between walking and running.
				InputFrame coast = g_lastFrame[peer];
				coast.buttons = g_lastButtons[peer];
				DriveOnePeer(peer, slot, coast);
				++driven;
				continue;
			}

			// Out of patience. Stop, because by now the guess is worse than the truth.
			//
			// Only on the fallback path. PlayerDrive handles running out of patience by
			// feeding that character a neutral input, which lets the engine's own ramps
			// decay it to a halt instead of snapping it, and a snap is both visible and a
			// motion the other machine never produced.
			if (!PlayerDriveActive())
			{
				Character* chr = CharacterForPeer(peer);
				if (chr)
					HoldCloneStill(chr);
			}
		}

		g_lastDriven = driven;

		int coasting = 0;
		for (int peer = 0; peer < MaxPeers; ++peer)
			if (g_coastSteps[peer] > 0)
				++coasting;

		if (coasting > 0)
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "remote players: %d driven on step %u, %d coasting, %ld steps coasted",
			    driven, (unsigned)clock->CurrentStep(), coasting, g_coastedTotal);
		else
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "remote players: %d driven on step %u, %ld steps coasted so far", driven,
			    (unsigned)clock->CurrentStep(), g_coastedTotal);
	}

	int RemotePlayerCount()
	{
		const int local = LocalPeerId();

		int n = 0;
		for (int peer = 0; peer < MaxPeers; ++peer)
		{
			if (peer == local)
				continue;
			if (g_entryForPeer[peer] >= 0)
				++n;
		}

		return n;
	}

	const char* RemotePlayerStatus()
	{
		return g_status;
	}

	void LogRemotePlayers()
	{
		Log("=== remote players ===");
		Log("%s", g_status);

		const int local = LocalPeerId();
		Log("local peer %d, so slot 0 of the trigger pass is us and the engine drives it",
		    local);

		for (int peer = 0; peer < MaxPeers; ++peer)
		{
			if (peer == local)
			{
				Log("  peer %d: US, driven by the engine", peer);
				continue;
			}

			const LONG entry = g_entryForPeer[peer];
			if (entry < 0)
			{
				Log("  peer %d: unbound (would be trigger slot %d)", peer,
				    TriggerSlotForPeer(peer, local));
				continue;
			}

			Character* chr = CharacterForPeer(peer);
			if (!chr)
			{
				Log("  peer %d: bound to entry %ld, but that entry has no live character",
				    peer, (long)entry);
				continue;
			}

			float x = 0.0f, y = 0.0f, z = 0.0f;
			Game.GetPos(chr, &x, &y, &z);
			Log("  peer %d: trigger slot %d, entry %ld, pool slot %ld, at %.1f %.1f %.1f "
			    "facing %.2f",
			    peer, TriggerSlotForPeer(peer, local), (long)entry, (long)SlotOfEntry(entry),
			    x, y, z, CharacterFacing(chr));
		}
	}

} // namespace pilgrimage
