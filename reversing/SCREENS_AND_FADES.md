# Fades, the paused overlay, and the three screens

Project Zanarkand item 1.5.11. Written 2026-10-13 against the shipped FFX HD PC exe in IDA.
This is how the renderer sees fades, the paused overlay branch and the three-screen two-pass loop,
written against PORTING_RULES F3 (parity mode).

Tags used on every claim: **[V]** seen in Hex-Rays or the disassembly, **[I]** inferred.

Start points: `RENDER_PASSES.md` (the 32 steps of `FFX_Frame_RenderScene 0x668930` and the target
manager), `DISPLAY_LIST.md` (the render delegation registry `dword_CCCA98`, phase masks and
`FFX_RenderDelegation_FreeFlaggedEntries 0x63DAB0`), `MAIN_LOOP.md` sections 6a and 6b (the hold
byte and catch-up), and the camera section of `FFX_GAME_NOTES.md`. Step-coupled render state in
general is `VISUAL_CENSUS.md`, which this doc does not repeat. The fade alpha that the simulation
reads back is `READBACK_CENSUS.md` section 2.1.

Target manager offsets below are relative to `dword_CCC8A0` (gfx+4300). Decimal offsets are the
ones `RENDER_PASSES.md` uses: +140 main colour, +144 depth, +156 cluster target, +168 slot 1
colour, +172 the copy target the paused overlay shows.

## 1. Fades

### 1.1 The layers

There are three fade layers and one persistent colour. All of them end in the same draw call.

| Layer | Who sets it | Queue | Phase or pass | Lifetime | Ev |
|---|---|---|---|---|---|
| Sg screen fades, 5 channels | ATEL and battle code into the channel table, stepped by `FFX_Sg_StepScreenFades 0x82C0C0` | `sub_639240` (flags 0x1001) into `FFX_Gfx_QueueFadeQuad 0x6AE650` | 0x1000, one of four sites in `FFX_Frame_RenderScene` | one-shot per step | V |
| Battle result fade | `FFX_BtlResult_StepStateMachine` | `sub_639280` (0x801) | 0x800, runs in `sub_6AD380` at 0x66AD88, after the 2D pass, last | one-shot | V |
| Magic plugin fade | magic host API (data ref 0xC65B64) | `sub_6392C0` (0x8001) | 0x8000, runs in `sub_6AE0C0` at 0x669BF2, mid scene at step 18 | one-shot | V |
| Pass command kind 2 | step code through `sub_644D40` | display list pass command | executed by `FFX_GfxCtx_ExecPassCommand` at 0x66719B, `sub_638840` writes the colour | per frame | V |
| Stored fade colour plus lock byte | `FFX_ScreenFade_Begin 0x641E20`, `SetColor 0x643900`, `SetBlendMode 0x6438E0`, `End 0x6448D0` | none, it is state | redrawn as the fallback at step 18 and at each 0x1000 site | persistent until End | V |

`sub_639240` has other callers too: `sub_812FA0`, `sub_924C70` and the magic host API data ref
0xC65B34 **[V]**.

### 1.2 The Sg channel table

`FFX_Sg_StepScreenFades 0x82C0C0` is called from `FFX_MainStep` at 0x8214EA **[V]**. Five
channels of 16 bytes at 0x1300708 (ending 0x130075C), active mask `dword_1300704`. Byte 0 is the
type, bytes 1 to 3 the colour, then 16-bit counter, level or duration, target and step **[V]**.

| Type | Colour | Alpha (n counter, d duration) | Blend | Ev |
|---|---|---|---|---|
| 0 | none | ramps a level toward the target. Calls `sub_82BAF0(level)` when `g_ffxGsScreenW == 512`, and exports the level to `flt_12FFAF4` and `dword_C496D0` when `dword_12FB8F0` is set | n/a | V |
| 2 | black | 1 - (127 - 127n/d) / 127, plus `sub_82BD10` | 0 | V |
| 3 | black | 1 - (127n/d) / 127 | 0 | V |
| 4 | channel colour | (255n/d) / 127, can exceed 1 | 0 | V |
| 5 | channel colour | (255 - 255n/d) / 127 | 0 | V |
| 6 | channel colour | (127n/d) / 127 | 0 | V |
| 7 | channel colour, constant | level / 127 | 0 | V |
| 8 | channel colour, constant | level / 127 | 16 (subtract) | V |

