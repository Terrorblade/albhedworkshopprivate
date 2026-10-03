#pragma once

#include "ffx/Api.h"

// Turning player input into a character's speed and heading.
//
// HEADING CONVENTION, which is the thing to get right.
//   FFX_Ch_UpdateMotionAll integrates as
//       m_velX = cos(m_moveDir) * k
//       m_velZ = sin(m_moveDir) * k
//   so m_moveDir is a radian angle in the XZ plane with 0 along +X and pi/2 along
//   +Z. A world direction (dx, dz) therefore has heading atan2f(dz, dx).
//
//   Which arrow means which world direction depends on where the camera points, so
//   camera-relative mode subtracts the camera yaw exactly the way the game's own
//   control path does. The yaw bias on top is a live dial for when the convention
//   still looks wrong on some map, so it can be fixed without a rebuild.
//
//   Speed selects the animation on its own: FFX_Ch_AutoLocomotionAnim plays idle at
//   exactly 0.0, walk below m_runThreshold (18.0 by default) and run above it. So
//   the walk and run speeds are also the gait control and there is nothing else to
//   drive.

namespace pilgrimage
{

	// Reads the keyboard or the pad and writes this character's heading and speed.
	void DriveClone(ffx::Character* chr);

	// Drives a character from a WORLD SPACE direction instead of from this machine's own
	// input. This is the networked path: the numbers arrive already resolved against the
	// owner's camera and already quantised to the wire, so both machines compute the same
	// heading from the same bytes. See the InputFrame comment in workshop/Protocol.h.
	//
	// dirX and dirZ are a direction in the XZ plane whose length is the stick deflection.
	// A zero length vector stops the character and selects its idle animation, which is
	// the same thing here as it is everywhere else in this file.
	//
	// Deliberately does NOT touch the camera, the yaw bias, or settings.cameraRelative.
	// Those are local presentation dials and letting them reach a networked character is
	// how one machine ends up walking somewhere the other did not.
	void DriveCloneFromWorldDir(ffx::Character* chr, float dirX, float dirZ, bool run);

	// Zero speed, which both stops a character and selects its idle animation. Used on
	// every clone that does not currently hold the input focus.
	void HoldCloneStill(ffx::Character* chr);

	// True when the game window is in the foreground, or when the caller does not care
	// because settings.requireForeground is off.
	bool GameHasFocus();

} // namespace pilgrimage
