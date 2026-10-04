# FFX.exe debug mode and debug menu

Addresses in prose are VA. The promotion table at the bottom is RVA, because that is what the
project headers store. VA = RVA + 0x400000.

Target: `FINAL FANTASY X HD Remaster`, `FFX.exe`, Steam build, the one in the project IDB.

## Headline

Yes, there is a usable debug menu in this build. There are two of them, and they are unrelated.

1. **`DebugMenuManager`**, a Phyre based overlay the HD team ported. Its singleton is created on
   every normal boot, its nine pages still draw every frame once enabled, and the pages include a
   warp menu, a battle test launcher, a stat editor and a cheat page. The one thing the PC build
   dropped is the per-frame call into the pages' input handlers. Those handlers are still in the
   binary, intact, orphaned. A plugin opens the menu with one call and restores input with one
   more call per frame. **This is the recommended target.**
2. **`SG_DebugGui`**, the original PS2 developer window system. 95 window procedures, a root
   window with seven tabs, a real text renderer, and a window stack. Its draw pump and its input
   pump are BOTH still wired into the main loop behind `g_ffxDebugMode`. Its opener survives but
   is orphaned. The one thing that is genuinely broken is its pointer device read, which was
   stubbed to `xor eax,eax / ret`, so the mouse cursor can never move. That is three dwords a
   plugin can fill itself.

The cheapest path for a cheat plugin is neither menu: the individual cheat functions
(`FFX_Debug_FullItem`, `FFX_Debug_FullGil`, `FFX_Debug_OneOfEveryItem`, ...) are ordinary
functions with no gate on them at all. See section 7.

---

## 1. g_ffxDebugMode, VA 0x133C910, RVA 0xF3C910

One byte. `FFX_IsDebugMode` 0x822550 and a duplicate `FFX_IsDebugMode_dup` 0x887C50 both just
return it. 41 cross references.

### The writers, all three of them

There are exactly three instructions in the whole binary that store to it.

1. `0x820683`, inside the `FFX_MainStep` region but outside any recognised function body,
   `mov g_ffxDebugMode, 0`. A reset.
2. `0x822509` / `0x822512` in `FFX_ParseCommandLineAndInitDebug` 0x822490. This is the real one.
   It is a plain `argv` check:

   ```c
   if (argc > 1 && (strcmp("debug", argv[1]) == 0 || strcmp("DEBUG", argv[1]) == 0))
       g_ffxDebugMode = 1;
   else
       g_ffxDebugMode = 0;
   ```

   So the shipped retail exe still honours a command line switch. The literal first argument has
   to be `debug` or `DEBUG`, case sensitive, and it has to be `argv[1]` exactly, not any later
   argument. Verified in the disassembly. The caller chain is
   `FFXApplication__initScene -> FFX_BootSequence -> FFX_ParseCommandLineAndInitDebug_tramp`
   0x636410, with `FFX_BootSequence` pushing the trampoline as a callback at 0x641848 and
   0x641952. I did not verify that the `argv` reaching it is the real process command line
   rather than a synthesised one, so treat "launch with the argument debug" as plausible but
   untested. Writing the byte is the sure thing.
3. `0x887C98`, inside an unnamed function I created and named `FFX_SetDebugMode_ORPHAN` at
   `0x887C80`:

   ```asm
   0x887C80  push ebp
   0x887C81  mov  ebp, esp
   0x887C83  cmp  ds:byte_B5EC4F, 0
   0x887C8A  jz   short 0x887C9D        ; refuse, do nothing
   0x887C8C  mov  eax, [ebp+8]          ; arg
   0x887C8F  test eax, eax
   0x887C91  jz   short 0x887C98
   0x887C93  mov  eax, 1                ; normalise to 0/1
   0x887C98  mov  g_ffxDebugMode, al
   0x887C9D  pop  ebp
   0x887C9E  retn
   ```

   Nothing in the binary calls it. `byte_B5EC4F` is 1 in the loaded image, so its gate is open,
   but with no caller the function is only useful to a plugin.

**No config key, no ini read, no environment variable anywhere on the write path.** The plugin
answer is: write 1 to the byte at RVA 0xF3C910 directly.

### The readers

All 41 cross references. `cmp` unless noted.

```
0x672016  FFX_DebugMenu_drawIfActive        gates the DebugMenuManager mode-5 overlay   LIVE
0x786EC6  FFX_InitNewSaveData
0x82031D  FFX_InitSubsystems1
0x820683  (no func, in the MainStep region)   mov g_ffxDebugMode, 0   WRITE
0x8207FF  FFX_DrawDebugOverlay              gates SG_DebugGui_DrawAll                  LIVE
0x820959  FFX_MainInit                      gates FFX_LoadEventIdTable
0x820A16  FFX_MainInit
0x820AB7  FFX_MainInit                      gates one SG_Printf_STUB call, so a no-op
0x821565  FFX_MainStep                      byte_12FBBB8 && debug -> sub_7E6560
0x822224  FFX_StepPacing                    gates SG_DebugGui_UpdateInput              LIVE
0x822509  FFX_ParseCommandLineAndInitDebug  mov 1   WRITE
0x822512  FFX_ParseCommandLineAndInitDebug  mov 0   WRITE
0x822550  FFX_IsDebugMode                   the getter
0x82500D  FFX_Ch_Allocate
0x825D16  FFX_Ch_LoadChrData                cmp g_ffxDebugMode, dl
0x825DF5  FFX_Ch_LoadChrData
0x826F14  FFX_Ch_Init
0x82D0ED  FFX_Player__readPad               the debug free-move keys
0x82DB29  sub_82DB20
0x83034D  sub_830330
0x83660B  sub_836520
0x83722E  FFX_Mot_GetName
0x83AEE0  (no func)
0x83AFF3  Sg_PcWrite                        host-PC file write for MemDump / Capture
0x83BAB3  (no func)
0x83BB93  (no func)
0x83C073  sub_83C070
0x83F5B6  FFX_PlayerCam_ReadPadInput
0x840FC3  sub_840FC0
0x841233  sub_841230
0x8412D0  sub_8412C0
0x841440  sub_841440
0x841470  sub_841470
0x841490  sub_841490
0x841613  sub_841610
0x844EB0  sub_844EB0
0x8702B3  (no func)
0x887C50  FFX_IsDebugMode_dup               second copy of the getter
0x887C98  FFX_SetDebugMode_ORPHAN           mov g_ffxDebugMode, al   WRITE
0xA7AFE7  (no func, the message-window language table walk)
0xA7B178  (no func, same function, gates sub_855B30)
```

Eight functions call `FFX_IsDebugMode` and twenty-five call `FFX_IsDebugMode_dup`. The notable
consumers of the duplicate are `FFX_Btl_BeginBattle`, `FFX_Btl_Init`, `FFX_Btl_MainStep`,
`FFX_Btl_SetupFieldPointers`, `FFX_Map_WarpTo` 0x86FECD and `FFX_Map_WarpToWithSavedFade`
0x86FF4D. That last pair is the gate `WorldState.h` already warns about on `MapWarpTo`.

---

## 2. sub_7E6560 and FFX_DrawDebugOverlay

The brief flagged these two. One is a red herring and one is the live door to the SG GUI.

### sub_7E6560 is a red herring, 28 bytes

```c
int sub_7E6560()
{
  int result;
  if ( byte_2322794 != 0 ) result = sub_A5DFB0();
  if ( byte_2322790 != 0 ) return sub_A5DFB0();
  return result;
}
```

`sub_A5DFB0` is 49 bytes and only bumps a few counters in the effect system. `byte_2322790` and
`byte_2322794` are the effect subsystem's "effect debug mode" flags, set by `sub_A63B00` which
prints "effect debug mode on" / "off" depending on whether an effect registry file was found.
There is no drawing here at all. It needs no font and no render target and forcing the gate on
would not crash, it would just tick two counters. `byte_12FBBB8`, the other half of the gate,
is set to 1 unconditionally by `FFX_MainInit` at 0x8209DC through `sub_820380(1)`.

### FFX_DrawDebugOverlay 0x8207F0 is the live SG GUI door

