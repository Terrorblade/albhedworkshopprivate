# The ATEL Sys, Sg, MapFx, Test and AbMap syscall libraries

2026-10-26. PROGRESS 1.8.10, the first half (Movie and Save are a separate note). Five of the
libraries in `g_ffxAtelSysFuncLibs 0x1328558`, each a table of 16 byte rows {start, poll, float
getter, int getter}, all registered by `FFX_Atel_Init 0x86D6D0` through
`FFX_Atel_RegisterSysFuncLib`:

| lib | name | table | rows | registered at | what follows the last row |
|---|---|---|---|---|---|
| 1 | Sys | `g_ffxAtelSysFuncLib01_Sys 0xC52BE0` | 30 | 0x86D811 | the string `aAtelsaveram000` at 0xC52DC0 |
| 4 | Sg | `g_ffxAtelSysFuncLib04_Sg 0xC88D88` | 71 | 0x86D859 | `g_ffxAtelSysFuncLib05_Ch` at 0xC891F8 |
| 8 | MapFx | 0xC5DC90 | 108 | 0x86D880 | `dword_C5E350` |
| 9 | Test | 0xC5D8C0 | 1 | 0x86D841 | data at 0xC5D8D0 |
| 13 | AbMap | 0xC85EB0 | 1 | 0x86D88C | `off_C85EC0` |

The ABI is `ATEL_CONTEXT.md` 11.7 and 13. This note names every row a shipped script calls, gives the
dead rows a role too, marks them dead, and lists what each row means for lockstep.
`../tools/apply_misc_lib_names.py` puts the names in the IDB (section 6). All addresses are IDA VAs at
base 0x400000.

A script operand picks the library in bits 12..15 and the function in bits 0..11, so a Sys call is
0x1NNN, Sg 0x4NNN, MapFx 0x8NNN, Test 0x9NNN and AbMap 0xDNNN. Opcode 0x58 is the statement form
(the result goes to R), 0x35 the expression form. [V]

Read only throughout. The handlers were decompiled through idalib on a scratch copy of the IDB, with
nothing renamed and nothing saved. The census walked the install with `../tools/ebp.py` and the
recreation's `tools/mon_ai.py` loader, the same sources as the Came census, and no script bytes were
copied anywhere.

Marks: [V] read in the decompile or the disassembly, [I] inferred (a role read from the shape of a
wrapper, an engine field with no traced reader), [D] counted in shipped data. The tables carry V or I
per row.

The lockstep column uses these flags, shared by all five libraries:

- `sim` writes state the simulation reads (movement, collision, water, the save block).
- `global` writes a single slot global or carries a hard coded scene, level, event or magic case.
- `camread` hands camera state to the player simulation.
- `input` runs a native UI that reads the local pad and returns its choice.
- `readback` hands renderer side state back to the script as a value.
- `wait` blocks for a length decided by engine state, or gates something a wait reads.
- `lang` depends on the machine's language setting.
- `heap` allocates or frees on the FFX heap. `cache` frees CHR cache residency. `disk` loads files.
- `float` returns a CRT transcendental or sqrt result that scripts feed into positions and branches.
- `local` writes through raw pointers or stores a resolved script variable address.
- `debug` writes debug only state with no reader.
- `render` only sets render or audio state.

Across all five libraries, as read: no RNG stream is drawn, no wall clock is read, no pad is read
(except through the menu sgMenu opens), nothing paces on an FMV. [V]

## 1. Sys, library 1

### 1.1 Short version

- **12 of the 30 rows are called, 18 are not.** 7,332 call sites, almost all in event packages
  (7,324 in 148 packages) and 8 in one battle field file. Every row has a role, the dead ones too.
  No row is null. [D] [V]
- **Sys is the script math library.** sin, cos, tan, atan, atan2, sqrt, fixed point angle tables,
  angle unit conversions, distances, a facing cone and a sphere test. sys:6 Sqrt alone is 4,979 calls.
  Every row except 0, 28 and 29 is a pure function of its arguments. [V]
- **The math is CRT math.** `sub_88C3A0` is `(float)sin(a1)`, `sub_88B3E0` is `(float)cos(a1)`, and
  sys:4, 5, 6, 7 and 25 call `atan`, `atan2`, `sqrt` and `fabs` directly. Results go into script
  variables that place actors and decide branches. Both peers run the same binary, so this is no
  hazard between two copies of FFX.exe. It is a hard requirement for the recreation, which has to
  reproduce the MSVC CRT results bit for bit. Section 1.5. [V]
- **Rows 0, 28 and 29 are a debug overlay that nothing reads.** They write the ATEL context arrays at
  ctx+0xA4 and ctx+0x1A4. A displacement scan of all of .text and an xref scan of the context found no
  reader. sys:0 is still called 673 times in 88 packages, all of it dead weight in retail. [V] [D]
- **No hazard flags beyond float and debug.** No RNG, clock, pad, language, disk or single slot
  global. [V]

### 1.2 Census [D]

| source | files | files with Sys | Sys calls |
|---|---|---|---|
| event packages, `jppc/event/obj/*.ebp` | 397 | 148 | 7,324 |
| battle field files, `battle/btl/*/*.bin` | 816 | 1 | 8 |
| monster AI, `battle/mon/_mNNN/mNNN.bin` section 0 | 361 | 0 | 0 |
| `battle/btl/system_01/system_01.bin` | 1 | 0 | 0 |

No call names an index past row 29. `ATEL_CONTEXT.md` 13.3 notes that rows 8..37 of libraries 2, 3,
10, 14 and 15 would alias Sys rows 0..29, and no shipped script calls those either. [D]

Used rows: 0, 1, 2, 5, 6, 10, 19, 21, 25, 26, 27, 28. The heaviest are sys:6 Sqrt 4,979 in 49
packages, sys:0 DebugWatchVar 673 in 88, sys:26 Dist2D 505, sys:27 Dist3D 354, sys:19 WrapAnglePi 223,
sys:2 Cos 198 and sys:1 Sin 183. sys:5 Atan2 is only in `test23`. Everything except rows 0 and 28 is
called in expression form (0x35), 6,654 of the 7,332 sites. The title (`test20`) calls only sys:0, the
Besaid event packages call sys:0 and sys:6.

Dead rows, 18: 3, 4, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 23, 24, 29.

Row shapes, matching `ATEL_CONTEXT.md` 13.2: 12 rows have only the int getter, 18 only the float
getter. Handlers sit at 0x8778B0..0x878110, each its own function, none shared. [V]

The arity column is the handler's pop count. The bytecode estimate agrees on the two statement form
rows (sys:0 four, sys:28 two). The expression form rows have no bytecode estimate.

### 1.3 Names already there

Every slot in the scratch copy is positional, `FFX_AtelSys_Sys_NNN_<slot>`. `tools/ebp.py` has
`LIB_INFO` 1 = (0xC52BE0, 30, 28) and no Sys names. Nothing to correct. [V]

### 1.4 Families

- **Transcendentals**, 1 to 7: Sin, Cos, Tan, Atan, Atan2(y, x), Sqrt, RSqrt. Float in, float out.
- **Fixed point**, 8 to 16: table sine and cosine on `word_C54E40` through `sub_88C690` at 0x8000
  units per turn, `v << 15`, `(int)(v * 32768)`, and conversions between radians and the 0x8000 and
  4096 per turn units. All dead. sys:10 ToInt is used, 33 times, and is just `FFX_Atel_PopInt`, which
  truncates a float operand toward zero.
- **Geometry**, 17 to 27: heading between two XZ points, yaw and pitch into two variables, wrap to
  -pi..pi, turn toward, `FFX_Atel_TestFacingCone`, a sphere test through `FFX_DistSqLE`, a spline
  sample and a polar to vector helper writing into variables, fabs, 2D and 3D distance.
- **Debug**, 0, 28, 29: the watch entries below.

The debug rows. sys:0 pops (slot, x, y, var) and writes 8 bytes at ctx+0xA4 + 8 * slot, slot clamped
to 0..31: a byte `(x / 8) & 0x3F | 0x80`, a byte `(y / 4) & 0x7F`, the calling actor's id from the
word at actor+46, and the raw variable reference. A negative x or y clears the entry. That is a screen
cell and a variable to print, a debug watch window. sys:28 and 29 go through `sub_878140`, which pops
(slot, var), clamps the slot to 0..7 and stores the resolved address of the variable (or 0) at
ctx+0x1A4 + 8 * slot, then the row sets +4 to 0 (sys:28) or 1 (sys:29). The meaning of that flag is
unknown because nothing reads it. [V]

### 1.5 Lockstep hazards

- **float.** The transcendental rows call the MSVC CRT. Two copies of FFX.exe on two machines run the
  same CRT code. MSVC's CRT chooses an x87 or an SSE2 implementation of sin, cos and the rest from a
  CPU check at start up, so in principle two machines could take different paths. Every CPU that runs
  the PC port has SSE2, so in practice both take the same path. [I] For the recreation this is a
  requirement, not a hazard: scripts compare sys:26 and 27 distances against thresholds and feed
  sys:1 and 2 into positions, so a libm that differs in the last bit can flip a branch. The port must
  ship its own bit exact versions of these six functions or prove its libm matches.
- **Float to int.** sys:10, 12, 13, 14 and `FFX_Atel_PopInt` truncate through `__ftol2_sse`, which
  checks the machine local ISA level `dword_193ED20` to pick a path. Both paths truncate, so the
  result is the same. [V]
- **debug and local.** The two watch arrays live in the ATEL context. sys:28 stores an absolute
  address of script variable memory, which is machine local. A state hash that covers the context has
  to leave ctx+0xA4..0x1E3 out. Nothing in the game reads either array, so they cannot desync play.
  [V]
- Nothing else. [V]

### 1.6 Open

- The six arguments of sys:21 are named a..f in push order. Their meaning is `FFX_Atel_TestFacingCone`'s
  parameter order, which this note did not trace.
- sys:20, 23 and 24 are dead and their helpers (`sub_88B570`, `sub_88A4A0`, `sub_88A3A0`) were read
  by shape only.
- What the +4 flag of the pointer watch meant in the debug build.

### 1.7 The table

Columns: index, call sites, files, source groups (e event, m monster AI, b battle field, s
system_01), arguments in push order, result, whether it blocks, the names, meaning, lockstep flags,
mark.

