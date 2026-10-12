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

	// ---------------------------------------------------------------------------
	// SURFACE CODES
	// ---------------------------------------------------------------------------

	// The codes that mean something. Everything else is walkable. Of the four, only 1
	// and 14 appear in shipped walkmeshes: a census over all 469 sections found 317228
	// code 0, 663 code 1, 6087 code 14 and 18841 in the remapped 48..61 range. Codes 2
	// and 13 only ever arrive through the run time remap table.
	const int kSurfaceWallForEveryone = 1;
	const int kSurfacePlayerOnlyPassage = 2; // a wall for everyone who is not the player
	const int kSurfaceFlags2Keyed = 13;      // keyed on the character's m_flags2 bit 0x4
	const int kSurfacePlayerOnlyWall = 14;   // a wall for the player and nobody else

	// A triangle's raw surface code, the low 7 bits of its attribute dword. Returns -1
	// when no walkmesh is loaded or the index is out of range.
	int WalkmeshSurfaceCode(int tri);

	// The same code with the run time indirection applied, which is the code that
	// actually decides passability. Returns 0 for walkable and -1 when it cannot read.
	//
	// Codes 48..63 are indices into a table an ATEL script writes, so the map file on
	// disk does not tell you what one of those surfaces does. The engine resolves them
	// in a loop with no hop limit, so a script that points two entries at each other
	// hangs it. This gives up after a few hops and answers walkable, which is the same
	// answer the engine gives for a hop that lands out of range.
	int WalkmeshResolvedSurfaceCode(int tri);

	// ---------------------------------------------------------------------------
	// MAKING THE PLAYER-ONLY SURFACES WORK FOR MORE THAN ONE PLAYER
	//
	// Read the note on Rva::MapIsTriBlockedForChr for why this is needed. Short
	// version: two surface codes are answered with "is this character the one global
	// player", and in co-op that global holds a different character on each machine.
	//
	// Install a test that says whether a character is ANY player's body and those two
	// codes get answered from it instead. The test has to be a pure function of state
	// both machines already agree on, because two machines disagreeing about who can
	// cross a wall is not something a later message can repair.
	//
	// IT RUNS INSIDE THE ENGINE'S COLLISION PASS, several times per character per
	// frame, on the step thread. So it must be cheap, it must not call anything that
	// steps the game, and it must not call workshop::Readable, which is a VirtualQuery.
	// ---------------------------------------------------------------------------

	// What the test is allowed to say. Three answers rather than two on purpose:
	// Unknown is how "no session yet", "the bodies are not resolved yet" and "a map is
	// loading" are all expressed, and it leaves the shipped game's behaviour alone to
	// the byte. Every uncertainty should come out here.
	enum PlayerBodyAnswer
	{
		PlayerBodyUnknown = 0, // leave this one to the engine, change nothing
		PlayerBodyYes,         // treat as a player for codes 2 and 14
		PlayerBodyNo,          // treat as not a player
	};

	typedef PlayerBodyAnswer(__cdecl* PlayerBodyTestFn)(Character* chr);

	// Patch the one call site, once, at startup. Returns false and logs on any
	// mismatch, and the game is left exactly as it was. Calling it again just swaps
	// the test.
	bool InstallPlayerBodyWalkmesh(PlayerBodyTestFn test);

	// Swap the test, or pass NULL to hand the question back to the engine entirely.
	// The patch stays in either way, per the install-once-gate-at-runtime rule.
	void SetPlayerBodyTest(PlayerBodyTestFn test);

	bool PlayerBodyWalkmeshInstalled();

	// How many times the replacement gave an answer the engine would not have, by
	// direction. Both staying at zero during a session means either nobody has walked
	// into a player-only surface or the test never said Yes, and those are worth
	// telling apart before trusting this.
	LONG WalkmeshPassagesOpened(); // we said walkable where the engine said wall
	LONG WalkmeshWallsEnforced();  // we said wall where the engine said walkable

} // namespace ffx
