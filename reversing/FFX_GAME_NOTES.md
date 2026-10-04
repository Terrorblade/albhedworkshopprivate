# FFX game-specific map

Addresses are VAs at the preferred base `0x400000`, which is what IDA shows. **The exe is ASLR'd** -
runtime code must use `GetModuleHandle(NULL) + (VA - 0x400000)`. See `README.md`.

A name prefixed `maybe_` is a guess. Everything else here was derived from strings, asserts, RTTI or
call structure, and the matching function in the IDB carries a comment saying what the evidence was.

## The entity model: `ClassCharacter` is not the actor

`??_7ClassCharacter@@6B@` at `0xb47e0c` has exactly **one** vtable slot (the deleting destructor) and
no RTTI base classes. It is a **36-byte handle for a loaded character asset bundle**, not a live
actor. Several other FFX managers are the same shape - `AsyncLoadManager`, `AutoTestManager`,
`DebugMenuManager`, `ViewerManager`, `TextureAnimationManager`, `FmodManager`,
`PostProcessTweakUtil` each have a single vtable slot. They are singletons with non-virtual APIs, so
vtable walking yields almost nothing for them and the productive route was the embedded PS2 source
filenames and assert strings.

`ClassCharacter` layout (from ctor `0x69E030`, dtor `0x69E830`, loader `0x663F20`, instancer
`0x63C540`):

| off | field |
|---|---|
| +0x00 | vtable |
| +0x04 | loaded Phyre cluster / asset root (mesh list reachable at `*(+4)+28`) |
| +0x08/+0x0C/+0x10 | `{data, size, capacity}` vector of sub-asset blobs |
| +0x14 | array of Phyre asset handles |
| +0x18 | count for +0x14 |
| +0x1C | BYTE loaded/ready (also set to 1 on load *failure*) |
| +0x20 | BYTE isHiLod - set when asset id is 4347/4348/4349/4352 or in (800,900) |

Methods: `ClassCharacter__ctor 0x69E030`, `__dtor 0x69E830`, `__scalar_deleting_dtor 0x69EE90`,
`__loadFromFile 0x663F20`, `__createInstance 0x63C540`, `ClassCharacterTable__find 0x6A2110`,
`ClassCharacterTable__get 0x69F6C0`, `FFX_LoadCharacterData 0x63D140`,
`FFX_EnqueueCharacterLoad 0x6642F0`.

### The real per-entity actor is the ported PS2 `CHR` struct

`FFX_Ch_Init 0x826EA0` prints `sizeof(CHR)=0x880` (2176 bytes). Array base pointer
`g_ffxChrArray = 0x23C44E4`, count `g_ffxChrCount = 0x23C44E0`, so `chr = g_ffxChrArray + 0x880 * i`.
Every loop in the game does exactly that and skips entries whose `m_inUse` byte is 0. **Being in the
array with `m_inUse` set is the whole registration** - there is no separate per-frame or render
sign-up call.

**The full layout lives in `ffx_types.h`** and is applied to the IDB as the `CHR` local type
(0x880 exactly, 0 parse errors), along with `CHRDATA` (300), `CHRPART` (56) and `CHRANIMF` (12).
Read that file rather than a table here. **`PHASE1_CLONE.md` is the write-up oriented at spawning a
second controllable character.** Highlights:

- `+0x00` id, where `id >> 12` is the category (0 pc, 1 mon, 2 npc, 3 sum, 4 wep, 5 obj, 6 skl,
  0xF viewer prototype). **Tidus is id 1, which formats as `"c001"`.**
- `+0x154` `m_speed` and `+0x168` `m_moveDir` are the entire movement input for any CHR.
- `+0x158` `m_rotY` facing, slewed toward `m_moveDir` when `m_flags1` bit `0x400` is set.
- `+0x180` `m_hideFlags` is a **hide** mask, 0 meaning visible. See the polarity note below.
- `+0x19C` `m_parent`. Children are found by scanning the array for `m_parent == this`.
- `+0x75C` `m_slots[32]`, the motion slot table: `[0]` idle, `[1]` walk, `[2]` run.
- `+0x830` and `+0x838` are the two Phyre mesh-instance lists. `+0x834` is a cloned
  `ClassCharacter`, not an instance.
- `+0x1C8` is the embedded root joint node, 352 bytes, which lands exactly on `m_joints` at `+0x328`.

Three corrections to earlier passes, each re-verified against the binary:

1. **`FFX_Ch_SetHidden 0x82B4E0` (was named `FFX_Ch_SetVisible`) is inverted - its argument is
   hide.** The debug CHRInfo window prints `CHR+0x180` as `"Hide   : %x"` with bit labels, every
   per-frame pass skips a CHR whose value is non-zero, and `FFX_ChrInstance_SetHidden 0x63C390` is
   itself inverted. The whole `Vis`-named family has been renamed to `Hide`.
   **The label strings are not in bit order.** They sit at `0xB5B6F8` as `" Ev" " Eff" " Btl" " Disp"
   " Z"`, which is the order the window prints them, but the `test` order in
   `SG_DebugGui_ChrInfoWindowProc` at `0x85395C`-`0x853A0B` is bit `0x01` Ev, `0x02` Eff, `0x10` Btl,
   `0x08` Disp, `0x20` Z. So **Disp is `0x08` and Btl is `0x10`**, and `0x04` carries no label.
   `FFX_Ch_UpdateCameLenAndZClip` masking with `0xD7` (clearing `0x08|0x20`) agrees.
2. **`CHR+0x174` is not a collision radius.** The debug window prints it as `"ClipZ"` and `+0x178` as
   `"CameLen"`. The real collision radius is `+0x4E8`, recomputed each step from the scale times
   `+0x4E4`. `FFX_Ch_SetCollisionRadius` is now `FFX_Ch_SetClipZ 0x82B150`.
   The pair is a draw-distance test, not camera control: `FFX_Ch_UpdateCameLenAndZClip 0x82E220`
   writes `m_cameLen` every frame as the distance from `m_bodyCentre` (`+0x694`, not the origin) to
   the active camera eye, then sets hide bit `0x20` when it exceeds `m_clipZ`. **`m_cameLen` is an
   output.** Writing it changes nothing but one frame of Z-culling.
3. **`camSetScrShake 0x7DFDE0` was a bad name of mine** and is now
   `SG_DebugWin_CameraShakeEditorProc`. It sets its window title to `"Shake Editor"` and has
   `[HORIZON]`/`[VERTICAL]` speed, acc, proc, random and time fields. It *emits* the
   `camSetScrShake(...)` script text rather than being that command. A reminder that the
   name-from-own-string pass mis-fires on debug windows that print script source.
3. **`+0xBC` and `+0x330`/`0x33C`/`0x348` are shade colour, not scale.** Settled by following
   `FFX_ChrInstance_SetShadeColor 0x63D050` into `sub_6AB010`, which looks up the shader constant
   named `"CharacterShadeColor"`, and `FFX_ChrInstance_SetShadeCoeff 0x63CDD0` into `sub_6AAEF0`,
   which looks up `"CharacterShadeCoeff"`. The `Scale`/`Alpha` names in that family are now `Shade`.

Two things a mod has to respect:

1. `FFX_Ch_MoveChrMemory 0x826FA0` does **not** relocate CHRs - it compacts the heap blocks a CHR
   points at (`m_joints`, `m_collisionVolumes`, `m_parts`, `m_channels`, the motion pointers). So a
   `CHR *` is stable for the life of the pool, but any pointer read *out of* a CHR must be re-read.
   The pool itself is freed and reallocated on every map transition, so nothing survives that.
2. `FFX_Ch_AllocChrArray 0x825670` allocates `2 * count * 0x880`. The upper half is **not** spare
   capacity - it is the shadow area `FFX_Ch_CopyToScratchSlot 0x828B30` uses for map save and
   restore. The sanctioned headroom is the `+8` that `maybe_FFX_Map_SetupChars 0x875510` adds when
   it sizes the pool (floor 36).

CHRDATA is a fixed table, not a heap object: 40 entries of 300 bytes at
`g_ffxChrDataTable 0x23C8D00`, a free entry marked by `m_id == -1`.
`FFX_Ch_BlkAllocate 0x825760` takes the first free one, `FFX_Ch_FindChrData 0x825EA0` looks one up
by id.

`g_ffxTidusChr 0x12FBC60` **caches the CHR whose CHRDATA name is `c001` or `c101`**, i.e. Tidus.
`FFX_Ch_BindChrData 0x826070` is the only writer and does the string compare itself.
`g_ffxControlledChr 0x1300788` is the separate "who the pad drives" binding
(`FFX_Ch_GetPlayerChr 0x82D860` / `FFX_Ch_SetPlayerChr 0x82DAD0`).

