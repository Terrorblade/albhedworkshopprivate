# The display list: what the step queues and how the render walks it

Written 2026-10-12 for Project Zanarkand PROGRESS 1.5.7 (the F1 seam). The pass order that reads
these queues is in RENDER_PASSES.md, and the recreation's typed copy of the layouts is
`src/render/DisplayList.h` in the projectzanarkand repo. Every VA below is at the preferred base
0x400000 and was read in Hex-Rays or the disassembly unless a line says "inferred". IDA was used
read only for this note, so the names in section 10 are proposals and are not in the IDB yet. Where
this doc uses a `sub_` name it is because the function has no IDB name today.

## 1. Short answer

There is no single display list in the PS2 sense, a buffer of draw packets that the step writes and
the render plays back. What exists instead is three layers:

1. A retained Phyre scene. Meshes live in PhyreEngine cluster lists hanging off the gfx context.
   The step moves them (`FFX_Gfx_SetMeshTransform`) and shows or hides them by linking and
   unlinking (`FFX_Gfx_LinkMeshToNode`, `FFX_ChrInstance_SetHidden 0x63C390`). The render culls and
   draws whatever is linked at the moment it runs. Nothing is recorded per step for these.
2. FFX-side queues inside `g_ffxGfxCtx` plus a few side structures. These are the real display
   list: VFX draw items, a sorted transparent list, pass commands that break the transparent walk,
   model matrix overrides, a callback registry, the 2D sprite list and the VFX primitive build
   queue. The step appends, the render consumes. Sections 3 to 8.
3. The PhyreEngine `PRenderer` command list. A ring of 72-byte records the main thread writes while
   `FFXApplication::render 0x42F930` runs and the Phyre render thread executes. This is per
   presented frame and is pure Phyre. Section 2.

The PS2 GS path is a write-only vestige on PC. `FFX_Dma_StartGifChain`, the packet buffers
`g_ffxGsPacketBufA 0x23016E0` and `g_ffxGsPacketBufB 0x23036E0`, and `FFX_Gs_SendDrawEnvPacket`
all end in `nullsub_39` or `g_ffxPs2HwRegSink`. No reader of those buffers exists apart from the
DMA helpers themselves and `sub_760CE0` / `sub_760E30`. A port can drop them.

## 2. The PRenderer command list (render time, Phyre)

Every command is a 72-byte record (18 dwords). Dword 0 is the kind, the payload starts at +8. The
appenders all have the same shape, seen in `sub_5BC060` and `sub_5B6330`:

- check the ring cursors at renderer+0x24/+0x28 and +0x38/+0x3C and call renderer vtbl+8 (flush and
  post) when the next 72 bytes would cross them
- `sub_613E10(72)` takes 72 bytes from the ring, returning 0 when full (the appender then returns 13)
- the first record of a frame is stored at renderer+0x4C, and the end pointer at renderer+0x50 is
  moved to record+72

| kind | appender | payload |
|---|---|---|
| 1, 2, 3 | mesh draw path, see below | draw records, executed in `Phyre_RenderThread_ExecuteList 0x615550`. Payload not decoded |
| 4 | `sub_5BC060` setViewport | x +8, y +12, w +16, h +20. Also cached at renderer+8..+20. Width or height above 0x8000 prints "wyq Viewport size error" and writes to address 0 on purpose |
| 5 | `sub_5BBDC0` setScissor | rect at +8..+20, executed by `Phyre__PRenderingD3D11__setScissorRect` |
| 6 | `sub_5BADA0` setClearColor | four floats +8..+20 |
| 10 | `sub_5BAFC0` | one int at +8, meaning not read |
| 11 | `sub_5BB130` setColorTarget | +8 points at a 16-byte target desc copied out of the ring, +12 is the slot, slots 0..3 |
| 12 | `sub_5BB5B0` setDepthTarget | +8 points at the target desc |
| 19 | `sub_5B5E30` beginScene | int at +8. Sets renderer+4 bit 0, returns 22 when a scene is already open |
| 20 | `sub_5B6330` clear | flags at +8. Returns 22 unless a scene is open. Values seen are 1, 10, 11 |
| 21 | `sub_5B6950` endScene | int at +8 |
| 22 | `sub_5B6AF0` present | the executor calls `Phyre_RenderD3D11_Present` then `sub_430210`. The appender bumps renderer+0x13C |
| 30 | `sub_5BB390` | byte at +8, dword at +12, meaning not read |
| 44 | no helper found | callback, function pointer at +8 and argument at +12, run by `Phyre_RenderThread_ExecuteList` |

