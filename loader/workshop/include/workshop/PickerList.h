#pragma once

// A Picker's options plus the storage their labels point into.
//
// Picker takes a PickerItem array whose labels have to outlive the call, and a
// list read out of game data has nowhere to put them. This is that somewhere.
//
//     static workshop::PickerList maps;
//
//     maps.Reset("map name table");
//     for (int i = 0; i < count; ++i)
//         maps.Add(row[i].id, "%s", row[i].name);
//     maps.SetLive(true);
//
//     workshop::Picker("map", maps.Items(), maps.Count(), &state, &mapId);
//
// No allocation: one fixed item array and one fixed string arena, so a plugin can
// fill one from DllMain or from the render thread. Adds past the cap are dropped
// and Overflowed says so.
//
// Costs about 168 KB each, so hold one per list that stays on screen and share a
// scratch one for anything built and thrown away.

#include "workshop/OverlayWidgets.h"

namespace workshop
{

	class PickerList
	{
	public:
		static const int MaxItems = 4096;
		static const int ArenaBytes = 128 * 1024;

		PickerList();

		// Empties it. source is kept by pointer, not copied, so point it at a
		// literal. It is there for a panel to show where the options came from.
		void Reset(const char* source = nullptr);

		// Copies the label in. Returns false when either the item array or the
		// arena is full.
		bool Add(int id, const char* label);
		bool AddFormatted(int id, const char* format, ...);

		// The name the engine gave it, when that is a fixed width field rather
		// than a C string. Stops at the first zero or at length, whichever comes
		// first, and refuses a row whose bytes are not printable ASCII, which is
		// how an unloaded or garbage table row gets dropped instead of drawn.
		bool AddFixedName(int id, const char* name, int length);

		const PickerItem* Items() const { return items; }
		int Count() const { return count; }
		bool Empty() const { return count == 0; }
		bool Overflowed() const { return overflowed; }
		const char* Source() const { return source; }

		// Whether this came from the game or is a hand written fallback. The cheat
		// panel draws an unlive list differently, because an option that is really
		// a guess should look like one.
		void SetLive(bool isLive) { live = isLive; }
		bool Live() const { return live; }

		// One line for a panel or the log: "map name table, 412 options, live".
		const char* Describe() const;

	private:
		PickerItem items[MaxItems];
		char arena[ArenaBytes];
		int count;
		int arenaUsed;
		bool overflowed;
		bool live;
		const char* source;
		mutable char description[96];
	};

	// True when every byte up to the first zero is printable ASCII and there is at
	// least one. What AddFixedName screens on, exposed because a caller reading a
	// table usually wants to skip a bad row entirely rather than just its label.
	bool PrintableName(const char* name, int length);

} // namespace workshop