Per-frame passes over the whole array, all called straight from `FFX_MainStep 0x820AE0`:

| address | name | what it does |
|---|---|---|
| `0x832DB0` | `FFX_Ch_ClearFlags1StepBits` | clears `m_flags1 & 0x07000000`, the per-frame scratch bits |
| `0x832E10` | `FFX_Ch_UpdateMotionAll` | turn, animate, integrate velocity, collide, ground clamp |
| `0x833390` | `FFX_Ch_DispatchInBatches` | the parented CHRs plus motion events |
| `0x82ED50` | `FFX_Ch_StepAll` | per-viewport render state, 4272 bytes |
| `0x830180` | `FFX_Ch_ShadowPassAll` | only CHRs with `m_flags2` bit 0x10 |

Both batch passes queue `FFX_Ch_UpdateRenderJob 0x833530` on the Phyre worker pool via
`FFX_Ch_DispatchUpdateJob 0xA44B90`. Render side: `FFX_Ch_BuildSkinMatrices 0x832760` builds the
joint palette and `FFX_ChrInstance_UploadBoneMatrices 0x63BE60` memcpys it into the Phyre mesh
instances. **FFX does not use PhyreEngine's animation runtime at all** - of 2,464 `PAnimation*`
symbols only three have a code xref outside the reflection registration region.

CHR API: `FFX_Ch_SetPos 0x82B480`, `FFX_Ch_SetPosXZ 0x82B440`, `FFX_Ch_SetRot 0x82B520`,
`FFX_Ch_SetRotAndMoveDir 0x82B1B0`, `FFX_Ch_SetMoveSpeed 0x82B840`, `FFX_Ch_SetMoveDir 0x82B190`,
`FFX_Ch_SetHidden 0x82B4E0`, `FFX_Ch_SetScaleMasked 0x82B560`, `FFX_Ch_SetScaleUniform 0x82B590`,
`FFX_Ch_SetShadeCoeff 0x82B720`, `FFX_Ch_SetClipZ 0x82B150`, `FFX_Ch_SetSlot 0x82AFE0`,
`FFX_Ch_Allocate 0x824F90`, `FFX_Ch_AllocateWithObjId 0x825650`, `FFX_Ch_Dispose 0x8266F0`,
`FFX_Ch_DisposeIfLive 0x8722F0`, `FFX_Ch_DisposeAll 0x8268E0`, `FFX_Ch_LoadModel 0x82A1C0`,
`FFX_Ch_ModelSetHide 0x82AEA0`, `FFX_Ch_ModelSetAlpha 0x82AE30`,
`FFX_Ch_ModelCreatePacket 0x827280`, `FFX_Ch_ModelPurgePacket 0x827700`,
`FFX_Chr_AttachModelInstance 0x63D370`, `FFX_Chr_CreateInstanceNow 0x637880`,
`FFX_Chr_ProcessPendingAttachments 0x639DB0`, `FFX_Ch_CopyState 0x828620`,
`FFX_Ch_CountLive 0x8285E0`, `FFX_Ch_NextLiveChr 0x826D70`, `FFX_Ch_IsLiveChr 0x8261B0`,
`FFX_Ch_FindById 0x8261F0`, `FFX_Ch_AttachToParentBone 0x832630`,
`FFX_Ch_DebugSpawnByName 0x8295E0`.
Instance-side setters: `FFX_ChrInstance_SetShadeColor 0x63D050`,
`FFX_ChrInstance_SetShadeCoeff 0x63CDD0`, `FFX_ChrInstance_SetHidden 0x63C390`,
`FFX_ChrInstance_SetPartHidden 0x63BDF0`, `FFX_ChrInstance_SetPartAlpha 0x63BC50`,
`FFX_ChrInstance_UploadBoneMatrices 0x63BE60`.

## Frame loop

```
WinMain 0x62EE50
  FFX_GameSettings__loadIni 0x401880
      %DOCUMENTS%\Square Enix\FINAL FANTASY X&X-2 HD Remaster\GameSetting.ini
      -> g_ffxGameSettings 0x22FB504
  Phyre__PApplication__run 0x629A40
    Phyre__PApplication__onInit 0x62A600     -> vtbl+8 initApplication, vtbl+12 initScene
                                                installs the frame fn at app+616
    Phyre__PApplication__frameTick 0x627940  (per frame)
       vtbl+28  FFXApplication__update   0x42F770 -> FFX_Input__updateFromFFXApp 0x630E80
       vtbl+16  FFXApplication__animate  0x42F520 -> FFX_MainStepLoop 0x822840
                                                       -> FFX_MainStep 0x820AE0
                                                    -> FFX_GameTick_639300 0x639300
       per-viewport render
       vtbl+32  FFXApplication__render   0x42F930
       vtbl+36  FFXApplication__resize   0x42F9A0
       vtbl+40  FFXApplication__endFrame 0x42F4F0
```

`FFXApplication` vtable at `0xb0d998`, 12 slots: 0 dtor `0x42F150`, 1 `0x42F8F0` (releases the 9
dynamic-mesh singletons), 2 `initApplication 0x42F7F0`, 3 `initScene 0x42F8E0`,
4 `animate 0x42F520`, 5 `exitScene 0x42F660`, 6 `exitApplication 0x42F600`, 7 `update 0x42F770`,
8 `render 0x42F930`, 9 `resize 0x42F9A0`, 10 `endFrame 0x42F4F0`, 11 `0x626C90` (inherited).

`FFX_MainStep 0x820AE0` is the ported PS2 main-loop body (field / battle / event / menu / VFX).
`FFX_StepPacing 0x821E80` computes `g_ffxPendingSteps 0x12FB808`, the extra catch-up steps
`FFX_MainStepLoop` runs. `FFXApplication+941` caches `FFX_EscMenu__isOpen 0x63DA80` and **skips the
whole game step while the pause menu is up**. `g_ffxApplication = 0xCC9CD8`,
`Phyre__getApplication 0x623B90`, target frame time `g_ffxTargetFrameTime 0xCC9D00`.

**`app + 0x268` (616) holds the per-frame driver function pointer**, installed by `onInit` to
`frameTick 0x627940` or the script-replay variant `0x62C6F0`. `WinMain` calls through it every pump
iteration, so overwriting that one dword is a frame hook needing no vtable patch.

## Input

```
XInputGetState/SetState  <- PInputDevicePadXInput__vf05 0x62DBA0
DirectInput8Create       <- Phyre__PInput__createDirectInput8 0x624350  (from onInit)
Phyre__PInput__updateAndCheck 0x62E340 -> Phyre__PInput__update 0x625770
FFXApplication__update 0x42F770
  -> FFX_Input__updateFromFFXApp 0x630E80
       -> FFX_Input__poll 0x632A30            <-- best injection point
       -> Iggy menu dispatch 0x68A3A0 / 0xA25AB0 / 0xA26CE0
FFX_MainStep 0x820AE0
  -> FFX_Pad__updateAll 0x889A80 -> FFX_Pad__scePadStateMachine 0x889050
       -> sub_888F90 -> FFX_Pad__fillSceReadData 0x8898A0 -> FFX_Input__getButtonMask 0x630C80
  game code reads via FFX_Pad__readButtons16 0x888D70 / FFX_Pad__getPortState 0x888EC0
```

`g_ffxInput = 0xCCB170`: +0 current PS2 button mask, +4 previous, +140/144/148/152 analog LX/LY/RX/RY,
+164 the action-map vector, and **+172/+176/+180 an override ctx/arg/callback - if the callback at
+180 sets its out-param non-zero, `FFX_Input__poll` skips the entire poll.** Nothing in the shipped
binary installs it, so that is a free, purpose-built input hook.

`FFX_Input__buildActionMap 0x6315C0` builds 75 `{PInputMap action id, PS2 pad bit}` pairs; action ids
come from `g_ffxGameSettings+96`. Bits are standard SCE: 0x1000 up, 0x4000 down, 0x8000 left,
0x2000 right, 0x10 triangle, 0x20 circle, 0x40 cross, 0x80 square, 0x04 L1, 0x08 R1, 0x01 L2,
0x02 R2, 0x100 select, 0x800 start.

`g_ffxThreadedPadMode 0x133C930`: when non-zero (Blitzball, set by `sub_891B80`) polling moves to
`FFX_InputThread__main 0x68D740` / `__pushSample 0x68CC10`, consumed by
`FFX_Input__consumeThreadedSample 0x630DE0`. The emulated `scePad` buffers are at `0x13304C0`
(32-byte stride, `FFX_Pad__getPortBuffer 0x888E10`); per-port state structs come from
`FFX_Pad__getPortState 0x888EC0` (0x100 stride, buttons at +152/+154).

