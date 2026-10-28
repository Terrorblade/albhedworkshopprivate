# Visual census: what the render reads that only the step writes

Written 2026-10-13 for Project Zanarkand PROGRESS 1.5.13. The rules it serves are PORTING_RULES F6
and F7. The step publishes a snapshot with a previous and a current value for everything the
renderer draws, and the renderer interpolates by the time since the last step. This note lists those
values for FFX.exe, says which kind each one is, and gives the rule the recreation should apply.
It also lists the places where the original goes the other way (render state read by the step, F2),
the things the original already computes per present, and the readers of the catch-up flag that
change what gets drawn.

Everything was read from the IDA database of FFX.exe (VA base 0x400000). IDA was used read only.
Four areas (2D and fades, FMV and post, map and background, effects) were researched by helper
agents and their load-bearing claims were spot checked in the binary. Those checks are marked.
Characters and camera were done directly.

Background reading: DISPLAY_LIST.md (the queues), RENDER_PASSES.md (the pass order), MAIN_LOOP.md,
FFX_GAME_NOTES.md (camera), PHASE1_CLONE.md and CLASS_CHARACTER.md (the CHR layout),
THREADS_AND_CLOCKS.md, FMV_SYNC.md.

## 1. Conventions

**Marks.** [V] means seen in Hex-Rays or the disassembly. [I] means inferred from names, strings,
call shape or a partial read. A row marked V can still carry an I inside one cell, and that cell
says so.

**Rates.** These go in the writer column.

| tag | meaning |
|---|---|
| step | once per `FFX_MainStep 0x820AE0`, catch-up steps included |
| step* | once per non-catch-up step (gated on `g_ffxIsCatchUpStep 0x12FB7D0` or `g_ffxIsCatchUpStepRender 0x12FB7D8`) |
| sub-step | inside the sub-step loop of `FFX_MainStep`. The count is `g_ffxPlayerSubStepCount 0xC496D4`, which is 1 in retail [V: the .data low byte is 1, the only non-debug writer is `FFX_Btl_BeginBattle` at 0x781106 and it sets 1, and `FFX_Player__readPad` changes it only under `g_ffxDebugMode`]. So sub-step equals step in retail |
| job-in-step | a JobWorker job dispatched and joined inside the same step (the character update jobs) |
| job-after | a JobWorker job run once per `FFX_MainStepLoop 0x822840` after the last step of the burst, from that last step's data, joined by the render at 0x642BBE and 0x669DD2 (the VFX prim build) |
| present | once per presented frame on the main thread, in `FFX_GameTick 0x639300` -> `FFX_Frame_UpdateBoostersAndOverlays 0x657140`, or in the presenter `FFX_Frame_RenderScene 0x668930` |
| RT | on the Phyre render thread, as a PRenderer kind 44 callback |

**Kinds.**
- **continuous**: moves smoothly between steps. Interpolate it.
- **discrete**: changes in whole steps and must not be blended (a flag, a frame number, a page).
  Hold the current value.
- **cut**: a value whose change is itself a discontinuity. When it changes, every continuous value
  that depends on it snaps for that step.
- **derived**: built by the step from other rows. The recreation recomputes it per present from
  the interpolated inputs and does not snapshot it.
- **present-rate**: the original already advances it per present or by wall time. These are
  listed so the recreation decides on purpose, see section 13.

**GPU candidate column** (PORTING_RULES F8, added 2026-10-13). yes means display-rate work that is
data parallel and that nothing in the step reads back. no means some part is simulation visible
(the cell says which) or there is nothing to gain. partial means the value itself is step state and
stays on the CPU, while the presentation work built from it can move. "already GPU" marks passes the
original already runs on the GPU. The reference CPU routines for every yes are in section 17.

**Frame shape** (MAIN_LOOP.md, re-read for this note [V]). `Phyre__PApplication__frameTick
0x627940` runs animate, which is `FFX_MainStepLoop` (zero or more `FFX_MainStep`, then the VFX prim
build flush) and then `FFX_GameTick` once. Then render, `FFXApplication::render 0x42F930` ->
`FFX_Frame_PresentScene 0x642BB0` -> `FFX_Frame_RenderScene`. Then `endFrame 0x42F4F0`, which flips
the DynGeoMem segment. Catch-up means every step of a burst but the last.
`FFX_RenderDelegation_FreeFlaggedEntries 0x63DAB0` (called at 0x82285B) frees the 0x1000 delegation
records before every step, and the 2D list swap runs only on non-catch-up steps. So what the render
sees is almost entirely the last step's output.

Abbreviations: gfx = `g_ffxGfxCtx`, pp = the post-process target manager `dword_CCC8A0`,
mgr = `g_ffxFmvPlayerManager`, mapWork = `g_ffxMapWork 0x1876E00`, env = `*(gfx+204)`,
CHR = the 0x850-byte character record, slot = a `g_ffxCameSlots 0x1137540` entry (stride 3488).

## 2. What matters most for the snapshot design

1. **The step does not hand the render a state block, it hands it finished Phyre objects.**
   Character root matrices, skin palettes, material colours, light matrices and hidden bits are
   written straight into `PMeshInstance` objects and material constants during the step, or in
   jobs joined inside the step. There is no step-side render struct to copy. The snapshot has to
   be taken one level up, from the CHR fields that feed `FFX_Ch_BuildSkinMatrices 0x832760`
   (position, facing, scale, tilt, joint local transforms, parent binding), and the recreation
   rebuilds the matrices per present. [V]
2. **Characters interpolate cleanly at the joint level.** Every joint has local euler XYZ,
   translation and scale floats (floats 57..59, 61..63 and 65..67 of the 352-byte joint record)
   produced by the motion code once per step, and the world chain is rebuilt from them. Snapshot
   those plus the root inputs, slerp the rotations, lerp the rest. Joint overrides (magic 437 and
   438, look-at, head sway) land in the same locals or as override matrices. [V]
3. **There is no teleport flag in the original.** `FFX_Ch_SetPos 0x82B480` mirrors pos into prevPos
   and sets flags1 0x2000000 and walkmeshTri to -1, which looks like a teleport. But
   `FFX_Atel_PushActorPosToChr 0x866800` calls it every step for any dirty scripted actor, and
   `FFX_Btl_SetUnitPos` uses it for battle placement. So "a script SetPos" (PROGRESS 6.2.8) cannot
   be the cut signal as written. The recreation needs explicit cut signals (section 16) plus a
   distance threshold as the backstop. [V]
4. **The camera has a natural cut signal.** `FFX_Came_StepAll 0x7BE110` compares slot+3212 (the
   last active priority) with the active priority. When they differ it runs `sub_7C0D40` and skips
   the smoothing filters, which is exactly a camera cut. The other camera cuts are entering or
   leaving the matrix override (`FFX_Came_SetMatrixOverride 0x7C06D0`, used by FMV cameras and by
   rcBg), map change and battle transition. [V]
5. **Effect particles mostly cannot be interpolated from what the original stores.** Sprite quads
   (prim kinds 1 and 3) are expanded in the view space of the step camera with the world matrix
   set to that camera's inverse, so an interpolated camera sees them facing the old camera.
   Particle identity is not kept in the vertex buffers. Kind 0 effect models can interpolate by
   world matrix, keyed on their u64 model key. Everything else should hold, unless the recreation
   rebuilds vertices per present from snapshotted particle state, which is not mapped yet. [V]
6. **2D has no identity either.** Sprites are batched per texture into records of up to 12 quads
   and the list is rebuilt every step. To interpolate menu scroll and cursor motion the recreation
   must key sprites at the producer. The few animated 2D values (menu dim, Sphere Grid open,
   message window open phase, copyright alpha, fade levels) are listed as scalars. [V]
7. **Texture animation and material morph are already per present in the original.** The
   TextureAnimationInfo autoplay advances by present dt (`sub_6B47E0`), and the morph, UV morph and
   UV offset weights are applied per present by `sub_6ACD20`, which also integrates a UV offset by
   present dt. So the original's texture animation speed is wall-clock based, while map node
   animation is step based. [V]
8. **Several post effects are present-rate feedback** (MotionBlur trail, AccBuf, Distortion
   feedback, BlurEffect, BlurEffectEx, the second-camera crossfade). They will look different at
   any present rate other than 29.97. Section 13. [V]
9. **There are real F2 read-backs.** The loud ones: the map load pending byte gfx+4501 is cleared
   by the render and gates the whole gameplay step, the ScreenFade register is written by the
   render and read back by `FFX_MainStep` in the load hold, the FMV frame index for videos 46, 72
   and 32 is written on the render thread and read by the step's movie camera, and the light 0
   colour is pulsed per present and read by the step's character lighting. Section 14.
10. **The catch-up flag leaks into CHR state through character culling.** On a catch-up step
    `FFX_Gfx_SetCameraWorldMatrix 0x639860` does not write, so `FFX_Ch_StepAll`'s camera refresh
    and `FFX_Ch_UpdateCameLenAndZClip 0x82E220` cull against the previous presented step's camera
    and write the Disp and Z bits of `m_hideFlags`. Those bits live in the CHR record. Section 15.
    [V for each link, I that any hash covers the byte]
11. **The camera is per step, its Phyre side is written at step and consumed at present.** The view
    matrix lands in gfx+64004..64048 at step, and the presenter re-points the PCamera at it and
    calls `sub_6720C0` per present. So the camera is an easy snapshot: take the slot's eye,
    reference, up, roll and ScrDpt and build the matrix per present. [V]

## 3. Characters

Exhaustive for the field and battle character path. Everything here runs inside `FFX_MainStep`.
The order in a step [V]:
- `FFX_Ch_UpdateMotionAll` (sub-step) advances motion and the seven CHRANIMF channels, then
  dispatches `FFX_Ch_UpdateRenderJob` for the root CHRs through `FFX_Ch_DispatchUpdateJob 0xA44B90`
  (JobSchedule__postJob3).
- `FFX_Ch_DispatchInBatches` (step) dispatches the parented CHRs and runs `FFX_Mot_RunEvents`.
- The jobs are joined inside the step by `sub_A44B80`, which waits on `sub_6DBF50`.
- `FFX_Ch_StepAll 0x82ED50` then pushes the results to Phyre. It runs once per live screen when
  `FFX_Gfx_IsSecondCameraActive()` is 0 and more than one screen is live.

`FFX_Ch_UpdateRenderJob` runs, per CHR: `FFX_Ch_UpdateBlink`, `FFX_Ch_UpdateLookAt`,
`FFX_Ch_BuildSkinMatrices` (when flags1 & 0x3000000), `FFX_Ch_UpdateBonePositions` (step-side
cached positions, not a render input), `FFX_Ch_UpdateShadeFromLights 0x834110`, and it keeps the
flags1 0x200 player bit [V].

