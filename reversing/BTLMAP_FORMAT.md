# Battle map containers and battle backgrounds

Project Zanarkand item 1.11.13. What a `btlmap` container is, the Phyre files beside it, how
`FFX.exe` loads a battle background, which parts feed the simulation and which are only drawn, and
how a battle picks its map.

Tags: **[V]** read in Hex-Rays or disassembly, **[V data]** checked against every shipped file with
`../tools/btlmap.py`, **[I]** inferred, **[D]** dev only, unreachable in retail. Addresses are VAs at
base 0x400000. IDB names are as of 2026-10-27. Names proposed here are in section 9 and in
`../tools/apply_btlmap_names.py`.

Reader: `../tools/btlmap.py`. It opens `FFX_Data.vbf` read only through `vbf.py`, parses in memory
and writes nothing.

```
python -I btlmap.py survey      # every container to the last byte, exit 1 on any failure
python -I btlmap.py show <name> # the region map of one container
python -I btlmap.py rows        # the asset kind 26 rows and the btl.bin scenes that use each
python -I btlmap.py assets      # the ps3data/btlmap files by extension, directory and Phyre class
```

## 1. Short form

- A btlmap container is a **MAP1 file**, the same format and the same loader as a field map
  (`MAP_FORMAT.md`, `MAP_SECTIONS.md`). It lives at
  `ffx_ps2/ffx/master/jppc/btlmap/<group>/<name>/bin/mapout.vpa` and is asset kind 26, row `r`
  being path table entry `2r+1`.
- There are **59 files** under `btlmap/`: **58 MAP1 containers** and one zero length build leftover,
  `kino/kino01_a/bin/mapout.$$$` [V data].
- What differs from a field map is which sections are filled. **No btlmap has section 0**, so there
  is no pre-rendered background and no scene camera. Every one has section 1 (the 3D map stream),
  54 have a zone 0 walkmesh, 43 have the particle set, and only 3 and 4 have sections 11 and 12.
  No btlmap has a zone 1..7 walkmesh [V data].
- What a battle draws is the **Phyre scene beside it**:
  `ffx_data/gamedata/ps3data/btlmap/<group>/<name>/<name>.ahwin32`, then `mdl/d3d11/<name>.dae.phyre`
  and its textures.
- **Only the zone 0 walkmesh feeds the simulation.** It is the ground of every battle CHR, and the
  battle reads it back through `FFX_Btl_GetUnitGroundHeight` for move targets, arc paths and a
  script visible syscall. Unit positions do **not** come from the btlmap. They come from the battle
  field file's placement section. Everything else is presentation.
- The load runs at battle start in `FFX_Btl_StepSubPhase` cases 8, 9 and 34. The MAP1 read
  completes on the step thread, the Phyre scene on the worker, and the step waits for the worker
  (hazard H1 of `ASYNC_LOAD_REQUESTS.md`).

## 2. The container on disk

### 2.1 Header

The MAP1 header is 0x80 bytes, read by `FFX_Map_GetSection 0x907F00` [V]:

| off | field |
|---|---|
| `+0x00` | `"MAP1"` |
| `+0x04` | map flags, bit 0 indoor. 1 only in `bsil03_a` among the btlmaps. The battle never reads it (`MAP_SECTIONS.md` 4) |
| `+0x08`, `+0x0C` | zero, unread |
| `+0x10..+0x7C` | 28 u32 section offsets, 0 when absent |

### 2.2 Sections a btlmap uses [V data]

| idx | count of 58 | what it is | reader | role in battle |
|---|---|---|---|---|
| 0 | 0 | rcBg scene set, YN stream | `rcbgMain 0x921D60` | absent everywhere |
| 1 | 58 | 3D map stream, magic `0x00214365`, version `0x110` | `FFX_Map_MountMapStream 0x90F8E0` -> `FFX_Map_ParseMapStream 0x90F900` | part transforms, part animation, fog and environment [I] |
| 2 | 54 | zone 0 walkmesh (`ffxmap.id`) | `FFX_Map_ActivateWalkmesh` | **the battle ground** |
| 3..9 | 0 | zone 1..7 walkmesh | | absent everywhere |
| 10 | 43 | particle set, the `.pdt` format | `FFX_MapFx_LoadParticleSet 0x72A7F0`, `FFX_Vfx_RelocateParticleSet 0x711FA0` | placed ambient effects |
| 11 | 3 | guide map model, YN stream of YNGM | `rcbgMain` | minimap model, no minimap in battle [I] |
| 12 | 4 | particle textures, YN stream of YNFT | `rcbgMain`, fetched through `sub_919BA0` | textures of the section 10 effects |
| 13..27 | 0 | | | absent everywhere |