PhyreEngine also ships input record/replay driven from the command line: `-script=`, `-input=`,
`-timestep=`, with commands `Keypress`/`KeyDown`/`Buttonpress`/`InputSource`/`Frames`/`Expect`
(`Phyre__InputScript__buttonFromName 0x62C730`). A second supported injection channel.

## Debug facilities, and exactly what gates them

**One flag: `g_ffxDebugMode` at `0x133C910`.** Written in exactly one place,
`FFX_ParseCommandLineAndInitDebug 0x822490`:

- `argc <= 1` -> 0 (store at `0x822512`)
- `strcmp(argv[1], "debug") == 0` **or** `strcmp(argv[1], "DEBUG") == 0` -> 1 (store at `0x822509`)
- anything else -> 0

Only `argv[1]`, case-sensitive, exact match. Getter `FFX_IsDebugMode 0x822550` (duplicate at
`0x887C50`); ~40 more sites test the byte inline. Chain:
`FFXApplication__initScene 0x42F8E0` -> `FFX_BootSequence 0x6416C0` -> `0x636410` -> `0x822490`.

What it unlocks: `FFX_StepPacing 0x821E80` calls `SG_DebugGui_UpdateInput 0x84A460` every frame;
`FFX_DrawDebugOverlay 0x8207F0` calls `SG_DebugGui_DrawAll 0x849850` every frame (also needs
`byte_12FBBB3 != 0` and `sub_83A620() == 0`); `FFX_MainInit 0x820860` starts at game section 0
instead of 23 and loads the event-id table; `FFX_Ch_Init 0x826EA0` loads
`/ffx/proj2/chr/common/<pc|mon|npc|sum|wep|obj|skl>.tbl`; `FFX_InitNewSaveData 0x786B00` sets the
scenario word to 5; `FFX_DebugMenu_drawIfActive 0x672000` and the HUD pages become reachable.

**The original PS2 "SG" developer GUI is fully intact and wired into the frame loop.**
`SG_DebugGui_OpenWindow 0x84A920`, root proc `SG_DebugGui_RootWindowProc 0x84F550` with CHR / MAP /
SYS / BTL / EVENT tabs (MemDump, MotDump, LgtDump, Capture, Pause, Clock, Console, CHRInfo, TexAnim,
Reload...), sub-windows via `SG_DebugGui_OpenSubWindow 0x7C86E0` (camera info `0x7CFCD0`,
battle-position editor `0x7D80A0`, camera filter `0x7D9C20`, spline editor `0x93EE10`). The only
missing piece is the routine that opens the root window: it is **orphaned dead code at `0x84F100`**
(labelled `SG_DebugGui_Open_ORPHAN`), with no code xrefs and no dword pointer to it anywhere in
`.text`/`.rdata`/`.data`. Calling it with `g_ffxDebugMode = 1` should restore the whole dev menu.

**`DebugMenuManager`** (`g_debugMenuManager 0xCCCAB0`) is constructed unconditionally by
`FFX_GraphicInitialize 0x6419D0`, but `DebugMenuManager__setMode 0x6B4D10` has exactly one caller -
`DebugMenuManager__close 0x6B4A70`, which passes 10 (= off). So the 10 HUD pages are never opened by
shipped code and a mod must call `setMode(0..9)` itself. Page 5 is the battle character parameter
editor (HP/MP/Strength/.../invincibility); page 9 is the cheat page (FullItem, FullGill, FullLevel,
FullOverdrive, Full Albhed Dictionary, Battle Count, PLAYTIME).

**`AutoTestManager` is live in retail with no gate at all.** `__createSingleton 0x6BCC40` runs
unconditionally from `FFX_GraphicInitialize`, and `__ctor 0x6BCA90` spawns a thread named
`"Read command"` running `__stdinThread 0x6BD1E0`, an infinite `std::cin.getline` loop. Commands
(`__execCommand 0x6BCE00`): `EnableAutoTest`, `DisableAutoTest`, `JumpMap <mapname>`,
`SetBattle Enabled|Disabled` (writes `g_ffxBattleDisabled 0x112CA2C`), `Crash`. Everything except
Enable/Disable needs the enabled byte at `g_autoTestManager+297`. FFX.exe is GUI-subsystem, so stdin
has to be attached or redirected to reach it.

**`ViewerManager`** `0x6B5870` (defaults character `c001`, map `grid00`) plus
`FFX_Debug_LoadSaveForViewer 0x649040` / `FFX_Debug_ApplyViewerSave 0x8B55E0`: reads
`<dataroot>/FFX_Data/GameData/PS3Data/savesforviewer/<N>` with plain `fopen`, bypassing the VBF,
0x6900 bytes, and memcpys 0x68C0 bytes from file offset 0x40 straight over the live save block. A
ready-made save-state injection path.

## ATEL: the event script VM, and the only non-player mover

The PS2 event system survives whole. Its log strings are prefixed `TK:` (`TK:MapNo = %d`,
`TK:float:STACKP:%d`, `TK:ID:%dPC:%x`), so the module was `tk*.c`. Everything that is not the pad or
the battle system moves characters through this, and nothing else does.

**There is no compiled-in follower, chase or formation code.** A search of the whole string table for
follow / formation / trail / chase found nothing in game code. The party members trailing behind in a
field map are ordinary CHRs with `m_partyIndex` set, ticked by the normal pool walk, and steered by
the map's own script data. `FFX_Atel_BuildPartyVisibleMask 0x8623B0` builds a 3-bit mask of the
active field party into `g_ffxAtelPartyVisibleMask 0x1326B7C`, and its only consumer is
`FFX_Atel_RestoreActorChrState 0x86E5B0`, which calls `FFX_Ch_SetHideBit1` to show or hide them.

### The actor

`FFX_Atel_GetActor 0x86A830`. The pool is **segmented, not a flat array**: character actors are 2904
bytes, then groups of 1368, 1464, 744 and 48 bytes for the lighter actor kinds
(`FFX_Atel_CalcActorPoolSize 0x86A290` has the expression).

**`actor+0` is a pointer to the actor's 52-byte definition record inside the loaded `.ebp` image, not
the type byte.** The type is one dereference further. An earlier version of this section said the type
was the byte at `actor+0`, which was wrong. Proof, from `FFX_Atel_GetActorMotionChannel 0x872650`:

```
mov  eax, [edx]        ; edx = actor, so eax = the def record
mov  al,  [eax]        ; the type byte
sub  al,  5
```

`FFX_Atel_GetActorMotionState 0x86C2B0` does the same double indirection. `actor+4` is the ATEL block
base.

Types: **0 subroutine container**, 1 character, 2 line trigger, 3 box trigger, 4 no motion state, 5
and 6 unidentified. Type 0 was missing from the earlier list and it matters: all 5,560 `callactor`
instructions in the archive target a type-0 actor, all 441 `ret` instructions sit inside one, and
there are exactly 441 type-0 actors, one `ret` each.

| offset | meaning |
|---|---|
| `+0x00` | **pointer** to the 52-byte actor definition record in the `.ebp`. The type byte is `*(u8*)actor[0]` |
| `+0x04` | the ATEL script block base |
| `+0x2E` | actor id |
| `+0x34` | flags. **bit `0x20` = pad-controlled** (see below) |
| `+0x40` | party character index, `-1` if none |
| `+0x9C` | the CHR this actor drives (`FFX_Atel_GetActorChr 0x86AE80`) |
| `+0xAA` | 1 when CHR-backed |
| `+0x284` / `+0x558` | motion state block, `+0x284` for actor types 5 and 6, `+0x558` otherwise (`FFX_Atel_GetActorMotionState 0x86C2B0`). Speed at `+0x0C`, yaw at `+0x20`, pitch at `+0x24`, rotation at `+0x34` |
| `+0x12C + 76*ch` | the **script thread** context, 9 channels: PC, stack and registers. This is what the interpreter runs |
| `+0x5B8 + 76*ch` | the MOVE command, 9 channels (`FFX_Atel_GetMoveCmd 0x86C0A0`). Character actors only. Same stride as the thread array above but a different array, do not confuse them |

### The MOVE command: the engine's own "walk to a point"

This is the primitive the script uses, and the closest thing in the binary to a navigation API.

| offset | meaning |
|---|---|
| `+0` | target actor id, for chase mode |
| `+2` | `kind \| flags`, kind is the low 7 bits |
| `+4` / `+6` | duration frames / elapsed frames |
| `+8` / `+12` | spline t / spline denominator |
| `+20` | spline pointer |
| `+24` | jump height |
| `+28/32/36` | start XYZ |
| `+40/44/48` | **target XYZ** |
| `+52` | acceleration |
| `+56` | arrival distance |
| `+60/64` | yaw / pitch turn rate |

