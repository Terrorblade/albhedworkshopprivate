#include "ffx/Walkmesh.h"

#include "ffx/addresses/Character.h"
#include "ffx/Character.h"
#include "ffx/Layout.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

using workshop::Log;
using workshop::ModuleAddress;

namespace ffx
{
	namespace
	{

		volatile LONG bindCount = 0;

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

} // namespace ffx