Section 1 record types across all 58: type 0 x627, type 1 x33877, type 4 x57, type 5 x149,
type 6 x336. Types 2, 3, 7 and 8 do not occur in a btlmap [V data].

One container, `mihn05_a`, has a section 1 of version `0x100`. `FFX_Map_MountMapStream` refuses any
version but `0x110`, so its body is opaque. Nothing can load it anyway, see 4.3.

### 2.3 Section 1, the map stream

A 64 byte header (`+0` magic, `+4` version, `+12` record count), then `count` records of a 64 byte
record header (`+0` type, `+4` payload size) and the payload. `FFX_Map_ParseMapStream 0x90F900`
switches on types 0..8. The reader checks what the exe checks [V]:

- type 0 payloads are at least 64 bytes (`FFX_Map_ReadPartRecord` reads 64)
- type 1 is a 64 byte sub-header plus 16 byte entries, and `(size - 64) / 16` must equal the
  sub-header `+12`, or the parser stops the whole stream (`FFX_Map_ReadPartMeshRecord 0x90EF10`)
- type 5 holds 128 byte keys and type 8 32 byte keys when the record header `+32` is 0

### 2.4 Section 2, the walkmesh

Same as a field walkmesh (`MAP_FORMAT.md`): a 32 byte header, 8 byte vertices from `+0x20`, an
optional 8 byte slot, 16 byte triangles, then the 16 byte group records. Across all 469 walkmeshes
in the archive the group records end the section exactly, and the optional slot, where present, is
the ASCII text `DEWPRISM` (178 of 469) [V data]. No code reads it.

The btlmap walkmeshes come in a few shapes [V data]:

| shape | count | what |
|---|---|---|
| `grid1250` | 36 | 676 vertices, 1250 triangles, a flat 2500 x 2500 square at y = 0, scale 22.938 |
| `plane2` | 14 | 4 vertices, 2 triangles, a flat 1000 x 1000 square at y = 0, scale 57.344 |
| `mesh104` | 1 | `test00_a` |
| `mesh146` | 1 | `sins05_a`, scale 191.15 |
| `mesh229` | 1 | `lmyt01_a`, scale 127.91 |
| `mesh7` | 1 | `test00_b`, scale 8.58 |
| none | 4 | `nagi05_b`, `omeg00_a`, `omeg01_a`, `sfia00_a` |

So 50 of the 54 battle grounds are a flat plane at y = 0, and three real battle maps have a shaped
ground (`sins05_a`, `lmyt01_a`, and `test00_b` which a btl.bin scene uses, see 4.2).

The four with no walkmesh are live battle maps (rows 43, 52, 53, 57, all used by btl.bin).
`FFX_Map_LoadMapOutAsync` calls `FFX_Map_UnloadMapOut 0x909EA0` first, which at `0x909EB7` is
`push 0` / `call FFX_Map_ActivateWalkmesh`, so the walkmesh is cleared before every load and these
four battles run with no walkmesh (`g_ffxWalkmeshTris` 0, scale 1.0) [V]. With no triangles,
`FFX_Ch_WalkmeshMove 0x83E5F0` takes its else branch: `pos += vel` on X and Z with no slide,
`m_groundHeight = 0`, `m_groundAttrs = 0xFFFE0000` and the normal `(0, -1, 0)` [V].

### 2.5 Section 10, the particle set

The field particle set, the `.pdt` format. Every offset is relative to the set base.

| off | field |
|---|---|
| `+0x04` u16 | placed effect count, 80 byte records from `+0x20` (`sub_72A7F0` advances by 80) |
| `+0x06` u16 | program count, u32 offsets in the table at `+0x10` |
| `+0x08` u16 | count of 32 byte records at `+0x14`, offsets at record `+20/+24/+28` |
| `+0x0A` u16 | count of u32 offsets at `+0x18` |
| `+0x0C` u16 | count of 8 byte records at `+0x1C`, an offset at record `+4` |

