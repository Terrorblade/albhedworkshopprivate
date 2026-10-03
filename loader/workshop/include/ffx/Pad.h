#pragma once

#include <windows.h>

// A second gamepad, read straight out of PhyreEngine's pad slots.
//
// Reading the bytes rather than calling Phyre__PApplication__isPadButtonDown
// 0x629CA0 is deliberate. That function decompiles to exactly
// `*(BYTE *)(device + semantic - 11 + 24) != 0` after two bounds checks, so the
// call buys nothing, and going direct avoids a __thiscall thunk and any question
// about when it is safe to call. The bounds checks are reproduced here.
//
// Freshness is not our problem: the device poll runs from _WinMain through
// Phyre__PInput__updateAndCheck every frame, so these fields are current no
// matter which FFX code path we are on.
//
// One caveat to know rather than fix: slot assignment is DirectInput enumeration
// order, nothing clears a slot on disconnect, and a device keeps its slot for the
// life of the process. So which physical pad is slot 1 is not something the game
// decides. The control panel shows which slots are bound so it can be checked
// rather than assumed.

namespace ffx
{

	struct PadState
	{
		bool bound;
		float leftX, leftY; // -1 to 1. leftY POSITIVE IS DOWN, as the engine stores it.
		bool cross, circle, square, triangle, l1, r1;
		bool up, down, left, right;
	};

	// The device in a Phyre pad slot, or null if that slot is unbound.
	BYTE* PadDeviceAt(LONG slot);

	// Fills `out` and returns true when the slot is bound and the device reports
	// connected. `out` is zeroed either way.
	bool ReadPad(LONG slot, PadState* out);

	LONG BoundPadCount();

	// One line per bound slot. Worth logging once at startup.
	void LogPadSlots();

} // namespace ffx
