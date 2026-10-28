# The yonishi effect engine

PROGRESS 1.10.3. What runs effects in FFX.exe, where its code lives, what a step of it does, and which
of its writes the simulation reads back. All addresses are IDA VAs (RVA + 0x400000) unless a line
says RVA. Evidence tags as elsewhere: [V] read in the binary, [I] inferred from structure, [M]
measured by a scan.

Proposed names are in `tools/apply_yonishi_names.py` (691 functions, 16 globals). The doc uses the
proposed names where they help and gives the current IDB name beside them the first time.

## 1. Summary

- **"yonishi" is two engines, and they are not where PROGRESS put them.** The 0x720000..0x760000 range
  holds the tail of the Virtuos prim build jobs, the field placed particle glue (MapFx, "FP"), the
  ppp core and the **ppp (npart) particle operator library**, 260 operators in 556 functions. The
  Oef2 script interpreter (`FFX_Magic_Oef2Dispatch`), the `op_*` / `opu_*` / `opp_*` code and the
  effect unit live at **0x7E3000..0x81CF40**, 712 functions. Section 2.
- **Oef2 is an opcode interpreter over the PS2 effect data, and it runs in the exe.** 256 slot table
  `g_ffxOef2SubCommandTable 0xC48EC8`, 137 slots populated by 119 handlers, plus a per unit DLL table
  at header +0x7C for op words with bit 8. 256 byte objects, four threads each, a fixed point
  integrator and a 12 slot move function table at `0xC492C8`. Section 5.
- **ppp is table driven, not interpreted.** Each operator is a 40 byte record with nine function
  columns. Four tables: 145 records for the exe (`0xC3A508`), 273 for the DLLs (`0xC3BBB0`, host data
  slot 921), 246 for the PS2 remote tool (`0xC3E7A8`), 32 more at `0xC86080`. Section 4.
- **The effect unit is a 0x100 byte header followed by four u16 lists and an array of 256 byte
  objects.** Unit 0 is the exe's, static at `0x22DA070` with 512 objects, built once at boot. Unit 1
  is a magic DLL's. Nothing ever frees a unit. Section 7.
- **Every entry point is step driven.** Oef2 pass 0 runs once per sub-step, pass 1 and opp run in the
  effects pass loop, pass 2 is a VFX callback at the end of the pass. Section 8.
- **Effects are not pure presentation.** Opcodes advance the battle effect state (`actor+0xDFB`),
  apply pending hits (which draws `FFX_Rand_Stream`), and on the field write CHR position,
  rotation, motion, visibility and i408. The integrator that moves objects is step locked. The
  cosmetic RNG `g_ffxEffectRandState` can reach a CHR position through ops 18..23 then op 72.
  Section 9.

## 2. Where the engine lives

| range | functions | what |
| --- | --- | --- |
| 0x70F400..0x720000 | 151 | ppp runtime: particle set relocate, prim list alloc, clock and step (`sub_712250`, `sub_7123C0`), the calc passes `sub_716C40` and `sub_717010`, the hook callers `sub_712D20..sub_712E80`, birth `sub_716770`, `maybe_FFX_pppMemAlloc 0x716A30` |
| 0x720000..0x760000 | 655 | section 3. Prim build jobs, MapFx, ppp core, the ppp operators, a math tail |
| 0x760000..0x76B900 | 94 | the PS2 PMCOM remote particle tool protocol (`FFX_Vfx_PumpCommandRing 0x7614A0`, the `PROTO_PARTMAST` driver at `sub_761D60`) and `FFX_Vfx_StepAll 0x762240` |
| 0x7E3000..0x7E6560 | 49 | Oef2 data: `op_oef2_set`, `FFX_Oef2_SetFromUnit`, `FFX_Oef2_GetSubTable`, texture and CLUT load, the texture upload queue `FFX_Tex_DrainUploadQueue 0x7E4720` |
| 0x7E6560..0x7EB300 | 64 | Oef2 math: `op_m_normalize`, `op_mul_m33`, `op_vector_m`, `FFX_Rand_Scale`, `FFX_Rand_SignedScale`, the scratch stack `FFX_Scratch_Push/Pop` |
| 0x7EB300..0x7ED000 | 42 | VFX callback groups (`FFX_Vfx_AddEffectCallback`, `FFX_Vfx_RunCallbackGroup`, `FFX_Vfx_EndPass 0x7EC470`) and GS draw env packets |
| 0x7ED000..0x7FB000 | 37 | effect object drawing, a few very large prim builders (`sub_7ED0C0` alone is 0x7900 bytes), catch-up gated |
| 0x7FB000..0x7FD000 | 78 | the effect API the rest of the game calls: `op_et_battle_effect_*`, `op_event_effect_*`, `op_fp_effect_*`, `op_ovl_effect_*`, the CHR shadow registry |
| 0x7FD000..0x800000 | 53 | effect units and link records: `FFX_Effect_InitUnit 0x7FDA80`, `sub_7FEBA0` (link record alloc), `op_malloc` / `op_free`, `FFX_Rand_SeedCosmeticStreams 0x7FF360`, `maybe_FFX_Yonishi__feedPadStateBothPorts 0x7FF680` |
| 0x800000..0x801000 | 42 | SPU stream requests and the resident overlay table functions `FFX_MagicResident_*` |
| 0x801000..0x81CF40 | 347 | the interpreter: Oef2 opcode handlers (up to `0x81CA60`), opp kinds, move functions, `FFX_BtlExplode_*`, `opu_part_run 0x80B800`, `opp_main 0x80BD50`, `FFX_Magic_Oef2Dispatch 0x80CC00`. `FFX_Se_*` starts at 0x81CF40 |

The 0x7E3000.. rows are coarse. Only the interpreter's tables were walked function by function.

## 3. Module attribution of 0x720000..0x760000

