# FFX menu system, mapped for co-op sync

Target: `G:\Steam\steamapps\common\FINAL FANTASY FFX&FFX-2 HD Remaster\FFX.exe`
All addresses below are VA with the loaded base 0x400000 unless marked RVA.
Status: in progress. Append-only log, newest sections at the bottom.

## Summary so far

The in-game party / system / equipment menu is a **native PS2-era "module"**, not Flash and
not Iggy. The game keeps a table of 25 module descriptors, each with its own init / exec /
term function pointers and its own integer state field. The menu is one of those modules.
Iggy and Flash only cover the PC port additions (`escmenu.swf` pause overlay and
`pausemenu.swf`).

Evidence for native:
- `addMenuPrim error!!!  menu->mprimn >= MAXMENUPRIM\n` at 0xB7B558. That is C code with a
  C struct `menu` holding a primitive array and a count `mprimn`. A Flash menu would not
  build its own primitive list in C.
- `%d:sgMenu Init`, `%d:sgMenu Exec`, `%d:sgMenu Result = %d` at 0xB7C588/98/A8, used by
  functions already named `sgMenuInit 0xA78210`, `sgMenuExec 0xA78270`,
  `sgMenuResult 0xA782A0`. Native init/exec/result trio.
- Menu assets are `.dcp` / `.ftc` / `.dds.phyre` files under `menu/`, loaded with the
  templates `%smenu/%s.dcp` (0xB5EAF0), `%smenu/base.ftc` (0xB5EB74),
  `%smenu/newkit.ftc` (0xB5EB8C), `%smenu/%s.bin` (0xB5EBF0). Bitmap font plus own
  renderer, not a SWF.
- The only `.swf` files in the binary are `/FFX_Data/GameData/PS3Data/flash/pausemenu.swf`
  (0xB42578) and `/FFX_Data/GameData/PS3Data/flash/escmenu.swf` (0xB425A8). Those are the
  PC port's pause and options overlay, handled by `FFEscMenu__*` and `IggyMenu__*`.

## The module manager (the backbone of the menu)

Globals:

| VA | Name I gave it | What it is |
|----|------|------|
| 0x1840888 | `g_ffxModuleTable` | array of 25 module descriptor pointers, index = module id 0..24 |
| 0x18408EC | `g_ffxModuleActiveMask` | bitmask, bit N set while module N is running |
| 0x1841C64 | `g_ffxModuleDrawPrio` | draw priority / depth bias set around module start and stop |

Functions:

| VA | Name I gave it | What it does |
|----|------|------|
| 0x8AA100 | `FFX_Module_Start` | `Start(id, arg)` clamps id to 0..24, looks up the descriptor, calls `desc->init(desc, arg)` then `desc->exec(desc)` once, sets `desc->flags = 0x0101`, sets bit id in the active mask |
| 0x8AA460 | `FFX_Module_GetActiveMask` | returns `g_ffxModuleActiveMask` |
| 0x8AAB20 | `FFX_Module_Stop` | `Stop(id)` clears the table slot, calls `desc->term(desc)`, clears bit id in the active mask |
| 0x8AB260 | `FFX_Module_SetDrawPrio` | stores into `g_ffxModuleDrawPrio` |

Module descriptor layout, read off `FFX_Module_Start` plus the one static descriptor I found
at 0xC5A8C8 (the TK test harness module):

```
+0x00  void (*exec)(desc*)            per-frame step
+0x04  void (*fn1)(desc*)
+0x08  void (*fn2)(desc*)
+0x0C  void (*init)(desc*, int arg)   called by FFX_Module_Start
+0x10  void (*fn4)(desc*)
+0x14  void (*term)(desc*)            called by FFX_Module_Stop
+0x18  byte  active                   FFX_Module_Start writes word 0x0101 here
+0x19  byte  ?
+0x1C  int   state                    the module's own state machine variable
```

The `+0x1C` state field is read and written as a switch selector all through
`sub_8DB4E0` (0x8DB4E0), the TK test module's exec, which walks states 0..10. So every
module carries its own state integer at descriptor+0x1C. That is the first half of the
answer to "where is the menu's state".

### How I found it

`idautils.Strings()` turned up `TK:Open Menu:%d\n` at 0xB6676C, referenced once from
`sub_8DB4E0`. That function is the dev test harness that opens the menu from the debug
menu. It calls `sub_8AA100(module_id, 0)` to start the menu module, then polls
`sub_8AA460()` for the module's bit to clear to know the menu closed, then
`sub_8AAB20(22)` to stop module 22. Decompiling those three gave the manager.

## Module ids, and which one is the menu

`FFX_Module_Register 0x8AAAB0` is called from `FFX_Module_SwitchGameMode 0x8AA780` with a
static descriptor for each id. Descriptor layout confirmed:

```
+0x00  exec      per-frame step, called by FFX_Module_StepAll 0x8AA200
+0x04  draw      per-frame draw, called by FFX_Module_DrawAll 0x8AA290
+0x08  prepare   called by FFX_Module_Register with (desc, g_ffxModuleGameMode)
+0x0C  init      called by FFX_Module_Start with (desc, arg)
+0x10  suspend   called by FFX_Module_Suspend 0x8AAD70
+0x14  term      called by FFX_Module_Stop 0x8AAB20
+0x18  byte active / 0x19 byte drawActive / 0x1A, 0x1B shadow copies
+0x1C  int  state        <- the module's own state machine variable
```

