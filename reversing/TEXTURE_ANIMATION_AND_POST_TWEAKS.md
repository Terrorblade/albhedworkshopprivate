# TextureAnimationManager and PostProcessTweakUtil

PROGRESS 1.5.9. Inputs, outputs and data files of the two Virtuos singletons that MAP.md lists
next to the dynamic meshes.

Marks: [V] read in Hex-Rays or disassembly, or measured in the archive. [I] inferred. VAs at the
preferred base 0x400000. IDA was read only. Proposed names are in sections 9 and 15.

Short version:
- `TextureAnimationManager` swaps textures. Map and rcBg materials get a whole new TextureSampler
  texture per frame. Character materials get up to six layered texture windows. The frame tables
  are `textureanimation.ags.phyre` and `textureanimationchar.ags.phyre` clusters, and the material
  link is `PMaterial.m_texAnimID`.
- Map autoplay runs on the present clock. Scripted map and rcBg frames are step state. Character
  frames are step driven and applied from the per step character worker jobs.
- `PostProcessTweakUtil` is the "PostProcess" debug page (DebugMenuManager mode 1). It loads no
  file, has no clock, and its input handler is orphaned, so a retail game never runs an edit.

# Part 1. TextureAnimationManager

## 1. Data files

| file | what | archive count [V] |
|---|---|---|
| `textureanimation.ags.phyre` | Phyre cluster of `PSpriteAnimationInfo`, map and rcBg | 196 clusters (118 under `/2d/`), 1,589 animations, 11,366 frame ids |
| `textureanimationchar.ags.phyre` | Phyre cluster of `PSpriteAnimationInfoChar`, characters | 427 clusters, 426 with animations (chr/obj/f057 holds only a `PTexturePacker`), 1,727 animations, 11,530 frame ids |
| `<asset>.ahwin32` | asset header. The symbol `textureAnimation` or `textureAnimationChar` is the row of the .ags cluster | 204 headers with `textureAnimation`, 427 with `textureAnimationChar` |
| `textureAnimationChar.ovp` | optional text next to a character asset, exclusive group table | 0 in FFX_Data.vbf and none loose in the install |
| `PMaterial.m_texAnimID` (+16, PString) | the material to animation link | 4,141 of 43,686 materials: 2,953 in rcBg models, 152 in 3D map models, 1,036 `Mat_CTA` character IDs |

The counts are from `tools/texanim.py census --materials` in the Zanarkand repo. They agree with
SCENE_OBJECTS.md section 1 (1,589 and 1,727 objects in 196 and 426 clusters). [V]

`PSpriteAnimationInfo`, 20 bytes: `m_name` PString @0, `m_textureAtlasInfo` @4 (null in every
object), `m_timeInterval` float @8 (seconds per frame), `m_subTextureIDs` PArray<PUInt32> @12 (the
frame sequence). `PSpriteAnimationInfoChar`, 40 bytes, adds `m_groupName` PString @20 and `mUStart`,
`mVStart`, `mUEnd`, `mVEnd` floats @24..@36. [V]

Measured over the archive [V]:
- `m_timeInterval`: 0.5 s on 1,586 map animations and all 1,727 character ones. The exceptions are
  0.1 s twice (genk12 2D `0_3_160_100`, genk05 2D `0_2_102_64`) and 4.0 s once (genk12 2D
  `0_2_70_88`).
- Frames per map animation: 1 frame 1,094 times, then 27 (176), 16 (110), 30 (62), 3 (46), 32 (32),
  maximum 64. Per character animation: 1 (571), 2 (483), 8 (180), 14 (54), maximum 102.
- Map names: 3D `%05d_%02d_%03d_%03d` (all 113, for example `12160_19_000_128`, which reads like a
  PS2 texture base, PSM, x, y [I]). rcBg `scene_layer_w_h` (all 1,476, for example `0_2_106_76`).
  Character: `m_name` `Animation<n>`, `m_groupName` `G<n>`.
- Character groups: 1,711, of which 1,698 hold one animation. Groups per file: 4 is the most common
  (141 files), then 1 (110), 5 (44).
- Every UV window is inside 0..1.

The frame ids index the textures of the same asset: the PTexture2D objects of the clusters in rows
`[0, symbol)` of the asset header, in order. 15 headers have a frame id at or past their texture
row count (m153, m189, m190, m323 at 27 against 20, n291, n336, c307, lchb05 `1.ahwin32`, and the
slik07, slik08, swin03, swin04 `2d/tex` alternates). Those frames resolve to a null texture and
nothing is written for them. [V for the count, V for the null skip]

`.ovp` format, from the parser in `sub_6B05D0` only [V parse, I meaning]: CRLF lines
`<groupId>:<n>!,<n>?,...`. Each comma terminated token ends in `!` or `?`, the two mean the same. A
row's column 0 is its group id, the rest are group ids that this group removes from the character's
layer stack when it is pushed, so they are mutually exclusive groups. A token without `!` or `?`
would loop forever [I]. With no file the table is uninitialised `FFX_Malloc` memory in column 0 and
-1 elsewhere, which is harmless.

