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
//  5. CHAIN BEHIND ANOTHER PLUGIN, DO NOT REFUSE. See below. This one was
//     learned the hard way.
//
// ---------------------------------------------------------------------------
// TWO PLUGINS, ONE HOOK POINT. THIS IS THE NORMAL CASE, NOT AN ERROR.
//
// Every plugin statically links this library, so every plugin has its own copy
// of every hook in it. Two plugins that both want the step event both try to
// patch FFX_MainStep, and whichever loads first wins. Plugin order is the scan
// order, which is alphabetical, so it is not even a race: the same plugin wins
// every time.
//
// This USED TO REFUSE, and the cost was not a missing feature, it was a silent
// one. Measured 2026-10-06 with AlBhedCheats and PilgrimageTogether installed
// together: Cheats loaded first and got FFX_MainStep and the sub step call site,
// and Pilgrimage logged a prologue mismatch, a refused call site, and then
//
//     hub: running, frame but no step events, sub step unavailable
//     lockstep: gate added to the hub's step chain, gate is measuring only
//
// which is the entire co-op simulation layer registered on a chain that is never
// called. No step gate, no sub step event, no player drive. The symptoms were a
// remote player with no body, a pause that did not sync, and a desync, none of
// which look like a hook collision from the outside.
//
// The vtable hook got this right from the start, because a vtable slot chains
// naturally: you save what was in the slot and call it. Both functions below now
// do the same thing:
//
//  - A target byte pattern that does not match, where byte 0 is 0xE9 and the
//    jump lands OUTSIDE the game's image, is another plugin's detour. Chain to
//    it. The trampoline becomes a jmp to their hook, so ours runs in front and
//    theirs still reaches the real function.
//  - A call site that points somewhere other than the expected function, but
//    outside the game's image, is another plugin's call site patch. Same thing.
//  - A mismatch that points INSIDE the game's image is still a hard refusal,
//    because that means these addresses are for a different build, and that is
//    the thing the checks exist to catch.
//
// WHAT TO CALL TO RUN THE ORIGINAL. `detour.trampoline` and `site.previous`,
// never ModuleAddress of the target. With a chain they differ, and reaching for
// the real function by address skips every plugin that got there first.
//
// ONE SEMANTIC CONSEQUENCE WORTH KNOWING. A chained gate-style hook that decides
// NOT to call through holds back every plugin behind it, because they are behind
// it. For the lockstep gate that is right: a step that is held should be held for
// everybody. For a hook that merely observes, always call through.
// ---------------------------------------------------------------------------

namespace workshop
{

	struct Detour
	{
		const char* name;
		DWORD rva;
		void* replacement;
		const BYTE* expectedBytes; // exactly what we are allowed to overwrite
		DWORD stolenLength;        // how many bytes get relocated, 5 or more

		// CALL THIS TO RUN THE ORIGINAL, always. With a plain detour it is the
		// stolen bytes plus a jump back. With a chain it is a jump to the plugin
		// that got here first, and the real function is further down.
		void* trampoline;

		bool installed;

		// True when we went in behind another plugin rather than onto clean bytes.
		// Nothing needs to behave differently, it is for the log and the status line.
		bool chained;
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

		// What the site called before we patched it. USE THIS to run the original
		// rather than resolving expectedTargetRva, because when another plugin
		// patched the site first this is their hook and that is not.
		void* previous;

		// True when `previous` is another plugin rather than the game.
		bool chained;
	};

	// what is used in the log lines, so make it a short phrase like "menu open" or
	// "player driver". Returns false and writes nothing on any mismatch.
	bool PatchCallSite(CallSitePatch& site, DWORD siteRva, DWORD expectedTargetRva,
	    void* replacement, const char* what);

} // namespace workshop
