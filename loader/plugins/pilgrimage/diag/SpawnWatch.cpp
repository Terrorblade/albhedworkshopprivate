#include "diag/SpawnWatch.h"

#include <stdio.h>

#include "workshop/Log.h"
#include "diag/CharacterDump.h"
#include "ffx/RenderProbe.h"
#include "ffx/Character.h"

namespace pilgrimage
{

	// The game library. Anything unqualified below that looks like FFX
	// knowledge, or like mod plumbing, comes from one of these two.
	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// How long the watch runs, in frames. A deferred model attach should resolve
		// inside one or two, so anything still wrong at the end of this is really wrong.
		const LONG WatchFrames = 150;

		volatile LONG watchEndsAtFrame = 0;
		volatile LONG dumpDone = 1;

		// Log the first ten frames then thin out, because the interesting window is the
		// first handful and after that only the settled state matters.
		bool ShouldLogAt(LONG age)
		{
			return age <= 10 || age == 20 || age == 30 || age == 60 || age == 90 ||
			       age == WatchFrames;
		}

	} // namespace

	void BeginSpawnWatch(LONG currentFrame)
	{
		InterlockedExchange(&watchEndsAtFrame, currentFrame + WatchFrames);
		InterlockedExchange(&dumpDone, 0);
	}

	void StepSpawnWatch(LONG currentFrame, Character* clone)
	{
		if (!clone || currentFrame > watchEndsAtFrame)
			return;

		const LONG age = currentFrame - (watchEndsAtFrame - WatchFrames);
		if (ShouldLogAt(age))
		{
			char tag[24];
			_snprintf_s(tag, sizeof(tag), _TRUNCATE, "f+%ld", age);
			LogCharacterDiag(tag, clone);
		}

		// At the end of the window, dump whatever state it has settled into. Done
		// unconditionally rather than guessing which failure case applies.
		if (!dumpDone && currentFrame == watchEndsAtFrame)
		{
			InterlockedExchange(&dumpDone, 1);
			Log("AUTO DUMP at f+%ld. draw gate: %s", WatchFrames, FirstFailingDrawGate(clone));
			DumpCharacterDiff(clone, LivePlayerCharacter());
		}
	}

} // namespace pilgrimage
