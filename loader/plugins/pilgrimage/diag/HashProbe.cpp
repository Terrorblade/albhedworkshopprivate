#include "diag/HashProbe.h"

#include <stdio.h>
#include <string.h>

#include "ffx/GameState.h"
#include "workshop/Log.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		// The bucket names, in the same order as ffx::GameStateBucket. Kept next to the
		// only code that prints them, so a report is readable without the header open.
		// NO EXPLICIT SIZE. With [kBucketCount] here the count check below compares
		// kBucketCount against itself and passes while the missing entries are quietly
		// null, which is a null pointer handed to a %s. Leaving the size off makes the
		// array as big as this list and the check mean something.
		//
		// The first two used to read "mapId" and "playtime", which was wrong and sent a
		// reader to the wrong bytes: index 0 is the live location block and index 4 is
		// the CHECKPOINT at 0xB8, not playtime. Playtime is in no bucket at all.
		const char* const BucketNames[] = {
			"liveLocation",
			"headerFlags",
			"scriptParams",
			"mapEntryPair",
			"checkpoint",
			"progress",
			"partyGilFlags",
			"inventory",
			"itemMasks",
			"monsters",
			"keyItems",
			"equipment",
			"characters",
			"abilities",
			"names",
			"tail",
		};

		typedef char BucketNameCountCheck[(sizeof(BucketNames) / sizeof(BucketNames[0]) ==
		                                      (size_t)kBucketCount)
		                                      ? 1
		                                      : -1];

		bool running = false;
		int framesWanted = 0;
		int framesSeen = 0;
		GameStateHash firstHash;
		DWORD lastBucket[kBucketCount];
		long changeCount[kBucketCount];
		char status[64] = "hash probe: idle";

		void Report()
		{
			Log("=== save block stability over %d frames ===", framesSeen);
			Log("A bucket that changed while the game sat still either does not belong in "
			    "a desync detector's combined hash, or is worth understanding first.");

			int quiet = 0, noisy = 0;
			for (int i = 0; i < kBucketCount; ++i)
			{
				if (changeCount[i] == 0)
				{
					++quiet;
					continue;
				}
				++noisy;
				Log("  MOVED  %-11s %ld change%s in %d frames  (%08lX -> %08lX)",
				    BucketNames[i], changeCount[i], changeCount[i] == 1 ? "" : "s",
				    framesSeen, firstHash.bucket[i], lastBucket[i]);
			}
			for (int i = 0; i < kBucketCount; ++i)
				if (changeCount[i] == 0)
					Log("  still  %-11s %08lX", BucketNames[i], lastBucket[i]);

			Log("%d bucket%s sat still, %d moved. Expect playtime to move, and the map id "
			    "only if you walked through a transition.",
			    quiet, quiet == 1 ? "" : "s", noisy);

			_snprintf_s(status, sizeof(status), _TRUNCATE, "hash probe: done, %d moved", noisy);
		}

	} // namespace

	void BeginHashProbe(LONG frame, int frames)
	{
		GameStateHash hash;
		if (!HashGameState(&hash) || !hash.valid)
		{
			Log("hash probe: no save block to read yet, so load a game first");
			strcpy_s(status, sizeof(status), "hash probe: no game loaded");
			return;
		}

		running = true;
		framesWanted = (frames > 0) ? frames : 300;
		framesSeen = 0;
		firstHash = hash;
		memset(changeCount, 0, sizeof(changeCount));
		for (int i = 0; i < kBucketCount; ++i)
			lastBucket[i] = hash.bucket[i];

		Log("hash probe: watching %d buckets for %d frames from frame %ld. Stand still "
		    "and do nothing for the best reading.",
		    (int)kBucketCount, framesWanted, frame);
		_snprintf_s(status, sizeof(status), _TRUNCATE, "hash probe: 0/%d", framesWanted);
	}

	void StepHashProbe(LONG)
	{
		if (!running)
			return;

		GameStateHash hash;
		if (!HashGameState(&hash) || !hash.valid)
		{
			// Losing the save block mid-run means a load or a teardown, and the
			// reading would be meaningless. Stop rather than report nonsense.
			Log("hash probe: the save block went away after %d frames, abandoning the run",
			    framesSeen);
			running = false;
			strcpy_s(status, sizeof(status), "hash probe: abandoned");
			return;
		}

		for (int i = 0; i < kBucketCount; ++i)
		{
			if (hash.bucket[i] != lastBucket[i])
			{
				++changeCount[i];
				lastBucket[i] = hash.bucket[i];
			}
		}

		++framesSeen;
		_snprintf_s(status, sizeof(status), _TRUNCATE, "hash probe: %d/%d",
		    framesSeen, framesWanted);

		if (framesSeen >= framesWanted)
		{
			running = false;
			Report();
		}
	}

	bool HashProbeRunning()
	{
		return running;
	}
	const char* HashProbeStatus()
	{
		return status;
	}

} // namespace pilgrimage
