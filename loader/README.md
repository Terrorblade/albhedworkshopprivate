# Al Bhed Workshop loader

A proxy DLL that gets our code into FINAL FANTASY X HD Remaster, plus a plugin host so the mod
itself can be rebuilt and reloaded without ever touching the proxy again.

**Status: tested against the real game and working end to end.** See the test results below.

## Install

```
copy build\dinput8.dll  "G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\"
```

Launch the game once and the loader creates `AlBhedWorkshop\` next to `FFX.exe` containing `loader.ini`,
`pilgrimage_loader.log` and an empty `plugins\`. Drop plugin DLLs into `plugins\`, and every `*.dll`
there is loaded.

## Uninstall

Delete `dinput8.dll` from the game folder. That is the whole uninstall - nothing else was modified,
no registry keys, no changes to `FFX.exe`. The `AlBhedWorkshop\` folder can be deleted too. To keep the
proxy but disable the mod, set `Enabled=0` in `AlBhedWorkshop\loader.ini`. Forwarding keeps working so the
game is unaffected.

Steam's *Verify integrity of game files* leaves `dinput8.dll` alone (it is not a game file), which is
one more reason this beats editing the exe.

## Build

```
build.bat             both proxy variants into build\
build.bat dinput8     just the primary
build_plugin.bat      the example plugin
```

Needs an x86 MSVC toolchain. Verified with Visual Studio 18 Community, MSVC 14.51.36231, via
`vcvars32.bat`. `CMakeLists.txt` is an alternative (`cmake -S . -B build-x86 -A Win32`).

## Why `dinput8.dll`

The proxy name has to be a DLL `FFX.exe` statically imports, that is **not** a KnownDLL, and that the
game does not already ship locally. The real KnownDLLs list on a Windows 10 machine eliminates most
candidates outright - `IMM32`, `ole32`, `OLEAUT32`, `ADVAPI32`, `SHELL32`, `SHLWAPI`, `USER32` and
`KERNEL32` are all KnownDLLs, so a copy in the game folder is simply ignored. The game ships
`fmodex`, `fmod_event`, `iggy_w32`, `msvcp110`, `msvcr110` and `steam_api` itself, so those are out
too (and Steam would restore them).

Of what remains, the deciding factor is **export surface**, because once our DLL owns that base name
every consumer in the process resolves through us, not just `FFX.exe`:

| candidate | exports | verdict |
|---|---|---|
| **dinput8.dll** | **6**, no ordinals | **chosen** |
| xinput9_1_0.dll | 5 | Steam Input and controller remappers hook XInput heavily |
| **version.dll** | **17**, no ordinals | **fallback**, built as `build\version.dll` |
| d3d11.dll | 51 | ReShade / overlay territory |
| winmm.dll | 193, 1 ordinal-only | too large to forward safely, and fmodex may use it |
| dbghelp.dll | 259, 19 ordinal-only | too large |

`dinput8` also loads during process init (static import), `DirectInput8Create` is genuinely called by
the game so there is a deterministic later hook point for free, and it collides with none of
ReShade's install names or the Steam overlay.

## How the forwarding works, and why not a linker forwarder

The textbook approach is a `.def` full of forwarders (`DirectInput8Create = dinput8.DirectInput8Create`).
**That cannot work for a proxy**: a forwarder names its target module by *base name*, our module *is*
`dinput8`, so the loader resolves it back to us and recurses. The common workaround ships a renamed
copy of the Microsoft DLL. We avoid shipping and never servicing a Microsoft binary.

Instead each export is a `__declspec(naked)` stub whose body is `jmp dword ptr [g_real_X]`, with the
real DLL loaded from `GetSystemDirectoryW()` on first use. Because the thunk touches no stack slot
and no register, it is **calling-convention agnostic**: `__stdcall` arg cleanup, `__cdecl`, HRESULT in
EAX, a float in ST0, a struct return through the hidden pointer - all pass through untouched and the
real function returns straight to the game's caller. Typed C wrappers are where proxy DLLs usually
break, because one wrong `__stdcall` argument count silently corrupts the caller's stack.

The real DLL is resolved **lazily, on the first forwarded call**, deliberately not from `DllMain` and
not pre-warmed from the bootstrap thread. `DllMain` runs under the loader lock during process
initialisation, where `LoadLibrary` is the classic deadlock.

## Verified test results

Run on the shipped `FFX.exe` (build stamp 2026-09-10), Steam running, `dinput8.dll` in the game
folder and the example plugin in `AlBhedWorkshop\plugins\`:

```
[23:28:26.331] === Al Bhed Workshop loader (dinput8.dll proxy) ===
[23:28:26.331] host exe   : ...\FINAL FANTASY FFX&FFX-2 HD Remaster\FFX.exe
[23:28:26.332] config     : Enabled=1 DelayMs=0 HostExe=FFX.exe
[23:28:26.334]   loaded plugin example_plugin.dll -> base 0x72A70000
[23:28:26.334] plugin scan done: 1 loaded, 0 failed
[23:28:26.966] forwarding to real DLL C:\Windows\system32\dinput8.dll (base 0x73B90000)
[23:28:26.967]   all forwarded exports resolved
```

- Proxy loaded, host filter matched, plugin loaded, all 6 exports resolved.
- The real DLL came up at a **different base** (`0x73B90000`) from ours, proving the lazy
  full-path `LoadLibraryW` gets the system DLL and does not resolve back to us.
- The logged path reads `C:\Windows\system32` because that is what `GetSystemDirectoryW` returns in a
  32-bit process, and WOW64 file-system redirection silently serves the 32-bit `SysWOW64` copy. Correct
  behaviour, do not "fix" it to a hardcoded SysWOW64 path.
- Plugin hooked `FFXApplication::animate` (vtable slot +0x10) and ran every frame past frame 300
  while the game loaded from the logos into real content (304 MB -> 628 MB resident), returning the
  original result unchanged each time. `this` matched the app singleton exactly.

### Two things the test corrected

**1. `OptionalHeader.ImageBase` is rewritten in memory.** The plugin's first layout self-check
compared the in-memory `ImageBase` against the on-disk `0x00400000` and so **rejected every launch**.
The Windows loader updates that field to the address the image was actually relocated to, so at
runtime it equals `GetModuleHandle(NULL)`. Measured: `in-memory ImageBase=0x00510000`. Never validate
it against the file value. The vtable slot contents are the correct check - they are relocated
pointers, so comparing them to `base + RVA` validates the layout *and* the RVA table. All four slots
matched exactly.

**2. Plugins do not load early.** The bootstrap thread cannot run until process initialisation
releases the loader lock, so a plugin attaches **after** `FFX.exe`'s CRT startup. Measured: the app
singleton at RVA `0x008C9CD8` is **already non-null** when the plugin's `DllMain` runs, meaning
`FFXApplication` is already constructed. Consequence: **hook per-frame or repeating functions, not
one-shot startup functions.** `animate` always fires, but a hook on `initApplication` is a race you will
sometimes lose. If you truly need to be earlier, the proxy's own `DllMain` is the only
guaranteed-early point, with all the loader-lock restrictions that implies.

### ASLR is per-boot, not per-launch

`FFX.exe` has `DllCharacteristics = 0x8140` (`DYNAMIC_BASE`) and ships a full `.reloc`, so it is
relocated - but **both test launches came up at the same base, `0x00510000`**. Windows randomises an
image's base once per boot and reuses it, so a hardcoded VA will appear to work perfectly until the
machine reboots. Always compute `GetModuleHandle(NULL) + RVA`. Every address in the plugin source is
stored as an RVA for this reason.

## Al Bhed Workshop, the shared game library

Everything known about `FFX.exe` lives in `workshop\` and is linked into each plugin DLL as a
static library. Addresses, structure layouts, the resolved API, the per-frame hook point, the
walkmesh binding, the render probes. A plugin never re-derives any of it, and a second plugin
gets the lot by linking `build\AlBhedWorkshop.lib`.

It has two halves. `workshop\` is generic Win32 modding plumbing that knows nothing about FFX:
logging, the host module and its guards, and the detour engine. `ffx\` is the game.

`workshop\README.md` lists every header and carries the startup contract a plugin follows. The
short version:

```c
workshop::OpenLog(L"my_plugin.log");
workshop::BindHostModule();
if (!ffx::VerifyLayout()) return;   // wrong build, hook nothing
ffx::BindApi();
ffx::HookAnimate(&MyAnimate);
```

Build it with `build_workshop.bat`. `build_pilgrimage.bat` calls that first, so you rarely need to.

**Two rules the library exists to enforce.** Addresses live in exactly one file,
`workshop\include\ffx\Addresses.h`, and nothing else in the project may contain a literal
address. And no plugin hooks anything until `ffx::VerifyLayout()` has passed, because these
addresses are valid for one build of one game and patching the wrong build is worse than not
running.

## The clone plugin

`plugins\clone\` is the phase 1 prototype: it spawns extra playable characters and drives one
of them. Build it with `build_pilgrimage.bat`, copy the DLL into `AlBhedWorkshop\plugins\`. It is also the
worked example of a plugin that links workshop, so copy its build script when starting a new one.

| key | what it does |
|---|---|
| F9 | spawn a clone next to player 1, up to 8 at once |
| F10 | despawn the one you are driving, shift+F10 despawns all |
| F4 | hand the arrow keys to the next clone |
| arrow keys | walk the clone that holds the input focus |
| shift | run |
| F11 | show or hide the control window |
| F2 | host a session on the configured UDP port |
| F3 | join a host on 127.0.0.1, shift+F3 disconnects |
| F7 | cycle which chr id F9 spawns, shift+F7 walks back |
| F8 | dump a character field diff against the real player |
| F5 / F6 | render diagnostics, see `workshop\include\ffx\GfxContext.h` first |

It logs to `AlBhedWorkshop\pilgrimage_together.log`.

**What it does not reimplement, which is most of it.** Movement in this game is already fully
per-character: everything below the input layer takes a CHR pointer and reads no player global. So the
plugin allocates a second CHR, writes `m_speed` and `m_moveDir` into it each frame, sets `m_flags1`
bit `0x400`, and the engine turns it, animates it, moves it, collides it and clamps it to the ground
on its own. The research behind that is `..\reversing\PHASE1_CLONE.md`, which is the place to look
before changing anything here.

**The one line you must not delete.** `FFX_Ch_BindChrData`, reached from inside `FFX_Ch_Allocate`,
writes the `g_ffxTidusChr` one-slot cache unconditionally for any CHR whose CHRDATA name is `c001` or
`c101`. Allocating a second Tidus silently repoints it at the clone, and `FFX_Ch_Dispose` nulls both
that cache and player 1's pad binding when it disposes whatever the cache points at. So `SpawnClone`
saves the pointer across the allocation and puts it back. Without it, despawning the clone takes
player 1's controls away with it. That is the only single-instance assumption in the entire path.

**Three behaviours that are correct but will look wrong.** The clone is not "the player" for walkmesh
passability, so it gets blocked by player-only passages and walks through places Tidus cannot. It has
no ATEL actor, so it passes through every line and box trigger without firing one. And the camera
frames `g_ffxControlledChr`, so the clone can walk off screen. All three are the right trade for phase
1 and all three are explained in `PHASE1_CLONE.md`.

**Known limitation: the arrow keys are world-relative, not camera-relative.** Up on the keyboard is
not up on the screen. The control window has yaw bias buttons to dial the offset in by hand per map
while that is being fixed properly.

### Why the UI is a Win32 window and not ImGui

ImGui inside this game needs an `IDXGISwapChain::Present` hook, a window-procedure hook, a D3D11
backend and the ImGui sources vendored into the repo. Worth doing, and nothing in the plugin blocks
it, but for getting the clone moving it is all risk and no information: when the game crashes you
cannot tell whether the clone logic or the renderer hook did it. So the control window is plain
user32 on its own thread. It cannot touch the game's renderer at all.

The threading rule that makes it safe: **the UI thread never calls a game function and never
dereferences a CHR.** It reads published state and sets request flags. The `animate` hook, on the
game's own thread, is the only code that calls into FFX. When ImGui does go in, it raises the same
requests against the same shared struct, so only the drawing changes.

## Files

| file | purpose |
|---|---|
| `workshop_proxy.cpp` | the proxy; both `dinput8` and `version` targets in one file |
| `build.bat` | builds the proxy variants |
| `build_plugin.bat` | builds the example plugin |
| `workshop/` | the shared FFX knowledge library, linked into every plugin. See its own README. |
| `build_workshop.bat` | builds `build\AlBhedWorkshop.lib`; called by `build_pilgrimage.bat` |
| `CMakeLists.txt` | CMake alternative |
| `plugins_example/example_plugin.cpp` | the minimal reference plugin, deliberately standalone with no workshop dependency, so it stays readable as a single file |
| `plugins/clone/` | the phase 1 clone mod, see above. `CloneMod.cpp` explains the layout. |
| `build_pilgrimage.bat` | builds workshop and then the clone plugin |
| `EXE_EDIT_NOTES.md` | the exe-edit route, researched and **not recommended** - no room to grow the import directory, and the Authenticode cert sits at the file tail |
