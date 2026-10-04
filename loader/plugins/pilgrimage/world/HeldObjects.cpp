#include "world/HeldObjects.h"

#include <stdio.h>
#include <string.h>

#include "ffx/Atel.h"
#include "ffx/Character.h"
#include "ffx/Layout.h"
#include "ffx/WorldState.h"
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

		// One carry, as the census records it.
		//
		// Both actor ids are kept even though the carrier pointer would be enough to
		// read either, because the whole point of the census is to be comparable
		// between two machines, and a pointer is not.
		struct CarryEntry
		{
			int objectSlot;     // CHR pool slot, deterministic under lockstep
			int objectActorId;  // m_objId, an ATEL actor id, or -1
			int objectChrId;    // CHR+0, the model type id. For the log only.
			int carrierSlot;    // CHR pool slot of the carrier, or -1 if it is not in the pool
			int carrierActorId; // m_parentUid
			int carrierJoint;   // m_parentJoint, an index into the CARRIER's skeleton
		};

		bool g_installed = false;
		bool g_started = false;

		// The census. Two copies, because the edges are the useful part and an edge is a
		// difference between two steps.
		CarryEntry g_now[kMaxTrackedCarries];
		int g_nowCount = 0;
		CarryEntry g_was[kMaxTrackedCarries];
		int g_wasCount = 0;
		bool g_haveWas = false;

		// -1 when the census could not be taken at all, which is not the same as zero
		// carries and must not hash the same way either.
		int g_reportedCount = -1;
		uint32_t g_hash = 0;

		// Which map the census is of. A transition disposes the whole pool and builds a
		// new one, so every carry on the old map reads as a drop and every carry on the
		// new one as a pickup. That is not news, so the edge log is suppressed for the
		// step the map id changes on.
		int g_censusMapId = -1;
		bool g_haveCensusMap = false;

		// The step the ordered commands were last drained for. The gate can ask more than
		// once per frame and the frame path asks as well, and draining twice on one step
		// would consume nothing the second time but would still cost the walk.
		uint32_t g_drainedStep = 0;
		bool g_haveDrainedStep = false;

		LONG g_pickups = 0;
		LONG g_drops = 0;
		LONG g_handovers = 0;
		LONG g_applied = 0;
		LONG g_refused = 0;
		LONG g_overflows = 0;

		char g_status[220] = "held: not installed";

		int LiveMapId()
		{
			WorldLocation where;
			if (!ReadWorldLocation(&where) || !where.valid)
				return -1;
			return where.mapId;
		}

		// ---------------------------------------------------------------------------
		// THE CENSUS
		// ---------------------------------------------------------------------------

		// FNV-1a, 32 bit. Chosen because the rest of the mod's hashing is FNV and a
		// second hash function would be one more thing to keep in step for no gain.
		void HashBytes(uint32_t* hash, const void* bytes, size_t count)
		{
			const unsigned char* p = (const unsigned char*)bytes;
			for (size_t i = 0; i < count; ++i)
			{
				*hash ^= (uint32_t)p[i];
				*hash *= 16777619u;
			}
		}

		void HashInt(uint32_t* hash, int value)
		{
			const int32_t wide = (int32_t)value;
			HashBytes(hash, &wide, sizeof(wide));
		}

		// Walks the CHR pool and records every carry. Returns the count, or -1 when the
		// pool is not there or there are more carries than there is room for.
		//
		// REFUSING ON OVERFLOW RATHER THAN TRUNCATING is the one judgement call in here.
		// A truncated census would hash cleanly and would silently stop covering part of
		// the world, so two machines could agree on a hash while disagreeing about a
		// carry the hash no longer reached. A refusal is visible.
		int TakeCensus()
		{
			if (!PoolIsReady())
				return -1;

			const int slots = *Game.chrCount;
			int found = 0;

			for (int slot = 0; slot < slots; ++slot)
			{
				Character* chr = CharacterFromSlot((LONG)slot);
				if (!chr)
					continue;

				// The in-use byte is the whole registration for a CHR, so a zero here
				// means the slot is free and whatever else it holds is last map's
				// rubbish.
				if (ByteAt(chr, Chr::InUse) == 0)
					continue;

				CarryLink link;
				if (!ReadCarryLink(chr, &link))
					continue;
				if (!link.carrier)
					continue;

				if (found >= kMaxTrackedCarries)
				{
					InterlockedIncrement(&g_overflows);
					return -1;
				}

				CarryEntry& entry = g_now[found];
				entry.objectSlot = slot;
				entry.objectActorId = CharacterActorId(chr);
				entry.objectChrId = (int)(unsigned short)ShortAt(chr, Chr::Id);
				entry.carrierSlot = (int)SlotOfCharacter(link.carrier);
				entry.carrierActorId = link.carrierActorId;
				entry.carrierJoint = link.carrierJoint;
				++found;
			}

			return found;
		}

		// The hash the desync detector wants. See the comment on HeldObjectsHash for why
		// this is in slot order rather than sorted.
		//
		// The attach offset is deliberately NOT hashed. It is a float derived from the
		// carrier's model scale, and hashing a float across two machines turns a
		// harmless last-bit difference into a reported desync. The three integers are
		// what actually decides who is holding what.
		uint32_t HashCensus(const CarryEntry* entries, int count)
		{
			if (count <= 0)
				return 0;

			uint32_t hash = 2166136261u;
			for (int i = 0; i < count; ++i)
			{
				HashInt(&hash, entries[i].objectActorId);
				HashInt(&hash, entries[i].carrierActorId);
				HashInt(&hash, entries[i].carrierJoint);
			}
			return hash;
		}

		const CarryEntry* FindBySlot(const CarryEntry* entries, int count, int slot)
		{
			for (int i = 0; i < count; ++i)
				if (entries[i].objectSlot == slot)
					return &entries[i];
			return NULL;
		}

		// What changed since the last census, as log lines. Matched on POOL SLOT rather
		// than on actor id, because an object with no actor behind it reads as -1 and
		// several of those would all match each other.
		void LogEdges(uint32_t step)
		{
			for (int i = 0; i < g_nowCount; ++i)
			{
				const CarryEntry& now = g_now[i];
				const CarryEntry* was = FindBySlot(g_was, g_wasCount, now.objectSlot);

				if (!was)
				{
					InterlockedIncrement(&g_pickups);
					Log("held: step %u, chr %d (slot %d, actor %d) was picked up by actor "
					    "%d at joint %d",
					    (unsigned)step, now.objectChrId, now.objectSlot, now.objectActorId,
					    now.carrierActorId, now.carrierJoint);
					continue;
				}

				if (was->carrierActorId != now.carrierActorId ||
				    was->carrierSlot != now.carrierSlot)
				{
					InterlockedIncrement(&g_handovers);
					Log("held: step %u, chr %d (slot %d) changed hands, actor %d -> actor "
					    "%d, joint %d -> %d",
					    (unsigned)step, now.objectChrId, now.objectSlot,
					    was->carrierActorId, now.carrierActorId, was->carrierJoint,
					    now.carrierJoint);
					continue;
				}

				if (was->carrierJoint != now.carrierJoint)
					Log("held: step %u, chr %d (slot %d) moved from joint %d to joint %d "
					    "on the same carrier",
					    (unsigned)step, now.objectChrId, now.objectSlot, was->carrierJoint,
					    now.carrierJoint);
			}

			for (int i = 0; i < g_wasCount; ++i)
			{
				const CarryEntry& was = g_was[i];
				if (FindBySlot(g_now, g_nowCount, was.objectSlot))
					continue;

				InterlockedIncrement(&g_drops);
				Log("held: step %u, chr %d (slot %d) is no longer carried by actor %d",
				    (unsigned)step, was.objectChrId, was.objectSlot, was.carrierActorId);
			}
		}

		void BuildStatus()
		{
			if (!g_installed)
			{
				strcpy_s(g_status, sizeof(g_status), "held: not installed");
				return;
			}

			if (g_reportedCount < 0)
			{
				_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
				    "held: no census (pool not ready, or more than %d carries), %ld "
				    "overflows",
				    kMaxTrackedCarries, g_overflows);
				return;
			}

			_snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
			    "held: %d carried, hash %08lX, %ld up / %ld down / %ld handed over, %ld "
			    "ordered%s",
			    g_reportedCount, (unsigned long)g_hash, g_pickups, g_drops, g_handovers,
			    g_applied, g_started ? "" : " (commands idle)");
		}

		// ---------------------------------------------------------------------------
		// THE ORDERED HANDOVER
		// ---------------------------------------------------------------------------

		Character* CharacterForActor(int actorId)
		{
			if (actorId < 0)
				return NULL;
			return (Character*)ChrForActor(actorId);
		}

		void ApplyHandover(const Command& command)
		{
			if (command.length < (int)sizeof(HeldObjectCommand))
			{
				InterlockedIncrement(&g_refused);
				Log("held: a handover command from peer %u was %d bytes and wanted %d, so "
				    "it was dropped. That is the shape of a protocol mismatch rather than "
				    "a user error, and if the other machine accepted it the two are now "
				    "holding different things.",
				    (unsigned)command.issuer, (int)command.length,
				    (int)sizeof(HeldObjectCommand));
				return;
			}

			HeldObjectCommand wanted;
			memcpy(&wanted, command.data, sizeof(wanted));

			// The map check, and it is not belt and braces. An ATEL actor id is map
			// scoped, so the same id names a different object on a different map and the
			// engine would attach the wrong one without complaining.
			const int liveMap = LiveMapId();
			if (liveMap < 0 || (int)wanted.mapId != liveMap)
			{
				InterlockedIncrement(&g_refused);
				Log("held: REFUSED peer %u's handover on step %u, it was issued for map %d "
				    "and this machine is on map %d. Actor ids are map scoped, so applying "
				    "it would have moved the wrong object.",
				    (unsigned)command.issuer, (unsigned)command.step, (int)wanted.mapId,
				    liveMap);
				return;
			}

			Character* object = CharacterForActor((int)wanted.objectActorId);
			if (!object)
			{
				InterlockedIncrement(&g_refused);
				Log("held: REFUSED peer %u's handover on step %u, actor %d has no character "
				    "on this machine. Both machines ran the same script, so if this is "
				    "happening on one side only they have already diverged.",
				    (unsigned)command.issuer, (unsigned)command.step,
				    (int)wanted.objectActorId);
				return;
			}

			// A -1 carrier is a put-down and is a legitimate request, so the NULL has to
			// be told apart from a lookup that failed.
			Character* carrier = NULL;
			if (wanted.carrierActorId >= 0)
			{
				carrier = CharacterForActor((int)wanted.carrierActorId);
				if (!carrier)
				{
					InterlockedIncrement(&g_refused);
					Log("held: REFUSED peer %u's handover on step %u, the carrier actor %d "
					    "has no character on this machine",
					    (unsigned)command.issuer, (unsigned)command.step,
					    (int)wanted.carrierActorId);
					return;
				}
			}

			if (!carrier)
			{
				if (!DetachFromCarrier(object))
				{
					InterlockedIncrement(&g_refused);
					Log("held: could not detach actor %d on step %u", (int)wanted.objectActorId,
					    (unsigned)command.step);
					return;
				}

				InterlockedIncrement(&g_applied);
				Log("held: peer %u put actor %d down, applied on step %u",
				    (unsigned)command.issuer, (int)wanted.objectActorId,
				    (unsigned)command.step);
				return;
			}

			// Work out the bone. A bone id in the command wins. A -1 means "keep the bone
			// it is on", which has to be translated: the object's m_parentJoint is an
			// index into the OLD carrier's skeleton, so it has to go back through that
			// carrier's bone point table to a logical bone id before it means anything on
			// the new one.
			int boneId = (int)wanted.boneId;
			if (boneId < 0)
			{
				CarryLink link;
				const bool haveLink = ReadCarryLink(object, &link) && link.carrier != NULL;
				if (haveLink)
					boneId = BoneIdForJoint(link.carrier, link.carrierJoint);

				if (boneId < 0)
				{
					InterlockedIncrement(&g_refused);
					Log("held: REFUSED peer %u's handover of actor %d on step %u, it asked "
					    "to keep the current bone and joint %d does not map back to a bone "
					    "id on the old carrier. Attaching anyway would put the object on "
					    "the new carrier's root.",
					    (unsigned)command.issuer, (int)wanted.objectActorId,
					    (unsigned)command.step, haveLink ? link.carrierJoint : -1);
					return;
				}
			}

			// Ask before attaching, because the engine does not report a miss: a bone id
			// the new carrier has no record for resolves to joint 0 with a zero offset,
			// which quietly puts the object at the carrier's origin rather than in its
			// hand. Better to refuse and say why.
			const int newJoint = JointForBoneId(carrier, boneId);
			if (newJoint < 0)
			{
				InterlockedIncrement(&g_refused);
				Log("held: REFUSED peer %u's handover of actor %d to actor %d on step %u, "
				    "bone id %d is not in the new carrier's bone point table. The engine "
				    "would have attached it to the carrier's root instead of refusing.",
				    (unsigned)command.issuer, (int)wanted.objectActorId,
				    (int)wanted.carrierActorId, (unsigned)command.step, boneId);
				return;
			}

			if (!AttachToCarrierBone(object, carrier, boneId))
			{
				InterlockedIncrement(&g_refused);
				Log("held: could not attach actor %d to actor %d on step %u, the guarded "
				    "attach refused. The usual cause is the carrier's character data not "
				    "being loaded yet.",
				    (int)wanted.objectActorId, (int)wanted.carrierActorId,
				    (unsigned)command.step);
				return;
			}

			// The vertex rebuild. FFX_Atel_SetActorPos does this itself on a position
			// write, and an attach is a position change by another route, so without it
			// a character with part vertex buffers keeps last frame's skinning for a
			// frame.
			MarkCharacterDirty(object);

			InterlockedIncrement(&g_applied);
			Log("held: peer %u handed actor %d to actor %d at bone %d (joint %d), applied "
			    "on step %u",
			    (unsigned)command.issuer, (int)wanted.objectActorId,
			    (int)wanted.carrierActorId, boneId, newJoint, (unsigned)command.step);
		}

		void ApplyCommandsForStep(Lockstep* clock)
		{
			// Four is already generous. A handover comes from a pickup, and a human
			// cannot produce more than one of those per step, so the only way to see
			// several is a burst after a stall.
			const Command* commands[4];
			const int count = clock->CommandsForStep(commands, 4);

			for (int i = 0; i < count; ++i)
				if (commands[i]->kind == (uint8_t)kCommandHeldObject)
					ApplyHandover(*commands[i]);
		}

	} // namespace

	bool InstallHeldObjects()
	{
		if (g_installed)
			return true;

		g_nowCount = 0;
		g_wasCount = 0;
		g_haveWas = false;
		g_reportedCount = -1;
		g_hash = 0;
		g_haveCensusMap = false;
		g_haveDrainedStep = false;

		if (!CarryApiReady())
		{
			Log("held: the CHR attach entry points did not resolve, so a carried object "
			    "cannot be handed from one character to another. The carry itself still "
			    "works, because the engine does it with a bone parent and nothing about "
			    "that needs us. What is lost is the correction for the game's single "
			    "bound-player slot, so in co-op the wrong character will carry things.");
			strcpy_s(g_status, sizeof(g_status), "held: ATTACH API NOT RESOLVED");
			return false;
		}

		g_installed = true;
		Log("held: installed. A carried object is a bone parent, so nothing about it goes "
		    "on the wire. The census runs every step and hashes who is holding what, which "
		    "is how a disagreement would be noticed.");
		BuildStatus();
		return true;
	}

	bool HeldObjectsInstalled()
	{
		return g_installed;
	}

	void StartHeldObjects()
	{
		g_started = true;
		g_haveDrainedStep = false;
		g_applied = 0;
		g_refused = 0;
		BuildStatus();
	}

	void StopHeldObjects()
	{
		g_started = false;
		g_haveDrainedStep = false;
		BuildStatus();
	}

	void ServiceHeldObjectStep()
	{
		if (!g_installed)
			return;

		Lockstep* clock = ActiveLockstep();
		const uint32_t step = clock ? clock->CurrentStep() : 0;

		// 1. The ordered commands, before the census, so a handover applied this step
		//    shows up in this step's census and reports itself as an edge rather than
		//    turning up a step late and looking like the engine did it.
		//
		//    ONLY THE COMMANDS NEED A CLOCK. The census does not, and keeping that
		//    distinction is what lets the whole research claim be re-checked in a solo
		//    game with no networking at all.
		if (g_started && clock)
		{
			if (!g_haveDrainedStep || step != g_drainedStep)
			{
				g_drainedStep = step;
				g_haveDrainedStep = true;
				ApplyCommandsForStep(clock);
			}
		}

		// 2. The census.
		const int mapId = LiveMapId();
		const bool mapChanged = !g_haveCensusMap || mapId != g_censusMapId;

		const int count = TakeCensus();
		g_nowCount = (count > 0) ? count : 0;
		g_reportedCount = count;
		g_hash = (count >= 0) ? HashCensus(g_now, g_nowCount) : 0;

		// 3. The edges. Suppressed across a transition, because a map change disposes
		//    the whole pool and rebuilds it, so every carry on the old map would report
		//    as a drop and every one on the new map as a pickup. That is thirty log
		//    lines saying nothing.
		if (count >= 0 && g_haveWas && !mapChanged)
			LogEdges(step);

		if (count >= 0)
		{
			memcpy(g_was, g_now, sizeof(CarryEntry) * (size_t)g_nowCount);
			g_wasCount = g_nowCount;
			g_haveWas = true;
		}
		else
		{
			// A refused census must not leave the previous one standing as the baseline,
			// or the next successful one reports every difference accumulated while the
			// census was blind as if it happened on one step.
			g_haveWas = false;
			g_wasCount = 0;
		}

		g_censusMapId = mapId;
		g_haveCensusMap = true;

		BuildStatus();
	}

	bool RequestHeldObjectHandover(int objectActorId, int carrierActorId, int boneId)
	{
		if (!g_installed || !g_started)
			return false;

		Lockstep* clock = ActiveLockstep();
		if (!clock)
			return false;

		// An object with no actor behind it reads -1 for its m_objId, and there is no way
		// to name such a CHR that both machines would agree on, so this refuses rather
		// than reaching for the pool slot. A slot index would look like it worked.
		if (objectActorId < 0 || objectActorId > 0x7FFF)
			return false;
		if (carrierActorId > 0x7FFF || boneId > 0x7FFF)
			return false;

		const int mapId = LiveMapId();
		if (mapId < 0 || mapId > 0xFFFF)
			return false;

		HeldObjectCommand wanted;
		memset(&wanted, 0, sizeof(wanted));
		wanted.mapId = (uint16_t)mapId;
		wanted.objectActorId = (int16_t)objectActorId;
		wanted.carrierActorId = (int16_t)((carrierActorId < 0) ? -1 : carrierActorId);
		wanted.boneId = (int16_t)((boneId < 0) ? -1 : boneId);

		if (!clock->RequestCommand((uint8_t)kCommandHeldObject, &wanted, (int)sizeof(wanted)))
			return false;

		Log("held: asking for actor %d to be carried by actor %d at bone %d, on map %d",
		    objectActorId, carrierActorId, boneId, mapId);
		return true;
	}

	int HeldObjectCount()
	{
		return g_reportedCount;
	}

	uint32_t HeldObjectsHash()
	{
		return g_hash;
	}

	int HeldObjectCarrier(int objectActorId)
	{
		if (objectActorId < 0 || g_reportedCount < 0)
			return -1;

		for (int i = 0; i < g_nowCount; ++i)
			if (g_now[i].objectActorId == objectActorId)
				return g_now[i].carrierActorId;

		return -1;
	}

	const char* HeldObjectsStatus()
	{
		return g_status;
	}

	void LogHeldObjects()
	{
		Log("=== held objects ===");
		Log("%s", g_status);

		if (!g_installed)
		{
			Log("not installed, so nothing is watching who is carrying what. The carries "
			    "themselves still work: FFX attaches a carried object with a bone parent "
			    "and both machines derive the transform for free.");
			return;
		}

		Log("the attachment question is ANSWERED: bone parent, not physics and not a "
		    "per-frame script write. FFX_Ch_BuildSkinMatrices 0x832760 derives a carried "
		    "object's world matrix from its carrier, m_parentJoint and m_attachOffset and "
		    "reads none of the object's own position. So nothing streams.");

		if (g_reportedCount < 0)
		{
			Log("no census this step. Either the CHR pool is not allocated yet, or there "
			    "were more than %d carries and the census refused rather than truncating. "
			    "%ld overflows so far.",
			    kMaxTrackedCarries, g_overflows);
			return;
		}

		Log("map %d, %d carries, census hash %08lX", g_censusMapId, g_reportedCount,
		    (unsigned long)g_hash);

		for (int i = 0; i < g_nowCount; ++i)
		{
			const CarryEntry& entry = g_now[i];

			// The bone id as well as the joint, because the joint is the thing that is
			// meaningless off this skeleton and the bone id is the thing a handover
			// would actually use.
			int boneId = -1;
			Character* carrier = CharacterFromSlot((LONG)entry.carrierSlot);
			if (carrier)
				boneId = BoneIdForJoint(carrier, entry.carrierJoint);

			// The position comes off the CARRIER's bone, not off the object, and that is
			// the point. The object's own m_pos is stale while it is held, so the carrier
			// bone is where the thing visibly is.
			float xyz[3] = { 0.0f, 0.0f, 0.0f };
			const bool havePos = boneId >= 0 && BoneWorldPosition(carrier, boneId, xyz);

			Log("  chr %d slot %d actor %d  <- carrier actor %d slot %d, joint %d, bone "
			    "%d%s",
			    entry.objectChrId, entry.objectSlot, entry.objectActorId,
			    entry.carrierActorId, entry.carrierSlot, entry.carrierJoint, boneId,
			    (entry.objectActorId < 0)
			        ? "   NO ACTOR ID, so this one cannot be named on a wire"
			        : "");

			if (havePos)
				Log("      bone is at %.1f %.1f %.1f in the world. Remember +Y IS DOWN, "
				    "and that the object's own m_pos is STALE while it is carried.",
				    xyz[0], xyz[1], xyz[2]);
		}

		Log("%ld picked up, %ld put down, %ld changed hands, %ld ordered handovers "
		    "applied, %ld refused",
		    g_pickups, g_drops, g_handovers, g_applied, g_refused);

		if (!g_started)
			Log("the command half is idle, so a handover cannot be asked for. That is "
			    "normal with no session.");

		Log("NOTHING CALLS RequestHeldObjectHandover yet, on purpose. Asking for one needs "
		    "to know which PEER earned the pickup, and that answer has to be identical on "
		    "both machines. TriggerPass knows it and does not expose it. See "
		    "world/HeldObjects.h.");
		Log("HeldObjectsHash is folded into the lockstep checksum as region 15, so a "
		    "carry disagreement is reported as a desync at a step number rather than "
		    "only showing up here.");
	}

} // namespace pilgrimage
