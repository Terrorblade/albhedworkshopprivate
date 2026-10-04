#pragma once

#include <windows.h>

// ONE ImGui PER PROCESS, OWNED BY THE LOADER.
//
// The loader DLL, dinput8.dll or version.dll, creates the ImGui context, hooks the
// swapchain and pumps one frame. Plugins draw into that context and register panels
// with it. This header is the boundary between the two.
//
// WHY. AlBhedWorkshop.lib is a STATIC library, so every plugin that links it gets its
// own copy of every file scope global in it, ImGui's included. Before this existed both
// shipped plugins installed their own overlay: each created a context, each patched
// IDXGISwapChain::Present, each logged "no other known overlay DLL is loaded" in the
// same millisecond, and which one ended up calling the real dxgi Present came down to
// which thread won. Two contexts on one device worked by luck, and a ResizeBuffers
// re-hook could have left the two hooks pointing at each other.
//
// HOW A PLUGIN SHARES THE CONTEXT. Each plugin keeps its own compiled copy of ImGui's
// code, because there are hundreds of ImGui calls across the plugin sources and making
// every one of them a cross DLL call would be absurd. What it does NOT keep is its own
// state: AdoptOverlayHost points this module's ImGui at the host's context and the
// host's allocator, which is the method Dear ImGui documents for exactly this. After
// that an ImGui::Begin in a plugin and an ImGui::Begin in the loader are talking about
// the same frame.
//
// THE VERSIONS MUST MATCH EXACTLY. Sharing one ImGuiContext between two different ImGui
// versions is undefined behaviour, because the plugin would be reading the host's
// context through a different struct layout. AdoptOverlayHost compares
// IMGUI_VERSION_NUM and refuses rather than corrupting anything. A refused plugin still
// runs, it just has no UI, which is the right trade: the overlay is one feature and the
// rest of a plugin, hooks and sync and logging, is unaffected.
//
// IF THERE IS NO LOADER. A plugin dropped next to a different loader still works. The
// ownership claim below is a named section, not a loader export, so the first module to
// ask becomes the owner. Normally that is the loader because it publishes before it
// loads any plugin. Without it, the first plugin takes the job and the rest adopt it.
// Either way there is exactly one owner.

namespace workshop
{
	// The callback a panel draws with. Lives here as well as in Overlay.h because both
	// sides of the boundary need it and neither should have to include the other.
	typedef void (*OverlayHostDrawFn)(void* user);

	// How a panel gets its window.
	enum OverlayPanelKind
	{
		// The host wraps the callback: it opens a window titled after the panel and
		// calls the callback inside it. The callback must NOT call ImGui::Begin or
		// ImGui::End. This is what every panel did before kinds existed, so it is the
		// default and the two shipped panels keep working untouched.
		PanelInShell = 0,

		// The host calls the callback with no window open and the callback does its own
		// ImGui::Begin and ImGui::End. For a free floating window, a HUD, an always on
		// readout, or anything that wants more than one window.
		PanelOwnWindow = 1,
	};

	// Bump this whenever the struct below changes shape. A client that sees a different
	// number refuses to adopt, because reading a struct through the wrong layout is
	// worse than having no overlay.
	const DWORD OverlayHostAbiVersion = 1;

	// The function table the owner publishes. Plain function pointers and no ImGui types
	// on purpose, so this header costs nothing to include and does not drag imgui.h into
	// the loader's own translation units. The context is opaque: only code that includes
	// imgui.h casts it back to ImGuiContext *.
	struct OverlayHostApi
	{
		DWORD abiVersion;         // OverlayHostAbiVersion as the owner was built
		DWORD imguiVersionNum;    // IMGUI_VERSION_NUM as the owner was built
		const char* imguiVersion; // IMGUI_VERSION, a literal in the owner's image
		DWORD ownerModuleBase;

		// THE STRUCT SIZES, NOT JUST THE VERSION NUMBER.
		//
		// A matching IMGUI_VERSION_NUM is necessary and not sufficient. Two modules can
		// agree on the version and still disagree on layout, because a stale imgui.h on
		// one include path or a different imconfig.h changes the size of ImGuiIO without
		// changing the version. ImGui ships IMGUI_CHECKVERSION for this, but that
		// compares a module against ITS OWN library, which cannot see across a DLL
		// boundary. So the owner publishes what it measured and the client compares.
		//
		// These are the same six sizes IMGUI_CHECKVERSION checks.
		DWORD sizeOfImGuiIo;
		DWORD sizeOfImGuiStyle;
		DWORD sizeOfImVec2;
		DWORD sizeOfImVec4;
		DWORD sizeOfImDrawVert;
		DWORD sizeOfImDrawIdx;

