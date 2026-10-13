# Text rendering: the FFX encoding, the .ftc fonts and the path from a glyph id to a quad

All addresses are VA at the preferred base 0x400000. Everything marked **V** was read out of
Hex-Rays and the disassembly in the IDB, or checked against the shipped data, in this pass. **I**
is inference. Names in `code` that start with `FFX_` and are not in the IDB yet are proposals,
listed in section 11. The IDB was not changed.

The recreation's side is `src/engine/text/` in projectzanarkand (`FfxText.h`, `FtcFont.h`,
`TextLayout.h`), checked by `tests/engine/TextLayoutTest.cpp` and the independent Python model
`tools/ftc.py`.

## 1. The short version

- Text is FFX's own byte code. A text byte is `0x30 + glyph index`. Bytes below 0x30 are line ends
  and control codes, most of which take one argument byte. **V**
- The glyph index picks a cell on a PS2 era sheet of 9 columns of 14 texels by rows of 18, two
  glyphs per cell, one on each of two pages: `page = glyph & 1`, `u = 14 * (glyph % 18 / 2)`,
  `v = 18 * (glyph / 18)`. **V**
- Per glyph widths are signed quarter texels in the FTCX block of a `.ftc`. For every Latin
  language that is `ffx_ps2/ffx/master/uspc/menu/base.ftc`, 230 glyphs. **V**
- The HD page textures are that 128x234 sheet scaled to 512x1024 and stored bottom up. The UVs are
  the sheet texel divided by the FTCX sheet size, and the vertex gets `1 - v`. **V**
- All 2D layout is in the PS2's 512x416 units. A pen position goes to the fixed 1920x1080 frame at
  3.75 across and 2.596 down, but a glyph's width is scaled by 2.596 on both axes and every edge is
  rounded to a whole HD pixel. The pen advance is `round(width * scale * 2.596)` HD pixels plus 3,
  and a line is 16 units. **V**
- Every glyph quad on PC goes through one function, `sub_640660` (`FFX_Glyph2D_Draw`), into the
  dynamic 2D draw list `dword_CCC81C` with `FFX_DynDrawList_Append 0x67BA50`, which only it and
  `FFX_Sprite2D_Draw 0x63EEE0` call. **V**

## 2. The encoding

### 2.1 Byte classes and steps

`FFX_Text_ClassifyByte 0x8DF610` sorts a byte, `FFX_Text_NextChar 0x8DFD80` steps over a
character. Both are table switches, read whole. **V**

| bytes | class | step | meaning |
|---|---|---|---|
| 0x00 | 1 line end | 0 | terminator |
| 0x01, 0x02 | 1 | 1 | page end. `FFX_MesWin_MeasureTokenRun` resets the height, the draw treats it as a new line |
| 0x03 | 1 | 1 | new line |
| 0x04 | 4 text | 3 if the next byte is <= 0x2F, else 2 | Korean and Chinese page prefix (+1040). A Latin draw shows the blank glyph |
| 0x05 | 4 | 1 | skipped by the measure |
| 0x06 | 4 | 2 | sheet 5 glyph |
| 0x07 | 2 control | 2 | pen position, section 2.2 |
| 0x08 | 2 | 2 | no handler in the window draw found |
| 0x09 | 2 | 2 | typewriter speed and pause |
| 0x0A | 2 | 2 | colour |
| 0x0B | 2 | 2 | button icon, the argument is the icon id |
| 0x0C | 4 | 1 | blank glyph |
| 0x0D | 2 | 2 | no handler in the window draw found |
| 0x0E | 2 | 2 | text style |
| 0x0F, 0x11 | 4 | 1 | blank glyph |
| 0x10 | 2 | 2 | choice marker, 0x50..0x57 inserts a choice string |
| 0x12..0x23 | 3 ignored | 2 | inserted values, expanded by the window draw, skipped by `FFX_Text_DrawString` |
| 0x24, 0x25 | 4 | 2 | blank glyph |
| 0x26..0x2B | 4 | 2 | kanji sheets 3, 2, 1, two bytes, `208 * b + next - 7952 / 8368 / 8784` |
| 0x2C..0x2F | 4 | 2 | page prefix, `glyph = 208 * (b - 0x2B) + next - 0x30` |
| 0x30..0xFF | 4 | 1 | `glyph = b - 0x30`. A run of 0x47 is one character |