655 functions. Every one is in exactly one row. The method: table membership first (a function
that sits in a ppp record column belongs to that operator), then callers (a helper whose callers
all belong to one operator belongs to it), then neighbours (a helper sandwiched between two
functions of the same operator). `j_j_FFX_Mtx_Mul` thunks at `0x741B80`, `0x743270`, `0x744490`,
`0x7467A0` and `0x7479C0` start each DrawShapeField file, which is how the file boundaries inside the
draw families were checked.

The layout law (`LUA_MODULE.md`) holds inside an operator: its functions run in the alphabetical
order of their likely names (`pppKeTh`, `pppKeThCon`, `pppKeThDes`, `pppKeThDraw` ...). It does not hold
across operator files. Those follow makefile order, for example `pppRandFloat`, `pppRandUpFloat`,
`pppRandDownFloat`, which is source order. One file per operator is confirmed by the leaked path
`..\npart\eiProg\pppYanagi.c` (string 0xB51A38, function 0x766B90).

| range | fns | in a ppp record | operators | module and role |
| --- | --- | --- | --- | --- |
| 0x7256C0..0x7289CF | 7 | 0 | | Virtuos prim build jobs: `FFX_Vfx_PrimBuild_FrameBuffer`, `FFX_Vfx_PrimBuild_Sprite`, `FFX_Vfx_PrimBuildAllocSlot`, `FFX_Vfx_PrimBuildInitBuckets` and helpers. `FFX_Vfx_PrimBuild_Model 0x71F320` straddles the range start |
| 0x7289D0..0x72AC4F | 53 | 1 | | MapFx, the field placed particles. `nullsub_643 0x7289D0` is the shared empty ppp column (73 table refs). Group API for ATEL and rcBg (`FFX_MapFx_SuspendGroup`, `ResumeGroup`, `ResetGroup`, the first two applied as `EnableGroup` and `DisableGroup` with the sense backwards, see `RCFLEX.md` 7), `FFX_MapFx_UpdatePlacedEffects 0x729AB0`, `FFX_MapFx_LoadParticleSet 0x72A7F0`, `FFX_MapFx_LoadParticleTextures`, `FFX_Vfx_SetTimeFrozen 0x72A750`, and the four FFX_Vfx_StepAll helpers `0x729850`, `0x7299E0`, `0x729F70`, `0x729F90` |
| 0x72AC50..0x72D1AF | 20 | 0 | | ppp core. `nullsub_84 0x72C1A0` (the call at the top of FFX_MainStep), the ppp heap (`FFX_PppHeap_Alloc 0x72C3D0`, `Init 0x72C480`, host 337, `Free 0x72C510`), four helpers called from the 0x87xxxx..0x92xxxx script code, the rotation builders `FFX_Ppp_RotMatrixX/Y/Z` and the six orders `FFX_Ppp_RotMatrixXYZ..ZYX 0x72C800..0x72CC70` |
| 0x72D1B0..0x72DECF | 7 | 7 | 8 | ppp `pppMatrix` and its six rotation order variants, one calc each |
| 0x72DED0..0x72F3BF | 11 | 9 | 3 | ppp model deform and fade: `pppKeMdlDtt`, `pppVtMime`, `pppKeTkFade` |
| 0x72F3C0..0x73401F | 44 | 33 | 33 | ppp random values: 24 `pppRand*` (Float, Char, Short, Int, FV, IV, CV, HCV, each plain, Up and Down) and 9 `pppSRand*`. `FFX_Rand_EffectFloat1to2 0x72F420` opens the run |
| 0x734020..0x7350FF | 23 | 23 | 21 | ppp matrices: `pppKeMatSN`, `pppMatrixLoc/Scl`, `pppKeMatPht`, `pppKeOfsMatXYZ`, `pppParMatrix`, `pppKeParMatR`, `pppChr*Matrix`, `pppDrawMatrix*`, `pppKeDMat*` |
| 0x735100..0x73604F | 4 | 3 | 3 | ppp Z correction: `pppKeZCrct`, `pppKeZCrctShp`, `pppEiZCrctDisPos` |
| 0x736050..0x7371BF | 47 | 47 | 41 | ppp texture handles: `pppKeTh` and its 37 `pppKeThRes*` size variants, `pppKeThTp`, `pppKeThTp2`, `pppKeThSft` |
| 0x7371C0..0x7409DF | 69 | 49 | 20 | ppp model draw: `pppNeiChrPointLight`, `pppDrawMdl`, `Semi`, `Ts`, the 2 and 3 variants, `Sea`, `Inf`, `PSim`, `Camera`, `Loop`, `LoopZ`, `LoopDisPos`, `CameraLoop`, `Rev` |
| 0x7409E0..0x748AFF | 60 | 24 | 9 | ppp shape draw: `pppDrawShape`, `X`, `Rev`, `Field`, `FieldRev`, `FieldSpd`, `FieldGlobal`, `Camera`, `CameraDisPos` |
| 0x748B00..0x74BC4F | 30 | 21 | 11 | ppp model bump and texture fade: `pppKeMdlBmp`, six `pppKeMdlTfd*`, four `pppNeiDraw*PointLight` |
| 0x74BC50..0x75542F | 67 | 30 | 9 | ppp shape tails: `pppKeShpTail`, `X`, `2`, `2X`, `3`, `3X`, `Pht`, `Lc`, and `pppKeShpDtt` |
| 0x755430..0x7573BF | 17 | 11 | 4 | ppp `pppDrawRain`, `pppDrawSprite`, `pppDrawFilter`, `pppDrawHook` |
| 0x7573C0..0x75964F | 39 | 28 | 15 | ppp emitters: `pppPointAp`, `pppPointRAp`, four `pppVertexAp*`, `pppSegmentAp`, `pppFaceAp`, six `pppKeBornRnd*`, `pppKeBornPtCmpl` |
| 0x759650..0x759F2F | 14 | 14 | 7 | ppp gravity and hits: `pppKeGrvEff`, `pppKeGrvTgt`, `pppKeHitBall`, `pppKeHitBorn`, `pppKeHitChkPxB`, `pppKeHmgEff`, `pppKeThHitBorn` |
| 0x759F30..0x75B22F | 37 | 29 | 11 | ppp lens flares: `pppKeLnsLpSft`, `pppKeLnsArnd`, `Clm`, `Crn`, `Fls`, `Lp` and their T variants |
| 0x75B230..0x75D27F | 60 | 56 | 26 | ppp motion: `pppKeAcmSolid`, `pppKeAcmCic`, `pppKeAccSpdSv`, `pppKeThCp*`, `pppKeThLz`, the `pppAccele` and `pppMove` families, `pppPoint`, `pppKeOfsPt`, `pppAngle`, `pppScale`, `pppColor` |
| 0x75D280..0x75E4AF | 27 | 26 | 12 | ppp lights and misc: `pppPObjPoint`, `pppVertexAttend`, `pppEiWindFun`, `pppNeiLightEikyo`, `pppNeiPointLight`, five `pppFpPointLight*`, `pppKeMvYpEff`, `pppKeDrct` |
| 0x75E4B0..0x75FFFF | 19 | 0 | | tail: PS2 VU0 macro emulation (`0x75E7D0`, `FFX_Vu0_SubXyWithFlags 0x75E870`, `0x75E8F0`), seven math helpers shared by ppp operators, two helpers of `sub_7ED0C0`, and five functions of the PMCOM tool side, including its protocol callback `0x75FF00` registered by `sub_761D60` |