Kinds, from the name table at `0xC535FC`: `0 END, 1 LIN, 2 LN3, 3 ACC, 4 AC3, 5 SPL, 6 SP2, 7 L3T,
8 A3T, 9 JP1, 10 JP2, 11-14 ER1-ER4`. Flags: `0x200` snap-turn, `0x400` 3D arrival test instead of XZ,
`0x800` teleport on END, `0x1000` arrived, `0x2000` track the target actor, `0x8000` turn toward it.

C-level it is two calls plus the per-frame tick:

```c
FFX_Atel_SetMoveCmdTarget(actor, x, y, z);                       // 0x86F780
FFX_Atel_StartMoveCmd(actor, chanMask, kind|flags, targetActor);  // 0x8703E0
```

`FFX_Atel_RetargetMoveCmdAtActor 0x86F940` is the built-in chase: it re-aims the target at another
actor's live position, and `FFX_Atel_StepMoveCmd 0x868990` calls it every frame while flags
`0x8000|0x2000` are both set.

Script side, **`0xC50050` is not "the opcode table"**. An earlier version of this section called it
that, and said it had four handler slots per entry of "start / poll / value-returning / void". Both
halves were wrong.

What it actually is: `FFX_Atel_Init 0x86D6D0` registers **16 system function libraries** into
`g_ffxAtelSysFuncLibs 0x1328558`, 11 of them real and 5 filled with a shared stub at `0xC52B60`.
`0xC50050` is **library 0**, 616 entries. A `syscall` operand is a 24-bit id split as
`[23:12] library, [11:0] function`, so a script-visible command is a `(library, function)` pair, not a
flat index. Where this section used to say "slot 21" and "slot 24" it should read "lib 0 function 21
and 24".

| lib | address | name | entries | module |
|---|---|---|---|---|
| 0 | `0xC50050` | `g_ffxAtelSysFuncLib00_Core` | 616 | ATEL core event commands |
| 1 | `0xC52BE0` | `..._Sys` | 30 | system and flow |
| 4 | `0xC88D88` | `..._Sg` | 71 | `sg*` (sgMenu) |
| 5 | `0xC891F8` | `..._Ch` | 145 | **the `ch*` character library** |
| 6 | `0xC43998` | `..._Came` | 138 | camera |
| 7 | `0xC42628` | `..._Btl` | **296** | battle |
| 8 | `0xC5DC90` | `..._MapFx` | 108 | map particles (`mpfp*`) |
| 9 | `0xC5D8C0` | `..._Test` | 1 | `atel_test_print` |
| 11 | `0xC40E30` | `..._Movie` | **146** | movie, save menu, HDD |
| 12 | `0xC52DD8` | `..._Save` | 94 | save RAM, field particles |
| 13 | `0xC85EB0` | `..._AbMap` | 1 | `abiritymap_debug` |

**All eleven table sizes are now confirmed by scanning to each table's actual end**, which replaces the
earlier "or more" entries for libs 7 and 11. The test is cheap and exact: walk rows forward and stop at
the first row with a non-zero slot that does not point at the start of a `.text` function. Nine of the
eleven ends are also corroborated by what immediately follows them, which is string data, an integer
table, or the next library's named table.

Two sizes were wrong before. **Lib 7 (Btl) is 296, not 235** - rows 235 to 295 are all valid handler
rows and row 296 at `0xC438A8` starts an unrelated id table. **Lib 11 (Movie) is 146, not 145.** That
is 62 handler entries that earlier counts missed.

For libraries 0, 1, 4, 5, 8 and 12 the size also agrees with a `.data` pointer scan and with the highest
index any shipped script actually calls, to within one entry.

An entry is 16 bytes: `+0` start, `+4` poll, `+8` **float** result getter, `+12` **int** result
getter. The int getter is consulted only when the float one is null
(`FFX_Atel_SysFuncResult 0x8777F0`). The real *opcode* set is a separate thing: a 123-case switch in
`FFX_Atel_RunScript 0x8641E0`, documented in `EBP_FORMAT.md`.

There is **no command-name table left in the binary**. A sweep of `.rdata` and `.data` for any run of
60 or more consecutive string pointers found only the scene path list, the menu font paths and the
help texture paths, and only 44 of the populated library entries reference a string at all. So command
names have to be recovered one at a time from format strings and call targets.

**All 2,018 handler functions are now named positionally**, as `FFX_AtelSys_<Lib>_<func>_<role>`, for
example `FFX_AtelSys_Ch_023_start`. That is deliberately a positional name, and for this table
positional **is** the identity: a `(library, function)` pair is the script ABI, so a script calling
syscall (5, 23) always lands on that handler. Each one carries a repeatable comment naming its library,
function index and role. Rename them to real command names as those are recovered one at a time.

The handler slots are almost perfectly one function per slot: of 2,068 distinct handlers, 2,066 are used
by exactly one slot, one by two, and `nullsub_222 0x773B90` (a single `retn`, now
`FFX_AtelSys_NullHandler`) is shared by 13 Movie-library slots as the do-nothing placeholder.

Note the prefix distinction. `FFX_AtelSys_*` are **syscall library handlers**, reached through a
`(library, function)` pair. `FFX_AtelOp_*` are **opcode handlers**, the 123-case switch in
`FFX_Atel_RunScript 0x8641E0`. They are different dispatch mechanisms and the names keep them apart.
About 50 table entries already carried descriptive `FFX_AtelOp_*` names from an earlier pass and were
left alone rather than renumbered.

### The per-actor tick, and the flag that decides who owns the CHR

`FFX_Atel_StepActor 0x8666E0`, installed as the `ctx+92` callback by
`FFX_Atel_InstallContextCallbacks 0x871190` and called from the tail of `FFX_Atel_RunScript`:

```c
FFX_Atel_StepMoveCmd(actor);                  // 0x868990  updates motionState speed and yaw
FFX_Atel_StepRotCmd(actor);
if ( actor->flags34 & 0x20 ) {                // pad-controlled: pull state OFF the CHR
    FFX_Atel_ReadMoveDirFromChr(actor);       // 0x869DF0
    FFX_Atel_ReadRotFromChr(actor);           // 0x869F90
} else {                                      // script-driven: push state ONTO the CHR
    FFX_Atel_ApplyMoveToChr(actor);           // 0x870540 -> FFX_Ch_SetMoveSpeed
    FFX_Atel_ApplyDirToChr(actor);            // 0x870360 -> FFX_Ch_SetMoveDir
    FFX_Atel_ApplyRotToChr(actor);            // 0x870DC0
    FFX_Atel_IntegrateActorPos(actor);        // 0x8629C0, non-CHR actor kinds only
}
FFX_Atel_TestPlayerProximity(...);            // 0x866980, the talk/examine pick
```

**`actor+0x34` bit `0x20` is the single most important flag for co-op.** Clear, the script owns the
CHR's heading and speed and will overwrite anything we write, every frame. Set, the actor follows the
CHR instead. `FFX_Atel_SetControlledActor 0x86F580` and `FFX_Atel_BindPlayerChr 0x871AB0` are what
maintain it in normal play, but neither is reusable as-is for a second character: Bind binds exactly
one CHR via `FFX_Ch_SetPlayerChr`, and `FFX_Atel_UnbindPlayerChr 0x871A73` clears the bit on *every*
actor. Set the bit directly instead.

### Triggers are tested against the player actor only

`FFX_Atel_StepLineTrigger 0x8684B0` (type 2, segment-vs-segment against the player's movement this
frame) and `FFX_Atel_StepBoxTrigger 0x866CC0` (type 3, outcode box test) both fire script events
3/4/5/6 through `sub_8764F0`. The ATEL context caches only the current player position
(`ctx+536..544`) and the previous frame's (`ctx+552..560`) - there is no per-actor breadcrumb trail.
So a second character that is not the bound player actor will walk through doorway and cutscene
triggers without firing them. That is a feature for phase 1 and a problem for phase 3.

A surviving developer string confirms the whole design:
`"VIRTUOS WARNING: got invalid rotation from atelscript in Ch_SetRot()!!"` at `0xB59730`, referenced
only from `FFX_Ch_SetRot 0x82B540`. The script is the expected driver of CHR transforms.

### The `ch*` script library

`0xA78000-0xA7B200` holds the PS2 `chXxx()` script bindings (`chEnGravity`, `chGetMoveSpeed`,
`chSetRunThreshold`, ...). **There is no `chMove` or `chWalkTo`** - movement is not exposed there, it
goes through the MOVE command.

The on-disk side of all this, the `.ebp` container and the full bytecode, is in `EBP_FORMAT.md`.

## Camera

