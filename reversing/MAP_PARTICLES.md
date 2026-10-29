# MAP_PARTICLES: the field placed particle set

PROGRESS 1.7.9 in projectzanarkand. The map particles: the data in MAP1 section 10, the placed
emitters, the ppp program VM that runs them, the per-frame update, the RNG it draws, what of it the
step can see, and a port spec for the one effect 6.3.9 needs, the Thunder Plains rain. All addresses
are IDA VAs (RVA + 0x400000). Tags: [V] read in the binary at the address given, [M] measured by a
scan over shipped data, [I] inferred. Only [V] and [M] are proof.

Reader: `tools/mapparticle.py` (`survey`, `census`, `show <map> [programs]`, `rain`). It parses in
memory and writes nothing. Names: `tools/apply_mapparticle_names.py`. The doc uses those names and
gives the old IDB name the first time.

Related: `MAP_SECTIONS.md` 7 (where section 10 sits), `YONISHI_ENGINE.md` (the ppp operators and
the Oef2 VM around them), `RCFLEX.md` (pppDrawMdlPSim), `RCBG.md` (the FP objects that drive groups
on four scenes, and the section 12 particle textures), `ATEL_MISC_LIBRARIES.md` 3 (the 108 MapFx
rows), `THREADS_AND_CLOCKS.md` R3, R4 and R5 (the effect RNG and the CRT stream).

## 1. Summary

- **What the names mean.** `mpfp` is the script side name ("map FP", FP = field particle, `RCBG.md`
  3), only surviving in the debug string `"**** mpfpbindpos id:%d pos:%f,%f,%f"` at 0xB675D0 used by
  `FFX_AtelOp_MapParticleBindPos 0x91B0A0` (MapFx 10). `FFX_MapParticle_*` at 0x909120..0x9092E0
  are the PS2 dev loaders for loose `host0:/ffx/proj/map/f_particle/m%03d.pdt` and `b%03d.pdt`
  pairs, reached only from `save:11` and `save:12`. The retail path loads the same format from MAP1
  section 10. The 108 MapFx syscalls are the ATEL library. The code that runs the set is
  `FFX_MapFx_*` at 0x728A60..0x72AC2F plus the shared ppp core at 0x7120F0..0x717400. [V]
- **The format is fully mapped.** 317 sets (274 field, 43 battle), all parsed to the last byte, 0
  unclaimed bytes. 5,910 placed emitters, 2,934 programs, 11,810 nodes, 107,885 operator entries,
  112 of the 145 operators used. [M] Section 3.
- **The update runs on the step thread, once per sub step**, from `FFX_MainStep` at 0x82148C, inside
  the effects pass loop `k < max(FFX_Player__getSubStepCount(), 1)` (0x821400). It is not gated on
  `g_ffxIsCatchUpStep`. Only the final model submission is (0x6458B2). [V] Section 5.
- **The clock is a fixed 4096 per pass**, the start argument at 0x729C99, so one particle "frame" per
  sub step, never wall time. `g_ffxVfxTimeFrozen 0x230FD34` stops the clocks and most Calc ops, but not
  the rain's motion. [V]
- **RNG.** Map particles draw the cosmetic effect state `g_ffxEffectRandState 0xC0A000`, and the
  `pppRand*` ops also `srand()` the step thread's CRT stream from it. The number of draws depends on
  the camera (frustum culled emitters pause, the rain respawns drops that leave a cube around the
  camera). [V] Section 6.
- **It feeds the step, a little.** ATEL waits on group state (MapFx 4, 5), rcBg FP objects read group
  work, the point light commit writes every live CHR's +0x188, and through `srand` the camera
  dependent draw count sets where the step thread's CRT stream stands, and the one step thread
  reader that can reach the simulation is the rcBg scene shake (R14). [V chain, I that it matters]
  Section 7. (Corrected 2026-10-28: R5 runs on a worker with its own seed, see item 4.)
- **Thunder Plains rain is the ppp operator `pppDrawRain` (op 97)** in kami00, kami00_a, kami03 and
  kami04, 7 entries. It is a camera centred cube of line streaks, world space, 500 to 2000 drops, no
  texture, not culled. kami01 and kami02 have no rain operator. [V, M] Section 9 has the port spec.

## 2. Code map

| range | what | anchor |
|---|---|---|
| 0x7120F0..0x717400 | ppp core: relocate, start, step, birth, calc pass, free | shared with the magic PM effects (`FFX_Vfx_StepAll 0x762240`), many are magic host API rows |
| 0x71A0C0, 0x7112D0, 0x714B60 | the dynamic line model the rain draws into | callers in `pppDrawRain__f755430` |
| 0x728A60..0x72AC2F | MapFx glue, placed set lifetime, group API, cull, transforms, point light commit | `"pppFpLoop : Packet Error!!"` at 0x729E1E |
| 0x72C190..0x72CCF0 | `PppCore`, ppp heap, rotation matrices | `YONISHI_ENGINE.md` |
| 0x72F4B0..0x762460 | the 145 ppp operators | `g_ffxPppOpTable 0xC3A508` |
| 0x9083E0 | `FFX_MapFx_StepPlacedEffects` | called from `FFX_MainStep` 0x82148C |
| 0x909120..0x9092E0 | `FFX_MapParticle_*` dev loaders | `host0:` strings 0xB66F24..0xB66FA8 |
| 0x91AF30..0x91C8AF | the MapFx ATEL library | `ATEL_MISC_LIBRARIES.md` 3 |