		// ---- the ImGui handoff ----

		// The owner's ImGuiContext *, as a void *. Null until the overlay is actually up.
		void*(__cdecl* Context)(void);

		// The owner's allocator, so a plugin's ImGui allocates out of the same heap the
		// owner frees from. Sharing a context without sharing this frees a block with
		// the wrong allocator the first time ImGui grows a vector the other side wrote.
		void(__cdecl* Allocators)(void*(__cdecl** alloc)(size_t, void*),
		    void(__cdecl** release)(void*, void*), void** user);

		// ---- panels ----
		int(__cdecl* RegisterPanel)(const char* title, OverlayHostDrawFn draw, void* user, int kind);
		void(__cdecl* UnregisterPanel)(int panel);
		void(__cdecl* SetPanelOpen)(int panel, bool open);
		bool(__cdecl* PanelOpen)(int panel);

		// ---- the overlay as a whole ----
		bool(__cdecl* Ready)(void);
		bool(__cdecl* Visible)(void);
		void(__cdecl* SetVisible)(bool visible);
		void(__cdecl* Toggle)(void);
		bool(__cdecl* WantsInput)(void);
		void(__cdecl* SetToggleKey)(int virtualKey);
		int(__cdecl* ToggleKey)(void);

		// ---- the game thread ----
		//
		// ONE VALUE FOR THE WHOLE PROCESS, which is the point. Two plugins note the step
		// thread and three ask about it, and with a static library each was answering
		// from its own copy. A panel asking "am I on the thread it is safe to call the
		// engine from" was getting an answer about its own DLL.
		void(__cdecl* NoteGameThread)(void);
		bool(__cdecl* OnGameThread)(void);

		// ---- the hook target ----
		//
		// The owner knows nothing about any game. Whichever plugin knows where the host
		// keeps its IDXGISwapChain * publishes the slot here, and the owner's hook thread
		// is already waiting for it. That is what keeps the loader usable for FFX-2
		// without a second loader.
		void(__cdecl* SetSwapChainSlot)(void* const* slot);

		// Fills a caller supplied buffer rather than returning a pointer into the
		// owner's statics, because a client reading the owner's buffer is a lifetime and
		// a threading problem for no benefit.
		void(__cdecl* Status)(char* out, int bytes);
	};

	// ---------------------------------------------------------------------------
	// Owner side. Called by the loader, or by the first plugin when there is no loader.
	// ---------------------------------------------------------------------------

	// This module's own table, filled in by the overlay implementation. Only
	// PublishOverlayHost has any business calling it.
	const OverlayHostApi* OverlayOwnerApi();

	// Claims ownership of the process's overlay and publishes the table. True when this
	// module got it. False means somebody else already owns it, which is not an error:
	// call AdoptOverlayHost instead.
	//
	// Does not create the context and does not hook anything. InstallOverlay does that.
	bool PublishOverlayHost();

	// True when THIS module is the owner.
	bool OverlayHostIsSelf();

	// ---------------------------------------------------------------------------
	// Client side. Called by a plugin.
	// ---------------------------------------------------------------------------

	// The owner's table, or null when nobody owns it yet.
	const OverlayHostApi* OverlayHost();

	// Whether this module could share the owner's context at all: same host ABI, same
	// ImGui version, same ImGui struct layout. Says nothing about whether the overlay
	// is up yet.
	//
	// This is the check a plugin wants before it registers anything, because the
	// decision on a mismatch is to keep the plugin and drop its UI, and that decision
	// has to be made at registration time rather than discovered mid frame.
	// OverlayHostAdoptStatus says why when it is false.
	bool OverlayHostCompatible();

	// Points this module's ImGui at the owner's context and allocator.
	//
	// Call it once, early, before any ImGui call in this module. Returns false when
	// there is no owner, when the ABI differs, or when the owner's ImGui is a different
	// version. On false this module must not make ImGui calls and must not register
	// panels, and the log says which of those it was.
	//
	// Safe to call more than once and safe to call before the overlay is up: the context
	// is re-read each time until it exists.
	bool AdoptOverlayHost();

	// True once AdoptOverlayHost has succeeded AND the owner's context exists, which is
	// the real "may I make ImGui calls" test.
	bool OverlayHostAdopted();

	// Why the last AdoptOverlayHost failed, for a log line. Never null.
	const char* OverlayHostAdoptStatus();
} // namespace workshop
