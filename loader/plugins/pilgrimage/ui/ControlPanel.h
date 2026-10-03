#pragma once

// A plain Win32 tool window on its own thread: a live readout and live tuning.
//
// WHY NOT IMGUI. ImGui is the obvious choice, but drawing it inside this game
// means hooking IDXGISwapChain::Present and the window procedure, building a D3D11
// backend and vendoring the ImGui sources into this repo. That is a worthwhile
// second step and nothing here blocks it, but while the fundamentals were being
// worked out it was all risk and no information: if the game crashed you could not
// tell whether the clone logic or the renderer hook did it. This has zero
// dependencies beyond user32 and cannot touch the game's renderer at all.
//
// When ImGui does go in, the state it shows and the requests it raises are exactly
// the structs in ModState.h, so only the drawing changes.
//
// THREADING, THE ONE RULE THAT MATTERS. This runs on its own thread with its own
// message loop, because a message loop cannot share a thread with the game. That
// thread NEVER calls a game function and never dereferences a character. All it
// does is read published telemetry and write settings and request flags. Breaking
// that rule is how you get a crash that only happens when someone clicks a button
// mid-frame.

namespace pilgrimage
{

	// Starts the UI thread. Safe to call from DllMain. Returns false if the thread
	// could not be created, in which case the hotkeys still work.
	bool StartControlPanel();

	// Show or hide the window. Called from the game thread by the F11 hotkey, which
	// is fine because it only posts to the window.
	void ToggleControlPanel();

} // namespace pilgrimage
