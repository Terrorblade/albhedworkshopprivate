# Render passes: what FFXApplication::render submits, in order

Written 2026-10-12 for Project Zanarkand PROGRESS 1.5.6. The queues this note reads from are in
DISPLAY_LIST.md, which also has the PRenderer command kinds and the pass type records referred to
here. VAs are at the preferred base 0x400000. Call-site addresses are the `call` instruction inside
the presenter. Everything is read from Hex-Rays or the disassembly unless marked "inferred". IDA was
read only for this note, so `sub_` names are what the IDB has today. DISPLAY_LIST.md section 10 has
proposed names.

## 1. The frame around the presenter

`Phyre__PApplication__frameTick 0x627940` does, per presented frame:

1. update, then animate. Animate is `FFX_MainStepLoop`, which runs zero or more `FFX_MainStep`s.
   This is where the queues in DISPLAY_LIST.md are filled.
2. per viewport, vtbl+0xC (not read), then vtbl+0x20, which is `FFXApplication::render 0x42F930`.
3. `sub_5B6AF0`, which waits on renderThread+0x164 and appends the Present record.
4. renderer vtbl+8, `PRendererBase__vf02 0x5B6C50`, which posts the frame's command list to the
   render thread.
5. vtbl+0x28, endFrame.

`Phyre_RenderThread_FlushGlobal` at `0x627B4A` is not part of every frame. The disassembly at
`0x627B20..0x627B4A` compares the app dirty flags at +0x14..+0x18 and jumps to `loc_627CA2` past it
when they are clear. See the corrections in section 5.

`FFXApplication::render` reaches the presenter through `FFX_Frame_PresentScene 0x642BB0`, which
calls `sub_668930`. When the frame is paused it calls `FFX_Frame_PresentPausedOverlay`
(`sub_66B130`) instead, which clears black, copies target +172 of the target manager to the back
buffer with `sub_6959D0`, then runs Iggy.

## 2. Targets

The presenter draws into targets owned by the post-process target manager at gfxCtx+4300
(`dword_CCC8A0`, returned by `sub_692BE0`). Offsets below are into that object.

| offset | role |
|---|---|
| +140 | main scene colour |
| +144 | main scene depth |
| +148 | second colour, bound as RT1 next to +140 during the opaque pass |
| +156 | aux colour, used by the ring-kernel pass, postOpaque and the layer 0 group draw |
| +164 | aux colour used by depth of field |
| +168 | copy of +140, source for refraction and pass command kind 1 |
| +172 | copy target, also what the paused overlay shows |
| +74540 | motion blur target |
| +74420 | byte. Nonzero picks the MSAA pass types (Opaque, Transparent), zero the NoMSAA ones |
| +74652 | blend state slot for the full-screen colour quad |

`sub_692C40(mgr, renderer, src, dst, ...)` is the plain full-screen copy. `sub_692E90` is the same
with a ColorScale constant. `sub_697490` draws a full-screen colour quad with a blend.

## 3. The main 3D path in sub_668930

Prologue, every frame:

- `sub_658AE0`, `sub_651BE0` once (guarded by gfx+63944)
- `sub_6542B0` 0x66897F, the shadow map pass ("MySMScaleBias")
- `sub_6D4E20` 0x66898B, BrokenScreen setup, and `sub_698F70`
- back buffer clear: colour target none, full viewport, clear colour (0,0,0,1), begin(1), end(1)
  (0x6689A5..0x668A06)
- camera updates through `sub_6720C0`, and the FMV camera swap on `g_ffxFmvState`

Early exits:

- `g_ffxMenuSysTexLoad` phase below 4: clear, `sub_652A10` when the phase is below 2 (a shortened
  scene draw that also runs the sort and merge), `sub_659000`, then the tail (step 30 onward)
- an FMV script: `sub_653960` (the ordered queue walk only) and Iggy, then return
- gfx+68910 set: Iggy only
- `g_ffxMenuOpenPending`: `sub_653060`
- `sub_6662F0` false: no scene, jump to the 2D path
- `g_ffxMenuSysRunning`: jump to the 2D path (step 28)

The scene path. "RT" is the colour target, "DS" the depth target. Pass types are the records of
DISPLAY_LIST.md section 2. Clusters are the gfx cluster list heads at +124, +156, +172, +188.

