#pragma once

// The engine's cull-disable flag, as a test switch driven by settings.disableCull.
//
// The flag itself, and the warning about what it cannot prove, live in
// ffx/GfxContext.h. This file is only the per-frame apply and restore policy.

namespace pilgrimage
{

	// Call once per frame from the game thread.
	void ApplyCullOverride();

} // namespace pilgrimage
