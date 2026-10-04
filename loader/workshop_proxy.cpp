// workshop_proxy.cpp - FFX Co-op mod loader, proxy DLL form.
//
// WHAT THIS IS
//   A 32-bit proxy (a.k.a. "wrapper" / "hijack") DLL that FINAL FANTASY X HD
//   Remaster loads automatically because FFX.exe statically imports it. On load
//   it does two jobs:
//     1. Forward every export of the real system DLL so the game keeps working.
//     2. Bootstrap the Workshop: write a log, then LoadLibrary every *.dll found in
//        <game dir>\AlBhedWorkshop\plugins\.
//   The proxy itself should never need to change again. All actual mod work
//   lives in the plugins, which can be rebuilt and reloaded without touching
//   this file.
//
// BUILD TARGET
//   Default target is dinput8.dll (FFX.exe imports DINPUT8.dll!DirectInput8Create).
//   Define FFXCOOP_TARGET_VERSION to build the version.dll variant instead.
//   See build.bat / CMakeLists.txt in this folder.
//
// INSTALL LAYOUT (game dir = folder containing FFX.exe)
//   G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\
//     FFX.exe
//     dinput8.dll                      <- this DLL, built as the proxy
//     AlBhedWorkshop\
//       loader.ini                     <- optional, see ReadLoaderConfig()
//       workshop_loader.log             <- created by us
//       plugins\
//         PilgrimageTogether.dll             <- the actual mod, and anything else
//
// WHY A RUNTIME-RESOLVED JMP THUNK AND NOT A LINKER FORWARDER
//   The obvious approach is a .def file full of forwarders
//   (`DirectInput8Create = dinput8.DirectInput8Create`). That does not work for
//   a proxy: a forwarder names its target module by *base name*, and our module
//   IS "dinput8", so the loader resolves the forwarder back to us and recurses
//   forever. The usual fix is to ship a renamed copy of the real system DLL
//   (dinput8_orig.dll) next to the game, which means shipping and never
//   servicing a copy of a Microsoft binary. We avoid that: we LoadLibraryW the
//   real DLL out of GetSystemDirectoryW() at runtime and jump through a function
//   pointer.
//
//   Each export is a __declspec(naked) stub whose entire body is
//   `jmp dword ptr [g_real_X]`. Nothing touches the stack or any register, so
//   the thunk is completely calling-convention agnostic: __stdcall arg cleanup,
//   __cdecl, HRESULT in EAX, a float in ST0, a struct return via the hidden
//   pointer - all of it passes through untouched, and the real function returns
//   straight to the game's caller. This is why naked JMP beats writing typed
//   C wrappers, which is where proxy DLLs usually get broken (one wrong
//   __stdcall arg count silently corrupts the caller's stack).
//
//   Name decoration: on x86, __stdcall C functions decorate to _Name@N, but the
//   real dinput8.dll/version.dll export *undecorated* names. We therefore never
//   rely on the compiler's decoration at all - every export is emitted with an
//   explicit `/EXPORT:PublicName=_internal_symbol` linker directive below, which
//   is exact and cannot fuzzy-match the wrong thing the way a .def can.
//   Verified against C:\Windows\SysWOW64: dinput8.dll exports 6 named functions
//   and no ordinal-only exports, version.dll exports 17, so the full surface is
//   covered in both cases.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// The only part of the Al Bhed Workshop library the proxy links. It is deliberately
// self contained, so this costs one object file and no other dependency. Installing
// here is what makes a crash inside a plugin's DllMain produce a report, which is
// exactly the case where the loader log otherwise stops mid scan and says nothing.
#include "workshop/CrashHandler.h"

#if !defined(_M_IX86)
#error "Al Bhed Workshop proxy must be built for 32-bit x86. FFX.exe is a 32-bit process."
#endif

// ---------------------------------------------------------------------------
// Target selection
// ---------------------------------------------------------------------------