Kinds 7 to 0x2B not in the table are dispatched by `Phyre_RenderThread_ExecuteCommand 0x614C60`.
`sub_5B35C0` builds the 16-byte target desc `{target, 0, arg, 0}` the target setters take.

Meshes are recorded through `sub_5B9A90(renderer, meshInstance, meshInstance+16, 0, renderer+316)`
or the shorter `sub_5B9A70(meshInstance)`. Which shader pass a mesh draws with is chosen by a
pointer the caller stores at renderer+72 before drawing. It points at a 16-byte
`{const char* name, 0, 0, -1}` pass type record:

| record | name |
|---|---|
| 0xC169B4 | Opaque |
| 0xC169C4 | BlackInside |
| 0xC169D4 | Transparent |
| 0xC169E4 | OpaqueNoMSAA |
| 0xC169F4 | TransparentNoMSAA |
| 0xC16A04 | preOpaque |
| 0xC16A14 | preTransparent |
| 0xC16A24 | preTransparentATC |
| 0xC16A34 | postOpaque |
| 0xC16A44 | postTransparent |
| 0xC16A54 | Shadow |
| 0xC16A64 | ShadowTransparent |
| 0xC16A74 | ZPrePass |
| 0xC16A84 | DeferredRender |
| 0xC16A94 | SpecularPass |
| 0xC16AA4 | SpecularPass_nolight |
| 0xC16AB4 | SpecularPass_nolight_kohno1 |
| 0xC16AC4 | SpecularPass_nolight_akaza1 |
| 0xC16AD4 | SpecularPass_nolight_akaza2 |
| 0xC16AE4 | SpecularPass_akaza |
| 0xC16AF4 | PS2Lighting |
| 0xC16B04 | TransparentNega |
| 0xC16B14 | FakeTransparent |
| 0xC16B24 | DressUpFast |
| 0xC16B34 | DressUpMelt |
| 0xC16B44 | HSVPass |
| 0xC16B54 | TransparentClipY |
| 0xC16B64 | SetStencilFlag |
| 0xC16B74 | EnableStencilTest |
| 0xC16B84 | OptimizedTrans |
| 0xC16B94 | DepthTestDisable |
| 0xC16BA4 | DepthTestDisableWithOptimize |
| 0xC16BB4 | CaptureEmissiveSurfaces |
| 0xC16BC4 | CaptureTriangleGeometry |
| 0xC16BD4 | NULL |

The table is 35 records of 16 bytes from 0xC169B4 to 0xC16BD4, every one `{name, 0, 0, -1}`. Only
the ones this note names a user for are confirmed in use. The SpecularPass family is selected in
`sub_6C59C0`, see pass command kind 4.

Blend, depth and cull state are not set by FFX code per pass. They come from the material's
technique for the named pass, so RENDER_PASSES.md names the pass type and the targets, not a blend
enum.

Posting. After `FFXApplication::render` returns, `Phyre__PApplication__frameTick 0x627940` calls
`sub_5B6AF0`. That first runs `sub_614C00(renderThread)`, which waits on the handle at
renderThread+0x164, then appends the kind 22 record. Renderer vtbl+8 is
`PRendererBase__vf02 0x5B6C50`. It hands begin (+0x4C), end (+0x50) and the event at +0x44 to
`sub_615210`, which puts them in the render thread's 16-slot ring, then waits on +0x44 when it is
non-null and resets the list. The render thread main loop `0x616010` executes a slot and clears it.

