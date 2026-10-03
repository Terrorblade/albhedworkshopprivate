#include "ffx/GfxContext.h"

#include "ffx/Api.h"
#include "ffx/Layout.h"
#include "workshop/HostModule.h"

namespace ffx
{

	DWORD* CullOverrideSlot()
	{
		if (!Game.gfxContext)
			return NULL;
		BYTE* context = *Game.gfxContext;
		if (!workshop::Readable(context, Gfx::DisableCull + 4))
			return NULL;
		return (DWORD*)(context + Gfx::DisableCull);
	}

} // namespace ffx