| sys | uses | files | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 673 | 88 | e | slot:int, x:int, y:int, var:int | int 1 | no | `FFX_AtelSys_Sys_000_DebugWatchVar_resi` | debug overlay entry: ctx+0xA4+8*slot (slot clamped 0..31) gets byte (x/8)&0x3F|0x80, byte (y/4)&0x7F, the caller's actor id (word at actor+46) and the raw var reference. A negative x or y clears the entry. Nothing in the binary reads ctx+0xA4..0x1A3 back | debug | V |
| 1 | 183 | 17 | e,b | rad:float | float | no | `FFX_AtelSys_Sys_001_Sin_resf` | sub_88C3A0, the CRT sin | float | V |
| 2 | 198 | 17 | e,b | rad:float | float | no | `FFX_AtelSys_Sys_002_Cos_resf` | sub_88B3E0, the CRT cos | float | V |
| 3 | 0 | 0 |  | rad:float | float | no | dead `FFX_AtelSys_Sys_003_Tan_resf` | sub_88C420, tan | float | V |
| 4 | 0 | 0 |  | v:float | float | no | dead `FFX_AtelSys_Sys_004_Atan_resf` | (float)atan(v) | float | V |
| 5 | 3 | 1 | e | y:float, x:float | float | no | `FFX_AtelSys_Sys_005_Atan2_resf` | (float)atan2(y, x). Only test23 calls it | float | V |
| 6 | 4979 | 49 | e | v:float | float | no | `FFX_AtelSys_Sys_006_Sqrt_resf` | (float)sqrt(v), the heaviest Sys row | float | V |
| 7 | 0 | 0 |  | v:float | float | no | dead `FFX_AtelSys_Sys_007_RSqrt_resf` | 1 / sqrt(v) | float | V |
| 8 | 0 | 0 |  | angle:int | int | no | dead `FFX_AtelSys_Sys_008_FixedSin_resi` | sub_88C380: table sine from word_C54E40 through sub_88C690, 0x8000 units per turn |  | V |
| 9 | 0 | 0 |  | angle:int | int | no | dead `FFX_AtelSys_Sys_009_FixedCos_resi` | sub_88C120: the table cosine twin of sys:8 |  | V |
| 10 | 33 | 4 | e | v:any | int | no | `FFX_AtelSys_Sys_010_ToInt_resi` | returns FFX_Atel_PopInt, so a float operand comes back truncated toward zero |  | V |
| 11 | 0 | 0 |  | v:int | int | no | dead `FFX_AtelSys_Sys_011_IntShl15_resi` | v << 15 |  | V |
| 12 | 0 | 0 |  | v:float | int | no | dead `FFX_AtelSys_Sys_012_FloatToFixed15_resi` | (int)(v * 32768.0) |  | V |
| 13 | 0 | 0 |  | rad:float | int | no | dead `FFX_AtelSys_Sys_013_RadToAngle32768_resi` | sub_88C040: radians to 0x8000 units per turn |  | V |
| 14 | 0 | 0 |  | rad:float | int | no | dead `FFX_AtelSys_Sys_014_RadToAngle4096_resi` | sub_88BFE0: radians to 4096 units per turn |  | V |
| 15 | 0 | 0 |  | angle:int | float | no | dead `FFX_AtelSys_Sys_015_Angle32768ToRad_resf` | sub_88C0E0, the inverse of sys:13 |  | V |
| 16 | 0 | 0 |  | angle:int | float | no | dead `FFX_AtelSys_Sys_016_Angle4096ToRad_resf` | sub_88C0A0, the inverse of sys:14 |  | V |
| 17 | 0 | 0 |  | x1:float, z1:float, x2:float, z2:float | float | no | dead `FFX_AtelSys_Sys_017_Atan2Between2D_resf` | sub_88B430: the heading from one XZ point to another | float | V |
| 18 | 0 | 0 |  | yawVar:int, pitchVar:int, x1:float, y1:float, z1:float, x2:float, y2:float, z2:float | float yaw | no | dead `FFX_AtelSys_Sys_018_AnglesBetween3DToVars_resf` | sub_88B4D0 writes the yaw and pitch from point 1 to point 2 into two script variables and the row returns the yaw | float | V |
| 19 | 223 | 12 | e,b | rad:float | float | no | `FFX_AtelSys_Sys_019_WrapAnglePi_resf` | sub_88BC20: wraps into -pi..pi |  | V |
| 20 | 0 | 0 |  | from:float, to:float, step:float | float | no | dead `FFX_AtelSys_Sys_020_TurnAngleToward_resf` | sub_88B570: steps one angle toward another by at most step |  | I |
| 21 | 79 | 5 | e | a:float, b:float, c:float, d:float, e:float, f:float | int bool | no | `FFX_AtelSys_Sys_021_InFacingCone_resi` | FFX_Atel_TestFacingCone with the six floats in push order | float | V |
| 22 | 0 | 0 |  | r:float, x1:float, y1:float, z1:float, x2:float, y2:float, z2:float | int bool | no | dead `FFX_AtelSys_Sys_022_InSphere_resi` | FFX_DistSqLE: 1 when the squared distance between the points is at most r * r |  | V |
| 23 | 0 | 0 |  | a:int, b:int, n:int, t:float | float 0 | no | dead `FFX_AtelSys_Sys_023_SplineSampleToVar_resf` | sub_88A4A0 with two resolved script variable addresses, a count and t. The spline reading comes from the helper shape | local | I |
| 24 | 0 | 0 |  | var:int, a:float, b:float, len:float | float 0 | no | dead `FFX_AtelSys_Sys_024_PolarToVecVar_resf` | sub_88A3A0 writes a vector built from two angles and a length into a script variable | local | I |
| 25 | 97 | 16 | e | v:float | float | no | `FFX_AtelSys_Sys_025_Fabs_resf` | (float)fabs(v) |  | V |
| 26 | 505 | 10 | e | x1:float, z1:float, x2:float, z2:float | float | no | `FFX_AtelSys_Sys_026_Dist2D_resf` | sqrt of the XZ squared distance | float | V |
| 27 | 354 | 29 | e | x1:float, y1:float, z1:float, x2:float, y2:float, z2:float | float | no | `FFX_AtelSys_Sys_027_Dist3D_resf` | sqrt of the XYZ squared distance | float | V |
| 28 | 5 | 2 | e | slot:int, var:int | int 0 | no | `FFX_AtelSys_Sys_028_DebugWatchPtrMode0_resi` | sub_878140: ctx+0x1A4+8*slot (slot clamped 0..7) = the resolved address of var, or 0, then +4 = 0. No reader in the binary | debug | V |
| 29 | 0 | 0 |  | slot:int, var:int | int 0 | no | dead `FFX_AtelSys_Sys_029_DebugWatchPtrMode1_resi` | as sys:28 with +4 = 1 | debug | V |

## 2. Sg, library 4

### 2.1 Short version

- **44 of the 71 rows are called, 27 are not.** One of the 27 is the null row sg:2 (start none, poll
  5, no getter). 25,955 call sites: 23,321 in 391 event packages, 2,621 in 542 battle field files, 6 in
  3 monster AI files and 7 in system_01. Every call is statement form (0x58). [D]
- **Sg is the screen and graphics library**, plus a handful of player and map parameters that
  ended up in it. Fades, tints, motion blur, depth of field, the directional and point lights, the
  offscreen and accumulation buffers, draw enables, water level, ground type remap, footstep sounds,
  wind, blob shadows, and the native menu. [V]
- **Two rows already had names, and they are right.** sg:29 `sgMenuInit`, `sgMenuExec`,
  `sgMenuResult`, and sg:49 `j_maybe_FFX_Player__isMode2`. Section 2.3. [V]
- **sg:63 and sg:64 put the camera yaw into player control.** Both latch `j_FFX_Came_GetYaw` into
  `g_ffxPlayerCamYaw`, which `FFX_Player__stepControl` reads at 0x82D7A9. sg:64 is 397 calls in 185
  packages. With a per player camera this is the stick interpretation frame question again. Section
  2.5. [V]
- **sg:29 opens a native menu that runs on the local pad** and returns its choice as the row's
  result. 1,007 calls in 147 packages, including the title. [V]
- **Five rows write single slot player parameters** (22, 34, 48, 62, 69): walk, run and swim speeds,
  keep speed, a mode bit, a fixed yaw and swim rise. One global each, for whoever is the player. [V]
- **The rest is render state**, with simulation reach only through the water level (1, 60) and the
  ground type remap (23), which scripts set the same way on every peer. [V]

### 2.2 Census [D]

| source | files | files with Sg | Sg calls |
|---|---|---|---|
| event packages | 397 | 391 | 23,321 |
| battle field files | 816 | 542 | 2,621 |
| monster AI | 361 | 3 | 6 |
| system_01 | 1 | 1 | 7 |

No call names an index past row 70. Rows 71..215 of an Sg operand would alias Ch rows
(`ATEL_CONTEXT.md` 13.3) and no shipped script uses one. [D]

The heaviest rows: sg:19 SetLight 12,490 in 892 files (10,343 event, 2,147 battle field), sg:5
FadeOutToBlack 2,651, sg:4 FadeInFromBlack 2,443, sg:10 SetMotionBlur 1,246 (the only row in monster
AI and system_01), sg:29 sgMenu 1,007, sg:26 SetAccBufAlpha 872, sg:23 SetGroundTypeRemap 750, sg:13
WaitScreenFade 685 and sg:59 SetFootstepSound 577 in 522 files, 451 of those battle fields.

Single package rows: 27, 28, 36, 42, 58, 63. Single call sites: 36, 42, 63. The title calls 1, 4, 5,
10, 12, 13, 20, 25, 26, 29 and 32. The Besaid event packages call 29 rows, including the point light
set (35, 36, 37, 42).

Dead rows, 27: 0, 2 (null), 16, 17, 18, 24, 30, 33, 38, 39, 40, 41, 43, 44, 45, 46, 47, 48, 49, 50, 51,
53, 55, 56, 61, 65, 66. Most of the point light accessors are dead, only Alloc, Free, Set and
SetEnabled are used.

Row shapes, matching `ATEL_CONTEXT.md` 13.2: 63 rows int getter only, 5 float getter only (45, 51, 52,
53, 57), sg:29 start, poll and int getter, sg:13 start and poll, sg:2 null. Handlers sit at
0xA77880..0xA785B0, none shared. [V]

