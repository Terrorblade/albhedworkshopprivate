#include "workshop/HostModule.h"

#include "workshop/Log.h"

#include <string.h>

namespace workshop
{
	namespace
	{

		BYTE* base = NULL;
		DWORD imageSize = 0;

		// Set by SweepImage below, which MEASURES whether the whole host image is
		// committed and readable rather than assuming it. Readable's fast path is off
		// until that measurement says yes.
		bool imageAllReadable = false;
		DWORD imageRegions = 0;

		// Walks every region of the host image once and answers the question
		// Readable's fast path depends on: is all of it committed and readable. One
		// VirtualQuery per region, and the image has about a dozen.
		void SweepImage()
		{
			imageAllReadable = false;
			imageRegions = 0;
			if (!base || !imageSize)
				return;

			const BYTE* end = base + imageSize;
			const BYTE* at = base;

			// 64 is far more regions than any image has. It is a bound so a surprise
			// cannot turn a boot time check into a long loop.
			for (int i = 0; i < 64 && at < end; ++i)
			{
				MEMORY_BASIC_INFORMATION info;
				if (VirtualQuery(at, &info, sizeof(info)) != sizeof(info))
					return;
				if (info.State != MEM_COMMIT)
				{
					Log("  guard: image region at 0x%08X is not committed (state 0x%lX), "
					    "so Readable keeps querying every call",
					    (unsigned)(UINT_PTR)at, info.State);
					return;
				}
				if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))
				{
					Log("  guard: image region at 0x%08X is protect 0x%lX, so Readable "
					    "keeps querying every call",
					    (unsigned)(UINT_PTR)at, info.Protect);
					return;
				}

				++imageRegions;
				const BYTE* next = (const BYTE*)info.BaseAddress + info.RegionSize;
				if (next <= at)
					return; // no forward progress, so stop rather than spin
				at = next;
			}

			if (at < end)
			{
				Log("  guard: the image has more than 64 regions, so Readable keeps "
				    "querying every call");
				return;
			}

			imageAllReadable = true;
			Log("  guard: all %lu regions of the image are committed and readable, so a "
			    "Readable of a module address needs no syscall",
			    imageRegions);
		}

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

		SweepImage();
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

	// ---------------------------------------------------------------------------
	// WHY Readable HAS A FAST PATH, AND WHY IT IS MEASURED RATHER THAN ASSUMED.
	//
	// Readable is a VirtualQuery, which is a syscall that takes the process address
	// space lock. The kit calls it as a guard before every host read, and most of
	// those reads are of module globals. One list rebuild was making roughly half a
	// million of these calls and the game froze for ten seconds inside
	// NtQueryVirtualMemory.
	//
	// The loader maps a PE as one view covering the whole SizeOfImage, and that view
	// cannot be unmapped while the module is loaded, so for an address inside the
	// host image the answer is both always yes and permanently fixed. That is the
	// theory. SweepImage checks it once, with one VirtualQuery per region, and the
	// fast path stays off unless the measurement agrees. If some build really does
	// have a no-access page inside its image, nothing changes except the speed.
	// ---------------------------------------------------------------------------
	bool Readable(const void* p, SIZE_T bytes)
	{
		if (!LooksLikePointer((DWORD)(UINT_PTR)p))
			return false;

		if (imageAllReadable && bytes != 0)
		{
			const BYTE* first = (const BYTE*)p;
			const BYTE* last = first + bytes - 1;

			// Both ends, because a span starting in the image can run off the end of
			// it, and the tail would then be somebody else's memory.
			if (last >= first && InsideImage(first) && InsideImage(last))
				return true;
		}

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