```c
int FFX_DrawDebugOverlay()
{
  sub_844A80();
  result = sub_821A40(4);
  if ( g_ffxDebugMode != 0 )
  {
    sub_8412C0();
    if ( !sub_83A620() && byte_12FBBB3 != 0 )
    {
      SG_DebugGui_DrawAll(dword_23CBBF4, sub_83B650());
      sub_83FD20();
    }
    result = sub_83A270();
    if ( result != 0 )
      return sub_83B110(0, 1, 128);
  }
  return result;
}
```

It is called unconditionally from `FFX_MainStep` at 0x82157A. Its three inner conditions, all
checked:

* `g_ffxDebugMode != 0`. The plugin sets this.
* `sub_83A620() == 0`. That function is `return dword_1301804 != 0 && dword_1301940 != 0;`. Both
  globals live in the zero-filled tail of `.data` (`ida_bytes.is_loaded` is False for both), so
  they are 0 at process start and the condition holds until the debug text layer at
  `sub_83A470` / `sub_83A4F0` / `sub_83A580` turns itself on.
* `byte_12FBBB3 != 0`. In the disassembly this is `cmp byte_12FBBB3, al` at 0x820816 where `al`
  is provably 0, because the preceding `test eax, eax / jnz` fell through. So it really is
  "compare against zero", and Hex-Rays reads it correctly. `FFX_MainInit` calls `sub_820420(1)`
  at 0x8209C7, outside any debug gate, and that function is `byte_12FBBB3 = a1 & 1`. So this is
  1 by the time any frame is drawn.

Conclusion: with `g_ffxDebugMode` set, `SG_DebugGui_DrawAll` runs every frame. It does not need
a debug-build font or render target. The whole draw backend is full-body shipped code, see
section 5.

---

## 3. The DebugMenuManager, the live PC debug menu

### The object graph

`g_debugMenuManager` VA 0xCCCAB0 holds the singleton pointer.

* `DebugMenuManager__ctor` 0x6B4900 sets the vftable and `+4 = 10`, where 10 means off.
* `DebugMenuManager__createSingleton` 0x6B4950 is called from `FFX_GraphicInitialize` at
  0x641A80, so the singleton EXISTS at runtime in a normal retail boot. A plain unconditional
  call, not behind a debug gate.
* `DebugMenuManager__destroySingleton` 0x6B4990, from `FFX__graphicFinalize`.
* `DebugMenuManager__get` 0x6B49B0 returns the pointer.
* `DebugMenuManager__getMode` 0x6B4A80 returns `this+4`.
* `DebugMenuManager__setMode` 0x6B4D10, `char __thiscall (this, int mode)`. It does
  `this->mode = mode % 11`, disables all ten pages, then enables the one selected.
* `DebugMenuManager__close` 0x6B4A70 is `setMode(10)`.

**`setMode` has exactly one caller in the whole binary, and that caller is `close()`.** No
shipped code path ever opens a page. That is the first thing a plugin has to do itself.

### The ten pages

Each page is a separate singleton in its own global, fetched by a six-byte getter. The titles
below are the sprintf format strings inside the per-frame draw functions, so these are the names
the developers saw on screen.

| mode | page object | getter | enable fn | title drawn | per-frame draw | input handler |
|------|-------------|--------|-----------|-------------|----------------|---------------|
| 0 | via 0x6A6E40 | 0x6A6E40 | 0x6AA5F0 | not settled | - | - |
| 1 | - | - | 0x6984F0 | not settled | - | - |
| 2 | - | - | 0x6BB670 | not settled | - | - |
| 3 | - | - | 0x6BB880 | not settled | - | - |
| 4 | - | - | 0x6BB5F0 | not settled | - | - |
| 5 | 0xCCCAD4 | 0x6B6340 | 0x6BB6D0 | Character Texture Animation | 0x6BA4D0 | 0x6B8A20 |
| 6 | 0xCCCAD0 | 0x6B62F0 | 0x6BAF60 | Basic Debug Information | 0x6BA1A0 | 0x6B7630 |
| 7 | 0xCCCAC8 | 0x6B6360 | 0x6BB480 | MapSwitching + current map name | 0x6BA610 | 0x6B9160 |
| 8 | 0xCCCAE0 | 0x6B6300 | 0x6BB3B0 | battle character parameter config | 0x6BA240 | 0x6B7AF0 |
| 9 | 0xCCCAE4 | 0x6B6350 | 0x6BB7C0 | Game parameter config | 0x6BA570 | 0x6B8CE0 |

Three more page objects exist and are drawn every frame but I did not tie them to a mode number:
0xCCCAD8 getter 0x6B6310 "Battle Debug Information" draw 0x6BA2E0 input 0x6B81D0, 0xCCCADC getter
0x6B6320 "Character Animation" draw 0x6BA390 input 0x6B85C0, 0xCCCAC4 getter 0x6B6330
"CharacterSwitching" draw 0x6BA430 input 0x6B8820. Those three sit somewhere in modes 0 to 4
alongside the two whose enable functions are 0x6AA5F0 and 0x6984F0. Settling that is five short
decompiles for whoever picks it up.

0xCCCAC0 `g_debugInfoState`, getter `sub_6B6370`, is a shared state block the pages write into
rather than a page of its own.

CORRECTION TO AN EXISTING NAME. `FFX_DebugPage_battleCharParams_enable` 0x6BB6D0 is misnamed.
setMode passes it the 0xCCCAD4 object, whose draw function prints "Character Texture Animation",
and the label initialiser for that page is the one with "Material Name:", "Group ID:",
"Animation ID:", "Current Frame:", "AutoPlay:". The page that really is battle character
parameters is mode 8, which I named `FFX_DebugPage_battleCharParams_enable_mode8` 0x6BB3B0, and
that is the one calling `FFX_DebugPage_battleCharParams_formatLine` and
`FFX_SaveData__getCharCurrentStats`. I left the pre-existing name alone as instructed, but
anything built on it will be pointing at the wrong page. `FFX_DebugPage_gameParams_enable`
0x6BB7C0 is correctly named.

### The draw path IS live

`FFX_Frame_PresentScene` 0x642BB0 calls `sub_668930` 0x668930 every frame. Between 0x66AF99 and
0x66B09C that function does, nine times over, "fetch page singleton, call its draw function,
passing the Phyre application and the measured fps". Each draw function opens with
`if (this->enabled == 0 || windowHandle == 0) return 0;` and otherwise sprintf's the title and
calls `sub_6F6930` then `sub_6F65B0`. So the moment a page's enabled flag is set, it draws. In
that loop the application pointer passed in is `*(ebx + 0xD0)` and the fps is read from
`Phyre__getApplication() + 0x30`.

Separately `FFX_Frame_UpdateBoostersAndOverlays` 0x657140 calls `FFX_DebugMenu_drawIfActive`
0x672000 every frame:

```c
if ( g_ffxDebugMode != 0 && (g_ffxPendingChrAttachCount & 0x7FFFFFFF) == 0 )
{
    if ( DebugMenuManager__getMode(DebugMenuManager__get()) == 5 )
    {
        sub_6B6370();     // g_debugInfoState
        sub_6BC210(fps);  // the mode 5 extra overlay
    }
}
```

That one does need `g_ffxDebugMode`. The nine page draws in `sub_668930` do NOT, so the pages are
reachable without touching the debug flag at all.

`sub_657A50` 0x657A50 is the matching init: it calls all eight `*_initLabels` functions once,
from `sub_636430` in the boot path.

### The menu content, from the label initialisers

`Basic Debug Information`, 13 rows, from `FFX_DebugPage_basicInfo_initLabels` 0x6B9360. Row
numbers are the switch cases in the input handler, cross-checked against the label slot order:

```
0  Character Name:                    read only
1  Character Polygon Count:           read only
2  Map Name:                          read only
3  Battle Enable:                     toggle -> FFX_Btl_SetRandomEncountersEnabled 0x782F70
4  ClothSystem Enable:                NO CASE in the input handler, a dead row
5  UI Enable:                         toggle g_debugInfoState+80 -> sub_91F640
6  MiniMap Enable:                    toggle -> sub_91F640
7  Show 4:3 Frame:                    three states, dword_1136FC0 cycles 0,1,2
8  Thunder Plain Treasure Enable:     toggle -> sub_A43210
9  Game Section:                      1..113, confirm key calls FFX_Debug_ApplyViewerSave
10 Saveload fake slot :               1..99
11 Disable CRC check:                 sets maybe_g_ffxSaveCrcSkip 0xCCB9A4 to 0 or 1
12 Achievements Reset :               toggle, and when set calls sub_6F1420
```