The function also drives the AccBuf capture pair (`sub_841D80`, `sub_82BB90`, `sub_6430F0`) **[V]**.

Each queued quad carries {renderer app+292, camera `*(gfx+0xDC)+4`, target 0, r, g, b, a, mode}.
Its callback `sub_6AE5B0` writes the colour and blend into the target manager unless the lock byte
+0x123A0 is set, then calls the draw **[V]**.

### 1.3 The draw

`sub_697490` (FFX_PostFx_DrawScreenFadeQuad) **[V]**:
- Returns at once when alpha is 0.0.
- Sets shader parameter "ScreenFadeColor".
- Picks the pass record by blend: 2 is `unk_CCC96C` "ScreenFadeAdditive", 16 is `unk_CCC97C`
  "ScreenFadeSubtract", anything else is `unk_CCC95C` "ScreenFade". Proved from the static inits at
  0xAEFD50, 0xAEFD70 and 0xAEFD90.
- Draws the full-screen mesh at target manager +74628 into target +140, or into the target
  description it was handed.

It draws over the whole target. There is no scissor and no per-screen rect.

### 1.4 Where the 0x1000 fades run in FFX_Frame_RenderScene

All four sites need gfx+0xF9C5, which the map loader `sub_65CBF0` sets when the map name is not
"grid00" **[V]**. Exactly one site runs per frame **[V]** from the conditions:

| Site | Call | When | Ev |
|---|---|---|---|
| A | 0x66A27A, inside the post chain, into a local target description | `sub_65F1F0` is true (magic id in {106, 281, 515, 577, 522, 667, 445, 270}) | V |
| B | 0x66A807, after phase 0x400, before the 2D pass | (gfx+0x3860 count == 0 or `dword_CCB9C4` != 0), magic not in that set, and not `g_ffxTitleScreenActive`. The result lands in `byte_CCB9C1` | V |
| C | 0x66ACCF, after the 2D pass | count > 0, `dword_CCB9C4` == 0, magic not in the set, not the title screen and not `g_ffxMenuSysRunningAlt` | V |
| D | 0x66AD21 | on the title screen | V |

`dword_CCB9C4` is 1 for events 355 and 356 **[V]**. At each site, if no 0x1000 entry ran and the
lock byte is set, the stored colour is redrawn with `sub_697490` **[V]**.

After the sites, in order **[V]**: `sub_6956D0` when `dword_1136FC0 == 1` (it also uses the
"ScreenFade" pass), the kind 30 pass, then phase 0x800 (`sub_6AD380` at 0x66AD88).

Step 18 at 0x669BDF: when gfx+0xF9C5 is set, phase 0x8000 runs. If nothing ran, the stored colour
quad is drawn (`sub_697490` at 0x669C22) **[V]**.

So a frame can draw the fade colour up to three times (step 18, one 0x1000 site, and pass command
kind 2), plus the battle result quad at 0x800 **[I]** that all of them can coincide in one frame.

### 1.5 Per step or per present

- Every queued fade is per step. `FFX_RenderDelegation_FreeFlaggedEntries 0x63DAB0` runs before
  every `FFX_MainStep` and drops the one-shot entries **[V]**. So the quads queued by a catch-up
  step are thrown away by the next step, and only the last step's quads reach the screen **[I]**
  from the free order, consistent with `MAIN_LOOP.md` on catch-up.
- The stored colour plus the lock byte is persistent and is redrawn on every present **[V]**.
- The render-side writers can change the stored colour (section 2.1 of `READBACK_CENSUS.md`), and
  the step reads it back. That is the one F2 leak.

### 1.6 F3 tie-in

- Fades are full-screen draws and ignore the per-screen rect, exactly the bleed `FFX_GAME_NOTES.md`
  warns about. Parity mode should keep that: one full-target quad per layer.
- Parity needs the same layer order: step 18 (0x8000 or stored colour), site A in the post chain,
  site B or C around the 2D pass or D on the title, then `sub_6956D0`, kind 30, then 0x800.
- Parity needs "last step wins" for queued quads under catch-up, and the stored colour drawn once
  per present.
- Type 4 alpha above 1 must not be clamped before blending if the original blend state does not
  clamp **[I]**. Check the pass record blend state before relying on it.

## 2. The paused overlay

### 2.1 The branch