| id | descriptor | exec | what it is |
|----|-----------|------|-----------|
| 0 | 0xC59500 | 0x8A8C10 | boot / title sequencer, loads the `menumain` overlay |
| 1 | 0xC5B2B0 | 0x8E0350 | field / map top level (the default mode) |
| 2 | 0xC5A320 | 0x8C60B0 | submenu |
| 3 | 0xC5A2FC | 0x8C5450 | submenu |
| 4 | 0xC5A744 | 0x8CF020 | submenu |
| 6 | 0xC5A7D8 | 0x8D1E00 | submenu |
| 7 | 0xC5A720 | 0x8CC330 | submenu |
| 9 | 0xC5A7FC | 0x8D5540 | submenu |
| 10 | 0xC5A5E4 | 0x8CB0E0 | submenu |
| 11 | 0xC5A08C | 0x8BEF50 | save / load menu. Its suspend hook is `j_sa_menu_stop`, and the `sa_menu_*` strings belong to it. Game mode 0x400000 |
| 12 | 0xC5A820 | 0x8D6E40 | game mode 0x40000 |
| 13 | 0xC5A844 | 0x8D9600 | game mode 0x20000 |
| 14 | 0xC5A0B0 | 0x8C3340 | game mode 0x80000, FMV / movie |
| 15 | 0xC59FF0 | 0x8B15D0 | game mode 0x200000, also a submenu |
| 16 | 0xC5A014 | 0x8B15D0 | game mode 0x100000, same exec as 15 |
| 17 | 0xC59524 | 0x8BA660 | game mode 0x10000 |
| **19** | **0xC5B2F4** | **0x8E26D0** | **THE MAIN MENU (party / system / equipment)** |
| 20 | 0xC5B318 | 0x8E2870 | submenu |
| 21 | 0xC5A768 | 0x8D0460 | submenu |
| 22 | 0xC5A8D8 | 0x8DB4E0 | TK dev menu test harness, game mode 0x800000 |
| 23 | 0xC5B33C | 0x8E2960 | - |

`g_ffxModuleGameMode` at **0x18408F8** holds the current mode bit.

### 1. THE MENU ENTRY POINT  (answers question 1)

**Open:** `FFX_Module_Start(19, 0)` at **0x8E09C6**, inside module 1's exec
`sub_8E0350` (0x8E0350), the field top level. Right next to it at 0x8E0A42 is
`FFX_Module_Start(11, 0)` which opens the save menu, so 0x8E0350 is the field state machine
that reacts to the open-menu button.

**Per-frame step:** module 19's exec is **0x8E26D0**. It is a two state machine on
`desc+0x1C`:
- state 0: wait for `sub_8A8E50()`, then call `sub_A54B40()` once (the menu builder, owner of
  18 of the `addMenuPrim error` call sites), then go to state 1.
- state 1: call **`sub_A53570()` every frame**. That is the real menu per-frame step.

So the two VAs asked for are **open = 0x8E09C6 calling FFX_Module_Start 0x8AA100 with id 19**,
and **per-frame step = 0xA53570** (reached through module exec 0x8E26D0).

**How I identified them:** string `addMenuPrim error!!!  menu->mprimn >= MAXMENUPRIM\n`
(0xB7B558) is referenced 18 times from `sub_A54B40`. `sub_A54B40` has exactly one caller,
module 19's exec `0x8E26D0`. Module 19's descriptor is registered by
`FFX_Module_SwitchGameMode`, and the only `FFX_Module_Start(19, ...)` call is in module 1's
exec.

### 2. NATIVE, NOT FLASH, NOT IGGY  (answers question 2)

Native C. The per-frame step `sub_A53570` is plain C that walks a 12 element array of 832
byte page objects, calls a function pointer out of each, multiplies 4x4 matrices with
`FFX_Mtx_Mul_Api`, writes a sine table lookup `flt_C44BF0`, and keeps its own 2D camera. No
Iggy call, no ActionScript, no SWF anywhere on the path. The `.swf` files in the binary are
only `pausemenu.swf` and `escmenu.swf`, both PC-port overlays owned by `FFEscMenu__*` /
`IggyMenu__*`, which are a separate system that the notes already cover.

### 3 and 4 (in progress). The menu state object

`sub_A53570` dereferences two global pointers:

- **`dword_1A86108`** - the menu's page table object. It is an array of **12 entries of 832
  bytes** (12 * 832 = 9984 = 0x2700) with a per-page step function pointer at **+0x34**
  inside each entry, plus, immediately after the array at **+0x2700 (9984)**, a **signed
  int16 that is the current page / mode selector**. `sub_A53570` branches on it:
  negative means "no page, run the callback at the other object's +71080",
  value 7 is special cased (calls `sub_A58EC0` and compares the callback against
  `sub_A59E80`), any other non negative value calls `sub_A583E0`.
- **`dword_2305834`** - a large per-menu work block, offsets seen from 70408 (0x11308) to
  71328 (0x11460). Holds the menu's 2D camera (vec4 at +70424, matrices at +70496 / +70560 /
  +70624 / +70688 / +70752 / +70816 / +70880), a fade / open animation counter
  (`+71096` byte, incremented every frame until it passes 0x14, driving alphas at +71144 /
  +71148 / +71152 / +71156), three frame counters at +71320 / +71324 / +71328 and a
  function pointer at **+71080** that is the current screen's handler.

Both are heap pointers, so the replicable state lives in a heap object, not in simple
globals. Still nailing down their allocation and layout.

---

# CORRECTION and the real entry chain

I had module 1 wrong above. **Module 1 is the main menu top level, not the field.** Proof:
every submenu module exec (`sub_8B15D0`, `sub_8B3DB0`, `sub_8BF0B0`, `sub_8C5450`,
`sub_8C60B0`, `sub_8CB0E0`, `sub_8CC330`, `sub_8CF020`, `sub_8D0460`, `sub_8D1E00`,
`sub_8D5540`, `sub_8E27E0`, `sub_8E2870`) calls `FFX_Module_Start(1, 0)` when it closes, to
come back to the list. And `FFX_Module_SwitchGameMode` picks module 1 for game mode 0.

Module 19 is one of the menu's screens, reached when the main-menu cursor selection maps to
result 3. Given its pannable / zoomable / rotatable 2D camera and the `menu/abmap/...`
("ability map") textures nearby, module 19 is very likely the **Sphere Grid**. I have not
proved that by text, so treat the specific screen identity as inference. What is proved is
that it is a native module driven by `FFX_Menu_Step 0xA53570`.

## 1. THE MENU ENTRY POINT, for real  (question 1)