`Game parameter config`, 15 rows, from `FFX_DebugPage_gameParams_initLabels` 0x6B9CD0. The cheat
page:

```
FullItem:            FullCommand:              FullLevel:
FullOverdrive:       FullGill:                 Full Blitz:
Blitz Cheat:         Full Albhed Dictionary:   Full Arena Localization:
Battle Count:        PLAYTIME:                 Change Name:  Tidus
Reset Name:  All Members                       Chocobo Game Debug Enable:
PlayGo download speed:
```

`battle character parameter config`, 17 rows, from
`FFX_DebugPage_battleCharParams_initLabels` 0x6B94A0:

```
Player:   Ply Invincible:   Mon Invincible:   Ply HP1:   Mon HP1:
HP:  MP:  SP:  Strength:  Defence:  Magic:  Magic Defence:
Agility:  Luck:  Evasion:  Accuracy:  LoveParam:
```

`Battle Debug Information`, 0x6B9600: `(ro)Battle Map Name:`, `Map Index:`, `Group Number:`,
`player never die:`, `Enable Mon Input:`. A battle test launcher with a formation number and a
god mode.

`Character Animation`, 0x6B97F0: `Motion Mode:`, `Loop:`, `Next:`, `Animation Count:`.

`CharacterSwitching`, 0x6B9970: the seven CHR model category letters `c k m n s w f`, formatted
`%s%s%03d`, so "pick a model by category and number".

`Character Texture Animation`, 0x6B9AA0: `Debug Output:`, `Material Name:`, `Group ID:`,
`Animation ID:`, `Current Frame:`, `AutoPlay:`.

`MapSwitching`, 0x6B9E20: the 42 map area prefixes
`azit bika bjyt bltz bsil bsmm bsvr bsyt bvyt cdsp djyt dome genk grid guad hiku ikai kami kino
klyt lchb lmyt luca maca mcfr mcyt mihn mmmc msmm mtgz nagi omeg ptkl sins slik ssbt stbv swin
titl zkrn znkd zzzz`, formatted `%s%s%02d`. The warp menu.

### WHAT IS BROKEN: the input layer was dropped

All eight per-page input handlers are ORPHANED. IDA had not even made them into functions. I
created the functions, decompiled them, and they are intact, real code calling live engine
functions, with nothing calling them.

How I proved the orphaning, because this is the load-bearing claim of the whole document. A
`call rel32` is relative, so searching for an absolute pointer alone would miss it. I did both:

1. Read all of `.text` (0x400000..0xB0C000) raw and decoded every `0xE8` and `0xE9` byte as a
   rel32, computing the target. Zero calls and zero jumps to any of the eight.
2. Searched every segment's raw bytes for the little endian absolute dword of each entry point,
   which catches a vtable slot, a jump table entry, a `push offset` or a `mov reg, imm32`.
   Zero hits.
3. Control for the method, so the negative result means something: the same rel32 scan finds the
   one call to the Basic Info draw function at 0x66B054, the two calls to its cursor mover, and
   all twenty calls to `sub_6BC410`, and the same absolute scan finds
   `DebugMenuManager__scalar_deleting_dtor` in its vftable at 0xB48630.

| entry | bytes | cursor mover | page |
|-------|-------|--------------|------|
| 0x6B7630 | 1129 | 0x6BA8C0 | Basic Debug Information |
| 0x6B7AF0 | 1611 | 0x6BA9B0 | battle character parameter config |
| 0x6B81D0 | 955  | 0x6BAAA0 | Battle Debug Information |
| 0x6B85C0 | 597  | 0x6BAB90 | Character Animation |
| 0x6B8820 | 509  | 0x6BAC60 | CharacterSwitching |
| 0x6B8A20 | 642  | 0x6BAD30 | Character Texture Animation |
| 0x6B8CE0 | 1051 | 0x6BAE00 | Game parameter config |
| 0x6B9160 | 511  | 0x6BAEF0 | MapSwitching |

All eight are `void __thiscall (pageObject, Phyre::PApplication* app)` and all eight open with a
test of the page's enabled byte and return early if it is clear.

**CORRECTION, 2026-10-04: the enabled byte is at a DIFFERENT offset on every page, and three of
the eight also test the debug flag.** The original wording here reads as though one offset
served all eight, and a plugin that wants to ask "which page is open" by reading the byte needs
the per-page number. Verified by reading each handler's opening bytes:

| page | enabled byte | also calls FFX_IsDebugMode |
|------|--------------|----------------------------|
| Character Animation | `this+192` | **YES** |
| Character Texture Animation | `this+208` | no |
| CharacterSwitching | `this+212` | **YES** |
| Game parameter config | `this+232` | no |
| battle character parameter config | `this+252` | no |
| Basic Debug Information | `this+256` | no |
| Battle Debug Information | `this+452` | no |
| MapSwitching | `this+500` | **YES** |

The raw disassembly at 0x6b9177, which is MapSwitching, is the pattern for the three:
`cmp byte ptr [edi+1F4h], 0` / `jz` / `call FFX_IsDebugMode` / `test al, al` / `jz`. So the
debug flag is not optional for those three, and **MapSwitching, the warp page, is one of them**.
The eight offsets are in `addresses/Debug.h` as `DebugPageEnabledOff*` and the kit surfaces the
flag requirement per page through `ffx::DebugPageNeedsDebugFlag`.

They all read input the same way:

```c
up    = consumeKeyDown(app, 36, 0) || sub_626960(23, 1) || getPadAxis(app, 3, 0) < -0.5
down  = consumeKeyDown(app, 38, 0) || sub_626960(25, 1) || getPadAxis(app, 3, 0) >  0.5
left  = isKeyDown(app, 37, 0) || isPadButtonDown(app, 26, 1) || getPadAxis(app, 2, 0) < -0.5
right = isKeyDown(app, 39, 0) || isPadButtonDown(app, 24, 1) || getPadAxis(app, 2, 0) >  0.5
```

Up and down move the cursor through `moveCursor(0)` and `moveCursor(1)`. Left and right change
the value on the selected row and then call the page's line refresh `sub_6BC410`. Key 77 with
pad button 13 is the confirm on the Basic Info page's Game Section row. 36/37/38/39 and 77 are
Phyre key enum values, not Win32 VK codes, so a plugin that wants to rebind them has to go
through `Phyre::PApplication` the same way.

Note that up and down use `consumeKeyDown`, which is edge triggered and eats the key, while left
and right use `isKeyDown`, which is level triggered. So a value row changes once per frame while
held. The handlers count the held frames in `this+44` and `this+48` but nothing throttles on it.

Nothing here is stripped. There is no `nullsub` and no "return a constant" to patch around. The
only thing missing is the call.

### The exact sequence a plugin needs

```c
// once, at init or on a hotkey
*(BYTE*)(base + 0xF3C910) = 1;             // g_ffxDebugMode, for the mode 5 overlay,
                                           // for FFX_Map_WarpTo, and for the SG GUI
void* mgr = DebugMenuManager__get();       // RVA 0x2B49B0, non null after FFX_GraphicInitialize
DebugMenuManager__setMode(mgr, 7);         // RVA 0x2B4D10, __thiscall, 7 = MapSwitching

// every frame, after the game's own step, to restore input
void* page = FFX_DebugPage_getMapSwitching();          // RVA 0x2B6360
FFX_DebugPage_mapSwitching_updateInput(page, Phyre__getApplication());   // RVA 0x2B9160
```

`Phyre__getApplication` is RVA 0x223B90.

### What the input handlers reach

`Game parameter config` 0x6B8CE0 calls `FFX_Debug_FullItem`, `FFX_Debug_FullGil`,
`FFX_Debug_FullSphereLevels`, `FFX_Blitz_DebugFullBlitz`, `FFX_Btl_LoadPlayerData`,
`FFX_SaveData__setPlaytime`, `FFX_SaveData__reloadCharNames`, `DebugMenuManager__close`, plus
`sub_787220`, `sub_787350`, `sub_790420`, `sub_820360`, `sub_906740`, `sub_A431F0`. The first
four are the cheats and a plugin can call them directly with no menu at all.