Load: `FFX_MapFx_LoadParticleSet 0x72A7F0` from `FFX_Map_LoadMapOutSync` 0x9095DD,
`FFX_Map_OnMapOutLoaded` 0x909905 and the dev `FFX_MapParticle_LoadPair` 0x90929C [V]. It sets
`g_ffxMapFxActive 0xCEC1F0` (was `dword_CEC1F0`) = 1 (0x72A800), stores the set in
`g_ffxMapFxSet 0xD2C258`, relocates it (`FFX_Vfx_RelocateParticleSet 0x711FA0` ->
`FFX_Ppp_RelocateProgram 0x7120F0` per program), points the ppp heap at the set's three tables
(`FFX_Ppp_InitHeapForSet 0x7169D0`), allocates the extra heap and the packet buffer once, copies each
placed record into its runtime slot and checks the special cases (section 4.1). Teardown is
`FFX_MapFx_ReleaseSet 0x7291C0` (was `MapFx__f7291C0`), from `FFX_Map_UnloadMapOut` 0x909F0F and the
dev loader. [V]

## 3. The data format

Section 10 of `mapout.vpa`, the PS2 `.pdt` format. Every offset in a table is relative to the set,
every offset inside a program is relative to the program. `FFX_Vfx_RelocateParticleSet` turns them
into PrxPtrs in place. All [V] at the reader named, the shipped values [M].

### 3.1 Header, 32 bytes

| off | type | field | reader | shipped |
|---|---|---|---|---|
| +0 | u8 | format | not read | `'2'` in 312, `'1'` in 5 |
| +1 | u8 | extra heap, 8 KB blocks | 0x72A7F0, `n << 13`, a second heap at `unk_CEC23C` the allocator falls back to | 0 in 277, max 152 |
| +2 | u8 | packet buffer class: 1 -> 1 MB, 2 -> 1.25 MB, anything else 768 KB | 0x72A9E4 | 0 in 294 |
| +3 | u8 | | not read | 0 |
| +4 | u16 | placed record count | every MapFx loop | 0..113 |
| +6 | u16 | program count | 0x711FA0 | |
| +8 | u16 | model count, 32 byte records, offsets at +20/+24/+28 of each | 0x711FA0, heap +32 for the Mdl draw ops | |
| +10 | u16 | shape count, u32 offsets | heap +36 for the Shape ops | |
| +12 | u16 | vertex list count, 8 bytes `{s16 model, s16 count, u32 offset}` | heap +40 for the VertexAp, KeBornRnd and PSim ops | |
| +14 | u16 | | not read | 0 |
| +16..+28 | u32 x4 | table offsets: programs, models, shapes, vertex lists | 0x711FA0 | |

### 3.2 Placed record, 80 bytes from +32

Copied into the runtime slot by 0x72A7F0 (slot +64 = record +0 and so on).

| off | type | field | shipped |
|---|---|---|---|
| +0 | vec4 | position | |
| +16 | s32 x4 | rotation angles, 65536 per turn (`FFX_Ppp_RotMatrixZ` indexes `flt_C44BF0` by `(a >> 4) & 0xFFF`) | |
| +32 | vec4 | scale | |
| +48 | s32 | initial slot +0. 0 starts on the first update, -4096 (0xFFFFF000) starts dead and waits for a script | 0 in 4039, -4096 in 1871 |
| +52 | u32 | program index | |
| +56 | f32 | cull far distance, negative never culls | negative in 2249 |
| +60, +64 | f32 | cull half width and height | |
| +68 | u16 | group. 0 means ungrouped and frustum culled | 0 in 3029 |
| +70 | u16 | second group word (MapFx 96, `FFX_MapFx_SuspendGroup2` etc.) | |
| +72 | u16 | not read | |
| +74 | u8 | view mode, `FFX_MapFx_ApplyViewMode 0x72A260`: 0 view x M, 1 camera aligned, 2 a shared basis from `FFX_MapFx_BuildSharedBasis 0x72A690`, and any 2 sets `g_ffxMapFxNeedSharedBasis 0xD2C25C` (0x72A97F) | 0 in 5827, 1 in 77 |
| +75 | u8 | rotation order, `FFX_MapFx_RecordRotScaleMatrix 0x72A2E0`: 0 XYZ, 1 XZY, 2 YXZ, 3 YZX, 4 ZXY, 5 ZYX, else XYZ | 0 in 5893 |
| +76 | u32 | not read | 0 in 5902 |

azit02 and mcfr10 carry stale bytes in +70..+79 of some records [M], harmless because +72 and +76
are never read and the odd +74/+75 values fall into the defaults.

