# rcFlex, the PS2 particle and spring simulation

PROGRESS 1.7.8 in projectzanarkand. What `rcFlex` is, where its code lives, who drives it, which
characters and maps use it, and whether anything it writes reaches the step. All addresses are IDA
VAs (RVA + 0x400000). Evidence tags as elsewhere: [V] read in the binary, [M] measured by a scan over
shipped data, [I] inferred. [V] and [M] are the proved ones.

Proposed names are in `tools/apply_rcflex_names.py`. The doc uses them and gives the current IDB name
the first time when it differs.

## 1. Summary

- **rcFlex is a small, general mass-spring library**: one particle system plus six force types,
  spring, nail (a pin to a moving anchor), damp, gravity, wind and the particle integrator itself.
  Six object files from `..\program\src\rcFlex\` at 0x9285A0..0x92990B, in makefile order
  (Particle, Spring, Nail, Damp, Gravity, Wind), not alphabetical (`RCBG.md` 2). [V]
- **Its only client is `rcbgPSim`** ("P-Sim", `rcbgPSim.c` 0x91F650..0x91FFC9), which turns a ppp
  model's vertices into particles and its polygon edges into springs. [V]
- **`rcbgPSim` has one effect driver, the ppp operator `pppDrawMdlPSim`** (exe op table index 101).
  No CHR code, no ATEL syscall and no Oef2 handler calls into rcFlex or rcbgPSim. [V]
- **Shipped data uses it exactly once**: field map `slik02` (the S.S. Liki deck), section 10, program
  8, op 6. No battle map set, no magic DLL and no other file in the archive has it. [M]
- **That instance only simulates during the battle on that map.** The anchors are written by the
  battle special block (`btl:86` mode 1 in `slik02_00`), which pins one end to bone 14 of battle unit
  0x17. Until both anchors are set the sim never starts. [V for the code, I that this is the Sin fin
  harpoon cable]
- **No character uses it.** Lulu's and Yuna's sleeves, and every other character's cloth and hair,
  are the PC `ClothSystem` (render thread) and `FFX_Ch_StepSecondaryPhysics` (worker 0). Section 8.
- **Verdict: rcFlex does not feed the step.** Its outputs are its own heap arrays and the vertex block
  of the ppp model it was built from, and the only readers are its own draw. Its one RNG call (wind,
  `rand` at 0x92971C) is never reached by shipped data. It runs on the step thread, inside the effects
  pass, so it is step locked, not wall clock. Section 5. [V for the readers named, I that there are
  no others]

## 2. Code map

The source strings sit at 0xB67B74..0xB67C2C (for example `..\program\src\rcFlex\rcfxParticle.c`).
The exact name `rcbgPSimAddGravity` at 0x91F7C0 survives in the IDB, between AddDamp and AddNail,
which is where the alphabetical layout law (LUA_MODULE.md) puts it. The other names follow that law
and the decompiled behaviour. Each file starts with its own copy of the small vector helpers. [V]

| file | range | functions |
| --- | --- | --- |
| rcbgPSim.c | 0x91F650..0x91FFC9 | helpers VecAdd 0x91F650, VecScale 0x91F6B0, VecCopy 0x91F710, VecSub 0x91F740. AddDamp 0x91F7A0, AddGravity 0x91F7C0, AddNail 0x91F850, AddWind 0x91F980, ClearModelData 0x91F9B0, Create 0x91F9D0, CreateModel 0x91FD00, Delete 0x91FD40, Draw 0x91FD60, Final 0x91FD80, GetModelPacket 0x91FDC0, GetNailPos 0x91FDD0, Init 0x91FE00, New 0x91FE40, Process 0x91FE70, SetNailNum 0x91FEF0, SetNailPos 0x91FF10, Start 0x91FFB0 |
| rcfxDamp.c | 0x9292A0..0x92943E | VecScale 0x9292A0, VecSub 0x929300, Apply 0x929360, Delete 0x9293C0 (`j_j_j_FFX_MemFreeLocked_2_3`), Init 0x9293D0, New 0x929420 |
| rcfxGravity.c | 0x929440..0x9295DE | VecAdd 0x929440, VecScale 0x9294A0, Apply 0x929500, Delete 0x929560 (`_2_4` thunk), Init 0x929570, New 0x9295C0 |
| rcfxNail.c | 0x928DF0..0x92929B | VecAdd 0x928DF0, VecScale 0x928E50, VecCopy 0x928EB0, VecSub 0x928EE0, Apply 0x928F40, Delete 0x929070, Final 0x929090, Init 0x929100, New 0x929170, Reset 0x929190, SetNum 0x929230 |
| rcfxParticle.c | 0x9285A0..0x928A93 | VecAdd 0x9285A0, VecScale 0x928600, AddForce 0x928660, Calc 0x928710, ClearForces 0x928800, Delete 0x928850, Final 0x928870, Init 0x9288E0, New 0x928950, Reset 0x928970, ResetVelocity 0x9289B0, SetNum 0x9289F0, Update 0x928A60 |
| rcfxSpring.c | 0x928AA0..0x928DEA | Apply 0x928AA0, Delete 0x928C60, Final 0x928C80, Init 0x928CE0, New 0x928D50, Reset 0x928D70 (`nullsub_172`), SetNum 0x928D80 |
| rcfxWind.c | 0x9295E0..0x92990B | VecAdd 0x9295E0, VecScale 0x929640, VecCopy 0x9296A0, Apply 0x9296D0, Delete 0x929840 (`_2_5` thunk), Init 0x929850, New 0x9298C0, Set 0x9298E0 |

The ppp side, in the operator library: `pppDrawMdlPSim_Con 0x73CFB0`, `_ReCon 0x73CFE0`, `_Des
0x73D010`, `_Draw 0x73D070`, `pppDrawMdlPSim_Create 0x73D4D0` (`pppDrawMdlPSim__f73D4D0`) and
`pppDrawMdlPSim_GetScale 0x73D7C0` (`__f73D7C0`). Record at exe op table 0xC3A508 index 101
(0xC3B4D0), the same operator at DLL table index 101 (0xC3CB78), PS2 remote tool table index 161. [V]

All allocation goes through `sub_9258A0`, a thin wrapper over the shared engine heap. [V]

## 3. Data

### 3.1 Particle system, 0x40 bytes, `rcfxParticleNew` [V]

| offset | type | what |
| --- | --- | --- |
| +0 | double | current time |
| +8 | double | dt, 0.03 from Init |
| +16 | double | target time, written by Update |
| +24 | float | 0.001 from Init, no reader found |
| +28 | int | particle count |
| +32 | float | mass, 0.1 from Init |
| +36 | vec4 * n | positions |
| +40 | vec4 * n | the per particle vectors wind projects onto. No writer found, so it stays null and wind uses its plain branch [I] |
| +44 | vec4 * n | velocities |
| +48 | vec4 * n | force accumulators |
| +52 | int | force count |
| +56 | force ** | forces, kept sorted by priority (force +24) by AddForce |

`rcfxParticleSetNum` allocates the four 16 * n arrays. `rcfxParticleUpdate(ps, t)` sets target = t and
runs `while (time < target) { Calc(ps), time += dt }`. `Calc` zeroes the forces, calls every force's
Apply, then integrates semi-implicit Euler: `vel += f * (dt / mass)`, `pos += vel * dt`. [V]

### 3.2 Forces [V]

Every force starts with the same header: +0 type id, +4 Delete, +16 Reset, +20 Apply(self, ps), +24
priority.

| force | type | size | Init defaults | Apply |
| --- | --- | --- | --- | --- |
| gravity | 0x30002 | 0x2C | | `f += g * mass` |
| damp | 0x30003 | 0x20 | c (+28) = 1e-4 | `f -= vel * c` |
| nail | 0x30004 | 0x40 | priority 2 | pins particles to an anchor matrix, below |
| spring | 0x30005 | 0x30 | k (+40) = 0.8, c (+44) = 0.4 | `d = pj - pi`, `len = max(abs(d), rest / 1000)`, `F = d / len * (len - rest) * k + (vj - vi) * c`, `fi += F`, `fj -= F` |
| wind | 0x30006 | 0x60 | period (+64) = 1.0 | below |

Spring pairs are 8 * n bytes at +32 and rest lengths 4 * n at +36 (`rcfxSpringSetNum`).

Nail: +28 count, +32 particle indices, +36 local positions, +40 current targets, +44 previous
targets, +48 last time, +56 the anchor matrix pointer (PSim +28 or +92). Apply transforms each local
position by the matrix and moves each pinned particle from the previous target toward the new one by
`(t - lastT) / (target - lastT)`, then sets its force to zero, so it is kinematic.

Wind (`rcfxWindApply 0x9296D0`): when `t > nextT` it shifts next into previous, `nextT += period`, and
draws `nextAmp = 1 - rand() / 32767 * 2` (the CRT `rand` at 0x92971C). Each tick it eases between the
two amplitudes with `(3 - 2s) * s * s`, forms `w = base + gust * amp`, and adds `normal * dot(w,
normal)` when ps +40 is set, else plain `w`. `rcfxWindSet` writes base +28, gust +44 and period +64.

### 3.3 rcbgPSim object, 0xB0 bytes, type 0x20004 [V]

| offset | what |
| --- | --- |
| +16 | flags, bit 0 = running |
| +20 | the rcbgModel (0x140 bytes, type 0x20001, `rcbgModel__f9283B0`) |
| +24 | the particle system |
| +28 | anchor matrix 0 |
| +92 | anchor matrix 1 |
| +156, +160 | anchor 0 and 1 set flags |
| +164 | anchors still to set |
| +168 | double, the sim clock |

`rcbgPSimCreate 0x91F9D0` makes one particle per model vertex and one spring per unique triangle or
quad edge, rest length = the edge's length at build time, then calls Reset.

`rcbgPSimAddGravity 0x91F7C0` overwrites the caller's vector with (0, 1, -0.1, 0) and logs "DEBUG
Gravity Change", so the data's gravity is ignored.

`rcbgPSimSetNailPos 0x91FF10` writes the anchor's translation (+76 or +140), sets its flag, counts
+164 down, and at zero calls `rcbgPSimStart 0x91FFB0`, which resets the particles and sets +16 bit 0.

`rcbgPSimProcess 0x91FE70` (`test byte ptr [edi+10h],1` at 0x91FE78): when running,
`rcfxParticleUpdate(ps, clock)` then `clock += 0.5` (`qword_B92220`). It always writes the particle
positions back as s16 vertices into the model blob (`sub_9281D0`, blob + blob[8]) and calls model slot
+8. One Process call is about 16 or 17 ticks of 0.03. The time base is the number of calls, nothing
reads a clock. [V]

### 3.4 The operator's parameters, read by `pppDrawMdlPSim_Create 0x73D4D0` [V]

| offset | what |
| --- | --- |
| +4 | ppp model index into the current heap's model list, 0xFFFF for none |
| +34 | nail count, clamped to 4 |
| +35, +36 | damp on, coefficient |
| +40, +48 | gravity on, vector (ignored, see AddGravity) |
| +64, +80, +96, +112 | wind on, base, gust, period |
| +116..+120 | draw flags into the model packet (20 vs 4, 0x2000, 0x200, 8, 0x20) |
| +122..+128 | s16 nail vertex list index per nail |

The per particle work area at op offset +160 holds the PSim PrxPtr, six UV scroll floats, and the
params PrxPtr at +28 of that block. The PSim is built on the first Draw call that finds the PrxPtr
null.

## 4. Where it runs [V]

```
FFX_MainStep effects pass loop 0x821400..0x8214CA, max(subSteps, 1) times per step
  if g_ffxMapFPPassEnabled                                     0x821480
    FFX_MapFx_StepPlacedEffects                                call at 0x82148C
      FFX_MapFx_UpdatePlacedEffects 0x729AB0
        record runtime flag 0x10 set -> skip and unlink        0x729B65
        sub_7122F0 -> sub_717010 (ppp calc pass)
          pppDrawMdlPSim_Draw 0x73D070
            first call: pppDrawMdlPSim_Create 0x73D4D0
            UV scroll, gated on g_ffxVfxTimeFrozen
            model packet alpha byte != 0                       cmp byte ptr [ebx+1Bh],0 at 0x73D30B
              rcbgPSimProcess                                  call at 0x73D3F3
              submit: sub_718610 + FFX_Vfx_SubmitModel, or sub_711180