`battle character parameter config` 0x6B7AF0 calls `FFX_SaveData_SetCharCurrentStats`,
`FFX_SaveData__getCharCurrentStats` and `FFX_SaveData__getLoveParam`, so it is a live stat editor
writing into the real save data. It also touches `sub_7C86C0` / `sub_7C86D0`, next door to the
SG battle windows.

`Battle Debug Information` 0x6B81D0 calls `sub_7C5FC0`, `sub_7C6D80`, `sub_7C6DA0`, `sub_7C6DB0`,
`sub_7C6DC0`, `sub_7C6DF0`, `sub_7C86B0` and `sub_78E620`. Those 0x7C6Dxx functions sit right
next to `SG_DebugWin_BattleConsoleProc` 0x7C6110 and `SG_DebugWin_BattleConfigProc` 0x7C6E20, so
the new menu and the old PS2 GUI share one battle test backend.

`Basic Debug Information` 0x6B7630 reaches `FFX_Debug_ApplyViewerSave`,
`FFX_Btl_SetRandomEncountersEnabled`, `sub_91F640`, `sub_A43210`, `sub_6F1420` and writes
`maybe_g_ffxSaveCrcSkip` and `dword_1136FC0`.

`CharacterSwitching` 0x6B8820 and `Character Texture Animation` 0x6B8A20 both call
`maybe_ViewerManager__setCharacterByName` 0x6B66D0, which is the one caller of
`FFX_Ch_DebugSpawnByName` 0x8295E0. That function takes a name like "c001", builds the CHR type
word, allocates the CHR, puts it at the player's position and calls `FFX_Ch_SetPlayerChr` on it.
A shipped spawn-and-possess. Both handlers also reference the literal `"grid00"`, the Calm Lands
map, as their fallback.

---

## 4. The SG_DebugGui family, the PS2 developer GUI

95 functions match `SG_Debug`. Size range 10 to 5882 bytes, median 129. Only two are under 16
bytes and both of those are tail-jump thunks, not stubs. **None of the window procedures is
stripped.**

### Both pumps are still wired into the main loop

* `FFX_DrawDebugOverlay` 0x8207F0, called unconditionally from `FFX_MainStep` at 0x82157A, calls
  `SG_DebugGui_DrawAll` 0x849850 when `g_ffxDebugMode` is set. Gate analysis in section 2.
* `FFX_StepPacing` 0x821E80 calls `SG_DebugGui_UpdateInput` 0x84A460 at 0x82222D when
  `g_ffxDebugMode` is set. Two instructions, no other condition:

  ```asm
  0x822224  cmp  g_ffxDebugMode, 0
  0x82222B  jz   short 0x822232
  0x82222D  call SG_DebugGui_UpdateInput
  ```

`SG_DebugGui_DrawAll` itself only has one more gate, `if (dword_130375C == 0)`, and the SG state
initialiser (which IDA has misnamed `__cfltcvt_init_88` at 0x8497A0) sets that to 0 along with
`dword_1303760 = 1` and `dword_1303730 = 128`. So the draw runs.

### How to open it: the orphan opener at 0x84F100

`SG_DebugGui_Open_ORPHAN` VA 0x84F100, RVA 0x44F100. IDA has it as data because no code or
pointer reaches it, but the bytes are a complete, stack-balanced, no-prologue cdecl function
taking no arguments and running from 0x84F100 to the `retn` at 0x84F1BC:

```asm
0x84F100  call SG_InitProtocolDriver        ; 0x84EEE0, returns >=0 on success
0x84F105  test eax, eax
0x84F107  jns  0x84F116
0x84F109  push "InitProtocolDriver() failed\n"
0x84F10E  call SG_Printf_STUB               ; 0x62F500, one byte: C3. A no-op.
...
0x84F116  push 0 / push 1 / call sub_83FBE0 / push eax / call sub_844F90
0x84F125  push offset SG_DebugGui_RootWindowProc
0x84F12A  push 2       ; flags
0x84F12C  push 0x52    ; h
0x84F12E  push 0x50    ; w
0x84F130  push 0       ; y
0x84F132  push 0x1B0   ; x
0x84F137  call SG_DebugGui_OpenWindow        ; 0x84A920
0x84F13C  call sub_84F080 / sub_84CA00 / sub_84EA00
0x84F14B  zero nine dwords at 0x1323D48..0x1323D68
0x84F17A  call sub_8531A0
...       two more SG_Printf_STUB calls and two conditional sub-window opens
0x84F1BC  retn
```

I verified orphan status independently with the same two sweeps used in section 3: zero `E8`/`E9`
rel32 references and zero absolute dword references anywhere in the image. The only absolute
reference to `SG_DebugGui_RootWindowProc` in the whole binary is the `push offset` at 0x84F126,
inside this function.

Its `add esp, 30h` at 0x84F192 accounts for exactly the 12 dwords pushed since the last cleanup,
so the function is balanced and safe to call as `((void(*)())(base+0x44F100))()`.
`SG_InitProtocolDriver` failing is not fatal, the failure branch only calls a one-byte stub.

So: **set `g_ffxDebugMode = 1`, then call RVA 0x44F100 once.** The root window opens and
`FFX_DrawDebugOverlay` will draw it from the next frame.

### The root window, and what it can do

`SG_DebugGui_RootWindowProc` 0x84F550, 3607 bytes. Seven tabs, and these are its literal widget
labels in order:

```
< CHR >   MapDir USER MAP shade Reload HideMap IDdisp0 F.P.off IDshd:1 BgColor FOG
          Alpha1st PRE2D
< MAP >   MAP Capture HIDE RATE CPU/VU1 MEMORY CacheCLR MemDump MemDumpD MotDump
          MotEv LgtDump Pause Clock Info
< SYS >   SYSTEM Console Pos Config CHR1 CHR2 CHRInfo Info
< BTL >   BATTLE REF CAM Spline Shake always BINDOFF
< CAM >   CAMERA ChrIcon HideMap Pre2d IDdisp: CPU/VU1 FP Mem MATRIX Reload
< EFF >   EFFECT Select CHRInfo TkAnti Focus Blur Flash Acc CLR FLG KeepSp:1 Movie
          HideChr TexAnim
<EVENT>   EVENT
```

It opens sub-windows through `SG_DebugGui_OpenSubWindow` 0x7C86E0 at ten sites (0x84FDDB..
0x84FE74) and through `SG_DebugGui_OpenWindow` directly at 0x84FF6D. It also prints
`"SG:call op_et_bindeff_off_all()"` and `"SG:call op_et_bindeff_all()"`, which are ATEL script
operator calls, so the EFF tab actually drives the event system.

Two item handlers the project will care about:

* `maybe_SG_DebugGui_MapJumpItemProc` 0x854DF0, called from the root proc at 0x84FC35. On item 44
  it reads the selected map out of the list widget, stores it in `g_ffxDebugMapJumpTarget`
  0x1325928, updates two labels and calls `FFX_Map_WarpTo(target, 0)`. This is the PS2 debug
  menu's warp.
* `FFX_SG_DebugWin_OpenChrAndTakeControl` 0x84CEA0, three callers, all inside the SG CHR list
  windows (0x84D29C, 0x84D5C6, 0x84D68F). Signature `int __cdecl (CHR* chr)`. It does:

  ```c
  FFX_Ch_SetPlayerChr(chr);                                  // possess it
  win = SG_DebugGui_OpenWindow(chr, 0, 0, 90, 120, 6, sub_84C1E0);
  w   = sub_84AEC0(win);
  *(DWORD*)(w + 4) = maybe_FFX_PrxPtr_Set(chr);
  strcpy(w + 80,  FFX_Ch_CategoryToDirName(chr->m_id >> 12));
  strcpy(w + 112, chr->m_name);
  SG_DebugWin_SetTitle(w, win, *(char**)(maybe_FFX_PrxPtr_Get(*(DWORD*)(w+4)) + 4));
  sub_84CC30(win, chr);
  sub_849380(win, 12, 1);
  sub_8493E0(win, 12, 1 - FFX_Ch_GetFlags1Bit800(chr));
  ```

  The first line is the whole prize: `FFX_Ch_SetPlayerChr` on an arbitrary CHR, which is
  "possess this actor". A plugin that only wants possession should call `FFX_Ch_SetPlayerChr`
  directly and skip the window, since everything after line one is UI.

Also `SG_DebugGui_OpenChrInfo` 0x8537B0 opens `SG_DebugGui_ChrInfoWindowProc` 0x8537D0, which is
already documented in the IDB as the best single source of CHR animation field offsets in the
binary.