### 3.3 Program

| off | field |
|---|---|
| +0 | end time (4096 per pass) |
| +4 | u8 loop. 1 in 2740, 0 in 194 [M] |
| +5..+7 | 0 |
| +8 | op list offset: `{u32 n, n x u32 op index}`, the ops used, drives the Init, PrePass and PostPass hooks |
| +12 | node list offset: `{u32 n, n x u32 node offset}`, always equal to the chain [M] |
| +16 | first node, then the chain through node +0 |

The chain ends at a node whose `next` points at a zero dword, which always sits right before the op
list [M, all 2934]. `FFX_Ppp_RelocateProgram 0x7120F0` stops on exactly that test.

### 3.4 Node, 40 bytes, then 16 byte entries

| off | field | shipped |
|---|---|---|
| +0 | next node offset | |
| +4, +8 | offsets or a float, no runtime read found | |
| +12 | flags. Bit 0 skips every Draw of the node (`FFX_Ppp_CalcPassChecked 0x717010`) | 0x20 in 5834, 0x21 in 1002 |
| +16 | birth time, 0x7FFFFFFF never | 0 in 8533, never in 3067 |
| +20 | life, 0x70000000 means infinite | infinite in 395 |
| +24 | loop_to | |
| +28 | loop_at | |
| +32 | offset of the key pointer table inside the particle, so particle size = this + 4 x op count | |
| +36 | s16 draw order (particle list insertion order, 0x716770) | |
| +38 | s16 entry count | |

Entry: `+0 op index, +4 u16 key stride, +6 u16 key count, +8 key offset, +12 descriptor offset`.
Keys start with a u32 time and the run ends with the sentinel 0xFFFFF000 [M, all 107,885]. The
descriptor sits right after the sentinel [M, all] and holds the work offsets the op reads, desc[0],
desc[1] and so on, length op specific. Work offsets are relative to particle +160.

### 3.5 Survey

`python -I tools/mapparticle.py survey` [M]:

    section 10 sets : 317 (274 field, 43 battle), 317 parse to the last byte, 0 FAIL
    bytes           : 23363664 = 7691724 sized + 15596104 bounded + 73692 zero pad + 2144 stale + 0 unclaimed
    placed records  : 5910, programs 2934, nodes 11810, entries 107885

Sized regions have an extent the exe reads (keys 4,582,636, nodes 2,198,560, placed 472,800, op lists
177,360). Bounded regions are blobs from a referenced offset to the next one: model data 11,811,748,
shape data 1,664,256, descriptors 1,020,580, node +4 data 914,868, vertex list data 184,652. Those
are read by the Mdl, Shape and VertexAp ops and are not decoded field by field here. The 2,144 stale
bytes are alignment gaps under 16 bytes between a node's entries and its first keys. The checks the
survey makes: the sentinel, descriptor right after it, node list equals chain, op list equals the set
of ops used, header +5..+7 zero. `census` has the full op histogram, 112 of 145 used, 33 never.

## 4. Runtime state

### 4.1 Globals

| address | name | role |
|---|---|---|
| 0xD2C258 | `g_ffxMapFxSet` | the loaded set, 0 when none |
| 0xD2C260 | `g_ffxMapFxSlots` | one 128 byte slot per placed record |
| 0xD30260 | `g_ffxMapFxAttach` | one 76 byte attach record per placed record |
| 0xCEC1F0 | `g_ffxMapFxActive` | the update's first gate. 1 on load, MapFx 12, core 550, `sub_933CC0` |
| 0xD2C25C | `g_ffxMapFxNeedSharedBasis` | some record has view mode 2 |
| 0xCEC228 | `g_ffxMapFxFilterQuirk` | set when the placed count is exactly 40 or the level hash is 564 (0x72A9CA), read only by `pppDrawFilter__f756740` |
| 0xC3A504 | `g_ffxMapFxCurrentRecord` | record index during the update, -1 outside |
| 0xC3A500 | `g_ffxMapFxCurrentGroup` | record +68 during its run, read by the Mdl draw ops |
| 0xCEC22C | `g_ffxMapFxHeap` | the ppp heap the set runs in |
| 0xCEC220, 0xCEC200 | `g_ffxMapFxPacketBuffer`, `g_ffxMapFxPacketSize` | the draw packet buffer |
| 0xCEC204, 0xCEC208 | `g_ffxMapFxPacketUsed`, `g_ffxMapFxPacketPeak` | debug meters |
| 0xCEC20C, 0xCEC210 | `g_ffxMapFxHeapUsed`, `g_ffxMapFxHeapPeak` | debug meters |
| 0xCEC224 | `g_ffxMapFxExtraHeapBlock` | header +1 allocation |
| 0xCEC1F4, 0xCEC1F8, 0xCEC1FC | `g_ffxVfxPointLightCount`, `g_ffxVfxPointLightCapacity`, `g_ffxVfxPointLightMask` | shared with the magic effects, section 7 |
| 0x230FFE4, 0x230FD20 | `g_ffxPppCurrentPart`, `g_ffxPppCurrentOpIndex` | the slot and op being run |
| 0x230FD3C | `g_ffxPppAllocFailed` | set by the particle allocator, ends the slot |
| 0x230FD2C | `g_ffxVfxPacketCursor` | packet write cursor, shared with `FFX_Vfx_StepAll` |