The blank glyph is the fallback branch of `FFX_Text_DecodeGlyph 0x8B7240`: font id as usual,
baseline 0, width 10, cell `(70, 0)..(80, 18)`, which is glyph 10, the space. **V**

### 2.2 The control codes the window draw acts on

`sub_8DF8F0` (`FFX_MesWin_ApplyControlCode`) is called for class 2 bytes from the window draw. **V**

| code | argument | effect on the pen (`+4` x, `+8` y, `+26` style, `+27` colour) |
|---|---|---|
| 0x07 | 0xE0..0xEF | `x += arg - 224` |
| 0x07 | 0x40..0xBF | `x = lineStart + 4 * arg - 256` |
| 0x07 | 0x31, 0x38 | `sub_8DF520`: centre the rest of the line, `x = (right - left - width) / 2 + left` with the width from `FFX_MesWin_MeasureTokenRun` |
| 0x07 | 0x32 | right align, `x = right - width` |
| 0x09 | 0x30 | textObject+172 = 0, the per character rate. Other values below 0xC0 set it to `arg - 0x30`, 0xC0 and up set the pause +180 to `arg - 0xC0` |
| 0x0A | low nibble n != 0 | glyph colour nibble = n - 1 |
| 0x0A | high nibble h >= 4 | shadow colour nibble = h - 4 |
| 0x0B | icon id | draws the icon (`sub_8F8EE0`) at 0.65 or 0.73 scale and advances by its width plus 1 or 1.5 |
| 0x0E | 0x30..0x33 | style = arg - 0x30 |
| 0x0E | 0x40 | style 3 |
| 0x0E | 0x41 | style 1, and `x += 5` first when the style was 3 |

The style byte (`textObject+26`) starts from the window attribute in
`FFX_MesWin_ApplyAttrToTextObject 0x8B8BB0`: 0 normally, 3 for attribute 0x2000 with 0x8000, 1 for
0x2000 alone, and 1 or 3 again for window type 3 by the sign bit. **V**

### 2.3 What the shipped dialogue looks like

From `new_uspc/event/obj_ps3/*/*.bin`, the per event message table `FFX_LoadLocalizedBin 0x88CAC0`
loads (u16 table bytes, then 8 byte records, the first u16 the string offset). **V**

- A speaker line is the name, 0x03, then the text: `Wakka{03}What is this?` (scene6 message 1).
- Narration is wrapped in 0x0E 0x40 and 0x0E 0x41: `{03}{0E:40}Listen to my story.{0E:41}`
  (znkd0600 message 0). So the opening line is drawn slanted and outlined.
- Coloured headings use 0x0A 0x43 and 0x0A 0x41, inserted names use the class 3 codes
  (`{19:71}`, `{20:33}`), and button prompts use 0x0B.

The Latin glyph order for indices 0 to 93 is ASCII with the digits first and `@` left out
(`0123456789 !"#$%&'()*+,-./:;<=>?A..Z[\]^_\`a..z{|}~`), the kit's FFX_Text_DrawString notes, and
every shipped line sampled round trips through it. Indices 94 to 229 have widths but no mapped
character yet. **V** for 0..93, the rest open.

## 3. The .ftc file and the FTCX block

`sub_8AC130` (`FFX_Font_InstallFtc`) installs one block into a 16 byte slot per sheet at
`dword_1841DA0 + 16 * sheet` (`+0` pixels, `+4` width table, `+8` pixel bytes, `+12` u16 sheet
width, `+14` u16 sheet height). It returns 1, not installed, when the version is below 0xC8. The
width table is copied into a fixed buffer per sheet (sheet 0 `unk_1864E10`, 1 `unk_18656C0`,
2 `unk_1865840`, 3 `unk_18659C0`, 4 `unk_18655F0`, 5 `unk_1865B60`). Sheets 0 and 4 also copy the
pixels into fixed buffers. **V**