## 3. The gfx context draw queues

`g_ffxGfxCtx` is a 0x10DE8-byte object built by `FFX_GfxCtx_ctor 0x64DC00` (BOOT_SEQUENCE.md
section 10). The fields this note uses:

| offset | what |
|---|---|
| +124, +140, +156, +172, +188 | 16-byte Phyre cluster list heads (`sub_4429E0`), see RENDER_PASSES.md |
| +204 | clear and fog colour object. Floats [12..14] are the clear RGB |
| +208 | `PRenderer*`, set to app+292 in `sub_657A50` |
| +220 | `PCamera*` of the main camera (points at +224). More cameras at +568, +916, +1620, +2016 |
| +4300 | the post-process target manager, the same object as `dword_CCC8A0` (`sub_692BE0` returns it) |
| +0x3854 | `PArray<PMeshInstance*>` for submit mode 2 (distortion sample) |
| +0x3860 | `PArray` for submit mode 1 |
| +0x386C | `PArray` filled by `sub_656A00` from `GuildMapDynamicGeometry` vf02 `0x6A5070` |
| +0x3878 | `PArray` for submit mode 0 when the item byte +80 is set (`sub_6565E0`) |
| +0x3884 | unique set `{u32 capacityAndFlag, u32 data, i32 count}` for submit mode 4, default capacity 1024 |
| +0x3890..+0x38A0 | deferred removal list |
| +0x38CC | ordered list, `PMeshInstance*[3072]` |
| +0x68CC | sorted list, `VFXDynamicGeometry*[3072]` |
| +0x98CC | bucket sort link nodes, `{item, next}[3072]`, scratch for mode 1 of `sub_654650` |
| +0xF8CC | ordered count |
| +0xF8D0 | sorted count |
| +0xF8D4 | batch marks, `i32[16]`, ordered count at each mark |
| +0xF914 | batch marks, `i32[16]`, sorted count at each mark |
| +0xF954 | batch mark count |
| +0xFB48, +0xFB50 | mesh group list count and data |
| +0xFB54 | pass command array `{i32 count, i32 capacity, u32 data}` of 16-byte records |
| +0xFB60 | `i32[7]` per-kind pass command counters |
| +0x10D2C | byte, "needs sort" |
| +0x10D84 | model matrix override count, high bit is a flag |
| +0x10D88 | model matrix override data, 68-byte records |

The `PArray` shape is `{i32 count, i32 capacity, u32 data}`, 12 bytes.

### 3.1 Appending a draw item

`sub_656C10(gfx, item, ordered)`. When `ordered` is set the item's mesh instance (`item+12`) goes on
the ordered list. Otherwise the `VFXDynamicGeometry*` itself goes on the sorted list and byte
+0x10D2C is set if `sub_6721B0` returns nonzero. Both lists cap at 3072. The sort key is the float
at item+0xDC, written by `sub_6A3B20` through `sub_645750(key, depth)`.

### 3.2 Batch marks

`sub_645840` copies the two counts into the next mark slot (16 slots). It is skipped on catch-up
steps. Callers are `FFX_Vfx_EndPass 0x7EC48B`, `sub_7EC7E0` and host API slot 932. `sub_6564A0`
writes a mark as well (section 4). A batch is the run of items appended between two marks.

### 3.3 Sort and merge, render time

`sub_654650` runs at render `0x669DD9`, right after `FFX_Frame_WaitJobSchedule 0x669DD2`, and in
`sub_652A10`. It appends a closing mark. Then, only when the needs-sort byte +0x10D2C is set (and it
clears it), it sorts each batch's segment of the sorted list. The mode comes from `sub_6721B0`:

- 2: `qsort` with comparator `sub_659140`. Magic ids 515 and 667.
- 1: a 4096-bucket sort on `depth * 0.0013020834` (1/768), clamped to 0..4095, stable within a
  bucket. The link nodes live at +0x98CC and the bucket heads and tails are two 16 KB stack
  arrays. The default, and a listed set of magic ids.
