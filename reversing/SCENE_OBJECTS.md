# Scene objects, sprites and fonts (PNode, PWorldMatrix, cameras, PMeshInstance, PSpriteAnimationInfo, PBitmapFont)

Written 2026-10-13 for Project Zanarkand items 5.2 and 5.6. The readers are
`src/engine/phyre/Scene.h/.cpp` and `src/engine/phyre/Sprite.h/.cpp` in projectzanarkand, checked
over the whole archive by `phyrecheck scene` and `phyrecheck sprites`. Every claim says whether it
was read in IDA (verified, static), off the files (verified, data), or is inferred. The PhyreEngine
3.21 SDK headers were read to confirm member meanings only. The game ships 3.9, the binary wins.

## 1. What ships and what does not

Counted by `phyrecheck scene` and `phyrecheck sprites` over FFX_Data.vbf (47,095 clusters parse,
the 30 big endian ones are skipped as everywhere else). Verified, data.

| class | objects | clusters |
|---|---|---|
| PNode | 101,522 | 1,271 (every one a `.dae.phyre`) |
| PWorldMatrix | 15,605 | |
| PCameraOrthographic | 1,415 | 448 |
| PCameraPerspective | 875 | 485 |
| PMeshInstance | 11,286 | |
| PSpriteAnimationInfo | 1,589 | 196 (`textureanimation.ags.phyre`) |
| PSpriteAnimationInfoChar | 1,727 | 426 (`textureanimationchar.ags.phyre`) |
| PBitmapFont | 1 | 1 (`fonts/d3d11/tuffy.fgen.phyre`) |
| PBitmapFontCharInfo | 95 | 1 |
| PWorld, PLight, PSpriteCollection | 0 | 0 |

There is nothing to read for PWorld, PLight or PSpriteCollection. Map lighting is not a Phyre
object in this game.

## 2. Raw offset fixups are shifted by the class layout word

A fixup whose dword 0 has bit 31 set names a byte offset, not a member. That offset is NOT the slot.
Verified, static:

- `PClusterLoad__applyArrayFixups 0x57A320` hands the raw value to `PFixupMemberCache__find 0x57A280`.
- `PPackedClass__findMemberAtRawOffset 0x47D3E0` (proposed name) walks the class chain. For each
  class with members it returns null when `raw + layout[2] >= size` would need the nested class
  visitor, otherwise `PPackedClass__findMemberCovering 0x47E4E0` (proposed) takes the member
  whose `[offset, offset + max(count,1) * size)` covers `raw + layout[1]`. `layout[1]` is packed
  class dword 5, the runtime descriptor +20.

PNode's layout is `[4, 4, 4]`. All 100,251 PNode name fixups say raw 76, which is m_name at 80. The
cameras say raw 204: PCameraPerspective and PCameraProjection have layout word 1 of 4 but own no
member there, PCamera has layout 0 and m_name is at 204. Verified, data, over the archive.

`Cluster::SlotOf` and `tools/phyre.py slot_of` both return the raw value unchanged. That is right
for every class whose layout is zero (all mesh, material and texture classes) and wrong for PNode
names. `RuntimeSlot` in Scene.cpp is the engine rule. Pointer fixups in PNode use member indices,
so only names were affected.

## 3. PNode and PWorldMatrix

PNode, 84 bytes (verified, data and SDK header): m_next +0, m_parent +4, m_firstChild +8,
m_worldMatrix +12 (PWorldMatrix*), m_localMatrix +16 (PMatrix4, column major), m_name +80.

PWorldMatrix is 48 bytes, one PMatrix4x3 m_matrix. Its storage is not row or column major.
`PMatrix4x3__toMatrix4 0x451080` (proposed, verified static) reads it as three columns of four
floats, column 1, column 2 and column 3 (the translation), with the three elements of column 0 in
the fourth float of each:

    stored = [c1.x c1.y c1.z c0.x | c2.x c2.y c2.z c0.y | c3.x c3.y c3.z c0.z]

`PMatrix4x3__fromMatrix4 0x451190` (proposed) is the inverse.

### World composition

`PNode__composeWorldMatrix 0x506660` (proposed, verified static): copy the node's local matrix
verbatim, then for each ancestor from the parent up, `out = ancestor.local x out` with
`Matrix4__affineMultiply 0x4C9DA0` (proposed). That multiply sends columns 0 to 2 of the right
operand through `Matrix4__transformVector3 0x4CA140` and column 3 through
`Matrix4__transformPoint3 0x4C9EB0`, each element `((x*a_r + y*a_4+r) + z*a_8+r) [+ a_12+r]` in an
x87 register stored to float once, and `Matrix4__fromColumns 0x450DE0` writes the bottom row
0, 0, 0, 1. The input bottom rows are never read.