A program (relocated by `sub_7120F0`) holds offsets relative to itself: `+8` a counted list, `+12` a
counted offset list, `+16` the first node of a chain. A node is `+0` next, `+38` s16 entry count,
16 byte entries from `+40` with offsets at `+8` and `+12`. The chain ends at a node whose `+0` is
zero. The reader checks that every relocated offset lands inside its program [V data]. The program
bodies are the particle VM and are counted as bounded blobs, not decoded.

Between the tables and the next aligned object the exporter leaves up to 15 bytes of stale non-zero
padding (232 bytes in the whole set). Nothing counts or points into them.

### 2.6 Sections 11 and 12, YN streams

A 16 byte `YNDT` header, chunks of a 16 byte header (`+0` tag, `+4` body size in 16 byte units) and
the body, up to `YNED`.

- **YNFT** (section 12): u16, u16, 12 bytes, then two TRX images, each a 32 byte header (w at `+24`,
  h at `+28`) and `w*h` bytes. The two images end the chunk body exactly in all 36 YNFT chunks
  [V data].
- **YNGM** (section 11): u32 kind, u32 vertex size, the vertex bytes, then the tail.
  `rcbgLoad__f92B2F0` reads `8 + vertexSize + 0x130` bytes. That is 8 bytes past the chunk body in
  288 chunks and 24 bytes past in 7 [V data]. In `sins05_a` and `test00_b` section 11 is the last
  thing in the file, so the 24 byte read runs past the end of the MAP1 data. Whether it runs past
  the allocation is open, see 10.

### 2.7 Survey

`cd tools && python -I btlmap.py survey`, exit code 0:

```
files under btlmap/      : 59 (58 MAP1 containers, 1 empty build leftovers)
parsed to the last byte  : 57 ok, 1 with an opaque refused stream, 1 empty, 0 FAIL
bytes                    : 70938944 total = 69129532 sized + 771496 bounded + 404 zero pad + 232 stale alignment + 1037280 opaque + 0 unclaimed
sections present         : 1 x58, 2 x54, 10 x43, 11 x3, 12 x4
section 1 record types   : 0 x627, 1 x33877, 4 x57, 5 x149, 6 x336
zone 0 walkmesh          : grid1250 36, mesh104 1, mesh146 1, mesh229 1, mesh7 1, no walkmesh 4, plane2 14
kind 26 path table rows  : 58, 57 name a mapout.vpa, 56 of those ship, missing: 38 nagi00_b
containers no row names  : kino03_a, mihn05_a
rows btl.bin battles use : 51 (1 2 3 4 5 6 7 8 9 10 11 12 14 15 16 17 18 20 21 22 23 24 25 26 27 28 30 31 32 33 34 35 36 37 39 41 42 43 44 45 46 47 48 49 50 51 52 53 55 56 57)
shipped rows no battle uses: 13 kino05_a 19 kami03_a 29 bika04_a 40 nagi03_a 54 test00_a
field maps used as a battle background: 39 map ids
containers with no ps3data scene header: kino03_a, mihn05_a
```

Every byte is one of: **sized** (its extent comes from a count or size field the exe reads),
**bounded** (a blob that starts at a referenced offset and ends at the next one, only the section
10 programs and blobs), **zero pad**, **stale** alignment bytes, or **opaque** (the refused
`mihn05_a` stream). Unclaimed bytes or overlaps are failures.

## 3. The load path

`FFX_Btl_StepSubPhase 0x783020`, on the step thread [V]:

```
case 7   FFX_Btl_SetupFieldPointers 0x783E20
         phase = (j_FFX_SaveData_GetSceneId() == (s16)g_ffxBattleSceneAndMap) ? 10 : 8
         so a battle staged on the field map the party already stands on loads nothing
case 8   g_ffxBtlMapLoadPending 0x112CA68 = 1
         sub_643490(1)                                   gfx +4502 battle map flag
         phase = 9, byte_112C9DA = 1                     0x7831A1, 0x7831A8
         id = g_ffxBattleSceneAndMap & 0x3FF
         if (g_ffxBattleSceneAndMap & 0x400)             test at 0x783195
             FFX_Map_LoadMapAsync 0x9083A0(id, 0)        call at 0x7831B2
               -> FFX_Map_LoadMapOutAsync 0x909A20(26, id, 0)
         else
             FFX_Map_LoadFieldMapOutAsyncFromBattle 0x908600(id, 0)   call at 0x7831C2
               -> FFX_Map_LoadMapOutAsync(19, id, 0)
case 9   wait while sub_908F10() & 0x20 or & 0x40        0x7831D2..0x7831E6
         FFX_Map_LoadPhyreScenesForMapOut 0x908000       0x7831EC
         phase = 34
case 34  wait for g_ffxBtlMapLoadPending == 0            0x7831F8
         phase = 10, FFX_Btl_RollPreemptiveOrAmbush
case 10  if byte_112C9DA: sub_784420 puts monsters 20..27 at their placement points
```