| offset | type | Latin base.ftc | meaning |
|---|---|---|---|
| +0x00 | char[4] | "FTCX" | magic (not checked by the installer) |
| +0x04 | u16 | 0xC8 | version, must be >= 0xC8 |
| +0x06 | u8, u8 | 0x14, 0x06 | 0x14 is the GS PSMT4 format id. **I** |
| +0x08 | u16 | 4 | sheet slot 0..5 |
| +0x10 | u32 | 0xE6 | width table bytes, one per glyph |
| +0x14 | u16 | 14 | cell width |
| +0x16 | u16 | 18 | cell height |
| +0x20 | u32 | 0x40 | pixel offset |
| +0x24 | u32 | 0 | pixel bytes. 0xFC00 in the Japanese file, 0 in the Latin one |
| +0x28 | u16 | 128 | sheet width, the U divisor |
| +0x2A | u16 | 234 | sheet height, the V divisor. 13 rows of 18 |
| +0x30 | u32 | 0x40 | width table offset |
| +0x34 | u32 | 0xEA | equals the sheet height in both files read. **I** |

The Latin table: 230 signed bytes, quarter texels, sum 6303, MD5 `872852ad69f9dd2779204385c3b13bb0`.
'0' 36 (9.0), space 24 (6.0), 'A' 42 (10.5), 'a' 31 (7.75). **V**, `tools/ftc.py header` and
`TextLatinFtcHeaderAndWidths`.

Which file a language loads (`TOInit 0x8AD4D0`): languages 0, 9 and 10 go through
`FFX_LoadLocalizedFtc 0x88C860` ("base", and "newkit" when present), every other language through
the asset loader, kind 4 entry 0, which `cdrom.fnd` names `host0:/ffx/master/uspc/menu/base.ftc`.
That is the only Latin base.ftc in the archive. Events also install an `.ftc` through
`FFX_Ev_LoadEventPackage 0x872EF0`, but no `new_uspc` event or battle `.ftc` ships, so a Latin
game never replaces sheet 4. **V**

## 4. From a glyph id to a page texture and a UV cell

`FFX_Text_DecodeGlyph 0x8B7240` fills a 0x24 byte record, the input to every glyph draw. **V**

| offset | value |
|---|---|
| +0 | font id: 0x3C00 for language 0, 0x3D40 for languages 1 to 8, 0x3D30 for 9, 0x3D10 for 10 |
| +4 | page = glyph & 1 |
| +8, +12 | u0 = 14 * (glyph % 18 / 2), v0 = 18 * (glyph / 18) |
| +16, +20 | u1 = u0 + width, v1 = v0 + 18 |
| +24 | baseline: 1 for languages 1 to 8, 0 for 0 and 9, -1 for 10 |
| +28 | width = (signed char)widths[sheet][glyph] * 0.25, sheet from `sub_8AC760` (4 for Latin) |
| +32 | 18 |

A run of n > 1 dashes (0x47) is one glyph of width `n * width`, doubled on sheet 4, with u0 + 4 and
u1 - 4 so the cell's inner part stretches. That is the horizontal rule. The space next to two
page 1 glyphs is narrowed to 5 for Japanese, Korean and Chinese only
(`FFX_Text_SpaceIsBetweenWideGlyphs 0x8B5760`).

The page texture comes from `FFX_Font_ResolvePageTexture 0x8ACF50 (fontId, colourByte, page,
shadowPass, outColours)`. The colour nibble is `colourByte & 0xF` for the glyph and `>> 4` for a
shadow pass. A nibble of 8 or more, or a shadow pass, takes the shadow table. Font 0x3D40 always
takes `menu_us/base_ftc/D3D11/font_0_{page}` (0xC595E8) or `shadow_0_{page}` (0xC59628), whatever
`FFX_MesWin_GetTextLanguage` says. Font 0x3C00 switches on it. The out colours are two RGBA words
from 0xB5F890 (glyph) or 0xB5F8D0 (shadow) at index `2 * (slot / 2)`, the first for the two bottom
vertices and the second for the top two, a vertical gradient. Glyph colour 0 is grey 0x9D9D9D at
the bottom to white at the top. **V**