The loader does not call it. It is reached from `PNode__setMatricesForWorldMatrix`,
`PAttachableComponent__update`, the physics initial transform and the debug hierarchy draw
`0x6BE3F0`. So what a node's PWorldMatrix holds after load is the stored value. Over the archive
14,625 of the 14,625 nodes with a world store exactly, bit for bit, what the composition gives
(double sums rounded to float, which is x87 at the MSVC default 53 bit precision control, inferred).
86,897 nodes have no world.

Every cluster with nodes has exactly one root, child lists agree with the parent pointers, no chain
loops, nothing is non finite. Verified, data.

## 4. Cameras and mesh instances

PCamera (332): m_localToWorldMatrix +176 (PWorldMatrix*), m_nearPlane +180, m_farPlane +184,
m_name +204, fog, glow, depth of field and character light members after it. PCameraProjection
adds m_aspect +336, PCameraPerspective m_FOV +340, PCameraOrthographic m_height +340. All 2,290
cameras share their world with a node (in bsil03 the node of the same name). Verified, data.

Correction 2026-10-13: the PCamera offsets above are the member table's, which hold for a PCamera
object. Every camera is a PCameraProjection, which adds a vptr in front of the PCamera subobject, so
in a camera object m_localToWorldMatrix is at +180, m_nearPlane +184, m_farPlane +188, m_name +208
and the fog members from +212. bsil03's GlobalCamera reads near 10 and far 4000, the Maya cameras
0.1 and 10000. See the vptr shift note in PHYRE_FORMAT.md.

PMeshInstance (132): m_mesh +0, m_localToWorldMatrix +4, m_materialSet +16, m_bounds +28
(PMeshInstanceBounds*), m_name +44, then the FFX members m_animCt, m_mimeCt +68, m_groupID, ...
All 11,286 have bounds, and every PMeshInstanceBounds points back at its instance through
m_meshInstance +28. 10,306 share a node's world. No world is shared by two mesh instances.
Verified, data. The "node" pointer at mesh instance +0x1C that DISPLAY_LIST.md mentions is this
m_bounds PMeshInstanceBounds, not a PNode.

## 5. What a map load does to the model cluster

`FFX_Gfx_AdoptMapCluster 0x65AF60` (proposed, verified static) runs for the first model row of a
map asset header (`gfx+216 == 0`). For every PMeshInstance of the cluster, in instance list order:

- field map: `world = (world x diag(-2.5, -2.5, 1, 1)) x translate(-256, -224, 0)`, two products,
  each rounded to float.
- battle map: `world = world x diag(10, 10, 10, 1)`.

The result goes back through `PMatrix4x3__fromMatrix4` into the shared PWorldMatrix object (so the
node sharing it sees it too), and `bounds->m_worldMatrix` (+12) is pointed at it. A battle load then
walks every PCamera of the cluster and multiplies floats 8, 9, 10 of its m_localToWorldMatrix (the
translation) by 10. `ApplyMapLoadTransform` reproduces both. bsil03 mesh instance 46 ends at
`[-2.5 0 0 0 | 0 -2.5 0 0 | 0 0 1 0 | 650.789062 562.490601 -21.1114998 1]`.

## 6. Where a live read finds it

Not done, the kit was not available. The path, verified static:

- `g_ffxGfxCtx` at VA 0xCCB9D8 (RVA 0x8CB9D8) holds the graphics context pointer.
- Field map: `*(gfx+0x1128)` is the model PCluster* (bsil03.dae.phyre on that map). The PArray of
  every PCluster* of the map asset header is count `gfx+0x112C`, capacity `+0x1130`, data `+0x1134`.
  The header object sits at `gfx+0x1144`. Battle map: model `gfx+0x10D4`, array from `+0x10D8`,
  header `+0x10E4`.
- PCluster +28 is the instance list sentinel. Each list node: +0 next, +44 PClassDescriptor*,
  +52 object base, +60 count. Object i is `base + i * desc[+28] + desc[+140]`. Compare the
  descriptor with PNode 0xCA91F8, PWorldMatrix 0xC92090, PMeshInstance 0xCA7228,
  PCameraPerspective 0xC93608 (the iterator `PInstanceListIter__load 0x441DC0` does the same).
- A PNode in memory: next +0, parent +4, firstChild +8, world +12, local +16, PString buffer +80
  (low bit masked). PWorldMatrix is the 48 byte PMatrix4x3 above.
- `gfx+0x4F8` caches the GlobalCamera PCameraPerspective.

A live check would print the PNode tree of the model cluster through that path and compare it with
`PrintNodeTree` after `ApplyMapLoadTransform(Field)`.

## 7. Sprites

PSpriteAnimationInfo (20): m_name +0, m_textureAtlasInfo +4 (null in all 3,316), m_timeInterval +8,
m_subTextureIDs +12 (PArray<PUInt32>, the frame sequence). PSpriteAnimationInfoChar (40) adds
m_groupName +20 and mUStart, mVStart, mUEnd, mVEnd at +24 to +36. 22,896 frames in all, every
interval positive, every UV inside 0 to 1. Verified, data. Which texture the ids index, with the
atlas pointer null, is still open (inferred: FFX's own texture animation code, not Phyre's).

