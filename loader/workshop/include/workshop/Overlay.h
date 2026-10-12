#pragma once

#include <windows.h>

#include "workshop/OverlayHost.h"

// Dear ImGui overlay, drawn inside IDXGISwapChain::Present.
//
// ONE OVERLAY PER PROCESS. The loader owns the ImGui context, the swapchain hook, the
// backends and the window input, and every plugin draws into that one context. A plugin
// registers a panel and calls ImGui in it:
//
//     #include "workshop/Overlay.h"
//     #include "imgui.h"
//
//     static void Draw(void*)
//     {
//         ImGui::Text("hello");
//         if (ImGui::Button("go"))
//             DoThing();
//     }
//
//     workshop::AdoptOverlayHost();                        // once, early
//     workshop::RegisterOverlayPanel("My Plugin", &Draw);  // a tab in the shell
//
// That is the whole contract. Do not create a context, do not call NewFrame or Render,
// and do not touch imgui_impl_dx11 or imgui_impl_win32: the owner has done all of it
// and the backends assert if a second module tries. See workshop/OverlayHost.h for why.
//
// ONLY CALL ImGui FROM INSIDE A DRAW CALLBACK. The owner points this module's ImGui at
// the shared context immediately before it calls a panel, so inside a callback
// everything works. Outside one there may be no context yet, and on a version mismatch
// there will never be one.
//
// THE DRAW CALLBACK RUNS ON WHOEVER CALLS Present. That is normally the game's main
// thread, but prove it before you call an engine function from a panel.
// NoteOverlayGameThread plus OverlayOnGameThread is how you check, and both now answer
// for the whole process rather than per DLL.

namespace workshop
{

	// Same type as OverlayHostDrawFn. Both names exist so neither header has to include
	// the other for a callback signature.
	typedef void (*OverlayDrawFn)(void* user);

	// WHERE THE HOST KEEPS ITS IDXGISwapChain *. The overlay deliberately knows no game
	// addresses, so that FFX-2 needs no change here. For FFX the slot is
	// ffx::Rva::PhyreD3DSwapChain, which is PhyreEngine's D3D context singleton + 0x74:
	//
	//     workshop::SetOverlaySwapChainSlot(
	//         (void* const*)ModuleAddress(ffx::Rva::PhyreD3DSwapChain));
	//
	// It is a SLOT, not a swapchain, because the engine has not created one when a
	// plugin starts. The overlay polls it, and it waits for the slot itself as well,
	// since the loader installs the overlay before any plugin is loaded and only a
	// plugin knows where to look. For FFX just call ffx::PointOverlayAtSwapChain.
	void SetOverlaySwapChainSlot(void* const* slot);

	// Claims the process's overlay and hooks IDXGISwapChain::Present on the game's own
	// swapchain. The loader calls this. A plugin does not need to: AdoptOverlayHost
	// covers it, including taking the job when nothing else has.
	//
	// It does NOT create a swapchain of its own: doing that makes the Steam overlay
	// double hook Present and recurse until the stack is gone. See the long comment in
	// Overlay.cpp. Safe from DllMain.
	//
	// Calling it when another module already owns the overlay is not an error. It
	// shares that one instead and returns true, so a plugin built against an older kit
	// keeps working.
	bool InstallOverlay();
	bool OverlayInstalled();

	// True once a real swapchain presented and ImGui came up on it.
	bool OverlayReady();

	// Returns a panel handle, or 0 if the table is full or this module cannot share the
	// owner's ImGui. Panels draw in registration order.
	//
	// PanelInShell, the default, means the owner opens the window and puts this panel in
	// it under its own tab, so the callback draws contents only. PanelOwnWindow means
	// the callback is called with no window open and does its own ImGui::Begin and
	// ImGui::End, which is what to use for a free floating window, a HUD, an always on
	// readout, or more than one window.
	int RegisterOverlayPanel(const char* title, OverlayDrawFn draw, void* user = nullptr,
	    OverlayPanelKind kind = PanelInShell);
	void UnregisterOverlayPanel(int panel);

	void SetOverlayPanelOpen(int panel, bool open);
	bool OverlayPanelOpen(int panel);

	// Nothing draws and no input is captured while hidden.
	void SetOverlayVisible(bool visible);
	bool OverlayVisible();
	void ToggleOverlay();

	// Bare key press toggles the overlay, ignored when ctrl, shift or alt is held.
	// Defaults to VK_F11. Pass 0 to take the key back and drive ToggleOverlay yourself.
	//
	// PROCESS WIDE, so this is not a per plugin setting any more. A plugin that calls
	// it is deciding for every other plugin too, which is almost never what it wants.
	void SetOverlayToggleKey(int virtualKey);
	int OverlayToggleKey();

	// True while ImGui has the mouse or the keyboard, so a plugin can stop feeding
	// the same input to the game.
	bool OverlayWantsInput();

	// Call once per frame from the game thread. OverlayOnGameThread then answers
	// whether a panel is safe to call engine code from.
	void NoteOverlayGameThread();
	bool OverlayOnGameThread();

	const char* OverlayStatus();
	void LogOverlay();

} // namespace workshop
