# Map containers and the walkmesh

Reader: `../tools/mapfile.py`, which parses all 491 containers and can export a walkmesh as a
Wavefront OBJ.

## Where the walkmesh is

It is called **`ffxmap.id`**, and in the shipped PC build it is not a standalone file. It is a
section inside `mapout.vpa`, one per map:

```
ffx_ps2/ffx/master/jppc/map/<group>/<name>/bin/mapout.vpa       field, 433 files
ffx_ps2/ffx/master/jppc/btlmap/<group>/<name>/bin/mapout.vpa    battle, 58 files
```

Despite the `.vpa` extension the bytes start with the ASCII magic **`MAP1`**. The name `ffxmap.id`
survives in the exe as the string `"%s/ffxmap.id"` at `0xB5AA24`, used only by the dev loose-file
loader `FFX_Map_DevLoadMapFromHost 0x844D10`.

## The MAP1 container

Confirmed from `FFX_Map_GetSection 0x907F00`.

| off | size | field |
|---|---|---|
| `0x00` | 4 | magic `"MAP1"` |
| `0x04` | 12 | zero in all 491 files, unknown, not proven to be padding |
| `0x10` | 112 | `u32 sectionOffset[28]`, file-relative, 0 means absent |

Section roles, from `FFX_Map_OnMapOutLoaded 0x9097C0`, `FFX_Map_LoadMapOutSync 0x9093B0` and
`FFX_Map_SetZoneWalkmesh 0x90A4A0`:

| idx | role | status |
|---|---|---|
| 0, 11, 12 | background render data, all three funnel into `sub_91AA60` -> `rcbgMain 0x921D60` (11 and 12 are thunks to the same function) | confirmed |
| 1 | map object / instance table, mounted by `sub_90F8E0`. Own magic, first two dwords `0x2140A5` and `0x110`, then 64-byte records | confirmed as the call target, contents not decoded |
| **2 + zone** | **walkmesh (`ffxmap.id`)**, zones 0..7 map to indices 2..9 | confirmed |
| 10 | `sub_72A7F0`, a table of float6 records. Not collision | confirmed as the call target |

The zone mapping is not inferred. `FFX_Map_SetZoneWalkmesh 0x90A4A0` is literally
`FFX_Map_ActivateWalkmesh(FFX_Map_GetSection(g_ffxMapOut, zone + 2))`.

103 of the 491 containers are 64-byte stubs with no sections at all.

## `ffxmap.id` header, 32 bytes

| off | type | field | status |
|---|---|---|---|
| `0x00` | u32 | relocation marker, 0 in the file. `0x844BC0` sets `delta = &hdr - *(u32*)hdr` and writes delta back, so re-running is a no-op | confirmed |
| `0x04` | u16 | format stamp. Only 1827 (`0x0723`) and 4611 (`0x1203`) ship. `0x844BC0` tests `> 4610` | confirmed |
| `0x06` | s16 | group count, 16-byte records at `+0x1C` | confirmed, the relocator relocates exactly this many |
| `0x08` | u16 | 0 in all 469 shipped sections | unknown |
| `0x0A` | u16 | vertex count | **inferred.** Never read by the exe, but `field == maxVertexIndex + 1` in 469 of 469, and `0x20 + 8*field` equals the triangle offset or is 8 short |
| `0x0C` | f32 | `scale * 10`. `g_ffxWalkmeshScale = this / 10.0` | confirmed |
| `0x10` | u32 | 0 everywhere | unknown |
| `0x14` | u32 | 0 everywhere | unknown |
| `0x18` | u32 | vertex array offset, `0x20` in every file | confirmed |
| `0x1C` | u32 | group array offset | confirmed |

### Vertex, 8 bytes

`s16 x, s16 y, s16 z, s16 spare`, confirmed from `FFX_Map_TriNormalFromVerts 0x83E420`. The spare is
0 in every vertex of all 469 sections, so it is probably alignment padding but nothing proves it.

`world = value / g_ffxWalkmeshScale`, and **up is -Y**: `FFX_Ch_WalkmeshMove` seeds the fallback
normal as `(0, -1, 0)`.

### Group record, 16 bytes

| off | field | status |
|---|---|---|
| `0x00` | 0 in all 469, and the relocator does not touch it | unknown |
| `0x04` | 0 in all 469 | unknown |
| `0x08` | s16 triangle count -> `g_ffxWalkmeshTriCount` | confirmed |
| `0x0A` | 0 everywhere | unknown |
| `0x0C` | u32 triangle array offset -> `g_ffxWalkmeshTris` | confirmed |

