#include "workshop/OverlayHost.h"

#include "workshop/Overlay.h"
#include "workshop/Log.h"

#include "imgui.h"

#include <stdio.h>

namespace workshop
{
	namespace
	{
		// "ABHO", Al Bhed Host.
		const DWORD kMagic = 0x4F484241;

		// THE SAME MECHANISM AS THE HANG WATCHDOG'S BLOCK, AND FOR THE SAME REASON.
		// AlBhedWorkshop.lib is a static library, so every module that links it gets its
		// own copy of every file scope global in it. The one thing all of them have to
		// agree on therefore cannot live in any of them.
		//
		// api IS THE CLAIM. It is not a separate owner flag with the table published
		// afterwards, because that leaves a window where a second module sees an owner
		// but no table and has to decide what to do about it. One pointer sized compare
		// and swap closes that: whoever lands their table first owns the overlay, and
		// anybody who can see a table can use it.
		struct Shared
		{
			DWORD magic;
			const OverlayHostApi* api;
			LONG ownerBase; // informational, for the log line only
		};

		HANDLE g_section = NULL;
		Shared* g_shared = NULL;

		bool g_isOwner = false;
		bool g_adopted = false;
		char g_adoptStatus[256] = "AdoptOverlayHost has not been called";

		Shared* State()
		{
			if (g_shared)
				return g_shared;

			// Created unconditionally rather than opened first, because opening first is
			// itself a race against another module creating it. CreateFileMappingW on a
			// name that already exists opens that one, which is exactly what is wanted.
			if (!g_section)
			{
				g_section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
				    sizeof(Shared), L"Local\\AlBhedWorkshopOverlayHost");
			}
			if (!g_section)
				return NULL;

			g_shared = (Shared*)MapViewOfFile(g_section, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
			if (!g_shared)
				return NULL;

			// A fresh section is zero filled by the kernel, so there is nothing to set up
			// beyond the marker. The claim below is the only thing that has to be atomic.
			if (g_shared->magic != kMagic)
				g_shared->magic = kMagic;

			return g_shared;
		}