The UV divisor is the FTCX sheet size from `sub_8AC400`, 128x234 for font 0x3D40. The HD pages
are 512x1024 DXT5, so U scales by 4 and V by 4.376. Decoding the alpha of
`menu_us/base_ftc/d3d11/font_0_0.dds.phyre` gives glyph column bands starting every 56 texels
(0, 56, ..., 448) and the content of sheet row 0 at image rows 945 to 1023, upside down (glyph 2,
'2', has its flat foot at the top of the band). So the page is stored bottom up and the original's
`1 - v` in `sub_640660` samples it the right way up. **V**

## 5. Units, FFX_UiScaleX/Y and the 4:3 to 16:9 handling

The 2D code lays out in the PS2 frame, 512x416. `sub_640DB0` returns 1920x1080 as constants, so
the factors are fixed, not tied to the window: `flt_18716A8 = 1920 / 512 = 3.75` and
`flt_18716AC = (float)1080 / 416 = 2.596153736`, the same value as the double `dbl_B41D10`. **V**

- `FFX_UiScaleX 0x6447E0` = `x * 512 / 1920` and `FFX_UiScaleY 0x644820` = `y * 416 / 1080` go the
  other way, HD to PS2 units, for code that has an HD position (198 and 209 callers, mostly the
  battle HUD and menus).
- `FFX_Sprite2D_Draw 0x63EEE0` with its third argument set maps a PS2 unit sprite as
  `x * 3.75 - 960`, `y * 2.596 - 540`, no rounding, so sprites stretch to the full 16:9 width.
- Glyphs do not stretch. The pen goes across at 3.75 but the width at 2.596, so a glyph keeps square
  HD pixels while the lines spread over the wider frame. There is no pillarbox for the 2D layer,
  512 units always span 1920 pixels.

## 6. Advance, line height and the quad

### 6.1 The pen advance, `sub_8E8ED0` (`FFX_Text_AdvancePen`)

Read from the disassembly, with the float stores where the x87 code has them: **V**

```
snapped = (float)(int)((float)(3.75 * x) + 0.5) / 3.75          // the pen on a whole HD pixel
advance = (float)(int)(width * scale * 2.596153736 + 0.5) / 3.75 // sub_8FA6B0
spacing = sub_8FA570(style)
x       = (float)(spacing + advance + snapped)
```

`sub_8FA570` (`FFX_Text_GlyphSpacing`) is `round(3.75 * 0.7) / 3.75 = 0.8`, three HD pixels, for
languages 1 to 8 in every style. For 0, 9 and 10 it is 0.8 for styles 1 and 3 and
`round(3.75 * 0.5) / 3.75 = 0.5333`, two pixels, otherwise. Hex-Rays drops the return of
`sub_8FA6B0` here and in `sub_8B9330`, the disassembly adds it (`fstp qword [ebp-10h]` then
`fadd qword [ebp-10h]`).

So in HD pixels a glyph advances `round(width * scale * 2.596) + 3` from a snapped pen.

### 6.2 The quad, `sub_640660` (`FFX_Glyph2D_Draw`)

The callers build a PS2 unit rectangle: `x0 = pen.x`, `y0 = pen.y + baseline * scale`,
`x1 = pen.x + width * scale`, `y1 = pen.y + (18 + baseline) * scale` (`flt_C5D778` and
`flt_C5D77C` are 1.0 multipliers). `sub_640660(rect, texture, offX, offY, batchSlot)` then: **V**

```
X0 = (int)((float)(x0 * 3.75 + offX) + 0.5)
X1 = (int)((float)((float)(x1 - x0) * 2.596153736 + X0) + 0.5)
Y0 = (int)((float)(y0 * 2.596153736 + offY) + 0.5)
Y1 = (int)((float)(y1 * 2.596153736 + offY) + 0.5)
```

