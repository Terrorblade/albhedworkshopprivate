#pragma once

#include <windows.h>

// Characters, motion, the walkmesh, the camera and the graphics context.
//
// The original area, and the pattern every other area file follows. Addresses
// are RVAs, which is the IDA VA minus 0x00400000. Names, argument counts and
// calling conventions were read back out of the IDB rather than assumed.
// Everything in the Ch family is __cdecl.
//
// Owned by nobody in particular, so coordinate before a large edit. New
// subsystems get their own file in this folder instead of being added here.

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The per-frame hook point, proven by the example plugin
		// ---------------------------------------------------------------------------
		const DWORD AnimateVtableSlot = 0x0070D9A8; // FFXApplication vtable +0x10
		const DWORD AnimateExpected = 0x0002F520;   // what that slot should contain

		// The matching teardown point, slot 6 of the same vtable. Reached only through
		// the vtable, like animate, so it patches the same way. Hook it to get a
		// callback while the engine is still readable, then call the original.
		const DWORD ExitApplicationVtableSlot = 0x0070D9B0; // vtable +0x18
		const DWORD ExitApplicationExpected = 0x0002F600;   // FFXApplication::exitApplication

		// ---------------------------------------------------------------------------
		// Character allocation and teardown
		// ---------------------------------------------------------------------------
		const DWORD ChAllocate = 0x00424F90;      // Character *(int chrId)
		const DWORD ChDisposeIfLive = 0x004722F0; // void (Character *)
		const DWORD ChIsLive = 0x004261B0;        // int (Character *), 0 means live
		const DWORD ChCountLive = 0x004285E0;     // int (void)

		// ---------------------------------------------------------------------------
		// Data and motion paging. All of these are idempotent.
		// ---------------------------------------------------------------------------
		const DWORD ChRomRead = 0x00429EF0;            // int (int chrId)
		const DWORD ChDataReadSync = 0x0042A040;       // int (int chrId) 0 ready 1 busy 2 missing
		const DWORD ChMotionSetReadStart = 0x00436A40; // int (int chrId, int set)
		const DWORD ChMotionSetReadSync = 0x00436A50;  // int (int chrId, int set)
		const DWORD ChLoadMotionSetSync = 0x00436870;  // void (int chrId, int set)

		// ---------------------------------------------------------------------------
		// Per-character setters
		// ---------------------------------------------------------------------------
		const DWORD ChSetByte184 = 0x00435B50;    // int (Character *, char)
		const DWORD ChSetPartyIndex = 0x0042B0D0; // Character *(Character *, int)
		const DWORD ChSetPos = 0x0042B480;        // int (Character *, float, float, float)
		const DWORD ChSetPosXZ = 0x0042B440;      // int (Character *, float x, float z). SLAMS Y, see below
		const DWORD ChGetPos = 0x0042AC90;        // Character *(Character *, float *, float *, float *)
		const DWORD ChSetRot = 0x0042B520;        // int (Character *, float radians)
		const DWORD ChSetMoveSpeed = 0x0042B840;  // Character *(Character *, float)
		const DWORD ChSetMoveDir = 0x0042B190;    // Character *(Character *, float radians)

		// Character *(Character *, float radians). Writes m_rotY AND m_moveDir in one
		// call, which is what you actually want. m_flags1 bit 0x400 makes the locomotion
		// driver slew m_rotY back toward m_moveDir at 0.314 to 0.524 radians per
		// sub-step, so setting the rotation alone turns the character to face where you
		// asked and then smoothly turns it away again.
		const DWORD ChSetRotAndMoveDir = 0x0042B1B0;
		const DWORD ChSetFlags1Bit400 = 0x0042AAE0; // bool (Character *, short on)
		const DWORD ChSetGroundMode = 0x0042B240;   // void (Character *, int)

		// void __cdecl (Character *). FFX_Ch_SetFlags1Bit10 plus a vertex rebuild that
		// recopies every part's skinned vertices from the source mesh and then marks the
		// children dirty too. The rebuild is gated on m_flags1 bit 0x200000, so for a
		// character without that bit it is just the flag set and it is cheap either way.
		//
		// FFX_Atel_SetActorPos calls this itself right after FFX_Ch_SetPos, so a
		// placement that goes through the ATEL path does not need to.
		const DWORD ChMarkDirty = 0x00424650;

		// ---------------------------------------------------------------------------
		// The one-shot that WOULD swallow a placement, except it is dead code.
		//
		// Byte. Non-zero makes the very next FFX_Ch_SetPos do nothing at all: it clears
		// this flag, returns an uninitialised eax, and never touches the position.
		//
		// IT CANNOT BE ARMED. The only writer that sets it is FFX_Ch_SuppressNextSetPos
		// 0x42ACC0, and that function has zero callers. It is not in
		// g_ffxMagicHostApiTable either, checked by searching the whole image for its
		// address, so no magic DLL can reach it. The byte lives in uninitialised .data,
		// so it is 0 at startup and stays 0 forever.
		//
		// Left here because an unarmable landmine is still worth knowing about: a future
		// patch or a plugin could arm it, and the failure it produces is invisible. A
		// swallowed SetPos does not fault and does not log, the character just stays put.
		// ffx::SetPosSuppressed reads it, and placement clears it, which costs nothing.
		//
		// Note FFX_Ch_SetPosXZ does NOT honour it.
		// ---------------------------------------------------------------------------
		const DWORD SuppressNextSetPos = 0x00EFFAD8;

		// Read only here. The single global player binding, and the reason most of the
		// co-op work exists.
		const DWORD ChGetPlayerChr = 0x0042D860; // Character *(void)

		// ---------------------------------------------------------------------------
		// The visibility detour target.
		//
		// int __cdecl (Character *). One caller, FFX_Ch_StepAll at 0x82F256. This is the
		// only thing in the binary that sets m_hideFlags bit 0x08 (Disp).
		// ---------------------------------------------------------------------------
		const DWORD ChUpdateCameLenAndZClip = 0x0042E220;

		// ---------------------------------------------------------------------------
		// The walkmesh.
		//
		// ChWalkmeshMove is void __cdecl (Character *) and is the ONLY code in the binary
		// that resolves a position to a walkmesh triangle and binds it. Binding is a side
		// effect of this per-frame step, there is no separate bind call. It reads the
		// character's own position plus velocity (+0x4C, +0x54), so with zero velocity it
		// is a pure bind, and it self-heals the -1 sentinel:
		//     if (m_walkmeshTri == -1) m_walkmeshTri = FindTri(pos * scale);
		// It also fills m_groundHeight (+0x16C), m_groundAttrs (+0x828) and the ground
		// normal, which is why calling it matters for more than the triangle index.
		// ---------------------------------------------------------------------------
		const DWORD ChWalkmeshMove = 0x0043E5F0;

		// The resolver WalkmeshMove uses. Exposed so a placement can ask whether a point
		// is on the mesh BEFORE committing to it, rather than finding out by watching a
		// character slide. See reversing\PLACEMENT.md for the signature.
		// int __cdecl (float *xyz), and the argument is in WALKMESH SPACE, which is
		// world multiplied by WalkmeshScale. Passing world coordinates straight in gives
		// nonsense rather than a -1.
		//
		// There IS a world-space wrapper at 0x43EAE0 and you must not use it. It is dead
		// code and it divides the returned TRIANGLE INDEX by the scale, which is
		// meaningless.
		//
		// A -1 means, and only means, "no triangle contains this XZ". A wrong Y never
		// produces one: the test rejects floors above the query Y and keeps the lowest
		// of those at or below it, so it finds the first floor under your feet. Since +Y
		// is down, too small a Y means too high up, and that is the safe direction.
		//
		// WHY A PLACEMENT SHOULD ASK FIRST: when this fails inside
		// FFX_Ch_WalkmeshMove, the engine does posX += velX * 10 and posZ += velZ * 10
		// and runs nothing else, leaving m_groundHeight, the ground normal and
		// m_groundAttrs stale. It leaves m_walkmeshTri at -1 rather than storing
		// anything bad, so it retries next frame. With the velocity zeroed the character
		// just sits there with stale ground state, which is survivable. With velocity it
		// gets flung.
		//
		// ffx::WalkmeshTriangleAt does the scaling.
		const DWORD ChWalkmeshFindTri = 0x0043DE10;

		// For telling "no walkmesh is loaded" apart from "the point is off it".
		const DWORD WalkmeshTris = 0x00F01A84;     // void *, the triangle array
		const DWORD WalkmeshTriCount = 0x00F01A88; // int
		const DWORD WalkmeshScale = 0x00F01A90;    // float

		// Byte. Non-zero disables FFX_Ch_UpdateMotionAll for every character, so it is
		// worth logging at spawn time to rule out "motion is globally off".
		const DWORD MotionKillSwitch = 0x00EFBBBB;

		// ---------------------------------------------------------------------------
		// The character pool. A flat array at stride 0x880.
		// ---------------------------------------------------------------------------
		const DWORD ChrArray = 0x01FC44E4;      // Character *, the pool base
		const DWORD ChrCount = 0x01FC44E0;      // int, slots in the pool
		const DWORD TidusChr = 0x00EFBC60;      // Character *, a one-slot cache. See CloneSpawner.
		const DWORD ControlledChr = 0x00F00788; // Character *, player 1's binding

		// ---------------------------------------------------------------------------
		// CARRYING AND BONE ATTACHMENT.
		//
		// All __cdecl. Argument counts and senses read back out of the IDB rather than
		// taken from the decompiler's guess. The attachment is a BONE PARENT and nothing
		// streams: FFX_Ch_BuildSkinMatrices computes a carried object's world matrix from
		// the carrier's matrix, the carrier's joint palette, m_parentJoint and
		// m_attachOffset, and reads neither the carried object's position nor its
		// rotation. See reversing/HELD_OBJECTS.md.
		//
		// NOTE on the two id spaces. The bone functions take a LOGICAL BONE ID, 0 to 21,
		// which is not a joint index. FFX_Ch_UpdateBonePositions indexes by the bone id
		// field, which is what settles it, and it is why a replicated handover travels as
		// a bone id rather than as a joint index.
		// ---------------------------------------------------------------------------

		// int (Character* chr, Character* carrier, int boneId)
		// Writes m_parent and m_parentUid = carrier->m_objId, then LookupBonePoint fills
		// m_parentJoint and m_attachOffset from the bone id. A null carrier detaches and
		// sets m_parentUid to -1.
		//
		// DEREFERENCES carrier->m_data WITH NO NULL CHECK, inside LookupBonePoint. The
		// kit's AttachToCarrierBone is what guards that, not any readiness flag.
		const DWORD ChAttachToParentBone = 0x00432630;

		// int (Character*, Character* carrier, int jointIndex)
		// The same by joint index, with no bone point lookup and the attach offset
		// zeroed. Pure field writes, so unlike the above it cannot fault on a carrier
		// whose character data has not loaded.
		const DWORD ChAttachToParentJoint = 0x00432680;

		// CHRDATA* (Character*, Character* carrier, int jointIndex, float ox, float oy,
		//           float oz)
		// Explicit joint index and an offset pre-multiplied by 100 * m_modelScale. Reads
		// chr->m_data and not the carrier's. Not wrapped in the kit yet, because wrapping
		// it with no caller would mean guessing the units.
		const DWORD ChAttachToParentOffset = 0x004326E0;

		// int (Character*, int boneId, int* outJoint, float* outOffset4)
		// Returns the bone point kind: 0 none or a plain joint, 1 joint plus offset, 2
		// plain node. *outJoint is 0 on a miss and NOT -1, which is worth knowing because
		// 0 is also a valid joint.
		const DWORD ChLookupBonePoint = 0x00433A70;

		// float* (Character*, int boneId, float* out4)
		// boneId is a logical bone id and the engine does NOT range check it.
		const DWORD ChGetBoneWorldPos = 0x004354F0;

		// ---------------------------------------------------------------------------
		// PhyreEngine's input mapper, for reading a SECOND gamepad.
		//
		// Phyre keeps 18 fully independent pad slots and FFX only ever reads slot 0, so
		// slot 1 is a free second controller needing no hook at all. Verified in
		// Phyre__PInputMapper__bindPads 0x626AA0, which loops i < 18 storing the i-th
		// device whose getDeviceType() == 2 into mapper+0x38+4*i, and in
		// Phyre__PInputMapper__latchDeviceStates 0x628320, which walks the same 18.
		//
		// DO NOT USE the FFX scePad port layer for this. FFX_Pad__updateAll does step
		// ports 0 and 1, which makes port 1 look like a free second controller, but both
		// are filled by FFX_Pad__fillSceReadData -> FFX_Input__getButtonMask, which takes
		// no port argument and returns the single g_ffxInput mask. So port 1 is a
		// byte-for-byte clone of port 0 and not a second pad. That is a trap worth stating.
		// ---------------------------------------------------------------------------
		const DWORD Application = 0x008C9CD8; // FFXApplication *

		// ---------------------------------------------------------------------------
		// The active gameplay camera, for camera-relative input. See GameCamera.cpp.
		// ---------------------------------------------------------------------------
		const DWORD CameActiveSlot = 0x01F11430; // BYTE *, points into CameSlots
		const DWORD CameSlots = 0x00D37540;      // 3 slots of 0xDA0

		// ---------------------------------------------------------------------------
		// The graphics context. Only used for the cull override experiment, and see the
		// warning on it in diag/RenderProbe.cpp before trusting that experiment.
		// ---------------------------------------------------------------------------
		const DWORD GfxContext = 0x008CB9D8; // void *, the context pointer

		// ---------------------------------------------------------------------------
		// Every address above, for the startup build check. VerifyLayout walks this so
		// a typo is caught at startup rather than by a fault later.
		//
		// Keep it in step with the constants. If you add an address and forget this
		// list, nothing breaks today and something breaks confusingly in a year.
		// ---------------------------------------------------------------------------
		inline const DWORD* CharacterRvaList(int* count)
		{
			static const DWORD list[] = {
				AnimateVtableSlot,
				AnimateExpected,
				ExitApplicationVtableSlot,
				ExitApplicationExpected,
				ChAllocate,
				ChDisposeIfLive,
				ChIsLive,
				ChCountLive,
				ChRomRead,
				ChDataReadSync,
				ChMotionSetReadStart,
				ChMotionSetReadSync,
				ChLoadMotionSetSync,
				ChSetByte184,
				ChSetPartyIndex,
				ChSetPos,
				ChSetPosXZ,
				ChGetPos,
				ChSetRot,
				ChSetMoveSpeed,
				ChSetMoveDir,
				ChSetRotAndMoveDir,
				ChSetFlags1Bit400,
				ChSetGroundMode,
				ChMarkDirty,
				SuppressNextSetPos,
				ChGetPlayerChr,
				ChUpdateCameLenAndZClip,
				ChWalkmeshMove,
				ChWalkmeshFindTri,
				WalkmeshTris,
				WalkmeshTriCount,
				WalkmeshScale,
				MotionKillSwitch,
				ChrArray,
				ChrCount,
				TidusChr,
				ControlledChr,
				ChAttachToParentBone,
				ChAttachToParentJoint,
				ChAttachToParentOffset,
				ChLookupBonePoint,
				ChGetBoneWorldPos,
				Application,
				CameActiveSlot,
				CameSlots,
				GfxContext,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