7 + 53 + 20 + 556 + 19 = 655. The 556 ppp functions split into 370 record functions that get a
name, 40 record functions that are shared nullsubs or already named, 103 helpers attributed to one
operator, and 43 left unattributed because they sit on a file boundary with no caller to decide it.

## 4. ppp, the particle operator library

### 4.1 The record

40 bytes, `RelocateParticleSet` (`FFX_Vfx_RelocateParticleSet 0x711FA0`, host 711) turns an operator
index into `table + 40 * idx` (`sub_7120F0`).

| off | column | called by | role |
| --- | --- | --- | --- |
| +0 | | | name string (`"pppKeShpTail"`) |
| +4 | 1 Group | `sub_716C40`, `sub_717010` | runs the whole group itself when set. When set, +8 and +12 are not used |
| +8 | 2 Calc | same | per particle, every step |
| +12 | 3 Draw | same | per particle, right after Calc, skipped when the program's flag +12 bit 0 is set. Only builds prim lists |
| +16 | 4 Init | `sub_712E40` | all zero in every table |
| +20 | 5 PrePass | `sub_712E80` | set hook before the pass |
| +24 | 6 PostPass | `sub_712E00` | only `pppKeAcmSolid` has one |
| +28 | 7 Con | `sub_712D90` from `sub_716770` | per particle constructor at birth |
| +32 | 8 ReCon | `sub_712D20` | re-constructor when a looping program restarts |
| +36 | 9 Des | `sub_7168F0` | destructor |

The names in the apply script follow the columns: `pppKeShpTail_Draw`, `pppAccele_Con`. When one
function fills both +28 and +32 it is `_Con` and the comment says so.

### 4.2 The tables

| table | records | who uses it |
| --- | --- | --- |
| `0xC3A508` (`g_ffxPppOpTable`) | 145 | the exe: `FFX_MapFx_LoadParticleSet 0x72A7F0` and `FFX_MagicResident_HdrRelocateParticles 0x800700`. `dword_C3A504` before it is the current FP group |
| `0xC3BBB0` (`g_ffxPppOpTableForDlls`) | 273 | only the magic DLLs, as host data slot 921 (`0xC65B4C`, RVA `0x865B4C`) |
| `0xC3E7A8` (`g_ffxPppOpTablePm`) | 246 | the PMCOM remote tool, `FFX_Vfx_PumpCommandRing` pushes `0xC3E780` |
| `0xC86080` | 32 | `sub_A54760`, caller `sub_8E27D0` |

The header strings at `0xC3A4E0` are the remote tool's status words: `STOP`, `RUN `, `END `,
`-----`, `CHR  `, `MAP-G`, `MAT  `, `MAP-D`. 411 distinct functions across all four tables, 260
distinct operator names. Every operator function lies in `0x72D1B0..0x75E470`.

### 4.3 How a part steps

A ppp part is a prim list slot. `sub_7123C0` (host 714) sets it up and `sub_712250` (host 712) is its
clock and step. The slot holds time at +8, +12 and +16, and the clock stops while
`g_ffxVfxTimeFrozen` is set. The step calls the calc pass, `sub_716C40` or `sub_717010` (the
second one tracks allocation failure). The pass walks the part's programs, and for each operator
entry calls +4, or else +8 and then +12 per particle. So Calc and Draw both run inside the step, and
Draw only records prims. Particles are born by `sub_716770` (constructor +28) and freed by
`sub_7168F0` (destructor +36). Memory comes from the ppp heap, a first fit allocator with a 16 byte
header, through `g_ffxPppCurrentHeap 0xC3A4CC`, which each caller points at its own heap first.

The `pppRand*` operators call `srand(g_ffxEffectRandState & 0x7FFFFF)` (THREADS_AND_CLOCKS R3, R4).

There are three ways a ppp part gets stepped:

1. A kind 3 Oef2 object, through Oef2 op 221 `opu_part_run`, which calls the header +0x48 slot
   (`FFX_MagicResident_HdrStepPrimList 0x8008B0` for unit 0), which calls `sub_712250`.
