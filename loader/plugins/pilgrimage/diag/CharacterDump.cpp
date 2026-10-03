#include "diag/CharacterDump.h"

#include <windows.h>

#include "workshop/HostModule.h"
#include "workshop/Log.h"
#include "ffx/HideFlags.h"
#include "ffx/RenderProbe.h"
#include "ffx/Character.h"
#include "ffx/Layout.h"

namespace pilgrimage
{

	// The game library. Anything unqualified below that looks like FFX
	// knowledge, or like mod plumbing, comes from one of these two.
	using namespace ffx;
	using namespace workshop;

	void LogCharacterDiag(const char* tag, Character* chr)
	{
		if (!chr)
		{
			Log("diag %-10s <null>", tag);
			return;
		}

		LONG withoutNode = 0;
		LONG meshes = SubMeshCount(chr, &withoutNode);
		BYTE hideFlags = ByteAt(chr, Chr::HideFlags);

		Log("diag %-10s id=%-5d inUse=%u instance=0x%08X meshes=%ld noNode=%ld "
		    "hide=%u(%s) b181=%u party=%ld flags1=0x%08X flags2=0x%08X alpha=%.3f wmesh=%d",
		    tag,
		    (int)ShortAt(chr, Chr::Id),
		    ByteAt(chr, Chr::InUse),
		    DwordAt(chr, Chr::Instance), meshes, withoutNode,
		    hideFlags, HideFlagNames(hideFlags),
		    ByteAt(chr, Chr::Byte181),
		    (long)DwordAt(chr, Chr::PartyIndex),
		    DwordAt(chr, Chr::Flags1), DwordAt(chr, Chr::Flags2),
		    (double)FloatAt(chr, Chr::ShadeAlpha),
		    (int)ShortAt(chr, Chr::WalkmeshTri));

		// The verdict, so the log says what is wrong rather than needing to be read.
		Log("     %-10s draw gate: %s", tag, FirstFailingDrawGate(chr));
	}

	void DumpCharacterDiff(Character* clone, Character* player)
	{
		if (!IsLive(clone))
		{
			Log("diff: no live clone");
			return;
		}
		if (!IsLive(player))
		{
			Log("diff: no live player to compare against");
			return;
		}
		if (clone == player)
		{
			Log("diff: the clone IS the player, nothing to compare");
			return;
		}

		Log("---- character diff: player 0x%08X (slot %ld) vs clone 0x%08X (slot %ld) ----",
		    (unsigned)(UINT_PTR)player, SlotOfCharacter(player),
		    (unsigned)(UINT_PTR)clone, SlotOfCharacter(clone));
		LogCharacterDiag("player", player);
		LogCharacterDiag("clone", clone);

		const volatile DWORD* playerWords = (const volatile DWORD*)player;
		const volatile DWORD* cloneWords = (const volatile DWORD*)clone;

		struct Range
		{
			DWORD low, high;
			const char* name;
		};
		const Range ranges[] = {
			{ 0x000, 0x200, "A  0x000 to 0x200  id, flags and state" },
			{ 0x700, 0x880, "B  0x700 to 0x880  the tail, instances live here" },
		};
		const int rangeCount = (int)(sizeof(ranges) / sizeof(ranges[0]));

		for (int r = 0; r < rangeCount; ++r)
		{
			Log("  section %s", ranges[r].name);
			int shown = 0;
			for (DWORD offset = ranges[r].low; offset < ranges[r].high; offset += 4)
			{
				DWORD left = playerWords[offset / 4];
				DWORD right = cloneWords[offset / 4];
				if (left == right)
					continue;
				if (++shown > 96)
				{
					Log("    (more, truncated)");
					break;
				}
				Log("    +0x%03X  player 0x%08X (%g)   clone 0x%08X (%g)",
				    offset, left, (double)*(const float*)&left,
				    right, (double)*(const float*)&right);
			}
			if (!shown)
				Log("    identical");
		}

		// The render geometry, side by side. This is the part worth reading first.
		Log("  ---- render geometry ----");
		LogInstanceGeometry(player, "player");
		LogInstanceGeometry(clone, "clone");

		Log("  section C  pointer set on one side and zero on the other, whole struct");
		int holes = 0;
		for (DWORD offset = 0; offset < Chr::Stride; offset += 4)
		{
			DWORD left = playerWords[offset / 4];
			DWORD right = cloneWords[offset / 4];
			if (LooksLikePointer(left) && right == 0)
			{
				Log("    +0x%03X  player 0x%08X, CLONE IS NULL   <-- missing allocation",
				    offset, left);
				++holes;
			}
			else if (left == 0 && LooksLikePointer(right))
			{
				Log("    +0x%03X  player NULL, clone 0x%08X", offset, right);
				++holes;
			}
		}
		if (!holes)
			Log("    none, every pointer-shaped field is set on both or neither");
		Log("---- end character diff ----");
	}

} // namespace pilgrimage
