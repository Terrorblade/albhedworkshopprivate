// Recipe 2, the camera build, for Project Zanarkand PROGRESS 6.3.1.
//
// reversing/CAMERA_BUILD.md section 10. One record per FFX_Came_ActivateScreen call: the
// screens, slots, RNG stream 9 and the globals that choose the path on entry, and the
// view, the projection, the screens and stream 9 again on exit. The matrices are taken
// on entry too, because two of the three paths can leave one or both untouched and the
// recreation then has to start from what was there.
//
// About 13.5 KB a call and three calls a step, so roughly 1.2 MB a second at 29.97 Hz.
// The record is filled in place in the writer's buffer, no copy, and nothing here makes
// a syscall.

#include "capture/Capture.h"

#include <string.h>

#include "ProbeSettings.h"
#include "ffx/CameraBuild.h"
#include "ffx/Random.h"
#include "workshop/Log.h"

namespace probe
{
	namespace
	{

		CameraRecord* g_open = NULL; // reserved on entry, committed on exit

		bool CopyMatrices(float* view, float* proj)
		{
			const float* v = ffx::CameViewMatrixLive();
			const float* p = ffx::CameProjMatrixLive();
			if (!v || !p)
				return false;
			memcpy(view, v, 16 * sizeof(float));
			memcpy(proj, p, 16 * sizeof(float));
			return true;
		}

		void __cdecl OnActivateScreen(int screen, bool afterCall, int result)
		{
			if (!afterCall)
			{
				const WORD call = cursor.cameraCallInStep++;

				g_open = NULL;
				if (!settings.camera || !BeginRecord())
					return;
				workshop::RecordFile* file = FileFor(KindCamera);
				CameraRecord* r = file ? (CameraRecord*)workshop::ReserveRecord(file) : NULL;
				if (!r)
				{
					EndRecord();
					return;
				}

				FillPrefix(&r->prefix, KindCamera, call, 0);
				r->screen = screen;

				DWORD rng = 0;
				if (ffx::ReadRandStreamState(ffx::RandStreamCameraShake, &rng))
				{
					r->rngStream9Entry = rng;
					r->prefix.flags |= CameraFlagRng;
				}

				r->activeScreenEntry = ffx::CameActiveScreenIndex();
				r->activeSlotEntry = ffx::CameActiveSlotIndex();

				ffx::CameraBuildGlobals g;
				if (ffx::ReadCameraBuildGlobals(&g))
				{
					r->prefix.flags |= CameraFlagGlobals;
					r->directLookAtFlag = g.directLookAtFlag;
					r->overrideFlag = g.overrideFlag;
					r->levelNameHash = g.levelNameHash;
					r->levelNameHashValid = g.levelNameHashValid ? 1 : 0;
					r->pendingGlobalEffect = g.pendingGlobalEffect;
					r->pendingBattleEffect = g.pendingBattleEffect;
					r->ps2FrameWidth = g.ps2FrameWidth;
					r->ps2FrameHeight = g.ps2FrameHeight;
					r->magicId = g.magicId;
					r->magicPrevId = g.magicPrevId;
					r->magicFreezeFlag = g.magicFreezeFlag;
					r->liveSceneId = g.liveSceneId;
					r->nearLatchEntry = g.mainNearLatch;
					memcpy(r->directBlock, g.directBlock, sizeof(r->directBlock));
				}

				bool frozen = false;
				r->magicFrozen = ffx::QueryMagicCameraFrozen(&frozen) ? (frozen ? 1 : 0) : 0xFF;

				CopyMatrices(r->viewEntry, r->projEntry);

				const BYTE* screens = ffx::CameScreensBase();
				const BYTE* slots = ffx::CameSlotsBase();
				if (screens && slots)
				{
					memcpy(r->screensEntry, screens, sizeof(r->screensEntry));
					memcpy(r->slotsEntry, slots, sizeof(r->slotsEntry));
					r->prefix.flags |= CameraFlagBlocks;
				}

				g_open = r;
				return;
			}

			CameraRecord* r = g_open;
			if (!r)
				return;
			g_open = NULL;

			r->result = result;
			DWORD rng = 0;
			if (ffx::ReadRandStreamState(ffx::RandStreamCameraShake, &rng))
				r->rngStream9Exit = rng;
			r->activeScreenExit = ffx::CameActiveScreenIndex();
			r->activeSlotExit = ffx::CameActiveSlotIndex();

			ffx::CameraBuildGlobals g;
			if (ffx::ReadCameraBuildGlobals(&g))
				r->nearLatchExit = g.mainNearLatch;

			if (CopyMatrices(r->viewExit, r->projExit))
				r->prefix.flags |= CameraFlagMatrices;

			const BYTE* screens = ffx::CameScreensBase();
			if (screens)
				memcpy(r->screensExit, screens, sizeof(r->screensExit));

			workshop::CommitRecord(FileFor(KindCamera));
			EndRecord();
		}

	} // namespace

	DWORD CameraExtensionBytes(void* out, DWORD capacity)
	{
		CameraExtension e;
		e.screenBytes = CameScreenBytes;
		e.screenCount = CameScreenCount;
		e.slotBytes = CameSlotBytes;
		e.slotCount = CameSlotCount;
		if (capacity < sizeof(e))
			return 0;
		memcpy(out, &e, sizeof(e));
		return sizeof(e);
	}

	bool InstallCameraRecipe()
	{
		static_assert(CameScreenBytes == ffx::CameBuild::ScreenBytes, "screen size agrees with the kit");
		static_assert(CameSlotBytes == ffx::CameBuild::SlotBytes, "slot size agrees with the kit");

		const bool ok = ffx::HookActivateScreen(&OnActivateScreen);
		workshop::Log("camera recipe: %s",
		    ok ? "hooked FFX_Came_ActivateScreen" : "COULD NOT HOOK FFX_Came_ActivateScreen");
		return ok;
	}

} // namespace probe
