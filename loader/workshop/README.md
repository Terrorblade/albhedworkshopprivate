# Al Bhed Workshop

Everything we know about FINAL FANTASY X HD Remaster's `FFX.exe` that is worth reusing, as a
32-bit static library. Plugin DLLs link it instead of carrying their own copy of the addresses,
the structure layouts and the hook plumbing.

Build it with `..\build_workshop.bat`, which drops `build\AlBhedWorkshop.lib`. `build_pilgrimage.bat` calls that
first, so you rarely run it by hand.

## The two halves

**`workshop/`** is generic Win32 modding plumbing. Nothing in it knows anything about FFX, so it
would work against any 32-bit host.

| header | what it gives you |
| --- | --- |
| `workshop/Log.h` | a log file per plugin, plus the one-line status string a control panel shows |
| `workshop/HostModule.h` | the host base, RVA resolution, `Readable`, PE header validation, the x86 prologue and jmp-thunk checks, and `WriteProtectedDword` for patching a vtable slot |
| `workshop/Detour.h` | 5-byte jmp detours with a trampoline, and the `DETOUR_*` macros that keep a hook's address, prologue bytes and body declared together |
| `workshop/Transport.h` | the transport interface: bytes between a few peers, reliable or not. Nothing above it knows which backend is running. |
| `workshop/UdpTransport.h` | the development backend, plain UDP, usually loopback. Two copies of the game on one PC can talk. No reliability layer, and the header says so plainly. |
| `workshop/SteamAbi.h` | the Steam ABI, pinned. Vtable layouts, struct sizes and calling conventions, each block carrying where the fact came from, with static_asserts that turn a drift into a compile error. Read its provenance comments before changing anything that calls Steam. |
| `workshop/SteamTransport.h` | the shipping backend, Steam P2P over ISteamNetworking005. NAT traversal and Valve relay, addressed by SteamID. |
| `workshop/SteamUser.h` | who the local player is, and the friend list. Exists so a host can be shown their own SteamID instead of hunting for their profile page. |
| `workshop/Protocol.h` | the wire format, in one place so both ends agree by construction. Every message carries a sequence number so silent loss gets reported. |
| `workshop/Session.h` | membership: who is here, who hosts, who went quiet, and whether both ends agree on protocol, build and shared settings. Not the step clock, which is the lockstep layer above. |
| `workshop/Settings.h` | a described settings registry: one record per option with a label, a range and a **scope** saying whether it has to match on the other machine. Backs the control panel, the settings file, the shared-settings hash and, if the game's own options screen turns out to be extensible, that too. |

**`ffx/`** is the game itself.

| header | what it gives you |
| --- | --- |
| `ffx/Addresses.h` | the umbrella over `ffx/addresses/`, one file per subsystem. **The only place in the project allowed to contain a literal address.** Adding a subsystem is a new file there plus two lines, which is what lets two people map two subsystems without touching the same file. |
| `ffx/Layout.h` | field offsets, grouped per structure: `Chr::`, `Hide::`, `Instance::`, `Mesh::`, `Bounds::`, `PadDevice::Button::`, `Came::`, `Gfx::` |
| `ffx/Api.h` | the resolved function pointers and globals, as `ffx::Game` |
| `ffx/VerifyLayout.h` | is this the build those addresses came from? |
| `ffx/Character.h` | typed field access and the pool: slot to character, character to slot, liveness, free-slot search |
| `ffx/Walkmesh.h` | `BindToWalkmesh`, which you must call after every `SetPos` on a character you own. The reason is a genuine deadlock and it is written out in the header. |
| `ffx/AnimateHook.h` | the per-frame hook point, `FFXApplication::animate` |
| `ffx/Camera.h` | the active camera's yaw, in the same convention as `m_moveDir` |
| `ffx/Input.h` | the game's own input: the single global button mask and its bit names, the stick axes, edge detection, and the contract for suppressing or injecting input. Everything funnels through one singleton, which is both the worst single-player assumption in the game and the best lever for co-op. |
| `ffx/MainLoop.h` | the frame loop: the game's own step counter, the pause state, the one-byte simulation hold the shipped pause menu uses, fast forward as a catch-up mechanism, and the fixed timestep. Where a lockstep gate goes. |
| `ffx/Pad.h` | PhyreEngine's 18 pad slots, which is how you get a second controller with no hook at all |
| `ffx/RenderProbe.h` | reading back whether the engine is really drawing a character, and which gate rejected it |
| `ffx/HideFlags.h` | the game's own names for the `m_hideFlags` bits |
| `ffx/GameState.h` | the save data: the party and the three active battle slots, which chr id each character is, per-character stats, inventory with its single add-item commit point, gil, and equipment entries. Plus `HashGameState`, which is what a desync detector reads. |
| `ffx/GfxContext.h` | the cull-disable flag, with a warning about what it cannot prove |
| `ffx/Ffx.h` | umbrella header, and the startup contract |

## Writing a plugin against it

```c
#include "ffx/Ffx.h"

int __fastcall MyAnimate(void *self, void *unusedEdx)
{
    // Run the game first. Returning non-zero kills the game, so pass the
    // original result through unchanged.
    const int result = ffx::OriginalAnimate()(self, unusedEdx);
    // ... your per-frame work ...
    return result;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(module);

    workshop::OpenLog(L"my_plugin.log");
    workshop::BindHostModule();
    if (!ffx::VerifyLayout()) return TRUE;   // wrong build, hook nothing
    ffx::BindApi();
    ffx::HookAnimate(&MyAnimate);
    return TRUE;
}
```

Compile with `/I<loader>\workshop\include` and link `build\AlBhedWorkshop.lib`. `plugins\clone` is the
worked example, and `build_pilgrimage.bat` is the build script to copy.

**Skipping `VerifyLayout` is not an option.** These addresses are valid for one build of one
game. Patching the wrong build is worse than not running, and the check is the only thing
standing between those two outcomes.

## Rules that keep this working

- **Never hardcode a VA.** Store an RVA in `ffx/Addresses.h` and resolve it with
  `workshop::ModuleAddress`. ASLR on this exe is per-boot rather than per-launch, so a hardcoded
  VA appears to work perfectly until the machine reboots.
- **Give every address area an `RvaList` function** and add the one line for it to the area table in
  `src/ffx/VerifyLayout.cpp`, so a typo is caught at startup instead of by a fault later. Copy
  `addresses/Character.h`, which is the worked example.
- **Never dereference a game pointer that has not been through `workshop::Readable`.** Engine
  allocations can be stale or half built, and a fault on the frame path takes the game down.
- **The library is static and exports nothing.** Each plugin DLL gets its own resolved pointers
  and its own log, so two plugins cannot interfere through it.
- **Basenames must stay unique** across `src/`, because the build puts every `.obj` in one
  folder.

## Where the research lives

The code carries the conclusions. The derivations are in `..\..\reversing\`, and
`reversing\PHASE1_CLONE.md` is the one to read first if something in `ffx/Walkmesh.h` or
`ffx/RenderProbe.h` needs changing.
