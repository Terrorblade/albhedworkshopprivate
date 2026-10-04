#include "workshop/Overlay.h"

#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "workshop/HostModule.h"
#include "workshop/Log.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace workshop
{

	namespace
	{

		// IDXGISwapChain vtable slots. IUnknown 0-2, IDXGIObject 3-6,
		// IDXGIDeviceSubObject 7, then the swapchain's own.
		const int SlotPresent = 8;
		const int SlotResizeBuffers = 13;

		typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
		typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
		typedef HRESULT(WINAPI* CreateDeviceAndSwapChainFn)(IDXGIAdapter*, D3D_DRIVER_TYPE,
		    HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*,
		    IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

		const int MaxPanels = 32;

		struct Panel
		{
			char title[64];
			OverlayDrawFn draw;
			void* user;
			bool open;
			bool used;
		};

		Panel g_panels[MaxPanels];
		int g_panelCount = 0;

		PresentFn g_originalPresent = nullptr;

		// Where the host keeps its IDXGISwapChain *. Supplied by the game layer so this
		// file needs no game addresses of its own. For FFX that is
		// ffx::Rva::PhyreD3DSwapChain, the PhyreEngine D3D context singleton + 0x74.
		void* const* g_swapChainSlot = nullptr;
		ResizeBuffersFn g_originalResizeBuffers = nullptr;

		IDXGISwapChain* g_swapChain = nullptr;
		ID3D11Device* g_device = nullptr;
		ID3D11DeviceContext* g_context = nullptr;
		ID3D11RenderTargetView* g_target = nullptr;
		HWND g_window = NULL;
		WNDPROC g_originalWndProc = nullptr;

		bool g_installed = false;
		bool g_ready = false;
		bool g_initFailed = false;
		bool g_visible = false;
		bool g_showHub = true;
		int g_toggleKey = VK_F11;
		DWORD g_gameThread = 0;
		DWORD g_presentThread = 0;
		unsigned long g_frames = 0;
		char g_status[200] = "not installed";

		// ---------------------------------------------------------------------------

		void ReleaseTarget()
		{
			if (g_target)
			{
				g_target->Release();
				g_target = nullptr;
			}
		}

		bool EnsureTarget()
		{
			if (g_target)
				return true;
			if (!g_swapChain || !g_device)
				return false;

			ID3D11Texture2D* back = nullptr;
			if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) || !back)
				return false;
			const HRESULT hr = g_device->CreateRenderTargetView(back, nullptr, &g_target);
			back->Release();
			return SUCCEEDED(hr) && g_target != nullptr;
		}

		LRESULT CALLBACK OverlayWndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
		{
			// A bare press of the toggle key, no modifier. Checked before ImGui sees it
			// so the key still works when a text box has focus.
			if (message == WM_KEYDOWN && g_toggleKey != 0 && (int)wparam == g_toggleKey)
			{
				const bool modifier = (GetKeyState(VK_CONTROL) & 0x8000) != 0
				    || (GetKeyState(VK_SHIFT) & 0x8000) != 0
				    || (GetKeyState(VK_MENU) & 0x8000) != 0;
				if (!modifier)
				{
					ToggleOverlay();
					return 0;
				}
			}

			if (g_ready && g_visible)
			{
				if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
					return 1;

				// Swallow what ImGui is using, so a click on a button is not also a
				// click in the game. The game's own GetAsyncKeyState polling cannot be
				// blocked this way, which is what OverlayWantsInput is for.
				const ImGuiIO& io = ImGui::GetIO();
				switch (message)
				{
				case WM_MOUSEMOVE:
				case WM_LBUTTONDOWN:
				case WM_LBUTTONUP:
				case WM_LBUTTONDBLCLK:
				case WM_RBUTTONDOWN:
				case WM_RBUTTONUP:
				case WM_RBUTTONDBLCLK:
				case WM_MBUTTONDOWN:
				case WM_MBUTTONUP:
				case WM_MBUTTONDBLCLK:
				case WM_MOUSEWHEEL:
				case WM_MOUSEHWHEEL:
				case WM_SETCURSOR:
					if (io.WantCaptureMouse)
						return 1;
					break;
				case WM_KEYDOWN:
				case WM_KEYUP:
				case WM_SYSKEYDOWN:
				case WM_SYSKEYUP:
				case WM_CHAR:
					if (io.WantCaptureKeyboard || io.WantTextInput)
						return 1;
					break;
				default:
					break;
				}
			}

			return CallWindowProcA(g_originalWndProc, window, message, wparam, lparam);
		}

		bool EnsureImGui(IDXGISwapChain* swapChain)
		{
			if (g_ready)
				return g_swapChain == swapChain;
			if (g_initFailed)
				return false;

			ID3D11Device* device = nullptr;
			if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device)) || !device)
			{
				// Not a D3D11 swapchain. Leave it alone rather than failing for good,
				// since the game may make another.
				return false;
			}

			DXGI_SWAP_CHAIN_DESC desc;
			memset(&desc, 0, sizeof(desc));
			if (FAILED(swapChain->GetDesc(&desc)) || desc.OutputWindow == NULL)
			{
				device->Release();
				g_initFailed = true;
				Log("overlay: the swapchain has no output window, giving up");
				return false;
			}

			ID3D11DeviceContext* context = nullptr;
			device->GetImmediateContext(&context);
			if (!context)
			{
				device->Release();
				g_initFailed = true;
				Log("overlay: no immediate context, giving up");
				return false;
			}

			g_swapChain = swapChain;
			g_device = device;
			g_context = context;
			g_window = desc.OutputWindow;

			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr; // no stray ini in the game folder
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			ImGui::StyleColorsDark();

			if (!ImGui_ImplWin32_Init(g_window) || !ImGui_ImplDX11_Init(g_device, g_context))
			{
				ImGui::DestroyContext();
				g_initFailed = true;
				Log("overlay: an ImGui backend refused to start");
				return false;
			}

			g_originalWndProc = (WNDPROC)SetWindowLongPtrA(g_window, GWLP_WNDPROC,
			    (LONG_PTR)&OverlayWndProc);
			if (!g_originalWndProc)
				Log("overlay: could not subclass the game window, so there is no mouse or "
				    "keyboard input. GetLastError=%lu", GetLastError());

			g_ready = true;
			Log("overlay: up on hwnd %p, %ux%u, present thread %lu, ImGui %s",
			    (void*)g_window, desc.BufferDesc.Width, desc.BufferDesc.Height,
			    GetCurrentThreadId(), ImGui::GetVersion());
			return true;
		}

		void DrawHub()
		{
			if (!g_showHub)
				return;

			ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Al Bhed Workshop", &g_showHub))
			{
				ImGui::Text("%d panel%s registered", g_panelCount,
				    g_panelCount == 1 ? "" : "s");
				ImGui::Separator();
				for (int i = 0; i < MaxPanels; ++i)
					if (g_panels[i].used)
						ImGui::Checkbox(g_panels[i].title, &g_panels[i].open);
				ImGui::Separator();
				ImGui::Text("%.1f fps, frame %lu", ImGui::GetIO().Framerate, g_frames);
				ImGui::TextUnformatted(OverlayOnGameThread()
				        ? "drawing on the game thread"
				        : "drawing off the game thread");
			}
			ImGui::End();
		}

		void DrawFrame()
		{
			if (!EnsureTarget())
				return;

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ImGui::GetIO().MouseDrawCursor = true; // the game hides the OS cursor

			DrawHub();

			for (int i = 0; i < MaxPanels; ++i)
			{
				Panel& panel = g_panels[i];
				if (!panel.used || !panel.open || !panel.draw)
					continue;
				ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
				if (ImGui::Begin(panel.title, &panel.open))
					panel.draw(panel.user);
				ImGui::End();
			}

			ImGui::Render();

			// imgui_impl_dx11 saves and restores the pipeline state but not the render
			// targets, so those are ours to put back.
			ID3D11RenderTargetView* previousTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { nullptr };
			ID3D11DepthStencilView* previousDepth = nullptr;
			g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
			    previousTargets, &previousDepth);

			g_context->OMSetRenderTargets(1, &g_target, nullptr);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

			g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
			    previousTargets, previousDepth);
			for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
				if (previousTargets[i])
					previousTargets[i]->Release();
			if (previousDepth)
				previousDepth->Release();

			++g_frames;
		}

		HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
		{
			g_presentThread = GetCurrentThreadId();
			if (EnsureImGui(swapChain) && g_visible)
				DrawFrame();
			return g_originalPresent(swapChain, syncInterval, flags);
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffersHook(IDXGISwapChain* swapChain, UINT bufferCount,
		    UINT width, UINT height, DXGI_FORMAT format, UINT flags)
		{
			// The cached view holds a reference to the old back buffer, and
			// ResizeBuffers fails outright while anything still does.
			if (swapChain == g_swapChain)
				ReleaseTarget();
			return g_originalResizeBuffers(swapChain, bufferCount, width, height, format, flags);
		}

		// ---------------------------------------------------------------------------
		// Finding the vtable.
		//
		// A throwaway device and swapchain, purely to read IDXGISwapChain's vtable. It
		// lives in dxgi.dll and every swapchain in the process shares it, so patching
		// a slot here catches the game's swapchain too, whenever it gets made. That is
		// why load order does not matter.
		// ---------------------------------------------------------------------------

		bool PatchSlot(void** vtable, int slot, void* replacement, void** outOriginal, const char* what)
		{
			DWORD previous = 0;
			if (!WriteProtectedDword(&vtable[slot], (DWORD)(DWORD_PTR)replacement, &previous))
			{
				Log("overlay: could not write the %s vtable slot", what);
				return false;
			}
			*outOriginal = (void*)(DWORD_PTR)previous;
			return true;
		}

		// ---------------------------------------------------------------------------
		// Hooking the game's OWN swapchain.
		//
		// THIS USED TO CREATE A THROWAWAY DEVICE, WINDOW AND SWAPCHAIN just to read the
		// shared IDXGISwapChain vtable, and that cost a boot crash. A second swapchain
		// appearing in the process makes the Steam overlay install its Present hook a
		// second time, and its second trampoline's stolen bytes are its own jump back to
		// itself. The result was a two frame cycle about 21,000 deep wholly inside
		// gameoverlayrenderer.dll, a stack overflow, and no frame of ours anywhere on the
		// stack. Square Enix's own CSERHelper then died writing CoreDump.dmp, so the only
		// evidence was a dbgcore.dll fault that pointed nowhere near the cause.
		//
		// So no device of our own now. The host tells us where it keeps its swapchain,
		// we wait for it to exist, and we take the vtable from that. Same vtable, since
		// every IDXGISwapChain in the process shares it, but nothing new is created.
		//
		// The slot is supplied rather than hardcoded so this file stays game agnostic,
		// which is what FFX-2 will need.
		// ---------------------------------------------------------------------------
		bool HookGameSwapChain()
		{
			// imgui_impl_dx11 compiles its two shaders with D3DCompile, which pulls in
			// d3dcompiler_47.dll. That import is delay loaded, so a machine without the
			// DLL costs the overlay and nothing else, but only if we check here rather
			// than letting the first delay load fault.
			if (!LoadLibraryA("d3dcompiler_47.dll"))
			{
				Log("overlay: d3dcompiler_47.dll is missing, so ImGui cannot build its "
				    "shaders. Everything else still works.");
				return false;
			}

			if (!g_swapChainSlot)
			{
				Log("overlay: no swapchain slot was supplied, so there is nothing to hook. "
				    "Call workshop::SetOverlaySwapChainSlot before InstallOverlay.");
				return false;
			}

			// The slot lives past the end of the host's initialized data, so it reads 0
			// until the engine creates the swapchain. Wait for it rather than assuming.
			IDXGISwapChain* swapChain = nullptr;
			const DWORD deadline = GetTickCount() + 60000;
			for (;;)
			{
				if (Readable(g_swapChainSlot, sizeof(void*)))
				{
					void* held = *g_swapChainSlot;
					if (LooksLikePointer((DWORD)(DWORD_PTR)held)
					    && Readable(held, sizeof(void*)))
					{
						swapChain = (IDXGISwapChain*)held;
						break;
					}
				}

				if (GetTickCount() > deadline)
				{
					Log("overlay: the game never published a swapchain, so there is no "
					    "overlay. The slot stayed empty for 60 seconds.");
					return false;
				}
				Sleep(25);
			}

			// The vtable is the one in dxgi.dll, shared by every swapchain in the
			// process. Check it before writing to it.
			void** vtable = *(void***)swapChain;
			if (!Readable(vtable, sizeof(void*) * (SlotResizeBuffers + 1)))
			{
				Log("overlay: the swapchain at 0x%08X has no readable vtable",
				    (unsigned)(DWORD_PTR)swapChain);
				return false;
			}

			Log("overlay: using the game's own swapchain 0x%08X, vtable 0x%08X",
			    (unsigned)(DWORD_PTR)swapChain, (unsigned)(DWORD_PTR)vtable);

			bool ok = false;
			void* present = nullptr;
			void* resize = nullptr;
			if (PatchSlot(vtable, SlotPresent, (void*)&PresentHook, &present, "Present"))
			{
				g_originalPresent = (PresentFn)present;
				ok = true;

				// Worth saying, because it tells us whether another hook got there
				// first and therefore who we are chained behind.
				HMODULE owner = NULL;
				if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
				            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				        (LPCWSTR)present, &owner)
				    && owner)
				{
					wchar_t name[MAX_PATH] = { 0 };
					GetModuleFileNameW(owner, name, MAX_PATH);
					Log("overlay: the Present we chain to lives in %S", name);
				}

				if (PatchSlot(vtable, SlotResizeBuffers, (void*)&ResizeBuffersHook, &resize, "ResizeBuffers"))
					g_originalResizeBuffers = (ResizeBuffersFn)resize;
				else
					Log("overlay: Present is hooked but ResizeBuffers is not, so a "
					    "resolution change will leave the overlay blank");
			}

			return ok;
		}

		// InstallOverlay is called from a plugin's DllMain, and this waits minutes for
		// the engine to publish its swapchain and calls LoadLibrary on the way, neither
		// of which is allowed under the loader lock. So it goes on its own thread.
		DWORD WINAPI HookThread(LPVOID)
		{
			if (HookGameSwapChain())
			{
				g_installed = true;
				Log("overlay: swapchain hooked, waiting for the first frame");
			}
			else
			{
				Log("overlay: not installed, the in-game panels are unavailable");
			}
			return 0;
		}

	} // namespace

	namespace
	{
		// <game dir>\AlBhedWorkshop\overlay_off, presence only, contents ignored.
		//
		// This exists because the overlay is the one part of the kit that has to share
		// the swapchain with whatever else is injected into the process, and when that
		// goes wrong it takes the whole game down before anything can be logged. A file
		// the user can create and delete splits "the kit is broken" from "the overlay is
		// broken" in one run, with no rebuild.
		bool OverlayTurnedOffByFile()
		{
			wchar_t path[MAX_PATH];
			if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0)
				return false;

			int cut = -1;
			for (int i = 0; path[i]; ++i)
				if (path[i] == L'\\' || path[i] == L'/')
					cut = i;
			if (cut < 0)
				return false;
			path[cut] = 0;

			wchar_t marker[MAX_PATH];
			_snwprintf_s(marker, _countof(marker), _TRUNCATE,
			    L"%s\\AlBhedWorkshop\\overlay_off", path);
			return GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES;
		}

		// Who else is in here fighting over the swapchain. Worth saying out loud,
		// because a Present hook that works alone can still recurse forever next to
		// another one, and then there is no log to read afterwards.
		void LogSwapchainNeighbours()
		{
			static const wchar_t* const known[] = {
				L"gameoverlayrenderer.dll", // Steam
				L"GameOverlayRenderer.dll",
				L"RTSSHooks.dll",           // RivaTuner, so MSI Afterburner
				L"d3d11_nvngx.dll",
				L"NvCamera64.dll",
				L"nvngx_dlss.dll",
				L"DiscordHook.dll",
				L"overlay.dll",
				L"GamePP.dll",
				L"EOSOVH-Win32-Shipping.dll", // Epic
			};

			int found = 0;
			for (int i = 0; i < (int)(sizeof(known) / sizeof(known[0])); ++i)
			{
				if (GetModuleHandleW(known[i]))
				{
					Log("overlay: ANOTHER OVERLAY IS IN THIS PROCESS: %S. Two Present "
					    "hooks can recurse into each other, and that ends as a stack "
					    "overflow with nothing in the log.",
					    known[i]);
					++found;
				}
			}
			if (!found)
				Log("overlay: no other known overlay DLL is loaded");
		}

	} // namespace

	void SetOverlaySwapChainSlot(void* const* slot)
	{
		g_swapChainSlot = slot;
	}

	bool InstallOverlay()
	{
		static bool started = false;
		if (started)
			return true;
		started = true;

		if (OverlayTurnedOffByFile())
		{
			Log("overlay: OFF, because AlBhedWorkshop\\overlay_off exists. Delete that "
			    "file to get the overlay back. Everything else in the kit still runs, so "
			    "if the game is stable like this the overlay is what was killing it.");
			started = false;
			return false;
		}

		LogSwapchainNeighbours();

		HANDLE thread = CreateThread(NULL, 0, HookThread, NULL, 0, NULL);
		if (!thread)
		{
			Log("overlay: CreateThread failed, GetLastError=%lu", GetLastError());
			started = false;
			return false;
		}
		CloseHandle(thread);
		return true;
	}

	bool OverlayInstalled()
	{
		return g_installed;
	}

	bool OverlayReady()
	{
		return g_ready;
	}

	int RegisterOverlayPanel(const char* title, OverlayDrawFn draw, void* user)
	{
		if (!title || !draw)
			return 0;

		for (int i = 0; i < MaxPanels; ++i)
		{
			if (g_panels[i].used)
				continue;
			Panel& panel = g_panels[i];
			strncpy_s(panel.title, sizeof(panel.title), title, _TRUNCATE);
			panel.draw = draw;
			panel.user = user;
			panel.open = true;
			panel.used = true;
			++g_panelCount;
			return i + 1;
		}

		Log("overlay: no room for the panel \"%s\", %d is the limit", title, MaxPanels);
		return 0;
	}

	void UnregisterOverlayPanel(int panel)
	{
		if (panel < 1 || panel > MaxPanels || !g_panels[panel - 1].used)
			return;
		memset(&g_panels[panel - 1], 0, sizeof(Panel));
		--g_panelCount;
	}

	void SetOverlayPanelOpen(int panel, bool open)
	{
		if (panel < 1 || panel > MaxPanels)
			return;
		g_panels[panel - 1].open = open;
	}

	bool OverlayPanelOpen(int panel)
	{
		if (panel < 1 || panel > MaxPanels)
			return false;
		return g_panels[panel - 1].used && g_panels[panel - 1].open;
	}

	void SetOverlayVisible(bool visible)
	{
		g_visible = visible;
	}

	bool OverlayVisible()
	{
		return g_visible;
	}

	void ToggleOverlay()
	{
		g_visible = !g_visible;
	}

	void SetOverlayToggleKey(int virtualKey)
	{
		g_toggleKey = virtualKey;
	}

	int OverlayToggleKey()
	{
		return g_toggleKey;
	}

	bool OverlayWantsInput()
	{
		if (!g_ready || !g_visible)
			return false;
		const ImGuiIO& io = ImGui::GetIO();
		return io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput;
	}

	void NoteOverlayGameThread()
	{
		g_gameThread = GetCurrentThreadId();
	}

	bool OverlayOnGameThread()
	{
		return g_gameThread != 0 && GetCurrentThreadId() == g_gameThread;
	}

	const char* OverlayStatus()
	{
		if (!g_installed)
			return g_initFailed ? "install failed" : "installing";
		if (!g_ready)
			return "hooked, no frame yet";

		_snprintf(g_status, sizeof(g_status) - 1,
		    "%s, %d panel%s, %lu frames, present thread %lu%s",
		    g_visible ? "visible" : "hidden", g_panelCount, g_panelCount == 1 ? "" : "s",
		    g_frames, g_presentThread,
		    (g_gameThread && g_presentThread == g_gameThread) ? " (game thread)" : "");
		g_status[sizeof(g_status) - 1] = 0;
		return g_status;
	}

	void LogOverlay()
	{
		Log("overlay : %s", OverlayStatus());
	}

} // namespace workshop
