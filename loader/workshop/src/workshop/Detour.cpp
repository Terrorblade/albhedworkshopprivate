#include "workshop/Detour.h"

#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <stdio.h>
#include <string.h>

namespace workshop
{

	namespace
	{

		// Who owns the code at this address, by file name, or NULL. Used only to make
		// a chain log line say which plugin we went in behind, which is the difference
		// between a line somebody can act on and a line about hex.
		const char* OwnerModuleName(const void* p)
		{
			static char owner[MAX_PATH];
			owner[0] = 0;

			HMODULE module = NULL;
			if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			        (LPCSTR)p, &module)
			    || !module)
			{
				return NULL;
			}
			if (!GetModuleFileNameA(module, owner, MAX_PATH))
				return NULL;

			// The leaf name. The full path is 90 characters of Steam library that is
			// the same on every line.
			const char* slash = strrchr(owner, '\\');
			return slash ? slash + 1 : owner;
		}

		// Where a 5 byte E8 or E9 at `p` lands.
		BYTE* BranchTarget(const BYTE* p)
		{
			INT32 relative = 0;
			memcpy(&relative, p + 1, sizeof(relative));
			return (BYTE*)(p + 5 + relative);
		}

	} // namespace

	bool PatchCallSite(CallSitePatch& site, DWORD siteRva, DWORD expectedTargetRva,
	    void* replacement, const char* what)
	{
		site.rva = siteRva;
		site.patched = false;
		site.previous = NULL;
		site.chained = false;

		BYTE* call = (BYTE*)ModuleAddress(siteRva);
		if (!Readable(call, 5))
		{
			Log("%s call site RVA 0x%08X is not readable, refusing", what, siteRva);
			return false;
		}
		if (*call != 0xE8)
		{
			Log("%s call site RVA 0x%08X holds 0x%02X not an E8 call, refusing. Re-derive "
			    "this site from the IDB.",
			    what, siteRva, (unsigned)*call);
			return false;
		}

		BYTE* target = BranchTarget(call);
		const BYTE* expected = (const BYTE*)ModuleAddress(expectedTargetRva);
		if (target != expected)
		{
			// Inside the game's own image and not the function we named, so these
			// addresses belong to a different build. That is what this check is for and
			// it still refuses.
			if (InsideImage(target))
			{
				Log("%s call site RVA 0x%08X calls 0x%08X, expected 0x%08X, and that is "
				    "inside the game, so these addresses are for a different build. "
				    "Refusing to patch.",
				    what, siteRva, (unsigned)(UINT_PTR)target, (unsigned)(UINT_PTR)expected);
				return false;
			}

			// Outside the image, so another plugin patched this site before we loaded.
			// Go in front of it and let it keep its own route to the real function. See
			// the chaining note in Detour.h.
			const char* owner = OwnerModuleName(target);
			site.chained = true;
			Log("%s call site RVA 0x%08X is already patched by %s, so we are going in "
			    "front of it rather than refusing. Our hook runs first and theirs still "
			    "reaches the game.",
			    what, siteRva, owner ? owner : "something with no module");
		}

		// Set BEFORE the write, because this is what the caller must invoke to run the
		// original and after the write the bytes no longer say what it was.
		site.previous = (void*)target;

		// PAGE_EXECUTE_READWRITE and not PAGE_READWRITE. Taking execute off a .text page,
		// even for the one dword this writes, is a window in which another thread running
		// through that page faults. InstallDetour below makes the same choice.
		const INT32 wanted = (INT32)((BYTE*)replacement - (call + 5));
		DWORD previousProtect = 0;
		if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &previousProtect))
		{
			Log("%s call site RVA 0x%08X could not be made writable, GetLastError=%lu", what,
			    siteRva, GetLastError());
			return false;
		}

		memcpy(call + 1, &wanted, sizeof(wanted));
		VirtualProtect(call, 5, previousProtect, &previousProtect);
		FlushInstructionCache(GetCurrentProcess(), call, 5);

		site.patched = true;
		Log("%s call site patched: RVA 0x%08X now calls 0x%08X, original at 0x%08X%s",
		    what, siteRva, (unsigned)(UINT_PTR)replacement,
		    (unsigned)(UINT_PTR)site.previous, site.chained ? " (chained)" : "");
		return true;
	}

	namespace
	{

		// Renders the first few bytes of a mismatch, so the log says what was actually
		// there rather than just that it was wrong.
		void FormatBytes(char* out, size_t count, const BYTE* bytes, DWORD length)
		{
			out[0] = 0;
			for (DWORD i = 0; i < length && i < 10; ++i)
			{
				char pair[8];
				_snprintf_s(pair, sizeof(pair), _TRUNCATE, "%02X ", bytes[i]);
				strcat_s(out, count, pair);
			}
		}

	} // namespace

	bool InstallDetour(Detour& detour, const char* name, DWORD rva, void* replacement,
	    const BYTE* expectedBytes, DWORD stolenLength)
	{
		detour.name = name;
		detour.rva = rva;
		detour.replacement = replacement;
		detour.expectedBytes = expectedBytes;
		detour.stolenLength = stolenLength;
		detour.trampoline = NULL;
		detour.installed = false;
		detour.chained = false;

		if (stolenLength < 5)
		{
			Log("detour %s: stolen length %lu is under the 5 a jmp rel32 needs, refusing",
			    name, (unsigned long)stolenLength);
			return false;
		}

		BYTE* target = (BYTE*)ModuleAddress(rva);
		if (!Readable(target, stolenLength))
		{
			Log("detour %s: target RVA 0x%08X is not readable, refusing", name, rva);
			return false;
		}
		if (memcmp(target, expectedBytes, stolenLength) != 0)
		{
			// Before calling this a wrong build, check whether it is OUR OWN KIND of
			// patch. Another plugin's InstallDetour leaves an E9 at byte 0 whose
			// displacement lands in its own DLL, outside the game's image.
			BYTE* existing = NULL;
			if (target[0] == 0xE9)
			{
				BYTE* jumped = BranchTarget(target);
				if (!InsideImage(jumped))
					existing = jumped;
			}

			if (!existing)
			{
				char got[64], want[64];
				FormatBytes(got, sizeof(got), target, stolenLength);
				FormatBytes(want, sizeof(want), expectedBytes, stolenLength);
				Log("detour %s: PROLOGUE MISMATCH at RVA 0x%08X. got [%s] want [%s]. "
				    "Refusing to patch, re-derive this detour from the IDB.",
				    name, rva, got, want);
				return false;
			}

			// CHAINING. Nothing is stolen this time. The original prologue bytes are
			// already in the FIRST plugin's trampoline and are not here to take, and
			// the first five bytes are already the jmp we would have written. So the
			// trampoline is a bare thunk to whatever that jmp pointed at, and only the
			// four displacement bytes get rewritten. The NOP padding the first plugin
			// left behind stays exactly as it is.
			BYTE* thunk = (BYTE*)VirtualAlloc(NULL, 5, MEM_COMMIT | MEM_RESERVE,
			    PAGE_EXECUTE_READWRITE);
			if (!thunk)
			{
				Log("detour %s: VirtualAlloc for the chain thunk failed, GetLastError=%lu",
				    name, GetLastError());
				return false;
			}
			thunk[0] = 0xE9;
			const INT32 toExisting = (INT32)(existing - (thunk + 5));
			memcpy(thunk + 1, &toExisting, sizeof(toExisting));

			DWORD previousProtect = 0;
			if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &previousProtect))
			{
				Log("detour %s: VirtualProtect for the chain failed, GetLastError=%lu",
				    name, GetLastError());
				VirtualFree(thunk, 0, MEM_RELEASE);
				return false;
			}
			const INT32 toOurs = (INT32)((BYTE*)replacement - (target + 5));
			memcpy(target + 1, &toOurs, sizeof(toOurs));

			DWORD restored = 0;
			VirtualProtect(target, 5, previousProtect, &restored);
			FlushInstructionCache(GetCurrentProcess(), target, 5);

			detour.trampoline = thunk;
			detour.installed = true;
			detour.chained = true;

			const char* owner = OwnerModuleName(existing);
			Log("detour %s CHAINED at RVA 0x%08X: ours 0x%08X runs in front of 0x%08X in "
			    "%s, thunk 0x%08X. Nothing stolen, only the jmp displacement rewritten.",
			    name, rva, (unsigned)(UINT_PTR)replacement, (unsigned)(UINT_PTR)existing,
			    owner ? owner : "something with no module", (unsigned)(UINT_PTR)thunk);
			return true;
		}

		// Trampoline: the stolen bytes, then a jmp back to the rest of the original.
		BYTE* trampoline = (BYTE*)VirtualAlloc(NULL, stolenLength + 5, MEM_COMMIT | MEM_RESERVE,
		    PAGE_EXECUTE_READWRITE);
		if (!trampoline)
		{
			Log("detour %s: VirtualAlloc for the trampoline failed, GetLastError=%lu",
			    name, GetLastError());
			return false;
		}
		memcpy(trampoline, target, stolenLength);
		trampoline[stolenLength] = 0xE9;
		INT32 backRelative = (INT32)((target + stolenLength) - (trampoline + stolenLength + 5));
		memcpy(trampoline + stolenLength + 1, &backRelative, sizeof(backRelative));

		DWORD previousProtect = 0;
		if (!VirtualProtect(target, stolenLength, PAGE_EXECUTE_READWRITE, &previousProtect))
		{
			Log("detour %s: VirtualProtect failed, GetLastError=%lu", name, GetLastError());
			VirtualFree(trampoline, 0, MEM_RELEASE);
			return false;
		}
		target[0] = 0xE9;
		INT32 forwardRelative = (INT32)((BYTE*)replacement - (target + 5));
		memcpy(target + 1, &forwardRelative, sizeof(forwardRelative));
		for (DWORD i = 5; i < stolenLength; ++i)
			target[i] = 0x90;

		DWORD ignored = 0;
		VirtualProtect(target, stolenLength, previousProtect, &ignored);
		FlushInstructionCache(GetCurrentProcess(), target, stolenLength);

		detour.trampoline = trampoline;
		detour.installed = true;
		Log("detour %s installed: RVA 0x%08X -> 0x%08X, trampoline 0x%08X, %lu bytes stolen",
		    name, rva, (unsigned)(UINT_PTR)replacement, (unsigned)(UINT_PTR)trampoline,
		    (unsigned long)stolenLength);
		return true;
	}

} // namespace workshop
