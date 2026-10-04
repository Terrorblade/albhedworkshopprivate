#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

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

	// ---------------------------------------------------------------------------
	// PhyreEngine's D3D11 objects.
	//
	// FFX_Phyre_CreateDeviceAndSwapChain 0x595800 is the only caller of
	// D3D11CreateDeviceAndSwapChain in the whole binary, and it stores its out params
	// into one static singleton at ida 0x00C94F00. So there is exactly one device,
	// one immediate context and one swapchain in the process, and these are them.
	//
	// EVERY ONE OF THESE READS NULL UNTIL THE ENGINE GETS THERE. The singleton lives
	// past the end of initialized .data, so the loader zero fills it. Phyre fills it
	// during PApplication::onInit, which is well after a plugin's DllMain. Poll,
	// never assume.
	ID3D11Device* D3DDevice();
	ID3D11DeviceContext* D3DImmediateContext();
	IDXGISwapChain* D3DSwapChain();
	D3D_FEATURE_LEVEL D3DFeatureLevel();

	// The ADDRESS of the swapchain field rather than its value, for anything that has
	// to wait for the swapchain to appear. This is what the overlay hooks.
	IDXGISwapChain* const* D3DSwapChainSlot();

	// Hands that slot to workshop::SetOverlaySwapChainSlot. Call it before
	// workshop::InstallOverlay, or the overlay has nothing to hook.
	//
	// It lives here rather than in the overlay because the overlay must stay free of
	// game addresses for FFX-2 to reuse it.
	void PointOverlayAtSwapChain();

} // namespace ffx
