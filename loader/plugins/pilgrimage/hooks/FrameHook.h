#pragma once

// The clone mod's per-frame work, hung off FFXApplication::animate.
//
// The hook point itself, its calling convention and the reason a non-zero return
// kills the game are all documented in ffx/AnimateHook.h. This file is just the
// body: poll the hotkeys, drain the requests, service the clones, drive the one
// with input focus, publish telemetry.

namespace pilgrimage
{

	bool InstallFrameHook();

} // namespace pilgrimage