2. A field placed effect, through `FFX_MapFx_UpdatePlacedEffects 0x729AB0`.
3. The PMCOM slot loop in `FFX_Vfx_StepAll`, dead in retail (section 8).

## 5. Oef2, the effect script interpreter

### 5.1 Dispatch

`FFX_Magic_Oef2Dispatch(u16 unit, u8 pass)` at `0x80CC00`, host 716 [V]:

1. `g_ffxInMagicOverlayCall = 1` when `unit == 1`.
2. `FFX_Scratch_Push(1024)` for a context. It copies the view and projection matrices
   (`g_ffxEffectViewMatrix 0x113FCD0`, `g_ffxEffectProjMatrix 0x113FD10`) to ctx+640 and ctx+704, and
   **the whole 256 byte unit header to ctx+768**. ctx+528..543 start from a zero template.
3. ctx+540 = pass, ctx+548 = the pass list `unit+0x10+4*pass`. ctx+542 = 1 when `sub_780CB0()`, the
   battle test.
4. For each object index in the list (u16, -1 ends it): object = `unit+0x20` base + `index << 8`. For
   each of its four threads: skip if the thread's bit is set in obj+0x1F, count down the wait byte
   obj+0x10+t if it is not zero, otherwise run opcodes from the IP in obj+t*4 until a handler sets
   the yield count ctx+528.
5. Each op word is u16. **Bit 8 clear: `g_ffxOef2SubCommandTable[op & 0xFF]` at 0x80D57B. Bit 8 set:
   `(unit+0x50 header)+0x7C [op & 0xFF]`, at 0x80D563..0x80D570.** A handler takes the context and
   returns the next IP. A missing DLL table logs and leaves the IP where it is, so that thread stalls
   on the op for good.
6. After the threads, the integrator (5.4), then the move function.
7. Then a second sweep over the same list with ctx+541 = -1. In that sweep a thread runs only if its
   bit IS set in obj+0x1F, and the bit is cleared first. That is how a handler defers work to the end
   of the pass.
8. **The 256 byte context copy is written back over the unit header.** A handler that writes the
   unit directly instead of through ctx+768 loses the write.

Context fields the handlers use: +528 yield count, +530 time frozen (always 0 here, set by opp_main),
+532 current object index, +534 current thread, +536 list cursor, +538 the op word, +540 pass, +541
second sweep, +542 in battle, +544 current object pointer, +548 current list, +768..+1023 the unit
copy (so +772 is unit+4, +800 the object array, +848 the header, +860 unit+0x5C, the camera handle
op 57 uses).

### 5.2 The op word

| bits | meaning |
| --- | --- |
| 0..7 | opcode |
| 8 | 1 = the DLL table at header +0x7C |
| 9 | used by the SetVector ops. For op 9 the whole word >> 9 is the wait |
| 11 | take an operand from an object variable (`*(float*)(obj + imm)`) instead of the immediate |
| 12..15 | a mode, most handlers switch on it |

### 5.3 The opcode table

`g_ffxOef2SubCommandTable 0xC48EC8`, 256 dwords ending at `0xC492C8`. Populated slots: 0 1 2 3 4 5
9..15 18..23 26 29 30 31 33..43 46 47 50 51 53..58 60..65 68 72 74 77 78 79 81..87 90 92 93 95 97 99
100 103 104 107..115 125..134 136 137 138 140 142 143 144 146 150..153 156 161 162 164 168..173 175 176
192 201 202 210..225 242 254 255. 137 slots, 119 handlers, `0x7F7E30..0x81CA60`. Shared handlers:
10..15, 18..23, 214..219, 36 and 37, 152 and 153, 169 and 173.

The ones read for this pass [V]:

| op | handler | does |
| --- | --- | --- |
| 0 | `0x80C3F0` End | ends the thread. Thread 0 also clears the four IPs and frees the object's prims (`sub_8014C0`, which calls header +0x54, and `sub_800D10`). It waits 120 instead while its link record is a pending type 4 |
| 1 | `0x7FD720` KillObject | clears the IPs, yields, restarts the list walk, releases the link record (`sub_7FE6C0` -> `FFX_Btl_EffectAdvanceState`) |
| 2 | `0x8168B0` Jump | IP += s16 |
| 3, 4 | `0x815760`, `0x817280` | Call and Return, through a per thread return slot at obj+0x20+4*t |
| 5 | `0x8176F0` | set, clear, branch on or wait on bits of unit+4 |
| 9 | `0x80C500` Wait | yield for word >> 9 |
| 10..15 | `0x818410` SetVector | write a vector field of the object (offset `word_C49388[op-10]`, scale `unk_C49398`), then call the move function |
| 18..23 | `0x81A4A0` SetVectorRandom | the same, each lane times a draw from `g_ffxEffectRandState` |
| 34 | `FFX_Oef2_OpApplyPendingHits 0x815D90` | `sub_78D980` at 0x815DFE -> `FFX_Btl_ApplyPendingEffects` |
| 57 | `0x8157A0` Camera | `FFX_Came_SetPos` eye and target from object vectors, with fix ups per magic id |
| 63 | `0x8044E0` ChrMotion | CHR motion key, hokan, frame counts |
| 65 | `0x8047B0` | mode 0 `FFX_Ch_SetSpeedMulA`, modes 1 and 2 `sub_787950` |
| 72 | `0x8190E0` UnitMove | `FFX_Btl_StartUnitMove` (0x8192D0), unit flag 1042, and `FFX_Oef2_SetUnitOrChrPos(slot, obj+0x90)` at 0x819171 |
| 79 | `0x817470` ObjFlags | obj+0xB2 bits 0..2, bit 0 with `FFX_Btl_SetExplodeActive` |
| 112 | `0x817AD0` ChrI408 | test, branch, wait on or set CHR i408 |
| 127 | `0x80C490` | once per link record, `FFX_Btl_EffectAdvanceState(rec+0)` |
| 128 | `maybe_FFX_Oef2_OpRegisterHdrCallback 0x80B180` | registers header +0x74 as a VFX callback (EFFECT_OVERLAY 2.3) |
| 130 | `FFX_Oef2_ExplodeModel 0x804270` | |
| 151 | `0x8161F0` ChrVisibility | CHR hide bits 1 and 2, battle show votes (`sub_796660`) |
| 192 | `FFX_Oef2_OpEffectSync 0x816680` | `maybe_FFX_Btl_EffectSetReady` at 0x8166F1 |
| 221 | `opu_part_run 0x80B800` | kind 3 objects (obj+0xBB == 3, obj+0xBC the ppp work): header +0x48 at 0x80BCD8 |
| 254 | `maybe_FFX_Ch_PreloadScriptStep 0x8148C0` | `FFX_Ch_Allocate` |

