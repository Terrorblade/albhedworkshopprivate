# GPU and device read-back census

Project Zanarkand item 1.1.11. Written 2026-10-13 against the shipped FFX HD PC exe in IDA.
The question: does anything read from the D3D11 device, a GPU resource, the swap chain or a DXGI
object back into CPU memory, and if so does the simulation ever see the result (PORTING_RULES F2).

Tags used on every claim: **[V]** seen in Hex-Rays or the disassembly, **[I]** inferred.

Related docs: `RENDER_DEVICE.md` (device fields, Present, resize), `DISPLAY_LIST.md` (the render
delegation registry and pass commands), `RENDER_PASSES.md` (the 32 render steps and targets),
`SCREENS_AND_FADES.md` (the fade layers this census turned up), and `VISUAL_CENSUS.md` (another
agent's census of step-coupled render state, which this doc does not repeat).

Method: every `call [reg+disp]` and every `mov reg,[x+disp]` then `call reg` in .text (6346
indirect calls) was collected, and each base register was traced backwards to prove it came from
the D3D11 context, device or swap chain (`g_ffxPhyreD3DContext 0xC94F00`, device at +0x6C,
context at +0x70, swap chain at +0x74, and the Iggy GDraw context at `[dword_1A858B4]+4`). Map
type was read from the pushes (x86 COM pushes `this` last, so the order before the call is
pMapped, MapFlags, MapType, Subresource, pResource, this). Liveness was proved by walking callers
to a root or to a function with no references.

## 1. Answer in one paragraph

No D3D11 or DXGI read-back reaches the simulation. There are no GPU queries at all in the binary,
so there is no occlusion-query logic. The live read-backs are one vertex-buffer round trip at
character instance-ready time (feeds cloth, presentation only) and one back-buffer screenshot that
only the `-script=` InputLog harness can reach. Everything else that can read is dead code
(no references) or behind a debug gate that retail never sets. `FFX_Ch_UpdateCameLenAndZClip
0x82E220` does not read the GPU. The one real F2 problem found is not a D3D call at all. The
screen fade colour is renderer-owned state written at render time, and `FFX_MainStep` reads its
alpha to decide whether to re-arm a fade during a load hold. Details in section 2.

## 2. Simulation visible, listed first

### 2.1 FFX_MainStep reads the render-written fade alpha (F2 defect, not a GPU read)

- The fade colour lives in the post-process target manager (`dword_CCC8A0`, see
  `RENDER_PASSES.md`): RGBA floats at +0x1238C..+0x12398, blend mode at +0x1239C, a lock byte at
  +0x123A0, the load hold mode at +0x123A4 and an auto-clear flag at +0x123A8 **[V]** for the
  offsets used by the accessors and writers below.
- Render-time writers of that colour, each skipped while the lock byte is set **[V]**:
  - `sub_6AE5B0`, the callback of every 0x1000 fade quad queued through `FFX_Gfx_QueueFadeQuad
    0x6AE650`. It writes colour and blend, then draws.
  - `sub_638840`, run by `FFX_GfxCtx_ExecPassCommand` kind 2 at 0x66719B (the step queues the
    command through `sub_644D40`). The executor tests the lock at 0x667167.
  - `sub_698F70` in the presenter prologue zeroes blend and alpha when the auto-clear flag is 1.
  - `sub_637C50`, a render delegation callback that clears the colour.
- `FFX_MainStep` reads it **[V]**. Inside the block gated on `FFX_SaveUi_IsIdle()` and
  `!FFX_MainStep_IsFadeBlocking()`, while `FFX_ScreenFade_GetLoadHoldMode 0x6413E0` returns 1 or 2
  (calls at 0x820D1F and 0x820D29), it calls `FFX_ScreenFade_GetColor 0x641390` at 0x820D4F and,
  if alpha is not exactly 1.0, ends any active fade and begins a new one with the same RGB at
  alpha 1.0 and blend 0.
- `FFX_MainStep_IsFadeBlocking 0x81FCD0` (called at 0x820CE9) includes
  `!FFX_AsyncLoad_IsIdle() && FFX_ScreenFade_IsActive()` **[V]**. So during an async load, the
  fade state decides whether the whole gated block of `FFX_MainStep` runs.
- Why it matters: the alpha the step sees depends on which render passes ran since the last step.
  On a catch-up step no render ran in between. With decoupled presentation (F6) the render may run
  zero or several times between steps. Either way the branch outcome can change. That an
  observable divergence follows is **[I]**. The branch only re-arms a fade at full alpha, so the
  visible effect is probably limited to fade timing during loads, but `IsFadeBlocking` makes the
  fade gate step work, so treat it as simulation visible until proved harmless.
- Fix for the recreation: the fade colour, blend and lock are step state owned by the simulation.
  The render-side writers become step-side effects applied at the original cadence, which is once
  per presented frame, after the last (non catch-up) step of that frame, in the render order given
  in section 1 of `SCREENS_AND_FADES.md`. The renderer only reads a snapshot.
- `FFX_Ch_EventjumpInit 0x83B460` also calls `FFX_ScreenFade_GetColor` (0x83B483), but uses only
  the RGB and forces alpha to 1 **[V]**. Presentation only.

### 2.2 Step-side culling uses a stale camera on catch-up steps (not a GPU read)

Recorded here because the brief asked about `FFX_Ch_UpdateCameLenAndZClip`, see section 4. The cull
is pure CPU, but `FFX_Gfx_SetCameraWorldMatrix 0x639860` returns early when
`g_ffxIsCatchUpStep` is set **[V]**, so a catch-up step culls against the camera of the last
non catch-up step. That is a catch-up dependency, not a renderer read. Whether the resulting
CHR+0x180 bit reaches gameplay is open (section 6).

## 3. The table, one row per call site

Columns: call site, containing function (proposed name in brackets), D3D call, what is read,
where the bytes end up, classification, evidence.

### 3.1 Map with READ or READ_WRITE, and their staging copies

| Call site | Function | Call | What is read | Consumer | Class | Ev |
|---|---|---|---|---|---|---|
| 0x562F69 | sub_562E00 (PDataBlockD3D11__mapForRead) | CopyResource buffer -> staging (Usage 3, CPU 0x30000) | a vertex buffer | the Map at 0x563057 | per caller, see 3.2 | V |
| 0x562F9D | sub_562E00 | Map READ, direct, when the buffer already has CPU READ (0x20000) | vertex buffer bytes | caller | per caller | V |
| 0x563057 | sub_562E00 | Map READ on the staging buffer | vertex buffer bytes | caller | per caller | V |
| 0x5631C5 | sub_5630C0 | Map READ_WRITE (3) | buffer bytes | none, function has no references | dead | V |
| 0x563816 | sub_563740 | Map READ_WRITE (3) | buffer bytes | none, no references | dead | V |
| 0x5635F9 | sub_563490 (PIndexDataBlockD3D11__mapForRead, I) | CopyResource -> staging | index buffer | 0x5636E2 | dead | V |
| 0x56362D | sub_563490 | Map READ (1), direct | index buffer bytes | callers sub_563740, sub_A14570 and unreferenced code at 0xA1B220 | dead | V |
| 0x5636E2 | sub_563490 | Map READ (1), staging | index buffer bytes | as above | dead | V |
| 0x58ED59 | sub_58EC50 (PRenderInterfaceD3D11__copyViewportImage) | ResolveSubresource, format 28 | back buffer | 0x58ED6B | dev only, presentation | V |
| 0x58ED6B | sub_58EC50 | CopyResource -> staging (Usage 3, CPU READ) | resolved back buffer | 0x58ED84 | dev only, presentation | V |
| 0x58ED84 | sub_58EC50 | Map READ | back buffer pixels, converted to 24-bit RGB, rows flipped | BMP writer `sub_624DA0` "PApplication::screenshot: %s" | dev only, presentation | V |
| 0x59624B | sub_5960E0 (PTexture2DD3D11__mapForRead, I) | CopyResource -> staging | texture | 0x59627A, 0x5962A1 | dead | V |
| 0x59627A, 0x5962A1 | sub_5960E0 | Map READ | texture texels | texture format convert chain, see below | dead | V |
| 0x5963CE | sub_5962F0 | Map READ_WRITE (3) | texels | same chain | dead | V |
| 0x59664D, 0x596686 | sub_5964C0 | Map, type 2 and a register | texels | same chain | dead | V |
| 0x596859 | sub_5966E0 | CopyResource -> staging | texture | 0x59688E, 0x59690F | dead | V |
| 0x59688E, 0x59690F | sub_5966E0 | Map READ (1) | texels | same chain | dead | V |
| 0x596B25 | sub_596970 | Map READ_WRITE (3) | texels | same chain | dead | V |
| 0x59700C | sub_596ED0 | Map READ (1) | texels | same chain | dead | V |
| 0x59724C | sub_597070 | Map READ_WRITE (3) | texels | same chain | dead | V |
| 0x5976AE | sub_5972C0 | Map, type in a register | texels | same chain | dead | V |
| 0x597862 | sub_597710 | Map READ (1) | texels | same chain | dead | V |
| 0x597AD1 | sub_5978E0 | Map READ_WRITE (3) | texels | same chain | dead | V |
| 0x597E99 | sub_597B60 | Map, type from a local | texels | same chain | dead | V |

The texture family (0x5960E0 to 0x597B60) is the 2D, 3D and cube map lock paths. Every caller
funnels through `sub_A1B2A0`, `sub_A1B300`, `sub_A1B390`, `sub_A1B430` or `sub_A1B4C0`, which
are reached only from `sub_A04460`, `sub_A045D0` and `sub_A04750`, which are reached only from the
thunks at 0xA0EF60, 0xA0EFA0 and 0xA0EFE0, and those have no references **[V]**. `sub_5A77E0`
(called only by the dead `sub_A04460`) and `sub_5A4440` (no callers) wrap the family without a Map
of their own **[V]**. The PhyreEngine 3.21 SDK has the same shape in its 2D, 3D and cube map
texture classes and its data block classes, which is where the proposed class names come from
**[I]**. The game ships 3.9, so the binary is the authority.

### 3.2 Who calls the vertex read (sub_562E00)

| Route | Reached from | What happens to the bytes | Class | Ev |
|---|---|---|---|---|
| `sub_63A810` (FFX_Chr_CloneClassCharacterVertexData) | `FFX_Ch_OnInstanceReady` at 0x827D71, when CHR `m_flags1 & 0x200000` and `m_hasClonedClassChar` | see below | presentation only | V |
| `sub_638080` (FFX_DataBlock_LockForRead) | only `sub_A07740`, reached only from the unreferenced `sub_A14570` | nothing | dead | V |
| `sub_63B290` (FFX_Chr_DebugDumpVertexBuffers, "start outputting all vertex buffers") | gate `dword_13007E0` has no writer except a clear | printf | dead in retail | V |
| `sub_5630C0` | no references | nothing | dead | V |

The live route, `sub_63A810` **[V]**:
- Reads the position streams (vertex types `unk_C985D8` and `unk_C98620`) into 12-byte CPU arrays,
  and the `unk_C98650` stream (16 bytes, 8 with half floats, 4 with RGBA8) into float4 arrays.
- Spins on the GPU refcount (`sub_430030`) before reading.
- Builds a 560-byte entry with bounds seeded at plus and minus 100000 and appends it to
  `g_ffxClonedChrList 0xCCB254` (count `dword_CCB250`).

Consumers of the cloned data:
- `FFX_Chr_StepClonedCharacterList 0x63B740` is called from `FFXApplication__render`, so render
  time. It runs `sub_6365F0` ("ClothWorld") and writes vertex buffers back through
  `sub_434C50` and `sub_564CB0`. Presentation only **[V]**.
- `FFX_Chr_FreeClonedClassCharacter` frees it **[V]**.
- `sub_827E90` (FFX_Chr_BindClonedVertsToBones) at instance ready **[V]**:
  - It writes CHR+0x868 = CHR+0x844 - minY * 1000, where CHR+0x844 comes from the cdf via
    `sub_826340`. A whole-.text scan for displacement 0x868 found only writers (`sub_824620`
    zeroes it in `FFX_Ch_Allocate`, and `sub_827E90` itself). No reader **[V]** for the scan, so
    the value is unused **[I]**. The 0x40A8xx hits are other structs.
  - It builds a per-vertex bone binding through `sub_828C10` into the scratch globals
    `flt_23C5D00`, `flt_23C5100` and `flt_23C4500` **[V]**.
- The bytes read back are the asset's own vertex data, uploaded and read back unchanged, so even
  the CHR+0x868 write is a deterministic function of the asset **[I]**. For the recreation, read
  the asset on the CPU and skip the GPU round trip.

### 3.3 GPU to GPU copies (no CPU read, listed so nothing is missed)

| Call site | Function | Call | Purpose | Class | Ev |
|---|---|---|---|---|---|
| 0x58EB83 | sub_58EB10 (Phyre_RenderD3D11_CopyResource) | CopyResource | resource to resource | presentation | V |
| 0x58EBFE | sub_58EBB0 (Phyre_RenderD3D11_CopyStructureCount) | CopyStructureCount | UAV count into a buffer | presentation | V |
| 0x58EC1E | sub_58EBB0 | CopyResource | count buffer to destination, from render thread executor 0x615095 | presentation | V |
| 0x5930A1 | sub_593010 | ResolveSubresource | MSAA resolve | presentation | V |
| 0xA395BD | sub_A39200 (Iggy GDraw) | ResolveSubresource | render target to Iggy texture | presentation | V |
| 0xA397AF | sub_A39200 | CopySubresourceRegion | same | presentation | V |

Upload paths that stage through CopyResource in the other direction (`sub_564CB0`, `sub_59D570`)
are writes, not read-backs **[V]**.

### 3.4 Write-only Maps (not read-backs, listed for completeness)

All **[V]**:
- `sub_560E90` and `sub_560F90`: type 2 through `sub_563900` or `sub_563290`, becoming 4 for a
  dynamic buffer.
- `sub_563290`: 2. `sub_58E200`: 4. `sub_58F050`: 2, unreferenced.
- `sub_591B10` and `sub_591DD0`: 4. `sub_593930`: 4.
- `sub_5937A0`: 2 or 4, constant buffer upload from `sub_59B3D0`.
- `sub_596B90`: 2 or 4, texture map, used by the FMV upload `sub_A27E20` from `sub_A284A0`,
  `FFX_Fmv_StepPresentation` and `sub_672070`.
- Every GDraw Map is 4 or 5 (`sub_A3C070` uses `(a3 == 0) + 4`).

### 3.5 Queries

| Check | Result | Ev |
|---|---|---|
| Context GetData (vtable 0x74) | zero calls in the binary, any base | V, full indirect-call scan |
| Context End (0x70) | zero calls | V |
| Context Begin (0x6C) | zero D3D calls. The 4 hits at that slot are Bullet objects | V |
| SetPredication (0x78) | zero calls | V |
| Device CreateQuery (0x60) or CreatePredicate | no caller found | V |

So there is no occlusion query, no timestamp query and no predication. The 3.21 SDK has a GPU
timer built on timestamp queries. The 3.9 build the game ships has nothing of the kind.

### 3.6 Swap chain, DXGI and device property reads

| Call site | Function | Call | What is read | Consumer | Class | Ev |
|---|---|---|---|---|---|---|
| 0x5959C4 | FFX_Phyre_CreateDeviceAndSwapChain | GetBuffer | back buffer interface | RTV creation | settings/boot | V |
| 0x59928F | ResizeSwapChain 0x599210 | GetBuffer | back buffer interface | RTV recreation | settings/boot | V |
| 0x58DFF9 | ResizeTargetToMode 0x58DF00 | ResizeTarget, after EnumAdapters(0), EnumOutputs(0), GetDisplayModeList | display mode list | resolution choice | settings/boot | V |
| 0x59377F | Present 0x593760 | Present | return value is ignored | none | presentation | V |
| creation | FFX_Phyre_CreateDeviceAndSwapChain | feature level out-param | feature level | forces config row 14 (MSAA) off at 10_0 | settings/boot | V |
| 0xA3DA96 | sub_A3D9F0 (Iggy GDraw) | OMGetRenderTargets | bound RTV and DSV | GDraw state save | presentation | V |

The swap chain has no other user apart from teardown in `sub_59C400` **[V]**. There is no
GetFrameStatistics, GetLastPresentCount or GetContainingOutput anywhere **[V]**. `sub_594F40`
(PRenderInterfaceD3D11__getViewportSize, I) returns the stored width, height and pitch without
touching the device **[V]**. The exe imports only `D3D11CreateDevice` and
`D3D11CreateDeviceAndSwapChain` from d3d11, with no dxgi, D3DX, d3dcompiler or gdi imports **[V]**.

### 3.7 Adjacent, not D3D

- **PS2 frame capture** `sub_83A270` (FFX_SgCapture_Step), from `FFX_DrawDebugOverlay`, armed by
  `dword_1301804`, which `sub_83A470`, `sub_83A4F0` and `sub_83A580` set from
  `FFX_PlayerCam_ReadPadInput`, `SG_DebugGui`, ATEL `Save_004` and `Save_005`, and the Sg capture
  window. It calls `sub_83A690`, which reads through the GS StoreImage stub `sub_62F830`, and that
  stub is `return 0` (magic host API slot 426) **[V]**. Nothing comes back from the GPU. It writes
  "host0:" BMPs. Debug only.
- **Fake PS2 hardware register** `g_ffxPs2HwRegSink 0x23056E8`: `FFX_Ch_UpdateMotionAll` reads it
  as the T0 timer at 0x8331FC and 0x83337C, and the result lands in `dword_13009BC`, which has no
  reader **[V]**. Not a GPU read.

## 4. FFX_Ch_UpdateCameLenAndZClip 0x82E220 does not read the GPU

**[V]** for every link of the chain unless marked:
- It calls `FFX_Chr_InstanceAnyMeshVisible 0x63B230`, which calls
  `FFX_Gfx_IsMeshInstanceVisible 0x6603C0`, which calls `FFX_Gfx_TestBoundsAgainstFrustum
  0x663CC0`.
- The test uses the CPU-side `PMeshInstanceBounds` at mesh+0x1C and the camera view-projection at
  `*(gfx+0xDC)+0x74`. It is a pure CPU frustum test.
- `gfx+0x10D8C != 0` (split camera mode) short-circuits it to visible.
- The view-projection is written by `sub_6720C0` (Phyre_PCamera_UpdateViewProjection:
  +0x70 = projection(+0x30) times inverse(world)), called with ecx = `*(gfx+0xDC)+4`.
- At step time `sub_640DF0` (FFX_Gfx_RefreshCameraViewProj) recomputes it. It is called at
  0x82EDFA in `FFX_Ch_StepAll`, before the cull call at 0x82F256. It binds the world pointer
  cam+0xB4 to gfx+0xFA04, and does the same for the cameras at gfx+0x654 and gfx+0x238 when split
  mode is 1.
- The world matrix comes from `FFX_Gfx_SetCameraWorldMatrix 0x639860`, which is skipped on
  catch-up steps (section 2.2).
- The projection comes from `maybe_FFX_Gfx_SetCameraFov` and `maybe_FFX_Gfx_SetCameraAspect`,
  driven by `FFX_Came_PushViewToRenderCtx 0x7BC980` from `FFX_Came_ActivateScreen`, which runs
  before `FFX_Ch_StepAll` every step. Both call `PCameraPerspective` vfunc 1 `0x456D10`, which
  rebuilds the projection from fov (+0x154) and aspect (+0x150).
- The render then overwrites the main camera projection. It applies the pixel offset translation
  from gfx+0xFA34/0xFA38 at 0x669013..0x66905D, and in `sub_64FBB0` at 0x64FC0C it multiplies in
  the 4:3 fold from `sub_651650` (Y scale 1.3333, Y shift 1/3 - 2 * (gfx+0xF9BC + gfx+0xF9C0)),
  unless gfx+0xF9C7 or `byte_CCB9C0` is set. The next step rebuilds the projection before
  `FFX_Ch_StepAll`, so the render-side change does not leak into the cull in normal flow **[I]**.
  It would leak if a step ran the cull without first passing through `FFX_Came_ActivateScreen`.
  None was found **[I]**.
- `byte_CCB9C0` is content driven (map and hash list `dword_B421C8`, set in the map loader
  `sub_65CBF0`), not a user setting.

So the cull is CPU only, but it shares one `PCamera` object with the renderer. In the recreation
the step owns its own camera copy and the renderer owns the folded one.

## 5. Proposed IDB names

IDA was read only for this pass. One row each, VA, name, evidence.

| VA | Proposed name | Evidence |
|---|---|---|
| 0x562E00 | PDataBlockD3D11__mapForRead | CPU READ check, staging CreateBuffer, CopyResource 0x562F69, Map READ 0x563057 |
| 0x563290 | PDataBlockD3D11__mapForWrite | Map type 2 path used by sub_560E90 |
| 0x563490 | PIndexDataBlockD3D11__mapForRead | same staging shape as 0x562E00, all callers dead |
| 0x564AB0 | PDataBlockD3D11__unmapRead | pairs with 0x562E00 (I) |
| 0x564CB0 | PDataBlockD3D11__unmapWrite | Unmap then CopyResource staging -> buffer |
| 0x560E90 | Phyre_D3D11_MapBuffer | dispatches to 0x563900 or 0x563290 with type 2 |
| 0x560F90 | Phyre_D3D11_MapBuffer2 | twin of 0x560E90 |
| 0x5960E0 | PTexture2DD3D11__mapForRead | staging CopyResource 0x59624B then Map READ |
| 0x58EC50 | PRenderInterfaceD3D11__copyViewportImage | Resolve, staging copy, Map READ, RGB24 row flip, caller is the screenshot BMP writer |
| 0x594F40 | PRenderInterfaceD3D11__getViewportSize | returns stored width, height and pitch only |
| 0x624DA0 | PApplication__screenshot | string "PApplication::screenshot: %s", writes a BMP from 0x58EC50, reached from the InputLog "RenderFrame" command |
| 0x62C480 | PApplication__screenshotUnused | second caller of 0x58EC50, no references |
| 0x58EB10 | Phyre_RenderD3D11_CopyResource | single CopyResource at 0x58EB83 |
| 0x58EBB0 | Phyre_RenderD3D11_CopyStructureCount | CopyStructureCount 0x58EBFE then CopyResource 0x58EC1E |
| 0x596B90 | PTexture2DD3D11__mapForWrite | Map 2 or 4, FMV upload caller |
| 0x5937A0 | PConstantBufferD3D11__upload | Map 2 or 4 from sub_59B3D0 |
| 0x59D570 | PTexture2DD3D11__unmap | Unmap then staging -> texture CopyResource |
| 0x63A810 | FFX_Chr_CloneClassCharacterVertexData | reads position and colour streams, builds a 560-byte entry in g_ffxClonedChrList |
| 0x827E90 | FFX_Chr_BindClonedVertsToBones | writes CHR+0x868, calls the sub_828C10 bone binding |
| 0x638080 | FFX_DataBlock_LockForRead | thin wrapper on 0x562E00, dead |
| 0x63B290 | FFX_Chr_DebugDumpVertexBuffers | string "start outputting all vertex buffers", gate never set |
| 0xA07740 | PDataBlock_CopyStridedOut | strided copy out of a read map, dead |
| 0x6720C0 | Phyre_PCamera_UpdateViewProjection | +0x70 = proj(+0x30) x inverse(world) |
| 0x640DF0 | FFX_Gfx_RefreshCameraViewProj | binds world pointers and recomputes VP, called in FFX_Ch_StepAll at 0x82EDFA |
| 0x456D10 | PCameraPerspective__updateProjection | vfunc 1, rebuilds projection from +0x154 fov and +0x150 aspect |
| 0x651650 | FFX_Gfx_BuildPs2ScreenFoldProj | builds the 4:3 Y scale and shift matrix used at 0x64FC0C |
| 0x83A270 | FFX_SgCapture_Step | armed by dword_1301804, writes host0: BMPs |
| 0x83A690 | FFX_SgCapture_WriteBmp | reads through the StoreImage stub |
| 0x62F830 | FFX_Gs_StoreImage_Stub | body is return 0, magic host API 426 |

## 6. Open

- Readers of CHR+0x180 bit 0x08 (the cull result) beyond `FFX_Ch_UpdateMotionAll`, which masks
  the field with 0x13 at 0x832E5C before its own hide tests. `FFX_Ch_ResolveCollisionsAll`,
  `FFX_Ch_GetHideFlags` (callers `sub_80EB50`, `sub_814520`, `sub_816E50`),
  `FFX_Ch_DispatchInBatches`, `FFX_Ch_UpdateBoneMatrices` and `sub_82DBC0` read +0x180 and were not
  traced bit by bit. If any of them gates gameplay on 0x08, the catch-up camera skip is simulation
  visible.
- The render thread executor case 13 at 0x614D39 reaches `sub_58EC50`. No appender of command kind
  13 was found, but the scan of `sub_613E10` callers was partial, so its being unreachable is
  **[I]**.
- Whether the fade alpha branch in section 2.1 ever produces a different step outcome in practice.
  A runtime probe would log the alpha returned at 0x820D4F against the presenter count during a
  map load, with presentation forced to run zero times and then twice between two steps.