- 0: no sort. Level hashes 7862, 1154, 4453, 7752, 8358.

It then rebuilds the ordered list batch by batch, each batch's ordered entries followed by that
batch's sorted entries (their +12 mesh instance), capped at 3072, and resets the batch count and the
sorted count to zero. After this the sorted list is empty and the ordered list is the final
transparent draw order.

### 3.4 Walking the ordered list

`sub_650670` (render `0x669EAB`, also from `sub_653960`) walks the ordered list and draws each mesh
with `sub_5B9A90` under the Transparent pass type. Before each index it runs every pass command
whose drawIndex equals that index (section 4). There is a special case for magic id 662.

### 3.5 Resets

`FFX_Gfx_ResetDrawItemQueues 0x644DD0` runs twice per `FFX_MainStep` and clears the ordered and
sorted counts, the PArrays, the set, the batch marks and +100..+108. `sub_659000` (render
`0x669F57`) clears the pass command array, its counters and the mesh group count. `sub_65A620` runs
after the submit, drains the deferred removal list, and calls `sub_645620` (zeroes +12 of every
registry model) and `sub_6687A0` (removes one item from every queue).

## 4. Pass commands

Records are 16 bytes in the array at +0xFB54: `{i32 kind, i32 drawIndex, u32 arg8, u32 argC}`.
`sub_656940(gfx, record)` rejects kinds outside 0..6, sets `drawIndex` to the ordered count plus the
sorted count at the last mark (when a mark exists), pushes through `sub_656350` and increments the
per-kind counter at +0xFB60. Every public wrapper returns early on `g_ffxIsCatchUpStep`. The
executor is `sub_666E40`, called from the walker `sub_650670`.

| kind | appender | what the executor does |
|---|---|---|
| 0 | `sub_6564A0(gfx, layer)` via `sub_639FF0` (caller `sub_830CD0`), also writes a batch mark | layer break: endScene, draw the mesh groups of layer arg8 through `sub_6673B0`, rebind targets +140 and +144, viewport, beginScene |
| 1 | `sub_6568A0(gfx, src, dst, clearAfter, useColorScale)`. Wrappers `sub_644BE0` (0, 0, slot 904), `sub_644C40` (1, 1), `sub_644C70` (0, 1, slot 945) | target copy. Selector 0 is target +140, 1 is +168, 2 is +74540. `sub_692E90` with ColorScale when the short at +10 is nonzero, else `sub_692C40`. argC nonzero clears to (0,0,0,0) and does beginScene(11) |
| 2 | `sub_644D40(r, g, b, a, alphaFunc)`, blend from `maybe_FFX_Vfx_AlphaFuncToBlend` | full-screen colour quad. Bytes scaled by 1/128, blend written into target manager +74652, drawn by `sub_697490` |
| 3 | `sub_6E5C90` from `sub_642750` | `sub_6E5A40(arg8)`, the character light matrices (LvMatrix, LcMatrix, charlight) |
| 4 | `sub_6C56F0(chrInstance)`, skipped for magic ids 581, 661, 649, 578. Callers `sub_6C5B80`, `sub_6C5C90`, `sub_6C5DA0`, `sub_6C5E30`, `sub_6C7090` | specular character pass. `sub_6C59C0(arg8, renderer)` on the object `sub_6C57E0` returns (`dword_CDEC1C`). For each of its four registered character instances (or only arg8 when nonzero) it hides the instance, picks a SpecularPass variant from the slot's mode (0..5), draws every mesh, and unhides. Magic ids 324 and 626 draw a ZPrePass first |
| 5 | `sub_644CA0(chrInstance, flag)`, slot 931 | forces `FFX_ChrInstance_SetHidden(inst, 1)`, draws through `sub_6673B0(inst, flag)`, endScene, restores the hidden flag |
| 6 | `sub_644CF0(meshArray)`, slot 938 | draws each mesh of the array with pass type ZPrePass |

