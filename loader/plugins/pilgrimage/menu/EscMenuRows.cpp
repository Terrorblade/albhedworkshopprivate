#include "menu/EscMenuRows.h"

#include <stdio.h>
#include <string.h>

#include "ffx/EscMenu.h"
#include "ffx/MainLoop.h"
#include "workshop/Log.h"

namespace pilgrimage
{

	using namespace ffx;
	using namespace workshop;

	namespace
	{

		bool armed = false;
		bool rowAdded = false;
		BYTE* seenMenu = NULL; // which singleton we added to, so a rebuild re-adds

		char status[180] = "esc menu row test: not armed";

		// Two choices, so the row is a working spinner rather than a dead label. If the
		// movie appends, this reads as a real setting the player can click left and right
		// on, which also tells us the widget is wired and not just drawn.
		// Not const-of-const, because MakeChoiceArray takes const char ** and the
		// engine's own array builder reads through it rather than copying.
		const char* Choices[] = { "Off", "On" };

		// Built once and kept. Nothing in the game frees an Iggy array ref, so rebuilding
		// it per attempt would leak one per press.
		DWORD choiceRef = 0;

		bool TryAddRow(const char* why)
		{
			BYTE* menu = EscMenuPtr();
			if (!menu)
			{
				_snprintf_s(status, sizeof(status), _TRUNCATE,
				    "esc menu row test: armed, waiting for the menu to exist");
				return false;
			}

			const int built = VideoRowsBuilt();
			if (built < 0)
			{
				strcpy_s(status, sizeof(status),
				    "esc menu row test: the menu exists but the row count would not read");
				return false;
			}

			if (choiceRef == 0)
			{
				choiceRef = MakeChoiceArray(Choices, 2);
				if (choiceRef == 0)
					Log("esc menu row test: could not build the choice array, so the row "
					    "goes in without one. A spinner with no choices may draw empty.");
			}

			// kFirstFreeVideoRowIndex is 12 rather than the built count on purpose: in
			// the 11-row case the game still used indices 1 to 11 and skipped 0, so the
			// count is not a free index.
			const bool ok = AddVideoPageRow(kFirstFreeVideoRowIndex, kWidgetSpinner,
			    "Pilgrimage Together", "Off", choiceRef, -1);

			Log("=== esc menu row test (%s) ===", why);
			Log("the game's own builder emitted %d rows, we asked for index %d",
			    built, kFirstFreeVideoRowIndex);
			Log("the AS3 call %s", ok ? "went through" : "FAILED, see above for why");

			if (!ok)
			{
				strcpy_s(status, sizeof(status), "esc menu row test: the AS3 call failed");
				return false;
			}

			Log("Now open the Esc menu, go to Video, and scroll to the bottom.");
			Log("A 13th row reading 'Pilgrimage Together' means the movie APPENDS, and the "
			    "mod's settings can live in the game's own menu.");
			Log("No 13th row means the movie fills fixed slots, and the fallback is the "
			    "Special Feature page or the mod's own window.");

			rowAdded = true;
			seenMenu = menu;
			_snprintf_s(status, sizeof(status), _TRUNCATE,
			    "esc menu row test: row %d added after %d, now LOOK at Video",
			    kFirstFreeVideoRowIndex, built);
			return true;
		}

	} // namespace

	void ArmEscMenuRowTest()
	{
		armed = true;
		rowAdded = false;
		seenMenu = NULL;

		if (EscMenuPtr())
		{
			// The menu has been opened before, so the singleton and its pages already
			// exist and there is nothing to wait for.
			TryAddRow("the menu already existed");
			return;
		}

		Log("esc menu row test: armed. The pages are built once when the menu "
		    "singleton is constructed, so open the Esc menu with Escape and the row "
		    "goes in as soon as that happens.");
		strcpy_s(status, sizeof(status),
		    "esc menu row test: armed, press Escape to build the menu");
	}

	void AddEscMenuRowNow()
	{
		armed = true;
		TryAddRow("asked for directly");
	}

	void ServiceEscMenuRowTest()
	{
		if (!armed)
			return;

		BYTE* menu = EscMenuPtr();
		if (!menu)
			return;

		// A different singleton than the one we added to means the menu was torn down
		// and rebuilt, which drops our row with it. Add it again.
		if (rowAdded && menu == seenMenu)
			return;

		TryAddRow(rowAdded ? "the menu was rebuilt" : "the menu appeared");
	}

	const char* EscMenuRowTestStatus()
	{
		return status;
	}

} // namespace pilgrimage