The vertices are written about the centre, `X - 960`, `Y - 540`, z 1, vertex 0 and 1 at Y1, 2 and 3
at Y0, UVs `(u0, 1 - v1) (u1, 1 - v1) (u1, 1 - v0) (u0, 1 - v0)`, indices `0 1 3 3 1 2`. When the
rectangle's draw kind (+128) is 2001 the two top vertices move 5 pixels right, the slant. Vertex
colours are the pen RGBA over 255 (alpha times 2, PS2 0x80 is 1.0) times the gradient from
section 4. It returns at once on a catch-up step (`g_ffxIsCatchUpStep`).

### 6.3 Line height

A line end puts x back to the line start and adds 16 units to y. Each edge rounds on its own, so a
line is 41 or 42 HD pixels. The measure, `FFX_MesWin_MeasureTokenRun 0x8B9650`, counts 16 per 0x03
and reports the height plus 2. **V**

### 6.4 The measure

`FFX_MesWin_MeasureTokenRun` sums, per glyph, `sub_8B9330` = the same `sub_8FA6B0` width plus
`sub_8FA570`, with no pen snap, so it can differ from the drawn width by the snap. When
`dword_18663EC` is set (frame style 1, no menu open, `dword_133C948` clear and not the Blitzball
text mode) each glyph is also multiplied by `(1920 / 1080) / 1.2307693 = 1.444`. `0x0E 0x41` after
style 3 adds 5, matching the draw. `FFX_Text_MeasureString 0x905290` is the menu twin and
subtracts one trailing spacing. **V**

## 7. Worked example: the opening line

znkd0600 message 0, `{03}{0E:40}Listen to my story.{0E:41}`, pen start (0, 0), font scale 1.0
(znkd0600 is not a Blitzball event). The 0x03 moves the pen to (0, 16), 0x0E 0x40 picks style 3,
scale 1.0, 10 copy outline, slanted. `y0 = 16 + 1 = 17` gives Y0 = round(44.13) = 44,
`y1 = 16 + 19 = 35` gives Y1 = round(90.87) = 91. **V** by `tools/ftc.py layout` and
`TextLayoutDialogueLineMatchesOriginalArithmetic`.

| glyph | index | page | width | round(w * 2.596) | X0 | X1 |
|---|---|---|---|---|---|---|
| L | 43 | 1 | 7.00 | 18 | 0 | 18 |
| i | 72 | 0 | 2.25 | 6 | 21 | 27 |
| s | 82 | 0 | 7.00 | 18 | 30 | 48 |
| t | 83 | 1 | 6.25 | 16 | 51 | 67 |
| e | 68 | 0 | 7.75 | 20 | 70 | 90 |
| n | 77 | 1 | 7.25 | 19 | 93 | 112 |
| space | 10 | 0 | 6.00 | 16 | 115 | 131 |
| ... | | | | | | |
| . | 24 | 0 | 2.50 | 6 | 375 | 381 |

The pen after 'L': `snapped 0`, `advance 18 / 3.75 = 4.8`, `x = 0.8 + 4.8 = 5.6000004` after the
float store, so 'i' starts at round(21.0) = 21. The closing `0x0E 0x41` adds 5 to the pen, which
ends at 107.4 (403 HD pixels), and leaves style 1.

The same arithmetic for the small style, scene6 message 1 (`Wakka{03}What is this?`), style 0,
scale 0.78: the name's glyphs land at 0, 30, 49, 67, 85, rows 2 to 38, the second line at 0, 30, 48,
67 ... 197, rows 44 to 80. `TextLayoutSmallStyleSpeakerLine`.

## 8. The message window path to the 2D list

**V** unless marked.

1. `FFX_MainStep` calls `FFX_MesWin_DrawAllWindows 0x8ABD30` (the field pass, skipped on a catch-up
   step, see DIALOGUE.md section 7), which calls `FFX_MesWin_DrawWindow 0x8B5DC0` per window.