#if defined(FFXCOOP_TARGET_VERSION)
#define WORKSHOP_REAL_DLL_NAME L"version.dll"
#define WORKSHOP_PROXY_LABEL "version.dll"
// All 17 named exports of C:\Windows\SysWOW64\version.dll.
#define FFXCOOP_EXPORTS(X)        \
	X(GetFileVersionInfoA)        \
	X(GetFileVersionInfoByHandle) \
	X(GetFileVersionInfoExA)      \
	X(GetFileVersionInfoExW)      \
	X(GetFileVersionInfoSizeA)    \
	X(GetFileVersionInfoSizeExA)  \
	X(GetFileVersionInfoSizeExW)  \
	X(GetFileVersionInfoSizeW)    \
	X(GetFileVersionInfoW)        \
	X(VerFindFileA)               \
	X(VerFindFileW)               \
	X(VerInstallFileA)            \
	X(VerInstallFileW)            \
	X(VerLanguageNameA)           \
	X(VerLanguageNameW)           \
	X(VerQueryValueA)             \
	X(VerQueryValueW)
#else
#define FFXCOOP_TARGET_DINPUT8 1
#define WORKSHOP_REAL_DLL_NAME L"dinput8.dll"
#define WORKSHOP_PROXY_LABEL "dinput8.dll"
// All 6 named exports of C:\Windows\SysWOW64\dinput8.dll.
#define FFXCOOP_EXPORTS(X) \
	X(DirectInput8Create)  \
	X(DllCanUnloadNow)     \
	X(DllGetClassObject)   \
	X(DllRegisterServer)   \
	X(DllUnregisterServer) \
	X(GetdfDIJoystick)
#endif

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static HMODULE g_hSelf = NULL;
static HMODULE g_hRealDll = NULL;
static wchar_t g_gameDir[MAX_PATH] = { 0 };   // dir containing FFX.exe / this DLL
static wchar_t g_coopDir[MAX_PATH] = { 0 };   // <gameDir>\AlBhedWorkshop
static wchar_t g_pluginDir[MAX_PATH] = { 0 }; // <gameDir>\AlBhedWorkshop\plugins
static wchar_t g_logPath[MAX_PATH] = { 0 };   // <coopDir>\workshop_loader.log
static wchar_t g_iniPath[MAX_PATH] = { 0 };   // <coopDir>\loader.ini
static volatile LONG g_logReady = 0;

// One slot per forwarded export. Declared extern "C" and at file scope so the
// inline assembler can name them directly.
#define FFXCOOP_DECLARE_SLOT(name) extern "C" void* g_real_##name = NULL;
FFXCOOP_EXPORTS(FFXCOOP_DECLARE_SLOT)
#undef FFXCOOP_DECLARE_SLOT

// ---------------------------------------------------------------------------
// Logging. Open/append/close per line so the log survives a hard crash and so
// we never hold a handle the user cannot delete.
// ---------------------------------------------------------------------------

static void LogRawA(const char* text)
{
	if (!g_logPath[0])
		return;
	HANDLE h = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
	    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	SetFilePointer(h, 0, NULL, FILE_END);
	DWORD written = 0;
	WriteFile(h, text, (DWORD)strlen(text), &written, NULL);
	CloseHandle(h);
}

static void Log(const char* fmt, ...)
{
	char line[1024];
	SYSTEMTIME st;
	GetLocalTime(&st);
	int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02u:%02u:%02u.%03u tid=%lu] ",
	    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
	if (n < 0)
		n = 0;

	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(line + n, sizeof(line) - n, _TRUNCATE, fmt, ap);
	va_end(ap);

	size_t len = strlen(line);
	if (len + 2 < sizeof(line))
	{
		line[len] = '\r';
		line[len + 1] = '\n';
		line[len + 2] = 0;
	}
	LogRawA(line);
	OutputDebugStringA(line);
}

// ---------------------------------------------------------------------------
// Path setup
// ---------------------------------------------------------------------------

