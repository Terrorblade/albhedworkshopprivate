#pragma once

#include <windows.h>

// Inline detours, as a small reusable piece rather than a one-off per hook.
// A plain 5-byte jmp with a trampoline, which is all this mod needs.
//
// THE RULES THIS FOLLOWS, because an inline hook that gets any of them wrong
// corrupts the process in ways that are painful to debug:
//
//  1. VERIFY THE BYTES FIRST. Every detour carries the exact prologue bytes it
//     expects and refuses to install if memory does not match. A silently wrong
//     patch on the wrong build is the worst outcome available here.
//  2. THE STOLEN BYTES MUST BE POSITION INDEPENDENT. They are relocated to the
//     trampoline unchanged, so a rel8 or rel32 branch among them would end up
//     pointing somewhere meaningless. Every target's stolen range is checked by
//     eye in IDA. No length disassembler is used, so the length is explicit.
//  3. STOLEN LENGTH >= 5. The jmp rel32 needs 5 bytes, and anything left over is
//     padded with 0x90 so a disassembler following the original still lands on
//     instruction boundaries.
//  4. INSTALL ONCE, GATE AT RUNTIME. Hooks go in at startup and stay in. A
//     toggle is a flag the hook body reads, never a patch and unpatch cycle,
//     which would be a race against the game thread for no benefit.

namespace workshop
{

	struct Detour
	{
		const char* name;
		DWORD rva;
		void* replacement;
		const BYTE* expectedBytes; // exactly what we are allowed to overwrite
		DWORD stolenLength;        // how many bytes get relocated, 5 or more
		void* trampoline;          // call this to run the original
		bool installed;
	};

	bool InstallDetour(Detour& detour, const char* name, DWORD rva, void* replacement,
	    const BYTE* expectedBytes, DWORD stolenLength);

// ---------------------------------------------------------------------------
// One detour, declared in one place.
//
// The three facts that must agree are the target address, the prologue bytes and
// the hook body. Keeping them in these macros means they are written together
// and cannot drift apart in separate edits.
//
//   DETOUR_DECLARE(UpdateCameLen, int, (Character *chr));
//   DETOUR_PROLOGUE(UpdateCameLen) = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28 };
//
//   static int __cdecl UpdateCameLenHook(Character *chr) {
//       int result = DETOUR_ORIGINAL(UpdateCameLen)(chr);
//       ...
//   }
//
//   DETOUR_INSTALL(UpdateCameLen, Rva::ChUpdateCameLenAndZClip);
// ---------------------------------------------------------------------------

// Declares the trampoline type, the hook body and the detour record.
#define DETOUR_DECLARE(Name, ReturnType, ParamList)          \
	typedef ReturnType(__cdecl* Name##OriginalFn) ParamList; \
	static ReturnType __cdecl Name##Hook ParamList;          \
	static ::workshop::Detour Name##Detour

// Declares the expected prologue array. Assign the bytes to it.
#define DETOUR_PROLOGUE(Name) static const BYTE Name##Prologue[]

// Installs, verifying the prologue first. Evaluates to a bool.
#define DETOUR_INSTALL(Name, targetRva)                         \
	::workshop::InstallDetour(Name##Detour, #Name, (targetRva), \
	    (void*)&Name##Hook, Name##Prologue,                     \
	    (DWORD)sizeof(Name##Prologue))

// Calls through to the original.
#define DETOUR_ORIGINAL(Name) ((Name##OriginalFn)Name##Detour.trampoline)

#define DETOUR_INSTALLED(Name) (Name##Detour.installed)

	// ---------------------------------------------------------------------------
	// PATCHING ONE CALL SITE, rather than detouring the function it calls.
	//
	// Use this instead of a detour when you want to intercept SOME callers of a
	// function and not all of them, or when the target has no five position
	// independent bytes at its entry for a detour to steal. Both come up in this
	// binary more often than you would expect: several functions start with a rel32
	// call, and relocating a stolen call leaves it pointing nowhere.
	//
	// It is also strictly more precise. One dword is written, nothing is relocated,
	// and the target function is never modified, so it stays callable by address and
	// its other callers are untouched.
	//
	// IT VERIFIES BEFORE IT WRITES. The byte at the site must be 0xE8 and the
	// displacement must resolve to expectedTargetRva, or it refuses and logs. On a
	// build that moved, that is a clean refusal with an explanation rather than a jump
	// into the middle of something.
	// ---------------------------------------------------------------------------
	struct CallSitePatch
	{
		DWORD rva;
		bool patched;
	};

	// what is used in the log lines, so make it a short phrase like "menu open" or
	// "player driver". Returns false and writes nothing on any mismatch.
	bool PatchCallSite(CallSitePatch& site, DWORD siteRva, DWORD expectedTargetRva,
	    void* replacement, const char* what);

} // namespace workshop
