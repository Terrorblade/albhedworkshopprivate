#include "workshop/PickerList.h"

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

	void PickerList::Reset(const char* newSource)
	{
		count = 0;
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
