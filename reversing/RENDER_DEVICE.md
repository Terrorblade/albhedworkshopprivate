# The D3D11 device, swap chain and present path

Written 2026-10-12 for Project Zanarkand's D3D12 layer (PROGRESS 6.1.1 and 6.1.4 there). Every
claim below was read from FFX.exe in IDA and the names are applied in the IDB. Addresses are VAs at
the preferred base 0x400000, see README.md for the ASLR slide.

## 1. Where the device lives

`g_ffxPhyreD3DContext 0xC94F00` is the one renderer object, a PhyreEngine `PRenderingD3D11`
instance, not a pointer. The fields this note uses:

| offset | what |
|---|---|
| +0x6C | `ID3D11Device*` |
| +0x70 | `ID3D11DeviceContext*`, the immediate context |
| +0x74 | `IDXGISwapChain*` |
| +0x78 | the `DXGI_SWAP_CHAIN_DESC` the chain was created from, kept and updated on resize |
| +0xB4 | the `D3D_FEATURE_LEVEL` the device came up at |
| +0x131C | the vsync byte, see section 4 |
| +0x1320 | pointer to the window record `g_phyreWindowRecord 0xCCA310` (`hwnd` at +0, the constant 1 at +4, the fullscreen byte `app+0x298` at +8, the vsync byte `app+0x288` at +9). Corrected 2026-10-27, +4 was called the msaa count, see `COMMAND_LINE.md` section 6 |
| +0x1334, +0x1338 | current back buffer width and height |
| +0x133C | `IDXGIFactory*`, from `GetParent` on the swap chain |

## 2. Creation

`FFX_Phyre_InitDisplay 0x595750 (this, windowRecord*)` stores four floats (0.1, 0.1, 0.8, 0.5,
the clear colour and depth defaults by the look of them), takes `GetClientRect` of the window and
calls `FFX_Phyre_CreateDeviceAndSwapChain 0x595800`, the only caller of
`D3D11CreateDeviceAndSwapChain` in the binary. Arguments: width and height from the client rect,
the window record's +4 as the sample count, the constant 1 for the windowed flag, the byte at +9 of
the record, and the hwnd. Corrected 2026-10-27: record +4 is not the msaa setting, it is the
constant 1 that `Phyre__PApplication__createWindowAndInit` writes at 0x629ADF and 0x629B17, so the
swap chain is always single sampled. The msaa setting (`app+0x284`, from `settings+56` or `-msaa=`)
is read by the render target builder `sub_693D70` instead. The byte at +9 is `app+0x288` and
becomes the initial vsync byte, see section 4.

The function first probes with `D3D11CreateDevice(nullptr, HARDWARE, ..., SDK 7)` with no
requested levels and returns 9 (failure) when the device creation fails or the level is below
10_0. On exactly 10_0, with two settings words (`g_ffxGameSettings+56` and `+72`) both nonzero,
it calls `FFX_Config_SetValue(settings, 14, 0)`, so a feature level 10 card has one setting
forced off. It then fills the swap chain desc at +0x78 in place:

| field | value |
|---|---|
| BufferDesc.Width, Height | the client rect |
| BufferDesc.RefreshRate | 60 / 1 |
| BufferDesc.Format | 28, `DXGI_FORMAT_R8G8B8A8_UNORM` |
| ScanlineOrdering, Scaling | 0, 0 |
| SampleDesc.Count | the sample count argument, 0 becomes 1 and 16 and above become 16. The caller always passes 1 |
| SampleDesc.Quality | 0 |
| BufferUsage | 32, `DXGI_USAGE_RENDER_TARGET_OUTPUT` |
| BufferCount | 1 |
| OutputWindow | the hwnd |
| Windowed | always TRUE, the caller passes the constant 1 |
| SwapEffect | 0, `DXGI_SWAP_EFFECT_DISCARD` |
| Flags | 2, `DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH` |

and calls `D3D11CreateDeviceAndSwapChain(nullptr, HARDWARE, nullptr, 0, {11_0, 10_1, 10_0}, 3,
7, &desc, &swapChain, &device, &level, &context)`. After success it queries `ID3D11Debug` and
`ID3D11InfoQueue` (GUIDs at 0xB31450 and 0xB31460, released at once, a debug build leftover),
calls `GetBuffer(0, IID_ID3D11Texture2D)` (0xB31470), builds the render target and sets it with
`OMSetRenderTargets`, takes `GetParent(IID_IDXGIFactory)` (0xB31480) into +0x133C and calls
`IDXGIFactory::MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER)` at 0x595BB7. That one call
is why Alt+Enter does nothing in this game. The success path returns 0, the Hex-Rays `return 9`
is the merged failure path.

Fullscreen is therefore never an exclusive swap chain. The settings menu's fullscreen mode is a
`ChangeDisplaySettingsA` desktop mode switch plus a popup style window, handled entirely in
`PWindowWin32Base` (MAIN_LOOP.md section 1 and the window notes in Project Zanarkand's
`src/platform/Window.h`).

## 3. Present

