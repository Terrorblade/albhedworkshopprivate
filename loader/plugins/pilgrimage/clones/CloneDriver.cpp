#include "clones/CloneDriver.h"

#include <math.h>
#include <windows.h>

#include "ModState.h"
#include "ffx/Api.h"
#include "ffx/Camera.h"
#include "ffx/Character.h"
#include "ffx/Pad.h"

namespace pilgrimage
{

	// The game library, for the FFX knowledge used below.
	using namespace ffx;

	namespace
	{

		const float Pi = 3.14159265358979f;

		// Radial deadzone. The game's own is a per-axis band, but a radial one feels
		// better and nothing downstream cares.
		const float StickDeadzone = 0.25f;

		// What the player's own input resolves to for one frame.
		struct MoveIntent
		{
			float forward;   // -1 to 1, positive is away from the camera
			float right;     // -1 to 1
			float magnitude; // 0 to 1. Only the pad produces a partial magnitude.
			bool run;
			bool fromPad;
		};

		void ReadPadIntent(const PadState& pad, MoveIntent* out)
		{
			out->fromPad = true;

			// The engine stores the left stick Y positive-down, so forward is -leftY.
			out->right = pad.leftX;
			out->forward = -pad.leftY;
			out->magnitude = sqrtf(out->right * out->right + out->forward * out->forward);

			if (out->magnitude < StickDeadzone)
			{
				out->magnitude = 0.0f;
				out->right = 0.0f;
				out->forward = 0.0f;
			}
			else if (out->magnitude > 1.0f)
			{
				out->magnitude = 1.0f;
			}

			// The dpad is full deflection, same as the game treats it.
			if (pad.up || pad.down || pad.left || pad.right)
			{
				out->forward = (pad.up ? 1.0f : 0.0f) - (pad.down ? 1.0f : 0.0f);
				out->right = (pad.right ? 1.0f : 0.0f) - (pad.left ? 1.0f : 0.0f);
				out->magnitude = 1.0f;
			}

			// Cross is the WALK modifier in the shipped game, not the run button:
			// stepControl picks 16.0 while it is held and 38.0 otherwise, and 16 is below
			// the run threshold of 18 while 38 is above it. Matched here.
			out->run = !pad.cross;
		}

		void ReadKeyboardIntent(MoveIntent* out)
		{
			if (GetAsyncKeyState(VK_UP) & 0x8000)
				out->forward += 1.0f;
			if (GetAsyncKeyState(VK_DOWN) & 0x8000)
				out->forward -= 1.0f;
			if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
				out->right += 1.0f;
			if (GetAsyncKeyState(VK_LEFT) & 0x8000)
				out->right -= 1.0f;
			out->run = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
			out->magnitude = (out->forward != 0.0f || out->right != 0.0f) ? 1.0f : 0.0f;
		}

		// The half of DriveClone after the intent is known: heading, speed, and the turn
		// enable bit. Pulled out so the networked path can reach it without going anywhere
		// near the local camera.
		void ApplyHeadingAndSpeed(Character* chr, float heading, float magnitude, bool run)
		{
			const float baseSpeed = run ? settings.runSpeed : settings.walkSpeed;
			const float speed = baseSpeed * (magnitude > 0.0f ? magnitude : 1.0f);

			Game.SetMoveDir(chr, heading);
			Game.SetMoveSpeed(chr, speed);

			// Bit 0x400 is what enables turn-toward-heading, and the player path sets it every
			// substep rather than once, so this does the same.
			Game.SetFlags1Bit400(chr, 1);
		}

	} // namespace

	bool GameHasFocus()
	{
		if (!settings.requireForeground)
			return true;
		DWORD foregroundPid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
		return foregroundPid == GetCurrentProcessId();
	}

	void DriveCloneFromWorldDir(Character* chr, float dirX, float dirZ, bool run)
	{
		if (!chr)
			return;

		const float magnitude = sqrtf(dirX * dirX + dirZ * dirZ);
		if (magnitude <= 0.0f)
		{
			// Exactly 0.0 is what selects the idle animation, so this is both stop and
			// play idle, the same as the local path.
			Game.SetMoveSpeed(chr, 0.0f);
			return;
		}

		// Already a world direction, so the heading is just its angle. 0 along +X and
		// pi/2 along +Z, which is what FFX_Ch_UpdateMotionAll integrates against.
		const float heading = atan2f(dirZ, dirX);
		ApplyHeadingAndSpeed(chr, heading, magnitude > 1.0f ? 1.0f : magnitude, run);
	}

	void HoldCloneStill(Character* chr)
	{
		if (chr)
			Game.SetMoveSpeed(chr, 0.0f);
	}

	void DriveClone(Character* chr)
	{
		MoveIntent intent;
		intent.forward = 0.0f;
		intent.right = 0.0f;
		intent.magnitude = 0.0f;
		intent.run = false;
		intent.fromPad = false;

		PadState pad;
		const bool padReady = ReadPad(settings.padSlot, &pad);
		telemetry.padBound = padReady;
		telemetry.padsPresent = BoundPadCount();

		const bool wantPad = (settings.inputSource == InputSource::Pad) ||
		                     (settings.inputSource == InputSource::Auto && padReady);

		// A pad does not need window focus, a keyboard does.
		if (settings.inputEnabled && (GameHasFocus() || wantPad))
		{
			if (wantPad && padReady)
				ReadPadIntent(pad, &intent);
			else if (GameHasFocus())
				ReadKeyboardIntent(&intent);
		}
		telemetry.usingPad = intent.fromPad;

		float cameraYaw = 0.0f;
		const bool haveCamera = ActiveCameraYaw(&cameraYaw);
		telemetry.cameraYaw = cameraYaw;
		telemetry.cameraYawValid = haveCamera;

		if (intent.forward == 0.0f && intent.right == 0.0f)
		{
			// Exactly 0.0 is what selects the idle animation, so this is both "stop"
			// and "play idle".
			Game.SetMoveSpeed(chr, 0.0f);
			return;
		}

		// This is exactly what FFX_Player__stepControl does with the left stick:
		//
		//     desired   = atan2(right, forward)      note the argument order
		//     m_moveDir = cameraYaw - desired
		//
		// confirmed at 0x82D658 for the atan2 and 0x82D786 for the subtraction and the
		// fchs that negates it.
		const float desired = atan2f(intent.right, intent.forward);
		float heading = (settings.cameraRelative && haveCamera) ? (cameraYaw - desired) : desired;
		heading += settings.yawBiasDegrees * Pi / 180.0f;

		// Scale by stick deflection the way the game does, so a half-pushed stick walks.
		// The keyboard always reports magnitude 1, so it is unaffected.
		ApplyHeadingAndSpeed(chr, heading, intent.magnitude, intent.run);
	}

} // namespace pilgrimage