`FFX_Map_LoadMapOutAsync 0x909A20` [V]:

- calls `FFX_Map_UnloadMapOut 0x909EA0` (walkmesh off, rcBg torn down, particle set released)
- resolves asset `2*row+1` of kind 26 to a path and allocates the buffer 64 byte aligned
  (`sub_90BBE0`)
- queues a ROM read with callback `FFX_Map_OnMapOutLoaded 0x9097C0` through `AssetLoader[0]`
- sets `g_ffxMagicDrawFlags |= 0xA0`

`FFX_Map_OnMapOutLoaded 0x9097C0` runs on the **step thread**, from
`FFX_AsyncLoad_DispatchCompletions` [V]. It mounts section 0 if present, section 1, activates the
zone 0 walkmesh, loads section 10, then sections 12 and 11. It stores the row in `dword_18DED30`
and clears `0x20` and `0x40` of the draw flags (`&= 0xFFFFFF5F`), which releases case 9.

`FFX_Map_LoadPhyreScenesForMapOut 0x908000` [V] builds the header path with
`FFX_Gfx_BuildMapHeaderPath 0x659290`, `PS3Data/btlmap/<g>/<n>/<n>.ahwin32`, and calls
`FFX_Gfx_FieldMapLoad 0x640210(path, 1)` because section 1 is present. It would call it with 0 for
a section 0 rcBg scene, which no btlmap has. It then calls `sub_6454A0`, which requests the
`fp/tex/TexList.txt` textures. The Phyre clusters load on the **worker**, and the last cluster's
callback `FFX_Gfx_FinishMapClusterSet 0x65A6C0` calls `FFX_Btl_MapLoadEnd 0x782FD0` (at `0x65A72E`
and `0x65A746`), which clears `g_ffxBtlMapLoadPending` [V].

What it reads, in order: the MAP1 file (one ROM read), then the `.ahwin32` header, then each
cluster the header lists (the `dae.phyre`, its `tex/d3d11` textures, `texswap` and `ags.phyre` when
present), then the `fp/tex` texture list. `PS3Data/btlmap/hdao_settings.bin` is not part of this.
It is loaded once at post effect init by `sub_693D70` through the two path table at `off_C3414C`,
next to `PS3Data/map/hdao_settings.bin` [V].

Other paths into the same containers: `FFX_Map_LoadMapSync` (group 26) from Save 049 [V, from
`MAP_SECTIONS.md`]. The dev reader `sub_937960` reads `host:/ffx/proj/map/btlmaster/info/btlmap.csv`
or `master/info/mapid.csv` to name a map id [D].

### 3.1 Timing

The battle start step number depends on two loads. The MAP1 completion is dispatched on the step
thread when the ROM read finishes, and case 34 waits for the worker to finish the Phyre clusters.
Both depend on disk speed, and case 34 rolls the ambush and consumes battle RNG on the step it
releases. This is H1 in `ASYNC_LOAD_REQUESTS.md`, nothing new here, but it applies to every battle
that loads a background, and to none that are staged on the current field map.

## 4. How a battle picks its map

### 4.1 The map id

The encounter gives `FFX_Btl_BeginBattle` a scene index into `btl.bin`
(`ffx_ps2/ffx/master/jppc/battle/kernel/btl.bin`, `CHEAT_BATTLE.md` 13). The scene's zone record
holds the map id at `+1 | +2 << 8`, and `FFX_Btl_BeginBattle` stores it in the low word of
`g_ffxBattleSceneAndMap 0x112C254` [V]. Then:

- bit `0x400` set: a battle map, row `id & 0x3FF` of asset kind 26, the btlmap container
- bit `0x400` clear: a field map id, group 19. If it equals the current scene id (case 7) nothing
  loads and the battle runs on the field map in memory, walkmesh and all

`btlmap.py rows` prints every row with the scenes that use it, and the field map ids used as a
battle background. `btlmap.py survey` counts them: 51 battle map rows and 39 field map ids.

### 4.2 Rows and containers [V data]