## 2. Load path

Loader `sub_6B05D0(mgr, modelCluster, clusterArray, first, animIndex, charName, unused, ovpText)`
[V]:

| path | caller | arguments |
|---|---|---|
| map 3D | `FFX_Gfx_FinishMapLoad 0x65B8A0` | model gfx+4308, clusters gfx+4312, first 0, anim index `PAssetHeader__symbol("textureAnimation")`, no name, no .ovp |
| map rcBg | `FFX_Gfx_FinishMapLoad 0x65B8A0`, second call | model gfx+4392, clusters gfx+4396, the same symbol |
| character | `FFX_Chr_FinishCharacterLoad 0x65A760` | only when the header has `textureAnimationChar`. Name = the asset basename ("c001"), .ovp text from the asset directory + `/textureAnimationChar.ovp` (exists check, read in 0x3FF chunks) or null |

What it does [V]:
- Textures: every PTexture2D (descriptor 0xCA37D0) in clusters `[first, animIndex)`, appended in
  order to mgr+92 for maps, to the character set's own list for characters.
- Animations: cluster `animIndex` is walked for `PSpriteAnimationInfo` (0xCB1EB8). An object whose
  class chain has `PSpriteAnimationInfoChar` (0xCB1F50) becomes a `TextureAnimationInfoChar` in the
  character set, any other a `TextureAnimationInfo` in mgr+124.
- Materials: every PMeshInstance of the model, every material with a non empty `m_texAnimID`. An ID
  containing "Mat_CTA" builds a 0x7C byte character record (section 4). Any other appends the
  `PMaterial*` to mgr+116.

Open, probable retail quirk [I, needs a runtime check]: both map calls take the one symbol lookup
for "textureAnimation" (`PAssetHeader__symbol 0x6F4350` is a name to index map). 13 maps have a 3D
and a 2D bundle whose symbols differ (slik00 30 and 38, kino00 30 and 21, genk10 32 and 21). For
those, one of the two calls likely reads the wrong cluster index.

## 3. How m_texAnimID selects an animation

- Map and rcBg: exact `strcmp` of the whole ID against `m_name`. `TextureAnimationManager__findInfo
  0x6B1D10` returns the first info in mgr+124 with that name. `sub_6B20A0` goes the other way, name
  to the index of the first material in mgr+116 with that ID. Every map bundle ID resolves except 8
  bundle level rows in bltz02 (`0_22_50_5` .. `0_27_50_5`). [V]
- Characters: an info links to a `Mat_CTA` material when `strstr(texAnimID, groupName)` hits and the
  next character is NUL or `G`, so `G1` matches `Mat_CTA_G1G3` and not `Mat_CTA_G10`. The link is
  stored at info+56. IDs are `Mat_CTA_G<n>[G<n>...]`, up to 8 groups, 8 of them with a `pasted__`
  prefix. `setAnimatedMaterialsChar` then picks the info by exact groupName and name, built from
  `"G"` (`off_C322A0`) and `"Animation"` (`off_C322A4`) plus a number (a table at `off_C322B0` below
  20, `sprintf` above). [V]
- No non `Mat_CTA` ID appears in a character model, so character loads never touch mgr+116. 1,014
  of the 1,032 `Mat_CTA` materials inside bundles link to at least one group, and 1,516 of the 1,727
  infos get a material. The 4 IDs in m018 and m317 sit in models without a `textureAnimationChar`
  symbol and are dead. [V, from the census of the texture animation agent, the 1,036 total
  re-measured]

## 4. Layouts

`TextureAnimationManager`, 0x84 bytes, singleton `dword_CCCAA8` (boot row 52, `sub_6B00A0`). Counts
carry a bit 31 "not owned" flag, mask with 0x7FFFFFFF. [V]

| offset | field |
|---|---|
| +0 | vtable 0xB48438 (one slot, scalar deleting dtor 0x6AFE30) |
| +4 | enabled. Set at the end of a load when mgr+124 is not empty, cleared by clearMap. Gates applyFrames |
| +8 | rcBg layer base, written only by `sub_642D90` (from `sub_9217E0`, the rcBg scene select) |
| +12 | material base of the latest load in +116, in practice the 3D map material count |
| +16 | texture base of the latest load in +92 |
| +20 | PSamplerState, 36 bytes: min filter 5, mag 1, wrap 1, 1, 1 |
| +56 | PSamplerState: 5, 1, wrap 2, 2, 2 (the +48 and +84 bit 0x80 marks created) |
| +92 / +96 | map textures, count and `PTexture2D**` |
| +100 / +104 | character sets, count and 32 byte records |
| +108 / +112 | instance registrations, count and 12 byte records |
| +116 / +120 | map and rcBg materials with an ID, count and `PMaterial**` |
| +124 / +128 | map infos, count and 32 byte `TextureAnimationInfo` |

The ctor does not initialise +4, +8, +12, +16. [V]

