# Phase 1: a separately controllable clone of Tidus

**Status: implemented.** `..\loader\plugins\clone\` is the working prototype built
from this document. F9 spawns the clone, the arrow keys drive it, F10 despawns it, and F11 opens a
control window with a live readout. Build it with `..\loader\build_pilgrimage.bat`. If something in here
changes, that file is what has to change with it, and the reverse is also true: anything learned while
making the prototype work belongs back in here.

Everything here is read out of `FFX.exe` (Steam build, stamp 2026-09-10). Addresses are VAs at the
preferred base `0x400000`, which is what IDA shows. **The exe is ASLR'd**, so runtime code must use
`GetModuleHandle(NULL) + (VA - 0x400000)`. Struct layouts are in `ffx_types.h`, applied to the IDB
as the `CHR` / `CHRDATA` / `CHRPART` / `CHRANIMF` local types.

## The headline: movement is already per-character

This was the one thing that decided how big phase 1 is, so it got checked properly, including a
full instruction sweep of 0x600000-0xB0C000 for memory operands at displacement 0x154 and 0x168.

**Everything below the input layer is parameterised by the CHR pointer and reads no player global.**

The two fields are written by exactly three instructions in the binary: one inside each setter, plus
one direct store at `0x828734` inside `FFX_Ch_CopyState 0x828620`, which writes `m_moveDir` only.
`m_speed` is **not** copied by `FFX_Ch_CopyState` - re-checked instruction by instruction, the
displacement `0x154` does not appear anywhere in that function. What it copies is the contiguous
float run `0x158` to `0x170` (`m_rotY`, three unknowns, `m_moveDir`, one unknown, `m_runThreshold`),
stepping over `0x154` exactly. Worth knowing when cloning: a fresh clone inherits the player's
facing and heading but starts at zero speed. Everything else goes through the setters. **The setters are not player-only** - they have three independent groups of callers:

| caller | what drives it |
|---|---|
| `FFX_Player__stepControl 0x82D180` | the pad |
| `FFX_Atel_ApplyMoveToChr 0x870540` and `FFX_Atel_ApplyDirToChr 0x870360` | the event script, every frame, for every script-driven actor |
| `maybe_FFX_Btl_SetupUnitChr 0x793670`, `FFX_BtlAtelOp_SetUnitMoveSpeed 0x7A5840` | battle setup and battle script |

That is the strongest evidence available that this is the intended way to move any character: the
game already does it for non-player actors on every frame of every cutscene.

- `FFX_Player__stepControl 0x82D180` reads one pad (port 0 only), keeps its ramp and heading state
  in module globals, fetches its target from `g_ffxControlledChr 0x1300788`, and finishes with
  exactly two writes to that CHR:

  ```
  FFX_Ch_SetMoveSpeed 0x82B840 (chr, speed)   -> chr->m_speed    (CHR+0x154)
  FFX_Ch_SetMoveDir   0x82B190 (chr, radians) -> chr->m_moveDir  (CHR+0x168)
  ```
  plus `FFX_Ch_SetFlags1Bit400 0x82AAE0(chr, 1)` each substep, which is what enables turn-toward-dir.

- `FFX_Ch_UpdateMotionAll 0x832E10` then does the same work for **every** in-use CHR in the array:
  `k = dt * 30.0 * 0.05 * m_speed`, `m_velX = cos(m_moveDir) * k`, `m_velZ = sin(m_moveDir) * k`,
  previous position saved, then collision, walkmesh slide and the ground clamp.

- The turn and the animation choice live in `FFX_Ch_AutoLocomotionAnim 0x835BB0`, which takes the
  CHR as its only argument. With the struct applied it decompiles to this, which is the whole story:

  ```c
  if ( (m_flags1 & 0x400) != 0 )                        // turn toward the desired heading
      chr->m_rotY = ApproachAngle(chr->m_rotY, chr->m_moveDir, rate);
  if ( chr->m_runThreshold >= chr->m_speed )
      if ( chr->m_speed == 0.0 )  play(chr->m_slots[0]);    // idle
      else                        play(chr->m_slots[1]);    // walk
  else                            play(chr->m_slots[2]);    // run
  ```

The only three places the whole chain consults the player global are cosmetic or trigger-only: the
"is player" marker (`m_flags1` bit `0x200`, maintained by `FFX_Ch_UpdateRenderJob 0x833530`), the
debug noclip exemption in `FFX_Ch_WalkmeshMove 0x83E5F0`, and the player-bump trigger in
`FFX_Ch_ResolveCollisionsAll 0x83D3B0`.

**So we do not have to swap the global or reimplement movement.** Allocate a second CHR, write its
`m_speed` and `m_moveDir` each frame, set `m_flags1` bit `0x400`, and the engine turns it, animates
it, moves it, collides it and clamps it to the ground for free.

### One thing that will fight you: the script also writes those fields

`FFX_Atel_StepActor 0x8666E0` is the per-actor tick. For a character actor it runs the move command,
then branches on **`actor+0x34` bit `0x20`**:

```c
if ( actor->flags34 & 0x20 )  {            // player-controlled: read back off the CHR
    FFX_Atel_ReadMoveDirFromChr(actor);    // 0x869DF0  motionState+0x20 = chr->m_moveDir
    FFX_Atel_ReadRotFromChr(actor);        // 0x869F90  motionState+0x34 = chr->m_rotY
} else {                                   // script-driven: push onto the CHR
    FFX_Atel_ApplyMoveToChr(actor);        // 0x870540  calls FFX_Ch_SetMoveSpeed
    FFX_Atel_ApplyDirToChr(actor);         // 0x870360  calls FFX_Ch_SetMoveDir
    FFX_Atel_ApplyRotToChr(actor);         // 0x870DC0
}
```

So which of the two spawn strategies we pick decides whether this matters:

- **A CHR allocated by us has no ATEL actor at all**, so nothing clobbers it and nothing has to be
  flagged. It is also invisible to the script: line and box triggers test the *player* actor's
  movement (`FFX_Atel_StepLineTrigger 0x8684B0`, `FFX_Atel_StepBoxTrigger 0x866CC0`), so player 2
  will walk through doorway triggers without firing them. For phase 1 that is the right trade.
- **Driving an existing actor's CHR** (a trailing party member, say) needs `actor+0x34` bit `0x20`
  set on that actor or `FFX_Atel_ApplyMoveToChr` overwrites both fields immediately after we write
  them, every frame. Setting the bit also keeps the actor's own idea of its heading in sync, which is
  what triggers and later cutscenes read. Do not reuse `FFX_Atel_BindPlayerChr 0x871AB0` wholesale
  for this: it binds exactly one CHR, and `FFX_Atel_UnbindPlayerChr 0x871A73` clears bit `0x20` on
  *every* actor. Set the bit directly.

There is a third option that we do not want: the script's own point-to-point mover. The MOVE command
(`actor + 0x5B8 + 76*channel`, 9 channels, target XYZ at `+40/44/48`, kind in the low 7 bits of `+2`)
is an arrival-detecting controller that wakes the script when it lands and then zeroes its own kind
word. Steering it from a pad means rewriting the target ahead of the stick every frame and
suppressing arrival forever. It is strictly more state for a worse result. Keep it in reserve for the
opposite job: when a cutscene needs player 2 walked to a mark, clear bit `0x20` and the script takes
over with no other work.

## Registration: there is none

A CHR ticks and renders if and only if:

1. it lives in `g_ffxChrArray[0 .. g_ffxChrCount)` (so it must come from `FFX_Ch_Allocate`, not our
   own allocation),
2. `m_inUse != 0`,
3. `m_hideFlags == 0` (or `m_b181 != 0`), which is true after `FFX_Ch_Allocate`'s memset,
4. `m_flags2` bit `0x20` clear,
5. `m_instance != 0`, which the engine fills in by itself (see below).

Every per-frame pass is a raw linear walk of the array at stride `0x880`, called straight from
`FFX_MainStep 0x820AE0`:

| call site | function | role |
|---|---|---|
| `0x820FDF` | `FFX_Ch_ClearFlags1StepBits 0x832DB0` | clears `m_flags1 & 0x07000000` |
| `0x82107D` | `FFX_Ch_UpdateMotionAll 0x832E10(dt)` | turn, animate, integrate, collide, ground clamp |
| `0x8210DA` | `FFX_Ch_DispatchInBatches 0x833390` | the parented CHRs, plus motion events |
| `0x8212F8` | `FFX_Ch_StepAll 0x82ED50` | per-viewport render state |
| `0x821465` | `FFX_Ch_ShadowPassAll 0x830180(0)` | shadows |

Both batch passes hand work to `FFX_Ch_UpdateRenderJob 0x833530` on the Phyre worker pool via
`FFX_Ch_DispatchUpdateJob 0xA44B90`, and that job also iterates a contiguous run of array slots.
There is no separate list anywhere.

## Tidus is chr id 1

`chrId = (category << 12) | number`, and `FFX_Ch_LetterToCategory 0x829D80` gives
`c=0 pc, m=1 mon, n=2 npc, s=3 sum, w=4 wep, f=5 obj, k=6 skl`. So id 1 is category 0 number 1,
which `FFX_Ch_IdToModelName 0x838100` formats as `"c001"`.

Five independent confirmations:

1. `FFX_Ch_BindChrData 0x826070` does `strcmp(chrdata->m_name, "c001")` and `strcmp(..., "c101")`
   and on a match caches the CHR in `g_ffxTidusChr 0x12FBC60`. That is the engine's own "this is the
   main character" test, and `FFX_Ch_Dispose` clears it.
2. `ViewerManager__ctor 0x6B5870` defaults its character to `"c001"`.
3. `FFX_Ch_Allocate`'s fallback for a CHRDATA that fails to load is `FFX_Ch_LoadChrData(1)`.
4. A Phyre asset-load hook special-cases any path containing `"/c001/"`, so the main character gets
   its own shader treatment.
5. `FFX_Ch_DebugSpawnByName 0x8295E0` is literally
   `id = (letterToCategory(name[0]) << 12) | atoi(name + 1)`.

`c101` (id 101) is the alternate main-character model and `FFX_Ch_BindChrData` treats it identically.

**A clone needs no new assets.** Motion banks are registered globally by model id into
`g_ffxMotGroupTable 0x1300A08`, not per CHR, so a second `c001` shares everything already resident.

## The spawn recipe

The shipped code to copy is `FFX_Field_ChrRegTask 0x861850` (the `"CHRREG"` boot task) and, shorter,
`FFX_Ch_DebugSpawnByName 0x8295E0`.

```c
// once per map, after the "DONE" boot task has run
if (g_ffxChrArray == 0) return;                      // pool not up yet
if (FFX_Ch_CountLive() >= g_ffxChrCount) return;     // FFX_Ch_Allocate null-derefs when full

