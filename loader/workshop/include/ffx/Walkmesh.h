#pragma once

#include <windows.h>

#include "ffx/Api.h"

namespace ffx
{

	// Bind a character to a walkmesh triangle, and fill in its ground height.
	//
	// THE REASON THIS EXISTS is a real deadlock rather than a tidy-up, and it is the
	// whole answer to why a freshly spawned character is invisible:
	//
	//  - FFX_Ch_SetPos deliberately writes m_walkmeshTri = -1. That is the engine's
	//    "needs relocating" sentinel, not an error, and every placement path in the
	//    game leaves it that way.
	//  - The recovery normally happens inside FFX_Ch_UpdateMotionAll ->
	//    FFX_Ch_ResolveCollisionsAll -> FFX_Ch_WalkmeshMove on the next frame.
	//  - But FFX_Ch_UpdateMotionAll skips any character with m_hideFlags != 0 and
	//    m_b181 == 0, so a hidden character never gets relocated.
	//  - And FFX_Ch_Allocate sets m_groundMode = 1, which makes the motion pass write
	//    m_posY = m_groundHeight unconditionally. While unbound, m_groundHeight is
	//    still the 0.0 that Allocate's memset left, so the character is dragged to
	//    Y = 0.
	//
	// Put together: spawn, dragged to Y = 0, fails the camera test, hidden, motion
	// pass skips it, never relocated, stays at Y = 0 forever. Calling WalkmeshMove
	// ourselves breaks the cycle, and it is exactly what the engine would have done
	// anyway, just not skipped.
	//
	// SO: CALL THIS AFTER EVERY SetPos ON A CHARACTER YOU OWN. It is cheap and it is
	// the difference between a character that exists and one that is permanently
	// invisible.
	//
	// `reason` appears in the log line. Pass something starting with 's' (such as
	// "spawn") to force a log even when the triangle did not change.
	void BindToWalkmesh(Character* chr, const char* reason);

	// True when a walkmesh is loaded at all, which tells "no map yet" apart from
	// "this point is off the mesh".
	bool WalkmeshIsLoaded();

	// Which walkmesh triangle contains this world point, or -1.
	//
	// Ask this BEFORE committing a placement. Off the mesh, FFX_Ch_WalkmeshMove skips
	// collision entirely and does posX += velX * 10, posZ += velZ * 10, leaving the
	// ground height, the ground normal and the ground attributes stale. A character
	// with no velocity just sits there with stale ground state, which is survivable,
	// but one with any velocity gets flung.
	//
	// It handles the scaling, which is the easy mistake: the engine's
	// FFX_Ch_WalkmeshFindTri takes WALKMESH space, not world space.
	//
	// A -1 means "no triangle contains this XZ" and nothing else. Y never causes a -1,
	// it only picks between stacked floors: the test rejects triangles above the query
	// Y and keeps the lowest of those at or below it, so it finds the first floor under
	// your feet. Remember +Y is DOWN, so too small a Y means too high up, and that is
	// the safe direction to be wrong in.
	int WalkmeshTriangleAt(float x, float y, float z);

	// The question the above exists to answer.
	inline bool PointIsOnWalkmesh(float x, float y, float z)
	{
		return WalkmeshTriangleAt(x, y, z) >= 0;
	}

	// How many binds this plugin has done. Diagnostic only.
	LONG WalkmeshBindCount();

} // namespace ffx
