#pragma once

#include <windows.h>

// Reach the game's own plugin surface: the 947 slot function pointer table FFX.exe hands
// to every magic DLL.
//
// Replacing one slot intercepts what all 581 shipped DLLs do through it, so this is the
// cheap way to see or change ability behaviour without touching a single DLL file.
//
//     void* previous = nullptr;
//     ffx::SetMagicHostApiSlot(ffx::Rva::MagicSlotPadReadPressed16, &MyReadPressed, &previous);
//
// Your replacement gets the DLL's arguments and must match the original's calling
// convention. Call the previous pointer for the real behaviour.
//
// The table is in .data and read-write, and it is populated in the file rather than at
// runtime, so a slot is readable and writable from the moment the process starts.

namespace ffx
{

	// False before the module is resolvable, or if the table does not read back as
	// plausible function pointers.
	bool MagicHostApiReady();

	// 947. Slot indices worth naming are in addresses/MagicDll.h.
	int MagicHostApiSlotCount();

	// Null for an out of range index or an unreadable table.
	void* MagicHostApiSlot(int index);

	// Remembers the original the first time a given slot is replaced, so Restore works
	// even after several replacements. outPrevious may be null.
	bool SetMagicHostApiSlot(int index, void* replacement, void** outPrevious);

	// Puts back what was there before the FIRST SetMagicHostApiSlot on this slot.
	bool RestoreMagicHostApiSlot(int index);
	void RestoreAllMagicHostApiSlots();

	int MagicHostApiSlotsReplaced();
	void LogMagicDll();

} // namespace ffx
