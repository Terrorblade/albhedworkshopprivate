# Dynamic meshes and ClassDynamicGeometry

PROGRESS 1.5.8. Every PhyreEngine dynamic mesh the Virtuos PC layer adds, plus the
`ClassDynamicGeometry` family that wraps FFX geometry into Phyre draws. For each class: vtable, ctor,
what it draws, vertex format, who creates it, who updates it, step or present, the pass it draws
in, and the PORTING_RULES F8 GPU note.

Marks: [V] read in Hex-Rays or disassembly, [I] inferred. VAs at the preferred base 0x400000. IDA
was read only for this note. Proposed names are in section 11.

## 1. The count

MAP.md says "7 dynamic mesh classes". RTTI settles it. A scan of every complete object locator in
.rdata for a class hierarchy that contains `PDynamicMesh` (type descriptor 0xC1037C) finds Phyre's
own `PModifierNetworkDynamicMesh` and exactly seven game side classes [V]:

| class | vtable | size | real ctor | instance class | instance vtable | instance size |
|---|---|---|---|---|---|---|
| `ClassDynamicMesh` | 0xB43ED8 | 0x1C | `sub_672630` | `ClassDynamicMeshInstance` | 0xB44128 | 0x10 |
| `DistortionGridDynamicMesh` | 0xB44288 | 0x1C | 0x673F80 (not a function in the IDB) | `DistortionGridDynamicMeshInstance` | 0xB44460 | 0xC |
| `RadialLineDynamicMesh` | 0xB44740 | 0x20 | 0x675EF0 | `RadialLineDynamicMeshInstance` | 0xB445D0 | 0xC |
| `BrokenScreenPolygonDynamicMesh` | 0xB44A90 | 0x20 | 0x677550 | `BrokenScreenPolygonDynamicMeshInstance` | 0xB44910 | 0xC |
| `ShadowDynamicMesh` | 0xB44C78 | 0x1EC | 0x6782B0 | `ShadowDynamicMeshInstance` | 0xB4C250 | 0xC |
| `Phyre::ClothInstancingDynamicMesh` | 0xB4AA10 | 0x28 | 0x6E0830 (inlined in `sub_6E2600`) | `Phyre::ClothInstancingDynamicMeshInstance` | 0xB4AB28 | 0x2010 |
| `Phyre::DynamicMeshDefaultImplmenetation` (sic) | 0xB4A8F0 | 0x1C | 0x6E09C0 | none | | |

Sizes are the `PClassDescriptor__Init` arguments. The functions IDA lists as vtable xrefs next to
the ctors (`sub_672680`, `sub_673FD0`, `sub_675F40`, `sub_6775A0`, `sub_678300`, `sub_6E0AA0`,
`sub_6E0B40`) are destructor bodies: they restore the vtable and call `sub_4969F0`. `sub_673730`,
`sub_674D30`, `sub_675650`, `sub_676CB0` and `sub_6F95D0` have no references, each is an unused copy
of an instance descriptor create. [V]

`ClassDynamicGeometry` is not a dynamic mesh. Its RTTI has no Phyre base. It and its three
subclasses (`VFXDynamicGeometry`, `GuildMapDynamicGeometry`, `MenuDynamicGeometry`) own a
`ClassDynamicMesh` each and are covered in section 9. With the six Instance twins and Phyre's
`DynamicMeshDefaultImplmenetation`, that is the 26 class family MAP.md lists. [V]

## 2. The shared machinery

PDynamicMesh vtable slots in this 3.9 build, from the binary (the base vtable 0xB16868 has
`_purecall` in slots 1, 5, 6 and 7) [V]:

| slot | role |
|---|---|
| 0 | scalar deleting dtor |
| 1 | createInstance(cluster, existing). Every override returns `existing` when it is not null |
| 2 | prepare, `0x498D20`. Calls slot 5 and stores the returned job handle |
| 3 | draw, `0x498500` (indexed) |
| 4 | drawInstanced, `0x4985B0` |
| 5 | fillDynamicBlocks(segment, preprocess, dataBlocks, indexBlock), returns a job handle |
| 6 | fillDynamicBlocks with a gate: slot 5, then `*gate[0] = gate[1]` |
| 7 | sync |

PDynamicMeshInstance slots: 0 dtor, 1 getPreprocessDataSize, 2 populatePreprocessData(block, ctx).
Base ctor `sub_4FA420` (+4 mesh, +8 zero). PDynamicMesh ctor `sub_4968C0`: +8/+12 stream
descriptors `{0, semantic*, type byte}` 12 bytes each, +16/+20 segment descriptors
`{0, streamCount, vertexCount, indexCount}` 16 bytes each. [V]

Who calls them [V]:
- Instance slots 1 and 2 from `PRenderer__captureDrawBuffers 0x5B5A50`, inside
  `PRenderer__drawMeshInstance`. That is record time, on the thread running `FFX_Frame_RenderScene`.
- Mesh slot 2 (then 5) on the Phyre render thread: `Phyre_RenderThread_ExecuteList 0x615550` ->
  `sub_6148D0` (the "issued job count overflow. Dynamic mesh render error will occur" site in
  THREADS_AND_CLOCKS.md).

Every populate below stores only pointers to CPU arrays. The fill memcpys from those arrays later,
on the render thread. So whatever writes the arrays races the render thread unless the writer is
double buffered. `ClassDynamicMesh` is (the DynGeoMem set). Distortion, RadialLine and BrokenScreen
are not. [V for the pointer capture, I for the race in practice]

Vertex semantics. The globals at 0xC985B4.. are filled by CRT initialisers at 0xAE1A10..0xAE1EF0
(IDA shows 0xFF). The ones used here: `0xC985B4` ScreenVertex, `0xC98620` Vertex, `0xC9862C`
Normal, `0xC98638` ST, `0xC98650` Color. Type bytes: 1 float2, 2 float3, 3 float4 [V names, I type
bytes from the copy strides]. Index type 8 is u32 [I].

## 3. ClassDynamicMesh