The arity column is the handler's pop count, and the bytecode estimate agrees on every used row
(sg:19 shows 6 on 12,482 of its 12,490 sites, the rest are calls the walk could not size).

### 2.3 Names already there

Kept as they are, and checked against the bodies [V]:

- sg:29 `sgMenuInit 0xA78210`, `sgMenuExec 0xA78270`, `sgMenuResult 0xA782A0`. Init pops the mode,
  calls `FFX_MenuSys_RequestOpen(mode)` (for modes 0x4007000F..0x40070011 only when `sub_860A90`
  agrees) and sets `dword_22FB4D4 = 1`. Exec decrements it and finishes on the second poll. Result
  returns `sub_821740`, `g_ffxMenuEnterResult`. ebp.py calls it sgMenu.
- sg:49 `j_maybe_FFX_Player__isMode2 0xA77970`, a thunk. Dead. ebp.py calls it Player_isMode2.

`tools/ebp.py` has `LIB_INFO` 4 = (0xC88D88, 71, 70) and three Sg names: 29 sgMenu, 37 lgt (sg:37
SetPointLight here) and 49 Player_isMode2. All agree with the bodies.

Helper names under the rows that could be sharper (not wrong, not table slots, hand edits if wanted):
the two depth of field helpers `sub_82CB60` and `sub_82CE20` are unnamed in the scratch copy.
`CAMERA_BUILD.md` lists them as tentative `FFX_Ch_SetLightDirty` and `FFX_Ch_SetLightSetId`, and
`VISUAL_CENSUS.md` reads the globals they write as the depth of field enable and focus depths. This
note follows `VISUAL_CENSUS.md`, see 2.6.

### 2.4 Families

- **Fades and tints**, 3 to 13. sg:4 to 7 are `FFX_ScreenFade_Request(frames, in, white)`, fade types
  2 to 5. sg:3 fades to a colour (type 6), sg:8 and 9 set an additive or subtractive tint (types 7 and
  8, level 0 clears). sg:11 ends the fade, sg:12 drops the tint, sg:13 waits until the counter
  `word_130074C` reaches zero. The fade channel is one global for the whole game, the same one battle
  start, the random encounter, `FFX_Map_WarpToWithSavedFade` and the battle result use.
- **Lights.** sg:19 SetLight, the directional light by index with a colour and two angles in degrees,
  through `sub_836310` and `sub_836430` (magic host 589 and 590). sg:17 and 18 are dead split
  setters. Point lights, 35 to 51: Alloc frees the old set and allocates 44 bytes per light, records
  in `dword_13008CC`, then Set, SetEnabled and many dead accessors. sg:39, 41 and 55 write through raw
  pointers, not script variable references.
- **Buffers and passes.** sg:10 motion blur (the helper came:125 also reaches), sg:20 and 21 depth of
  field, sg:25 and 26 the accumulation buffer, sg:27, 28 and 58 the offscreen buffer for map and CHRs,
  sg:57 the offscreen clear, sg:52 alpha pass order, sg:14 and 15 map and CHR draw enables (read by
  `FFX_MainStep`), sg:70 the script effects pass.
- **Player parameters.** sg:22 speeds by kind (1 walk, 2 run, 18 swim, 19 the second swim), sg:34
  keep speed, sg:48 a mode bit, sg:62 fixed yaw, sg:63 and 64 camera yaw and heading offset, sg:69
  swim rise.
- **Map parameters.** sg:1 and 60 water level and its max, sg:23 ground type remap (read by
  `FFX_Map_GroundTypeOf` and the triangle block test), sg:59 footstep sound per ground type, sg:54
  wind (read by `FFX_Ch_StepSecondaryPhysics`), sg:67 blob shadow parameters.
- **Residency.** sg:31 to 33 free CHR cache entries, sg:30 is a dead dev memory switch.
- **Stubs.** sg:0, 65, 66 return 0, sg:16 and 68 pop and drop, sg:53 calls a getter and returns 0.0,
  sg:56 prints a cache dump, sg:61 writes `byte_13019B5`, which nothing reads.

### 2.5 Lockstep hazards

- **camread, sg:63 and 64.** `FFX_Player__syncCamYaw 0x82D930` and `FFX_Player__setHeadingOffsetDeg
  0x82D950` store `j_FFX_Came_GetYaw` into `g_ffxPlayerCamYaw 0x130079C`. sg:64 also sets
  `g_ffxPlayerHeadingOffset = wrap(deg) + g_ffxPlayerDesiredHeading`. `FFX_Player__stepControl` reads
  the yaw at 0x82D7A9 and writes it itself at 0x82D6AE, 0x82D6E5, 0x82D6F2 and 0x82D725, so the camera
  yaw reaches player movement every step anyway, and these rows add explicit latch points. With one
  shared camera, nothing differs. With a per player camera, the heading the stick is read in differs
  per peer, which is the interpretation frame problem the lockstep notes already describe. The yaw
  has to be replicated with the input, or the player's control frame has to come from a camera every
  peer computes the same. [V] mechanism, [I] the size of the effect.
- **input, sg:29.** The menu runs on the local pad and its result comes back to the script. In
  shipped data most calls open mode 0x40200000 between a fade out and a fade in, and the following
  code rarely reads R, but the menu itself edits party and inventory state, so a menu session is
  simulation input. Whose pad drives it, and how its edits reach the other peers, is a co-op design
  decision. What each mode opens was not traced here. [V] open and result, [I] content.
- **global, the player parameters.** sg:22, 34, 48, 62 and 69 write one global each that
  `FFX_Player__stepControl` or the motion step reads for the controlled character. With more than one
  controlled body these are one slot caches of the kind the single instance audit looks for. [V]
- **sim.** sg:1, 60 (water) and 23 (ground type remap, which changes collision) are simulation state
  set by scripts, the same on every peer. A port has to carry them. [V]
- **wait, sg:13.** The fade counter is stepped by `FFX_Sg_StepScreenFades`, called from
  `FFX_MainStep` at 0x8214EA, so the wait is a step count. It only differs if a peer starts a fade the
  others do not, which the shared fade channel makes possible when one peer runs a battle start or
  warp fade on its own. [V]
- **heap, sg:35 and 36.** `FFX_MemAlloc(44 * count)` and the free. Allocation order matters to the
  heap layout, the same on peers that run the same scripts. [V]
- **cache, sg:31 and 32.** Freeing CHR cache entries changes what a later load finds resident, which
  can change how long a load wait lasts. [I]
- **global, hard coded.** sg:26 `sub_82BD10` has a case for event 258. [V]
- No RNG, no wall clock, no language, no FMV. [V]

### 2.6 Open

- What each sgMenu mode opens, and what `sub_860A90` checks for modes 0x4007000F..11.
- sg:17 and 18 split direction and colour by the helper shape only.
- Whether wind (sg:54) and secondary physics feed anything a state hash sees.
- The came:126 and 127 names. came:126 and 127 (`ATEL_CAME_LIBRARY.md`) reach the same `sub_82CE20`
  and `sub_82CB60` as sg:20 and 21, and that note calls the values light set ids. `VISUAL_CENSUS.md`
  and `FFX_Sg_StepScreenFades` read them as depth of field. One of the two notes is wrong, and the
  depth of field reading has the reader on its side. Worth a recheck before the Came names are
  applied live.

### 2.7 The table