### 4.2 Slot, 128 bytes (`FFX_Ppp_StartPart 0x7123C0` fills it)

+0 countdown or state (0xFFFFF000 dead, >= 0 counting down to a restart, other negatives running),
+4 end time, +8 time, +12 previous time, +16 dt (4096), +20 op list count, +24 node count, +28 loop
byte, +29 mode (0 run, 1 stopping, 2 one shot), +32..+40 the cull values, +44 first node, +48
particle list, +52 op list, +56 node list, +60 node states (16 bytes each: node, first particle,
birth time, u16 live count, u8 index), +64..+108 the record transform, +112..+118 u16 x4 colour
scale (4096 = 1.0), +120. [V 0x7123C0, 0x72A7F0]

### 4.3 Attach record, 76 bytes

+0 flags (1 CHR, 2 map part, 8 map node, 0x10 suspended), +4 target, +8 joint, +12 matrix, +60..+68
position. Written by `FFX_MapFx_BindGroupToChr 0x72A530`, `..ToMapPart 0x72A590`, `..ToMapNode
0x72A5E0`, `FFX_MapFx_SetGroupPos 0x72A630`, cleared by `FFX_MapFx_UnbindGroup 0x72A120` (keeps
0x10). [V]

### 4.4 Particle

+0 next, +4 parent, +8 node state, +12 age, +16 local matrix, +64 translation, +80 model view
matrix, +144 and +148 PrxPtrs, +156 byte, work from +160, the key pointer table at node +32. Allocated
by `FFX_Ppp_BirthParticle 0x716770` from the current heap, falling back to the extra heap, inserted
by draw order, then Con on every entry (`FFX_Ppp_ConParticle 0x712D90`). [V]

## 5. The per-frame path

### 5.1 Call chain and gates, in order

| step | address | test | sense |
|---|---|---|---|
| `FFX_MainStep` | 0x8213D6 | `g_ffxEffectsPassEnabled` | 0 skips the whole effects pass |
| | 0x8213E3 | `g_ffxMovieScreenModePrev` | nonzero skips |
| | 0x8213F0 | `g_ffxScriptEffectsPassOff` | nonzero skips |
| | 0x821400..0x821417 | `k < max(FFX_Player__getSubStepCount(), 1)` | one pass per sub step |
| | 0x82147C | `g_ffxMapFPPassEnabled` | 0 skips. Set 1 in `FFX_MainInit` 0x8209E3, only the debug GUI changes it |
| | 0x82148C | call `FFX_MapFx_StepPlacedEffects` between `FFX_Glue_SetDrawLayer(1)` and `(4)` | |
| `FFX_MapFx_StepPlacedEffects` | 0x9083E0 | `g_ffxMapOutSection10 != 0` and `g_ffxMagicDrawFlags & 1` (0x9083E9) | bit 0x1 is set by the map loads (`or 0x11` at 0x909625, 0x9099BE), cleared and set by Oef2 op 144 (0x80A094, 0x80A00B) |
| `FFX_MapFx_UpdatePlacedEffects` | 0x729AC0 | `g_ffxMapFxActive` | 0 skips |

Then 0x729ACD..0x729B39 fetches the camera projection and view into `g_ffxVfxProjMatrix` and
`g_ffxVfxViewMatrix`, builds the inverse, builds the shared basis when needed, sets the packet cursor
and the heap, and zeroes the point light count and mask. [V]

### 5.2 Per placed record (loop 0x729B50..0x729DE2)

1. Attach flag 0x10 (test at 0x729B65): suspended. `FFX_MapFx_UnlinkSlotMeshes 0x728E40` and skip.
2. Slot +0 == 0xFFFFF000 (0x729B78): dead, skip.
3. Group 0 (0x729B9E): `FFX_MapFx_FrustumCull 0x728F10` on the record position and +56/+60/+64. It
   returns 0 for visible (and always when the far value is negative), 15 when past far or behind,
   else the AND of four corner outcodes against x +-0.889 and y +-0.5 after the divide. A culled
   record is skipped, so it pauses, its time does not advance. Exceptions (0x729BD5..0x729C29): event
   310 runs every culled record except 2, 6 and 10, event 49 runs culled record 38 while story
   progress <= 0x2C, event 374 runs culled record 13. [V]
4. `g_ffxMapFxCurrentGroup` = group (0x729C33).
5. Countdown (0x729C38): if slot +0 >= 0, decrement. Still >= 0, skip this pass. Went negative:
   free the prim list and `FFX_Ppp_StartPart(slot, 4096, program, 1)` (0x729C9F), and if +29 was 2
   set it to 1, so a one shot plays one cycle and then stops. So a countdown of 0 means "restart on
   the next pass", and every script restart works by writing 0. [V]
