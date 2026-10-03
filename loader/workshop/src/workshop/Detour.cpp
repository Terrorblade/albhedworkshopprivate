#include "workshop/Detour.h"

#include "workshop/HostModule.h"
#include "workshop/Log.h"

#include <stdio.h>
#include <string.h>

namespace workshop
{
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
			char got[64], want[64];
			FormatBytes(got, sizeof(got), target, stolenLength);
			FormatBytes(want, sizeof(want), expectedBytes, stolenLength);
			Log("detour %s: PROLOGUE MISMATCH at RVA 0x%08X. got [%s] want [%s]. "
			    "Refusing to patch, re-derive this detour from the IDB.",
			    name, rva, got, want);
			return false;
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
