# CHEAT_WARP - warping to any map, starting any event, cutscene or dialogue

Subject: the data and the calls behind a "warp anywhere / play anything" cheat panel. Written for
a plugin that runs in process with an ImGui UI, where every option in the UI must come from game
data and typing is only ever a filter.

Addresses are **RVAs** (IDA VA minus `0x00400000`) unless a line says VA. Everything marked
CONFIRMED was read out of the disassembly or out of the shipped data in this pass. Everything
marked INFERRED was not executed and not observed.

---

## 0. The one-paragraph answer

**There is a single id space, 0..401, and it is both the map list and the event list.** The game
calls it a map id in one place and an event id in another and they are the same number. Id 0..401
indexes a path table that names one `.ebp` event package per id, and the package's own header names
the background/walkmesh map number it sits on. 330 of the 402 ids have a package that actually
ships. `FFX_Map_RequestChange(id, entryPoint)` is the whole warp, it is a deferred request with no
gate of any kind, and the engine does the load itself one simulation step later. The entry point is
an index into a 32-byte doorway record array inside the package, which is where the spawn position
comes from.

So "warp to a map" and "start an event" are the same operation with the same argument, and the UI
is one flat list of 330 rows with a second small list of doorways per row.

---

## 1. THE MAP LIST AND THE EVENT LIST (they are the same list)

### 1.1 Where the list lives

**CONFIRMED.** Asset kind 12 of the game's own path table. Three facts, all read from the code:

* `FFX_Ev_LoadEventPackage 0x472EF0` loads the package for id `n` with
  `assetLoader[4](12); assetLoader[5](18*n); assetLoader[0](18*n, buf, 0, 0)` and takes the
  display name from `assetLoader[22](18*n)`. So **kind 12, stride 18, sub-index 0 is the `.ebp`**.
* `FFX_GetAssetLoader 0x36D0D0` returns `g_ffxAssetLoader`, VA `0x2310C40`, a 37-slot function
  pointer table. On the PC build the slots are filled by the function at `0x642BA0`
  (`__cfltcvt_init_143` in the IDB, an IDA misname), which installs `0x642830` as slot 22 and
  `0x6428A0` as slot 5.
* Slot 22 is `ResolvePath(kindBase + subIndex)` where `kindBase = assetLoader[8]`, set by slot 4,
  and `ResolvePath` is `FFX_Asset_ResolvePathWithDebugOverrides 0x642C00`. That reads
  `assetLoader[12]`, the path table itself: `u32 count`, then `count+1` u32 byte offsets relative
  to the table base, then the strings. **A zero-length slot returns `nullptr`, which is the
  validity test the UI needs.**

The three index files that build it are loaded at boot by `0x642FC0`:

| file | slot | what |
|---|---|---|
| `ffx_ps2/ffx/proj/battle/jp/cddata/cdrom.fnd` | 12 | the path table, 811,400 bytes, **16,305 paths** |
| `ffx_ps2/ffx/proj/battle/jp/cddata/cdrom.fid` | 11 | `s16[65]`, the per-kind base index. `-1` means the kind is unused |
| `ffx_ps2/ffx/proj/battle/jp/cddata/cdrom.mdg` | - | the size/offset side table, DVD mode only on PC |

**Kind 12's base index is 562.** So the `.ebp` path for id `n` is path-table entry `562 + 18*n`.
Verified against the shipped `cdrom.fnd`: entry 562 is
`host0:/ffx/master/jppc/event/obj/ev/event00/event00.ebp`, entry 580 is
`.../st/startmap0/startmap0.ebp`, entry 598 is `.../ca/camtest0/camtest0.ebp`. Sub-indices 3..17
are the package's 15 `.mgrp` motion groups, sub-indices 1 and 2 are empty in every row checked.

### 1.2 The other list, and why it is the worse one

There is a second, name-only table that an earlier pass found first: pointer `dword_25D5888`
RVA `0x21D5888`, count `dword_1934EC8` RVA `0x1534EC8`, row stride 16, `char[12]` name at row+0,
`u32` id at row+12. `FFX_AutoTest_JumpMap 0x508100` substring-searches it and
`FFX_Map_GetCurrentMapName 0x507E70` reverse-looks-up the current id in it.

**It is empty at rest in retail.** CONFIRMED two ways: `ida_bytes.is_loaded` is false for both
addresses and both read back as `0xFFFFFFFF`, and the only writer is
`FFX_LoadEventIdTable 0x507F50`, which is reached from `FFX_MainInit` **only in debug mode** and
from `AutoTestManager::execCommand` on the `EnableAutoTest` command. It also has a once-only guard
at `dword_1934ECC` RVA `0x1534ECC`.

`FFX_LoadEventIdTable` reads `/ffx/proj/event/header/eventid.bin` (shipped as
`ffx_ps2/ffx/proj/event/header/eventid.bin`, 6,741 bytes) and builds the rows. That file is
**402 eight-byte index entries** (`u32 nameOffset, u32 nameLength`) followed by the name strings,
and **the id is just the row number**, because the loader writes `row+12 = i++`. I parsed it: 402
names, `event00, startmap0, camtest0, test01, ...`, ending `scene1 .. scene9` at 393..401. One
name is 12 characters (`test_yanagi2`, id 251) and the inline field is 12 bytes with no room for a
terminator, so that row's name runs into the id field in the built table. Cosmetic, but it means a
`char[12]` read needs an explicit length cap.

**Use kind 12 of the path table, not this.** It is always populated, it gives the real on-disk
path rather than a 12-byte stub, and it tells you whether the package exists.

If you do want this table, a plugin can populate it with one call: `FFX_LoadEventIdTable(0)` at
RVA `0x507F50`, `void __usercall (int a1@<edi>)`. The argument is only forwarded to `FFX_MemAlloc`
as an allocation tag, so passing garbage in `edi` is harmless. That is also exactly what
`EnableAutoTest` does.

### 1.3 What actually ships

**CONFIRMED by scanning `cdrom.fnd` and the VBF together.**

```
402   ids in eventid.bin / reachable id space
348   ids with a non-empty kind-12 path
330   ids whose .ebp is actually in FFX_Data.vbf      <- THE USABLE LIST
 18   ids with a path but no shipped file             <- WILL HANG, see 2.5
 54   ids with no path at all                        <- WILL HANG, see 2.5
397   .ebp files in the archive in total
```

So **67 shipped `.ebp` files are not reachable by any id**. They are dev and test packages
(`te/test22`, which is the one that uses the save-transplant script opcode, is one of them: id 25
`test22` has no path). The cheat UI cannot reach them without faking a path table entry.

The 330 loadable ids are not contiguous. The blocks are:

```
0-2, 16-23, 41-65, 67-100, 102-110, 112-113, 115-185, 187-215, 217-227, 230,
234-245, 247-250, 252-261, 263-277, 279-280, 282-299, 301-303, 305-327,
329-341, 347-349, 351-352, 354-356, 359, 361-368, 370-372, 374-377, 380-392
```

The 18 that have a path but no file, i.e. the ones that look valid and are not:

```
186 bltz0100   216 bvyt1400   228 lchb0804   229 lchb0805   231 lchb0807
232 lchb0808   233 lchb0809   262 hiku1700   300 lchb1900   304 hiku1200
342 kami0900   350 hiku1800   353 bltz0003   357 bltz0007   358 bltz0008
360 bvyt1500   369 znkd1100   373 ikai0700
```

### 1.4 The display label

The path is `host0:/ffx/master/jppc/event/obj/<xx>/<name>/<name>.ebp`. The game's own label is the
middle part: `FFX_Ev_LoadEventPackage` does `strstr(path, "/event/obj/") + 11`, chops at the last
`/`, and stores the result in `g_ffxCurrentEventName` RVA `0x1FCBC60`. So the label is
**`<xx>/<name>`**, e.g. `bl/bltz0000`, `ka/kami0400`, `na/nagi0000`. That is the string the brief
already saw in `EvCurrentEventName`, and it is produced by exactly this path chop.

**There are no human-readable English map names in this list.** The names are the development
short codes (`mihn0400`, `bsil0100`, `luca0100`). The two-letter prefix is the area group, which is
the useful grouping axis for a filter box: `bs` Besaid, `ki` Kilika, `lu` Luca, `mi` Mihen,
`ka` Thunder Plains (kaminari), `gu` Guadosalam, `ma` Macalania, `bv` Bevelle, `zn` Zanarkand,
`mc` Mt Gagazet / Calm Lands cluster, `na` Calm Lands (nagi), `hi` airship (hikuusen), `bl`
Blitzball, `sc` menu scenes, `te`/`ev`/`st`/`ca` dev.

There IS a separate localised map-name string set, used by the in-game save screen. The save block
carries a map name id at `+0xD6` and `FFX_SaveData_SetMapNameIdFromGroundDic 0x470F00` writes it.
That is a different id space and I did not map it in this pass. **UNSETTLED.** If a reader wants
"Mi'ihen Highroad" rather than `mi/mihn0400` in the UI, that function is where to start.

### 1.5 Building the list at runtime, in process

**Preferred, and it needs no baked data.** Pseudocode, each step matching a verified call:

```c
typedef int (__cdecl *AssetFn)(int, int, int, int);
AssetFn* loader = ((AssetFn* (__cdecl*)(void))Rva(FFX_GetAssetLoader))();

loader[4](12, 0, 0, 0);                       // select kind 12, sets loader[8] = 562
for (int id = 0; id < 402; ++id)
{
    const char* p = (const char*)loader[22](18 * id, 0, 0, 0);
    if (!p || !*p)
        continue;                             // no path: skip, this id would hang the game
    // p points at g_ffxResolvedAssetPathBuf, a SHARED 255-byte buffer. Copy it now.
    char label[64];
    const char* s = strstr(p, "/event/obj/");
    ...                                        // s + 11, chop at the last '/'
}
```

Two gotchas, both CONFIRMED in the code:

1. Slot 22 returns `g_ffxResolvedAssetPathBuf` RVA `0x1685C30`, a single 255-byte static. Copy out
   of it before the next call.