```
FFX_MainStep 0x820AE0                       (called from the frame driver)
  -> FFX_MenuSys_PollOpenAndStep 0x820750   (3 call sites: 0x820CB0, 0x820E1F, 0x820E2E)
       not running and no request pending:
         if (g_ffxPlayerPadPressed & 0x10)          <-- TRIANGLE, at 0x820771
            FFX_MenuSys_RequestOpen 0x821750 (0)    <-- stores mode 0
       request pending:
         FFX_MenuSys_Enter 0x8BE840 (mode)
           -> FFX_MenuSys_RunFrame 0x8AB030 (mode)   first call does the init
       already running:
         FFX_MenuSys_RunFrame 0x8AB030 (mode)        every later frame
              -> FFX_MenuSys_StepFrame 0x8A9CA0
                   first pass: FFX_Module_ResetAll 0x8AA570
                               FFX_Module_SwitchGameMode 0x8AA780 (mode & 0xFFFF0000, mode & 0xFFFF)
                                   mode 0 -> FFX_Module_Register(1, 0xC5B2B0); FFX_Module_Start(1, 0)
                   every pass: g_ffxModuleSuspendMask = 0
                               FFX_MenuSys_SamplePad 0x8BE500      <-- the only pad read
                               FFX_Module_StepAll   0x8AA200       <-- calls every module exec
                               FFX_Module_DrawAll   0x8AA290
```

**Open: `0x820750`, specifically the triangle test at `0x820771` and the call to
`FFX_MenuSys_RequestOpen 0x821750`.**
**Per-frame step: `FFX_MenuSys_RunFrame 0x8AB030`, whose body is `FFX_MenuSys_StepFrame
0x8A9CA0`. The module level per-frame step is `FFX_Module_StepAll 0x8AA200`.**

Supporting globals:

| VA | name | meaning |
|----|------|---------|
| 0x12FBC38 | `g_ffxMenuRequestedMode` | pending menu mode, -1 = nothing pending. Mode 0 = ordinary main menu. 0x40100000 / 0x40200000 = cloud save, 0x42000000 = gated |
| 0x12FBC3C | `g_ffxMenuEnterResult` | result of the enter attempt |
| 0x1340824 | `g_ffxMenuSysRunning` | nonzero while the menu system is up |
| 0x1340828 | `g_ffxMenuSysRunningAlt` | second running flag, checked with the first |
| 0x186A214 | `g_ffxMenuEnterMode` | the mode the menu was entered with |
| 0x18408F8 | `g_ffxModuleGameMode` | `mode & 0x3FFFFFFF`, drives module registration |
| 0x23C44A8 | `g_ffxPlayerPadPressed` | the field player's newly-pressed mask, already named in the IDB |

How I found it: `FFX_MenuSys_SamplePad 0x8BE500` is the only function in the whole binary
that calls `FFX_Pad__readButtons16` three times plus `readPressed16` three times plus
`readWord10` three times. It has exactly one caller. Walking up from there gave the chain.
`FFX_MenuSys_SwitchGameMode` has exactly one caller too, the same `FFX_MenuSys_StepFrame`.

## 5. WHERE INPUT ENTERS  (question 5, answered early because it is the cleanest result)

**One function, one call site, once per frame: `FFX_MenuSys_SamplePad` at `0x8BE500`,
called only from `FFX_MenuSys_StepFrame 0x8A9CA0` at `0x8A9E27`.**

It writes a single 0xC0 byte global block, `g_ffxMenuPadBlock` at **0x25D09C0 .. 0x25D0A80**:

| VA | size | name | content |
|----|------|------|---------|
| 0x25D09C2 | 16 bytes | `g_ffxMenuPadHoldCount` | per-button hold frame counters, index = bit number 0..15 |
| 0x25D09D2 | 2 | `g_ffxMenuPadHeldTrig` low half | held mask, port 0 slot 0 |
| 0x25D09D4 | 2 | high half of the same dword | newly-pressed (trigger) mask |
| 0x25D09D6 | 2 | `g_ffxMenuPadWord10` | the `readWord10` mask (repeat from the pad layer) |
| 0x25D09D8 | 2 | - | held mask OR'd with the previous frame |
| 0x25D09DA | 2 | - | pressed mask OR'd with the previous frame |
| 0x25D09DC | 2 | - | word10 OR'd with the previous frame |
| 0x25D09DE | 4 | `g_ffxMenuPadAnalog` | byte0 = left stick X, byte1 = left stick Y, cleared to 0x80808080 |
| 0x25D09E2 | 2 | `g_ffxMenuPadSynthHeldTrig` low | held mask with dpad bits synthesised from the stick (<0x18 or >0xE8 per axis) |
| 0x25D09E4 | 2 | high half | the newly-pressed edge of that synthesised mask |
| 0x25D09E6 | 2 | `g_ffxMenuPadRepeat` | auto-repeat mask. First repeat after 0.4667 s, then one every 0.1667 s. NOT every 0.3 s, see the determinism section. Start (0x800) and Select (0x100) are excluded from repeat |
| 0x25D0A00 | 64 | `g_ffxMenuPadHoldTimer` | 16 floats, seconds held |
| 0x25D0A40 | 64 | `g_ffxMenuPadLastSampleTime` | 16 floats, last sample timestamp |

Three accessors read it and are called at the top of **every** module exec, 34 call sites each:
- `FFX_MenuSys_GetHeld 0x8BE410`
- `FFX_MenuSys_GetRepeat 0x8BE470`
- `FFX_MenuSys_GetPressed 0x8BE4B0`

`FFX_MenuSys_ClearPad 0x8BE3D0` zeroes the whole block, and is already called by
`FFX_Module_Stop` and `FFX_MenuSys_Enter`, so the game itself treats a full wipe as a legal
operation **on a module boundary**. It is not free to do every step, and the reason is the next
section.

**Only the middle of the 192 bytes is input.** The full layout:

```
+0x00  arm byte              FFX_MesWin_SamplePadPort0 bails out when +0x00 and +0x01 are both 0
+0x01  second arm byte
+0x02  holdCount[16]         one byte each, input to the repeat calculation
+0x12  held        +0x14 pressed        +0x16 word10
+0x18  heldSticky  +0x1A pressedSticky  +0x1C word10Sticky
+0x1E  analogX     +0x1F analogY
+0x22  synthHeld   +0x24 synthPressed   +0x26 repeat
+0x40  holdTimer[16]         floats, seconds held
+0x80  lastSampleTime[16]    floats, THIS machine's process clock
```

The masks are the replication surface. Everything else is an arm flag or local scratch. Two
consequences that bite a replicator and not the game:

- **The arm bytes.** `FFX_MesWin_SamplePadPort0` bails out entirely when both are zero and zeroes
  one of them on its way out, so something has to re-arm it every step, and that something is the
  message window draw pass. The game only ever calls `ClearPad` on a module boundary so this never
  shows up. A mod calling it every step on the passenger can hold the message window's pad read
  disarmed, and the symptom is that the passenger cannot advance a dialogue box while the menu
  looks perfectly fine.
- **`lastSampleTime` is a local clock.** Zeroing it makes the next frame accumulate the whole
  process uptime in one step, and copying the raw bytes between two machines feeds a nonsense delta
  into the accumulator.

So to mean "no buttons this step", write zeroes to the mask fields only and leave `+0x00`, `+0x01`,
`+0x02..+0x11` and `+0x40..+0xBF` alone. Set the analog bytes to `0x80`, which is centred, rather
than to 0, which reads as full deflection.

**There are TWO writers of this block, not one.** `FFX_MenuSys_SamplePad 0x8BE500` and
`FFX_MesWin_SamplePadPort0 0x8B7CD0` write the same globals, and they use different repeat
constants: the menu is 14 steps then 5, the message window is 7 then 3. Any layer that hooks one
of them is sharing state with a layer that hooks the other.

**What the read depends on:** nothing but `sub_8CA510()`. If that returns 1 the block is
filled from a different source (`sub_8CA490/8CA500/8CA520/8CA530/8CA320`) instead of the
pad. That is a recorded-input / demo playback channel, so there is a second, already-built
injection path too. Otherwise the read is unconditional and portless.

**CO-OP HAZARD, the big one:** `FFX_Pad__readButtons16(0, 0, 0)` and
`FFX_Pad__readButtons16(0, 0, -1)` are the only pad calls. Args 1 and 2 are port and slot,
both hardcoded 0. Arg 3 is a frame offset into the ring, not a port. So the menu input path
has no notion of a second player at all, which matches what the project already found for the
field.

**Design conclusion for input:** hook `FFX_MenuSys_SamplePad 0x8BE500`. Let it run, then
overwrite `g_ffxMenuPadBlock` from the driving player's transmitted masks on both machines,
or on the passenger's machine simply `memset` it and write the received masks. There is no
need to touch anything deeper. That single 192 byte block is the complete menu input surface.

---

# SECOND PASS: commit points, screen identity, determinism and the ownership gate

Appended by the agent that built `loader\workshop\include\ffx\addresses\MenuSystem.h`,
`loader\workshop\include\ffx\MenuSystem.h` and `loader\workshop\src\ffx\MenuSystem.cpp`.
Everything in this pass was read out of the IDB and the names and comments are in there, saved.

## Scoring the first pass

Nothing above is a wrong address. Every VA in the tables was re-read and they all check out,
including the whole `g_ffxMenuPadBlock` field table, the module descriptor layout, the module
id to descriptor map, `g_ffxMenuPages` with its 12 x 832 pages and the int16 selector at
`+0x2700`, and the entry chain. Three refinements rather than corrections:

1. **The "recorded-input / demo playback" guess was mischaracterised.** `sub_8CA510` is not demo
   playback. It is the **ATEL script virtual pad**, a double-buffered fake controller that
   cutscene and tutorial scripts use to drive the menu. See the section below. The conclusion the
   first pass drew from it, that it is a ready-made injection channel, was right for the wrong
   reason, and it is a better channel than it looked.
2. **`GetRepeat` has 35 call sites, not 34.** `GetHeld` and `GetPressed` have 34. Trivial, noted
   for completeness.
3. **"Simply memset it and write the received masks" is not quite enough.** Two fields in that
   table are load-bearing in a way the table does not flag, and they are the subject of the
   determinism section below: `+0x18` (the sticky held copy) and `+0x26` (the repeat mask).

## The recorded-input path, identified: the ATEL script virtual pad

`sub_8CA510` is now `FFX_VirtualPad_IsEnabled`. The whole family:

| VA | name given | what it does |
|----|------------|--------------|
| 0x186A430 | `g_ffxVirtualPadEnabled` | BYTE. Non-zero and the menu ignores the real controller. |
| 0x186A420 | `g_ffxVirtualPadButtons` | DWORD, the live mask |
| 0x186A424 | `g_ffxVirtualPadButtonsNext` | DWORD, written by a script, latched next sample |
| 0x186A428 / 29 | `g_ffxVirtualPadAxis2` / `Next` | left stick X, 0x80 centred |
| 0x186A42A / 2B | `g_ffxVirtualPadAxis3` / `Next` | left stick Y |
| 0x186A42C / 2D | `g_ffxVirtualPadAxis0` / `Next` | right stick X |
| 0x186A42E / 2F | `g_ffxVirtualPadAxis1` / `Next` | right stick Y |
| 0x8CA490 | `FFX_VirtualPad_Latch` | flips Next into live, resets Next to 0 / 0x80 |
| 0x8CA500 / 520 / 530 | `FFX_VirtualPad_GetButtons` / `_Repeat` / `_Pressed` | **all three return the same dword** |
| 0x8CA320 | `FFX_VirtualPad_GetAxis` | axis 0..3 |
| 0x8CA370 | `FFX_VirtualPad_SetAxis` | |
| 0x8CA560 | `FFX_VirtualPad_SetButtons` | writes Next |
| 0x8CA570 | `FFX_VirtualPad_SetEnabled` | |
| 0x8CA540 | `FFX_VirtualPad_Reset` | clears both masks and disables |

Who uses it in the shipped game: `FFX_AtelSys_Core_501_resi` 0x85A17E enables it,
`FFX_AtelSys_Core_502_resi` 0x85A2EB and `FFX_AtelSys_Core_562_poll` 0x85A60B set the buttons,
`FFX_AtelSys_Core_556_resi` 0x85F2FE sets an axis, and `sub_8655E0` at 0x86563C resets it. Plus
the TK dev harness twice. So this is the mechanism behind scripted tutorial sequences that move
the menu cursor for you.

**Can a mod use it? Yes, and it is nicer than overwriting the pad block.** `FFX_MenuSys_SamplePad`
calls the latch itself, so the only contract is "write the mask every frame". Because the latch
resets Next, a mask written once produces exactly one frame of input, and because all three
getters return the same dword, held, pressed and repeat are identical in this mode. One frame of
mask is one clean button press with no edge bookkeeping and no timers.