### WHAT IS BROKEN: the pointer device read is stubbed

`SG_DebugGui_UpdateInput` 0x84A460 is 10 bytes, `call SG_DebugGui_PollPointerDevice; jmp <frame
counter>`. The counter half is fine. The poll half, which I renamed
`SG_DebugGui_PollPointerDevice` 0x851920, is:

```c
sub_855BF0(0);                                   // drain
if ( SG_Dev_DeviceRead_STUBBED(0, &g_sgDebugGuiDeviceBuf) != 0 ) { stub_print(); return 1; }
g_sgDebugGuiButtons   = g_sgDebugGuiDevButtons;        // +0x08
dword_23CBC04        += dword_1325294;                 // +0x14
g_sgDebugGuiCursorAccumX += (float)g_sgDebugGuiDevDeltaX;   // +0x0C
g_sgDebugGuiCursorAccumY += (float)g_sgDebugGuiDevDeltaY;   // +0x10
// accumX clamped 0..1024, accumY clamped 0..1248
g_sgDebugGuiCursorX = clamp((int)(0.5  * accumX), 0, 512);
g_sgDebugGuiCursorY = clamp((int)(accumY / 3.0),  0, 416);
```

`SG_Dev_DeviceRead_STUBBED` 0x855BC0 forwards to `sub_A44750`, and that function is literally
three bytes, `33 C0 C3`, `xor eax,eax / ret`. `sub_A44760`, used by the drain, is the same three
bytes. Verified by reading the bytes, not just the decompilation.

So the read reports SUCCESS and fills NOTHING. `g_sgDebugGuiDeviceBuf` at 0x1325280 has exactly
one reference in the whole binary, the `push offset` in this function, and the three fields read
out of it have no writer anywhere. The cursor sits at 0,0 and no button is ever down, forever.

`SG_DebugGui_GetMouseEdgeFlags` 0x8517C0 turns `g_sgDebugGuiButtons` into the flags the window
procs test: bits 1 / 0x100 / 0x10000 held for left / middle / right, 2 / 0x200 / 0x20000 pressed
this frame, 4 / 0x400 / 0x40000 released, and 8 for a double click within 50 frames at the same
position. `sub_849440` hit-tests widgets against `g_sgDebugGuiCursorX/Y`. So the GUI is a
conventional mouse-driven window system, and the mouse is the only thing missing.

**How to restore it, and whether it would work.** Each frame, before the game's step, write the
real mouse into the three dwords the stubbed read should have filled:

```c
*(int*)(base + 0xF25288) = buttonMask;   // bit0 left, bit1 middle, bit2 right
*(int*)(base + 0xF2528C) = deltaX;       // pixels this frame
*(int*)(base + 0xF25290) = deltaY;
```

That is exactly the data the device read was contracted to deliver, the consumer is unchanged
intact code, and nothing in the engine writes those three dwords, so there is no race with the
game. I rate this as very likely to work. Beware the axis scaling: X is halved and Y is divided
by three on the way to the cursor, so feed `deltaX * 2` and `deltaY * 3` if you want 1:1 with
screen pixels in the 512x416 debug space. As an alternative, `SG_DebugGui_SetCursorPos` 0x851AE0
sets the cursor and both float accumulators consistently in one call, `(int x, int y)`, which is
the better choice if you want to drive the cursor absolutely rather than by delta. You still need
to write `g_sgDebugGuiDevButtons` for clicks, because `PollPointerDevice` overwrites
`g_sgDebugGuiButtons` from it every frame.

### What else of the SG layer is stubbed

* `SG_Printf_STUB` 0x62F500 is one byte, `C3`. Every developer console print in the SG layer
  goes nowhere. Cosmetic, nothing depends on the return.
* `Sg_PcWrite` 0x83AFE0 is a real function but writes to `"host0:<path>"` through the file layer,
  which is the PS2 dev-host prefix. On PC that open will fail and it raises a `yiAssert` rather
  than crashing. So MemDump, MotDump, LgtDump and Capture will produce nothing useful without
  redirecting that prefix.
* Everything in the draw backend is full-body real code: `sub_848880` is a 531-byte text renderer
  with its own escape codes (`%~b` bold, `%~c RRGGBB` colour, `%~i`, `%~w`), `sub_848840` is the
  text cursor, `sub_84AC80` is 575 bytes of widget drawing, `SG_DebugWin_SetTitleRaw` 0x849610
  does real allocation. No font or texture is loaded conditionally on a debug build anywhere on
  that path.
* The 45 `SG_DebugHud_Show*` functions (0x87A6D0..0x87BC20) all have callers, through
  `sub_881BE0`, `sub_883E80` and one unanalysed dispatcher. Sizes 18 to 397 bytes, none stubbed.

### The one genuinely dead SG-era screen

`maybe_FFX_Atel__debugScreen` 0x87BD90, 6313 bytes, zero callers, no pointer. Strings
`TK:ATEL_DEBUG`, `INFO WAVE`, `INFO SE`, `INFO MUSIC`. It reads pad PORT 1 for its own
navigation, which means the developers drove it with controller 2. Hex-Rays will not decompile
it. Reviving this one is a much bigger job than the two menus above and I would not start here.

---

## 5. Every other debug or cheat gate found

| thing | what it gates | live? |
|-------|---------------|-------|
| `g_ffxDebugEncountersOn` 0xC421CC | read by `FFX_Field_StepRandomEncounter` 0x780DF8, written only by `FFX_Btl_SetRandomEncountersEnabled` 0x782F70. The PS2 debug encounter switch. | live, and the setter is callable |
| `g_ffxBlitzCheatEnabled` 0xCCCACC | read by `FFX_AtelSys_Core_613_resi` 0x85A7F0, so a live ATEL script opcode reads it. Written by the Game parameter config input handler. | live |
| `g_ffxChocoboGameDebugEnable` 0xCCCAB8 | `FFX_Asset_ResolvePathWithDebugOverrides` swaps `nagi0000.ebp` for `dbg_nagi0000.ebp`, the debug Chocobo race script. Toggled by `sub_6B6790`. | live, but needs the dbg asset present |
| `g_ffxPlayerDebugNoClip` 0x23C44B4 | `FFX_Ch_WalkmeshMove` at 0x83E6A4 and 0x83E793 lets the player leave the walkmesh. Written by `FFX_Player__readPad` at 0x82D0FB / 0x82D162, which is itself gated on `g_ffxDebugMode` at 0x82D0ED. Shipped noclip. | live, needs debug mode on |
| `maybe_g_ffxSaveCrcSkip` 0xCCB9A4 | the "Disable CRC check" row | live |
| `dword_1136FC0` 0x1136FC0 | the "Show 4:3 Frame" row, three states | live |
| `FFX_Booster_PollDebugKeyI` 0x6F7640, `..KeyJ` 0x6F7680 | raw Phyre keys 'I' and 'J', caching into `g_boosterDebugKeyIEdge` 0xCE82D8 / `..J` 0xCE82DC | DEAD, zero callers |
| `[MemoryDebug]` dumpers `sub_6DCE50`, `sub_6DCFE0`, `sub_6DD3B0`, `sub_6DD4B0` | the MemDump / MemDumpD items. Real code, but they write files. | live code, output path suspect |
| `AutoTestManager` stdin console | `EnableAutoTest`, `DisableAutoTest`, `JumpMap <name>`, `SetBattle <id>`, `Crash` | another agent owns this, not re-derived here |
| `ViewerManager` 0x6B5870 | a model viewer. `maybe_ViewerManager__setCharacterByName` 0x6B66D0 is the sole caller of `FFX_Ch_DebugSpawnByName` and is itself called only from the CharacterSwitching and Character Texture Animation input handlers, so it is reachable exactly as far as those are. | reachable via the orphan handlers |
| `g_ffxDebugCharNames` 0xC3432C | `const char*[8]`, "Tidus(0)".."Seymour(7)", read only by `FFX_DebugPage_battleCharParams_formatLine` | live |
| `g_ffxDebugCharModeNames` 0xC34318, `g_ffxDebugAuthorNames` 0xC448F0 | label tables for other debug pages | data only |
| `byte_133C912` | `g_ffxDebugMode + 2`, tested at 0xA7B016 next to a `g_ffxDebugMode` test. A second flag in the same byte triple. Not chased. | unsettled |

