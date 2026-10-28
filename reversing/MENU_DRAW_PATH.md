# The menu draw path: from a module's draw slot to quads on screen

Written 2026-10-28 for Project Zanarkand item 1.12.8, and as the reference for 6.4.2 (the main menu
matching a screenshot) and the 2D part of the section 6 display list. Every address is an IDA VA in
FFX.exe (image base 0x400000). `[V]` was read in Hex-Rays or the disassembly this session, `[I]` is
inferred and says from what. Names are the ones `tools/apply_menu_draw_names.py` proposes (section
14), with the `sub_` name beside them where an unpatched IDB still shows it.

PROGRESS calls this "the sgMenu draw path". There is no such thing. `sgMenuInit/Exec/Result` are
ATEL syscall handlers that open a module and wait on it (TEXT_RENDERING 9, MENU_SCRIPT 9). The path
below is the one every menu module uses, the main menu included.

Related docs: MENU_SYSTEM.md (modules and descriptors), MENU_SCRIPT.md (menu_script.bin values and
scenes, its 4.2 is the draw group side of this), TEXT_RENDERING.md (glyphs in depth),
DYNAMIC_MESHES.md 9.4 (the 2D pools), DISPLAY_LIST.md 8, VISUAL_CENSUS.md 7.

## 1. Summary

- **Everything 2D ends in one function.** A module's draw slot (descriptor +0x04) calls a handful of
  rect helpers, which build a 152 byte sprite record and call `FFX_Sprite2D_Draw 0x63EEE0`. Text
  goes through `FFX_Glyph2D_Draw 0x640660`. Those two are the only callers of
  `FFX_DynDrawList_Append 0x67BA50`, so menus, message windows and the battle HUD all land in the
  one list manager `dword_CCC81C`. [V]
- **Layout is in PS2 units, 512x416, and the quad is in a fixed 1920x1080 frame.** The main menu
  writes its constants as HD pixels and converts them to PS2 units with `FFX_UiScaleX/Y`, and
  Sprite2D converts them back with `X = x * 3.75 - 960`, `Y = y * 2.596153736 - 540`. So in effect
  a main menu constant is an HD pixel measured from the top left corner. [V]
- **The 2D camera is an orthographic camera over -960..960 by -540..540** and the viewport is the
  whole render target, so the 2D layer stretches to the window. There is no pillarbox. [V for the
  camera values, I for y pointing down on screen]
- **No sort, no depth.** Quads draw in append order. Order is: the background, the menu objects of
  priority 0, every module's draw slot in id order, the SA task, menu objects of priority 1 to 8,
  the dim quad, then the message windows. Inside a priority, menu objects draw in pool order and
  the menu_script scenes of that priority draw first. [V]
- **The menu is drawn at the step rate, on the main thread.** Both the quad build and the command
  recording run on T0. A catch-up step builds nothing, a frame with no step re-renders the last
  step's quads. The Phyre render thread only executes and presents. [V for the call sites, I for
  the T1 split, from THREADS_AND_CLOCKS]
- **Simulation verdict: the draw path is presentation, with three known leaks out of it**, all
  already named: the pad sampler arm, the achievement 16 theater flag and the button prompt
  substitution state. One new hazard: the battle_script scenes the overdrive minigames wait on
  advance once per non-catch-up step, so their timing depends on the catch-up pattern. Section 12.
  [V]

## 2. The call path

```
FFX_MainStep 0x820AE0
  FFX_MenuSys_PollOpenAndStep
    FFX_MenuSys_RunFrame 0x8AB030
      FFX_MenuSys_StepFrame 0x8A9CA0
        FFX_DynDrawList_SwapFrameSets 0x642320            only when g_ffxIsCatchUpStep == 0
        ++byte_1840884                                    the menu step counter
        FFX_MenuSys_SamplePad 0x8BE500
        FFX_Module_StepAll 0x8AA200                       every module's exec (+0x00)
        g_ffxMenuDimLevel += g_ffxMenuDimStep, clamp 0..128
        FFX_Font_UploadMenuSheets 0x8B1190                dead on PC, see below
        FFX_Module_DrawAll 0x8AA290                       or sub_8DB870 (demo path, below)
          FFX_Menu_DrawBackgroundFade sub_8F5C60          the ffx_bg backdrop
          FFX_MenuObj_DrawAllAtPrio(0) 0x8A9690
          desc->draw (+0x04) for module 0..24              e.g. FFX_Menu_Module1Draw sub_8E0BA0
          g_ffxMenuSaTask
          FFX_MenuObj_DrawAllAtPrio(1..8)                 e.g. FFX_Menu_MainRowsDraw sub_8E20F0 at 7
        dim quad: FFX_Menu_DrawFillRect(0, 0, 512, 416, dim << 24, dim << 24)  module 1 active only
        FFX_MenuSys_DrawAllWindows 0x8ABE40               the menu's message windows
        sub_89DC10
        FFX_MenuSys_ReleaseTexLoadWhenDone                only when not catch-up
        sub_8A9510                                        PS2 fade upload, dead on PC
        FFX_DynDrawList_RefreshBatchGeometry 0x6423B0     only when not catch-up
                     |
   rect helpers  ->  FFX_Sprite2D_Draw 0x63EEE0  ->  FFX_DynDrawList_Append 0x67BA50
   text          ->  FFX_Glyph2D_Draw 0x640660   ->  FFX_DynDrawList_Append
                     |
FFXApplication__render 0x42F930  (same thread, after the step)
  FFX_Frame_PresentScene
    FFX_Frame_RenderScene 0x668930
      menu running: skip the 3D scene, bind target gfx+4300 +156 colour only, BeginScene(1)
      FFX_GfxCtx_Draw2DMeshes 0x650340                    kind 1 records
      FFX_GfxCtx_Draw2DList sub_650CB0                    kind 0 records, kind 2 under the HUD
        PRenderer__drawMeshInstance per record
      EndScene, sub_692C40 copies +156 to the back buffer
PhyreEngineRenderThread (T1) executes the recorded commands and presents
```