| sg | uses | files | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 0 |  |  | int 0 | no | dead `FFX_AtelSys_Sg_000_ReturnZero_resi` | returns 0 |  | V |
| 1 | 145 | 50 | e | y:float | int 0 | no | `FFX_AtelSys_Sg_001_SetWaterLevel_resi` | FFX_Map_SetWaterLevel: g_ffxWaterLevel, read by FFX_Ch_UpdateMotionAll and the auto swim animation | sim | V |
| 2 | 0 | 0 |  |  |  |  | null row (start none, poll 5, no getter) | nothing to name |  | V |
| 3 | 219 | 52 | e | frames:int, r:int, g:int, b:int | int 0 | no | `FFX_AtelSys_Sg_003_ScreenColorFade_resi` | sub_82CCA0: screen fade channel type 6 at 0x1300728 toward the colour | render | V |
| 4 | 2443 | 372 | e | frames:int | int 0 | no | `FFX_AtelSys_Sg_004_FadeInFromBlack_resi` | FFX_ScreenFade_Request(frames, 1, 0), type 2 | render | V |
| 5 | 2651 | 308 | e | frames:int | int 0 | no | `FFX_AtelSys_Sg_005_FadeOutToBlack_resi` | FFX_ScreenFade_Request(frames, 0, 0), type 3 | render | V |
| 6 | 151 | 78 | e | frames:int | int 0 | no | `FFX_AtelSys_Sg_006_FadeInFromWhite_resi` | FFX_ScreenFade_Request(frames, 1, 1), type 4 | render | V |
| 7 | 195 | 83 | e | frames:int | int 0 | no | `FFX_AtelSys_Sg_007_FadeOutToWhite_resi` | FFX_ScreenFade_Request(frames, 0, 1), type 5 | render | V |
| 8 | 68 | 38 | e | r:int, g:int, b:int, level:int | int 0 | no | `FFX_AtelSys_Sg_008_SetScreenTint_resi` | sub_82CC20: additive tint, fade type 7, level 0 clears | render | V |
| 9 | 67 | 19 | e | r:int, g:int, b:int, level:int | int 0 | no | `FFX_AtelSys_Sg_009_SetScreenTintSubtract_resi` | sub_82CC80: the subtractive twin, type 8 | render | V |
| 10 | 1246 | 174 | e,m,b,s | strength:int | int 0 | no | `FFX_AtelSys_Sg_010_SetMotionBlur_resi` | sub_82BEE0, the same blur helper came:125 reaches | render | V |
| 11 | 4 | 3 | e |  | int 0 | no | `FFX_AtelSys_Sg_011_EndScreenFade_resi` | sub_82CC00: word_130074C = 0, the fade counter | render | V |
| 12 | 115 | 56 | e |  | int 0 | no | `FFX_AtelSys_Sg_012_ClearScreenTint_resi` | sub_82CC70: drops the tint channel | render | V |
| 13 | 685 | 210 | e |  | none | yes | `FFX_AtelSys_Sg_013_WaitScreenFade_start`, `FFX_AtelSys_Sg_013_WaitScreenFade_poll` | start is empty, poll is sub_82CC10() == 0, the fade counter FFX_Sg_StepScreenFades counts down | wait | V |
| 14 | 36 | 16 | e | on:int | int 0 | no | `FFX_AtelSys_Sg_014_SetMapDrawEnabled_resi` | FFX_SetMapDrawEnabled, read by FFX_MainStep for the draw | render | V |
| 15 | 24 | 11 | e | on:int | int 0 | no | `FFX_AtelSys_Sg_015_SetChrDrawEnabled_resi` | FFX_SetChrDrawEnabled, read by FFX_MainStep for the draw | render | V |
| 16 | 0 | 0 |  | a:int, b:float, c:float | int 0 | no | dead `FFX_AtelSys_Sg_016_PopDiscard_resi` | pops three values and does nothing |  | V |
| 17 | 0 | 0 |  | i:int, x:float, y:float, z:float | int 0 | no | dead `FFX_AtelSys_Sg_017_SetLightDir_resi` | sub_8364F0 on the directional light record i | render | I |
| 18 | 0 | 0 |  | i:int, r:float, g:float, b:float | int 0 | no | dead `FFX_AtelSys_Sg_018_SetLightColor_resi` | sub_836400 on the directional light record i | render | I |
| 19 | 12490 | 892 | e,b | i:int, r:int, g:int, b:int, yawDeg:int, pitchDeg:int | int 0 | no | `FFX_AtelSys_Sg_019_SetLight_resi` | sub_836520 -> sub_836310 and sub_836430 (magic host 589 and 590): colour 0..255 and direction in degrees for light i | render | V |
| 20 | 340 | 80 | e | on:int | int handle | no | `FFX_AtelSys_Sg_020_SetDofEnabled_resi` | sub_82CB60: dword_130076C = on & 1, the depth of field enable FFX_Sg_StepScreenFades reads | render | V |
| 21 | 367 | 48 | e | which:int, z:int | int 0 | no | `FFX_AtelSys_Sg_021_SetDofDepth_resi` | sub_82CE20: dword_1300758[which] = z plus a bit in dword_1300768, the focus depths StepScreenFades hands to the renderer | render | V |
| 22 | 270 | 44 | e | kind:int, value:float | int 0 | no | `FFX_AtelSys_Sg_022_SetPlayerSpeedParam_resi` | FFX_Player__setSpeedParam: kind 1 walk, 2 run, 18 swim, 19 the second swim speed, one global each | sim global | V |
| 23 | 750 | 130 | e | from:int, to:int | int 0 | no | `FFX_AtelSys_Sg_023_SetGroundTypeRemap_resi` | FFX_Map_SetGroundTypeRemap, read by FFX_Map_GroundTypeOf and the triangle block test | sim | V |
| 24 | 0 | 0 |  | from:int | int | no | dead `FFX_AtelSys_Sg_024_GetGroundTypeRemap_resi` | FFX_Map_GetGroundTypeRemap |  | V |
| 25 | 437 | 107 | e,b | unused:int | int 0 | no | `FFX_AtelSys_Sg_025_BeginAccBufCapture_resi` | pops and drops one int, then sub_82BDF0 starts the accumulation buffer capture | render | V |
| 26 | 872 | 107 | e,b | level:int, frames:int | int 0 | no | `FFX_AtelSys_Sg_026_SetAccBufAlpha_resi` | sub_82BD10 (magic host 693): the accumulation blend level, tweened over frames, with a hard coded case for event 258 | render global | V |
| 27 | 4 | 1 | e | on:int | int 0 | no | `FFX_AtelSys_Sg_027_SetOffscreenComposite_resi` | sub_82BCB0: byte_1300776, FFX_Gs_CompositeOffscreenBuffer | render | V |
| 28 | 2 | 1 | e | on:int | int 0 | no | `FFX_AtelSys_Sg_028_SetMapToOffscreen_resi` | sub_82BE10: byte_1300777, FFX_Gs_RedirectMapToOffscreen | render | V |
| 29 | 1007 | 147 | e | mode:int | int menu result | yes | kept `sgMenuInit`, kept `sgMenuExec`, kept `sgMenuResult` | sgMenuInit calls FFX_MenuSys_RequestOpen(mode) (modes 0x4007000F..11 only when sub_860A90 agrees) and sets dword_22FB4D4 = 1, sgMenuExec finishes on the second poll, sgMenuResult returns sub_821740 (g_ffxMenuEnterResult) | input | V |
| 30 | 0 | 0 |  | op:int | int | no | dead `FFX_AtelSys_Sg_030_DevMemOp_resi` | sub_8467B0: op 0 loads the dev maps sugimoto and kaisou from the host, 2 allocates 64 KB into dword_13036B8, 3 frees it and requests a CHR memory move | heap disk | V |
| 31 | 62 | 27 | e |  | int 0 | no | `FFX_AtelSys_Sg_031_FreeUnlockedChrCache_resi` | j_FFX_ChCache_FreeUnlockedEntries | cache | V |
| 32 | 104 | 99 | e |  | int 0 | no | `FFX_AtelSys_Sg_032_FreeStaleChrCache_resi` | j_FFX_ChCache_FreeStaleUnlocked | cache | V |
| 33 | 0 | 0 |  | id:int | int | no | dead `FFX_AtelSys_Sg_033_FreeChrCacheForId_resi` | FFX_ChCache_FreeAllForId | cache | V |
| 34 | 31 | 16 | e | on:int | int | no | `FFX_AtelSys_Sg_034_SetMotionKeepSpeed_resi` | FFX_Mot_SetKeepSpeed, one global the motion step reads | sim global | V |
| 35 | 2 | 2 | e | count:int | int 0 | no | `FFX_AtelSys_Sg_035_AllocPointLights_resi` | sub_831950 (magic host 400): frees the old set, FFX_MemAlloc(44 * count) into dword_13008CC, count in dword_13008D0 | heap | V |
| 36 | 1 | 1 | e |  | int 0 | no | `FFX_AtelSys_Sg_036_FreePointLights_resi` | sub_831A30 | heap | V |
| 37 | 3 | 2 | e | i:int, x:float, y:float, z:float, radius:float, r:int, g:int, b:int, group:int | int 0 | no | `FFX_AtelSys_Sg_037_SetPointLight_resi` | sub_8319B0 fills point light record i. ebp.py's lgt | render | V |
| 38 | 0 | 0 |  | i:int, mask:int, x:float, y:float, z:float | int 0 | no | dead `FFX_AtelSys_Sg_038_SetPointLightPosMasked_resi` | sub_831CA0: the axes picked by mask | render | I |
| 39 | 0 | 0 |  | i:int, xPtr:int, yPtr:int, zPtr:int | int 0 | no | dead `FFX_AtelSys_Sg_039_GetPointLightPosToPtrs_resi` | sub_831B30 writes through raw pointers, not var references | local | I |
| 40 | 0 | 0 |  | i:int, r:int, g:int, b:int | int 0 | no | dead `FFX_AtelSys_Sg_040_SetPointLightColor_resi` | sub_831C10 | render | V |
| 41 | 0 | 0 |  | i:int, rPtr:int, gPtr:int, bPtr:int | int 0 | no | dead `FFX_AtelSys_Sg_041_GetPointLightColorToPtrs_resi` | sub_831A60 writes through raw pointers | local | I |
| 42 | 1 | 1 | e | i:int, on:int | int 0 | no | `FFX_AtelSys_Sg_042_SetPointLightEnabled_resi` | sub_831C60 | render | V |
| 43 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_Sg_043_GetPointLightEnabled_resi` | sub_831AD0 |  | V |
| 44 | 0 | 0 |  | i:int, r:float | int 0 | no | dead `FFX_AtelSys_Sg_044_SetPointLightRadius_resi` | sub_831CE0 | render | V |
| 45 | 0 | 0 |  | i:int | float | no | dead `FFX_AtelSys_Sg_045_GetPointLightRadius_resf` | sub_831B80 |  | V |
| 46 | 0 | 0 |  | i:int, group:int | int 0 | no | dead `FFX_AtelSys_Sg_046_SetPointLightGroup_resi` | sub_831C80 | render | V |
| 47 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_Sg_047_GetPointLightGroup_resi` | sub_831B00 |  | V |
| 48 | 0 | 0 |  | on:int | int 0 | no | dead `FFX_AtelSys_Sg_048_SetPlayerMode2_resi` | maybe_FFX_Player__setMode2 | sim global | I |
| 49 | 0 | 0 |  |  | int bool | no | kept `j_maybe_FFX_Player__isMode2` | The thunk returns maybe_FFX_Player__isMode2. ebp.py calls it Player_isMode2 |  | V |
| 50 | 0 | 0 |  | i:int, f:float | int 0 | no | dead `FFX_AtelSys_Sg_050_SetPointLightFalloff_resi` | sub_831BF0, the field reading as falloff is from the record layout | render | I |
| 51 | 0 | 0 |  | i:int | float | no | dead `FFX_AtelSys_Sg_051_GetPointLightFalloff_resf` | sub_831A40 |  | I |
| 52 | 93 | 62 | e,b | on:int | float 0 | no | `FFX_AtelSys_Sg_052_SetMapAlphaPassFirst_resf` | FFX_SetMapAlphaPassFirst, draw order | render | V |
| 53 | 0 | 0 |  |  | float 0 | no | dead `FFX_AtelSys_Sg_053_GetMapAlphaPassFirst_resf` | calls FFX_GetMapAlphaPassFirst and returns 0.0 anyway |  | V |
| 54 | 32 | 14 | e | x:float, y:float, z:float | int 0 | no | `FFX_AtelSys_Sg_054_SetWind_resi` | sub_824E30: flt_12FBC44.., read by FFX_Ch_StepSecondaryPhysics | render | I |
| 55 | 0 | 0 |  | xPtr:int, yPtr:int, zPtr:int | int 0 | no | dead `FFX_AtelSys_Sg_055_GetWindToPtrs_resi` | sub_824DD0 writes through raw pointers | local | V |
| 56 | 0 | 0 |  |  | int 0 | no | dead `FFX_AtelSys_Sg_056_DebugCacheDump_resi` | sub_83BB50, a Cache Dump print |  | V |
| 57 | 3 | 2 | e | mode:int, r:int, g:int, b:int | float 0 | no | `FFX_AtelSys_Sg_057_SetOffscreenClear_resf` | sub_82BDC0, read by FFX_Gs_BeginFrameDrawEnv | render | V |
| 58 | 2 | 1 | e | on:int | int 0 | no | `FFX_AtelSys_Sg_058_SetChrToOffscreen_resi` | sub_82BE00: byte_1300778, FFX_Gs_SetChrFrameTarget | render | V |
| 59 | 577 | 522 | e,b | groundType:int, sound:int | int 0 | no | `FFX_AtelSys_Sg_059_SetFootstepSound_resi` | FFX_Map_SetFootstepSound, audio only | render | V |
| 60 | 12 | 5 | e | y:float | int 0 | no | `FFX_AtelSys_Sg_060_SetWaterLevelMax_resi` | FFX_Map_SetWaterLevelMax | sim | V |
| 61 | 0 | 0 |  | v:int | int 0 | no | dead `FFX_AtelSys_Sg_061_SetUnreadByte13019B5_resi` | sub_83C520 writes byte_13019B5, which nothing reads |  | V |
| 62 | 18 | 9 | e | on:int, deg:float | int 0 | no | `FFX_AtelSys_Sg_062_SetPlayerFixedYaw_resi` | FFX_Player__setFixedYawDeg: g_ffxPlayerUseFixedYaw and g_ffxPlayerFixedYaw, which replace the camera yaw in stepControl | sim global | V |
| 63 | 1 | 1 | e |  | int 0 | no | `FFX_AtelSys_Sg_063_SyncPlayerCamYaw_resi` | FFX_Player__syncCamYaw: g_ffxPlayerCamYaw = j_FFX_Came_GetYaw, heading offset 0 | camread | V |
| 64 | 397 | 185 | e | deg:float | int 0 | no | `FFX_AtelSys_Sg_064_SetPlayerHeadingOffsetDeg_resi` | FFX_Player__setHeadingOffsetDeg: latches the camera yaw into g_ffxPlayerCamYaw, then g_ffxPlayerHeadingOffset = wrap(deg) + g_ffxPlayerDesiredHeading | camread sim | V |
| 65 | 0 | 0 |  |  | int 0 | no | dead `FFX_AtelSys_Sg_065_ReturnZero_resi` | returns 0 |  | V |
| 66 | 0 | 0 |  |  | int 0 | no | dead `FFX_AtelSys_Sg_066_ReturnZero_resi` | returns 0 |  | V |
| 67 | 10 | 5 | e,b | a:int, b:int | int 0 | no | `FFX_AtelSys_Sg_067_SetBlobShadowParams_resi` | sub_830120, read by FFX_Ch_BuildBlobShadow | render | I |
| 68 | 2 | 2 | e,b | a:int, b:int, c:int | int 0 | no | `FFX_AtelSys_Sg_068_PopDiscard3_resi` | pops three ints and does nothing |  | V |
| 69 | 13 | 13 | e | on:int | int 0 | no | `FFX_AtelSys_Sg_069_SetSwimRiseEnabled_resi` | FFX_Player__setSwimRiseEnabled, read by stepControl | sim global | V |
| 70 | 3 | 3 | e | on:int | int 0 | no | `FFX_AtelSys_Sg_070_SetScriptEffectsPassOff_resi` | FFX_SetScriptEffectsPassOff, read by FFX_MainStep | render | I |

