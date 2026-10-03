#include "ffx/HideFlags.h"

#include <string.h>

#include "ffx/Layout.h"

namespace ffx
{

	const char* HideFlagNames(BYTE flags)
	{
		static char text[40];
		text[0] = 0;
		if (!flags)
			return "visible";
		if (flags & Hide::EventScript)
			strcat_s(text, sizeof(text), "Ev ");
		if (flags & Hide::Effect)
			strcat_s(text, sizeof(text), "Eff ");
		if (flags & Hide::NotVisible)
			strcat_s(text, sizeof(text), "Disp ");
		if (flags & Hide::Battle)
			strcat_s(text, sizeof(text), "Btl ");
		if (flags & Hide::PastClipZ)
			strcat_s(text, sizeof(text), "Z ");
		return text;
	}

} // namespace ffx
