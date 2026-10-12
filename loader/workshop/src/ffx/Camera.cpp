#include "ffx/Camera.h"

#include <math.h>
#include <stdio.h>
#include <windows.h>

#include "ffx/Api.h"
#include "ffx/Layout.h"
#include "ffx/addresses/Atel.h"
#include "ffx/addresses/Character.h"
#include "ffx/addresses/Cutscene.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	namespace
	{

		using workshop::Log;
		using workshop::ModuleAddress;
		using workshop::Readable;

		typedef int(__cdecl* FindPrioSlotFn)(int handle);
		typedef int(__cdecl* GetActiveHandleFn)(BYTE screen);
		typedef int(__cdecl* GetActivePrioSlotFn)(BYTE screen);
		typedef int(__cdecl* OpenHandleFn)(int owner, unsigned int screen, char classCode);
		typedef int(__cdecl* CloseHandleFn)(int handle);
		typedef void(__cdecl* GetPosFn)(int handle, char refOrCam, float* outVec3);
		typedef int(__cdecl* SetTargetActorsFn)(int handle, char refOrCam, short actor0, short actor1, const int* offsetVec4);
		typedef int(__cdecl* FindActorIdByChrFn)(Character* chr);

		template <typename T>
		T Fn(DWORD rva)
		{
			void* p = (void*)ModuleAddress(rva);
			return Readable(p, 1) ? (T)p : (T)NULL;
		}

		// ---------------------------------------------------------------------------
		// WHERE A MODE RECORD LIVES, worked out once here rather than at each caller.
		//
		//   slot      = cameSlots + 0xDA0 * screen
		//   subBlock  = slot + 0x30 + 1564 * (eye ? 1 : 0)
		//   record    = subBlock + 304 * prio + 192
		//   modeByte  = record[2]
		//
		// Read off FFX_Came_SetTargetActors, which writes exactly
		// `&unk_1137570 + 3488*screen + 1564*refOrCam + 304*prio + 192`, where
		// 0x1137570 is cameSlots + 0x30. The 3488 and 1564 are the same 0xDA0 and
		// 0x61C the rest of the kit uses, spelled in decimal by the decompiler.
		// ---------------------------------------------------------------------------
		const DWORD kSubBlockBase = 0x0030;
		const DWORD kSubBlockStride = 1564;
		const DWORD kModeRecordStride = 304;
		const DWORD kModeRecordInSubBlock = 192;
		const DWORD kModeByteInRecord = 2;

		const BYTE* ModeRecord(int screen, bool eyeNotLookAt, int prio)
		{
			if (screen < 0 || (DWORD)screen >= Came::SlotCount)
				return NULL;
			if (prio < 0 || prio > 3)
				return NULL;

			const BYTE* record = Game.cameSlots + Came::SlotStride * (DWORD)screen
			    + kSubBlockBase + kSubBlockStride * (eyeNotLookAt ? 1u : 0u)
			    + kModeRecordStride * (DWORD)prio + kModeRecordInSubBlock;

			return Readable(record, 64) ? record : NULL;
		}

	} // namespace

	// Lifted from the game's own FFX_Came_GetYaw 0x7BCF20, which is
	//
	//     ecx = activeCameraSlot
	//     FFX_Came_VecToPolar(ecx + 0x50, ecx + 0x66C, &theta, NULL, NULL)
	//
	// and VecToPolar computes atan2(delta.z, delta.x) of (target - eye). So the yaw
	// is atan2(target.z - eye.z, target.x - eye.x). m_moveDir uses the identical zero
	// axis and direction, confirmed in FFX_Ch_UpdateMotionAll where
	// velX = cos(moveDir) and velZ = sin(moveDir), so the two combine with no
	// conversion at all.
	//
	// WHY THE atan2 IS DONE HERE rather than by calling FFX_Came_GetYaw. VecToPolar
	// does its arithmetic through fixed global scratch at 0xC8F7D0 which it shares
	// with the look-at matrix builder. Calling it is fine from the game thread during
	// the step phase and not fine from anywhere else. Reading the two vectors touches
	// nothing, so it is safe unconditionally and one less thing to reason about.
	//
	// WHY NOT g_ffxPlayerCamYaw 0x130079C, which is a plain float sitting right
	// there. It is only refreshed inside stepControl's analogue-stick branch, so it is
	// skipped when control is disabled, skipped when the dpad branch is taken, and in
	// control mode 3 only refreshed past a 0.349 radian change. It goes stale during
	// cutscenes. The slot is always current.
	bool ActiveCameraYaw(float* outYaw)
	{
		BYTE* slot = *Game.cameActiveSlot;
		if (!slot)
			return false;

		// Validate rather than trust: the pointer must land exactly on one of the
		// three slots. It should never be null once FFX_Came_Init has run, but this
		// mod attaches mid-game and then dereferences +0x66C off it.
		ptrdiff_t delta = slot - Game.cameSlots;
		if (delta < 0 || (delta % Came::SlotStride) != 0)
			return false;
		if ((DWORD)(delta / Came::SlotStride) >= Came::SlotCount)
			return false;

		const volatile float* target = (const volatile float*)(slot + Came::TargetPos);
		const volatile float* eye = (const volatile float*)(slot + Came::EyePos);
		float dx = target[0] - eye[0];
		float dz = target[2] - eye[2];
		if (dx == 0.0f && dz == 0.0f)
			return false; // degenerate, eye sits on target

		*outYaw = atan2f(dz, dx);
		return true;
	}

	// ---------------------------------------------------------------------------
	// Who owns the view.
	// ---------------------------------------------------------------------------

	bool FollowCameraIsDriving(int screen)
	{
		if (screen < 0 || (DWORD)screen >= Came::SlotCount)
			return false;

		FindPrioSlotFn find = Fn<FindPrioSlotFn>(Rva::CameFindPrioSlotForHandle);
		if (!find)
			return false;

		// HANDLE 0, WHICH IS WHAT THE FOLLOW CAMERA ALWAYS HAS. Its screen is the low
		// byte, so passing the screen index on its own IS handle 0 for that screen,
		// which is exactly what FFX_PlayerCam_Publish ends up passing.
		//
		// And the return sense is the engine's, not ours: -1 means a write would go
		// through, 0 means it would be dropped. Verified against FFX_Came_SetPos,
		// which does `if (result != 0)` and writes nothing otherwise.
		return find(screen) != 0;
	}

	DWORD ActiveCameraHandle(int screen)
	{
		if (screen < 0 || (DWORD)screen >= Came::SlotCount)
			return 0;

		GetActiveHandleFn get = Fn<GetActiveHandleFn>(Rva::CameGetActiveHandle);
		if (!get)
			return 0;

		return (DWORD)get((BYTE)screen);
	}

	int CameraHandleClass(DWORD handle)
	{
		return (int)((handle >> 16) & 0xFu);
	}

	const char* CameraHandleClassName(DWORD handle)
	{
		// The engine prints these as single letters in its own debug window. Spelled
		// out here because a log line reading "class M" helps nobody.
		switch (CameraHandleClass(handle))
		{
			case 0:
				return "none";
			case 1:
				return "map";
			case 3:
				return "event";
			case 5:
				return "battle";
			case 7:
				return "viewer";
			default:
				break;
		}

		static char other[24];
		_snprintf_s(other, sizeof(other), _TRUNCATE, "class %d", CameraHandleClass(handle));
		return other;
	}

	const char* ActiveCameraModeName(int screen, bool eyeNotLookAt)
	{
		GetActivePrioSlotFn prioOf = Fn<GetActivePrioSlotFn>(Rva::CameGetActivePrioSlot);
		if (!prioOf || !Game.cameSlots)
			return "unknown";

		const BYTE* record = ModeRecord(screen, eyeNotLookAt, prioOf((BYTE)screen));
		if (!record)
			return "unknown";

		const BYTE mode = record[kModeByteInRecord];

		// The name table is an array of pointers, so both it and the string it picks
		// have to be checked. 32 is a generous ceiling on a mode byte, and the engine
		// uses 17 of them.
		const char* const* names = (const char* const*)ModuleAddress(Rva::CameModeNames);
		if (mode > 32 || !Readable(names, sizeof(char*) * 33))
			return "unknown";

		const char* name = names[mode];
		if (!name || !Readable(name, 1))
			return "unknown";

		return name;
	}

	bool CameraMatrixOverrideActive()
	{
		const DWORD* flag = (const DWORD*)ModuleAddress(Rva::CameOverrideFlag);
		return Readable(flag, sizeof(DWORD)) && *flag != 0;
	}

	// ---------------------------------------------------------------------------
	// Pointing a script camera at something.
	// ---------------------------------------------------------------------------

	int ActorIdForCharacter(Character* chr)
	{
		if (!chr)
			return -1;

		FindActorIdByChrFn find = Fn<FindActorIdByChrFn>(Rva::AtelFindActorIdByChr);
		if (!find)
			return -1;

		return find(chr);
	}

	bool SetCameraTargetActors(DWORD handle, bool eyeNotLookAt, int actorId0, int actorId1, float offsetY)
	{
		SetTargetActorsFn set = Fn<SetTargetActorsFn>(Rva::CameSetTargetActors);
		if (!set)
			return false;

		// 0xFFFF is the engine's "unused", and it is what -1 truncates to anyway. Said
		// explicitly so a caller passing -1 and a caller passing 0xFFFF mean the same
		// thing on purpose rather than by accident.
		const short kUnusedActor = -1; // 0xFFFF as a short, which is what it is
		const short a0 = (actorId0 < 0) ? kUnusedActor : (short)actorId0;
		const short a1 = (actorId1 < 0) ? kUnusedActor : (short)actorId1;

		// A vec4, because the engine copies four dwords out of it. The w is left at
		// zero, which is what every game caller passes.
		float offset[4];
		offset[0] = 0.0f;
		offset[1] = offsetY;
		offset[2] = 0.0f;
		offset[3] = 0.0f;

		return set((int)handle, eyeNotLookAt ? 1 : 0, a0, a1, (const int*)offset) != 0;
	}

	DWORD OpenCameraHandle(int owner, int screen, int classCode)
	{
		// Class 0 is refused by the engine with no diagnostic, and that silent refusal
		// is what made FFX_PlayerCam_Publish's handle always zero. Caught here instead,
		// where there is somewhere to say so.
		if (classCode <= 0 || classCode > 15)
		{
			Log("camera: refusing to open a handle with class %d. The engine only accepts "
			    "1 to 15, and it rejects 0 silently, which is a bug waiting to happen.",
			    classCode);
			return 0;
		}
		if (screen < 0 || (DWORD)screen >= Came::SlotCount)
			return 0;

		OpenHandleFn open = Fn<OpenHandleFn>(Rva::CameOpenHandle);
		if (!open)
			return 0;

		return (DWORD)open(owner, (unsigned int)screen, (char)classCode);
	}

	void CloseCameraHandle(DWORD handle)
	{
		if (!handle)
			return;

		CloseHandleFn close = Fn<CloseHandleFn>(Rva::CameCloseHandle);
		if (close)
			close((int)handle);
	}

	bool CameraHandlePos(DWORD handle, bool eyeNotLookAt, float* outXyz)
	{
		if (!outXyz)
			return false;

		GetPosFn get = Fn<GetPosFn>(Rva::CameGetPos);
		if (!get)
			return false;

		// Four, not three. The engine's writers move a vec4 through here and there is
		// no reason to believe the reader is narrower than its siblings.
		float scratch[4];
		scratch[0] = 0.0f;
		scratch[1] = 0.0f;
		scratch[2] = 0.0f;
		scratch[3] = 0.0f;

		get((int)handle, eyeNotLookAt ? 1 : 0, scratch);

		outXyz[0] = scratch[0];
		outXyz[1] = scratch[1];
		outXyz[2] = scratch[2];
		return true;
	}

} // namespace ffx