Three layers. All of it is `FFX_Came_*`, a prefix chosen from the `CameLen` / `CamePara` debug labels
and the `camSet*` / `refSet*` script command names - there is no `came*.c` filename in the binary, only
`..\program\lib\src\camera_debug.c` at `0xB68FAC` and the path `/ffx/proj/camera/` at `0xC61238`.

| layer | address | what it does |
|---|---|---|
| `FFX_PlayerCam_Step` | `0x83F2E0` | the field free-follow orbit camera |
| `FFX_Came_StepAll` | `0x7BE110` | runs the mode handler for every enabled screen's camera slot |
| `FFX_Came_ActivateScreen` | `0x7BC110` | builds the view and projection matrices and pushes them to the gfx context. Called three times per frame from `FFX_MainStep` |

### What the field camera follows

`FFX_PlayerCam_Step` is short enough to state in full:

```c
g_ffxPlayerCamDist -= (g_ffxPlayerCamDist - g_ffxPlayerCamDistTarget) * 0.125;   // 1/8 ease
FFX_Player__getPos(&g_ffxPlayerCamRefPos);     // 0x82D880, reads g_ffxControlledChr
g_ffxPlayerCamRefPos.y -= 10.0;
g_ffxPlayerCamRefPos += g_ffxPlayerCamRefOfs;
g_ffxPlayerCamEyePos = ref + (-dist) * (sin(yaw)*cos(pitch), sin(pitch), cos(yaw)*cos(pitch));
FFX_PlayerCam_Publish();                       // 0x83F130, the call is at 0x83F498
```

So the target is **a CHR pointer, indirected through `g_ffxControlledChr 0x1300788`**. Not a party
index, not a rail. `FFX_PlayerCam_Publish` opens the **lowest-priority** handle
(`FFX_Came_OpenHandle(owner 6, screen 0, class 0)`) and writes both positions in mode `pos`, so any
event camera (class 3) or battle camera (class 5) outranks it completely.

### State

- `g_ffxCameSlots 0x1137540` - **3 camera slots**, stride 3488. Each holds two 1564-byte sub-blocks:
  REF (the look-at point) at `+0` and CAM (the eye) at `+1564`. Within a sub-block, `+0x40` is the
  live position, `+0x50` the committed one that `FFX_Came_GetPos` returns, `+0x90..0x9C` theta/phi/len,
  `+0xBC` a `vec4 *` target pointer, and `+0xC0` four priority entries of 304 bytes. By default the
  two sub-blocks cross-link, each pointing at the other's live position.
- `g_ffxCameScreens 0x1137000` - **3 screen structs**, stride 448. `+0xC0` enable, `+0xC1` camera slot
  index, `+0xC4` screen kind, `+0x40..0x4C` the WIN rect, `+0x50..0x5C` the DRW rect.
- `g_ffxCameViewMatrix 0x2311440`, `g_ffxCameProjMatrix 0x2311480`. These are the two matrices
  `MagicFile__load` snapshots for a plugin DLL.
- `g_ffxCameActiveSlot 0x2311430` - a **pointer** to whichever of the three slots is current. Set to
  `&g_ffxCameSlots[0]` by `FFX_Came_Init 0x7BD5D0` and thereafter only by
  `FFX_Came_ActivateScreen` at `0x7BC1C9`, which stores `g_ffxCameSlots + 3488*screenSlotIndex`. It can
  never be null or garbage.

### Reading the camera yaw at runtime

This is the thing a mod actually needs, because it is what makes player-2 input camera-relative instead
of world-relative. The answer is to copy what the game does:

```c
BYTE *slot = *(BYTE **)g_ffxCameActiveSlot;            // 0x2311430
const float *ref = (const float *)(slot + 0x050);      // look-at target
const float *cam = (const float *)(slot + 0x66C);      // eye
float camYaw = atan2f(ref[2] - cam[2], ref[0] - cam[0]);
```

`0x66C` is `0x61C + 0x50`, the committed position inside the CAM sub-block. This is exactly
`FFX_Came_GetYaw 0x7BCF20`, which is 12 instructions and **takes no arguments** (it loads ECX from the
global, so the register is not a parameter):

```
mov  ecx, g_ffxCameActiveSlot
push 0 / push 0                    ; phi and len out, both unwanted
lea  eax, [ebp+var_4] / push eax   ; theta out
lea  eax, [ecx+66Ch]  / push eax   ; origin = CAM
lea  eax, [ecx+50h]   / push eax   ; pos    = REF
call FFX_Came_VecToPolar
fld  [ebp+var_4]                   ; theta in st0
```

`FFX_Came_VecToPolar 0x7C4990` computes `delta = pos - origin` and then `atan(delta.z/delta.x)` with a
`-pi` correction when `x <= 0`, which is `atan2(delta.z, delta.x)` modulo 2pi. **The raw range is about
`(-3pi/2, +pi/2]` rather than `[-pi, pi]`**, because the `x < 0` branch always subtracts pi instead of
picking a sign. That does not matter when the value only ever reaches `cos` and `sin`, but wrap it
before differencing two angles.

**It needs no conversion to combine with `m_moveDir`.** `FFX_Ch_UpdateMotionAll` at `0x832F05` does
`velX = cos(m_moveDir)*k` and `velZ = sin(m_moveDir)*k`, so `m_moveDir` is 0 along +X and pi/2 along
+Z, which is the identical zero axis and direction as the yaw above. Radians, not degrees, not fixed
point.

The game's own use of it, which is the formula to copy for a second character:

```c
desired   = atan2(right, forward);          // NOTE the argument order
m_moveDir = camYaw - desired;
```

Confirmed at `0x82D658` for the `_CIatan2` (the two `fld`s put the right-axis ramp in st1 and the
forward-axis ramp in st0) and at `0x82D786` for the subtraction plus the `fchs` that negates it. The
full expression including the script hooks is
`m_moveDir = camYaw - (g_ffxPlayerDesiredHeading - g_ffxPlayerHeadingOffset)`, where
`g_ffxPlayerHeadingOffset 0x1300780` defaults to 0 and
`g_ffxPlayerUseFixedYaw 0x13007C4` / `g_ffxPlayerFixedYaw 0x13007C8` let a script pin the yaw.

**Three traps worth knowing, each of which looks like the easy answer and is not:**

- **Do not read `g_ffxPlayerCamYaw 0x130079C`.** It is a plain float and it is tempting, but it is only
  refreshed inside `stepControl`'s analogue-stick branch: skipped when
  `g_ffxPlayerControlEnabled 0xC496D8` is 0, skipped when the dpad branch `(buttons & 0xF000)` is
  taken, and in control mode 3 only refreshed past a 0.349 rad change or on two specific map ids. It
  goes stale during cutscenes. The slot is always current.
- **Do not read `g_ffxPlayerCamYawCtl 0xC4DEF4`.** That is the debug free-orbit camera's own control
  yaw, integrated from the right stick by `FFX_PlayerCam_ReadPadInput 0x83F4B0`, and it uses the
  **other** convention (0 along +Z), so `FFX_Came_GetYaw == pi/2 - g_ffxPlayerCamYawCtl` and only while
  the player camera wins priority. Since `FFX_PlayerCam_Publish` opens the lowest-priority handle, on
  most field maps it loses and this global is not the camera on screen.
- **The view matrix includes camera shake and the slot does not.** `FFX_Came_ActivateScreen` builds the
  matrix from `slot+0x40` and `slot+0x65C`, and `FFX_Came_StepAll` sets `slot+0x40 = slot+0x50 +
  shakeOffset`. So the matrix forward vector (`g_ffxCameViewMatrix` elements `m[2] m[6] m[10]`, column
  2, untouched by the roll mix) is a genuinely independent second source that agrees with the slot
  derivation, but for driving input you want the un-shaken slot.

Prefer doing the `atan2` yourself over calling `FFX_Came_GetYaw`, for one concrete reason:
`FFX_Came_VecToPolar` does all its arithmetic through fixed global scratch at `0xC8F7D0..0xC8F85C`
shared with the look-at matrix builder, so it is safe on the game thread during the step phase and not
safe from any other thread. Reading the two vectors touches nothing.

`FFX_Came_GetYaw` tracks **every** camera class, not just the field camera. `FFX_Came_StepAll` runs
whichever priority entry wins, including event cameras (class 3) and the battle modes 11/12/13/15/16,
and they all write the same two sub-blocks. That is precisely why the game's own player control reads
it rather than the player camera's own yaw. The two paths where the slot is not what you see are
`g_ffxCameOverrideFlag 0x23114C4` (which force-writes the slot, so it stays correct) and
`g_ffxCameDirectLookAtFlag 0x231142C` (which bypasses the slot entirely in favour of
`g_ffxCameDirectEyePos 0x1136FC8` / `g_ffxCameDirectAtPos 0x1136FD8`, the pre-rendered background and
FMV path, where there is no gameplay control anyway).

