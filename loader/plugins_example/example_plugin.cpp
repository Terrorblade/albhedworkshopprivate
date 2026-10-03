// example_plugin.cpp
//
// Minimal AlBhedWorkshop plugin. Drop the built DLL into <game dir>\AlBhedWorkshop\plugins\
// and the proxy loader picks it up. Its only job is to prove the pipeline works
// and to show the shape a real plugin should take.
//
// Build (32-bit, from an x86 developer prompt):
//     cl /nologo /LD /MT /O2 /W4 /DNDEBUG example_plugin.cpp ^
//        /link /DLL /MACHINE:X86 /OUT:example_plugin.dll kernel32.lib
//
// FFX.exe IS ASLR-RELOCATED - NEVER HARDCODE A VA
//   FFX.exe has DllCharacteristics 0x8140, which includes
//   IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE (0x40), and it ships a full .reloc
//   section (0x1469E4 bytes of relocations, RELOCS_STRIPPED is not set). So the
//   module does NOT reliably load at its 0x00400000 imagebase; Windows rebases
//   it on every launch. IDA shows absolute VAs because IDA maps at the preferred
//   base, but at runtime every address must be computed as
//
//       GetModuleHandleW(NULL) + RVA
//
//   where RVA is the IDA VA minus 0x00400000. Everything below is stored as an
//   RVA for exactly that reason. Getting this wrong is the single easiest way to
//   write a mod that works once and then crashes on the next launch.
//
// WHEN A PLUGIN ACTUALLY GETS LOADED  (measured, not assumed)
//   The loader calls LoadLibraryExW on us from its bootstrap thread. That thread
//   cannot start until FFX.exe's process initialisation releases the loader lock,
//   which means we attach AFTER the exe's CRT startup, not before it.
//
//   Measured on a real launch: by the time our DLL_PROCESS_ATTACH runs, the app
//   singleton at RVA 0x008C9CD8 is ALREADY NON-NULL. So FFXApplication has been
//   constructed before we get control. Do not assume you are early.
//
//   The practical consequence: **hook per-frame or repeating functions, not
//   one-shot startup functions.** A hook on `animate` (slot +0x10) is guaranteed
//   to fire because it runs every frame forever. A hook on `initApplication`
//   (slot +0x08) is a race you will sometimes lose, because the game may already
//   have called it. If you truly need to run before the game's own init, the
//   proxy's DllMain is the only guaranteed-early point, and that runs under the
//   loader lock with all its restrictions.
//
//   Patching static data from DllMain is safe and deterministic regardless of
//   timing. The FFXApplication vtable lives in .rdata at RVA 0x0070D998 and never
//   moves relative to the module, so swapping a slot works whenever we run - the
//   next frame picks it up. That is what InstallAnimateHook does below.
//
//   And obey the loader-lock rules: our DllMain runs under the loader lock, so
//   no LoadLibrary, no COM init, no waiting on other threads. Spawn a thread if
//   you need any of that. VirtualProtect and a dword store are fine.
//
// ASLR IS PER-BOOT, NOT PER-LAUNCH
//   Two consecutive launches both came up at base 0x00510000. Windows picks an
//   image's randomised base once per boot and reuses it, so a hardcoded VA will
//   appear to work perfectly until the machine reboots. Do not let that fool you
//   into thinking VAs are safe.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>

#if !defined(_M_IX86)
#error "AlBhedWorkshop plugins must be 32-bit x86."
#endif

// ---------------------------------------------------------------------------
// FFX.exe RVAs (IDA VA minus 0x00400000). Established in step 2.
// ---------------------------------------------------------------------------
static const DWORD RVA_FFXApplication_vtable = 0x0070D998;  // ??_7FFXApplication@@6B@
static const DWORD RVA_vtslot_initApplication = 0x0070D9A0; // vtable +0x08
static const DWORD RVA_vtslot_animate = 0x0070D9A8;         // vtable +0x10, per-frame update
static const DWORD RVA_vtslot_exitApplication = 0x0070D9B0; // vtable +0x18
static const DWORD RVA_vtslot_render = 0x0070D9B8;          // vtable +0x20

static const DWORD RVA_FFXApp_initApplication = 0x0002F7F0; // expected slot +0x08 contents
static const DWORD RVA_FFXApp_animate = 0x0002F520;         // expected slot +0x10 contents
static const DWORD RVA_FFXApp_exitApplication = 0x0002F600; // expected slot +0x18 contents
static const DWORD RVA_FFXApp_render = 0x0002F930;          // expected slot +0x20 contents

static const DWORD RVA_g_pApplication = 0x008C9CD8; // FFXApplication*, null until startup
static const DWORD RVA_WinMain = 0x0022EE50;