`TextureAnimationInfo`, 32 bytes, vtable 0xB48420 (dtor 0x6AFD60, slot 1 advance 0x6B4450): +4 name
(low bit 1 not owned), +8 frame count, +12 frame to texture id array, +16 interval (s), +20
accumulator, +24 current frame, +28 autoplay byte (1 at load). [V]

`TextureAnimationInfoChar`, 64 bytes, vtable 0xB4842C (dtor 0x6AFE00, same advance): the 32 above,
+32 groupName, +36 mUStart, +40 mVStart, +44 mUEnd, +48 mVEnd, +52 active byte (set by apply, read
only by debug pages), +56 PArray of linked `Mat_CTA` records, +60 PArray of exclusive group ids
from the .ovp (always empty in retail). [V]

Character set, 32 bytes: +0/+4 textures, +8/+12 infos, +16/+20 `Mat_CTA` records, +24 and +28 the
name twice. Registration, 12 bytes: `{set index, the 32 byte ClassCharacter instance key, PArray of
8 byte {record*, LayerStack*}}`, a free slot is `{-1, 0, 0}`. LayerStack, 20 bytes (`sub_6AF420(32)`):
`{int* ids, Rect* rects, ids end, rects end, capacity 32}`, Rect 20 bytes `{PTexture2D*, mUEnd,
mVEnd, mUStart, mVStart}`. [V]

`Mat_CTA` record, 0x7C bytes: +0 the model's shared PMaterial, +4..+24 AnimSampler..AnimSampler6
parameter definitions, +28 AnimSamplerState, +32..+52 layer1..layer6, +56..+76
AnimTranslate_GAME..6, +80..+100 AnimScale_GAME..6, +104 AnimTranslate_MAYA, +108 AnimScale_MAYA,
+112 UV variant byte (the `CHR_TEXTURE_ANIMATION_UV` variant switch is set), +113 six layers byte
(always 1), +116 material index in the mesh instance's material set, +120 mesh instance index. At
load AnimSampler1..6 get the capture `{255, 0, mgr+56, default texture}`, layer1..6 = 0.0,
AnimSamplerState `{71, 0, mgr+56, 0}`. The `_MAYA` and AnimSamplerState definitions are looked up
but never written per frame. [V]

## 5. Frame timing

`TextureAnimationInfo__advance 0x6B4450(dt)` [V]:

```
if (autoplay) {
    acc += dt;
    if (interval <= acc) {
        acc = 0;
        frame = (count + frame + 1) % count;
    }
}
```

It loops, drops the remainder (at most one frame per call), and would divide by zero on a zero
count, which no map info has.

`TextureAnimationManager__advanceAll 0x6B47E0(dt)` walks only the map infos at mgr+124/+128. It is
called from `FFX_DebugMenu_drawIfActive 0x672000`, which
`FFX_Frame_UpdateBoostersAndOverlays 0x657140` calls with dt = `*(PApplication + 0x24)`: the raw
Phyre frame delta in seconds, once per presented frame, main thread, after `FFX_MainStepLoop`. It
is skipped while gfx byte +4501 is set. It is not booster scaled (`FFX_Time_ScaleDtForBooster
0x6F7340` returns a scaled copy and writes nothing back) and has no pause gate. [V]

So map and rcBg autoplay is wall clock driven and keeps running in pause [I, from the missing gate]. Character infos are never
advanced by time, their frame comes from the step (section 7).

## 6. Outputs

`TextureAnimationManager__applyFrames 0x6B44A0`, main thread, per present, only when mgr+4 is set
[V]:
1. For each material i in +116: info = findInfo(material ID), id = info.frames[info+24].
2. Texture = +96[id] when i < +12, else +96[+16 + id].
3. When the texture is not null, find "TextureSampler" in the material's effect variant
   (`sub_56CBF0`, definitions of 16 bytes with the name at +4, offset u16 at +8, size `u16 at +10 &
   0x1FFF`) and write the capture `{255 -> 66, 0, sampler, texture}` with `sub_66E4F0`, a memcpy into
   the shared cluster material's PParameterBuffer. The sampler is mgr+56 when the existing capture's
   wrapS is 2, else mgr+20.

No UV is written. Single frame rcBg layers are re-bound every present. The material written is the
cluster's shared one, so all instances of a map material change together.

`setAnimatedMaterialsChar 0x6B3060(G string, Animation string, frame, 4 unused floats, charName,
instance)`, worker thread, per step [V]:
1. Skip when the instance is in `dword_CCB230[1..count]` (at most 5, filled by `sub_63D400` for magic
   ids 630..637, reset by `sub_63D550`).
2. Find the registration and the info, set info+52.
3. For each linked record take the instance's own material: instance+4 -> mesh instance [rec+120]
   -> +16 material set -> [rec+116]. So character writes go to per instance material copies.
4. Texture = `sub_6B1E40(info, frame, charName)`, the set's textures[info.frames[frame]].
5. Layer stack: remove the ids in info+60 and the group's own id `atoi(G + 1)`, drop the bottom
   entry when full, push `{group id, rect {texture, mUEnd, mVEnd, mUStart, mVStart}}`.
