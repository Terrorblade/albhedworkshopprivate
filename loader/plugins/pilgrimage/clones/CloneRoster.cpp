#include "clones/CloneRoster.h"

#include "workshop/Log.h"
#include "ffx/Character.h"

namespace pilgrimage
{

	// The game library. Anything unqualified below that looks like FFX
	// knowledge, or like mod plumbing, comes from one of these two.
	using namespace ffx;
	using namespace workshop;

	namespace
	{

		volatile LONG entrySlots[MaxClones];
		volatile LONG activeEntry = 0;

		// peer -> roster entry, -1 for unbound. See the header for why it lives here.
		// Spelled out as three initialisers rather than left to zero init, because a
		// zero here would read as "peer 0's body is entry 0" and entry 0 is a real
		// entry. ResetRoster clears it again at plugin start.
		volatile LONG entryForPeer[workshop::MaxPlayers] = { -1, -1, -1 };

	} // namespace

	void ResetRoster()
	{
		for (LONG i = 0; i < MaxClones; ++i)
			entrySlots[i] = -1;
		activeEntry = 0;
		ClearPeerBodies();
	}

	LONG FirstFreeEntry()
	{
		for (LONG i = 0; i < MaxClones; ++i)
			if (entrySlots[i] < 0)
				return i;
		return -1;
	}

	LONG LiveCloneCount()
	{
		LONG count = 0;
		for (LONG i = 0; i < MaxClones; ++i)
			if (entrySlots[i] >= 0)
				++count;
		return count;
	}

	LONG SlotOfEntry(LONG entry)
	{
		if (entry < 0 || entry >= MaxClones)
			return -1;
		return entrySlots[entry];
	}

	void ClaimEntry(LONG entry, LONG poolSlot)
	{
		if (entry < 0 || entry >= MaxClones)
			return;
		InterlockedExchange(&entrySlots[entry], poolSlot);
	}

	void ReleaseEntry(LONG entry)
	{
		if (entry < 0 || entry >= MaxClones)
			return;
		InterlockedExchange(&entrySlots[entry], -1);

		// UNBIND ANYBODY POINTING AT IT, in the same breath as releasing it. An entry
		// is reused by the next spawn, so a peer left pointing at a released entry
		// would silently acquire whatever body got that entry next. That is the same
		// class of bug as the reused-pool-slot one PruneDeadClones guards against,
		// one level up.
		for (int peer = 0; peer < workshop::MaxPlayers; ++peer)
		{
			if (entryForPeer[peer] != entry)
				continue;
			InterlockedExchange(&entryForPeer[peer], -1);
			Log("clone roster: peer %d's body was entry %ld, which has just been "
			    "released, so that peer has no body now",
			    peer, entry);
		}
	}

	// ---------------------------------------------------------------------------
	// The peer -> body map. See the header.
	// ---------------------------------------------------------------------------

	void BindPeerBody(int peer, LONG rosterEntry)
	{
		if (peer < 0 || peer >= workshop::MaxPlayers)
		{
			Log("clone roster: refusing to bind peer %d, the session only has peers 0 "
			    "to %d",
			    peer, workshop::MaxPlayers - 1);
			return;
		}

		if (rosterEntry < -1 || rosterEntry >= MaxClones)
		{
			Log("clone roster: refusing to bind peer %d to entry %ld, which is not an "
			    "entry",
			    peer, rosterEntry);
			return;
		}

		const LONG previous = entryForPeer[peer];
		if (previous == rosterEntry)
			return;

		InterlockedExchange(&entryForPeer[peer], rosterEntry);

		if (rosterEntry < 0)
			Log("clone roster: peer %d has no body now (was entry %ld)", peer, previous);
		else if (previous >= 0)
			Log("clone roster: peer %d's body moved from entry %ld to entry %ld, pool "
			    "slot %ld",
			    peer, previous, rosterEntry, SlotOfEntry(rosterEntry));
		else
			Log("clone roster: peer %d's body is entry %ld, pool slot %ld", peer,
			    rosterEntry, SlotOfEntry(rosterEntry));
	}

	LONG PeerBodyEntry(int peer)
	{
		if (peer < 0 || peer >= workshop::MaxPlayers)
			return -1;
		return entryForPeer[peer];
	}

	Character* PeerBodyCharacter(int peer)
	{
		const LONG entry = PeerBodyEntry(peer);
		if (entry < 0)
			return NULL;

		const LONG slot = SlotOfEntry(entry);
		if (slot < 0)
			return NULL;

		Character* chr = CharacterFromSlot(slot);
		return IsLive(chr) ? chr : NULL;
	}

	int PeerOfBody(Character* chr)
	{
		if (!chr)
			return -1;
		for (int peer = 0; peer < workshop::MaxPlayers; ++peer)
			if (PeerBodyCharacter(peer) == chr)
				return peer;
		return -1;
	}

	int PeerBodyCount()
	{
		int count = 0;
		for (int peer = 0; peer < workshop::MaxPlayers; ++peer)
			if (PeerBodyCharacter(peer))
				++count;
		return count;
	}

	void ClearPeerBodies()
	{
		for (int peer = 0; peer < workshop::MaxPlayers; ++peer)
			InterlockedExchange(&entryForPeer[peer], -1);
	}

	LONG ActiveEntry()
	{
		LONG entry = activeEntry;
		return (entry < 0 || entry >= MaxClones) ? 0 : entry;
	}

	Character* ActiveClone()
	{
		return CharacterFromSlot(SlotOfEntry(ActiveEntry()));
	}

	void SetActiveEntry(LONG entry)
	{
		if (entry < 0 || entry >= MaxClones)
			return;
		InterlockedExchange(&activeEntry, entry);
	}

	void CycleActiveClone()
	{
		LONG start = ActiveEntry();
		for (LONG step = 1; step <= MaxClones; ++step)
		{
			LONG candidate = (start + step) % MaxClones;
			if (entrySlots[candidate] >= 0)
			{
				SetActiveEntry(candidate);
				SetStatus("driving clone %ld of %ld", candidate + 1, LiveCloneCount());
				Log("input focus moved to clone %ld (pool slot %ld)",
				    candidate + 1, entrySlots[candidate]);
				return;
			}
		}
		SetStatus("no live clone to drive");
	}

	bool IsManagedClone(Character* chr)
	{
		if (!chr)
			return false;
		for (LONG i = 0; i < MaxClones; ++i)
		{
			LONG slot = entrySlots[i];
			if (slot >= 0 && CharacterFromSlot(slot) == chr)
				return true;
		}
		return false;
	}

	LONG PruneDeadClones(Character** outLive)
	{
		const LONG before = LiveCloneCount();
		LONG live = 0;
		for (LONG i = 0; i < MaxClones; ++i)
		{
			LONG slot = entrySlots[i];
			if (slot < 0)
				continue;

			Character* chr = CharacterFromSlot(slot);
			if (!IsLive(chr))
			{
				// The pool is freed and reallocated on every map transition, so a
				// clone does not survive one. Notice rather than keep writing into a
				// slot that now belongs to somebody else.
				Log("clone %ld (pool slot %ld) is no longer live (map change?), dropping it",
				    i + 1, slot);
				ReleaseEntry(i);
				continue;
			}
			if (outLive)
				outLive[live] = chr;
			++live;
		}
		if (before > 0 && live == 0)
			SetStatus("clones lost on map change, press F9 again");
		return live;
	}

} // namespace pilgrimage
