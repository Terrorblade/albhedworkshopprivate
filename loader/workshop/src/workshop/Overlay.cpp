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
#include "workshop/OverlayHost.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---------------------------------------------------------------------------
// TWO HALVES IN ONE FILE.
//
// The top half is the OWNER: the ImGui context, the swapchain hook, the backends, the
// window subclass and the panel table. Exactly one module in the process runs it.
//
// The bottom half is the public API, and every function in it is a dispatcher. Call
// RegisterOverlayPanel in a plugin and it goes through the owner's function table.
// Call it in the owner and it lands on the owner's implementation directly. The caller
// does not need to know which it is, which is the whole point: a plugin is written as
// though it had the overlay to itself.
//
// workshop/OverlayHost.h is where the ownership claim and the ImGui handoff live, and
// its header comment explains why it has to work this way.
// ---------------------------------------------------------------------------

namespace workshop
{

	// =======================================================================
	// THE OWNER
	// =======================================================================

	namespace
	{

		// IDXGISwapChain vtable slots. IUnknown 0-2, IDXGIObject 3-6,
		// IDXGIDeviceSubObject 7, then the swapchain's own.
		const int SlotPresent = 8;
		const int SlotResizeBuffers = 13;

		typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
		typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

		const int MaxPanels = 32;

		struct Panel
		{
			char title[64];
			OverlayDrawFn draw;
			void* user;
			OverlayPanelKind kind;
			bool open;
			bool used;
		};

		Panel g_panels[MaxPanels];
		int g_panelCount = 0;

		PresentFn g_originalPresent = nullptr;

		// Where the host keeps its IDXGISwapChain *. Supplied by the game layer so this
		// file needs no game addresses of its own. For FFX that is
		// ffx::Rva::PhyreD3DSwapChain, the PhyreEngine D3D context singleton + 0x74.
		//
		// volatile because the hook thread spins on it while a plugin publishes it from
		// another thread, and without it the compiler is free to hoist the load out of
		// the wait loop and spin on a stale null forever.
		void* const* volatile g_swapChainSlot = nullptr;
		ResizeBuffersFn g_originalResizeBuffers = nullptr;

		IDXGISwapChain* g_swapChain = nullptr;
		ID3D11Device* g_device = nullptr;
		ID3D11DeviceContext* g_context = nullptr;
		ID3D11RenderTargetView* g_target = nullptr;
		ImGuiContext* g_imgui = nullptr;
		HWND g_window = NULL;
		WNDPROC g_originalWndProc = nullptr;

		bool g_installed = false;
		bool g_ready = false;
		bool g_initFailed = false;
		bool g_visible = false;
		int g_toggleKey = VK_F11;
		DWORD g_gameThread = 0;
		DWORD g_presentThread = 0;
		unsigned long g_frames = 0;
		char g_status[240] = "not installed";