		DWORD SelfBase()
		{
			HMODULE self = NULL;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			        (LPCWSTR)&SelfBase, &self))
			{
				return 0;
			}
			return (DWORD)(DWORD_PTR)self;
		}

		// The file name of the module a base address belongs to, so a log line can say
		// who owns the overlay instead of printing a number.
		void ModuleName(DWORD base, wchar_t* out, int count)
		{
			out[0] = 0;
			if (base == 0)
				return;

			wchar_t path[MAX_PATH] = { 0 };
			if (GetModuleFileNameW((HMODULE)(DWORD_PTR)base, path, MAX_PATH) == 0)
				return;

			const wchar_t* slash = wcsrchr(path, L'\\');
			_snwprintf_s(out, (size_t)count, _TRUNCATE, L"%s", slash ? slash + 1 : path);
		}

		void SelfName(wchar_t* out, int count)
		{
			ModuleName(SelfBase(), out, count);
		}

		void OwnerName(const OverlayHostApi* host, wchar_t* out, int count)
		{
			out[0] = 0;
			if (host)
				ModuleName(host->ownerModuleBase, out, count);
			if (!out[0])
				_snwprintf_s(out, (size_t)count, _TRUNCATE, L"%s", L"another module");
		}
	} // namespace

	bool PublishOverlayHost()
	{
		if (g_isOwner)
			return true;

		Shared* s = State();
		if (!s)
		{
			Log("overlay host: the shared section could not be created, so this module "
			    "can neither claim the overlay nor find whoever has it. Each module will "
			    "fall back to running its own, which is the behaviour this mechanism "
			    "exists to stop. GetLastError=%lu",
			    GetLastError());
			return false;
		}

		const OverlayHostApi* mine = OverlayOwnerApi();
		if (!mine)
			return false;

		// THE CLAIM. Two plugins loaded back to back both reach this, and the loser has
		// to find out that it lost rather than carrying on believing it owns ImGui.
		const void* won = InterlockedCompareExchangePointer((void* volatile*)&s->api,
		    (void*)mine, NULL);
		if (won != NULL)
		{
			wchar_t other[MAX_PATH] = { 0 };
			OwnerName((const OverlayHostApi*)won, other, MAX_PATH);
			Log("overlay host: %S already owns ImGui in this process, so this module "
			    "will share it rather than making a second context.",
			    other);
			return false;
		}

		s->ownerBase = (LONG)SelfBase();
		g_isOwner = true;

		wchar_t me[MAX_PATH] = { 0 };
		SelfName(me, MAX_PATH);
		Log("overlay host: %S owns ImGui for this process. Host ABI %lu, ImGui %s, "
		    "ImGuiIO %lu bytes. Every other module draws into this one context.",
		    me[0] ? me : L"this module", (unsigned long)OverlayHostAbiVersion,
		    IMGUI_VERSION, (unsigned long)sizeof(ImGuiIO));
		return true;
	}

	bool OverlayHostIsSelf()
	{
		return g_isOwner;
	}

	const OverlayHostApi* OverlayHost()
	{
		Shared* s = State();
		return s ? s->api : NULL;
	}

	bool OverlayHostCompatible()
	{
		if (g_isOwner)
		{
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "this module owns the overlay");
			return true;
		}

		const OverlayHostApi* host = OverlayHost();
		if (!host)
		{
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "no module owns the overlay yet");
			return false;
		}

		// ---------------------------------------------------------------------------
		// EVERY ONE OF THESE IS LOAD BEARING.
		//
		// Sharing a context means this module reads the owner's ImGuiContext through
		// its own compiled idea of that struct's shape. Disagree by one byte and every
		// field past it is garbage, and the failure shows up as corrupt draw data
		// rather than as an error anybody can read. So the owner publishes what it
		// measured and this refuses on any mismatch.
		//
		// The version number on its own is not enough. A stale imgui.h on one include
		// path, or a different imconfig.h, changes sizeof(ImGuiIO) without touching the
		// version. ImGui ships IMGUI_CHECKVERSION for exactly this, but it compares a
		// module against ITS OWN library and so cannot see across a DLL boundary.
		// ---------------------------------------------------------------------------
		if (host->abiVersion != OverlayHostAbiVersion)
		{
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "the owner publishes host ABI %lu and this module expects %lu, so the "
			    "function table is not even the right shape to call",
			    (unsigned long)host->abiVersion, (unsigned long)OverlayHostAbiVersion);
			return false;
		}

		if (host->imguiVersionNum != (DWORD)IMGUI_VERSION_NUM)
		{
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "the owner's ImGui is %s (%lu), this module was built against %s (%lu). "
			    "Rebuild this plugin against the loader's third_party/imgui",
			    host->imguiVersion ? host->imguiVersion : "unknown",
			    (unsigned long)host->imguiVersionNum, IMGUI_VERSION,
			    (unsigned long)IMGUI_VERSION_NUM);
			return false;
		}

		if (host->sizeOfImGuiIo != (DWORD)sizeof(ImGuiIO)
		    || host->sizeOfImGuiStyle != (DWORD)sizeof(ImGuiStyle)
		    || host->sizeOfImVec2 != (DWORD)sizeof(ImVec2)
		    || host->sizeOfImVec4 != (DWORD)sizeof(ImVec4)
		    || host->sizeOfImDrawVert != (DWORD)sizeof(ImDrawVert)
		    || host->sizeOfImDrawIdx != (DWORD)sizeof(ImDrawIdx))
		{
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "same ImGui version but a different layout: ImGuiIO %lu vs %lu, "
			    "ImGuiStyle %lu vs %lu, ImDrawVert %lu vs %lu. One side has a different "
			    "imconfig.h or a stale imgui.h on its include path",
			    (unsigned long)host->sizeOfImGuiIo, (unsigned long)sizeof(ImGuiIO),
			    (unsigned long)host->sizeOfImGuiStyle, (unsigned long)sizeof(ImGuiStyle),
			    (unsigned long)host->sizeOfImDrawVert, (unsigned long)sizeof(ImDrawVert));
			return false;
		}

		if (!host->Context || !host->Allocators || !host->RegisterPanel)
		{
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "the owner's table is missing the ImGui handoff");
			return false;
		}

		return true;
	}

	bool AdoptOverlayHost()
	{
		if (g_isOwner)
		{
			// Trivially adopted. The owner's ImGui already points at the owner's
			// context, because it is the module that made it.
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "this module owns the overlay");
			g_adopted = true;
			return true;
		}

		// NOBODY OWNS IT YET, SO TAKE THE JOB. Normally the loader claimed the overlay
		// before it loaded a single plugin, and this branch never runs. It is here for
		// a plugin dropped next to somebody else's loader, or next to none: the first
		// module to ask becomes the owner, so a plugin on its own still gets a UI.
		if (!OverlayHost())
		{
			if (PublishOverlayHost())
			{
				Log("overlay host: nothing had claimed the overlay, so this module took "
				    "it. That means it is running without the Al Bhed Workshop loader, "
				    "which is supported.");
				InstallOverlay();
				_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
				    "this module owns the overlay");
				g_adopted = true;
				return true;
			}
			// Lost the claim to somebody between the two calls above. Fall through and
			// adopt whoever won.
		}

		if (!OverlayHostCompatible())
			return false;

		const OverlayHostApi* host = OverlayHost();

		// ---------------------------------------------------------------------------
		// THE ALLOCATOR FIRST, THEN THE CONTEXT, AND THE ORDER IS NOT A STYLE CHOICE.
		//
		// ImVector's grow is inline in imgui.h, so it compiles into THIS module and
		// binds to THIS module's allocator globals. The loader and every plugin link
		// /MT, a static CRT, so malloc here and malloc in the owner are two different
		// heaps. Share a context without sharing the allocator and the first vector
		// this module grows gets freed by the owner through the wrong heap, which is a
		// corruption that surfaces somewhere else entirely.
		//
		// Setting the allocator before the context means nothing can slip an
		// allocation through the default malloc in between.
		// ---------------------------------------------------------------------------
		ImGuiMemAllocFunc alloc = NULL;
		ImGuiMemFreeFunc release = NULL;
		void* user = NULL;
		host->Allocators(&alloc, &release, &user);
		if (!alloc || !release)
		{
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "the owner handed over no allocator");
			return false;
		}
		ImGui::SetAllocatorFunctions(alloc, release, user);

		// A null context is normal rather than a failure: the owner does not create one
		// until the game has a swapchain and a frame has run. Say so and let the caller
		// come back. Panels registered before this point are fine, they just are not
		// drawn yet, and the registration wrapper retries this on the way into a frame.
		void* ctx = host->Context();
		if (!ctx)
		{
			_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
			    "the owner is found and the allocator is shared, but it has not created "
			    "its context yet, which is normal before the first frame");
			g_adopted = false;
			return false;
		}

		ImGui::SetCurrentContext((ImGuiContext*)ctx);
		g_adopted = true;

		wchar_t owner[MAX_PATH] = { 0 };
		OwnerName(host, owner, MAX_PATH);
		_snprintf_s(g_adoptStatus, sizeof(g_adoptStatus), _TRUNCATE,
		    "sharing %S's ImGui %s context at 0x%08X", owner,
		    host->imguiVersion ? host->imguiVersion : "?", (unsigned)(DWORD_PTR)ctx);
		return true;
	}

	bool OverlayHostAdopted()
	{
		if (g_isOwner)
			return true;
		if (!g_adopted)
			return false;

		// Checked rather than trusted. The flag says adoption happened once, and
		// drawing into a context that has since been torn down is worse than not
		// drawing at all.
		return ImGui::GetCurrentContext() != NULL;
	}

	const char* OverlayHostAdoptStatus()
	{
		return g_adoptStatus;
	}
} // namespace workshop
