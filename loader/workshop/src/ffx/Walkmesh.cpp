#include "ffx/Walkmesh.h"

#include "ffx/addresses/Character.h"
#include "ffx/Character.h"
#include "ffx/Layout.h"
#include "workshop/Detour.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

using workshop::Log;
using workshop::ModuleAddress;

namespace ffx
{
	namespace
	{

		volatile LONG bindCount = 0;

		// The engine's own signature. BOOL is an int here and the one call site tests
		// eax, so anything non-zero reads as blocked.
		typedef int(__cdecl* IsTriBlockedFn)(Character* chr, int tri);

		workshop::CallSitePatch playerBodySite;
		IsTriBlockedFn originalIsTriBlocked = NULL;
		PlayerBodyTestFn playerBodyTest = NULL;
		bool playerBodyInstalled = false;

		// Resolved once at install, read on the hot path. These are cached rather than
		// looked up per call because the hook runs inside the collision pass, and the
		// range check they would otherwise need is a VirtualQuery.
		Character** controlledSlot = NULL;
		const int* remapTable = NULL;

		volatile LONG passagesOpened = 0;
		volatile LONG wallsEnforced = 0;

		// The lowest and highest surface code that is an index into the remap table.
		const int kRemapFirst = 48;
		const int kRemapLast = 63;

		// ---------------------------------------------------------------------------
		// The replacement for the two player-only codes.
		//
		// The shape here is deliberately timid, and the order of the three early outs
		// is the whole safety argument:
		//
		//  1. No test installed, no character, no original to fall back on: the engine
		//     answers. Nothing changes.
		//  2. The test says Unknown: the engine answers. That is how a plugin says
		//     "I do not know yet" without having to guess, and it covers solo play, a
		//     map load and the first step of a session.
		//  3. The test AGREES with the engine's single global about this character:
		//     the engine answers. This is the common case even mid-session, because on
		//     the machine whose player drives the party leader the two never differ.
		//
		// Only a genuine disagreement gets this far, and even then only codes 2 and 14
		// are decided here. Every other code goes back to the original, which is both
		// correct and the safer thing to return if the decode is ever wrong.
		// ---------------------------------------------------------------------------
		int __cdecl IsTriBlockedHook(Character* chr, int tri)
		{
			if (!originalIsTriBlocked)
				return 0;
			if (!playerBodyTest || !chr || !controlledSlot)
				return originalIsTriBlocked(chr, tri);

			const PlayerBodyAnswer answer = playerBodyTest(chr);
			if (answer == PlayerBodyUnknown)
				return originalIsTriBlocked(chr, tri);

			const bool coopSaysPlayer = (answer == PlayerBodyYes);
			const bool engineSaysPlayer = (*controlledSlot == chr);
			if (engineSaysPlayer == coopSaysPlayer)
				return originalIsTriBlocked(chr, tri);

			const int code = WalkmeshResolvedSurfaceCode(tri);
			if (code != kSurfacePlayerOnlyPassage && code != kSurfacePlayerOnlyWall)
				return originalIsTriBlocked(chr, tri);

			// A passage is a wall for everyone who is not a player. A player-only wall
			// is the other way round. Both reduce to the same one question once the
			// identity test has been replaced.
			const bool blocked = (code == kSurfacePlayerOnlyPassage)
			    ? !coopSaysPlayer
			    : coopSaysPlayer;

			// The engine's answer is always the opposite of ours here, because we only
			// reach this line when the two disagree about the character.
			if (blocked)
				InterlockedIncrement(&wallsEnforced);
			else
				InterlockedIncrement(&passagesOpened);

			return blocked ? 1 : 0;
		}

	} // namespace

	bool WalkmeshIsLoaded()
	{
		return Game.walkmeshTris && *Game.walkmeshTris != NULL;
	}

	LONG WalkmeshBindCount()
	{
		return bindCount;
	}

	int WalkmeshTriangleAt(float x, float y, float z)
	{
		if (!WalkmeshIsLoaded())
			return -1;

		const float scale = *Game.walkmeshScale;
		if (scale == 0.0f)
			return -1;

		// Walkmesh space, not world space. Getting this wrong does not return -1, it
		// returns a triangle somewhere else entirely.
		float query[3];
		query[0] = x * scale;
		query[1] = y * scale;
		query[2] = z * scale;

		typedef int(__cdecl * FindTriFn)(float*);
		FindTriFn find = (FindTriFn)ModuleAddress(Rva::ChWalkmeshFindTri);
		if (!find)
			return -1;

		return find(query);
	}

	void BindToWalkmesh(Character* chr, const char* reason)
	{
		if (!chr || !Game.WalkmeshMove)
			return;
		if (!WalkmeshIsLoaded())
		{
			Log("walkmesh bind (%s) skipped: the triangle array is null, no walkmesh is loaded",
			    reason);
			return;
		}

		const short triangleBefore = ShortAt(chr, Chr::WalkmeshTri);
		float x = 0.0f, yBefore = 0.0f, z = 0.0f;
		Game.GetPos(chr, &x, &yBefore, &z);

		// WalkmeshMove adds m_velX and m_velZ to the position before resolving, so
		// zeroing them first makes the position round-trip and turns this into a pure
		// bind rather than a step.
		WriteFloat(chr, Chr::VelocityX, 0.0f);
		WriteFloat(chr, Chr::VelocityZ, 0.0f);
		Game.WalkmeshMove(chr);
		InterlockedIncrement(&bindCount);

		const short triangleAfter = ShortAt(chr, Chr::WalkmeshTri);
		float yAfter = 0.0f;
		Game.GetPos(chr, &x, &yAfter, &z);

		const bool isSpawnBind = (reason[0] == 's');
		if (triangleBefore != triangleAfter || isSpawnBind)
		{
			Log("walkmesh bind (%s): tri %d -> %d, groundHeight %.2f, Y %.2f -> %.2f, "
			    "tris=0x%08X count=%d scale=%.4f",
			    reason, triangleBefore, triangleAfter,
			    (double)FloatAt(chr, Chr::GroundHeight), (double)yBefore, (double)yAfter,
			    (unsigned)(UINT_PTR)*Game.walkmeshTris, *Game.walkmeshTriCount,
			    (double)*Game.walkmeshScale);
		}
	}