`sub_65EFA0` is true when the kind 0 or kind 5 counter is above zero (and magic is not 697). The
presenter uses it to move colour correction after the transparent walk (RENDER_PASSES.md step 23).

## 5. Mesh groups and model matrix overrides

Mesh group list (count +0xFB48, data +0xFB50). Each entry is a `PArray` of meshes with a shown byte
at +20 and a layer char at +21. `sub_656590` adds one (from `sub_63A010`), `sub_668640` removes one.
`sub_6589E0(layer)` links the groups of a layer and unlinks the rest, `sub_66C770` links them all
back. Kind 0 pass commands and the layer 0 draw in the presenter use this.

Model matrix overrides (count +0x10D84, data +0x10D88). Records are 68 bytes,
`{u32 meshInstance, float matrix[16]}`. `FFX_Gfx_ClearModelMatrixOverrideList 0x63DAC0` clears them
at the top of `FFX_MainStep`. `sub_656780` pushes one (via `sub_639170`, host slot 913, and
`sub_80EB50`). `sub_66B360(restore)` consumes them at render. It writes each matrix to its mesh with
the X and Y rows negated, renders, and puts the old matrix back when `restore` is set. Called from
`sub_64FBB0` and `sub_6673B0`.

## 6. RenderDelegationHelper, the callback registry

The class name is in the string "CAS failed at RenderDelegationHelper::setDataToFreeList". The
registry is `dword_CCCA98`, guarded by the mutex `dword_CCCAA0`. Layout, 520 bytes:
`{u32 countAndFlag, u32 entries, {u32 record, LONG inUse} pool[64]}`. Records are 20 bytes:
`{u32 data, u32 ownedBuffer, u32 ownedSize, u32 flags, u32 callback}`. Bit 0 of `flags` frees the
record after it runs. The rest of `flags` is the phase mask. `sub_6ADE20(registry, src, size, flags,
fn)` registers one.

Who registers (all step time):

- `sub_6429F0`, generic, host slot 863
- `sub_645E60`, callback `sub_637C50` clears the fade, default flags 0x801, slot 864
- `sub_642A10`, flags 0x801, callback `sub_637C10`
- `sub_642B10`, flags 0x2001, callback `sub_637D40`, a 68-byte `{ptr, matrix}` payload
- `sub_6AE650`, the fade quad. 0x20-byte payload `{app+292, camera, r, g, b, a, mode}`, callback
  `sub_6AE5B0` which draws through `sub_697490`. Wrappers `sub_639240` (0x1001), `sub_639280`
  (0x801), `sub_6392C0` (0x8001). `FFX_Sg_StepScreenFades 0x82C0C0` queues through `sub_639240`.
- also `sub_65B8A0` and `sub_6F54F0`

Phase runners. Each runs the records whose flags carry its bit:

| mask | runner | where it runs |
|---|---|---|
| 0x2 | `sub_6AD4E0` | `FFX_Frame_UpdateBoostersAndOverlays` |
| 0x4 | `sub_6ADAD0` | `FFX_Frame_UpdateBoostersAndOverlays` |
| 0x8 | `sub_6ADA20` | presenter 0x668EF0, `sub_652A10` |
| 0x10 | `sub_6AD8C0` | 0x669349, 0x6693CE |
| 0x20 | `sub_6AD970` | 0x66935B |
| 0x40 | `sub_6AD810` | 0x669573 |
| 0x80 | `sub_6AD6B0` | 0x669F62 |
| 0x100 | `sub_6AD760` | 0x669855, 0x669EE1 |
| 0x200 | `sub_6AD2D0` | 0x669889, 0x669F15 |
| 0x400 | `sub_6AD600` | 0x66A7A8 |
| 0x800 | `sub_6AD380` | 0x66AD88, `sub_652A10` |
| 0x1000 | `sub_6AE450(target)` | 0x66A27A, 0x66A807, 0x66ACCF, 0x66AD21. Stores the target at data+8 and returns whether anything ran |
| 0x2000 | `sub_6ADB80` | 0x669BDA |
| 0x4000 | `sub_6AD430` | 0x669424 |
| 0x8000 | `sub_6AE0C0` | 0x669BF2 |