## 3. MapFx, library 8

### 3.1 Short version

- **63 of the 108 rows are called, 45 are not.** 50,989 call sites: 50,478 in 294 event packages, 451
  in 50 battle field files, 32 in one monster AI file and 28 in system_01. 55 sites use the
  expression form, all of them the getters 57, 59, 61, 66, 67, 68, 73, 74 and 75. No row is null. [D]
- **MapFx drives the map's own effects and backgrounds.** The placed particle set
  (`dword_D2C258`, groups, binds, one shots, waits), the rcBg layers (show, hide, position, frame,
  flags, scale), the rcBg scene camera, fog, the GS clear colour, the guide map model and two
  Blitzball hooks. [V]
- **One name was already there and is right**, mapfx:10 `FFX_AtelOp_MapParticleBindPos`. [V]
- **Two real hazards.** mapfx:15 ShowLayer can swap the layer for a language variant through
  `FFX_GetLanguage`, and the fog colour getters 66 to 68 (and the per channel setters 63 to 65) read
  the Phyre application's fog colour back. Section 3.5. [V]
- **The two waits are step counted** (mapfx:4 and 5 wait on a particle group's lifetime, stepped in
  `FFX_MainStep`), with one camera dependent corner case. [V]
- **mapfx:6 SetZone writes the save block** and carries a hard coded story case. [V]
- **Many hard coded cases**: scene 392, 382, events 212, 224, 320, 347, three level hashes for fog,
  magic 706 for the clear colour, and the title flag in `te/test20`. [V]

### 3.2 Census [D]

| source | files | files with MapFx | MapFx calls |
|---|---|---|---|
| event packages | 397 | 294 | 50,478 |
| battle field files | 816 | 50 | 451 |
| monster AI | 361 | 1 | 32 |
| system_01 | 1 | 1 | 28 |

No call names an index past row 107. [D]

The heaviest rows: mapfx:2 RestartOrStopGroup 17,654 in 185 packages, 9 BindGroupToSelf 7,719, 11
UnbindGroup 6,883, 10 MapParticleBindPos 3,953, 16 HideLayer 2,529, 103 LinkRcBgObjChrs 2,130 (all in
7 Blitzball packages), 13 SetGroupActive 2,090, 0 SetMapPartMaskBit 1,411 (the only row in monster AI
and system_01, plus 347 battle field sites), 15 ShowLayer 1,310 and 3 StartGroupOneShot 1,209.

Single package rows: 7, 46, 89, 92, 96, 102, 107. The title calls only mapfx:15. The Besaid event
packages call 0, 2, 3, 9, 11, 13, 29, 30, 31, 50, 55, 62 and 106.

Dead rows, 45: 14, 18, 19, 21 to 28, 35 to 37, 39 to 43, 49, 51, 52, 70 to 72, 76 to 78, 80 to 88, 90,
97 to 101, 104, 105.

Row shapes, matching `ATEL_CONTEXT.md` 13.2: 93 int getter only, 12 float getter only (21, 36, 37,
41, 51, 57, 59, 61, 80, 81, 86, 87), and 3 start and poll (4, 5, 6). mapfx:4's start calls mapfx:5's
start, but they are two functions, so no slot is shared. Handlers sit at 0x91AF30..0x91C890. [V]

The arity column is the handler's pop count, and the bytecode estimate agrees on every statement form
row it could size.

### 3.3 Names already there

Kept, and checked against the body [V]:

- mapfx:10 `FFX_AtelOp_MapParticleBindPos 0x91B0A0`. It pops the group as a float and truncates it,
  then three floats, and `sub_862920` binds the group to the calling actor at that offset. ebp.py
  calls it mpfpbindpos.

`tools/ebp.py` has `LIB_INFO` 8 = (0xC5DC90, 108, 107) and that one MapFx name.

Helper names that are vaguer than they could be (not table slots, hand edits if wanted) [V]:

- `maybe_FFX_MagicDraw_SetColorA 0x90A040` stores the clear colour `byte_18DF178..` that
  `FFX_Gs_BeginFrameDrawEnv` uses, and hands it to `sub_642E90` unless magic 706 is loaded. A
  sharper name is `FFX_MapFx_SetClearColor`.
- `maybe_FFX_MagicDraw_SetColorB 0x90A2C0` stores the fog colour `dword_18DF140`, and pushes it to
  the renderer in event 224. A sharper name is `FFX_MapFx_SetFogColor`.

### 3.4 Families

- **The placed particle set**, `dword_D2C258`: 80 byte records from the map with two group words (+68
  and +70 of the record, +100 and +102 of the header walk), 128 byte runtime records at `unk_D2C260`
  (+0 countdown, -4096 freed, +29 mode: 0 run, 1 stop, 2 one shot that turns into stop when the
  countdown ends), and 76 byte attach records at `unk_D30260` (flag 1 CHR, 2 map part, 8 map node,
  0x10 enabled, then target, bone, a matrix and a position). Rows 2, 3, 7, 8, 13, 53, 54, 93, 95 and
  96 start, stop, hide and kill groups on either group word. 4 and 5 wait. 9, 10, 11, 88, 89, 90, 91
  and 105 bind and place groups. mapfx:9 binds to the calling actor whatever its kind (CHR with a
  bone, map part, map node), up to four bindings per actor at actor+710. 12 gates the whole update.
- **rcBg layers**, 14 to 34, 46 to 49 and 76 to 87: 192 byte records counted by `sub_91A3D0`. Show
  and hide (bit 0 of +0), position (+12, +16, moved by the delta), frame (+150 of +152 frames),
  flag bits at +144, scale (+68, +72, with a tween), a second float pair (+76, +80, with a tween,
  meaning not traced) and a few raw fields. Most are dead. The used ones are 15, 16, 17, 20, 29 to 34,
  46, 47, 48 and 79.
- **rcBg scene**, 35 to 45 and 106: the scene camera offset (+184, +188), +172, two tweens (+192 and
  +220), a flag that relinks map mesh instances, and +260. Only 38, 44, 45 and 106 are used.
- **Fog**, 55 to 68: near, far and limit in `flt_18DF148`, `flt_18DF14C` and `flt_18DF144`
  (scaled by `flt_18DEFF8`), and the colour `dword_18DF140`.
- **Clear colour**, 69 to 75: `byte_18DF178..`.
- **Other**: 0 a map part mask bit, 1 and 97 to 100 magic draw flags and write only vectors, 6 the
  zone, 50 to 52 and 94 the guide map, 92 and 107 a post effect preset, 101 a map part bound check,
  102 a VFX time freeze, 103 and 104 the Blitzball rcBg object 0x20008.

### 3.5 Lockstep hazards

- **lang, mapfx:15.** `sub_6412B0(FFX_GetLanguage(), FFX_Level_GetNameHash())` looks the layer up in a
  language table (`sub_67FB30` on `dword_CCC81C`) and replaces it when a variant exists. Two peers with
  different languages show different background layers. The layers are rcBg backgrounds, so the
  effect is visual unless a script reads layer state back, and the readers (18, 19, 28) are dead.
  [V] dependency, [I] that it stays visual.