	// ---------------------------------------------------------------------------
	// Surface codes.
	// ---------------------------------------------------------------------------

	int WalkmeshSurfaceCode(int tri)
	{
		if (tri < 0 || !WalkmeshIsLoaded())
			return -1;
		if (!Game.walkmeshTriCount || tri >= *Game.walkmeshTriCount)
			return -1;

		// 16 bytes per triangle: u16 v0, v1, v2, then three s16 neighbours, then the
		// attribute dword. The engine reads the same byte with [base + tri*16 + 12].
		const BYTE* tris = (const BYTE*)*Game.walkmeshTris;
		const DWORD attributes = *(const DWORD*)(tris + (size_t)tri * 16 + 12);
		return (int)(attributes & 0x7Fu);
	}

	int WalkmeshResolvedSurfaceCode(int tri)
	{
		int code = WalkmeshSurfaceCode(tri);
		if (code < 0)
			return -1;
		if (!remapTable)
			return code;

		// The engine's loop with a hop limit bolted on. Its exit condition is copied
		// exactly: a hop that lands on 0, or above 63, means walkable.
		for (int hop = 0; hop < 8 && code >= kRemapFirst && code <= kRemapLast; ++hop)
		{
			code = remapTable[code];
			if (code < 1 || code > kRemapLast)
				return 0;
		}

		// Still in the remap range after eight hops, so a script has built a cycle and
		// the engine would be spinning on it right now. Answer walkable, which is what
		// it answers for a hop that leaves the range.
		if (code >= kRemapFirst && code <= kRemapLast)
			return 0;

		return code;
	}

	// ---------------------------------------------------------------------------
	// The player-only surfaces.
	// ---------------------------------------------------------------------------

	void SetPlayerBodyTest(PlayerBodyTestFn test)
	{
		playerBodyTest = test;
	}

	bool PlayerBodyWalkmeshInstalled()
	{
		return playerBodyInstalled;
	}

	LONG WalkmeshPassagesOpened()
	{
		return passagesOpened;
	}

	LONG WalkmeshWallsEnforced()
	{
		return wallsEnforced;
	}

	bool InstallPlayerBodyWalkmesh(PlayerBodyTestFn test)
	{
		if (playerBodyInstalled)
		{
			SetPlayerBodyTest(test);
			return true;
		}

		if (!workshop::LooksLikeFunctionStart(Rva::MapIsTriBlockedForChr))
		{
			Log("walkmesh: FFX_Map_IsTriBlockedForChr RVA 0x%08X does not start like a "
			    "function, so this is not the build the address was recorded on. "
			    "Refusing the player-only surface patch, which leaves the engine's "
			    "single-player answer in place. Expect the two machines to disagree "
			    "about the 6087 player-only walls.",
			    Rva::MapIsTriBlockedForChr);
			return false;
		}

		Character** controlled = (Character**)ModuleAddress(Rva::ControlledChr);
		const int* remap = (const int*)ModuleAddress(Rva::GroundTypeRemapBiasedBase);

		// Validated ONCE, here. The hook reads both of these inside the engine's
		// collision pass, so a check in the hook body would be a VirtualQuery per edge
		// crossing per character per frame. Only indices 48..63 of the table are ever
		// read, so that is the window that has to be mapped, and the biased base
		// itself does not have to be.
		if (!workshop::Readable(controlled, sizeof(void*))
		    || !workshop::Readable(remap + kRemapFirst,
		           (kRemapLast - kRemapFirst + 1) * sizeof(int)))
		{
			Log("walkmesh: refusing the player-only surface patch, the player binding "
			    "or the ground type remap window is not readable");
			return false;
		}

		controlledSlot = controlled;
		remapTable = remap;
		originalIsTriBlocked = (IsTriBlockedFn)ModuleAddress(Rva::MapIsTriBlockedForChr);
		playerBodyTest = test;

		// The call site rather than the function, because the function has exactly one
		// caller and leaving it intact is what lets the hook defer to it. Nothing is
		// relocated and the target is never written to.
		if (!workshop::PatchCallSite(playerBodySite, Rva::MapIsTriBlockedCallSite,
		        Rva::MapIsTriBlockedForChr, (void*)&IsTriBlockedHook,
		        "player-only surfaces"))
		{
			playerBodyTest = NULL;
			originalIsTriBlocked = NULL;
			controlledSlot = NULL;
			remapTable = NULL;
			return false;
		}

		playerBodyInstalled = true;
		Log("walkmesh: the player-only surface codes now ask who owns a body instead of "
		    "which single character the engine calls the player. Codes 2 and 14 only, "
		    "every other code still goes to the engine, and a test that answers Unknown "
		    "changes nothing.");
		return true;
	}

} // namespace ffx