FFX_Ch_RomRead(1);                                   // 0x829EF0, idempotent
if (FFX_Ch_DataReadSync(1) == 1) return;             // 0x82A040: 1 = still loading, 0 = ready,
                                                     //   2 = no cache entry (pops an error box)
FFX_Ch_MotionSetReadStart(1, 0);                     // 0x836A40
if (FFX_Ch_MotionSetReadSync(1, 0) == 1) return;     // 0x836A50

CHR *clone = FFX_Ch_Allocate(1);                     // 0x824F90
if (!clone) return;
FFX_Ch_LoadMotionSetSync(1, 0);                      // 0x836870
FFX_Ch_SetByte184(clone, 15);                        // 0x835B50, what field CHRs get
FFX_Ch_SetPartyIndex(clone, 255);                    // 0x82B0D0, not a party member
FFX_Ch_SetPos(clone, x, y, z);                       // 0x82B480
FFX_Ch_SetRot(clone, yaw);                           // 0x82B520
// nothing else.
```

Then every frame, from our own input source:

```c
FFX_Ch_SetMoveSpeed(clone, speed);                   // 0x82B840
FFX_Ch_SetMoveDir(clone, heading);                   // 0x82B190
FFX_Ch_SetFlags1Bit400(clone, 1);                    // 0x82AAE0
```

**One extra line is needed, and it is the only one-instance assumption in the whole path:**

```c
CHR *realTidus = g_ffxTidusChr;                      // 0x12FBC60, read BEFORE allocating
CHR *clone = FFX_Ch_Allocate(1);
// ... the rest of the setup ...
g_ffxTidusChr = realTidus;                           // put it back, see below
```

`FFX_Ch_BindChrData 0x826070` writes `g_ffxTidusChr 0x12FBC60` unconditionally at `0x8260EC` for any
CHR whose `CHRDATA.m_name` is `"c001"` or `"c101"`. It is a one-slot cache with no guard, so
allocating a second Tidus silently repoints it at the clone. That matters because of one of its two
readers, `FFX_Ch_Dispose 0x8266F0`:

```c
if (FFX_Ch_GetPlayerChr() == chr || g_ffxTidusChr == chr) {
    FFX_Ch_SetPlayerChr(nullptr);
    g_ffxTidusChr = nullptr;
}
```

Disposing the clone would therefore also null `g_ffxControlledChr` and drop player 1's pad binding.
The other reader, `FFX_Ch_StepAll 0x82EE60`, just publishes that CHR's position and is cosmetic. Six
xrefs in total, two of them readers, and one dword write fixes it.

Teardown: `FFX_Ch_DisposeIfLive(clone) 0x8722F0`, then drop the pointer and respawn after the next
map load.

`FFX_Ch_DataDispose(1, 0)` is **safe** and needs no special handling. `0x824F00` opens with
`if (FFX_Ch_FindById(id) != nullptr) return -1;`, and `FFX_Ch_FindById 0x8261F0` is a linear scan of
`g_ffxChrArray` for a live CHR with that id. So while either Tidus is alive the call is a no-op and
the data survives until the last one dies, which is exactly the right semantics for N instances. An
earlier version of this doc said never to call it. That was wrong. The two with **no** such guard,
which really will pull the data out from under a live clone, are `FFX_Ch_DataDisposeForce 0x8259F0`
and `FFX_Ch_DataDisposeAll 0x8259C0`.

### What happens on its own, with no mod code

- **The mesh attaches itself.** `FFX_Ch_Allocate` calls `FFX_Chr_AttachModelInstance 0x63D370`. If
  the Phyre model is not loaded yet it queues a 16-byte record, and
  `FFX_Chr_ProcessPendingAttachments 0x639DB0` (called every `FFX_MainStep`) drains that queue a few
  frames later. `m_instance != 0` is the "mesh is live" test.
- **The skeleton is built.** `FFX_Ch_BuildSkeletonInstance 0x8277F0`, also from `FFX_Ch_Allocate`,
  allocates the joint node array into `m_joints` and the animated channels into `m_channels` from the
  skeleton header at `m_skeleton`. Without this there are no joints to animate.
- **It idles and walks.** `FFX_Ch_Allocate` seeds `m_slots[0..2] = 0/1/2` and leaves
  `m_locomotionMode` at 0, which makes `FFX_Ch_UpdateMotionAll` run the locomotion driver every
  frame.
- **It is already visible.** `m_hideFlags` is 0 after the memset.

### Do not call FFX_Ch_SetHidden

`FFX_Ch_SetHidden 0x82B4E0` (named `FFX_Ch_SetVisible` in earlier passes) is **inverted: its
argument is hide**. Proof: the debug CHRInfo window prints `CHR+0x180` as `"Hide   : %x"` with bit
labels; every per-frame pass skips a CHR whose value is non-zero; and
`FFX_ChrInstance_SetHidden 0x63C390` is itself inverted, its `a2 == 1` being what relinks the mesh
into the render graph.

The label strings at `0xB5B6F8` read `" Ev" " Eff" " Btl" " Disp" " Z"`, but that is print order, not
bit order. The actual bits, from the `test` sequence in `SG_DebugGui_ChrInfoWindowProc`, are `0x01`
Ev, `0x02` Eff, `0x08` **Disp**, `0x10` **Btl**, `0x20` Z, with `0x04` unlabelled.

## Pointer lifetime

- `g_ffxChrArray` is written only by `FFX_Ch_AllocChrArray 0x825670`, so a raw `CHR *` is stable for
  the life of the pool.
- The pool is freed and reallocated on **every map transition**, so a pointer is not stable across
  one. Store the slot index instead: `idx = ((char *)chr - g_ffxChrArray) / 0x880`. That also makes a
  natural network id.
- `FFX_Ch_MoveChrMemory 0x826FA0` does **not** move CHRs. It compacts the heap blocks a CHR points at
  (`m_joints`, `m_collisionVolumes`, `m_parts`, `m_channels`, the motion pointers and the per-part
  blocks). So any pointer read *out of* a CHR must be re-read each frame rather than cached.

## Pool capacity

`maybe_FFX_Map_SetupChars 0x875510` -> `FFX_Ch_EventjumpInit 0x83B460` -> `FFX_Ch_AllocChrArray`
sizes the pool as `max(36, atelCharActorCount + n + 8)`, so **maps ship with at least 8 spare slots
and a floor of 36**. That `+8` is the sanctioned headroom for a clone.

`FFX_Ch_AllocChrArray` allocates `2 * count * 0x880`, but the upper half is **not** spare capacity -
it is the shadow area `FFX_Ch_CopyToScratchSlot 0x828B30` uses for map save and restore, and
`sub_866240` stores that address in the field object. Do not raise `g_ffxChrCount` to steal it.

## Where to hook

`maybe_FFX_Field_QueueBootTasks 0x861160` pushes a named task chain onto
`g_ffxBootTaskQueues 0x1327F50` through `FFX_BootTask_Push 0x8751B0`:
`"CAMERA"`, `"MOVIE"`, `"SNDCLR"`, `"SNDREG"`, `"CHRREG"`, `"EFFDONE"`, `"DONE"`, `"DONE2"`.
`FFX_BootTask_RunHead 0x8750B0` runs the head as `fn(&stateDword)` and pops it when it returns
non-zero.

Three options, best first:

1. **`FFX_BootTask_Push(0, myTask, "COOP")`** - appends after `"DONE"`, runs once the real CHRs
   exist, and the callback gets a persistent state dword. This is the engine's own idiom.
2. Hook `FFX_Field_BootDoneTask 0x861480` - single purpose, one caller, fires once per field load.
3. Hook `maybe_FFX_Scene_Init 0x88E070` for the earlier "a new scene is starting" edge. At that
   point the pool exists but the CHRs do not.

For the per-frame work, the loader's existing `FFXApplication::animate` vtable hook (RVA
`0x0070D9A8`) is already proven and runs before `FFX_MainStep`.

## Input for the second player

**Settled: a second gamepad needs no hook at all.** PhyreEngine keeps 18 fully independent pad slots
and FFX only ever reads slot 0, so slot 1 is a free second controller that can simply be read.

```c
BYTE *app    = *(BYTE **)g_pApplication;              // 0xCC9CD8
BYTE *mapper = *(BYTE **)(app + 0x2B8);               // PInputMapper
BYTE *dev    = *(BYTE **)(mapper + 0x38 + 4*slot);    // slot 0..17, NULL if unbound
```

Confirmed in `Phyre__PInputMapper__bindPads 0x626AA0`, which loops `i < 18` storing the i-th device
whose `getDeviceType() == 2` into `mapper + 0x38 + 4*i`, and in
`Phyre__PInputMapper__latchDeviceStates 0x628320`, which walks the same 18 slots. `mapper = *(app +
0x2B8)` is corroborated twice, by `Phyre__PApplication__getPadAxis` using `[ecx+2B8h]` and by
`Phyre__PInput__updateAndCheck` passing `*(g_pApplication + 696)`.

Per device, the same layout for the XInput and DirectInput backends:

| field | offset | notes |
|---|---|---|
| buttons, latched held state | `dev+24 + (semantic-11)` | 16 bytes. This is what `isPadButtonDown` returns |
| buttons, raw per-frame | `dev+920 + (semantic-11)` | written by the device poll |
| connected | `dev+105` | byte |
| LX / RX / LY / RY | `dev+880 / +884 / +892 / +896` | int, about -255..255. **LY is positive-DOWN** |
| analog channel ptr | `dev+56 + 4*axis`, value at `+28`, deadzone at `+24` | float -1..1, deadzone 0.2 |
| XInput user index | `dev+944` | creation order, not a true XInput slot |

Semantics 11 to 26: square, cross, circle, triangle, L1, R1, L2, R2, select, start, L3, R3, up, right,
down, left.

**Read the bytes rather than calling the accessor.** `Phyre__PApplication__isPadButtonDown 0x629CA0`
decompiles to exactly `*(BYTE *)(dev + semantic - 11 + 24) != 0` after two bounds checks, so the call
buys nothing. `Phyre__PApplication__getPadAxis 0x628460` (args `axisIndex`, `padIndex`, axes 0 LX, 1 LY,
2 RX, 3 RY) is a real function that applies the 0.2 deadzone, and is fine to call if you want that.
Freshness is not a concern either way: the device poll runs from `_WinMain` through
`Phyre__PInput__updateAndCheck` every frame, independent of any FFX code path.

**Two corrections to earlier versions of this section.**

1. **`dev+40..55` is NOT a previous-frame button array.** `latchDeviceStates` compares against it and
   then overwrites it with the current value in the same pass, so after the latch it equals the current
   state. There is no engine-maintained pressed or released array for pad buttons. Do your own edge
   detection.
2. **The scePad port layer is a trap, and the earlier "port 1 is free" claim was wrong.**
   `FFX_Pad__getPortState 0x888EC0` (0x100 stride, buttons at +152/+154) and `FFX_Pad__getPortBuffer
   0x888E10` (32-byte stride, buffers at `0x13304C0`) do exist, and `FFX_Pad__updateAll 0x889A80` really
   does step both ports 0 and 1 while `FFX_Pad__commitAllPorts 0x8893E0` commits a ring slot for each.
   So port 1 looks free. **It is not: it is a byte-for-byte clone of port 0.** Both ports are filled by
   `FFX_Pad__fillSceReadData 0x8898A0` and `FFX_Pad__fillAnalogAndSynthDpad 0x889570`, which ignore
   their port argument and call `FFX_Input__getButtonMask 0x630C80` and `FFX_Input__getAnalogAxes
   0x630C90`, neither of which takes a port. Both return the single `g_ffxInput 0xCCB170` singleton. The
   sce read itself is a stub. Reading port 1 gives you player 1's input.

   If you ever do want to go through that layer, the one place the design intended a port to matter is
   those two fill functions, so hooking them to honour their `port` argument is the clean version.
   Writing the port state directly does not work reliably, because `FFX_MainStep` calls `updateAll` at
   `0x820B84` and `commitAllPorts` at `0x820BA3` and the write would have to land between them.

**Caveats worth testing rather than assuming.** Slot assignment is DirectInput enumeration order,
`bindPads` fills slot `i` only if it is currently 0, and nothing clears a slot on disconnect, so a
device keeps its slot for the life of the process and which physical pad becomes slot 1 is not something
the game decides. The `"Controller(%d): Controller (%d) has been connected."` lines printed through
`Phyre_TtyPrintf 0x4352F0` show exactly this slot index. Hotplug is also doubtful:
`Phyre__PInput__update` re-enumerates only when `g_phyrePInputRescanDevices 0xCC9CD0` is set, and what
sets it has not been traced.

The older channel, kept for reference: `FFX_Input__poll 0x632A30`, reached from
`FFX_Input__updateFromFFXApp 0x630E80`, collapses everything to one logical pad. `g_ffxInput 0xCCB170`
holds the current PS2 button mask at +0, previous at +4, analog LX/LY/RX/RY at +140/144/148/152. That
is the singleton everything downstream reads, which is why it is the wrong place for a second player.

Simplest first cut: read a second XInput pad ourselves in the `animate` hook and write
`m_speed` / `m_moveDir` directly. That skips the pad emulation entirely.

**What the prototype does:** it reads Phyre pad slot 1 directly, as above, and falls back to the arrow
keys through `GetAsyncKeyState` when no second pad is bound. Either way it writes `m_speed` and
`m_moveDir` from the `animate` hook. No input hook, no pad emulation, no XInput of our own.

It mirrors two details of the game's own feel. Stick deflection scales speed, so a half-pushed stick
walks. And **cross is the walk modifier, not the run button**: `stepControl` picks `16.0` while cross is
held and `38.0` otherwise, and 16 is below the CHR run threshold of 18 while 38 is above it, which is
exactly what flips the animation.

## Nothing else is shared: the three subsystem audits

Each system a second CHR has to pass through was audited for one-instance assumptions, by enumerating
every piece of mutable state outside the CHR struct rather than by reading the happy path. All three
came back clean. The `g_ffxTidusChr` write above is the only hazard found anywhere.

**Character data: sharing is the designed path, not a hack.** `FFX_Ch_LoadChrData 0x825A40`
short-circuits on `FFX_Ch_FindChrData`, so the second `FFX_Ch_Allocate(1)` gets the *same*
`CHRDATA *`. No copy, no refcount, no second cache lock. Nothing in the binary ever stores through
`chr->m_data`: a sweep of all 1,567,690 instruction heads in `0x600000-0xB0C000` for displacement
`0x1C4` found 55 functions, and a per-function taint check for stores whose base register came from
`[x+1C4h]` found zero. The only write to `+0x1C4` itself is in `FFX_Ch_Allocate`. There is no back
pointer from `CHRDATA` to a CHR, only 7 functions reference `g_ffxChrDataTable` at all, the skeleton,
channels, parts and collision volumes are per-CHR heap blocks, the render instance is a fresh 32-byte
record per CHR, and section 10 of the `.chr` is memcpy'd into a private block rather than shared.

The engine also already ships cross-model sharing of exactly this kind: 146 of 1,023 pc motion ids
have a high word that is not their own model, and `c041` borrows all four motion banks of `c007`.

**Motion: no globals at all.** `FFX_Mot_AdvanceFrame`, `StepSequence`, `SeqExec`, `StartClip`,
`BindChannels`, `EvalChannels`, `DecodeKeyStream`, `BlendChannels`, `BuildJointTransforms`,
`ComposeJointSrt`, `RunEvents`, `WrapFrameAtEnd`, `RewindChannels`, `SetMotionKey`, `SetByModeIndex`
and `AutoLocomotionAnim` write none. Details in the Motion section below.

**Walkmesh: per CHR, and the step is strictly serial.** The current triangle is `CHR+0x824`, not a
global cache. The five walkmesh globals (`g_ffxWalkmeshTris 0x1301A84`, `TriCount 0x1301A88`,
`Verts 0x1301A8C`, `Scale 0x1301A90`, `Header 0x1301A94`) are written only by
`FFX_Ch_SetActiveWalkmesh 0x83EA00` on a map or zone change, and are read-only while stepping.

Three globals *are* transient scratch, `g_ffxWalkmeshEdgeRecurseDepth 0x1301AE8` and
`g_ffxWalkmeshCrossedEdgeV0/V1 0x1301AEC/0x1301AF0`, but they are written and consumed inside a single
`FFX_Ch_WalkmeshMove` call and the call chain has no branching at all:
`FFX_Ch_WalkmeshMove 0x83E5F0` has exactly one caller, `FFX_Ch_ResolveCollisionsAll 0x83D3B0`, which
has exactly one caller, `FFX_Ch_UpdateMotionAll 0x832E10`, which has exactly one caller, `FFX_MainStep`
at `0x82107D`. Nothing in the Phyre worker-job path reaches it. So they are safe as long as nothing
calls `FFX_Ch_WalkmeshMove` re-entrantly or off the main thread.

Note the address: `FFX_Ch_WalkmeshMove` starts at **`0x83E5F0`**. `FFX_GAME_NOTES.md` said `0x83E670`,
which is inside the body.

### Three walkmesh behaviours that are not bugs but will be visible

1. **The clone is "not the player" for passability.** `FFX_Map_IsTriBlockedForChr 0x83E4E0` resolves
   "the player" as `FFX_Ch_GetPlayerChr()`, i.e. the single global `g_ffxControlledChr`. Surface code
   2 means "wall for everyone except the player" and code 14 means "wall only for the player". So the
   clone gets **blocked** by player-only passages and **walks through** places Tidus cannot. 6,087
   code-14 triangles and 663 code-1 triangles ship, so expect divergence at scripted barriers.
2. **Debug noclip is player-gated** and will not affect the clone.
3. **One walkmesh is active at a time.** `FFX_Map_SetZoneWalkmesh 0x90A4A0` swaps the globals wholesale
   and resets `m_walkmeshTri` to -1 on every live CHR, so both characters have to be in the same zone
   of the same map. A CHR with `m_walkmeshTri == -1` takes the off-mesh branch, which moves it at **10x
   speed with no collision** until it finds a triangle. That is the failure mode to watch for if the
   clone ends up somewhere the new zone's mesh does not cover.

Triggers are a separate matter, and the earlier claim is now confirmed from the code rather than
inferred. Both `FFX_Atel_StepLineTrigger 0x8684B0` and `FFX_Atel_StepBoxTrigger 0x866CC0` read only
the bound player actor's current and previous position out of the ATEL context
(`g_ffxAtelCtx 0x1326B28`, fields +536 and +552), and the two functions that write those positions are
both gated on `actor->id == *(u16*)(g_ffxAtelCtx + 10)`, the bound player actor id. Neither step
function loops over other actors or CHRs. **The clone walks through every line and box trigger without
firing it.**

## The camera

Short answer for phase 1: **do nothing.** `FFX_PlayerCam_Step 0x83F2E0` frames whatever
`g_ffxControlledChr 0x1300788` points at, so a second CHR that global does not point at is already
ignored. Player 2 can walk off screen, which is the correct first milestone. Full detail is in
`FFX_GAME_NOTES.md` under Camera.

When that stops being acceptable, the three options rank like this:

**(a) One shared camera framing both players - cheap, and the right next step.** Trampoline the 5-byte
`call FFX_PlayerCam_Publish` at `0x83F498`, which is the last thing `FFX_PlayerCam_Step` does. Rewrite
`g_ffxPlayerCamRefPos 0xC4DED0` to the midpoint of the two CHRs and `g_ffxPlayerCamDistTarget
0x1301BB8` from their separation, then tail-call the original. Every piece of state involved is a
global, so there is no register pressure and no stack work. Roughly 30 bytes of new code and no engine
changes. The engine's own `act` camera mode already midpoints two actors, so this is not fighting the
design.

Two things it needs on top: a leash so the players cannot separate past the frustum, and a fallback for
the maps where the event script installs a class 3 camera that outranks the player camera. For those,
either pass both actor ids to `FFX_Came_SetTargetActors 0x7BEEB0` or take the whole thing with
`FFX_Came_SetMatrixOverride 0x7C06D0`.

**(b) True split-screen - genuinely possible, but the UI work dominates.** The structure is all there
and shipped: 3 screens, each with its own enable byte, rect and camera slot, and a two-pass per-screen
render loop already inside `FFX_MainStep`. `FFX_Came_SetScreenEnableAndSlot 0x7BC950` is even entry 692
of the magic plugin API, so a loaded DLL opening a second screen is within the intended design. The
work is `SetScreenEnable(1,1)`, `SetScreenRect(1, ...)`, `SetScreenSlot(1,1)`, plus a clone of
`FFX_PlayerCam_Step` that reads player 2's CHR and publishes to a handle whose screen byte is 1.

What makes it expensive is not the camera, it is that **the per-screen rect lives in the projection
matrix and there is no scissor**. Scene geometry frames correctly, but sky, post-process, fades, UI and
the debug overlay all ignore the rect and bleed across the divider. Add to that: only two shipped maps
(152 and 601) ever enable screen 1, so expect latent bugs on that path, depth and clear handling
between the two passes is unverified, and the HUD is not per-screen. Call it 5 to 10 times the cost of
option (a).

**(c) Follow player 1 only - free, already the behaviour.** This is phase 1.

### But you do need the camera yaw, for input

Not to move the camera, to read it. Without it a second character walks in world axes, so "up" on the
keyboard is not up on the screen and the controls are unusable. The game already solves this and the
fix is to copy its formula:

```c
BYTE *slot = *(BYTE **)g_ffxCameActiveSlot;        // 0x2311430, never null
const float *ref = (const float *)(slot + 0x050);  // look-at target
const float *cam = (const float *)(slot + 0x66C);  // eye
float camYaw  = atan2f(ref[2] - cam[2], ref[0] - cam[0]);
float desired = atan2f(right, forward);            // note the argument order
chr->m_moveDir = camYaw - desired;
```

That is `FFX_Came_GetYaw 0x7BCF20` inlined, and the subtraction is what `FFX_Player__stepControl` does
at `0x82D786`. It needs no unit conversion, because `m_moveDir` and this yaw share the same zero axis
and direction. Full derivation, the three globals that look like easier answers but are stale or use
the opposite convention, and why to inline the `atan2` rather than call the function, are in
`FFX_GAME_NOTES.md` under "Reading the camera yaw at runtime".

## Useful extras

- `FFX_Ch_CopyState 0x828620(dst, src)` copies most mutable state between two CHRs - position,
  facing, `m_moveDir`, hide flags, the look-at block, the seven shade animators
  (`memcpy(dst+0x330, src+0x330, 0x54)`), `m_speedMulA` (0x750) and the whole motion slot table
  (`memcpy(dst+0x75C, src+0x75C, 0x80)`). The natural clone primitive.
  It deliberately skips three things: **`m_speed` (0x154)**, `m_cameLen` (0x178, an output anyway)
  and the owning motion bundle at `0x7E8`. So it will not give a clone a live motion, and it will
  not make it move on its own.
- `FFX_Ch_SetPlayerChr 0x82DAD0` / `FFX_Ch_GetPlayerChr 0x82D860` move the single player binding.
  `FFX_SG_DebugWin_OpenChrAndTakeControl 0x84CEA0` is a shipped "possess any live CHR" action.
- `FFX_Ch_AttachToParentBone 0x832630` parents one CHR to another at a bone - how weapons work.
- `g_ffxMotEventLog 0x13008DB` set to 1 gives a live per-frame animation event trace with no
  patching, and `g_ffxMotHalfSpeed 0x13017F8` is a free slow-motion animation debugger.

## Motion

### The answer for phase 1: a clone animates safely, and nothing is shared

Checked by a systematic `.data` and `.bss` write scan over every function in the playback path, with
the detector validated against known writers first. `FFX_Mot_AdvanceFrame`, `StepSequence`,
`SeqExec`, `StartClip`, `BindChannels`, `EvalChannels`, `DecodeKeyStream`, `BlendChannels`,
`BuildJointTransforms`, `ComposeJointSrt`, `RunEvents`, `WrapFrameAtEnd`, `RewindChannels`,
`SetMotionKey`, `SetByModeIndex` and `AutoLocomotionAnim` **write no globals at all**.

There is no scratch buffer, no one-entry cache, and no back pointer from bundle, motion entry or clip
to a CHR. The decode cursors live in the CHR's own per-joint channel array at `m_channels` (0x748),
heap allocated per CHR, so two CHRs decoding the same key stream walk independent cursors over
read-only bytes. `FFX_Ch_BuildSkinMatrices` uses a 0xC104 byte **stack** buffer, so it is per call.
`FFX_Ch_LoadMotionSetSync 0x836870` is idempotent, it early-outs if the bundle is already registered,
so the spawn recipe calling it costs nothing.

Three shared things that are not corruption but are worth knowing:

- `m_motionBundle` (CHR +0x7E8) records which `.mgrp` the current motion came from, and
  `FFX_Ch_ReleaseResource 0x836E20` walks the **whole** CHR array clearing `m_seqPC`, `m_motionId`
  and that field on every CHR using the bundle. So unloading a motion set stops the clone too. The
  matching `FFX_Ch_RelocateResourceBlock 0x8370C0` also walks all CHRs, which is good for us: a
  clone's pointers get fixed up for free when a bundle moves.
- `FFX_Mot_StartClip` has a hardcoded special case keyed on `strcmp(chr->m_name, "c001")` plus scene
  122 plus `startFrame == 12288`. It keys on the CHRDATA name, so a clone gets the same treatment.
  Harmless, but do not be surprised by it.
- `maybe_g_ffxC102SkinHackFlag 0x13008E0` is the one genuine cross-CHR global in the motion and
  render path, and it is gated on model id 102. A duplicate of model 1 cannot reach it.

### Tidus idle, walk and run, as concrete numbers

`FFX_Ch_AutoLocomotionAnim 0x835BB0` picks from the slot table purely off `m_speed`:

```c
if (m_flags1 & 0x400) m_rotY = approach(m_rotY, m_moveDir, rate);  // 0.314 idle / 0.349 walk / 0.524 run
if (m_motionRequestActive) { m_flags1 &= ~0x8000000; return; }     // a scripted motion wins
if (m_speed <= m_runThreshold)          // m_runThreshold is CHR+0x170, defaults to 18.0
    if (m_speed == 0.0)  play(m_slots[0]);   // idle, also sets flags1 0x4000000
    else                 play(m_slots[1]);   // walk, sets flags1 0x8000000