- **readback, mapfx:63 to 68.** `sub_908980` reads `dword_18DF140`, then always overwrites red,
  green and blue with the Phyre application floats +96, +100, +104 (+80, +84, +88 under magic draw
  flag 4) times 255. So mapfx:66 to 68 return renderer state, and the per channel setters 63 to 65
  store the renderer's other two channels back into the engine global. `mcfr0100` and `mcfr0200`
  store 66 to 68 into variables, the dome packages store 57, 59 and 61. Where those variables go was
  not followed. A port must reproduce what the Phyre application holds there, or replace the getters
  with the engine global. [V] mechanism, [I] consumers.
- **readback, mapfx:57, 59 and 61.** `sub_908BB0` returns the engine fog values, except in event 224,
  where it reads Phyre application +112, +116 and +124 instead. [V]
- **sim and global, mapfx:6.** `sub_874000` sets `g_ffxPendingZoneMode = 1` and
  `g_ffxPendingZoneNotify = zone`, and `FFX_SaveData_SetSceneAndSub` writes the zone into the save
  block. At level hash 5532, story 2020, zone 2 it also sets `dword_193BF68`. All keyed on simulation
  values, so every peer agrees. [V]
- **wait, mapfx:4 and 5.** The poll reads the group's first runtime record through `sub_72A760`. The
  countdown and the free happen in `FFX_MapFx_UpdatePlacedEffects`, reached from
  `FFX_MapFx_StepPlacedEffects` in `FFX_MainStep` at 0x82148C, so the wait is a step count. Two
  corners: the whole update is gated by `dword_CEC1F0` (mapfx:12 and core:550), so a wait never ends
  while it is off, and a record with group word 0 skips its countdown while `sub_728F10` culls it
  against the camera view matrix (except fixed cases for events 49, 310 and 374). A wait on group 0
  would therefore depend on the camera. The shipped waits seen name nonzero groups. [V] mechanism,
  [I] that no shipped wait uses group 0.
- **global, hard coded.** mapfx:0 forces bit 14 on in scene 392. mapfx:15 and 16 widen the layer
  bound to 28 in events 347 and 212, and `sub_91A830` raises `g_ffxTitleScreenActive` when the event
  name contains `te/test20`, which is how the title flag goes up. mapfx:17 nudges layer 26 in scene
  382. mapfx:56, 58 and 60 ignore the call at level hashes 3234, 7736 and 1689. Events 224 and 320
  push fog to the renderer directly. mapfx:69 skips the renderer under magic 706. All keyed on
  simulation values. [V]
- **render.** Everything else, including the Blitzball rcBg links. [V]
- No RNG, no wall clock, no pad, no FMV, no disk. [V]

### 3.6 Open

- What the layer fields +20, +36, +40, +67, +164 and the +76/+80 pair mean, and the scene tweens at
  +192 and +220. Rows 20, 38, 46 and 48 are used, so their meaning matters more than the dead ones.
- What sets the Phyre application fog colour outside events 224 and 320, and where the dome and mcfr
  packages send the values they read.
- Whether any shipped mapfx:4 or 5 call names group 0.
- Who reads `dword_230FD34` (mapfx:102) and the guide map parameter (mapfx:50).

### 3.7 The table