static void StripToDirectory(wchar_t* path)
{
	wchar_t* lastSep = NULL;
	for (wchar_t* p = path; *p; ++p)
		if (*p == L'\\' || *p == L'/')
			lastSep = p;
	if (lastSep)
		*lastSep = 0;
}

static void JoinPath(wchar_t* dst, size_t dstCount, const wchar_t* dir, const wchar_t* leaf)
{
	_snwprintf_s(dst, dstCount, _TRUNCATE, L"%s\\%s", dir, leaf);
}

static void SetUpPaths(void)
{
	// Use our own module path, not the exe path: a proxy always sits in the
	// application directory (that is why the loader found it), and this keeps
	// working if someone ever side-loads us somewhere else.
	if (GetModuleFileNameW(g_hSelf, g_gameDir, MAX_PATH) == 0)
		return;
	g_gameDir[MAX_PATH - 1] = 0;
	StripToDirectory(g_gameDir);

	JoinPath(g_coopDir, MAX_PATH, g_gameDir, L"AlBhedWorkshop");
	JoinPath(g_pluginDir, MAX_PATH, g_coopDir, L"plugins");
	JoinPath(g_logPath, MAX_PATH, g_coopDir, L"workshop_loader.log");
	JoinPath(g_iniPath, MAX_PATH, g_coopDir, L"loader.ini");
}

// ---------------------------------------------------------------------------
// Real DLL resolution. Lazy, triggered by the first forwarded call.
//
// LOADER-LOCK NOTE: this deliberately does NOT run from DllMain and is NOT
// pre-warmed by the bootstrap thread. DllMain runs with the loader lock held
// during process initialisation; calling LoadLibrary there is the classic
// deadlock. By resolving on first use we run on a normal game thread, long
// after process init, where LoadLibraryW is completely safe.
// ---------------------------------------------------------------------------

