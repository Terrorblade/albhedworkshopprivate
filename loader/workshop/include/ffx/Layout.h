#pragma once

#include <windows.h>

// Field offsets for the game structures this mod reads. Grouped by structure so
// a use site reads as Chr::Speed or Instance::MeshArray and the layout lives in
// exactly one place.

namespace ffx
{

	// ---------------------------------------------------------------------------
	// CHR, the character record. sizeof is 0x880 and the pool is a flat array at
	// that stride, so a slot index is a stable identity across a frame and the
	// natural network id later. A raw pointer is NOT stable across a map transition,
	// because the pool is freed and reallocated.
	// ---------------------------------------------------------------------------
	namespace Chr
	{

		const DWORD Stride = 0x880;

		// Identity and liveness
		const DWORD Id = 0x000;    // short
		const DWORD InUse = 0x002; // byte

		// Position. FFX_Ch_SetPos writes Position and mirrors it into PreviousPosition
		// itself, so a caller never has to touch the second one.
		const DWORD Position = 0x00C;         // float[4], x y z w. +Y IS DOWN
		const DWORD PreviousPosition = 0x01C; // float[4]
		const DWORD WalkmeshPosition = 0x03C; // float[4], the position in walkmesh space

		// Motion, which is all this mod writes to drive a character
		const DWORD VelocityX = 0x04C;        // float
		const DWORD VelocityY = 0x050;        // float
		const DWORD VelocityZ = 0x054;        // float
		const DWORD Speed = 0x154;            // float
		const DWORD Facing = 0x158;           // float, m_rotY
		const DWORD MoveDirection = 0x168;    // float, m_moveDir
		const DWORD RunThreshold = 0x170;     // float, defaults to 18.0
		const DWORD GroundHeight = 0x16C;     // float, m_groundHeight
		const DWORD WalkmeshTri = 0x824;      // short, -1 means off the walkmesh
		const DWORD GroundAttributes = 0x828; // dword, refilled from the bound triangle

		// m_vertVel. The separate vertical velocity, which the water mode drives.
		// VelocityX/Y/Z are zeroed every frame by FFX_Ch_ResolveCollisionsAll, so THIS
		// and Speed are the two that actually carry motion across a placement.
		const DWORD VerticalVelocity = 0x504;

		// Visibility and render state
		const DWORD HideFlags = 0x180;  // byte, see namespace Hide
		const DWORD Byte181 = 0x181;    // byte, read right after HideFlags
		const DWORD GroundMode = 0x182; // byte, m_groundMode. Allocate sets 1.
		const DWORD Byte183 = 0x183;    // byte, set by category
		const DWORD PartyIndex = 0x18C; // int
		const DWORD Flags1 = 0x194;     // dword
		const DWORD Flags2 = 0x198;     // dword
		const DWORD Mode19C = 0x19C;    // dword, UpdateRenderJob branches on it
		const DWORD ClipZ = 0x174;      // float, m_clipZ. Allocate sets 50*100.
		const DWORD CameLength = 0x178; // float, m_cameLen, distance to the camera

		// m_animShadeA.m_cur. FFX_Ch_StepAll loop C skips a character whose value here is
		// exactly 0.0, so it is an independent draw gate. FFX_Ch_Allocate already calls
		// FFX_Ch_SetShadeAlpha(chr, 1.0, 0), so it is 1.0 from the start.
		const DWORD ShadeAlpha = 0x354;

		// m_rootJoint[8], the matrix FFX_Ch_StepAll loop 1 pushes into the mesh transform.
		// Logging it alongside the transform shows whether that push happened at all.
		const DWORD RootJoint8 = 0x1D0;

		// The model instances
		const DWORD Instance = 0x830;       // void *, non-null once the mesh is live
		const DWORD SecondInstance = 0x838; // void *, the second attach slot
		const DWORD PartBoneFlag = 0x83C;   // dword, 1 when part bone buffers exist

	} // namespace Chr

	// ---------------------------------------------------------------------------
	// m_flags1 bits, the ones that have been identified. The dword is at Chr::Flags1.
	//
	// Worth knowing before hunting one of these: FFX_Ch_ClearFlags1StepBits 0x432DB0
	// wipes bits 24, 25 and 26 at the top of every frame, so those three are per-frame
	// signals rather than state.
	// ---------------------------------------------------------------------------
	namespace ChrFlag1
	{

		// Free move. Reading this corrects an older note in the IDB that called it
		// 0x80000000.
		const DWORD FreeMove = 0x00000080;

		// The locomotion driver slews m_rotY toward m_moveDir while this is set, at
		// 0.314 to 0.524 radians per sub-step depending on the gait. So writing the
		// facing without also writing m_moveDir turns the character to where you asked
		// and then smoothly turns it away again. FFX_Ch_SetRotAndMoveDir writes both.
		const DWORD SlewToMoveDirection = 0x00000400;

		// Set by FFX_Ch_SetPos, FFX_Ch_SetPosXZ and FFX_Ch_UpdateMotionAll on every
		// position write.
		//
		// NOTHING READS IT. Checked properly rather than assumed: an immediate search
		// across all of .text, a byte pattern sweep for every test, bt and byte-test
		// encoding, and a decompile of all 76 functions that touch displacement 0x194.
		// Zero readers. It is wiped by the per-frame clear above along with bits 24 and
		// 26. So it is a dead latch, there is nothing to redo when it is set and nothing
		// to clear. Recorded so the next person does not spend an afternoon on it.
		const DWORD PositionChanged = 0x02000000;

		// What FFX_Ch_MarkDirty's vertex rebuild is gated on. A character without this
		// bit gets only the flag set, which is why MarkDirty is cheap in the common case.
		const DWORD HasPartVertexBuffers = 0x00200000;

	} // namespace ChrFlag1