6. `sub_6B3660` applies the stack. Layer k is entry k-1 for depth up to 6, otherwise the top six.
   Each layer gets AnimSamplerK = `{255, 0, mgr+56, texture}`, layerK = 1.0, AnimScale_GAMEK =
   (mUStart, mVStart), AnimTranslate_GAMEK = (mUEnd, mVEnd). Missing layers get layerK = 0.
7. The UV variant (+112) gets AnimSampler = the top texture, layer1 = 1, layer2..6 = 0, no
   translate or scale.

The data reads mUStart and mVStart as a window size and mUEnd and mVEnd as its origin (m003: 0.0625,
0.0625, 0.3555, 0.0820) [binding V, meaning I]. The floats `sub_643EB0` computes from the PS2 rect
are ignored.

`sub_6B33C0` (a V scroll) uses the set's first info and its frame +24 (always 0): AnimSampler and
AnimSampler2 get that texture, scale 1.02 on both, translate (u - 0.01, v - 0.01) and (u - 0.01,
v - 1.01), layer1 = layer2 = 1. Called with u = 0, v = 1 - t/n. [V]

The shader side: the `CHR_TEXTURE_ANIMATION=1` variant (hash `6de69770`) declares every Anim* and
layer parameter. The UV only variant (`0f527c1b`) declares none of them (it has GlobalTime), so the
lookups return 0 and nothing is written there. [V for those two variants]

## 7. Who sets frames

Map 3D, step thread [V]:
- `FFX_MainStep` -> `FFX_Map_DrawPass0/1 0x908480/0x9084A0` -> `sub_90BF10` -> `sub_90BDB0` ->
  `sub_643A40` -> `TextureAnimationManager__setFrame 0x6B42C0`.
- mapWork slots: count at +645572, stride 1824. Slot +0 bit 0 pending, slot +4 frame. Per frame
  records at slot +645616 + 28*frame, whose +36 and +52 build the `%05d_%02d_%03d_%03d` name. The
  material index is found by name, then `setFrame(index, frame, absolute = 1)`.
- `setFrame` clears autoplay for good and sets the frame when it is in range. When no material
  matches, the pending bit stays and the request retries every step. A missing info logs
  "SceneName.%s, TextureAnimName.%s.".
- Pending is set by `sub_914A10` case 3 (a map animation track, frame = (int)track + 64), under
  `FFX_Map_StepAnims 0x908320` from `FFX_MainStep`.

rcBg, step thread [V]: `FFX_RcBg_Step 0x91A8C0` -> `sub_9217B0` -> `sub_920AA0` -> `sub_920B00`,
per 192 byte layer record i: `sub_643A00(i, 0)` turns autoplay off, `sub_643A20(i, rec+160->+60)`
sets the frame relative (material index +8 + +12 + i). Exceptions: genk12 layer 3 and genk05 layer 2
get autoplay 1 (the two 0.1 s animations), genk12 layer 2 writes +164. Layers with flags & 3 get
MaterialColour alpha 0 through `sub_644120` -> `sub_66D5C0`. `sub_6582E0` sets MaterialColour alpha
= a3/128 by layer.

Characters [V]:
- `FFX_Ch_UpdateRenderJob 0x833530` (a per step worker job queued by `FFX_Ch_DispatchUpdateJob` from
  `FFX_MainStep` through `FFX_Ch_UpdateMotionAll` and `FFX_Ch_DispatchInBatches`) -> `sub_77C8D0` ->
  `sub_77F360` steps the PS2 character texture animation stream (`dword_2310000`, 48 bytes per CHR
  slot) and calls `sub_77FBF0` -> `sub_643EB0` -> `setAnimatedMaterialsChar`, and for type 3
  `sub_644150` -> `sub_6B33C0`.
- Select and reset through `sub_77FCE0` -> `sub_643EB0`, from `sub_77E940`, which is reached from
  `FFX_Mot_RunEvents 0x834350` (motion events), `sub_83D040`, the ATEL Ch 120 handler `0xA79EF0`
  (type, packed anim | mode << 8), the ATEL Btl 226 handler `0x7A32A0` (target, packed, type 4), and
  the debug window "TEX ANIM" `sub_93E370`.
- Debug: `sub_6BC210` -> `sub_829C00` -> `sub_6440E0` for the player, from the CharTexAnim page
  (`FFX_DebugMenu_drawIfActive`). Page byte +17 enables the "setAnimatedMaterialsChar(...)" log.

## 8. Unload and reset

- Map: `sub_6B2F50` (clearMap) via `sub_6403B0` from `FFXApplication__exitScene` and `sub_909EA0`
  (`FFX_Map_LoadMapOut*`). Empties +92 and +116, destroys +124, clears +4. Leaves +8, +12, +16. [V]
- Character instance: register `0x6B29C0` from `ClassCharacter__createInstance 0x63C540`,
  unregister `0x6B4330` ("unregister(%u)") from `ClassCharacter__unload 0x637980` and
  `FFX_Chr_ReleaseInstance 0x63A030`. [V]