`FFXApplication__render 0x42F930` checks the hold byte app+0x3AD (941) **[V]**:
- Set: it calls `FFX_Frame_PresentPausedOverlay 0x642BE0`, which is `sub_66B130(g_ffxGfxCtx)`
  then `sub_A267F0(dword_1984C6C)` (the Iggy draw).
- Clear: `FFX_SaveUtil_FrameHeld` (always 0 on PC), then `FFX_Chr_StepClonedCharacterList` when
  `g_ffxMenuSysTexLoad` is null or its phase is at least 4, then `FFX_Frame_PresentScene 0x642BB0`
  (RenderScene, WaitJobSchedule, ReleaseDeferredDrawItems), then `FFX_VsyncEmu_SignalPresent`.

### 2.2 What the overlay draws

`sub_66B130` (FFX_Frame_DrawPausedSnapshot) **[V]**:
- Binds the back buffer, sets the viewport to 0, 0, app+620, app+624 (the window size), clears to
  black, BeginScene(1) and EndScene(1).
- `sub_6959D0` (FFX_PostFx_DrawFullscreenTexture) draws one full-screen "ColorBuffer" quad that
  samples target +172 (or a null source when the +172 description byte +4 is set) into the back
  buffer, with pass record `unk_CCC8BC`.

The scene is not redrawn. The +172 snapshot is reused, and the only thing that changes per present
is the Iggy menu (its `IggyPlayerTickRS` loop in `sub_A3D9F0`) **[V]**.

### 2.3 Who fills +172

`FFX_Frame_BlitSceneForEscMenu 0x639D10` copies +140 (main colour) into +172 through
`sub_692C40`. It runs at render time when the hold byte is 0 and `FFX_EscMenu__isOpen()` is true,
which is the frame the menu opens **[V]**. That pins down the destination its IDB comment leaves
open.

Two more `sub_692C40` calls sit at the end of `FFX_Frame_RenderScene`. The one at 0x66ADCA takes
target +156 or +140 depending on a flag (its role was not traced). Resolved 2026-10-28 in
MENU_DRAW_PATH.md 8: the flag is RenderScene's local `v343`, set to 1 on the branch taken when
`g_ffxMenuSysRunning` is set, where the 3D scene is skipped and the 2D list is drawn into +156. So
it copies +156 for a menu frame and +140 otherwise. The one at 0x66AE6A runs when
`g_ffxFmvPauseOverlayRedrawState == 1`. It copies +140 into +156 at the target size and writes
state 2 **[V]**. The IDB already notes that nothing in the shipped binary writes 1, so the movie
pause branch is dead on PC **[V]** from the xref list (writers at 0x668E04 and 0x66AE6F both write
2).

### 2.4 During the hold

- `endFrame 0x42F4F0` re-latches the hold byte only when it is clear **[V]**.
- The animate path calls `FFX_Gfx_ResizeMainTargetToView 0x642CE0` during the hold **[V]**, so a
  window resize while paused can recreate targets. Whether +172 keeps its content then is **[I]**,
  probably not.
- `FFX_Frame_StepSuspendOverlay 0x680030` is the debug free camera and suspend icon, not this
  branch **[V]**.

### 2.5 F3 tie-in

Parity mode draws the snapshot quad plus the menu on every present while paused, and does not run
the scene. The snapshot is taken from the main colour target on the frame the menu opens, after
that frame's full scene including fades. The window-size viewport (not the 16:9 fitted target
size) matters for letterboxing parity.

## 3. The three screens and the two-pass loop

### 3.1 The loop in FFX_MainStep

**[V]** from Hex-Rays:
- Outer loop `for j in 0..1`. Inner loop starts at screen 0 and walks while
  `FFX_Came_ActivateScreen(i) <= 0`. `ActivateScreen` returns -1 for a disabled screen, 0 for an
  active one, and 1 at index 3 and above, which ends the walk.
- The screen kind comes from `FFX_Came_GetScreenWindow 0x7BBD00`, which returns screen+0xC4 (196)
  and the WIN rect at +64..+76.
- Kind 0 (the main screen) draws only in pass j == 1 and records `g_ffxMainScreenIndex`. Kind 1
  draws only in pass 0. Kind 2 and above draw in both passes.
- Each draw: view and projection into VFX, the magic and character draw matrices,
  `FFX_Vfx_BuildPhyreCameraMatrices`, the GS environment, the map passes, and `FFX_Ch_StepAll`.
