#include "ffx/Camera.h"

#include <math.h>
#include <windows.h>

#include "ffx/Api.h"
#include "ffx/Layout.h"

namespace ffx
{

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

} // namespace ffx