2. Slot 22 applies the three debug package swaps (`nagi0000 -> dbg_nagi0000`,
   `kami0400 -> 200thunder_kami0400`, `nagi0700 -> full_nagi0700`). If any of those process-local
   flags is set the label changes. Harmless for a label, but see `addresses/Minigames.h` for why
   those flags matter in co-op.

**"Does the file exist" is a separate question from "is there a path".** 18 ids have a path and no
file. Slot 5, `0x6428A0`, answers it properly: it opens the resolved path and returns the rounded
size, or 0. One file open per id, 402 of them, so do it once when the panel is first opened and
cache it. A plugin that would rather not touch the file system can bake the 18-id deny list from
section 1.3.

---

## 2. HOW TO WARP

### 2.1 The primitive

`FFX_Map_RequestChange 0x48EA60`, `void __cdecl (int mapId, char entryPoint)`. **CONFIRMED,
five stores and a flag, no gate of any kind:**

```c
if (mapId == g_ffxQuitPseudoMapId)        // the dword at 0x433480, a constant 399
{
    g_ffxLeaveFieldRequest = 1;           // the dword at 0xF27108
    return;                               // NOT a map load
}
if (mapId == 23) { sub_645310(); nullsub_62(); }   // the title map, extra teardown
if (mapId < 0)                            // negative means "go to the checkpoint"
{
    entryPoint = save.word[0xB8];
    mapId      = save.word[0xBA];
    save.word[0xB8] = 0;                  // the checkpoint is consumed
}
save.byte[0x0D] = save.byte[0x0C];        // previous entry point
save.word[0x02] = save.word[0x00];        // previous map id
save.word[0x00] = mapId;
save.byte[0x0C] = entryPoint;
if (mapId < 0) { save.word[0x02] = save.word[0xC2]; save.byte[0x0D] = save.byte[0xCE]; }
g_ffxMapChangePending = 1;                // 0xF3084C
```

Nothing loads here. One simulation step later `FFX_Atel_StepOnce 0x48D3D0`, running inside
`FFX_MainStep`, sees the flag, counts down `MapChangeDelayFrames`, and calls
`maybe_FFX_Scene_Init 0x48E070`, which does the real work on the simulation thread at a step
boundary. **So this is safe to call from any frame hook.** It is a request, not an action.

### 2.2 The game's own full warp, and how to use it

`FFX_Map_WarpTo 0x46FEC0`, `int __cdecl (int mapId, char entryPoint)`. **CONFIRMED:**

```c
if (!(atelCtx->byte0 & 0x80) && !g_ffxDebugMode)   // THE GATE
    return 0;
FFX_Atel_UnbindPlayerChr();
FFX_Map_RequestChange(mapId, entryPoint);
if (dword_1327070) dword_1327070();                // NULL in retail
FFX_Btl_CancelPendingBattle();
atelCtx->byte0  &= ~0x80;                          // the gate bit is CONSUMED
atelCtx->dw210  |=  2;
g_ffxScreenFadeRate = -0.06666667f;                // 0x477450, one float store
FFX_Map_SetTransitionFrames(save.word[0x02] == 265 ? 30 : 0);
```

The gate bit 0x80 on byte 0 of the selected ATEL context has **exactly two setters in the whole
binary** (CONFIRMED by walking all 100 functions that reference `g_ffxAtelCtx` and all 77 that
reference `g_ffxAtelCtxArray`, and by a byte scan for `or ..., 80h`):

* `FFX_Map_ArmWarpGate 0x4729C0` - `*(BYTE*)atelCtx |= 0x80; *(DWORD*)(atelCtx+0x210) |= 2;`
  Three instructions, no gate, no side effects. Its one caller is
  `AutoTestManager::execCommand` on the `JumpMap` path.
* `FFX_Return_To_Title 0x46DA10` - sets the bit on **context 0 specifically**, then warps to
  `(23, 0)`.

So the game itself arms the bit immediately before warping. **A plugin should do the same:**

```c
FFX_Map_ArmWarpGate();                 // 0x4729C0
FFX_Map_WarpTo(mapId, entryPoint);     // 0x46FEC0
```

This is the correct correction to the `DO NOT CALL THIS` note currently on `Rva::MapWarpTo` in
`addresses/WorldState.h`. The gate is not an obstacle, it is a one-line precondition, and the
engine's own warp path satisfies it the same way. The note was right that a *second* call without
re-arming is silently refused.

### 2.3 The higher level "go to this place" the brief asked for

**CONFIRMED. It is `FFX_Map_WarpToWithSavedFade 0x46FF40`,
`int __cdecl (int mapId, char entryPoint, int useSavedFade)`.** Same body as `FFX_Map_WarpTo`
plus: when `useSavedFade` is non-zero the transition frame count comes from `save.byte[0x12]` and
the fade style branches on `save.byte[0x11]`, rather than the fixed 0-or-30 rule.

**This is the function every door, save point and airship destination in the game goes through.**
Its only callers are four ATEL script syscalls, and nothing else in the binary calls it:

| syscall | call |
|---|---|
| Core 17 `0x4581C0` | `WarpToWithSavedFade(pop(), pop(), 1)` |
| Core 171 `0x458330` | `WarpToWithSavedFade(pop(), pop(), 1)` |
| Core 267 `0x458460` | `WarpToWithSavedFade(pop(), pop(), 0)` |
| Core 268 `0x458610` | `WarpToWithSavedFade(pop(), pop(), 0)` |

Each pops the entry point first and the map id second. So the script-visible signature is
`warp(mapId, entryPoint)` and the only difference between the four is the fade. It carries the
same gate bit, so it needs the same `FFX_Map_ArmWarpGate` first.

**Recommendation.** For a cheat panel, prefer `ArmWarpGate` + `WarpToWithSavedFade(id, ep, 0)`.
It is byte for byte the path a Zanarkand door takes, it unbinds the player CHR (which
`MapRequestChange` alone does not), it cancels a pending random encounter, and it sets the fade so
the transition does not snap. Use bare `MapRequestChange` only when you deliberately want no fade
and no CHR unbind, for instance when replaying a recorded transition in lockstep.

### 2.4 Preconditions, and what each game state does

What the engine itself checks: **nothing**. Neither `MapRequestChange` nor the gate has a state
test beyond the one bit. What a plugin should check anyway, with the reason:

| check | address | why |
|---|---|---|
| the id has a path and a file | section 1.5 | a missing package is an **infinite loop**, see 2.5 |
| `FFX_Atel_MesWinBlockingKind() == 0` | `0x46C8B0` | a dialogue box mid-warp leaves an orphaned window. The engine calls `FFX_Atel_MesWinResetAll 0x463B10` on load, so this is cosmetic rather than fatal |
| `FFX_Fmv_IsPlaying() == 0` | `0x241CF0` | `maybe_FFX_Scene_Init` calls `FFX_Fmv_AbortForSceneChange 0x76ECE0` for you, so a warp during an FMV **is** handled. INFERRED that it is clean, not observed |
| not in battle | `g_ffxBattlePendingKind` | see below |
| `g_ffxSceneLoaded != 0` | `0xF30858` | zero means a load is already in flight. A second request would be consumed by the same `StepOnce` and the first would be lost |
| `g_ffxMapChangePending == 0` | `0xF3084C` | same reason. **This is the one real serialisation check.** |

**Battle. CONFIRMED that a battle is not a map change.** `g_ffxMapChangePending` has four
references in the binary and the only writers are `MapRequestChange` and
`MapRequestResumeFromCheckpoint`, neither of which is on the battle path. So the field package and
the actor pool survive an encounter. The consequence for the cheat is that a warp requested during
a battle sits in the flag until the field resumes stepping, then fires. `FFX_Map_WarpTo` calls
`FFX_Btl_CancelPendingBattle`, which handles a *queued* encounter but not a live one.
**Recommendation: refuse the warp while a battle is live and say so in the UI.** INFERRED that
firing it mid-battle is harmful rather than merely late.

**Menu.** The in-game menu runs ATEL context 6 through `AtelStepMenuContext`, and the field
contexts 0..5 keep stepping, so `StepOnce` still consumes the flag. The map will load under the
open menu. INFERRED. Close the menu first.

**FMV.** The simulation runs through a movie (see `reversing/CUTSCENE.md`, which corrected the
earlier claim that it does not), so the flag is consumed normally and `Scene_Init` aborts the
movie. INFERRED to be clean.

### 2.5 The hard hang, and it is the most important sentence in this document

`FFX_Ev_LoadEventPackage 0x472EF0` checks the package magic like this, **CONFIRMED in the
disassembly:**

```c
if (*(BYTE*)image != 'E' || *(BYTE*)(image + 1) != 'V')
    while (1) ;                 // no break, no printf, no return
```

A map id with no shipped `.ebp` allocates a buffer, reads nothing into it, reads `*image` out of
fresh memory and spins forever on the simulation thread. **There is no recovery.** This is why
section 1.3's 330-id list is not a nicety: it is the only thing between the cheat panel and a hard
lock. Gate every warp on it.

### 2.6 Two special ids

* **399** equals `g_ffxQuitPseudoMapId` (the constant at `0x433480`, value `0x18F`). Passing it
  means "leave the field" and only raises `0xF27108`. It is not a map. **Its data name is
  `scene7`, so event 399 is unreachable through `FFX_Map_RequestChange`: the reserved id and a
  real package id collide.** Harmless in practice, because `scene7` is one of the seven boot/menu
  packages and has no path in the path table anyway, so it was never loadable from a warp. Worth
  knowing because the kit refuses 399 on purpose and a reader may wonder which event got lost.
* **23** is the title screen in retail, and `FFX_Return_To_Title 0x46DA10` warps to `(23, 0)`
  after 20 teardown calls. Its data name is `test20` and its entry map number is 690. Do not put
  it in a plain "warp here" list without a confirmation, because `Scene_Init(23)` disposes the
  whole CHR cache.