| value | lives at | writer(s), rate | render reader(s) | kind | rule | cut signal | mark | GPU candidate |
|---|---|---|---|---|---|---|---|---|
| position | CHR+0x0C float3 | movement and motion code, `FFX_Ch_SetPos 0x82B480` (which also mirrors into prevPos CHR+0x1C), `FFX_Atel_PushActorPosToChr 0x866800` every step for dirty actors, `FFX_Btl_SetUnitPos`. step and sub-step | none directly. Consumed by `FFX_Ch_BuildSkinMatrices 0x832760` (job-in-step) into the root matrix | continuous | lerp | the section 16 list. prevPos is not usable as the snapshot's previous value because SetPos overwrites it | V | partial: step state, stays on the CPU. Its per-present lerp is part of the GPU pose interpolation |
| facing | CHR+0x158 m_rotY | movement, scripts, battle. step | BuildSkinMatrices uses -rotY - pi/2 | continuous | shortest-arc angle lerp | as position | V | partial: step state, stays on the CPU. Its per-present lerp is part of the GPU pose interpolation |
| X tilt | CHR+0x15C triple, current approached toward target each step | BuildSkinMatrices runs the approach | BuildSkinMatrices | continuous | lerp the current value | as position | V | partial: step state, stays on the CPU. Its per-present lerp is part of the GPU pose interpolation |
| scale and model offset | CHR+0x5C float3 scale, CHR+0x6C float3 offset, model scale m_f30 / modelScale * 0.01 | scripts, spawn. step | BuildSkinMatrices | continuous | lerp | spawn | V | partial: step state, stays on the CPU. Its per-present lerp is part of the GPU pose interpolation |
| root world matrix | CHR+0x1C8 m_rootJoint (352 bytes), world at CHR+0x1D0, scaled copy at CHR+0x210 | `FFX_Ch_BuildSkinMatrices` in `FFX_Ch_UpdateRenderJob`, when flags1 & 0x3000000. job-in-step | pushed by `FFX_Ch_StepAll` loop 1 through `sub_63C3E0` -> `FFX_Gfx_SetMeshTransform 0x66D810` (m_rootJoint float 8 on, times diag(1000), X and Y rows negated, bounds at +0x0C) into m_instance CHR+0x830 and m_instance2 CHR+0x838. Drawn by the retained scene passes | derived | rebuild per present from the interpolated position, facing, tilt, scale, offset and parent | inherits | V | partial: the step copy feeds parented CHRs and bone positions, so it stays on the CPU. The presentation rebuild from the interpolated inputs is GPU work |
| parent binding | CHR+0x19C m_parent, +0x1A0 m_parentJoint, +0x1A4 attach offset, CHR+0x10C m_i10C (root matrix override pointer) | attach and detach calls, scripts. step | BuildSkinMatrices composes the root from the parent joint world plus the offset, or copies the matrix at m_i10C | discrete | hold the binding. Interpolate the child in parent space and compose with the interpolated parent joint | any change of m_parent, m_parentJoint or m_i10C is a cut for this CHR | V | no: discrete step state |
| joint local rotation, translation, scale | CHR+0x328 m_joints, stride 352: euler XYZ at floats 57..59, translation at floats 61..63, scale at floats 65..67 | `FFX_Mot_AdvanceFrame` from `FFX_Ch_UpdateMotionAll`, sub-step. Motion blending happens inside the step | BuildSkinMatrices | continuous | slerp the rotation (convert the euler XYZ to a quaternion first), lerp translation and scale | a motion change is not a cut, the original blends it. Cut only with the CHR cuts | V | partial: step state on the CPU. The per-joint slerp and lerp for presentation is data parallel GPU work |
| joint world matrix | joint+136 (64 bytes) | BuildSkinMatrices: local from euler, translation, scale, times the parent joint world. job-in-step | rigid parts read it through `maybe_FFX_ChrInstance_SetBoneMatrix 0x63C430` (root times joint+136) in StepAll loop 1. Skinned meshes read the palette row | derived | rebuild per present | inherits | V | partial: the step's chain is read by FFX_Ch_UpdateBonePositions and by parented CHRs, so it stays on the CPU. The presentation chain is GPU work |
| joint override matrix and constraint | joint float 50 holds an override matrix pointer, joint float 51 a constraint | `FFX_Ch_BuildJointOverrideMatrices 0x832110` for magic 437 and 438, and job `sub_82E390` for chr ids 3 and 103 (StepAll loop 4). step | BuildSkinMatrices uses the override in place of the computed local | presence discrete, matrix continuous | hold presence. Interpolate the matrix when it is present in both snapshots | presence change | V | no: F8 keeps the override maths on the CPU. The resulting matrix is an input to the GPU pose |
| skin palette | `PMeshInstance` field 3 + 64*j, through the joint remap. The SDK 3.21 header has a `PArray<PMatrix4> m_currentPose` in that position [I, 3.21 not 3.9] | BuildSkinMatrices builds inverse bind times joint world into a 0xC104-byte stack buffer, then `FFX_ChrInstance_UploadBoneMatrices 0x63BE60` copies into m_instance and m_instance2. Skipped on `g_ffxIsCatchUpStepRender`. job-in-step* | the skinning pass | derived | rebuild per present from the interpolated pose | inherits | V. Special cases: the c102 hack, and model 8298 skips joints 66 and up | yes: display only, skipped on catch-up, nothing in the step reads the PMeshInstance pose |
| blink and head sway | CHR+0x4D4 m_blinkState, bone 13 rotation | `FFX_Ch_UpdateBlink` in the render job, using CRT `rand()` on a JobWorker thread. job-in-step | eye texture or UV, and the joint locals | blink discrete, sway continuous | hold blink, lerp sway | none | V. The multithreaded MSVC CRT keeps rand state per thread, so which worker ran the job decides the value [I]. A hidden RNG consumer | no: worker rand() and the sway feeds joint locals the step reads |
| look-at | head and neck joint locals | `FFX_Ch_UpdateLookAt` in the render job | joint locals | continuous | slerp with the joints | none | V call, I detail | no: writes joint locals the step reads |
| hide flags | CHR+0x180 m_hideFlags: 0x01 Ev, 0x02 Eff, 0x08 Disp, 0x10 Btl, 0x20 Z | scripts, battle, and the view cull row. `FFX_Ch_UpdateMotionAll` clears 0x08 and 0x20 before its own gate (0x832E5C, `and byte ptr [esi+180h], 13h`) | `FFX_ChrInstance_SetHidden 0x63C390` links or unlinks in StepAll loop 3, and every StepAll loop gates on hideFlags == 0 | discrete | hold | hidden to shown is a cut for that CHR's transforms | V | no: step state |
| skip-render flag | CHR+0x198 m_flags2 bit 0x20 (bit 0x10 is shadow) | scripts and battle. StepAll loop 2 sets flags2 bits 0x50 | StepAll loops 1 and 4 | discrete | hold | as hide flags | V | no: step state |
| view cull (Disp and Z bits) | m_hideFlags 0x08 and 0x20, m_cameLen CHR+0x178, m_clipZ CHR+0x174 | `FFX_Ch_UpdateCameLenAndZClip 0x82E220` in StepAll loop 3, frustum test against camera gfx+220 field +0x74. step | through the hide flags | discrete | the recreation culls in the renderer against the interpolated camera, and keeps these bits for the simulation only | none | V. Catch-up uses a stale camera, section 15 | no: the Disp and Z bits are simulation visible (F8 names 0x82E220). A separate render-side frustum cull is yes |
| part alpha and part hidden | CHR+0x1BC m_parts, CHRPART stride 56: +0x14 m_alpha, +0x18 m_hidden | scripts and battle. step | `sub_63BC80` per part (m_alpha times shade alpha) in StepAll loop 3. step* | alpha continuous, hidden discrete | lerp alpha, hold hidden | none | V | no: a few scalars per part, nothing to gain |
| shade colour and alpha channels | seven CHRANIMF {cur, target, mode} from CHR+0x330: shadeR, shadeG, shadeB, shadeA (+0x354), anim360, shadeCoeff (+0x36C), +0x378 | stepped in `FFX_Ch_UpdateMotionAll`, set through `FFX_Ch_SetShadeColorAnim` and `FFX_Ch_SetShadeCoeff`. sub-step | StepAll loop 3, step*: colour = shade RGB times a factor from anim360 or shadeCoeff with per-map formulas, sent by `sub_63BB30` ("MaterialColour"). `sub_651400` switches the pass to FakeTransparent when alpha < 1 | continuous | lerp. Pick FakeTransparent when either snapshot has alpha < 1, so the pass does not flip in the middle of an interpolation | none | V | no: CHRANIMF is step state, the result is a few scalars |
| shade constants | `CharacterShadeCoeff`, `CharacterShadeColor` material constants | `FFX_Ch_UpdateShadeFromLights 0x834110` (render job) -> `FFX_ChrInstance_SetShadeCoeff 0x63CDD0`, `FFX_ChrInstance_SetShadeColor 0x63D050`, `maybe_FFX_ChrInstance_UpdateShadeConstants 0x63CEC0`. The old-walkmesh branch is gated on `g_ffxIsCatchUpStepRender`. job-in-step | GPU | continuous | lerp | map change | V writer, I reader | no: a few scalars per CHR |
| point-light accumulation | CHR+900..1024 (0x384..0x400) | `sub_8305A0` in StepAll loop 4 sums the 44-byte light records of `dword_13008CC` (count `dword_13008D0`) by distance, then `sub_63CB80` uploads the light matrices. step* | GPU constants | continuous | lerp the accumulated colours and matrices | map change | V. Light 0 can be written per present, F2 item 7 | no: written into CHR+900..1024 inside the CHR record, which the step owns |
| specular params and camera vector | `SgSpec_Color`, `SgSpec_LightVec`, `SgSpec_CameraVec`, `SgSpec_LightMat` material params on parts with skin flag 8 | `sub_643CC0` per part in StepAll loop 4. The camera vector comes from `FFX_PlayerCam_GetEyePos`, the player camera, not the active camera. step | GPU | continuous, camera-baked | recompute per present from the interpolated player camera eye and the interpolated CHR | camera cut | V | partial: the camera vector can be formed in the shader from the interpolated eye, the rest is step state |
| mesh group membership | gfx+0xFB48 list, ChrInstance +20 shown, +21 layer | `sub_63A010` -> `sub_656590` from StepAll at 0x82F9ED. step* | `FFX_GfxCtx_LinkMeshGroupLayer 0x6589E0` at 0x669AF3 | discrete | hold the last non-catch-up list | none | V | no: discrete step output |
| blob shadow quad | ChrShader buffer `dword_CCCA74`, four world-space vertices per CHR, set index from `sub_63FF50` (the DynGeoMem set) | reset by `sub_643DC0`, built by `sub_830240` -> `sub_82E3E0` -> `sub_831D10`, submitted by `sub_643DD0` -> `sub_69F9D0`. Shadow mode CHR+0x183 m_b183 with per-map overrides. step | the shadow mesh draw | continuous, no identity besides CHR order | rebuild per present from the interpolated CHR position and the snapshotted ground height | as position | V | yes: four vertices per CHR from the interpolated position, display only. The ground height input comes from the step |
| shadow pass | `FFX_Ch_ShadowPassAll 0x830180` in the effects pass k == 0, shadow mode 1 | step | shadow cluster gfx+0x28C4 | derived | rebuild | inherits | V call, skimmed | partial: a render pass, skimmed |
| sky projection offset | gfx+63932 | `sub_65D860`, called in StepAll's CHR loop when Tidus is in use: Tidus's projected screen Y, clamped to 0.035..0.295, with map special cases. step | `maybe_FFX_Gfx_ApplyProjOffsetToPhyreCamera 0x6507D0` for camera +2016 (cluster +140, preOpaque and preTransparent), per present. Also read live by the kind 3 prim job | continuous, camera-baked | recompute per present from the interpolated Tidus position and camera, or lerp | camera cut | V | no: one scalar, and the kind 3 prim job reads it live |
| cloth vertex clone | CHR+0x834 m_clonedClassChar (a 560 byte cloth clone record, CHR_CLUSTER.md 8), flags1 0x200000 | `FFX_Ch_UpdateBoneMatrices 0x8314C0`, `sub_831480` (hide flags), `sub_830B90`. step | the clone's instances | as the joints | as the joints | inherits | V calls, skimmed | partial: as the joints |
| time-sliced flags1 0x400 | CHR+0x194 bit 0x400 | `FFX_Ch_TimeSlicedFlags1Bit10`, round robin on profiler ticks that come from `g_ffxPs2HwRegSink` (deterministic, not the wall clock). step | not traced | discrete | hold | none | V writer, meaning open | no: step state |
| character texture animation | TextureAnimationInfo entries of the character materials | `setAnimatedMaterialsChar 0x6B3060` and `sub_6B33C0`, reached from the render job path. The autoplay frame advances per present, see section 6 | `sub_6B44A0` per present | discrete | hold, drive from the step clock | spawn | I, skimmed | yes: frame select by snapshot time, display only |
| character light set (pass command 3) | `unk_CE6408`, 8 slots of 300 bytes, ids in `dword_CE63E4` | `sub_6E5C90` via `sub_642750` (step*) from `sub_A6D380` / `sub_A6D670` | `sub_6E5A40` from `FFX_GfxCtx_ExecPassCommand`, which frees the slot | continuous | lerp per slot id | spawn, map change | V, details in section 10 | no: small per-slot constant build, the lighting itself already runs in the shader |

Two more writes at the top of `FFX_Ch_StepAll` belong to this flow. `sub_640DF0` recomputes the
PCameras from gfx+64004 through `sub_6720C0`, and an `FFX_Mtx_Mul` writes `unk_1300820`, whose
product has no reader found [V call, I dead]. StepAll loop 4 also sets flags1 0x1000 as a once per
step guard and runs the one-shot `sub_63B290` [V].

## 4. Camera

Exhaustive for the main field and battle camera. `FFX_Came_StepAll 0x7BE110` runs per sub-step.
`FFX_Came_ActivateScreen 0x7BC110` runs about three times per step (two passes plus the reselect),
and each call advances its own tweens [V].