`FFX_RenderDelegation_FreeFlaggedEntries 0x63DAB0` runs from `FFX_MainStepLoop` before every
`FFX_MainStep` and frees the 0x1000 records through `sub_6AE530`. So a fade queued by a catch-up
step is dropped before any render sees it, and only the last step of a frame reaches the screen.

## 7. VFX dynamic geometry, the producer of most queue items

Registry `dword_CCCA6C`, keyed by a u64, pools of 3000. Transform records are 208 bytes at
registry+44, geometry objects are 248 bytes at +48, used bytes at +52, next free at +56. Allocate
`sub_69F750`, look up `sub_69F620` (cached), add `sub_69F890`.

`sub_644DE0` (host slot 881) creates a model and sets the transform defaults: identity matrices at +0
and +64, bounds -10000 to +10000 at +152..+172, qword +184 = 2, +196 = 1, +200 = -1, short
+194 = -1. On the geometry object it sets +36 = transform, +60 = argument, +81 = the current draw
layer `byte_C322AA` (set by `FFX_Glue_SetDrawLayer 0x645670`).

`VFXDynamicGeometry` (vtable 0xB47E40, ctor `sub_69E3E0`):

| offset | what |
|---|---|
| +12 | `PMeshInstance*` |
| +20 | material or mesh `PArray` |
| +36 | transform record |
| +48 | byte |
| +60 | flags. 0x100 forces submit mode 4 |
| +64 | colour override byte |
| +68..+75 | two RGBA colours |
| +76 | float |
| +80 | byte, routes mode 0 to +0x3878 |
| +81 | draw layer |
| +82 | flag for the matrices at +84 and +148 |
| +212 | float |
| +216 | byte, YIQ |
| +220 | sort depth, default -1.0 |
| +224..+236 | colour, default 1, 1, 1, 1 |

Transform record fields read: +128 zCrct, +132 screen shift flag, +136 ScreenShift float[2], +144
texture count, +148 texture array of 108-byte entries (set by `sub_645040`, slot 882), +182, +183
cameraFadeout, +192 multi, +196 part count, +200 part mask.

Submit is vf02 `sub_6A52F0(mode)`. It calls vf10 `sub_6A53E0` (constants) unless mode bit
0x40000000 is set, then routes on `mode & 0x7FFFFFFF`:

| mode | goes to |
|---|---|
| 0 | +80 set: +0x3878. Else `sub_656C10`, the sign bit of mode picks the ordered list |
| 1 | +0x3860 |
| 2 | +0x3854 |
| 4 | the +0x3884 set |

`dword_CCCA58 == 1` forces mode 4 unless the mode is 2. Mode 3 falls through and is not queued
anywhere I found.

Step-time submitters, all skipping on `g_ffxIsCatchUpStep` and looping parts with key + 10000 * i:
`sub_6458A0` (mode 0, slot 775), `sub_645BA0(key, mode)` (slot 865) with wrappers `sub_645600`
(0x80000000, slot 870, 29 callers), `sub_645330` (2), `sub_644DB0` (4), `sub_6414F0` (3),
`sub_644BC0`, and `sub_644B50` (vf01 then vf02(1)).

Sibling vtables: `ClassDynamicGeometry` 0xB47E14 (vf02 `0x6A4A50` sets zCrct, zMatrix,
ScreenShift, doCameraFadeout), `GuildMapDynamicGeometry` 0xB47E70 (vf02 `0x6A5070` ->
`sub_656A00`), `MenuDynamicGeometry` 0xB47E9C.

## 8. The VFX primitive build queue and the 2D list