String sweep notes. `"debug"` / `"DEBUG"` at 0xB59148 / 0xB59150 are the two argv literals and
nothing else uses them for a gate. `"EXIT_GAME_DEBUG"`, `"SKS_DEBUG"`, `"[MemoryDebug]"`,
`"dbgPrintf.log"`, `"FFXDEBUG"` and the `read_debug.c` / `camera_debug.c` / `movie_debug.c`
source-path strings are all logging, not gates. `"Invincible"` at 0xB7C8B4 is the booster speed
icon flash asset, not a cheat.

---

## 6. What is broken, and can it be restored

| broken thing | shape of the damage | restorable? |
|--------------|---------------------|-------------|
| DebugMenuManager never opens | `setMode` has no caller but `close`. Nothing stripped. | YES, trivially. One `__thiscall`. |
| DebugMenuManager page input | all eight handlers orphaned, bodies intact | YES. Call the handler per frame. No patch. |
| SG dev GUI never opens | `SG_DebugGui_Open_ORPHAN` 0x84F100 unreferenced, body intact and balanced | YES. One call, no arguments. |
| SG dev GUI mouse | `sub_A44750` / `sub_A44760` replaced with `33 C0 C3` | YES, by filling the destination buffer yourself. Patching the stubs is NOT the move, they are shared with the save/file device paths and have 15 and 7 callers. Write the three dwords instead. |
| `SG_Printf_STUB` | one byte, `C3` | pointless to restore, nothing reads the result |
| `Sg_PcWrite` dumps | real code, `"host0:"` dev-host path | only by redirecting that prefix in the file layer |
| `maybe_FFX_Atel__debugScreen` | 6313 bytes, orphaned, Hex-Rays refuses it, drives off pad port 1 | technically yes, practically a project of its own |
| `FFX_Booster_PollDebugKeyI/J` | orphaned 52-byte pollers | yes but they do nothing by themselves, they only set an edge flag nobody reads |
| Basic Info row 4, "ClothSystem Enable" | the input handler's switch has no `case 4` | no, that row was never finished |

There is no case anywhere in either menu of a gated feature whose body became a `nullsub` or an
immediate `ret`. The stripping in this build is uniformly "the call was removed, the code stayed".
That is the best possible outcome for a plugin, because calling intact code is far safer than
re-arming a branch.

---

## 7. The cheat functions, no menu required

These take no arguments, have no gate, and write the live save data directly. All of them are
reached from the Game parameter config input handler but none of them checks anything.

```
FFX_Debug_FullGil            0x7849C0   dword_11307D8 = 999999999
FFX_Debug_FullItem           0x7849D0   112 item slots, 99 of each
FFX_Debug_OneOfEveryItem     0x784C20   112 item slots, 1 of each
FFX_Debug_ClearInventory     0x784CD0   112 slots to id 255, count 0
FFX_Debug_FullSphereLevels   0x784AE0   record[0x3B] = 98 for all 18 char records, stride 148
FFX_Debug_MaxHpMpAndStats    0x784B00   99999 HP, 9999 MP, all eight base stats to 0xFF
FFX_Blitz_DebugFullBlitz     0x7845B0   every Blitzball technique, already in Minigames.h
FFX_Debug_ApplyViewerSave    0x8B55E0   __cdecl (int id) loads savesforviewer/<id> and warps
FFX_Debug_LoadSaveForViewer  0x649040   the raw fopen under it
```

`FFX_Debug_ApplyViewerSave` is the big one for testing: it reads
`<dataroot>/FFX_Data/GameData/PS3Data/savesforviewer/<N>` straight off disk with `fopen`, not
through the VBF, memcpys 0x68C0 bytes over the live save block, and then warps to the map stored
in that save. Ids 1..113 per the Basic Info page's Game Section row. If that directory ships, it
is a one-call "jump to any point in the game".

---

## 8. Addresses to promote

RVA. Kind is fn, global or table. Everything below was read out of the IDB in this run.