Every other handler gets a positional `FFX_Oef2_OpNNN` name.

### 5.4 The integrator and the move table

For each object, unless obj+0xB2 bit 1 is set or ctx+530 is nonzero [V, 0x80D7C0..0x80D90A]:

    vel(obj+0x50..0x6F) += accel(obj+0x70..0x8F)
    pos(obj+0x30..0x4F) += vel

Then, unless obj+0xB2 bit 2 is set, obj+0x90 = a position vector times 1/16 (the world unit copy the
ops read) and, if obj+0xBA is 1..11, `g_ffxOef2MoveFnTable[obj+0xBA](ctx, ip)` at 0x80D998. The table
is at `0xC492C8`, right after the opcode table, 12 slots, 1 and 2 empty. IDA names it `funcs_80D9A0`
after the call site. The arithmetic goes through emulated VU registers at `0xC8F508..0xC8F990`.

## 6. opp

`opp_main(u16 unit)` at `0x80BD50`, host 721 [V]. Its own context: ctx+530 = time frozen =
`(byte_2322780 & byte_2322781) || FFX_Player__getSubStepCount() == 0`. It walks the opp list
(`unit+0x1C`) and for each object reads the kind obj+0xBB at 0x80C20B:

- kind >= 0: `g_ffxOppKindTable 0xC48E78 [kind]` at 0x80C3C8. 20 slots. 0, 3 and 19 are empty. Kind
  3 is the ppp part, stepped by op 221 instead.
- kind < 0: `(header+0x78)[kind & 0x7F]` at 0x80C222, the DLL's own opp table.

An object with obj+0x19 negative swaps in a third matrix (`dword_113FD50`) for its call. Kind 9
(`0x8124B0`) calls CRT `rand` on the step thread. Most kinds read `g_ffxIsCatchUpStep` to skip draw
submission.

## 7. The effect unit

### 7.1 Header, 0x100 bytes

Built by `FFX_Effect_InitUnit(u16 idx, startIp, dataId, unit*, u16 maxObjs, a6, a7)` at 0x7FDA80, host
715. It stores `g_ffxEffectUnitPtrs 0x12A40C0 [idx] = unit` at 0x7FDA8F, memsets 0x100 bytes (0x7FDAAE),
and lays the lists out from unit+0x100 [V].

| off | what |
| --- | --- |
| +0x00 | u16 unit index |
| +0x02 | u16 0x8000 |
| +0x04 | flag bits, op 5 |
| +0x08 | start IP |
| +0x10, +0x14, +0x18 | pass 0, 1, 2 lists, u16 object indices, -1 ended, 2*maxObjs bytes each |
| +0x1C | the opp list |
| +0x20 | the object array, 256 bytes per object |
| +0x24 | self pointer |
| +0x28, +0x2C | unit 0 only: `off_C498DC` |
| +0x34 | u16 maxObjs |
| +0x36 | 0x00010001 |
| +0x50 | **the overlay header**, unit 0 gets `g_ffxResidentEffectOverlayTable 0xC48D90` from slot 0, a DLL unit gets its own `g_effectOverlayTable` |
| +0x54, +0x58 | an offset base the resident slots 3 and 4 swap in and out (`dword_2332E88..E90`) |
| +0x5C | camera handle (op 57) |
| +0x60..+0x9F | 16 registered data ids, `obj+0x1E` indexes them |
| +0xA8 | unit 0 only: a 384 KB block from `sub_7FF2D0` |

**The header at +0x50 is EFFECT_OVERLAY's 32 dword table.** The unit stores the table's base, and
the exe calls its upper slots: +0x48 from op 221 `opu_part_run` (0x80BCD8), +0x54 from `sub_8014C0`
(object prim free, 0x801661), +0x60 from `sub_8170C0`, +0x74 from op 128 as a callback, +0x78 from
`opp_main` (0x80C222) and +0x7C from the dispatch (0x80D566). The resident table has +0x40..+0x54 and
+0x74, and +0x78 and +0x7C are zero, so unit 0 has no DLL ops and no negative opp kinds.

### 7.2 Object, 256 bytes

| off | what |
| --- | --- |
| +0x00..+0x0F | four thread IPs, stored through `maybe_FFX_PrxPtr_Set` |
| +0x10..+0x13 | four wait counters |
| +0x14 | u16 link record index into `g_ffxEffectLinkRecords` |
| +0x18 | u8 slot into `g_ffxEffectChrSlots` (the caster or target) |
| +0x19 | sign bit selects the third matrix in opp |
| +0x1E | data id registry index |
| +0x1F | thread done and defer mask |
| +0x20..+0x2F | four thread return IPs (ops 3 and 4) |
| +0x30..+0x4F | position block |
| +0x50..+0x6F | velocity block |
| +0x70..+0x8F | acceleration block |
| +0x90..+0x9F | world unit position, 1/16 scale, what ops 57 and 72 read |
| +0xB2 | flags: bit 0 explode, bit 1 no integrate, bit 2 no move function |
| +0xB3 | mask |
| +0xBA | move function index |
| +0xBB | opp kind |
| +0xBC | ppp work, for kind 3 |
| +0xC0 | flags, bit 0 mirror Y |
| +0xC8..+0xCB | RGBA |
| +0xD0 | matrix array pointer |
| +0xD4 | index |
| +0xDC | scale |