* **393..399** are the seven menu/boot packages, and they are the only ids whose path is **not**
  in the path table: `FFX_Ev_LoadEventPackage` routes them through `sub_67AAC0 0x27AAC0`, which
  indexes a 7-entry array of hardcoded strings at VA `0xC334B8` offset by `dword_C33468` = 393.
  `maybe_FFX_IsTitleOrBootMode 0x27AB50` is `(id - 393) <= 6`. Ids 400 and 401 (`scene8`,
  `scene9`) are outside that window and have no path, so they hang.

---

## 3. SPAWN COORDINATES

**CONFIRMED, and the answer is that the second argument to every warp function is the spawn
index.** It is an index into a 32-byte doorway record array that lives inside the event package.

`FFX_Atel_GetEntryRecord 0x46BFE0` and `FFX_Atel_GetEntryRecordByIndex 0x46C020`:

```c
atel = *(void**)(holder + 4);           // the loaded ATEL block, == Rva::EvPackageBase + 0x40
base = *(u32*)(atel + 4);               // byte offset of the record array, relative to atel
end  = *(u32*)(atel + 8);               // one past it
off  = base + 32 * entryPoint;          // entryPoint < 0 means "the save block's current one"
if (off >= end) off = base;             // OUT OF RANGE SILENTLY CLAMPS TO RECORD 0
return (char*)atel + off;               // or &unk_1325B50, an all-zero dummy, when base == 0
```

So **record count = `(atel[8] - atel[4]) / 32`**, and an out of range index is safe: you get
record 0.

### 3.1 The record, 32 bytes

| off | type | meaning | reader |
|---|---|---|---|
| +0x00 | s16 | destination map (background/walkmesh) number | `FFX_Atel_GetEntryMapNo 0x46BD90` |
| +0x06 | s16 | arrival script entry id | `FFX_Atel_StartEntryArrivalScript 0x46ED80`, Core 201 |
| +0x08 | f32 | facing yaw, radians | Core 131 / Core 194 |
| +0x0C | f32 | spawn X | Core 128 / Core 191 |
| +0x10 | f32 | spawn Y | Core 129 / Core 192 |
| +0x14 | f32 | spawn Z | Core 130 / Core 193 |
| +0x18 | 8 bytes | zero in every record I dumped | - |

The IDB comment on `GetEntryRecord` guessed +0x08 was "probably the facing yaw, NOT CONFIRMED".
**Now confirmed from the data:** across the packages I dumped, +0x08 is always within about
`-pi..2pi` and lands on exact quarter turns often (`1.571` = pi/2, `4.712` = 3pi/2, `0.0`). A
positional float would not look like that. Real examples:

```
ka/kami0400  id 256  mapNo 304  3 doorways
  ep 0  yaw  1.571  X  -61.576  Y  27.234  Z   -4.443   arrival script 6
  ep 1  yaw  4.712  X  -46.527  Y  23.040  Z  114.204   arrival script 6
  ep 2  yaw  0.000  X  -78.748  Y  26.273  Z   22.653   arrival script 6
na/nagi0000  id 223  mapNo 425  5 doorways
  ep 0  yaw  1.620  X  530.190  Y -81.679  Z -1885.134  arrival script 6
  ep 4  yaw  2.194  X -652.653  Y  36.842  Z   -62.134  arrival script 13
```

Doorway counts across the 330 loadable packages: 107 have 1, 91 have 2, 57 have 3, 31 have 4,
22 have 5, and 21 have 6 to 9. Maximum is 9. So the UI is a flat map list plus a small spin box.

### 3.2 Who applies the coordinates - and it is not C++

**CONFIRMED and this is the part that catches people.** Nothing in the C++ copies the record XYZ
into the player. The engine only exposes the record to the ATEL script VM through the Core getters
above. `FFX_Atel_StartEntryArrivalScript 0x46ED80` runs at the very end of map boot (from
`maybe_FFX_Map_SetupChars 0x475510`, after the actor pool is built) and launches the ATEL thread
named by the record's +0x06. **That script** reads X/Y/Z through Core 128/129/130 and sets the
position with Core 19 / Core 294, which land on `FFX_Atel_SetActorPos`.

Two consequences:

* The spawn is correct for free as long as the record's arrival script id is non-zero.
  `StartEntryArrivalScript` returns 0 and does nothing when `*(u16*)(atel + 24)` is `0xFFFF` or
  when the record's +0x06 is 0. In that case the player lands wherever the actor definition put
  them, which is usually fine but is not the doorway.
* A plugin that wants an arbitrary position should not rewrite the record. Let the map boot, then
  call `Rva::AtelSetActorPos 0x470B20` on the player actor. That is what scripts do all day.
  Beware `Rva::AtelSetActorPosXZ 0x470970`, which writes Y = 0, and +Y is down in FFX.

### 3.3 Reconciling with `ReadWorldLocation`'s `mapId`

**CONFIRMED.** The save block holds both numbers, and the kit's `mapId` is the event/package id:

| save offset | meaning | getter |
|---|---|---|
| `+0x00` word | **the event/package id, 0..401.** This is what `MapRequestChange` takes | `FFX_SaveData_GetMapId 0x48D660` |
| `+0x0C` byte | **the entry point index** | `FFX_SaveData_GetEntryPoint 0x48D670` |
| `+0x04` word | **the entry MAP NUMBER**, i.e. the background/walkmesh container | `FFX_SaveData_GetSceneId 0x48D690` |

The `+0x04` word is written by the load path, not chosen by the caller:
`FFX_Ev_LoadEventPackage` calls `FFX_Atel_GetEntryMapNo(atel, 0)` and the result goes to
`FFX_SaveData_SetSceneAndSub 0x48EAF0` and to `sub_5085D0`, which loads the background. So the
block's "scene id" is derived from the package, and the only two things a warp chooses are
`(packageId, entryPoint)`.

In the shipped data, record 0's map number spans 1..692 with 280 distinct values across the 330
packages, which matches `reversing/MAP_FORMAT.md`'s 491 `mapout` containers and 469 walkmesh
sections. **There are more background maps than there are reachable packages**, because several
packages share a background and some backgrounds are only reached through a non-zero entry point.
`FFX_Atel_GetEntryMapNo` can also be overridden per language region by a table at
`RES+0x3C -> T+0x0C`, which is how localised backgrounds work.

---

## 4. THE EVENT LIST

**It is the map list. Section 1 is the answer to this deliverable as well.** 330 rows, id 0..392
with holes, label `<xx>/<name>`.

The brief expected two id spaces because `EvCurrentEventName 0x1FCBC60` holds `bl/bltz0000` and
`EvCurrentEventId 0xEFBC40` holds a number. They are the same thing:
`FFX_Ev_LoadEventPackage 0x472EF0` opens with `g_ffxCurrentEventId = eventId` and then builds
`g_ffxCurrentEventName` from the path of that same id. **CONFIRMED by two independent readers of
that global:**

* `FFX_Map_GetCurrentMapName 0x507E70` matches it against the eventid table's `row+12`, which is
  why it reads as a map id.
* `FFX_Ev_LoadEventPackage 0x472EF0` writes it from its event id argument, and that argument comes
  from `maybe_FFX_Scene_Init 0x48E070`, which took it from the save block's live map word `+0x00`.

**So `addresses/Minigames.h`'s name `EvCurrentEventId` is the right label** and nothing needs
changing there. A map and an event are one id in this game, which is the single fact that makes
both readings look correct. Noted here only because `reversing/BLITZBALL.md` calls it an event id
without saying it is also the map id.

### 4.1 The sub-list: individual scripts inside a package

A package is not one event, it is a bag of scripts. Per package, measured over the 330:

```
2 .. 150     actors per package
6 .. 2164    script entry points per package
111,389      script entry points across all 330 packages
```

The actor definition table and each actor's entry-point table are both inside the `.ebp`, and
`tools/ebp.py` already parses both (`EbpFile.actors`, `a.n_entries`, `a.entries`). So a two-level
"run script N on actor M" picker is buildable today from the archive, and at runtime the same
tables are at `Rva::EvPackageBase + 0x40` plus the offsets `ebp.py` documents.

**One identification this pass adds.** `ebp.py` lists the ATEL block's `u16` at `+0x18` as `[unk]`.
It is **the map/system actor id**, the actor that owns the map-level scripts.
`FFX_Atel_StartEntryArrivalScript 0x46ED80` reads `*(u16*)(atel + 24)`, treats `0xFFFF` as "none",
and uses it as the target actor when it launches the arrival script. CONFIRMED against the data: it
is `0xFFFF` in 3 of the 330 packages and a valid actor index (`< n_actors`) in the other 327.

---

## 5. HOW TO START AN ARBITRARY EVENT

### 5.1 Do not call `FFX_Ev_LoadEventPackage` yourself

`FFX_Ev_LoadEventPackage 0x472EF0`, `int __cdecl (int eventId)`. One argument. What it does, in
order, all CONFIRMED by reading it:

1. About 20 resets: `FFX_Atel_MesWinResetAll`, `FFX_Btl_CancelPendingBattle`, sound, text, camera.
2. Loads the `.ebp` into a fresh `FFX_MemAlloc` rounded up to 2 KB (`sub_48D2C0`, which asks
   `assetLoader[5]` for the size and `assetLoader[0]` for the read).
3. **Checks the magic and `while(1)` on a mismatch.** See 2.5.
4. Frees the previous `g_ffxEvPackageBase 0x1FCBD70` and installs the new image.
5. `FFX_Atel_CalcActorPoolSize 0x46A290` on the ATEL block, frees the previous
   `g_ffxAtelActorPool 0x1FCBD78`, allocates the new one (110,064 bytes for `bltz0000`).
6. Loads the localised message table (`FFX_LoadLocalizedBin 0x48CAC0`) and font
   (`FFX_LoadLocalizedFtc 0x48C860`), freeing the previous ones.
7. `FFX_Atel_GetEntryMapNo(atel, 0)`, then `FFX_SaveData_SetSceneAndSub` and `sub_5085D0` to load
   the background.
8. `maybe_FFX_Map_SetupChars 0x475510`, which builds the actor pool and ends by launching the
   arrival script.
9. BGM, SE, the resource list and the sound groups.