**Every shipped section has exactly one group**, and `FFX_Ch_SetActiveWalkmesh` only ever reads group
0, so the multi-group capability is dead.

### Triangle, 16 bytes, all confirmed

| off | field |
|---|---|
| `0x00` / `0x02` / `0x04` | `u16` vertex index 0 / 1 / 2 |
| `0x06` / `0x08` / `0x0A` | `s16` neighbour triangle across edge v0-v1 / v1-v2 / v2-v0, -1 for none |
| `0x0C` | `u32` attributes, copied wholesale into `CHR+0x828 m_groundAttrs` |

## The attribute bitfield

Confirmed from the accessor block `0x83D7A0`-`0x83D940` plus the debug CHRInfo window format strings
at `0xB5B7C0` and `0xB5B7E4`.

| bits | name | accessor |
|---|---|---|
| 0..6 | surface type (the window labels it `map`), codes 48..63 indirect through `g_ffxGroundTypeRemap 0x13019D8` | `FFX_Map_GroundTypeRawOf 0x83D860` / `FFX_Map_GroundTypeOf 0x83D840` |
| 7..8 | `enc`, encounter group, copied into `g_ffxSaveData+16` | `0x83D820` |
| 9..10 | `eff`, footstep effect, indexes `g_ffxFootstepSoundTable` | `0x83D7C0` |
| 11..12 | `dic` | `0x83D7A0` |
| 13..14 | `wat` | `0x83D920` |
| 15..16 | `snd` | `0x83D900` |
| 17..21 / 22..26 / 27..31 | **5-bit shade weight at vertex 0 / 1 / 2** | `0x83D8A0` / `0x83D8C0` / `0x83D8E0` |

Off-mesh sentinel is `0xFFFE0000` (`FFX_Map_ClearGroundAttrs 0x83D940`).

**There is no map id in this dword.** An earlier note in `ffx_types.h` said the low 17 bits held one.
That span is just the packed subfields above, which the debug window prints as `id=%x` by masking
`attr & 0x1FFFF`. Corrected.

The top 15 bits being a per-vertex shade weight also corrected a second mistake: `CHR+0x8C/0x9C/0xAC`
were named `m_lightDir0/1/2` and described as directions that get dotted. They are actually the three
**vertices** of the CHR's current triangle in walkmesh space, written by
`FFX_Ch_WalkmeshTestEdgeCrossing` at `0x83DC91`-`0x83DCF8`, and
`FFX_Ch_UpdateShadeFromLights 0x834110` takes the 3D **distance** from `m_walkmeshPos` to each
(`FFX_Vec3_Distance 0x8366A0`), inverts it, weights by the three shade fields and feeds the length to
`FFX_Ch_SetShadeCoeff`. It is inverse-distance interpolation of a baked per-vertex shadow value, not
lighting. The whole feature is gated on `FFX_Map_IsNewWalkmeshFormat()`, and the 123 shipped
walkmeshes with the old stamp get a flat 16912 = 16/16/16 instead.

### Surface codes that mean something

From `FFX_Map_IsTriBlockedForChr 0x83E4E0`, which returns true when the triangle is a wall:

| code | meaning |
|---|---|
| 1 | wall for everyone |
| 2 | wall for every CHR that is **not** `FFX_Ch_GetPlayerChr()`, i.e. a player-only passage |
| 13 | keyed on `CHR m_flags2` bit `0x4` |
| 14 | wall **only** for the player |
| 48..63 | remapped through `g_ffxGroundTypeRemap` and re-dispatched |
| anything else | walkable |

Shipped data uses 0, 1, 14, 48..59 and 61. Census over all 469 sections: 317,228 code-0, 663 code-1,
6,087 code-14 and 18,841 in the 48..61 remapped range. Codes 2 and 13 only ever arrive via the
runtime remap table, which an ATEL script writes through `FFX_Map_SetGroundTypeRemap 0x83E9E0`.

Codes 2 and 14 are the reason a cloned character diverges from the player at scripted barriers. See
`PHASE1_CLONE.md`.

## There is no spatial index

The only acceleration is per-triangle edge adjacency plus the per-CHR cached triangle at `CHR+0x824`.
A cold lookup is a brute-force linear scan of every triangle
(`FFX_Ch_WalkmeshFindTri 0x83DE10`). The largest shipped mesh, `nagi00`, has over 11,000 triangles.

## `g_ffxWalkmeshScale`

At **`0x1301A90`**, a float. Written only by `FFX_Ch_SetActiveWalkmesh 0x83EA00` (store at
`0x83EA79`) as `*(float*)(ffxmapHdr + 12) / 10.0`. Getter `FFX_Map_GetWalkmeshScale 0x83E9A0`.

