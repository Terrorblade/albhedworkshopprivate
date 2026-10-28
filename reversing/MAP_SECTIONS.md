# MAP1 sections beyond the walkmesh

PROGRESS item 1.7.10. Every one of the 28 slots in a `mapout.vpa` (MAP1) header, plus the three
header dwords at `+4`, with a name, its readers in `FFX.exe` and what it drives, or the proof that
nothing reads it. The container itself and the walkmesh (sections 2..9) are in `MAP_FORMAT.md` and
are not repeated here.

Tags: **[V]** read in Hex-Rays or disassembly, **[V data]** checked against all 491 shipped
containers with `tools/mapfile.py`, **[I]** inferred. Addresses are VAs at base 0x400000. IDB names
are as of 2026-10-13. Names proposed here are collected in section 11.

Reader: `../tools/mapfile.py census` prints every archive number in this document,
`mapfile.py chunks <map>` lists the non-walkmesh sections of one map.

## 1. Summary

| idx | header field | name | element and count rule | readers | drives | step? |
|---|---|---|---|---|---|---|
| hdr +4 | `+0x04` u32 | **map flags**, bit 0 = indoor | one dword | `sub_908DD0` <- `sub_871E50` (field step), also orphans `0x908890`, `0x86BD60` | save data byte +41, the indoor latch, placed effects and ambient sound volume | **yes** |
| hdr +8, +12 | `+0x08`, `+0x0C` | none | zero in all 491 | none | nothing | unread |
| 0 | `+0x10` | **rcBg scene set** (YN stream) | YNDT, YNPR (scene count), one YNSC per scene, YNED | `rcbgMain__f921D60` via `sub_91AA60`. Presence also selects the Phyre rcBg scene load in `sub_908000` / `FFX_Map_LoadMapOutSync` | pre-rendered background scenes, layers, and **the scene camera, which overrides the field camera** | **yes** (camera) |
| 1 | `+0x14` | **3D map stream** | 64 byte header (magic `0x00214365`, version `0x110`, record count at +12), then records of a 64 byte header plus payload | `sub_90F8E0` -> `sub_90F900`. Presence also selects the Phyre 3D scene load | map part transforms, part animations, fog and environment, map animation tracks | no [I] |
| 2..9 | `+0x18`..`+0x34` | walkmesh, zone 0..7 | see `MAP_FORMAT.md` | `FFX_Map_ActivateWalkmesh` | collision, ground attributes, encounter group | **yes** |
| 10 | `+0x38` | **field particle set** (the `.pdt` format) | 32 byte header, placed effect records of 80 bytes from +32, count u16 at +4 | `sub_72A7F0`, then `FFX_MapFx_UpdatePlacedEffects 0x729AB0` every step | placed map particle effects | no (presentation on the step thread) |
| 11 | `+0x3C` | **guide map model** (YN stream of YNGM) | one YNGM per minimap variant | `sub_919D30` -> `rcbgMain__f921D60` | the field minimap | no |
| 12 | `+0x40` | **particle textures** (YN stream of YNFT) | one YNFT per texture | `sub_919B90` -> `rcbgMain__f921D60`, fetched by the particle operators through `sub_919BA0` | textures of the section 10 effects | no |
| 13..27 | `+0x44`..`+0x7C` | none | absent (offset 0) in all 491 | none, see section 9 | nothing | unread |

## 2. Archive statistics [V data]

491 containers, 103 of them 64 byte stubs with no sections. Battle maps are the 58 under `btlmap/`.

| idx | field maps | battle maps | size min | median | max |
|---|---|---|---|---|---|
| 0 | 149 | 0 | 117,200 | 781,872 | 6,082,432 |
| 1 | 243 | 58 | 2,432 | 1,688,688 | 8,390,896 |
| 2 | 327 | 54 | 112 | 8,016 | 363,248 |
| 3 | 30 | 0 | 416 | 2,352 | 25,456 |
| 4 | 14 | 0 | 688 | 1,296 | 8,016 |
| 5 | 4 | 0 | 784 | 1,248 | 4,480 |
| 6 | 1 | 0 | 4,352 | 4,352 | 4,352 |
| 7 | 0 | 0 | | | |
| 8 | 1 (`znkd08`) | 0 | 112 | 112 | 112 |
| 9 | 38 | 0 | 112 | 112 | 5,504 |
| 10 | 274 | 43 | 32 | 41,696 | 953,696 |
| 11 | 261 | 3 | 32 | 2,560 | 36,416 |
| 12 | 33 | 4 | 32 | 558,208 | 837,344 |
| 13..27 | 0 | 0 | | | |