| mapfx | uses | files | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 1411 | 107 | e,m,b,s | bit:int, on:int | int bit | no | `FFX_AtelSys_MapFx_000_SetMapPartMaskBit_resi` | sub_88EBA0 sets or clears one bit of the mask sub_908960 returns and stores it through sub_90A2B0 (dword_18DED0C, magic host 557 and 560, also written by the map loaders). In scene 392 bit 14 is forced on | render global | V |
| 1 | 10 | 4 | e | on:int | int | no | `FFX_AtelSys_MapFx_001_SetMagicDrawFlag10_resi` | FFX_MagicDraw_SetFlags or ClearFlags(0x10) plus sub_643DF0 -> sub_66E7B0 on the gfx context | render | I |
| 2 | 17654 | 185 | e | group:int, restart:int | int | no | `FFX_AtelSys_MapFx_002_RestartOrStopGroup_resi` | restart nonzero: FFX_MapFx_ResetGroup. Zero: sub_729660 sets runtime +29 = 1 (stop) on every record of the placed particle set dword_D2C258 in that group | render | V |
| 3 | 1209 | 66 | e | group:int | int 1 | no | `FFX_AtelSys_MapFx_003_StartGroupOneShot_resi` | sub_729810: countdown 0 and +29 = 2, which UpdatePlacedEffects turns into a stop when the countdown runs out | render | V |
| 4 | 247 | 21 | e | group:int | none | yes | `FFX_AtelSys_MapFx_004_WaitGroupGone_start`, `FFX_AtelSys_MapFx_004_WaitGroupGone_poll` | pushes 1 and runs mapfx:5, so it waits until the group's first record is freed | wait | V |
| 5 | 227 | 14 | e | group:int, untilGone:int | none | yes | `FFX_AtelSys_MapFx_005_WaitGroup_start`, `FFX_AtelSys_MapFx_005_WaitGroup_poll` | poll on sub_72A760: keeps waiting while the record runs (+29 = 0), and with untilGone also while it is stopping, done when freed or missing | wait | V |
| 6 | 175 | 28 | e | zone:int | none | yes | `FFX_AtelSys_MapFx_006_SetZone_start`, `FFX_AtelSys_MapFx_006_SetZone_poll` | sub_874000: g_ffxPendingZoneMode = 1, g_ffxPendingZoneNotify = zone, and FFX_SaveData_SetSceneAndSub writes the zone into the save block. Level hash 5532 at story 2020 with zone 2 sets dword_193BF68. The poll returns 1, done at once | sim global | V |
| 7 | 1 | 1 | e |  | int 1 | no | `FFX_AtelSys_MapFx_007_ResetAllGroups_resi` | sub_729740: countdown 0 and +29 = 0 for every grouped record | render | V |
| 8 | 16 | 11 | e |  | int 1 | no | `FFX_AtelSys_MapFx_008_KillAllGroupedEffects_resi` | sub_7296A0: frees the prim list of every grouped record and marks it -4096 | render | V |
| 9 | 7719 | 151 | e | group:int, bone:int | int 1 | no | `FFX_AtelSys_MapFx_009_BindGroupToSelf_resi` | sub_862800: up to four bindings at actor +710, then by actor kind: 1 a CHR (sub_72A530 with the bone), 4 a map part (sub_72A590), 5 a map node (sub_72A5E0) | render | V |
| 10 | 3953 | 37 | e | group:float, x:float, y:float, z:float | int 1 | no | kept `FFX_AtelOp_MapParticleBindPos` | Pops the group as a float and truncates it, then sub_862920 binds at an offset. ebp.py's mpfpbindpos | render | V |
| 11 | 6883 | 130 | e | group:int | int 1 | no | `FFX_AtelSys_MapFx_011_UnbindGroup_resi` | sub_8628A0: drops that group (or every group for 0, negatives clamp to 0) from the actor's four slots and clears the attach record | render | V |
| 12 | 5 | 4 | e,b | on:int | int bool | no | `FFX_AtelSys_MapFx_012_SetPlacedEffectsActive_resi` | sub_729050: dword_CEC1F0, the gate of FFX_MapFx_UpdatePlacedEffects. The same global as core:550 | wait | V |
| 13 | 2090 | 144 | e,b | group:int, active:int | int | no | `FFX_AtelSys_MapFx_013_SetGroupActive_resi` (was applied as `..._SetGroupHidden_resi`, the sense was backwards) | active != 0: FFX_MapFx_ResumeGroup (clears runtime flag 0x10, the group runs), else FFX_MapFx_SuspendGroup (sets it, the update skips the group and unlinks its meshes). RCFLEX.md 7 | render | V |
| 14 | 0 | 0 |  |  | int | no | dead `FFX_AtelSys_MapFx_014_GetLayerCount_resi` | sub_91A3D0, the rcBg layer count (192 byte records) |  | V |
| 15 | 1310 | 38 | e | i:int | int 1 | no | `FFX_AtelSys_MapFx_015_ShowLayer_resi` | sub_6412B0(FFX_GetLanguage(), level hash) can swap the layer for a language variant, then sub_91A830 clears hidden bit 0. Raises g_ffxTitleScreenActive in te/test20, events 347 and 212 widen the bound to 28 | lang global | V |
| 16 | 2529 | 34 | e | i:int | int 1 | no | `FFX_AtelSys_MapFx_016_HideLayer_resi` | sub_91A7C0: sets hidden bit 0, the same 347 and 212 case | render global | V |
| 17 | 314 | 18 | e | i:int, x:int, y:int | int 1 | no | `FFX_AtelSys_MapFx_017_SetLayerPos_resi` | sub_91AB30: +12/+16 and moves the layer by the delta (sub_639D40). Layer 26 in scene 382 nudges y by one at two rows | render global | V |
| 18 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_018_GetLayerX_resi` | +12 |  | V |
| 19 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_019_GetLayerY_resi` | +16 |  | V |
| 20 | 4 | 4 | e | i:int, v:float | int 1 | no | `FFX_AtelSys_MapFx_020_SetLayerField20_resi` | sub_91ACC0: +20. Only bltz0201 | render | I |
| 21 | 0 | 0 |  | i:int | float | no | dead `FFX_AtelSys_MapFx_021_GetLayerField20_resf` | +20 |  | V |
| 22 | 0 | 0 |  | i:int, a:int, b:int | int 1 | no | dead `FFX_AtelSys_MapFx_022_SetLayerFields36And40_resi` | sub_91AC40: +36 and +40 | render | I |
| 23 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_023_GetLayerField36_resi` | +36 |  | V |
| 24 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_024_GetLayerField40_resi` | +40 |  | V |
| 25 | 0 | 0 |  | i:int, a:int, b:int | int 1 | no | dead `FFX_AtelSys_MapFx_025_SetLayerOrigin_resi` | sub_91AC80: +4 and +8, the pair the scale rows pass to sub_639D60 | render | I |
| 26 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_026_GetLayerOriginX_resi` | +4 |  | I |
| 27 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_027_GetLayerOriginY_resi` | +8 |  | I |
| 28 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_028_GetLayerFrameCount_resi` | +152 |  | V |
| 29 | 497 | 23 | e | i:int, frame:int | int 1 | no | `FFX_AtelSys_MapFx_029_SetLayerFrame_resi` | sub_91A930: the word at +150 | render | V |
| 30 | 291 | 28 | e | i:int | int 1 | no | `FFX_AtelSys_MapFx_030_SetLayerFlag1_resi` | +144 |= 1 | render | V |
| 31 | 62 | 21 | e | i:int | int 1 | no | `FFX_AtelSys_MapFx_031_ClearLayerFlag1_resi` | +144 &= ~1 | render | V |
| 32 | 12 | 4 | e | i:int | int 1 | no | `FFX_AtelSys_MapFx_032_SetLayerFlag2_resi` | +144 |= 2 | render | V |
| 33 | 53 | 5 | e | i:int | int 1 | no | `FFX_AtelSys_MapFx_033_ClearLayerFlag2_resi` | +144 &= ~2 | render | V |
| 34 | 67 | 5 | e | i:int, v:int | int 1 | no | `FFX_AtelSys_MapFx_034_SetLayerFlagNibble_resi` | sub_919FE0: clears +144 bits 8..11 and ORs v in as given | render | V |
| 35 | 0 | 0 |  | x:float, y:float | int 1 | no | dead `FFX_AtelSys_MapFx_035_SetSceneCamOffset_resi` | sub_91AD40: scene +184/+188, then FFX_RcBg_ApplySceneCamera | render | V |
| 36 | 0 | 0 |  |  | float | no | dead `FFX_AtelSys_MapFx_036_GetSceneCamOffsetX_resf` | scene +184 |  | V |
| 37 | 0 | 0 |  |  | float | no | dead `FFX_AtelSys_MapFx_037_GetSceneCamOffsetY_resf` | scene +188 |  | V |
| 38 | 19 | 13 | e | a:int, b:float, c:int, d:int, e:int | int 1 | no | `FFX_AtelSys_MapFx_038_StartSceneShake_resi` | FFX_RcBg_StartSceneShake: a sine shake of the scene camera, +192 mode, +196 speed, +200 frames, +204 amplitude, +208 random range, phase +212 and counter +216 zeroed. With a random range it draws CRT rand on the step thread (0x921172, `RCBG.md` 4.5) | render, CRT rand | V |
| 39 | 0 | 0 |  |  | int 1 | no | dead `FFX_AtelSys_MapFx_039_ResetSceneCamOffset_resi` | sub_91A900 -> sub_921200(0) | render | I |
| 40 | 0 | 0 |  | v:float | int 1 | no | dead `FFX_AtelSys_MapFx_040_SetSceneField172_resi` | sub_91AD10: scene +172, then ApplySceneCamera | render | V |
| 41 | 0 | 0 |  |  | float | no | dead `FFX_AtelSys_MapFx_041_GetSceneField172_resf` | scene +172 |  | V |
| 42 | 0 | 0 |  | a:int, b:float, c:int, d:float | int 1 | no | dead `FFX_AtelSys_MapFx_042_StartSceneTween220_resi` | FFX_RcBg_StartSceneRoll: the roll oscillator, +220 mode, +224 speed, +228 frames, +232 amplitude, phase +236 and counter +240 zeroed (`RCBG.md`) | render | V |
| 43 | 0 | 0 |  |  | int 1 | no | dead `FFX_AtelSys_MapFx_043_ResetSceneField172_resi` | sub_91A8D0 -> sub_920650(0) | render | I |
| 44 | 3 | 3 | e |  | int 1 | no | `FFX_AtelSys_MapFx_044_EnableSceneFlag0_resi` | sub_91A220: bit 0 of the rcBg object, then FFX_Gfx_RelinkMapMeshInstances | render | V |
| 45 | 5 | 4 | e,b |  | int 1 | no | `FFX_AtelSys_MapFx_045_DisableSceneFlag0_resi` | sub_91A200: clears bit 0, then sub_65F0E0 | render | V |
| 46 | 3 | 1 | e | i:int, v:int | int 1 | no | `FFX_AtelSys_MapFx_046_SetLayerField164_resi` | sub_91A9C0: +164. Only hiku0500 | render | I |
| 47 | 12 | 4 | e | i:int | int 1 | no | `FFX_AtelSys_MapFx_047_SetLayerLastFrame_resi` | sub_91A970: +150 = +152 - 1 | render | V |
| 48 | 300 | 19 | e | i:int, v:int | int 1 | no | `FFX_AtelSys_MapFx_048_SetLayerByte67_resi` | sub_91AA90: sub_639D20 and the byte at +67 | render | I |
| 49 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_049_GetLayerByte67_resi` | +67 |  | V |
| 50 | 117 | 117 | e | v:float | int 1 | no | `FFX_AtelSys_MapFx_050_SetGuideMapParam_resi` | sub_919DF0 -> sub_91D580 on the guide map model, about one call per package | render | I |
| 51 | 0 | 0 |  |  | float | no | dead `FFX_AtelSys_MapFx_051_GetGuideMapParam_resf` | sub_919CA0 -> sub_91D450 |  | I |
| 52 | 0 | 0 |  |  | int 1 | no | dead `FFX_AtelSys_MapFx_052_DebugDumpPlacedEffects_resi` | sub_919B70 -> sub_729060 |  | I |
| 53 | 377 | 14 | e | group2:int, restart:int | int | no | `FFX_AtelSys_MapFx_053_RestartOrStopGroup2_resi` | mapfx:2 keyed on the second group word (record +70) | render | V |
| 54 | 601 | 37 | e,b | group2:int, active:int | int | no | `FFX_AtelSys_MapFx_054_SetGroup2Active_resi` (was applied as `..._SetGroup2Hidden_resi`, the sense was backwards) | mapfx:13 keyed on the second group word: active != 0 resumes (0x91B7F0), MAP_PARTICLES.md 8 | render | V |
| 55 | 33 | 16 | e,b | near:float, far:float, limit:float | int 1 | no | `FFX_AtelSys_MapFx_055_SetFog_resi` | sub_90A350: flt_18DF148/14C scaled by flt_18DEFF8, flt_18DF144. Event 224 and event 320 (sub_91C8A0) also drive the renderer directly | render global | V |
| 56 | 10 | 10 | e,b | v:float | int 1 | no | `FFX_AtelSys_MapFx_056_SetFogNear_resi` | ignored when the level hash is 3234, 7736 or 1689 | render global | V |
| 57 | 8 | 7 | e |  | float | no | `FFX_AtelSys_MapFx_057_GetFogNear_resf` | sub_908BB0, which reads the Phyre application instead in event 224 | readback | V |
| 58 | 12 | 11 | e,b | v:float | int 1 | no | `FFX_AtelSys_MapFx_058_SetFogFar_resi` | the same three level skips | render global | V |
| 59 | 8 | 7 | e |  | float | no | `FFX_AtelSys_MapFx_059_GetFogFar_resf` | sub_908BB0 | readback | V |
| 60 | 20 | 15 | e,b | v:float | int 1 | no | `FFX_AtelSys_MapFx_060_SetFogLimit_resi` | the same three level skips | render global | V |
| 61 | 11 | 10 | e |  | float | no | `FFX_AtelSys_MapFx_061_GetFogLimit_resf` | sub_908BB0 | readback | V |
| 62 | 14 | 12 | e,b | r:int, g:int, b:int | int 1 | no | `FFX_AtelSys_MapFx_062_SetFogColor_resi` | FFX_MagicDraw_SetFogColor: dword_18DF140, pushed to the renderer in events 224 and 320 | render global | V |
| 63 | 23 | 11 | e,b | r:int | int 1 | no | `FFX_AtelSys_MapFx_063_SetFogColorR_resi` | reads the colour with sub_908980, replaces red, stores it back | readback | V |
| 64 | 35 | 16 | e,b | g:int | int 1 | no | `FFX_AtelSys_MapFx_064_SetFogColorG_resi` | as mapfx:63 for green | readback | V |
| 65 | 37 | 17 | e,b | b:int | int 1 | no | `FFX_AtelSys_MapFx_065_SetFogColorB_resi` | as mapfx:63 for blue | readback | V |
| 66 | 2 | 2 | e |  | int | no | `FFX_AtelSys_MapFx_066_GetFogColorR_resi` | sub_908980: red of dword_18DF140, then overwritten by the Phyre application float +96 (+80 under magic draw flag 4) times 255 | readback | V |
| 67 | 8 | 7 | e |  | int | no | `FFX_AtelSys_MapFx_067_GetFogColorG_resi` | as mapfx:66, Phyre +100 | readback | V |
| 68 | 9 | 8 | e |  | int | no | `FFX_AtelSys_MapFx_068_GetFogColorB_resi` | as mapfx:66, Phyre +104 | readback | V |
| 69 | 20 | 16 | e | r:int, g:int, b:int | int 1 | no | `FFX_AtelSys_MapFx_069_SetClearColor_resi` | maybe_FFX_MagicDraw_SetClearColor: byte_18DF178.., and sub_642E90 unless magic 706 is loaded | render global | V |
| 70 | 0 | 0 |  | r:int | int 1 | no | dead `FFX_AtelSys_MapFx_070_SetClearColorR_resi` | sub_9086D0, replace red, store | render | V |
| 71 | 0 | 0 |  | g:int | int 1 | no | dead `FFX_AtelSys_MapFx_071_SetClearColorG_resi` | as mapfx:70 for green | render | V |
| 72 | 0 | 0 |  | b:int | int 1 | no | dead `FFX_AtelSys_MapFx_072_SetClearColorB_resi` | as mapfx:70 for blue | render | V |
| 73 | 3 | 3 | e |  | int | no | `FFX_AtelSys_MapFx_073_GetClearColorR_resi` | sub_9086D0: byte_18DF178, scaled by the magic draw colour matrix when flags 1 and 4 are set | global | V |
| 74 | 3 | 3 | e |  | int | no | `FFX_AtelSys_MapFx_074_GetClearColorG_resi` | as mapfx:73 for green | global | V |
| 75 | 3 | 3 | e |  | int | no | `FFX_AtelSys_MapFx_075_GetClearColorB_resi` | as mapfx:73 for blue | global | V |
| 76 | 0 | 0 |  | i:int, s:float | int 1 | no | dead `FFX_AtelSys_MapFx_076_SetLayerScale_resi` | sub_91ABC0(i, s, s): +68 and +72 (0 keeps an axis), tween reset, sub_639D60 | render | V |
| 77 | 0 | 0 |  | i:int, s:float | int 1 | no | dead `FFX_AtelSys_MapFx_077_SetLayerScaleX_resi` | sub_91ABC0(i, s, 0) | render | V |
| 78 | 0 | 0 |  | i:int, s:float | int 1 | no | dead `FFX_AtelSys_MapFx_078_SetLayerScaleY_resi` | sub_91ABC0(i, 0, s) | render | V |
| 79 | 37 | 15 | e | i:int, s:float, frames:int | int 1 | no | `FFX_AtelSys_MapFx_079_TweenLayerScale_resi` | sub_91A150: tween of +68/+72 toward s over frames | render | V |
| 80 | 0 | 0 |  | i:int | float | no | dead `FFX_AtelSys_MapFx_080_GetLayerScaleX_resf` | +68 |  | V |
| 81 | 0 | 0 |  | i:int | float | no | dead `FFX_AtelSys_MapFx_081_GetLayerScaleY_resf` | +72 |  | V |
| 82 | 0 | 0 |  | i:int, x:float, y:float | int 1 | no | dead `FFX_AtelSys_MapFx_082_SetLayerPairB_resi` | sub_91AAE0: +76 and +80, tween counters zeroed. What the pair means is not traced | render | I |
| 83 | 0 | 0 |  | i:int, x:float | int 1 | no | dead `FFX_AtelSys_MapFx_083_SetLayerPairBX_resi` | sub_91AAE0 with y read back from +80 | render | I |
| 84 | 0 | 0 |  | i:int, y:float | int 1 | no | dead `FFX_AtelSys_MapFx_084_SetLayerPairBY_resi` | sub_91AAE0 with x read back from +76 | render | I |
| 85 | 0 | 0 |  | i:int, x:float, y:float, frames:int | int 1 | no | dead `FFX_AtelSys_MapFx_085_TweenLayerPairB_resi` | sub_91A0F0: tween of +76/+80 through +100..+112, frames at +124 | render | I |
| 86 | 0 | 0 |  | i:int | float | no | dead `FFX_AtelSys_MapFx_086_GetLayerPairBX_resf` | +76 |  | V |
| 87 | 0 | 0 |  | i:int | float | no | dead `FFX_AtelSys_MapFx_087_GetLayerPairBY_resf` | +80 |  | V |
| 88 | 0 | 0 |  | party:int, bone:int, group:int | int 1 | no | dead `FFX_AtelSys_MapFx_088_BindGroupToPartyChr_resi` | sub_72A530 with the CHR maybe_FFX_Atel_PartyCharHasPendingScript returns for the party index | render | V |
| 89 | 19 | 1 | e | part:int, group:int | int 1 | no | `FFX_AtelSys_MapFx_089_BindGroupToMapPart_resi` | sub_72A590: attach flag 2 | render | V |
| 90 | 0 | 0 |  | group:int | int 1 | no | dead `FFX_AtelSys_MapFx_090_UnbindGroupById_resi` | sub_72A120: clears the attach flags and target | render | V |
| 91 | 23 | 3 | e | group:int, x:float, y:float, z:float | int 1 | no | `FFX_AtelSys_MapFx_091_SetGroupPos_resi` | sub_72A630: identity matrix and the position in the attach record. The first pop (z) lands in the first vector slot, so the axis order in the record is reversed | render | V |
| 92 | 3 | 1 | e | on:int | int 1 | no | `FFX_AtelSys_MapFx_092_SetPostFxPreset_resi` | dword_25D5EB8 = on != 0, then sub_645E40 -> sub_66E850 | render | V |
| 93 | 4 | 2 | e |  | int 1 | no | `FFX_AtelSys_MapFx_093_RestartAllEffectsAndResetLayerFrames_resi` (was applied as `..._KillAll..._resi`) | sub_7297E0 zeroes every countdown, which restarts every placed effect on the next pass, dead ones included (MAP_PARTICLES.md 5.2), then every layer frame goes to 0 | render | V |
| 94 | 58 | 23 | e | i:int | int 1 | no | `FFX_AtelSys_MapFx_094_SelectGuideMapModel_resi` | FFX_RcBg_SelectGuideMapModel | render | V |
| 95 | 249 | 9 | e | group:int | int 1 | no | `FFX_AtelSys_MapFx_095_KillGroup_resi` | sub_7295F0: frees each record of the group | render | V |
| 96 | 2 | 1 | e | group2:int | int 1 | no | `FFX_AtelSys_MapFx_096_KillGroup2_resi` | sub_729480: the same keyed on the second group word | render | V |
| 97 | 0 | 0 |  | v:int | int 1 | no | dead `FFX_AtelSys_MapFx_097_SetMagicDrawFlag100_resi` | sub_908580: g_ffxMagicDrawFlags |= 0x100 and dword_18DED2C = v, which nothing reads |  | V |
| 98 | 0 | 0 |  |  | int 1 | no | dead `FFX_AtelSys_MapFx_098_ClearMagicDrawFlag100_resi` | sub_908440 |  | I |
| 99 | 0 | 0 |  | a:float, b:float, c:float, d:float | int 1 | no | dead `FFX_AtelSys_MapFx_099_SetUnreadVec1931208_resi` | sub_90A200 writes flt_1931208.., write only |  | V |
| 100 | 0 | 0 |  | a:float, b:float, c:float, d:float, e:float, f:float | int 1 | no | dead `FFX_AtelSys_MapFx_100_SetUnreadVec1931218_resi` | sub_90A230, write only |  | V |
| 101 | 0 | 0 |  | i:int | int | no | dead `FFX_AtelSys_MapFx_101_IsMapPartValid_resi` | sub_9086B0: 0 when i is below dword_18DF588, else -1 |  | V |
| 102 | 2 | 1 | e | on:int | int 1 | no | `FFX_AtelSys_MapFx_102_SetVfxTimeFrozen_resi` | sub_72A750: dword_230FD34. Only znkd0800 | render | I |
| 103 | 2130 | 7 | e | actorA:int, actorB:int, mode:int, unused:int | int 1 | no | `FFX_AtelSys_MapFx_103_LinkRcBgObjChrs_resi` | sub_919A80: rcBg object 0x20008 gets the two actors' CHRs and the mode (sub_922EF0). Blitzball only | render | V |
| 104 | 0 | 0 |  |  | int 1 | no | dead `FFX_AtelSys_MapFx_104_StopRcBgObj_resi` | sub_919AB0 -> sub_922B50 on rcBg object 0x20008 | render | I |
| 105 | 0 | 0 |  | node:int, group:int | int 1 | no | dead `FFX_AtelSys_MapFx_105_BindGroupToMapNode_resi` | sub_72A5E0: attach flag 8 | render | V |
| 106 | 24 | 20 | e | v:int | int 1 | no | `FFX_AtelSys_MapFx_106_SetSceneField260_resi` | sub_91A260: scene +260 | render | I |
| 107 | 3 | 1 | e | on:int | int 1 | no | `FFX_AtelSys_MapFx_107_SetPostFxPreset2_resi` | dword_25D5EB8 = 2 when on, then sub_645E40. Only one package | render | V |