	// ---------------------------------------------------------------------------
	// m_hideFlags bits, named from the game's own debug GUI at
	// SG_DebugGui_ChrInfoWindowProc 0x85393C, which prints "Hide : %x" and then
	// appends one label per bit.
	//
	// NONE OF THESE LATCH. FFX_Ch_UpdateMotionAll does m_hideFlags &= 0x13 every
	// frame and FFX_Ch_UpdateCameLenAndZClip does m_hideFlags &= 0xD7 on entry, so
	// writing one of these from outside the render phase achieves nothing.
	// ---------------------------------------------------------------------------
	namespace Hide
	{

		const BYTE EventScript = 0x01; // 'Ev',   an event script hid it
		const BYTE Effect = 0x02;      // 'Eff',  an effect hid it
		const BYTE NotVisible = 0x08;  // 'Disp', nothing of it is visible to the camera
		const BYTE Battle = 0x10;      // 'Btl',  battle hid it
		const BYTE PastClipZ = 0x20;   // 'Z',    further away than m_clipZ

	} // namespace Hide

	// ---------------------------------------------------------------------------
	// The 32-byte instance record ClassCharacter__createInstance builds.
	// ---------------------------------------------------------------------------
	namespace Instance
	{

		const DWORD SubMeshCount = 0x00;   // dword, low 31 bits are the count
		const DWORD MeshArray = 0x04;      // void **
		const DWORD ClassCharacter = 0x10; // ClassCharacter *
		const DWORD ShownByte = 0x14;      // byte, mirrors the last show or hide decision
		const DWORD PassId = 0x15;         // byte, which render pass claims it
		const DWORD OwnerChr = 0x18;       // Character *, the back pointer

	} // namespace Instance

	// ---------------------------------------------------------------------------
	// A per-sub-mesh render object.
	//
	// Mesh::Node is the thing that makes an end-to-end draw test possible.
	// FFX_ChrInstance_SetHidden 0x63C390 ultimately does
	//     node = *(void **)(mesh + 0x1C);
	//     if (node) *(void **)(node + 0x1C) = show ? mesh : NULL;
	// and that single pointer store is what links the mesh into, or out of, the
	// render graph. Reading it back is downstream of every FFX-side gate.
	//
	// The same object doubles as the PMeshInstanceBounds the frustum test works on,
	// which is why the Bounds offsets below are read off the same pointer.
	// ---------------------------------------------------------------------------
	namespace Mesh
	{

		const DWORD Transform = 0x04; // void *, the transform object
		const DWORD Node = 0x1C;      // void *, the Phyre node

	} // namespace Mesh

	namespace Node
	{

		const DWORD MeshBackPointer = 0x1C; // void *, equals the mesh when linked, 0 when not

	} // namespace Node

	namespace Bounds
	{

		const DWORD Corner = 0x00;    // float[3], the OBB corner
		const DWORD Transform = 0x0C; // void *, must equal Mesh::Transform
		const DWORD Extents = 0x10;   // float[3], the OBB half extents

	} // namespace Bounds

	namespace Transform
	{

		const DWORD TranslationColumn = 0x30; // float[3]

	} // namespace Transform

	// ---------------------------------------------------------------------------
	// The graphics context.
	// ---------------------------------------------------------------------------
	namespace Gfx
	{

		// Dword, non-zero makes the per-mesh visibility test return "visible" for
		// everything. READ THE WARNING in diag/RenderProbe.cpp before using it as an
		// experiment: only one function reads it, and the real render passes do not.
		const DWORD DisableCull = 0x10D8C;

	} // namespace Gfx

	// ---------------------------------------------------------------------------
	// FFXApplication and PhyreEngine's input mapper.
	// ---------------------------------------------------------------------------
	namespace App
	{

		const DWORD InputMapper = 0x02B8; // PInputMapper *

	} // namespace App

	namespace InputMapper
	{

		const DWORD PadArray = 0x0038; // PInputDevicePad *[18]
		const DWORD PadSlots = 18;

	} // namespace InputMapper

	// ---------------------------------------------------------------------------
	// One pad device. Same layout for the XInput and DirectInput backends.
	// ---------------------------------------------------------------------------
	namespace PadDevice
	{

		const DWORD LatchedButtons = 0x0018; // 16 bytes, indexed by semantic minus 11
		const DWORD Connected = 0x0069;      // byte
		const DWORD AxisLeftX = 0x0370;      // int, roughly -255 to 255
		const DWORD AxisRightX = 0x0374;
		const DWORD AxisLeftY = 0x037C; // int, POSITIVE IS DOWN
		const DWORD AxisRightY = 0x0380;

		// Button semantics as the engine numbers them. LatchedButtons is indexed by
		// (semantic - 11), which is why the block starts at 11.
		namespace Button
		{
			const int Square = 11;
			const int Cross = 12;
			const int Circle = 13;
			const int Triangle = 14;
			const int L1 = 15;
			const int R1 = 16;
			const int Up = 23;
			const int Right = 24;
			const int Down = 25;
			const int Left = 26;
		} // namespace Button

	} // namespace PadDevice

	// ---------------------------------------------------------------------------
	// A camera slot. Three of them, and Rva::CameActiveSlot points at the live one.
	// ---------------------------------------------------------------------------
	namespace Came
	{

		const DWORD SlotStride = 0x0DA0;
		const DWORD SlotCount = 3;
		const DWORD TargetPos = 0x0050; // vec4, the look-at target
		const DWORD EyePos = 0x066C;    // vec4, the eye

	} // namespace Came

} // namespace ffx