6. `FFX_MapFx_BuildRecordMatrix 0x728AC0`: rotation order, scale, attach to a CHR joint (x 1000), a
   map part or a map node, then the view mode.
7. Colour scale +112..+118 = min(`maybe_FFX_MagicDraw_GetVec4`, 1.0) x 4096 per channel
   (0x729CC0..0x729D9E). This is how a script fade dims every map effect.
8. `FFX_Ppp_StepPartChecked(slot, 0x30)` (0x729DB0, was `sub_7122F0`). True: free the prim list and
   set +0 = 0xFFFFF000 (0x729DCD). [V]

After the loop `FFX_MapFx_CommitPointLights 0x729F90` and the debug meters (0x729DED..0x729EEB,
the packet overflow message only prints). [V]

### 5.3 One slot pass, `FFX_Ppp_StepPartChecked 0x7122F0`

PrePass hooks (flag 0x10, `FFX_Ppp_RunPrePass 0x712E80`), `FFX_Ppp_BirthNodes 0x712B80`,
`FFX_Ppp_CalcPassChecked 0x717010`, PostPass hooks (0x20, `FFX_Ppp_RunPostPass 0x712E00`). If an
allocation failed the slot is done. Unless `g_ffxVfxTimeFrozen` (0x712362), time +12 = old, +8
+= dt. Non looping: done when time >= end. Looping: while stopping, done when the particle list is
empty, otherwise the time clamps at 0x70000000. The magic PM effects use the unchecked twin
`FFX_Ppp_StepPart 0x712250` and `FFX_Ppp_CalcPass 0x716C40`. [V]

`FFX_Ppp_BirthNodes`: one particle per node whose birth time <= slot time, then the node's birth
time becomes 0x7FFFFFFF. Emitter ops (`pppPointAp`, `pppKeBornRnd*`, the `pppVertexAp*` helpers)
call `FFX_Ppp_BirthParticle` for further particles. [V]