- `FFX_Ch_StepAll` runs only if `!FFX_Gfx_IsSecondCameraActive() || v10`, where v10 is set before
  the loop and cleared after the first draw. So outside split mode `FFX_Ch_StepAll` runs once per
  drawn screen. That is a simulation fact, not a render one, and it is why the screen count is
  simulation visible.
- After the loop, `FFX_Came_ActivateScreen(g_ffxMainScreenIndex)` restores the main screen.

### 3.2 What ActivateScreen does with a screen

`FFX_Came_ActivateScreen 0x7BC110` **[V]**:
- Computes the screen rects.
- `FFX_Came_BuildProjFromScreen 0x7C3D60` builds `g_ffxCameProjMatrix` from screen +16..+124
  (aspect, ScrDpt at +28, centre, offset, WIN and DRW). This is the PS2-side matrix with the rect
  folded in.
- Builds the look-at view. For three ids (5381, 2082 and 6275, source of the id not traced) it
  does so only when `dword_C421E8` and `dword_C421E4` are negative and the eye and target vectors
  are in a sane range.
- `FFX_Gfx_SetProjPixelOffset 0x642D60` writes gfx+0xFA34/0xFA38 with
  x = 2 * (s[28] + s[12] - s[20] + flt_1136FB8) / 512 and y = (s[29] - s[13] - s[21] + flt_1136FBC) * 2 / 416.
- `FFX_Gfx_SetScreenOrigin 0x642D40` writes `flt_CCB228` and `flt_CCB22C`.
- `FFX_Came_PushViewToRenderCtx 0x7BC980` sets fovY = 2 * atan((448 / (2 * s[0x14])) / s[0x1C])
  (times 0.75 inside the atan when `dword_1136FC0 == 2`) and aspect = (512 / (2 * s[0x10])) /
  (448 / (2 * s[0x14])).
- Override paths exist through `g_ffxCameDirectLookAtFlag` and `g_ffxCameOverrideFlag`.
- Magic 540 in scene 208 drives `sub_645F00` with `sub_638E70() + 2.0`, other content resets it
  to 1.0 (latched by `byte_C44238`). Not traced further.

The render then applies the pixel offset and the 4:3 fold to the main camera (see
`READBACK_CENSUS.md` section 4). There is no scissor, as `FFX_GAME_NOTES.md` already says.

### 3.3 Which content uses a second screen

Two separate mechanisms, both at the tail of `FFX_Came_PushViewToRenderCtx` **[V]**:

1. **Magic 152 and 601, screen 1.** When `MagicFile__getMagicId()` is 152 or 601, the screen is 1
   and screen 1 is enabled (`byte_1137280` = screens[1]+0xC0 == 1), it calls
   `sub_639B30(g_ffxCameViewMatrix, 1)` and returns. `sub_639B30` writes camera slot 1 at
   gfx+0xFA3C (bound to the camera at gfx+0x394) and sets gfx+0xFAA1 = 1. When screen 1 is
   disabled, `ActivateScreen` calls `sub_639B30(nullptr, 0)`, which clears gfx+0xFAA1.
   At render time `sub_653230` (called at 0x66A6C9) tests gfx+0xFAA1 and, when set, draws a pass
   with the camera at gfx+0x394 into target +168 with depth +144, viewport = the target size,
   clear to 0,0,0,0, then the cluster at +156 opaque then transparent. It also animates gfx+1264.
   So magic 152 and 601 render a second camera view into an offscreen target that the effect then
   samples, not a split screen **[I]** for the sampling.