**It expects to be called from inside `maybe_FFX_Scene_Init 0x48E070` and nowhere else, and it is:
it has exactly one caller.** Scene_Init does the things LoadEventPackage does not. It unloads the
previous map (`maybe_FFX_Map_Unload`), re-inits the camera, aborts an FMV, snapshots the location
history into the save block, and afterwards sets `g_ffxSceneLoaded = 1`, clears the pending flag
and requests the map's sync data. Calling LoadEventPackage on its own leaves the old map's CHRs
alive against a brand new actor pool.

### 5.2 The correct sequence, which is the warp

```c
// 1. Validate. A missing package is an unrecoverable hang, see 2.5.
if (!EventPackageExists(id))
    return;

// 2. Serialise against a load already in flight.
if (*(int*)Va(Rva::MapChangePending) != 0 || *(int*)Va(Rva::SceneLoaded) == 0)
    return;

// 3. Arm the gate, exactly as the engine's own warp path does.
((void(__cdecl*)(void))Va(Rva::MapArmWarpGate))();                        // 0x4729C0

// 4. Request it. The engine loads it one simulation step later, on the sim thread.
((int(__cdecl*)(int,char,int))Va(Rva::MapWarpToWithSavedFade))(id, ep, 0); // 0x46FF40
```

That is the whole event launcher. CONFIRMED to be the same call the four Core warp syscalls make,
differing only in the third argument.

### 5.3 Running one script inside the package that is already loaded

**This is the other meaning of "start an event", and it is one call.**
`FFX_Atel_StartThreadByChannel 0x46EBA0`:

```c
int __cdecl FFX_Atel_StartThreadByChannel(int callerActorId,    // 0xFFFF for "nobody"
                                          int targetActorId,
                                          int kind,             // 0 normal, 3 appends to a second list
                                          int channel,          // 0..8, nine per actor
                                          int scriptEntryIndex);
```

It builds a five-dword record and hands it to `FFX_Atel_StartThreadIfChannelFree 0x46EBE0`, which
walks the target actor's thread list at `actor+128` and **refuses when a thread on the same channel
is still unfinished**, returning a quiet 0. `FFX_Atel_CreateThread 0x46EA00` then takes a record
off the actor's free list at `actor+136` and returns 0 when that is empty. So a spammed launch
cannot stack scripts and a failure is never a fault. CONFIRMED from the bodies.

The record is also the one `FFX_Atel_StartEntryArrivalScript` builds, which is the worked example:
`{0xFFFF, mapSystemActorId, 0, 1, scriptEntryId}`. A `targetActorId` of `0xFFFF` makes both
`CreateThread` and `StartThreadIfChannelFree` return 0 immediately, so that is the safe no-op.

**HAZARD, CONFIRMED.** `FFX_Atel_CreateThread` dereferences `FFX_Atel_GetActor(targetActorId)`, and
`FFX_Atel_GetActor` **clamps** an out of range id rather than rejecting it (see
`addresses/Atel.h`). So an actor id past the package's count quietly operates on some other actor.
Bounds-check against the live actor count at `ctx+12` yourself. Entry indices are not bounds
checked anywhere on this path either, and the entry table length is `actorDef+0x08`.

### 5.4 Cleanup, and what it costs

**Nothing special.** A warp to another map tears everything down through `Scene_Init`:
LoadEventPackage frees the old package image, the old actor pool, the old localised bin and the old
localised ftc, and `FFX_Atel_MesWinResetAll` clears both message window buffers. So the way back to
normal play is **another warp**, to the id the player came from with the entry point they came
from. Both are in the save block already: `save.word[0x02]` is the previous map id and
`save.byte[0x0D]` is the previous entry point, and `FFX_Map_RequestChange` maintains both for you.
**Snapshot them before the cheat warp and warp back with them.**

A negative map id does the same from the checkpoint rather than from the previous map, and
`FFX_Map_RequestResumeFromCheckpoint 0x48DD60` is the written-out version. Prefer the explicit
previous-map pair, because the checkpoint is wherever the player last saved and that can be hours
behind.

### 5.5 "What if the event expects a map that is not loaded"

**It cannot happen, and that is a property of the format rather than luck.** The package names its
own background map in entry record 0, and `FFX_Ev_LoadEventPackage` loads that background itself in
step 7, from the package it just read. There is no way to express "event A on map B" through the
warp API, because the warp API has no map-number argument. The only place the pairing can differ is
the per-language override at `RES+0x3C -> T+0x0C` inside the same package.

The failure mode that does exist is the **missing walkmesh**. `reversing/MAP_FORMAT.md` records 469
walkmesh sections across 491 containers, with 103 containers that are 64-byte stubs. A package
whose entry map number lands on a stub boots with no collision. I did not cross-join the two tables
in this pass. **UNSETTLED**, and worth doing before shipping: join record 0's map number per id
against `tools/mapfile.py`'s section-2 presence and deny the empties.

---

## 6. DIALOGUE

### 6.1 The message list is PER PACKAGE, so the UI is a two-level picker

**CONFIRMED.** `FFX_Atel_GetMessageText 0x46BF30` reads the table pointer from
`*(u32*)(atelCtx + 0x34)` and falls back to `g_ffxEvBuiltinMsgTable` when a given record's offset
is 0. Both are set by `FFX_Ev_LoadEventPackage` from the package that is currently loaded. **There
is no global message table.**

Table layout, 8 bytes per message id, CONFIRMED from the two getters:

```
+0  u16  string offset, relative to the table base     FFX_Atel_GetMessageText
+2  u16  attribute                                     FFX_Atel_GetMessageAttr 0x46BF10
+4  u16  duplicate offset
+6  u16  duplicate attribute
```

**The first u16 of the table is the byte size of the record array, so `count = table[0] / 8`.**
Message id 0 therefore reads the count as a string offset, which is why ids are 1-based in
practice. Across the archive the two pairs are identical in 40,125 of 40,376 records.

Counts, measured over the 330 loadable packages' built-in US tables:

```
15,413   total messages
   365   most in one package, lc/lchb1200, then na/nagi0000 at 361
     0   in 46 packages
```

**Neither getter bounds-checks the id.** `GetMessageText` reads `*(u16*)(table + 8*id)` with no
comparison against `table[0]`. An out of range id returns a pointer into whatever follows and the
renderer then walks it looking for a terminator. **Clamp to `table[0]/8` yourself.**

Three sources exist per package and the engine picks between them:

| source | where | note |
|---|---|---|
| the shipped localised table | `ffx_ps2/ffx/master/new_<lang>pc/event/obj_ps3/<xx>/<name>/<name>.bin` | what `FFX_LoadLocalizedBin 0x48CAC0` loads, and what the game actually shows |
| the package's built-in US table | `.ebp` container section 4 | byte-identical to the shipped one in 81 of 294 packages, an older revision in 213 |
| the package's built-in JP table | `.ebp` container section 1 | the fallback when there is no localised file |

`<lang>` is one of `jp us fr sp de it kr ch`, chosen by `FFX_GetLanguage`. **For UI labels, read the
`new_uspc` file rather than the `.ebp` section**, because 213 of 294 differ and the shipped one is
what the player will see. `tools/ebp.py`'s `messages()` and `decode_text()` parse both, and the
record layout is identical, so the same reader works on either.

### 6.2 The text codec is only partly mapped

`tools/ebp.py` decodes `0x3A` space, `0x3B` `.`, `0x41` `'`, `0x4F` `?`, `0x50..0x69` `A..Z` and
`0x70..0x89` `a..z`, and prints everything else as `{XX}`. That is already readable enough for a
UI label. A sample, straight out of `bs/bsil0100` message 2:

```
{03}I'll be good while you're{03}gone. I promise{46} ya?
```

`{03}` is a line break: it appears where the shipped line wraps and never mid-word, in every record
I looked at. INFERRED, not proved from the renderer. `{46}` sits where a comma belongs in every one
of the dozen records I checked, so it is very likely a comma, also INFERRED. `{13}{31}` and `{19}a`
are **name and term macros** (`{13}{31}` renders as the stored name for Yuna, `{19}a` and `{19}d`
as Sin and Sinspawn), which is why a raw decode can never be complete without the macro table.
**UNSETTLED**, and the place to finish it is the per-package `.ftc` glyph table plus
`FFX_MesWin_LayoutMessageAndFindChoices 0x4B9DF0`, the function that walks these codes.

For a filter box, strip the `{XX}` runs and match on the letters. That works today.

### 6.3 Showing an arbitrary message box with no event running

**Yes, and here is the exact argument plumbing. The plumbing is CONFIRMED by reading. The
end-to-end behaviour is INFERRED, because I did not run it.**

The three ATEL ops do all of it, and each takes its arguments off the script operand stack rather
than off the C stack:

| op | handler | pops, in this order |
|---|---|---|
| core:100 set message | `FFX_AtelOp_MesWinSetMessage 0x457870` | messageId, then windowIndex |
| core:106 show | `FFX_AtelOp_MesWinShow 0x458B50` | attribute, then windowIndex |
| core:107 close | `FFX_AtelOp_MesWinClose 0x459060` | windowIndex |

Each is `int __cdecl (int a1, int a2, int* stack)`, and `a1` and `a2` are only forwarded to
`FFX_Atel_PopInt 0x46DF00`, which ignores them both. PopInt's view of the stack block, CONFIRMED
from its body and matching the real actor layout (`actor+0xC4` sp, `actor+0xC8` values,
`actor+0x118` tags):

```c
struct AtelOperandStack          // the shape at actor + 0xC4
{
    int  sp;                     // +0x00  how many values are pushed
    int  value[19];              // +0x04  value[i] is stack slot i
    char tag[20];                // +0x54  tag[i]: 1 = int, 2 = float
};
```

PopInt reads slot `sp-1`, converts from float when the tag is not 1, decrements `sp`, and on
underflow returns 0 and zeroes `g_ffxAtelActorClampCount` (a free canary, see `addresses/Atel.h`).
So:

```c
AtelOperandStack s = {};
s.sp = 2;
s.value[0] = windowIndex;  s.tag[0] = 1;      // pushed first, popped second
s.value[1] = messageId;    s.tag[1] = 1;      // pushed last,  popped first
MesWinSetMessage(0, 0, (int*)&s);

s.sp = 2;
s.value[0] = windowIndex;  s.tag[0] = 1;
s.value[1] = attribute;    s.tag[1] = 1;
MesWinShow(0, 0, (int*)&s);
```

Preconditions, all CONFIRMED:

* A package must be loaded, because GetMessageText reads `atelCtx+0x34`. Zero there returns a
  pointer to `unk_1325B74`, a static, which is safe but shows nothing.
* `windowIndex` is clamped to 0..7 by `FFX_Atel_GetMessageWindow 0x46BE90` (already declared in
  `addresses/Cutscene.h`), so it cannot fault. Eight windows, two buffers, selected by
  `AtelMesWinBufSel`.
* MesWinShow runs the attribute through `sub_471210`, which clears the choice bit when the message
  declares no options, so a wrong attribute degrades to a plain box rather than breaking.
* MesWinShow calls `FFX_MesWin_SetInputFocus`, which takes the single global focus. If a real
  dialogue is up, this steals it. Check `MesWinBlockingKind 0x46C8B0` is 0 first.
* `MesWinStepAll 0x4AB910` runs from the `FFX_MainStep` sub-step loop and advances the box, so once
  shown it behaves exactly like a script's box, confirm press included. Nothing extra is needed.

To take it down, `FFX_Atel_MesWinRequestClose 0x4640F0` takes the window index as a normal C
argument, so the close needs no fake stack at all.

---

## 7. CUTSCENES AND FMVS, WHICH ARE THREE DIFFERENT THINGS

The user's "event, cutscene or dialogue" is four mechanisms. `reversing/CUTSCENE.md` is the long
version. The short version, for a cheat panel:

| the thing | what it really is | launcher | list source |
|---|---|---|---|
| a scripted event, or a real time cutscene | ATEL bytecode in a package, stepped once per simulation step | warp to the package id (5.2), or run one script entry in the loaded package (5.3) | section 1, then the package's own actor and entry tables |
| a voiced line | the same VM, but `FFX_StepPacing` switches to a timing track off the millisecond clock | the same. It arms itself when the script hits a call-actor-script opcode | `SyncList.txt`, see `addresses/Cutscene.h` |
| a dialogue box | its own module in the same sub-step loop, driven by three ATEL ops | section 6.3 | the loaded package's message table |
| an FMV | a Phyre WebM player on its own decode thread | `FFX_Fmv_StartVideo`, 7.1 | `FFX_VideoList.txt`, 7.2 |

**There is no separate real time cutscene mechanism.** An in-engine cutscene is an ATEL script
driving the class 3 event camera (`CameEventHandles`, see `addresses/Cutscene.h`). So it has the
same launcher as an event, which is why this document treats them as one list.

### 7.1 The FMV launcher

`FFX_Fmv_StartVideo 0x241690`, `BOOL __cdecl (int videoId, int flag)`. **CONFIRMED.** It is
`FFX_Fmv_CreatePlayer(FFX_Fmv_GetManager(), videoId, 0, flag != 0)`, returning whether a player was
made. 0 means the video id is not in the list, or the allocation failed.

The script path is `ATEL Movie:10 start 0x36EB80`, and copying it is the right thing to do:

```c
g_ffxFmvCurrentVideoId = videoId;                       // 0x840E28
if (FFX_Fmv_StartVideo(videoId, subtitleFlag & 0x80))   // 0x241690
{
    FFX_Fmv_SetFullscreenMode(0);                       // 0x2427C0
    g_ffxFmvScriptRunning  = 1;                         // 0xD2A008
    g_ffxFmvMovieOpActive  = 1;                         // 0x840E2C
    g_ffxFmvScriptBlocking = 1;                         // 0xD2A00C
}
```

`StartVideo` hardcodes CreatePlayer's HD argument to 0, so it plays at 1280x720. A plugin that
wants 1920x1080 has to call `FFX_Fmv_CreatePlayer 0x2D9C80`,
`__thiscall (mgr, int videoId, char hd, char flag)`, itself.

**Set those three script globals only if a script is actually parked at a movie wait.** A cheat that
just plays a video outside any script should leave them alone, or `FFX_Fmv_AbortForSceneChange
0x36ECE0` and the FMV barrier in `plugins/pilgrimage/world/FmvSync.h` will be reading state that
does not match reality.

### 7.2 The FMV list, and it is enumerable at runtime

**CONFIRMED.** The list is a plain text file, one shipped per region:

```
ffx_data/gamedata/ps3data/video/us/ffx_videolist.txt      1805 bytes, 62 entries
ffx_data/gamedata/ps3data/video/jp/ffx_videolist.txt      1805 bytes
ffx_data/gamedata/ps3data/video/asia/ffx_videolist.txt    1809 bytes
ffx_data/gamedata/ps3data/video/cn/ffx_videolist.txt      1721 bytes
```

Format, one line per video: `<videoId>:,<primaryFile>,<altFile>`. The parser is
`sub_6D82F0 0x2D82F0`, `__thiscall (mgr, char useAltColumn)`. It picks the region file from
`sub_6DB070`'s master-version word (0 JP, 1 and 2 US, 3 Asia), splits on `:` then on two commas,
prefixes `getDataRoot() + "/FFX_Data/GameData/" + "PS3Data" + "/Video/"`, and inserts
`{atoi(idText), fullPath}` into a `std::map<int, std::string>`.

**It runs at boot in retail with no flag:** `FFX_BootSequence -> sub_636430 -> sub_6D82F0(mgr, 0)`.
CONFIRMED, one call site, unconditional.

**The map lives at `FmvPlayerManager + 1768`**, and `sub_6D93E0 0x2D93E0`,
`__thiscall (mgr, int videoId, char* out128)`, is the lookup. So a plugin can walk the map
directly. The node shape, read out of `sub_6D93E0`, is a plain MSVC `std::map`:

```
+0x00  _Left      +0x04  _Parent    +0x08  _Right
+0x0C  _Color     +0x0D  _Isnil
+0x10  int         the key, i.e. the video id
+0x14  std::string the value, the full path. In place when the capacity at
                   +0x28 is below 16, a pointer otherwise.
```

`*(void**)(mgr + 1768)` is the sentinel node and the root is its `_Parent`. An in-order walk from
the root gives every id with its path, sorted, which is the UI list exactly. **So the FMV picker
needs no baked data either.** 62 rows. The label is the file basename, a three-letter mnemonic plus
a language suffix: `OPN_us`, `END_us`, `BTS_us`, `SIN_us`.

The ids as shipped in the US list, for a baked fallback:

```
  0 OPN   2 IWK   4 IYU   6 ILL   8 IKM  10 LVA  12 LVB  14 LVC  16 LVD  18 LVE
 20 SAK  22 SDF  24 LCI  26 BZP  28 IAR  30 SMA  32 MST  34 MSN  36 MVA  38 MTS
 40 OPK  42 IRK  44 PRO  46 FAR  48 SLS  50 SDA  52 FIN  54 FGO  56 FBY  58 MAR
 60 MIN  62 MKS  64 MFL  66 DAT  68 GGI  70 GGM  72 SNS  74 SWA  76 BSC  78 BGR
 80 BLF  82 BRF  84 OPP  86 BBB  88 OPL  90 SIN  92 THT  94 END  96 BTS  98 BMA
100 BMB 102 BTB 104 FAL 106 WithoutEvent_480 107 save22_480 109 2_480
114 1_480_new 115 3_480 116 4_480 118 syuin_us 121 SIR
```

Ids 106..121 are the low-resolution save-point and in-menu clips. `FFX_Fmv_CreatePlayer` also
carries a 16-id list at VA `0xB49B08`, `{2,4,6,18,24,32,38,42,44,46,50,60,62,70,72,84}`, which sets
`mgr+1869` to 0 for those ids and 1 for everything else. Purpose not identified. **UNSETTLED.**

The archive holds 99 `.webm` files under `ffx_data/gamedata/ps3data/video/`, more than the 62 the
list names, because most cutscenes ship a `_jp` and a `_us` variant and the list picks one per
region. `tools/vbf.py find ps3data/video` enumerates them.

---

## 8. THE AUTOTEST CONSOLE, AND WHY IT IS A DEAD END

An earlier note in `addresses/WorldState.h` called `FFX_AutoTest_JumpMap` "reachable in retail
today with stdin attached and no patching". **That is not true, for two independent reasons, and
the second one is fatal.** I have corrected the header comment.

**Reason one: the name table is empty.** Covered in 1.2. `FFX_AutoTest_JumpMap 0x508100`
substring-searches a table that nothing fills in a normal boot, so the search finds nothing, falls
through to the hardcoded `strstr("grid00", arg)` case, and returns without warping unless the typed
name is a substring of `grid00`.

That part is fixable, because the load works: `FFX_LoadEventIdTable 0x507F50` reads through
`FFX_Ch_ReadFileDev 0x43AE30 -> Sg_PcRead 0x43B1D0`, which sprintf's `"host0:%s"` and hands it to
`sub_62FA30 0x22FA30`. **And `sub_62FA30` is archive-aware**, CONFIRMED from its body: it strips
the leading 7 bytes (`host0:/`), prepends `/ffx_ps2/`, runs `FFX_fiosUnifyFilename` and then
`FFX_File_Exists` + `FFX_File_Open`. So `/ffx/proj/event/header/eventid.bin` resolves to
`/ffx_ps2/ffx/proj/event/header/eventid.bin`, which is in `FFX_Data.vbf` as
`ffx_ps2/ffx/proj/event/header/eventid.bin`, 6,741 bytes. The proof that those three calls read the
archive is that `FFX_LoadLocalizedBin` uses exactly them on `/ffx_ps2/ffx/master/new_uspc/...`
paths, which exist only inside the archive, and localised text works in retail. **So calling
`FFX_LoadEventIdTable(0)` from a plugin does populate the table.** The earlier reading that
`Sg_PcRead` can only reach loose files was wrong.