Shipped values run from **7.708** (`kino02`, an 1800 x 600 unit outdoor area) to **716.8** (a small
interior), with 22.938, 57.344, 89.6, 179.2, 204.8, 238.933 and 384.0 common.

Why the fixed point exists: the vertices are **s16**, a PS2 memory decision inherited wholesale. A
per-map scale is the only way to make +/-32767 counts cover both a 60-unit corridor and an 1800-unit
field at usable precision, so the scale is chosen per map to just fit the extent. Everything inside
the walkmesh code works in these integer units, because the edge half-plane equations are evaluated
on raw s16 products. The scale therefore only appears at the two boundaries:
`m_walkmeshPos = pos * scale` going in, `pos = result / scale` and `m_groundHeight = height / scale`
coming out.

Related: `g_ffxWalkmeshStepHeight 0xC4DEB0` is **300.0** world units, converted into walkmesh units
into `g_ffxWalkmeshStepHeightScaled 0x1301AF4` at the top of every `FFX_Ch_WalkmeshMove`.

## The load path

```
FFX_Map_LoadMapAsync 0x9083A0 (mapIndex, zone)        the normal path
  -> FFX_Map_LoadMapOutAsync 0x909A20 (26, i, zone)
       asset id 2*i+1 in group 26, callback FFX_Map_OnMapOutLoaded
  -> FFX_Map_OnMapOutLoaded 0x9097C0
       g_ffxMapOut 0x19138A8 = the MAP1 blob
       section 0        -> sub_91AA60,  cached in 0x191389C
       section 1        -> sub_90F8E0   (map objects)
       section zone+2   -> FFX_Map_ActivateWalkmesh 0x845000 (thunk)
                        -> FFX_Map_RelocateAndActivateWalkmesh 0x844BC0
                             fixes hdr+24, hdr+28 and each group's +12
                             g_ffxMapIsNewWalkmeshFormat 0x13034B8 = (*(u16*)(hdr+4) > 4610)
                        -> FFX_Ch_SetActiveWalkmesh 0x83EA00
                             g_ffxWalkmeshHeader   0x1301A94
                             g_ffxWalkmeshTriCount 0x1301A88
                             g_ffxWalkmeshTris     0x1301A84
                             g_ffxWalkmeshVerts    0x1301A8C
                             g_ffxWalkmeshScale    0x1301A90
                             then m_walkmeshTri = -1 on every live CHR
       section 10       -> sub_72A7F0,  cached in 0x19138A0
       sections 12, 11  -> sub_919B90, sub_919D30

FFX_Map_LoadMapSync 0x908370    the synchronous twin
zone change without a reload: FFX_Map_SetZoneWalkmesh 0x90A4A0 / ...AndNotify 0x90A6F0,
  queued through g_ffxPendingZoneMode 0x132711C and drained by sub_872BD0 / sub_873CC0,
  requested by the ATEL script via sub_873E60 / sub_874000
teardown: FFX_Map_ClearActiveWalkmesh 0x844CA0, from FFX_MainInit 0x820967
```

The dev loose-file path (`FFX_Map_DevLoadMapFromHost 0x844D10`) builds one of three templates chosen
by `g_ffxMapDevPathMode 0xC4DF50`, reads `"<dir>/ffxmap.id"` through `Sg_PcRead 0x83B1D0`, and is
reached only from a hardcoded dev entry (`sub_8467B0`, "sugimoto" / "kaisou"). Not used in retail.

**Note the address:** `FFX_Ch_WalkmeshMove` starts at **`0x83E5F0`**, not `0x83E670` as I had it in
one place. `0x83E670` is inside the body.

## Triggers are not in the map file

They are ATEL actors (type 2 line, type 3 box) whose geometry lives in the actor's motion state block
at `actor+1368`, which comes from the event script. The on-disk layout is an `.ebp` question.

Runtime offsets, from `FFX_Atel_StepLineTrigger 0x8684B0`:

| motion state off | field |
|---|---|
| `+0x00` / `+0x04` / `+0x08` | endpoint A xyz |
| `+0x30` / `+0x34` / `+0x38` | endpoint B xyz |
| `+0x1C` | Y tolerance, tested only when > 0 |
| `+0x48` | trigger radius, squared before use |
| `+0x40` | examine radius |
| `+0x3C` | facing angle for the examine test |

