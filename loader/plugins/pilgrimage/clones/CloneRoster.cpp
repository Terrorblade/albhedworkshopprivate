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

	} // namespace

	void ResetRoster()
	{
		for (LONG i = 0; i < MaxClones; ++i)
			entrySlots[i] = -1;
		activeEntry = 0;
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