2. `FFX_MesWin_DrawWindow` runs the draw slot `textObject+300`. `sub_8DF6D0`
   (`FFX_MesWin_InstallTypeHandlers`) fills it from the 9 slot table at 0xC5B1B0 indexed by the
   window type (`attribute & 0xF`): `sub_8DEA40` for types 0 and 1, `sub_8DEE00` for 2 and 3.
   `sub_8DFEA0` replaces both when `dword_18659BC` is 1, and `TOInit` sets it to 0.
3. `sub_8DEA40` (`FFX_MesWin_DrawText`) copies the 28 byte pen from textObject+184 and the start
   from +72 (x), +76 (y), +80 (right edge), +88 (RGBA), walks the text up to the typewriter count
   (+156 against +160), and per character: a line end resets x to +72 and adds 16, class 2 goes to
   `FFX_MesWin_ApplyControlCode`, class 3 to `sub_8B5960` (`FFX_MesWin_DrawInsertedValue`, which
   looks the value up with `sub_8B6660` and draws it the same way), text to
   `FFX_Text_DecodeGlyph` then `sub_8FC7C0`. When `dword_25D0B20[window]` is set it also wraps at
   `right - 5`.
4. `sub_8FC7C0` (`FFX_MesWin_DrawGlyph`) by style:
   - 0 and 2: `sub_8FCCA0` drop shadow at (+2, +2) HD pixels from the shadow page in layer 0, then
     `sub_8FCAD0` the glyph in layer 1, both at scale 0.78, then the advance at 0.78.
   - 1: `sub_8FC870` the outline, then `sub_8FCAD0`, at `g_ffxMesWinFontScale`.
   - 3: `sub_8FCE90` the outline, then `sub_8FD0A0`, both draw kind 2001 (slanted).
   The outline is ten copies from the shadow page at (-2,-2) (0,-2) (2,-2) (-1,0) (-2,0) (1,0)
   (2,0) (-2,2) (0,2) (2,2), each divided by `sub_640D70` = min(1, backBufferWidth / 1920).
   `g_ffxMesWinFontScale` is 1.0, or 0.9 in an event whose name holds "bltz".
5. `sub_8E5A20` clips the rectangle to the scissor at `flt_18716F8..flt_1871704` when
   `dword_18716D0` is 1, then `sub_640660` appends it.
6. `sub_640660` asks `FFX_DynDrawList_Append(dword_CCC81C, texturePath, 0)` for a record, sets
   record+188 = 1 (the glyph flag VISUAL_CENSUS lists), and writes 4 vertices and 6 indices into the
   geometry at record+148: +4 vertex count, +8 index count, +12 positions, +16 normals (0, 0, 1),
   +20 colours, +24 UVs, +28 u16 indices, +32 the texture path. The list keeps adding to the
   current record while the texture path matches. In the interleaved mode `sub_643AF0(1)` turns on
   (+23436) up to 6 textures stay open at once with at most 12 quads each (`dword_CCC7EC`), and
   `sub_640DD0` (+23440) is the layer, which is how the shadow pass lands under the glyphs.

The PS2 path is still in the binary and dead on PC: `sub_8FD960`, reached only from `sub_8DFEA0`
and `sub_8B5EA0`, builds GS packets with `sub_8FD9F0` and `sub_8FDC00` for styles other than 3.

## 9. The menu path, sgMenu and rcUI

`FFX_Text_DrawString 0x905AB0 (s, x, y, colour, scale)` is the menu's text entry, 37 callers
(config rows, the overdrive list and the other menu modules). Its pen is RGBA 0x80808080, style 0,
the colour byte from the fourth argument. Per glyph it draws the shadow (`sub_8FBAE0`, +2 +2 in
layer 0) and the glyph (`sub_8FB940`, layer 1, batch slot page + 2) at the caller's scale and
advances with `sub_8E8ED0`. A line end puts x back and adds 16. 0x0B draws the icon at 0.6 or 0.73
and advances by its width. When `dword_18716BC` is set it draws the glyph only. **V**