Two caveats. It is **global and all-or-nothing**: while it is on, the real controller cannot reach
the menu on that machine at all, so the driving player would have to route its own input through
it too. And the three MenuSys accessors also branch on the flag, skipping the
analog-synthesised dpad fallback, so a stick-only input has to be converted to dpad bits by the
injector. Good for "the passenger watches a scripted sequence", prefer the pad hook for "the
driver plays and the passenger mirrors".

## Does the menu read the clock or any RNG? (question 4)

**RNG: no, not on any decision path.** A forward call-graph walk seeded with
`FFX_MenuSys_StepFrame`, `FFX_MenuSys_RunFrame`, `FFX_MenuSys_PollOpenAndStep`, `FFX_Menu_Step`,
`FFX_Menu_Build` and all six function pointers of all sixteen descriptors the menu registers
(modules 0, 1, 2, 3, 4, 6, 7, 9, 10, 11, 15, 16, 19, 20, 21, 23) visited 3,926 functions and
reached exactly one RNG function, `FFX_Rand_Stream 0x7988F0`, by exactly one path:

```
FFX_Menu_ExecModule10Config 0x8CB0E0        (the config screen)
  -> TOSwapInternationalEnvExec 0x8B0500    (reload the environment)
     -> sub_782090 -> sub_781020 -> sub_783020
        -> sub_78D090 -> FFX_Rand_Stream
```

`sub_78D090` rolls `FFX_Rand_Stream(1)` and bails out when a front-row party member's
record+0x4A has bit 0x04 set, which is almost certainly No Encounters. So it is the **encounter
roll on the map load path**, reached because the Config screen and the Save/Load screens can
trigger an environment reload. It is not menu logic, and it is already a field-side lockstep
concern rather than a new one.

**The clock: yes, in two places, and one of them matters.**

1. **`FFX_MenuSys_SamplePad` calls `FFX_Input__getTimeSeconds 0x630C60`, which is
   `Phyre_Time_NowSeconds() - dbl_C90278`.** The 16 hold timers at `g_ffxMenuPadBlock+0x40` are
   seconds. **So the auto-repeat mask at `+0x26` is NOT deterministic**, and two machines holding
   the same button from the same simulation step will not agree on when it repeats.

   **The interval is 0.1667 s, not 0.3 s.** An earlier pass of this doc said 0.3 and that is a
   misread worth spelling out, because the same mistake is sitting in the message window sampler
   and in two IDB comments. The loop body is:

   ```
   if (holdTimer[i] > 0.4666666) { repeat |= bit; holdCount[i] = 9; holdTimer[i] = 0.3; }
   ```

   0.3 is a **reset value pushed back into the accumulator**, not an interval, and the comparison
   stays at 0.4666666. So the gap between repeats is `0.4666666 - 0.3 = 0.1666666` s. The
   `holdCount[i] = 9` is dead: `holdCount` is only ever tested `== 0`, only on a fresh press frame,
   and a release zeroes it anyway. 14 and 9 are the original PS2 frame counts at 30 fps, since
   `14/30 = 0.46666` and `9/30 = 0.3` exactly, and the PC port turned the first into a threshold
   and the second into a seed. That quietly changed the interval from 9 frames to 5. The message
   window sampler has the same shape with 7 and 4, so its shipped interval is 3 steps rather
   than 4.

   **Derive it, do not transmit it.** This reverses what this doc used to recommend, and the reason
   is that both thresholds are exact 30 fps frame counts while the game steps at 29.97, so each one
   lands within a fraction of a frame of a boundary. At 1/29.97 the first repeat is 14 steps with
   1.4% of a frame to spare and the interval is 5 steps with 0.5% to spare. At exactly 1/30 both
   tip over to 15 and 6. The delta being accumulated is real wall clock, so under ordinary frame
   jitter the interval flutters between 5 and 6 steps **on a single machine**. There is no stable
   shipped behaviour to be faithful to, which removes the only argument for transmitting it. A
   step-counted repeat of 14 then 5, derived on both machines from the replicated held mask, is
   deterministic by construction, costs no payload, and gives a steadier cursor than the shipped
   game does.

   The Sphere Grid never sees this mask at all. `FFX_Menu_SphereGridReadPad` deliberately does not
   call `FFX_MenuSys_GetRepeat` and runs its own frame-counted repeat off the sticky held copy, so
   it is already deterministic given the same held mask.

2. Module 1's **draw** function `sub_8E0BA0` reaches `Phyre_Time_NowSeconds` through
   `sub_8E7D30 -> sub_641410 -> sub_42F690`. Draw-side only, so it affects an animation phase and
   nothing a screen decides. Harmless.

Nothing else. No `GetTickCount`, no `timeGetTime`, no `QueryPerformanceCounter`, no `clock`, no
`_time64` anywhere in the reachable set.

## The one deterministic island: the Sphere Grid runs its own repeat

`FFX_Menu_Step` at 0xA536D2 calls a function with `g_ffxMenuWork + 71276`. That function is
`sub_A57520`, now **`FFX_Menu_SphereGridReadPad 0xA57520`**, and it is the Sphere Grid's own pad
decode. It deliberately does not call `FFX_MenuSys_GetRepeat`. Layout of the struct it fills, in
int16s from `g_ffxMenuWork + 71276`:

```
+71276  held            <- FFX_MenuSys_GetHeldSticky 0x8BE440, which is pad block +0x18
                           OR a dpad bit synthesised from analog axis 3 at a +-64 threshold
+71278  pressed         held & ~prevHeld
+71280  prevHeld
+71282  heldOrRepeat    <- FFX_Menu_SphereGridSwitchChar reads L1 (0x04) and R1 (0x08) here
+71284  pressedOrRepeat
+71286  BYTE[16]        hold frame counters, first repeat at 6 frames
+71302  BYTE[16]        repeat sub-counters, then every 3 frames
```

**That repeat is frame counted, not clock counted.** So given the same held mask the Sphere Grid's
cursor is fully deterministic, which is convenient because the grid is the screen the co-op
requirement cares most about.

The cost is a trap: `FFX_MenuSys_GetHeldSticky` is the **only** reader of pad block `+0x18` in the
whole binary, and the Sphere Grid is its only caller. **An injector that writes `+0x12` and
forgets `+0x18` works on every screen except the Sphere Grid.** `ffx::WriteMenuPad` writes both.

