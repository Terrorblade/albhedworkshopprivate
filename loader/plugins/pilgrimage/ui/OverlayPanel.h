#pragma once

// The control panel, drawn as an ImGui window inside the game.
//
// Registers one panel with the workshop overlay. Everything it reads comes from
// ModState telemetry and from each module's status string, and everything it
// writes is a setting or a request flag.
//
// The co-op rows the mod adds to the game's own Config screen are separate and
// stay where they are. See menu/CoopConfig.h.

namespace pilgrimage
{

	// Installs the overlay and registers the panel. F11 shows and hides it.
	bool StartOverlayPanel();
	bool OverlayPanelStarted();

	void ToggleOverlayPanel();

} // namespace pilgrimage
