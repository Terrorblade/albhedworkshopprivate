#pragma once

#include <windows.h>

// Dear ImGui overlay, drawn inside IDXGISwapChain::Present.
//
// The workshop owns the swapchain hook, the D3D11 device, the ImGui context and
// the window input. A plugin just registers a panel and calls ImGui in it:
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
//     workshop::InstallOverlay();                          // once, idempotent
//     workshop::RegisterOverlayPanel("My Plugin", &Draw);
//
// Begin and End are called for you. Draw only the contents.
//
// THE DRAW CALLBACK RUNS ON WHOEVER CALLS Present. That is normally the game's
// main thread, but prove it before you call an engine function from a panel.
// NoteOverlayGameThread plus OverlayOnGameThread is how you check.

namespace workshop
{

	typedef void (*OverlayDrawFn)(void* user);

	// WHERE THE HOST KEEPS ITS IDXGISwapChain *. Call this before InstallOverlay, or
	// there is nothing to hook and the overlay says so and gives up.
	//
	// The overlay deliberately knows no game addresses, so that FFX-2 needs no change
	// here. For FFX the slot is ffx::Rva::PhyreD3DSwapChain, which is PhyreEngine's
	// D3D context singleton plus 0x74:
	//
	//     workshop::SetOverlaySwapChainSlot(
	//         (void* const*)ModuleAddress(ffx::Rva::PhyreD3DSwapChain));
	//     workshop::InstallOverlay();
	//
	// It is a SLOT, not a swapchain, because the engine has not created one yet when a
	// plugin starts. The overlay polls it.
	void SetOverlaySwapChainSlot(void* const* slot);

	// Hooks IDXGISwapChain::Present on the game's own swapchain. It does NOT create one
	// of its own: doing that makes the Steam overlay double hook Present and recurse
	// until the stack is gone. See the long comment in Overlay.cpp.
	// against the game's own device does not matter. Safe from DllMain.
	bool InstallOverlay();
	bool OverlayInstalled();

	// True once a real swapchain presented and ImGui came up on it.
	bool OverlayReady();

	// Returns a panel handle, or 0 if the table is full. Panels draw in
	// registration order.
	int RegisterOverlayPanel(const char* title, OverlayDrawFn draw, void* user = nullptr);
	void UnregisterOverlayPanel(int panel);

	void SetOverlayPanelOpen(int panel, bool open);
	bool OverlayPanelOpen(int panel);

	// Nothing draws and no input is captured while hidden.
	void SetOverlayVisible(bool visible);
	bool OverlayVisible();
	void ToggleOverlay();

	// Bare key press toggles the overlay, ignored when ctrl, shift or alt is held.
	// Defaults to VK_F11. Pass 0 to take the key back and drive ToggleOverlay
	// yourself.
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