Sizes are "up to the next populated section or EOF", so they are upper bounds.

Header dwords: `+8` and `+12` are 0 in all 491. `+4` is 1 in 49 containers and 0 in the rest:
48 field maps (`azit06 azit07 bjyt12 bsvr02..06 bsvr09 bsyt03 bsyt04 bvyt01..04 bvyt06..08 bvyt14
bvyt15 cdsp08 guad02..07 kami01 kami02 kami05..07 klyt06 klyt07 luca04 luca05 luca07 luca08 maca01
mcyt03 mcyt04 mihn03 ptkl08 ptkl14 slik05 slik06 stbv00 swin05`) and one battle map, `bsil03_a`.

How sections 0, 1, 10, 11, 12 combine (non-stub containers):

| count | present |
|---|---|
| 139 | 1, 10, 11 |
| 47 | 1, 10 |
| 39 | 0, 10, 11 |
| 32 | 0, 1, 10, 11 |
| 25 | 0 |
| 21 | 1, 10, 11, 12 |
| 17 | 1 |
| 13 | 0, 1, 10 |
| 12 | 0, 10 |
| 9 | 0, 11 |
| 8 | 1, 11 |
| 7 | 0, 1, 11 |
| 6 | 0, 1, 10, 11, 12 |
| 5 | 1, 10, 12 |
| 3 | 0, 1 |
| 2 | 1, 11, 12 |
| 1 each | 0, 10, 12 / 0, 1, 10, 12 / 0, 10, 11, 12 |

So a field map is either pre-rendered (section 0), 3D (section 1), or both (62 maps carry both).

## 3. Who reads the MAP1 header, the complete list [V]

Found three ways: every xref to `FFX_Map_GetSection 0x907F00`, every xref to the two globals that
hold a MAP1 blob (`g_ffxMapOut 0x19138A8` and the pending async blob `dword_1934ED0`), and a byte
scan of `.text` for the magic immediate `0x3150414D` ("MAP1"), which finds the inlined copies of
`GetSection`.

| reader | how | reads |
|---|---|---|
| `FFX_Map_GetSection 0x907F00` | the lookup, `base + *(u32*)(base + 16 + 4*idx)` | any slot it is asked for |
| `FFX_Map_OnMapOutLoaded 0x9097C0` | five inlined copies plus one `GetSection` call | `+0x10` (0), `+0x14` (1), zone+2, `+0x38` (10), `+0x40` (12), `+0x3C` (11) |
| `FFX_Map_LoadMapOutSync 0x9093B0` | `GetSection` | 0, 1, zone+2, 10, 12, 11 |
| `sub_908000` | `GetSection` on `dword_1934ED0` | 0 and 1, presence only, to start the Phyre scene loads |
| `FFX_Map_SetZoneWalkmesh 0x90A4A0`, `...AndNotify 0x90A6F0` | `GetSection` | zone+2 |
| `sub_908DD0` | `*(u32*)(g_ffxMapOut + 4)`, or -1 with no map | header `+4` |
| `0x908890` (no function defined, no xref, no pointer to it anywhere) | identical body to `sub_908DD0` | header `+4`, dead |
| `0x90A9E0` (no function defined, no xref, no pointer to it anywhere) | two inlined copies plus `GetSection` | magic only (result discarded), `+0x14` (1), slot 2 handed to `sub_72AA40`, dead |

Nothing else touches a MAP1 header. `g_ffxMapOutSection0 0x191389C` is written by the three loaders
and cleared by `sub_909EA0` but **never read** [V, all four xrefs are stores].
`g_ffxMapOutSection10 0x19138A0` is read only as a null test by `FFX_MapFx_StepPlacedEffects
0x9083E0` and the unload `sub_909EA0` [V].

The dead `0x90A9E0` is worth a sentence. It looks like the completion callback of a MAP1 that held a
particle pair, with slot 1 as a map stream and **slot 2 as the OEF2 particle texture bin** that
`sub_72AA40` loads (the same function `sub_909190` uses for the loose
`host0:/ffx/proj/map/f_particle/m%03d.bin`). So slot 2 was not always the zone 0 walkmesh. No code
reaches it in retail. [V for the reads and the absence of references, I for what it was.]

`FFX_Map_MountDevVpa` (dev only) also stores a blob in `g_ffxMapOut`, but that blob is a raw section
1 stream with no MAP1 header, so the zone walkmesh lookups on it fail the magic test [V].

### The load paths, corrected

`FFX_Map_LoadMapAsync 0x9083A0` (asset group 26) has one caller, `FFX_Btl_StepSubPhase`. It is the
**battle** map path. Field maps load through asset group 19 [V]:

```
field region load task "MAPREG"  sub_861650 (pushed by sub_8615B0)
  case 2: sub_908630(sceneId, sceneSubId) -> FFX_Map_LoadMapOutAsync(19, sceneId, subId)
            completion FFX_Map_OnMapOutLoaded (sections 0, 1, zone+2, 10, 12, 11)
  case 3: sub_908000  -> FFX_Gfx_FieldMapLoad(name, 0) if section 0, (name, 1) if section 1
  case 4: sub_919DA0(dword_1326B68)  guide map model select
          sub_729A10(&dword_1326BE0) particle state restore
  case 5: sub_873CC0  pending zone swap, sub_90A2B0, sub_919A40
battle:  FFX_Btl_StepSubPhase case 8, at battle START:
           map id & 0x400 -> FFX_Map_LoadMapAsync(id & 0x3FF, 0) -> group 26, zone 0
           otherwise      -> sub_908600(id, 0) -> group 19, a field map as the background
         the way back to the field is the MAPREG task above (sub_908630), not sub_908600
         (corrected 2026-10-27, BTLMAP_FORMAT.md 3)
sync:    sub_9085D0 (group 19) from FFX_Ev_LoadEventPackage and Save 003,
         FFX_Map_LoadMapSync (group 26) from Save 049
unload:  sub_909EA0  walkmesh off, rcBg context torn down and recreated (j_rcbgMain__f921CE0),
         particle set released (sub_7291C0), map work reset (sub_90CD90)
```

So the zone index of a field map load is the saved scene sub id, and the map index is the scene id.

## 4. Header `+4`: map flags, bit 0 = indoor

Readers [V]:

```
FFX_MainStep
  -> FFX_Field_DrawEvTexSpritesAndStepVoice 0x88D4F0
     -> FFX_EvTex_DrawSprites 0x872C90        (its first instruction is call sub_871E50)
        -> sub_871E50
             if !FFX_Btl_IsBattlePendingOrActive():
                 f = sub_908DD0()              (MAP1 +4, or -1 with no map)
                 if f >= 0: save[+41] = f & 1
             if save[+41]: byte_1326B83 = 1
             else if atelCtx[+2] & 1: byte_1326B83 = (atelCtx float +520 <= FFX_Came_GetPos(1).y)
             else byte_1326B83 = 0
FFX_Atel_StepFieldAll 0x872BD0, tail -> sub_8682F0
     if the field context is live and no battle is pending:
         sub_865740(atelCtx[+2] & 4 ? (atelCtx[+2] & 2) != 0 : byte_1326B83)
sub_865740(on): if on != save[+40]: save[+40] = on,
     sub_865780(event package, on)    sounds flagged 0x2 in the event's sound list get parameter
                                      12 set to 63 (on) or 127 (off), through sub_886FF0
     sub_8682A0(on)                   for each effect group id in dword_1326CF0[0..3]:
                                      on:  sub_729700 (reset the runtime records) and
                                           sub_7295B0 (clear runtime flag 0x10, the group runs)
                                      off: sub_729570 (set runtime flag 0x10, the group is
                                           suspended and its meshes unlinked)
```

What it means [I]: "the player is indoors", from the sound half: flagged ambient sounds drop to
about half (63 against 127, I that parameter 12 is volume). The effect half runs the other way from
what this doc used to say. `FFX_MapFx_UpdatePlacedEffects` **skips** a record with runtime flag 0x10
(`test byte ptr [eax],10h` at 0x729B65, then `MapFx__f728E40` unlinks its meshes through
`sub_645710(x, 0)` and `FFX_Gfx_LinkMeshToNode(x, 0)`) [V]. So with the bit on, the groups named by
the event are reset and **run**, and with it off they are suspended. Which effects those groups hold
was not checked, so the old reading "rain and snow stop indoors" does not follow from the code and
is open (corrected 2026-10-28, see `RCFLEX.md` 7). The 49 maps that set it are
temple cloisters (`*yt*`), Guadosalam, ship cabins (`slik`, `swin`), Besaid huts (`bsvr`), Luca
interiors, and `ptkl08` and `ptkl14` in the Thunder Plains. A map without the bit can still go
indoor per frame by camera height, through the event context's threshold at `+520`.

Why it matters: **save data bytes +40 and +41 are written from map data every field step**, and the
dynamic branch reads the camera position inside the step. In a lockstep simulation both peers load
the same map, so `+41` agrees, but the dynamic branch makes the field camera a simulation input
(on top of the known camera inputs in `CHARACTER_MOTION.md` 2.3). The battle map `bsil03_a` has the
bit, but `sub_871E50` never consults the header while a battle is active, so it is never applied.

