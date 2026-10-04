#pragma once

// ImGui widgets the kit provides so plugins do not each write them.
//
// The one that matters is Picker: a combo whose options come from a list, with a
// filter box inside it. Built for lists that are too long to scroll, which is
// most things read out of game data.
//
//     static workshop::PickerState models;
//     int chrId = 0;
//     if (workshop::Picker("model", items, count, &models, &chrId))
//         ApplyModel(chrId);
//
// The filter matches the label case-insensitively AND matches the id typed as a
// number, so "1234" finds id 1234 and "tidus" finds Tidus. Long lists are drawn
// through ImGuiListClipper, so a 10,000 entry list costs the same as a short one.

#include "imgui.h"

namespace workshop
{

	// One option. label must outlive the call, so point it at stable storage
	// rather than a stack buffer.
	struct PickerItem
	{
		int id;
		const char* label;
	};

	struct PickerState
	{
		char filter[64];
		int selected; // index into the item array, -1 for nothing chosen yet
		bool focusFilter;

		PickerState()
		    : selected(-1),
		      focusFilter(false)
		{
			filter[0] = 0;
		}
	};

	// Returns true on the frame the selection changes, and writes the chosen id
	// to outId. outId may be null if you only want the index in state->selected.
	bool Picker(const char* label, const PickerItem* items, int count, PickerState* state,
	    int* outId = nullptr);

	// Picks by id rather than by index, for when the caller holds an id and the
	// list may have been rebuilt underneath it. Returns true when it changes.
	bool PickerById(const char* label, const PickerItem* items, int count, PickerState* state,
	    int* ioId);

	// True when the item passes the state's filter. Exposed because a caller
	// sometimes wants to drive its own list with the same matching rule.
	bool PickerMatches(const PickerItem& item, const char* filter);

	// A labelled row of value plus a drag control, clamped. Returns true when the
	// value changes. Just less boilerplate than the ImGui call.
	bool IntRow(const char* label, int* value, int low, int high, int step = 1);

	// Same, for the common "edit this engine word" case. Reads through a pointer
	// that may be null, in which case it draws the label greyed out and returns
	// false, so a caller does not have to guard every row.
	bool WordRow(const char* label, unsigned short* value, int low, int high);
	bool ByteRow(const char* label, unsigned char* value, int low, int high);
	bool DwordRow(const char* label, unsigned int* value, int low, int high);

} // namespace workshop
