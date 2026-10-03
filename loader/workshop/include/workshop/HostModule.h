#pragma once

#include <windows.h>

// The host image, and the safety rails for touching it. Nothing here knows
// anything about FFX. The FFX-specific checks live in ffx/VerifyLayout.h.
//
// THE HOST IS ASLR-RELOCATED, SO NEVER HARDCODE A VA. Addresses are stored as
// RVAs (for FFX.exe, the IDA VA minus 0x00400000) and resolved through
// ModuleAddress against GetModuleHandleW(NULL). ASLR on FFX.exe is per-boot
// rather than per-launch, so a hardcoded VA appears to work perfectly until the
// machine reboots.

namespace workshop
{

	// Records the host base. Call once, first thing in DllMain.
	void BindHostModule();

	BYTE* ModuleBase();

	// 0 until ReadPeHeaders has run.
	DWORD ModuleImageSize();

	// RVA to a live address.
	inline void* ModuleAddress(DWORD rva)
	{
		return ModuleBase() + rva;
	}

	// Validates the PE headers and records SizeOfImage. Must run before InsideImage
	// or any RVA bounds check means anything.
	//
	// NEVER validate OptionalHeader.ImageBase against the on-disk preferred base. The
	// loader rewrites that field in memory to the relocated base, so at runtime it
	// equals GetModuleHandle(NULL). An earlier plugin did this and rejected every
	// launch.
	bool ReadPeHeaders();

	// Is this pointer inside the host image? Used when following a jmp thunk, so a
	// corrupt displacement cannot send a guard off reading unmapped memory.
	bool InsideImage(const void* p);

	// Is this RVA inside the host image?
	bool RvaInImage(DWORD rva);

	// Does this dword look like a usermode pointer rather than a small integer?
	bool LooksLikePointer(DWORD value);

	// Never dereference a host pointer that has not been through this. Engine
	// allocations can be stale or half built, and a fault on the frame path takes the
	// whole game down.
	bool Readable(const void* p, SIZE_T bytes);

	// Does this RVA look like the start of a callable x86 function?
	//
	// A JMP THUNK IS A LEGITIMATE ENTRY POINT. An earlier version of this check did
	// not accept one and rejected every launch: FFX_Ch_MotionSetReadStart 0x836A40 is
	// a single jmp to FFX_Ch_RomReadMotionSet, so its first byte is E9 and the check
	// failed on a function that was perfectly fine to call. MSVC emits these for
	// COMDAT folding and incremental linking and they are stable call targets.
	//
	// So E9 and EB are accepted, but the jump is FOLLOWED and the destination checked
	// instead, which keeps the guard meaningful rather than just looser. Only one
	// level of indirection is followed, which is all MSVC ever emits here.
	bool LooksLikeFunctionStart(DWORD rva);

	// True when the RVA holds a jmp rel8 or rel32. Fills outTarget with where it
	// goes, when outTarget is not null.
	bool IsJumpThunk(DWORD rva, const BYTE** outTarget);

	// Overwrites one dword in a read-only section, restoring the protection after.
	// This is how a vtable slot gets patched. Returns the previous contents through
	// outPrevious.
	bool WriteProtectedDword(void* address, DWORD value, DWORD* outPrevious);

} // namespace workshop