## 4. Test, library 9

### 4.1 Short version

- **One row, dead.** No shipped script calls test:0. [D]
- **The name is right and kept.** `atel_test_print 0x907E60` calls `SG_Printf_STUB` and returns 0.
  ebp.py names it atel_test_print, `LIB_INFO` 9 = (0xC5D8C0, 1, None). [V]
- No hazard. Nothing to apply. [V]

### 4.2 The table

| test | uses | files | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 0 |  |  | int 0 | no | kept `atel_test_print` | atel_test_print calls SG_Printf_STUB only |  | V |

## 5. AbMap, library 13

### 5.1 Short version

- **One row, dead.** No shipped script calls abmap:0. [D]
- **The name is right and kept.** `abiritymap_debug 0xA445F0` pops one int and calls
  `SG_Printf_STUB`. The spelling is the binary's own (ability map). ebp.py names it abiritymap_debug,
  `LIB_INFO` 13 = (0xC85EB0, 1, None). [V]
- No hazard. Nothing to apply. [V]

### 5.2 The table

| abmap | uses | files | src | args | result | blk | name | meaning | lockstep | mark |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 0 |  | v:int | int 0 | no | kept `abiritymap_debug` | abiritymap_debug pops one int and calls SG_Printf_STUB only |  | V |

## 6. The apply script

`../tools/apply_misc_lib_names.py`, built like `apply_came_names.py` and generalised to five tables.
`LIBS` maps each library to its name, table and row count, and `NAMES` holds one block per library.
For each row in a block it gives each non-null slot `FFX_AtelSys_<Lib>_NNN_<Role>_<slot>`
(`--no-slot-suffix` drops the `_<slot>` on single slot rows). It only renames a slot whose current
name is a positional name of that row (`FFX_AtelSys_<Lib>_NNN_<slot>`, with or without padding, or
`FFX_AtelSys_<Lib>_NNN` on a single slot row), a `sub_`, `loc_` or `nullsub_` placeholder, or an
entry of `MISNAMED`, which is empty because no signature misname was found on these tables. It skips a
slot that is not a function start, a function in more than one slot of any of the five tables, and a
name already taken. Dead rows are left alone unless `--include-dead`. `--lib` limits a run to some of
the libraries. Dry run by default, it never saves.

NAMES has 205 rows: Sys 30, Sg 68 (all but the null row 2 and the kept rows 29 and 49), MapFx 107
(all but the kept row 10), Test and AbMap none. 88 of them are dead.

Dry run on the scratch copy:

- default: 121 to rename, 0 skipped, 0 already named. Sys 12, Sg 44 (43 rows, two slots on sg:13),
  MapFx 65 (62 rows, two slots each on 4, 5 and 6).
- `--include-dead`: 209 to rename, 0 skipped. Sys 30, Sg 69, MapFx 110.
- `--include-dead --apply` as a headless rehearsal: 209 renamed, 0 failed, database not saved.

No slot was refused, so every slot outside the kept rows is positional in the scratch copy and no
function is shared between slots.