```

- **Thread**: the step thread. The whole chain is direct calls from `FFX_MainStep`.
- **Rate**: once per effects pass iteration, so `max(subSteps, 1)` Process calls per step, each one
  0.5 sim seconds. It never looks at a wall clock or the frame delta.
- **Catch-up**: the effects pass is not gated on `g_ffxIsCatchUpStep`, and neither is the ppp calc
  pass (`sub_716C40` and `sub_717010` call Draw per particle unless program +12 bit 0 is set, with no
  catch-up or time frozen test). So rcFlex steps on catch-up steps too. Only the final draw
  submission inside `FFX_Vfx_SubmitModel` is catch-up gated.
- **Time frozen**: `g_ffxVfxTimeFrozen` stops only the UV scroll. Process still runs.
- **Turbo (speed booster)**: turbo runs more steps per frame (BOOSTERS.md), so rcFlex does the same
  work per step and more per wall second. Nothing about it depends on the booster state.
- **Alpha gate**: a fully transparent instance does not simulate. That is a step-side condition on a
  value the ppp program computes on the step, so it stays deterministic.

## 5. Does it feed the step

No. The chain, output by output:

1. **Particle arrays** (positions, velocities, forces): heap memory owned by the particle system.
   The only code that reads them is rcFlex itself, `rcbgPSimGetNailPos 0x91FDD0`, and the vertex write
   back `sub_9281D0`. The callers of GetNailPos are the two rcui debug routines at 0x930DB0 and
   0x930E70, which have no callers. [V]
2. **The model's vertex block**: `sub_9281D0` writes s16 positions into the ppp model blob in the
   loaded section 10 data. Readers are the ppp model draw (`sub_718610`, `FFX_Vfx_SubmitModel`,
   `sub_711180`) and a later `rcbgPSimCreate` if a second PSim were built from the same model. No
   second instance exists in shipped data. [V for those readers, I that nothing else reads that
   block]
3. **The anchors** go the other way. rcbgPSim reads a battle bone position, nothing reads back. [V]
4. **RNG**: the only random draw is CRT `rand` at 0x92971C in `rcfxWindApply`, on the step thread.
   THREADS_AND_CLOCKS.md R14 already lists it with the rcBg cosmetic draws. The one shipped instance
   has wind off (+64 = 0, `test` at 0x73D6C4 skips the AddWind call at 0x73D70E), so the call never
   runs in retail. If a mod enabled wind it would advance the step thread's CRT stream, which is the
   cosmetic stream, not `FFX_Rand_Stream`. [V for the code, M for "off in data"]
5. **Allocation**: `sub_9258A0` on the shared engine heap. The PSim is built once per instance and
   lives as long as the ppp particle, so it adds one allocation pattern on the step. Two peers that
   run the same steps make the same allocations. [V for the allocator, I for the lifetime]

So for the recreation it is presentation. It can run anywhere that sees the same anchor inputs, and
if it runs on the step it does not have to be hashed.

## 6. Census

### 6.1 Map particle sets [M]

`census1.py` (scratch) walked section 10 of every map in the archive with `tools/mapfile.py`, parsed
each ppp program and counted ops by index. 317 sets, 274 field and 43 battle. Op 101 appears once:

- `slik02`, program 8, group 0, op 6. Params: model 14, nails 2 (vertex lists 5 and 6), damp on at
  0.01, gravity on with (0, 30, 3), which the code replaces with (0, 1, -0.1), wind off, draw flags
  [1, 0, 0, 1, 0].
- `slik02`'s placed effect record 10 carries group 91, which is the group the battle code asks for
  (6.3).

### 6.2 Magic DLLs and everything else [M]

- No magic DLL contains the string `pppDrawMdlPSim`, while 136 of them name `pppDrawMdl2`, so the
  string test does see operator names. A structural scan for particle sets found 585 sets in 576
  DLLs, all with local op indices 0..58 into their own tables, and none of those tables names PSim.
- A structural scan of the other 4645 files under `ffx_ps2` found 516 particle sets and no PSim.
  FFX.exe itself carries none.

### 6.3 Who sets the anchors [V]

Two callers reach `rcbgPSimSetNailPos`, both through `sub_919F10` (proposed
`FFX_RcBg_SetPSimNailPos`, which forwards anchors 0 and 1 only). The PSim comes from `sub_919F00`
(`FFX_RcBg_GetPSim`) -> `sub_9199F0` (`FFX_RcBg_GetPSimByIndex`) -> `sub_923100`
(`FFX_RcBgFP_GetGroupOpWork`):

1. **The battle special block.** `FFX_BtlEv_StepBattleContext 0x7972E0` calls `sub_79D430` (proposed
   `FFX_BtlSpecial_Step`) every battle context step (0x797325), and it switches on the mode byte
   `word_112A8E6`. `btl:86` (`FFX_AtelSys_Btl_086_SetSpecialBlock_resi`) and `FFX_Btl_Init` set the
   mode through `sub_79DE60` (proposed `FFX_BtlSpecial_SetBlock`), which allocates a 0x870 state
   block for modes 1 to 3 and stores 0 for anything else. In mode 1 `sub_79D430` gets the PSim through `sub_919F00(0)` -> `sub_9199F0` -> `sub_923100(91, 6, 2)`, which finds group
   91 through `MapFx__f7292F0` on `dword_D2C258` and returns op 6, work slot 2, +160. It then sets
   anchor 1 to the world position of unit 0x17 bone 14 (`FFX_Ch_GetBoneWorldPos`, `sub_919F10` at
   0x79D51D), and anchor 0 to a point carried through map object rotations with `FFX_Came_ApplyShake`
   (`sub_919F10` at 0x79DCCE).
2. **The rcui "P-Sim" debug window.** `sub_935710`, the window proc that `sub_935000` registers under
   the label "P-Sim" (push at 0x9351AC), sets the anchors to (0, 0, -15) and (0, 0, 15) on message
   0x8011. `sub_935000` is the "PRE2D" window from `rcuiPre__f930D80`, which only
   `SG_DebugGui_RootWindowProc` calls, and the debug GUI opener is an orphan in retail
   (DEBUG_MODE.md section 3). So retail never reaches it.

The `btl:86` census over every battle script: `slik02_00` calls it with mode 1. `ssbt00_00` and
`ssbt01_00` use mode 2 and `hiku15_00` uses mode 3, and those branches of `sub_79D430` do not touch
the PSim. [M]

So the one instance only moves during the `slik02_00` battle. Battle scene `slik02` runs on field
map 97 (`btlmap.py`, and CHEAT_WARP lists `slik0200` as map 97), so the battle uses the field map's
section 10 in memory [I]. On the field outside that battle nothing sets the anchors, +164 never
reaches zero, and Process only does the vertex write back. [V for the callers, I for the reading]

What it is [I]: the rcui code logs "rope:%f,%f,%f", one end follows a bone of a battle unit, the
other a point on the ship. That fits the harpoon cable from the S.S. Liki to Sin's fin in that
fight.

## 7. Correction: the MapFx group flag runs the other way

This came up while tracing the update loop. `FFX_MapFx_UpdatePlacedEffects` **skips** a placed effect
record whose runtime flag 0x10 is set: `test byte ptr [eax],10h` at 0x729B65, then `MapFx__f728E40`,
which walks the record's meshes and calls `sub_645710(x, 0)` -> `sub_6A2370` ->
`FFX_Gfx_LinkMeshToNode(mesh, 0)`, an unlink, and jumps past the step. [V]

So:

- `0x729570`, which sets 0x10, suspends and hides a group. It was named `FFX_MapFx_EnableGroup`, and
  the script renames it `FFX_MapFx_SuspendGroup`.
- `0x7295B0`, which clears 0x10, lets the group run again. It was named `FFX_MapFx_DisableGroup`, and
  becomes `FFX_MapFx_ResumeGroup`.
- `mapfx:13` (`0x91C740`) pops the last argument first (ebx) and calls ResumeGroup when it is nonzero,
  so it is `SetGroupActive(group, active)`, not `SetGroupHidden(group, hide)`.
- Two more callers fit the corrected sense: `FFX_Field_FreezeActorsToScratch` suspends,
  `FFX_Atel_RestoreActorFromScratch` resumes.
- The indoor flag (MAP_SECTIONS.md section 4) resets and **runs** its named groups when it turns on and
  suspends them when it turns off. The old reading "rain and snow stop indoors" came from the wrong
  sense and is now open.

Fixed in MAP_SECTIONS.md (sections 4 and 7 and the names table), ATEL_MISC_LIBRARIES.md (mapfx row 13
and the usage line), YONISHI_ENGINE.md (the MapFx row) and `tools/apply_misc_lib_names.py` (row 13).

## 8. For PROGRESS 12.8: the sleeves are not rcFlex

12.8 reads "rcFlex hair and cloth". No character uses rcFlex, so the sleeves have to come from
elsewhere. Two systems move character cloth and hair on PC:

1. **The PC `ClothSystem`.** A mesh segment is cloth when its `m_clothmodelIndex` and `m_clothIndex`
   are both set (WORD != 0xFFFF, the test in `sub_6365F0`). `FFX_Chr_StepClonedCharacterList 0x63B740`,
   called from `FFXApplication__render`, runs "ClothWorld" and writes the vertex buffers. Render
   thread, presentation only (DYNAMIC_MESHES.md 8, READBACK_CENSUS.md). [V there]
2. **`FFX_Ch_StepSecondaryPhysics 0x822DE0`**, the PS2 sway chains, run on worker 0 inside the
   JointOverrideJob, gated on `m_flags1` 0x200000 and 0x400000. It draws worker `rand()` and is the
   only clear of `m_flags3 & 4`, which feeds the next step's composer choice, so it is **not**
   hash neutral (CHR_POOL.md 5.3, CHARACTER_MOTION.md 11.10 to 11.13). [V there]

Cloth segment census over the 872 `.dae.phyre` files under `ps3data/chr/` (`phyre.py` from
projectzanarkand) [M]: 122 have cloth segments, 32 pc, 51 npc, 17 mon, 15 weapon, 4 summon and 4
field object. Yuna c002 has 17 cloth segments out of 22, Lulu c006 16 out of 24, Tidus c001 5 out of
13. So Lulu's and Yuna's sleeves are ClothSystem cloth, and 12.8 should be built against
ClothWorld, plus whatever StepSecondaryPhysics chains those models carry. The 1.7.8 answer
(presentation only) does not carry over to StepSecondaryPhysics.

## 9. Recreation notes [I]

- The whole library is about 2 KB of float code. A port needs the integrator, the six forces, the
  edge builder in `rcbgPSimCreate`, the 0.5 per call clock, and the s16 write back.
- The one retail use needs the battle special block's anchor feed (6.3). Without it the cable is just
  the static model.
- Keep wind's `rand` off the simulation streams if wind is ever enabled.
- Gravity is hard wired to (0, 1, -0.1) regardless of data.

## 10. Open

- ps +40, the vectors wind projects onto: no writer found. With wind off everywhere it does not
  matter for retail.
- ps +24 (0.001): no reader found.
- Whether anything other than the draw reads the model vertex block after the write back. The draw
  readers are proved, the absence of others is not.
- The visual identity of the slik02 instance (the harpoon cable) is inferred, not seen.
- What the indoor flag's effect groups actually are, now that the sense is reversed (section 7).
- 0x930DB0 and 0x930E70 are rcui debug code with no function in the IDB and no callers. Left
  unnamed.
