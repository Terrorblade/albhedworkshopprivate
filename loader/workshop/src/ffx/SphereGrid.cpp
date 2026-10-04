#include "ffx/SphereGrid.h"

#include "ffx/GameState.h"
#include "ffx/MenuSystem.h"
#include "ffx/addresses/GameState.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using namespace workshop;

	namespace
	{

		const int kNodeStride = 2;
		const int kOffKind = 0;
		const int kOffMask = 1;

		// Byte offsets from the blob base. The 1280 cosmetic link bytes sit at 0x0A00,
		// between the nodes and the cursors, and nothing here touches them.
		const DWORD kOffCursors = 0x0F00; // 7 WORDs
		const DWORD kBlobBytes = 0x0F1A;

		BYTE* Blob()
		{
			// Goes through GameLoaded rather than just Readable, because the blob is
			// inside a static buffer that reads fine at the title screen with nothing in
			// it. A zeroed grid would then look like 1280 empty slots, which is a
			// plausible answer to the wrong question.
			if (!GameLoaded())
				return nullptr;

			BYTE* base = (BYTE*)ModuleAddress(Rva::SphereGridNodes);
			return Readable(base, kBlobBytes) ? base : nullptr;
		}

		// Reads for free, writes only when the grid screen is not holding the data.
		BYTE* WritableBlob()
		{
			if (SphereGridMenuOpen())
				return nullptr;
			return Blob();
		}

		BYTE* NodeAt(int slot)
		{
			if (slot < 0 || slot >= kGridNodeSlots)
				return nullptr;

			BYTE* base = Blob();
			if (!base)
				return nullptr;
			return base + (size_t)slot * kNodeStride;
		}

		bool ValidCharacter(BYTE charIndex)
		{
			return charIndex < (BYTE)kGridCharacters;
		}

		// Sets or clears one character's bit across the scanned range, and reports how
		// many slots it actually changed.
		int SetAll(BYTE charIndex, bool on)
		{
			if (!ValidCharacter(charIndex))
				return 0;

			BYTE* base = WritableBlob();
			if (!base)
				return 0;

			const BYTE bit = (BYTE)(1 << charIndex);
			int changed = 0;

			// Only the scanned range. A bit set past slot 1023 is read by nothing, so
			// writing there would report success and change no stats.
			for (int slot = 0; slot < kGridNodeSlotsScanned; ++slot)
			{
				BYTE* node = base + (size_t)slot * kNodeStride;
				if (node[kOffKind] == (BYTE)kGridNodeEmpty)
					continue;

				const BYTE was = node[kOffMask];
				const BYTE now = on ? (BYTE)(was | bit) : (BYTE)(was & ~bit);
				if (now == was)
					continue;

				node[kOffMask] = now;
				++changed;
			}
			return changed;
		}

	} // namespace

	bool SphereGridReadable()
	{
		return Blob() != nullptr;
	}

	bool SphereGridMenuOpen()
	{
		return MenuSystemRunning() && ModuleActive(kMenuModuleSphereGrid);
	}

	int GridNodeKind(int slot)
	{
		const BYTE* node = NodeAt(slot);
		return node ? (int)node[kOffKind] : -1;
	}

	int GridNodeMask(int slot)
	{
		const BYTE* node = NodeAt(slot);
		return node ? (int)node[kOffMask] : -1;
	}

	bool GridNodeActivated(int slot, BYTE charIndex)
	{
		if (!ValidCharacter(charIndex))
			return false;

		const int mask = GridNodeMask(slot);
		if (mask < 0)
			return false;
		return (mask & (1 << charIndex)) != 0;
	}

	bool SetGridNodeActivated(int slot, BYTE charIndex, bool on)
	{
		if (!ValidCharacter(charIndex))
			return false;
		if (slot < 0 || slot >= kGridNodeSlots)
			return false;

		BYTE* base = WritableBlob();
		if (!base)
			return false;

		BYTE* node = base + (size_t)slot * kNodeStride;

		// Refuse an empty slot. Setting a bit on one is harmless but it is also
		// meaningless, and returning true would say otherwise.
		if (node[kOffKind] == (BYTE)kGridNodeEmpty)
			return false;

		const BYTE bit = (BYTE)(1 << charIndex);
		if (on)
			node[kOffMask] = (BYTE)(node[kOffMask] | bit);
		else
			node[kOffMask] = (BYTE)(node[kOffMask] & ~bit);
		return true;
	}

	int GridNodeCount()
	{
		const BYTE* base = Blob();
		if (!base)
			return 0;

		int count = 0;
		for (int slot = 0; slot < kGridNodeSlotsScanned; ++slot)
			if (base[(size_t)slot * kNodeStride + kOffKind] != (BYTE)kGridNodeEmpty)
				++count;
		return count;
	}

	int GridActivatedCount(BYTE charIndex)
	{
		if (!ValidCharacter(charIndex))
			return 0;

		const BYTE* base = Blob();
		if (!base)
			return 0;

		const BYTE bit = (BYTE)(1 << charIndex);
		int count = 0;
		for (int slot = 0; slot < kGridNodeSlotsScanned; ++slot)
		{
			const BYTE* node = base + (size_t)slot * kNodeStride;
			if (node[kOffKind] == (BYTE)kGridNodeEmpty)
				continue;
			if (node[kOffMask] & bit)
				++count;
		}
		return count;
	}

	int ActivateAllNodes(BYTE charIndex)
	{
		const int changed = SetAll(charIndex, true);
		if (changed)
			Log("sphere grid: activated %d nodes for character %d. Call RecomputeDerivedStats or the stats will not move.", changed, (int)charIndex);
		return changed;
	}

	int ClearAllNodes(BYTE charIndex)
	{
		const int changed = SetAll(charIndex, false);
		if (changed)
			Log("sphere grid: cleared %d nodes for character %d", changed, (int)charIndex);
		return changed;
	}

	int GridCursorNode(BYTE charIndex)
	{
		if (!ValidCharacter(charIndex))
			return -1;

		const BYTE* base = Blob();
		if (!base)
			return -1;

		const WORD* cursors = (const WORD*)(base + kOffCursors);
		return (int)cursors[charIndex];
	}

	int GridId()
	{
		if (!GameLoaded())
			return -1;

		const DWORD* flags = (const DWORD*)ModuleAddress(Rva::SaveDataOptionFlags);
		if (!Readable(flags, 4))
			return -1;
		return (int)((*flags >> 14) & 3);
	}

	void LogSphereGrid()
	{
		if (!SphereGridReadable())
		{
			Log("sphere grid: no game loaded");
			return;
		}

		Log("sphere grid: grid %d, %d of %d slots hold a node", GridId(), GridNodeCount(), kGridNodeSlotsScanned);
		for (BYTE i = 0; i < (BYTE)kGridCharacters; ++i)
			Log("sphere grid: %-10s %4d activated, cursor at slot %d", CharacterName(i), GridActivatedCount(i), GridCursorNode(i));
	}

} // namespace ffx