else                     play(m_slots[2]);   // run
```

The ids themselves are **in the `.chr` file, not the `.mgrp`**. `FFX_Mot_SetByModeIndex 0x837D00`
reads `*(CHRDATA.m_blobBase + 16 + 8*g_ffxMotModeToSetSlot[mode])`, and
`g_ffxMotModeToSetSlot 0xC49784` is `BYTE[4] = {5,6,7,8}`, so `.chr` sections 5/6/7/8 are flat `u32`
arrays of complete motion ids indexed by logical index. For `c001.chr` the section counts are
5 / 216 / 4 / 215, which match the motion counts of `resident0..3.mgrp` exactly.

Tidus, field mode, `c001.chr` section 5:

| slot | motion id | clip | frames | loop | role |
|---|---|---|---|---|---|
| `m_slots[0]` | `0x00011040` | 0 | 40 | forever | **idle** |
| `m_slots[1]` | `0x00011094` | 4 | 28 | forever | **walk** |
| `m_slots[2]` | `0x00011093` | 3 | 24 | forever | **run** |
| - | `0x00011066` | 2 | 30 | once | transition |
| - | `0x00011065` | 1 | 30 | once | transition |

Walk and run were told apart independently of the slot order, by their own embedded footstep sound
events: the run clip fires SND id 1 at frames 6 and 18 (a 12 frame cadence) and the walk clip fires
SND id 2 at frames 8 and 22 (14 frames). Swim, from `FFX_Ch_AutoSwimAnim 0x835D30` and section 7:
`m_slots[16]` `0x110B8` idle, `m_slots[18]` `0x1107D` submerged, `m_slots[19]` `0x1107C` surface,
surface chosen when `m_posY > g_ffxWaterLevel + 1`.

The motion set files are `<chr>/mot/resident<mode>.mgrp` with mode 0 field, 1 field battle, 2 swim,
3 swim battle, pinned by the debug tag `'[%s:resident%d.mgrp]'` at `0xB59700`. See
`MGRP_FORMAT.md` for the container and the sequence bytecode.

### The rest, in brief

A motion id is `(modelId << 16) | motionIndex`. `FFX_Ch_SetMotionKey 0x837AC0` resolves one against
the global group chain `g_ffxMotGroupChainHead 0x13011D8` and fills the CHR's motion pointers, but
does not start the clip. The public setters that do both are `FFX_Ch_SetMotionTbl 0x837350`,
`FFX_Ch_SetMotionFrames 0x8373E0`, `FFX_Ch_SetMotionData 0x837460` and
`FFX_Ch_SetMotionFramesInclusive 0x8374C0`, all ending in `FFX_Mot_StartClip 0x839B80`.

`FFX_Mot_AdvanceFrame 0x838C90` advances `m_frame` (24.8 fixed point) by
`m_frameRateScale * ((m_speedMulA * m_playSpeed) >> 8) / 7680`, which with both multipliers at 256 is
exactly one frame per tick.

**FFX does not use PhyreEngine's animation runtime at all.** Of 2,464 `PAnimation*` symbols, only
three have a code xref outside the reflection registration region, and all three are type-descriptor
lookups. The PS2 code computes joint matrices itself in `FFX_Ch_BuildSkinMatrices 0x832760` and
memcpys them into Phyre through `FFX_ChrInstance_UploadBoneMatrices 0x63BE60`. That is the entire
bridge, and it is driven off the array like everything else.

To retarget a clone's locomotion you write its slot table, not a motion id:
`FFX_Ch_SetSlot 0x82AFE0(chr, slot, value)`. A slot whose low word is `>= 0x1000` is taken as a
complete motion id, otherwise it is a logical index resolved through
`FFX_Mot_SetByModeIndex 0x837D00`.

`MotDump` is compiled out - the menu entry dispatches to an empty `0xC3` at `0x836F00` whose format
strings were stripped. `SG_DebugGui_ChrInfoWindowProc 0x8537D0` is the intact CHR dumper and is where
most of the motion field names came from.

## Known unknowns

- The exact `g_ffxChrCount` per map needs a live read of `0x23C44E0`.
- Whether the walk/run threshold and the three turn rates need per-clone tuning, or whether the
  defaults look right on a second character.
- How two characters should interact with the field camera, which currently follows
  `g_ffxControlledChr`.
- `FFX_Ch_ResolveCollisionsAll` pushes CHRs apart in pairs, so two Tiduses will shove each other.
  Whether that is wanted or needs `m_flags1` bit `0x100` (no body collision) set is a design call.
- Where `m_flags1` bit `0x200000` ("needs a cloned ClassCharacter") gets set. Not in
  `FFX_Ch_Allocate`, so some per-type table or script does it.
- Whether FieldBattle (mode 1) and SwimBattle (mode 3) are ever reached through
  `FFX_Mot_SetByModeIndex`. The four modes themselves are now pinned: they are `.chr` sections 5/6/7/8
  selected by `g_ffxMotModeToSetSlot 0xC49784` = `BYTE[4]{5,6,7,8}`, backed by
  `mot/resident{0,1,2,3}.mgrp`, and for `c001` mode 1 holds 216 ids against mode 0's 5. So the battle
  motions are in the file, indexed by logical slot. What is unproven is who passes mode 1 or 3 at
  runtime, since the only callers found were the debug browser and `CHR+0x185`. A breakpoint on
  `0x837D00` during a battle settles it.
- Which of `c002`-`c008` is which party member. `g_ffxDebugCharNames 0xC3432C` gives the eight names
  in party order (Tidus, Yuna, Auron, Kimahri, Wakka, Lulu, Rikku, Seymour) but the model mapping is
  data-driven: the field path takes the chr id from ATEL actor+168. One runtime read of `m_partyIndex`
  against `CHR+0` on a map with a known party settles it.

## Why a spawned CHR does not draw: SOLVED, plus the render path mapped

**Resolution first, since that is what most readers want.** A spawned field CHR is invisible
because nothing binds it to a walkmesh triangle, and the fix is one call:

```c
FFX_Ch_SetPos(clone, x, y, z);        // this deliberately sets m_walkmeshTri = -1
clone->m_velX = clone->m_velZ = 0.0f; // so WalkmeshMove is a pure bind
FFX_Ch_WalkmeshMove(clone);           // 0x83E5F0, binds the triangle and fills m_groundHeight
```

Confirmed at runtime. The log line that proves it:

```
walkmesh bind (spawn): tri -1 -> 27, groundHeight -37.48, Y -37.43 -> -37.43
```

**The mechanism, which is a genuine deadlock.** `FFX_Ch_Allocate` calls
`FFX_Ch_SetGroundMode(chr, 1)`, and ground mode 1 makes `FFX_Ch_UpdateMotionAll` write
`m_posY = m_groundHeight` unconditionally every frame. While the CHR is unbound,
`m_groundHeight` is still the `0.0` that Allocate's `memset` left. On a map whose floor sits at
Y = -37, that yanks the character 37 units into the air on its first frame. Up there it fails
the camera test, so `m_hideFlags |= 0x08` (`Disp`). And `FFX_Ch_UpdateMotionAll`'s own gate is
`m_inUse && (m_hideFlags == 0 || m_b181 != 0)`, so once hidden it is skipped, which is what
would have relocated it. Hidden forever, at Y = 0, never relocated.

The engine never hits this because its own spawns are placed by ATEL scripts on frames where
the character is visible, so the next-frame recovery in
`FFX_Ch_UpdateMotionAll -> FFX_Ch_ResolveCollisionsAll -> FFX_Ch_WalkmeshMove` always runs.

**It needs doing once, not every frame.** A per-frame fallback that re-binds whenever
`m_walkmeshTri == -1` was added as a safety net and it has never fired: after the spawn-time
bind, the engine's own motion pass maintains the binding normally. Worth keeping for map
transitions, but it is dormant in steady state.

**Health check for a correctly spawned clone**, from the runtime log, useful as a target:

```
hide=0(visible)  wmesh=56  flags2=0x111  f354=1.000  meshes=1
draw gate: drawing: 1 of 1 sub-meshes linked, shown=1
flags1=0x457D1C20   vs the player's 0x457D1E20
```

`flags1` matching the real player in every bit except `0x200` is the thing to look for. That
bit is the engine's own "this CHR is the player" marker, maintained by
`FFX_Ch_UpdateRenderJob`, and it is correctly clear on a clone.

---

The rest of this section maps the render path that was traversed to get there. It is kept
because the render path is needed for anything involving a second visible character, and
because the dead ends are worth not re-running. Several wrong theories were discarded along the
way and they are recorded below as well as the conclusions.

### The hide bit vocabulary, from the game's own debug GUI

`SG_DebugGui_ChrInfoWindowProc 0x85393C` prints `"Hide   : %x"` and then appends a label per
bit, which hands us the developers' own names for `m_hideFlags` (CHR+0x180):

| bit | label | meaning |
|---|---|---|
| 0x01 | `Ev` | hidden by an event script |
| 0x02 | `Eff` | hidden by an effect |
| 0x08 | `Disp` | no sub-mesh is camera-visible. Set only by `FFX_Ch_UpdateCameLenAndZClip` |
| 0x10 | `Btl` | hidden by battle |
| 0x20 | `Z` | `m_cameLen > m_clipZ`, distance culled |

**None of it latches.** `FFX_Ch_UpdateMotionAll` does `m_hideFlags &= 0x13` every frame and
`FFX_Ch_UpdateCameLenAndZClip` does `m_hideFlags &= 0xD7` on entry, so `Disp` and `Z` are
recomputed from scratch every frame. There is no stale state to clear.

### FFX_Ch_UpdateRenderAll is NOT the draw path

Worth stating loudly, because it is an easy and expensive mistake. `FFX_Ch_UpdateRenderAll
0x82FE00` has exactly one caller, `FFX_Ch_UpdateRenderForMagic573 0x826950`, whose whole body
is

```c
if (MagicFile__getMagicId() == 573)
    FFX_Ch_UpdateRenderAll();