Orphan duplicates [V]: `0x86BD60` (no function, no xref) does the same `save[+41] = flags & 1` and
returns `save[+41]`. It looks like a removed ATEL getter. `0x908890` is a second copy of
`sub_908DD0`.

## 5. Section 0: the rcBg scene set

A "YN" chunk stream, parsed by `rcbgMain__f921D60 0x921D60` (the source file is
`..\program\src\rcBg\rcbgMain.c`). The tag table is `g_ffxYnSectionTags` [V]:

| table index | tag | what `rcbgMain` does (switch on index - 1) | shipped where |
|---|---|---|---|
| 0 | `YNDT` | stream magic, checked once at +0 | every YN stream |
| 1 | `YNPR` | `rcbgPre__f921770` allocates the scene set, `rcbgLoad__f92B390` reads u32 scene count, 272 byte runtime record per scene | section 0, 149/149 |
| 2 | `YNSC` | `rcbgLoad__f92B5E0`: one scene, see below | section 0, 192 chunks in 147 maps |
| 3 | `YNTX` | default case, logged through a nullsub and skipped | never |
| 4 | `YNTM` | default case, skipped. **Shipped but unread** | section 0, 29 maps |
| 5 | `YNSM` | `sub_92B6B0` | never |
| 6 | `YNST` | `sub_92B6E0` | never |
| 7 | `YNSP` | case 6, skipped | never |
| 8 | `YNTT` | `rcbgTrxTex__f9221B0`, a texture transfer object of type `0x20003` | never |
| 9 | `YNSE` | default case, skipped | never |
| 10 | `YNFT` | `sub_9220C0`, the same object with type `0x20006` | section 12 |
| 11 | `YNGM` | `rcbgModel__f9283B0` + `rcbgLoad__f92B2F0`, attached to the scene named by the u16 at chunk header +10 | section 11 |
| 12 | `YNED` | end | every YN stream |

Chunk layout [V]: a 16 byte header (`tag[4]`, `u32 n`, then 8 bytes of which only the u16 at +10 is
read, by YNGM), then `16 * n` bytes of body. The stream header is a bare 16 byte `YNDT` chunk.
Every shipped stream in sections 0, 11 and 12 walks cleanly to `YNED` [V data].

YNSC body, as `rcbgLoad__f92B5E0` and `sub_92B1A0` consume it [V]:

```
u32 layerCount, 12 bytes skipped
per layer:  144 bytes  layer header              (sub_92B1A0, into the 192 byte runtime layer)
            u32 animFrameCount, 12 skipped
            if > 0: u32[animFrameCount] padded to 16, then one TRX image block
            u8 (layer +186), u8 (layer +187), 14 skipped
            one TRX image block                  (the layer image)
16 bytes  camera eye        -> scene +104
16 bytes  camera target     -> scene +120  (the "ref" position)
16 bytes  third vector      -> scene +136  (an up or roll axis, I)
u32       screen depth      -> scene +168  (float, 0.0 disables the camera)
u32       int screen Y offset -> scene +100
TRX image block (sub_92B710): 3 x 8 bytes, u32 w, u32 h, then w*h bytes kept by pointer.
            The upload hook nullsub_181 is empty on PC [V], so the pixel bytes are not drawn
            from here. The PC draws the Phyre "scene%dlayer%dShape" meshes instead (see
            VISUAL_CENSUS.md rows on rcBg layers) [I that nothing else reads the bytes].
```

**What it drives that the step can see** [V]: `sub_920C00` (from the YNSC load, and on the step
from `sub_920AA0` and the sway `sub_9210E0`, among eight callers) calls `FFX_Came_SetMatrixOverride(1, proj, view, target, eye, depth)`. While
that override is on, `FFX_Came_StepAll` copies the override reference position into every camera
slot at `+80`, `+32` and `+16` (checked at 0x7BE3D0..), and `FFX_Came_GetYaw` reads slot `+80`. The
field stick-to-move direction is camera relative, so **section 0's camera decides which way the
player walks on a pre-rendered map**. `sub_920C00` also sets the player CHR's clip Z to 1000. The
scene index is switched by `FFX_Map_SetZoneWalkmeshAndNotify` (zone n selects rcBg scene n through
`sub_91A0D0` -> `sub_9217E0`), so for these maps a zone is a camera cut plus a walkmesh swap.

Presentation side, already covered elsewhere: layer texture frames, layer alpha, scroll, camera
sway and roll with its CRT `rand()` consumer (the rcBg rows of `VISUAL_CENSUS.md` sections 4 and 6,
`THREADS_AND_CLOCKS.md` R14).

