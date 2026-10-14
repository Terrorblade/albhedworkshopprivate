#pragma once

#include <windows.h>

// The camera BUILD: FFX_Came_ActivateScreen, the view and projection it writes, the three
// screens it steps, and the globals that pick which of its three paths runs.
//
// Addresses are RVAs, the IDA VA minus 0x00400000. Every one was checked in the IDB on
// 2026-10-13. The derivation is reversing\CAMERA_BUILD.md, which also has the capture
// recipe the probe plugin implements.
//
// NOT DECLARED HERE, because another area found them first and owns them:
//   CameSlots, CameActiveSlot   addresses\Character.h (the camera relative input work)
//   CameOverrideFlag            addresses\Cutscene.h  (SetMatrixOverride)
//   GfxContext                  addresses\Character.h (the level name hash lives in it)
//   RandStreamState             addresses\Minigames.h (camera shake draws stream 9)
//   LiveSceneId                 addresses\WorldState.h (scene 208 gates the near latch)

namespace ffx
{
	namespace Rva
	{

		// ---------------------------------------------------------------------------
		// The functions. Both __cdecl, both called only from FFX_MainStep.
		// ---------------------------------------------------------------------------

		// int __cdecl (int screen). Hex-Rays types the argument as a float and it is not,
		// it is compared signed against 3 and used as an index. Returns 1 for screen >= 3
		// (the end of FFX_MainStep's screen walk), -1 for a disabled screen and 0 after a
		// build. Three call sites, 0x821189, 0x82135E and 0x821380, all in FFX_MainStep,
		// so it runs about three times per step and never per sub step.
		//
		// Prologue 55 8B EC 83 EC 20, six bytes with no relative operand, so it detours.
		const DWORD CameActivateScreen = 0x003BC110;

		// int __cdecl (void). Moves the three camera slots, called per sub step at
		// 0x82104D and once more at 0x8210C2. Not hooked by anything yet, listed because
		// the next camera fixture (the slot shake) wants it. Prologue 55 8B EC 83 EC 44.
		const DWORD CameStepAll = 0x003BE110;

		// ---------------------------------------------------------------------------
		// What ActivateScreen writes.
		// ---------------------------------------------------------------------------
		const DWORD CameViewMatrix = 0x01F11440;   // float[16], row major, row vectors
		const DWORD CameProjMatrix = 0x01F11480;   // float[16], the PS2 GS style projection
		const DWORD CameActiveScreen = 0x01F114C0; // BYTE *, into CameScreens

		// ---------------------------------------------------------------------------
		// What it reads.
		// ---------------------------------------------------------------------------

		// 3 screens of 448 bytes, contiguous with CameSlots, which starts at its end.
		const DWORD CameScreens = 0x00D37000;
		const int CameScreenBytes = 448;
		const int CameScreenCount = 3;

		// int. Non-zero takes the direct look-at path, which builds the view from the
		// block below instead of from a slot and pushes nothing to the gfx side.
		const DWORD CameDirectLookAtFlag = 0x01F1142C;

		// 64 bytes, 0x1136FB8..0x1136FF7, read as one block: pixel offset bias x and y
		// (float, +0x00 and +0x04), the debug fov mode (int, +0x08, 0 in retail), the
		// direct roll (float, +0x0C), then the direct eye, at and up as float[4] each at
		// +0x10, +0x20 and +0x30.
		const DWORD CameDirectBlock = 0x00D36FB8;
		const int CameDirectBlockBytes = 64;

		// int[2], 512 and 416, the PS2 frame size the rect offsets subtract.
		const DWORD CamePs2FrameSize = 0x00844240;

		// BYTE, initial 1. The magic 540 / scene 208 main camera near latch.
		const DWORD CameMainNearLatch = 0x00844238;

		// int, -1 for none. The view gate on ssbt01, ssbt00 and hiku15 rebuilds the view
		// only while both are negative.
		const DWORD CamePendingGlobalEffect = 0x008421E4; // written by maybe_FFX_Magic_BeginGlobalEffect
		const DWORD CamePendingBattleEffect = 0x008421E8; // written by FFX_Btl_BeginMagicEffect

		// ---------------------------------------------------------------------------
		// The magic camera freeze. These belong to the magic file loader rather than to
		// the camera, but the camera build is their only reader that matters, so they
		// live here until the magic area wants them.
		// ---------------------------------------------------------------------------

		// BOOL __cdecl (void), pure reads: FreezeFlag, or PrevId 165 or 307, or 338 while
		// the battle sub phase key equals 0x18B0046. When true ActivateScreen copies the
		// snapshot MagicFile__load took over both matrices.
		const DWORD MagicFileIsCameraFrozen = 0x005DA700;
		const DWORD MagicFileGetMagicId = 0x005DA3B0;   // int __cdecl (void), returns MagicFileId
		const DWORD MagicFileId = 0x00864CA0;           // int, the loaded magic file, -1 for none
		const DWORD MagicFilePrevId = 0x00864CA8;       // int
		const DWORD MagicCameraFreezeFlag = 0x01540ABC; // int, dword_1940ABC

		inline const DWORD* CameraRvaList(int* count)
		{
			static const DWORD list[] = {
				CameActivateScreen,
				CameStepAll,
				CameViewMatrix,
				CameProjMatrix,
				CameActiveScreen,
				CameScreens,
				CameDirectLookAtFlag,
				CameDirectBlock,
				CamePs2FrameSize,
				CameMainNearLatch,
				CamePendingGlobalEffect,
				CamePendingBattleEffect,
				MagicFileIsCameraFrozen,
				MagicFileGetMagicId,
				MagicFileId,
				MagicFilePrevId,
				MagicCameraFreezeFlag,
			};
			if (count)
				*count = (int)(sizeof(list) / sizeof(list[0]));
			return list;
		}

	} // namespace Rva
} // namespace ffx