```

so it runs for exactly one magic plugin and never in normal play. It happens to carry a gate
chain that closely resembles the real one, which is what makes it so convincing. **The real
per-frame path is `FFX_Ch_StepAll 0x82ED50`**, called from `FFX_MainStep` at `0x8212F8`.

### The real chain, in order

`FFX_MainStep` runs `FFX_Ch_UpdateMotionAll 0x82107D`, `FFX_Ch_DispatchInBatches 0x8210DA`,
`FFX_Ch_StepAll 0x8212F8`, then `FFX_Ch_ShadowPassAll 0x821465`.

`FFX_Ch_StepAll` contains four separate walks of the CHR pool. Two of them matter:

**Loop 1 (0x82EE97) pushes the world transform.** Gate: `m_inUse && m_hideFlags == 0 &&
!(m_flags2 & 0x20) && m_instance != 0`. It does
`FFX_Mtx_Mul_Api(m, &chr->m_rootJoint[8], ...)` then `sub_63C3E0(m_instance, m)`, ending in
`FFX_Gfx_SetMeshTransform 0x66D810`, which stores the matrix into the per-mesh transform
object at `mesh+0x04` and sets `bounds+0x0C = mesh+0x04`.

**Loop 3 (0x82F224) does the cull and the show/hide decision.** It calls
`FFX_Ch_UpdateCameLenAndZClip 0x82E220`:

```c
chr->m_hideFlags &= 0xD7;                       // clear Disp and Z
chr->m_cameLen = |m_bodyCentre - FFX_Came_GetPos(1)|;
if (chr->m_hideFlags != 0)              return 1;   // some other hide bit
if ((chr->m_flags1 & 0x800) == 0)       return 0;   // this CHR opts out of z-clip
if (!FFX_Chr_InstanceAnyMeshVisible(chr->m_instance)) { m_hideFlags |= 0x08; return 1; }
if (noCullGlobal || m_cameLen <= m_clipZ)           return 0;
chr->m_hideFlags |= 0x20; return 1;
```

and then, at `0x82F54D`, `FFX_ChrInstance_SetHidden(m_instance, m_hideFlags ? 0 : 1)`.

**`FFX_ChrInstance_SetHidden 0x63C390` has a polarity trap: 1 shows, 0 hides.** It writes
`*(BYTE *)(inst+0x14) = (a2 == 1)`, which is a readable mirror of the last decision, then per
sub-mesh calls `FFX_Gfx_LinkMeshToNode 0x66D910`:

```c
node = *(void **)(mesh + 0x1C);
if (node) *(void **)(node + 0x1C) = show ? mesh : NULL;
```

That single pointer store is what links the mesh into or out of the PhyreEngine graph, so
**reading `node+0x1C` back is a true end-to-end "is this being drawn" test**, downstream of
every FFX-side gate.

### The frustum test works on a bounds proxy, not on the CHR

`FFX_Chr_InstanceAnyMeshVisible 0x63B230` loops the instance's sub-meshes and calls
`FFX_Gfx_IsMeshInstanceVisible 0x6603C0`, which is `__thiscall(gfxCtx, mesh)` with `retn 4`.
Hex-Rays loses the ECX argument and invents a phantom one, so **the decompilation of that
function is actively misleading**. The real body:

```c
if (mesh == 0) return 0;
if (*(DWORD *)(gfxCtx + 0x10D8C) != 0) return 1;   // the cull-disable override
bounds = *(DWORD *)(mesh + 0x1C);
if (bounds == 0) return 1;                          // no bounds, treated visible
return FFX_Gfx_TestBoundsAgainstFrustum(bounds, *(gfxCtx + 0xDC) + 0x74);
```

and the test itself, `FFX_Gfx_TestObbCornersAgainstFrustum 0x663A30`, builds 8 OBB corners
from `bounds+0x00` (corner) and `bounds+0x10` (half extents), transforms them by
`frustum * bounds+0x0C`, and returns `(AND of the 8 clip outcodes) == 0`.

**So the quantity being culled is the bounds box, positioned by `bounds+0x0C`, and not the
character's own position.** A stale transform there means the box sits somewhere the character
is not, and the box is what decides.

### The render-side deadlock, which is real but was NOT the cause here

Worth understanding because it is a trap for anything that hides a CHR deliberately, but note
that the actual invisibility above was caused upstream, by the position being wrong rather than
by the bounds proxy going stale. Put loop 1 and loop 3 together and the failure is
self-sustaining:

1. the bounds box is somewhere other than where the character is
2. loop 3's frustum test rejects it, so `m_hideFlags |= 0x08` (`Disp`)
3. next frame, loop 1 is gated on `m_hideFlags == 0`, so **the transform is no longer pushed**
4. the box stays where it was, so go to 2

The same hide bit also stops `FFX_Ch_UpdateMotionAll`, whose gate is
`m_inUse && (m_hideFlags == 0 || m_b181 != 0)`. That matters twice over, because the walkmesh
search lives under it (see the walkmesh section) and because `m_groundMode == 1`, which
`FFX_Ch_Allocate` sets, makes the motion pass write `m_posY = m_groundHeight` unconditionally.
While unbound, `m_groundHeight` is still the `0.0` left by Allocate's memset, so the character
is dragged to Y=0 before it is hidden.

### Dead ends, recorded so they are not re-run

**The `gfxCtx+0x10D8C` cull override cannot work, and testing it wasted a cycle.** That field
is read in exactly one place, `FFX_Gfx_IsMeshInstanceVisible`, and that function exists only
to maintain `m_hideFlags`. The render passes call `FFX_Gfx_TestBoundsAgainstFrustum` directly
(`sub_652A10` at `0x652D35` and `0x652EBC`) and never consult the override. Forcing it on
therefore clears the hide bit and changes nothing about what is drawn. Confirmed at runtime:
`hide` went to 0 and the character stayed invisible.

A second trap in the same experiment: a one-shot write to that field lapses, because the
engine rewrites it. Any such probe has to be re-asserted every frame or it silently stops
being applied, and the test then looks like a clean negative.

**It is not a duplicate chr id.** Spawning id 103 while the player is id 1 fails identically,
and those are different models and different ClassCharacters.

**It is not the alpha.** `CHR+0x354` is `m_animShadeA.m_cur`, the per-character alpha, and it
is a genuine gate tested in three places in `FFX_Ch_StepAll`. But `FFX_Ch_Allocate` already
calls `FFX_Ch_SetShadeAlpha(chr, 1.0, 0)` at `0x8250F1`, and `FFX_Ch_AnimFloatSet` with
`mode == 0` writes `m_cur` immediately, so it is 1.0 from the start. `CHRANIMF` is
`{float m_cur; float m_target; int m_mode;}` and CHR carries seven of them from `0x330`:
`m_animShadeR`, `G`, `B`, `A` (0x354), `m_anim360`, `m_animShadeCoeff`, `m_anim378`.

**`m_walkmeshTri == -1` is not read by the render path, but it IS the root cause.** This one is
worth stating carefully because the obvious reading is wrong in both directions. Nothing in the
render path reads `CHR+0x824`, so the triangle index cannot gate drawing, and for a long time
that was taken to mean the walkmesh was irrelevant. It is not: being unbound leaves
`m_groundHeight` at 0, and ground mode 1 then moves the character somewhere it cannot be seen.
The walkmesh affects visibility through the *position*, never through the render gates.

The confusing observations that came from this: one character sat on a valid triangle for 90
frames while still invisible (it had already been dragged to Y=0 before binding, and being
hidden then stopped the Y from being corrected), and another drew correctly with
`m_walkmeshTri == -1` (it happened to be bound in the same frame it was spawned). Both are
consistent with the Y clamp being the real mechanism.

**`m_flags2` bit 0 is an effect, not a cause.** It correlates perfectly with visibility across
658 runtime samples, which is seductive. But the only instruction that sets it is in
`FFX_Ch_StepAll` loop D at `0x82FA08`, immediately *after* the work it reports on, and the
only one that clears it is in `FFX_Ch_UpdateRenderJob`. It is a one-shot token between those
two passes and nothing in the draw path reads it. Useful as a diagnostic, useless as a lever.

**`m_objId` and `m_b184` are not render state.** `m_objId` (+0x190) is read by exactly three
functions, all parent-bone attachment. `m_b184` is read by `SndKickFoot` and a footstep table.
Both are worth setting for correctness but neither can affect drawing.

### The engine's own field spawn, for comparison

`FFX_AtelOp_SetActorModel 0x85D000` is the shipped one-call spawner, and it is short:

```c
chr = maybe_FFX_Ch_SpawnWithObjId(typeId, actor->objId);   // -> FFX_Ch_AllocateWithObjId
                                                           // -> FFX_Ch_LoadMotionSetSync