`FFX_Ppp_CalcPassChecked`, per node state, per particle, per entry: the Group column, or the key
pointer advance (when the next key's time equals the age, move to it), then Calc, then Draw unless
node +12 bit 0. Then age += 4096 unless frozen. Then lifetime: not stopping and age >= loop_at gives
age = loop_to and ReCon (`FFX_Ppp_ReConParticle 0x712D20`, which also resets the key pointers),
else age >= life frees the particle (`FFX_Ppp_FreeParticle 0x7168F0`, Des columns). Stopping with
every life infinite frees all. [V]

Operator calling convention: `op(particle, current key, entry)`, desc from `*(entry + 12)`. The
colour family as read [V 0x75D1F0, 0x75C390, 0x75BA40]: when key time == age, `pppColor` adds the
key's s16 x4 at key +8 to the colour at work + desc[0]. `pppColMove` adds the key into the velocity
at desc[1], then colour (desc[0]) += velocity every pass. `pppColAccele` is the same one level up.
All 16 bit wrapping. Their Con and ReCon zero the state from the emulated zero register (0x75D240).
Most Calcs return early while `g_ffxVfxTimeFrozen`.

### 5.4 Clock

dt is the constant 4096 passed at 0x729C99, and every key, life and loop value counts in those
units, so map particles advance one PS2 frame per sub step whatever the display rate. No timer, no
frame delta. `g_ffxVfxTimeFrozen` is also host data slot 758 for the magic DLLs and MapFx 102
(`FFX_Vfx_SetTimeFrozen 0x72A750`). [V]

### 5.5 Thread and catch-up

Everything above runs on the step thread inside `FFX_MainStep` [V]. Nothing on the path tests
`g_ffxIsCatchUpStep`. The draw ops build and update their dynamic geometry during the pass, and
only `FFX_Vfx_SubmitModel 0x6458A0` checks catch-up (0x6458B2) before handing the model to the
renderer. So a catch-up step advances particles and draws RNG exactly like a normal step and only
drops the submit. [V]

## 6. RNG

- **`g_ffxEffectRandState 0xC0A000`**, the cosmetic float generator. Draws: `FFX_Rand_SignedScale
  0x7E6700` (a x [-1, 1)), `FFX_Rand_Scale 0x7E6780` (a x [0, 1)), `FFX_Rand_EffectFloat1to2
  0x72F420`. The step at 0x7E6714: bits r = 0x3F800000 | (s & 0x3FFFFF) << 1 | ((s ^ (s >> 18)) >> 4
  & 1), as a float in [1, 2). SignedScale: v = 2r, state = float(v - 1.887241363525391), returns
  (v - 3) x a. Scale: v = r - 1, state = float(v - 1.887241363525391), returns v x a. Until
  `g_ffxEffectRandInited 0xC8F728` is 1 the r is 1.0. [V]
- **`srand(g_ffxEffectRandState & 0x7FFFFF)`** in `pppRandFV_Calc` (0x730B31, 0x730BD0),
  `pppRandIV_Calc` (0x731181, 0x731220) and the other ops in `THREADS_AND_CLOCKS.md` R4. They first
  xor the state with `0x7FFFFF & (qword_C8F508 - 1)`, the emulated VU zero register minus one, so
  0x7FFFFF when that register holds 0 [V the code, I the register value]. pppRandFV, pppRandIV and pppSRandFV are in 113, 207 and 203 of the 317
  sets [M]. That reseeds the step thread's CRT `rand()`. [V]
- Who draws in a map set: the pppRand* ops, the emitters, and the rain (7 SignedScale per drop at
  create and per respawn, section 9.3). The count per pass depends on which group 0 records the
  frustum lets run and how many drops the camera motion pushes out of the rain cube. [V code, I that
  it varies in practice, it obviously does for the rain]

## 7. What the step can read

1. **ATEL waits.** MapFx 4 and 5 poll `FFX_MapFx_GetGroupState 0x72A760` (-1 no such group, 0 dead,
   1 running, 2 stopping or one shot). A script that waits for an effect to finish blocks on the
   particle clock. Deterministic given the same step sequence, because the clock is the 4096 per
   sub step, but a group 0 record pauses while culled, so a wait on a culled record would depend on
   the camera. Grouped records are never culled (0x729B9E), and the waits take a group, so the
   waits are camera independent. [V]
2. **rcBg FP objects** read group op work (`FFX_RcBgFP_GetGroupOpWork 0x923100`) and suspend and
   resume groups on scenes 97, 173, 565, 566 (`RCBG.md` 3, 4.5). Presentation in effect. [V there]
3. **Point lights.** `pppFpPointLight*` Calcs write lights into the shared pool and OR a bit into
   `g_ffxVfxPointLightMask`. `FFX_MapFx_CommitPointLights 0x729F90` grows the pool, disables the
   unused tail and writes the mask into +0x188 of every live CHR (`sub_830AE0`). Then
   `FFX_Vfx_StepAll 0x762240`, right after at 0x8214A2, resets the count and mask
   (`FFX_MapFx_ResetPointLights 0x729F70` at its start) and commits again at its end, so when its
   slot loop runs (`g_ffxVfxSlotLoopOff == 0`) the map lights end each pass disabled unless the magic
   effects re-add them. [V for the order, I for what reaches the screen, see open item 2]. CHR +0x188
   is render input, nothing in the step was found reading it [I].
4. **The CRT stream.** Section 6. The R4 `srand` calls set the step thread's CRT state from an
   effect state whose draw count depends on the camera. Corrected 2026-10-28: R5
   (`sub_822DE0`) does NOT read that stream. It runs in `FFX_Ch_JointOverrideJob`, posted to a
   worker through `JobSchedule__postJob2OnWorker` (0xA44B56), and MSVCR110 keeps the seed per
   thread. The step thread reader left is the rcBg scene shake at 0x921172 (R14), which only draws
   during a MapFx 038 shake with a random range. So peers with different cameras drift through map
   particles only on maps that run such a shake. [V for each link, I that the shake reaches the
   stick direction]
5. **Group save and restore.** `FFX_MapFx_SaveGroupStates 0x72A180` (from
   `maybe_FFX_Atel_ReleaseAllActorChrs` 0x865E04, into `dword_1326BE0`) and
   `FFX_MapFx_RestoreGroupStates 0x729A10` (from `FFX_Field_MapRegLoadTask` 0x86171E and `sub_8615B0`)
   carry each group's running and suspended bits across a reload. [V]
6. **Heap exhaustion** ends a program early (`g_ffxPppAllocFailed`), so a peer with a fuller ppp heap
   could see a group finish sooner and release an ATEL wait sooner [I, no shipped case known].

Not read by the step: positions, colours, particle counts, model output. [V by the absence of
readers outside the ppp, MapFx and FP code, I as a whole]

## 8. Script control

| MapFx row | function | effect on the slots of the group |
|---|---|---|
| 2 restart | `FFX_MapFx_ResetGroup 0x729700` | +0 = 0, +29 = 0, restart next pass |
| 2 stop | `FFX_MapFx_StopGroup 0x729660` (was `MapFx__f729660`) | +29 = 1, finish the cycle and free |
| 3 | `FFX_MapFx_StartGroupOneShot 0x729810` | +0 = 0, +29 = 2 |
| 4, 5 | `FFX_MapFx_GetGroupState 0x72A760` | read only |
| 7 | `FFX_MapFx_RestartAllGroups 0x729740` | +0 = 0, +29 = 0 for every grouped record |
| 8 | `FFX_MapFx_KillAllGrouped 0x7296A0` | free and +0 = 0xFFFFF000 for every grouped record |
| 9, 88, 89, 105, 10, 91, 11, 90 | bind, set position, unbind (4.3) | attach record |
| 12 | `FFX_MapFx_SetActive 0x729050` | the update's master gate |
| 13 | `FFX_MapFx_SuspendGroup 0x729570`, `FFX_MapFx_ResumeGroup 0x7295B0` | attach flag 0x10 |
| 53 | `FFX_MapFx_RestartGroup2 0x729530`, `FFX_MapFx_StopGroup2 0x7294F0` | mapfx:2 on record +70 |
| 54 | `FFX_MapFx_ResumeGroup2 0x729440`, `FFX_MapFx_SuspendGroup2 0x729400` | mapfx:13 on record +70, nonzero resumes (0x91B7F0) |
| 93 | `FFX_MapFx_ZeroAllCountdowns 0x7297E0` | +0 = 0 on every record, which RESTARTS all, dead ones included |
| 95 | `FFX_MapFx_KillGroup 0x7295F0` | free and dead |
| 96 | `FFX_MapFx_KillGroup2 0x729480` | the same on record +70 |
| 102 | `FFX_Vfx_SetTimeFrozen 0x72A750` | the freeze flag |

Other callers [V xrefs]: the rcbgFP objects at 0x9223B0..0x922E60 restart, stop, suspend, resume and
bind groups. `FFX_MapFx_ShowIndoorGroups` 0x8682BB..0x8682CB uses reset, resume and suspend (the
indoor flag, `MAP_SECTIONS.md` 4). `FFX_Field_FreezeActorsToScratch` 0x866391 suspends and
`FFX_Atel_RestoreActorFromScratch` 0x862F9B resumes. `sub_933CC0` is the rcui debug editor.

## 9. Thunder Plains rain

### 9.1 Where

Thunder Plains is the `kami` family. `pppDrawRain` (op 97) has exactly 7 entries in 4 sets [M]:

| map | record | group | start | program | nodes (drops, cube half size, life) |
|---|---|---|---|---|---|
| kami00 | 20 | 15 | autostart (+48 = 0), never culled | 12, end 30, loop | node 0: 500, 100, 15, **hidden** (flags 0x21). node 1: 1000, 200, 30, drawn |
| kami00_a | 0 | 0 | autostart, cull far 10000 | 0 | one node: 600, 200, 30, drawn |
| kami03 | 30 | 22 | autostart, never culled | 14 | 500, 100, 15 and 1000, 200, 30, both drawn |
| kami04 | 0 | 0 | autostart, never culled | 0 | 500, 100, 15 and 2000, 200, 30, both drawn |

Velocity (2, 10, 1) with jitter (0.5, 4, 0.2) everywhere except kami04, which falls straight, (0, 10,
0) with (0, 4, 0). Streak 16 for the small cube and 20 for the large, jitter 8. FFX is Y down, so +Y
is falling. [M from `mapparticle.py rain`]

What starts it: nothing, all four autostart on the first update after load. kami0000 never names
group 15 [M from the script]. kami0300 runs mapfx:3(22) and then mapfx:4(22), a one shot restart
and a wait, so the kami03 rain is restarted by the event as a one shot and stops after one more
cycle [M script, V for the slot logic]. kami01 and kami02 have no rain op [M]. If those maps show
rain it is from somewhere else (rcBg is the candidate, `RCBG.md`, not checked here).

### 9.2 The operator

Columns [V `g_ffxPppOpTable` row 97]: Con `pppDrawRain_Con 0x756240` (clears the drop pointer at
work + desc[1]), Des 0x756270, Draw `pppDrawRain_Draw 0x7562C0`. No Calc, no ReCon, so the drops
survive the node's loop. Draw: on the first call `pppDrawRain_CreateDrops 0x756320` (was
`pppDrawRain__f756320`), then every call `pppDrawRain_StepDrops 0x755A00` and
`pppDrawRain_BuildLines 0x755430` (0x7562F1..0x756308). [V]

