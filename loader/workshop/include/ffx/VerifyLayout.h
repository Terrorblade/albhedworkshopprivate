#pragma once

// Is the running FFX.exe the build these addresses were derived from?
//
// The vtable slot check is the real test: the slot holds a relocated pointer, so
// comparing it against base plus RVA validates both the layout and the address
// table at once. The prologue sweep on top will not catch a function that moved to
// a different address with the same prologue, but it does catch an address that
// landed in the middle of something, which is the realistic mistake.
//
// Every plugin should call this and refuse to hook anything if it fails. A wrong
// address silently patched into the wrong build is the worst outcome available.

namespace ffx
{

	bool VerifyLayout();

} // namespace ffx
