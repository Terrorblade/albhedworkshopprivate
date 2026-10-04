#include "workshop/OverlayWidgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace workshop
{

	namespace
	{

		char Lower(char c)
		{
			return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
		}

		// Case insensitive substring. Written out rather than using _stricmp so
		// there is no locale in the picture.
		bool Contains(const char* haystack, const char* needle)
		{
			if (!needle || !needle[0])
				return true;
			if (!haystack)
				return false;

			for (const char* start = haystack; *start; ++start)
			{
				const char* h = start;
				const char* n = needle;
				while (*h && *n && Lower(*h) == Lower(*n))
				{
					++h;
					++n;
				}
				if (!*n)
					return true;
			}
			return false;
		}

		bool AllDigits(const char* s)
		{
			if (!s || !s[0])
				return false;
			for (const char* p = s; *p; ++p)
				if (*p < '0' || *p > '9')
					return false;
			return true;
		}

	} // namespace

	bool PickerMatches(const PickerItem& item, const char* filter)
	{
		if (!filter || !filter[0])
			return true;

		// A pure number filters on the id as well as the label, so someone who
		// knows the id can reach it without scrolling. Substring on the decimal
		// id, not equality, so "90" finds 90, 190 and 900.
		if (AllDigits(filter))
		{
			char idText[16];
			_snprintf(idText, sizeof(idText) - 1, "%d", item.id);
			idText[sizeof(idText) - 1] = 0;
			if (Contains(idText, filter))
				return true;
		}

		return Contains(item.label, filter);
	}

	bool Picker(const char* label, const PickerItem* items, int count, PickerState* state,
	    int* outId)
	{
		if (!state)
			return false;

		if (!items || count <= 0)
		{
			ImGui::TextDisabled("%s: no entries", label);
			return false;
		}

		if (state->selected >= count)
			state->selected = -1;

		const char* preview = state->selected >= 0 ? items[state->selected].label
		                                           : "(nothing chosen)";

		bool changed = false;
		if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge))
		{
			// Focus the filter the first frame the popup is open, so typing just
			// works without a click.
			if (ImGui::IsWindowAppearing())
			{
				state->focusFilter = true;
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##filter", "type to filter", state->filter,
			    (int)sizeof(state->filter));

			// Collect the matches once, so the clipper can index them directly.
			// A fixed cap keeps this allocation free, and anything past it is
			// unreachable by scrolling anyway, so the hint says to filter.
			const int MaxShown = 4096;
			static int shown[MaxShown];
			int shownCount = 0;
			for (int i = 0; i < count && shownCount < MaxShown; ++i)
				if (PickerMatches(items[i], state->filter))
					shown[shownCount++] = i;

			if (shownCount == 0)
			{
				ImGui::TextDisabled("nothing matches");
			}
			else
			{
				ImGui::Text("%d of %d", shownCount, count);
				if (shownCount >= MaxShown)
					ImGui::TextDisabled("list truncated, narrow the filter");

				ImGui::Separator();
				if (ImGui::BeginChild("##list", ImVec2(0.0f, 0.0f)))
				{
					ImGuiListClipper clipper;
					clipper.Begin(shownCount);
					while (clipper.Step())
					{
						for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
						{
							const int index = shown[row];
							ImGui::PushID(index);
							const bool isSelected = index == state->selected;
							char text[160];
							_snprintf(text, sizeof(text) - 1, "%d  %s", items[index].id,
							    items[index].label ? items[index].label : "");
							text[sizeof(text) - 1] = 0;
							if (ImGui::Selectable(text, isSelected))
							{
								state->selected = index;
								changed = true;
								if (outId)
									*outId = items[index].id;
								ImGui::CloseCurrentPopup();
							}
							ImGui::PopID();
						}
					}
					clipper.End();
				}
				ImGui::EndChild();
			}
			ImGui::EndCombo();
		}

		if (!changed && outId && state->selected >= 0)
			*outId = items[state->selected].id;
		return changed;
	}

	bool PickerById(const char* label, const PickerItem* items, int count, PickerState* state,
	    int* ioId)
	{
		if (!state || !ioId)
			return false;

		// Re-find the id every frame, because the list can be rebuilt from game
		// data underneath us and an index would then point at the wrong row.
		state->selected = -1;
		if (items)
			for (int i = 0; i < count; ++i)
				if (items[i].id == *ioId)
				{
					state->selected = i;
					break;
				}

		int chosen = *ioId;
		if (!Picker(label, items, count, state, &chosen))
			return false;

		*ioId = chosen;
		return true;
	}

	bool IntRow(const char* label, int* value, int low, int high, int step)
	{
		if (!value)
		{
			ImGui::TextDisabled("%s: unreadable", label);
			return false;
		}
		return ImGui::DragInt(label, value, (float)step, low, high);
	}

	bool WordRow(const char* label, unsigned short* value, int low, int high)
	{
		if (!value)
		{
			ImGui::TextDisabled("%s: unreadable", label);
			return false;
		}
		int wide = (int)*value;
		if (!ImGui::DragInt(label, &wide, 1.0f, low, high))
			return false;
		if (wide < 0)
			wide = 0;
		if (wide > 0xFFFF)
			wide = 0xFFFF;
		*value = (unsigned short)wide;
		return true;
	}

	bool ByteRow(const char* label, unsigned char* value, int low, int high)
	{
		if (!value)
		{
			ImGui::TextDisabled("%s: unreadable", label);
			return false;
		}
		int wide = (int)*value;
		if (!ImGui::DragInt(label, &wide, 1.0f, low, high))
			return false;
		if (wide < 0)
			wide = 0;
		if (wide > 0xFF)
			wide = 0xFF;
		*value = (unsigned char)wide;
		return true;
	}

	bool DwordRow(const char* label, unsigned int* value, int low, int high)
	{
		if (!value)
		{
			ImGui::TextDisabled("%s: unreadable", label);
			return false;
		}
		int wide = (int)*value;
		if (!ImGui::DragInt(label, &wide, 1.0f, low, high))
			return false;
		if (wide < 0)
			wide = 0;
		*value = (unsigned int)wide;
		return true;
	}

} // namespace workshop