The kind 26 path table has 58 rows. Row 0 is empty. Row 38 names `nagi00_b`, which does not ship,
and no btl.bin scene uses row 38. 56 rows name a container that ships.

Five shipped rows are used by no btl.bin scene: 13 `kino05_a`, 19 `kami03_a`, 29 `bika04_a`,
40 `nagi03_a`, 54 `test00_a`. The first four are 448,592 byte files with 3 section 1 records, and
theirs are the only `dae.phyre` scenes that hold just cameras and nodes, so they look like empty
placeholders. `kino03_a` (unreachable, 4.3) and `grid00_a` (row 1, which btl.bin does use) also
have just 3 section 1 records, but `grid00_a` has a real scene. Row 55 `test00_b` is used by a
btl.bin scene despite the name.

### 4.3 Unreachable containers [V data]

`kino03_a` and `mihn05_a` are not named by any row, and they are also the only two containers
with no `ps3data` scene header. Nothing in retail can load them. `mihn05_a` is the one with the
refused version `0x100` section 1.

## 5. Simulation or presentation

| part | call | evidence |
|---|---|---|
| zone 0 walkmesh | **simulation** | below |
| no walkmesh (4 maps) | **simulation**, ground height 0 | 2.4 |
| header `+4` indoor flag | not read in battle | `MAP_SECTIONS.md` 4 |
| section 1 map stream | presentation [I] | part transforms, fog, animation tracks, no reader on the battle logic side was found |
| section 10 placed particles | presentation | ambient effects, `MAP_SECTIONS.md` 7. Their RNG stream is cosmetic, nothing reads it back [I] |
| sections 11 and 12 | presentation | guide map model and particle textures |
| Phyre `.ahwin32`, `dae.phyre`, textures, `texswap`, `ags.phyre` | presentation | drawn only |
| `PCamera*` in the `dae.phyre` | presentation, presumed unused [I] | the battle camera is the battle camera code. The only MAP1 camera is section 0, absent here |
| `hdao_settings.bin` | presentation | ambient occlusion |
| load completion timing | **simulation** | 3.1, H1 |

### 5.1 How the walkmesh reaches the simulation [V]

1. Every CHR, battle units included, steps through `FFX_MainStep` -> `FFX_Ch_UpdateMotionAll
   0x832E10` -> `FFX_Ch_ResolveCollisionsAll 0x83D3B0`. At `0x83D476` it is
   `test byte ptr [esi+194h], 80h`. With `m_flags1 & 0x80` clear it reaches `0x83D49F`
   `call FFX_Ch_WalkmeshMove`, the walkmesh slide that writes `m_groundHeight` (+0x16C) and the
   ground attributes. `FFX_Ch_UpdateMotionAll` then clamps `posY` to `m_groundHeight` by the ground
   mode.
2. `FFX_Btl_GetUnitGroundHeight 0x794FB0` returns `FFX_Ch_GetGroundHeight` of the unit's CHR. Its
   callers:
   - `FFX_AtelSys_Btl_146_GetHeightAboveGround_resi` at `0x7A5F51`, script visible
   - `sub_7A9AC0` at `0x7A9E46` (move target mode 13, placement point with y = ground - height/4)
     and `0x7A9E70` (mode 14, unit position with y = ground). It serves `FFX_Btl_StartUnitMove`,
     `FFX_Btl_StepUnitMove`, Btl 013 and Btl 044
   - `sub_7B78C0` at `0x7B7A0B`, the arc path of a unit move, unless `byte_112C9CE`
   - the battle UI projections `sub_7932B0`, `sub_795250`, `sub_796C30`, and `sub_7DBEE0`
   - the effect side `FFX_Oef2_ExplodeModel`, `FFX_Opp_Kind17`, `FFX_Oef2_Op142`, and the magic
     host API table at `0xC6516C` (index 289)

   Because the btlmaps are flat planes at y = 0 in 50 of 54 cases, the ground height in most battles
   is a constant, but `sins05_a`, `lmyt01_a` and `test00_b` have shaped ground and the four
   walkmesh-less maps give 0. A recreation must load the walkmesh and run the same ground query.

### 5.2 Positions are not in the btlmap [V]

`maybe_FFX_Btl_GetUnitTargetPos 0x7948E0` calls `sub_7ABFE0` with the actor's layout, kind and slot
bytes at `+1276..+1278`. `sub_7ABFE0` loads `dword_112A9B0` at `0x7ABFEA`, which
`FFX_Btl_SetupFieldPointers` sets to the battle field file plus its `+16` section offset.
`SG_DebugWin_BattlePosProc` reads the same table. So the placement points, and the home positions
`sub_784420` puts monsters at, come from the battle field file, not from the btlmap.