// Offset inside FFXApplication of the function pointer PApplication::initialize
// installs to drive one frame. WinMain calls through it every iteration of the
// PeekMessageA pump, so overwriting it is an alternative frame hook that does
// not touch any vtable.
static const DWORD OFF_app_frameFn = 0x268; // app + 616

static HMODULE g_hExe = NULL;
static BYTE* g_base = NULL;

static void* Rva(DWORD rva)
{
	return g_base + rva;
}

static void PluginLog(const char* fmt, ...)
{
	char body[512];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
	va_end(ap);

	char line[640];
	_snprintf_s(line, sizeof(line), _TRUNCATE, "[example_plugin] %s\r\n", body);
	OutputDebugStringA(line);

	wchar_t path[MAX_PATH];
	if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0)
		return;
	wchar_t* lastSep = NULL;
	for (wchar_t* p = path; *p; ++p)
		if (*p == L'\\' || *p == L'/')
			lastSep = p;
	if (!lastSep)
		return;
	*lastSep = 0;

	wchar_t logPath[MAX_PATH];
	_snwprintf_s(logPath, MAX_PATH, _TRUNCATE, L"%s\\AlBhedWorkshop\\example_plugin.log", path);

	HANDLE h = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
	    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	DWORD w = 0;
	WriteFile(h, line, (DWORD)strlen(line), &w, NULL);
	CloseHandle(h);
}

// Does the module we are inside actually look like the FFX.exe we analysed?
// Checked by verifying the vtable slots still hold the functions we expect,
// which is a far better signal than a version string: it fails loudly if Square
// Enix ever patches the exe, before a hook can corrupt anything.
//
// DO NOT VALIDATE OptionalHeader.ImageBase AGAINST 0x00400000.
//   An earlier version of this function did, and it rejected every single launch.
//   The Windows loader rewrites ImageBase in the *in-memory* optional header to
//   the address the image was actually relocated to, so on an ASLR'd module the
//   runtime value equals GetModuleHandle(NULL), not the value stored in the file
//   on disk. Reading it back and expecting the on-disk 0x00400000 is therefore
//   always wrong at runtime. It is logged below for information only.
//   The vtable slot contents are the real check: they are relocated pointers, so
//   comparing them against base+RVA validates both the layout and our RVA table.
static bool VerifyFFXLayout(void)
{
	const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)g_base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
	{
		PluginLog("  guard: DOS signature is 0x%04X, expected 0x5A4D", dos->e_magic);
		return false;
	}
	const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(g_base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
	{
		PluginLog("  guard: NT signature is 0x%08X, expected 0x00004550", nt->Signature);
		return false;
	}
	if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
	{
		PluginLog("  guard: Machine is 0x%04X, expected 0x014C (i386)", nt->FileHeader.Machine);
		return false;
	}
	if (nt->OptionalHeader.SizeOfImage < RVA_g_pApplication)
	{
		PluginLog("  guard: SizeOfImage 0x%08X is smaller than our largest RVA 0x%08X",
		    nt->OptionalHeader.SizeOfImage, RVA_g_pApplication);
		return false;
	}
	PluginLog("  header: in-memory ImageBase=0x%08X SizeOfImage=0x%08X DllCharacteristics=0x%04X",
	    nt->OptionalHeader.ImageBase, nt->OptionalHeader.SizeOfImage,
	    nt->OptionalHeader.DllCharacteristics);

	struct
	{
		const char* name;
		DWORD slotRva;
		DWORD wantRva;
	} checks[] = {
		{ "initApplication", RVA_vtslot_initApplication, RVA_FFXApp_initApplication },
		{ "animate", RVA_vtslot_animate, RVA_FFXApp_animate },
		{ "exitApplication", RVA_vtslot_exitApplication, RVA_FFXApp_exitApplication },
		{ "render", RVA_vtslot_render, RVA_FFXApp_render },
	};

	bool ok = true;
	for (int i = 0; i < 4; ++i)
	{
		DWORD got = *(volatile DWORD*)Rva(checks[i].slotRva);
		DWORD want = (DWORD)(UINT_PTR)Rva(checks[i].wantRva);
		PluginLog("  vtable slot %-16s = 0x%08X  expected 0x%08X  %s",
		    checks[i].name, got, want, got == want ? "OK" : "MISMATCH");
		if (got != want)
			ok = false;
	}
	return ok;
}