Presence of section 0 also makes `sub_908000` / `FFX_Map_LoadMapOutSync` call
`FFX_Gfx_FieldMapLoad(name, 0)`, the Phyre rcBg scene that `DISPLAY_LIST.md` places in cluster list
+0x8C [V].

## 6. Section 1: the 3D map stream

Mounted by `sub_90F8E0`, which refuses anything whose version dword is not `0x110`, and parsed by
`sub_90F900` into `g_ffxMapWork 0x1876E00` [V].

Header, 64 bytes [V]: `+0` magic `0x00214365` (the code compares against 2179941), `+4` version
`0x110`, `+8` u32 not read, `+12` record count, `+16..+63` not read. Records follow from +64.

Record: a 64 byte header (`+0` type, `+4` payload size, a few type specific dwords at +32..+40),
then `size` bytes of payload. An unknown type advances by the header only [V].

| type | handler | what it fills | shipped (records / maps) [V data] |
|---|---|---|---|
| 0 | `sub_90FF40` + `sub_90EDB0` | a map part (180 byte runtime record at mapWork dword 106979+45i, count at dword 106978): ids, 8 floats of placement, flags. Pushed to the Phyre map node by `sub_6426C0` -> `sub_66E3E0` (X and Y negated) and `sub_6426A0` -> `sub_66DB00` | 4,590 / 301 |
| 1 | `sub_90EF10` | one mesh of the current part, 64 byte sub-header then 16 byte entries, the entry count must equal payload +12 or the parse stops | 83,263 / 300 |
| 2 | inline | PS2 GS image upload. `sub_62F820` and `sub_62F7A0` are `return 0` on PC | 0 |
| 3 | `sub_917050` | PS2 GS transfer packet build, mode 2/3/4 from header +36 | 0 |
| 4 | `sub_90F090` | environment: payload +12 is the **fog colour** (lands at mapWork +426816 = `0x18DF140`, the value `SG_DebugWin_FogParameterProc` prints as R, G, B), +16..+24 three floats beside it (the debug window's LIMIT, NEAR, FAR, I), +28/+32 two angles in degrees turned into a rotation matrix at mapWork +426104, +0..+8 three dwords, +48..+60 four floats | 300 / 300 |
| 5 | inline | part animation key table, 128 byte keys, slot from header +36 | 1,269 / 204 |
| 6 | inline | map animation track, up to 64, advanced by `FFX_Map_StepAnims 0x908320` (light, fog, node params) | 3,060 / 82 |
| 7 | `sub_90ED10` | sixteen float4 at mapWork +426560 | 0 |
| 8 | inline | part animation key table, 32 byte keys | 8 / 1 |

One shipped section is refused: `mihn05_a` (a battle map) has version `0x100`, so `sub_90F8E0`
skips it entirely [V data + V code].

Drives [V]: part placement and animation of the Phyre 3D map, fog, map light tracks. Scripts start
part animations through Core 408..411 and 456/457 (`sub_908330`, `sub_908350`, `sub_9084B0`), and
all of those handlers return a constant. A byte scan for the animation slot offsets (641724, 641728,
641734) finds only the map module's own functions, so **no script or step code reads part animation
state back** [V for the scan, I for "presentation only"]. Collision never comes from here, only from
the walkmesh.

Presence of section 1 also makes the loaders call `FFX_Gfx_FieldMapLoad(name, 1)`, the Phyre 3D map
scene in cluster list +0xAC [V].

## 7. Section 10: the field particle set

Parsed by `sub_72A7F0`, the same function that loads the loose PS2
`host0:/ffx/proj/map/f_particle/m%03d.pdt` in `sub_909190`. So section 10 is a `.pdt` [V]. The set
pointer goes into `dword_D2C258`, and every step `FFX_MapFx_StepPlacedEffects 0x9083E0` ->
`FFX_MapFx_UpdatePlacedEffects 0x729AB0` runs it while `g_ffxMagicDrawFlags & 1`.

Header, 32 bytes [V code, V data for the values]:

| off | type | field | reader | shipped |
|---|---|---|---|---|
| +0 | u8 | format | not read | `'2'` (0x32) in 312, `'1'` in 5 |
| +1 | u8 | count of 8 KB scratch buffers | `sub_72A7F0`, allocates `n << 13` once | 0 in 277, up to 152 |
| +2 | u8 | work buffer class: 1 -> 1 MB, 2 -> 1.25 MB, else 768 KB | `sub_72A7F0` | 0 in 294 |
| +3 | u8 | | not read | 0 |
| +4 | u16 | placed effect count | `sub_72A7F0`, `sub_729570` etc. | 0..113, median 11 |
| +6 | u16 | program count, offsets at +16 | `sub_711FA0` relocates and links each program to the operator table `off_C3A508` | |
| +8 | u16 | count of 32 byte records at +20, three offsets each at +20/+24/+28 | `sub_711FA0` | |
| +10 | u16 | count of offsets at +24 | `sub_711FA0` | |
| +12 | u16 | count of 8 byte records at +28, offset at +4 | `sub_711FA0` | |
| +14 | u16 | | not read | 0 |
| +16..+28 | u32 x4 | table offsets, relocated in place | `sub_711FA0` | |

Placed effect record, 80 bytes, from +32 [V]: +0 position, +16 four s32 angles, +32 scale, +48 s32
start state, +52 program, +56..+64 cull values (`MAP_PARTICLES.md` 3.2 has every field), copied into the runtime table at `unk_D2C29C` (stride 128). +68 u16 **effect group**, which
`sub_729570` (set runtime flag 0x10 in the 76 byte records at `unk_D30260`), `sub_7295B0` (clear
it) and `sub_729700` (reset the record) match to switch every effect of a group. The update skips
records with 0x10 set and unlinks their meshes (0x729B65), so 0x10 means suspended, and group 0
records are also frustum culled. The indoor flag of section
4 uses exactly these calls. +74 u8, value 2 sets `dword_D2C25C`. The count rule
`32 + 80 * count` fits inside the section in all 317 shipped [V data]. Special case: a set with
exactly 40 effects, or map name hash 564, sets `dword_CEC228`.

Drives: placed map particle effects (waterfalls, rain, fireflies). Presentation. Their random
numbers are the cosmetic effect stream (`THREADS_AND_CLOCKS.md` R3/R4). The group show and hide
calls come from scripts and from the indoor flag, both step side. The step does read some state
back: ATEL waits on group state (MapFx 4 and 5), the point light mask lands in CHR +0x188, and the
`pppRand*` ops reseed the step thread's CRT `rand()`. `MAP_PARTICLES.md` 7 has the list [V].

## 8. Sections 11 and 12

**Section 11, guide map model** [V]. A YN stream of `YNGM` chunks (295 chunks in 262 maps, two maps
carry a bare `YNED`). Loaded by `sub_919D30` (a thunk to `sub_91AA60` -> `rcbgMain__f921D60`). Each
YNGM is read by `rcbgLoad__f92B2F0`: 4 bytes, u32 size, `size` bytes of vertex data, then 0x70,
0x20, 0x10, 0x40 and 0x40 byte blocks. It is attached to the rcBg scene named at chunk header +10
(when there is no section 0, `rcbgPre__f921640` makes a default scene). The current one is chosen by
`sub_919DA0` (scene +26), drawn every step through `FFX_Field_DrawGuideMap` -> `sub_919D00` ->
`sub_91DA80` -> `sub_928020`. That is the GuildMapDynamicGeometry minimap of `DYNAMIC_MESHES.md`
9.3. Presentation, except that the same draw writes the save block GuideMap explored bits on four
scenes (`RCBG.md` 4.4).

**Section 12, particle textures** [V for the reads, I for "these are the section 10 textures"]. A YN
stream of `YNFT` chunks. Each becomes a texture transfer object of type `0x20006` holding two TRX
image blocks. The VFX operators in the tables at `0xC3A90C`, `0xC3BFB4` and `0xC3FC8C`
(`sub_73ABE0`, `sub_73B5C0` and six more) fetch one by a byte index (+80 of the operator's own
record) through `sub_919BA0` -> `sub_921C60(0x20006, index)`. Every shipped section 12 with a YNFT is in a map that
also has section 10, and the two section 12s without one are in maps without section 10 (35 and 2)
[V data]. Presentation.

## 9. Proof that sections 13..27 are unread

1. Every `FFX_Map_GetSection` call site passes a constant 0, 1, 10, 11 or 12, or `zone + 2` [V].
2. The inlined copies read only `+0x10`, `+0x14`, `+0x38`, `+0x3C` and `+0x40` [V].
3. The zone comes from three places: the saved scene sub id (`sub_861650`), Core 393
   (`g_ffxPendingZonePlain`, a signed byte) and MapFx 006 (`g_ffxPendingZoneNotify`, s16). There is
   no bound check, so a zone of 11 would ask for slot 13. But a disassembly of all 397 shipped
   `.ebp` finds every Core 393 and MapFx 006 argument is an immediate: Core 393 with 0, 1 or 7
   (8, 10 and 2 calls), MapFx 006 with 0..4 (70, 59, 23, 16, 7 calls) [V data]. So the highest zone
   any script requests is 7, which is slot 9.
4. The offsets at `+0x44..+0x7C` are 0 in all 491 shipped containers [V data], so even an out of
   range zone would get "absent" and skip.

Related: **slot 7 (zone 5) is absent everywhere**, and **slot 8 (zone 6)** exists only in `znkd08`,
which no shipped script immediate selects. It is reachable only through a saved sub id of 6 [V data
for the census, I that no other writer of the sub id produces 6].

Header `+8` and `+12`: no reader in the complete list of section 3, and 0 in all 491 [V].

## 10. Step relevance, the short version

| part | reads in the step | lockstep relevant |
|---|---|---|
| header +4 | `sub_871E50` every field step, writes save +41 and +40 | yes: save data, and the dynamic branch reads the camera |
| section 0 | the camera override and scene select | yes: the camera yaw feeds player movement direction |
| sections 2..9 | the walkmesh | yes |
| section 1 | part animation and map light tracks on the step thread | no [I]: nothing reads them back |
| section 10 | placed effects on the step thread | no: cosmetic RNG stream, nothing reads back [I] |
| sections 11, 12 | minimap draw, particle textures | mostly no. The guide map draw writes the save block GuideMap explored bits on scenes 582/584/590/591, gated on option flag 0x40 (`RCBG.md` 4.4) |
| YNTM in section 0, header +8/+12, sections 13..27, slot 7 | nothing | unread |

## 11. Proposed IDB names

IDA was read only for this pass. One row each, with the evidence.

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x908000 | `sub_908000` | `FFX_Map_LoadPhyreScenesForMapOut` | presence of sections 0/1 of `dword_1934ED0` -> `FFX_Gfx_FieldMapLoad(name, 0/1)`, from the battle sub-phase and MAPREG case 3 |
| 0x908DD0 | `sub_908DD0` | `FFX_Map_GetMapOutFlags` | returns MAP1 `+4`, -1 with no map |
| 0x908890 | no function | `FFX_Map_GetMapOutFlags_dup_ORPHAN` | identical body, no reference |
| 0x86BD60 | no function | `FFX_Field_GetIndoorFlag_ORPHAN` | `save[+41] = flags & 1`, returns it, no reference |
| 0x871E50 | `sub_871E50` | `FFX_Field_UpdateIndoorFlag` | section 4 |
| 0x8682F0 | `sub_8682F0` | `FFX_Field_StepIndoorState` | calls `sub_865740` with the indoor flag from `FFX_Atel_StepFieldAll` |
| 0x865740 | `sub_865740` | `FFX_Field_SetIndoor` | latches save +40, sound volumes, effect groups |
| 0x865780 | `sub_865780` | `FFX_Field_SetAmbientMuffle` | flagged sounds to volume 63 or 127 |
| 0x8682A0 | `sub_8682A0` | `FFX_MapFx_ShowIndoorGroups` | toggles the effect groups in `dword_1326CF0[0..3]` |
| 0x1326B83 | `byte_1326B83` | `g_ffxFieldIndoor` | written by `sub_871E50` and `0x870240`, read by `sub_8682F0` and `0x86D9C0` |
| 0x90A9E0 | no function | `FFX_Map_OnParticleMapOutLoaded_ORPHAN` | MAP1 slot 1 mount and slot 2 -> `sub_72AA40`, no reference |
| 0x861650 | `sub_861650` | `FFX_Field_MapRegLoadTask` | the "MAPREG" boot task, scene id and sub id -> group 19 load |
| 0x908630 | `sub_908630` | `FFX_Map_LoadFieldMapOutAsync` | `FFX_Map_LoadMapOutAsync(19, ...)` |
| 0x908600 | `sub_908600` | `FFX_Map_LoadFieldMapOutAsyncFromBattle` | same, called from `FFX_Btl_StepSubPhase` |
| 0x9085D0 | `sub_9085D0` | `FFX_Map_LoadFieldMapSync` | `FFX_Map_LoadMapOutSync(19, 2i, 19, 2i+1, zone)` |
| 0x909EA0 | `sub_909EA0` | `FFX_Map_UnloadMapOut` | walkmesh off, rcBg reset, particle set release, map work reset |
| 0x90F8E0 | `sub_90F8E0` | `FFX_Map_MountMapStream` | section 1, refuses version != 0x110 |
| 0x90F900 | `sub_90F900` | `FFX_Map_ParseMapStream` | the record switch of section 6 |
| 0x90FF40 | `sub_90FF40` | `FFX_Map_ResetPart` | clears a 180 byte part, identity matrix |
| 0x90EDB0 | `sub_90EDB0` | `FFX_Map_ReadPartRecord` | type 0 |
| 0x90EF10 | `sub_90EF10` | `FFX_Map_ReadPartMeshRecord` | type 1 |
| 0x90F090 | `sub_90F090` | `FFX_Map_ReadEnvRecord` | type 4, fog colour at `0x18DF140` |
| 0x91AA60 | `sub_91AA60` | `FFX_RcBg_LoadYnStream` | guards the context, `rcbgMain__f921D60` then scene 0 |
| 0x919D30 | `sub_919D30` | `FFX_RcBg_LoadGuideMapSection` | section 11 thunk |
| 0x919B90 | `sub_919B90` | `FFX_RcBg_LoadParticleTextureSection` | section 12 thunk |
| 0x921940 | `sub_921940` | `FFX_RcBg_FindYnTag` | index into `g_ffxYnSectionTags` |
| 0x9217E0 | `sub_9217E0` | `FFX_RcBg_SelectScene` | scene index, camera, guide model |
| 0x91A0D0 | `sub_91A0D0` | `FFX_RcBg_SelectSceneForZone` | from `FFX_Map_SetZoneWalkmeshAndNotify` |
| 0x919DA0 | `sub_919DA0` | `FFX_RcBg_SelectGuideMapModel` | scene +26 = index, `sub_91D590` |
| 0x920C00 | `sub_920C00` | `FFX_RcBg_ApplySceneCamera` | `FFX_Came_SetMatrixOverride`, fov, aspect |
| 0x92B1A0 | `sub_92B1A0` | `rcbgLoad_ReadLayer` | YNSC layer parse |
| 0x92B710 | `sub_92B710` | `rcbgLoad_ReadTrxImage` | 32 byte header, w*h bytes |
| 0x9220C0 | `sub_9220C0` | `rcbgTrxTex_NewParticleTexture` | type 0x20006 |
| 0x919BA0 | `sub_919BA0` | `FFX_RcBg_GetParticleTexture` | `sub_921C60(0x20006, i)` from the VFX operators |
| 0x921C60 | `sub_921C60` | `FFX_RcBg_FindObject` | searches the current then every scene by type and index |
| 0x72A7F0 | `sub_72A7F0` | `FFX_MapFx_LoadParticleSet` | section 10 and the loose `.pdt` |
| 0x72AA40 | `sub_72AA40` | `FFX_MapFx_LoadParticleTextures` | OEF2 textures and CLUTs of the loose `.bin` |
| 0x909190 | `sub_909190` | `FFX_MapParticle_LoadPair` | `.bin` then `.pdt` from host0 |
| 0x711FA0 | `sub_711FA0` | `FFX_Vfx_RelocateParticleSet` | relocates +16..+28 and the four tables |
| 0x729570 | `sub_729570` | `FFX_MapFx_SuspendGroup` (was applied as `FFX_MapFx_EnableGroup`) | sets runtime flag 0x10 for every record whose +68 matches. The update skips flagged records and unlinks their meshes, so this stops the group. Renamed by `tools/apply_rcflex_names.py` |
| 0x7295B0 | `sub_7295B0` | `FFX_MapFx_ResumeGroup` (was applied as `FFX_MapFx_DisableGroup`) | clears the same flag, the group runs again |
| 0x729700 | `sub_729700` | `FFX_MapFx_ResetGroup` | zeroes the 128 byte runtime record of every match |
| 0x191389C | `g_ffxMapOutSection0` | keep, comment "write only" | four stores, no load |

The IDB function comments on `FFX_Map_GetSection` and `FFX_Map_OnMapOutLoaded` say the section 1
magic is `0x2140A5`, that section 10 holds float6 records, and that bytes 4..15 are unused. All three
are wrong, see `MAP_FORMAT.md`'s correction.

## 12. Open

- What `YNTM` (29 maps, section 0) was for. Nothing in the PC build reads it.
- The section 1 type 4 floats beyond the fog colour, and the two angles. Naming them needs a
  reader of mapWork +426820..+426828 and +426104 outside the debug window.
- The 180 byte map part record and the part animation key formats (types 5, 6, 8) in full.
- Section 10 program and table formats (+16..+28). That is the particle VM, a separate item.
- Whether anything besides the Phyre layer meshes reads the YNSC TRX image bytes.
- The meaning of the event context threshold at `+520` in the dynamic indoor test, and which
  syscall writes it.
