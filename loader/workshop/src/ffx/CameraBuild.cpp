#include "ffx/CameraBuild.h"

#include <string.h>
#include <windows.h>

#include "ffx/Addresses.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{
	namespace
	{

		using workshop::Log;
		using workshop::ModuleAddress;
		using workshop::Readable;

		template <typename T>
		T Global(DWORD rva)
		{
			return *(const volatile T*)ModuleAddress(rva);
		}

		struct GlobalSpan
		{
			DWORD rva;
			DWORD bytes;
		};

		// Every global the camera build reads, checked on the first call and never again.
		// Module globals cannot stop being readable. 0 not asked, 1 yes, -1 no.
		const GlobalSpan cameraSpans[] = {
			{ Rva::CameScreens, CameBuild::ScreenBytes* CameBuild::ScreenCount },
			{ Rva::CameSlots, CameBuild::SlotBytes* CameBuild::SlotCount },
			{ Rva::CameViewMatrix, CameBuild::MatrixFloats * 4 },
			{ Rva::CameProjMatrix, CameBuild::MatrixFloats * 4 },
			{ Rva::CameActiveScreen, 4 },
			{ Rva::CameActiveSlot, 4 },
			{ Rva::CameDirectLookAtFlag, 4 },
			{ Rva::CameOverrideFlag, 4 },
			{ Rva::CameDirectBlock, CameBuild::DirectBlockBytes },
			{ Rva::CamePendingGlobalEffect, 4 },
			{ Rva::CamePendingBattleEffect, 4 },
			{ Rva::CamePs2FrameSize, 8 },
			{ Rva::MagicFileId, 4 },
			{ Rva::MagicFilePrevId, 4 },
			{ Rva::MagicCameraFreezeFlag, 4 },
			{ Rva::LiveSceneId, 4 },
			{ Rva::CameMainNearLatch, 1 },
			{ Rva::GfxContext, 4 },
		};
		int cameraVerdict = 0;

		bool CameraGlobalsReadable()
		{
			if (cameraVerdict == 0)
			{
				cameraVerdict = 1;
				for (size_t i = 0; i < sizeof(cameraSpans) / sizeof(cameraSpans[0]); ++i)
				{
					if (!Readable(ModuleAddress(cameraSpans[i].rva), cameraSpans[i].bytes))
					{
						Log("camera build: global at RVA 0x%08X is not readable, the camera "
						    "readers stay off",
						    cameraSpans[i].rva);
						cameraVerdict = -1;
						break;
					}
				}
			}
			return cameraVerdict > 0;
		}

		// The gfx context is a heap object behind a module global. Checked once per
		// distinct pointer value, which is once per boot unless the renderer is rebuilt.
		const BYTE* checkedGfx = NULL;
		bool checkedGfxOk = false;

		bool LevelNameHash(DWORD* out)
		{
			const BYTE* gfx = Global<const BYTE*>(Rva::GfxContext);
			if (gfx != checkedGfx)
			{
				checkedGfx = gfx;
				checkedGfxOk = gfx && Readable(gfx + CameBuild::GfxCtxLevelNameHash, 4);
			}
			if (!checkedGfxOk)
				return false;
			*out = *(const volatile DWORD*)(gfx + CameBuild::GfxCtxLevelNameHash);
			return true;
		}

		int IndexOf(DWORD rvaBase, DWORD stride, int count, const BYTE* p)
		{
			const BYTE* base = (const BYTE*)ModuleAddress(rvaBase);
			if (!p || p < base)
				return -1;
			const DWORD offset = (DWORD)(p - base);
			if (offset % stride != 0 || offset / stride >= (DWORD)count)
				return -1;
			return (int)(offset / stride);
		}

		// MagicFile__isCameraFrozen starts cmp dword ptr [dword_1940ABC], 0, which is 83 3D
		// then the relocated address. Checking that the operand is OUR address for the
		// freeze flag proves both the function and the global in one comparison.
		int frozenVerdict = 0;

		bool FrozenCallable()
		{
			if (frozenVerdict == 0)
			{
				const BYTE* p = (const BYTE*)ModuleAddress(Rva::MagicFileIsCameraFrozen);
				frozenVerdict = -1;
				if (Readable(p, 7) && p[0] == 0x83 && p[1] == 0x3D)
				{
					DWORD operand = 0;
					memcpy(&operand, p + 2, 4);
					if (operand == (DWORD)(UINT_PTR)ModuleAddress(Rva::MagicCameraFreezeFlag) && p[6] == 0x00)
						frozenVerdict = 1;
				}
				if (frozenVerdict < 0)
					Log("camera build: MagicFile__isCameraFrozen does not start with the expected "
					    "cmp against the freeze flag, so it will not be called");
			}
			return frozenVerdict > 0;
		}

		ActivateScreenObserverFn observer = NULL;

		DETOUR_DECLARE(ActivateScreen, int, (int screen));

		// Verified in IDA at 0x7BC110. push ebp, mov ebp esp, sub esp 20h, six bytes with
		// no relative operand. The mov to dword_C4423C that follows is not stolen.
		DETOUR_PROLOGUE(ActivateScreen) = {
			0x55,            // push ebp
			0x8B, 0xEC,      // mov  ebp, esp
			0x83, 0xEC, 0x20 // sub  esp, 20h
		};

		int __cdecl ActivateScreenHook(int screen)
		{
			ActivateScreenObserverFn watch = observer;
			if (watch)
				watch(screen, false, 0);
			const int result = DETOUR_ORIGINAL(ActivateScreen)(screen);
			if (watch)
				watch(screen, true, result);
			return result;
		}

	} // namespace

	const BYTE* CameScreensBase()
	{
		return CameraGlobalsReadable() ? (const BYTE*)ModuleAddress(Rva::CameScreens) : NULL;
	}

	const BYTE* CameSlotsBase()
	{
		return CameraGlobalsReadable() ? (const BYTE*)ModuleAddress(Rva::CameSlots) : NULL;
	}

	const float* CameViewMatrixLive()
	{
		return CameraGlobalsReadable() ? (const float*)ModuleAddress(Rva::CameViewMatrix) : NULL;
	}

	const float* CameProjMatrixLive()
	{
		return CameraGlobalsReadable() ? (const float*)ModuleAddress(Rva::CameProjMatrix) : NULL;
	}

	int CameActiveScreenIndex()
	{
		if (!CameraGlobalsReadable())
			return -1;
		return IndexOf(Rva::CameScreens, CameBuild::ScreenBytes, CameBuild::ScreenCount,
		    Global<const BYTE*>(Rva::CameActiveScreen));
	}

	int CameActiveSlotIndex()
	{
		if (!CameraGlobalsReadable())
			return -1;
		return IndexOf(Rva::CameSlots, CameBuild::SlotBytes, CameBuild::SlotCount,
		    Global<const BYTE*>(Rva::CameActiveSlot));
	}

	bool ReadCameraBuildGlobals(CameraBuildGlobals* out)
	{
		if (!out)
			return false;
		ZeroMemory(out, sizeof(*out));
		if (!CameraGlobalsReadable())
			return false;

		out->directLookAtFlag = Global<DWORD>(Rva::CameDirectLookAtFlag);
		out->overrideFlag = Global<DWORD>(Rva::CameOverrideFlag);
		memcpy(out->directBlock, ModuleAddress(Rva::CameDirectBlock), CameBuild::DirectBlockBytes);
		out->levelNameHashValid = LevelNameHash(&out->levelNameHash);
		out->pendingGlobalEffect = Global<int>(Rva::CamePendingGlobalEffect);
		out->pendingBattleEffect = Global<int>(Rva::CamePendingBattleEffect);
		out->ps2FrameWidth = Global<int>(Rva::CamePs2FrameSize);
		out->ps2FrameHeight = Global<int>(Rva::CamePs2FrameSize + 4);
		out->magicId = Global<int>(Rva::MagicFileId);
		out->magicPrevId = Global<int>(Rva::MagicFilePrevId);
		out->magicFreezeFlag = Global<DWORD>(Rva::MagicCameraFreezeFlag);
		out->liveSceneId = Global<int>(Rva::LiveSceneId);
		out->mainNearLatch = Global<BYTE>(Rva::CameMainNearLatch);
		return true;
	}

	bool QueryMagicCameraFrozen(bool* out)
	{
		if (!out || !CameraGlobalsReadable() || !FrozenCallable())
			return false;
		typedef BOOL(__cdecl * FrozenFn)(void);
		FrozenFn frozen = (FrozenFn)ModuleAddress(Rva::MagicFileIsCameraFrozen);
		*out = frozen() != 0;
		return true;
	}

	bool HookActivateScreen(ActivateScreenObserverFn fn)
	{
		observer = fn;
		if (DETOUR_INSTALLED(ActivateScreen))
			return true;
		return DETOUR_INSTALL(ActivateScreen, Rva::CameActivateScreen);
	}

	bool ActivateScreenHookInstalled()
	{
		return DETOUR_INSTALLED(ActivateScreen);
	}

} // namespace ffx