Prim build queue. Six buckets `dword_CEC1A8[6]`, each `FFX_Malloc(0x2C6B8)` in `sub_728750`. Slots
are 532 bytes from the bucket base, kind byte at +0 with values 0..6, at most 342 per bucket. 342 *
532 is 0x2C6B8 - 0x400, so the last 1 KB of each bucket is never used. Counts `dword_CEC1C0[6]`,
costs `dword_CEC1D8[6]`. `sub_728660(cost)` picks the cheapest bucket with room and returns the
slot. `sub_71ED10` runs a range of slots, dispatching kind 0..6 to `sub_71F320`, `sub_727110`,
`sub_7269A0`, `sub_7256C0`, `sub_7287B0`, `sub_71EE80`, `sub_7282D0`.
`FFX_Vfx_FlushPrimBuildQueue 0x71EDB0` runs at step end in `FFX_MainStepLoop`. Under cost 500 and
count 50 it runs inline, otherwise it posts six JobSchedule jobs, which the render joins in
`FFX_Frame_WaitJobSchedule 0x71E810`.

2D list, manager `dword_CCC81C`. Two sets selected by the toggle at +23420. Set A: pools
+5600/+5604 and +5608/+5612, counts +11256/+11260, draw list at +0, count +11344. Set B: pools
+16952/+16956 and +16960/+16964, counts +22584/+22588, list at +11352, count +22592.
`FFX_DynDrawList_SwapFrameSets 0x642320` swaps per non-catch-up step. Appends go through
`sub_67BA50` from `sub_63EEE0` (the 2D sprite entry, 92 call sites, "NoTexture") and `sub_640660`
(22 sites in the 0x8EC000-0x8FA000 menu code). The render reads the count through `sub_67B970` and
draws with `sub_650CB0` and `sub_650340` (the second frustum-tests).

## 9. Step time versus render time

Built per step, inside `FFX_MainStep` or `FFX_MainStepLoop`:

- queue resets (`FFX_Gfx_ResetDrawItemQueues`, `FFX_Gfx_ClearModelMatrixOverrideList`)
- VFX submits into the ordered, sorted and PArray queues, and the batch marks
- pass commands, model matrix overrides, delegation records (fades among them)
- mesh transforms and cluster links (`FFX_Ch_StepAll`)
- the 2D list and its swap
- the prim build queue flush at step end, and `FFX_DynGeoMem_FlipFrameSegment` (also called from
  endFrame `0x42F4F0`)

Built per presented frame, inside `FFXApplication::render`:

- the join on the prim build jobs and the sort and merge (`sub_654650`)
- every cluster walk and its culling, every PRenderer command
- all post-processing, the final copy to the back buffer, Iggy

Consumed or cleared at render, which matters for presenting one step twice (PROGRESS 1.5.12):

- `sub_654650` empties the sorted list and the batch marks
- `sub_659000` empties the pass commands and the mesh group count
- the 0x1000 delegation records have run, and the one-shot records are freed

Inferred, not tested: a second render of the same step would still draw the merged ordered list,
but without the layer breaks, target copies and colour quads the pass commands carried, and without
the one-shot callbacks. The first render is the only faithful one.

## 10. Proposed IDB names