`Phyre_RenderD3D11_Present 0x593760`:

```
if (!swapChain) return 0;
swapChain->Present(vsyncByte != 0 ? 1 : 0, 0);
return 1;
```

The two callers are `j_Phyre_RenderD3D11_Present 0x6148B0`, a thunk with no callers of its own,
and `Phyre_RenderThread_ExecuteCommand 0x614C60` at 0x614D89, which runs on
`PhyreEngineRenderThread`, not on the main thread (corrected 2026-10-12 by the thread census,
`THREADS_AND_CLOCKS.md` T1). The main thread queues the Present record through
`Phyre_PRenderer_QueuePresent 0x5B6AF0`, which first waits with no timeout on the event at
renderThread+0x164, then posts the list through `PRendererBase__vf02 0x5B6C50`.
`Phyre_RenderThread_FlushGlobal 0x5BC3F0` at `frameTick 0x627B4A` is not the per frame join, it
only runs when a resize, msaa, gamma, resolution or vsync dirty byte is set (corrected 2026-10-12,
`DISPLAY_LIST.md`). Whether Present can complete a frame late is open. Flags are always 0, so there is no tearing flag and no
`DXGI_PRESENT_DO_NOT_WAIT`.

## 4. The vsync chain, now complete

MAIN_LOOP.md section 7 left this half proven. The whole chain is: the settings menu writes
`g_ffxGameSettings[40]` and sets the dirty flag `app+0x18` (`FFX_EscMenu_CommitVideoSettings`,
0x6FF353), `Phyre__PApplication__frameTick 0x627940` calls `Phyre_RenderD3D11_SetVsync 0x58E170
(setting != 0)` (or 0 while `app+0x1C` is set), the setter stores one byte at renderer+0x131C, and
`Phyre_RenderD3D11_Present` turns it into the `Present` sync interval, 1 or 0.

Corrected 2026-10-27 (`COMMAND_LINE.md`). This section used to say the `-vsync=` argument writes
`g_ffxGameSettings[40]` and that the game starts unsynced until the first frame services the flag.
Both were wrong. The renderer constructor `sub_583580` does zero the byte, but device creation then
sets it: `FFX_Phyre_CreateDeviceAndSwapChain` stores its seventh argument, the window record's
byte +9, into renderer+0x131C at 0x595AE6 (Hex-Rays drops that argument from the decompile, the
disassembly shows the store). That byte is `app+0x288`, which `WinMain` sets to
`settings[40] != 0` and `-vsync=N` then overrides with `N != 1`. So the boot interval is the ini's
VSync, or the argument, and `-vsync=0` means on while `-vsync=1` means off. `SetVsync` is only
reached again when the menu sets `app+0x18` (or `app+0x1D`, for which no setter was found).

## 5. Resize, two entry points

**The window changed size.** `WM_SIZE` sets `app+0x14`, the frame services it through
`PApplication__vf09 0x62C260`, which calls `Phyre_RenderD3D11_ResizeSwapChain 0x599210 (width,
height)`: `OMSetRenderTargets(0, nullptr, nullptr)`, release the colour and depth target wrappers,
`swapChain->ResizeBuffers(BufferCount from the desc (1), width, height, Format from the desc (28),
Flags 2)`, `GetBuffer(0, IID_ID3D11Texture2D)`, rebuild the render target view and the depth target
(`sub_4C40C0`, format at 0xCA347C), `Phyre__PRenderingD3D11__setViewport(0, 0, width, height)`,
store the size at +0x1334 and +0x1338 and in the desc. Nothing here touches the mode.

**The resolution setting changed.** `frameTick` calls `Phyre_RenderD3D11_ResizeTargetToMode
0x58DF00 (width, height)`: `factory->EnumAdapters(0)`, `adapter->EnumOutputs(0)`,
`output->GetDisplayModeList(R8G8B8A8_UNORM, 0)` twice (count, then the array, 28 bytes a mode,
`Phyre_TtyPrintf(2, "number of modes: %d\n")` in between), then a linear scan for the FIRST mode
whose width and height match, and `swapChain->ResizeTarget(&mode)`. Returns 19 when no mode
matches, 0 otherwise. Because the mode list is in DXGI's order, the refresh rate of the picked
mode is whatever DXGI lists first for that size, which is usually the lowest, and nothing reads
it back. On this always windowed swap chain `ResizeTarget` resizes the window, which raises
`WM_SIZE` and the first entry point does the rest.

## 6. What this means for a recreation

The behaviour the oracle and the player can see: one R8G8B8A8 back buffer presented with sync
interval 1 or 0 and no other flags, no exclusive fullscreen ever, Alt+Enter dead, a resize that
rebuilds the back buffer at the window's client size with the same flags, and a resolution change
that goes through the first matching display mode. Project Zanarkand reproduces each of those on
a D3D12 flip model swap chain (which cannot have BufferCount 1, DISCARD, or a multisampled back
buffer, so the msaa count moves to a separate scene target) and records the deviations under
PROGRESS 6.1.1 there.