- Character class: `sub_6B1A70(name)` from `FFX_Gfx_UnloadCharacterClusters 0x670B20` unregisters
  the set's instances, then `sub_6B3FE0` clears the set. All: `sub_6B1B60` via `sub_670960` from
  `sub_6378B0`. Singleton: `sub_6B0120` from `FFX__graphicFinalize`. [V]

## 9. For the recreation

- Map and rcBg scripted frames (mapWork +645576/+645580, the rcBg layer +60) are step state and go
  in the snapshot. Hold, do not interpolate.
- Map autoplay is display only on the wall clock. The recreation can derive the frame from snapshot
  time as `floor(t / interval) mod count`, which differs from the original only by the dropped
  remainder (the original drifts late by up to one present per frame). [I]
- Character frames are step state (the PS2 stream in `dword_2310000`) whose apply writes per
  instance material parameters. The apply is presentation and a GPU candidate (six texture windows
  per material are a natural shader uniform block). The original applies it on a worker job during
  the step, so a present in between can see a half written instance. [I]
- No step code reads any output of the manager. [V for the readers listed in sections 6 and 7]

Proposed names, texture animation [V from the code above unless marked]:

| VA | name |
|---|---|
| 0x6AF370 | TextureAnimationManager__ctor |
| 0x6AF8A0 | TextureAnimationManager__dtor |
| 0x6B02E0 | TextureAnimationManager__get |
| 0x6B0120 | TextureAnimationManager__destroySingleton |
| 0x6B3F40 | TextureAnimationManager__initSamplerStates |
| 0x6B05D0 | TextureAnimationManager__loadFromClusters |
| 0x6B42C0 | TextureAnimationManager__setFrame (material index, frame, absolute) |
| 0x6B3600 | TextureAnimationManager__setLayerAutoplay |
| 0x6B20A0 | TextureAnimationManager__findMaterialIndexById |
| 0x6B2070 | TextureAnimationManager__getLayerMaterial |
| 0x6B1D10 | TextureAnimationManager__findInfo |
| 0x6B1EF0 | TextureAnimationManager__findCharSet |
| 0x6B1E40 | TextureAnimationManager__getCharFrameTexture |
| 0x6B29C0 | TextureAnimationManager__registerCharInstance |
| 0x6B4330 | TextureAnimationManager__unregisterCharInstance |
| 0x6B1A70 | TextureAnimationManager__unloadCharSet |
| 0x6B1B60 | TextureAnimationManager__unloadAllCharSets |
| 0x6B2F50 | TextureAnimationManager__clearMap |
| 0x6B3FE0 | TextureAnimationCharSet__clear |
| 0x6B3660 | TextureAnimationManager__applyCharLayerStack |
| 0x6B33C0 | TextureAnimationManager__setCharUvScroll |
| 0x6B2CA0 | CharTexLayerStack__remove |
| 0x6AF420 | CharTexLayerStack__ctor |
| 0x6B2400 | TextureAnimationInfoChar__init |
| 0x6AF0D0 / 0x6AF270 | TextureAnimationInfo__copyCtor / TextureAnimationInfoChar__copyCtor |
| 0x6AF760 / 0x6AF7F0 | TextureAnimationInfo__dtor / TextureAnimationInfoChar__dtor |
| 0x6B03A0 | TextureAnimationInfoChar__constructArray |
| 0x642D90 | FFX_TexAnim_SetLayerBase |
| 0x643A00 | FFX_TexAnim_SetLayerAutoplay |
| 0x643A20 | FFX_TexAnim_SetLayerFrame |
| 0x643A40 | FFX_TexAnim_SetFrameByName |
| 0x643EB0 | FFX_TexAnim_SetCharFrameFromPs2Rect |
| 0x6440E0 | FFX_TexAnim_SetCharFrame |
| 0x644120 | FFX_TexAnim_SetLayerAlpha |
| 0x644150 | FFX_TexAnim_SetCharUvScroll |
| 0x6582E0 | FFX_RcBg_SetLayerAlpha |
| 0x66D5C0 | FFX_Material_SetAlpha |
| 0x90BDB0 | FFX_Map_ApplyTexAnimRequest |
| 0x77C8D0 | FFX_ChTexAnim_Dispatch |
| 0x77F360 | FFX_ChTexAnim_Step |
| 0x77FBF0 | FFX_ChTexAnim_ApplyRect |
| 0x77FCE0 | FFX_ChTexAnim_Select |
| 0x77E940 | FFX_ChTexAnim_Command |
| 0x83D040 | FFX_Ch_TexAnimCommand |
| 0x829C00 | FFX_DebugCharTexAnim_SetPlayer |
| 0x63D400 | FFX_TexAnim_ExcludeMagicInstance [I] |
| 0x63D550 | FFX_TexAnim_ClearExcluded |
| dword_CCB230 | g_texAnimExcludedInstances |

# Part 2. PostProcessTweakUtil

## 10. What it is

