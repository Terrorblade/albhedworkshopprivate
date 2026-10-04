#include "ffx/MagicDll.h"

#include "ffx/addresses/MagicDll.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

namespace ffx
{

	using namespace workshop;

	namespace
	{

		// Sixteen is far more than anything should need, and a fixed array keeps this
		// free of allocation so a plugin can use it from DllMain.
		const int MaxRemembered = 16;

		struct Remembered
		{
			int index;
			void* original;
		};

		Remembered g_remembered[MaxRemembered];
		int g_rememberedCount = 0;

		DWORD* Table()
		{
			DWORD* table = (DWORD*)ModuleAddress(Rva::MagicHostApiTable);
			if (!Readable(table, 4 * Rva::MagicHostApiSlots))
				return NULL;
			return table;
		}

		Remembered* Find(int index)
		{
			for (int i = 0; i < g_rememberedCount; ++i)
				if (g_remembered[i].index == index)
					return &g_remembered[i];
			return NULL;
		}

	} // namespace

	bool MagicHostApiReady()
	{
		DWORD* table = Table();
		if (!table)
			return false;

		// Slot 0 should be a function inside the image. A table that has been moved or a
		// wrong address reads back as something that is not.
		return RvaInImage(table[0] - (DWORD)(UINT_PTR)ModuleBase());
	}

	int MagicHostApiSlotCount()
	{
		return (int)Rva::MagicHostApiSlots;
	}

	void* MagicHostApiSlot(int index)
	{
		if (index < 0 || index >= (int)Rva::MagicHostApiSlots)
			return NULL;

		DWORD* table = Table();
		if (!table)
			return NULL;
		return (void*)(UINT_PTR)table[index];
	}

	bool SetMagicHostApiSlot(int index, void* replacement, void** outPrevious)
	{
		if (index < 0 || index >= (int)Rva::MagicHostApiSlots)
			return false;

		DWORD* table = Table();
		if (!table)
		{
			Log("magic dll: the host API table is not readable, so slot %d was left alone",
			    index);
			return false;
		}

		DWORD previous = 0;
		if (!WriteProtectedDword(&table[index], (DWORD)(UINT_PTR)replacement, &previous))
		{
			Log("magic dll: could not write host API slot %d", index);
			return false;
		}

		if (outPrevious)
			*outPrevious = (void*)(UINT_PTR)previous;

		// Only the FIRST replacement is remembered, so Restore puts back the shipped
		// pointer rather than another plugin's hook.
		if (!Find(index))
		{
			if (g_rememberedCount < MaxRemembered)
			{
				g_remembered[g_rememberedCount].index = index;
				g_remembered[g_rememberedCount].original = (void*)(UINT_PTR)previous;
				++g_rememberedCount;
			}
			else
			{
				Log("magic dll: slot %d replaced but there is no room left to remember "
				    "its original, so it cannot be restored",
				    index);
			}
		}

		Log("magic dll: host API slot %d  0x%08X -> 0x%08X", index, previous,
		    (unsigned)(UINT_PTR)replacement);
		return true;
	}

	bool RestoreMagicHostApiSlot(int index)
	{
		Remembered* held = Find(index);
		if (!held)
			return false;

		DWORD* table = Table();
		if (!table)
			return false;

		DWORD previous = 0;
		if (!WriteProtectedDword(&table[index], (DWORD)(UINT_PTR)held->original, &previous))
			return false;

		Log("magic dll: host API slot %d restored to 0x%08X", index,
		    (unsigned)(UINT_PTR)held->original);

		// Compact, so a second Restore on the same slot reports false rather than
		// writing the same pointer again.
		*held = g_remembered[g_rememberedCount - 1];
		--g_rememberedCount;
		return true;
	}

	void RestoreAllMagicHostApiSlots()
	{
		while (g_rememberedCount > 0)
			if (!RestoreMagicHostApiSlot(g_remembered[0].index))
				break;
	}

	int MagicHostApiSlotsReplaced()
	{
		return g_rememberedCount;
	}

	void LogMagicDll()
	{
		Log("magicdll: table %s, %d of %d slots replaced",
		    MagicHostApiReady() ? "ready" : "NOT READABLE", g_rememberedCount,
		    (int)Rva::MagicHostApiSlots);
		for (int i = 0; i < g_rememberedCount; ++i)
			Log("magicdll: slot %-4d original 0x%08X, now 0x%08X",
			    g_remembered[i].index, (unsigned)(UINT_PTR)g_remembered[i].original,
			    (unsigned)(UINT_PTR)MagicHostApiSlot(g_remembered[i].index));
	}

} // namespace ffx