| value | lives at | writer(s), rate | render reader(s) | kind | rule | cut signal | mark | GPU candidate |
|---|---|---|---|---|---|---|---|---|
| eye and reference point | slot+0x50 committed position, slot+1628 live eye, slot+64 live reference, slot+0 up | `FFX_Came_StepAll`, mode handlers per sub-block, smoothing filters with divisors at record+112..124, commit, then live = committed + shake (`sub_7C1480`). sub-step | through the view matrix | continuous | lerp eye and reference, rebuild the look-at per present | active priority change: slot+3212 differs from the active priority, `sub_7C0D40` runs and the filters are skipped | V | no: step state read by camera code. One matrix per present, nothing to gain |
| camera override | `g_ffxCameOverrideFlag`, override matrices | `FFX_Came_SetMatrixOverride 0x7C06D0` from `FFX_Fmv_ApplyMovieCamera 0x76EDC0` and from the rcBg camera `sub_920C00`. step | forces the position and the view | discrete switch, continuous inside | lerp inside, snap on entry and exit | override on or off | V | no: step state |
| direct look-at | `g_ffxCameDirectEyePos` and its partners | scripts, step | the direct branch of ActivateScreen | continuous | lerp | entering or leaving direct mode | V | no: step state |
| shake | three channels per sub-block | `sub_7C50F0` using `FFX_Rand_Stream(9)`, in StepAll and in each ActivateScreen call. sub-step | added into the live eye | continuous but noisy | lerp (it is part of the eye) | none | V | no: consumes rand stream 9, simulation |
| roll | slot+3216 live, slot+3220 target | StepAll, wrap-aware smoothing. sub-step | look-at roll | continuous angle | shortest-arc lerp | as eye | V | no: step state |
| screen depth (fov source) | slot+3228 ScrDpt, copied to screen+0x1C | StepAll, and the direct and override paths. sub-step | `FFX_Came_PushViewToRenderCtx 0x7BC980` turns it into fov = 2*atan(...) | continuous | lerp ScrDpt, derive the fov per present | as eye | V | no: step state |
| screen rect tweens | the screen record rect, 4 tweens per sub-block | `sub_7BDA80` in each ActivateScreen call, so about three advances per step | the viewport of that screen | continuous | lerp | screen activation | V | no: step state |
| screen time | screen+348, += rate | each ActivateScreen call | screen effects | continuous | lerp | screen activation | V | no: step state |
| projection and view matrix | `g_ffxCameProjMatrix 0x2311480`, `g_ffxCameViewMatrix 0x2311440` | ActivateScreen (`FFX_Came_BuildProjFromScreen`, look-at from slot+1628, slot+64, slot+0, roll slot+3216). step | not read by the presenter. Read live by the VFX prim jobs (job-after) and by effects at step | derived | build per present from the interpolated inputs. Keep the step copy for the simulation and for baked effect geometry | inherits | V | no: read by the step's effects and baked geometry |
| projection pixel offset | gfx+64052, gfx+64056 | `FFX_Gfx_SetProjPixelOffset` from ActivateScreen (the override path sets only this). step | `maybe_FFX_Gfx_ApplyProjOffsetToPhyreCamera 0x6507D0`, and the presenter at 0x669013, which pre-multiplies cam+52 and restores it afterwards | continuous | lerp | as eye | V | no: one value |
| screen origin | `flt_CCB228`, `flt_CCB22C` | `FFX_Gfx_SetScreenOrigin` from ActivateScreen. step | not traced | continuous | lerp | as eye | V writer, I reader | no: one value |
| field of view and aspect | PCamera+340 fov and +336 aspect, on the cameras at gfx+220, +1620 and +568 | `maybe_FFX_Gfx_SetCameraFov 0x66CDA0` (per-map fov hacks inside) and `maybe_FFX_Gfx_SetCameraAspect 0x66CBE0`, from PushViewToRenderCtx, non-override non-direct branch only. step | the PCamera at present | continuous | lerp | as eye | V | no: one value |
| main camera world matrix | gfx+64004..64048 (3x4) | `FFX_Gfx_SetCameraWorldMatrix 0x639860` from PushViewToRenderCtx. Returns early on `g_ffxIsCatchUpStep`. step* | the presenter prologue points camera +220 at it and calls `sub_6720C0` (view and frustum rebuild) per present. Also `sub_640DF0` at step | continuous | snapshot the slot inputs, not this matrix. If it has to be snapshotted, decompose into position and quaternion and slerp | as eye | V | no: one matrix, and the step reads it back through sub_640DF0 for the cull |
| sub-screen camera matrix | gfx+69012, used when gfx+69004 == 1 | `FFX_Gfx_SetCameraWorldMatrix2 0x638AA0` (256-wide sub-screen). step | the presenter points camera +568 at it, and the opaque pass splits into two viewports | continuous | as the main camera | as eye | V | no: one matrix |
| slot camera matrix | gfx+64060, live flag gfx+64161 | `FFX_Gfx_SetCameraWorldMatrixSlot 0x66CE70`, via `sub_639B30` for magic 152 and 601, and from ActivateScreen and PushViewToRenderCtx. `sub_639B30` clears the flag. step | the presenter points camera +916 at it, `sub_653230` crossfades | continuous | as the main camera | flag change | V | no: one matrix |
| guide map camera | gfx+1968 | `sub_66D940` from `sub_642410` <- `sub_91DA80` <- `FFX_Field_DrawGuideMap 0x872CA0`. step | the presenter points camera +1620 at it | continuous | lerp | guide map open | V | no: one matrix |
| rcBg camera | section-0 camera gfx+2016 | `sub_920C00` (override, fov, aspect, world matrix), sub-step | `sub_650F80` preOpaque and preTransparent over cluster +140 | continuous | lerp | scene change `sub_6587D0`, map load | V | no: CRT rand sway at step |
| Sphere Grid camera | gfx+3040 | `sub_642DA0` -> `sub_66CA60` from `FFX_Menu_Step 0xA53570`. step | not traced | continuous | lerp pan and zoom, wrap-aware angle | Sphere Grid open, character switch | V writer, I reader | no: step state |
| depth of field from camera | `dword_1300758`, `dword_130075C` (PS2 Z words), flags `dword_1300768`, `dword_130076C` | slot+3240..3252 through `sub_82CE20` / `sub_82CB60`. step | see section 10, pp+74693..74700 | continuous | lerp | none | V write, I mapping | no: step state |
| VFX camera copies | `g_ffxVfxViewMatrix 0x230FF60`, `g_ffxVfxProjMatrix 0x230FE60`, `g_ffxVfxInvViewMatrix 0x230FCA0`, `unk_230FC20`, `flt_230FBE0` | `FFX_Vfx_SetCameraMatrices 0x762550`, `FFX_Vfx_BuildPhyreCameraMatrices 0x711910`, per live screen in the step | not read by the render. Baked into effect geometry | derived | do not use for the render | as eye | V | no: step only |
| character draw view and projection | `g_ffxChDrawViewMatrix 0x1300938`, `g_ffxChDrawProjMatrix 0x13008F8` | `FFX_Ch_SetDrawViewMatrix`, `FFX_Ch_SetDrawProjMatrix`, per live screen in the step | step-side consumers only | derived | n/a | n/a | V | no: step only |
| magic draw-env matrices | `dword_18DED38`, `dword_18DED78` / B8 / F8, scale `flt_18DEFF8` | `FFX_Magic_SetProjMatrix 0x90A980`, `FFX_Magic_SetViewMatrix 0x90A140`, `FFX_Magic_SetWorldScale 0x90A950`, per step unless `g_ffxMagicDrawFlags & 0x800` | step only | derived | n/a | n/a | V | no: step only |
| FMV movie camera | keyframes at `dword_CE82AC`, 168 bytes each, indexed by the FMV frame | `FFX_Fmv_ApplyMovieCamera`, step, only for movies 46, 72, 32 and 70 | through the override | continuous per video frame, keyed on a discrete index | hold per step | keyframe jump, no flag | V | no: the step reads the index |

What the presenter does with the camera per present [V]. It points each PCamera's world pointer at
its gfx slot (+220 to gfx+64004, +916 to gfx+64060, +568 to gfx+69012 when gfx+69004 == 1, +1620
to gfx+1968), calls `sub_6720C0` on each, copies the main projection into env+64..+127
(`sub_453340`), applies and restores the pixel offset, and reads the clear colour from env floats
12..14. Nothing accumulates across presents on the camera side. The SDK 3.21 `PCamera` keeps the
world as a `PWorldMatrix *m_localToWorldMatrix`, which fits the re-pointing seen here [I, 3.21].

## 5. Effects and particles

In retail the effects pass runs once per step. `FFX_Vfx_StepAll`'s per-slot loop is dead in retail,
because `sub_761D60` (from `FFX_MainInit` at 0x820976) sets `dword_230FD38` to 1 and only the PM
remote tool message clears it. Retail effects are driven by magic DLL overlay calls and by
`FFX_MapFx_UpdatePlacedEffects 0x729AB0` [V, agent, spot checked].

Effect geometry is built in two halves. At step, producers allocate DynGeoMem (`sub_63FDB0`), queue
a prim build slot that captures matrices including the step camera, and append the draw item. After
the step loop, `FFX_Vfx_FlushPrimBuildQueue 0x71EDB0` runs the slots, inline when cheap, otherwise
as six JobWorker jobs. The jobs fill vertex buffers and, for kinds 0, 1 and 3, also write the world
matrix through `sub_645A50` -> vf10, the sort depth from the live `g_ffxCameProjMatrix`, the mesh
link and material constants. I checked that `sub_645A50` is called from `sub_71F320`, `sub_7256C0`
and `sub_727110` [V].

| value | lives at | writer(s), rate | render reader(s) | kind | rule | cut signal | mark | GPU candidate |
|---|---|---|---|---|---|---|---|---|
| model world matrix, kind 0 | transform record (208 bytes, geom+36) +0 | job `sub_71F320`: slot+468 inverse view times slot+32 local. job-after | `ClassDynamicGeometry__vf02 0x6A4A50` -> `FFX_Gfx_SetMeshTransform`, drawn by `sub_650670` at 0x669EAB | continuous | lerp and slerp per u64 model key (the camera cancels out of the product) | spawn `FFX_Vfx_CreateModel 0x644DE0`, kill via `sub_69FC10` | V | yes: built after the step loop from captured slot data, the step never reads it back (+193 skips vf10) |
| model world matrix, kinds 1 and 3 | transform +0 | jobs `sub_727110` (slot+208) and `sub_7256C0` (slot+176), the inverse of the step camera. job-after | as above | camera-baked | hold | as above | V | yes: as kind 0, built in the job from captured slot data |
| world matrix, kind 2, EvTex, BtlExplode, kind 4 | transform +0 | at step: `sub_A5C370`, `sub_7196F0` (EvTex slot matrix), `FFX_BtlExplode_StepFragments 0x805510` and `sub_A6E2E0` (identity) | as above | EvTex and kind 2 continuous, the rest identity | interpolate EvTex and kind 2 per key, hold the rest | as above | V | partial: kind 2 and EvTex matrices are built at step submit (CPU), the identity cases need no work |
| zMatrix and zCrct | transform +64, flag +128 | kind 0 job (inverse view times slot+96 when the slot byte +1 is set), `sub_7196F0` | vf02 constants "zCrct", "zMatrix" | matrix continuous, flag discrete | matrix as +0, hold the flag | flag toggle | V | yes: part of the kind 0 build |
| job-built flag | transform +193 | set by the producers `sub_718610`, `sub_71B8A0`, `sub_71C0B0`, cleared only at create | `FFX_Vfx_SubmitModel 0x6458A0` skips vf10 at step | discrete, sticky | hold | n/a | V | no: discrete |
| sort depth | VFXDynamicGeometry +220 (+0xDC) | job via `sub_645A50` -> `sub_6A3B20` from the live `g_ffxCameProjMatrix`. Step via `FFX_Vfx_SetSortDepth 0x645750` | `sub_654650` at 0x669DD9 | continuous, order only | recompute per present from the interpolated transform and camera. Never lerp across a reorder | n/a | V | yes: render-side ordering only |
| vertex data | DynGeoMem (`dword_CDEDF0`), part entry at transform+148 with per-segment records at +44 + 28*seg | alloc at step, filled by the jobs or directly at step (0x805510, `sub_7196F0`, `sub_7F49E0`). Rewound before every step by 0x63FD80 | Phyre mesh streams, reader not found | kind 0 static local, kinds 1 and 3 view space, BtlExplode and kind 4 world space | kind 0 via its matrix, the rest hold | n/a | V alloc and fill, I reader | partial: the kind 0, 1, 2, 5 and 6 builds are yes. The kind 3 counts are read by the next step (F2-1) and stay on the CPU |
| kind 0 vertex colour scale and UV scroll | prim slot +176..+188 (colour), +160 and +164 (UV scroll) | producer `sub_718610`, step | baked into vertices by the job | continuous, UV wraps | hold, or keep prev per key and lerp the unwrapped UV | wrap | V | yes: baked into vertices by the build job |
| material constants | VFXDynamicGeometry (248 bytes): colBs +68, colSs +72, lv +76, LvMatrix +84, LcMatrix +148, MdiLight +212, YIQ +216, uvOffset +224, rgbaPower +232 | `sub_6A2FD0` via `sub_6456D0` (magic 519 only), `sub_6A3A20` via `sub_645350` (event 392). uvOffset and rgbaPower stay at the constructor values 0 and 1 | `VFXDynamicGeometry__prepareConstants 0x6A53E0` | continuous | hold (rarely driven) | n/a | V, writer list partial | no: a few scalars |
| ScreenShift and cameraFadeout | transform +132 / +136, +183 | +132 set at 0x71473F, +136 and +183 only zeroed at create | vf02 "ScreenShift", "doCameraFadeout" | constant in practice | hold | n/a | V scan | no: constant |
| linked and visible | Phyre node+0x1C | job `sub_645A50` -> `sub_6A49E0` -> `FFX_Gfx_LinkMeshToNode 0x66D910`. Step `sub_645710` | cluster walk | discrete | hold | spawn, kill | V | no: discrete |
| draw item lists | gfx+0x38CC and +0x68CC | `VFXDynamicGeometry__submit 0x6A52F0` at step*, reset at 0x820F64 | `sub_654650`, `sub_650670` | discrete | hold the latest | implicit | V | partial: membership is step output, the sort and merge is GPU work |
| batch marks | gfx+0xF8D4, +0xF914 | `FFX_Vfx_EndPass` once per k pass, `sub_7EC7E0` (magic 154). step* | `sub_654650` | discrete | hold | n/a | V | no: discrete |
| effect local time | effect slot +8 (prev +12), rate +16 | `sub_712250` / `sub_7122F0` per effects pass, frozen while `dword_230FD34` | step only | continuous fixed point | n/a | restart | V | no: step state |
| effect point lights | `dword_13008CC` 44-byte records (pos +0, rgb +16, radius +32, falloff +36, enabled +40, group +41) | `sub_75D7F0` via `sub_8319B0`, commit `sub_729F90`, ATEL Sg 035/037/042. step | `sub_8305A0` (characters, step*) | position and colour continuous, enable discrete | lerp per record while enabled in both | count or enable change | V writers, I reader set | no: read by sub_8305A0 into the CHR record |
| spark-trail particles, kind 4 | heap ring {count, head, 32-byte records: pos, colour, velocity, age} | emitter `sub_A6E5D0` at step. Advanced in job `sub_7287B0` (pos += vel, age++), once per `FFX_MainStepLoop` | built in the same job | continuous, but simulated per present | hold. The recreation moves the advance into the step | ring overwrite | V | partial: the segment build is yes. The advance has no step reader found, so it can move only if the hash excludes the ring, otherwise it goes into the CPU step |
| model matrix overrides | gfx+0x10D84, +0x10D88 | cleared at 0x63DAC0 at the top of every step, pushed by `sub_639170` from battle op `sub_80EB50` (CHR part matrices times 1000) | `sub_66B360` | continuous, presence discrete | interpolate per mesh instance when present in both, else snap | presence | V | no: built at step from battle CHR part matrices |
| pass command 4 specular | `dword_CDEC1C`, 4 x 356 bytes | `sub_6C5B80` and siblings, step* | `sub_6C59C0` | continuous | lerp per slot | n/a | V | no: small |
| pass commands 5 and 6 | gfx+0xFB54 | `sub_644CA0`, `sub_644CF0`, step* | `sub_666E40` | discrete | hold | n/a | V | no: discrete |
| EvTex sprites | `word_1327148`, 128 x 28 bytes | ATEL 282..288, 297, 298, 575. Drawn at k == 0 by `FFX_EvTex_DrawSprites 0x872C90` -> `sub_7196F0` | gfx+0x3860 list | position, scale, RGBA continuous | interpolate per slot | slot enable | V | partial: slot state is ATEL state (CPU), the quad expansion is GPU work |
| BtlExplode fragments | fragment array, 192-byte stride | 0x805510: simulated every step including catch-up, vertices built only on non-catch-up steps | ordered list | continuous | hold, or keep a prev per fragment | explode start | V | partial: the fragment simulation runs every step and stays on the CPU, the vertex build is GPU work |
| screen-space effect quads | DynGeoMem | `sub_7F49E0`, step* | render | screen-baked | hold | n/a | V | yes: display-only geometry |
| effects sub-step counter | `g_ffxEffectSubStepCounter 0x23328D4` | 0x7FF6A6 once per effects pass, step | step readers (pulses on & 0x1F) | discrete | hold | n/a | V | no: step state |
| DynGeoMem segment | set {+4 index, +8 count, +16 array, +20 mutex} | `FFX_DynGeoMem_FlipFrameSegment 0x63FF70` from endFrame per present, rewind per step | the render thread reads segment N while the main thread writes N+1 | discrete | the recreation's snapshot owns its vertex memory | n/a | V | n/a: becomes the recreation's GPU buffer ring |
| deferred model release | `dword_CCCA6C` registry (+20 countdown) | `sub_69FC10` sets the countdown to 3, `FFX_DynGeoModel_StepDeferredRelease 0x645A40` ticks it on non-catch-up steps | memory lifetime | discrete | key the delay to presents in the recreation | kill | V | no: lifetime bookkeeping |
| rotating model key | `qword_C48C10` 1..5 | `sub_7EC7E0` bumps it each call | key match | identity changes every call | hold, the key never matches | n/a | V | no: identity bookkeeping |
| draw layer | `byte_C322AA` -> geom+81 | `FFX_Glue_SetDrawLayer 0x645670` | submit filter | discrete | hold | n/a | V | no: discrete |

