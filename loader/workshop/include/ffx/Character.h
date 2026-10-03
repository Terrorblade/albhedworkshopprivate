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

	// ---------------------------------------------------------------------------
	// Placing a character, and the one-shot that can swallow it.
	// ---------------------------------------------------------------------------

	// Is the SetPos suppression one-shot armed? While it is, the very next
	// FFX_Ch_SetPos on ANY character does nothing: it clears the flag, returns, and
	// never touches the position. The call after that behaves normally.
	//
	// IN THIS BUILD IT IS ALWAYS FALSE, and that was established properly rather than
	// hoped: the only writer that sets it has zero callers, it is not in the magic
	// plugin host API table, and the byte lives in uninitialised .data. Nothing in the
	// shipped game can arm it.
	//
	// So this is here for two reasons and neither is "guard against the engine". One,
	// a future patch or a magic DLL could arm it, and the failure it produces is
	// invisible: a swallowed SetPos does not fault and does not log, the character just
	// stays where it was. Two, a mod could arm it by accident. Both are worth one
	// cheap read on a path that runs once per join.
	bool SetPosSuppressed();

	// Disarm it. Returns what it was. Costs a byte write and cannot break anything,
	// because nothing arms it and nothing reads it back.
	bool ClearSetPosSuppression();

	// FFX_Ch_MarkDirty. Sets m_flags1 bit 0x10 and re-copies every part's skinned
	// vertices from the source mesh, then marks the children dirty as well. The
	// vertex work is gated on m_flags1 bit 0x200000, so for most characters this is
	// just the flag.
	//
	// FFX_Atel_SetActorPos already calls this, so a placement through the ATEL path
	// does not need it. A CHR-only position write does.
	void MarkCharacterDirty(Character* chr);

	// Zero every field that would carry the character's old motion into its new
	// position. Call this as part of a placement, or the character arrives already
	// moving in whatever direction it was heading when it was picked up.
	//
	// It also matters for the rebind: FFX_Ch_WalkmeshMove reads velocity as well as
	// position, so with the velocity zeroed it is a pure bind rather than a bind plus
	// a step.
	void StopCharacterMotion(Character* chr);

	// Point a character in a direction and have it STAY pointed there.
	//
	// Two writes, and the second is the one that is easy to miss. FFX_Ch_SetRot writes
	// m_rotY, which is where the character is looking, but when m_flags1 bit 0x400 is
	// set the locomotion driver slews m_rotY back toward m_moveDir every frame. So
	// setting the rotation alone makes a character turn to the heading you asked for
	// and then smoothly turn away again. Writing m_moveDir to the same angle removes
	// anywhere for it to slew to.
	//
	// Goes through FFX_Ch_SetRot rather than the field because that function validates
	// the angle and zeroes m_rotY on a bad one, which is a NaN guard worth keeping.
	void SetCharacterFacing(Character* chr, float radians);

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

	// Every other CHR field offset lives in ffx/Layout.h as namespace Chr, and the
	// m_flags1 bits as namespace ChrFlag1. Do not start a second set here.

} // namespace ffx
