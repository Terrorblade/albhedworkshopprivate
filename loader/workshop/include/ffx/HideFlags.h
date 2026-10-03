#pragma once

#include <windows.h>

namespace ffx
{

	// The game's own labels for m_hideFlags, lifted from
	// SG_DebugGui_ChrInfoWindowProc 0x85393C, which prints "Hide : %x" and then
	// appends one of these per bit. Returns "visible" for zero. The buffer is static,
	// so one call per log line.
	const char* HideFlagNames(BYTE flags);

} // namespace ffx