**Reason two, which is not fixable without patching: the command queue is never drained.**
CONFIRMED, and this is the sentence that settles the coordinator's question:

* `AutoTestManager::ctor 0x2BCA90` spawns the `"Read command"` stdin thread in retail with no
  debug flag, and `AutoTestManager::createSingleton 0x2BCC40` is called from
  `FFX_GraphicInitialize 0x241A85`, so **the singleton does exist and the thread does run**. No
  plugin action is needed to create it.
* `AutoTestManager::stdinThread 0x2BD1E0` reads a line and does one of two things. If the line
  starts with `EnableAutoTest` it calls `execCommand` **inline, on the reader thread**. Anything
  else is handed to `queueCommand 0x2BCCB0`, and only once `this+297` (the enabled flag) is set.
* The drain is the `__thiscall` at **`0x2BCFF0`**, which gates on `this+0x129` (the same enabled
  flag), calls `FFX_Map_GetCurrentMapName`, pops the 24-byte-element vector at `this+0x118` and
  calls `execCommand` per entry. **It has zero callers.** I scanned the whole `.text` for a
  `call`/`jmp rel32` landing on it and for the absolute address in every segment: nothing. The
  class vtable has one slot and it is the deleting destructor, so it is not a virtual either.
* The only thing that reaches the manager per frame is `sub_668930` calling `sub_6BD290` on
  `getSingleton()`, and **`sub_6BD290` is `xor eax,eax; retn 8`**, a stub.

So on the stdin route only `EnableAutoTest` ever executes. `JumpMap`, `SetBattle` and `Crash` go
into a vector that nothing reads.

**What the console is still good for.** `execCommand 0x2BCE00`,
`char __thiscall (this, char* command)`, works perfectly when called directly, and a plugin can
call it on `g_autoTestManager 0x8CCB00`. The command set, CONFIRMED from the body:

| command | what it does |
|---|---|
| `EnableAutoTest` | `FFX_LoadEventIdTable(this)` and sets `this+297 = 1`. The only one that works today |
| `DisableAutoTest` | clears `this+297` |
| `JumpMap <name>` | `FFX_Map_ArmWarpGate(); sub_2B6370(); sub_2B6730(name)` and the last of those calls `FFX_AutoTest_JumpMap`. Needs the table loaded |
| `SetBattle Enabled` / `Disabled` | writes `g_ffxBattleDisabled`, i.e. the **random-encounter toggle**. It is NOT a battle starter, so it is not useful for event launching |
| `Crash` | `*(int*)0 = 0` |

Every command except Enable and Disable is refused unless `this+297` is set.

**Recommendation: ignore all of it.** `ArmWarpGate` plus `WarpToWithSavedFade` is three
instructions and a call, it needs no table, no singleton, no string parsing, and no enabled flag.
The only reason to touch `execCommand` is to borrow `EnableAutoTest` as a one-liner that populates
the name table, and `FFX_LoadEventIdTable(0)` does that directly.

---

## 9. ADDRESS TABLE

RVAs. VA = RVA + 0x400000.

| RVA | kind | suggested name | signature or type | note |
|---|---|---|---|---|
| 0x0048EA60 | func | `MapRequestChange` | `void __cdecl (int mapId, char entryPoint)` | already in WorldState.h. THE primitive, no gate |
| 0x0046FEC0 | func | `MapWarpTo` | `int __cdecl (int mapId, char entryPoint)` | already in WorldState.h. Needs the gate bit |
| 0x0046FF40 | func | `MapWarpToWithSavedFade` | `int __cdecl (int mapId, char entryPoint, int useSavedFade)` | **the one to call.** What every door uses |
| 0x004729C0 | func | `MapArmWarpGate` | `void __cdecl (void)` | sets ctx byte0 bit 0x80 and ctx+0x210 bit 2. The warp precondition |
| 0x00477450 | func | `MapSetScreenFadeRate` | `void __cdecl (float rate)` | one float store. WarpTo passes -0.06666667 |
| 0x00F28310 | data | `MapScreenFadeRate` | float | what the above writes |
| 0x0048EB50 | func | `MapSetTransitionFrames` | `void __cdecl (int frames)` | already in WorldState.h |
| 0x0046DA10 | func | `MapReturnToTitle` | `void __cdecl (void)` | 20 teardown calls then `WarpTo(23, 0)`. Second setter of the gate bit |
| 0x0048E070 | func | `SceneInit` | `int __usercall (int saveData@<esi>, int mapId)` | the deferred consumer. Do not call, it is what `StepOnce` calls |
| 0x00F27108 | data | `MapLeaveFieldRequest` | int | raised by map id 399. `FFX_MainStep` consumes it |
| 0x0048D660 | func | `SaveDataGetMapId` | `int __cdecl (void)` | save word +0x00, the EVENT/PACKAGE id |
| 0x0048D670 | func | `SaveDataGetEntryPoint` | `int __cdecl (void)` | save byte +0x0C, the doorway index |
| 0x0048D690 | func | `SaveDataGetSceneId` | `int __cdecl (void)` | save word +0x04, the BACKGROUND MAP number |
| 0x0036D0D0 | func | `GetAssetLoader` | `void** __cdecl (void)` | returns `&g_ffxAssetLoader` |
| 0x01F10C40 | data | `AssetLoaderTable` | `void*[37]` | slot 4 select kind, 5 size, 22 path, 0 read, 8 kind base, 12 path table |
| 0x00642C00 | func | - | - | `AssetResolvePathWithDebugOverrides`, declared in addresses/Minigames.h |
| 0x00507F50 | func | `LoadEventIdTable` | `void __usercall (int tag@<edi>)` | populates the name table. Safe to call, reads the archive |
| 0x0021D5888 | data | `EventIdNameTable` | `row* ` | pointer. Row 16 bytes: `char name[12]`, `u32 id` |
| 0x01534EC8 | data | `EventIdNameCount` | int | -1 until loaded |
| 0x01534ECC | data | `EventIdTableLoaded` | int | the once-only guard |
| 0x00507E70 | func | `MapGetCurrentMapName` | `int __cdecl (char* out)` | reverse lookup for `EvCurrentEventId` |
| 0x00508100 | func | `AutoTestJumpMap` | `char* __cdecl (char* name)` | already in WorldState.h |
| 0x002BCE00 | func | `AutoTestExecCommand` | `char __thiscall (void* mgr, char* cmd)` | works when called directly |
| 0x002BCFF0 | func | `AutoTestDrainQueue` | `void __thiscall (void* mgr)` | **zero callers.** The queue is dead |
| 0x008CCB00 | data | `AutoTestManagerSingleton` | `void*` | created by `FFX_GraphicInitialize` |
| 0x00472EF0 | func | `EvLoadEventPackage` | `int __cdecl (int eventId)` | already in Minigames.h. `while(1)` on a bad magic |
| 0x00475510 | func | `MapSetupChars` | `int __cdecl (pool, atel, msg, x, y, z, poolSize)` | builds the actor pool, then the arrival script |
| 0x0046BFE0 | func | `AtelGetEntryRecord` | `void* __cdecl (int atelHolder)` | the save block's current doorway |
| 0x0046C020 | func | `AtelGetEntryRecordByIndex` | `void* __cdecl (int atelHolder, int index)` | negative index means "the current one" |
| 0x0046BD90 | func | `AtelGetEntryMapNo` | `int __cdecl (int atel, int index)` | s16 at record+0, or the language override |
| 0x0046ED80 | func | `AtelStartEntryArrivalScript` | `int __cdecl (int atel)` | what actually places the player |
| 0x0046A290 | func | `AtelCalcActorPoolSize` | `int __cdecl (void* atel)` | |
| 0x0046ADC0 | func | `AtelGetResFlags` | `int __cdecl (int atel)` | bit 0 picks which scene setter runs |
| 0x0046EBA0 | func | `AtelStartThreadByChannel` | `int __cdecl (int caller, int target, int kind, int channel, int entry)` | already in Atel.h. **the script launcher** |
| 0x0046DF00 | func | `AtelPopInt` | `int __cdecl (int unused, int* stack)` | lets a plugin drive any syscall handler |
| 0x004581C0 | func | `AtelSysCore017Warp` | `int __cdecl (int, int, int*)` | `WarpToWithSavedFade(pop, pop, 1)` |
| 0x00458330 | func | `AtelSysCore171Warp` | same | `(..., 1)` |
| 0x00458460 | func | `AtelSysCore267Warp` | same | `(..., 0)` |
| 0x00458610 | func | `AtelSysCore268Warp` | same | `(..., 0)` |
| 0x0048CAC0 | func | `LoadLocalizedBin` | `void* __cdecl (char* logicalName)` | the localised message table |
| 0x0048C860 | func | `LoadLocalizedFtc` | `void* __cdecl (char* logicalName)` | the localised font |
| 0x00241690 | func | `FmvStartVideo` | `BOOL __cdecl (int videoId, int flag)` | the FMV launcher |
| 0x002427C0 | func | `FmvSetFullscreenMode` | `int __cdecl (int mode)` | |
| 0x00840E28 | data | `FmvCurrentVideoId` | int | what Movie:10 records |
| 0x00840E2C | data | `FmvMovieOpActive` | int | one dword below the Movie lib table |
| 0x002D93E0 | func | `FmvLookupVideoPath` | `char __thiscall (void* mgr, int videoId, char* out128)` | the std::map lookup |
| 0x002D82F0 | func | `FmvParseVideoList` | `void __thiscall (void* mgr, char useAltColumn)` | runs at boot, unconditional |
| 0x0036EB80 | func | `AtelSysMovie010Start` | `int __cdecl (int, int, int*)` | the worked example of an FMV start |
| 0x0022FA30 | func | `FileOpenHost0` | `void* __cdecl (const char* host0Path, char mode)` | rewrites `host0:/x` to `/ffx_ps2/x`. Proof the dev reader is archive-backed |
| 0x0043AE30 | func | `ChReadFileDev` | `void* __cdecl (const char* path)` | |
| 0x0043B1D0 | func | `SgPcRead` | `void* __cdecl (const char* path, int mode)` | |