## WHERE THE MENU COMMITS TO THE SAVE BLOCK (question 1)

**No function in the menu touches the save block by address.** That was established two ways.

**Check one**, every direct reference into the 26,816-byte run at 0x112CA90..0x1133350. 123
functions reference it. Of those, the only ones in the menu code ranges (0x8A0000..0x8F0000 and
0xA40000..0xA90000) are `FFX_MenuTest_ExecTK` (the dev harness), `sub_8DB7E0` (the same harness),
`FFX_Debug_ApplyViewerSave 0x8B55E0` and `sub_8B5960`, all debug.

**Check two**, every caller of `FFX_GetSaveData 0x785240`. 26 call sites, 5 in the menu ranges, and
all five are the tiny get/set pair for **one config bit**, `saveData+0x5EC` bit 0:
`FFX_Menu_ConfigRowGetSaveBit5EC 0x8CB090`, `FFX_Menu_ConfigRowSetSaveBit5EC 0x8CB0B0`,
`FFX_Menu_GetSaveConfigBit5EC 0x8CC140`, `FFX_Menu_SetSaveConfigBit5EC 0x8CC150`,
`FFX_Menu_ApplySaveConfigBit5EC 0x8CC170`.

**Check three**, every caller of `FFX_SaveData__getCharRecord 0x785330`, because a menu function
could get a record pointer and write a field without ever naming the block. 35 callers, 22 of them
in the menu ranges, and only **three** of those write: `FFX_Menu_SetCharCurrentHp 0x8AAE90`
(record+0x1C, and it clears record+0x3D when HP is non-zero), `FFX_Menu_SetCharCurrentMp 0x8AAEF0`
(record+0x20) and `FFX_Menu_SetCharRecordByte38 0x8C2C90` (record+0x38). The other 19 are readers.

So **the whole in-game menu commits through about ten named functions**, and that is the desync
detector's natural attachment point:

| VA | function | what it writes | reached from |
|----|----------|----------------|--------------|
| 0x786EF0 | `FFX_SaveData_SpendSphereLevels` | record+0x3B -> record+0x3C | **Sphere Grid only, one caller** |
| 0x7AB970 | `FFX_SaveData__setCharEquip` | record+0x2D or +0x2E | **Equip screen (module 4)**, plus the two shop equip-on-buy helpers |
| 0x7AB9F0 | `FFX_SaveData_SwapEquipEntries` | two equipment entries | Customise (module 3) and five equipment-sort helpers |
| 0x7AB910 | `FFX_SaveData_AddEquipEntry` | a new equipment entry | shop buy |
| 0x7ABCA0 | `FFX_SaveData_RemoveEquipEntry` | frees an entry | shop sell (module 12 only) |
| 0x790550 | `FFX_SaveData_AddItem` | the item arrays and both change masks | Items (7), Customise (3), both shops, one 0xA5 helper |
| 0x7859A0 | `FFX_SaveData_SpendGil` | conf+0 | both shops |
| 0x8AAE90 | `FFX_Menu_SetCharCurrentHp` | record+0x1C, +0x3D | items and abilities |
| 0x8AAEF0 | `FFX_Menu_SetCharCurrentMp` | record+0x20 | Abilities (module 2), Equip HP/MP clamp |
| 0x8C2C90 | `FFX_Menu_SetCharRecordByte38` | record+0x38 | module 21 only |
| 0x8CB0B0 | `FFX_Menu_ConfigRowSetSaveBit5EC` | saveData+0x5EC bit 0 | Config (module 10) |

**And a useful negative: the in-game menu never changes the active party.** Nothing in the menu
ranges calls `FFX_SaveData_SetCharInParty 0x7869B0` (6 callers), `FFX_Btl_CommitPartyToField
0x786930` (2 callers) or writes `saveData+0x3D58`. That matches the game's design, where party
composition changes with the in-battle Switch command, not from the field menu. So **there is no
party-order screen in the field menu to sync.** The party-ordering problem is a battle-menu
problem, which is somebody else's area.

Also worth knowing: `FFX_SaveData_SetCharAbility 0x785E00` has exactly one caller,
`sub_7AE370`, whose only caller is `sub_792210`, and neither is in the menu. So learning an
ability is not committed from the menu screens either, it comes through the battle and reward
path.

## WHICH MODULE IS THE SPHERE GRID (question 2): PROVED, it is module 19

Three independent pieces, any one of which would do:

1. **The string `Got ABMAP pad input!!!`** is in module 19's subtree and nowhere else. ABMAP is
   "ability map", the internal name for the Sphere Grid, and it is the same name as the
   `menu/abmap/` textures the first pass noticed.
2. **`setSpheConePrim error MAXSPHECONE=%d`**, also module 19 only. Sphere cones are the grid's
   node geometry.
3. **The commit.** `FFX_SaveData_SpendSphereLevels 0x786EF0` has exactly one caller in the whole
   binary, `sub_A56160`, now `FFX_Menu_SphereGridActivateNode`. It sits in the same 0xA4..0xA5
   page-handler family that module 19's builder `FFX_Menu_Build 0xA54B40` owns and that
   `FFX_Menu_Step 0xA53570` drives, and it charges the character at `g_ffxMenuWork + 71100`.
   Confirmed signature from the IDB:

```c
// FFX_SaveData_SpendSphereLevels(unsigned char charIndex, int n)
v2 = saveData[0x55CC + 148*charIndex + 0x3B];   // available S.Lv
if (v2 - n < 0) return -1;
saveData[0x55CC + 148*charIndex + 0x3B] = v2 - n;
saveData[0x55CC + 148*charIndex + 0x3C] = clamp(n + spent, 0, 101);
```

That is exactly the field the brief named, record+59 = 0x3B, at `saveData+0x55CC` with a stride of
148. Inference upgraded to proof.

`FFX_Menu_SphereGridActivateNode` has no code xrefs: it is reached through the screen handler
pointer at `g_ffxMenuWork + 71080`, which is why the first pass's call-graph work could not see
it. It also clears bit `(1 << charIndex)` in the 20-byte node records at `g_ffxMenuWork + 43028`,
which is the menu's working copy of who has activated which node.