## 6. The Phyre files beside a container

`btlmap.py assets` over `ffx_data/gamedata/ps3data/btlmap/`, 2,734 files, classified with Project
Zanarkand's `tools/phyre.py` (set `PZ_TOOLS` if it is not at `e:/Git/projectzanarkand/tools`). Nothing
is copied. All `.phyre` files parse [V data].

| extension | files | bytes | where |
|---|---|---|---|
| `dds.phyre` | 2560 | 685,944,415 | `tex/d3d11` 1945, `fp/tex/d3d11` 309, `tex/texswap/d3d11` 306 |
| `ahwin32` | 60 | 541,649 | map root 56, `2d/` 4 |
| `dae.phyre` | 60 | 146,395,355 | `mdl/d3d11` 56, `2d/mdl/d3d11` 4 |
| `txt` | 43 | 9,362 | `fp/tex/texlist.txt` |
| `ags.phyre` | 10 | 31,297 | `mdl/d3d11/textureanimation.ags.phyre` |
| `bin` | 1 | 3,482 | `hdao_settings.bin` at the btlmap root |

By class:

- `dds.phyre`: 2544 `PTexture2D` and 16 `PTextureCubeMap` (reflection maps). Formats DXT1 1400,
  DXT5 1133, ARGB8 27.
- `dae.phyre`, the battle background scene: `PMesh`, `PMeshInstance`, `PMaterial`, `PNode`,
  `PWorldMatrix`, `PCameraPerspective` and usually `PCameraOrthographic`. 14 also carry a
  `PModifierNetwork` set (morph and dynamic mesh animation).
- 4 `dae.phyre` (`kami03_a`, `kino05_a`, `nagi03_a`, `bika04_a`) hold only cameras and nodes, the
  unused placeholder rows of 4.2.
- 4 `dae.phyre` under `2d/` (`bsil03_a`, `cdsp00_a`, `mtgz07_a`, `stbv00_a`) have no instance lists.
  The `2d/` header is the rcBg scene, which `FFX_Map_LoadPhyreScenesForMapOut` only loads for a
  section 0 map, so these are unreachable [I]. They are the same four maps that carry section 12.
- `ags.phyre`: 10 `PSpriteAnimationInfo`, texture animation.

## 7. For the recreation

- Load the MAP1 with the same reader as a field map. Only sections 1, 2, 10, 11 and 12 occur, and a
  battle needs only section 2 for state.
- Clear the walkmesh before the load, as `FFX_Map_UnloadMapOut` does, so the four walkmesh-less
  maps give ground height 0 rather than the field's ground.
- A battle staged on the current field map keeps the field walkmesh and skips the monster home
  placement of case 10.
- Model the battle start wait as a step count the host decides, not as a disk race (H1).

## 8. What the reader checks

The checks are the ones the exe makes, plus the extents of every table: the MAP1 magic and offsets,
the section 1 magic, version, record headers and the type 1, 5 and 8 sizes, the walkmesh through
`mapfile.Walkmesh` and its own `check()`, every section 10 table and every relocated offset inside
its program, every YN chunk up to `YNED` and the YNFT image sizes. An unclaimed byte, an overlap or
a failed check fails the container and the survey exits 1.

## 9. Proposed names

All placeholders today except `0x7948E0`. `../tools/apply_btlmap_names.py` applies them and was
rehearsed on a copy of the IDB with `--apply --comments`: 26 renamed, 0 failed.