The DebugMenuManager page of mode 1, the "PostProcess" page. `DebugMenuManager__setMode 0x6B4D10`
loads the page through `sub_692BF0` at 0x6B4D3D and calls its enable `sub_6984F0` at 0x6B4D91
(disable) and 0x6B4E07 (mode 1). It is a 22 row text menu over pointers into the scene context env
object (fog, glow), into the post target manager `pp = dword_CCC8A0` (USM), and into its own mirror
floats that it pushes into pp flags (glow, colour correction, HDAO, brightness, USM, SMAA). [V]

- RTTI: vtable 0xB473FC, one slot (scalar deleting dtor 0x692A60). Singleton `dword_CCC89C`, built
  by `sub_692B10` at boot (BOOT_SEQUENCE.md row 49), ctor `sub_692410`. [V]
- Data files: none. Every string referenced from 0x692400..0x699200 is one of the 22 labels,
  "%s: %.3f", ">%s: %.3f" or "PostProcess: %.2f fps". No method calls the file layer. [V]
- Clock: none. Edits are immediate, no blend, no dt. The only time value is the fps in the title.
  [V]

## 11. Object layout, 0x190 bytes

| offset | type | default | meaning |
|---|---|---|---|
| +0 | vptr | 0xB473FC | |
| +4 | u8 | 0, then 1 after init when the font loaded and env is set | initialised, ANDed into enable |
| +8..+151 | TextUtil, 0x90 bytes | ctor `sub_6F5B50` | the debug text panel every debug page embeds |
| +152 | char*[22] | | row labels |
| +240 | float*[22] | | what each row edits |
| +328 | float | 1.0 | Fog enable mirror |
| +332 | float | 1.0 | Glow enable mirror |
| +336 | float | 1.0 | ColorCorrection enable mirror |
| +340 | float | 1.0 | HDAO Level mirror |
| +344 | float | 1.0, then 51.0 (init adds 50.0) | Bright Level mirror, off by one against the pp default 50.0 |
| +348 | float | 1.0 | USM Enable mirror |
| +352 | float | 1.0 | USM Test mirror |
| +356 | float | 1.0 | SMAA Level mirror |
| +360 | float | 0 | fog limit saved while Fog enable is off |
| +364..+372 | float x3 | 0 | fog colour mirror |
| +376 | float* | env+16 | fog colour in env |
| +380 | int | 0 | cursor row 0..21 |
| +384 | u8 | 0 | visible (setEnabled) |
| +388 | float | 0.01 | multiplicative step |
| +392, +396 | int | 1280, 720 | view size, from the resize hook |

The mirrors are never seeded from pp, so they can disagree with the live state. [V]

## 12. What each row tweaks

env = `*(gfx + 0xCC)`, set once by `sub_6656E0` at 0x6656FC to `Phyre::getApplication() + 0x50`
(the PApplication scene context), so the cached pointers never go stale. Row table from
`initLabels 0x6938D0`, adjust rules from `0x698AB0` (dir is +1 or -1) [V]:

| row | label | edits | adjust | side effect |
|---|---|---|---|---|
| 0 | Fog far | env+36 | `*= 1 + 0.01*dir`, floor 0.01 | `sub_6E3B80` fog sync |
| 1 | Fog near | env+32 | same | same |
| 2 | Fog curve | env+40 | same | same |
| 3 | Fog limit | env+44 | `+= 0.003*dir`, shown x255 | same |
| 4..6 | Fog color r, g, b | mirror +364..+372 | reloaded from env+16..+24, `+= 0.001*dir`, written back | same |
| 7 | Fog enable | +328 | 1 or 0. Off saves env+44 to +360 and zeroes it, on restores | same |
| 8 | Glow upper threshold | env+128 | multiplicative | `sub_6A7550` glow recompute |
| 9 | Glow lower threshold | env+132 | multiplicative | same |
| 10 | Glow gain | env+136 | multiplicative | same |
| 11 | Glow spread | env+140 | `+= 0.01*dir` | same |
| 12 | Glow enable | +332 | 0 or 1 | pp+74675 byte |
| 13 | ColorCorrection enable | +336 | 0 or 1 | pp+74672 byte |
| 14 | HDAO Level | +340 | `+= dir`, wraps 0..3 | pp+74705 byte = level > 0, pp+4708 int = level |
| 15 | Bright Level | +344 | `+= dir`, wraps 0..100 | pp+74536 float = level |
| 16 | UnSharp Mask Threshold | pp+74412 | multiplicative | live pointer |
| 17 | UnSharp Mask intensity | pp+74408 | multiplicative | live pointer |
| 18 | UnSharp Mask Blur Distance | pp+74404 | multiplicative | live pointer |
| 19 | UnSharp Mask Test | +352 | 0 or 1 | pp+74416 float |
| 20 | UnSharp Mask Enable | +348 | 0 or 1 | pp+74707 byte |
| 21 | SMAA Level | +356 | `+= dir`, wraps 0..4 | pp+74709 byte = level > 0, pp+74524 int = level |

pp defaults from the pp ctor `sub_692020` [V]: USM blur distance 1.2, intensity 0.45, threshold 0.04,
test 0, pp+74516 = 0.05, brightness 50.0, SMAA level 2, HDAO level 1, pp+74672 = 0.