Already declared elsewhere, listed so nobody re-adds them: `AtelGetMessageText 0x46BF30`,
`AtelGetMessageAttr 0x46BF10`, `AtelGetMessageWindow 0x46BE90`, `AtelMesWinRequestClose 0x4640F0`,
`AtelOpMesWinSetMessage 0x457870`, `AtelOpMesWinShow 0x458B50`, `AtelOpMesWinClose 0x459060`,
`MesWinBlockingKind 0x46C8B0`, `MesWinStepAll 0x4AB910` (all `addresses/Cutscene.h`);
`EvLoadEventPackage 0x472EF0`, `EvCurrentEventId 0xEFBC40`, `EvCurrentEventName 0x1FCBC60`,
`EvPackageBase 0x1FCBD70`, `AtelActorPool 0x1FCBD78`,
`AssetResolvePathWithDebugOverrides 0x642C00`, `AssetResolvedPathBuf 0x1685C30` (all
`addresses/Minigames.h`); `AtelGetActor 0x46A830`, `AtelCreateThread 0x46EA00`,
`AtelStartThreadIfChannelFree 0x46EBE0`, `AtelSetActorPos 0x470B20` (all `addresses/Atel.h`);
`FmvCreatePlayer 0x2D9C80`, `FmvGetManager 0x2D73A0`, `FmvIsPlaying 0x241CF0`,
`FmvScriptRunning 0xD2A008`, `FmvScriptBlocking 0xD2A00C`, `AtelMovieLibTable 0x840E30` (all
`addresses/Cutscene.h`).

---

## 10. ENUMERATION TABLE

One row per list the UI needs.

| list | where it lives | stride and count | display label | runtime or baked |
|---|---|---|---|---|
| **maps and events** (one list) | asset path table, kind 12. Table at `AssetLoaderTable[12]`, kind base 562 from `cdrom.fid`, strings in `cdrom.fnd` | stride 18 **path-table entries** per id, sub-index 0 is the `.ebp`. 402 ids, 348 with a path, **330 usable** | the path's `<xx>/<name>` middle, as `FFX_Ev_LoadEventPackage` builds it. `bl/bltz0000` | **RUNTIME.** `loader[4](12)` then `loader[22](18*id)` per id. Copy out of the shared buffer. Existence needs `loader[5]` or the baked 18-id deny list |
| map and event names, alternative | `eventid.bin` through `EventIdNameTable 0x21D5888` | stride 16, count at `0x1534EC8`. 402 rows | `char name[12]` at row+0, **cap at 12, one row has no terminator**. Two dead rows, 101 and 111 | **RUNTIME, but only after `LoadEventIdTable(0)` 0x507F50.** Empty in a normal boot. Worse labels than the path route, use that instead |
| **doorways / spawn points** per map | inside the loaded `.ebp`, at `atel + *(u32*)(atel+4)` | **32 bytes** per record, count `(atel[8] - atel[4]) / 32`. 1 to 9 per package | `#N` plus the XYZ. There are no authored doorway names | **RUNTIME** once the package is loaded (`EvPackageBase + 0x40` is `atel`). Baked offline per id with `tools/ebp.py` for a picker that works before the warp |
| **messages** per map | the loaded package's table at `*(u32*)(atelCtx + 0x34)`, or the shipped `new_<lang>pc/event/obj_ps3/<xx>/<name>/<name>.bin` | **8 bytes** per id, count `table[0] / 8`. 0 to 365 per package, 15,413 total | the decoded string, `{XX}` for unmapped codes. Strip the codes to filter | **RUNTIME for the loaded package only** (it is the only table in memory). **BAKED** for a cross-package picker, via `tools/ebp.py` over the archive |
| **script entry points** per map | the loaded package's actor table. `actorDef+0x08` count, `actorDef+0x20` table | `u32` per entry. 6 to 2164 per package, 111,389 total | `actor <N> entry <M>`, plus the actor type. No authored names | **RUNTIME** from the loaded package, or **BAKED** with `tools/ebp.py` |
| **FMVs** | `std::map<int,std::string>` at `FmvPlayerManager + 1768`, built at boot from `ffx_videolist.txt` | red-black tree, key `int` at node+0x10, value `std::string` at node+0x14. **62 entries** | the path basename, e.g. `OPN_us.webm` | **RUNTIME.** Walk the map in order from `(*(node**)(mgr+1768))->_Parent`. 62-row baked fallback is in 7.2 |
| background map numbers | entry record +0x00 per doorway | s16. 280 distinct values over the 330 packages, range 1..692 | the number. There are no names | derived, not a UI list. Shown as detail on a map row |

---

## 11. WHAT I COULD NOT SETTLE

1. **Human-readable English map names.** The list labels are dev short codes. The save screen shows
   real names from a map-name id at save block `+0xD6`, written by
   `FFX_SaveData_SetMapNameIdFromGroundDic 0x470F00`. That id space is unmapped.
2. **The full text codec.** About 56 of the byte values are mapped. The name and term macros
   (`{13}{31}`, `{19}a`) need their own table, and the renderer that walks them is
   `FFX_MesWin_LayoutMessageAndFindChoices 0x4B9DF0`.
3. **Which background map numbers have no walkmesh.** 103 of the 491 containers are 64-byte stubs.
   The join between entry record +0x00 and `tools/mapfile.py`'s section-2 presence has not been run,
   so the 330-id list is not yet filtered for "boots with no collision".
4. **Whether a warp mid-battle, mid-FMV or mid-menu is clean.** The flag plumbing is confirmed to
   be consumed in all three, which is why the warp lands rather than being lost. Whether the result
   is a working field is INFERRED and needs a runtime probe.
5. **The purpose of the 16-id list at VA 0xB49B08** in `FFX_Fmv_CreatePlayer`.
6. **The `u16` at entry record +0x02 and +0x04, and the 8 bytes at +0x18.** All zero in every
   record I dumped, which is why they are unidentified rather than wrong.

## APPENDIX A - THE 330 LOADABLE IDS

Generated from `cdrom.fnd` kind 12 (base 562, stride 18) joined against `FFX_Data.vbf`,
with the background map number and doorway count read out of each `.ebp`. Columns are
`id label mapNo doorways messages`. `mapNo` is entry record 0's background map number and
`messages` is the built-in US table count. This is the **baked fallback**. The runtime route
in section 1.5 produces the id and the label without it, and the `.ebp` supplies the other
three columns once a package is loaded.

