#include "ffx/Character.h"

#include "ffx/addresses/Character.h"
#include "ffx/Layout.h"
#include "workshop/HostModule.h"

namespace ffx
{

	using workshop::LooksLikeFunctionStart;
	using workshop::LooksLikePointer;
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

	// ---------------------------------------------------------------------------
	// Held and carried objects. See the long comment in ffx/Character.h, and
	// namespace Chr in ffx/Layout.h for where each field came from.
	// ---------------------------------------------------------------------------

	bool ReadCarryLink(Character* chr, CarryLink* out)
	{
		if (!out)
			return false;

		out->carrier = NULL;
		out->carrierActorId = -1;
		out->carrierJoint = -1;
		out->offset[0] = out->offset[1] = out->offset[2] = 0.0f;
		out->offset[3] = 1.0f;

		// The whole span from m_parent to m_parentUid in one readability check, so a
		// half built CHR cannot give back a parent pointer with a garbage joint index
		// beside it.
		if (!chr)
			return false;
		const BYTE* span = (const BYTE*)chr + Chr::Parent;
		if (!Readable(span, (Chr::ParentUid + 4) - Chr::Parent))
			return false;

		const DWORD rawParent = DwordAt(chr, Chr::Parent);
		if (rawParent == 0)
			return true; // not carried, which is an answer and not a failure

		// A parent that is not a pointer means the record is being torn down or the
		// offset is wrong, and either way following it is the fault this is here to
		// avoid.
		if (!LooksLikePointer(rawParent))
			return false;

		out->carrier = (Character*)(UINT_PTR)rawParent;
		out->carrierActorId = (int)DwordAt(chr, Chr::ParentUid);
		out->carrierJoint = (int)DwordAt(chr, Chr::ParentJoint);
		for (int i = 0; i < 4; ++i)
			out->offset[i] = FloatAt(chr, Chr::AttachOffset + 4 * (DWORD)i);

		return true;
	}

	bool IsCarried(Character* chr)
	{
		CarryLink link;
		return ReadCarryLink(chr, &link) && link.carrier != NULL;
	}

	int CharacterActorId(Character* chr)
	{
		if (!chr)
			return -1;
		if (!Readable((const BYTE*)chr + Chr::ObjId, 4))
			return -1;
		return (int)DwordAt(chr, Chr::ObjId);
	}

	bool CarryApiReady()
	{
		// LooksLikeFunctionStart follows a jmp thunk, which matters here: the Ch family
		// is full of COMDAT folded single-jump entry points and refusing one would
		// refuse a function that is perfectly fine to call.
		return LooksLikeFunctionStart(Rva::ChAttachToParentBone) &&
		       LooksLikeFunctionStart(Rva::ChAttachToParentJoint) &&
		       LooksLikeFunctionStart(Rva::ChLookupBonePoint) &&
		       LooksLikeFunctionStart(Rva::ChGetBoneWorldPos);
	}

	namespace
	{

		// The bone point record array for a character, or NULL. Shared by the forward
		// and the reverse lookup, and the reason both are guarded the same way: the
		// chain is chr -> m_data -> m_data[108] -> records, and the engine checks only
		// the third link.
		const BYTE* BonePointRecords(Character* chr, int* outCount)
		{
			if (outCount)
				*outCount = 0;
			if (!chr)
				return NULL;
			if (!Readable((const BYTE*)chr + Chr::CharacterData, 4))
				return NULL;

			const DWORD rawData = DwordAt(chr, Chr::CharacterData);
			if (!LooksLikePointer(rawData))
				return NULL;

			const BYTE* data = (const BYTE*)(UINT_PTR)rawData;
			if (!Readable(data + ChrData::BonePointTable, 4))
				return NULL;

			const DWORD rawTable = *(volatile const DWORD*)(data + ChrData::BonePointTable);
			if (!LooksLikePointer(rawTable))
				return NULL;

			const BYTE* table = (const BYTE*)(UINT_PTR)rawTable;
			if (!Readable(table + ChrData::BonePoint::Records, 4) ||
			    !Readable(table + ChrData::BonePoint::Count, 4))
				return NULL;

			const int count = (int)*(volatile const DWORD*)(table + ChrData::BonePoint::Count);
			const DWORD rawRecords =
			    *(volatile const DWORD*)(table + ChrData::BonePoint::Records);

			// A count of zero is a real answer for a character with no bone points, and
			// a silly one says the offsets are wrong. 4096 is far past the 22 slots
			// m_boneWorldPos has room for, so it only catches nonsense.
			if (count <= 0 || count > 4096)
				return NULL;
			if (!LooksLikePointer(rawRecords))
				return NULL;

			const BYTE* records = (const BYTE*)(UINT_PTR)rawRecords;
			if (!Readable(records, (SIZE_T)count * ChrData::BonePoint::RecordStride))
				return NULL;

			if (outCount)
				*outCount = count;
			return records;
		}

	} // namespace

