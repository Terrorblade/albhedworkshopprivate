#pragma once

// An inline detour on FFX_Ch_UpdateCameLenAndZClip, which is the only thing in
// the binary that sets m_hideFlags bit 0x08 (Disp). The hook runs the original and
// then, for our clones only, takes Disp and Z back off.
//
// WHY A DETOUR AND NOT A POKE FROM THE FRAME HOOK. Ordering. This function runs
// inside FFX_Ch_StepAll and the draw submission reads m_hideFlags later in the
// same FFX_MainStep. Clearing the bit from the per-frame hook would land after
// both and be re-set before it ever mattered. Clearing it here puts it between the
// writer and the reader, which is the only place it works.
//
// This is now a diagnostic rather than a fix. The invisibility it was built for
// turned out to be a walkmesh binding problem (see game/Walkmesh.h) and this hook
// reported zero forced frames once that was fixed. It is kept because "force the
// character visible and see whether anything appears" is a useful question to be
// able to ask, and because it is the mod's worked example of a detour.

namespace pilgrimage
{

	bool InstallVisibilityDetour();
	bool VisibilityDetourInstalled();

} // namespace pilgrimage
