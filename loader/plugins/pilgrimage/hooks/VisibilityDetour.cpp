#include "hooks/VisibilityDetour.h"

#include <windows.h>

#include "ModState.h"
#include "clones/CloneRoster.h"
#include "workshop/Detour.h"
#include "ffx/Character.h"
#include "ffx/Addresses.h"
#include "ffx/Api.h"
#include "ffx/Layout.h"

namespace pilgrimage
{

	// The game library. Anything unqualified below that looks like FFX
	// knowledge, or like mod plumbing, comes from one of these two.
	using namespace ffx;
	using namespace workshop;

	namespace
	{

		DETOUR_DECLARE(UpdateCameLenAndZClip, int, (Character * chr));

		// Verified in IDA at 0x82E220. Six bytes, all position independent, so they
		// relocate to the trampoline unchanged.
		DETOUR_PROLOGUE(UpdateCameLenAndZClip) = {
			0x55,            // push ebp
			0x8B, 0xEC,      // mov  ebp, esp
			0x83, 0xEC, 0x28 // sub  esp, 28h
		};

		// Deliberately tiny: this runs once per live character per frame.
		int __cdecl UpdateCameLenAndZClipHook(Character* chr)
		{
			const int result = DETOUR_ORIGINAL(UpdateCameLenAndZClip)(chr);

			if (settings.forceVisible && IsManagedClone(chr))
			{
				const BYTE forced = Hide::NotVisible | Hide::PastClipZ;
				BYTE flags = ByteAt(chr, Chr::HideFlags);
				if (flags & forced)
				{
					WriteByte(chr, Chr::HideFlags, (BYTE)(flags & ~forced));
					InterlockedIncrement(&counters.visibilityForcedFrames);
				}
			}
			return result;
		}

	} // namespace

	bool InstallVisibilityDetour()
	{
		return DETOUR_INSTALL(UpdateCameLenAndZClip, Rva::ChUpdateCameLenAndZClip);
	}

	bool VisibilityDetourInstalled()
	{
		return DETOUR_INSTALLED(UpdateCameLenAndZClip);
	}

} // namespace pilgrimage