	int BoneIdForJoint(Character* chr, int jointIndex)
	{
		if (jointIndex < 0)
			return -1;

		int count = 0;
		const BYTE* records = BonePointRecords(chr, &count);
		if (!records)
			return -1;

		for (int i = 0; i < count; ++i)
		{
			const BYTE* record = records + (DWORD)i * ChrData::BonePoint::RecordStride;
			const WORD joint =
			    *(volatile const WORD*)(record + ChrData::BonePoint::JointIndex);
			if ((int)joint != jointIndex)
				continue;

			const WORD head =
			    *(volatile const WORD*)(record + ChrData::BonePoint::KindAndBoneId);
			return (int)(head & ChrData::BonePoint::BoneIdMask);
		}

		return -1;
	}

	int JointForBoneId(Character* chr, int boneId)
	{
		if (!chr || boneId < 0)
			return -1;

		// Through the engine so the answer matches what an attach would do, but only
		// after the dereference chain it skips has been checked here.
		if (!BonePointRecords(chr, NULL))
			return -1;
		if (!LooksLikeFunctionStart(Rva::ChLookupBonePoint))
			return -1;

		typedef int(__cdecl * LookupBonePointFn)(Character*, int, int*, float*);
		int joint = -1;
		float offset[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		const int kind = ((LookupBonePointFn)ModuleAddress(Rva::ChLookupBonePoint))(
		    chr, boneId, &joint, offset);

		// A miss leaves *outJoint at 0 and returns 0, which is indistinguishable from a
		// real hit on joint 0 that happens to be a plain joint. So the reverse walk
		// settles it, because that one can tell the two apart.
		if (kind == 0 && BoneIdForJoint(chr, 0) != boneId)
			return -1;

		return joint;
	}

	bool BoneWorldPosition(Character* chr, int boneId, float* xyz)
	{
		if (!chr || !xyz)
			return false;

		// The engine's own getter has no bounds check and a large id reads clean off
		// the end of the CHR, so the clamp is the point of this wrapper.
		if (boneId < 0 || boneId >= (int)Chr::BoneWorldPosCount)
			return false;

		const BYTE* entry =
		    (const BYTE*)chr + Chr::BoneWorldPos + Chr::BoneWorldPosStride * (DWORD)boneId;
		if (!Readable(entry, Chr::BoneWorldPosStride))
			return false;
		if (!LooksLikeFunctionStart(Rva::ChGetBoneWorldPos))
			return false;

		// Through the engine rather than reading the four floats, because the fallback
		// chain is worth having: an empty entry falls back to entry 0 and then to the
		// CHR world matrix translation, which is a sensible answer rather than zeros.
		typedef float*(__cdecl * GetBoneWorldPosFn)(Character*, int, float*);
		float out[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		((GetBoneWorldPosFn)ModuleAddress(Rva::ChGetBoneWorldPos))(chr, boneId, out);

		xyz[0] = out[0];
		xyz[1] = out[1];
		xyz[2] = out[2];
		return true;
	}

	bool AttachToCarrierBone(Character* chr, Character* carrier, int boneId)
	{
		if (!chr)
			return false;
		if (!Readable((const BYTE*)chr + Chr::ParentUid, 4))
			return false;
		if (!LooksLikeFunctionStart(Rva::ChAttachToParentBone))
			return false;

		// The guard that matters. With a carrier the engine runs
		// FFX_Ch_LookupBonePoint, which dereferences carrier->m_data without checking
		// it, so a carrier whose character data has not loaded faults inside the
		// engine. BonePointRecords walks that whole chain first.
		if (carrier && !BonePointRecords(carrier, NULL))
			return false;

		typedef int(__cdecl * AttachToParentBoneFn)(Character*, Character*, int);
		((AttachToParentBoneFn)ModuleAddress(Rva::ChAttachToParentBone))(
		    chr, carrier, boneId);
		return true;
	}

	bool AttachToCarrierJoint(Character* chr, Character* carrier, int jointIndex)
	{
		if (!chr)
			return false;
		if (!Readable((const BYTE*)chr + Chr::ParentUid, 4))
			return false;
		if (!LooksLikeFunctionStart(Rva::ChAttachToParentJoint))
			return false;

		// No m_data check here on purpose. This entry point reads the carrier's
		// m_objId and nothing else, so there is nothing to fault on.
		if (carrier && !Readable((const BYTE*)carrier + Chr::ObjId, 4))
			return false;

		typedef int(__cdecl * AttachToParentJointFn)(Character*, Character*, int);
		((AttachToParentJointFn)ModuleAddress(Rva::ChAttachToParentJoint))(
		    chr, carrier, jointIndex);
		return true;
	}

	bool DetachFromCarrier(Character* chr)
	{
		// Through the joint variant, because detaching needs no bone point table and
		// that entry point is pure field writes.
		return AttachToCarrierJoint(chr, NULL, 0);
	}

} // namespace ffx