| VA | proposed name | evidence |
|---|---|---|
| 0x5BC060 | Phyre_PRenderer_SetViewport | writes kind 4, "wyq Viewport size error" |
| 0x5BBDC0 | Phyre_PRenderer_SetScissor | kind 5, executor setScissorRect |
| 0x5BADA0 | Phyre_PRenderer_SetClearColor | kind 6, four floats |
| 0x5BB130 | Phyre_PRenderer_SetColorTarget | kind 11, slot below 4 |
| 0x5BB5B0 | Phyre_PRenderer_SetDepthTarget | kind 12 |
| 0x5B5E30 | Phyre_PRenderer_BeginScene | kind 19, sets renderer+4 bit 0 |
| 0x5B6330 | Phyre_PRenderer_Clear | kind 20, needs an open scene |
| 0x5B6950 | Phyre_PRenderer_EndScene | kind 21 |
| 0x5B6AF0 | Phyre_PRenderer_QueuePresent | kind 22, executor presents |
| 0x613E10 | Phyre_CommandRing_Alloc | takes n bytes, wraps, 0 when full |
| 0x5B35C0 | Phyre_RenderTargetDesc_Init | builds {target, 0, arg, 0} |
| 0x668930 | FFX_Frame_RenderScene | the presenter, every pass in RENDER_PASSES.md |
| 0x654650 | FFX_GfxCtx_SortMergeDrawQueues | section 3.3 |
| 0x650670 | FFX_GfxCtx_DrawOrderedQueue | section 3.4 |
| 0x666E40 | FFX_GfxCtx_ExecPassCommand | switch on kinds 0..6 |
| 0x656940 | FFX_GfxCtx_PushPassCommand | kind range check, drawIndex, counter |
| 0x6564A0 | FFX_GfxCtx_PushLayerBreak | kind 0 plus a batch mark |
| 0x6568A0 | FFX_GfxCtx_PushCopyCommand | kind 1 |
| 0x656C10 | FFX_GfxCtx_AppendDrawItem | ordered or sorted list |
| 0x645840 | FFX_GfxCtx_MarkDrawBatch | writes the two mark arrays |
| 0x659000 | FFX_GfxCtx_ClearPassCommands | zeroes the +0xFB54 count, counters, +0xFB48 |
| 0x65A620 | FFX_GfxCtx_ReleaseDeferredDrawItems | drains +0x3890 after the submit |
| 0x656780 | FFX_Gfx_PushModelMatrixOverride | 68-byte record into +0x10D88 |
| 0x66B360 | FFX_Gfx_ApplyModelMatrixOverrides | negates X and Y rows, renders, restores |
| 0x6589E0 | FFX_GfxCtx_LinkMeshGroupLayer | links one layer, unlinks the rest |
| 0x66C770 | FFX_GfxCtx_LinkAllMeshGroups | relinks every group |
| 0x6ADE20 | RenderDelegationHelper_Add | 20-byte record into `dword_CCCA98` |
| 0x6AE450 | RenderDelegationHelper_RunFades | mask 0x1000, stores the target |
| 0x6AE650 | FFX_Gfx_QueueFadeQuad | 0x20 payload, callback `sub_6AE5B0` |
| 0x6A52F0 | VFXDynamicGeometry__submit | vf02 mode router |
| 0x6A53E0 | VFXDynamicGeometry__prepareConstants | vf10 |
| 0x6458A0 | FFX_Vfx_SubmitModel | mode 0, slot 775 |
| 0x645BA0 | FFX_Vfx_SubmitModelMode | mode argument, slot 865 |
| 0x645750 | FFX_Vfx_SetSortDepth | writes item+0xDC |
| 0x644DE0 | FFX_Vfx_CreateModel | transform defaults, slot 881 |
| 0x71ED10 | FFX_Vfx_PrimBuildRunSlots | kind switch over 532-byte slots |
| 0x728660 | FFX_Vfx_PrimBuildAllocSlot | cheapest bucket under 342 |
| 0x728750 | FFX_Vfx_PrimBuildInitBuckets | six `FFX_Malloc(0x2C6B8)` |
| 0x67BA50 | FFX_DynDrawList_Append | reads the +23420 toggle |
| 0x63EEE0 | FFX_Sprite2D_Draw | 92 callers, "NoTexture" |

## 11. Unresolved

- What content each cluster list +124, +140, +156, +172, +188 holds. `sub_657A50` initialises them
  and `sub_634850` clears them, and no populator was found by offset scan. Map in +124 and
  characters in +172 is a guess.
- The payload of PRenderer draw records kinds 1, 2 and 3, and the meaning of kinds 10 and 30.
- The exact wait semantics of renderThread+0x164 and renderer+0x44, so how far the main thread can
  run ahead of the render thread.
- Submit mode 3 is not routed by `sub_6A52F0`. Either it is dropped or a subclass handles it.
- The 2D list record layout.
- The magic id sets in `sub_6721B0` beyond the ones listed in 3.3.
