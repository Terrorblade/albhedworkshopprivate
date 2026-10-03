#pragma once

// The per-frame hook point: FFXApplication::animate, vtable slot +0x10.
//
// This is the one hook every plugin wants, so it lives here rather than being
// re-derived per plugin.
//
// WHY A VTABLE SLOT RATHER THAN A STARTUP HOOK. A plugin DLL attaches after the
// exe's CRT startup, so FFXApplication already exists. Patching a vtable slot is
// time-independent, because the slot lives in .rdata at a fixed RVA and the next
// frame picks it up. A hook on a one-shot startup function would be a race you
// lose.
//
// THE CALLING CONVENTION. animate is __thiscall, so __fastcall with a dummy second
// parameter is the binary compatible way to express it.
//
// THE RETURN VALUE IS NOT OPTIONAL. Phyre's frameTick treats a non-zero return as
// fatal and tears the game down with "App failed during animate". Always run the
// original and return its result unchanged.
//
//   int __fastcall MyAnimate(void *self, void *unusedEdx) {
//       const int result = ffx::OriginalAnimate()(self, unusedEdx);
//       ... your per-frame work ...
//       return result;
//   }
//
//   ffx::HookAnimate(&MyAnimate);

namespace ffx
{

	typedef int(__fastcall* AnimateFn)(void* self, void* unusedEdx);

	// Patches the vtable slot. Returns false and logs if the slot could not be
	// written, in which case nothing was changed.
	bool HookAnimate(AnimateFn replacement);

	// The function that was in the slot. Null before HookAnimate succeeds.
	AnimateFn OriginalAnimate();

} // namespace ffx
