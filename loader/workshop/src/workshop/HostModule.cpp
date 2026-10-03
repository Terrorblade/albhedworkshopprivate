#include "workshop/HostModule.h"

#include "workshop/Log.h"

#include <string.h>

namespace workshop
{
	namespace
	{

		BYTE* base = NULL;
		DWORD imageSize = 0;

		bool LooksLikePrologue(const BYTE* p)
		{
			// push ebp; mov ebp, esp      the overwhelmingly common MSVC frame setup
			if (p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC)
				return true;
			// the frame-pointer-omitted shapes
			if (p[0] == 0x83 && p[1] == 0xEC)
				return true; // sub esp, imm8
			if (p[0] == 0x81 && p[1] == 0xEC)
				return true; // sub esp, imm32
			if (p[0] >= 0x50 && p[0] <= 0x57)
				return true; // push reg
			if (p[0] == 0x8B)
				return true; // mov r32, ...
			if (p[0] == 0xA1)
				return true; // mov eax, [imm32]
			if (p[0] == 0x8D)
				return true; // lea
			if (p[0] == 0x33 || p[0] == 0x31)
				return true; // xor r32, r32
			if (p[0] == 0xB8)
				return true; // mov eax, imm32
			if (p[0] == 0x66)
				return true; // operand-size prefix, so a
				             // 16-bit first instruction

			// Each of the next four was added because a real, verified function start in
			// FFX.exe begins this way. The name is the one that needed it, so the reason
			// is traceable rather than being a guess that accumulated.
			if (p[0] >= 0xD8 && p[0] <= 0xDF)
				return true; // x87. FFX_Atel_Init
			if (p[0] == 0xE8)
				return true; // call rel32.
				             // FFX_MainStep_IsFadeBlocking
			if (p[0] == 0x6A)
				return true; // push imm8. FFX_Player_ReadPad
			if (p[0] == 0x80)
				return true; // cmp/test byte with an imm.
				             // FFX_Player_GetSubStepCount

			// Worth saying why this list keeps growing rather than being tightened. The
			// two kinds of mistake here are not symmetric. A false negative refuses a
			// CORRECT address and the plugin then refuses to load at all, which blocks
			// everything. A false positive accepts a wrong address and the failure shows
			// up later with a clearer symptom. So when a real function start turns up
			// that this does not recognise, widen it. Random data still fails.
			//
			// THAT SAID, this is the fourth widening, and each one costs discriminating
			// power. The heuristic is standing in for the check we actually want, which is
			// "is this still the function it was when the address was recorded". A hash of
			// the first N bytes of each address, captured once from a known-good build and
			// compared at load, would answer that exactly and could never refuse a correct
			// address. Worth doing if this needs widening a fifth time.
			return false;
		}

	} // namespace

	void BindHostModule()
	{
		base = (BYTE*)GetModuleHandleW(NULL);
	}

	BYTE* ModuleBase()
	{
		return base;
	}
	DWORD ModuleImageSize()
	{
		return imageSize;
	}

	bool ReadPeHeaders()
	{
		const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
		if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		{
			Log("  guard: DOS signature 0x%04X, expected 0x5A4D", dos->e_magic);
			return false;
		}
		const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
		{
			Log("  guard: not a 32-bit PE (signature 0x%08X machine 0x%04X)",
			    nt->Signature, nt->FileHeader.Machine);
			return false;
		}
		imageSize = nt->OptionalHeader.SizeOfImage;
		Log("  header: in-memory ImageBase=0x%08X SizeOfImage=0x%08X",
		    nt->OptionalHeader.ImageBase, nt->OptionalHeader.SizeOfImage);
		return true;
	}

	bool InsideImage(const void* p)
	{
		if (!imageSize)
			return false;
		return (const BYTE*)p >= base && (const BYTE*)p < base + imageSize;
	}

	bool RvaInImage(DWORD rva)
	{
		return imageSize != 0 && rva < imageSize;
	}

	bool LooksLikePointer(DWORD value)
	{
		return value >= 0x00010000u && value < 0x80000000u;
	}

	bool Readable(const void* p, SIZE_T bytes)
	{
		if (!LooksLikePointer((DWORD)(UINT_PTR)p))
			return false;
		MEMORY_BASIC_INFORMATION info;
		if (!VirtualQuery(p, &info, sizeof(info)))
			return false;
		if (info.State != MEM_COMMIT)
			return false;
		if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))
			return false;
		SIZE_T available = (SIZE_T)((BYTE*)info.BaseAddress + info.RegionSize - (BYTE*)p);
		return available >= bytes;
	}

	bool IsJumpThunk(DWORD rva, const BYTE** outTarget)
	{
		const BYTE* p = (const BYTE*)ModuleAddress(rva);
		if (p[0] == 0xE9)
		{
			INT32 displacement;
			memcpy(&displacement, p + 1, sizeof(displacement));
			if (outTarget)
				*outTarget = p + 5 + displacement;
			return true;
		}
		if (p[0] == 0xEB)
		{
			if (outTarget)
				*outTarget = p + 2 + (INT8)p[1];
			return true;
		}
		return false;
	}

	bool LooksLikeFunctionStart(DWORD rva)
	{
		const BYTE* target = NULL;
		if (IsJumpThunk(rva, &target))
		{
			if (!InsideImage(target))
				return false;
			return LooksLikePrologue(target);
		}
		return LooksLikePrologue((const BYTE*)ModuleAddress(rva));
	}

	bool WriteProtectedDword(void* address, DWORD value, DWORD* outPrevious)
	{
		DWORD previousProtect = 0;
		if (!VirtualProtect(address, sizeof(DWORD), PAGE_READWRITE, &previousProtect))
		{
			Log("WriteProtectedDword: VirtualProtect failed at 0x%08X, GetLastError=%lu",
			    (unsigned)(UINT_PTR)address, GetLastError());
			return false;
		}
		if (outPrevious)
			*outPrevious = *(volatile DWORD*)address;
		*(volatile DWORD*)address = value;

		DWORD ignored = 0;
		VirtualProtect(address, sizeof(DWORD), previousProtect, &ignored);
		return true;
	}

} // namespace workshop
