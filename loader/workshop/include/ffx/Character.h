#pragma once

#include <windows.h>

#include "ffx/Api.h"

// Reading and writing a CHR, and finding one in the pool.
//
// The pool is a flat array at stride 0x880, so a slot INDEX is a stable identity
// across a frame and the natural network id later on. A raw Character pointer is
// NOT stable across a map transition, because the pool is freed and reallocated,
// which is why the clone roster holds indices.

namespace ffx
{

	// Field access. All volatile, because the game thread writes these behind us.
	inline BYTE* FieldAt(Character* chr, DWORD offset)
	{
		return (BYTE*)chr + offset;
	}

	inline BYTE ByteAt(Character* chr, DWORD offset)
	{
		return *(volatile BYTE*)FieldAt(chr, offset);
	}
	inline short ShortAt(Character* chr, DWORD offset)
	{
		return *(volatile short*)FieldAt(chr, offset);
	}
	inline DWORD DwordAt(Character* chr, DWORD offset)
	{
		return *(volatile DWORD*)FieldAt(chr, offset);
	}
	inline float FloatAt(Character* chr, DWORD offset)
	{
		return *(volatile float*)FieldAt(chr, offset);
	}

	inline void WriteFloat(Character* chr, DWORD offset, float value)
	{
		*(volatile float*)FieldAt(chr, offset) = value;
	}

	inline void WriteByte(Character* chr, DWORD offset, BYTE value)
	{
		*(volatile BYTE*)FieldAt(chr, offset) = value;
	}

	// Is the pool allocated and non-empty? Nothing here works before a map loads.
	bool PoolIsReady();

	Character* CharacterFromSlot(LONG slot);
	LONG SlotOfCharacter(Character* chr);

	// FFX_Ch_Allocate memsets through a NULL pointer when every slot is taken. Its
	// own slot search is "first entry whose in-use byte at +2 is zero", so this runs
	// exactly that search and lets the caller refuse. Not belt and braces, it is the
	// documented precondition.
	bool PoolHasFreeSlot();

	// Convenience wrapper, because Game.IsLive returning 0 for live reads backwards
	// at every call site.
	inline bool IsLive(Character* chr)
	{
		return chr && Game.IsLive(chr) == 0;
	}

	Character* LivePlayerCharacter();

	// m_rotY, the facing in radians about Y, zero along +Z. The engine slews it toward
	// m_moveDir when m_flags1 bit 0x400 is set, so it is where the character is looking
	// rather than where it is heading. That is the one a facing cone wants.
	//
	// The bound API has GetPos and no facing accessor, which is why this is here rather
	// than in Api.h. Reading it needs no call.
	const DWORD ChrRotY = 0x158;

	inline float CharacterFacing(Character* chr)
	{
		return chr ? FloatAt(chr, ChrRotY) : 0.0f;
	}

} // namespace ffx