Which vector in the +0x30 block is scaled into +0x90 is not settled. The decompile reads the second
half, while op 57 treats `obj+0x30 / 16` as interchangeable with +0x90.

### 7.3 Who allocates, steps and frees

- **Unit 0** is static: `g_ffxResidentEffectUnitPtr 0xC88A90` points at `0x22DA070`, and the next
  five dwords point at its lists (`0x22DA170`, `0x22DA570`, `0x22DA970`, `0x22DAD70`) and its 512
  objects (`0x22DB170`). `FFX_Rand_SeedCosmeticStreams 0x7FF360` calls `sub_7FD760` once at boot,
  which calls `FFX_Effect_InitUnit(0, .., maxObjs 0x200, .., -1)`. Then it calls slot 0
  `FFX_MagicResident_OvlInit 0x800580`, which stores the resident table at unit+0x50.
- **Unit 1** belongs to a magic DLL. Its slot 0 calls host 715 with its own storage (magic_0021,
  EFFECT_OVERLAY 7).
- **Nothing frees a unit.** A unit lives in static or DLL storage and InitUnit just runs again.
  Objects end through ops 0 and 1, which free their prims and release their link record.
- **Link records**: `g_ffxEffectLinkRecords 0x12A4100`, 512 x 32 bytes, from `sub_7FEBA0`. +0 the
  battle effect handle, +4 the object, +8 target count, +16 targets, +24 caster, +29 a once latch.
- **CHR slots**: `g_ffxEffectChrSlots 0x12A8100`, 32 CHR pointers, filled by `sub_7FE200`,
  `sub_7FE970` and op 100.

## 8. The per-step entry points, in order

Inside `FFX_MainStep 0x820AE0` [V, decompile and the IDB comment there]:

1. `nullsub_84 0x72C1A0` at the top, then `FFX_Fmv_StepFromMainStep`.
2. Sub-step loop, `i < FFX_Player__getSubStepCount()`: `stepControl`, `FFX_Btl_MainStep`,
   `FFX_Atel_StepOnce`, `FFX_MesWin_StepAll`, then, if `g_ffxMovieScreenMode == 0`,
   **`FFX_Magic_CallOverlayStep(0)`**: resident slot 3 `FFX_MagicResident_OvlStep 0x800610` ->
   **Oef2 pass 0 on unit 0**, then DLL slot 3 for each battle actor in effect state 4 or 5 (whose DLL
   runs its own unit 1 pass 0). Then `Came_StepAll`, `RcBg_Step`, `Ch_UpdateMotionAll`,
   `PlayerCam_Step`.
3. If the sub-step count is 0, `FFX_Magic_CallOverlayStep(0)` once anyway, not gated on movie mode.
4. The camera block, `FFX_Vfx_GetCameraMatrices` / `SetCameraMatrices`, world scale, projection, view.
5. The effects pass loop, `k < max(subSteps, 1)`, gated on `g_ffxEffectsPassEnabled`,
   `MovieScreenModePrev == 0` and `ScriptEffectsPassOff == 0`:
   1. `maybe_FFX_Yonishi__feedPadStateBothPorts 0x7FF680`, one CRT `rand` discarded.
   2. On k == 0, the field EvTex and RcBg scene objects.
   3. `FFX_Gs_SendDrawEnvPacket`.
   4. If `g_ffxMapFPPassEnabled`, `FFX_MapFx_StepPlacedEffects 0x9083E0` ->
      `FFX_MapFx_UpdatePlacedEffects 0x729AB0` (when the map has section 10 and draw flag bit 1).
   5. **`FFX_Magic_CallOverlayStep(1)`**: resident slot 4 `FFX_MagicResident_OvlEffectsPass 0x800670`
      -> texture upload drain, CLUT palette, SPU stream service, camera matrices, **Oef2 pass 1 on unit
      0, `opp_main(0)`**, then DLL slot 4 for actors in state 4 or 5.
   6. **`FFX_Vfx_StepAll 0x762240`**. Its slot loop runs only while `g_ffxVfxSlotLoopOff 0x230FD38`
      is 0. `sub_761D60` (from FFX_MainInit) sets it to 1 at 0x761DDB, and the only clear is a PMCOM
      part start command at 0x761AEB in `FFX_Vfx_PumpCommandRing`. Retail has no remote tool, so this
      only pumps the command ring.
   7. DMA sync.
   8. **`FFX_Vfx_EndPass 0x7EC470`**, the VFX callback groups. Header +0x74
      (`FFX_MagicResident_HdrOef2Pass2 0x800A30`, **Oef2 pass 2**) runs here when op 128 registered it.

Pass 0 runs exactly once per sub-step, or once when there are none. Pass 1, opp and pass 2 run
`max(subSteps, 1)` times. The sub-step count comes from game state, not a clock, so all of it is step
locked. None of the three overlay call sites tests `g_ffxIsCatchUpStepRender`.

## 9. Simulation versus presentation

**Writes the simulation reads back** [V unless marked]:

| what | path |
| --- | --- |
| battle effect state `actor+0xDFB` 5 -> 6 | op 127 directly, op 1 through `sub_7FE6C0`, both to `FFX_Btl_EffectAdvanceState 0x788920` |
| `actor+0xDFB` 4 -> 5 | op 192 `FFX_Oef2_OpEffectSync` -> `maybe_FFX_Btl_EffectSetReady` at 0x8166F1 |
| damage, `FFX_Rand_Stream` | op 34 -> `sub_78D980` -> `FFX_Btl_ApplyPendingEffects`, which also runs AI commit and BtlEv |
| battle unit move and position | op 72 (`FFX_Btl_StartUnitMove`, `FFX_Btl_SetUnitPos`), op 86 (`sub_78CA20` -> SetUnitPos, AttachToParentBone, BtlEv) |
| **field CHR position and rotation** | op 72 modes 0 and 5 -> `FFX_Oef2_SetUnitOrChrPos 0x81B2B0` -> `FFX_Ch_SetPos` at 0x81B2EA, which teleports (walkmesh triangle -1). `FFX_Oef2_SetUnitOrChrRot 0x81B270` -> `FFX_Ch_SetRotAndMoveDir` |
| CHR motion, speed, i408, hide bits | ops 63, 65, 112, 151 |
| ATEL threads and battle events | op 65 -> `sub_787950` -> `sub_7AB360` -> `BtlEv_StartFirstMatch`, `FFX_Atel_CreateThread`. op 57 -> `FFX_Btl_CloseAllCameraHandles` -> `BtlEv_FireEvent` |
| CHR allocation | op 254 -> `FFX_Ch_Allocate` |
| explode flag | op 79 -> `FFX_Btl_SetExplodeActive` |
| camera | op 57 `FFX_Came_SetPos`, the battle camera (presentation unless something reads the camera back) |

**Not simulation**:

- No exe opcode reaches `FFX_Btl_Rand 0x7989A0`. The DLLs do, through host slot 324 (MAGIC_DLL).
- Many handlers look like they reach `FFX_Rand_Stream` through `sub_7FEBA0` -> `sub_7B7170`. They do
  not: `sub_7FEBA0` pushes 0 as the third argument (0x7FEC53) and `sub_7B7170` only draws when it is
  nonzero (0x7B7210).
- The ppp library, `FFX_Vfx_StepAll`, opp and the draw builders only make prims, except as below.
- `g_ffxIsCatchUpStep` readers in this engine (`0x7117C0`, `0x718610`, `0x71A4E0`, `0x71B8A0`,
  `0x72E790`, `0x740A70`, `0x740F60`, `0x741B90`, `0x742CC0`, `0x750620`, `0x755040`, `0x7645E0`,
  `0x7F49E0`, `0x7F5F70`, `FFX_BtlExplode_StepFragments`, `0x811530`) gate draw submission.

**The RNGs**:

- `g_ffxEffectRandState 0xC0A000` is cosmetic by intent. It drives particle spread, ops 18..23 and
  130, `FFX_Rand_EffectFloat1to2`, and the `pppRand*` srand seeds.
- CRT `rand` on the step thread: `maybe_FFX_Yonishi__feedPadStateBothPorts` once per pass, opp kind
  9, and every `pppRand*` srand reseeds the stream. CHR_POOL moved the `sub_822DE0` draw to worker 0,
  so whether any step thread simulation code reads CRT `rand` is the open question THREADS_AND_CLOCKS
  R3 to R5 track.

## 10. Hazards

### Co-op

1. **Effect playback must be identical on both peers, because battle progress waits on it.** The
   effect state machine only leaves 4 and 5 when an effect script runs op 192, 127 or 1, and hits
   land when it runs op 34. That fits "battle replicates the action": both peers must run the same
   script for the same number of passes, with the same DLL loaded. A peer that skips or shortens an
   effect desyncs the battle.
2. **The effects pass gates are simulation inputs.** Pass 1, opp, and through them ops 34, 72, 127,
   192, only run while `g_ffxEffectsPassEnabled`, `MovieScreenModePrev == 0` and
   `ScriptEffectsPassOff == 0`. Pass 0 skips while `g_ffxMovieScreenMode != 0`. Any local pause,
   overlay or movie state that flips one of these on one peer only changes when hits land. Treat them
   as lockstep state, not display state.
3. **A field effect can move a character.** Unit 0 runs on the field. Op 72 writes `FFX_Ch_SetPos`
   from the integrated object position, a teleport with the walkmesh triangle reset. If a field
   script plays such an effect on a co-op body, the body moves by effect, not by input. The
   integrated position can include `g_ffxEffectRandState` draws (ops 18..23), so the cosmetic RNG
   has a path into CHR position [I, the path exists, no shipped script was checked for it].
4. **`g_ffxEffectRandState` has to stay in lockstep after all**, because of 3 and because the ppp
   `srand` reseeds feed CRT `rand`. Do not let a peer advance it from presentation only code (a
   local preview, a skipped draw that still runs its Calc).
5. **Pass 0 in the zero sub-step arm is not movie gated.** A step with no sub-steps still runs Oef2
   pass 0 during a movie, a step with sub-steps does not. Both peers agree only if they agree on the
   sub-step count and the movie mode, which they should, but a hook that changes either locally
   (catch-up handling, a skipped movie) shifts effect timing.
6. **opp freezes time on a zero sub-step step, Oef2 does not.** The integrator in the dispatch always
   runs (ctx+530 is zero there), opp does not. A recreation or a hook that makes them agree would
   change object motion.

### Recreation

1. **The dispatch copies the unit in and writes it back.** Handlers must write unit fields through
   ctx+768, never the unit, or the copy back erases them.
2. **The integrator and SetVector ops run on emulated VU registers at `0xC8F508..0xC8F990`.** Matching
   positions bit for bit (which 3 needs on the field) means matching x87 and SSE order there,
   including the 1/16 and 1/4096 scales.
3. **The op word mode nibble is per handler.** There is no shared operand decoder, every handler
   decodes `op >> 12` and bit 11 itself, and handlers share code across opcode ranges (10..15 index
   tables by `op - 10`).