## 6. Map, background and texture animation

| value | lives at | writer(s), rate | render reader(s) | kind | rule | cut signal | mark | GPU candidate |
|---|---|---|---|---|---|---|---|---|
| map node translation (track kind 0) | 180-byte node records in mapWork (+428012), pushed to Phyre mesh+4 floats 8..10 as (-x, -y, z) | `sub_914A10` from `FFX_Map_StepAnims 0x908320` -> `sub_6426C0` -> `sub_66E3E0`. step. Re-pushed by `sub_90BF10` when gfx byte +68909 is set | retained cluster +0x10D4 in list +172, opaque pass `sub_64FBB0` | continuous | lerp | map load (gfx+4501 set). A track restart is not a cut | V | no: tens of nodes, and the evaluated values live in mapWork (step state) |
| map node rotation (kind 1) | node +427996 euler, wrapped to [-pi, pi] by `sub_915120`, order at +428050 | `sub_914A10` -> `sub_6426A0` -> `sub_66DB00`. step | as above | continuous | slerp, or shortest-arc euler lerp. Mind the wrap at pi | map load | V | no: as translation |
| map animation time base | `sub_908970` = 2 x `g_ffxMainStepCounter`, or `sub_908F70` = `g_ffxGameClock60Hz`, chosen by `dword_193BB08` | step | step evaluator only | discrete counter | evaluate at step | pause `byte_12FBBBB` shifts start times | V | no: step state |
| node morph weights (kind 2) | node +428068 / +428072 -> animator `dword_CCCA90` | `sub_914A10` -> `sub_6436E0` -> `sub_6ABFC0`. step | applied per present by `sub_6ACD20` (from 0x657184, only while gfx byte +4501 == 0) as MorphWeight, UVMorphWeight, UvOffsetX/Y | continuous | lerp | map load | V | partial: the weights are step state, the per-vertex morph already runs in the shader |
| vertex colour morph weight | animator +8 / +12 | `sub_90A4D0` -> `sub_643700` -> `sub_6AC0B0` (ATEL Core 417..420). step | `sub_6ACD20` per present | continuous | lerp | map load | V | partial: as morph weights |
| animator UV offset accumulator | animator +24 / +28, accumulated dt at +32 | `sub_6ACD20` itself, by present dt | the same | present-rate | move to the step clock if determinism matters | none | V | yes: present-rate and display only, becomes a shader uniform from snapshot time |
| map texture animation frame | mapWork +645576 (bit 0 pending), frame +645580 -> TextureAnimationInfo +24, +28 autoplay cleared | `sub_914A10` sets pending. `sub_90BF10` -> `sub_90BDB0` -> `sub_643A40` -> `sub_6B42C0`. step | `sub_6B44A0` (writes the TextureSampler through `sub_66E4F0`), per present from the `FFX_DebugMenu_drawIfActive` region | discrete | hold | map load | V | partial: the frame number is step state, the sampler select is display work |
| texture animation autoplay frame | TextureAnimationInfo: +16 frame duration, +20 accumulator, +24 frame, +28 autoplay | `TextureAnimationInfo__advance 0x6B4450` via `sub_6B47E0(dt)`, per present dt | `sub_6B44A0` per present | present-rate discrete | drive it from the step clock or the snapshot time | map load, character unload | V | yes: display only, no step reader of +24 found |
| rcBg layer texture frame | rcBg layer record (192 bytes) | `sub_920AA0` -> `sub_920B00` -> `sub_643A20` / `sub_643A00`. sub-step, only when `g_ffxMovieScreenMode` == 0 | `sub_6B44A0` | discrete | hold | scene select, map load | V | partial: as the map texture frame |
| rcBg layer hidden | "scene%dlayer%dShape" mesh link, plus MaterialColour alpha 0 | `sub_920B00` -> `sub_638F30` -> `sub_65F050`, and `sub_644120` -> `sub_66D5C0`. sub-step | scene traversal | discrete | hold | none | V | no: discrete |
| rcBg layer material alpha | MaterialColour of the layer | `sub_644120` -> `sub_66D5C0` from rcBg, `FFX_DreamFilter_Update`, MapFx 015/016. step | material | continuous | lerp | layer show or hide | V | no: one scalar |
| rcBg layer scroll | "layer%d" mesh 12-float block, floats 8 and 9 accumulate | `sub_92ADE0` -> `sub_639D60` -> `sub_658570`. sub-step | scene draw | continuous | lerp, mind wraps [I] | scene change, map load | V | no: step accumulators, one offset per layer |
| rcBg scene index | gfx+4568 | `sub_6587D0`, step and render install | scene traversal | cut | hold | itself | V | no: cut |
| rcBg camera sway and roll | rcBg +184 / +188 sway, +172 roll | `sub_9210E0` (sine table, CRT `rand()` amplitude), `sub_920580`. sub-step | through `sub_920C00`, section 4 | continuous | lerp | none | V. Hidden RNG consumer | no: CRT rand at step |
| layer visibility mask | mesh +72, previous mask gfx+64276 | `sub_90BF10` -> `sub_6423D0` -> `sub_66D610`. step | scene traversal | discrete | hold | map load | V | no: discrete |
| map draw enabled, map visible | `g_ffxMapDrawEnabled 0x12FBBB1`, `g_ffxMapVisible 0x12FBBB9` | `FFX_SetMapDrawEnabled 0x820450`, `FFX_SetMapVisible`. step | step-side gates on the map passes | discrete | hold | battle ops 7 and 8 | V | no: step gates |
| map alpha pass order | `g_ffxMapAlphaPassFirst 0x12FBBB6` | render install `sub_65B8A0` -> `sub_6E4470`, plus step writers | `FFX_MainStep` orders DrawPass1 against `FFX_Ch_StepAll` | discrete | hold | map load | V, F2 item 2 | no: F2 |
| clear colour | env floats 12..14, base copy gfx+68912..68920 | render install `sub_65B8A0`, scaled at step by `sub_66D1D0` / `sub_66D3B0` | presenter clears at 0x668E69, 0x669119, 0x6692A5, 0x6696A2 | continuous | lerp | map load | V | no: F2 base copy, one value |
| map colour scale | gfx+64632..64644 -> material "clrScaleFactor" | `sub_66D1D0`, `sub_66D3B0` (magic and battle). step | materials and the clear | continuous | lerp | magic start or stop can snap [I] | V | no: the step reads the base clear colour |
| fog colour, near, far, curve, limit | env floats 4..6, env+32, +36, +40, +44 (also reached as `dword_CE5170`+160) | install `sub_66EE70` (render). Step: `sub_6E36F0`, `sub_6E3B10`, `sub_6E3A30`, `sub_6E3760`, `sub_6E3AA0` from MapFx 055..065 and `sub_90A350` | fog shader constants, `sub_694F10` via pp+0 | continuous | lerp | map load | V writers, I object identity | no: a few scalars |
| fog effect weight "fgEff" | fog manager +172 value, +176 time, +180 mode | mode `sub_642F10` from MagicFile start and stop (step). Value `sub_6E3F40` by present dt -> `sub_6E35C0` | fog shader | present-rate | none, or move to the step clock | mode change | V | yes, trivially: a shader uniform from snapshot time |
| post params from GlobalCamera | env+128..152 | `sub_66F140` at render install | post passes | continuous | hold per map | map load | V | no: config per map |
| screen wave grid | Distortion `dword_CDEB88`: colours +108, UVs +112 (9x9), strength +120 | `FFX_Map_DrawScreenWave 0x908550` -> `sub_9131D0` -> `sub_913200` -> `sub_644940` -> `sub_6C3240`, strength via `sub_642F30`. step, gated on `g_ffxMagicDrawFlags & 0x1000000` | `sub_6C2DC0` at 0x66A494 | continuous | lerp the grid per vertex, not the phases | wave on or off, mapWork+778360 | V | partial: the 9x9 grid build is display only and data parallel, the phases stay on the CPU |
| screen wave phases | mapWork +778368, +778372 and parameters to +778408 | `sub_913200`, step, catch-up included | through the grid | continuous, unbounded | do not lerp the phase, lerp the derived grid | as above | V | no: step state, advanced on catch-up steps too |
| guide map geometry | `GuildMapDynamicGeometry` (vtable 0xB47E70, vf02 0x6A5070) | `FFX_Field_DrawGuideMap` -> `sub_919D00` -> `sub_91DA80` (skimmed) -> `sub_656A00`. step | `sub_650340` 2D pass | transform continuous, presence discrete | lerp the transform, hold presence | guide map open or close | V, skimmed | partial: a dynamic mesh, display only, skimmed |
| map scene membership | cluster +0x10D4 in list +172, +0x1128 in list +140 | unload `sub_6403B0` at step. Load `sub_640210` -> `sub_65CE90` (sets gfx+4501) -> job -> worker callback sets gfx+4500. Linked at render start by `sub_6566C0` in `sub_658AE0` (called at 0x668961) | opaque `sub_64FBB0` (+172), `sub_650F80` (+140) | cut | none | gfx+4501 set is the load, gfx+4500 the install edge | V | no: cut, and F2 |
| layer break cluster move | cluster gfx+0x11E4 between lists +124 and +156, flag gfx+0xFB44 | `FFX_GfxCtx_PushLayerBreak 0x6564A0` (step), moved back by `sub_658F00` (render) and `sub_6578E0` | opaque pass and step 16 | discrete | hold | none | V, F2 item 5 | no: F2 |
| shadow cluster placement | cluster gfx+0x28C4 in +188 or +124, mode gfx+63948 | render install, `sub_65A590` (render) | +188 pass for modes 1 and 3 | discrete | hold | map load | V wiring, I content | no: discrete |
| light 0 colour pulse | `dword_13008CC` entry 0 floats 4..6 | `sub_6AB630(dt)` -> `sub_831C10` at present (0x6571C6), gated on `dword_CCCA84`+460 | read at step by `sub_8305A0` | present-rate | F2, see item 7 | none | V | no: F2, the step reads it |
| title water flipbook | `byte_CCBA09` 0..69 | `sub_65B5B0` via `FFX_Title_DrawWaterAnim 0x63E920`, step including catch-up | 2D sprite "Water_%02d" | discrete | hold | title enter | V | no: one step counter |

Dead on PC and left out of the snapshot: the PS2 map light dirs `unk_18DEEB8` / `dword_18DEEF8`
written by `sub_908D00` (no other xrefs) and the GS packet buffers `dword_13032C4` [V xrefs, I dead].

## 7. 2D, menus and message windows

The 2D list `dword_CCC81C` has two sets. `FFX_DynDrawList_SwapFrameSets 0x642320` runs at 0x820ED7
(`FFX_MainStep`) and 0x8A9DBD (`FFX_MenuSys_StepFrame`) on each non-catch-up step before any append,
so the render reads the set the latest non-catch-up step wrote [V, both call sites checked].