- Creates: only `ClassDynamicGeometry__vf01 0x6A46E0`, through `sub_6594C0` into geometry+8, and the
  instance through `sub_6595D0` into geometry+16. `ClassDynamicGeometry__vf09 0x6A3B40` sets
  instance+12 back to the geometry. The gfx context pools hold 3000 of each (meshes 28 bytes at
  gfx+0xFBA4, instances 16 bytes at gfx+0xFBB8) when the owner pool is gfx+0x12D4, otherwise the
  Phyre descriptor allocates. [V]
- Format: `sub_673030` (from geometry vf08) builds four streams, Vertex float3, Normal float3, Color
  float4, ST float2, and one segment per geometry part `{0, 4, part vertex count, part index
  count}`. Indices are u16 in the part, widened to u32 by the fill. [V]
- Populate `sub_673B20` (size 8 from `sub_673AE0`): block+16 = the geometry record (geometry+36),
  block+20 = `sub_63FF50()`, the current DynGeoMem set. [V]
- Fill `sub_672B80`, gated on `dword_C32694`: per part, the per set pointers when part
  [7*set+13] is set, otherwise the base arrays, strides 12, 12, 16, 8. For an index type other
  than 8 it memcpys `indexCount` bytes, a latent byte count bug that type 8 never reaches. Zeroes
  the per set counts after. Error string "indexDataBlock == NULL in fillDynamicBlocks. VFX Setup
  error." Sync is `nullsub_519`. [V]
- Segment counts: `sub_6732A0`, from `ClassDynamicGeometry__vf02` and the GuildMap vf02, copies the
  per set vertex and index counts in, with a floor of 4 and 6. [V]
- Draws, updates and passes depend on the owning geometry class (section 9).
- GPU: the mesh itself is a transport. The DynGeoMem set double buffer separates the step writer
  from the render reader. [I]

## 4. DistortionGridDynamicMesh: the screen wave grid

