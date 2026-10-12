#include "workshop/PickerList.h"

#include <intrin.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace workshop
{

	bool PrintableName(const char* name, int length)
	{
		if (!name || length <= 0)
			return false;

		int seen = 0;
		for (int i = 0; i < length; ++i)
		{
			const unsigned char c = (unsigned char)name[i];
			if (c == 0)
				break;
			if (c < 0x20 || c > 0x7E)
				return false;
			++seen;
		}
		return seen > 0;
	}

	PickerList::PickerList()
	    : count(0),
	      arenaUsed(0),
	      overflowed(false),
	      live(false),
	      source(nullptr)
	{
		items[0].id = 0;
		items[0].label = nullptr;
		arena[0] = 0;
		description[0] = 0;
	}

	// ---------------------------------------------------------------------------
	// A LIST IS FILLED ON ONE THREAD AND READ ON ANOTHER, so the order these fields
	// are published in matters.
	//
	// The save derived lists are rebuilt from the game's step thread, because the
	// rebuild calls engine code and that is the only thread it is safe on, while the
	// panel reads them from whichever thread calls Present. Those are genuinely
	// different threads in FFX: one run measured the step on 31392 and Present on
	// 20640.
	//
	// count is therefore the gate. A reader walks items[0..count), so as long as an
	// entry is complete before count admits it, and as long as count drops to zero
	// before the arena behind it is reused, the worst a reader can see is a list that
	// is shorter than it will be a frame later. Get the order wrong and it sees an
	// item whose label points at bytes being overwritten.
	//
	// _WriteBarrier is a compiler barrier and nothing more, which is all that is
	// needed: x86 does not reorder one store ahead of another, so the only risk was
	// the optimiser hoisting the count update. It costs nothing at runtime, which
	// matters because the model list runs this 892 times.
	// ---------------------------------------------------------------------------
	void PickerList::Reset(const char* newSource)
	{
		// FIRST, AND BEHIND A BARRIER, so a reader has stopped walking items before
		// the arena those items point into is handed out again.
		count = 0;
		_WriteBarrier();

		arenaUsed = 0;
		overflowed = false;
		live = false;
		source = newSource;
		description[0] = 0;
	}

	bool PickerList::Add(int id, const char* label)
	{
		if (!label)
			label = "";

		if (count >= MaxItems)
		{
			overflowed = true;
			return false;
		}

		const int bytes = (int)strlen(label) + 1;
		if (arenaUsed + bytes > ArenaBytes)
		{
			overflowed = true;
			return false;
		}

		char* stored = &arena[arenaUsed];
		memcpy(stored, label, (size_t)bytes);
		arenaUsed += bytes;

		items[count].id = id;
		items[count].label = stored;

		// LAST, so the entry is whole before count says it exists.
		_WriteBarrier();
		++count;
		return true;
	}

	bool PickerList::AddFormatted(int id, const char* format, ...)
	{
		// One pass into a stack buffer, then the normal Add. A label longer than
		// this is unreadable in a combo anyway, so truncating is the right answer.
		char text[192];
		va_list args;
		va_start(args, format);
		_vsnprintf(text, sizeof(text) - 1, format, args);
		va_end(args);
		text[sizeof(text) - 1] = 0;

		return Add(id, text);
	}

	bool PickerList::AddFixedName(int id, const char* name, int length)
	{
		if (!PrintableName(name, length))
			return false;

		char text[64];
		int copy = length;
		if (copy > (int)sizeof(text) - 1)
			copy = (int)sizeof(text) - 1;

		int i = 0;
		for (; i < copy && name[i]; ++i)
			text[i] = name[i];
		text[i] = 0;

		return Add(id, text);
	}

	const char* PickerList::Describe() const
	{
		_snprintf(description, sizeof(description) - 1, "%s, %d option%s, %s%s",
		    source ? source : "unnamed", count, count == 1 ? "" : "s",
		    live ? "live" : "NOT FROM THE GAME", overflowed ? ", TRUNCATED" : "");
		description[sizeof(description) - 1] = 0;
		return description;
	}

} // namespace workshop