| value | lives at | writer(s), rate | render reader(s) | kind | rule | cut signal | mark | GPU candidate |
|---|---|---|---|---|---|---|---|---|
| 2D set toggle | `dword_CCC81C`+23420 | the swap, step* | `sub_67B970` (count), `sub_67B990` (item) | discrete | the render reads the latest set | n/a | V | no: discrete |
| 2D records (batched quads) | set A list +0 (count +11344, reader array +5616), set B list +11352 (count +22592, reader array +16968) | `FFX_DynDrawList_Append 0x67BA50` from `FFX_Sprite2D_Draw 0x63EEE0` and `sub_640660`. step* | `sub_650CB0` (0x66AC1F, 0x66AEF1, 0x653F3A), `sub_650340` (0x66AC18, 0x653F2C) | discrete membership | no stable ID. Key by producer (call site and order) to interpolate, otherwise hold | none | V, the reader array indirection I | partial: membership is step output, the batching and quad expansion are GPU work |
| 2D vertex positions | geom+12, 1920x1080 centred pixels | `FFX_Sprite2D_Draw` (PS2 512x416 mapping when a3 != 0), `sub_640660` rounds to whole pixels | through the mesh | continuous | lerp only for keyed sprites, glyphs stay pixel snapped | none | V | yes: per-present lerp of keyed sprites, display only |
| 2D vertex colour | geom+20, float4 | step* | same | continuous | lerp when keyed | none | V | yes: as positions |
| 2D UVs | geom+24 | step* | same | discrete for flipbooks, continuous for scrolls | hold, except known scrolls | none | V | yes: as positions |
| 2D texture, depth, draw kind | geom+32 name, geom+40 sort depth, item+36 -> +180 kind, item+188 glyph flag | step*, kind writer not found (corrected 2026-10-28, see the end of this doc) | `sub_650CB0`, `sub_650340` | discrete | hold | none | V reads, I writer | no: discrete |
| 2D clip rect | manager +23456 on, bounds +23480..+23492 | `sub_67E3B0` (dead in retail, corrected 2026-10-28, see the end of this doc) | culled at append by `sub_644320` (step) | discrete | hold | n/a | V | yes: the append-time clip has no step reader, a GPU scissor does it |
| menu dim level | `dword_184087C` 0..128 | `FFX_MenuSys_StepFrame` adds `dword_1840880` per menu step | black quad via `sub_8F3DB0` | continuous | lerp | `sub_8A9670` write | V | no: one scalar |
| menu background fade | `dword_1841C68` 0..128 | StepFrame adds `g_ffxModuleDrawPrio` per step | `sub_8A9510`, a dead PS2 upload on PC. Read by step logic | continuous, no PC visual found | n/a | n/a | V | no: step state |
| menu frame counter | `byte_1840884` | ++ per menu step | draw helpers via `sub_8A9C60`. Odd parity steps the menu's message windows every other step | discrete phase | derive from the interpolated step time | menu enter | V | no: step state |
| Sphere Grid open animation | `g_ffxMenuWork` +71096 counter (stops at 20), derived scales +71144..+71156, alpha +71097 | `FFX_Menu_Step 0xA53570`, step | `sub_A545A0`, `FFX_Menu_DrawAllPartsIgnoringCatchUp 0xA54660` -> `sub_A4C460` | continuous | lerp | +71096 = 0 on open | V | no: a few scalars |
| Sphere Grid pan, angle, zoom | `g_ffxMenuWork` +70424 / +70428, +70464, +70480, matrices +70496..+70880 | `FFX_Menu_Step`, step | gfx+3040 camera (section 4) | continuous | lerp, wrap-aware angle | character switch, open | V writer, I reader | no: step state |
| Sphere Grid pulse | +71076 = sin(table of +71320), +71109 | `sub_A47440`, step | Sphere Grid draw | periodic | evaluate from a fractional counter | none | V | no: one scalar |
| message window open and close | frame object: phase +6 0..4096 (682 per step, 6 steps), rect x1 at +80, state +8 | style 0: `sub_907CF0` (open), `sub_906FC0` (close), via `FFX_MesWin_StepFrame 0x906BD0`. sub-step | `sub_906BF0` -> frame+120, in `FFX_MesWin_DrawAllWindows` (skipped on catch-up) and `FFX_MenuSys_DrawAllWindows` (not gated) | continuous | lerp phase and x1 | state change, new window | V style 0, I styles 1..7 | no: step state |
| typewriter reveal | text object +160 characters shown, +176 remainder, +172 rate | `FFX_MesWin_TextPageStep 0x8DE970` / `TypewriterAdvance 0x8DF570`, sub-step, 2 characters per step by default | text object +300 draw | discrete | hold, or the floor of a lerp | `TextPageBegin 0x8DE8C0` sets +160 = 0 | V write, I reader | no: step state |
| letterbox bars alpha | `dword_C59F38` | `sub_8AB570` from the host table, instant | `sub_8F3A50` | discrete | hold | write | V | no: one scalar |
| battle HUD and battle menus | page records `0x133C950` (stride 240), cursor `dword_133D1F4`, popups `dword_133F7B4` / `dword_133F7B8` | `FFX_BtlMenu_Step 0x89AE20`. Drawn by `FFX_BtlMenu_DrawRoot 0x89B360` from `FFX_MesWin_DrawAllWindows`, so skipped on catch-up | 2D list | pages and cursor discrete, gauges continuous | hold pages, lerp gauges | page state change | I, skimmed | no: step state |
| copyright screen alpha | `g_ffxQuitScreenAlpha`, +4 per step to 128, -4 from frame 272 | `FFX_Quit_DrawCopyrightScreen 0x904A70`, step | 2D list | continuous | lerp | `g_ffxQuitPhase` | V | no: one scalar |
| dream filter overlay | sprites from `sub_63DF70`: rand UV jitter per step, dirt moved every 3 steps (`word_CCB46C`), alpha `flt_12FFAF4` | `FFX_DreamFilter_Update 0x8223A0`, step including catch-up (main thread `rand()` consumed on catch-up steps too) | 2D list | jitter discrete, alpha continuous | hold the jitter, lerp the alpha | reset `sub_822410` | V | no: the jitter consumes main-thread rand() every step |
| material morph weights | `dword_CCCA90` tables | step (section 6) | applied per present by `sub_6ACD20` | continuous | lerp | map change | V | partial: the weights are step state, the apply is shader uniform work |

Iggy (the Flash UI layer) is advanced on the Phyre render thread by its own clock, once per present:
`sub_A267F0` -> `sub_A28EB0` -> `sub_A3D9B0` queues a kind 44 callback through `sub_5B5940`, and the
render thread runs `sub_A3D9F0`, which loops `IggyPlayerTickRS` while `IggyPlayerReadyToTick` and
then draws [V location, I Iggy clock semantics].

The 31 wall-clock draw pulses listed in THREADS_AND_CLOCKS.md 2.4 are not present-rate in the
original. They are evaluated inside draw functions called from the step (`FFX_MesWin_DrawAllWindows`,
`FFX_Module_DrawAll`, `FFX_BtlMenu_DrawRoot`), once per non-catch-up step. Evaluating them at present
time in the recreation is safe and smoother [V, agent].

## 8. Fades

| value | lives at | writer(s), rate | render reader(s) | kind | rule | cut signal | mark | GPU candidate |
|---|---|---|---|---|---|---|---|---|
| Sg fade channels (5) | `0x1300708` + 16k: mode byte +0 (0..8), RGB +1..+3, counter +4, total or level +6, target +8, rate +10. Active mask `dword_1300704` | `FFX_Sg_StepScreenFades 0x82C0C0`, step including catch-up. Queues a payload via `sub_639240` -> `FFX_Gfx_QueueFadeQuad 0x6AE650` (flags 0x1001) | `RenderDelegationHelper_RunFades 0x6AE450` at 0x66A27A, 0x66A807, 0x66ACCF, 0x66AD21 -> `sub_6AE5B0` -> `sub_697490` | continuous (modes 2..6 alpha = counter / total, modes 7 and 8 a constant level) | lerp alpha, RGB constant per channel | channel start or restart (`sub_82BD10`, `sub_82BEE0`, `sub_82CC20`, `sub_82CCA0`, `sub_82CD50`, `sub_82CDC0`), end when the active bit clears | V, the setter bodies I | no: step state |
| fade quad payload | 0x20-byte delegation record | per step, but the 0x1000 records are freed before every step (0x82285B), so only the last step of a burst reaches the screen | first 0x1000 phase reached in the render | snapshot of the row above | as above | n/a | V | no: one quad, already a GPU draw |
| channel 0 level ramp (AccBuf alpha) | `word_130070E` toward `word_1300710` by `word_1300712` per step | `sub_82BD10` sets it, StepScreenFades ramps it | presenter 0x66A5CD reads it / 128 as BlendWeight | continuous | lerp | `sub_82BD10` | V | no: step state |
| ScreenFade register | pp colour +74636..+74648, blend +74652, active +74656, load hold +74660 | step, event driven: `FFX_ScreenFade_SetColor 0x643900`, `Begin 0x641E20`, `End 0x6448D0` from `FFX_MainStep`, `FFX_SaveUi_Step`, `FFX_Ch_EventjumpInit`, `FFX_BtlResult_StepStateMachine` and others. Also written by the render, F2 item 1 | `sub_697490` at 0x669C22, 0x66A2B1, 0x66A841, 0x66AD02, 0x66AD54, each firing only when active, alpha != 0 and no queued fade ran at that site. `sub_6E4EA0` uses the RGB as its clear | discrete held state | snap, no lerp | Begin and End | V | no: F2 |
| pass command kind 2 colour quad | gfx+0xFB54 record {2, drawIndex, rgba, blend} | `sub_644D40`, step*, from magic host API 352 only | `FFX_GfxCtx_ExecPassCommand 0x6671C4` | continuous alpha | lerp if the producer is stable, otherwise hold | n/a | V | no: one quad |
| battle result fade | 0x801 record | `sub_639280` from `FFX_BtlResult_StepStateMachine` | `sub_6AD380` at 0x66AD88 | one-shot | hold | n/a | V | no: one quad |
| idle fade reset arm | pp+74664 | `sub_641660`, every step from StepScreenFades | `sub_698F70` at 0x66898B (blend and alpha to 0 when not active) | flag | n/a | n/a | V | no: flag |
| load hold cover | pp+74660 and the async load state | `FFX_MainStep` forces alpha 1.0 while async loading with hold mode 1 or 2 | the ScreenFade register | discrete | snap | load start and end | V | no: step state |

## 9. FMV

The FMV frame is chosen by the step. `sub_A284A0` pops exactly one decoded frame per
`FFX_Fmv_StepPresentation 0x6D7680` call, which runs once per `FFX_MainStep` [V, spot checked]. The
decode thread has no clock in it. So the shown video frame advances per step, and the decoder can
only stall it. FMOD audio is the wall-clock part.

| value | lives at | writer(s), rate | render reader(s) | kind | rule | cut signal | mark | GPU candidate |
|---|---|---|---|---|---|---|---|---|
| video frame (Y, U, V planes) | PTexture2D at mgr+908, +1024, +1140, drawn as mesh mgr+904 | `sub_A284A0`, once per step, inline in StepPresentation, or for videos 46, 72 and 32 (mgr+1392 set) in the kind 44 job `sub_6DAE00` on the render thread, queued by `sub_5B5880` | `FFX_Fmv_DrawFrame 0x6D7570` via `sub_653960` and 0x642C4B | discrete | hold, never blend frames | `FFX_Fmv_CreatePlayer 0x6D9C80` zeroes +1752 and +1760, a skip writes 65534 | V | partial: the frame choice stays in the step, the YUV plane upload and conversion are GPU work |
| frame index, previous index, consumed count | mgr+1752, mgr+1760, player+256 (ms interval player+264) | as above, step or RT | step: `FFX_Fmv_ApplyMovieCamera` via `sub_6410D0` -> `sub_6D91B0` | discrete | hold | 65534 means past the end | V | no: the step reads it |
| "has frame" byte (the kit calls it PlaybackComplete) | mgr+1764 | set to 1 after every successful upload (0x6D769E `lea ebx,[edi+6E4h]`, the pointer goes into the job at 0x6D7775 and the 1 is stored at 0x6D7828, corrected 2026-10-27), and in `sub_6DAE00`. Cleared by CreatePlayer and the constructor | `FFX_Fmv_DrawFrame` gate. Step readers `FFX_Fmv_FirstFrameShownOrStopped 0x645DC0`, `FFX_Fmv_StepPresentation` | discrete | hold | movie start | V | no: the step reads it |
| draw aspect | mgr+1732 from mgr+1740 | `FFX_Fmv_DrawFrame` per present | same | derived per present | n/a | n/a | V | no: one value |
| movie screen mode | `g_ffxMovieScreenMode 0x12FB8FB`, prev `0x12FB8FC` | movie ATEL calls, cleared by `FFX_Fmv_ApplyMovieCamera` when the index runs out. step | step only (gates the magic overlay step, `FFX_RcBg_Step`, the effects pass) | discrete | hold | itself | V | no: step gate |
| skip prompt flag and alpha | mgr+1868, prompt object gfx+69088 (+16 alpha) | set by `FFX_Fmv_PollSkipButtons 0x6D9460` (step), cleared by `FFX_Fmv_DrawFrame` (present) when the wall-clock fade `FFX_Fmv_StepFadeAlpha 0x6E8540` ends | `sub_6E7F00` | flag discrete, alpha present-rate | n/a | n/a | V, F2 item 4 | no: F2 |
| video on an effect surface | gfx+3536 enable, +3537 frame latch, textures +3544..+3552, +4297 owned by effect | `sub_654960` via `FFX_Gfx_StepVideoPlayback 0x6442C0` (step) queues `sub_672070` (RT upload, sets +3537) | effect models 120 and up via `FFX_Vfx_SubmitModel` | discrete | hold | none | V, F2 item 5 | no: F2 latch |

## 10. Post effects and lighting