FFX_Ch_SetPartyIndex(chr, actor->partyIdx == -1 ? 255 : actor->partyIdx);
FFX_Ch_SetByte184(chr, actor->b184);
FFX_Ch_SetRotAndMoveDir(chr, dir);
FFX_Ch_SetRot(chr, dir);
FFX_Atel_SetActorPos(...)    // -> FFX_Ch_SetPos + FFX_Ch_MarkDirty
sub_885FD0(b184, objId);     // footstep bookkeeping
```

**There is no walkmesh bind, no ground snap, no draw-list registration, no scene-graph insert,
and no `m_flags1`/`m_flags2` bit beyond what `FFX_Ch_Allocate` already sets.** All nine
callers of `FFX_Ch_Allocate` were enumerated to establish that. So a mod's spawn recipe is not
missing a magic activation step, which is worth knowing because that was the most attractive
remaining hypothesis.

### The runtime probe for bounds problems

This was built to settle the bounds question and is kept because it is the only way to tell
three different render failures apart. It was not needed in the end, since the cause turned out
to be the position rather than the proxy, but a stale or missing bounds box is a real failure
mode (see the creation path below) and this is how to recognise it. For the clone and the
player side by side, read:

```
inst   = *(void **)(chr + 0x830)
n      = *(DWORD *)inst & 0x7FFFFFFF
meshes = *(void ***)(inst + 4)
for each mesh m:
    xform  = *(void **)(m + 0x04)
    bounds = *(void **)(m + 0x1C)
    bounds+0x00..0x08      OBB corner
    bounds+0x10..0x18      OBB half extents
    bounds+0x0C            must equal xform
    bounds+0x1C            must equal m once StepAll has shown it
    xform+0x30..0x38       the translation column
