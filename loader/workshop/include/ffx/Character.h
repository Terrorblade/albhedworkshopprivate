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

	// ---------------------------------------------------------------------------
	// Held and carried objects
	//
	// FFX attaches a carried object to its carrier with a plain bone parent. Nothing
	// physical, nothing scripted per frame. The whole story is four fields on the
	// CARRIED character, and FFX_Ch_BuildSkinMatrices derives the carried object's
	// world matrix from them and from the carrier every frame:
	//
	//   m_parent       a live CHR pointer to the carrier
	//   m_parentUid    the carrier's ATEL ACTOR ID, which is the durable name
	//   m_parentJoint  a joint index on the carrier's skeleton
	//   m_attachOffset a float4 offset in that joint's frame
	//
	// There is a fifth, Chr::AttachScale at 0x1B4, and CarryLink deliberately leaves it
	// out. It is a scale factor rather than part of the identity, it defaults to 1.0,
	// and every writer of it is replicated, so nothing a mod does needs it. Read it
	// straight off the character if you ever want it.
	//
	// Two things follow and both are in ffx/Layout.h in full. One, a carried object
	// needs no transform replication at all, because both machines derive it. Two,
	// m_pos of a carried object is STALE, so BoneWorldPosition is what tells you where
	// it actually is.
	//
	// THE BONE ID VERSUS THE JOINT INDEX, because getting this wrong is the trap here.
	// A joint index is an index into one particular skeleton's joint array and means
	// nothing on another character. A LOGICAL BONE ID is a semantic number, 0..21,
	// looked up per skeleton in the CHRDATA bone point table. So moving a carried
	// object from one carrier to another has to go bone id -> new joint index, which is
	// what AttachToCarrierBone does. Copying m_parentJoint across would attach the
	// object to whatever joint happened to have that index, and a bone id the new
	// skeleton has no record for silently lands the object on the root.
	// ---------------------------------------------------------------------------

	// What a carried character's four carry fields say. Read in one go so a caller
	// cannot see half of a carry that changed under it.
	struct CarryLink
	{
		Character* carrier; // m_parent. NULL means not carried. Not wire safe.
		int carrierActorId; // m_parentUid, an ATEL actor id, or -1
		int carrierJoint;   // m_parentJoint, an index into the CARRIER's skeleton
		float offset[4];    // m_attachOffset, in the carrier joint's frame
	};

	// False, with *out zeroed, when chr is NULL or not readable. A character that is
	// not carried reads back true with carrier NULL, because "not carried" is an
	// answer and not a failure.
	bool ReadCarryLink(Character* chr, CarryLink* out);

	bool IsCarried(Character* chr);

	// m_objId. For a CHR an event script spawned this is its ATEL ACTOR ID, and -1
	// for anything the engine allocated without one. This is the only identity on a
	// CHR that means the same thing on two machines, so it is what a carry goes on a
	// wire as.
	int CharacterActorId(Character* chr);

	// Are the engine's three attach entry points resolvable and do they look like
	// functions? Worth asking once at startup rather than finding out from a fault.
	bool CarryApiReady();

	// The logical bone id that resolves to this joint index on this character's
	// skeleton, or -1. A reverse walk of the CHRDATA bone point table, which is how
	// a carry gets translated from one carrier to another.
	//
	// AMBIGUITY IS REAL AND IS RESOLVED BY TAKING THE FIRST MATCH. More than one bone
	// point record can name the same joint, one plain and one with an offset. The
	// first record wins, which matches the engine's own forward lookup, since
	// FFX_Ch_LookupBonePoint also stops at the first match.
	int BoneIdForJoint(Character* chr, int jointIndex);

	// The joint index a logical bone id resolves to on this character, or -1 when the
	// skeleton has no record for it. Goes through the engine's own
	// FFX_Ch_LookupBonePoint so the answer matches what an attach would do.
	int JointForBoneId(Character* chr, int boneId);

	// Where a bone actually is in the world, from m_boneWorldPos. boneId is a LOGICAL
	// BONE ID, 0..21, and is range checked here because the engine's own getter is
	// not. False when there is nothing readable.
	//
	// This is the one to use on a carried object. Its m_pos is stale, but its bone
	// positions are not, because FFX_Ch_UpdateBonePositions derives them from the
	// world matrix that the parented branch does fill in.
	bool BoneWorldPosition(Character* chr, int boneId, float* xyz);

	// Attach chr to carrier at a LOGICAL BONE ID, which is the engine's own
	// FFX_Ch_AttachToParentBone. Pass a NULL carrier to detach.
	//
	// Guarded, because the engine's path is not: FFX_Ch_LookupBonePoint dereferences
	// the carrier's m_data with no null check, so a carrier whose character data has
	// not loaded faults inside the engine. This refuses instead.
	//
	// Returns false on a refusal. It does NOT return false when the bone id is absent
	// from the carrier's table, because the engine does not report that either: it
	// resolves joint 0 and a zero offset, which puts the object on the carrier's root.
	// Ask JointForBoneId first if you need to know.
	bool AttachToCarrierBone(Character* chr, Character* carrier, int boneId);

	// The same thing by JOINT INDEX, no bone point lookup. This is
	// FFX_Ch_AttachToParentJoint, and it is the safest of the three to call because it
	// is pure field writes and touches no character data. Only correct when the joint
	// index came from the SAME skeleton you are attaching to.
	bool AttachToCarrierJoint(Character* chr, Character* carrier, int jointIndex);

	// Clears the carry. m_parent to NULL and m_parentUid to -1, through the engine's
	// own path so anything else it does comes along.
	bool DetachFromCarrier(Character* chr);

	// Every other CHR field offset lives in ffx/Layout.h as namespace Chr, and the
	// m_flags1 bits as namespace ChrFlag1. Do not start a second set here.

} // namespace ffx
