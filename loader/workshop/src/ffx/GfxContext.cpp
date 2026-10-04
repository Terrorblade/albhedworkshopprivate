#include "ffx/GfxContext.h"

#include "ffx/Api.h"
#include "ffx/Addresses.h"
#include "ffx/Layout.h"
#include "workshop/HostModule.h"
#include "workshop/Overlay.h"

namespace ffx
{

	DWORD* CullOverrideSlot()
	{
		if (!Game.gfxContext)
			return NULL;
		BYTE* context = *Game.gfxContext;
		if (!workshop::Readable(context, Gfx::DisableCull + 4))
			return NULL;
		return (DWORD*)(context + Gfx::DisableCull);
	}

	namespace
	{
		// One read of one field of the Phyre D3D singleton, guarded.
		void* PhyreD3DField(DWORD rva)
		{
			void** slot = (void**)workshop::ModuleAddress(rva);
			if (!workshop::Readable(slot, sizeof(void*)))
				return NULL;
			void* held = *slot;
			if (!workshop::LooksLikePointer((DWORD)(DWORD_PTR)held))
				return NULL;
			if (!workshop::Readable(held, sizeof(void*)))
				return NULL;
			return held;
		}

	} // namespace

	ID3D11Device* D3DDevice()
	{
		return (ID3D11Device*)PhyreD3DField(Rva::PhyreD3DDevice);
	}

	ID3D11DeviceContext* D3DImmediateContext()
	{
		return (ID3D11DeviceContext*)PhyreD3DField(Rva::PhyreD3DImmediateContext);
	}

	IDXGISwapChain* D3DSwapChain()
	{
		return (IDXGISwapChain*)PhyreD3DField(Rva::PhyreD3DSwapChain);
	}

	D3D_FEATURE_LEVEL D3DFeatureLevel()
	{
		DWORD* slot = (DWORD*)workshop::ModuleAddress(Rva::PhyreD3DFeatureLevel);
		if (!workshop::Readable(slot, sizeof(DWORD)))
			return (D3D_FEATURE_LEVEL)0;
		return (D3D_FEATURE_LEVEL)*slot;
	}

	IDXGISwapChain* const* D3DSwapChainSlot()
	{
		return (IDXGISwapChain* const*)workshop::ModuleAddress(Rva::PhyreD3DSwapChain);
	}

	void PointOverlayAtSwapChain()
	{
		workshop::SetOverlaySwapChainSlot((void* const*)D3DSwapChainSlot());
	}

} // namespace ffx