*(BYTE *)(inst + 0x14)     the last show/hide decision
```

against `CHR+0x1D0` (`m_rootJoint[8]`). Three readings, three different answers:

- **extents all zero** -> the shared template bounds were never copied in
- **`bounds+0x0C` null or != `xform`** -> the transform link is broken
- **transform identity or at the origin** -> loop 1 never pushed a matrix, which is exactly
  what being hidden since frame 0 produces

The clone mod logs all of this at spawn, at f+150, and on demand.

### Bounds creation, for when the answer is "never copied in"

`sub_66F7D0 0x66F7D0`, called from `ClassCharacter__createInstance 0x63C540`:

```
0066F8AE  call sub_440B70        ; PCluster::create(1, PMeshInstanceBounds)
0066F8B5  jz   no_bounds         ; allocation failed -> mesh+0x1C stays 0
0066F8BF  jz   no_bounds         ; [eax+0x34] null   -> mesh+0x1C stays 0
0066F8CC  call sub_4FF800        ; bounds+0x0C = *(mesh+4); mesh+0x1C = bounds
```

`sub_440BA0` takes cluster headers from a free list and returns 0 when it cannot grow, which
is the fixed-pool failure mode to watch for: a mesh with no bounds object reads as *visible*
to `FFX_Gfx_IsMeshInstanceVisible` (it returns 1 on a null bounds) but can never be linked by
`FFX_Gfx_LinkMeshToNode`, which tests `if (node)`. So that failure is invisible in the hide
flags and only shows up in the link state.
