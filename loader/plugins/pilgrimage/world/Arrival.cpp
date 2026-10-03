#include "world/Arrival.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Atel.h"
#include "ffx/Character.h"
#include "ffx/Walkmesh.h"
#include "ffx/WorldState.h"
#include "workshop/Log.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// How long to wait for the map to finish loading before giving up.
		//
		// Generous on purpose. A map load reads from disk and the joiner is not holding
		// the simulation during it, so this is wall clock on an unknown machine rather
		// than a step count. Giving up does not fail the join, it just means the joiner
		// stays at the doorway and has to walk, which is a far better outcome than the
		// host being frozen forever waiting for a reply.
		const DWORD ArrivalTimeoutMs = 20000;

		// A character index this build knows about. The save block has 8.
		const int MaxAnchorSlots = 8;

		WorldAnchorPayload g_anchor;
		bool g_haveAnchor = false;

		bool g_pending = false;
		bool g_complete = true; // nothing to wait for until an arrival starts
		DWORD g_startedMs = 0;
		uint32_t g_forSnapshot = 0;
		int g_placed = 0;
		int g_refused = 0;

		char g_status[224] = "arrival: idle";

		// Is the map the anchor describes actually loaded and walkable?
		//
		// All four conditions matter and none of them is redundant:
		//
		//  - the deferred change must have been consumed, otherwise the map we are
		//    looking at is the old one
		//  - the scene must be loaded, which is the engine's own "there is a map here"
		//  - the walkmesh must be loaded, because a placement that cannot be bound to a
		//    triangle leaves the character at Y = 0 forever. See ffx/Walkmesh.h
		//  - the live map id must MATCH the anchor, because placing a character at
		//    another map's coordinates puts it somewhere arbitrary, and off the walkmesh
		//    the engine stops doing collision at all
		bool MapIsReadyForAnchor(int* liveMapIdOut)
		{
			if (liveMapIdOut)
				*liveMapIdOut = -1;

			WorldLocation where;
			if (!ReadWorldLocation(&where) || !where.valid)
				return false;

			if (liveMapIdOut)
				*liveMapIdOut = where.mapId;

			if (where.changePending || !where.sceneLoaded)
				return false;
			if (!WalkmeshIsLoaded())
				return false;
			if (where.mapId != (int)g_anchor.mapId)
				return false;

			return true;
		}

		void Finish(const char* why)
		{
			g_pending = false;
			g_complete = true;
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "arrival: %s, %d placed, %d refused",
			    why, g_placed, g_refused);
			Log("arrival: %s. %d characters placed, %d could not be.", why, g_placed, g_refused);
		}

		// Put one character where the anchor says. Returns true when it landed.
		bool PlaceOne(const WorldAnchorSlot& slot)
		{
			if (slot.charIndex < 0 || slot.charIndex >= MaxAnchorSlots)
				return false;

			const int actorId = ActorIdForPartyCharacter(slot.charIndex);
			if (actorId < 0)
			{
				// Normal, not an error. A character the host has in its party may have no
				// actor in this map, and a character nobody is using has none anywhere.
				return false;
			}

			// One call does the whole recipe: the off-mesh check, the motion stop, the
			// script's move command cancelled, the position with snapPrev, the facing and
			// the walkmesh rebind. See ffx/Atel.h for why each part is there.
			if (!PlaceActor(actorId, slot.x, slot.y, slot.z, slot.facing, "arrival"))
			{
				Log("arrival: character %d has actor %d but the placement was refused. The "
				    "most likely reason is that the host's XZ is off this machine's "
				    "walkmesh, which would mean the two maps are not the same.",
				    (int)slot.charIndex, actorId);
				return false;
			}

			return true;
		}

		void DoPlacement()
		{
			// Worth knowing, and it is handled rather than ignored: the party lands
			// within a couple of metres of itself, and the CHR separation pass fires the
			// touch-NPC event when a player flagged CHR overlaps another one. That test
			// is gated on m_speed != 0, and PlaceActor stops the motion before it moves
			// anything, so a stack of freshly placed characters does not fire anything.
			g_placed = 0;
			g_refused = 0;

			int count = g_anchor.slotCount;
			if (count < 0)
				count = 0;
			if (count > MaxAnchorSlots)
				count = MaxAnchorSlots;

			for (int i = 0; i < count; ++i)
			{
				if (PlaceOne(g_anchor.slots[i]))
					++g_placed;
				else
					++g_refused;
			}

			if (g_placed == 0)
				Finish("nobody could be placed, so this machine is still at the doorway");
			else
				Finish("landed beside the host");
		}

	} // namespace

	void StartArrival()
	{
		g_haveAnchor = false;
		g_pending = false;
		g_complete = true;
		g_placed = 0;
		g_refused = 0;
		memset(&g_anchor, 0, sizeof(g_anchor));
		strcpy_s(g_status, sizeof(g_status), "arrival: idle");
	}

	void StopArrival()
	{
		StartArrival();
	}

	bool CaptureArrivalAnchor(uint32_t snapshotId, uint32_t hostStep, WorldAnchorPayload* out)
	{
		if (!out)
			return false;

		memset(out, 0, sizeof(*out));
		out->snapshotId = snapshotId;
		out->hostStep = hostStep;

		WorldLocation where;
		if (!ReadWorldLocation(&where) || !where.valid)
			return false;

		out->mapId = (uint16_t)where.mapId;
		out->entryPoint = (uint16_t)where.entryPoint;

		int filled = 0;
		for (int charIndex = 0; charIndex < MaxAnchorSlots; ++charIndex)
		{
			const int actorId = ActorIdForPartyCharacter(charIndex);
			if (actorId < 0)
				continue;

			float xyz[3] = { 0.0f, 0.0f, 0.0f };
			if (!ActorPosition(actorId, xyz))
				continue;

			WorldAnchorSlot& slot = out->slots[filled];
			slot.charIndex = (int32_t)charIndex;
			slot.x = xyz[0];
			slot.y = xyz[1];
			slot.z = xyz[2];

			// The facing comes off the CHR rather than the actor's pos+52, because pos+52
			// is the trigger box rotation and is only meaningful for the trigger kinds.
			// m_rotY is where the character is actually looking.
			Character* chr = (Character*)ChrForActor(actorId);
			slot.facing = chr ? CharacterFacing(chr) : 0.0f;

			++filled;
		}

		out->slotCount = (int32_t)filled;
		return filled > 0;
	}

	void NoteArrivalAnchor(const WorldAnchorPayload& anchor)
	{
		g_anchor = anchor;
		g_haveAnchor = true;

		Log("arrival: the host says it is on map %u entry %u with %d characters placed, "
		    "as of its step %u",
		    (unsigned)anchor.mapId, (unsigned)anchor.entryPoint, (int)anchor.slotCount,
		    (unsigned)anchor.hostStep);
	}

	void BeginArrival(uint32_t snapshotId)
	{
		g_forSnapshot = snapshotId;
		g_placed = 0;
		g_refused = 0;

		if (!g_haveAnchor)
		{
			// Not fatal. The world is still correct, the joiner is just standing at the
			// doorway the block named. Say so plainly rather than hanging the host.
			g_pending = false;
			g_complete = true;
			strcpy_s(g_status, sizeof(g_status), "arrival: no anchor arrived, left at the doorway");
			Log("arrival: no anchor arrived with snapshot %u, so this machine stays where the "
			    "save block put it. That is a correct world in the wrong place.",
			    (unsigned)snapshotId);
			return;
		}

		if (g_anchor.snapshotId != snapshotId)
		{
			// A leftover anchor from an abandoned transfer. Using it would place the
			// joiner at coordinates from a different moment, and possibly a different map.
			g_pending = false;
			g_complete = true;
			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "arrival: anchor is for snapshot %u, not %u", (unsigned)g_anchor.snapshotId,
			    (unsigned)snapshotId);
			Log("arrival: the anchor in hand belongs to snapshot %u but this install is %u, so "
			    "it is being ignored rather than guessed at",
			    (unsigned)g_anchor.snapshotId, (unsigned)snapshotId);
			return;
		}

		g_pending = true;
		g_complete = false;
		g_startedMs = GetTickCount();
		strcpy_s(g_status, sizeof(g_status), "arrival: waiting for the map to load");
		Log("arrival: waiting for map %u to load, then placing %d characters",
		    (unsigned)g_anchor.mapId, (int)g_anchor.slotCount);
	}

	bool ArrivalPending()
	{
		return g_pending;
	}

	bool ArrivalComplete()
	{
		return g_complete;
	}

	void ServiceArrival()
	{
		if (!g_pending)
			return;

		int liveMapId = -1;
		if (MapIsReadyForAnchor(&liveMapId))
		{
			DoPlacement();
			return;
		}

		const DWORD now = GetTickCount();
		if (now - g_startedMs > ArrivalTimeoutMs)
		{
			Log("arrival: map %u never became ready in %u ms, live map reads %d. Giving up on "
			    "the placement so the host can carry on, which leaves this machine at the "
			    "doorway with a correct world.",
			    (unsigned)g_anchor.mapId, (unsigned)(now - g_startedMs), liveMapId);
			Finish("timed out waiting for the map");
			return;
		}

		_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
		    "arrival: waiting for map %u, live map %d, %u ms", (unsigned)g_anchor.mapId,
		    liveMapId, (unsigned)(now - g_startedMs));
	}

	const char* ArrivalStatus()
	{
		return g_status;
	}

	void LogArrival()
	{
		Log("=== arrival ===");
		Log("%s", g_status);

		if (!g_haveAnchor)
		{
			Log("no anchor is in hand, so a join right now would land at whatever doorway the "
			    "save block names");
		}
		else
		{
			Log("anchor: snapshot %u, host step %u, map %u entry %u, %d slots",
			    (unsigned)g_anchor.snapshotId, (unsigned)g_anchor.hostStep,
			    (unsigned)g_anchor.mapId, (unsigned)g_anchor.entryPoint,
			    (int)g_anchor.slotCount);

			int count = g_anchor.slotCount;
			if (count > MaxAnchorSlots)
				count = MaxAnchorSlots;
			for (int i = 0; i < count; ++i)
			{
				const WorldAnchorSlot& slot = g_anchor.slots[i];
				const int actorId = ActorIdForPartyCharacter(slot.charIndex);
				Log("  char %d at %.1f %.1f %.1f facing %.2f, actor here is %d",
				    (int)slot.charIndex, slot.x, slot.y, slot.z, slot.facing, actorId);
			}
		}

		int liveMapId = -1;
		const bool ready = MapIsReadyForAnchor(&liveMapId);
		Log("live map %d, walkmesh %s, ready for the anchor: %s", liveMapId,
		    WalkmeshIsLoaded() ? "loaded" : "NOT loaded", ready ? "yes" : "no");

		if (SetPosSuppressed())
			Log("THE SETPOS SUPPRESSION ONE-SHOT IS ARMED. The next position write on any "
			    "character would be silently dropped. PlaceActor clears it first, so this is "
			    "worth noticing but not a problem for a placement.");
	}

} // namespace pilgrimage
