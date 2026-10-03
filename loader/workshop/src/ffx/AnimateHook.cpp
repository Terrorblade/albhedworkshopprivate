#include "ffx/AnimateHook.h"

#include <windows.h>

#include "ffx/Addresses.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{
	namespace
	{

		AnimateFn original = NULL;

	} // namespace

	AnimateFn OriginalAnimate()
	{
		return original;
	}

	bool HookAnimate(AnimateFn replacement)
	{
		void* slot = workshop::ModuleAddress(Rva::AnimateVtableSlot);

		DWORD previous = 0;
		if (!workshop::WriteProtectedDword(slot, (DWORD)(UINT_PTR)replacement, &previous))
		{
			workshop::Log("animate hook: could not write the vtable slot at 0x%08X",
			    (unsigned)(UINT_PTR)slot);
			return false;
		}
		original = (AnimateFn)(UINT_PTR)previous;

		workshop::Log("animate hook installed: slot 0x%08X  0x%08X -> 0x%08X",
		    (unsigned)(UINT_PTR)slot, previous, (unsigned)(UINT_PTR)replacement);
		return true;
	}

} // namespace ffx
