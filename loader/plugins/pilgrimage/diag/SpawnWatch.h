#pragma once

#include <windows.h>

#include "ffx/Api.h"

// Watches a freshly spawned clone's render fields for a few seconds.
//
// The question this was built to answer is whether the model instance fills in
// late (a deferred attach that eventually drains) or never (an attach that
// silently did nothing). It stays because it is a free record of whether a spawn
// went cleanly, and because it dumps a full field diff by itself if something
// goes wrong, so nobody has to notice and press a key.

namespace pilgrimage
{

	// Start watching. Called by the spawner right after a successful spawn.
	void BeginSpawnWatch(LONG currentFrame);

	// Call once per frame with the clone being watched. Does nothing outside the
	// watch window.
	void StepSpawnWatch(LONG currentFrame, ffx::Character* clone);

} // namespace pilgrimage
