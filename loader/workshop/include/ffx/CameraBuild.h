#pragma once

#include <windows.h>

// The camera build: FFX_Came_ActivateScreen 0x7BC110 and everything it reads and writes.
//
// ActivateScreen picks one of three screens, steps that screen's WIN and DRW rects and
// its two screen shake channels, then writes the projection and the view. Which of three
// paths builds them is decided by two flags: direct look-at, the override, or the normal
// build from the slot the screen shows. The normal path can then be replaced wholesale by
// the magic camera freeze. reversing\CAMERA_BUILD.md has all of it, and section 10 is the
// recipe this header exists to serve.
//
// Screen shake draws from RNG stream 9 on every call, so a recording carries the stream 9
// state on both sides of a call. See ffx/Random.h.
//
// Every reader here is a module global validated once, so all of it is cheap enough for a
// per call recorder. The one exception is the level name hash, which lives behind the gfx
// context pointer and is re-validated only when that pointer changes.

namespace ffx
{

	namespace CameBuild
	{
		const int ScreenBytes = 448;
		const int ScreenCount = 3;
		const int SlotBytes = 0xDA0; // 3488
		const int SlotCount = 3;
		const int MatrixFloats = 16;
		const int DirectBlockBytes = 64;

		// Offsets into a screen, the two the probe reads by name. The rest are in
		// reversing\CAMERA_BUILD.md section 2.2.
		const DWORD ScreenEnabled = 0xC0;   // BYTE
		const DWORD ScreenSlotIndex = 0xC1; // BYTE

		// The level name hash FFX_Level_GetNameHash returns, inside the gfx context object.
		const DWORD GfxCtxLevelNameHash = 0xFAF4;
	} // namespace CameBuild

	// Live pointers into the game, or NULL when the span did not validate. Read only. The
	// slots and screens are written by the step thread, so a copy taken anywhere else can
	// tear.
	const BYTE* CameScreensBase();     // ScreenCount * ScreenBytes
	const BYTE* CameSlotsBase();       // SlotCount * SlotBytes
	const float* CameViewMatrixLive(); // 16 floats
	const float* CameProjMatrixLive(); // 16 floats

	// Which screen and slot the last ActivateScreen bound, as indices. -1 when the pointer
	// is null or does not land on a record boundary.
	int CameActiveScreenIndex();
	int CameActiveSlotIndex();

	// The globals that choose ActivateScreen's path and feed the view gate, read together.
	struct CameraBuildGlobals
	{
		DWORD directLookAtFlag;
		DWORD overrideFlag;
		BYTE directBlock[CameBuild::DirectBlockBytes]; // bias x y, fov mode, roll, eye, at, up
		DWORD levelNameHash;                           // 0 when the gfx context is not up
		int pendingGlobalEffect;                       // -1 for none
		int pendingBattleEffect;                       // -1 for none
		int ps2FrameWidth;                             // 512
		int ps2FrameHeight;                            // 416
		int magicId;                                   // MagicFile__getMagicId, -1 for none
		int magicPrevId;
		DWORD magicFreezeFlag;
		int liveSceneId;
		BYTE mainNearLatch;
		bool levelNameHashValid;
	};
	bool ReadCameraBuildGlobals(CameraBuildGlobals* out);

	// MagicFile__isCameraFrozen, called. It is pure reads, but one of its inputs is a
	// battle getter, so calling it is simpler and more faithful than re-deriving it. The
	// first two bytes and the global it tests are checked against the expected encoding
	// once before the first call. Returns false and leaves out alone when that check failed.
	// Step thread only.
	bool QueryMagicCameraFrozen(bool* out);

	// ---------------------------------------------------------------------------
	// The observer hook, a detour on FFX_Came_ActivateScreen. The observer runs before the
	// original with afterCall false and result 0, then after it with afterCall true and the
	// real result. It cannot change anything and the result goes back unchanged.
	//
	// Chains behind another plugin's detour, see workshop/Detour.h. One observer per
	// module, installing again replaces it.
	// ---------------------------------------------------------------------------
	typedef void(__cdecl* ActivateScreenObserverFn)(int screen, bool afterCall, int result);
	bool HookActivateScreen(ActivateScreenObserverFn observer);
	bool ActivateScreenHookInstalled();

} // namespace ffx
