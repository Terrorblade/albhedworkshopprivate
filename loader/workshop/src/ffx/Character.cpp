#include "ffx/Character.h"

#include "ffx/addresses/Character.h"
#include "ffx/Layout.h"
#include "workshop/HostModule.h"

namespace ffx
{

	using workshop::ModuleAddress;
	using workshop::Readable;

	bool SetPosSuppressed()
	{
		const BYTE* flag = (const BYTE*)ModuleAddress(Rva::SuppressNextSetPos);
		if (!Readable(flag, 1))
			return false;
		return *(volatile const BYTE*)flag != 0;
	}

	bool ClearSetPosSuppression()
	{
		BYTE* flag = (BYTE*)ModuleAddress(Rva::SuppressNextSetPos);
		if (!Readable(flag, 1))
			return false;

		const bool was = *(volatile BYTE*)flag != 0;
		*(volatile BYTE*)flag = 0;
		return was;
	}

	void MarkCharacterDirty(Character* chr)
	{
		if (!chr)
			return;

		typedef void(__cdecl * MarkDirtyFn)(Character*);
		((MarkDirtyFn)ModuleAddress(Rva::ChMarkDirty))(chr);
	}

	void StopCharacterMotion(Character* chr)
	{
		if (!chr)
			return;

		// m_speed FIRST, because it is the one that actually matters. The three linear
		// velocity components are zeroed every frame anyway, inside
		// FFX_Ch_ResolveCollisionsAll, so they are not what carries motion across a
		// placement. m_speed is: the locomotion driver turns it back into velocity on
		// the next step, so a character placed with speed still set walks away from
		// where you put it.
		//
		// Through the engine's setter rather than the field, so whatever else that
		// setter does comes along.
		if (Game.SetMoveSpeed)
			Game.SetMoveSpeed(chr, 0.0f);
		else
			WriteFloat(chr, Chr::Speed, 0.0f);

		// The velocities anyway, cheap and it makes the next FFX_Ch_WalkmeshMove a pure
		// bind. That matters off the mesh: when the triangle search fails, the engine
		// does posX += velX * 10 and posZ += velZ * 10 and runs nothing else, so a
		// character with velocity gets flung and one without just sits there.
		WriteFloat(chr, Chr::VelocityX, 0.0f);
		WriteFloat(chr, Chr::VelocityY, 0.0f);
		WriteFloat(chr, Chr::VelocityZ, 0.0f);

		// m_vertVel is separate and the water mode drives it, so a character picked up
		// while swimming would otherwise arrive still rising or sinking.
		WriteFloat(chr, Chr::VerticalVelocity, 0.0f);

		// m_moveDir is deliberately NOT touched. It is the facing target, not a
		// velocity, and zeroing it would spin the character to face +Z. Placement sets
		// it through SetCharacterFacing instead.
	}

	void SetCharacterFacing(Character* chr, float radians)
	{
		if (!chr)
			return;

		// One engine call writes both halves. FFX_Ch_SetRotAndMoveDir is not in the
		// bound Api table, so it is resolved here.
		typedef Character*(__cdecl * SetRotAndMoveDirFn)(Character*, float);
		SetRotAndMoveDirFn both =
		    (SetRotAndMoveDirFn)ModuleAddress(Rva::ChSetRotAndMoveDir);
		if (both)
		{
			both(chr, radians);
			return;
		}

		// The two call fallback, which is what the one call does anyway.
		if (Game.SetRot)
			Game.SetRot(chr, radians);
		else
			WriteFloat(chr, Chr::Facing, radians);

		if (Game.SetMoveDir)
			Game.SetMoveDir(chr, radians);
		else
			WriteFloat(chr, Chr::MoveDirection, radians);
	}

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