| # | call site | function | RT / DS | what |
|---|---|---|---|---|
| 1 | 0x668EDE | `sub_653600` | none | effect prep (`sub_6C9E50`, `sub_6CAEE0`), role not read |
| 2 | 0x668EE5 | `sub_66B560` | none | ortho camera setup, "clrScaleFactor" |
| 3 | 0x668EF0 | `sub_6ADA20` | none | delegation phase 0x8 |
| 4 | 0x668F12..0x669245 | target setters | +140 / +144 | clear to the gfx+204 colour, clear(11) |
| 5 | same block | target setters | +148 | clear white, clear(1) |
| 6 | 0x66925F..0x66931C | target setters | RT0 +140, RT1 +148 / +144 | MRT bind, beginScene(0) |
| 7 | 0x669349, 0x66935B | `sub_6AD8C0`, `sub_6AD970` | same | phases 0x10 and 0x20, then `maybe_FFX_Gfx_ApplyProjOffsetToPhyreCamera` |
| 8 | 0x669364 | `sub_64FBB0(this, 1)` | same | Opaque (or OpaqueNoMSAA) over clusters +172 then +124, front to back. Then model matrix overrides `sub_66B360`. Then Transparent (or TransparentNoMSAA) over +172 and +124 back to front, order set by byte +212, plus +188 when gfx+63948 is 1 or 3, and `sub_651070`. When gfx+69004 is 1 it splits into two half-width viewports, the second with camera +568 |
| 9 | 0x669419 | `sub_62BE70` | same | +188 with Transparent, when byte +212 is clear and gfx+63948 is 0 |
| 10 | 0x669424, 0x669431 | `sub_6AD430`, end | same | phase 0x4000, endScene(1) |
| 11 | 0x66954C | `sub_695BA0` | +156 cleared to 0 | unless BrokenScreen is active. Ring kernel with g_numRing, g_kernelScaleChr, reject, accept, intensityChr. Screen-space ambient occlusion, inferred from those names |
| 12 | 0x669573, 0x669643, 0x669667 | `sub_6AD810`, `sub_62BE70` x2 | +156 cleared to 0 | phase 0x40, then postOpaque over +172 and +124 |
| 13 | 0x6697BB, 0x6697F8, 0x66981F | `sub_62BE70` x3 | +140 / +144 | postTransparent over the cluster lists |
| 14 | 0x66983D..0x669889 | `sub_65EFA0`, `sub_6AD760`, `sub_694F10`, `sub_6AD2D0` | +140 | only when there are no kind 0 or kind 5 pass commands: phase 0x100, colour correction (ColorCorrectionBuffer), phase 0x200 |
| 15 | 0x669915..0x669AC5 | `sub_692C40`, setters | +140 -> +168 | when gfx+64325 is set, copy, then rebind and begin(11), (10) or (0) |
| 16 | 0x669AF3..0x669B92 | `sub_6589E0(0)`, `sub_62BE70` x3, `sub_66C770` | cluster +156 | mesh groups of layer 0 with Opaque, Transparent, postTransparent, then relink all groups |
| 17 | 0x669BCF | `sub_695790` | +164 | depth of field ("FocusDepth"), only for events 393..398 |
| 18 | 0x669BDA, 0x669BF2, 0x669C22 | `sub_6ADB80`, `sub_6AE0C0`, `sub_697490` | +140 | phases 0x2000 and 0x8000, then a fade quad |
| 19 | 0x669C82..0x669D76 | `sub_6C4A70`, `sub_6C4CF0`, `sub_62BE70` x2 | | BlurEffectEx when the magic id is 20 |
| 20 | 0x669DD2, 0x669DD9 | `FFX_Frame_WaitJobSchedule`, `sub_654650` | | join the prim build jobs, sort and merge the draw queues |
| 21 | 0x669E21, 0x669E5A | `sub_692C40` | +140 -> +156, +140 -> +172 | copies for magic ids 272 and 273, and when `dword_CEC19C` is set |
| 22 | 0x669E6B..0x669EB8 | setters, `sub_650670`, end | +140 / +144 | beginScene(0), walk the ordered queue with Transparent, running pass commands in place, endScene |
| 23 | 0x669EC9..0x669F15 | `sub_65EFA0`, `sub_6AD760`, `sub_694F10`, `sub_6AD2D0` | +140 | colour correction here instead, when kind 0 or 5 commands existed |
| 24 | 0x669F50, 0x669F57, 0x669F62 | `sub_658F00`, `sub_659000`, `sub_6AD6B0` | | clear the pass commands, phase 0x80 |
| 25 | 0x669FC0..0x66A0E8 | `sub_692C40`, `sub_6531B0` | +140 -> +168, then +140 | the gfx+0x3854 list (submit mode 2), sampling the +168 copy. Distortion style, inferred |
| 26 | 0x66A0ED..0x66A704 | post chain | various | motion blur (`sub_6C5130` singleton, `sub_6C5B60`, `sub_6C5410`), fade phase 0x1000 at 0x66A27A with `sub_697490`, BrokenScreen `sub_6D5680`, BlurEffect `sub_6C3970`, BlurEffectEx `sub_6C4CF0`, Distortion `sub_6C2DC0`, `sub_653230`, unsharp mask `sub_697670` ("USM"), `sub_6535B0` (into `sub_696AF0`), brightness `sub_695440` (g_brightPara, g_BrightTex) |
| 27 | 0x66A7A8..0x66A95D | `sub_6AD600`, `sub_650F80`, `sub_650EA0`, `sub_6538D0` | +140 | phase 0x400, second camera pass, the +0x3860 list (submit mode 1), the +0x3884 set with Transparent. A second fade phase 0x1000 at 0x66A807 |
| 28 | 0x66AA55..0x66AC2C | `sub_67B970`, `sub_650EA0`, `sub_650340`, `sub_650CB0` | +140 | the 2D pass. Scissor set, sub-viewport from gfx+68928..+68940 when gfx+120 is set |
| 29 | 0x66ACCF..0x66AD88 | `sub_6AE450` x2, `sub_697490` x2, `sub_6956D0`, `sub_5BB390`, `sub_6AD380` | +140 | fades phase 0x1000, `sub_6956D0` when `dword_1136FC0` is 1, a kind 30 command, phase 0x800 |
| 30 | 0x66ADCA | `sub_692C40(renderer, src, 0, 1, 0, 1)` | back buffer | final copy of +140, or of +156 in the menu |
| 31 | 0x66AEA8..0x66AFE8 | `sub_698750`, `sub_6A9F00`, `sub_6953A0`, debug page titles | back buffer | debug text ("CharacterLighting", "PostProcess"), then `sub_6DDEB0`, `sub_658F40`, `sub_6E4EA0`, `sub_6F2D20` |
| 32 | 0x66B117 | `sub_A267F0` | back buffer | Iggy UI, last |