- Owner: the Distortion singleton `dword_CDEB88` (0x80 bytes, ctor `sub_6C2010`, created by
  `sub_6C2190` from `FFX_GraphicInitialize`, getter `sub_6C2290` "Distortion singleton is not created
  yet."). `sub_6C2710` (from `sub_657A50` at 0x657BC8) loads `PhyreDistortionShader.cgfx.phyre`,
  then `sub_6C28B0` -> `sub_6C2520` creates the colour target "Distortion_colorTarget", the mesh
  (`sub_674740`, 81 vertices and 384 indices) and the instance (`sub_673E20`). [V]
- Owner fields: +8 target drawn into (the +140 colour target of the post manager [I]), +36/+40 the
  colour target, +44 ColorBuffer parameter, +48 material, +56 PMeshInstance, +60 mesh, +64 instance,
  +104 positions (0x3CC), +108 colours (0x510), +112 UVs (0x288), +116 indices (0x600), +120
  strength (default 0.75, clamped to 0.8 at draw), +124 enabled, +125 skip next draw, +126 fresh
  copy, +127 target ready. [V]
- Draws: a 9 by 9 vertex, 8 by 8 cell clip space grid over the screen (x and y from -1 to 1 in
  0.25 steps, z 1.0), triangle list, sampling the previous frame copy, so moving a vertex or its UV
  bends the picture. Built by `sub_6C2FF0` (`sub_6C2CA0` positions, `sub_6C28E0` colours 1.0,
  `sub_6C2BB0` UVs in 0.125 steps, `sub_6C2990` indices). [V]
- Format: ScreenVertex float3, Color float4, ST float2, u32 indices. [V]
- Populate `sub_675120` (24 bytes, the four array pointers, 81, 384), fill `sub_674570`. [V]
- Updated per step [V for the chains]:
  - `FFX_Distortion_SetGridVertex sub_644940` -> `sub_6C3240(row, col, rgba, u, v)`: vertex
    `9*row - col + 8`, v stored as 1 - v. From `FFX_Map_DrawScreenWave` -> `sub_9131D0` ->
    `sub_913200` in `FFX_MainStep`.
  - `sub_642F30` -> `sub_6C3230` strength, from `sub_913200`.
  - `sub_642F50` enable. A rising edge sets +126 and strength 0.25. Callers `sub_908450` (AtelSys
    483 and `FFX_Map_ReturnToTitle`) and `sub_9085A0` (AtelSys 482).
  - `0x642F80`, named `FFX_MainStep_SetStepSkippedFlag` in the IDB, only writes Distortion +125
    (skip next draw). Five sites in `FFX_MainStep` and `FFX_Map_DrawScreenWave`. The name is
    misleading.
  - No dt is read. Catch-up steps advance the wave phases (VISUAL_CENSUS.md).
- Pass: `sub_6C2DC0` at 0x66A494, post chain row 26 of RENDER_PASSES.md. If +125 is set it clears
  it, sets +126 and draws nothing this present. Otherwise it copies the scene colour into the colour
  target (a plain copy when +126 is set, else `sub_692E90` blended by strength, a feedback trail),
  then draws the grid with ColorBuffer = that target. The trail weight applies once per present,
  so its persistence depends on present rate. [V for the code, I for the visible effect]
- Read back: the array getters `sub_6C26F0`, `sub_6C26B0`, `sub_6C26E0`, `sub_6C26C0`,
  `sub_6C2700`, `sub_6C26D0` are referenced only by the populate. No step reads the grid. [V]
- GPU: candidate. The grid is step written data (81 vertices), the copy and feedback blend are GPU
  work already. The trail should be driven from snapshot time if the recreation presents at other
  rates.

## 5. RadialLineDynamicMesh: BlurEffect zoom lines

- Owner: an object at BlurEffect+28 (0x78 bytes, ctor `sub_6F7A30`, init `sub_6F7C10`, which names
  "RadialLine_colorTarget"). Shader from the BlurEffect cluster `PhyreBlur.cgfx.phyre`
  (`off_C343E8`), parameters GaussianBlurBufferSize, OutputScale, UvScaleBias, ColorBuffer. The
  BlurEffect itself: getter `sub_6C3580`, init `sub_6C38B0`, resources `sub_6C35D0`. [V]
- Owner fields: +40 mesh (mesh+28 points back at the owner), +44 instance, +48/+52 W and H, +80
  positions, +84 colours, +88 UVs, +92 UV targets (`sub_6F81C0`), +96 indices, +100 per line state
  32 bytes `{t, speed, ..., rgb, alpha}`. [V]
- Draws: one line per border pixel of the screen, from the centre (0, 0, 1) to the border. W
  along the top, H down the right, W along the bottom, H up the left, with W and H the screen size
  at init (gfx+0x10DD0 and +0x10DD4, pushed at 0x657BE7). That is 4*(W+H) vertices with identity
  u32 indices, line list. Built by `sub_6F8C50` (`sub_6F85A0` positions). The count is fixed at
  init, a resize (`sub_6F89B0`) only changes the target. [V]
- Format: ScreenVertex float3, Color float4, ST float2 (`sub_676680` init). [V]
- Populate `sub_675A90` through the getters 0x6F7BF0, 0x6F7BB0, 0x6F7BE0, 0x6F7BC0, 0x6F7C00,
  0x6F7BD0, fill `sub_6764B0`. [V]
- Updated by `sub_6F8F00` (animate): per line `t += speed` unless paused, alpha from t, colours
  written to +84, UVs lerped toward +92. Speeds come from CRT `rand()` at init in `sub_6F8110`
  (THREADS_AND_CLOCKS.md R10). No dt, so one advance per call. Chain: `sub_6C3FD0` <- `sub_6449D0` <-
  `sub_7F8E80` (op 13 of `sub_7F7E30`, which is entry 132 of `g_ffxOef2SubCommandTable` 0xC48EC8,
  the magic program dispatcher). The BlurEffect start is op 12 -> `sub_7F9280` -> `sub_639140`. [V for
  the chain, I that the magic program runs on the step side, so catch-up steps advance it too]
- Pass: BlurEffect `sub_6C3970` at 0x66A463, post chain row 26. It ping-pongs the front and back
  buffers, runs the gaussian, pushes colour and scale from BlurEffect +76 and +116 (the per present
  phase `sub_6C3D40`), and `sub_6F8850` copies the accumulate buffer into the colour target and draws
  the lines into the BlurEffect front buffer. [V]
- Read back: none. The getters serve only the populate, the animate reads only its own state. [V]
- GPU: strong candidate. Thousands of lines, each a pure function of its t and speed. Keep t and
  speed as step state (they come from `rand()` at init, which the recreation must own) and build the
  lines in a vertex shader.

## 6. BrokenScreenPolygonDynamicMesh: glass shards

- Owner: the BrokenScreen manager `dword_CDEC98` (0x78 bytes, ctor `sub_6D4BA0`, create `sub_6D4D30`,
  getter `sub_6D4DE0`, init `sub_6D51D0` from `sub_657A50` at 0x657CBB). Capture texture
  "BrokenScreen_capturedFrame" (`sub_6D5050`). Shard pool `sub_6D54C0`: +52 count, +56 array of
  16 byte `{face polygon, specular polygon}`. Spawn `sub_6D55B0`. [V]
- Shard polygon object, 0xB0 bytes, ctor `sub_6D3560(mode)`: mode 1 face (colour 1.0), mode 2
  specular edge layer (colour 0.2). +8 vertex count 6 or 8, indices 24 or 36 (double sided,
  2*(n-2) triangles, `sub_6D41B0`), arrays carved by `sub_6D4780`: +64 positions, +68 normals (CPU
  only), +72 colours, +76 UVs, +80 indices. +96..+156 a rotation matrix. Init `sub_6D3FB0` creates
  the mesh with `sub_677CE0(owner, n, indexCount)`, mesh+28 points back at the polygon. [V]
- Draws: the screen breaking into glass. A captured frame on a background mesh, then each shard
  as a textured polygon with a face layer and a specular layer. [V for the structure, I for the
  look]
- Format: ScreenVertex float3, Color float4, ST float2, u32 indices, triangle list. [V]
- Populate `sub_6770F0` (getters 0x6D3F90, 0x6D3F30, 0x6D3F80, 0x6D3F40, 0x6D3FA0, 0x6D3F50), fill
  `sub_677B10`. [V]
- Updated per step by the magic program, through the step wrappers `sub_639450` (pool),
  `sub_639470` (spawn), `sub_639500` -> `sub_6D49F0` (vertex: position, normal, UV), `sub_639540` ->
  `sub_6D4A90` (rotation), `sub_6394B0` (light), and the flag setters `sub_639360` (+5),
  `sub_639370` (+4), `sub_639390` (+6). All are called from the Oef2 sub 132 helpers `sub_7F8930`
  (op 8), `sub_7F8570` (op 9), `sub_7F75E0` and `sub_7F7A40` (ops 10 and 11, registered as tasks
  through `sub_7EB410`). [V for the calls, I that they run on the step side]
- Per present: `sub_6D3650` (from `sub_6D5680`) overwrites the colours +72 with per vertex
  lighting, diffuse for mode 1 and a pow 4 specular for mode 2. Alpha is 1 only inside -1..1 with
  z >= 0. Stateless. [V]
- Pass: capture `sub_6D4E20` at 0x66898B (render prologue), which clears +4 and sets +8 = 1.0. Draw
  `sub_6D5680` at 0x66A3CE (post chain row 26): background mesh +88 with depth, then face polygons,
  then specular polygons through `sub_6D44C0`. It clears +5 when +6 is 1. [V]
- Read back: `sub_6D3E90` (get vertex) is reachable only from the stub 0x639410, which has no code
  or data reference. Dead. No step reads the render written colours. [V]
- GPU: candidate for the lighting and the shard transforms. The flags and the shard spawn data are
  step requests and stay on the CPU (VISUAL_CENSUS.md already lists them).

## 7. ShadowDynamicMesh: the shadow atlas fade quads

- Geometry inline in the 0x1EC object: 8 vertices, 12 u32 indices, two screen space quads. +28
  positions (8 float3), +124 colours (8 float4, zeroed), +252 Normal float3 used as a gradient
  parameter (1, t, 1), +380 ST float2, +444 indices. Init `sub_678BD0(8, 12)`: streams ScreenVertex
  float3, Color float4, ST float2, Normal float3, triangle list. Builders `sub_678E60` and
  `sub_6790C0`: quads span x -1..1, split at y = 2t - 1, z 0.5, modes 1, 2, 3 pick the fade factors.
  [V]
- Populate `sub_6F9A30` (28 bytes) points straight into the mesh object. Fill `sub_6789F0`. [V]
- Owner: the shadow manager (singleton behind `sub_6D2390`). `sub_6D1B40` (from `sub_6CE100`, from
  `FFX_Boot_FirstAsyncLoad`) creates 32 meshes through `sub_6C9580`, two sets of 16, mesh instances
  at +3268, meshes at +3396. [V]
- Updated and drawn per present, with no clock: `sub_6C9E50` -> `sub_6D12F0` (twice) and
  `sub_6D1530` take a mesh from `sub_6CC980` ("GetASpareGradMeshInstance()") and flip the set
  `dword_CDEC24`. Thresholds `flt_CDEC2C`, 0.95 and `flt_CDEC34` scaled by (`dword_C34408` - 64).
  `sub_6C9E50` is called by `sub_653600`, which `FFX_Frame_RenderScene` calls at 0x668EDE
  (RENDER_PASSES.md row 1, "role not read") and the FMV route reaches at 0x653A98. [V]
- What it draws: fade gradients in the character shadow atlas. `sub_6C9E50` renders the atlas in
  tiles with a 32 pixel border and GaussianBlurBufferSize. So RENDER_PASSES.md row 1 is the shadow
  atlas render. [I, strong]
- The step side `sub_644A00` -> `sub_6D3000` (from the blob shadow `sub_82E3E0`) touches the shadow
  manager's tile table, not these meshes. Whether that table is a step and render shared state is
  open. [V for the call, I for the question]
- Read back: none. GPU: render only already, trivially small.

## 8. ClothInstancingDynamicMesh and DynamicMeshDefaultImplmenetation

- `ClothInstancingDynamicMesh` is a decorator. +28 the inner mesh (the original mesh of the segment,
  or a `DynamicMeshDefaultImplmenetation` stub), +32 the Color semantic, +36 type 3 (float4). [V]
- Installed by `sub_6E2600` from `sub_6A1320` from `ClassCharacter__createInstance 0x63C540` (at
  0x63C65F), once per mesh instance when any segment of its mesh has m_clothmodelIndex and m_clothIndex
  (+32, +36) both not -1 (0x6A1364, 0x6A136A, see CHR_CLUSTER.md 4). The
  callers of `createInstance` are `FFX_Ch_Allocate` (battle and field spawns) and
  `FFX_Chr_ProcessPendingAttachments` from `FFX_MainStep`, so installation is on the step. Binding
  object `sub_6E0800`, 20 bytes. [V]
- Fill (slot 5, 0x6E2150 -> `sub_6E2000`): memcpy the cloth buffer into the data block whose
  semantic is Color, mark the overridden streams, then run the inner fill. Returns the preprocess
  pointer as the job handle. Sync (slot 7, 0x6E2DF0) spins on a done flag. [V]
- `DynamicMeshDefaultImplmenetation`: slot 1 returns 0, slot 5 returns its argument, slot 6
  signals the gate, slot 7 clears. A do nothing inner mesh. [V]
- Instance, 0x2010 bytes, 128 segment slots. Slot 1 returns the inner size + 8. Slot 2 does
  `InterlockedCompareExchange(dirty[+0x810 + 4i], 0, 1)` and when it was 1 memcpys staging
  (+0xC10[i], size +0x1810[i]) into the render copy (+0x210[i]), then runs the inner populate. The
  setter of the dirty flag was not found. [V]
- Updated per present: `sub_6365F0` from `FFX_Chr_StepClonedCharacterList 0x63B740` from
  `FFXApplication__render` sets "ClothWorld" and calls `sub_6E2AC0` (staging = cloth object +76,
  size 16 * object +68) and `sub_6E2B10`. The vertex payload is the cloth simulation output as one
  float4 per vertex on the Color stream, for the `CLOTH_SYSTEM_ENABLED` character shaders. [V]
- Pass: the normal character opaque and transparent draws, cluster list +124. [I]
- Read back: none (READBACK_CENSUS.md). GPU: candidate, cloth is named in F8 already.

## 9. ClassDynamicGeometry and its three subclasses

A plain C++ wrapper that turns one FFX geometry record into the four Phyre objects a draw needs: a
material array, a `ClassDynamicMesh`, a `PMeshInstance` and a `ClassDynamicMeshInstance`. Each is
taken from fixed pools of 3000 in the gfx context (`sub_659860`, `sub_659910`, `sub_6594C0`,
`sub_6595D0`, `sub_6599C0`, all `this == gfx`, falling back to a Phyre allocation when the pool
argument is not gfx+0x12D4). [V]

Object: 60 bytes for the base, GuildMap and Menu (`FFX_Alloc(0x3C)` in `sub_69F3A0`,
`sub_681970`, `sub_680970`, 0x40 in `sub_641510`), 248 for `VFXDynamicGeometry` (pool of 3000 in
`sub_69E140`). [V]

| offset | what | written by |
|---|---|---|
| +0 | vtable | ctor |
| +4 | material set, 56 byte pool object (`sub_433700`) | vf01 via `sub_659910` |
| +8 | `ClassDynamicMesh*` | vf01 via `sub_6594C0` |
| +12 | `PMeshInstance*`, 132 byte pool object (`sub_4F27E0`) with a PWorldMatrix from a 48 byte pool | vf01 via `sub_6599C0` |
| +16 | `ClassDynamicMeshInstance*` | vf01 via `sub_6595D0` |
| +20 | `PArray<PMaterial*>`, 8 byte pool object | vf01 via `sub_659860` |
| +24, +28 | per material arrays, inline at +49 and +53 when there is one material | vf06 |
| +32 | the effect from the VFX shader table `sub_6C06E0(dword_CDEB80, name, flags)` | subclass vf01 |
| +36 | the geometry record (the 208 byte "transform record" of DISPLAY_LIST.md section 7) | creator |
| +40 | technique or pass found by vf06 | vf06 |
| +44 | owner pool, gfx+0x1284 (2D) or gfx+0x12D4 (default) | vf01 |
| +48 | byte, built | vf01, vf02 |
| +60 | subclasses: the shader flag word fed to `sub_6C06E0` | creator |

Geometry record parts: record +144 is the part count, +148 an array of 108 byte parts. Filled by
`sub_6A0100` (blob shadow) and `sub_645040` (VFX models, host slot 882) [V]:

| part offset | what |
|---|---|
| +4, +8 | vertex count, index count |
| +12 | positions, float3 |
| +16 | normals, float3 |
| +20 | colours, float4 |
| +24 | texture coordinates, float2 |
| +28 | indices, u16 |
| +32 | texture name (`char*`: "Shadowmap", "NoTexture", or a VFX texture path) |
| +36 | sampler state index for `sub_65E050` |
| +44 + 28*set | per DynGeoMem set `{vertex count +44, index count +48, set pointers valid +52}`, set = `sub_63FF50()` |
| +100, +104 | capacity, vertices and indices |

Vtable (slots 3, 5, 7, 8, 9 are shared by all four) [V]:

| slot | base 0xB47E14 | meaning |
|---|---|---|
| 0 | `0x69EF00` | scalar deleting dtor, release through `sub_6A1040` |
| 1 | `0x6A46E0` | create(effect, pool): material array, materials (slot 6), mesh (slot 5), dynamic mesh (slot 8), instance (slot 9), mesh instance (slot 7) |
| 2 | `0x6A4A50` | submit(mode): segment counts for the current set (`sub_6732A0`), `FFX_Gfx_SetMeshTransform`, bounds, a first time texture bind, then zCrct, zMatrix, ScreenShift, MaterialColour, doCameraFadeout, cameraFadeoutFactor |
| 3 | `0x6A59D0` | rebind textures: TextureSampler, TextureSamplerState, UvScaleBias, TextureSamplerY for a movie texture |
| 4 | `0x6A0F30` | release the mesh instance (`sub_656B90`) |
| 5 | `0x6A44D0` | build the PMeshSegment array, 108 bytes each, one per part |
| 6 | `0x6A3B50` | build the materials: TextureSampler, TextureSamplerState, TargetTexture, UvScaleBias, CCTexture, FrameBuffer |
| 7 | `0x6A46C0` | point the mesh instance at the material array |
| 8 | `0x6A3B30` | `sub_673030`, the ClassDynamicMesh streams and segments |
| 9 | `0x6A3B40` | instance back pointer |
| 10 | VFX only, `0x6A53E0` | prepare constants |

The vertex format is the ClassDynamicMesh one of section 3. The type bytes match float2, float3 and
float4 by the buffer sizes `sub_6A0100` allocates for 500 vertices (6000, 6000, 8000, 4000 bytes)
and 1500 indices (3000 bytes). [V sizes, I mapping]

### 9.1 ClassDynamicGeometry itself: the character blob shadow

The only bare `ClassDynamicGeometry` is built at boot by `sub_69F3A0` (`FFX_Boot_FirstAsyncLoad`
at 0x6364E5) into `dword_CCCA74`, a 20 byte holder: two geometry records (one per DynGeoMem set,
`sub_6A0100`, texture "Shadowmap", 500 vertices and 1500 indices each), the geometry object at +8,
a built byte at +12, the current set at +16. [V]

- Draws: one quad per character blob shadow, colour (0, 0, 0, 1), corners at half width and half
  depth around the CHR at its ground height, transformed by the CHR matrix (`sub_6ED840`). [V]
- Updated per step, step thread [V]:
  - `FFX_ChrShadow_ResetBlobBuffer 0x643DC0` at 0x82F1FA in `FFX_Ch_StepAll` picks the set and
    clears it.
  - `sub_82E3E0` -> `FFX_ChrShadow_WriteBlobQuad 0x831D10`, 4 vertices and 6 indices per CHR.
  - `FFX_ChrShadow_SubmitBlobMesh 0x643DD0` at 0x82F837. The first time, `sub_69F9D0` runs vf01
    with the shader at gfx+64124, binds the shadow texture from `sub_6D2390`, and links the mesh
    instance into the render graph (`FFX_Gfx_LinkMeshToNode(mesh, 1)`). Every step it calls
    vf02(0).
- Pass: the scene graph, because the mesh instance stays linked to its node. Base vf02 appends to
  no gfx queue. [V]
- Released by `sub_69F540` from `FFX__graphicFinalize 0x640500`. [V]
- GPU: candidate, four vertices per CHR from the interpolated CHR position (VISUAL_CENSUS.md row
  "blob shadow quad"). Not step visible.

### 9.2 VFXDynamicGeometry: effect models

DISPLAY_LIST.md section 7 has the layout and submit modes. Added here [V]:

- ctor `sub_69E3E0`, vtable 0xB47E40. vf01 `0x6A48D0` keeps what follows "GL/" in the first part's
  texture name and looks the effect up by name and flags (+60), retrying with only the 0x8 bit, then
  runs the base vf01 with pool gfx+0x12D4. vf04 `0x6A0F50`, vf06 `0x6A4240` and vf10 `0x6A53E0` are
  the overrides.
- Created: the pool of 3000 in `sub_69E140` ("vfxdg pool"), handed out by `sub_69F750`, keyed by
  u64 in `dword_CCCA6C`. `FFX_Vfx_CreateModel 0x644DE0` (host slot 881), `sub_645040` (slot 882,
  parts, 43 call sites across the VFX builders 0x712EC0..0x716110 and the battle and menu effect
  code), `sub_645780` (vf01 over every part, callers 0x66FB70 and 0x710FB5..0x7113D0).
- Updated per step: the vertex data comes from the VFX prim build jobs
  (`FFX_Vfx_PrimBuildRunSlots 0x71ED10`, DISPLAY_LIST.md section 8) into DynGeoMem frame segments.
  Submission through `FFX_Vfx_SubmitModelMode 0x645BA0` and its wrappers calls vf02 virtually and
  skips catch-up steps.
- Pass: the sorted and ordered VFX queues (`FFX_GfxCtx_SortMergeDrawQueues 0x654650` at 0x669DD9,
  `FFX_GfxCtx_DrawOrderedQueue 0x650670` at 0x669EAB) and the mode 1, 2 and 4 lists.
- GPU: the prim builds are already named in F8. Submission membership is step output.

### 9.3 GuildMapDynamicGeometry: the field guide map

"GuildMap" is the binary's spelling of guide map. ctor `sub_69E2B0`, vtable 0xB47E70. [V]

- Draws: the minimap. Untextured ("NoTexture") triangle lists, one part per guide strip, at most
  30 parts (`sub_7133E0` stops at 0x1E), from the rcbg guide model (20 byte source vertices). vf01
  `0x6A4800` sets gfx+63942 for events 266, 223, 205 and 221, which selects a second render state
  set in the 2D pass. vf02 `0x6A5070` writes OffsetX and OffsetY (fixed pixel offsets
  `flt_C34180..flt_C34198`, mirrored by the gfx+116 screen mode, other values for events 102 and
  223), then appends the mesh instance to gfx+0x386C with `sub_656A00`. vf02 ignores its mode. [V]
- Created: `sub_641510` (a 0xD0 record and a 0x40 object, key in `dword_CCCA6C`) from `sub_7133E0`
  <- `sub_710ED0` <- `sub_928020` <- `sub_91DA80` and `sub_928320` (rcbg). [V]
- Updated per step: `FFX_MainStep` at 0x821525 -> `FFX_Field_DrawGuideMapThunk` ->
  `FFX_Field_DrawGuideMap` (0x872D0E) -> `sub_919D00` -> `sub_91DA80` -> `sub_928020`, which builds
  (`sub_710ED0`) or refills the vertices, bounds and matrix (`sub_717620`) and submits with
  `sub_6414F0`, mode 3. [V]
- Pass: the 2D pass `sub_650340` from `FFX_Frame_RenderScene` at 0x66AC18, after the 2D list items
  whose record +180 is 1, with camera gfx+1620, frustum tested per item by
  `FFX_Gfx_TestObbCornersAgainstFrustum`. [V]
- GPU: display only. The vertex build is a few hundred vertices per step, not worth moving. The
  map position it reflects is step state.

### 9.4 MenuDynamicGeometry: 2D batches and menu shapes

ctor `sub_69E320`, vtable 0xB47E9C, no overrides. [V]

- Draws 2D quads, for two owners [V]:
  1. The 2D list `dword_CCC81C`. `sub_67E6C0` makes, per frame set, 600 "Single TextureName"
     objects (one quad, 4 vertices and 6 indices) and 800 "Multi TextureName" objects (12 quads, 48
     vertices and 72 indices) through `sub_67D760`, inline in the manager (60 byte stride from
     +23536, count +191536). Each step `sub_67E7F0` gives every list record a pooled object (record
     +176 picks multi) and `sub_67D920` binds it.
  2. The menu shapes. `sub_680970` (built by `FFX_MenuSys_CreateTexLoad` at 0x684AAC, from
     `sub_638F90` <- `FFX_Menu_Build`) creates fixed objects for 14 shape kinds (`sub_681C10`)
     through `sub_681970`, with the shader from gfx+64112..+64148 by kind.
- Updated per step: `FFX_Glue_RefreshVfxTextureBindings` (`FFX_MainStep` 0x8215EF on non catch-up
  steps, `FFX_MenuSys_StepFrame` 0x8A9FBD) builds the 2D pools once and then runs `sub_67E7F0` every
  step. The appends are `sub_67BA50` from `FFX_Sprite2D_Draw 0x63EEE0` and `sub_640660`. The IDB
  name says VFX texture bindings, but the objects it builds are these 2D batch geometries. [V]
- Pass: the 2D pass, `sub_650CB0` and `sub_650340` (DISPLAY_LIST.md section 8). [V]
- GPU: the 2D list is rebuilt per step and has no identity (VISUAL_CENSUS.md finding 6). Already
  GPU drawn, nothing to move.

## 10. Summary for F8

| class | data written by | rate | drawn in | step reads it | GPU candidate |
|---|---|---|---|---|---|
| ClassDynamicMesh | its geometry owner | as the owner | as the owner | no | transport only |
| DistortionGrid | screen wave, step | step | post chain 26, 0x66A494 | no | yes |
| RadialLine | magic program, step [I] | per call, no dt | post chain 26, 0x66A463 | no | yes, strong |
| BrokenScreenPolygon | magic program, step [I], lighting per present | step and present | prologue 0x66898B, post chain 26 0x66A3CE | no | yes, lighting and transforms |
| ShadowDynamicMesh | shadow atlas render | present | render row 1, 0x668EDE | no | already render only |
| ClothInstancing | cloth simulation | present | character draws [I] | no | yes |
| DefaultImpl | nothing | | | | n/a |
| blob shadow (ClassDynamicGeometry) | `FFX_Ch_StepAll` | step | scene graph | no | yes |
| VFXDynamicGeometry | VFX prim build | step | VFX queues | no | yes (prim build) |
| GuildMapDynamicGeometry | guide map | step | 2D pass 0x66AC18 | no | not worth it |
| MenuDynamicGeometry | 2D list | step | 2D pass | no | already GPU drawn |

None of the dynamic meshes is read back by a step. The step state that drives them (the Distortion
flags, the BrokenScreen requests, the RadialLine t and speed, the guide map position, the CHR
positions) stays on the CPU.

## 11. Proposed names

| VA | name | evidence |
|---|---|---|
| 0x498D20 | PDynamicMesh__prepare | calls slot 5, writes the job handle |
| 0x498500 | PDynamicMesh__draw | indexed draw |
| 0x4985B0 | PDynamicMesh__drawInstanced | instanced indexed draw |
| 0x4968C0 | PDynamicMesh__ctor | stream and segment arrays |
| 0x4969F0 | PDynamicMesh__dtorBody | called by every subclass dtor body |
| 0x4FA420 | PDynamicMeshInstance__ctor | +4 mesh, +8 zero |
| 0x4FA980 | PDynamicMeshInstance__getPreprocessDataSize | base, returns 0 |
| 0x4FA9C0 | PDynamicMeshInstance__populatePreprocessData | base, returns 0 |
| 0x672B30, 0x6744C0, 0x676400, 0x677A60, 0x678940 | ClassDynamicMesh, DistortionGridDynamicMesh, RadialLineDynamicMesh, BrokenScreenPolygonDynamicMesh, ShadowDynamicMesh __createInstance | slot 1 |
| 0x672B80, 0x674570, 0x6764B0, 0x677B10, 0x6789F0 | the same five __fillDynamicBlocks | slot 5, the stream memcpys |
| 0x672F20, 0x674650, 0x676590, 0x677BF0, 0x678B10 | the same five __fillDynamicBlocksGated | slot 6 |
| 0x673AE0, 0x6750E0, 0x675A40, 0x6770B0, 0x6F99C0 | the five instances __getPreprocessDataSize | 8, 24, 24, 24, 28 |
| 0x673B20, 0x675120, 0x675A90, 0x6770F0, 0x6F9A30 | the five instances __populatePreprocessData | array pointers into the block |
| 0x673030 | ClassDynamicMesh__setupStreams | 4 streams, one segment per part |
| 0x6732A0 | ClassDynamicMesh__updateSegmentCounts | per set counts into the segment descriptors |
| 0x6594C0 | FFX_GfxCtx_AllocClassDynamicMesh | 28 byte pool of 3000 |
| 0x6595D0 | FFX_GfxCtx_AllocClassDynamicMeshInstance | 16 byte pool of 3000 |
| 0x659860 | FFX_GfxCtx_AllocMaterialArray | 8 byte pool of 3000 |
| 0x659910 | FFX_GfxCtx_AllocMaterialSet | 56 byte pool, `sub_433700` |
| 0x6599C0 | FFX_GfxCtx_AllocMeshInstance | 132 byte PMeshInstance plus world matrix |
| 0x63FF50 | FFX_DynGeoMem_GetCurrentSet | returns `dword_CDEDF0`->+4 |
| 0x69E0C0 | ClassDynamicGeometry__ctor | vtable 0xB47E14 |
| 0x69E2B0 | GuildMapDynamicGeometry__ctor | vtable 0xB47E70, +60 = -1 |
| 0x69E320 | MenuDynamicGeometry__ctor | vtable 0xB47E9C |
| 0x69E3E0 | VFXDynamicGeometry__ctor | vtable 0xB47E40, defaults of DISPLAY_LIST section 7 |
| 0x6A1040 | ClassDynamicGeometry__release | frees the four pooled objects |
| 0x6A46E0 | ClassDynamicGeometry__create | slot 1 |
| 0x6A4A50 | ClassDynamicGeometry__submit | slot 2, the per draw constants |
| 0x6A59D0 | ClassDynamicGeometry__rebindTextures | slot 3 |
| 0x6A44D0 | ClassDynamicGeometry__buildSegments | slot 5 |
| 0x6A3B50 | ClassDynamicGeometry__buildMaterials | slot 6 |
| 0x6A48D0 | VFXDynamicGeometry__create | slot 1, "GL/" effect lookup |
| 0x6A4800 | GuildMapDynamicGeometry__create | slot 1, events 266, 223, 205, 221 |
| 0x6A5070 | GuildMapDynamicGeometry__submit | OffsetX, OffsetY, `sub_656A00` |
| 0x6C06E0 | FFX_VfxShaderTable_Find | name or flag lookup in `dword_CDEB80` |
| 0x69F3A0 | FFX_ChrShadow_CreateBlobMesh | builds `dword_CCCA74` at boot |
| 0x69F9D0 | FFX_ChrShadow_BuildBlobGeometry | vf01, "Shadowmap" bind, link to node |
| 0x6A0100 | FFX_ChrShadow_InitBlobRecords | two 208 byte records, 500 and 1500 capacity |
| 0x69F540 | FFX_ChrShadow_DestroyBlobMesh | from `FFX__graphicFinalize` |
| 0x645040 | FFX_Vfx_SetModelParts | host slot 882, 108 byte parts |
| 0x645780 | FFX_Vfx_BuildModelGeometry | vf01 over every part, "Have not create dynamic geometry for the vfx!" |
| 0x641510 | FFX_GuideMap_CreateGeometry | GuildMap object and record |
| 0x6414F0 | FFX_GuideMap_Submit | `FFX_Vfx_SubmitModelMode(key, 3)` |
| 0x7133E0 | FFX_GuideMap_BuildParts | "NoTexture" parts, at most 30 |
| 0x717620 | FFX_GuideMap_FillVertices | vertices, bounds, matrix |
| 0x656A00 | FFX_GfxCtx_AppendGuideMapMesh | unique append to gfx+0x386C |
| 0x650340 | FFX_GfxCtx_Draw2DMeshes | 2D list items and the guide map, 0x66AC18 |
| 0x67D760 | FFX_DynDrawList_CreateBatchGeometry | MenuDynamicGeometry, 1 or 12 quads |
| 0x67E6C0 | FFX_DynDrawList_CreateBatchPools | 600 single, 800 multi, per set |
| 0x67E7F0 | FFX_DynDrawList_AssignBatchGeometry | per step pool assignment |
| 0x6423B0 (`FFX_Glue_RefreshVfxTextureBindings`) | FFX_DynDrawList_StepBatchGeometry | rename: it builds and assigns 2D batch geometry, not VFX bindings |
| 0x680970 | FFX_MenuTexLoad_ctor | 14 shape kinds |
| 0x681970 | FFX_MenuTexLoad_CreateShapeGeometry | MenuDynamicGeometry by shader kind |
| 0x674740 | DistortionGridDynamicMesh__init | 81 vertices, 384 indices |
| 0x676680 | RadialLineDynamicMesh__init | (owner, n, n) |
| 0x677CE0 | BrokenScreenPolygonDynamicMesh__init | (owner, n, indexCount) |
| 0x678BD0 | ShadowDynamicMesh__init | (8, 12) |
| 0x6C2010, 0x6C2190, 0x6C2290 | Distortion__ctor, __createSingleton, __get | "Distortion singleton" |
| 0x6C2710, 0x6C28B0, 0x6C2520 | Distortion__init, __onShaderLoaded, __createResources | PhyreDistortionShader |
| 0x6C2FF0 | Distortion__buildGridMesh | `sub_6C2CA0`, `sub_6C28E0`, `sub_6C2BB0`, `sub_6C2990` |
| 0x6C2DC0 | Distortion__render | 0x66A494 |
| 0x6C3230, 0x6C3240 | Distortion__setStrength, __setGridVertex | +120, 9*row - col + 8 |
| 0x642F30, 0x642F50, 0x644940 | FFX_Distortion_SetStrength, _SetEnabled, _SetGridVertex | step wrappers |
| 0x642F80 | FFX_Distortion_SetSkipNextDraw | replaces `FFX_MainStep_SetStepSkippedFlag`, it only writes Distortion +125 |
| 0x673E20 | Distortion_CreateGridMeshInstance | |
| 0x6F7A30, 0x6F7C10 | RadialLine__ctor, __init | "RadialLine_colorTarget" |
| 0x6F8C50, 0x6F85A0 | RadialLine__buildMesh, __initPositions | |
| 0x6F8110 | RadialLine__initLineState | CRT `rand()` speeds |
| 0x6F8F00 | RadialLine__animate | t += speed |
| 0x6F8850, 0x6F89B0 | RadialLine__render, __resize | |
| 0x6C3580, 0x6C38B0, 0x6C35D0, 0x6C3970 | BlurEffect__get, __init, __createResources, __render | |
| 0x6C3FD0, 0x6449D0 | BlurEffect__animateRadialLines, FFX_BlurEffect_AnimateRadialLines | |
| 0x7F7E30, 0x7F8E80 | FFX_Oef2_Sub132_ScreenEffects, FFX_Oef2_ScreenFx_RadialLines | table 0xC48EC8 entry 132, op 13 |
| 0x6D3560, 0x6D3FB0, 0x6D4780, 0x6D41B0 | BrokenShardPoly__ctor, __init, __carveArrays, __initIndices | |
| 0x6D49F0, 0x6D4A90, 0x6D3650, 0x6D44C0 | BrokenShardPoly__setVertex, __setRotation, __computeLighting, __draw | |
| 0x6D3E90 | BrokenShardPoly__getVertex | dead |
| 0x6D4BA0, 0x6D4DE0, 0x6D51D0 | BrokenScreen__ctor, __get, __init | |
| 0x6D4E20, 0x6D5050, 0x6D5680 | BrokenScreen__captureFrame, __createCaptureTexture, __render | |
| 0x6D54C0, 0x6D55B0 | BrokenScreen__initShardPool, __spawnShard | |
| 0x639450, 0x639470, 0x639500, 0x639540 | FFX_BrokenScreen_InitShardPool, _SpawnShard, _SetShardVertex, _SetShardRotation | step wrappers |
| 0x6C9580 | FFX_Shadow_CreateGradMesh | |
| 0x6CC980 | FFX_Shadow_GetSpareGradMeshInstance | string |
| 0x6D12F0, 0x6D1530 | FFX_Shadow_BuildGradMeshes, FFX_Shadow_BuildGradMeshesB | |
| 0x678E60, 0x6790C0 | ShadowDynamicMesh__buildFadeQuads, __buildFadeQuadsB | |
| 0x6D1B40 | FFX_Shadow_InitResources | 32 grad meshes |
| 0x6C9E50 | FFX_Shadow_RenderAtlas | tiles with a 32 pixel border [I] |
| 0x6E2600 | Cloth_InstallInstancingMesh | m_clothmodelIndex, m_clothIndex |
| 0x6E2000 | ClothInstancingDynamicMesh__injectClothStream | Color stream memcpy |
| 0x6E2AC0, 0x6E2B10 | ClothInstancingDynamicMeshInstance__setSourceBuffer, __setClothObject | |
| 0x6A1320 | ClassCharacterInstance__addClothBinding | from `ClassCharacter__createInstance`, see CHR_CLUSTER.md 4 |

## 12. Open

- The setter of the cloth instance dirty flag and the undefined setters at 0x6E2B30,
  0x6E2BC0 and 0x6E2BE0. `sub_6E17F0` is not that setter, it is
  `ClothInstancingDynamicMesh__buildStreamDescs` (CHR_CLUSTER.md 4).
- Whether the step write versus render thread memcpy race on the Distortion, RadialLine and
  BrokenScreen arrays shows in practice. It depends on how far the main thread runs ahead of the
  render thread (THREADS_AND_CLOCKS.md).
- That the RadialLine and BrokenScreen magic program ops run on the step side, inferred from the
  Oef2 dispatcher, not traced to `FFX_MainStep`.
- The shadow manager tile table touched by the step through `sub_6D3000`.
- The PDataType ids behind the stream type bytes 1, 2, 3 and the index type 8, read from strides
  only.