One piece of geometry that is reasoning rather than direct code reading: `FFX_Came_Init` writes `-1.0`
to `slot+0x04` and `FFX_Came_ActivateScreen` passes `slot+0x00` as the up vector, so up is `(0,-1,0)`
and **world +Y points down**. Viewed from above that makes increasing `m_moveDir` rotate
counter-clockwise, so `camYaw - pi/2` is screen-right. The formula above is lifted verbatim from the
game so it is correct regardless of how the direction is labelled.

### Modes

`g_ffxCameModeNames 0xC44640`, 18 entries, indexed by the mode byte at `prioEntry+2` and dispatched by
the switch in `FFX_Came_StepAll`:

`0 not, 1 pos, 2 pol, 3 act, 4 hyp1, 5 hyp2, 6 hyp3, 7 polo, 8 pflw, 9 phyp, 10 act2, 11 bpos,
12 bpl1, 13 bpl2, 14 spln, 15 cpl1, 16 cpl2, 17 spl2`

Two of these matter for co-op. **`act` (3)** takes up to two arbitrary ATEL actor ids and uses their
**midpoint plus an offset** (`FFX_Came_ModeAct_Step 0x7C0560`, set by
`FFX_Came_SetTargetActors 0x7BEEB0`). **`pflw` (8)** is the classic FFX field rail, a 1/d-weighted
blend over the map's camera-point set, and it is what most field maps actually install. Modes 11, 12,
13, 15 and 16 are battle. The rest of the table is field. Only 5 of 18 entries are battle-specific,
which is why this table is `g_ffxCameModeNames` and not a battle-only one - the battle name table is
the separate `cam_chr_*` list, `g_ffxCameChrTargetNames 0xC44870`.

Priority is a separate axis from mode. `FFX_Came_OpenHandle 0x7BD110` allocates one of 4 priority
slots per screen and returns a packed handle `screen | prio<<8 | class<<16 | owner<<24`. Classes 1, 3
(Event), 5 (Battle) and 7 print as M/E/B/V in the debug window.

### Multi-view already exists, in the game and not in Phyre

`Phyre::PApplication` has a generic viewport list (count at `this+700`, array at `this+704`) that
`Phyre__PApplication__frameTick 0x627940` loops over. **FFX never populates it** - the count stays 0
and everything goes through `FFXApplication__render 0x42F930`. The D3D11 viewport and scissor wrappers
(`Phyre__PRenderingD3D11__setViewport 0x58C790`, `__setScissorRect 0x59AAF0`) exist but are only
reached from `PApplication::resize` and the render-target command processor.

The game brings its own instead: 3 screens, each independently enabled, rect'd and bound to one of the
3 camera slots, and `FFX_MainStep` already contains a **two-pass per-screen render loop** that draws
the scene once per enabled screen, sub-screens first and the main screen last. It is script-exposed as
`FFX_AtelCmd_CameScreenOpen 0x7B8AD0` / `Close 0x7B8B80` / `SetSlot 0x7B8B50`, and
`FFX_Came_SetScreenEnableAndSlot 0x7BC950` is **entry 692 of the magic plugin API**, so an external
DLL opening a second screen is within the shipped design.

Two shipped maps use it: `FFX_Came_ActivateScreen` special-cases screen 1 on map ids 152 and 601, and
routes a 256-wide sub-screen's view matrix into a second gfx-context camera slot
(`FFX_Gfx_SetCameraWorldMatrix2 0x638AA0`).

**The catch:** the per-screen rect is folded into the projection matrix by
`FFX_Came_BuildProjFromScreen 0x7C3D60`, PS2 style, and **there is no scissor**. Scene geometry is
framed correctly by NDC clipping, but any full-screen pass - sky, post-process, fades, UI, the debug
overlay - ignores the rect and bleeds across the divider.

### Overrides, cheapest first

1. **`g_ffxPlayerCamRefPos 0xC4DED0`** plus `g_ffxPlayerCamDistTarget 0x1301BB8`. Trampoline the
   5-byte `call FFX_PlayerCam_Publish` at `0x83F498`, rewrite the globals, tail-call the original.
   All state is in globals, so no register pressure.
2. **`FFX_Player__getPos 0x82D880`** detour. Smaller, but it has three other callers.
3. **`g_ffxControlledChr 0x1300788`** - one dword, moves camera and player control together.
4. **`FFX_Came_SetTargetActors 0x7BEEB0`** on a class 3 or higher handle, to make the engine itself
   midpoint two actors. Shipped code, needs ATEL actor ids.
5. **`FFX_Came_SetMatrixOverride 0x7C06D0`** - `(enable, proj, view, refPos, camPos, scrDpt)`. While
   `g_ffxCameOverrideFlag 0x23114C4` is set, `FFX_Came_StepAll` forces both sub-blocks to the override
   positions, bypassing every mode and every priority. Shipped and exercised by the pre-rendered
   background and FMV paths. A lighter variant replaces only the view matrix:
   `maybe_FFX_Came_SetDirectLookAt 0x7BBB10` with `g_ffxCameDirectLookAtFlag 0x231142C`.

Options 1 to 3 all share one limitation: in most field maps the event script installs a class 3
camera that outranks the player camera entirely, so fixing only the player camera has no visible
effect there. Options 4 and 5 work everywhere.

### Battle camera

Same slots, same priority arbitration, same `FFX_Came_ActivateScreen`. It differs only in using handle
class 5, its own modes (11/12/13/15/16, all handled by `FFX_Came_ModeBattle_Step 0x7C58E0`), and
target resolution through the `cam_chr_*` enum at `g_ffxCameChrTargetNames 0xC44870`:
`nop, active, target, target_now, all, party1..party7, own, all_ply, all_mon, own_target, reaction,
input`. Each entry carries **two** target ids and the handler **averages** them, so battle co-op gets
a two-target midpoint and a `party<N>` selector for free.

## The game already has a plugin DLL system, and it ships 581 of them for FFX

This is the most useful thing found so far for a mod, so it gets its own section.

`magicFiles\FFX\magic_%04d.dll`, loaded at runtime by `LoadLibrary` on a path relative to the game
directory. **581 ship for FFX**, 349 MB in total. They hold the spell and summon effects. PS3
heritage shows in the naming: PRX was Sony's name for a dynamic module and the code kept it.

An earlier version of this section said 1,425. That number is both games added together: FFX 581 plus
FFX-2 844, and the FFX-2 844 is 826 `magic_NNNN.dll` plus 18 localised variants
(`magic_0718_{ch,cn,de,es,fr,it,kr,uk,us}.dll` and the same for `magic_0797`) that the `magic_%04d.dll`
format string cannot even produce. FFX has no localised variants.

### The module interface

Each DLL is a 32-bit PE32 exporting **exactly two symbols**:

```
InitMagicPRX
GetEffectOverlayTable
```

`magic_0003.dll` for example is 312 KB: 28 KB of `.text` and a 2.9 MB virtual `.data`, so they are
mostly effect data wrapped around a small code stub. Their import tables pull only `MSVCR110` CRT
startup and nine `KERNEL32` functions. **They import nothing from `FFX.exe`.** Everything they need
comes in through the one argument to `InitMagicPRX`.

### The host API table

That argument is `g_ffxMagicHostApiTable` at `0xC64CE8`, handed over by `MagicFile__start 0x9DA7F0`
as `g_ffxMagicInitMagicPRX(g_ffxMagicHostApiTable)`. This is the game's own curated public API, and it
is worth reading as documentation of what the developers considered a safe operation from outside code.

**The table is 947 entries, `0xC64CE8` to `0xC65BB4`, not 741.** An earlier version of this section
said 741, which is only where the first unbroken run of code pointers stops: indices 741-765 are 25
pointers to host *data*, then code resumes. Proof that it runs further: index 812 is
`FFX_Magic_BuildTexturePath 0x906420`, index 946 is still a `.text` pointer, the dword at `0xC65BB4`
is zero and the next named thing is an unrelated `type_info`, and every shipped DLL's `InitMagicPRX`
reads host offsets up to `0xEA0` (index 936) and calls up to index 942. Note the table has interior
NULL slots, so a scan that stops at the first zero stops early.

