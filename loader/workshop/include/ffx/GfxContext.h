#pragma once

#include <windows.h>

namespace ffx
{

	// The engine's cull-disable flag, resolved and bounds checked. Null when the
	// graphics context is not up yet.
	//
	// A WARNING BEFORE YOU BUILD AN EXPERIMENT ON THIS. gfxContext+0x10D8C is read in
	// exactly one function, FFX_Gfx_IsMeshInstanceVisible 0x6603C0, and that function
	// exists only to maintain m_hideFlags. The real render passes call
	// FFX_Gfx_TestBoundsAgainstFrustum directly, at 0x652D35 and 0x652EBC, and never
	// consult the flag. So forcing it on clears the hide bit and changes nothing about
	// what is drawn, which is exactly what happened when it was tested.
	//
	// It is still a legitimate way to separate "the hide bookkeeping" from "the actual
	// cull". It is not a way to rule culling out.
	DWORD* CullOverrideSlot();

} // namespace ffx