## 8. Fonts

The one PBitmapFont is the debug text font. `FFX_DebugText_LoadFont 0x6F6370` (proposed) enqueues
`/FFX_Data/GameData/PS3Data/Shaders/GCM/PhyreText.cgfx.phyre` and
`/FFX_Data/GameData/PS3Data/fonts/GCM/tuffy.fgen.phyre` (table 0xC38E10), the archive only holds
the d3d11 copy. tuffy: size 88, line spacing 172, baseline 86, 95 glyphs 32 to 126, no kerning,
an L8 512x512 page. m_rotated marks a glyph stored rotated (and mirrored, says the SDK header), with
width and height swapped in the page: 13 glyphs would leave the page otherwise, none do with the
swap. Verified, data.

The menu font is FFX's own font 0x3C00, not a Phyre font. `FFX_Font_ResolvePageTexture 0x8ACF50`
(proposed) picks a page texture by font id, colour nibble and page:

- `FFX_MesWin_GetTextLanguage 0x887C30` non zero: `menu_us/base_ftc/D3D11/font_0_{0,1}.dds.phyre`
  (table 0xC595E8), shadow pages `shadow_0_{0,1}` (0xC59628). Otherwise the Japanese `menu/base_ftc`
  tables 0xC59568 and 0xC595A8. Each table has 16 slots, page 0 and 1 repeated for 8 colour
  nibbles, the shadow table taken for a nibble of 8 or more or a shadow pass.
- `FFX_Font_LoadPageTextures 0x67C920` (proposed) releases the pages by language (0, 9, 10, other).

menu_us pages are DXT5 512x1024 one level, the Japanese ones DXT5 512x4096. All 3,848 font page
clusters in the archive (menus, help, event and battle text) decode: DXT5 3,628, DXT3 216, ARGB8 4.
The glyph widths live in the FTCX block of `ffx_ps2/ffx/master/uspc/menu/base.ftc` (304 bytes,
"FTCX", 0xE6 glyphs). That format is not read yet.

Correction 2026-10-13 (TEXT_RENDERING.md): 0x3C00 is the Japanese font id. For languages 1 to 8
`FFX_Text_DecodeGlyph` stamps 0x3D40, which always resolves to the `menu_us` pages. The FTCX widths
are now read: 230 entries in signed quarter texels, see TEXT_RENDERING.md section 3.

## 9. Names proposed

Not applied (the 5.2 and 5.6 work kept the IDB read only).

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x506660 | sub_506660 | PNode__composeWorldMatrix | copies +16, loops parent +4, affine multiply per ancestor |
| 0x451080 | sub_451080 | PMatrix4x3__toMatrix4 | the 12 float shuffle above |
| 0x451190 | sub_451190 | PMatrix4x3__fromMatrix4 | inverse, used by 0x65AF60 before the write back |
| 0x4C9DA0 | sub_4C9DA0 | Matrix4__affineMultiply | three vector and one point transform, fromColumns |
| 0x4C9EB0 | sub_4C9EB0 | Matrix4__transformPoint3 | adds column 3 |
| 0x4CA140 | sub_4CA140 | Matrix4__transformVector3 | no column 3 |
| 0x450DE0 | sub_450DE0 | Matrix4__fromColumns | writes the 0,0,0,1 row |
| 0x4C9430 | sub_4C9430 | Matrix4__copy | 16 float copy through x87 |
| 0x47D3E0 | sub_47D3E0 | PPackedClass__findMemberAtRawOffset | layout[2] guard, nested visitor, per class walk |
| 0x47E4E0 | sub_47E4E0 | PPackedClass__findMemberCovering | raw + layout[1] in [offset, offset + n * size) |
| 0x65AF60 | sub_65AF60 | FFX_Gfx_AdoptMapCluster | -2.5 / -256 / -224 and 10 constants, mesh instance and camera loops |
| 0x64CDA0 | sub_64CDA0 | FFX_Gfx_CameraListIter_init | iterator over the PCamera descriptor 0xC922C0 |
| 0x6BE3F0 | sub_6BE3F0 | PNode__debugDrawHierarchy | lines from each node to its children |
| 0x6F6370 | sub_6F6370 | FFX_DebugText_LoadFont | enqueues PhyreText.cgfx and tuffy.fgen |
| 0x6F63F0 | sub_6F63F0 | FFX_DebugText_OnAssetLoaded | its completion callback |
| 0x8ACF50 | sub_8ACF50 | FFX_Font_ResolvePageTexture | font id switch over the page path tables |
| 0x67C920 | sub_67C920 | FFX_Font_LoadPageTextures | per language release of the same tables |