All of the StepFrame order is read from the decompile of 0x8A9CA0. [V]

Details worth knowing:

- `FFX_Module_DrawAll` only runs its body when `byte_1340859 != 0`, or `dword_1871538 == 0`, or
  the live scene is 382. [V]
- StepFrame calls `sub_8DB870` instead of `FFX_Module_DrawAll` when `dword_1340820` is set and no
  menu texture load is in flight. `dword_1340820` is set only for the StepFrame arguments
  0x800001 and 0x800002, the scripted menu demo modes (module 22 in MENU_SYSTEM). [V for the set,
  I that 0x8DB870 is that player's own draw]
- `FFX_Font_UploadMenuSheets 0x8B1190` runs every menu step but does nothing on PC.
  `FFX_Gs_UploadTexture 0x8B0B10` builds a PS2 GIF packet and hands it to `nullsub_39`. [V]
- The dim quad is drawn only while module 1 is active (`g_ffxModuleActiveMask & 2`) and
  `sub_8A98A0` (`FFX_MenuSys_GetDimLevel`) is non zero. Colour `dim << 24` is black with alpha
  `dim`, up to 128, which the untextured shader doubles to 1.0. It sits over everything the
  modules drew and under the message windows. [V]
- Opening the menu sets the rate divider to 1 (59.94 Hz) and closing it sets 2. [V]

## 3. Menu objects and the priority rules

The menu object pool is 32 records of 152 bytes from 0x1840900 (`FFX_MenuObj_Alloc 0x8AA1A0` scans
to 0x1841C40 checking the in-use byte at +0x40, `FFX_MenuObj_ClearAll` loops 32). [V]

```
+0x0C  step fn(obj)    called by FFX_MenuObj_StepAllAtPrio 0x8A9230 when +0x3F == prio
+0x10  draw fn(obj)    called by FFX_MenuObj_DrawAllAtPrio 0x8A9690 when +0x3E == prio
+0x3E  u8 draw prio    0..8
+0x3F  u8 step prio
+0x40  u8 in use
```

`FFX_MenuObj_Clear 0x8AA4B0` writes the word 257 at +62, so both priorities start at 1. [V]

**The step and draw walks cover only the first 16 objects.** DrawAllAtPrio starts at
`unk_1840910` (obj+0x10) and stops at `dword_1841290`, stride 152, which is 16 objects.
StepAllAtPrio stops at `dword_184128C`, also 16. Objects 16..31 can be allocated but are never
stepped or drawn. [V for the bounds, I that nothing in retail allocates more than 16 at once]

DrawAllAtPrio(prio), per call:

1. returns at once when `dword_134085C == 1` and `prio >= 6`. [V]
2. sets the 2D clip to the full frame, `FFX_Menu_SetClipRect sub_8F9230(0, 0, 512, 416)`, which
   writes `flt_18716F8..flt_1871704` and sets `dword_18716D0 = 1`. This is the scissor that
   `FFX_Text_ClipQuadToScissor 0x8E5A20` and `sub_8E58E0` clip against. [V]
3. draws the menu_script scenes of draw group `prio`, `FFX_MenuScene_DrawGroupRects(1, prio)`. [V]
4. walks the 16 objects and calls `obj->draw(obj)` for each whose +0x3E equals `prio`
   (`movzx eax, byte ptr [esi+2Eh]` with esi = obj+0x10, at 0x8A96E0). [V]

So the order rules are: a higher priority draws on top, a scene draws under the objects of its own
priority, objects of one priority draw in pool slot order, and module draw slots sit between
priority 0 and priority 1. Module 1's command list is a priority 7 object
(`FFX_Menu_BuildMainMenuRows 0x8E1C60` writes `mov byte ptr [edi+3Eh], 7` at 0x8E1F93), so it draws
over the party plates that the module 1 draw slot laid down. [V]

## 4. The primitives

### 4.1 The sprite record

`FFX_Sprite2D_Draw(rec, texPath, hdMap, cornerMode, kind)` takes a 152 byte record on the caller's
stack. Four corners of 32 bytes: [V]

```
corner k at +32k, k = 0..3
  +0  float x        PS2 units when hdMap != 0
  +4  float y
  +8  float u        normalised, top down
  +12 float v
  +16 int r, +20 int g, +24 int b, +28 int a     0..255, PS2 0x80 means 1.0
+128 int  draw kind  (500 is special in the clip code)
+136, +144, +148     zeroed by the helpers
```

Colour channel order low byte to high byte as r, g, b, a is [I], from `dim << 24` acting as an
alpha fade and the top byte of the packed colours being the alpha everywhere it is used.

### 4.2 The rect helpers

| helper | arguments | does |
|---|---|---|
| `FFX_Menu_DrawFillRect sub_8F3DB0` | x, y, w, h (ints), c0, c1 | untextured rect, `Sprite2D(rec, NULL, 1, 0, 0)`. Skipped while a menu texture load is in flight |
| `FFX_Menu_DrawFillRectClipped sub_8F4B20` | x, y, w, h, c0, c1 | the same through the scissor (`sub_8E58E0`) |
| `FFX_Menu_DrawTexRect sub_903EE0` | texId, x, y, w, h, u0, v0, u1, v1, c0, c1 | clips through `FFX_Text_ClipQuadToScissor`, normalises the UVs per atlas, `Sprite2D(rec, path, 1, 0, 0)` |
| `FFX_Menu_DrawTexRectNeutral sub_903BB0` | texId, x, y, w, h, u0, v0, u1, v1 | the same with every channel 128 |
| `FFX_MenuScript_DrawRectPrim 0x8FDF20` | a menu_script rect op | `Sprite2D(rec, 0, 1, 0, 0)`. The `bwakk` y = 67 patch (MENU_SCRIPT 4.2) is here |
| `FFX_MenuScript_DrawShapePrim 0x8FE0B0`, `FFX_MenuScript_DrawDigitsPrim 0x8FE080` | script ops | shapes and digits. The line op is `nullsub_159`, so script lines draw nothing on PC |
| `FFX_Menu_DrawScrollingGlow sub_8E7D30` | rect, UVs, scroll speeds, two colours | `texture.dds` (16001) with UVs scrolled by `FFX_Time_AppElapsedSeconds 0x641410` times the speed. The active party row shimmer |
| `FFX_Menu_DrawHandCursor sub_8C0670` | x, y, flag | the hand cursor, meswin id 200 at UVs (0.0009765625, 0.76074219) to (0.067382812, 0.8046875), bobbing by `cos(AppElapsedSeconds * 8) * 3` |
| `FFX_Menu_DrawHandCursorBlink sub_8C13D0` | x, y, flag | the cursor used while a second selection is pending, drawn on alternate halves of the menu step counter |
| `sub_8F8EE0 -> sub_8E7A90 -> sub_8E6070` | | the icon draw, ids 600 and up |

92 call sites reach Sprite2D. The dominant argument triple is (hdMap 1, cornerMode 0, kind 0) on
51 of them, which is every helper above. [V, by call site census, other sites not read one by one]

### 4.3 Atlases

`FFX_Menu_DrawTexRect` picks a texture by id and divides the UVs: [V]

| id | texture id | file | U divisor | V divisor |
|---|---|---|---|---|
| -1 | 16000 | battle_kuang | sheet width (2048) | sheet height (1024) |
| -2 | 16002 | ffx_bg | 2048 | 1024 |
| -3 | 16003 | menu_new | 2048 | 1024 |
| -4 | 16004 | stonetexture | 256 | 256 |
| -6 | 16006 | summonbg | sheet size | sheet size |
| 0..198, 200..398 | 15872 | meswin | already normalised | |
| 400..598 | 16128 | battle | already normalised | |
| 600..649 | 15808 | icon | 1024 | 512 |

`FFX_Font_GetSheetSize 0x8AC400` supplies the sheet sizes. `FFX_Menu_GetAtlasPath sub_8AC8C0` maps a
texture id to a path under `/FFX_Data/GameData/PS3Data/menu{,_us}/D3D11/*.dds.phyre`. 15872 meswin,
16128 battle, 15808 icon and 11980 strtex are language dependent (`menu_us` by default, `menu` for
language 0). 16000 battle_kuang, 16001 texture, 16002 ffx_bg, 16003 menu_new, 16004 stonetexture,
16006 summonbg, 11984 face_ply, 11988 face_smn and 11948 worldmap are fixed. The string table
pointers are at 0xC59D58..0xC59E58 and 0xC5997C. [V]

## 5. The quad

### 5.1 What Sprite2D does before it builds anything

1. **Returns on a catch-up step**: `cmp g_ffxIsCatchUpStep, 0` at 0x63EEF3. [V]
2. **Defers while a menu texture load is in flight** (`g_ffxMenuSysTexLoad` phase < 2): it rewrites
   the record into a four corner form, doubles untextured colours, and queues it on the loading
   list through `sub_63E930` / `sub_684CD0` instead of drawing. [V]
3. **PC button prompt substitution**: `FFX_ButtonPrompt_Substitute sub_6E69E0(&dword_C5955C,
   &dword_C59560, rec, &path, cornerMode)` with `this = dword_CE6DC0` swaps the texture for the
   keyboard or pad prompt sheet and remaps the UVs. A non zero return skips the draw. [V]
4. **Append**: `FFX_DynDrawList_Append(dword_CCC81C, path or "NoTexture", kind)`. The returned
   record's geometry is `rec+148`. Sprite2D writes geom+0 = textured flag, geom+32 = the path,
   geom+40 = the kind bits, and record +188 = 0 (the glyph path writes 1). [V]

### 5.2 Corner modes

`cornerMode % 5 == 1` is a triangle from corners 0, 1, 2 with 2 repeated. `cornerMode == 4` takes
four explicit corners. Anything else is an axis aligned rect from corner 0 (x0, y0, u0, v0) and
corner 1 (x1, y1, u1, v1). [V]

### 5.3 The rect, exactly

```
vertex   position    uv              colour, mode 0 (horizontal)   colour, mode 2 (vertical)
v0       (x0, y1)    (u0, 1 - v1)    c0                            c1
v1       (x1, y1)    (u1, 1 - v1)    c1                            c1
v2       (x1, y0)    (u1, 1 - v0)    c1                            c0
v3       (x0, y0)    (u0, 1 - v0)    c0                            c0

indices  base+0, 1, 3, 3, 1, 2        normals (0, 0, 1)    z = 1
colour   each int / 255.0 into a float4
```

The `1 - v` is because the D3D11 sheets are stored bottom up. [V for the code, I for the reason, the
same as TEXT_RENDERING 4]

### 5.4 The position transform

With `hdMap != 0`, which is every menu helper: [V]

```
X = (float)(x * 3.75 - 960.0)                 3.75 is dbl_B41D18 (0x63F9A9), 960.0 is dbl_B41D50
Y = (float)(y * 2.596153736114502 - 540.0)    dbl_B41D10 (0x63F9D0), 540.0 is dbl_B41D40
```

3.75 is 1920 / 512 exactly. 2.596153736114502 is `(float)(1080 / 416)` widened back to a double,
not the exact 2.5961538461. Each vertex is an x87 double expression stored to a float.

With `hdMap == 0`: mode 1 writes the corners raw, mode 3 writes `x - 960`, `y - 540` (already HD
pixels, 0x63F757 and 0x63F776), and the other modes keep the centre and scale the half extents by
3.75 and 2.596 (the 0.5 constant `qword_B92220` from 0x63F844). [V]

The main menu feeds that transform with `FFX_UiScaleX 0x6447E0` = `fld x, fmul 512.0
(dbl_B41D38), fdiv 1920.0 (dbl_B41D68), fstp float`, and `FFX_UiScaleY 0x644820` with 416.0
(dbl_B41D30) and 1080.0 (dbl_B41D58). So a main menu HD constant `h` lands at
`(float)((float)(h * 512 / 1920) * 3.75) - 960`, which is `h - 960` to within float rounding. For y
the round trip comes back about 4e-8 relative low, under 0.0001 pixel at the bottom of the frame.
[V for the code, the error figure is arithmetic]

### 5.5 Counts and segments

Each rect adds 4 vertices and 6 indices. The counts are stored per DynGeoMem segment at
`geom + 44 + 28 * sub_6DE9F0(dword_CDEDF0)`, where `dword_CDEDF0` is the DynGeoMem manager and
`sub_6DE9F0` its current set (FFX_DynGeoMem_GetCurrentSet in DYNAMIC_MESHES). [V]

### 5.6 The clip at +23456 is dead

`FFX_Sprite2D_ClipTest sub_644320` culls against bounds at list +23480..+23492 when list +23456 is
set. Its only setter, `sub_67E3B0`, has one caller, at 0x6448C6 inside a code block starting near
0x644850 that has no xrefs and whose address appears nowhere in the binary as a pointer. So in
retail the append time clip never turns on, and the clipping that does happen is the scissor of
`FFX_Menu_SetClipRect`, applied by the helpers before Sprite2D. [V for the xrefs and the byte
search]

## 6. Batching and submission

`FFX_DynDrawList_Append 0x67BA50` works on the set chosen by the toggle at list +23420. [V]

- **Not interleaved** (the default): a quad with the same texture as the open record joins it. The
  first quad makes a single record (`FFX_DynDrawList_NewSingleRecord sub_67C210`), the second
  promotes it to a multi record (`FFX_DynDrawList_PromoteToMulti sub_67DC70`), and a multi record
  holds up to 12 quads (counter at +23416). A texture change opens a new record. [V]
- **Interleaved** (list +23436, set by `FFX_DynDrawList_SetInterleaved 0x643AF0` through
  `sub_67E4D0`): up to 6 open texture slots at once, with names in `dword_CCC7D4[]`, counts in
  `dword_CCC7EC[]` and layers in `dword_CCC804[]`. The layer is list +23440, set by
  `FFX_DynDrawList_SetLayer 0x640DD0` through `sub_67E4E0`. `FFX_DynDrawList_FlushInterleaved
  sub_67B530` emits the open records layer 0 first, then layer 1. Text uses this so every shadow of
  a string lands under every glyph of it. [V]
- **Kinds 1, 2 and 10 always start a fresh single record.** [V]
- **Pools**: 600 single and 800 multi records per set (DYNAMIC_MESHES 9.4).
- **The swap**: `FFX_DynDrawList_SwapFrameSets 0x642320` clears the set about to be written and
  flips the toggle, on non-catch-up steps, and only when no menu texture load is in flight. The
  render reads through the same toggle: `FFX_DynDrawList_GetCount sub_67B970` returns +22592 when
  the toggle is set, otherwise +11344, and `FFX_DynDrawList_GetItem sub_67B990` reads through +16968
  or +5616, gated on +23453. So the render reads the set the latest step wrote. The other set holds
  the step before. [V]
- **Binding**: `FFX_DynDrawList_RefreshBatchGeometry 0x6423B0` and
  `FFX_DynDrawList_RebindBatchGeometry 0x67E7F0` run `FFX_DynDrawList_BindBatchGeometry sub_67D920`
  on each record at the end of the step. That binds a pooled MenuDynamicGeometry, picks the shader
  and copies geom+40 into record +180. The shader choice: [V]

| record | shader | gfx slot | loaded by |
|---|---|---|---|
| glyph (+188 set) | OptimizedFontShader | gfx+0xFA94 | `sub_665580` |
| path "RealFrameBuffer" | the frame buffer copy | gfx+0xFA88 | |
| path "NoTexture" | MenuDefaultShaderNT | gfx+0xFA80 | `sub_665010` |
| anything else | MenuDefaultShader | gfx+0xFA84 | `sub_6650F0` |
| (not chosen here) | MenuDefaultShaderNTZTest | gfx+0xFA78 | `sub_6653C0` |

All four loaders are called from `FFX_Boot_FirstAsyncLoad`. [V]

**The kind is a pass selector, not a depth.** Record +180 is read by the two 2D draw functions: [V]

- kind 0, the normal case: `FFX_GfxCtx_Draw2DList sub_650CB0`.
- kind 1: `FFX_GfxCtx_Draw2DMeshes 0x650340`, which RenderScene calls first, so kind 1 lands under
  kind 0.
- kind 2: drawn by `sub_650CB0` only when `FFX_Booster_IsHudShown`, with a scissor.

Draw order inside a kind is append order. Depth test is off in every menu shader. [V]

## 7. Shaders and colour math

From `tools/phyre_shaders.py disasm` on the shipped shader blobs (derived text in a scratch folder,
nothing checked in). [V]

| shader | vertex | pixel | blend |
|---|---|---|---|
| MenuDefaultShader (PS 605613f9) | position by `cb0[10..13]` | `o = 2 * tex * vcol`, then `o.rgb *= o.a` | one, invsrcalpha, z off, cull none |
| MenuDefaultShaderNT (VS 7671bc, PS 1f432444) | `o1 = vcol * (1, 1, 1, 2)` | `o.rgb = v.rgb * v.a`, `o.a = v.a` | one, invsrcalpha |
| OptimizedFontShader (PS de0160c1) | | `o.rgb = 0.5 - 0.5 * cos(pi * sat(c^0.588235 + 0.3c))` on the vertex colour, `o.a = tex.a * v.a`. Texture RGB unused | srcalpha, invsrcalpha |

What that means for matching a screenshot:

- **Textured quads**: a vertex colour of 128 per channel is `128 / 255 * 2 = 1.0039`, so 0x80 is
  neutral. The output is premultiplied and blended `one, invsrcalpha`, which is ordinary alpha
  blending.
- **Untextured fills** double only the alpha. A 0x80 grey fill is 0.502 grey at alpha 1.0. That is
  the PS2 GS rule, modulate for textures and raw colour for flat fills.
- **Glyphs** colour from the vertex only, through that cosine curve, and take their shape from the
  texture alpha. Glyph vertex alpha is already doubled by the glyph path (TEXT_RENDERING 6.2).
- The MenuDefaultShader material has Transparent, ScissorPass and ExtraPass. `sub_650CB0` asks for
  "Opaque" then "Transparent", and only Transparent exists, so each record draws once.

## 8. The render side and the camera

- **Camera**: gfx+0xA84 is a `PCameraOrthographic` (constructed at 0x64DE7A).
  `FFX_EscMenu_LoadMovie 0x65F450` writes aspect gfx+0xBD4 = 1.7777778 (0x65F7B7, `flt_B41CF0`) and
  half height gfx+0xBD8 = 540 (0x65F7D2, `flt_B43DD4`). So the view is -960..960 by -540..540. That
  function is called from `FFX_BootSequence`, so its name covers only part of what it does. [V]
- **Viewport**: the full target size, so the 1920x1080 frame stretches to any window. [V]
- **y down**: positions grow down the screen (y0 is the top in every menu layout). That the
  camera maps +Y down is [I], from the bottom up textures, the `1 - v` and the upright menu.
- **`FFX_Frame_RenderScene 0x668930` while a menu runs**: when `g_ffxMenuSysRunning` is set it
  skips the 3D scene after the job wait, binds target gfx+4300 +156 as colour 0 with no depth, sets
  the local flag `v343 = 1`, calls BeginScene(1), draws kind 1 (`FFX_GfxCtx_Draw2DMeshes`, when
  gfx+120 is set) then kind 0 (`sub_650CB0`), and ends the scene. Later `sub_692C40` at 0x66ADCA
  copies +156 (menu) or +140 (field) to the back buffer, then the debug titles and Iggy draw. [V]
- **`FFX_GfxCtx_Draw2DList sub_650CB0`** is gated on gfx+69068 or the menu running, and on
  `g_debugInfoState+80` through `sub_6B6370`. It returns early when `g_ffxMenuSysRunningAlt` is set
  and the booster HUD is hidden. [V]
- `FFX_Frame_Draw2DOnlyMenuOpen sub_653060` is the frame where the menu open is pending: it clears to
  black and draws all 2D. `sub_653960` is the FMV path. [V]
- The menu background is not the captured field. `FFX_Menu_DrawBackgroundFade sub_8F5C60` draws
  `ffx_bg` (texture id -2) over 0,0 to 512,416 with UVs 0,0 to 1920,1024, taking the alpha bytes of
  `col_menubg0/1` (bytes 0x25D1678..0x25D167F, from menu_script.bin) as a left to right gradient and
  forcing the RGB to black. In game mode 0x400000 both colours are 0x80000000. Module 1 then draws
  `ffx_bg` again at neutral colour through `FFX_Menu_DrawBackground sub_8F5C10`. [V]

## 9. Threads and rate

| work | thread | evidence |
|---|---|---|
| quad build (Sprite2D, Glyph2D, Append, Bind) | T0, inside `FFX_MainStep` | the call path in section 2 [V] |
| PRenderer command recording (`sub_650CB0` -> `PRenderer__drawMeshInstance`) | T0, inside `FFXApplication__render 0x42F930` after the step | THREADS_AND_CLOCKS T0 [V there] |
| command execution and present | T1, PhyreEngineRenderThread | THREADS_AND_CLOCKS T1 [V there] |

The menu is drawn at the step rate. Quads are built only on non-catch-up steps (Sprite2D and Glyph2D
both return on a catch-up, and the swap and the rebind are skipped). A present with no step in front
of it records the same set again. The menu runs at divider 1, so on a 60 Hz display every present
normally has a step. [V for the gates]

How T1 is kept off the set T0 is writing is [I]: the DynGeoMem segment index in the count slots
(section 5.5) suggests the vertex memory rotates per frame, which is what would make the hand off
safe. The swap toggle does not do it, since the render reads the set the step just wrote.

One exception to "nothing on a catch-up step": the PC sphere grid. Module 19's draw slot
`FFX_Menu_Module19Draw sub_8E2720` (descriptor 0xC5B2F4 +4 = 0xC5B2F8) calls `sub_A54560`, which may call
`FFX_Menu_DrawAllPartsIgnoringCatchUp 0xA54660`. That saves `g_ffxIsCatchUpStep`, clears it, draws
the PC menu parts and restores it, so those parts do draw on catch-up steps. [V for the chain, the
sphere grid itself is item 1.12.14]

## 10. Text, briefly

`FFX_Text_DrawString 0x905AB0 (s, x, y, colour, scale)` and `FFX_Text_DrawStringScaled sub_905970`
(the main menu's label call, adds an alignment byte) walk the string: [V]

```
FFX_Text_ClassifyByte 0x8DF610 -> FFX_Text_DecodeGlyph 0x8B7240        a 0x24 byte glyph record
FFX_DynDrawList_SetInterleaved(1)
  SetLayer(0): sub_8FAE40 -> FFX_Text_DrawGlyphShadow 0x8FBAE0         shadow at +2, +2
  SetLayer(1): FFX_Text_DrawGlyphQuad 0x8FB940                         kind 2000,
               FFX_Font_ResolvePageTexture 0x8ACF50, UVs over FFX_Font_GetSheetSize, the scissor
               -> FFX_Glyph2D_Draw 0x640660 (rect, tex, 0, 0, page + 2)
FFX_Text_AdvancePen 0x8E8ED0
line end: x back to the line start, y += 16
```

The glyph quad rounds each edge to a whole HD pixel and keeps glyphs square (pen across at 3.75,
width at 2.596). TEXT_RENDERING 4 to 6 has the formulas, they are not repeated here.

## 11. For 6.4.2: the main menu

Every number here is HD pixels from the top left of the 1920x1080 frame, which is what the code's
`FFX_UiScaleX/Y` round trip comes to (5.4). `t` values are Q12, 4096 = 1.0.

### 11.1 Module 1's draw slot, `FFX_Menu_Module1Draw sub_8E0BA0`

Reached only through descriptor 0xC5B2B0 +4. In order: [V, decompile]

1. `FFX_Menu_DrawBackground`: `ffx_bg`, 0,0 to 1920,1080, UVs 0,0 to 1920,1024 over 2048x1024,
   neutral colour.
2. Writes `g_ffxMenuNamePlateSpacing = 44` and `g_ffxMenuCommandPlateSpacing = 32`. Nothing reads
   either, so they change nothing.
3. Party rows, for i = 0 .. `sub_8A9B50()` - 1 (the party list count). A row whose slide word is 0
   is skipped entirely, so rows appear one at a time.

```
slide  = FFX_Ease_ExpOut(g_ffxMenuPartySlide[i]) * -1024 / 12288     PS2 units, 0 when settled
x      = 145 + slide * 3.75                                          HD
y      = 200 + 90 * i
```

`FFX_Ease_ExpOut sub_88E750(a) = g_ffxEaseTableQ12[(4096 - a) / 64]`. The table is 65 words built
at boot in `tklib__f88D710` (0x88D837): entry 64 = 4096, then going down `e[k-1] = e[k] * 9 / 10`
in integers, so 4096, 4096, 3686, 3317, 2985, 2686, 2417, 2175, 1957, 1761, 1584, 1425, 1282, 1153,
1037, 933, ... 11, 9, 8, ... 1, 0 at entry 0. Its twin `FFX_Ease_ExpIn sub_88E730(a)` reads
`e[a / 64]`. The full 65 values come from the arithmetic, the code is [V]. So a row starts 341.3 PS2
units (1280 HD pixels) left and eases in.

Per row, with x, y as above:

| piece | call | rect (x, y, w, h) | source rect or UVs | colour |
|---|---|---|---|---|
| plate | DrawTexRectNeutral(-3) | x, y, 1109, 72 | menu_new 0, 86i to 1109, 86i + 72 | neutral |
| glow x4, active members only (i < `sub_8A9B70()`, which returns `dword_1841C24`) | DrawScrollingGlow | x, y, 250, 64 and x + 250, y, 250, 64 | texture.dds, source origin U 0, 250, 300, 550 at V 64(i+1), followed by 250, 64 (a size, I), scroll speeds (50, -20) for the first pair and (-80, -10) for the second | RGB 0x70, 0x68, 0xC5 with alpha 0 to 0x1A (first pair) and 0 to 0x33 (second), mirrored on the right half |
| name | `sub_905930` | x + 160, y + 9 | scale 0.82 | `sub_8A9A70(char)` |
| HP label | DrawTexRectNeutral(33) | x + 490, y + 5, 61.5, 23 | UVs 0.34765625, 0.014648438 to 436/1024, 41/1024 | neutral |
| HP text | `sub_901660` "%5d/%5d" | x + 604, y + 6 | scale 0.72, colour 37 | |
| MP label | DrawTexRectNeutral(34) | x + 540, y + 32, 49, 23 | 0.49804688, 0.014648438 to 570/1024, 0.040039062 | neutral |
| MP text | "%4d/%4d" | x + 625, y + 34 | 0.72, 37 | |
| bar | DrawTexRectNeutral(-1) | x + 844, y + 28, 208, 33 | battle_kuang 0,603 to 208,636 | neutral |
| frame | DrawTexRectNeutral(61) | x + 1038, y - 3, 75, 75 | 0.48632812, 0.58886719 to 0.55957031, 0.66210938 | neutral |
| level plate | DrawTexRectNeutral(61) | x + 952, y + 35, 117, 26 | 834/1024, 0.08203125 to 978/1024, 0.11328125 | neutral |
| level number | `sub_905820` | x + 1076 - width / 2, y + 16 | scale 0.68 | |
| cursor | DrawHandCursor | x + 150, y + 14 | | |
| pending cursor | DrawHandCursorBlink | x + 140, y + 9 | | |

Language 11 widens the HP label by 22 and heightens it by 1 (and its u1 and v1 by 22/1024 and
1/1024), moves both number strings right by 11, and changes the level plate to u0 -35/1024,
u1 +20/1024, x -10 and width +25. The HP label's u1 and v1 are 436/1024 and 41/1024 in the base
case. Ids 33, 34 and 61 are meswin cells with normalised UVs.

4. The bottom of the screen:

| piece | call | rect | colour |
|---|---|---|---|
| gil band | DrawFillRectClipped | 639, 870, 600, 32 | 0x00FFFFFF to 0x40FFFFFF |
| gil | `sub_8C0A20` | 145, 870, 1108, 32 | |
| playtime band | DrawFillRectClipped | 145, 870, 325, 32 | 0x40FFFFFF to 0x00FFFFFF |
| playtime | `sub_8E19F0` | 270, 878 | colour 37, the fifth argument flips between 37 and 1 with the playtime's low bit, which is the colon blink [I that it is the colon] |
| location bar | DrawTexRect(-1) | 145, 951, 1130, 55, battle_kuang 0,544 to 1600,599 | 0x80808080 both |
| location tab | DrawTexRectNeutral(200) | 145, 933, 360, 28, UVs 0.52929688, 0.46582031 to 0.88085938, 0.49316406 | neutral |
| location name | `sub_9016B0` | 235, 956, scale 0.78 | |

### 11.2 The command list, `FFX_Menu_MainRowsDraw sub_8E20F0` (object prio 7)

Draws only when `FFX_Module_GetActiveMask() & 0x80800` is zero. `t = g_ffxMenuMainRowsOpen`. [V]

```
divider   DrawTexRect(-1, 1280, 194 * t/4096, 2, 805 * t/4096,  UV 1879,542 to 1881,662, 0x80808080)
rows      for each row i except the cursor row, then the cursor row last:
          x = 1304
          y = ((194 + 75 * i) * t + 7.788 * (4096 - t)) / 4096     (7.788 is 3 PS2 units)
          plate  DrawTexRect(-3, x, y, 476, 60, UV 1118, 74.7i to 1599, 74.7i + 67,
                             colour 0x808080 with alpha t / 32 on both corners)
          label  centred in 476 at y + 7, scale 0.78 (FFX_Text_DrawStringScaled)
          cursor x + 115, y + 10, only on the cursor row and only when t == 4096
```

So the rows fall from y 3 to their place and fade in together, with the divider growing from the
top. The 74.7 is a float constant (74.69999694824219). In the code the start term is `3 * (4096 - t)`
in PS2 units, so the rows start 3 PS2 units, about 7.8 HD pixels, from the top. The row index and
`t` reach `FFX_Menu_MainRowDraw` as its first and fifth arguments (the fifth is `dword_1871558`
loaded into ecx). [V for the code, the unit reading is arithmetic]

### 11.3 The animation clocks

| motion | clock | rule |
|---|---|---|
| command list open | menu step, `FFX_Menu_MainRowsStep sub_8E2040` | `t += 341` per step while `dword_1871554` is set, clamp 4096 (4092 after 12 steps, 4096 on the 13th). `t -= 341` to 0 when closing |
| party row slide | menu step, same function | each row waits `g_ffxMenuPartySlideDelay[i]` steps, then its slide word adds 227 per step to 4096 (19 steps). `g_ffxMenuPartyRowsLanded` (0x1871564) is the AND of all 8, so 4096 means every row has landed |
| hand cursor bob | wall clock, `FFX_Time_AppElapsedSeconds` | `cos(seconds * 8) * 3` |
| party row glow | wall clock | UVs scroll by seconds times the speeds |
| pending cursor blink, playtime colon | menu step counter and playtime | |

For a screenshot match, freeze the wall clock (or compare at a known `AppElapsedSeconds`) and wait at
least 13 steps for the list and 4 * (rows - 1) + 19 for the plates. The delay seeding of 4i is
from the open path [I, read in passing, not re-checked for this doc].

## 12. Simulation verdict

**The draw path is presentation.** A census of the static call tree under `FFX_Module_DrawAll`,
`FFX_Menu_Module1Draw`, `FFX_Menu_MainRowsDraw` and `FFX_MenuSys_DrawAllWindows` covers 267
functions and 46 absolute address globals written. All of them are draw scratch except: [V]

| written in the draw path | why it matters | already known |
|---|---|---|
| `g_ffxMenuPadBlock = 1` in `FFX_MenuSys_DrawAllWindows` | arms the pad sampler for the next step | yes, INPUT_LAYER |
| `g_ffxAchTheaterArmed` via `FFX_MesWin_DrawInsertedValue` | achievement 16 | yes, STEAM_ACHIEVEMENTS 5.10 |
| `dword_C5955C`, `dword_C59560`, `dword_1841D24` | the PC button prompt substitution state | presentation, but step written |
| the two plate spacings | no reader at all | harmless |

Not covered by the census: pointer writes (the known ones are the scene state inside the
menu_script buffer and the `bwakk` patch) and the indirect calls at 0x8AA382 (module draw slots),
0x8AA3A1 (the SA task), 0x8A96F2 (menu object draws) and 0x8B5E65 (the MesWin draw slot). Module
1's targets were added as roots by hand. The other modules' draw slots are outside this item.

**State the step reads back from the draw path:**

- The menu_script scenes (slot 1) step inside `FFX_MenuScene_DrawGroupRects`, called from
  DrawAllAtPrio, called from `FFX_Module_DrawAll`, which StepFrame calls on catch-up steps too. So
  a slot 1 scene advances once per menu step, catch-up or not. Nothing reads a slot 1 scene's state
  (MENU_SCRIPT 7), so that stays presentation. [V]
- **The battle_script scenes (slot 2) are a determinism hazard.** `FFX_BtlMenu_DrawRoot 0x89B360`
  has one caller, `FFX_MesWin_DrawAllWindows 0x8ABD30` at 0x8ABD80. That has one caller,
  `FFX_MainStep` at 0x82153C, behind `cmp g_ffxMesWinDrawEnabled, 0` at 0x82152A and
  `cmp g_ffxIsCatchUpStep, 0` at 0x821533. On a catch-up step it calls
  `FFX_MesWin_ArmPadSampler(1)` instead. So `btidu`, `bgood`, `bauro`, `blulu` and the rest advance
  once per **non-catch-up** step and not at all on a catch-up step, while the overdrive minigames
  that wait on `FFX_MenuScene_IsDone(2, ...)` step every step. How many steps a minigame phase lasts
  therefore depends on how many catch-up steps fell inside it, which is wall clock and booster
  dependent. [V for the gates and the callers, the effect on a battle outcome is item 1.10's]
- `FFX_BtlOd_WakkaReels_Draw 0x8974A0` is the other slot 2 caller. It is reached only through the
  overdrive vtable at 0xC65710, so its rate depends on who dispatches that slot. Not traced here.

## 13. Corrections made to other docs

- **VISUAL_CENSUS.md section 7**: the row for 2D texture, depth and kind called geom+40 a sort depth
  with "kind writer not found". geom+40 is the pass kind that Sprite2D writes from its fifth
  argument, and `sub_67D920` copies it into record +180 at bind time. The 2D clip rect row named
  `sub_67E3B0` as the writer without noting that its only caller is unreferenced, so the clip is
  dead in retail. Note added there.
- **MENU_SCRIPT.md** sections 1, 4.2, 7 and 11: "advance per draw call, so they are display rate"
  is wrong for slot 1. A draw call happens once per menu step, catch-up included, so they are step
  rate. The open question on `FFX_BtlMenu_DrawRoot` is answered (once per non-catch-up step, never
  on a catch-up), which makes the battle_script scene timing catch-up dependent. Notes added there.
- **SCREENS_AND_FADES.md 2.3**: the 0x66ADCA `sub_692C40` flag whose role "was not traced" is
  RenderScene's local `v343`, set when the menu is running, so it copies +156 for a menu frame and
  +140 otherwise. Note added there.
- **OVERDRIVES.md / MINIGAMES_TIMED.md**: a pointer to section 12 for the scene clock. Note added
  to MINIGAMES_TIMED.
- **IDB comment on `FFX_MenuObj_StepAllAtPrio`** says "32-entry pool". The pool is 32 but the walk is
  16, and the draw twin's priority byte is +62 where the step's is +63. The names tool only comments
  rows it renames, so this needs a hand edit: append "The walk stops at dword_184128C, 16 of the 32
  objects. Objects 16..31 are never stepped. MENU_DRAW_PATH.md 3" to both walks.
- **`FFX_EscMenu_LoadMovie 0x65F450`** also sets up the 2D orthographic camera and is called from
  `FFX_BootSequence`. Not renamed here, it has callers and docs that cite the name. Worth a comment
  by hand.

## 14. Names

`tools/apply_menu_draw_names.py`, rehearsed with `--apply --comments` on a copy of the IDB: 40
functions and 15 globals renamed, 0 skipped, 0 failed. All targets were placeholders and no proposed
name was taken.

Functions: `FFX_Menu_Module1Draw` 0x8E0BA0, `FFX_Menu_MainRowsDraw` 0x8E20F0,
`FFX_Menu_MainRowsStep` 0x8E2040, `FFX_Menu_MainRowDraw` 0x8E22A0, `FFX_Menu_DrawTexRect`
0x903EE0, `FFX_Menu_DrawTexRectNeutral` 0x903BB0, `FFX_Menu_DrawFillRect` 0x8F3DB0,
`FFX_Menu_DrawFillRectClipped` 0x8F4B20, `FFX_Menu_DrawBackgroundFade` 0x8F5C60,
`FFX_Menu_DrawBackground` 0x8F5C10, `FFX_Menu_DrawScrollingGlow` 0x8E7D30,
`FFX_Menu_DrawHandCursor` 0x8C0670, `FFX_Menu_DrawHandCursorBlink` 0x8C13D0,
`FFX_Menu_GetAtlasPath` 0x8AC8C0, `FFX_Menu_SetClipRect` 0x8F9230, `FFX_Menu_Module19Draw`
0x8E2720, `FFX_MenuSys_GetDimLevel` 0x8A98A0, `FFX_GfxCtx_Draw2DList` 0x650CB0,
`FFX_Frame_Draw2DOnlyMenuOpen` 0x653060, `FFX_GfxCtx_LoadMenuShaderNT` 0x665010,
`FFX_GfxCtx_LoadMenuShader` 0x6650F0, `FFX_GfxCtx_LoadFontShader` 0x665580,
`FFX_GfxCtx_LoadMenuShaderNTZTest` 0x6653C0, `FFX_DynDrawList_GetCount` 0x67B970,
`FFX_DynDrawList_GetItem` 0x67B990, `FFX_DynDrawList_BindBatchGeometry` 0x67D920,
`FFX_DynDrawList_NewSingleRecord` 0x67C210, `FFX_DynDrawList_NewMultiRecord` 0x67C170,
`FFX_DynDrawList_PromoteToMulti` 0x67DC70, `FFX_DynDrawList_FlushInterleaved` 0x67B530,
`FFX_DynDrawList_OpenSlot` 0x67E4A0, `FFX_DynDrawList_FindSlot` 0x67E310,
`FFX_DynDrawList_SetInterleavedImpl` 0x67E4D0, `FFX_DynDrawList_SetLayerImpl` 0x67E4E0,
`FFX_DynDrawList_SetClipTransform_UNUSED` 0x67E3B0, `FFX_Sprite2D_ClipTest` 0x644320,
`FFX_ButtonPrompt_Substitute` 0x6E69E0, `FFX_Text_DrawStringScaled` 0x905970, `FFX_Ease_ExpOut`
0x88E750, `FFX_Ease_ExpIn` 0x88E730.

Globals: `g_ffxMenuMainRowsOpen` 0x1871558, `g_ffxMenuMainRowsOpening` 0x1871554,
`g_ffxMenuPartySlide` 0x1871678, `g_ffxMenuPartySlideDelay` 0x1871668, `g_ffxMenuDimLevel`
0x184087C, `g_ffxMenuDimStep` 0x1840880, `g_ffxMenuStepCounter` 0x1840884, `g_ffxDynDrawList`
0xCCC81C, `g_ffxDynDrawSlotTex` 0xCCC7D4, `g_ffxDynDrawSlotCount` 0xCCC7EC, `g_ffxDynDrawSlotLayer`
0xCCC804, `g_ffxButtonPromptPendingA` 0xC5955C, `g_ffxButtonPromptPendingB` 0xC59560,
`g_ffxEaseTableQ12` 0x23CBEE0, `g_ffxMenuPartyRowsLanded` 0x1871564.

Existing names that deserve a comment by hand (the tool does not touch named rows):
`FFX_MenuObj_StepAllAtPrio` and `FFX_MenuObj_DrawAllAtPrio` (the 16 object walk),
`FFX_EscMenu_LoadMovie` (the 2D camera), `FFX_MesWin_DrawAllWindows` (the only caller of
`FFX_BtlMenu_DrawRoot`, skipped on catch-up steps).

## 15. Open

- How T1 is kept off the vertex memory T0 writes. The DynGeoMem segment rotation is the likely
  answer, not proved here (DYNAMIC_MESHES owns it).
- The rate of `FFX_BtlOd_WakkaReels_Draw`, reached through the overdrive vtable 0xC65710.
- Whether anything allocates more than 16 menu objects at once, which would leave some undrawn.
- The draw slots of modules other than 1, and the `sub_8DB870` demo draw path.
- `FFX_ButtonPrompt_Substitute`'s rules (which ids it swaps, which sheet) are not decoded. A
  screenshot taken on a pad and one on a keyboard will differ where a prompt icon shows.
- The y direction of the 2D camera is inferred, not read from the projection.