And from `FFX_Atel_StepBoxTrigger 0x866CC0`, same slots with different meaning: `+0x00..+0x08`
centre, `+0x30` half extent X, `+0x34` yaw (used negated), `+0x38` half extent Z, `+0x1C` half extent
Y tested only when > 0. The box test is Cohen-Sutherland outcodes on both the previous and the
current position, so a fast move that straddles the box still fires. Both report script events 3
(inside), 4 (entered), 5 and 6 (exit variants).

**They are player-only, and this is now confirmed from the code rather than inferred.** Both step
functions read only `g_ffxAtelCtx 0x1326B28` fields `+536/540/544` (current player position) and
`+552/556/560` (previous frame). Only two functions write those, `sub_869E40` at `0x869EC0` and
`FFX_Atel_SetActorPos 0x870B20` at `0x870BF0`, and both are gated on the identical test
`actor->id == *(u16*)(g_ffxAtelCtx + 10)`, the bound player actor id. Neither step function loops over
other actors or CHRs, and there is no per-actor breadcrumb trail.

## `.ahwin32` and `.ah` are build artefacts, not game data

Plain ASCII **C++ headers** generated by the Phyre asset pipeline ("ah" = asset header). No magic, no
sections. 1,324 `.ahwin32` and 8 `.ah` in the archive. **No code reads them** - the exe contains no
`".ah"` or `"ahwin32"` string at all.

What they are useful for: each gives the ordered **asset index -> `.phyre` path** mapping for one
asset group, which is exactly what `FFX_GetAssetLoader` is indexed by at runtime.
`FFX_Map_LoadMapSync` uses asset group **26** with ids `2*mapIndex` and `2*mapIndex+1`, so the map
group's header is the key for turning a map index into file paths. Dead weight for the shipped game,
a lookup table for us.

`.ah` is the same format guarded on `PHYRE_RENDERING_PLATFORM_GL` instead of `_D3D11`, a leftover
from an earlier target, and keeps slightly more identifiers.

## Does the decoded walkmesh look real?

Yes. `bsil02` (Besaid village) is 519 triangles, 0 degenerate, uniform winding, 201 boundary edges of
1,557, y from -6 to 61, median triangle area 36, and a top-down projection shows a plaza with
branching paths. `znkd04` shows corridors and rooms. `hiku00` has four separate zones in sections
2..5 at different scales covering different regions of the same map. This is hand-authored navmesh.

Triangle winding is uniform: raw `(v1-v0) x (v2-v0)` has positive Y in 5,750 of 5,752 sampled
triangles.

Side finding: **MAP1 section 9 (zone 7) is always a single flat 1000 x 1000 two-triangle plane** in
all 38 maps that have it, at a map-specific Y. That is a free-move or cutscene plane, not real
geometry.

## Known unknowns

- **Header `+0x08`, `+0x10`, `+0x14`, group `+0x00`, `+0x04`, `+0x0A`, and vertex `+0x06`.** All zero
  in all 469 sections and nothing in the exe reads them. Most likely PS2 runtime scratch that the
  exporter zeroes. Only a PS2-era `ffxmap.id` or the map exporter would settle it.
- **Why the format stamps are 1827 and 4611.** They look like `0x0723` and `0x1203`, so a Jul 23 and
  Dec 3 build date in BCD-ish nibbles, and `0x844BC0` compares against `0x1202`. A dated map-tool
  build log would confirm.
- **MAP1 section 1, the map object table**, past the `0x2140A5` / `0x110` magic and 64-byte stride.
  `sub_90F900` has a per-record-type switch that would give the whole thing. Worth a pass if a later
  phase needs placed objects.
- **MAP1 section 10.** Reads a `u8` at `+1` to size an `n * 8 KB` buffer, a `u16` at `+4` as a record
  count, then float6 records from `+32`. Reading the consumers of `unk_CEC258` would settle it.
- **What `rcbgMain 0x921D60` does with sections 0, 11 and 12.** Compare the section bytes against the
  `2d/*.dds.phyre` backgrounds.
- **What `g_ffxGroundTypeRemap` is actually set to in play.** The writer is an ATEL opcode handler, so
  the values live in the per-map `.ebp`.
- **Whether the ledge test really is `300 / scale` walkmesh units.** The arithmetic is unambiguous in
  the decompilation but has not been observed live. A breakpoint on `0x83DDB2` while walking off a
  ledge would confirm.
- **23 one-way adjacency links across 15 maps.** Mutual adjacency holds everywhere else. These are
  probably authoring noise, all in large meshes, but could be deliberate one-way doors.
- **7 maps have sections but no walkmesh** (`nagi05_b`, `omeg00_a`, `omeg01_a`, `sfia00_a`, `genk07`,
  `test11`, `titl01`). Several of those names look like test or title screens.