The readers, all render side [V]:
- pp+74536 brightness: `sub_695440` at 0x6954DE (`g_brightPara`).
- pp+74404..+74416 and pp+74707: `sub_697670`, the USM pass ("intensity", "threshhold").
- pp+4708 (0x1264) level and pp+74705 (0x123D1) enable: `sub_695BA0` (HDAO, gates at 0x695BDB and
  0x695BE4, level at 0x695C88). pp+74705 also in `sub_694F10`.
- pp+74524 and pp+74709: `sub_696AF0` (SMAA).
- pp+74672 and pp+74675: `sub_694F10` (composite with ColorCorrectionBuffer), `sub_697F20`, and
  `FFX_Gfx_ResizeMainTargetIfChanged`.
- Fog: `sub_6E3B80` (`FFX_FogMgr`, `dword_CE5170`) copies env clear and fog fields into the fog
  manager, which feeds the fog constants.
- Glow: `sub_6A7550(dword_CCCA84)` reads env+128..+140, clamps (upper minus lower to 0.01..1.1, gain
  and spread to 0.01..0.99), rebuilds four level weights at mgr+464..+492 through `sub_6A6E90`, gated
  on mgr+460. The glow pass `sub_694C60` consumes them ("FullGlowThreshold", "NoGlowThreshold").

Where the real values come from, since the page loads nothing [V]:
- Per map, the map scene's GlobalCamera (gfx+1272, set by `FFX_Gfx_FindGlobalCamera`). Fog install
  `sub_66EE70`: env 4..6 = GC+212/216/220 (fog RGB), env+32 = GC+228 (near), env+36 = GC+232 (far),
  env+40 = GC+236 (curve), env+44 = GC+240 / 255 (limit). Without a GlobalCamera: colour 0.28, 0.35,
  0.60, near 65536, far 65537, curve 1, limit 1. Post install `sub_66F140`: env+128..+140 = GC+260..
  +272 (glow upper, lower, gain, spread, lower clamped to at least 0.35), env+144..+152 = GC+276..
  +284. Forced to 1, 1, 0, 0 on map 409, and on 4842 and 5939 while `FFX_Story_GetProgress()` is 0.
  Without a GlobalCamera: 1, 1, 0, 0, 10000, 10000, 10000 and the warning "No global camera.
  Post-process is using default values from scene context".
- The options: `sub_66F010` copies `g_ffxGameSettings` +36 -> pp+74536 (brightness), +68 ->
  pp+74707 (USM), +72 -> pp+4708 (HDAO), +64 -> the AA mode (1 sets gfx+69080 = 1, 2..5 set gfx+69080
  = 0 with pp+74524 = 1..4, otherwise gfx+69080 = 2).

## 13. Callers

Every reference to `dword_CCC89C` is in create 0x692B10, destroy 0x692BC0 and the getter 0x692BF0.
[V]

| caller | function | calls | when |
|---|---|---|---|
| 0x641B3D -> 0x66574C | `FFX_GraphicInitialize` -> `sub_6656E0` | initLabels 0x6938D0(env) | boot, once |
| 0x66AF58 | `FFX_Frame_RenderScene`, the debug page draw loop | draw 0x6953A0(app, fps from app+0x30) | every present, returns at once unless +384 |
| 0x66C61A | `FFX_Gfx_ResizeAllTargets` | 0x698060(w, h) | target resize |
| 0x6B4D91, 0x6B4E07 | `DebugMenuManager__setMode` | 0x6984F0(0 or 1) | only reached from close (mode 10), so always 0 in retail |
| 0x6B4BA0, 0x6B4C16 | inside 0x6B4A90, the DebugMenuManager input handler (not an IDA function, no references) | 0x6984F0 | orphan. Key 0x49 or pad 0x13 + 0x0B cycles the mode |
| 0x6B4A1D | `sub_6B49C0` (disable all pages) <- 0x66FAD0 <- 0x643E30 | 0x6984F0(0) | 0x643E30 has no reference, orphan |
| 0x659D96 | `sub_659D20` <- `FFX__graphicFinalize` | 0x698F60 (release the text font) | shutdown |
| 0x64050A | `FFX__graphicFinalize` | destroy 0x692BC0 | shutdown |
| none | 0x693720 input, 0x698370 cursor helper | -> 0x698AB0 (callers 0x693896, 0x6938A9), -> 0x698FA0 | orphan: no call, jump or stored pointer to 0x693720, 0x698370, 0x643E30 or 0x66FAD0 in the image |

Input map in 0x693720 (raw Phyre key and pad codes, directions inferred): key 0x24 or pad 0x17
cursor up, 0x26 or 0x19 cursor down, held 0x25 or 0x1A adjust -1, held 0x27 or 0x18 adjust +1. The
adjust is a held check, so it repeats every call. [V codes, I directions]

Step versus present: in retail nothing runs. Driven by a plugin, the pp writes are render side
config. The env fog fields it writes are also written by step code (the MapFx setters listed in
VISUAL_CENSUS.md), so a driven fog edit races those. [I]