| ea | now | proposed |
|---|---|---|
| 0x643490 | `sub_643490` | `FFX_Gfx_SetBattleMapFlag` |
| 0x643450 | `sub_643450` | `FFX_Gfx_SetMapExpectsBgScene` |
| 0x643470 | `sub_643470` | `FFX_Gfx_SetMapExpects3dScene` |
| 0x908F10 | `sub_908F10` | `FFX_GetMagicDrawFlags` |
| 0x908F40 | `sub_908F40` | `FFX_Map_GetLoadedMapOutRow` |
| 0x90BBE0 | `sub_90BBE0` | `FFX_Map_AllocAligned64` |
| 0x6454A0 | `sub_6454A0` | `FFX_Map_RequestParticleTextures` |
| 0x7816A0 | `sub_7816A0` | `FFX_Btl_GetDbgMapCircle` |
| 0x784420 | `sub_784420` | `FFX_Btl_PlaceMonstersAtHome` |
| 0x7ABFE0 | `sub_7ABFE0` | `FFX_Btl_AccessPlacementPoint` |
| 0x7A9AC0 | `sub_7A9AC0` | `FFX_Btl_ResolveMoveTargetPos` |
| 0x7B78C0 | `sub_7B78C0` | `FFX_Btl_BuildArcPath` |
| 0x795250 | `sub_795250` | `FFX_BtlUi_ProjectUnitToScreen` |
| 0x796C30 | `sub_796C30` | `FFX_BtlUi_ProjectUnitGroundToScreen` |
| 0x7932B0 | `sub_7932B0` | `maybe_FFX_BtlUi_CacheUnitBoneHeight` |
| 0x937960 | `sub_937960` | `FFX_RcDev_ReadMapNameCsv` |
| 0x7948E0 | `maybe_FFX_Btl_GetUnitTargetPos` | `FFX_Btl_GetUnitPlacementPos` |
| 0x112C9DA | `byte_112C9DA` | `g_ffxBtlMapLoadIssued` |
| 0x112C9CE | `byte_112C9CE` | `g_ffxBtlFieldAirborne` |
| 0x112A9B0 | `dword_112A9B0` | `g_ffxBtlFieldPlacementSection` |
| 0x1934ED0 | `dword_1934ED0` | `g_ffxMapOutPendingBlob` |
| 0x1934EE8 | `byte_1934EE8` | `g_ffxMapOutPendingPath` |
| 0x1934EC0 | `dword_1934EC0` | `g_ffxMapSceneKind` |
| 0x1934EC4 | `dword_1934EC4` | `g_ffxMapSceneLoadCount` |
| 0x18DED30 | `dword_18DED30` | `g_ffxMapOutLoadedRow` |
| 0xC3414C | `off_C3414C` | `g_ffxHdaoSettingsPaths` |

The IDB function comment on `FFX_Ch_ResolveCollisionsAll` says free move is `m_flags1` bit
`0x80000000`. The code at `0x83D476` tests `0x80`. The apply script cannot fix a comment, so fix it
by hand.

## 10. Corrections made to other documents

- `MAP_SECTIONS.md` 3: "sub_908600 -> group 19 on the way back" was wrong. Its only caller is
  `FFX_Btl_StepSubPhase` case 8 at `0x7831C2`, at battle start, when the map id is a field map. The
  return to the field is the MAPREG task.
- `MAP_FORMAT.md`: the same claim in the 2026-10-13 correction, the "7 maps have no walkmesh ...
  look like test or title screens" note (four of them are live battle maps), the vertex count row's
  "8 short" (the 8 bytes are `DEWPRISM`), and where the group records sit. Added as a dated
  correction.
- `ffx_types.h` `m_flags1`: "0x80 up-vector override" and "0x80000000 free move / raw anm clip".
  Free move is `0x80` (`0x83D476`, and `CHARACTER_MOTION.md` agrees), `0x80000000` is the raw `.anm`
  clip only.
- Not edited here: Project Zanarkand `MAP.md` says "59 containers". It is 59 files, 58 containers and
  one empty leftover. `VBF_FORMAT.md`'s 59 is a file count and is right.

## 11. Open

- **The YNGM over-read.** `rcbgLoad__f92B2F0` reads 8 or 24 bytes past every YNGM chunk body, and in
  `sins05_a` and `test00_b` 24 bytes past the end of the MAP1 data. Whether the 64 byte aligned
  allocation covers it, and what the 0x130 byte tail really is, was not traced. Harmless in
  practice since the game ships like this, but a recreation should bound the read [V data for the
  sizes, open for the effect].
- **Section 1 in battle.** That it is presentation only is inferred from what the record types do on
  the field side. No battle logic reader was found, but the type 4 and 6 consumers were not traced
  one by one [I].
- **The `PCamera*` objects** in the battle `dae.phyre`. Presumed unused because the battle camera is
  its own code, not read [I].
- **`sub_7DBEE0`**, one caller of `FFX_Btl_GetUnitGroundHeight`, was not identified.
- **`byte_112C9CE`** as "airborne" rests on the arc path skipping the ground snap when it is set and
  on Btl 005 returning it. Which battles set it was not surveyed [I for the name].