| RVA | kind | suggested name | signature or type | note |
|-----|------|----------------|-------------------|------|
| 0xF3C910 | global | (already `WorldState.h` `Rva::DebugMode`) | BYTE | NOT redeclared in Debug.h, see Addresses.h ownership rule |
| 0x422550 | fn | IsDebugMode | `int __cdecl(void)` | returns the byte |
| 0x487C50 | fn | IsDebugModeDup | `int __cdecl(void)` | second copy, 25 callers |
| 0x487C80 | fn | SetDebugModeOrphan | `void __cdecl(int on)` | the only setter, zero callers |
| 0x422490 | fn | ParseCommandLineAndInitDebug | `int __cdecl(int argc, char** argv)` | argv[1] == "debug" or "DEBUG" |
| 0x75EC4F | global | DebugSetterGate | BYTE | `byte_B5EC4F`, 1 in the image, gates the setter |
| 0x8CCAB0 | global | DebugMenuManagerSingleton | void* | created by FFX_GraphicInitialize |
| 0x2B4900 | fn | DebugMenuManagerCtor | `void* __thiscall(void*)` | mode starts at 10, off |
| 0x2B4950 | fn | DebugMenuManagerCreateSingleton | `void __cdecl(void)` | called at 0x241A80 |
| 0x2B4990 | fn | DebugMenuManagerDestroySingleton | `void __cdecl(void)` | |
| 0x2B49B0 | fn | DebugMenuManagerGet | `void* __cdecl(void)` | |
| 0x2B4A80 | fn | DebugMenuManagerGetMode | `int __thiscall(void*)` | reads this+4 |
| 0x2B4D10 | fn | DebugMenuManagerSetMode | `char __thiscall(void*, int)` | THE ENTRY POINT, mode % 11 |
| 0x2B4A70 | fn | DebugMenuManagerClose | `char __thiscall(void*)` | setMode(10) |
| 0x272000 | fn | DebugMenuDrawIfActive | `int __stdcall(float fps)` | the only gate that needs DebugMode |
| 0x268930 | fn | FramePresentDebugPageDraw | `void __thiscall(...)` | draws all nine pages, per frame |
| 0x257A50 | fn | DebugPageInitAllLabels | `void __cdecl(void)` | calls the eight initLabels |
| 0x2BC210 | fn | DebugMenuMode5Overlay | `int __cdecl(float)` | |
| 0x2BC410 | fn | DebugPageRefreshLine | `int __cdecl(int row)` | 20 call sites |
| 0x8CCAC0 | global | DebugInfoState | void* | shared page state, getter 0x2B6370 |
| 0x2B6370 | fn | DebugPageGetInfoState | `void* __cdecl(void)` | |
| 0x8CCAD0 | global | DebugPageBasicInfo | void* | |
| 0x8CCAE0 | global | DebugPageBattleCharParams | void* | |
| 0x8CCAD8 | global | DebugPageBattleInfo | void* | |
| 0x8CCADC | global | DebugPageCharAnim | void* | |
| 0x8CCAC4 | global | DebugPageCharSwitching | void* | |
| 0x8CCAD4 | global | DebugPageCharTexAnim | void* | |
| 0x8CCAE4 | global | DebugPageGameParams | void* | |
| 0x8CCAC8 | global | DebugPageMapSwitching | void* | |
| 0x2B62F0 | fn | DebugPageGetBasicInfo | `void* __cdecl(void)` | |
| 0x2B6300 | fn | DebugPageGetBattleCharParams | `void* __cdecl(void)` | |
| 0x2B6310 | fn | DebugPageGetBattleInfo | `void* __cdecl(void)` | |
| 0x2B6320 | fn | DebugPageGetCharAnim | `void* __cdecl(void)` | |
| 0x2B6330 | fn | DebugPageGetCharSwitching | `void* __cdecl(void)` | |
| 0x2B6340 | fn | DebugPageGetCharTexAnim | `void* __cdecl(void)` | |
| 0x2B6350 | fn | DebugPageGetGameParams | `void* __cdecl(void)` | |
| 0x2B6360 | fn | DebugPageGetMapSwitching | `void* __cdecl(void)` | |
| 0x2B7630 | fn | DebugPageBasicInfoInput | `void __thiscall(void*, void* app)` | ORPHAN, restore by calling |
| 0x2B7AF0 | fn | DebugPageBattleCharParamsInput | `void __thiscall(void*, void* app)` | ORPHAN, stat editor |
| 0x2B81D0 | fn | DebugPageBattleInfoInput | `void __thiscall(void*, void* app)` | ORPHAN, battle test |
| 0x2B85C0 | fn | DebugPageCharAnimInput | `void __thiscall(void*, void* app)` | ORPHAN |
| 0x2B8820 | fn | DebugPageCharSwitchingInput | `void __thiscall(void*, void* app)` | ORPHAN, model swap |
| 0x2B8A20 | fn | DebugPageCharTexAnimInput | `void __thiscall(void*, void* app)` | ORPHAN |
| 0x2B8CE0 | fn | DebugPageGameParamsInput | `void __thiscall(void*, void* app)` | ORPHAN, the cheat page |
| 0x2B9160 | fn | DebugPageMapSwitchingInput | `void __thiscall(void*, void* app)` | ORPHAN, the warp page |
| 0x2BA1A0 | fn | DebugPageBasicInfoDraw | `int __thiscall(void*, int, float)` | live, per frame |
| 0x2BA240 | fn | DebugPageBattleCharParamsDraw | `int __thiscall(void*, int, float)` | live |
| 0x2BA2E0 | fn | DebugPageBattleInfoDraw | `int __thiscall(void*, int, float)` | live |
| 0x2BA390 | fn | DebugPageCharAnimDraw | `int __thiscall(void*, int, float)` | live |
| 0x2BA430 | fn | DebugPageCharSwitchingDraw | `int __thiscall(void*, int, float)` | live |
| 0x2BA4D0 | fn | DebugPageCharTexAnimDraw | `int __thiscall(void*, int, float)` | live |
| 0x2BA570 | fn | DebugPageGameParamsDraw | `int __thiscall(void*, int, float)` | live |
| 0x2BA610 | fn | DebugPageMapSwitchingDraw | `int __thiscall(void*, int, float)` | live |
| 0x2BA8C0 | fn | DebugPageBasicInfoMoveCursor | `int __cdecl(int dir)` | 0 up, 1 down |
| 0x2BA9B0 | fn | DebugPageBattleCharParamsMoveCursor | `int __cdecl(int dir)` | |
| 0x2BAAA0 | fn | DebugPageBattleInfoMoveCursor | `int __cdecl(int dir)` | |
| 0x2BAB90 | fn | DebugPageCharAnimMoveCursor | `int __cdecl(int dir)` | |
| 0x2BAC60 | fn | DebugPageCharSwitchingMoveCursor | `int __cdecl(int dir)` | |
| 0x2BAD30 | fn | DebugPageCharTexAnimMoveCursor | `int __cdecl(int dir)` | |
| 0x2BAE00 | fn | DebugPageGameParamsMoveCursor | `int __cdecl(int dir)` | |
| 0x2BAEF0 | fn | DebugPageMapSwitchingMoveCursor | `int __cdecl(int dir)` | |
| 0x2BAF60 | fn | DebugPageBasicInfoEnable | `char __cdecl(int on)` | mode 6 |
| 0x2BB480 | fn | DebugPageMapSwitchingEnable | `char __cdecl(int on)` | mode 7 |
| 0x2BB3B0 | fn | DebugPageBattleCharParamsEnable | `char __cdecl(int on)` | mode 8, the REAL stat page |
| 0x2BB7C0 | fn | DebugPageGameParamsEnable | `char __cdecl(int on)` | mode 9, cheats |
| 0x2BB6D0 | fn | DebugPageCharTexAnimEnable | `char __thiscall(void*, int on)` | mode 5, misnamed in the IDB |
| 0x2BB5F0 | fn | DebugPageEnableMode4 | `char __cdecl(int on)` | unidentified page |
| 0x2BB670 | fn | DebugPageEnableMode2 | `char __cdecl(int on)` | unidentified page |
| 0x2BB880 | fn | DebugPageEnableMode3 | `char __cdecl(int on)` | unidentified page |
| 0x2AA5F0 | fn | DebugPageEnableMode0 | `char __thiscall(void*, int on)` | unidentified page |
| 0x2984F0 | fn | DebugPageEnableMode1 | `char __cdecl(int on)` | unidentified page |
| 0x2B9360 | fn | DebugPageBasicInfoInitLabels | `int __thiscall(void*)` | 13 labels |
| 0x2B94A0 | fn | DebugPageBattleCharParamsInitLabels | `int __thiscall(void*)` | 17 labels |
| 0x2B9600 | fn | DebugPageBattleInfoInitLabels | `int __thiscall(void*)` | 5 labels |
| 0x2B97F0 | fn | DebugPageCharAnimInitLabels | `int __thiscall(void*)` | 4 labels |
| 0x2B9970 | fn | DebugPageCharSwitchingInitLabels | `int __thiscall(void*)` | 7 category letters |
| 0x2B9AA0 | fn | DebugPageCharTexAnimInitLabels | `int __thiscall(void*)` | 6 labels |
| 0x2B9CD0 | fn | DebugPageGameParamsInitLabels | `int __thiscall(void*)` | 15 labels, the cheats |
| 0x2B9E20 | fn | DebugPageMapSwitchingInitLabels | `int __thiscall(void*)` | 42 map prefixes |
| 0x2B69A0 | fn | DebugPageBasicInfoFormatLine | `(void*, int row, char* out)` | |
| 0x2B6BB0 | fn | DebugPageBattleCharParamsFormatLine | `(void*, int row, char* out)` | reads DebugCharNames |
| 0x2B72E0 | fn | DebugPageGameParamsFormatLine | `(void*, int row, char* out)` | |
| 0x44F100 | fn | SgDebugGuiOpenOrphan | `void __cdecl(void)` | ORPHAN. Opens the PS2 dev GUI. |
| 0x44F550 | fn | SgDebugGuiRootWindowProc | window proc | seven tabs |
| 0x44A920 | fn | SgDebugGuiOpenWindow | `void* (x, y, w, h, flags, proc)` | 50 call sites |
| 0x3C86E0 | fn | SgDebugGuiOpenSubWindow | `void* __usercall(int, void*)` | ~11 sub windows |
| 0x449850 | fn | SgDebugGuiDrawAll | `void __cdecl(int, void*)` | LIVE from DrawDebugOverlay |
| 0x44A460 | fn | SgDebugGuiUpdateInput | `int __cdecl(void)` | LIVE from StepPacing |
| 0x451920 | fn | SgDebugGuiPollPointerDevice | `int __cdecl(void)` | the broken one |
| 0x455BC0 | fn | SgDevDeviceReadStubbed | `int __cdecl(int, void*)` | forwards to a ret-0 stub |
| 0x644750 | fn | SgDevRemoteReadStubRet0 | `int __cdecl(void)` | literally 33 C0 C3 |
| 0x451AE0 | fn | SgDebugGuiSetCursorPos | `int __cdecl(int x, int y)` | sets cursor and accumulators |
| 0x4517C0 | fn | SgDebugGuiGetMouseEdgeFlags | `int __cdecl(void)` | held/pressed/released/dblclick |
| 0x451750 | fn | SgDebugGuiGetCursorX | `int __cdecl(void)` | |
| 0x451760 | fn | SgDebugGuiGetCursorY | `int __cdecl(void)` | |
| 0xF25240 | global | SgDebugGuiCursorX | int | 0..512 |
| 0xF25244 | global | SgDebugGuiCursorY | int | 0..416 |
| 0xF25248 | global | SgDebugGuiCursorAccumX | float | 0..1024, cursorX = 0.5 * this |
| 0xF2524C | global | SgDebugGuiCursorAccumY | float | 0..1248, cursorY = this / 3 |
| 0xF25260 | global | SgDebugGuiButtons | int | copied from the device buffer each frame |
| 0xF25280 | global | SgDebugGuiDeviceBuf | BYTE[32] | the stubbed read's destination |
| 0xF25288 | global | SgDebugGuiDevButtons | int | WRITE THIS, bit0 L bit1 M bit2 R |
| 0xF2528C | global | SgDebugGuiDevDeltaX | int | WRITE THIS |
| 0xF25290 | global | SgDebugGuiDevDeltaY | int | WRITE THIS |
| 0xF25920 | global | SgDebugGuiTextLineY | int | Printf line cursor, +7 per line |
| 0xF25928 | global | DebugMapJumpTarget | int | the SG warp menu's selected map |
| 0x453E40 | fn | SgDebugGuiPrintf | `int __cdecl(const char*, ...)` | vsprintf + draw one line |
| 0x454DF0 | fn | SgDebugGuiMapJumpItemProc | `void __usercall(int, int item)` | item 44 calls FFX_Map_WarpTo |
| 0x44CEA0 | fn | SgDebugWinOpenChrAndTakeControl | `int __cdecl(CHR*)` | FFX_Ch_SetPlayerChr then a window |
| 0x4537B0 | fn | SgDebugGuiOpenChrInfo | `void* __cdecl(...)` | the CHR field dump window |
| 0x44CE10 | fn | SgDebugGuiFillChrList | `(...)` | |
| 0x453DC0 | fn | SgDebugGuiFillChrList2 | `(...)` | |
| 0x44A470 | fn | SgDebugWinAddWidget | `(...)` | ~400 call sites |
| 0x44B6A0 | fn | SgDebugWinSetTitle | `(int, int, char*)` | |
| 0x449610 | fn | SgDebugWinSetTitleRaw | `(int, int, char*)` | |
| 0x22F500 | fn | SgPrintfStub | `void __cdecl(...)` | one byte, C3 |
| 0x43AFE0 | fn | SgPcWrite | `void __cdecl(int, int, const char*)` | writes to "host0:" |
| 0x4207F0 | fn | DrawDebugOverlay | `int __cdecl(void)` | the SG draw door |
| 0x421E80 | fn | StepPacing | `void __cdecl(float)` | the SG input door |
| 0xEFBBB3 | global | SgDrawEnable | BYTE | set to 1 by MainInit, must be nonzero |
| 0x420420 | fn | SetSgDrawEnable | `int __cdecl(char)` | writes the byte above |
| 0xEFBBB8 | global | EffectDebugTickEnable | BYTE | the sub_7E6560 half of the MainStep gate |
| 0x3E6560 | fn | EffectDebugTick | `int __cdecl(void)` | 28 bytes, draws nothing |
| 0x3849C0 | fn | DebugFullGil | `void __cdecl(void)` | 999999999 |
| 0x3849D0 | fn | DebugFullItem | `int __cdecl(void)` | 99 of all 112 items |
| 0x384C20 | fn | DebugOneOfEveryItem | `int __cdecl(void)` | |
| 0x384CD0 | fn | DebugClearInventory | `int __cdecl(void)` | |
| 0x384AE0 | fn | DebugFullSphereLevels | `void __cdecl(void)` | |
| 0x384B00 | fn | DebugMaxHpMpAndStats | `void __cdecl(void)` | |
| 0x4B55E0 | fn | DebugApplyViewerSave | `void __cdecl(int id)` | id 1..113, loads and warps |
| 0x249040 | fn | DebugLoadSaveForViewer | `int __cdecl(int id, void* dst, size_t n)` | raw fopen |
| 0x382F70 | fn | BtlSetRandomEncountersEnabled | `void __cdecl(int on)` | writes DebugEncountersOn |
| 0x8CB9A4 | global | SaveCrcSkip | BYTE | "Disable CRC check" |
| 0xD36FC0 | global | Show43Frame | int | 0, 1, 2 |
| 0x42D830 | fn | PlayerIsDebugNoClipHeld | `int __cdecl(void)` | |
| 0x1FC44B4 | global | PlayerDebugNoClip | int | set by readPad under the debug gate |
| 0x4295E0 | fn | ChDebugSpawnByName | `void __usercall(char* name)` | "c001" style, spawns and possesses |
| 0x438060 | fn | ChIdToDebugName | `(...)` | |
| 0x2B5870 | fn | ViewerManagerCtor | `void* __thiscall(void*)` | model viewer |
| 0x2B66D0 | fn | ViewerManagerSetCharacterByName | `(...)` | sole caller of ChDebugSpawnByName |
| 0x47BD90 | fn | AtelDebugScreenOrphan | `char __cdecl(void)` | 6313 bytes, dead, pad port 1 |
| 0x2F7640 | fn | BoosterPollDebugKeyI | `int __cdecl(void)` | dead |
| 0x2F7680 | fn | BoosterPollDebugKeyJ | `int __cdecl(void)` | dead |
| 0x83432C | table | DebugCharNames | `const char*[8]` | already in GameState.h |
| 0x834318 | table | DebugCharModeNames | `const char*[]` | |
| 0x8448F0 | table | DebugAuthorNames | `const char*[]` | |
| 0x223B90 | fn | PhyreGetApplication | `void* __cdecl(void)` | needed by every input handler |
| 0x226A00 | fn | PhyreConsumeKeyDown | `bool __thiscall(void*, int key, int)` | edge, consumes |
| 0x229D50 | fn | PhyreIsKeyDown | `bool __thiscall(void*, int key, int)` | level |
| 0x229CA0 | fn | PhyreIsPadButtonDown | `bool __thiscall(void*, int btn, unsigned)` | |
| 0x228460 | fn | PhyreGetPadAxis | `float __thiscall(void*, int axis, int)` | axis 2 = X, 3 = Y |