sgMenu is not a draw path. `sgMenuInit 0xA78210`, `sgMenuExec 0xA78270` and `sgMenuResult 0xA782A0`
are ATEL syscall handlers (no code callers, reached through the syscall table) that open a menu
module through `FFX_MenuSys_RequestOpen` and wait on it. The modules then draw with
`FFX_Text_DrawString` and with `FFX_Sprite2D_Draw` and the icon draw `sub_8F8EE0`. **V** for the
handlers, the module draw sites are the 37 callers above.

rcUI is `rcuiPre.c`, the SG debug GUI's "PRE2D" window: `rcuiPre__f930D80` creates a window with
`sub_937EF0` and the proc `sub_935000` from `SG_DebugGui_RootWindowProc`. It belongs with the SG GUI
windows (PROGRESS 6.4.3). Its text is not traced to the 2D list here. **I**

Both 2D producers append to the same list: `FFX_DynDrawList_Append` has exactly two callers,
`FFX_Sprite2D_Draw` and `sub_640660`. So menus, sgMenu modules and message windows all meet in
`dword_CCC81C`. **V**

## 10. Corrections to other docs

- SCENE_OBJECTS.md section 8 says the menu font is font 0x3C00. That is the Japanese font id. For
  languages 1 to 8 `FFX_Text_DecodeGlyph` stamps 0x3D40, which always resolves to the `menu_us`
  pages. 0x3C00 reaches the same pages only when `FFX_MesWin_GetTextLanguage` is non zero.
- SCENE_OBJECTS.md says the widths are "about 0xE6 glyphs, not read yet". Read: 230, quarter
  texels, section 3.
- DIALOGUE.md section 7 says `g_ffxMesWinFontMode` picks the glyph advance 0.73 vs 0.65 in
  `FFX_MesWin_MeasureTokenRun`. It does not read the font mode. 0.73 and 0.65 are the 0x0B button
  icon scales, picked by the style argument (1 or 3 against the rest). The glyph advance there is
  `sub_8B9330` at 0.78 or the font scale.
- EBP_FORMAT.md's open item on "the text codec above 0x89" and "the glyph order table in the FTCX
  block": the codec is the glyph table of section 2.3 (0x8A..0x8D are `{ | } ~`), and the FTCX block
  holds widths only, no order table.
- projectzanarkand `fs::LocalizedFtcPath("base", English)` builds `new_uspc/menu/base.ftc`, which
  does not ship. English loads `uspc/menu/base.ftc` through the asset loader. `text::MenuFtcPath`
  has the right one.

