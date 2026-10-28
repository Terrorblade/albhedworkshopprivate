# RCBG: the rcBg background renderer

The rcBg library from `..\program\src\rcBg\`: the pre-rendered background scenes, their layers and
camera, the guide map (minimap), the per-map field particle objects, and the host-side authoring
tools that came with it. PROGRESS 1.7.6, item 2 of MAP section 9.

Tags: [V] read in the binary at the address given, [M] measured by a scan, [I] inferred.

Related docs: `MAP_SECTIONS.md` 5 and 8 (the section 0, 11 and 12 formats), `RCFLEX.md` (rcbgPSim
and the rcFlex mass-spring library, not repeated here), `ATEL_MISC_LIBRARIES.md` (the MapFx rows
that call the glue), `VISUAL_CENSUS.md` (the rcBg rows), `DEBUG_MODE.md` (the orphaned debug root
window). Names are applied by `tools/apply_rcbg_names.py`.

## 1. Summary

- rcBg is live in retail. It runs on the step thread, inside `FFX_MainStep 0x820AE0`, through four
  call sites (section 4). None of them tests `g_ffxIsCatchUpStep`. [V]
- **It feeds the step.** Five ways, in order of weight (section 4.5):
  1. the section 0 scene camera override, which sets the camera yaw the stick is read against,
  2. the guide map writes explored bits into the save block on four scenes, gated on option flag
     0x40,
  3. the scene shake draws CRT `rand()` on the step thread while a MapFx 038 shake with a random
     range runs,
  4. the FP objects suspend, resume and bind MapFx effect groups that ATEL scripts wait on,
  5. the player CHR's clip Z.
- rcBg is a PS2 library and its PC port kept the data paths but not the GS upload. The TRX upload
  hook is an empty function and the layer GS packet builders are dead code. What reaches the screen
  are the Phyre `scene%dlayer%dShape` meshes and the dyngeo minimap, pushed from the rcBg step. [V]
- rcbgDebug, rcbgSave, rcbgSaveText and rcbgLoadText are orphaned in retail. They are reached only
  from the rcui editor, whose only entry is a debug root window that nothing opens. [V]
- The original name of `g_ffxRcBgCtx 0x193BF80` is `g_pMainWork`, from the assert string
  `"g_pMainWork->m_chunkGuide == NULL"` in rcbgDebug.c dead code at 0x92CE3F. Context +0x14 is
  `m_chunkGuide`. The IDB keeps `g_ffxRcBgCtx` because many docs use it. [V]

## 2. File layout

Inside one file, functions follow the alphabetical layout law where the names are known (it holds
in rcbgPSim, `RCFLEX.md`, and the names below follow it where the order allows). Across files the
order is NOT alphabetical, it is the makefile order. Observed order [V by string anchors and
callers]:

Guide, PSim, Scene, rcBBox, Pre, Main, TrxTex, FP, rcdtTm2, rcdtRsd2, (Yanagi host API), rcPacket,
Model, rcfxParticle, rcfxSpring, rcfxNail, rcfxDamp, rcfxGravity, rcfxWind, (layer and clut),
Load, rcdtChunk, rcdtParse, Debug, Trx, (Yanagi task engine), Anime, rcuiPre and the rest of rcui,
Save, SaveText, LoadText.

The rcFlex files sit in that order too (Particle, Spring, Nail, Damp, Gravity, Wind), which is not
alphabetical. `RCFLEX.md` 1 is corrected for this.

## 3. Code map

Ranges are the first and last byte of each file's functions. [V] where a string names the file,
[I] where the edge is set by the neighbours.

| range | file | anchor | live |
|---|---|---|---|
| 0x9199F0..0x91AF2F | FFX side glue (not an rcbg file) | callers in the MapFx library and FFX_MainStep | yes |
| 0x91AF30..0x91C8AF | the MapFx ATEL library, `ATEL_MISC_LIBRARIES.md` | | yes |
| 0x91CAE0..0x91F64D | rcbgGuide.c | 0x91D4E0, 0x91D5E0, "RenderMiniMapFog" in 0x91ECB0 | yes |
| 0x91F650..0x91FFC9 | rcbgPSim.c | `RCFLEX.md` | yes |
| 0x91FFD0..0x921200 | rcbgScene.c | 0x920540 (line 118), 0x920670 (line 186) | yes |
| 0x921220..0x9215D2 | rcBase math and rcBBox.c | rcBBox.c string at 0x921513 | yes |
| 0x9215E0..0x9217AF | rcbgPre.c | 0x921640, 0x921770. Start [I] | yes |
| 0x9217B0..0x9220B0 | rcbgMain.c | 0x921CE0, 0x921D60 | yes |
| 0x9220C0..0x922282 | rcbgTrxTex.c | 0x9221B0 | yes |
| 0x922290..0x923231 | rcbgFP.c | 0x9225A0, 0x922BB0, 0x922F80 | yes |
| 0x923240..0x92594D | rcData (rcdtTm2, rcdtRsd2), Yanagi host API, rcBase memory | not rcbg | |
| 0x925950..0x927AFF | rcBase prims and rcPacket.c | not rcbg, the guide uses 0x9259C0..0x927A70 | |
| 0x927B00..0x928573 | rcbgModel.c | 0x927BE0, 0x9283B0 | yes |
| 0x9285A0..0x92990B | rcFlex | `RCFLEX.md` | yes |
| 0x929910..0x92B19F | layer and clut support, file name not anchored [I] | | partly, see 6 |
| 0x92B1A0..0x92B7C6 | rcbgLoad.c | 0x92B2F0, 0x92B390, 0x92B5E0 | yes |
| 0x92B7D0..0x92C12F | rcdtChunk, rcdtParse | not rcbg | |
| 0x92C130..0x92D22D | rcbgDebug.c | dead chunks 0x92CCF0 and 0x92CDD0 | no |
| 0x92D230..0x92D681 | rcbgTrx.c | 0x92D3E0, 0x92D580 | yes |
| 0x92D690..0x93080F | Yanagi task engine | not rcbg | |
| 0x930830..0x930A08 | rcbgAnime.c | 0x9308D0 | yes |
| 0x930D80..0x937F6F | rcui (rcuiPre at 0x930D80) | | no |
| 0x938080..0x9399E1 | rcbgSave.c, start [I] | 0x938350, 0x938590, 0x938A10, 0x939040 | no |
| 0x939B10..0x93A26B | rcbgSaveText.c | 0x939B10 | no |
| 0x93A280..0x93C045 | rcbgLoadText.c | 0x93A280, 0x93AE60, 0x93B6B0, 0x93B930, 0x93BBC0, 0x93BEB0 | no |

The per-function names and one-line roles are in `tools/apply_rcbg_names.py` (256 functions, 6
renames, 18 globals). The highlights, by file:

**Glue 0x9199F0..0x91AF2F.** Every getter and setter goes through `g_ffxRcBgCtx` +0 (the scene set)
+8 (the current scene), and layers at scene +8 with a 192 byte stride. Each bound checks the index
against `FFX_RcBg_GetLayerCount 0x91A3D0` (scene +4). When the context is null the code falls
through to `MEMORY[8]`, a null read, so the MapFx layer rows assume a context [V 0x91A3F0]. The
existing names `FFX_RcBg_Step 0x91A8C0`, `FFX_RcBg_BeginDraw 0x91A250`,
`FFX_RcBg_StepSceneObjects 0x91A240`, `FFX_RcBg_SelectSceneForZone 0x91A0D0`,
`FFX_RcBg_LoadYnStream 0x91AA60` and the section 11 and 12 thunks are kept.

**rcbgGuide.c.** The guide map: a 120 byte object (`rcbgGuideInit 0x91D490`, +108 scale 350), a
model from section 11 or one built from the walkmesh (`rcbgGuideBuildModel 0x91D5E0`, at most 512
polygons), a fade state machine (`rcbgGuideFadeStep 0x91D280`), the draw (`rcbgGuideDraw
0x91DA80`), the fog of war (`rcbgGuideDrawFog 0x91ECB0`) and the explored bitmap
(`FFX_GuideMap_MarkExplored 0x91F420`). Layout presets (`rcbgGuideSetLayout 0x91D930`) [V]:

| preset | centre | size |
|---|---|---|
| 0 | (80, 64) | 128 |
| 1 | (432, 64) | 128 |
| 2 | (80, 352) | 128 |
| 3 | (432, 352) | 128 |
| 4 | (256, 208) | 256 |

The boot values of `g_rcbgGuideLayout 0xC5E354` = 3, size 128 and centre (432, 352) are preset 3,
bottom right [V data]. `g_rcbgGuideFlags 0xC5E350` boots as 0x10D, so the map starts enabled
(bit 0) [V data].

**rcbgScene.c.** The 272 byte scene. `rcbgSceneProcess 0x920AA0` is the per sub-step update,
`FFX_RcBg_ApplySceneCamera 0x920C00` builds the projection and view and installs the camera
override, `rcbgSceneStepShake 0x9210E0` and `rcbgSceneStepRoll 0x920580` are two sine
oscillators on the table `flt_C44BF0`, `rcbgSceneStepLayers 0x920B00` pushes the layer state to
the Phyre meshes.

**rcbgPre.c.** The 20 byte scene set: +0 scene count, +4 scenes, +8 current scene, +12 a 64K
buffer, +16 [V 0x921770]. `rcbgPreCreateDefaultSet 0x921640` makes a one scene set when no YNPR was
loaded, which is how a 3D map with only a section 11 still gets a scene to hang its guide model on.
`rcbgPreFreeSet 0x9216B0` also clears the camera override.

**rcbgMain.c.** The context, init, teardown, the YN switch (`rcbgMainLoadYnStream 0x921D60`) and
the FP object choice (`rcbgMainCreateFP 0x921A50`, from `rcbgMainGetFPSceneId 0x921BA0`).

**rcbgTrxTex.c, rcbgTrx.c, rcbgAnime.c, rcbgModel.c, rcbgLoad.c.** Object types, readers and the
layer animation, see section 5.

**rcbgFP.c.** FP is Field Particle. The rcui strings "FP Pos.X" and "FP Memory" and the host path
`host:/ffx/proj/map/f_particle/m%03d.fpd` say so [V]. Three hand-written objects, one per map
family, each a Step only object of type 0x20008 (slot +12 is 0) [V]:

| scene id | map | object | size | what it does |
|---|---|---|---|---|
| 97 | slik02 (S.S. Liki) | `rcbgFPSlikNew 0x9225A0` | 64 | binds the 9 groups in `g_rcbgFPSlikBinds 0xC5E4B8`, then reads group 90 op 6 work every step and shows or hides the other groups on its y thresholds |
| 173 | bltz0000 (Blitzball) | `rcbgFPBltzNew 0x922BB0` | 40 | a 4 mode state machine over effect groups 78, 83..85, 88, 89, 97, given two CHRs by MapFx 103 and stopped by MapFx 104 |
| 565, 566 | ssbt00, ssbt01 | `rcbgFPSsbtNew 0x922F80` | 16 | binds the 3 groups in `g_rcbgFPSsbtBinds 0xC5E6B0`, then writes a 64 byte matrix from group 90 into slot 10 of the table at 0x18DF59C through `sub_90A520` |

`rcbgMainGetFPSceneId 0x921BA0` maps the battle encounter key 0x610000 to 97, 0x2350000 to 565 and
0x2360000 to 566, any other key to 0, and outside battle returns the save block scene id [V]. So
the FP object is chosen once per context start (`rcbgMainStart 0x922010`), and in debug mode
`rcbgMainDebugBattleRestart 0x921910` restarts it when a battle begins.

## 4. The per-frame path

All of it runs on the step thread, inside `FFX_MainStep 0x820AE0`. [V]

### 4.1 The four call sites

| call | what | gate | rate |
|---|---|---|---|
| 0x82105B `FFX_RcBg_Step` | context start, scene process, debug tail | `g_ffxMovieScreenMode == 0` (cmp 0x821052, jnz 0x821059) | every sub-step |
| 0x821175 `FFX_RcBg_BeginDraw` | `rcbgSceneBeginDraw` on the current scene | movie mode == 0 (0x82114E, 0x821163 jump to 0x8214CF) | once per step |
| 0x821456 `FFX_RcBg_StepSceneObjects` | `rcbgSceneDrawObjects`, each object's Draw slot | effects pass k == 0, `g_ffxEffectsPassEnabled`, previous movie mode 0, script effects pass not off, `g_ffxMapDrawEnabled` (0x821444), `g_ffxMapVisible` (0x82144D) | once per step |
| 0x821525 `FFX_Field_DrawGuideMapThunk` | -> `FFX_Field_DrawGuideMap 0x872CA0` -> `FFX_RcBg_DrawGuideMap 0x919D00` -> `rcbgGuideDraw 0x91DA80` | movie mode == 0 (0x8214F4) | once per step |

None of the four is gated on `g_ffxIsCatchUpStep` [V]. In a sub-step, `FFX_Came_StepAll` (call at
0x82104D) runs before `FFX_RcBg_Step`, so an override the rcBg step installs is consumed by the
camera on the next sub-step [V for the order, I for the one step lag].

### 4.2 FFX_RcBg_Step 0x91A8C0

1. Returns at once when `g_ffxRcBgCtx` is 0 [V].
2. Debug mode only (`FFX_IsDebugMode_dup` and `sub_887C60`): `rcbgMainDebugBattleRestart`.
3. If ctx +8 (started) is 0 (cmp 0x92207C), `rcbgMainStart 0x922010`: a default set if none, the FP
   object for the scene, ctx +8 = 1.
4. If the set exists, `rcbgMainProcessCurrentScene 0x9217B0` -> `rcbgSceneProcess 0x920AA0`:
   1. each object's Step slot (+8), which is where the FP objects and the PSim run,
   2. when the scene has layers (cmp `[esi+4], 0` at 0x920AC9), `FFX_RcBg_ApplySceneCamera`
      (push at 0x920ACF),
   3. `rcbgSceneStepShake 0x9210E0`, `rcbgSceneStepRoll 0x920580`, each of which also ends with
      `FFX_RcBg_ApplySceneCamera` while it is running,
   4. `rcbgSceneStepLayers 0x920B00`: per layer `rcbgLayerStep 0x92AD80` (anime, clut, tweens),
      then the texture frame goes to the Phyre TextureAnimationManager (`sub_643A20`,
      `sub_643A00`) and a layer with `+0 & 3` is hidden (`sub_638F30`, `sub_644120`).
5. The debug tail, chunk 0x92CF10: only in debug mode and only when `g_rcbgDebugWork 0x193C0F0` is
   non zero, which it never is in retail (section 6).

### 4.3 FFX_RcBg_ApplySceneCamera 0x920C00

- Skipped when the scene depth +168 is 0.0 (`fucompp`, `test ah, 44h`, `jnp` at 0x920C26) [V].
  A 3D map's default scene has depth 0, so this only acts on section 0 maps [I].
- `FFX_Ch_SetClipZ(player, 1000)`, the CHR +0x174 clip [V call, I for the render use].
- Projection from the fov, with a pixel bias and +176/+180, into the projection globals at
  0x25D58E0 (`rcbgSetProjGlobals 0x92B0D0`) [V].
- View from eye +104 and ref +120 scaled by +244 (10.0), with the roll +172 and the shake offsets
  +184/+188 [V].
- `FFX_Came_SetMatrixOverride(1, proj, view, target, eye, depth)` at 0x920D91 [V]. While that
  override is on, `FFX_Came_StepAll` copies the reference into every camera slot and
  `FFX_Came_GetYaw` reads it (`MAP_SECTIONS.md` 5).
- Also `FFX_Gfx_SetCameraFov`, `FFX_Gfx_SetCameraAspect`, `FFX_Gfx_SetCameraWorldMatrix`.

`rcbgSceneApplyMovieCamera 0x920E40` is the variant `FFX_Fmv_ApplyMovieCamera` calls, without the
override [V].

### 4.4 The guide map

`FFX_Field_DrawGuideMap 0x872CA0` [V]:

- needs Atel context 0 byte +0 bit 0, and no battle pending or active,
- show = ((ctx +3 & 0x20) and not (ctx +3 & 0x10)) or (ctx +3 & 0x40),
- show = show AND option flag 0x40 (`and al, 40h` at 0x872CE9, the "minimap shown" option),
- ctx +3 bit 8 forces show = 1 regardless of the option (0x872CF4),
- show = 0 unless ctx +0x1F4 >= 3 (cmp at 0x872D00, unsigned),
- then `FFX_RcBg_DrawGuideMap(show)` is called every time, whatever show is.

`rcbgGuideDraw 0x91DA80` [V]:

1. returns when `g_rcbgGuideMasterEnable 0xC5E478` is 0 (cmp 0x91DBD1). It boots as 1 and is only
   written by an orphaned debug page.
2. returns when there is no model (jz 0x91DD57) or the enable bit 0 of `g_rcbgGuideFlags` is clear
   (test 0x91DD5D),
3. `rcbgGuideFadeStep(show)` (0x91DD81): state 0 hidden, 1 fading in, 2 shown, 3 fading out, a 2
   frame delay before each fade and a 6 frame fade, alpha = level * 128 / 6 into
   `g_rcbgGuideAlpha 0x193BF78`,
4. returns when that alpha is 0 (cmp 0x91DD94),
5. draws the model, the markers (`sub_86B700`, `sub_86B710`, flags 0x800 player, 0x2000, 0x400,
   0x10000 actor start and end points), the arrows, and the fog,
6. when the explored record pointer guide +0x70 is set (cmp 0x91EA1B), `FFX_GuideMap_MarkExplored`
   (call 0x91EA3C).

The record pointer is set only by `FFX_GuideMap_LoadSceneRecord 0x91CE50`, for scenes 582, 584,
590 and 591 (sins0200, sins0400, omeg0000, omeg0100), to save block `+0x350C + 512 * i` [V]. These
are the GuideMap bucket of `WORLD_STATE.md`.

### 4.5 What the rcBg step writes that the step reads

| write | where | gated on | reader | weight |
|---|---|---|---|---|
| the camera override | 0x920D91 | scene depth != 0, section 0 maps [I] | `FFX_Came_StepAll`, `FFX_Came_GetYaw` -> the field stick direction | load-bearing, `MAP_SECTIONS.md` 5 [V] |
| explored bits, save block +0x350C..+0x3D0C | 0x91EA3C | scenes 582/584/590/591, guide alpha != 0, which follows option flag 0x40 or ctx +3 bit 8 | the save block itself, and the fog draw | a desync of the save block if peers' option bits differ [V for the gate, I for the co-op effect] |
| CRT `rand()` | 0x921172 | scene +192 != 0 (jz 0x9210F3) and random range +208 > 0 (cmp 0x921169, jle 0x921170) | the shared CRT stream (`THREADS_AND_CLOCKS.md` R5) | only during a MapFx 038 shake with a random range [V] |
| MapFx group suspend, resume, binds | the FP Step slots | scenes 97, 173, 565, 566 | MapFx 004 and 005 waits in ATEL [I] | small, deterministic if the inputs are |
| player clip Z | 0x920C00 | scene depth != 0 | render [I] | none known |

Everything else rcBg writes is presentation: TextureAnimationManager frames (`sub_643A20`,
`sub_643A00`), layer visibility and alpha (`sub_638F30`, `sub_638F60`, `sub_644120`), layer mesh
position, scale and byte +67 (`sub_639D40`, `sub_639D60`, `sub_639D20`), the gfx scene
(`sub_639D90` -> `sub_6587D0`), `sub_642D90`, `sub_6425F0` and `sub_6425D0`, the guide map camera
and dyngeo submit, and the gfx camera setters. [V calls, I that nothing on the step reads them back,
consistent with `VISUAL_CENSUS.md`]

The teardown clears the override: `rcbgPreFreeSet 0x9216B0` ends with
`FFX_Came_SetMatrixOverride(0, ..., 1024.0)` [V].

### 4.6 Load and teardown

- `FFX_RcBg_Init 0x919A70` -> `rcbgMainInit 0x921CE0`: from `FFX_MainInit` (via 0x909030) and from
  `FFX_Map_UnloadMapOut 0x909EA0`. It tears an old context down and allocates a fresh 28 byte one
  with a new guide [V].
- `FFX_RcBg_Teardown 0x919A60` -> `rcbgMainTeardown 0x921AA0`: from `FFX_Map_UnloadMapOut` and
  0x90A9C0. Ends with `g_ffxRcBgCtx = 0` at 0x921B84 [V].
- `FFX_RcBg_LoadYnStream 0x91AA60` (sections 0, 11 and 12) from `FFX_Map_LoadMapOutSync 0x9093B0`
  and `FFX_Map_OnMapOutLoaded 0x9097C0`, then scene 0 is selected [V]. Which thread the async
  completion runs on is not checked here [I step thread].
- `FFX_RcBg_SelectSceneForZone 0x91A0D0` from `FFX_Map_SetZoneWalkmeshAndNotify` [V].

## 5. Data consumed

### 5.1 Map sections (MAP_SECTIONS.md numbering)

| section | chunks | read by | becomes |
|---|---|---|---|
| 0 | YNPR, YNSC | `rcbgLoadReadSceneSet 0x92B390`, `rcbgLoadReadScene 0x92B5E0`, `rcbgLoad_ReadLayer 0x92B1A0` | the scene set, scenes, layers with anime and clut, layer TRX |
| 11 | YNGM | `rcbgLoadReadModel 0x92B2F0` | an rcbgModel attached to scene +28[] by the u16 at chunk +10, the guide map model |
| 12 | YNFT | `rcbgLoadReadTrxTex 0x92B780` | a TrxTex of type 0x20006 added to the current scene, fetched by the ppp operators through `FFX_RcBg_GetParticleTexture 0x919BA0` and the count, frame and tex0 helpers at 0x919BC0..0x919BE0 |

Plus, outside the map file: the active walkmesh (`FFX_Map_GetActiveWalkmesh`, for a guide model
built at runtime), the save block GuideMap records, and the map marker list.

### 5.2 Structs, fields actually seen read or written

Context `g_ffxRcBgCtx`, 28 bytes [V 0x921CE0]:

| off | what |
|---|---|
| +0 | scene set |
| +4 | guide |
| +8 | started |
| +12 | debug encounter key |
| +16 | FP chunk |
| +20 | `m_chunkGuide` |
| +24 | chunk |

Object header, shared by every scene object [V]: +0 type, +4 Delete, +8 Step, +12 Draw. Types:

| type | object | init |
|---|---|---|
| 0x20001 | model | `rcbgModelInit 0x928320`, 0x140 bytes |
| 0x20002 | trx | `rcbgTrxInit 0x92D510`, 64 bytes |
| 0x20003 | trxtex | `rcbgTrxTexInit 0x922160`, 160 bytes, TRX at +32 and +96 |
| 0x20004 | psim | `RCFLEX.md` |
| 0x20006 | particle texture (a trxtex) | `rcbgTrxTex_NewParticleTexture 0x9220C0` |
| 0x20008 | FP | the three FP inits |

Scene, 272 bytes [V 0x9209E0 and the setters]:

| off | what |
|---|---|
| +0 | flags, bit 0 from MapFx 044/045 |
| +4 | layer count |
| +8 | layers, 192 byte stride |
| +12, +16 | objects, count |
| +20 | freed in Final |
| +26 | guide model index (u16) |
| +28 | guide models |
| +92, +96, +100 | OffX, OffY, AdjY (names from the rcbgSaveText writer 0x939B10), +100 is the y offset the camera uses |
| +104, +120, +136 | eye, ref, third vector |
| +168 | screen depth, 0.0 disables the camera |
| +172 | roll |
| +176, +180 | projection bias |
| +184, +188 | shake offset x, y |
| +192..+216 | shake: mode, speed, frames, amplitude, random range, phase, counter |
| +220..+240 | roll: mode, speed, frames, amplitude, phase, counter |
| +244 | eye scale, 10.0 |
| +248, +252 | hi-reso flag (height >= 448), and its pair |
| +256 | player view z * 0.1 |
| +260 | BeginDraw ran, also MapFx 106 |

Layer, 192 bytes [V 0x92AD20, 0x939B10 and the setters]:

| off | what |
|---|---|
| +0 | Flag. Bit 0 hidden (MapFx 015/016), `+0 & 3` hides in StepLayers |
| +4, +8 | the scale pivot passed to `sub_639D60` (MapFx 025..027 "origin") |
| +12, +16 | X, Y (MapFx 017..019) |
| +20..+32 | Z, four floats |
| +36, +40 | MapFx 022..024 |
| +44, +48 | SpeedX, SpeedY, 256 at init |
| +56 | Alpha, 68 at init |
| +64 | colour 0x80808080 |
| +67 | byte, MapFx 048/049 |
| +68, +72 | scale, 1.0 at init |
| +76, +80 | pair B (MapFx 082..085) |
| +84..+96, +116, +120 | scale tween from, to, frames, counter |
| +100..+112, +124, +128 | pair B tween from, to, frames, counter |
| +144 | anime (below) |
| +176 | clut work, 16 bytes, +186 Pal Type, +187 Pal Time, +188 the clut TRX |

Anime at layer +144 [V 0x9308A0, 0x930920]: +0 flags (loop mode in `& 0xF00`, reverse bit 2, bits
0 and 1 from MapFx 030..033), +4 hold, +6 frame (layer +150), +8 count (layer +152), +12 frame
table, +16 the image TRX (layer +160), +20 speed 256, +24 accumulator.

Guide, 120 bytes [V 0x91D490, 0x91D880]: +0 the section 11 model, +8 a model built from the
walkmesh, +64 model, +80 bbox, +96..+104 extents, +108 scale (350), +0x70 the explored record.

Model, 0x140 bytes [V 0x928320, 0x928020]: +16 flags (bit 0 bbox cull, bit 2 skip the matrix),
+20 prim blob, +32..+143 packet, +144 bbox, +192 and +256 matrices.

TRX, 64 bytes [V 0x92D510]: +60 frame, an upload is attempted when it is not -1, and the upload
hook `rcbgTrxUploadHook 0x92D680` is empty.

## 6. Live or orphaned

| file | verdict | why |
|---|---|---|
| rcbgMain, rcbgPre, rcbgScene, rcbgLoad | live | section 4 |
| rcbgModel, rcbgTrx, rcbgTrxTex, rcbgAnime | live | the scene objects and layers |
| rcbgGuide | live | the minimap, every step |
| rcbgPSim | live | `RCFLEX.md` |
| rcbgFP | live | scenes 97, 173, 565, 566 |
| layer and clut support | live, except the PS2 GS sprite packet builders 0x929BF0, 0x929F50, 0x92A440, 0x92A710, which are only called from the dead chunk 0x92AAC0..0x92AC8E | |
| rcbgDebug | orphaned | the dead chunks 0x92CCF0 (DEBUG FP LOAD) and 0x92CDD0 (DEBUG GUIDE MAP LOAD) have no incoming references, and the rest is reached only from the `FFX_RcBg_Step` tail, which needs `g_rcbgDebugWork != 0` |
| rcbgSave, rcbgSaveText, rcbgLoadText | orphaned | only called from rcui |
| rcui | orphaned | `rcuiPre 0x930D80`'s only caller is `SG_DebugGui_RootWindowProc 0x84FDBE`, and the root window is opened only by the orphan at 0x84F100 (`DEBUG_MODE.md`) |

The proof for rcbgDebug: `g_rcbgDebugWork 0x193C0F0` is past the initialised .data (which ends at
0xC8B600), so it reads 0 at boot, and its only writers are 0x9343C0 (`mov dword_193C0F0, eax`, in
the rcui editor) and 0x932F0B (`mov dword_193C0F0, 0`) [V]. So the tail never runs in retail, and
with it `sub_937190` and its `rand()` at 0x93724B.

`rcbgGuideSetMasterEnable 0x91F640` is live code with an orphaned caller, so
`g_rcbgGuideMasterEnable` stays at its boot value of 1.

The rcbgSave and LoadText functions write and read the host tree `host:/ffx/proj/map/...`
(`b%03d.fpd`, `m%03d.fpd`, `gmobjdata.bin`, `map2d.bin`, `.2d` text, tm2 and txt). None of these
ship, and nothing reachable calls them.

## 7. Corrections made to other docs

1. `ATEL_CORE_LIBRARY.md`: core 243 is the guide map layout preset (0..4), not a background scene.
   Core 325 is the guide map screen centre and core 326 the guide map size. The rows at 204, 649,
   731 and 732 are corrected and the names renamed in `apply_rcbg_names.py`. `apply_core_names.py`
   rows 243, 325 and 326 still carry the old names and should follow.
2. `MAP_TRANSITIONS.md`: row 11 `sub_919D70(0)` is guide map off, row 19 `sub_91D930(ctx+4, 0)` is
   guide map layout preset 0, row 24 "rcBg enable" is guide map enable, and the summary "rcBg on"
   is guide map on.
3. `EVENT_LAYER.md` step 16: "the rcBg scene" is the guide map layout preset, "rcBg on" is guide
   map on.
4. `THREADS_AND_CLOCKS.md` R14: 0x921172 is the rcbgScene shake, and only runs during a MapFx 038
   shake with a random range. 0x93724B is the rcui debug code and unreachable in retail. 0x924DF0
   and 0x92E4F0 are the Yanagi magic host API, not rcBg backgrounds, and the table at 0xC653F8 is
   the magic host API table.
5. `RCFLEX.md` 1: the rcFlex files are in makefile order, not alphabetical.
6. `MAP_SECTIONS.md` 10: section 11 is not purely presentation, the guide map draw writes the save
   block GuideMap records on four scenes.
7. `ATEL_MISC_LIBRARIES.md` MapFx 038 and 042: these start the shake and the roll oscillators, not
   tweens.

## 8. Open

- Which thread `FFX_Map_OnMapOutLoaded 0x9097C0` runs on, so whether a section 0 load can install
  the override off the step thread. It has no direct code reference.
- Whether the explored bits matter to co-op in practice: they need peers with different minimap
  option bits, on four Sin and Omega maps. The fix would be to replicate option flag 0x40 or to
  make `rcbgGuideDraw`'s MarkExplored call independent of the alpha.
- The meaning of scene +260 (MapFx 106) and layer +36/+40 and +76/+80. They are named by offset.
- `0x919B80`, a thunk to rcui `0x933C80` from the debug page 0x84F550, is left unnamed.
- The file names of the layer and clut support at 0x929910..0x92B19F. No string anchors it. It
  could be rcbgLayer and rcbgClut or part of rcbgLoad.
- Spot-check status: the call sites and compares in section 4 were read in this pass. The layer
  field names from rcbgSaveText are [V], the meaning of the MapFx rows that use them is from
  `ATEL_MISC_LIBRARIES.md`.
