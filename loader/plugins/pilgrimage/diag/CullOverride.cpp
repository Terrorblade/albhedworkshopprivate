#include "diag/CullOverride.h"

#include <windows.h>

#include "ModState.h"
#include "ffx/GfxContext.h"
#include "workshop/Log.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		DWORD savedValue = 0;
		LONG engineResets = 0;

	} // namespace

	// RE-ASSERTED EVERY FRAME ON PURPOSE. The first version of this wrote the dword
	// once, on the off-to-on transition, which made the experiment worthless if the
	// engine rewrites the field itself: the override would hold for an instant and
	// then quietly lapse, and the test would look like a clean negative when it had
	// really just stopped being applied. So it is written every frame while on, and
	// the number of times it was found already cleared is counted and reported. If
	// that number climbs, the engine owns the field and a one-shot poke can never
	// work.
	//
	// See ffx/GfxContext.h for why this experiment cannot prove what it looks like it
	// proves.
	void ApplyCullOverride()
	{
		DWORD* slot = CullOverrideSlot();
		if (!slot)
		{
			if (settings.disableCull)
			{
				Log("cull override: the graphics context is not readable yet, cannot force it");
				InterlockedExchange(&settings.disableCull, 0);
			}
			return;
		}

		if (settings.disableCull)
		{
			if (!counters.cullOverrideHeld)
			{
				savedValue = *slot;
				engineResets = 0;
				InterlockedExchange(&counters.cullOverrideHeld, 1);
				Log("cull override ON: the slot held %lu. Re-asserting it every frame.",
				    (unsigned long)savedValue);
			}
			else if (*slot == 0)
			{
				if (++engineResets <= 3 || (engineResets % 600) == 0)
					Log("cull override: the engine cleared the slot again (%ld times so far), "
					    "re-asserting. A one-shot write is not enough.",
					    engineResets);
			}
			*slot = 1;
		}
		else if (counters.cullOverrideHeld)
		{
			*slot = savedValue;
			InterlockedExchange(&counters.cullOverrideHeld, 0);
			Log("cull override OFF: the slot is back to %lu, the engine had cleared it "
			    "%ld times while on",
			    (unsigned long)savedValue, engineResets);
		}
	}

} // namespace pilgrimage
