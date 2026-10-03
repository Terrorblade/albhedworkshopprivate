#include "ffx/Character.h"

#include "ffx/Layout.h"

namespace ffx
{

	bool PoolIsReady()
	{
		return *Game.chrArray != NULL && *Game.chrCount > 0;
	}

	Character* CharacterFromSlot(LONG slot)
	{
		if (slot < 0)
			return NULL;
		Character* base = *Game.chrArray;
		if (!base)
			return NULL;
		if (slot >= *Game.chrCount)
			return NULL;
		return (Character*)((BYTE*)base + Chr::Stride * (DWORD)slot);
	}

	LONG SlotOfCharacter(Character* chr)
	{
		Character* base = *Game.chrArray;
		if (!base || !chr)
			return -1;
		ptrdiff_t delta = (BYTE*)chr - (BYTE*)base;
		if (delta < 0 || (delta % Chr::Stride) != 0)
			return -1;
		LONG slot = (LONG)(delta / Chr::Stride);
		return (slot < *Game.chrCount) ? slot : -1;
	}

	bool PoolHasFreeSlot()
	{
		Character* base = *Game.chrArray;
		int count = *Game.chrCount;
		if (!base || count <= 0)
			return false;
		for (int i = 0; i < count; ++i)
		{
			BYTE* entry = (BYTE*)base + Chr::Stride * (DWORD)i;
			if (*(volatile BYTE*)(entry + Chr::InUse) == 0)
				return true;
		}
		return false;
	}

	Character* LivePlayerCharacter()
	{
		Character* player = Game.GetPlayerChr();
		return IsLive(player) ? player : NULL;
	}

} // namespace ffx