| value | lives at | writer(s), rate | render reader(s) | kind | rule | cut signal | mark | GPU candidate |
|---|---|---|---|---|---|---|---|---|
| MotionBlur enable and skip-one | MB (`dword_CDEC04`) +88, +89 | `sub_643730` from `sub_82BEE0` (host API 543), step. Also written by the render | `sub_6C5410` at 0x66A3EC, 0x66A616 | discrete | hold | enable edge | V | no: step requests |
| MotionBlur strength | MB+92 BlendWeight, step mirror `dword_130071C` | `sub_6C5600` via `sub_643750`, step | `sub_6C5410` | continuous | lerp (but section 13) | n/a | V | already GPU: the blend pass is a GPU pass, the strength is step state |
| AccBuf active and capture pending | pp+74683, pp+74682, captured target pp+74760 | `sub_639150` via `sub_82BDF0` (Sg 025), step. The presenter clears both | presenter, `sub_6593D0`, `sub_6601E0` | discrete | hold | capture edge | V | no: step requests |
| AccBuf and MB refresh request | pp+74692 | `sub_693C40` / `sub_699080`, step [I] | presenter 0x66A686 | discrete one-shot | hold | itself | V | no: step request |
| depth of field | pp+74693 enable, pp+74696 / +74700 focus depths | `FFX_Sg_StepScreenFades` every step: `sub_642C70` clears, then when `dword_130076C` and `dword_1300768` are set `sub_6430F0` -> `sub_698550` sets the focus and `sub_641670` enables | `sub_695790` at 0x669BCF, events 393..398 only | enable discrete, depth continuous | hold, lerp | the `dword_1300768` edge | V | already GPU: the pass is GPU, the focus depth is step state |
| BrokenScreen | `dword_CDEC98`: +4 capture request, +5 active, +6 one-shot, +8 alpha, +12..+40 light, +52 / +56 shards | step setters `sub_639360`..`sub_639540` from VFX vtable code. Render consumes +4 and +5 and forces +8 = 1.0 | `sub_6D4E20` 0x66898B, `sub_6D5680` 0x66A3CE, `sub_6D5670` | flags discrete, alpha and shard transforms continuous | hold, lerp | +4 spawn | V, step chain I | partial: the flags are step requests, the shard transforms and per-present shard lighting are GPU work |
| BlurEffect | `dword_CDEBA0`: +12 on, +16 warm-up, +132 phase | started by `sub_639140` (step [I]). Phase advanced per present by `sub_6C3D40` (+0.098 per call) | `sub_6C3970` 0x66A463 | present-rate | none, see section 13 | start | V | yes: present-rate and display only, the phase becomes a uniform from snapshot time |
| BlurEffectEx | `dword_CDEBFC`: +40 request, +44 mode, +60 present counter | `sub_639110` -> `sub_6C4A90`, step [I]. `sub_6C4E00` per present | `sub_6C4CF0` 0x669C89, 0x66A482 | one-shot capture | hold | request | V | partial: the request is step state, the capture is a GPU pass |
| Distortion | `dword_CDEB88`: +124 on, +125 skip, +126 rearm, +120 strength, grid +108 / +112 | `sub_642F50` (enable), the screen wave writers (section 6), `FFX_Distortion_SetSkipNextDraw 0x642F80` (+125). step. The render writes +125 / +126 | `sub_6C2DC0` 0x66A494 | strength and grid continuous, flags discrete | lerp, hold | +126 | V | partial: the grid and the feedback blend are GPU work, the enable and skip flags are step state |
| second-camera crossfade | gfx+64161 live, gfx+1264 weight, gfx+1268 latch | gfx+64161 from `FFX_Gfx_SetCameraWorldMatrixSlot` (step). gfx+1264 ramps -0.05 per present to 0.45 then +0.01 per present to 1.0 inside `sub_653230` | `sub_653230` 0x66A6C9 | present-rate | none, see section 13 | slot camera write | V | yes: present-rate weight, display only |
| character light set (pass command 3) | `unk_CE6408` 8 x 300 bytes (LvMatrix, LcMatrix, charlight, ffxLvMatrix, ffxLcMatrix, ffxcharlight, ffxshade, ambientlt, flag +296) | `sub_6E5C90` via `sub_642750` (step*) from `sub_A6D380` / `sub_A6D670`. Inputs `xmmword_11A0020`, `xmmword_11A0050`, chr object +880..+892 | `sub_6E5A40` from `FFX_GfxCtx_ExecPassCommand` 0x6671CE, which frees the slot | continuous | lerp rows (re-orthonormalise LvMatrix), lerp colours | spawn, map change | V, the chain up to the step I | no: small per-slot constant build, the lighting itself already runs in the shader |
| fog and clear colour | env object (section 6) | section 6 | presenter, `sub_694F10` | continuous | lerp | map change | V | no: a few scalars |
| ATEL fog state | `dword_18DF140` RGBA, `flt_18DF144..14C` | MapFx 055..068, battle save and restore `sub_909C60` / `sub_909B90`. step | dead GS path, host API 558 / 559 | continuous | lerp | battle swap | V | no: step state |
| config-only post | colour correction pp+74672..+74680, brightness pp+74536, HDAO pp+74704, AA mode gfx+69080, USM pp+74404..+74412, MySMScaleBias (`sub_6542B0`, per present, only when a dirty flag is set) | options, init | `sub_694F10`, `sub_695440`, `sub_695BA0`, `sub_6535B0`, `sub_697670` | config | n/a | n/a | V, USM skimmed | already GPU: config driven passes |

## 11. Debug overlay (skimmed)

`FFX_DrawDebugOverlay` runs once per `FFX_MainStep`, catch-up steps included, from 0x82157A [V]. So
the in-game debug GUI text is built at step rate and is subject to the same 2D list rules as
section 7. The presenter's debug pages are fed the measured fps and are present-rate by design.
`sub_6F69F0` cycles debug text on a 300 s timer by present dt. `sub_6DE010` (memory report, 0.33 s)
and `sub_6E50A0` (load overlay accumulators, dt clamped to 0.05) are present-rate. The recreation can
treat the whole overlay as a present-time UI that reads the snapshot and needs no interpolation.
Not read further.

## 12. Computed per present in the original

Main thread, `FFX_Frame_UpdateBoostersAndOverlays 0x657140` (dt = app+0x24) [V]:
- phase 0x2 runner `sub_6ADAD0` first and phase 0x4 runner `sub_6AD4E0` near the end. Only host API
  slot 863 can register those flags, so in practice they are magic DLL callbacks.
- `sub_6ACD20`: material morph, UV morph and UV offset apply, plus the UV offset dt integrator.
- the `FFX_DebugMenu_drawIfActive` region: the TextureAnimationManager tick `sub_6B47E0(dt)` and
  apply `sub_6B44A0`, for map, rcBg and character texture animation.
- `sub_6AB630(dt)`: the light 0 colour pulse (F2 item 7).
- `FFX_Cutscene_StepPauseOverlay`, booster icons, Iggy toasts, `FFX_Frame_StepSuspendOverlay`.
- frame-count driven: BlurEffect `sub_6C3D40`, BlurEffectEx `sub_6C4E00`.
- dt driven: fgEff `sub_6E3F40`, load overlay `sub_6E50A0`, memory report `sub_6DE010`, debug text
  `sub_6F69F0`.

Presenter `FFX_Frame_RenderScene` [V]:
- camera prologue (section 4), env projection copy, pixel offset apply and restore.
- `sub_658AE0` at render start: links pending map clusters and installs a newly loaded map
  (`sub_65B8A0`: clear colour, fog, post params, shadow mode, layer links, alpha pass order).
- `sub_698F70` idle fade reset, the choice of fade site.
- `sub_658F40` deferred mesh release countdown (gfx+64288, +64300, +64312).
- `FFX_Fmv_DrawFrame` re-draws the last uploaded frame.
- BrokenScreen shard lighting `sub_6D3650`, stateless.
- sort and merge of VFX items `sub_654650`, pass command execution `sub_666E40`, model matrix
  override apply `sub_66B360`.

Jobs after the step loop: the VFX prim build (section 5). Render thread: Iggy tick and draw, FMV
upload for 46/72/32, video texture upload `sub_672070`.

## 13. Present-rate feedback hazards

These accumulate or animate per presented frame, so the look changes with the present rate. The
recreation must move each onto the step clock or onto a fixed-rate timer driven by snapshot time.

1. MotionBlur trail: each present `sub_6C5410` copies +140 to +172 and blends with BlendWeight MB+92
   into +140. Decay per second scales with the present rate [V copies, I shader math].
2. AccBuf: each present blends against the captured target with weight `word_130070E` / 128 [V].
3. Distortion feedback buffer Distortion+40: blends +140 in with alpha min(+120, 0.8) per present [V
   call, I blend].
4. BlurEffect: ping-pong swap, a 2-present warm-up, phase +0.098 per present (about 102 presents to
   finish) [V].
5. BlurEffectEx: the +60 present counter picks the capture present [V].
6. Second-camera crossfade gfx+1264: -0.05 then +0.01 per present [V].
7. gfx+2 = (gfx+14420 != 0) is written at the end of each present and decides whether the next
   present re-copies +140 to +168 [V].
8. Texture animation autoplay, animator UV offset, fgEff, light 0 pulse: per present dt, so wall
   clock rather than frame count. These look the same at any present rate but are not step locked
   [V].
9. Kind 4 spark trails advance once per `FFX_MainStepLoop`, so they run slower during catch-up
   bursts and at the same speed as presents otherwise [V].

## 14. F2 read-backs (render-side state read by the step)

Listed loud because PORTING_RULES F2 forbids each of them in the recreation.

1. **ScreenFade register.** pp+74636..+74652 is written at render whenever the register is not
   active: `sub_6AE5B0` (the 0x1000 fade callback), the pass kind 2 executor at 0x66718B and
   `sub_638840` at 0x66719B, and `sub_698F70` at 0x66898B, which zeroes alpha and blend every
   frame. `FFX_MainStep` at 0x820D4F reads it back through `FFX_ScreenFade_GetColor 0x641390` in
   the load hold: if alpha != 1 it ends, sets the colour with alpha 1, and begins again.
   `FFX_Ch_EventjumpInit` at 0x83B483 copies the RGB into the hold fade. So the load-hold colour
   comes from the last rendered fade, and whether Begin runs, and so whether
   `FFX_MainStep_IsFadeBlocking 0x81FCD0` blocks later steps, depends on a render write [V for the
   writes and the read, I for the blocking consequence].
2. **Map load pending.** gfx byte +4501 is cleared at render by `mov word ptr [ebx+1194h], 0` at
   0x658C89 (inside `sub_658AE0`, which also clears +4500). The step reads it in
   `FFX_MainStep_IsFadeBlocking` through `sub_641BF0`, which gates the whole gameplay step. The
   step cannot advance past a map load until a present has run [V].
3. **Map alpha pass order.** `g_ffxMapAlphaPassFirst 0x12FBBB6` is written by the render-side
   install `sub_65B8A0` -> `sub_6E4470`, and read by `FFX_MainStep` to order DrawPass1 against
   `FFX_Ch_StepAll`. `sub_65B8A0(1)` is also reachable from the step (`sub_640200` <- `sub_908000`,
   `FFX_Map_LoadMapOutSync`) but returns early while +4501 is pending, so the asynchronous install
   is the render one [V].
4. **FMV frame state from the render thread.** For videos 46, 72 and 32 `sub_6DAE00` runs as a kind
   44 callback and writes mgr+1752, +1760, +1764 and player+256. `FFX_Fmv_ApplyMovieCamera` (step)
   reads the index through `sub_6410D0`, drives `FFX_Came_SetMatrixOverride`, and calls
   `FFX_SetMovieScreenMode(0)` when it runs out, which gates the magic overlay step and
   `FFX_RcBg_Step` in the sub-step loop. The index lags at least one present and races the next
   step [V]. The skip prompt mgr+1868 is cleared by `FFX_Fmv_DrawFrame` (present, wall clock) and
   read by `FFX_Fmv_PollSkipButtons` (step) [V].
5. **Video texture latch and the layer break flag.** gfx+3537 is set by `sub_672070` on the render
   thread and read by `sub_654960` at step to pause or resume the player [V]. gfx+0xFB44 and the
   cluster +0x11E4 membership are flipped by the presenter (`sub_658F00` at 0x669F50, `sub_653230`)
   and read and flipped at step by `FFX_GfxCtx_PushLayerBreak` [V]. gfx+64325 is cleared by the
   presenter at 0x669926 and read at step by `FFX_Glue_ReleaseMagic154_422Buffers` [V].
6. **Map install side effects.** gfx byte +68909 is set at render install and read and cleared at
   step by `sub_90BF10`, which then re-pushes all node transforms. The node map gfx+0x1198 is built
   at render and used by the step's node writers, so a node write before install is dropped [V
   build, I drop]. The base clear colour gfx+68912 is written at install and read by the step's
   colour scale writers [V].
7. **Light 0 colour pulse.** `sub_6AB630(dt)` -> `sub_831C10` writes `dword_13008CC` entry 0 colour
   per present (0x6571C6, gated on `dword_CCCA84`+460). `sub_8305A0` reads it at step from
   `FFX_Ch_StepAll` (0x82F9F6) into CHR+900... So per-character lighting depends on wall time when
   that gate is set [V].
8. **Worker results read by the next step.** The kind 3 prim job `sub_7256C0` writes vertex and
   index counts (0x726852, 0x72685F) that the next step's producer `sub_72DED0` reads as its
   allocation size. The kind 4 job advances persistent particle state once per `FFX_MainStepLoop`
   [V].
9. **Pause overlay redraw state.** `g_ffxFmvPauseOverlayRedrawState 0x1340844` is set to 2 by the
   presenter and compared with 1 by the step. A byte scan found no writer of 1, so it is inert in
   retail [V scan, I inert].
10. **Present-rate main-thread writers.** `FFX_Cutscene_StepPauseOverlay 0x8AB340` writes
    `g_ffxMenuSysRunningAlt`, `dword_1340850`, `maybe_g_ffxInputModalGate` and
    `g_ffxTimingTrackStartUs`. `FFX_Frame_StepSuspendOverlay 0x680030` forces the suspend state.
    These are UI and input driven, main thread [V, agent].
11. **Possible race, not an F2.** `FFX_Menu_DrawAllPartsIgnoringCatchUp` (Sphere Grid) appends 2D
    geometry on catch-up steps, which do not swap sets, so it can write into the set the previous
    frame's command list references [I].