---

## 9. What I could not settle

* Which of modes 0 to 4 is which page. Five enable functions and three drawn page objects are
  unmatched. Five short decompiles away.
* Whether `FFX_ParseCommandLineAndInitDebug` is handed the real process `argv`. The parse is
  certain, the plumbing above it is not.
* Whether `SG_DebugGui_DrawAll`'s display list actually reaches the Phyre renderer on PC. Every
  function on the path is full-body real code and the gates open, but I stopped before the final
  vertex submission.
* `byte_133C912`, two bytes past `g_ffxDebugMode`, tested at 0xA7B016. A separate flag in the
  same triple, unchased.
* The `savesforviewer` directory. `FFX_Debug_ApplyViewerSave` would be the single most useful
  cheat in the binary if those files ship, and I did not check the install.
* `sub_626960`, used as the pad-button half of up and down in every page input handler. It takes
  `(button, 1)` rather than an application pointer, so it reads some cached pad state. Not chased.

---

## 10. Which claims I verified against the disassembly

Verified by reading instructions or raw bytes, not by trusting Hex-Rays or an xref list:

* The argv parse in `FFX_ParseCommandLineAndInitDebug`, read as instructions.
* The `FFX_SetDebugMode_ORPHAN` body at 0x887C80, read as instructions.
* `FFX_DrawDebugOverlay`'s three inner conditions, including that `cmp byte_12FBBB3, al` at
  0x820816 really is a compare against zero because `al` is 0 on that path.
* `FFX_StepPacing`'s two-instruction gate on `SG_DebugGui_UpdateInput` at 0x822224.
* `sub_820420(1)` in `FFX_MainInit` at 0x8209C7 runs unconditionally. I audited every
  instruction from 0x820967 to 0x8209E5 for a branch or return and there is none.
* `DebugMenuManager__createSingleton` is an unconditional call in `FFX_GraphicInitialize` at
  0x641A80, with no test before it.
* `sub_668930` is the first thing `FFX_Frame_PresentScene` 0x642BB0 does, unconditionally.
* `DebugMenuManagerSetMode` has exactly one caller, `close`, confirmed twice: by IDA xrefs and
  by the independent rel32 byte sweep.
* The eight orphaned input handlers and `SG_DebugGui_Open_ORPHAN` have zero rel32 references and
  zero absolute references, confirmed with a control that proves the sweep finds real ones.
* `sub_A44750` and `sub_A44760` are `33 C0 C3`, read as bytes.
* `SG_Printf_STUB` is `C3`, one byte, read as bytes.
* `ida_bytes.is_loaded` is False for `dword_1301804`, `dword_1301940`, `byte_12FBBB3`,
  `dword_130375C` and the whole SG cursor block, which is how I know they start at zero rather
  than at the 0xFF that `get_byte` reports for an unmapped `.data` tail.

Inferred, not instruction-verified:

* That `FFX_ParseCommandLineAndInitDebug` is handed the real process `argv`.
* That `SG_DebugGui_DrawAll`'s display list reaches the screen on PC. Every function on the path
  is full-body code and the gates open, but I did not follow the submission to the renderer.
* The mode-to-page mapping for modes 0 to 4.
* The exact `<-- row` pairing for Basic Info rows 5 and 6 (UI Enable and MiniMap Enable). Both
  call `sub_91F640`, and I assigned them by label slot order, not by watching the engine.