Entries 0-38 are the character API and **entry 0 is `FFX_Ch_Allocate`**, so spawning a CHR from an
externally loaded DLL is a *sanctioned* operation. **It is also an exercised one.** This section used
to say there is not a single call to any of indices 0-32 and that only 36, 37 and 38 are used. That
was the old `tools/magicdll.py` matcher reporting zeroes it could not help reporting: it only
recognised the mod=10 `disp32` addressing form, and every slot below 32 has a displacement under
0x80 and so compiles to the mod=01 `disp8` form, with slot 0 compiling to mod=00 and no
displacement at all. With the scanner fixed, **38 of the 39 entries 0 through 38 are called, at 2951
sites below index 32 alone.** The only one untouched is slot 2, `FFX_Ch_MarkDirty`, and the
character block really does end at 38, because nothing from 39 to 45 is called either. **Entry 0
`FFX_Ch_Allocate` is called by 28 DLLs at 47 sites.** `magic_0162` at `0x10002DB0` is
`mov eax, dword_1010A28C; push esi; mov eax, [eax]; push 3011h; call eax`, and `0x3011` decodes
through `FFX_Ch_Allocate`'s own documented argument encoding as category 3 "sum", model 17, so a
summon effect DLL allocating its own CHR. So `PHASE1_CLONE.md`'s confidence signal is stronger than
"the developers made this callable". The shipped code does do this.

Usage data, measured across all 581 DLLs: **74,612 host-table call sites over 608 distinct indices**,
max index 946, and **339 of the 947 entries are never called by any FFX magic DLL**. The hottest
single entry is 941 (`maybe_FFX_PrxPtr_Get 0xA44650`, an identity function) with 2,853 sites in 539
DLLs, followed by 719 (`FFX_Oef2_GetSubTable`) and 233. These numbers replace an earlier
49,442 / 459 / max 942 / 488-never-called, which came from the broken matcher and was a lower bound
everywhere. `reversing/MAGIC_DLL.md` section 7 has the old and new figures side by side.

The rest, by region: the effect and particle module (`0x72xxxx-0x75xxxx`) takes 238 slots, battle
(`0x78xxxx-0x7Bxxxx`) 118, another render or effect block at `0x92xxxx` 91, the CHR module
(`0x82xxxx-0x83xxxx`) 108, and the remainder is spread thin. **588 of the first 741 and 772 of the
real 947 are still unnamed**, and they are the best-justified naming backlog in the binary: by
construction they are all API surface.

### The functions

| address | name | note |
|---|---|---|
| `0x9DA3E0` | `MagicFile__init` | |
| `0x9DA7A0` | `MagicFile__setMagicId` | must be called before load |
| `0x9DA430` | `MagicFile__load` | builds the path, calls `FFX_LoadDllOrFatal` |
| `0x9DB110` | `MagicFile__onDllLoaded` | the load callback, resolves the two exports |
| `0x9DA7F0` | `MagicFile__start` | calls `InitMagicPRX(g_ffxMagicHostApiTable)` |
| `0x9DA880` | `MagicFile__stop` | |
| `0x9DA960` | `MagicFile__unload` | |
| `0x9DA3B0` | `MagicFile__getMagicId` | 85 callers |
| `0x9DA370` | `MagicFile__getEffectOverlayTable` | guarded forwarder into the DLL |
| `0x9DAE90` | `MagicFilePreload__preload` | warms a DLL ahead of need |
| `0x9DACC0` | `MagicFilePreload__clearPreloadedPrx` | |
| `0x9DAFE0` | `MagicFile__selectEffectBudget` | per-id effect budget class |
| `0x6793E0` | `FFX_LoadDllOrFatal` | `LoadLibrary` plus a localized fatal dialog from string resource 30771 |

Only one magic DLL can be loaded at a time. `MagicFile__load` refuses a second one, and there is a
reuse path for the case where the requested id matches the one queued for deletion.

### What this does and does not give us

It gives us three things:

1. **Proof that relative-path `LoadLibrary` from the game directory works**, with the loader
   resolving named exports afterwards.
2. **A model for publishing state to outside code, and it is the opposite of what I first wrote.**
   The convention the game actually picked is **an accessor in the table**. A DLL gets the camera by
   *calling* host entries 724 (`FFX_Came_GetViewMatrix`) and 723 (`FFX_Came_GetProjMatrix`), and 534
   and 415 of the 581 DLLs respectively do exactly that.
   An earlier version of this section claimed `MagicFile__load` snapshots the camera into
   `g_ffxMagicSharedViewMatrix 0x22FB660` / `...Proj... 0x22FB620` for the DLL to read. **That was
   wrong and is struck.** Those two globals have exactly two touch points, both host side:
   `MagicFile__load` writes them and `MagicFile__restoreCameraSnapshot 0x9DA750` copies them *back
   into* `g_ffxCameViewMatrix` / `g_ffxCameProjMatrix`. It is a save and restore pair around the
   spell, not a publication channel. Neither address is in the host API table and nothing in the
   table points near them. Renamed to `g_ffxMagicSavedCameViewMatrix` / `...SavedCameProjMatrix`.
3. **A 947-entry whitelist of game functions** that are known to be callable from outside the normal
   call graph.
4. **One booby trap worth knowing about.** `MagicFile__load` walks
   `g_ffxMagicBlacklistedIds 0xC64CB4` = `{0, 693, 694, 695, 696, 44, 712, 713, 725}` terminated by
   `-1`, and on a match it prints
   `"[VIRTUOS WARNING] An unknown magic file is found: Magic%04d!!!"` and then executes
   `mov large byte ptr ds:0, 0` at `0x9DA560`. That is a literal store to address zero, an
   intentional crash. Five of those ids ship with a DLL anyway (`magic_0693` at 1.37 MB, `0694`,
   `0695`, `0696`, `0712`), so the files for those spells exist but casting them is hardwired to take
   the process down.

It is not a general mod entry point and we should not use it as one. Load timing is tied to casting
one specific spell, only one module can be resident, and taking over a `magic_NNNN.dll` would break
that spell. The `dinput8.dll` proxy in `../loader/` stays the right mechanism. This is a reference and
a fallback, not a replacement.

## Asset loading - the function to hook