Key, 64 bytes [V reads in 0x756320 and 0x755A00]:

| off | field |
|---|---|
| +0 | time |
| +4 | f32 R, cube half size |
| +8 | u32 N, drop count |
| +16 | vec3 base velocity |
| +32 | vec3 velocity jitter |
| +48 | f32 streak length |
| +52 | f32 streak jitter |

Drop, 48 bytes on the ppp heap: position vec4, velocity vec4, tail vec4. [V]

### 9.3 Per pass, exactly

Create (0x7563C7..0x75649C), per drop, 7 draws from `g_ffxEffectRandState`:

    cam = translation of inverse(g_ffxVfxViewMatrix)               (j_FFX_Mtx_InverseRigid 0x756345)
    pos = cam + (SignedScale(R), SignedScale(R), SignedScale(R)), w 1
    vel = base + (SignedScale(jx), SignedScale(jy), SignedScale(jz)), w 0   (sub_766F10 with a3 = 0)
    tail = normalize(vel) * -(streak + SignedScale(streakJitter))

Step (0x755A00), every Draw call, not dt scaled and not frozen gated:

    pos += vel
    end = pos + tail
    for each of the six faces of the cube cam +- R:
        if pos and end are both past that face, respawn the drop as in create (7 draws)

The first face test is at 0x755C1D..0x755C52 (x below cam.x - R, strict), and the six respawn blocks
start at 0x755C61, 0x755D57, 0x755E4F, 0x755F4B, 0x756041, 0x756137. A respawn picks a fresh random
point in the whole cube, not the opposite face. [V]