static void FatalW(const wchar_t* msg)
{
	// Only reachable if Windows itself is missing the DLL/export we forward to.
	// Fail loudly rather than jumping through a null pointer or returning with
	// the wrong amount of __stdcall stack cleanup, which would corrupt the
	// caller and produce an unexplainable crash somewhere else entirely.
	MessageBoxW(NULL, msg, L"Al Bhed Workshop loader", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
	TerminateProcess(GetCurrentProcess(), 0xC0000139);
}

// Landing pad for an export the real DLL turned out not to have.
extern "C" void __cdecl workshop_report_missing_export(void); // never returns

extern "C" __declspec(naked) void __cdecl workshop_missing_export(void)
{
	__asm pushad __asm call workshop_report_missing_export
	    // workshop_report_missing_export never returns.
	    __asm popad __asm ret
}

extern "C" void __cdecl workshop_report_missing_export(void)
{
	Log("FATAL: a forwarded export is missing from the real %s", WORKSHOP_PROXY_LABEL);
	FatalW(L"Al Bhed Workshop loader: the real " WORKSHOP_REAL_DLL_NAME L" in your Windows system "
	       L"directory does not export a function FFX.exe asked for.\n\n"
	       L"Delete " WORKSHOP_REAL_DLL_NAME L" from the game folder to run unmodded.");
}

static BOOL CALLBACK LoadRealDllOnce(PINIT_ONCE, PVOID, PVOID*)
{
	wchar_t sysDir[MAX_PATH];
	wchar_t realPath[MAX_PATH];

	UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		FatalW(L"Al Bhed Workshop loader: GetSystemDirectoryW failed.");

	// In a 32-bit process on 64-bit Windows this is C:\Windows\SysWOW64, which
	// is exactly the 32-bit DLL we want. Never hardcode the path.
	JoinPath(realPath, MAX_PATH, sysDir, WORKSHOP_REAL_DLL_NAME);

	// Full path, so the loader matches on path and hands us the system DLL
	// rather than noticing the matching base name and returning ourselves.
	g_hRealDll = LoadLibraryW(realPath);
	if (!g_hRealDll)
	{
		Log("FATAL: LoadLibraryW(%S) failed, GetLastError=%lu", realPath, GetLastError());
		FatalW(L"Al Bhed Workshop loader: could not load the real " WORKSHOP_REAL_DLL_NAME L" from the Windows system directory.");
	}

	Log("forwarding to real DLL %S (base 0x%08X)", realPath, (unsigned)(UINT_PTR)g_hRealDll);

	int missing = 0;
#define FFXCOOP_RESOLVE(name)                                                      \
	g_real_##name = (void*)GetProcAddress(g_hRealDll, #name);                      \
	if (!g_real_##name)                                                            \
	{                                                                              \
		g_real_##name = (void*)&workshop_missing_export;                           \
		Log("  WARNING: real %s has no export '%s'", WORKSHOP_PROXY_LABEL, #name); \
		++missing;                                                                 \
	}
	FFXCOOP_EXPORTS(FFXCOOP_RESOLVE)
#undef FFXCOOP_RESOLVE

	if (missing == 0)
		Log("  all forwarded exports resolved");
	return TRUE;
}

static INIT_ONCE g_realDllOnce = INIT_ONCE_STATIC_INIT;

extern "C" void __cdecl workshop_ensure_real(void)
{
	InitOnceExecuteOnce(&g_realDllOnce, LoadRealDllOnce, NULL, NULL);
}

// ---------------------------------------------------------------------------
// The forwarding thunks.
//
// pushad/pushfd/popfd/popad around the one-time resolve call keeps every
// register and flag the caller might care about intact; the call itself takes
// no arguments, so the caller's arguments sitting above our return address are
// never disturbed. After that it is a bare JMP: the real function sees exactly
// the stack the game built, and returns directly to the game.
//
// The resolve call costs one compare after the first invocation, and these are
// not hot functions (DirectInput8Create is called a handful of times at most).
// ---------------------------------------------------------------------------

#define FFXCOOP_DEFINE_THUNK(name)                                                                                           \
	extern "C" __declspec(naked) void __cdecl workshop_thunk_##name(void)                                                    \
	{                                                                                                                        \
		__asm pushad __asm pushfd __asm call workshop_ensure_real __asm popfd __asm popad __asm jmp dword ptr[g_real_##name] \
	}
FFXCOOP_EXPORTS(FFXCOOP_DEFINE_THUNK)
#undef FFXCOOP_DEFINE_THUNK

// Explicit, undecorated public export names. Written out one per line on
// purpose: this is the part that breaks silently if you get it wrong, so it
// should be readable rather than clever.
#if defined(FFXCOOP_TARGET_VERSION)
#pragma comment(linker, "/EXPORT:GetFileVersionInfoA=_workshop_thunk_GetFileVersionInfoA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoByHandle=_workshop_thunk_GetFileVersionInfoByHandle")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoExA=_workshop_thunk_GetFileVersionInfoExA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoExW=_workshop_thunk_GetFileVersionInfoExW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeA=_workshop_thunk_GetFileVersionInfoSizeA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeExA=_workshop_thunk_GetFileVersionInfoSizeExA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeExW=_workshop_thunk_GetFileVersionInfoSizeExW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeW=_workshop_thunk_GetFileVersionInfoSizeW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoW=_workshop_thunk_GetFileVersionInfoW")
#pragma comment(linker, "/EXPORT:VerFindFileA=_workshop_thunk_VerFindFileA")
#pragma comment(linker, "/EXPORT:VerFindFileW=_workshop_thunk_VerFindFileW")
#pragma comment(linker, "/EXPORT:VerInstallFileA=_workshop_thunk_VerInstallFileA")
#pragma comment(linker, "/EXPORT:VerInstallFileW=_workshop_thunk_VerInstallFileW")
#pragma comment(linker, "/EXPORT:VerLanguageNameA=_workshop_thunk_VerLanguageNameA")
#pragma comment(linker, "/EXPORT:VerLanguageNameW=_workshop_thunk_VerLanguageNameW")
#pragma comment(linker, "/EXPORT:VerQueryValueA=_workshop_thunk_VerQueryValueA")
#pragma comment(linker, "/EXPORT:VerQueryValueW=_workshop_thunk_VerQueryValueW")
#else
#pragma comment(linker, "/EXPORT:DirectInput8Create=_workshop_thunk_DirectInput8Create")
// The four COM self-registration entry points are exported from the DLL but
// marked PRIVATE so they stay out of the generated import library, which is what
// the real dinput8.dll does too and what silences LNK4104.
#pragma comment(linker, "/EXPORT:DllCanUnloadNow=_workshop_thunk_DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/EXPORT:DllGetClassObject=_workshop_thunk_DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllRegisterServer=_workshop_thunk_DllRegisterServer,PRIVATE")
#pragma comment(linker, "/EXPORT:DllUnregisterServer=_workshop_thunk_DllUnregisterServer,PRIVATE")
#pragma comment(linker, "/EXPORT:GetdfDIJoystick=_workshop_thunk_GetdfDIJoystick")
#endif

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

// Default host filter. Every executable in the FFX HD folder statically imports
// DINPUT8.dll (and VERSION, WINMM, d3d11, dbghelp, XINPUT9_1_0), so whichever
// DLL name we proxy, we also get loaded into FFX-2.exe and FFX&X-2_Will.exe.
// Per the launcher's embedded config:
//     <FFX>FFX.exe</FFX>            <SideStory>FFX.exe</SideStory>
//     <FFX2>FFX-2.exe</FFX2>        <LastMission>FFX-2.exe</LastMission>
//     <Credit>FFX&X-2_Will.exe</Credit>
// FFX.exe is the only host our FFX addresses are valid for (Eternal Calm is the
// same exe launched with "_ECalm" on the command line). FFX&X-2_Will.exe is the
// credits player. So by default we only bootstrap plugins inside FFX.exe;
// forwarding still happens in every host so the other executables keep working.
#define FFXCOOP_DEFAULT_HOSTS L"FFX.exe"

struct LoaderConfig
{
	DWORD delayMs;      // sleep before loading plugins; 0 = as early as possible
	DWORD enabled;      // 0 disables plugin loading entirely (forwarding still works)
	wchar_t hosts[512]; // ';'-separated exe names, or "*" for any host
};

static void ReadLoaderConfig(LoaderConfig* cfg)
{
	cfg->delayMs = 0;
	cfg->enabled = 1;
	wcscpy_s(cfg->hosts, FFXCOOP_DEFAULT_HOSTS);
	if (GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES)
		return;
	cfg->delayMs = GetPrivateProfileIntW(L"loader", L"DelayMs", 0, g_iniPath);
	cfg->enabled = GetPrivateProfileIntW(L"loader", L"Enabled", 1, g_iniPath);
	GetPrivateProfileStringW(L"loader", L"HostExe", FFXCOOP_DEFAULT_HOSTS,
	    cfg->hosts, (DWORD)(sizeof(cfg->hosts) / sizeof(wchar_t)), g_iniPath);
	if (cfg->delayMs > 60000)
		cfg->delayMs = 60000;
}

// Drop a self-documenting default ini on first run so the knobs are discoverable
// without reading this source.
static void WriteDefaultIniIfMissing(void)
{
	if (GetFileAttributesW(g_iniPath) != INVALID_FILE_ATTRIBUTES)
		return;
	static const char kDefaultIni[] =
	    "; Al Bhed Workshop loader settings. Created automatically on first run.\r\n"
	    "[loader]\r\n"
	    "; 1 = load plugins, 0 = forward the proxied DLL only and load nothing.\r\n"
	    "Enabled=1\r\n"
	    "\r\n"
	    "; Milliseconds to wait before scanning AlBhedWorkshop\\plugins. Leave at 0 so\r\n"
	    "; plugins come up as early as possible. Raise it only when debugging a\r\n"
	    "; plugin that is racing the game's own startup.\r\n"
	    "DelayMs=0\r\n"
	    "\r\n"
	    "; Which host executables get plugins. Every exe in this folder imports the\r\n"
	    "; DLL we proxy, so without this filter the mod would also load into\r\n"
	    "; FFX-2.exe and FFX&X-2_Will.exe (the credits player), where the FFX\r\n"
	    "; addresses are wrong. Semicolon-separated, or * for every host.\r\n"
	    "HostExe=FFX.exe\r\n";
	HANDLE h = CreateFileW(g_iniPath, GENERIC_WRITE, FILE_SHARE_READ, NULL,
	    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	DWORD written = 0;
	WriteFile(h, kDefaultIni, (DWORD)(sizeof(kDefaultIni) - 1), &written, NULL);
	CloseHandle(h);
}

static const wchar_t* BaseName(const wchar_t* path)
{
	const wchar_t* out = path;
	for (const wchar_t* p = path; *p; ++p)
		if (*p == L'\\' || *p == L'/')
			out = p + 1;
	return out;
}

// hostList is ';'-separated. "*" matches anything.
static bool HostIsAllowed(const wchar_t* hostList, const wchar_t* exeName)
{
	const wchar_t* p = hostList;
	while (*p)
	{
		while (*p == L' ' || *p == L';')
			++p;
		const wchar_t* start = p;
		while (*p && *p != L';')
			++p;
		const wchar_t* end = p;
		while (end > start && end[-1] == L' ')
			--end;
		size_t len = (size_t)(end - start);
		if (len == 1 && start[0] == L'*')
			return true;
		if (len && _wcsnicmp(start, exeName, len) == 0 && exeName[len] == 0)
			return true;
	}
	return false;
}

static int LoadPlugins(void)
{
	wchar_t pattern[MAX_PATH];
	JoinPath(pattern, MAX_PATH, g_pluginDir, L"*.dll");

	WIN32_FIND_DATAW fd;
	HANDLE hFind = FindFirstFileW(pattern, &fd);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		Log("no plugins found in %S", g_pluginDir);
		return 0;
	}

	int loaded = 0, failed = 0;
	do
	{
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		wchar_t full[MAX_PATH];
		JoinPath(full, MAX_PATH, g_pluginDir, fd.cFileName);

		// LOAD_WITH_ALTERED_SEARCH_PATH makes the plugin's own folder the first
		// place its dependencies are looked for, so a plugin can ship its own
		// support DLLs next to itself instead of polluting the game folder.
		HMODULE h = LoadLibraryExW(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (h)
		{
			Log("  loaded plugin %S -> base 0x%08X", fd.cFileName, (unsigned)(UINT_PTR)h);
			++loaded;
		}
		else
		{
			Log("  FAILED to load plugin %S, GetLastError=%lu", fd.cFileName, GetLastError());
			++failed;
		}
	} while (FindNextFileW(hFind, &fd));
	FindClose(hFind);

	Log("plugin scan done: %d loaded, %d failed", loaded, failed);
	return loaded;
}

static DWORD WINAPI BootstrapThread(LPVOID)
{
	// Keep ourselves resident for the life of the process. A plugin that has
	// patched a vtable to point into our address space must never see us
	// unmapped, and some tools do call FreeLibrary on the module that owns a
	// forwarded export.
	HMODULE pinned = NULL;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
	    (LPCWSTR)&BootstrapThread, &pinned);

	CreateDirectoryW(g_coopDir, NULL);
	CreateDirectoryW(g_pluginDir, NULL);
	InterlockedExchange(&g_logReady, 1);

	wchar_t exePath[MAX_PATH] = { 0 };
	GetModuleFileNameW(NULL, exePath, MAX_PATH);

	LogRawA("\r\n");
	Log("=== Al Bhed Workshop loader (%s proxy) ===", WORKSHOP_PROXY_LABEL);
	Log("host exe   : %S", exePath);
	Log("proxy dir  : %S", g_gameDir);
	Log("plugin dir : %S", g_pluginDir);
	Log("pid        : %lu", GetCurrentProcessId());

	WriteDefaultIniIfMissing();
	LoaderConfig cfg;
	ReadLoaderConfig(&cfg);
	Log("config     : Enabled=%lu DelayMs=%lu HostExe=%S", cfg.enabled, cfg.delayMs, cfg.hosts);

	if (!cfg.enabled)
	{
		Log("plugin loading disabled by loader.ini, forwarding only");
		return 0;
	}

	const wchar_t* exeName = BaseName(exePath);
	if (!HostIsAllowed(cfg.hosts, exeName))
	{
		Log("host '%S' is not in HostExe, forwarding only (set HostExe=* to load everywhere)", exeName);
		return 0;
	}

	if (cfg.delayMs)
		Sleep(cfg.delayMs);

	// The first LoadLibraryExW below blocks on the loader lock until the host
	// process has finished initialising its static imports, then runs. In
	// practice plugins therefore come up right as FFX.exe's CRT startup begins,
	// which is before FFXApplication is constructed and before WinMain
	// (RVA 0x0022EE50) runs. That is early enough to patch the FFXApplication
	// vtable (RVA 0x0070D998) or to install inline hooks.
	//
	// It is still a race against the game thread, so a plugin that needs a
	// guaranteed-early point should patch static data (the vtable) rather than
	// trying to run code before a specific game function.
	//
	// NOTE for plugin authors: FFX.exe has DYNAMIC_BASE set and ships a .reloc
	// section, so it is ASLR-relocated. Every address above is an RVA. Always
	// compute GetModuleHandle(NULL) + RVA; never use a literal VA.
	// Reads each loaded module's preferred base off the disk. Here rather than in
	// DllMain because it opens files, and before LoadPlugins so that FFX.exe's preferred
	// base is already known if a plugin faults inside its own DllMain. That is the exact
	// case that previously left nothing behind but a WER entry.
	workshop::WarmCrashHandler();

	Log("crash handler: %s. A crash writes albhed_crash.log with a call chain, and "
	    "that covers each plugin's DllMain, which is where the loader log would "
	    "otherwise just stop.",
	    workshop::CrashHandlerInstalled() ? "installed" : "COULD NOT BE INSTALLED");

	workshop::CrashContext("loader: scanning for plugins");
	LoadPlugins();
	workshop::CrashContext("loader: the plugin scan finished");
	return 0;
}

// ---------------------------------------------------------------------------
// DllMain - deliberately almost empty.
//
// We are loaded as a static import of FFX.exe, so this runs inside
// LdrpInitializeProcess with the loader lock held. Anything that takes the
// loader lock from another thread, or that calls LoadLibrary / CoInitialize /
// waits on a thread here, risks deadlock. So the only things we do are:
//   - record our module handle
//   - compute paths (pure string work plus GetModuleFileNameW, no loading)
//   - CreateThread, which is safe from DllMain as long as we do not wait on it
// Everything that can block, allocate a module, or touch the disk happens on
// BootstrapThread. Real-DLL forwarding is resolved even later, on the first
// forwarded export call (see workshop_ensure_real).
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
	{
		g_hSelf = hModule;
		// Our own thread notifications are useless to us and cost the loader
		// work on every CreateThread in a game with many worker threads. This
		// does not affect plugins; they get their own DllMain notifications.
		DisableThreadLibraryCalls(hModule);
		SetUpPaths();

		// Before the bootstrap thread, so it covers LoadPlugins and therefore every
		// plugin's DllMain. SetUnhandledExceptionFilter and the named section the
		// handler uses for its state are both safe here: no loader lock, no wait on
		// another thread, no disk.
		workshop::InstallCrashHandler();
		workshop::CrashContext("loader: DllMain, about to start the bootstrap thread");

		HANDLE th = CreateThread(NULL, 0, BootstrapThread, NULL, 0, NULL);
		if (th)
			CloseHandle(th);
		break;
	}
	case DLL_PROCESS_DETACH:
		// Nothing. We are pinned, so this only happens at process teardown,
		// where touching other modules is unsafe.
		break;
	}
	return TRUE;
}