2. **A 256-wide screen (screen+0x88 == 256.0).** `sub_638D40(0)` sets split mode gfx+0x10D8C = 1
   and orientation gfx+0x10D90 = 0. Screen 1's view goes to `FFX_Gfx_SetCameraWorldMatrix2
   0x638AA0`, which writes gfx+0x10D94 and binds the camera at gfx+0x238. Any other width calls
   `sub_638D20`, which clears split mode. In split mode `sub_64FBB0` draws the viewport
   (x, y, w/2, h) with the main camera and (x + w/2, y, w/2, h) with the camera at gfx+0x238. The
   aspect becomes base * 16/9 * 0.5 for gfx+0x238 and winW * base / winH * 0.5 for the main camera.
   Which content opens a 256-wide screen is open.

Everything else is one screen. The main target size comes from `FFX_Gfx_FitViewTo16by9`, which
sets gfx+0x10DD0/0x10DD4 from the window clamped to 16:9 **[V]**. The cameras are bound each render
at 0x668A0B..0x668A93 (gfx+0xDC, gfx+0x394, gfx+0x238 when split, and gfx+0x654 with gfx+0x7B0)
**[V]**. The 2D pass sets its own scissor and sub-viewport (`RENDER_PASSES.md`).

### 3.4 F3 tie-in

- Keep the projection fold. The per-screen rect lives in the projection, and there is no scissor,
  so full-screen passes (sky, post, fades, UI) cover every screen. Parity means reproducing that
  bleed, not fixing it.
- Viewport per pass: the main scene uses the 16:9 fitted target, split mode halves it, the slot 1
  pass uses its own target size, the paused overlay and the final blit use the window size.
- The two-pass loop is simulation, because `FFX_Ch_StepAll` runs inside it per drawn screen. The
  recreation must keep that call count even if the renderer draws all screens from one snapshot.
- Sub-screen content first, main screen last, matches the loop order and is what parity needs for
  any state the passes share.

## 4. Proposed IDB names

IDA was read only for this pass. One row each, VA, name, evidence.

| VA | Proposed name | Evidence |
|---|---|---|
| 0x697490 | FFX_PostFx_DrawScreenFadeQuad | "ScreenFadeColor" parameter, picks ScreenFade, ScreenFadeAdditive or ScreenFadeSubtract by blend |
| 0x6AE5B0 | FFX_Gfx_FadeQuadCallback | callback of FFX_Gfx_QueueFadeQuad entries, writes colour unless locked, calls 0x697490 |
| 0x639240 | FFX_Gfx_QueueFadeQuad1000 | queues with flags 0x1001, called by FFX_Sg_StepScreenFades |
| 0x639280 | FFX_Gfx_QueueFadeQuad800 | flags 0x801, called by FFX_BtlResult_StepStateMachine |
| 0x6392C0 | FFX_Gfx_QueueFadeQuad8000 | flags 0x8001, magic host API data ref 0xC65B64 |
| 0x637C50 | FFX_ScreenFade_ClearCallback | delegation callback that zeroes the fade colour |
| 0x638840 | FFX_ScreenFade_SetColorVec | run by pass command kind 2, writes the colour unless locked |
| 0x698F70 | FFX_ScreenFade_AutoClear | presenter prologue, zeroes blend and alpha when the auto-clear flag is 1 |
| 0x644D40 | FFX_Gfx_QueueFadePassCommand | step-side appender of pass command kind 2 (I for the exact name) |
| 0x65F1F0 | FFX_Magic_IsEarlyFadeMagic | true for magic ids 106, 281, 515, 577, 522, 667, 445, 270, selects fade site A |
| 0x66B130 | FFX_Frame_DrawPausedSnapshot | back buffer, window viewport, clear black, draws +172 through 0x6959D0 |
| 0x6959D0 | FFX_PostFx_DrawFullscreenTexture | full-screen "ColorBuffer" quad from a target description |
| 0x639B30 | FFX_Gfx_SetCameraSlot1View | writes gfx+0xFA3C and sets or clears gfx+0xFAA1 |
| 0x638D40 | FFX_Gfx_EnableSplitCamera | sets gfx+0x10D8C = 1 and the orientation |
| 0x638D20 | FFX_Gfx_DisableSplitCamera | clears gfx+0x10D8C |
| 0x654550 | FFX_GfxCtx_SetSplitMode | setter of gfx+0x10D8C |
| 0x654570 | FFX_GfxCtx_SetSplitOrientation | setter of gfx+0x10D90 |
| 0x6517D0 | FFX_GfxCtx_GetSplitMode | getter of gfx+0x10D8C |
| 0x653230 | FFX_Frame_RenderSlot1CameraPass | tests gfx+0xFAA1, camera gfx+0x394 into +168 with depth +144 |
| 0x64FBB0 | FFX_Frame_DrawOpaqueTransparentPass | applies the 4:3 fold, halves the viewport in split mode |

## 5. Open

- Which content opens a 256-wide screen (split mode). The readers of gfx+0x10D8C and +0x10D90 are
  known, the ATEL callers of `FFX_AtelCmd_CameScreenOpen` were not enumerated by map.
- Whether magic 152 and 601 sample target +168 or show it directly.
- Whether type 4 alpha above 1 is clamped by the blend state.
- Whether +172 survives a resize while paused.
- What `sub_645F00` scales for magic 540 in scene 208.