## 11. Names (applied to the IDB 2026-10-13 after spot checks)

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x8AC130 | sub_8AC130 | FFX_Font_InstallFtc | version >= 0xC8 check, per sheet slot fill from +8, width memcpy |
| 0x8AC3D0 | sub_8AC3D0 | FFX_Font_GetWidthTable | `dword_1841DA4[4 * sheet]` |
| 0x8AC400 | sub_8AC400 | FFX_Font_GetSheetSize | font id switch to the slot's +12/+14 or constants |
| 0x8AC760 | sub_8AC760 | FFX_Font_GetTextSheet | 4 unless language 0, 9, 10 without the international env |
| 0x8B1020 | sub_8B1020 | FFX_Font_UploadFieldSheets | GS uploads of the sheet pixels for the field |
| 0x8B1190 | sub_8B1190 | FFX_Font_UploadMenuSheets | the same for the menu |
| 0x640660 | sub_640660 | FFX_Glyph2D_Draw | (VISUAL_CENSUS's name) the rounding and vertex write of section 6.2 |
| 0x640DB0 | sub_640DB0 | FFX_Ui_GetVirtualScreenSize | returns 1920, 1080 |
| 0x640D70 | sub_640D70 | FFX_Ui_GetOutlineScale | min(1, gfx+69072 / 1920) |
| 0x640DD0 | sub_640DD0 | FFX_DynDrawList_SetLayer | writes list+23440 |
| 0x643AF0 | sub_643AF0 | FFX_DynDrawList_SetInterleaved | writes list+23436 |
| 0x8E8ED0 | sub_8E8ED0 | FFX_Text_AdvancePen | snap, width, spacing |
| 0x8FA6B0 | sub_8FA6B0 | FFX_Text_ScaledGlyphWidth | round(w * s * 2.596) / 3.75 |
| 0x8FA570 | sub_8FA570 | FFX_Text_GlyphSpacing | 0.8 or 0.5333 by language and style |
| 0x8FA740 | sub_8FA740 | FFX_Text_GlyphAdvanceNoSnap | width plus spacing, the menu measure |
| 0x8FA800 | sub_8FA800 | FFX_Text_ButtonIconWidth | icon id switch into sub_9062B0 |
| 0x8E5A20 | sub_8E5A20 | FFX_Text_ClipQuadToScissor | the flt_18716F8..1871704 clip with UV lerp |
| 0x8FB940 | sub_8FB940 | FFX_Text_DrawGlyphQuad | the menu glyph, layer 1 |
| 0x8FBAE0 | sub_8FBAE0 | FFX_Text_DrawGlyphShadow | the menu shadow, +2 +2 |
| 0x8FC7C0 | sub_8FC7C0 | FFX_MesWin_DrawGlyph | the per style switch of section 8 |
| 0x8FC870 | sub_8FC870 | FFX_MesWin_DrawGlyphOutline | ten offsets, style 1 |
| 0x8FCAD0 | sub_8FCAD0 | FFX_MesWin_DrawGlyphQuad | the window glyph |
| 0x8FCCA0 | sub_8FCCA0 | FFX_MesWin_DrawGlyphShadow | +2 +2, styles 0 and 2 |
| 0x8FCE90 | sub_8FCE90 | FFX_MesWin_DrawGlyphOutlineSlanted | ten offsets, kind 2001 |
| 0x8FD0A0 | sub_8FD0A0 | FFX_MesWin_DrawGlyphQuadSlanted | kind 2001 |
| 0x8FD960 | sub_8FD960 | FFX_MesWin_DrawGlyphGs | the dead PS2 packet path |
| 0x8DEA40 | sub_8DEA40 | FFX_MesWin_DrawText | the type 0 and 1 draw slot |
| 0x8DEE00 | sub_8DEE00 | FFX_MesWin_DrawTextAlt | the type 2 and 3 draw slot |
| 0x8DFEA0 | sub_8DFEA0 | FFX_MesWin_DrawTextGs | the slot when dword_18659BC is 1 |
| 0x8DF6D0 | sub_8DF6D0 | FFX_MesWin_InstallTypeHandlers | the 0xC5B1B0 table copy |
| 0x8DF8F0 | sub_8DF8F0 | FFX_MesWin_ApplyControlCode | 0x07, 0x09, 0x0A, 0x0B, 0x0E |
| 0x8DF520 | sub_8DF520 | FFX_MesWin_CentreLine | (right - left - width) / 2 + left |
| 0x8B5960 | sub_8B5960 | FFX_MesWin_DrawInsertedValue | class 3 expansion, recursive |
| 0x8B9330 | sub_8B9330 | FFX_MesWin_MeasureGlyph | the measure's per glyph advance |
| 0x8B7BE0 | sub_8B7BE0 | FFX_MesWin_MeasureGlyphOnTextSheet | sub_8B9330 with sub_8AC760 |

## 12. Still open

- Glyphs 94 to 229 of the Latin sheet: widths known, characters not mapped.
- Button icons (0x0B): the width `sub_9062B0` returns and the icon sheet, so a line with a prompt
  is not laid out past the icon.
- The class 3 inserted values (`sub_8B6660`) and the 0x10 choice inserts.
- `sub_8DEE00`, the type 2 and 3 draw. It ends in the same `FFX_Text_DecodeGlyph` and
  `sub_8FC7C0` and also converts positions with `FFX_UiScaleX/Y`, but its pen rules were not read.
- The window geometry that sets textObject+72/+76/+80 and the starting style of an ordinary
  dialogue window (it comes from the script's window attribute).
- The kanji sheets (0x26..0x2B, 0x06, and 0x04 for Korean and Chinese).
- rcUI's draw path, with 6.4.3.
- A pixel comparison against the running game.
