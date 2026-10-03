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
		const DWORD ChSetByte184 = 0x00435B50;      // int (Character *, char)
		const DWORD ChSetPartyIndex = 0x0042B0D0;   // Character *(Character *, int)
		const DWORD ChSetPos = 0x0042B480;          // int (Character *, float, float, float)
		const DWORD ChGetPos = 0x0042AC90;          // Character *(Character *, float *, float *, float *)
		const DWORD ChSetRot = 0x0042B520;          // int (Character *, float radians)
		const DWORD ChSetMoveSpeed = 0x0042B840;    // Character *(Character *, float)
		const DWORD ChSetMoveDir = 0x0042B190;      // Character *(Character *, float radians)
		const DWORD ChSetFlags1Bit400 = 0x0042AAE0; // bool (Character *, short on)
		const DWORD ChSetGroundMode = 0x0042B240;   // void (Character *, int)

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
				ChGetPos,
				ChSetRot,
				ChSetMoveSpeed,
				ChSetMoveDir,
				ChSetFlags1Bit400,
				ChSetGroundMode,
				ChGetPlayerChr,
				ChUpdateCameLenAndZClip,
				ChWalkmeshMove,
				WalkmeshTris,
				WalkmeshTriCount,
				WalkmeshScale,
				MotionKillSwitch,
				ChrArray,
				ChrCount,
				TidusChr,
				ControlledChr,
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