// ---------------------------------------------------------------------------
// The per-frame hook: FFXApplication::animate, vtable slot +0x10.
//
// CALLING CONVENTION
//   animate is __thiscall: `this` in ECX, no stack arguments, callee pops 0.
//   MSVC will not let you define a free function as __thiscall, so the standard
//   idiom is __fastcall with a dummy second parameter - __fastcall also takes its
//   first two arguments in ECX/EDX and also pops 0 stack bytes, so it is binary
//   compatible here. EDX is caller-scratch in both conventions, so passing
//   garbage for it is harmless.
//
// RETURN VALUE IS NOT OPTIONAL
//   Phyre::PApplication::frameTick treats a non-zero return from animate as
//   fatal and tears the game down with "App failed during animate". So we call
//   the original and return its result completely unchanged.
// ---------------------------------------------------------------------------

typedef int(__fastcall* AnimateFn)(void* self, void* edxUnused);

static AnimateFn s_origAnimate = NULL;
static volatile LONG s_frames = 0;

static int __fastcall HookedAnimate(void* self, void* edxUnused)
{
	LONG n = InterlockedIncrement(&s_frames);

	int result = s_origAnimate(self, edxUnused);

	// Log sparsely: the first few frames prove the hook fires, then milestones
	// prove it keeps firing without flooding the log (PluginLog reopens the file
	// per line, so this must not run every frame).
	if (n <= 3 || n == 60 || n == 300 || (n % 1800) == 0)
		PluginLog("animate #%ld -> %d   (this=0x%08X)", n, result, (unsigned)(UINT_PTR)self);

	return result;
}

static bool InstallAnimateHook(void)
{
	DWORD* slot = (DWORD*)Rva(RVA_vtslot_animate);

	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &oldProtect))
	{
		PluginLog("VirtualProtect on the vtable slot failed, GetLastError=%lu", GetLastError());
		return false;
	}

	s_origAnimate = (AnimateFn)(UINT_PTR)*slot;
	*slot = (DWORD)(UINT_PTR)&HookedAnimate;

	DWORD ignored = 0;
	VirtualProtect(slot, sizeof(DWORD), oldProtect, &ignored);

	PluginLog("animate hook installed: slot 0x%08X  0x%08X -> 0x%08X",
	    (unsigned)(UINT_PTR)slot, (unsigned)(UINT_PTR)s_origAnimate,
	    (unsigned)(UINT_PTR)&HookedAnimate);
	return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;

	DisableThreadLibraryCalls(hModule);

	g_hExe = GetModuleHandleW(NULL);
	g_base = (BYTE*)g_hExe;

	wchar_t exePath[MAX_PATH] = { 0 };
	GetModuleFileNameW(NULL, exePath, MAX_PATH);
	PluginLog("attached to %S", exePath);
	PluginLog("runtime module base = 0x%08X (preferred 0x00400000, ASLR slide %+d)",
	    (unsigned)(UINT_PTR)g_base, (int)((INT_PTR)g_base - 0x00400000));

	// Only touch a host whose basename is FFX.exe. The loader already filters on
	// this, but a plugin should never trust that it was launched correctly.
	const wchar_t* name = exePath;
	for (const wchar_t* p = exePath; *p; ++p)
		if (*p == L'\\' || *p == L'/')
			name = p + 1;
	if (_wcsicmp(name, L"FFX.exe") != 0)
	{
		PluginLog("host '%S' is not FFX.exe - doing nothing", name);
		return TRUE;
	}

	if (!VerifyFFXLayout())
	{
		PluginLog("LAYOUT CHECK FAILED - the exe does not match the analysed build. "
		          "Refusing to hook. Re-derive the RVAs before continuing.");
		return TRUE;
	}
	PluginLog("layout check passed");

	PluginLog("FFXApplication vtable at 0x%08X", (unsigned)(UINT_PTR)Rva(RVA_FFXApplication_vtable));
	PluginLog("WinMain at 0x%08X", (unsigned)(UINT_PTR)Rva(RVA_WinMain));
	{
		DWORD app = *(volatile DWORD*)Rva(RVA_g_pApplication);
		PluginLog("app singleton slot 0x%08X currently = 0x%08X (%s)",
		    (unsigned)(UINT_PTR)Rva(RVA_g_pApplication), app,
		    app ? "already constructed - we are NOT early, see the header note"
		        : "still null - we got in before FFXApplication was built");
	}
	PluginLog("once non-null, the per-frame driver pointer lives at app+0x%X", OFF_app_frameFn);

	// Patching the vtable is a time-independent hook: the slot lives in .rdata at
	// a fixed RVA and never moves relative to the module, so it does not matter
	// whether the game has already started. That is why this is the right thing
	// to do from DllMain, and why a one-shot hook on initApplication is not - see
	// the note about load timing above.
	InstallAnimateHook();

	return TRUE;
}