4. **Unit 0's header has no +0x78 or +0x7C.** An op with bit 8 on unit 0 stalls the thread forever
   instead of failing. A recreation should keep that, or log it, not crash.
5. **ppp operators index tables by record address.** Relocation turns indices into
   `table + 40 * idx`, so the four tables must keep their order and size. The DLL table (273) is not
   the exe's (145), and a DLL particle set relocated against the wrong one silently calls other
   operators.
6. **`FFX_Vfx_StepAll`'s slot loop is not dead code to delete.** Retail never clears its gate, but a
   PMCOM command would. A recreation can drop the PMCOM driver and the loop together.

## 11. Names

`tools/apply_yonishi_names.py` follows `apply_sound_names.py` exactly, with one change: a positional
`<Module>__f<hex>` name may hold digits in the module part, because ppp operator names do. A headless
dry run and a rehearsal with `--apply --comments` on a copy of the IDB rename all 707 rows with no
skips and no failures.

| group | rows | form |
| --- | --- | --- |
| hand checked | 14 | the ppp heap, `FFX_Ppp_RotMatrix*`, `FFX_Oef2_SetUnitOrChrPos/Rot` |
| ppp record functions | 370 | `<operator>_<Calc/Draw/Con/ReCon/Des/Group/PrePass>` |
| ppp helpers | 103 | `<operator>__f<hex>` |
| other modules in range | 69 | `VfxPrim__f`, `MapFx__f`, `PppCore__f`, `PppMath__f`, `Vu0__f`, `VfxPm__f`, `Oef2Util__f` |
| Oef2 opcode handlers | 110 | `FFX_Oef2_OpNNN`, with a role for the 17 read here |
| opp kinds | 15 | `FFX_Opp_KindNN` |
| move functions | 10 | `FFX_Oef2_MoveNN` |
| globals | 16 | the tables, the unit, link records, CHR slots, the Vfx gate, the matrices |

No existing good name is touched and RENAMES is empty. Shared nullsubs are skipped because one sits
in many columns.

## 12. Doc fixes made in this pass

- **MAGIC_DLL.md**, section 6 text and the RVA table: `0x72C730..0x733F70` was given as the particle
  operator library in a table of RVAs. It is a VA, and it starts at the rotation matrix builders, not
  the operators, and stops inside the `pppSRand*` run. Now RVA `0x32D1B0..0x35E4B0` for the library,
  with the `pppRand*` / `pppSRand*` run at `0x32F4B0..0x333FEC`. The string row `0xB50258..0xB511D4`
  was also a VA and starts at `pppKeBornRnd3`, not a `pppRand` name. The operator names are
  interleaved through VA `0xB50230..0xB513A0`, RVA `0x750230..0x7513A0`.
- **EFFECT_OVERLAY.md** 2.3: added the missing +0x48 reader, op 221 `opu_part_run` at 0x80BCD8.
- **PROGRESS 1.10.3** (not edited, for whoever owns it): it places op_*, opu_*, opp_* and Oef2 at
  0x720000..0x760000. They are at 0x7E3000..0x81CF40, and 0x720000..0x760000 is mostly ppp.

## 13. Addresses

| what | address |
| --- | --- |
| `FFX_Magic_Oef2Dispatch` | 0x80CC00, host 716 |
| `g_ffxOef2SubCommandTable` | 0xC48EC8, 256 dwords |
| `g_ffxOef2MoveFnTable` (`funcs_80D9A0`) | 0xC492C8, 12 dwords |
| `opp_main` | 0x80BD50, host 721 |
| `g_ffxOppKindTable` (`dword_C48E78`) | 0xC48E78, 20 dwords |
| `opu_part_run` | 0x80B800, slot 221 at 0xC4923C |
| `FFX_Effect_InitUnit` | 0x7FDA80, host 715 |
| `g_ffxEffectUnitPtrs` | 0x12A40C0, host data 743 |
| unit 0 | 0x22DA070, objects 0x22DB170, pointer 0xC88A90 |
| `g_ffxEffectLinkRecords` | 0x12A4100, 512 x 32 |
| `g_ffxEffectChrSlots` | 0x12A8100, 32 dwords |
| resident overlay table | 0xC48D90, pointer 0xC48D88, string "yonishi" at 0xC48D7C |
| ppp tables | 0xC3A508 (145), 0xC3BBB0 (273, host data 921), 0xC3E7A8 (246), 0xC86080 (32) |
| ppp heap | alloc 0x72C3D0, init 0x72C480 (host 337), free 0x72C510, current 0xC3A4CC |
| ppp part clock and setup | 0x712250 (host 712), 0x7123C0 (host 714) |
| `FFX_Vfx_StepAll` gate | 0x230FD38, set 0x761DDB, cleared 0x761AEB |
| effect view and projection copies | 0x113FCD0, 0x113FD10 |

## 14. Residuals

- 120 of the 137 opcode slots have no role yet. `reach.py` style closure over each says which ones
  touch CHR, ATEL or battle code, and section 9 lists the ones that matter. A full opcode reference
  is its own pass.
- Which half of the +0x30 block becomes +0x90 (7.2).
- `maybe_FFX_Effect_StepAll 0x7FDC80` (host 717) re-runs `op_oef2_set` on units picked by debug bits
  `word_2321580`, gated on `g_ffxSgDebugEnabled`, which `sub_761D60` sets to 1 at boot. The name is
  probably wrong (a debug reload, not a step) and who sets `word_2321580` is not known. Left as is.
- 43 ppp range functions on file boundaries are unattributed. The `0xC86080` table's owner
  (`sub_8E27D0`) is not identified.
- Whether a shipped field script uses op 72 on a party CHR. A scan of the unit 0 data
  (`et_ffx.bin`) for op word 72 would answer hazard 3, but that is game data and was not read.