Step requests that the render consumes and nothing in the step reads back (same hazard family, not
strict F2): MB+88/+89, Distortion +125/+126, BrokenScreen +4/+5/+8, BlurEffectEx +40, pp+74682,
+74683, +74692. A second present of the same step sees them already consumed.

## 15. Catch-up gate readers that change what is drawn

Gated (skipped on catch-up) [V]:
- `FFX_Gfx_SetCameraWorldMatrix 0x639860` (the main camera matrix).
- `FFX_ChrInstance_UploadBoneMatrices` (skin palette), `sub_63BB30` and `sub_63BC80` (material colour
  and part alpha), `sub_63A010` (mesh groups, 0x82F9DE), `sub_8305A0` (0x830A9C), the old-walkmesh
  branch of `FFX_Ch_UpdateShadeFromLights`, all on `g_ffxIsCatchUpStepRender`.
- `sub_642750` (character light pass command), the pass command wrappers for kinds 1, 2, 4, 5, 6.
- `FFX_Sprite2D_Draw` 0x63EEF3, `sub_640660`, `sub_63E930`, the 2D swap at 0x820ECE and 0x8A9DB4.
- `FFX_MesWin_DrawAllWindows` at 0x821533 (and with it the battle HUD and the letterbox).
- VFX submit, the kind 0 and 1 producers, many particle draw ops, `sub_7F49E0`, the BtlExplode
  vertex build.
- `FFX_DynDrawList_RefreshBatchGeometry` (0x8215E6), `FFX_DynGeoModel_StepDeferredRelease`.

Not gated, so they advance on catch-up steps too [V]: `FFX_Sg_StepScreenFades`,
`FFX_DreamFilter_Update` (with `rand()`), map tracks `sub_914A10`, `sub_90BF10`, the screen wave
`sub_913200`, `FFX_RcBg_Step`, the title water frame, `FFX_Fmv_StepPresentation` and
`FFX_Fmv_ApplyMovieCamera` (N steps consume N video frames when `FFX_StepPacing_AllowCatchUp 0x81FD30`
allows catch-up during a camera movie), `FFX_Gfx_StepVideoPlayback`, `FFX_DrawDebugOverlay`,
`FFX_MenuSys_DrawAllWindows`, `FFX_Module_DrawAll`.

**The culling leak.** On a catch-up step the main camera matrix at gfx+64004 is not written, so it
still holds the last presented step's camera. `FFX_Ch_StepAll` rebuilds the PCamera from it
(`sub_640DF0`), and `FFX_Ch_UpdateCameLenAndZClip` tests each CHR against that stale frustum and
writes the Disp and Z bits into `m_hideFlags`. `FFX_Ch_UpdateMotionAll` clears those bits before
its own gate, so motion is not affected. Other readers of the hide flags through `FFX_Ch_GetHideFlags`
are `sub_80EB50`, `sub_814520` and `sub_816E50` on the effect side, not classified. The bytes are in
the CHR record, so a state hash that covers the CHR sees a catch-up-dependent value. PROGRESS 3.4.7
says the step flag never reaches hash-visible state. This is a candidate exception [V for each link,
I for the consequence].

Magic DLLs keep their own copy of `g_ffxIsCatchUpStep` (host slot 936), which xrefs cannot see.

## 16. Cut signals

What the recreation should treat as "do not interpolate this value for this step":

| signal | scope | where the original shows it | mark |
|---|---|---|---|
| character spawn | that CHR | `FFX_Ch_Allocate`, m_inUse CHR+0x2 rising | V |
| character shown after hidden | that CHR | m_hideFlags going to 0, `FFX_ChrInstance_SetHidden` link | V |
| parent binding change | that CHR | m_parent, m_parentJoint, m_i10C change | V |
| large move | that CHR | no flag exists. Use a distance threshold on position, because `FFX_Ch_SetPos` is also the per-step path for scripted actors | V for the absence |
| camera priority switch | camera | slot+3212 != active priority, `sub_7C0D40` | V |
| camera override on or off | camera | `g_ffxCameOverrideFlag`, `FFX_Came_SetMatrixOverride` | V |
| direct look-at on or off | camera | ActivateScreen branch change | V |
| map change | everything | gfx+4501 set (load), gfx+4500 (install edge), `sub_6403B0` unload | V |
| battle transition | everything | `FFX_SetMapDrawEnabled` ops 7 and 8, `FFX_Btl_BeginBattle` | I for the full flow |
| rcBg scene change | background | `sub_6587D0`, gfx+4568 | V |
| movie start and end | FMV and camera | `FFX_Fmv_CreatePlayer`, `g_ffxMovieScreenMode` | V |
| fade channel restart | that channel | counter or mode write by the setters in section 8 | V |
| message window state change | that window | frame +8 | V |
| text page | that text | `TextPageBegin` sets +160 = 0 | V |
| effect spawn and kill | that model key | `FFX_Vfx_CreateModel`, `sub_69FC10` | V |

## 17. GPU candidates and their reference routines

Per PORTING_RULES F8, each move keeps the original CPU routine as the reference implementation, and a
test runs both on the same input. These are the routines for every yes and for the movable half of
every partial. VAs are FFX.exe [V unless marked].

| work | reference routine(s) | what must stay on the CPU |
|---|---|---|
| character pose interpolation and world chain for presentation | `FFX_Ch_BuildSkinMatrices 0x832760` (root build from pos, rotY, tilt, scale, parent, and the joint chain from euler, translation, scale) | the step's own chain at joint+136 and the root at CHR+0x1D0, read by `FFX_Ch_UpdateBonePositions` and parented CHRs. Joint override maths `FFX_Ch_BuildJointOverrideMatrices 0x832110` (F8) |
| skin palette and vertex skin | palette half of `FFX_Ch_BuildSkinMatrices`, upload `FFX_ChrInstance_UploadBoneMatrices 0x63BE60` (remap into PMeshInstance), rigid parts `maybe_FFX_ChrInstance_SetBoneMatrix 0x63C430`, root push `sub_63C3E0` -> `FFX_Gfx_SetMeshTransform 0x66D810` (the diag(1000) scale and X/Y negation) | nothing, the step never reads the palette |
| blob shadow quads | `sub_830240` -> `sub_82E3E0` -> `sub_831D10`, buffer reset `sub_643DC0`, submit `sub_643DD0` | the ground height and shadow mode inputs |
| render-side frustum cull | none in the original for characters (the only cull is the simulation-visible `FFX_Ch_UpdateCameLenAndZClip 0x82E220`, which stays). Phyre's own cull for the rest [I] | `FFX_Ch_UpdateCameLenAndZClip` |
| texture animation (map, rcBg, characters) | `TextureAnimationInfo__advance 0x6B4450` via `sub_6B47E0` (autoplay), `sub_6B44A0` (sampler apply via `sub_66E4F0`), `setAnimatedMaterialsChar 0x6B3060` [I] | the scripted frame numbers set at step through `sub_6B42C0` and mapWork+645580 |
| material morph and UV offset | `sub_6ACD20` (MorphWeight, VertexColorMorphWeight, UVMorphWeight, UvOffset with the dt integrator) | the weights written at step by `sub_6ABFC0`, `sub_6AC0B0` |
| VFX model build, kind 0 | `sub_71F320` (world = slot+468 times slot+32, zMatrix, vertex colour scale and UV scroll bake), `sub_645A50` (link, sort depth, vf10) | the slot capture at step `sub_718610` |
| VFX sprite build, kinds 1 and 3 | `sub_727110` (view-space quads), `FFX_Vfx_ZCorrectSpriteQuad sub_7119D0`, `sub_7256C0` (FrameBuffer distortion) | the kind 3 vertex and index counts at 0x726852 / 0x72685F, which the next step's `sub_72DED0` reads (F2) |
| VFX builds, kinds 2, 5, 6 | `sub_7269A0`, `sub_71EE80`, `sub_7282D0`, dispatched by `FFX_Vfx_PrimBuildRunSlots 0x71ED10` [skimmed] | the slot capture at step |
| spark-trail segments, kind 4 | segment build half of `sub_7287B0` | the advance (pos += vel, age++ at 0x72882D) unless the hash excludes the ring |
| EvTex, BtlExplode, screen-space effect quads | `sub_7196F0` (EvTex quad), vertex half of `FFX_BtlExplode_StepFragments 0x805510`, `sub_7F49E0` | the EvTex slots and the fragment simulation |
| draw queue sort and merge, sort depth | `FFX_GfxCtx_SortMergeDrawQueues 0x654650`, `VFXDynamicGeometry_SetSortDepth sub_6A3B20` | draw item membership |
| 2D sprite expansion and interpolation | `FFX_Sprite2D_Draw 0x63EEE0` (PS2 to 1920x1080 mapping), `sub_640660` (pixel rounding, colour multiply), `FFX_DynDrawList_Append 0x67BA50` (batching), clip `sub_644320`, draw `sub_650CB0` / `sub_650340` | which sprites exist, and their producer keys |
| screen wave grid | `sub_913200` (9x9 grid from the phases), `Distortion_SetGridVertex sub_6C3240`, `sub_6C2DC0` (render) | the phases at mapWork+778368 / +778372 |
| guide map mesh | `GuildMapDynamicGeometry` vf02 `0x6A5070` [skimmed] | open state |
| FMV plane upload and colour conversion | `sub_A284A0` (plane copy), `FFX_Fmv_DrawFrame 0x6D7570` | which frame is shown, chosen per step |
| BrokenScreen shards | `sub_6D3650` (shard lighting, per present), `sub_6D5680` (draw) | the request flags and shard spawn |
| present-rate effect timers | `sub_6C3D40` (BlurEffect phase), `sub_653230` (crossfade ramp), `sub_6E3F40` (fgEff), all rebased on snapshot time rather than present count | the start requests |

## 18. Proposed IDB names

Names for things I identified directly. The helper agents' proposals follow in the second table. All
are unnamed in the IDB today unless marked.

| VA | proposed name | evidence |
|---|---|---|
| 0x65D860 | FFX_Gfx_UpdateSkyProjOffsetFromTidus | writes gfx+63932 from Tidus's projected screen Y, clamped 0.035..0.295, called in FFX_Ch_StepAll |
| 0x63C3E0 | FFX_ChrInstance_SetRootTransform | m_rootJoint world times diag(1000) into FFX_Gfx_SetMeshTransform for both instances |
| 0x640DF0 | FFX_Gfx_RefreshCamerasFromWorld | rebuilds the PCameras from gfx+64004 through sub_6720C0 |
| 0x6720C0 | FFX_PCamera_UpdateFromWorldMatrix | per camera view and frustum rebuild, called per present by the presenter |
| 0xA44B80 | FFX_Ch_JoinUpdateJobs | waits on sub_6DBF50 after the dispatches |
| 0x82E390 | FFX_Ch_JointOverrideJob | job for chr ids 3 and 103, calls FFX_Ch_BuildJointOverrideMatrices |
| 0x63BB30 | FFX_ChrInstance_SetMaterialColour | "MaterialColour" param, StepAll loop 3 |
| 0x63BC80 | FFX_ChrInstance_SetPartAlpha | m_parts[].m_alpha times shade alpha |
| 0x651400 | FFX_Mesh_SelectFakeTransparentPass | switches the pass when alpha < 1 |
| 0x8305A0 | FFX_Ch_AccumulateLights | sums dword_13008CC records into CHR+900..1024 |
| 0x63CB80 | FFX_ChrInstance_UploadLightMatrices | called right after the accumulation |
| 0x643CC0 | FFX_ChrInstance_SetSpecParams | SgSpec_Color, LightVec, CameraVec, LightMat |
| 0x643DC0 | FFX_ChrShadow_ResetBlobBuffer | resets dword_CCCA74 for the current DynGeoMem set |
| 0x643DD0 | FFX_ChrShadow_SubmitBlobMesh | submits and links the shadow mesh via sub_69F9D0 |
| 0x830240 | FFX_Ch_BuildBlobShadow | per CHR, calls sub_82E3E0 |
| 0x831D10 | FFX_ChrShadow_WriteBlobQuad | writes four world-space vertices |
| 0x7C0D40 | FFX_Came_OnPrioritySwitch | runs when slot+3212 differs from the active priority, filters skipped |
| 0x7C1480 | FFX_Came_ApplyShake | live = committed + shake |
| 0x7C50F0 | FFX_Came_StepShake | FFX_Rand_Stream(9), three channels |
| 0x7BDA80 | FFX_Came_StepScreenRectTween | four per sub-block in ActivateScreen |
| 0x66D940 | FFX_GfxCtx_SetGuideMapCameraMatrix | writes gfx+1968 |
| 0x642410 | FFX_Gfx_SetGuideMapCamera | wrapper from sub_91DA80 |

Helper agent proposals, spot checked where marked in the sections above:

| VA | proposed name | evidence |
|---|---|---|
| 0x67B970 / 0x67B990 | FFX_DynDrawList_GetCount / GetItem | select by the +23420 toggle |
| 0x650CB0 / 0x650340 | FFX_Gfx_Draw2DList / Draw2DStencilList | draw kinds 0 and 2 / kind 1 |
| 0x640660 | FFX_Glyph2D_Draw | pixel-rounded quad, item+188 = 1 |
| 0x644320 | FFX_Sprite2D_ClipTest | culls against +23480..+23492 |
| 0x8F3DB0 | FFX_Menu_DrawFillRect | rect plus colours into Sprite2D |
| 0x8F3A50 / 0x8AB570 | FFX_MesWin_DrawLetterbox / SetLetterboxAlpha | bars, dword_C59F38 |
| 0x8A9670 / 0x8A9680 | FFX_MenuSys_SetDimLevel / SetDimRate | dword_184087C / dword_1840880 |
| 0x8A9C60 | FFX_MenuSys_GetFrameCounter | byte_1840884 |
| 0x6AE5B0 | FFX_Gfx_FadeQuadCallback | sets the fade register, calls sub_697490 |
| 0x697490 | FFX_PostTargets_DrawScreenFade | "ScreenFadeColor" |
| 0x638840 | FFX_PostTargets_SetFadeColorIfIdle | writes +74636..+74648 when not active |
| 0x698F70 | FFX_PostTargets_ResetIdleFade | alpha and blend to 0 |
| 0x6ADAD0 / 0x6AD4E0 | RenderDelegationHelper_RunPhase2 / RunPhase4 | test flags & 2 / & 4 [V] |
| 0x5B5880, 0x5B5940 | Phyre_PRenderer_QueueCallback, Phyre_PRenderer_QueueCallback2 | both write kind 44 records. 0x5B5880 [V], 0x5B5940 per agent |
| 0xA3D9F0 | FFX_Iggy_TickDrawOnRenderThread | IggyPlayerTickRS loop then draw |
| 0x906BF0 | FFX_MesWin_DrawFrame | calls frame+120 |
| 0x63DF70 / 0x822410 | FFX_DreamFilter_DrawOverlay / FFX_DreamFilter_Reset | rand jitter sprites / zeroes the alpha |
| 0x65B5B0 | FFX_Title_DrawWaterFrame | "Water_%02d" |
| 0x6ACD20 | FFX_MatMorph_ApplyPerFrame | MorphWeight, VertexColorMorphWeight, UVMorphWeight, UvOffset |
| 0x6DAE00 | FFX_Fmv_UploadFrameJob | kind 44 job, advances the index, sets +1764 [V] |
| 0xA284A0 | PVideoPlaybackWin32_ConsumeFrameToTextures | pops one frame, copies YUV [V] |
| 0x6410D0 / 0x6D91B0 | FFX_Fmv_GetFrameIndex / FFX_FmvMgr_GetFrameIndex | returns mgr+1752 [V] |
| 0x653960 | FFX_Frame_RenderFmvScene | FMV early-exit body |
| 0x654960 / 0x672070 | FFX_GfxCtx_StepVideoTextures / FFX_Gfx_UploadVideoTextureJob | queues the upload / sets gfx+3537 |
| 0x6C5410 / 0x6C5600 | MotionBlur_Render / MotionBlur_SetBlendWeight | "BlendWeight" |
| 0x82BEE0 / 0x82BD10 / 0x82BDF0 | FFX_Sg_SetMotionBlur / FFX_Sg_SetAccBufAlpha / FFX_Sg_BeginAccBufCapture | host APIs 543, 693, Sg 025 |
| 0x6C3D40 / 0x6C4E00 | BlurEffect_UpdatePerPresent / BlurEffectEx_UpdatePerPresent | per-present counters |
| 0x6C2DC0 / 0x6C3230 / 0x6C3240 | Distortion_Render / SetStrength / SetGridVertex | +124..+126, +120, +108/+112 |
| 0x695790 / 0x698550 | PostProcess_RenderDepthOfField / PostProcess_SetFocusDepth | "FocusDepth" |
| 0x653230 | FFX_Frame_RenderSecondCameraCrossfade | gfx+1264 ramp |
| 0x6542B0 | FFX_Gfx_RefreshShadowMapScaleBias | MySMScaleBias only, draws nothing [V] |
| 0x6E5C90 / 0x6E5A40 / 0x642750 | FFX_ChrLight_QueueLightSet / FFX_ChrLight_ApplyAndDraw / FFX_Gfx_PushCharLightCommand | 300-byte slots, catch-up gate |
| 0x6E36F0, 0x6E3B10, 0x6E3A30, 0x6E3760, 0x6E3AA0 | FFX_SceneEnv_SetFogColor / Near / Far / Curve / Limit | env fields |
| 0x6E3F40 | FFX_SceneEnv_StepFgEffFade | "fgEff" by dt |
| 0x914A10 | FFX_Map_StepAnimTracks | 64 tracks of 40 bytes, kinds 0..5 |
| 0x90BF10 | FFX_Map_DrawPassWorker | body of DrawPass0/1 |
| 0x66E3E0 / 0x66DB00 | FFX_GfxCtx_SetMapNodeTranslation / SetMapNodeRotation | via gfx+0x1198 |
| 0x658AE0 | FFX_Gfx_InstallPendingMapScene | clears +4500/+4501 at 0x658C89 [V] |
| 0x65B8A0 | FFX_Gfx_OnMapSceneInstalled | clear, fog, post, shadow mode, alpha pass order |
| 0x641BF0 | FFX_Gfx_IsMapSceneLoading | returns gfx byte +4501 [V] |
| 0x6B47E0 / 0x6B44A0 / 0x6B42C0 | TextureAnimationManager__advanceAll / applyFrames / setFrame | dt loop / sampler write / +24 write [V for the first two] |
| 0x913200 | FFX_Map_BuildScreenWaveGrid | 9x9 grid |
| 0x920AA0, 0x9210E0, 0x920C00, 0x92ADE0 | FFX_RcBg_StepLayers, StepCameraSway, ApplyCamera, StepLayerScroll | rcBg sub-step functions |
| 0x6587D0 | FFX_Gfx_SelectRcBgScene | gfx+4568 |
| 0x66D1D0 / 0x66D3B0 | FFX_Gfx_SetMapColorScale / MulMapColorScale | "clrScaleFactor" |
| 0x656590 / 0x63A010 | FFX_GfxCtx_AddMeshGroup / FFX_Gfx_AddChrMeshGroup | from FFX_Ch_StepAll |
| 0x831C10 / 0x8319B0 | FFX_ChrLight_SetColor / FFX_Light_SetPoint | dword_13008CC records |
| 0x71F320, 0x727110, 0x7256C0, 0x7287B0 | FFX_Vfx_PrimBuild_Model, _Sprite, _FrameBuffer, _SparkTrailStep | kinds 0, 1, 3, 4 [V sub_645A50 callers] |
| 0x718610, 0x71B8A0, 0x72DED0, 0xA6E2E0 | FFX_Vfx_QueueModelBuild, QueueSpriteBuild, QueueFrameBufferBuild, QueueSparkTrailBuild | prim queue producers |
| 0x63FDB0 | FFX_DynGeoMem_AllocModelPart | 5-region block |
| 0x645A50 | FFX_Vfx_ApplyBuiltModel | link, sort depth, vf10 [V callers] |
| 0x6A3B20 | VFXDynamicGeometry_SetSortDepth | z/w from g_ffxCameProjMatrix |
| 0x72A750 / dword_230FD34 | FFX_Vfx_SetTimeFrozen / g_ffxVfxTimeFrozen | MapFx 102 |
| dword_230FD38 | g_ffxVfxPmPreviewOff | set to 1 by sub_761D60 in MainInit |
| dword_CCCA90 | g_ffxMaterialParamAnimator | MorphWeight arrays |
| dword_13008CC | g_ffxChrLightTable | 44-byte records, count dword_13008D0 |
| dword_CDEB88 | g_ffxDistortion | singleton via sub_6C2290 |

## 19. Corrections made to other docs

Dated "Correction 2026-10-13" notes were appended, only for claims checked in the binary:
- DISPLAY_LIST.md: phase 0x2 and 0x4 runners swapped, kind 44 appender found, cluster contents,
  mesh groups are character instances, `FFX_Gfx_ResetDrawItemQueues` twice only on the menu-close
  step, the 2D render reads the latest non-catch-up step's set.
- RENDER_PASSES.md: cluster contents reversed, `sub_6542B0` draws nothing.
- FMV_SYNC.md: mgr+1764 is set on every upload, and the step does read the frame index.
- CUTSCENE.md 7.1: the shown FMV frame advances once per step.

Claims from the helper agents that were not re-checked and so were not written into other docs
(they are recorded here only): the MENU_SYSTEM.md meaning of `g_ffxModuleDrawPrio` (a per-step fade
delta, not a draw priority), DISPLAY_LIST.md section 7 VFXDynamicGeometry +224..+244 defaults,
DISPLAY_LIST.md section 8 flush placement (after the step loop, not at step end),
THREADS_AND_CLOCKS.md T4 (jobs also mutate simulation state), MAIN_LOOP.md line 245
(`g_ffxEffectSubStepCounter` gate), DEBUG_MODE.md section 2 (the overlay runs per step), RENDER_PASSES
step 18 (the 0x669C22 quad is conditional), and several IDB comments (`FFX_Map_StepAnims`,
`FFX_Distortion_SetSkipNextDraw`, `FFX_Gfx_StepVideoPlayback`, `FFX_Vfx_StepAll`,
`FFX_DebugMenu_drawIfActive`).

## 20. Open items and skimmed areas

Skimmed, marked as such in the tables:
- Menu screens other than the Sphere Grid (cursor and scroll fields of modules 1..24, the menu
  object pool `0x1840900`), the battle HUD gauges and overdrive HUDs.
- Message window frame styles 1..7, and how the text draw reads the reveal count.
- The guide map renderer `sub_91DA80`.
- Character texture animation callers, the cloned ClassChar path, `FFX_Ch_ShadowPassAll`.
- Prim kinds 2, 5 and 6 runners, callback groups.
- USM, colour correction internals, HDAO beyond its constants.
- The debug overlay.

Open:
- The particle state layout (ppp) is not mapped. It is the prerequisite for interpolating
  particles instead of holding them.
- The meaning of flags1 0x400 from `FFX_Ch_TimeSlicedFlags1Bit10`.
- Whether any state hash covers `m_hideFlags`, which decides whether the catch-up culling leak in
  section 15 is a real hash difference.
- The render-thread reader of the DynGeoMem segment pointers, and whether `PMeshInstance`
  matrices written in jobs can be read by the Phyre render thread for frame N-1.
- The writer of 2D item+180 (draw kind), the reader of gfx+3040, the reader of the screen origin
  `flt_CCB228`.
- The gate `dword_CCCA84`+460 behind the light 0 pulse F2.
- `gfx+63943` is read by the kind 3 prim job and cleared by the presenter, with no setter found. It
  could be another F2.
- The fade-blocking consequence of F2 item 1 needs a runtime probe: hold a load with a fully opaque
  queued fade and check whether the ScreenFade register is active.
- CRT `rand()` consumers: `FFX_Ch_UpdateBlink` on a worker thread, rcBg sway per sub-step, the dream
  filter per step. All are hidden RNG consumers the recreation has to own.

## Correction 2026-10-13 (texture animation and post tweaks, PROGRESS 1.5.9)

Found while writing TEXTURE_ANIMATION_AND_POST_TWEAKS.md. Checked in the binary.

- **Section 3 row "character texture animation", the section 6 row "texture animation autoplay
  frame" (its "character unload" reset), section 12 ("for map, rcBg and character texture
  animation") and the section 17 row "texture animation (map, rcBg, characters)".** Character
  texture animation does not advance per present. `TextureAnimationManager__advanceAll 0x6B47E0`
  walks only the map info array at mgr+124/+128. Character infos sit in per character sets at
  mgr+100 and their +24 frame is never advanced by time. The character frame comes from the PS2
  character texture animation stream, stepped per step per CHR inside `FFX_Ch_UpdateRenderJob
  0x833530` (a worker job queued from `FFX_MainStep`), which calls `sub_77C8D0` -> `sub_77F360` ->
  `sub_77FBF0` -> `sub_643EB0` -> `setAnimatedMaterialsChar 0x6B3060`. Finding 7 holds for map and
  rcBg autoplay only.
- **Section 10 row "config-only post", "HDAO pp+74704".** The HDAO level the pass uses is the int at
  pp+4708 (0x1264), and the enable byte is pp+74705 (0x123D1), read by `sub_695BA0` at 0x695BE4,
  0x695C88 and 0x695BDB. pp+74704 is a separate byte.

## Correction 2026-10-13 (gfx +4501)

The map scene pending byte gfx +4501 is not only cleared at render (0x658C89). The worker thread
also writes it in `FFX_Gfx_LoadMapAssetHeader 0x65CBF0`: it sets +4501 = 1 before enqueuing the
cluster rows, and when the header parse `sub_667BB0` returns non zero it clears +4501 and may set
+4500 on the worker. So `FFX_Gfx_IsMapSceneLoading` can see the edge from the worker as well as
from render. See `ASYNC_LOAD_REQUESTS.md` H2. Verified in Hex-Rays.

## Correction 2026-10-28 (the 2D kind and the 2D clip, PROGRESS 1.12.8)

Found while writing MENU_DRAW_PATH.md. Checked in the binary.

- **Section 7 row "2D texture, depth, draw kind".** geom+40 is not a sort depth. It is the pass
  kind, the fifth argument of `FFX_Sprite2D_Draw` (and of `FFX_DynDrawList_Append`), which Sprite2D
  stores at geom+40. The "kind writer not found" is `sub_67D920` (`FFX_DynDrawList_BindBatchGeometry`),
  which copies geom+40 into record +180 when it binds the batch geometry at the end of the step.
  Kind 0 draws in `sub_650CB0`, kind 1 in `sub_650340` (so under kind 0), kind 2 only under the
  booster HUD with a scissor. Kinds 1, 2 and 10 force a fresh record. There is no 2D depth, quads
  draw in append order. MENU_DRAW_PATH.md 6.
- **Section 7 row "2D clip rect".** `sub_67E3B0` is the only writer of +23456, and its one caller, at
  0x6448C6, sits in code near 0x644850 that has no xrefs and whose address is nowhere in the binary
  as a pointer. So the append time clip never turns on in retail and `sub_644320` always passes.
  The clipping the menus do see is the scissor of `sub_8F9230` (`FFX_Menu_SetClipRect`), applied
  by the rect helpers before Sprite2D. MENU_DRAW_PATH.md 5.6.