Post-process singletons named by their strings: MotionBlur `sub_6C5130`, BlurEffectEx `sub_6C4A70`,
Distortion `sub_6C2290`, BlurEffect `sub_6C3580`, BrokenScreenEffect `sub_6D4DE0`.

Cluster walks all go through `sub_62BE70(app, list, camera, sortDir)` into `sub_9F3C30`, which
iterates `PMeshInstanceBounds` (descriptor 0xCA89B8) and `POccluderGeometryInstance` (0x1A85420)
in the clusters, culls against the camera and records the linked meshes. sortDir 1 is front to
back, -1 back to front, through `sub_9F2580`. So culling happens at render time, per frame.

## 4. Step time versus render time, short form

The step fills the queues and moves and links meshes. The render owns the pass order, culling,
sorting, every target and every post effect. DISPLAY_LIST.md section 9 has the full split and the
list of queues the render consumes, which is what stops a second render of the same step from
matching the first.

## 5. Corrections to existing docs

- MAIN_LOOP.md around lines 19 to 25, THREADS_AND_CLOCKS.md T1, BOOT_SEQUENCE.md section 10,
  RENDER_DEVICE.md section 3 and the IDB comment on 0x5BC3F0 say or imply that
  `Phyre_RenderThread_FlushGlobal` runs every frame. It runs only when one of the app dirty flags at
  +0x14..+0x18 is set (`0x627B20..0x627B4A`, `jz loc_627CA2`). The per-frame handoff is
  `sub_5B6AF0` (wait on renderThread+0x164, Present record) followed by `PRendererBase__vf02
  0x5B6C50` (post, wait on renderer+0x44 when set).
- MAIN_LOOP.md section 2 step 9 lists the build as "FFX_PlayerCam_Publish, FFX_Came_ActivateScreen
  loop, FFX_Ch_StepAll, DmaSync, effects, 2D pass". On PC the GS and DMA parts are no-ops, they end
  in `g_ffxPs2HwRegSink` and nullsub senders. Nothing draws in the step. It fills queues.

## 6. Unresolved

- Which content lives in clusters +124, +156, +172, +188. Map in +124 and characters in +172 is a
  guess.
- What `sub_653600`, `sub_66B560`, `sub_6535B0` / `sub_696AF0`, `sub_653230` and `sub_651070` do.
  `sub_695BA0` as SSAO is inferred from shader constant names.
- Blend, depth and cull state per pass: answered 2026-10-12 in `SHADERS.md` sections 4 and 6.
  FFX sets none of it directly. Each material's PShaderPass carries the full D3D11 state for the
  pass type, found through `PRenderer__findShaderForPass 0x5BA690`.
- `sub_695BA0` may be the HDAO effect: FFX.exe names `Shaders/GCM/HDAO.cgfx.phyre` among its 34
  shader paths. Unchecked.
- The per-viewport vtbl+0xC call before render.
- How far the main thread runs ahead of the render thread (renderThread+0x164 and renderer+0x44).
- Which magic ids and events turn on each optional post effect, beyond the ones named above.