```
  0 ev/event00     1 1   0      1 st/startmap0   1 1   1      2 ca/camtest0   67 1   0
 16 pt/ptkl0200  117 3  55     17 bs/bsvr0000   75 9 103     18 kl/klyt0000  131 3  56
 19 bs/bsil0100   66 3  58     20 bs/bsil0200   67 3   1     21 bs/bsil0400   69 5  22
 22 bs/bsil0500   70 5  11     23 te/test20    690 1   6     41 bs/bsil0300   68 6  15
 42 bs/bsyt0000   85 5 306     43 pt/ptkl0100  116 3  24     44 kl/klyt0900  138 2  26
 45 kl/klyt1100  140 2  26     46 pt/ptkl1300  126 5  15     47 pt/ptkl1600  128 4  15
 48 bj/bjyt0000   30 2  24     49 bj/bjyt0200   32 3  27     50 bj/bjyt0300   33 2   2
 51 bj/bjyt0500   35 2   6     52 bj/bjyt0600   36 2   1     53 pt/ptkl1000  125 2  27
 54 ma/maca0400  334 2 149     55 lc/lchb0600  171 2   0     56 na/nagi0500  430 5  52
 57 lc/lchb0700  172 1  17     58 mi/mihn0200  212 4  69     59 mi/mihn0600  216 4 124
 60 bs/bsvr0200   77 2 150     61 sl/slik0200   97 3  32     62 bl/bltz0000  173 1  50
 63 bj/bjyt0400   34 5  39     64 cd/cdsp0700   57 2  11     65 kl/klyt0100  132 3  56
 67 bs/bsil0600   71 4  50     68 bs/bsvr0300   78 1  26     69 bs/bsil0700   72 4  26
 70 bs/bsil0000   65 3  82     71 cd/cdsp0000   50 3 131     72 lc/lchb1300  178 7 190
 73 lc/lchb1400  179 1  34     74 bj/bjyt0700   37 1   2     75 ge/genk0000  245 2  68
 76 dj/djyt0000  230 2  68     77 lc/lchb1800  183 3 211     78 kl/klyt0300  134 4  60
 79 ki/kino0000  220 5 115     80 mc/mcyt0600  345 3  53     81 dj/djyt0300  233 4  68
 82 dj/djyt0100  231 8  92     83 bs/bsvr0001   75 9  13     84 bs/bsyt0001   85 5  21
 85 lc/lchb0100  166 2  65     86 lc/lchb0200  167 4  39     87 lc/lchb0300  168 2  59
 88 lc/lchb0400  169 3  72     89 lc/lchb0500  170 2  98     90 dj/djyt0700  237 2  27
 91 dj/djyt0800  238 2   9     92 ki/kino0100  221 5  46     93 ki/kino0400  224 4 101
 94 sw/swin0200  147 3 179     95 mi/mihn0000  210 3 157     96 kl/klyt0500  135 3  64
 97 ge/genk1600  261 3  13     98 pt/ptkl0600  122 4  37     99 ge/genk0900  254 2  16
100 bs/bsvr0100   76 1  59    102 ma/maca0200  332 2  73    103 bs/bsyt0500   89 2   6
104 lu/luca0100  186 3 163    105 ge/genk0100  246 2  13    106 mc/mcyt0200  341 5  73
107 lu/luca0600  191 2 155    108 kl/klyt1000  139 2  33    109 ge/genk1500  260 2  55
110 mc/mcfr0000  310 7  96    112 mi/mihn0100  211 2  52    113 cd/cdsp0200   52 1  33
115 mi/mihn0400  214 6  49    116 mi/mihn0500  215 4  35    117 bs/bsyt0600   90 1   4
118 kl/klyt1200  141 1   3    119 ki/kino0200  222 2 121    120 mi/mihn0700  217 2  66
121 lc/lchb0800  173 1  11    122 bs/bsyt0100   86 3  47    123 lc/lchb0000  165 8   0
124 lc/lchb0801  173 1  10    125 lc/lchb0802  173 1   0    126 lc/lchb0803  173 1   0
127 mi/mihn0800  218 2 100    128 ki/kino0500  225 4  85    129 bi/bika0000  350 5  80
130 bi/bika0400  354 3  42    131 ki/kino0700  227 4  90    132 zn/znkd0600   16 3   2
133 bs/bsvr0002   75 9  20    134 zn/znkd0700   17 4  65    135 gu/guad0000  265 9 256
136 bi/bika0100  351 4 108    137 bi/bika0200  352 7  82    138 bi/bika0300  353 5  97
139 pt/ptkl0000  115 1   2    140 ka/kami0000  300 5  89    141 gu/guad0600  271 1  67
142 bs/bsvr0400   79 1 300    143 bs/bsvr0500   80 1 287    144 bs/bsvr0600   81 1 287
145 bs/bsvr0900   82 1   9    146 bs/bsyt0300   87 1   0    147 bs/bsyt0400   88 1 287
148 sl/slik0400   99 4  42    149 sl/slik0700  102 1   4    150 sl/slik0800  103 1  24
151 pt/ptkl0800  124 1  41    152 pt/ptkl1400  127 2  26    153 mc/mcyt0000  340 5  49
154 sl/slik0300   98 2  22    155 kl/klyt0600  136 1  21    156 kl/klyt0700  137 1  10
157 lc/lchb0900  174 2  24    158 lc/lchb1100  176 2  65    159 lu/luca0400  189 1 115
160 dj/djyt0400  234 1  14    161 dj/djyt0500  235 1  26    162 ka/kami0300  303 7  74
163 gu/guad0500  270 3  19    164 ma/maca0000  330 3  77    165 zn/znkd0500   15 1  30
166 kl/klyt0200  133 2   0    167 sw/swin0700  152 1  16    168 sw/swin0800  153 1  12
169 sw/swin0900  154 1  24    170 lc/lchb1000  175 2  38    171 mi/mihn0300  213 2 112
172 gu/guad0200  267 1  20    173 gu/guad0300  268 1  42    174 gu/guad0400  269 1  53
175 ik/ikai0000  275 2  11    176 mc/mcfr0500  315 3   9    177 mc/mcfr0700  317 4   6
178 mc/mcyt0300  342 1  30    179 mc/mcyt0400  343 1  17    180 bv/bvyt0200  412 2   8
181 bv/bvyt0300  413 2   0    182 bv/bvyt0400  414 3  27    183 mc/mcfr1100  321 2  56
184 lc/lchb1200  177 1 365    185 sw/swin0300  148 1  26    187 ge/genk0400  249 2  23
188 ge/genk0500  250 3  50    189 ge/genk1100  256 2  27    190 ge/genk1200  257 3  42
191 zn/znkd0400   14 2  29    192 ma/maca0300  333 3 113    193 ik/ikai0600  281 1 122
194 hi/hiku0800  388 3 222    195 bv/bvyt1000  420 2  15    196 bj/bjyt1000   40 2   1
197 gu/guad0700  272 1   9    198 bv/bvyt0900  419 3  26    199 ss/ssbt0000  565 1   9
200 ss/ssbt0100  566 1  17    201 ss/ssbt0200  567 1   0    202 ss/ssbt0300  568 2  65
203 si/sins0200  582 3  11    204 si/sins0400  584 6  38    205 bv/bvyt0000  410 1  35
206 mc/mcfr1200  322 3  74    207 bv/bvyt0500  415 1 113    208 st/stbv0100  406 5 129
209 st/stbv0000  405 3  10    210 dj/djyt0200  232 1   8    211 hi/hiku0000  380 9 118
212 bl/bltz0201    7 1 116    213 ik/ikai0800  283 1  17    214 dj/djyt0600  236 2  17
215 ma/maca0100  331 2  95    217 gu/guad0601  271 1  21    218 ki/kino0600  226 1   4
219 az/azit0400  364 2  50    220 sl/slik1000  105 1   8    221 mc/mcfr0400  314 5  38
222 do/dome0000  515 3  42    223 na/nagi0000  425 5 361    224 do/dome0400  519 2  97
225 zk/zkrn0200  502 2   2    226 bv/bvyt1200  422 2  30    227 bv/bvyt1300  423 1  77
230 lc/lchb0806  173 1   0    234 ge/genk0200  247 1   3    235 ge/genk0600  251 3  75
236 ge/genk1300  258 2  41    237 sw/swin0400  149 4  48    238 mc/mcfr0900  319 1  18
239 mc/mcyt0500  344 3  24    240 mm/mmmc0000  450 1   0    241 mc/mcfr0100  311 4  65
242 mc/mcfr0200  312 4   2    243 gu/guad0100  266 2  76    244 mt/mtgz0100  486 5  51
245 dj/djyt0900  239 1   5    247 ki/kino0300  223 2   8    248 mc/mcfr0300  313 1  45
249 zn/znkd0300   13 2  19    250 lc/lchb0601  171 2   8    252 bs/bsmm0000   65 2  23
253 ms/msmm0000  419 1  14    254 ki/kino0900  229 2  27    255 hi/hiku0801  388 3 100
256 ka/kami0400  304 3  15    257 ik/ikai0100  276 2  29    258 om/omeg0000  590 5  42
259 mt/mtgz0000  485 4 230    260 ma/maca0401  334 2  40    261 hi/hiku1600  396 1  28
263 ka/kami0100  301 3 129    264 ka/kami0200  302 3  19    265 hi/hiku0500  385 8 174
266 na/nagi0400  429 3  46    267 lc/lchb0201  167 3  73    268 lc/lchb0301  168 2  19
269 mc/mcfr1300  323 3  25    270 do/dome0600  521 1  83    271 om/omeg0100  591 2   2
272 mt/mtgz0600  491 9  32    273 mc/mcfr0600  316 3   0    274 mc/mcfr0800  318 4   0
275 az/azit0700  367 1  30    276 az/azit0000  360 2   3    277 hi/hiku1500  395 1  48
279 na/nagi0100  426 4   0    280 az/azit0300  363 7  59    282 sl/slik1100  106 1   0
283 na/nagi0600  431 1  20    284 mc/mcyt0700  346 1   5    285 mt/mtgz0200  487 3  88
286 az/azit0600  366 1   0    287 bv/bvyt0600  416 1  22    288 cd/cdsp0100   51 2   0
289 ki/kino0800  228 1   0    290 lm/lmyt0000  445 3  62    291 ge/genk1000  255 1  81
292 ga/gameover  691 1   0    293 pt/ptkl1700  129 1  20    294 pt/ptkl1800  130 1  20
295 bj/bjyt0800   38 2   0    296 si/sins0300  583 2  19    297 zn/znkd0000   10 1   3
298 bj/bjyt1200   42 1  12    299 lc/lchb1500  180 1   1    301 sl/slik0000   95 4 142
302 sw/swin0000  145 3 109    303 az/azit0500  365 2   0    305 bv/bvyt0100  411 3  14
306 bv/bvyt1100  421 2  14    307 na/nagi0700  432 2 169    308 lm/lmyt0100  446 2  55
309 mt/mtgz0300  488 6  24    310 mt/mtgz0700  492 5   0    311 mt/mtgz0800  493 2  48
312 mt/mtgz0900  494 2   0    313 zk/zkrn0100  501 4   0    314 zk/zkrn0300  503 5   0
315 zk/zkrn0600  506 2   4    316 do/dome0100  516 5  30    317 do/dome0200  517 1   0
318 do/dome0300  518 3  31    319 do/dome0500  520 2  42    320 do/dome0700  522 4  31
321 ss/ssbt0400    1 1   0    322 si/sins0000  580 4  16    323 si/sins0100   -1 0   0
324 si/sins0500  585 2   0    325 si/sins0600  586 2  89    326 si/sins0700  587 1  11
327 si/sins0900  589 4  11    329 mc/mcfr1400  324 4  14    330 st/stmm0100  406 1  15
331 ma/mamm0000  330 3  11    332 ka/kamm0300  303 3  26    333 ge/gemm0100  246 2  17
334 mi/mimm0200  212 4  22    335 lu/lumm0100  167 2  14    336 sl/slmm0200   97 3  22
337 bv/bvmm0000   75 8  12    338 ma/mamm0300  313 1  12    339 bv/bvmm0600  420 2  38
340 bl/bltz0001  173 1   0    341 ki/kimm0000  585 2   0    347 bl/bltz0200    7 1 136
348 me/memochek  692 1   0    349 lo/loopdemo   16 3   2    351 hi/hiku0200  382 6  67
352 bl/bltz0002  173 1   0    354 bl/bltz0004  106 1   0    355 bl/bltz0005  173 1 154
356 bl/bltz0006  173 1 113    359 bl/bltz0009  173 1   0    361 mt/mtgz1000  495 2   0
362 mt/mtgz1100  496 2   0    363 zk/zkrn0000  500 4   0    364 ik/ikai0101  276 2  10
365 ma/maca0500  335 2 102    366 zn/znkd0800   18 3   0    367 zn/znkd0900   19 2  12
368 zn/znkd1000   20 3  60    370 zn/znkd1200   22 1   4    371 zn/znkd1300   23 1   4
372 lm/lmyt0200  447 1   3    374 hi/hiku1900  399 3  90    375 hi/hiku2000  400 9  19
376 zn/znkd1400   24 2  54    377 lu/luca0800  193 1   7    380 cd/cdsp0800   58 1   0
381 is/isho0000  211 2  79    382 hi/hiku2100  401 1  20    383 mt/mtmm0000  486 2  16
384 op/open0100  586 2   7    385 op/open0200  226 1   4    386 en/endg0100   76 1   0
387 en/endg0200   90 1   0    388 en/endg0300  346 1   0    389 zn/znkd0801   18 3  19
390 en/endg0400  423 1   0    391 az/azmm0000  191 2  41    392 ma/matu0000  395 1   0
```