## WHICH MODULE IS WHICH SCREEN (question 3)

`FFX_Menu_StartSubmoduleByResult 0x8E24C0` is the main menu's submenu launcher. It reads
`dword_1871568[2 * cursorRow]` through `sub_8E2270` to get a result code, then:

| result | module | `FFX_Module_Start` site |
|--------|--------|-------------------------|
| 0 | 3 | 0x8E2556 |
| 1 | 2 | 0x8E2586 |
| 2 | 4 | 0x8E253E |
| 4 | 6 | 0x8E2526 |
| 5 | 7 | 0x8E256E |
| 7 | 9 | 0x8E25CE |
| 8 | 10 | 0x8E259E |
| 10 | 15 | 0x8E24F6 |
| 15 | 21 | 0x8E25B6 |
| 20 | 20 | 0x8E250E |

Every one of them suspends module 1 rather than stopping it, which is why module 1's bit stays set
in the active mask the whole time a submenu is up. Module 1's own exec starts module 19 (Sphere
Grid, at 0x8E09C6) and module 11 (save, at 0x8E0A42) directly.

Identified, with what proved each:

| id | screen | proof |
|----|--------|-------|
| 1 | **main menu top level** | every submenu calls `FFX_Module_Start(1, 0)` on close, and `FFX_Module_SwitchGameMode` picks it for game mode 0 |
| 2 | **Abilities** | L1 (repeat 0x04) and R1 (0x08) call the character cursor prev/next, and casting reads the ability's MP cost at `item record + 37` then calls `FFX_Menu_SetCharCurrentMp(char, mp - cost)`. Loads `scene10`. |
| 3 | **Customise** | calls `FFX_SaveData_AddItem` (spends the materials) and `FFX_SaveData_SwapEquipEntries` (re-sorts after the name id changes). Strings `TK:ABL ID:0x%x`, `TK:EXCHANGE:%d <> %d`, `TK:SRC = %d / DST = %d`. Starts module 23 as a sub-screen. |
| 4 | **Equip** | one of only three menu callers of `FFX_SaveData__setCharEquip`, and its strings are `KEEPHP:%d` and `NOW HP:%d`, which is the max-HP clamp an armour change needs |
| 7 | **Items** | `FFX_SaveData_AddItem` with a negative delta, plus `sub_785C50` to set a learned ability when a sphere or an Al Bhed primer is used. String `TK:SMN:Learn id = %d / command = %d(0x%x) / result = %d`. |
| 10 | **Config** | a row table at `g_ffxMenuConfigRows 0x186A44C`: row+4 value count, row+8 current value, row+12 selectable, row+20 apply callback. Cursor row `g_ffxMenuConfigCursorRow 0x186A444`, count `0x186A448`. Up/Down move rows skipping rows whose +12 is not 1, Left/Right change the value, Cross confirms, and on confirm it compares the map id and runs `TOSwapInternationalEnvExec` when it changed. |
| 11 | **save frontend** | `sa_menu_*` strings, suspend hook is `j_sa_menu_stop`, game mode 0x400000 |
| 12 | **a shop** | `FFX_SaveData_SpendGil` + `FFX_SaveData_RemoveEquipEntry` (sell), game mode 0x40000 |
| 13 | **a second shop** | `FFX_SaveData_SpendGil` via `sub_8D9BD0` / `sub_8D9E20`, game mode 0x20000 |
| 15, 16 | **save and load** | `/FFX_Data/GameData/PS3Data/saves/`, `.SAV`, `icon.sys`, `010906%02d.ico`. Same exec `sub_8B15D0` for both. |
| 19 | **Sphere Grid** | see above |
| 21 | **probably Overdrive mode** | only distinctive string is `TK:SND:OVERDRIVE`, and its only save-block write is `FFX_Menu_SetCharRecordByte38`, record+0x38 per character. Not proved. |
| 22 | TK dev harness | `TK:Open Menu:%d`, game mode 0x800000 |

**Still unidentified: 6, 9, 20 and 23.** None of them writes the save block and none has a
distinctive string. Module 9's subtree is 21 functions and module 20's is 11, so they are thin
helper screens, probably a confirmation prompt and a tutorial or help page. Module 6 is 58
functions. Module 23 is reached only from module 3, so it is a Customise sub-screen.

## IS THERE A PER-CHARACTER GATE (question 5): yes, and it is better than expected

**There is one menu-wide answer to "which character is the cursor on", and it is two plain static
globals plus a getter with 46 call sites.**

| VA | name given | what it is |
|----|------------|------------|
| 0x1841C14 | `g_ffxMenuCharList` | **8 bytes**, character record indices. The count lives immediately after it at 0x1841C1C, so the capacity really is 8. |
| 0x1841C1C | `g_ffxMenuCharListCountA` | int, used when `sub_8AA450()` is 0 |
| 0x1841C20 | `g_ffxMenuCharListCountB` | int, used otherwise |
| 0x1841C28 | `g_ffxMenuCharCursor` | int, index into the list |
| 0x1840886 | `g_ffxMenuCharCursorDir` | BYTE, 1 forward, 0xFF back |
| 0x8A9860 | `FFX_Menu_GetCursorChar` | `return g_ffxMenuCharList[g_ffxMenuCharCursor];` **46 callers** |
| 0x8A9B30 | `FFX_Menu_GetCharListCount` | picks between the two counts |
| 0x8AAFA0 | `FFX_Menu_CursorCharPrev` | L1. Decrements, wraps to count-1, sets dir 0xFF. |
| 0x8AAF10 | `FFX_Menu_CursorCharNext` | R1. Increments, wraps to 0, sets dir 1. |

**The address and offset the brief asked for: `g_ffxMenuCharList 0x1841C14` indexed by
`g_ffxMenuCharCursor 0x1841C28`, read through `FFX_Menu_GetCursorChar 0x8A9860`.** The value is a
character record index, the same identity `GameState.h` calls a character index, so it goes
straight into an ownership table.

For the Sphere Grid specifically there is a **second** field, and it is the one that matters:

- **`g_ffxMenuWork + 71100`**, a byte character index. `FFX_Menu_SphereGridSwitchChar 0xA58EC0`
  writes it from `FFX_Menu_GetCursorChar` when L1 or R1 is seen in the grid's own
  held-or-repeat mask at `g_ffxMenuWork + 71282`, saving the old value in `+71101` and the
  direction in `+71102` (1 = L1, 2 = R1, 0 = idle).
- **`FFX_SaveData_SpendSphereLevels` is charged against `+71100`, not against the cursor.** So the
  gate for "only a character's owner may act on that character's grid" reads `+71100`.

The two can legitimately differ for a frame during a switch, which is exactly why a detector
should sample both. The character-record accessors the menu uses, now named, are
`FFX_Menu_GetCharCurrentHp 0x8A98C0` (record+0x1C), `GetCharCurrentMp 0x8A9970` (+0x20),
`GetCharMaxHp 0x8A9990` (+0x24), `GetCharMaxMp 0x8A99B0` (+0x28),
`GetCharWeaponSlot 0x8A9C70` (+0x2D) and `GetCharArmourSlot 0x8A9820` (+0x2E).

One more identity note: the Equip screen does **not** take the character from the cursor. It takes
it from the equipment entry's `+4` ForChar byte, which is how `sub_8D8520` and `sub_8D9120` call
`FFX_SaveData__setCharEquip(entry[4], entry[5], slotId)`. So an equipment ownership rule has to
read the entry, not the cursor.

## The menu's button conventions, read off the Config screen

The bit names are already in `loader\workshop\include\ffx\Input.h` as `ffx::Btn::*` and the menu
uses the same 16-bit layout. What the first pass could not say, and this one can, is the meaning:

| bit | button | what the menu does with it |
|-----|--------|----------------------------|
| 0x0004 | L1 | previous character |
| 0x0008 | R1 | next character |
| 0x0010 | Triangle | opens the menu from the field (tested in `FFX_MainStep`, not in the menu) |
| 0x0040 | Cross | **confirm** (module 10 state 3, `Pressed & 0x40`) |
| 0x1000 | Up | cursor up |
| 0x2000 | Right | increase a value |
| 0x4000 | Down | cursor down |
| 0x8000 | Left | decrease a value |
| 0x0100, 0x0800 | Select, Start | **excluded from auto-repeat** by `FFX_MenuSys_SamplePad` |

## DOES "REPLICATE INPUT, NOT STATE" SURVIVE? Yes, with three named conditions

It survives, and the evidence is stronger than the first pass had.

**For it:**
- One input surface, 192 bytes, no port argument anywhere on the path.
- No RNG on any menu decision path, across 3,926 reachable functions.
- No state scribbling. Every commit goes through about ten named functions, so a divergence has
  somewhere to be caught rather than needing a memory watch.
- A zeroed input mask is a state the shipped game produces itself, so "no buttons" is a safe
  answer on a dropped network frame. Write the mask fields rather than calling
  `FFX_MenuSys_ClearPad`, which also wipes the arm bytes and the shared repeat state. See the
  layout note above.
- The auto-repeat can be derived from a step count instead of transmitted, so the replicated
  payload is just the masks.
- The Sphere Grid's own repeat is frame counted, so the screen the requirement cares most about is
  deterministic given the same held mask.

**The three conditions:**

1. **The repeat mask at `+0x26` must be replaced, and the replacement should be derived from a
   step count rather than transmitted.** It is wall-clock paced, and unstable enough that it
   varies on one machine, so there is nothing to be faithful to. Count steps on both machines off
   the replicated held mask: 14 steps to the first repeat, then every 5. The arithmetic and the
   reason the old advice was wrong are in the determinism section above.
2. **The sticky held copy at `+0x18` must be written too.** The Sphere Grid is its only reader and
   ignores `+0x12` entirely.
3. **Opening the menu is not part of the input block.** The Triangle test lives in `FFX_MainStep`
   reading the single global `g_ffxPlayerPadPressed 0x23C44A8`, before the menu system exists. So
   the open has to be replicated separately, through `FFX_MenuSys_RequestOpen 0x821750` on both
   machines. That function stores a mode and returns, and the menu actually comes up on the next
   frame, which is convenient: both machines can be told to open on the same simulation step.

**The attachment point:** detour `FFX_MenuSys_SamplePad 0x8BE500`, let the original run, then
overwrite the block. Five stolen bytes, `55 8B EC 51 56`, no branch and no absolute address among
them. The order inside `FFX_MenuSys_StepFrame` is sample, then `FFX_Module_StepAll`, so a
post-original overwrite lands early enough for every screen in the same frame, the Sphere Grid's
second-stage decode included. Hex-Rays types the function `__thiscall` only because it hands ecx
to `FFX_Input__getTimeSeconds`, which ignores it, and nothing after the call site reads ecx, so a
`__cdecl` hook is safe. That was checked instruction by instruction.

## What is in the kit now

- `loader\workshop\include\ffx\addresses\MenuSystem.h` - 90 RVAs with a
  `MenuSystemRvaList(int *count)` at the bottom. It deliberately does not redeclare
  `MenuSysRunning`, `MenuSysRunningAlt`, `MenuOpenPending`, `SaveUiState`, `EscMenuIsOpen` or
  `EscMenu`, which `addresses\MainLoop.h` already owns, nor `FFX_SaveData_AddItem`, which
  `addresses\GameState.h` owns.
- `loader\workshop\include\ffx\MenuSystem.h` - the offset namespaces (`MenuPadBlock`,
  `ModuleDesc`, `MenuWork`, `NodeRecord`, `MenuPages`), the `MenuModuleId` enum, `MenuPadFrame`
  and the free functions.
- `loader\workshop\src\ffx\MenuSystem.cpp` - compiles clean at `/W4 /WX`.

The useful calls: `MenuSystemRunning`, `MenuScreenName`, `ModuleStepping`, `ModuleState`,
`ReadMenuPad` / `WriteMenuPad` / `ClearMenuPad`, `HookMenuSamplePad`, `RequestMenuOpen`,
`MenuCursorChar` and `SphereGridCharacter`.

One trap the typed layer handles and a hand-rolled reader would not: `g_ffxModuleTable`'s `.data`
initialiser is **0xFFFFFFFF**, not 0. Entries are filled at runtime by `FFX_Module_Register`, so
every table read has to reject 0 **and** -1 before dereferencing. `ModuleDescriptor` does.