For the recreation: no data file to port. The page is a debug tool. The real inputs it shows are
the GlobalCamera post and fog fields and the options, both already step state at map load and
config.

## 14. TextUtil, the shared debug text panel

Ctor `sub_6F5B50`, also used by 0x6A6610, 0x6B52A0, 0x6B53B0, 0x6B5440, 0x6B5520, 0x6B55D0,
0x6B5670, 0x6B5730, 0x6B57D0, 0x6DDC80. Relative layout: +0/+4 resource handles, +8..+28 dwords
(+12 and +16 font and text objects), +32 identity matrix, +96 vec3, +112 dirty byte, +116/+120 the
line PArray, +124 title PString, +128 visible byte, +132 3.0, +136 0, +140 0.5. Release `0x6F6950`
also unregisters from the 16 slot list `dword_CE8260` ("TextUtilList failed to remove
TextUtilList"). [V ctor, I most meanings]

## 15. Proposed names, post tweak

| VA | name | evidence |
|---|---|---|
| 0x692410 | PostProcessTweakUtil__ctor | vtable, TextUtil ctor, defaults |
| 0x692940 | PostProcessTweakUtil__dtor | vtable, then 0x692950 on this+8 |
| 0x692950 | FFX_TextUtil__dtor | frees title and lines |
| 0x6F5B50 | FFX_TextUtil__ctor | shared by every debug page |
| 0x692BC0 | PostProcessTweakUtil__destroySingleton | from `FFX__graphicFinalize` |
| 0x692BF0 | PostProcessTweakUtil__get | returns `dword_CCC89C` |
| 0x6938D0 | PostProcessTweakUtil__initLabels | label and pointer tables, Bright + 50 |
| 0x693720 | PostProcessTweakUtil__handleInput | orphan, needs a function in the IDB |
| 0x698370 | PostProcessTweakUtil__moveCursor | orphan, needs a function |
| 0x698AB0 | PostProcessTweakUtil__adjustSelected | per row rules |
| 0x698FA0 | PostProcessTweakUtil__refreshLine | formats a row |
| 0x6953A0 | PostProcessTweakUtil__draw | "PostProcess: %.2f fps" |
| 0x6984F0 | PostProcessTweakUtil__setEnabled | +384 = enable and +4 |
| 0x698060 | PostProcessTweakUtil__onResize | +392, +396 |
| 0x698F60 | PostProcessTweakUtil__releaseText | jumps to 0x6F6950(this+8) |
| 0x6656E0 | FFX_Gfx_InitPostProcess | env, pp, pp init, tweak init |
| 0x6F68F0 | FFX_TextUtil__setVisible | +128 |
| 0x6F6670 | FFX_TextUtil__layout [I] | aspect maths, gated on visible |
| 0x6F5E30 | FFX_TextUtil__addLine | appends to +116 |
| 0x6F6070 | FFX_TextUtil__setLine | index check, assign, dirty |
| 0x6F6930 | FFX_TextUtil__setTitle [I] | assign, dirty |
| 0x6F65B0 | FFX_TextUtil__draw [I] | from every page draw |
| 0x6F6950 | FFX_TextUtil__release | TextUtilList unregister |
| 0x6E32C0 | FFX_FogMgr__get | returns `dword_CE5170` |
| 0x6E3B80 | FFX_FogMgr__syncFromEnv | env clear and fog -> manager |
| 0x6A6E30 | FFX_GlowMgr__get [I class] | returns `dword_CCCA84` |
| 0x6A7550 | FFX_GlowMgr__recomputeFromEnv | env+128..+140 -> four level weights |
| 0x66EE70 | FFX_Gfx_InstallFogFromGlobalCamera | GlobalCamera fog -> env |
| 0x66F140 | FFX_Gfx_InstallPostFromGlobalCamera | GlobalCamera +260..+284 -> env |
| 0x66F010 | FFX_Gfx_ApplyPostOptions | settings -> pp |
| 0x6B4A90 | DebugMenuManager__handleInput | orphan, needs a function |
| 0x6B49C0 | DebugMenuManager__disableAllPages | enable(0) on every page |

The DEBUG_MODE.md page table row for mode 1 can now read: page `dword_CCC89C`, getter 0x692BF0,
enable 0x6984F0, title "PostProcess: %.2f fps", draw 0x6953A0, input 0x693720 (orphan).

## 16. Tool

`tools/texanim.py` in the Zanarkand repo reads the formats of part 1: `ags <path>` lists one
cluster's animations, `ovp <file>` parses an .ovp text, `census [--materials]` produces the counts
in section 1. It imports `phyre.py` from the same directory (`PHYRE_DIR` overrides it) and `vbf.py`
from this repo. PostProcessTweakUtil has no data file, so there is no reader for it.

## 17. Open

- The map symbol quirk of section 2 on the 13 maps with a 3D and a 2D bundle, a runtime check.
- The meaning of mUStart, mVStart against mUEnd, mVEnd (size and origin is inferred from data).
- Whether a present can observe a half applied character instance (section 9).
- The direction meaning of the PostProcess page keys.
