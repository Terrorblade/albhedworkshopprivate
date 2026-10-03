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
#include "workshop/Lockstep.h"
#include "workshop/Log.h"
#include "workshop/Protocol.h"
#include "world/TriggerPass.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// Peer 0 is the host and owns the bound player, which the engine drives. So only
		// peers 1 and up get a clone, and that is why this is one shorter than it looks.
		const int MaxPeers = MaxTriggerPlayers;

		LONG g_entryForPeer[MaxPeers];

		// Last step's button mask per peer, so an examine can fire on the press EDGE. The
		// engine reads g_ffxAtelPadPressed, which is already an edge mask, but
		// InputFrame.buttons is the LEVEL state. Without this difference being handled a
		// held button re-examines the moment each script finishes, which looks like the
		// chest reopening itself.
		DWORD g_lastButtons[MaxPeers];

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
			if (peer <= 0 || peer >= MaxPeers)
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
		// where that character ended up.
		void DriveOnePeer(int peer, const InputFrame& frame)
		{
			Character* chr = CharacterForPeer(peer);
			if (!chr)
			{
				ClearTriggerPlayer(peer);
				return;
			}

			// leftX and leftY are a world direction, already resolved against the owner's
			// camera and already quantised. No camera is read here on purpose.
			const float dirX = DequantiseStick(frame.leftX);
			const float dirZ = DequantiseStick(frame.leftY);

			// Run unless the walk modifier is held, which is exactly what the local pad
			// path decides with out->run = !pad.cross. Mirroring it rather than inventing a
			// second rule keeps a networked character moving like a local one.
			const bool run = (frame.buttons & Btn::Cross) == 0;

			DriveCloneFromWorldDir(chr, dirX, dirZ, run);

			// The position AFTER the drive, because the trigger pass wants where this
			// character is now. The engine integrates motion later in the step, so this is
			// last step's position plus this step's intent, which is the same thing the
			// engine's own trigger pass sees for the bound player.
			float pos[3] = { 0.0f, 0.0f, 0.0f };
			if (!Game.GetPos || !Game.GetPos(chr, &pos[0], &pos[1], &pos[2]))
			{
				ClearTriggerPlayer(peer);
				return;
			}

			SetTriggerPlayer(peer, pos, CharacterFacing(chr));

			// And the examine button, which is the other half of "interact with the world".
			// On the press edge only, because the engine reads an edge mask and a level
			// would re-fire as soon as each script released the channel.
			const DWORD pressed = frame.buttons & ~g_lastButtons[peer];
			g_lastButtons[peer] = frame.buttons;

			if (pressed & ExamineButton)
				RequestTriggerExamine(peer, kAtelEventExamine);
		}

	} // namespace

	void ResetRemotePlayers()
	{
		for (int i = 0; i < MaxPeers; ++i)
		{
			g_entryForPeer[i] = -1;
			g_lastButtons[i] = 0;
		}

		g_lastDriven = 0;
		strcpy_s(g_status, sizeof(g_status), "remote players: reset");
	}

	void BindRemotePlayer(int peer, LONG rosterEntry)
	{
		if (peer <= 0 || peer >= MaxPeers)
		{
			// Peer 0 is not a mistake worth being quiet about. Somebody trying to bind the
			// host a clone has misunderstood which character the host already drives.
			Log("remote players: refusing to bind peer %d, only peers 1 to %d get a body "
			    "because peer 0 is the bound player",
			    peer, MaxPeers - 1);
			return;
		}

		g_entryForPeer[peer] = rosterEntry;

		// Forget the edge state on a rebind. Carrying it over would let a button that was
		// held when the old body went away count as a fresh press on the new one.
		g_lastButtons[peer] = 0;

		if (rosterEntry < 0)
			ClearTriggerPlayer(peer);

		Log("remote players: peer %d %s", peer,
		    rosterEntry < 0 ? "unbound" : "bound to roster entry");
	}

	LONG RemotePlayerEntry(int peer)
	{
		if (peer <= 0 || peer >= MaxPeers)
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
				for (int peer = 1; peer < MaxPeers; ++peer)
					ClearTriggerPlayer(peer);

				g_lastDriven = 0;
				strcpy_s(g_status, sizeof(g_status), "remote players: no session");
			}

			return;
		}

		int driven = 0;
		for (int peer = 1; peer < MaxPeers; ++peer)
		{
			if (g_entryForPeer[peer] < 0)
				continue;

			const InputFrame* frame = clock->InputForStep((uint8_t)peer);
			if (!frame)
			{
				// The clock has nothing for this peer on this step. That is the stall case
				// and the gate deals with it, so holding the character still is the right
				// answer rather than reusing stale input and drifting.
				Character* chr = CharacterForPeer(peer);
				if (chr)
					HoldCloneStill(chr);

				continue;
			}

			DriveOnePeer(peer, *frame);
			++driven;
		}

		g_lastDriven = driven;
		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "remote players: %d driven on step %u", driven, (unsigned)clock->CurrentStep());
	}

	int RemotePlayerCount()
	{
		int n = 0;
		for (int peer = 1; peer < MaxPeers; ++peer)
		{
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

		for (int peer = 1; peer < MaxPeers; ++peer)
		{
			const LONG entry = g_entryForPeer[peer];
			if (entry < 0)
			{
				Log("  peer %d: unbound", peer);
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
			Log("  peer %d: entry %ld, pool slot %ld, at %.1f %.1f %.1f facing %.2f", peer,
			    (long)entry, (long)SlotOfEntry(entry), x, y, z, CharacterFacing(chr));
		}
	}

} // namespace pilgrimage