Build (0x755430): a line list, one line per drop, head = pos and tail end = pos + tail, identity
model matrix, world space. Colour = (particle s16 colour x slot u16 scale) >> 19 per channel, head
alpha from the particle, tail alpha 0. `sub_71A0C0` fills the line model, rgb x 1/256 and alpha / 256
(`flt_C3A4D0` = 256.0), capped at 2500 lines. `sub_766B80` and `sub_62FC90` are return-0 stubs, so
nothing is culled. The model is created once through `sub_7112D0` -> `sub_714B60(key, 2500)`, keyed
by particle address plus op index, and submitted with `FFX_Vfx_SubmitModel` (catch-up gated). [V]

The colour comes from the generic ops of the same node: `pppColAccele`, `pppColMove`, `pppColor`
(section 5.3). In every rain node the rgb key is 0x4000 and only alpha moves [M]. Run through those
ops, rgb is 128 and alpha cycles with the node's loop [I, computed from the keys]:

    kami00 node 1, 30 passes: 17 18 20 21 22 22 23 23 24 24 24 23 23 23 22 21 20 19 18 17 17 16 16 15 15 15 15 15 15 16
    kami04 node 1, 30 passes: 50 52 54 56 57 58 58 58 58 58 57 55 53 51 49 46 43 40 37 36 34 34 33 34 35 36 38 41 44 48

So kami04, the storm map, is two to three times as opaque as kami00 and pulses slowly.

### 9.4 What the port needs

1. Section 10 parsed far enough to find the rain entries, or a table keyed by map with the values in
   9.1. The rest of the set can stay unported for 6.3.9.
2. The generic node loop: birth at 0, age 4096 per sub step, loop at loop_at back to loop_to with
   ReCon, Draw skipped for flag bit 0. Without it kami00 draws its hidden node.
3. The three colour ops on the s16 x4 state, with 16 bit wrap and the zero on Con and ReCon.
4. The drop pool, create once, step and respawn every pass from the camera of that pass.
5. A line list renderer, world space, 2500 lines per model, head alpha to tail alpha 0.
6. The effect RNG, bit exact if the port wants the same drops, from `g_ffxEffectRandState`. The drop
   count and respawns follow the camera, so per the Zanarkand contract this belongs to the
   presentation side and must not share a stream with anything hashed.
7. The slot gates: dead, suspended, the group 0 frustum cull (kami00_a's rain record has far 10000),
   the colour scale from the script fade, and the time freeze for the colour ops only.

## 10. Names

`tools/apply_mapparticle_names.py`: the MapFx glue from positional `MapFx__f*` names, the ppp core
`sub_*` functions, the rain helpers, and the globals in 4.1. Plus one rename in the MapFx library,
`FFX_AtelSys_MapFx_093_KillAllEffectsAndResetLayerFrames_resi` -> `..._093_RestartAllEffectsAndResetLayerFrames_resi`
(0x91C4A0), because zeroing the countdown restarts (5.2 step 5), and
`FFX_AtelSys_MapFx_054_SetGroup2Hidden_resi` -> `..._054_SetGroup2Active_resi` (0x91B7D0), the same
backwards sense RCFLEX.md 7 fixed on row 13: a nonzero second argument resumes (0x91B7F0..0x91B7F4). `MapFx__f728A60` (a vec3 scale used
by `sub_88A040`), `MapFx__f72AC20` (returns 0xFFFF for the ppp model code) and `MapFx__f72AC30` (a ppp
core init from `FFX_MainInit`) sit in the range by layout but are not map particle code, they are
left alone.

## 11. Corrections made to other docs

- `MAP_SECTIONS.md` 7: the placed record is not twelve floats, +16..+28 are s32 angles and +48 is the
  s32 start state. And the step does read some particle state back (section 7 here).
- `ATEL_MISC_LIBRARIES.md` 3, MapFx 93: zeroing the countdowns restarts every effect, it does not kill
  them. MapFx 54: nonzero resumes the group, so the name `SetGroup2Hidden` had the sense backwards.
  The two rows in `tools/apply_misc_lib_names.py` are aligned.
- `THREADS_AND_CLOCKS.md` R3: `FFX_Rand_SignedScale` and `FFX_Rand_Scale` draw from the same state,
  and the rain calls them.

## 12. Open

1. `sub_766AE0` builds a PS2 style projection (512 / 416 screen) in the rain build. The lines are
   submitted with an identity model matrix, so it is presumably only used for the dead cull. Not
   confirmed.
2. The point light double commit (section 7.3). A runtime check would settle whether map point
   lights ever reach the screen while the magic slot loop runs.
3. The model, shape and vertex list bodies are bounded, not decoded. The Mdl and Shape draw ops are
   the readers, `YONISHI_ENGINE.md` has their shapes.
4. Node +4 and +8 have no runtime reader found.
5. The event 310, 49 and 374 cull exceptions are hard coded per record index. Which visual each one
   keeps alive is not identified.