		DWORD SelfModuleBase()
		{
			HMODULE self = NULL;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			        (LPCWSTR)&SelfModuleBase, &self))
			{
				return 0;
			}
			return (DWORD)(DWORD_PTR)self;
		}

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
					g_visible = !g_visible;
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

			// THE ONE CONTEXT IN THE PROCESS. Every plugin's ImGui is pointed at this
			// by AdoptOverlayHost, so there is one font atlas, one input state and one
			// frame, however many modules draw into it.
			g_imgui = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr; // no stray ini in the game folder
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			ImGui::StyleColorsDark();

			if (!ImGui_ImplWin32_Init(g_window) || !ImGui_ImplDX11_Init(g_device, g_context))
			{
				ImGui::DestroyContext();
				g_imgui = nullptr;
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
			Log("overlay: up on hwnd %p, %ux%u, present thread %lu, ImGui %s, context 0x%08X",
			    (void*)g_window, desc.BufferDesc.Width, desc.BufferDesc.Height,
			    GetCurrentThreadId(), ImGui::GetVersion(), (unsigned)(DWORD_PTR)g_imgui);
			return true;
		}

		// ---------------------------------------------------------------------------
		// The shell.
		//
		// One window with a tab per in-shell panel, rather than a window per panel. A
		// plugin that wants a window of its own asks for PanelOwnWindow and gets called
		// outside this entirely.
		// ---------------------------------------------------------------------------

		void DrawShellOwnTab()
		{
			ImGui::Text("%d panel%s registered", g_panelCount, g_panelCount == 1 ? "" : "s");
			if (g_panelCount == 0)
				ImGui::TextDisabled("Nothing has registered a panel. Either no plugin is "
				                    "loaded or one refused to share this ImGui.");

			ImGui::Separator();

			// THE WAY BACK. Closing a tab sets its panel closed, and without this list
			// there would be nothing left on screen that could reopen it.
			for (int i = 0; i < MaxPanels; ++i)
			{
				Panel& panel = g_panels[i];
				if (!panel.used)
					continue;
				ImGui::PushID(i);
				ImGui::Checkbox(panel.title, &panel.open);
				ImGui::SameLine();
				ImGui::TextDisabled(panel.kind == PanelOwnWindow ? "(own window)" : "(tab)");
				ImGui::PopID();
			}

			ImGui::Separator();
			ImGui::Text("%.1f fps, frame %lu", ImGui::GetIO().Framerate, g_frames);
			ImGui::TextUnformatted(OverlayOnGameThread()
			        ? "drawing on the game thread"
			        : "drawing off the game thread");
			if (g_toggleKey != 0)
				ImGui::Text("toggle key: virtual key 0x%02X", (unsigned)g_toggleKey);
			else
				ImGui::TextUnformatted("toggle key: none, a plugin drives the toggle");
		}

		void DrawShell()
		{
			ImGui::SetNextWindowSize(ImVec2(860, 660), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);

			// NO CLOSE BUTTON, DELIBERATELY. The shell is the only route back to an
			// in-shell panel, so a close button on it is a dead end: press it and every
			// tab goes with it and nothing is left to bring them back. The toggle key
			// is how the overlay is dismissed.
			if (ImGui::Begin("Al Bhed Workshop", nullptr, ImGuiWindowFlags_NoCollapse))
			{
				if (ImGui::BeginTabBar("##albhed",
				        ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll))
				{
					for (int i = 0; i < MaxPanels; ++i)
					{
						Panel& panel = g_panels[i];
						if (!panel.used || panel.kind != PanelInShell || !panel.open || !panel.draw)
							continue;

						ImGui::PushID(i);
						if (ImGui::BeginTabItem(panel.title, &panel.open))
						{
							panel.draw(panel.user);
							ImGui::EndTabItem();
						}
						ImGui::PopID();
					}

					// LAST, so a plugin's own tab is what the overlay opens on. Nobody
					// wants to land on the diagnostics.
					if (ImGui::BeginTabItem("Workshop"))
					{
						DrawShellOwnTab();
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}
			}
			ImGui::End();
		}

		void DrawOwnWindowPanels()
		{
			for (int i = 0; i < MaxPanels; ++i)
			{
				Panel& panel = g_panels[i];
				if (!panel.used || panel.kind != PanelOwnWindow || !panel.open || !panel.draw)
					continue;

				// Nothing is opened for it. The callback calls Begin and End itself,
				// which is the whole difference between the two kinds.
				ImGui::PushID(i);
				panel.draw(panel.user);
				ImGui::PopID();
			}
		}

		void DrawFrame()
		{
			if (!EnsureTarget())
				return;

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ImGui::GetIO().MouseDrawCursor = true; // the game hides the OS cursor

			DrawShell();
			DrawOwnWindowPanels();

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

		// SAID ONCE, BECAUSE IT DECIDES WHETHER A PANEL MAY TOUCH THE ENGINE.
		//
		// If Present runs on the game's own step thread then a panel can call engine
		// code directly, and a list rebuilt on a step cannot be half written while the
		// UI reads it. If it does not, both of those are races and every panel has to
		// go through OverlayOnGameThread. That is too important to assume, and it
		// cannot be answered when the overlay comes up because the step thread id is
		// not known until the first step, so it is answered on the first frame after.
		void SayWhichThreadOnce()
		{
			static bool said = false;
			if (said || !g_ready || g_gameThread == 0)
				return;
			said = true;

			if (g_presentThread == g_gameThread)
				Log("overlay: Present runs on the game's own step thread (%lu), so a "
				    "panel may call engine code directly and a list rebuilt on a step "
				    "cannot be read half written.",
				    g_gameThread);
			else
				Log("overlay: PRESENT IS THREAD %lu AND THE GAME STEPS ON %lu. A panel "
				    "must check OverlayOnGameThread before calling engine code, and "
				    "anything the step thread rebuilds can be read half written by a "
				    "panel. Treat shared lists as needing a handoff.",
				    g_presentThread, g_gameThread);
		}

		HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
		{
			g_presentThread = GetCurrentThreadId();
			if (EnsureImGui(swapChain))
			{
				SayWhichThreadOnce();
				if (g_visible)
					DrawFrame();
			}
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

			// WAIT FOR THE SLOT RATHER THAN FAILING ON IT. The loader claims the overlay
			// and starts this thread before it has loaded a single plugin, and the slot
			// can only come from a plugin, because only a plugin knows the game. So an
			// empty slot here is the normal startup order and not a mistake. It used to
			// be a hard failure, which was right only while a plugin was the one
			// installing the overlay.
			IDXGISwapChain* swapChain = nullptr;
			bool sawSlot = false;
			const DWORD deadline = GetTickCount() + 60000;
			for (;;)
			{
				void* const* slot = g_swapChainSlot;
				if (slot)
				{
					sawSlot = true;
					if (Readable(slot, sizeof(void*)))
					{
						void* held = *slot;
						if (LooksLikePointer((DWORD)(DWORD_PTR)held)
						    && Readable(held, sizeof(void*)))
						{
							swapChain = (IDXGISwapChain*)held;
							break;
						}
					}
				}

				if (GetTickCount() > deadline)
				{
					if (!sawSlot)
						Log("overlay: no plugin ever said where the game keeps its "
						    "swapchain, so there was nothing to hook. A game layer has "
						    "to call workshop::SetOverlaySwapChainSlot, which for FFX is "
						    "ffx::PointOverlayAtSwapChain. Waited 60 seconds.");
					else
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
				// first and therefore who we are chained behind. This line is also how
				// the double overlay bug was finally proved: two of our own modules
				// each named the other here.
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

		// InstallOverlay can be called from DllMain, and this waits minutes for the
		// engine to publish its swapchain and calls LoadLibrary on the way, neither of
		// which is allowed under the loader lock. So it goes on its own thread.
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

		// ---------------------------------------------------------------------------
		// The owner's implementation, in the shape the function table needs.
		//
		// __cdecl on every one of them and no ImGui type in any signature, so a module
		// that has never seen imgui.h can still hold this table.
		// ---------------------------------------------------------------------------

		void* __cdecl OwnContext(void)
		{
			return g_imgui;
		}

		void __cdecl OwnAllocators(ImGuiMemAllocFunc* alloc, ImGuiMemFreeFunc* release, void** user)
		{
			// Whatever this module's ImGui is allocating through, which unless somebody
			// has changed it is this module's own malloc. That is the point: a plugin
			// adopting this table ends up on the same heap, so a vector one of them
			// grows can be freed by the other.
			ImGui::GetAllocatorFunctions(alloc, release, user);
		}

		int __cdecl OwnRegisterPanel(const char* title, OverlayHostDrawFn draw, void* user, int kind)
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
				panel.kind = (kind == PanelOwnWindow) ? PanelOwnWindow : PanelInShell;
				panel.open = true;

				// LAST, AND BEHIND A BARRIER. The present thread walks this table every
				// frame and keys on used, so everything else has to be in place and
				// visible before used turns on, or a panel can be drawn with half its
				// fields written.
				MemoryBarrier();
				panel.used = true;
				++g_panelCount;
				return i + 1;
			}

			Log("overlay: no room for the panel \"%s\", %d is the limit", title, MaxPanels);
			return 0;
		}

		void __cdecl OwnUnregisterPanel(int handle)
		{
			if (handle < 1 || handle > MaxPanels)
				return;
			Panel& panel = g_panels[handle - 1];
			if (!panel.used)
				return;

			// Off first, so the present thread stops walking into it, then clear.
			panel.used = false;
			MemoryBarrier();
			memset(&panel, 0, sizeof(Panel));
			--g_panelCount;
		}

		void __cdecl OwnSetPanelOpen(int handle, bool open)
		{
			if (handle < 1 || handle > MaxPanels)
				return;
			g_panels[handle - 1].open = open;
		}

		bool __cdecl OwnPanelOpen(int handle)
		{
			if (handle < 1 || handle > MaxPanels)
				return false;
			return g_panels[handle - 1].used && g_panels[handle - 1].open;
		}

		bool __cdecl OwnReady(void)
		{
			return g_ready;
		}

		bool __cdecl OwnVisible(void)
		{
			return g_visible;
		}

		void __cdecl OwnSetVisible(bool visible)
		{
			g_visible = visible;
		}

		void __cdecl OwnToggle(void)
		{
			g_visible = !g_visible;
		}

		bool __cdecl OwnWantsInput(void)
		{
			if (!g_ready || !g_visible)
				return false;
			const ImGuiIO& io = ImGui::GetIO();
			return io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput;
		}

		void __cdecl OwnSetToggleKey(int virtualKey)
		{
			g_toggleKey = virtualKey;
		}

		int __cdecl OwnToggleKey(void)
		{
			return g_toggleKey;
		}

		void __cdecl OwnNoteGameThread(void)
		{
			g_gameThread = GetCurrentThreadId();
		}

		bool __cdecl OwnOnGameThread(void)
		{
			return g_gameThread != 0 && GetCurrentThreadId() == g_gameThread;
		}

		void __cdecl OwnSetSwapChainSlot(void* const* slot)
		{
			g_swapChainSlot = slot;
		}

		void __cdecl OwnStatus(char* out, int bytes)
		{
			if (!out || bytes <= 0)
				return;

			if (!g_installed)
			{
				_snprintf_s(out, (size_t)bytes, _TRUNCATE, "%s",
				    g_initFailed ? "install failed" : "installing");
				return;
			}
			if (!g_ready)
			{
				_snprintf_s(out, (size_t)bytes, _TRUNCATE, "%s", "hooked, no frame yet");
				return;
			}

			_snprintf_s(out, (size_t)bytes, _TRUNCATE,
			    "%s, %d panel%s, %lu frames, present thread %lu%s",
			    g_visible ? "visible" : "hidden", g_panelCount, g_panelCount == 1 ? "" : "s",
			    g_frames, g_presentThread,
			    (g_gameThread && g_presentThread == g_gameThread) ? " (game thread)" : "");
		}

	} // namespace

	const OverlayHostApi* OverlayOwnerApi()
	{
		static OverlayHostApi api;

		api.abiVersion = OverlayHostAbiVersion;
		api.imguiVersionNum = (DWORD)IMGUI_VERSION_NUM;
		api.imguiVersion = IMGUI_VERSION;
		api.ownerModuleBase = SelfModuleBase();

		// MEASURED HERE, IN THE OWNER'S OWN TRANSLATION UNIT, which is the only place
		// the answer means anything. A client compares these against its own sizeof and
		// refuses rather than reading one struct through another's layout.
		api.sizeOfImGuiIo = (DWORD)sizeof(ImGuiIO);
		api.sizeOfImGuiStyle = (DWORD)sizeof(ImGuiStyle);
		api.sizeOfImVec2 = (DWORD)sizeof(ImVec2);
		api.sizeOfImVec4 = (DWORD)sizeof(ImVec4);
		api.sizeOfImDrawVert = (DWORD)sizeof(ImDrawVert);
		api.sizeOfImDrawIdx = (DWORD)sizeof(ImDrawIdx);

		api.Context = &OwnContext;
		api.Allocators = &OwnAllocators;

		api.RegisterPanel = &OwnRegisterPanel;
		api.UnregisterPanel = &OwnUnregisterPanel;
		api.SetPanelOpen = &OwnSetPanelOpen;
		api.PanelOpen = &OwnPanelOpen;

		api.Ready = &OwnReady;
		api.Visible = &OwnVisible;
		api.SetVisible = &OwnSetVisible;
		api.Toggle = &OwnToggle;
		api.WantsInput = &OwnWantsInput;
		api.SetToggleKey = &OwnSetToggleKey;
		api.ToggleKey = &OwnToggleKey;

		api.NoteGameThread = &OwnNoteGameThread;
		api.OnGameThread = &OwnOnGameThread;
		api.SetSwapChainSlot = &OwnSetSwapChainSlot;
		api.Status = &OwnStatus;

		return &api;
	}

	// =======================================================================
	// THE CLIENT SIDE
	// =======================================================================

	namespace
	{

		// A panel this module registered with somebody else's overlay. The host holds
		// the thunk below and one of these as its user pointer, rather than the real
		// callback, so this module gets a word in immediately before it draws.
		struct ClientPanel
		{
			OverlayDrawFn draw;
			void* user;
			int handle; // the owner's handle, so Unregister can find this slot again
			bool used;
		};

		ClientPanel g_clientPanels[MaxPanels];
		bool g_refusedLogged = false;

		ClientPanel* TakeClientPanel()
		{
			for (int i = 0; i < MaxPanels; ++i)
				if (!g_clientPanels[i].used)
					return &g_clientPanels[i];
			return nullptr;
		}

		ClientPanel* FindClientPanel(int handle)
		{
			for (int i = 0; i < MaxPanels; ++i)
				if (g_clientPanels[i].used && g_clientPanels[i].handle == handle)
					return &g_clientPanels[i];
			return nullptr;
		}

		// ---------------------------------------------------------------------------
		// WHY A PLUGIN'S PANEL IS WRAPPED.
		//
		// This module's ImGui has to be pointed at the owner's context before the
		// callback runs, and the earliest honest moment is right here. Adopting at
		// registration time cannot work: the owner has no context to share until its
		// first frame, which is long after a plugin registers from DllMain. Adopting
		// per frame from the plugin would mean every plugin author remembering to, and
		// forgetting is a null context dereference rather than a missing window.
		//
		// So the host calls this, on the present thread, inside its frame, and by then
		// the context exists. After the first call it costs a pointer compare.
		// ---------------------------------------------------------------------------
		void __cdecl ClientPanelThunk(void* user)
		{
			ClientPanel* panel = (ClientPanel*)user;
			if (!panel || !panel->draw)
				return;

			if (!OverlayHostAdopted() && !AdoptOverlayHost())
				return;

			panel->draw(panel->user);
		}

		// The table to dispatch through, or null when this module should do the work
		// itself. Null means either that this module owns the overlay or that nothing
		// does, and in the second case behaving the way the kit did before ownership
		// existed is better than having no overlay at all.
		const OverlayHostApi* Remote()
		{
			if (OverlayHostIsSelf())
				return nullptr;
			return OverlayHost();
		}

	} // namespace

	// =======================================================================
	// THE PUBLIC API. Every function below is a dispatcher.
	// =======================================================================

	void SetOverlaySwapChainSlot(void* const* slot)
	{
		const OverlayHostApi* host = Remote();
		if (host)
			host->SetSwapChainSlot(slot);
		else
			OwnSetSwapChainSlot(slot);
	}

	bool InstallOverlay()
	{
		// WHOEVER CALLS THIS IS VOLUNTEERING TO OWN THE PROCESS'S OVERLAY, and only the
		// first caller gets it. Losing is not an error and must not read like one: a
		// plugin written against an older kit still calls this, and the right outcome
		// is that it quietly shares the overlay that is already up.
		if (!PublishOverlayHost())
		{
			if (OverlayHost())
			{
				// Sets up the allocator now and the context as soon as there is one.
				// The panel thunk retries it, so a false here is not fatal.
				AdoptOverlayHost();
				Log("overlay: already owned by another module, so this one is sharing it. "
				    "%s",
				    OverlayHostAdoptStatus());
				return true;
			}
			Log("overlay: ownership could not be established and no other module has it "
			    "either, so this one is carrying on alone.");
		}

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
		const OverlayHostApi* host = Remote();
		// There is no Installed in the table on purpose. Ready is the question a plugin
		// actually means, and a client has no business caring whether somebody else's
		// hook is in place but has not drawn yet.
		return host ? host->Ready() : g_installed;
	}

	bool OverlayReady()
	{
		const OverlayHostApi* host = Remote();
		return host ? host->Ready() : OwnReady();
	}

	int RegisterOverlayPanel(const char* title, OverlayDrawFn draw, void* user, OverlayPanelKind kind)
	{
		const OverlayHostApi* host = Remote();
		if (!host)
			return OwnRegisterPanel(title, draw, user, (int)kind);

		// REFUSE THE OVERLAY, KEEP THE PLUGIN. A version or layout mismatch means this
		// module cannot touch the owner's context without corrupting it, so it gets no
		// UI. Everything else about the plugin, its hooks and its sync and its logging,
		// is unaffected, which is the right half to give up.
		if (!OverlayHostCompatible())
		{
			if (!g_refusedLogged)
			{
				g_refusedLogged = true;
				Log("overlay: THIS PLUGIN HAS NO UI. It cannot share the overlay owner's "
				    "ImGui: %s. The rest of the plugin is running normally.",
				    OverlayHostAdoptStatus());
			}
			return 0;
		}

		if (!title || !draw)
			return 0;

		ClientPanel* slot = TakeClientPanel();
		if (!slot)
		{
			Log("overlay: no room for the panel \"%s\", this module is already holding "
			    "%d", title, MaxPanels);
			return 0;
		}

		slot->draw = draw;
		slot->user = user;
		slot->handle = 0;
		slot->used = true;

		const int handle = host->RegisterPanel(title, &ClientPanelThunk, slot, (int)kind);
		if (handle == 0)
		{
			slot->used = false;
			return 0;
		}
		slot->handle = handle;
		return handle;
	}

	void UnregisterOverlayPanel(int panel)
	{
		const OverlayHostApi* host = Remote();
		if (!host)
		{
			OwnUnregisterPanel(panel);
			return;
		}

		host->UnregisterPanel(panel);
		ClientPanel* slot = FindClientPanel(panel);
		if (slot)
			memset(slot, 0, sizeof(ClientPanel));
	}

	void SetOverlayPanelOpen(int panel, bool open)
	{
		const OverlayHostApi* host = Remote();
		if (host)
			host->SetPanelOpen(panel, open);
		else
			OwnSetPanelOpen(panel, open);
	}

	bool OverlayPanelOpen(int panel)
	{
		const OverlayHostApi* host = Remote();
		return host ? host->PanelOpen(panel) : OwnPanelOpen(panel);
	}

	void SetOverlayVisible(bool visible)
	{
		const OverlayHostApi* host = Remote();
		if (host)
			host->SetVisible(visible);
		else
			OwnSetVisible(visible);
	}

	bool OverlayVisible()
	{
		const OverlayHostApi* host = Remote();
		return host ? host->Visible() : OwnVisible();
	}

	void ToggleOverlay()
	{
		const OverlayHostApi* host = Remote();
		if (host)
			host->Toggle();
		else
			OwnToggle();
	}

	void SetOverlayToggleKey(int virtualKey)
	{
		const OverlayHostApi* host = Remote();
		if (host)
			host->SetToggleKey(virtualKey);
		else
			OwnSetToggleKey(virtualKey);
	}

	int OverlayToggleKey()
	{
		const OverlayHostApi* host = Remote();
		return host ? host->ToggleKey() : OwnToggleKey();
	}

	bool OverlayWantsInput()
	{
		const OverlayHostApi* host = Remote();
		return host ? host->WantsInput() : OwnWantsInput();
	}

	void NoteOverlayGameThread()
	{
		const OverlayHostApi* host = Remote();
		if (host)
			host->NoteGameThread();
		else
			OwnNoteGameThread();
	}

	bool OverlayOnGameThread()
	{
		const OverlayHostApi* host = Remote();
		return host ? host->OnGameThread() : OwnOnGameThread();
	}

	const char* OverlayStatus()
	{
		const OverlayHostApi* host = Remote();
		if (!host)
		{
			OwnStatus(g_status, (int)sizeof(g_status));
			return g_status;
		}

		// Into this module's own buffer, because the table fills a caller supplied one
		// rather than handing back a pointer into the owner's statics. A client reading
		// another module's buffer is a lifetime problem for no benefit.
		static char remote[240] = "";
		host->Status(remote, (int)sizeof(remote));
		return remote;
	}

	void LogOverlay()
	{
		if (OverlayHostIsSelf())
			Log("overlay : %s, owned here", OverlayStatus());
		else if (OverlayHost())
			Log("overlay : %s, %s", OverlayStatus(), OverlayHostAdoptStatus());
		else
			Log("overlay : %s, no owner", OverlayStatus());
	}

} // namespace workshop