**`FFX_fios_openFile 0x607F40`.** It normalises `\` -> `/`, lowercases
(`FFX_fios_toLowerInPlace 0x607D50`), looks the path up in the VBF via
`FFX_VbfManager__openFileStream 0x61BF10`, and **only if it is not in the archive** falls back to
`CreateFileW` on a real file. That fallback is what makes loose-file override possible. Every Phyre
asset read reaches it through `PStreamReaderFile__vf03 0x576990` (and
`FFX_fios_FileHandle__ctor 0x607BC0`); writes go through `PStreamWriterFile__vf03 0x9F08C0`.

- `FFX_fiosUnifyFilename 0x679820` (77 callers) prepends `g_ffxDataRoot 0xCC9698` and rewrites
  `.cgfx.phyre` -> `.fx.phyre` and the `GCM` path component -> `D3D11`. Sized wrapper `0x679A10`.
- `FFX_InitFileSystem 0x6795B0`: data root `"../../.."`, VBF root `"../../../"`, mounts
  `data\FFX_Data.vbf`; on failure MessageBoxW + exit. Also picks the PS3 title id (PCSG00219 JP /
  PCSE00293 US / PCSB00395 EU / PCSH00042 Asia).
- `FFX_VbfArchive__open 0x61DAC0`: magic dword `0x4B595253`, which is **`SRYK`** in file byte order
  (else `"BigFile head wrong!"`). MD5s (CALG_MD5) the header block and compares it against the
  trailing 16 bytes of the file. The comparison goes through `FFX_Md5__isLess 0x61D6E0` called twice
  with the arguments swapped, so "neither is less" means equal.
  **The whole format is now decoded and implemented.** See `VBF_FORMAT.md` for the byte layout and
  `../tools/vbf.py` for a working reader, verified against the shipped 19.3 GB `FFX_Data.vbf`:
  71,979 entries, header MD5 checks out, 412 sampled files decompress to exactly their stated size.
  Supporting functions named from this: `FFX_Md5Map__insert 0x61DE30`, `FFX_Md5Map__clear 0x61D8E0`,
  `FFX_VbfStream__openHandle 0x61ACC0` (an archive carries **15** read streams), `FFX_Malloc 0x6304C0`.
- **VBF lookup keys are the MD5 of the lowercased path with the root prefix stripped** (prefix length
  at manager+60). `FFX_VbfManager__openFileStream 0x61BF10`, `__fileExists 0x61BE40`,
  `FFX_VbfArchive__openStreamByMd5 0x61DF10`. Both of those last two were previously named
  `findFile` / `findEntryByMd5`, which was wrong in a way that matters: **they return a bound read
  stream, not a pointer to the entry row**, and they search a red-black tree
  (`FFX_Md5Map__find 0x61D140`) rather than binary searching the on-disk sorted key array.
  There are **5 mount slots** at manager+16 and the loop takes the **first hit scanning 0..4**, so a
  second archive mounted in a lower slot shadows `FFX_Data.vbf`. That is probably the cleanest asset
  override route we have.
- Reads: `FFX_VbfStream__read 0x61AA90` -> `FFX_VbfManager__readStream 0x61BFE0`.
- PS2-era path builders (tklib.c) `0x88C860`, `0x88CAC0`, `0x88CCF0`, `0x88CE80` build
  `/ffx_ps2/ffx/master/new_{jp,us,de,sp,fr,it,kr,ch,cn}pc/...` from templates `%smenu/%s.dcp`,
  `%sbattle/btl/%s/%s.bin`, `%sbattle/kernel/%s.bin`, `%sevent/obj_ps3/%s%s.bin`. `0x8AD780` /
  `0x8AD840` hold the `host0:/ffx/master/...` -> `/FFX_Data/GameData/PS3Data/...` remap tables.
- `AsyncLoadManager`: `__get 0x691B40` (`g_asyncLoadManager 0xCCC88C`), `__ctor 0x690830`,
  `__startWorkerThread 0x691180` (thread "AsyncLoadManager"), `__workerThread 0x691360`
  (double-buffered queues at +32/+44, index +56, mutex +16), `__enqueue 0x6918B0`,
  `__enqueueWithContext 0x691660`, `__allocRequest 0x6910C0`.

## Game state globals

| address | contents |
|---|---|
| **`0x112CA90`** `g_ffxSaveData` | **the live save / game-state block, 0x68C0 = 26,816 bytes** (`0x112CA90`..`0x1133350`). Returned by `FFX_GetSaveData 0x785240` (thunk `0x88C830`), 142 call sites. memset by `FFX_InitNewSaveData 0x786B00`. |
| `0x112CABC` (+0x2C) | `LoveParam` DWORD[8], per-character affection - `FFX_SaveData__getLoveParam 0x7870D0` |
| `0x112CB48` (+0xB8) | current map/section id (HIWORD:LOWORD fed to `sub_86FEC0` to warp) |
| `0x112CB4C` (+0xBC) | playtime, clamped to 3,599,999 - `FFX_SaveData__setPlaytime 0x787610` |
| `0x112D67C` (+0xBEC) | scenario/progress word (forced to 5 in debug mode) |
| `0x113079C` (+0x3D0C) | `party_data` block, 0x1C bytes |
| `0x11307B8` (+0x3D28) | 0x20-byte block (kernel table 31) |
| `0x11307D8` (+0x3D48) | `conf` block, 0x84 bytes |
| `0x113081C` / `0x113082C` | **CORRECTED, these are NOT the party.** They are the new-game starting inventory, inside the `conf` block: WORD[8] item ids and BYTE[8] counts. Suggested names `g_ffxConfStartItemIds` / `g_ffxConfStartItemCounts`. The real party is three bytes at `+0x3D58`, see the rows below. |
| **`0x11307D8`** (+0x3D48) | **gil**, a DWORD at the head of the `conf` block. `FFX_SaveData_SpendGil` takes a negative amount to add. |
| **`0x11307E8`** (+0x3D58) | **the three active party members**, BYTE[3]. Each byte is a character record index, `0xFF` for an empty slot. This is the authoritative battle lineup and the thing per-character action ownership is keyed on. `g_ffxBattlePartyOrder` at `0x112C895` is a BYTE[7] whose first three entries mirror it. `FFX_Btl_SetupUnitRoster 0x79C110` writes the active slot number to battle actor+1278, which is the engine's own ownership table, and `FFX_Btl_CommitPartyToField 0x786930` is the writeback. There is no roster array: party MEMBERSHIP is bit 0 of character record+0x2C. |
| **`0x113095C`** (+0x3ECC) | **the inventory**, `WORD[256]` item ids. 112 entries are used. |
| **`0x1130B5C`** (+0x40CC) | **the inventory counts**, `BYTE[256]`, parallel to the ids. `FFX_SaveData_AddItem 0x790550` is the SINGLE commit point for both arrays, so it is the one place to hook for "the inventory changed". |
| `0x1130C9C`..`0x1130F1C` (+0x420C..+0x448C) | Monster Arena capture counts, followed by two bestiary masks. Not the inventory, which is the pair of rows above. |
| `0x1130F2C` (+0x449C) | `g_ffxEquipmentArray` - 200 entries x 22 bytes, entry+6 = owning character index. `FFX_SaveData__getEquipEntry 0x7ABBD0`, `__setCharEquip 0x7AB970` |
| **`0x113205C`** (+0x55CC) | `g_ffxCharRecords` - **18 character records of 0x94 (148) bytes**. `FFX_SaveData__getCharRecord 0x785330`. +4 base max HP, +8 base max MP, +12..+19 base stats (Str, Def, Mag, MagDef, Agi, Luck, Eva, Acc), +0x1C current HP, +0x20 current MP, +45 weapon slot, +46 armour slot, +47..+54 effective stats, +59 the available sphere level count (NOT a status byte, an earlier note had this wrong), and party membership is bit 0 of +0x2C. Readers `FFX_SaveData__getCharBaseStats 0x785B60`, `__getCharCurrentStats 0x787230` |
| `0x1132DDC` (+0x634C) | `g_ffxCharNames` - 18 x 20-byte names. `FFX_SaveData__reloadCharNames 0x787100` |
| `0x1135E00` | `g_ffxEquipStatBonus` - 8 x 28-byte equipped-gear bonus cache. `FFX_SaveData__getEquipStatBonus 0x7987F0` |
| `0x11334D4` | `g_ffxBattleAllyActors` base pointer; 31 slots x 3,984 (0xF90) bytes. Non-zero only in battle (`FFX_Battle_IsActive 0x795970`). In-battle stats at actor+1448..1455 / +1488 / +1492 |
| `0x113446C` | `g_ffxBattleMonsterActors` base pointer; slots 31..92 x 912 bytes. Accessor `FFX_Battle_GetActor 0x794020` |
| `0x23C44E4` / `0x23C44E0` | `g_ffxChrArray` / `g_ffxChrCount` - the live CHR instances |
| **`0xC423A0`** | **`g_ffxCharIndexToChrId`**, `DWORD[18]`. This is what finally pinned down which model id is which character, so no more guessing from spawn experiments. Character index to chr id: Tidus 1, Yuna 2, Auron 3, Kimahri 4, Wakka 5, Lulu 6, Rikku 7, Seymour 8, then ten aeons at `0x3001`..`0x300B`. So for the eight party members chr id = character index + 1. Rikku reads **41** instead of 7 when `g_ffxSaveData+0xD1` is set, which is her alternate outfit. Ids 101..108 are the same eight at high detail. Ids 45, 307, 901 and 908 have field motion sets but no entry in this table, so they are still unidentified. |
| `0xCCB170` | `g_ffxInput` |
| `0x133C910` | `g_ffxDebugMode` |
| `0x22FB504` | `g_ffxGameSettings` (GameSetting.ini object) |
| `0xCC9CD8` | `g_ffxApplication` |
| `0xCC9698` | `g_ffxDataRoot` |
| `0xCC9C48` | `g_ffxVbfManager` |
| `0xCCCAB0` / `0xCCCB00` / `0xCCC88C` / `0xCCCA70` / `0xCCC870` / `0xCCC830` | DebugMenuManager / AutoTestManager / AsyncLoadManager / ClassCharacterTable / FFEscMenu / FMV-state singletons |
| `0x12FB808` | `g_ffxPendingSteps` |
| `0x12FC598` | `g_ffxPendingChrLoads` - 51 slots x 272 bytes |
| `0xC3432C` / `0xC34318` | `g_ffxDebugCharNames` ("Tidus(0)".."Seymour(7)") / `g_ffxDebugCharModeNames` ("Field","FieldBattle","Swim","SwimBattle") |

## Things that will bite a co-op build

- **Single-instance mutex** GUID `9B8F7468-6DE0-4C5A-9209-89D363F8FCC9` in `WinMain`. Two local
  clients on one machine have to defeat this.
- `argv[1]` is overloaded: `"debug"`/`"DEBUG"` enables all the debug facilities above, `"_ECalm"`
  runs the Eternal Calm bonus episode. **They are mutually exclusive**, since both read `argv[1]`.
- `FFX-2.exe` and `FFX&X-2_Will.exe` import DINPUT8.dll identically, so a `dinput8.dll` proxy loads
  into all three. The loader filters on `HostExe=FFX.exe`; keep that.
- The pause menu stalls the entire game step (`FFXApplication+941`), so a networked peer will freeze
  relative to its partner whenever one player opens a menu.

## Not determined

- How `byte_12FBBB3` (the second condition gating SG GUI drawing) gets set.
- Per-field meanings inside the 0xF90-byte battle actor struct beyond the stat block at +1448.
- `VsyncThreadImpl` exists only as the RTTI type descriptor `.?AVVsyncThreadImpl@@` at `0xC330F0`,
  with no code xref, so its creator is unidentified.
