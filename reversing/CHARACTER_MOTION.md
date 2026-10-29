# Character motion: FFX_Ch_UpdateMotionAll, collision and the ground clamp

PROGRESS items 1.6.7 (the motion step written out) and 1.6.8 (collision volumes, the radius at
`+0x4E8`, character to character collision). Written 2026-10-13 against the IDB of `FFX.exe`
(32-bit, VA base `0x400000`, so RVA = VA - `0x400000`). IDA was read only for this pass: every name
below that is not already in the IDB is a proposal, collected in section 7.

Marks: **[V]** verified in Hex-Rays and, for anything float or branch sensitive, in the disassembly.
**[I]** inferred. Offsets are CHR offsets unless stated.

Related docs: `PLACEMENT.md` (sections 4, 6, 8, 9 cover the same functions from the "how do I place a
character" angle and agree with this one except where section 9 below says so), `MAP_FORMAT.md` (the
walkmesh), `MGRP_FORMAT.md` (the motion sequence VM, which is item 1.6.6 and not repeated here),
`MAIN_LOOP.md` (where the call sits), `FFX_GAME_NOTES.md` (camera yaw and the stick formula).

---

## 0. The short version

`FFX_MainStep 0x820AE0` calls `FFX_Ch_UpdateMotionAll(0.033373334f)` at `0x82107D` once per sub-step,
gated on `g_ffxChrMotionUpdateEnabled 0x12FB8FF`. The argument is the constant `flt_B59158`, never the
frame delta. [V]

Inside, four passes over `g_ffxChrArray`, each in array order:

1. **Turn, pick the locomotion clip, make the velocity, save prevPos.** Per CHR, independent of every
   other CHR.
2. **`FFX_Ch_ResolveCollisionsAll 0x83D3B0`.** Integrate (walkmesh slide, or free move), then circle
   push-out between characters. Up to two rounds. This is the only pass where CHRs affect each other.
3. **Ground and water clamp** on `m_groundMode`.
4. **Animate.** `FFX_Mot_AdvanceFrame` and the seven CHRANIMF animators, then queue the render jobs.

So "turn, animate, integrate, collide, clamp" in the PROGRESS wording is really turn, integrate,
collide, clamp, animate. Animation runs **after** the move, and the locomotion clip choice made in
pass 1 is what pass 4 plays. [V]

The facing yaw target (`m_moveDir`) and the speed (`m_speed`) are not computed here. For the player
they come from `FFX_Player__stepControl 0x82D180`, which runs earlier in the same sub-step, and for
everyone else from the ATEL actor step. Section 2 lists both because they are the inputs that matter.

---

## 1. The call order with addresses

```
FFX_MainStep 0x820AE0, sub-step loop
  0x821010 FFX_Player__stepControl 0x82D180      pad + camera yaw -> m_speed, m_moveDir of g_ffxControlledChr
  0x82101A FFX_Atel_StepOnce                     script actors -> m_speed, m_moveDir, m_rotY of their CHRs
  0x82104D FFX_Came_StepAll
  0x82107D FFX_Ch_UpdateMotionAll 0x832E10 (dt = 0.033373334f)
    0x832E16 sub_8205E0 (debug "freeze characters", returns byte_12FBBBB). Non-zero -> return at once
    pass 1, per in-use CHR:
      0x832E5C m_hideFlags &= 0x13           (clears every bit but Ev 0x01, Eff 0x02, Btl 0x10)
               skip unless the masked value is 0 or m_b181 != 0
      if !(m_flags1 & 0x40):
        m_locomotionMode 0 -> FFX_Ch_AutoLocomotionAnim 0x835BB0   (turn + idle/walk/run clip)
        m_locomotionMode 2 -> FFX_Ch_AutoSwimAnim       0x835D30   (turn + swim clips + vertVel)
      m_flags1 bit 0x20000 = (m_runThreshold < m_speed)
      velocity from m_speed, m_moveDir (section 3.3)
      m_prevPos[0..3] = m_pos[0..3]       (integer moves, bit exact)
    0x832FB3 FFX_Ch_ResolveCollisionsAll 0x83D3B0
      clear m_flags1 0x4000 on every active CHR
      round r = 0, 1:
        rebuild g_ffxChrCollideList 0x1301B08 in array order (CHRs without m_flags1 0x100)
        integrate every active CHR in array order:
          m_flags1 & 0x80 ? pos += vel on X, Y, Z
                          : FFX_Ch_WalkmeshMove 0x83E5F0 (X, Z) then posY += velY
          vel = 0, 0, 0
          m_collisionRadius (0x4E8) = (m_scaleZ + m_scaleX) * 0.5 * m_collisionRadiusScale
          m_520 = (m_51C == 0) ? m_vec6A4[1] : posY - m_51C
        FFX_Ch_SortCollideListByZ 0x83EB30   (selection sort, ASCENDING m_posZ)
        all pairs i < j in sorted order -> FFX_Ch_SeparatePair 0x83ECD0 (writes velocities)
        stop if no pair reported a push, else go round again (max 2 rounds)
      per active CHR: (0x4000 && !0x8000) -> FFX_Atel_FireEventOnChrActor(chr, 2), then bit 0x8000 = bit 0x4000
    pass 3, ground and water clamp, per active CHR (section 3.6)
    0x833217 FFX_Ch_JoinUpdateJobs           (waits for the PREVIOUS dispatch, see 3.8)
    pass 4, per active CHR:
      m_flags1 |= 0x1000000
      FFX_Mot_AdvanceFrame 0x838C90(chr, 1)
      FFX_Ch_StepAnimFloat 0x836020 on the seven CHRANIMF at 0x330..0x378
      root CHRs: m_updateStage = 1, count joints, FFX_Ch_DispatchUpdateJob every 200 joints
    dword_13009BC = g_ffxPs2HwRegSink delta   (profiling, write only)
  0x8210BD FFX_PlayerCam_Step, then FFX_Came_StepAll again
(after the loop) 0x8210DA FFX_Ch_DispatchInBatches 0x833390  (joins, parented CHRs, motion events)
```

Every line above is [V] from Hex-Rays and the disassembly of `0x832E10`, `0x83D3B0` and their callees.
`FFX_Ch_ResolveCollisionsAll`, `FFX_Ch_AutoLocomotionAnim`, `FFX_Ch_AutoSwimAnim` and
`FFX_Mot_AdvanceFrame` each have exactly one caller, `FFX_Ch_UpdateMotionAll`. `FFX_Ch_WalkmeshMove`
and `FFX_Ch_SeparatePair` each have exactly one caller, `FFX_Ch_ResolveCollisionsAll`. [V]

---

## 2. Every value read from outside the CHR

This is the list a lockstep or rollback design has to own. "Step" means simulation state that has to
be identical on every peer for the motion to match. "Debug" means a developer switch that is zero in
retail play. "Pres" means presentation only.

### 2.1 Read by the motion step itself (FFX_Ch_UpdateMotionAll and its callees)

| global | VA | read by | writer | class |
|---|---|---|---|---|
| dt argument `flt_B59158` | `0xB59158` | pass 1 velocity | constant `0.033373334f`, pushed at `0x821073` | step, constant [V] |
| `g_ffxChrMotionUpdateEnabled` | `0x12FB8FF` | `FFX_MainStep` gate at `0x82106A` | `FFX_SetChrMotionUpdateEnabled 0x8203C0` | step [V] |
| `byte_12FBBBB` (debug freeze) | `0x12FBBBB` | `sub_8205E0`, top of the step and pass 4 | `sub_821CB0`: `FFX_MainInit` writes 0, debug GUI item 103 at `0x8501E7` toggles | debug [V] |
| `g_ffxChrArray`, `g_ffxChrCount` | `0x23C44E4`, `0x23C44E0` | every pass | CHR pool alloc on map change | step, and **array order is semantic**, see 4 [V] |
| `g_ffxWaterLevel` | `0x13009D8` | pass 3 (0x20000000 test, mode 2), `FFX_Ch_AutoSwimAnim` | `sub_836640` (sets level and max) from `FFX_Ch_EventjumpInit` and script `Sg_001` | step [V] |
| `g_ffxWaterLevelMax` | `0x13009DC` | pass 3 mode 2 | pass 3 itself ratchets it up to the level every sub-step, `sub_836660` from `FFX_Ch_EventjumpInit` and script `Sg_060` | step [V] |
| `g_ffxWalkmeshTris`, `Verts`, `TriCount`, `Scale` | `0x1301A84`, `0x1301A8C`, `0x1301A88`, `0x1301A90` | walkmesh move, find, edge test, height | `FFX_Ch_SetActiveWalkmesh 0x83EA00` on map load | step (map data) [V] |
| `g_ffxWalkmeshStepHeight` | `0xC4DEB0` | `FFX_Ch_WalkmeshMove` | `.data` 300.0, setter has no callers | step, constant [V] |
| `g_ffxPlayerDebugNoClip` | `0x23C44B4` | `FFX_Player__isDebugNoClipHeld 0x82D830`, from `FFX_Ch_WalkmeshMove` | `FFX_Player__readPad`, only in debug mode | debug, but it is a pad-derived input [V] |
| `g_ffxControlledChr` | `0x1300788` | `FFX_Ch_GetPlayerChr` from `FFX_Map_IsTriBlockedForChr 0x83E4E0` (surface 2 player only passage, surface 14 wall for the player) | `FFX_Ch_SetPlayerChr 0x82DAD0` | step, **per-peer binding hazard** [V] |
| `g_ffxGroundTypeRemap` | `0x13019D8` | `FFX_Map_IsTriBlockedForChr` for surface codes 48..63 | `FFX_Map_SetGroundTypeRemap 0x83E9E0`, script `Sg_023` | step [V] |
| `maybe_g_ffxSceneId` | `0x1330844` | `FFX_Ch_WalkmeshMove`: scene 298 and `m_id == 1` (Tidus) gets `m_groundHeight - 1.0`. `FFX_Mot_BuildJointTransforms`: per scene composer overrides | `maybe_FFX_Scene_Init 0x88E10F` | step [V] |
| `g_ffxStepScale88` | `0x23CBBEC` | `FFX_Mot_AdvanceFrame` at `0x838DF3`, only when `m_flags1 & 0x100000` **and** `byte_12FBC34 != 0` | `FFX_MainStep_PublishStepScale`, integer 60 Hz tick delta or the timing track | step [V] |
| `byte_12FBC34` ("KeepSp") | `0x12FBC34` | `sub_8205A0`, the gate above | `sub_821AE0` from `FFX_Btl_BeginBattle`, `FFX_Btl_MainStep`, `FFX_Ch_EventjumpInit`, script `Sg_034`, `sub_788C00`, `sub_788D60`, `sub_83B5F0` (through the thunk `sub_839CA0`), the debug GUI. The debug window prints it as `"KeepSp:%d"` (`0x8502F8`) | step [V] |
| `g_ffxMotHalfSpeed` | `0x13017F8` | `FFX_Mot_AdvanceFrame` halves the frame delta | `FFX_Mot_SetHalfSpeed`, debug GUI only | debug [V] |
| `g_ffxCameActiveSlot` | `0x2311430` | `sub_839140` from `FFX_Mot_BuildJointTransforms`: eye distance and pitch pick the joint composer | camera step | **camera, see 2.3** [V] |
| `g_ffxBtlSubPhase`, ATEL context byte 0 bit 2 | `0x112A8E0` | `FFX_Ch_IsNearlyStationary 0x839200`, same composer choice | battle, ATEL | step [V] |
| story progress, `g_ffxCurrentEventId` | via `FFX_Story_GetProgress` | `FFX_Mot_BuildJointTransforms` (scene 60 override), sequence VM | story, event | step [V] |
| `g_ffxMainStepCounter` | `0x23CBBF0` | `SndKickFoot` (from the stop branch of `FFX_Ch_AutoLocomotionAnim`) | `FFX_MainStep` | pres (audio) [V] |
| scratch: `g_ffxChrCollideList/Count`, `g_ffxWalkmeshEdgeRecurseDepth`, `CrossedEdgeV0/V1`, `StepHeightScaled` | `0x1301B08`, `0x1301BA8`, `0x1301AE8`, `0x1301AEC`, `0x1301AF0`, `0x1301AF4` | the collision and walkmesh code | the same code, rebuilt every call | scratch, no carried state [V] |

Constants the step loads, all in `.rdata` [V]:

| VA | type | value | used for |
|---|---|---|---|
| `0xB412E8` | double | `0.05000000074505806` (that is `(double)0.05f`) | velocity scale |
| `0xB922C8` | double | `30.0` | velocity scale, water depth smoothing divisor |
| `0xB92220` | double | `0.5` | radius average |
| `0xB92270` | double | `2.0` | collision broad phase margin |
| `0xB599D0` | double | `8.5` | head height for the water tests |
| `0xB599C8` | double | `0.3268883228302002` | water mode gravity per sub-step |
| `0xB599B8` | double | `0.1634441614151001` | water mode buoyancy per sub-step |
| `0xB922B0` | double | `10.0` | water pull divisor, off-mesh fling multiplier |
| `0xB2C5D0` | double | `-2.0` | water rise speed cap |
| `0xB92194` | float | `0.5` | water mode position step factor |
| `0xB92248` | double | `1.0` | the scene 298 ground offset |
| `0xB599A4`, `0xB5982C`, `0xB599A8` | float | `0.31415921`, `0.34906578`, `0.52359867` | turn rate idle, walk, run |
| `0xB582F0`, `0xB59638`, `0xB59648` | double | `3.141592025756836`, `6.283184051513672`, `-3.141592025756836` | angle wrap (these are `(double)3.141592f`, not pi) |

### 2.2 Read before the step, by the code that writes m_speed and m_moveDir

`FFX_Player__stepControl 0x82D180` runs at `0x821010` in the same sub-step. Its reads are the stick
mapping inputs. [V] unless marked.

| value | VA | note |
|---|---|---|
| `g_ffxPlayerPadButtons` | `0x23C44AC` | dpad bits `0xF000`, cross `0x40` (run versus walk, swim rise) |
| `g_ffxPlayerAnalogLY`, `LX` | `0x23C44A4`, `0x23C44A5` | bytes, deadzone `88 < v < 168` |
| ramps `g_ffxPlayerDirRampUp` and its three siblings | `0x130078C` area | **persistent integer state**, step by 32, clamp 0..256. These carry across sub-steps, so they are part of the simulation snapshot, not input |
| `g_ffxControlledChr` | `0x1300788` | which CHR gets written. Exactly one |
| `g_ffxPlayerControlEnabled` | `0xC496D8` | 0 forces speed 0 (bit 0x400 still set) |
| camera yaw | `FFX_Came_GetYaw` via `j_FFX_Came_GetYaw 0x83F2D0` | reads the active camera slot. See `FFX_GAME_NOTES.md` |
| `g_ffxPlayerCamYaw` | `0x130079C` | cached yaw. In control mode 3 it is refreshed only past a 0.349 rad heading change or on level hashes 412 and 544, so it is **stateful** |
| `maybe_g_ffxPlayerCtrlMode` | `0xC496D5` | mode 3 switches to the cached-yaw behaviour above |
| `g_ffxPlayerDesiredHeading`, `PrevHeading`, `HeadingOffset` | `0x13007A0`, `0x13007A4`, `0x1300780` | persistent floats |
| `g_ffxPlayerUseFixedYaw`, `FixedYaw` | `0x13007C4`, `0x13007C8` | script pinned yaw |
| `g_ffxPlayerWalkSpeed[2]`, `g_ffxPlayerSwimSpeed` | `0x13007B4`, `0x13007BC` | set by `FFX_Player__setSpeedParam` |
| `g_ffxPlayerSwimRiseEnabled` | `0x13007C5` | cross adds 0.2 vertical velocity while swimming |
| `FFX_Level_GetNameHash()` | | 412, 544 refresh the cached yaw |

The resulting formula, from the decompile and the notes in `FFX_GAME_NOTES.md`:

```
speed = walkOrRunSpeed[(buttons & 0x40) == 0] * mag * (1/256)      (swim: swimSpeed * mag / 256)
mag   = sqrt(dx*dx + dy*dy) of the ramp differences, forced to 0 below 48 and clamped at 256
desired = atan2(right - left, up - down)                            (_CIatan2, x87)
m_moveDir = -((desired - headingOffset) - (useFixedYaw ? fixedYaw : camYaw))
```

For an ATEL-driven CHR the same two fields come from `FFX_Atel_ApplyMoveToChr`,
`FFX_Atel_ApplyDirToChr` and `FFX_Atel_ApplyRotToChr` (see `PLACEMENT.md` section 7). Those are
script state, deterministic given the script.

### 2.3 Hazards worth calling out

1. **Camera state feeds the joint pose inside the step.** [V for each link, I for the end effect]
   `FFX_Mot_BuildJointTransforms 0x838F70` picks one of three joint composers per CHR. When
   `m_jointComposerMode` (0x870) is set and `m_flags3` bit 4 is clear, it uses
   `FFX_Ch_IsNearlyStationary` (body centre moved less than 2 units on every axis since last time,
   no battle sub-phase, ATEL context byte 0 bit 2 clear) and then `sub_839140`, which takes
   `|m_bodyCentre - cameraEye|^2` against 900 (2500 for a model name containing `"c10"`) and the
   camera pitch at slot `+0x6B0` or `+0x6D0` against `-0.785398`. Near camera means
   `FFX_Mot_ComposeJointSrtSmoothed` on every joint, far means only on joints whose node byte `+0x144`
   is set. The smoothed composer damps the joint euler toward the animated value with factor 0.7, so it
   carries history. Two peers with different cameras therefore hold different joint eulers, and the
   difference persists. The joints feed `FFX_Ch_UpdateBonePositions 0x8350F0`, which writes
   `m_bodyCentre` (0x694) and `m_vec6A4` (0x6A4), and those two feed the height band of character
   collision (section 4.3). For a recreation with a shared simulation this is a design decision: either
   key the smoothing on something every peer agrees on, or move it out of the simulation.
2. **The player binding leaks into collision.** `FFX_Map_IsTriBlockedForChr` asks "is this CHR the
   one `g_ffxControlledChr` points at" for surfaces 2 and 14, and the touch event in section 4.4 asks
   `m_flags1` bit 0x200, which `FFX_Ch_UpdateRenderJob 0x833530` sets from the same global on a worker
   thread. With two players, the second player's character walks through no player-only gaps, gets
   blocked by player-only walls the wrong way round, and fires no touch events. [V]
3. **Worker thread fields read by the collision pass.** `m_bodyCentre[1]` (0x698), `m_vec6A4[1]`
   (0x6A8) and `m_flags1` bit 0x200 are written by `FFX_Ch_UpdateRenderJob`, which pass 4 dispatches to
   the Phyre worker pool. They are joined by `FFX_Ch_DispatchInBatches` (at its start) before the next
   frame's `FFX_Ch_UpdateMotionAll`, so with one sub-step per frame the read is ordered. With more than
   one sub-step (`FFX_Player__getSubStepCount() > 1`, debug fast forward or the turbo flag), the
   second sub-step's passes 1 to 3 run while the first sub-step's jobs may still be writing, and the
   job does a read-modify-write of `m_flags1` that races the main thread. [V for the call graph, I for
   the race actually biting]
4. **The motion step can fire script events and sounds.** `FFX_Atel_FireEventOnChrActor(chr, 2)`
   (touch) from the collision pass and `SndKickFoot(chr, 5)` from the stop branch of
   `FFX_Ch_AutoLocomotionAnim`. The motion events proper run later in `FFX_Ch_DispatchInBatches`. [V]
5. **Hide bits set by the camera do not freeze motion.** Pass 1 masks `m_hideFlags` with 0x13 before
   its gate, which clears the Disp (0x08) and Z (0x20) bits that `FFX_Ch_UpdateCameLenAndZClip
   0x82E220` sets from camera distance. That function's only caller is `FFX_Ch_StepAll`, after the
   motion step, so the camera culling bits never gate the simulation passes. [V]

---

## 3. The per character callees

Float notation: `fl(x)` means rounded to float (an `fstp dword` to memory or to a float stack
temporary). Everything not wrapped in `fl()` is computed on the x87 stack at the process precision,
which is 53 bits (section 5). So "in double" below is exact, not approximate.

### 3.1 Turn: FFX_Ch_AutoLocomotionAnim 0x835BB0 and FFX_Ch_AutoSwimAnim 0x835D30 [V]

Only when `m_flags1 & 0x400` (set every sub-step on the player by `FFX_Player__stepControl`, and on
script actors by `FFX_Atel_ApplyFlags1Bit400ToChr` when a move or rotate command starts):

```
step = (m_runThreshold >= m_speed) ? (m_speed == 0 ? 0.31415921f : 0.34906578f) : 0.52359867f
m_rotY = ApproachAngle(Wrap(m_rotY), Wrap(m_moveDir), step)
```

`FFX_Mot_WrapAngleToPi 0x836720(float a)`: while `a > 3.141592` do `a = fl(a - 6.283184)`, then while
`a < -3.141592` do `a = fl(a + 6.283184)`. The bounds are **closed** and are `(double)3.141592f`
(`0x40490FD0` as a float), not pi. A value already inside is returned untouched, without a rounding
store. NaN falls through both loops unchanged. An infinity would loop forever. [V]

`FFX_Mot_ApproachAngle 0x835F80(cur, target, step)` (IDB `maybe_FFX_ApproachAngle`):

```
d = Wrap(fl(cur - target))
if d == 0:            return target                (exact compare)
if d > 0:  r = (step <= d) ? cur - step : cur - d
else:      r = (-step < d) ? cur - d    : cur + step     (the cur - d case goes through fl(-d))
return Wrap(fl(r))
```

The turn rate is per sub-step, so roughly 18, 20 and 30 degrees per 1/29.97 s. [V for the values]

Then the clip choice, only when `m_motionRequestActive` (0x736) is 0:

* ground: `m_speed > m_runThreshold` -> `m_slots[2]` (run), `m_speed != 0` -> `m_slots[1]` (walk), both
  set `m_flags1 |= 0x8000000`. `m_speed == 0` -> `SndKickFoot(chr, 5)` if 0x8000000 was set, clear it,
  `m_slots[0]` (idle), `m_flags1 |= 0x4000000`. A slot value with a low word `>= 0x1000` is a full
  motion id for `FFX_Ch_SetMotionKey`, otherwise a logical index for `FFX_Mot_SetByModeIndex(chr, 0,
  idx)`. Then `m_motionRequestActive = 0` and `FFX_Mot_SetPendingLoopCount(chr, 0)`.
* swim: `m_speed == 0` -> `m_slots[16]`, else `g_ffxWaterLevel + 1.0 <= m_posY` -> `m_slots[19]`,
  otherwise `m_slots[18]`, all with mode 2. When moving and `m_vertVel < 1.0`, `m_vertVel += 0.15`.
* when `m_motionRequestActive != 0` the ground version only clears 0x8000000 (and writes back the
  `m_flags1` it read at entry).

The clip is selected here and played in pass 4, so a CHR that starts walking this sub-step moves
before its walk clip advances.

### 3.2 The run flag

`m_flags1 bit 0x20000 = (m_runThreshold < m_speed)` strictly, for every CHR in pass 1 including ones
with `m_flags1 & 0x40`. A NaN speed reads as not running. [V, `0x832EC2..0x832EF6`]

### 3.3 Velocity, the exact float order [V, stack traced at 0x832F03..0x832F4E]

```
if m_speed == 0:          m_velX = 0, m_velZ = 0              (fucomp, so NaN counts as moving)
else:
    k      = fl(dt * (30.0 * (0.05000000074505806 * m_speed)))   three multiplies in double
    c      = fl(cos((double)m_moveDir))                           _CIcos
    m_velX = fl(c * k)
    s      = fl(sin((double)m_moveDir))                           _CIsin
    m_velZ = fl(s * k)
    m_flags1 |= 0x2000000
m_velY is not touched here (FFX_Ch_SetVelY writes it, arg * 0.05)
```

With the constant dt, `k` is about `0.0500600 * m_speed` world units per sub-step. `m_moveDir` 0 is
+X and pi/2 is +Z, the same convention as `FFX_Came_GetYaw`.

### 3.4 Integrate: FFX_Ch_ResolveCollisionsAll 0x83D3B0, the move half [V]

Per active CHR in array order, inside each round:

* `m_flags1 & 0x80` (free move): `posX = fl(velX + posX)`, same for Y and Z. No walkmesh at all.
* otherwise `FFX_Ch_WalkmeshMove(chr)`, then `posY = fl(velY + posY)`.
* `velZ = velY = velX = 0.0`.
* `m_collisionRadius = fl(((m_scaleZ + m_scaleX) * 0.5) * m_collisionRadiusScale)`, in double.
* `m_520 = (m_51C == 0.0) ? m_vec6A4[1] : fl(posY - m_51C)`.

The second round, if it happens, re-runs all of this for every CHR. Velocity at that point is the
push from round one only, so round two is "apply the push through the walkmesh". See 4.2.

### 3.5 FFX_Ch_WalkmeshMove 0x83E5F0 and its queries [V]

`s = g_ffxWalkmeshScale` (float).

No walkmesh loaded (`g_ffxWalkmeshTris == 0`): `posX = fl(velX + posX)`, `posZ = fl(velZ + posZ)`,
`m_groundAttrs = 0xFFFE0000`, `m_groundHeight = 0`, `m_groundNormal = (0, -1, 0)`. Done.

Otherwise:

```
g_ffxWalkmeshStepHeightScaled = fl(300.0f * s)
m_walkmeshPos[i] = fl(pos[i] * s)  for X, Y, Z          (also the query point q, real Y)
noclip = g_ffxPlayerDebugNoClip && (m_flags1 & 0x200)   -> m_walkmeshTri = -1
if m_walkmeshTri == -1: m_walkmeshTri = FFX_Ch_WalkmeshFindTri(q)
q.x = fl(velX * s + q.x), q.z = fl(s * velZ + q.z), q.y = 0      (product exact in double, one sum)
if m_walkmeshTri == -1:                               off the mesh
    noclip ? pos += vel : posX = fl(velX * 10.0 + posX), posZ = fl(10.0 * velZ + posZ)
    return                                            (no ground state written)
if !noclip:
    tri0 = m_walkmeshTri
    for n = 0..3:
        g_ffxWalkmeshEdgeRecurseDepth = 0
        if FFX_Ch_WalkmeshTestEdgeCrossing(chr, &q, m_walkmeshTri) == 0: break
        if noclip: break
        if m_walkmeshTri != -1: FFX_Ch_WalkmeshSlideAlongEdge(m_walkmeshPos, &q)
        m_walkmeshTri = tri0
    if 4 tests all blocked: return                    (no position, no ground state)
m_groundAttrs  = tri.attr (or 0xFFFE0000)
m_groundNormal = FFX_Map_WalkmeshGetTriNormal(tri)
m_groundHeight = fl(FFX_Map_WalkmeshHeightAt(q, tri) / s)
if maybe_g_ffxSceneId == 298 && m_id == 1: m_groundHeight = fl(m_groundHeight - 1.0)
posX = fl(q.x / s), posZ = fl(q.z / s)
```

Two consequences worth knowing:

* **Position is requantised through the walkmesh scale every sub-step, moving or not.**
  `posX = fl(fl(posX * s) / s)` when velocity is zero, which need not return the same float. It
  converges to a fixed point after a step or two. A recreation that skips the round trip for a
  stationary character drifts from the original by an ulp.
* The two rounds of section 3.4 both call this, so a character in a pushing pair is requantised twice.

`FFX_Ch_WalkmeshFindTri 0x83DE10(q)`: linear scan of every triangle. A triangle contains the XZ point
when all three edge functions are `<= 0`. For edge (a, b) of s16 vertices the function is, in double,

```
fl((float)(xb - xa)) * q.z + fl((float)(za - zb)) * q.x + fl((float)(xa * zb)) - fl((float)(za * xb))
```

where the integer differences and products are computed with `sub` and `imul` and converted with
`fild` then `fstp dword`, so a product above 2^24 is rounded to float before it is used. Among the
containing triangles, `h = trunc(FFX_Map_WalkmeshHeightAt(q, tri))` (`__ftol2_sse`), triangles with
`(float)h < q.y` are skipped, and the smallest `h` wins (ties keep the first). No candidate at or below
q.y returns the last XZ hit. This is `PLACEMENT.md` section 4, re-verified.

`FFX_Ch_WalkmeshTestEdgeCrossing 0x83D960(chr, q, tri)`: returns 1 at once for `tri == -1`. Evaluates
the same edge functions (operand order `(za - zb) * q.x + (xb - xa) * q.z + ...`, which is the same
value since a two-term double sum commutes) and treats `> 0` as outside. When two edges are both
crossed it picks one with a cross product against `m_walkmeshPos` (the start point). Inside all three:
writes `m_triVert0/1/2` and `m_walkmeshTri = tri`, returns 0. Outside an edge with neighbour `nb`:

* `nb == -1`, or `FFX_Map_IsTriBlockedForChr(chr, nb)`, or recursion depth `> 10`: record the edge in
  `g_ffxWalkmeshCrossedEdgeV0/V1`, return 1.
* if the neighbour's normal Y is non-zero and `fl(HeightAt(q, nb)) < m_walkmeshPos[1] - stepScaled`
  (a floor more than 300 world units above you, +Y is down): wall, return 1.
* else depth++ and recurse into `nb`. A successful recursion leaves `m_walkmeshTri = nb`.

It never writes `m_walkmeshTri = -1` (the IDB function comment still says it does, see section 9).

`FFX_Map_IsTriBlockedForChr 0x83E4E0(chr, tri)`: surface code `attr & 0x7F`. 1 always blocks, 2 blocks
everyone but `g_ffxControlledChr`, 13 blocks when `m_flags2 & 4`, 14 blocks only `g_ffxControlledChr`,
48..63 redirect through `g_ffxGroundTypeRemap` and re-test. Everything else passes. [V]

`FFX_Ch_WalkmeshSlideAlongEdge 0x83E180(start, q)`: builds an edge frame from the crossed edge
direction `e = normalize(xb - xa, 0, zb - za)`, transforms the attempted displacement `q - start` into
it, zeroes the across-edge component, transforms back through `FFX_Mtx_InverseRigid 0x705DB0`
(proposed name, it transposes the rotation and negates the rotated translation), then while
`d.z * e.x - d.x * e.z > 0` subtracts 1.0 from the across component and transforms again. Result
`q = start + d`. [V for the operations, I for the geometric reading]

`FFX_Map_WalkmeshHeightAt 0x83E040(q, tri)`: `n = normalize(cross(v2 - v0, v1 - v0))` through
`FFX_Map_TriNormalFromVerts 0x83E420` (s16 differences through `fild`, `FFX_Vec4_Cross`,
`FFX_Vec4_Normalize`), then `fl((fl(ny*y0 + nx*x0 + nz*z0) - fl(nx*q.x + nz*q.z)) / ny)`. Returns 0
with no walkmesh or `tri == -1`. [V for the shape, I for the exact addition order, which is as
Hex-Rays prints it and should be confirmed against `0x83E040` when porting]

`FFX_Vec3_NormalizeInPlace 0x704200`: if `x*x + y*y + z*z` is NaN, infinite or `<= 1.1754944e-38`
(`sub_703BC0`) the vector becomes all zero including w. Otherwise `len2 = fl(y*y + x*x + z*z)`,
`len = fl(sqrt(len2))`, `inv = fl(1.0 / len)`, each component `fl(c * inv)`. [V]

### 3.6 Ground and water clamp, pass 3 [V, 0x832FB8..0x8331D5]

First `if g_ffxWaterLevel > g_ffxWaterLevelMax: g_ffxWaterLevelMax = g_ffxWaterLevel` (once per call).
Then per active CHR:

```
head = posY - 8.5                      (double, not stored)
m_flags1 bit 0x20000000 = (head > g_ffxWaterLevel)       +Y is down, so this is "head under water"
switch m_groundMode (0x182):
  0: m_vertVel = 0; if !(m_flags1 & 0x80) && m_groundHeight < posY: posY = m_groundHeight
  1: posY = m_groundHeight; m_vertVel = 0
  2: water, below
  other: nothing
```

Mode 0 only ever lifts a character out of the floor and has no gravity. Mode 1 is the default from
`FFX_Ch_Allocate` and snaps every sub-step. Neither has a step-down limit: the only height limit in
the whole move is the 300 unit climb test in the edge crossing.

Mode 2, written out with its float stores:

```
frac = 0
if head > water:                     frac = 1.0, buoyant
elif posY > water and (frac = fl((posY - water) / 8.5)) != 0:   buoyant
else:                                k = 1.0; m_vertVel = fl(0.3268883 + m_vertVel)   (falling)
buoyant:
    k = 0.5
    m_vertVel = fl(m_vertVel - 0.1634442)
    if posY < waterMax:
        posY = fl((waterMax - posY) / 10.0 + posY)
        if m_vertVel < 0: m_vertVel = 0
if m_vertVel < frac * -2.0: m_vertVel = frac * -2.0
depthT  = max(m_514, m_collisionRadiusScale)      (ties take m_collisionRadiusScale)
m_4FC   = fl((fl(depthT) - m_4FC) / 30.0 + m_4FC)
if m_groundHeight < m_vertVel + posY + m_4FC:  posY = fl(m_groundHeight - m_4FC); m_vertVel = 0
posY = fl(m_vertVel * k + posY)
```

`m_514` is `|posY - feetY|` from the bone pass (section 4.3). The radius scale doubling as a minimum
float depth is what the code does, whatever the intent was. [V]

### 3.7 Animate, pass 4 [V]

`FFX_Mot_AdvanceFrame 0x838C90(chr, 1)` runs on the integer 24.8 clock:

```
delta = m_frameRateScale * ((m_speedMulA * m_playSpeed) >> 8) / 7680      (C int division)
if (m_flags1 & 0x100000) && byte_12FBC34: delta = (g_ffxStepScale88 * delta) >> 8 (unsigned)
m_frameDelta = delta
FFX_Mot_StepSequence(chr)
if g_ffxMotHalfSpeed: delta /= 2
FFX_Mot_EvalChannels, FFX_Mot_BlendChannels(chr, m_hokanCountdown), FFX_Mot_BuildJointTransforms
m_hokanCountdown: if > 1, decrement, floor at 1
m_frame += delta
```

`m_flags1 & 0x40` takes the externally driven clip path instead (`.anm` when bit 0x80000000 is also
set). The clock is all integer, so it is exact in any build. The float work is in the channel decode,
blend and composers, which are 1.6.6 territory and are pure x87 like everything here.
Weapon CHRs `0x403D..0x4041` with a parent copy three joint blocks from the parent afterwards.

`FFX_Ch_StepAnimFloat 0x836020`: on a `{cur, target, frames}` triple, if `cur != target`:
`cur = frames ? fl((target - cur) / fl((float)frames) + cur) : target`, then `frames--` if non-zero.

### 3.8 Job dispatch

Pass 4 queues `FFX_Ch_UpdateRenderJob` for root CHRs in batches of about 200 joints via
`FFX_Ch_DispatchUpdateJob 0xA44B90`. That job builds skin matrices, runs `FFX_Ch_UpdateBonePositions`
(which writes the fields section 4.3 reads) and maintains `m_flags1` bit 0x200. `FFX_Ch_JoinUpdateJobs
0xA44B80` waits at `0x833217` (before pass 4 dispatches) and at the start and middle of
`FFX_Ch_DispatchInBatches`. [V]

---

## 4. Collision: the radius, the volumes, character to character

### 4.1 m_collisionRadius (0x4E8) and where it comes from [V]

`m_collisionRadius = ((m_scaleZ + m_scaleX) * 0.5) * m_collisionRadiusScale`, recomputed in the
integrate loop of every round, so nothing should ever write it directly.

`m_collisionRadiusScale` (0x4E4): `FFX_Ch_SetCollisionRadiusScale 0x82B880(chr, v)` writes `v`, or
`CHRDATA.m_f1C` when `v <= 0`. `FFX_Ch_Allocate` calls it with -1, so the default is the per-model
CHRDATA value. Other writers: `maybe_FFX_Btl_SetupUnitChr`, two battle helpers, `FFX_Ch_CopyState`,
`FFX_Ch_SetCollisionRadiusScaleAll` from script `Ch_077`, and script `Ch_057` per CHR. It is also read
by the shadow blob (`sub_82E3E0`, size `* 1.3`) and by water mode 2 as a minimum float depth.

### 4.2 The pair test and the push [V]

Membership: in use, `m_hideFlags == 0 || m_b181`, and `m_flags1 & 0x100` clear. Rebuilt in array order
every round. Sorted by `FFX_Ch_SortCollideListByZ 0x83EB30`, a selection sort that for each slot `i`
scans `j = i..n-1` and takes the **last** index whose `m_posZ <= current minimum's m_posZ`, then swaps
it into `i`. Ascending Z, not stable, and the tie rule matters for bit exactness. (The IDB comment says
descending. It is not, see section 9.)

For each `i` in sorted order and each `j > i`:

```
ri2 = 2.0 * r_i, rj2 = 2.0 * r_j                      (double)
if z_j - rj2 > z_i + ri2:        break                 (no later j can overlap)
if x_j - rj2 > x_i + ri2:        continue
if x_j + rj2 < x_i - ri2:        continue
if both have m_flags1 & 0x40000000:                  height band
    if bottom_j (0x698) < top_i (0x520): continue
    if top_j    (0x520) > bottom_i (0x698): continue
sum = fl(r_i + r_j)
d   = fl(sqrt(dx*dx + dz*dz)) with dx = fl(x_j - x_i), dz = fl(z_j - z_i)   (FFX_Ch_DistanceXZ 0x83E3E0)
if !(sum > d): continue
pushed += FFX_Ch_SeparatePair(i, j, sum, d)
touch: if i has 0x200 and speed_i != 0 -> j |= 0x4000. if j has 0x200 and speed_j != 0 -> i |= 0x4000
```

The broad phase uses twice the radius on each side, the narrow phase the plain sum. Only XZ, a pure
2D circle test, with an optional vertical band test.

`FFX_Ch_SeparatePair 0x83ECD0(a, b, sum, d)` (IDB `maybe_FFX_Ch_SeparatePair`). `m_f500` (0x500) is a
push weight, proposed name `m_pushWeight`, default 1.0 from `FFX_Ch_Allocate`:

```
wa, wb from m_pushWeight:
  both >= 0:   wa = a.w, wb = b.w, and both zero means both 1
  b < 0 only:  wa = 0, wb = 1
  a < 0 only:  wa = 1, wb = 0
  both < 0:    "mover pays" mode: da = fl(distXZ(a.prevPos, a.pos)), db likewise
               if da == db: return 0
               m = max(da, db), wa = da / m, wb = db / m
pen = fl(sum - d)
n   = normalize(a.pos - b.pos with y = 0)          (zero vector if coincident, see 3.5)
sa  = fl(pen * wa / (wa + wb))
if sa < 0: return 0
sb  = fl(pen - sa)
weights mode:  a.vel += n * sb,  b.vel -= n * sa      (heavier side pushes the lighter one further)
mover mode:    a.vel += n * sa,  b.vel -= n * sb      (the one that moved more is pushed back more)
each velocity component is fl(vel + n.c * s), and any non-zero result sets that CHR's m_flags1 0x20
return ((a.m_flags1 | b.m_flags1) & 0x20) != 0
```

So a negative weight means immovable against a non-negative one, and two negatives switch to the
"whoever moved more yields" rule. Every write is to `m_velX/m_velZ`, never to the position.

**Bit 0x20 is never cleared.** A scan of every `and`, `xor` and `mov` on `+0x194` and every
`0xFFFFFFDF` immediate in functions that touch `+0x194` found no clear, only the seven `or` sites in
`FFX_Ch_SeparatePair` and the wholesale copies (`FFX_Ch_CopyState`, `sub_843F10`). So once a CHR has
been pushed, every later overlap involving it reports "pushed" and triggers round two. [V for the scan,
I that `FFX_Ch_Allocate` starts it at zero]

### 4.3 The height band fields [V for the writes, I for the bone meanings]

`FFX_Ch_UpdateBonePositions 0x8350F0` (worker job, see 3.8) after filling `m_boneWorldPos`:

* `m_vec6B4` = bone 0 position, with `y -= m_f4F4`.
* feetY = max(bone 7 .y, bone 8 .y) (the lower of the two, +Y is down). midXZ = average of bones 7, 8.
* `m_prevBodyCentre = m_bodyCentre`.
* `m_bodyCentre = ((midXZ.x + bone1.x) / 2, feetY, (midXZ.z + bone1.z) / 2, bone7.w)`.
* `m_vec6A4 = (m_bodyCentre.x, bone1.y, m_bodyCentre.z, bone1.w)`.
* `m_510 = |m_vec6A4.y - feetY|`, the body height. `m_514 = |posY - feetY|`.
* `0x6C4..0x6D0 = (centre.x, feetY - 0.8 * height, centre.z, w)`.

So the collision band is `[m_520, m_bodyCentre.y]`, top to feet, where the top is either
`posY - m_51C` (script `Ch_128` sets `m_51C` through `FFX_Ch_SetF51C 0x82B340`) or the bone 1 height.
Bones 1, 7, 8 reading as head and feet is inferred from how they are combined.

### 4.4 Order dependence, the answer for parity and lockstep [V]

* **Pass 1 and the integrate are order independent.** Each CHR reads only itself, the walkmesh and
  globals that do not change during the pass (the walkmesh scratch is reset per call).
* **The pair pass is Jacobi, not Gauss-Seidel.** Positions do not change during it. Every pair sees
  every CHR's post-integrate position from the same round, so CHR N does not see N-1's push in that
  round. Pushes accumulate into velocity, and the accumulation order (sorted order, then j order) does
  change the float result when a CHR is in two or more overlapping pairs.
* **Round two applies the pushes through the walkmesh.** All CHRs integrate again, with velocity equal
  to their accumulated push, so a push into a wall slides like a step. Then the pairs are tested again,
  and any push from round two is **left in `m_velX/m_velZ` after the function returns**. Nothing
  integrates it, and pass 1 of the next sub-step overwrites both fields (zero when `m_speed == 0`,
  `cos * k` otherwise), so it never moves anything. It is only visible to a reader between the two.
* **The array order is semantic** in three places: ties in the Z sort, the order velocity pushes are
  summed, and which CHR is `i` in "i has 0x200, so j gets 0x4000". A recreation must keep the CHR slot
  order of the original pool, or at least reproduce the list order exactly.
* **The touch event is edge triggered.** After the rounds, a CHR with 0x4000 and without 0x8000 fires
  `FFX_Atel_FireEventOnChrActor(chr, 2)`, then 0x8000 takes the value of 0x4000. 0x4000 was cleared at
  the start of the call, so "bumped this sub-step" and "bumped last sub-step".

### 4.5 m_collisionVolumes (0x32C) is not character collision [V]

The 96-byte records are allocated by `FFX_Ch_BuildSkeletonInstance` at `0x82793F` as
`skeleton.u16[+0x2C] * 96` bytes, labelled `"%s:Collision"`, then cleared with a memset of
`count * 56` bytes (so the last 40 bytes of each record start as heap garbage until first written).
The only reader is `sub_822DE0`, run from `FFX_Ch_JointOverrideJob 0x82E390`, which `FFX_Ch_StepAll`
reaches through `sub_A44B40`. That function is gated on `m_flags1 & 0x200000` and `& 0x400000` and is
the secondary motion solver for hair and cloth chains: each frame it transforms the source records
(skeleton `+0x30`, 64 bytes each: `+0` radius, `+4` a second size, `+8` u16 type, `+10` s16 joint,
`+12` a length, `+16` local offset vec4, `+32` axis vec4) by their joint into the runtime records
(`+0` world centre, `+16` world axis), then pushes chain points out of them. Type 1 behaves like a
half space (push out by 1.2x when the dot is negative), type 2 like a capsule or cylinder (distance to
the axis against a scaled radius). `sub_824B40` is a hard coded per model, per event radius multiplier
(1.0 to 1.25). [V for the reads and branches, I for the shape names]

Nothing in `FFX_Ch_UpdateMotionAll`, `FFX_Ch_ResolveCollisionsAll` or the walkmesh code touches
`0x32C`. **Character to character collision is the 2D circle of section 4.2 and nothing else.**

---

## 5. Float determinism

### 5.1 The FPU mode [V]

* The CRT startup `_pre_c_init` calls `__setdefaultprecision 0x94A32E`, which is
  `_controlfp_s(0, _PC_53, _MCW_PC)`. The process runs x87 at **53-bit precision, round to nearest**.
* No `fldcw`, `fldenv`, `frstor` or `ldmxcsr` exists anywhere in the 194 functions reachable from
  `FFX_Ch_UpdateMotionAll` (depth 8) or in `FFX_Player__stepControl`'s tree. The 462 `fldcw` sites in
  the binary are all save, set chop, `fistp`, restore pairs on stack locals.
* Of those 194 functions only three far leaves (`sub_7EA850`, `sub_7E79A0`, `sub_42EED0`, reached
  through event code) contain any `xmm` instruction. **Every routine in this document is pure x87.**
* float to int is `__ftol2_sse` (truncation, identical on either path).

### 5.2 What that means for a 64-bit SSE recreation

At PC 53 an x87 add, sub, mul, div or sqrt on doubles is bit-identical to the SSE2 double operation
(the 15-bit exponent only matters for overflow, underflow and denormals, which none of this reaches).
So the porting rule is mechanical and exact:

1. Load every float operand as a double. Load `.rdata` double constants as doubles, float constants
   as floats widened.
2. Do every operation that stays on the FPU stack in double, in the original operand order.
3. Round to float exactly where the original does an `fstp dword` or `fst dword`, including stores
   to stack temporaries (`var_8`, `var_C`), which are easy to miss in Hex-Rays because they print as
   plain float locals.
4. Do int to float as `(float)int` (exact int, one rounding), float to int as truncation.
5. Reproduce every comparison's NaN behaviour from the parity flag test, not from the C operator
   Hex-Rays prints. The ones that matter: `m_speed == 0` treats NaN as moving, the run test treats NaN
   as walking, `ApproachAngle`'s `d == 0` treats NaN as non-zero.

A single float op on two float operands done in double and rounded once to float equals the plain
float op (53 >= 2 * 24 + 2), so SSE `float` arithmetic is fine for single-operation statements like
`posY = fl(velY + posY)`. It is wrong for chains like the velocity `k` or the edge functions.

### 5.3 Transcendentals [V for the dispatch, I where marked]

All three come from the game's own `msvcr110.dll` in the install folder (loaded ahead of the system
copy, and different from it, md5 `4ba25d2c...` versus `40ff84a8...` in SysWOW64).

| call | used by | what msvcr110 does |
|---|---|---|
| `_CIcos`, `_CIsin` | velocity, pass 1 | if `__use_sse2_mathfcns` (set at CRT init from `IsProcessorFeaturePresent(10)`, so on every SSE2 CPU, and `FFX.exe` never calls `_set_SSE2_enable`) **and** MXCSR has all exceptions masked with round to nearest (`& 0x7F80 == 0x1F80`) **and** the x87 control word has all exceptions masked (`& 0x7F == 0x7F`): spill st0 as a double and run the Intel SSE2 libm `cos`/`sin` (`0x10044AEF` for cos). Otherwise `fcos`/`fsin` with the control word forced to `0x27F`. On any normal PC the SSE2 path is taken. |
| `_CIsqrt` | `FFX_Ch_DistanceXZ`, normalize, stick magnitude | always x87 `fsqrt` with the control word forced to `0x27F`. Correctly rounded, so identical to `sqrtsd`. |
| `_CIatan2` | stick heading in `FFX_Player__stepControl` | no SSE2 dispatch at the entry, goes to the x87 table path (`fpatan`, I). |
| `_CIatan` | `FFX_Came_VecToPolar`, the camera yaw | has the same SSE2 dispatch as `_CIcos`. |

What the recreation needs to do about each:

* **sqrt**: nothing, `std::sqrt` on doubles is bit exact.
* **cos, sin**: the original is `fl(libm_cos((double)x))`. Any double `cos` whose error stays below
  about half a double ulp gives the same float except when the true value sits within that error of a
  float rounding boundary. That is rare (on the order of one input in 2^29) but not impossible, and
  `m_moveDir` is continuous. To claim bit parity, test the recreation's `cos` and `sin` against the
  original `msvcr110` over every float in `[-4pi, 4pi]` from inside the running game (the Workshop can
  call `_CIcos` directly) and either confirm zero mismatches or keep a small exception table. Do not
  copy Microsoft's code.
* **atan2**: its inputs in `FFX_Player__stepControl` are integer ramp differences in `[-256, 256]`, so
  there are only 513 * 513 possible calls. Dump them from the original and compare, which settles it
  exactly.

### 5.4 Where else determinism can slip

* The two rounds of collision and the Z sort are order sensitive (4.4).
* The position requantisation through the walkmesh scale (3.5) happens every sub-step.
* The angle wrap is a loop, so a huge `m_moveDir` costs iterations and an infinite one hangs.
* `FFX_Mot_BuildJointTransforms` is camera dependent (2.3).

---

## 6. CHR fields, proposed names and confirmations

These are proposals for `ffx_types.h`, not edits to it.

| offset | current name | proposal | evidence |
|---|---|---|---|
| 0x04C..0x054 | m_velX/Y/Z | keep, but note that a round-two push survives the return of `FFX_Ch_ResolveCollisionsAll` until the next pass 1 | 4.4 [V] |
| 0x07C..0x084 | m_groundNormal | keep. Comment fix: forced to (0,-1,0) when bit 0x80 ends up **set**, see 9 | `0x82AA93` [V] |
| 0x4E4 | m_collisionRadiusScale | keep. Default from `CHRDATA.m_f1C`, also the shadow blob size and the water minimum depth | 4.1 [V] |
| 0x4E8 | m_collisionRadius | keep. Derived every round, never write it | 3.4 [V] |
| 0x4FC | (in `_unk_4FC`) | `m_waterFloatDepth` | smoothed toward `max(m_514, m_4E4)` over 30 sub-steps in water mode 2 [V] |
| 0x500 | m_f500 | `m_pushWeight` | `FFX_Ch_SeparatePair` share rules, default 1.0, negative = immovable or mover-pays [V] |
| 0x510 | (in `_unk_508`) | `m_bodyHeight` | `|m_vec6A4.y - m_bodyCentre.y|` [V] |
| 0x514 | (in `_unk_508`) | `m_feetDepth` | `|posY - m_bodyCentre.y|`, read by water mode 2 [V] |
| 0x51C | m_f51C | `m_collisionHeight` | when non-zero the band top is `posY - this`, set by script `Ch_128` [V] |
| 0x520 | (in `_unk_520`) | `m_collisionTopY` | written in the integrate loop, read by the band test [V] |
| 0x694 | m_bodyCentre | keep, but note `.y` is the feet height (max of bones 7, 8), not a centre | 4.3 [V] |
| 0x6A4 | m_vec6A4 | `m_headPoint` | `(centre.x, bone1.y, centre.z)` [V], bone 1 = head [I] |
| 0x6B4 | m_vec6B4 | `m_rootPoint` | bone 0 position with `y -= m_f4F4` [V], bone 0 = root [I] |
| 0x870 | m_jointComposerMode | `m_jointSmoothingEnabled` | 0 forces the plain composer in `FFX_Mot_BuildJointTransforms` [V] |
| 0x871 | m_forceSmoothComposer | keep | non-zero forces the smoothed composer on every joint [V] |

`m_flags1` bits this pass reads or writes, with the meaning confirmed:

| bit | meaning | evidence |
|---|---|---|
| 0x20 | has been pushed by character collision, sticky | `FFX_Ch_SeparatePair` [V] |
| 0x40 | externally driven clip: skip the locomotion selector and the sequence VM | pass 1, `FFX_Mot_AdvanceFrame` [V] |
| 0x80 | free move: no walkmesh, no ground clamp in mode 0, normal forced to (0,-1,0) | 3.4, 3.6, `0x82AA93` [V] |
| 0x100 | excluded from character collision (still moves on the walkmesh) | `0x83D45C` [V] |
| 0x200 | the bound player, maintained by the render job | 2.3 [V] |
| 0x400 | slew `m_rotY` toward `m_moveDir` | 3.1 [V] |
| 0x4000, 0x8000 | bumped this sub-step, bumped last sub-step | 4.4 [V] |
| 0x20000 | `m_speed > m_runThreshold` | 3.2 [V] |
| 0x100000 | motion clock follows `g_ffxStepScale88` while KeepSp is set | 3.7 [V] |
| 0x1000000 | motion advanced this frame (pass 4), cleared at frame start | [V] |
| 0x2000000 | moving with non-zero speed this frame, no reader | `PLACEMENT.md` [V] |
| 0x4000000, 0x8000000 | idle clip chosen, walk or run clip chosen | 3.1 [V] |
| 0x20000000 | head below the water level (`posY - 8.5 > g_ffxWaterLevel`, +Y down) | 3.6 [V for the test, I for the wording] |
| 0x40000000 | use the vertical band in character collision | 4.2 [V] |

---

## 7. Proposed IDB names

IDA was not modified. One row each.

| VA | current | proposed | evidence |
|---|---|---|---|
| `0x8205E0` | `sub_8205E0` | `FFX_Dbg_IsChrUpdateFrozen` | returns `byte_12FBBBB`, gates `FFX_Ch_UpdateMotionAll`, `FFX_Ch_DispatchInBatches`, `FFX_Ch_UpdateRenderJob`, toggled by debug GUI item 103 at `0x8501DA` |
| `0x821CB0` | `sub_821CB0` | `FFX_Dbg_SetChrUpdateFrozen` | sole writer of `byte_12FBBBB`, `FFX_MainInit` passes 0 |
| `0x12FBBBB` | `byte_12FBBBB` | `g_ffxDbgChrUpdateFrozen` | above |
| `0x8205A0` | `sub_8205A0` | `FFX_Mot_GetKeepSpeed` | returns `byte_12FBC34`, the debug window prints it as `"KeepSp:%d"` |
| `0x821AE0` | `sub_821AE0` | `FFX_Mot_SetKeepSpeed` | stores `byte_12FBC34`, returns the old value, called by battle begin and step, event jump, script `Sg_034` |
| `0x12FBC34` | `byte_12FBC34` | `g_ffxMotKeepSpeed` | gates the `g_ffxStepScale88` scaling in `FFX_Mot_AdvanceFrame` |
| `0x836640` | `sub_836640` | `FFX_Map_SetWaterLevel` | writes `g_ffxWaterLevel` and `g_ffxWaterLevelMax` |
| `0x836660` | `sub_836660` | `FFX_Map_SetWaterLevelMax` | writes `g_ffxWaterLevelMax` only |
| `0x83ECD0` | `maybe_FFX_Ch_SeparatePair` | `FFX_Ch_SeparatePair` | 4.2, verified in full |
| `0x835F80` | `maybe_FFX_ApproachAngle` | `FFX_Mot_ApproachAngle` | 3.1, verified in full |
| `0x839200` | `maybe_FFX_Ch_IsNearlyStationary` | `FFX_Ch_IsNearlyStationary` | 2.3, verified |
| `0x839140` | `sub_839140` | `FFX_Ch_IsNearCameraForSmoothing` | body centre to camera eye squared distance against 900 or 2500, camera pitch against -0.785 |
| `0x6ED9B0` | `sub_6ED9B0` | `FFX_Vec3_LengthSq` | `x*x + y*y + z*z` rounded to float |
| `0x703BC0` | `sub_703BC0` | `FFX_Vec3_IsNormalizable` | not NaN, finite, length squared above `FLT_MIN` |
| `0x705DB0` | `sub_705DB0` | `FFX_Mtx_InverseRigid` | transposes the 3x3, translation = -(R^T t) |
| `0x6ED450` | `sub_6ED450` | `FFX_Mtx_InverseRigid_Api` | thunk to the above, host API entry 327 |
| `0x822DE0` | `sub_822DE0` | `FFX_Ch_StepSecondaryPhysics` | reads `m_collisionVolumes`, pushes chain points out of type 1 and 2 volumes (4.5) |
| `0x824B40` | `sub_824B40` | `FFX_Ch_GetVolumeRadiusScale` | per model, per event multiplier used by the above |
| `0x13009BC` | `dword_13009BC` | `g_ffxProfUpdateMotionAll` | the only write is the `g_ffxPs2HwRegSink` delta at the end of the step, no reader |

---

## 8. What is still open

* The exact operand order inside `FFX_Map_WalkmeshHeightAt` and `FFX_Ch_WalkmeshSlideAlongEdge` was
  read from Hex-Rays, not stack traced. Do that when porting them.
* Whether `msvcr110`'s SSE2 `cos`/`sin` ever disagree with a correctly rounded double implementation
  after the float rounding, over the range `m_moveDir` reaches. Needs the in-game sweep from 5.3.
* Whether the sub-step race in 2.3 item 3 has ever produced a visible difference. It needs a sub-step
  count above one, which retail play does not use as far as I can tell.
* The swim clip slots. `m_slots[19]` is chosen when the CHR is at least one unit below the water level
  and `m_slots[18]` otherwise. `ffx_types.h` calls 18 "submerged" and 19 "surface", which reads the
  other way round under +Y down, but I did not watch it at runtime.
* Whether `sub_A44B40` runs `FFX_Ch_JointOverrideJob` on a worker or inline. It matters for the IDB
  claim that `sub_822DE0`'s `rand()` draws are on the game thread, which this pass did not check.
* Scene id 298 is not identified. If `maybe_g_ffxSceneId` uses the event id numbering, `CHEAT_WARP.md`
  lists 298 as `bj/bjyt1200`, but the two globals are separate and I did not prove they share a space.

---

## 9. Corrections to earlier claims

Each proven against the binary in this pass.

1. **`ffx_types.h` line 143**, `m_groundNormalX`: "Forced to (0,-1,0) when m_flags1 bit 0x80 ends up
   clear". It is forced when bit 0x80 ends up **set**. `FFX_Ch_SetFlags1Bit80Inv 0x82AA70` does
   `test byte [edx+194h], 80h / jz 0x82AAC3`, and the write of 0, -1, 0 is on the fall-through, the
   set branch. The IDB function comment on `0x82AA70` has the same mistake.
2. **`ffx_types.h` `m_flags1` 0x20000000 "above water"**: the test is `posY - 8.5 > g_ffxWaterLevel`
   with +Y down, which is the head being below the surface. `PLACEMENT.md` already calls it
   submerged. [V for the test, I for which way the water level runs, from the mode 2 physics]
3. **IDB comment on `FFX_Ch_SortCollideListByZ 0x83EB30`**: says descending. The compare
   `fcompp / test ah, 41h / jp skip` selects when the scanned Z is less than or equal to the current
   pick, so the sort is ascending, with the last equal element winning a tie. The early `break` in the
   pair loop only makes sense for ascending order.
4. **IDB comment on `FFX_Mot_WrapAngleToPi 0x836720`**: says `(-pi, pi]`. The range is the closed
   `[-3.141592, 3.141592]` with `(double)3.141592f` as the bound.
5. **IDB comment on `FFX_Ch_ResolveCollisionsAll 0x83D3B0`**: says it is called from
   `FFX_Ch_StepAll`. Its only caller is `FFX_Ch_UpdateMotionAll` at `0x832FB3`.
6. **IDB function comment on `FFX_Ch_WalkmeshTestEdgeCrossing 0x83D960`**: still says it updates
   `CHR+0x824` "to the neighbour triangle (tri+6) or -1". It never writes -1 (`PLACEMENT.md` section 4
   already said so, the function comment was not updated).
7. **`MAIN_LOOP.md` section 4**: "FFX_Mot_AdvanceFrame at 0x838DF3 multiplies by g_ffxStepScale88".
   Only when `m_flags1 & 0x100000` and `byte_12FBC34` (KeepSp) are both set. A dated note is
   appended there.
8. **`PLACEMENT.md` section 8**: "Velocity cannot survive a frame boundary". A round-two collision push
   is left in `m_velX/m_velZ` after `FFX_Ch_ResolveCollisionsAll` returns. It cannot move anything
   because pass 1 overwrites it first, so that section's practical advice stands. A dated note is
   appended there.
9. **`MGRP_FORMAT.md` and the PROGRESS wording** put "animate" before "integrate". The clip is chosen
   before the move, but it is advanced after the clamp (section 0). Not wrong as a call list, worth
   knowing for ordering.

---

## 10. 2026-10-13, porting pass: the move, the slide and the step (PROGRESS 10.5, 12.2)

Written while porting `FFX_Ch_WalkmeshMove`, `FFX_Ch_WalkmeshSlideAlongEdge` and the whole of
`FFX_Ch_UpdateMotionAll` into Project Zanarkand (`src/engine/map/WalkmeshMove`, `src/engine/chr/Motion`,
cross checked bit for bit against an independent Python port in that repo's `tools/walkmesh_query.py` and
`tools/chr_motion.py`). Every routine below was stack traced from the disassembly in this pass. IDA was
read only.

### 10.1 FFX_Ch_WalkmeshSlideAlongEdge 0x83E180, instruction by instruction [V]

This settles the open point in section 8. `p` is the start (`CHR+0x3C`), `q` the attempted point, `a` and
`b` the two vertices `g_ffxWalkmeshCrossedEdgeV0/V1` point at.

```
edge  = (fl(b.x - a.x), 0, fl(b.z - a.z), 0)        s16 differences, fild / fstp dword
d     = (fl(q.x - p.x), 0, fl(q.z - p.z), 0)
e     = FFX_Vec4_Normalize(edge)                    copy, then FFX_Vec3_NormalizeInPlace
M     = identity, then m[0] = e.x, m[1] = 0, m[2] = -e.z, m[4] = 0, m[5] = 1, m[6] = 0,
        m[8] = e.z, m[9] = 0, m[10] = e.x            column major, m[c * 4 + r]
local = FFX_Mtx_TransformVec4(M, d)                 so local.x = e.x d.x + e.z d.z (along),
                                                    local.z = -e.z d.x + e.x d.z (across)
Minv  = FFX_Mtx_InverseRigid(M)                     through the thunk j_FFX_Mtx_InverseRigid 0x6ED450
local.z = 0
r     = FFX_Mtx_TransformVec4(Minv, local)
while (r.z * e.x - r.x * e.z) > 0:                  double, fldz / fcomp / test ah, 5 / jp
    local.z = fl(local.z - 1.0)
    r = FFX_Mtx_TransformVec4(Minv, local)
q.x = fl(r.x + p.x), q.z = fl(r.z + p.z)            q.y untouched
```

`FFX_Mtx_MulVec4 0x7058F0` (behind `FFX_Mtx_TransformVec4 0x6ED2E0`) sums each row as
`fl(((m[r] v0 + m[4+r] v1) + m[8+r] v2) + m[12+r] v3)`, all four terms even when they are zero, so a NaN or
an infinity in `d` spreads through the zero entries. `FFX_Mtx_InverseRigid 0x705DB0` copies the 3x3
transposed and writes `t'[r] = fl(-((m[4r] t0 + m[4r+1] t1) + m[4r+2] t2))`, zero in `[3]`, `[7]`, `[11]`
and 1 in `[15]`.

So the slide is the projection of the move onto the line through the **start** parallel to the edge,
not onto the edge itself: a character sliding along a wall keeps its distance from it. The loop only runs
when rounding leaves the projection a hair across, and each pass steps one walkmesh unit back. A move at
right angles to the edge projects to nothing and the character stays put for that try. The loop has no
cap, so an across component near 2^24 would spin, which nothing in play reaches. [V for the arithmetic,
I for "far side" as the name of the positive sign, which holds for the shipped winding in every case the
grid test hit]

### 10.2 FFX_Ch_WalkmeshMove 0x83E5F0, what section 3.5 leaves out [V]

* `m_walkmeshPos[i] = fl(pos[i] * s)` goes through the stack temporary `var_18` and the same value is the
  query point, so the two are always equal at the start of the move.
* The no-clip re-test inside the slide loop (`0x83E793`) can never fire. The loop is only entered when
  no-clip was false at the top, and nothing in between writes `g_ffxPlayerDebugNoClip`.
* The `m_walkmeshTri != -1` test before the slide always passes from the move: the loop restores the
  starting triangle after every try, and `FFX_Ch_WalkmeshTestEdgeCrossing` never writes -1.
* `FFX_Map_WalkmeshGetTriNormal` writes all four floats, so `CHR+0x88` (the w after `m_groundNormalZ`) is
  0 after any move that reaches the ground write. The no walkmesh path writes only x, y, z.
* Off the mesh the move is `fl(vel.x * 10 + pos.x)` and `fl(10 * vel.z + pos.z)` and nothing else,
  `m_walkmeshTri` stays -1, so the next sub-step scans again from the new point.
* In a 40 by 40 grid of moves on bsil01, bvyt11 and klyt02 (6,740 moves) one blocked try happened 1,131
  times, two 71, three 30 and four, the full refusal that leaves the character where it was, 49.

### 10.3 Corrections to sections 3 and 4 [V]

1. **Section 4.2, `if !(sum > d): continue`.** The test is `fcom / test ah, 41h / jnp`, which skips only
   an ordered `sum <= d`. A NaN sum or distance goes on to `FFX_Ch_SeparatePair`.
2. **Section 3.6, "ties take m_collisionRadiusScale".** `fcompp` of `m_514` against `m_4E4`, then
   `test ah, 5 / jp` loads `m_514` unless `m_514 < m_4E4` ordered. A tie, and a NaN, take `m_514`. It only
   shows for +0 against -0.
3. **Section 2.3 item 5.** The `and byte [esi+180h], 13h` in pass 1 is a read-modify-write before the gate.
   It clears hide bits 0x04, 0x08, 0x20, 0x40, 0x80 on every in-use CHR every sub-step, including ones the
   gate then skips because 0x01, 0x02 or 0x10 remain.
4. **Section 3.7, the clock.** On the normal path `m_frame` moves only while `m_seqPC` (0x714) is non-null.
   The divide by 7680 is the `0x88888889` multiply, a signed divide that truncates toward zero, and the
   KeepSp scale is `imul` then `shr 8`, unsigned. The half speed halve (`cdq / sub / sar`) also truncates
   toward zero and comes after `m_frameDelta` is stored. With `m_flags1 & 0x40` the clock runs only when
   `m_anmHeader` (0x800) is non-null. Without bit 0x80000000 it calls `FFX_Mot_WrapFrameAtEnd 0x8392D0`
   first, which reads the **previous** `m_frameDelta`, and the new delta is never stored. With bit
   0x80000000 (the `.anm` path) the frame advances by `m_playSpeed` alone, wraps on the s16 frame count at
   `m_anmHeader[0]` (a count of 0 would hang in the first wrap loop), and skips the hokan countdown.
5. **`FFX_Mot_WrapFrameAtEnd 0x8392D0`**, not written up before: clamp a negative frame to 0. If
   `((frameDelta + 0x1FF + frame) & ~0xFF) > ((clipEnd + 1) & ~0xFF)` and `m_loopCount == 1`: frame =
   `clipEnd & ~0xFF`, `m_clipPlaying = 0`, return 1. Otherwise while `frame > clipEnd`: a non-zero loop count
   (read unsigned) is decremented, and on reaching 0 the frame is set to `clipEnd & ~0xFF`,
   `m_clipPlaying = 0`, `m_playSpeed = 0`, return 1. A zero loop count with a zero `m_clipSpan` sets the
   frame to `m_clipStart` and returns 1. Otherwise subtract `m_clipSpan` and test again. Return 0.
6. **`FFX_Ch_AutoSwimAnim`** never touches `m_flags1` bits 0x4000000 or 0x8000000 and never calls
   `FFX_Mot_SetPendingLoopCount`. Only the ground selector does both.
7. **`g_ffxChrCollideList 0x1301B08` holds 40 entries** and `g_ffxChrCollideCount 0x1301BA8` follows it
   directly. The build loop (`0x83D468`) has no bound. A 41st collider is stored over the count and then
   immediately overwritten by the new count, and `FFX_Ch_SortCollideListByZ` would then read 41 as a CHR
   pointer. A 42nd lands on `0x1301BAC` and a 43rd on `g_ffxPlayerCamDistPreset 0x1301BB0`. [V for the
   layout and the missing check, I that the original crashes, since no scene was found with 41 colliders]
8. `dword_1301B0C`, `dword_1301B10` and `dword_1301B14` are not globals, they are `g_ffxChrCollideList + 4,
   + 8, + 12` as the four times unrolled inner loop of the sort addresses them. The unrolled loop uses the
   same compare as the tail loop, so the tie rule of section 4.2 holds throughout.

### 10.4 Capture recipe for the PROGRESS 10.5 done-when [I, not yet run]

The recreation now reproduces the step from a CHR state, so the oracle is a per step dump from the
running game on Besaid. Addresses are RVAs (VA - `0x400000`), which is what the Workshop takes.

Hook `FFX_Ch_UpdateMotionAll` at entry (RVA `0x432E10`) and at its return. At entry, and again at return,
copy:

* the step identity: `g_ffxMainStepCounter` RVA `0x1FCBBF0`, `maybe_g_ffxSceneId` RVA `0xF30844`, the
  current map name, and the dt argument (always `0x3D08B27A`, a check).
* the globals: `g_ffxChrArray` RVA `0x1FC44E4`, `g_ffxChrCount` RVA `0x1FC44E0`,
  `g_ffxWalkmeshScale` RVA `0xF01A90`, `g_ffxWalkmeshTriCount` RVA `0xF01A88`, `g_ffxWaterLevel`
  RVA `0xF009D8`, `g_ffxWaterLevelMax` RVA `0xF009DC`, the remap codes 48..63 at RVA `0xF01A98..0xF01AD7`,
  `g_ffxPlayerDebugNoClip` RVA `0x1FC44B4`, `byte_12FBBBB` (freeze) RVA `0xEFBBBB`, `byte_12FBC34`
  (KeepSp) RVA `0xEFBC34`, `g_ffxStepScale88` RVA `0x1FCBBEC`, `g_ffxMotHalfSpeed` RVA `0xF017F8`,
  `g_ffxControlledChr` RVA `0xF00788` (to mark which slot is the player).
* per in-use CHR (stride 0x880 from `g_ffxChrArray`): `+0x000` id, `+0x002`, `+0x00C..0x02B` pos and
  prevPos, `+0x03C..0x057` walkmeshPos and vel, `+0x05C`, `+0x064`, `+0x07C..0x08B` the normal, `+0x154`,
  `+0x158`, `+0x168..0x173`, `+0x180..0x182`, `+0x186`, `+0x194`, `+0x198`, the seven CHRANIMF at
  `+0x330..0x383`, `+0x4E4..0x523`, `+0x698`, `+0x6A8`, whether `+0x714` and `+0x800` are null, the s16 at
  `[+0x800]`, `+0x718..0x72B`, `+0x736`, `+0x740`, `+0x750..0x757`, `+0x75C..0x7DB`, `+0x7E0`, `+0x824`,
  `+0x828`.

Replay: load the first entry dump into the recreation's CHR array, then for every later step overwrite
only the fields something outside the step writes (`m_speed`, `m_moveDir`, `m_rotY` when a script set it,
`m_flags1` bits other code sets, the hide bits, `m_motionRequestActive` and the clip fields the sequence
VM owns, `+0x514`, `+0x698`, `+0x6A8` from the bone job, the globals), run one step, and compare the
return dump. Position, `m_rotY`, velocity, `m_groundHeight`, `m_walkmeshTri`, `m_groundAttrs` and the
`m_flags1` bits the step owns should match bit for bit under the C1 rule. Any mismatch confined to
`m_velX/Z` after a non-zero `m_speed` points at `_CIcos` and `_CIsin` (section 5.3) rather than the port.
Record with one sub-step per frame (section 2.3 item 3). A walk from the Besaid village entrance to the
temple door, with a stop, a turn on the spot and a bump into an NPC, covers every branch the scripted walk
in the recreation covers.

---

## 11. 2026-10-13, porting pass: motion playback (PROGRESS 10.4)

Written while porting the playback half of `FFX_Mot_AdvanceFrame` into Project Zanarkand
(`src/engine/chr/MotionLibrary` for the registry and the .chr fields, `src/engine/chr/MotionPlayback` for
the rest), cross checked bit for bit against an independent Python port in that repo's
`tools/chr_anim.py` on Tidus's shipped `resident0.mgrp`. Every routine below was read from the
disassembly in this pass, floats stack traced. IDA was read only. `MGRP_FORMAT.md` has the file layout
and the opcode census, this section has the runtime behaviour.

### 11.1 FFX_Mot_AdvanceFrame 0x838C90(chr, applyBlend), the whole function [V]

```
flags = m_flags1
if flags & 0x40:
    if m_anmHeader (0x800) == 0: return                      no weapon copy either
    if flags & 0x80000000: .anm clock (section 10.3 item 4), FFX_Mot_EvalAnmClip 0x838750, -> weapon copy
    else: WrapFrameAtEnd, delta = clock, halve -> jump to EVAL (skips the applyBlend test)
elif m_seqPC (0x714) == 0: -> weapon copy
else:
    delta = clock, KeepSp scale, m_frameDelta = delta
    FFX_Mot_StepSequence 0x8377C0
    halve
    if applyBlend == 0: -> ADVANCE                          FFX_Ch_UpdateMotionAll passes 1
EVAL:
    FFX_Mot_EvalChannels 0x8393C0
    FFX_Mot_BlendChannels 0x8395B0(chr, (s16)m_hokanCountdown)
    FFX_Mot_BuildJointTransforms 0x838F70
    hokan countdown (section 10.3)
ADVANCE:
    m_frame += delta
weapon copy (0x838E6F): m_parent (0x19C) set and m_id in 0x403D..0x4041 ->
    joint 1, 2, 3 dwords 57..60 = parent's joint 9, 10, 11 dwords 57..60      (rotation and its w lane)
```

So `FFX_Mot_StepSequence` sees `m_frame` before this step's delta, and so do the channels. The new
frame only shows on the next step.

### 11.2 Registration [V]

`FFX_Mot_RegisterBundle 0x836C90(hdr, kind, id, modelId)` takes the first null slot of
`g_ffxMotBundleTable 0x13011E0` (50 x 12 bytes: header, s16 kind, s16 id, s32 modelId). A full table
asserts "nb mgrp err" and stores nothing, but the banks are still registered. It relocates the bundle,
then calls `FFX_Mot_RegisterGroup 0x836D30` per bank in file order: the first slot of
`g_ffxMotGroupTable 0x1300A08` (100 x 20 bytes: bank, s16 model from `bank+4`, u16 chained, next same
model, next model, owning header) whose bank is null, then `FFX_Mot_RebuildGroupChains 0x838170`.

The rebuild clears every chained flag and next same model, then walks the slots in order. Each unchained
slot becomes a model head: `g_ffxMotGroupChainHead 0x13011D8` is the first, each head's next model is the
next head. Under each head, every other unchained slot of the same model is appended in slot order and
marked chained. So a freed slot refilled by a later bundle goes ahead of older banks of its model.

`FFX_Ch_SetMotionKey 0x837AC0` resolves an id by walking heads (movsx model against `id >> 16`, so a
model id of 0x8000 or more never matches), then that one chain, comparing each entry's u16 index
against `id & 0xFFFF`, first match wins. It never looks at a second head of the same model, there is
none by construction.

`FFX_Ch_ReleaseResource 0x836E20(kind, id, modelId)`: `FFX_Mot_FindBundle 0x836F10` matches all three,
`FFX_Mot_UnregisterGroup 0x836DE0` per bank (each rebuilds the chains), `FFX_Mot_UnregisterBundle
0x836DA0`, the free, then `FFX_Ch_ClearResourcePtrs 0x837540`, which zeroes `m_seqPC`, `m_motionId` and
`m_motionBundle` (0x7E8) on every in-use CHR whose `m_motionBundle` was that header.

### 11.3 FFX_Ch_SetMotionKey 0x837AC0(chr, id) and FFX_Mot_SetByModeIndex 0x837D00 [V]

SetMotionKey, in order:

1. `m_jointRemap` (0x758) = 0. If `(u16)m_id != (id >> 16) & 0xFFFF`, search the motreltabs at CHRDATA
   `+0x114` (s16 count at `+0x110`) for one whose s16 model is the motion's. None: `m_motionRequestActive
   = 0`, `m_clipPlaying = 1`, return 1.
2. Same id and `m_i730` (0x730) zero: return 1, nothing written.
3. `m_flags3 |= 2`, `m_i730 = 0`. If `(m_id & 0xF000) == 0` and the high words of the old and new id
   differ, `m_nextHokan = 1`. `m_motionId = id`. `sub_824800` (the motion patch flag, 11.10).
4. The chain walk fails: byte 0x7ED = 1, `m_motionRequestActive = 0`, `m_clipPlaying = 1`, return 1.
   The id is kept, so asking again for the same id returns at step 2.
5. Found: `m_motionGroup` (0x710) = bank, `m_motionEntry` (0x70C) = entry, `m_seqPC` = entry `+0xC`, the
   dword at 0x728 = 0 (that is `m_clipPlaying` and `m_seqWait` together), `m_pendingLoopCount = -1`, the
   word at 0x737 = 0x00FF (`m_seqWaitKey = -1`, byte 0x738 = 0), `m_motionRequestActive = 1`,
   `m_blendFrames` (0x73A) = `m_nextHokan` and then `m_nextHokan = 0` and `m_flags3 |= 4` when it was
   set, else 4. `m_flags1 |= 0x1000000`, `m_motionBundle` = the group's header, `m_eventCursor` (0x7E4) =
   -1, the word at 0x7ED = 0, byte 0x73E = 1. Return 0.

So a CHR's first motion, and any motion of another model, starts with a one frame hokan, which snaps
every channel to the new clip instead of blending over four steps.

SetByModeIndex, the .chr shape (CHRDATA `m_flags` bit 1, which `FFX_Ch_BlkAllocate` always sets): the
table is the section `g_ffxMotModeToSetSlot 0xC49784[mode]` = {5, 6, 7, 8}. A null table returns. Then
loop: if `m_flags2 & 0x200` and mode 1 and index 16, `sub_83D040(chr, 4, 1)` (a texture animation
pattern, 11.10). A non-zero `table[index]` goes to SetMotionKey. A zero one in mode 1..3 restarts at mode
0 index 0, in mode 0 it returns. The index is not bounded. The viewer shape at CHRDATA `+0x108` is not
used by the game.

The two locomotion selectors (section 3.1) call SetMotionKey for a slot whose low word is 0x1000 or
more, else SetByModeIndex(chr, mode, slot), and only then or `m_flags1` with 0x8000000 or 0x4000000
(memory read modify write, so the setter's own or of 0x1000000 survives).

### 11.4 The sequence VM [V]

`FFX_Mot_StepSequence 0x8377C0` runs `FFX_Mot_SeqExec 0x837900` until it returns non-zero, then: 1 ->
`FFX_Mot_WrapFrameAtEnd`, 2 -> WrapFrameAtEnd and, if `m_clipPlaying` is 0, `m_motionRequestActive = 0`,
3 -> byte 0x734 = 1 and `m_motionRequestActive = 0`, -1 -> the "seq err" assert and nothing else. A
JUMP to itself would hang the game. No shipped sequence has a JUMP.

SeqExec reads the opcode with `movsx`, so 0x80..0xFF are negative and take the default case. The
handler column is each case's target in the jump table at 0x83791D (read from the switch, 2026-10-13).

| op | handler | bytes | effect | returns |
|----|---------|-------|--------|---------|
| 0 END | 0x837924 | 1 | nothing, the PC stays | 2 |
| 1 PLAY | 0x837946 | 9 | below | 0 |
| 2 WAIT_CLIP_END | 0x8379DB | 1 | `m_clipPlaying != 0` -> block, else PC += 1 | 1 or 0 |
| 3 WAIT_FRAMES | 0x837A1E | 3 | `m_seqWait == 0` -> load the u16, then `--m_seqWait`, zero -> PC += 3 | 1 or 0 |
| 4 JUMP | 0x837A55 | 3 | PC = entry `+0xC` + (s16)labels[(s16)n], labels at entry `+8` | 0 |
| 5 STOP | 0x837935 | 1 | nothing | 3 |
| 6 WAIT_KEY | 0x8379F4 | 3 | `(s16)(s8)m_seqWaitKey < (s16)n` -> block, else PC += 3 | 1 or 0 |
| other | 0x837A82 | | "unknown motion sequence code" | -1 |

PLAY (s16 clip, s16 loop, s16 start marker, s16 end marker): the clip entry is `[m_motionGroup + 0x10] +
16 * clip`, markers at entry `+8` as s16. start = markers[start index], end = markers[end index], and the
end is incremented unless the end index (compared as a u16) is 1 or the loop count is 1. Then
`FFX_Mot_StartClip(chr, entry+0xC, loop, start << 8, end << 8)`, PC += 9, byte 0x73E = 0.

`FFX_Mot_StartClip 0x839B80(chr, clip, loop, start, end)`:

1. The hardcoded patch: model name "c001", `maybe_g_ffxSceneId == 122`, start 0x3000 and end -256 (the
   "to the clip end" marker) -> end = 0x2800.
2. `m_frameRateScale = 7680`, then `FFX_Mot_BindChannels`, which overwrites it from the clip.
3. `m_clipStart = start`. A negative end is `(s16)clip[0] << 8`. `m_clipSpan = end - start`, `m_clipEnd
   = end - 1`. `m_loopCount = m_pendingLoopCount` unless that is -1, then the operand.
   `m_pendingLoopCount = -1`, `m_playSpeed = 256`, `m_frame = m_clipStart`, `m_hokanCountdown =
   m_blendFrames` (1 when that is 0), `m_flags1 &= ~0x10000`, `m_clipPlaying = 1`.

### 11.5 Channels and key streams [V]

A channel is 44 bytes, nine a joint (rotation x, y, z in turns, translation x, y, z, scale x, y, z), the
array at CHR `+0x748`, sized by the skeleton's joint count (`[CHR+0x1C0] + 10`) and zeroed by
`FFX_Ch_BuildSkeletonInstance`. Fields: `+0` next streamed channel, `+4` mode, `+5` is angle, `+6` u16
repeat, `+8` stream start, `+0xC` cursor, `+0x14` u16 delta, `+0x16` u16 key, `+0x18` u16 previous key,
`+0x1A` u16 key saved by a rewind, `+0x1C` float target, `+0x20` float current, `+0x28` byte dirty.

`FFX_Mot_BindChannels 0x839980(chr, clip)`: `m_frameRateScale = (s16)clip[+4]`. A non-zero s16 event
count at `+6` sets `m_eventCount` and `m_eventArray = clip + [clip+0x10]`, a zero one clears only the
array. Channels = 9 x min(skeleton joints, (s16)clip joints), signed. Modes are 2 bits a channel, low
bits first, refilled as a 64 bit pair every 32 channels from `clip + [clip+8]`. Values run from `clip +
[clip+0xC]`:

* mode 0 -> target 0.0, mode 1 -> target 1.0, mode 2 -> target = Key12(next u16), values += 2.
* mode 3 -> streamed: is angle = channel % 9 < 3, start = cursor = values + 2, repeat 0, the dword at
  `+0x14` = 0 (delta and key), pushed on the front of the stream list, values += 2 x ((s16)len / 2) where
  len is the u16 at values, the stream's byte length including itself.
* every bound channel gets dirty = 1. Channels past the clip's joint count keep what they had.

Then `m_streamList` (0x74C) = the list head (the last streamed channel), `FFX_Mot_DecodeKeyStream(chr,
1)`, previous key = key on every list channel, `m_frameCursor` (0x7DC) = 1.

`FFX_Mot_DecodeKeyStream 0x8394D0(chr, n)`, per list channel, nothing when n <= 0, else n times:

```
if repeat: repeat -= 1                                    the old delta again
else: b = *cursor++
    b < 0x80:          delta = sext7(b)
    b & 0xC0 == 0xC0:  delta = sext14((next byte << 6) | (b & 0x3F)), cursor++
    b & 0xC0 == 0x80:  repeat = b & 0x3F                  this frame takes the old delta too
prev = key, key = (u16)(key + delta)
```

`FFX_Mot_RewindChannels 0x8398F0`: per list channel cursor = start, saved = key, repeat = delta = key =
0, then one frame decoded, then previous = saved on every list channel, `m_frameCursor = 1`.

`FFX_Mot_EvalChannels 0x8393C0`:

```
whole = (m_frame + 0x1FF) >> 8, last = (m_clipEnd + 1) >> 8, m_frameSnapshot (0x744) = m_frame
if whole > last: whole = (m_loopCount == 1) ? last : whole - (m_clipSpan >> 8)
n = whole - m_frameCursor
if n < 0 (sign of the subtraction): Rewind, n = whole - 1
if whole > m_frameCursor (re-read after the rewind): Decode(n), m_frameCursor += n
frac = m_frame & 0xFF, per list channel:
    frac == 0:  target = Key12(key)                       no 12 bit mask, so it can leave [0, 1)
    angle:      target = LerpAngle12((s16)prev, (s16)key, frac)
    else:       a = Key12(prev), d = fl(Key12(key) - a), target = fl(Frac8(frac) * d + a)
```

`Key12ToFloat 0x839DD0(k)` = fl(fl((float)(s16)k) * 2^-12), `Frac8ToFloat 0x839E00(f)` = fl(fl((float)(s16)f) *
2^-8). `FFX_Mot_LerpAngle12 0x839E80(prev, cur, frac)`: a = Key12(prev & 0xFFF), b = Key12(cur & 0xFFF), t
= Frac8(frac), d = fl(b - a). If d >= 0.5 (ordered): a = fl(a + 1), d = fl(b - a). Then if d <= -0.5
(ordered): b = fl(b + 1), d = fl(b - a). Result fl(a + d * t). Exactly half a turn apart forward takes
both unwraps, so lerping 0x000 to 0x800 gives 1.25 at the midpoint, not 0.75. Harmless, it is the same
angle.

### 11.6 The hokan blend [V]

`FFX_Mot_BlendChannels 0x8395B0(chr, n)` with n = `(s16)m_hokanCountdown`:

* `m_flags1 & 0x10000` (settled): the stream list only. Angle channels current = StepAngle(current,
  target, n), the others the linear step. Every one dirty = 1.
* otherwise every channel block of every skeleton joint, including joints the clip does not cover:
  channels 0..2 StepAngle, 3..8 linear, all dirty = 1. Then `m_flags1 |= 0x10000` when n == 1.

The linear step: d = fl(target - current), skipped only for an ordered d == 0, else current = fl(d /
(float)n + current).

`FFX_Mot_StepAngle 0x839F50(cur, target, n)`: a = fl(cur - (float)trunc(cur)), plus 1 if negative. d =
fl(a - target), e = fl(d - (float)trunc(d)), plus 1 if negative. An ordered e == 0 returns a. An ordered
0.5 > e returns fl(a - e / n), anything else fl(a + (1 - e) / n). Truncation is `__ftol2_sse`.

Consequence: the full blend zeroes every channel the clip never bound, scale included. For c001 the clips
cover 118 of the 136 joints, so joints 118..135 compose to scale 0. No hierarchy node of the c001 mesh
remaps to them, so nothing visible collapses.

### 11.7 FFX_Mot_BuildJointTransforms 0x838F70 and the composers [V]

s = fl(fl(CHRDATA `+0x38`) * (double)0.001f). The composer:

```
if CHR byte 0x871:                                          Smoothed for every joint, no scene override
else if CHR byte 0x870 == 0 or m_flags3 & 4:                Plain
else:
    mode = IsNearlyStationary ? (IsNearCamera ? Smoothed : Auto) : Plain
    scene overrides to Plain: 368, 285, 80 with m_id 4, 311 with m_id 0x5045, 100 with m_id != 0,
    60 while FFX_Story_GetProgress() < 0x9A
```

Then per channel block i < skeleton joints: with a motreltab, stop at i >= its u16 count at `+2`, the
destination joint is the u16 at `+8 + 2i`, 0xFFFF skips. Without one, joint i. The composer writes:

| composer | rotation k (dirty) | translation k (dirty) | scale k (dirty) | every call |
|----------|--------------------|-----------------------|-----------------|------------|
| Plain `0x838280` | r -> dwords 57+k and 69+k | t -> 61+k and 73+k | c -> 65+k and 77+k | |
| Smoothed `0x838470` | r -> 69+k | t -> 73+k | c -> 65+k and 77+k | 57+k = SmoothAngle(69+k, 57+k, 0.7f, 0, 10), 61+k = SmoothLinear(73+k, 61+k, 1, 0, 1) |
| Auto `0x838240` | joint byte 324 (`+0x144`) set -> Smoothed, else Plain | | | |

with r = `0x83A010`(fl((current * 2.0) * (double)3.141592f)), the same wrap loop as `FFX_Ch_WrapAnglePi`,
and t = fl((current * s) * 4096.0). Each clears the channel's dirty byte.

`sub_839CD0`(raw, cur, k, floor, m): if raw - cur > pi', cur = fl(cur + 2pi'). Then if cur - raw > pi',
cur = fl(cur - 2pi'). w = fl(k - |raw - cur| * m), floor it (ordered floor > w), return
`sub_839CB0`(raw, cur, w) = fl((1 - w) * raw + w * cur). `sub_839D70` is the same without the wrap. With
k 0.7 and m 10 an angle more than 0.07 radians off snaps to the raw value, closer ones keep up to 70
percent of the old one. Translation with k 1 and m 1 keeps 1 - |d| of the old one.

`FFX_Ch_IsNearlyStationary 0x839200`: false while `g_ffxBtlSubPhase` is non-zero or ATEL context byte 0
bit 2 is set (the byte `g_ffxAtelCtx` points at), else true iff |`m_prevBodyCentre` (0x684) -
`m_bodyCentre` (0x694)| < 2.0 on x, y and z, ordered, the difference unrounded on the x87. `FFX_Ch_IsNearCameraForSmoothing 0x839140`: v = fl(`m_bodyCentre` - eye) per component with the
eye at `g_ffxCameActiveSlot + 0x65C`, len2 = fl((x x + y y) + z z) (`sub_6ED9B0`), near = 2500 if the
model name contains "c10" else 900, pitch = slot `+0x6B0` when slot byte `+0x64D` is set, else slot
`+0x6D0`. True iff near > len2 and pitch > -0.785398006439209, both ordered.

The joint byte 324 comes from `FFX_Ch_BuildSkeletonInstance 0x8277F0` (0x827BB9): `maybe_FFX_Ch_InitJointNode
0x828B60` sets it to 1 on every joint and on the embedded root node at CHR `+0x1C8`. Bone points 7 and 8
(`FFX_Ch_GetBoneNodeIndex`) are walked up the parent pointers in step until they meet. If both are
non-zero and they meet on a node, that node gets 0, then in joint order every joint whose parent has 0
gets 0 (the meeting node's subtree), then the path from the meeting node to the root, root node
included, gets 0. Points at different depths would walk one side off the null root parent, which no
shipped .chr does. For c001 the points are joints 95 and 82 and 31 of 136 joints end at 0.

### 11.8 Motion events [V]

`FFX_Ch_DispatchInBatches 0x833390`, called from `FFX_MainStep` after the step, joins the render jobs,
then two passes over the CHR array, root CHRs then CHRs with `m_parent`. A live, visible (hide byte 0 or
`m_b181`), unfrozen CHR with `m_flags1 & 0x1000000` runs `FFX_Mot_RunEvents 0x834350` and clears
`m_flags1 & 0x1000`, the others set 0x1000.

RunEvents: nothing without `m_eventArray`. cur = `m_frameSnapshot >> 8`, last = (s16)`m_lastEventFrame`
(0x7F2). If cur > last, for each of the s16 `m_eventCount` records (u8 type, s8 payload dword count, s16
start, s16 end, s16 id, the payload, stride 8 + 4 x count): fire when last < end and cur >= start. Type 1
`SndKickFoot(chr, id)`, 2 the callback at `[CHR+0x7F8]` with (`[CHR+0x7FC]`, id) when set, 3
`sub_77E940([CHR+0x700], id >> 8, id & 0xFF, [CHR+4], [CHR+0x830])` when `[CHR+0x6FC]` is non-zero, 5
`sub_7B74D0([CHR+0x18C], id, 0)`. Always `m_lastEventFrame = cur`. Because the test is "last < end", an
event fires on every step that ends at or past its start while the previous step was still before its
end, not once per crossing. c001's walk and run clips carry four SND events each, the idle none.

### 11.9 Globals read, with their writers [V]

| global | VA | RVA | read by | writers |
|--------|----|-----|---------|---------|
| `g_ffxMotGroupTable` | `0x1300A08` | `0xF00A08` | SetMotionKey | RegisterGroup, UnregisterGroup, RebuildGroupChains |
| `g_ffxMotGroupChainHead` | `0x13011D8` | `0xF011D8` | SetMotionKey | RebuildGroupChains |
| `g_ffxMotBundleTable` | `0x13011E0` | `0xF011E0` | FindBundle | RegisterBundle, UnregisterBundle |
| `g_ffxMotModeToSetSlot` | `0xC49784` | `0x849784` | SetByModeIndex | none, `.rdata` {5, 6, 7, 8} |
| `maybe_g_ffxSceneId` | `0x1330844` | `0xF30844` | StartClip, BuildJointTransforms | `maybe_FFX_Scene_Init` |
| `g_ffxStoryProgress` | `0x112D67C` | `0xD2D67C` | BuildJointTransforms (scene 60) | the save block, `FFX_InitNewSaveData` and the event scripts |
| `g_ffxCurrentEventId` | `0x12FBC40` | `0xEFBC40` | `sub_824800` | `FFX_Ev_LoadEventPackage` |
| `g_ffxBtlSubPhase` | `0x112A8E0` | `0xD2A8E0` | IsNearlyStationary | the `FFX_Btl_*` phase code, 11 writers |
| `g_ffxAtelCtx` | `0x1326B28` | `0xF26B28` | IsNearlyStationary (bit 2 of the byte it points at) | the ATEL interpreter |
| `g_ffxCameActiveSlot` | `0x2311430` | `0x1F11430` | IsNearCameraForSmoothing | `FFX_Came_ActivateScreen`, `FFX_Came_Init` |
| `g_ffxMotKeepSpeed` | `0x12FBC34` | `0xEFBC34` | AdvanceFrame | debug GUI |
| `g_ffxStepScale88` | `0x23CBBEC` | `0x1FCBBEC` | AdvanceFrame | `FFX_MainStep_PublishStepScale` |
| `g_ffxMotHalfSpeed` | `0x13017F8` | `0xF017F8` | AdvanceFrame | `FFX_Mot_SetHalfSpeed`, debug GUI |
| `g_ffxMotEventLog` | `0x13008DB` | `0xF008DB` | RunEvents, log only | `FFX_Mot_SetEventLogEnabled` |
| `g_ffxMainStepCounter` | `0x23CBBF0` | `0x1FCBBF0` | RunEvents, log only | `FFX_MainStep` |

No random number is drawn anywhere in this tree.

### 11.10 Corrections and new facts [V]

1. **`m_flags3` (0x820) bit 4 is not "hidden".** SetMotionKey sets it when the hokan came from
   `m_nextHokan` (a one frame snap), `FFX_Ch_SetBlendFrames` and `FFX_Ch_SetHideFlagBits` also set it,
   and the only clears are in `FFX_Ch_StepSecondaryPhysics 0x822DE0` (at `0x822FD9`, `0x822E32` and
   `0x823161`), which runs on the render job from `FFX_Ch_JointOverrideJob` and only for CHRs with
   `m_flags1` 0x200000 and 0x400000. While it is set, BuildJointTransforms uses the plain composer. So a
   CHR without secondary physics that ever snapped keeps the plain composer until something else clears
   the bit. Tidus has the physics, so for him it lasts one step. Proposed meaning: "secondary physics
   reset pending".
2. **Bind rotations round once.** `0x827A3D`: `fild` then `fstp` to a float (exact for an s16), then v /
   100 x (double)3.141592f / 180 on the x87 and one `fstp`, then `FFX_Ch_WrapAnglePi`. There is no
   intermediate rounding.
3. **`sub_824800`**, which SetMotionKey calls: `m_flags3 &= ~8`, then sets 8 for CHR 2 and 102 when the
   motion id is in 0x2113D..0x2113F or is 0x6610CC (signed compares) or `g_ffxCurrentEventId` is 161, and
   for CHR 7 and 107 in events 183 and 309. It prints "CLOTHJIMEN" when set. Proposed
   `FFX_Ch_UpdateClothGroundFlag`.
4. **`sub_83D040(chr, a, b)`** calls `sub_77E940([CHR+0x700], a, b, [CHR+4], [CHR+0x830])` when
   `[CHR+0x6FC]` is non-zero, the same call the TEX motion event makes. So both are texture animation
   pattern switches.
5. `m_seqWait` (0x72A) has no reset of its own. The dword store at SetMotionKey step 5 clears it together
   with `m_clipPlaying`, and WAIT_FRAMES leaves it at 0 when it passes.

### 11.11 Proposed IDB names [I for the names, V for the behaviour]

| VA | current | proposed | why |
|----|---------|----------|-----|
| `0x83A010` | `sub_83A010` | `FFX_Mot_WrapJointAngle` | the composers' copy of the pi wrap loop |
| `0x839CB0` | `sub_839CB0` | `FFX_Mot_SmoothMix` | fl((1 - w) a + w b) |
| `0x839CD0` | `sub_839CD0` | `FFX_Mot_SmoothAngle` | the smoothed composer's rotation damping |
| `0x839D70` | `sub_839D70` | `FFX_Mot_SmoothLinear` | the smoothed composer's translation damping |
| `0x838470` | `sub_838470` | `FFX_Mot_ComposeJointSrtSmoothed` | 11.7 |
| `0x838240` | `sub_838240` | `FFX_Mot_ComposeJointSrtAuto` | 11.7 |
| `0x839140` | `sub_839140` | `FFX_Ch_IsNearCameraForSmoothing` | 11.7 |
| `0x6ED9B0` | `sub_6ED9B0` | `FFX_Vec3_LengthSq` | (x x + y y) + z z, stored |
| `0x824800` | `sub_824800` | `FFX_Ch_UpdateClothGroundFlag` | 11.10 item 3 |
| `0x83D040` | `sub_83D040` | `FFX_Ch_SetTexAnimPattern` | 11.10 item 4 |
| `0x77E940` | `sub_77E940` | `FFX_TexAnim_SetPattern` | the callee of both, takes the CHR's tex anim block |
| `0x7B74D0` | `sub_7B74D0` | `FFX_Btl_OnMotionEvent` | the type 5 motion event |
| `0x832540` | `sub_832540` | `FFX_Ch_MarkEventsDispatched` | the last call of DispatchInBatches, sets `byte_13009E4` |
| `0x828B60` | `maybe_FFX_Ch_InitJointNode` | `FFX_Ch_InitJointNode` | confirmed: parent 0, byte 324 = 1, dwords 57..64 = 0, 65..68 = 1 |

### 11.12 Capture recipe for the PROGRESS 10.4 done-when [I, not yet run]

The recreation reproduces the joint outputs from a CHR state, the loaded `.mgrp` and the composer inputs.
The oracle is a per step dump of Tidus (`m_id` 1) on a field map. Addresses are RVAs (VA - `0x400000`).

Hooks, all on the step thread:

* `FFX_Mot_AdvanceFrame` entry and return, RVA `0x438C90`. Filter on `[esp+4]` being Tidus's CHR.
* `FFX_Ch_IsNearlyStationary` RVA `0x439200` and `FFX_Ch_IsNearCameraForSmoothing` RVA `0x439140`,
  return values only, as a check on the composer choice.
* Optional: `FFX_Mot_StepSequence` RVA `0x4377C0` entry and return, to split a mismatch between the VM
  and the channels. `FFX_Ch_SetMotionKey` RVA `0x437AC0` entry (the id argument) to log clip changes.
* `FFX_Mot_RunEvents` RVA `0x434350` entry and return, to log `m_lastEventFrame` and what fired.

At AdvanceFrame entry, copy:

* the step identity: `g_ffxMainStepCounter` RVA `0x1FCBBF0`, `maybe_g_ffxSceneId` RVA `0xF30844`.
* the globals: `g_ffxStoryProgress` RVA `0xD2D67C` (u16), `g_ffxCurrentEventId` RVA `0xEFBC40`,
  `g_ffxBtlSubPhase` RVA `0xD2A8E0`, the byte `g_ffxAtelCtx` RVA `0xF26B28` points at,
  `g_ffxMotKeepSpeed` RVA `0xEFBC34`, `g_ffxStepScale88` RVA `0x1FCBBEC`, `g_ffxMotHalfSpeed` RVA `0xF017F8`,
  and through `g_ffxCameActiveSlot` RVA `0x1F11430`: byte `+0x64D`, floats `+0x65C..0x667`, `+0x6B0`,
  `+0x6D0`.
* the CHR: `+0x000` id, `+0x194` `m_flags1`, `+0x198`, `+0x19C` parent, `+0x684..0x6A3` the two body
  centres, `+0x70C..0x75B` (entry, group, PC, clip fields, waits, motion id, hokan, frames), `+0x7DC..0x7FF`,
  `+0x800`, `+0x820`, `+0x870`, `+0x871`, and the pointer `+0x7E8` (`m_motionBundle`) and `+0x748` (channels).
* the channel array: `[CHR+0x748]`, skeleton joints x 396 bytes, the skeleton joint count being the u16
  at `[CHR+0x1C0] + 10` (136 for c001).
* the joints: `[CHR+0x328]`, stride 352, per joint dwords 57..79 (bytes `+0xE4..0x13F`) and byte `+0x144`.

At AdvanceFrame return, copy the channel array, the joint block and the CHR range `+0x70C..0x7FF` again.

Pointers become offsets before replay: every stream pointer in a channel (`+8`, `+0xC`), `m_seqPC`,
`m_motionEntry`, `m_motionGroup` and `m_eventArray` minus the header address of the bundle they point
into, which for Tidus's locomotion is his `resident0.mgrp` (the dword at `m_motionBundle` is its header,
relocation leaves every offset the shipped file has). The next channel pointer at `+0` becomes an index,
(ptr - `[CHR+0x748]`) / 44.

Replay: load the entry dump into a MotionChr (channels, joints, the CHR fields, the bundle registered the
way the game did), set the MotionWorld globals and camera from the dump, run the recreation's
AdvanceFrame (through `FFX_Ch_UpdateMotionAll` pass 4, or directly), and compare the return dump. Joint
dwords 57..79 and every channel's key, previous key, target and current should match bit for bit. A
mismatch only in dwords 57..63 with an agreeing 69..75 is the composer choice, which the two hooked
returns settle. Between steps, the recreation has to be handed what the render job wrote: `m_bodyCentre`,
`m_prevBodyCentre` and the `m_flags3` bit 4 clear from `FFX_Ch_StepSecondaryPhysics`. Taking each step's
entry dump as the state, rather than chaining the recreation's own output, sidesteps all of them.

A session that covers the done-when: stand for two idle loops, walk, break into a run, drop back to a
walk, stop, all with the camera both far (the auto composer) and pulled close (the smoothed one). Any
field map works, the motion code reads nothing from the map but the scene id.

### 11.13 Still open

1. `FFX_Mot_EvalAnmClip 0x838750`, the `.anm` path for `m_flags1` 0x40 | 0x80000000, is not traced.
2. The viewer shape of `FFX_Mot_SetByModeIndex` (CHRDATA `+0x108`) is not traced. No game CHR uses it.
3. The joint smoothing reads the camera, so the pose is per viewer. Section 2.3 item 1 still stands.
4. `FFX_Ch_StepSecondaryPhysics` and the bone pass that writes `m_bodyCentre` run on worker jobs and
   feed the next step's composer choice. Neither is ported. Correction 2026-10-13: StepSecondaryPhysics
   is not on the render job. Its only caller is `FFX_Ch_JointOverrideJob 0x82E390`, which `FFX_Ch_StepAll`
   posts to queue 0, which any of the four workers drains (0x82FD9A, 0x82FDD6). It also calls the CRT `rand()` at 0x82395B (`rand() % 1000`,
   a jitter scale). MSVCR110 keeps the rand seed per thread, so that draw does not move the main thread's
   sequence, see `CHR_POOL.md` section 5.3.

---

## 12. 2026-10-13, CHR field naming pass (PROGRESS 1.6.5)

`ffx_types.h` had 12 runs longer than 16 bytes of `m_i`/`m_f`/`_unk_` fields with no comment saying what
reads them (CHR 0x0C8, 0x378, 0x40E, 0x4EC, 0x508, 0x6A4, 0x804, 0x840, CHRDATA 0x018, 0x03C, 0x0F9, CHRPART
0x019). Now there are 2 such runs, CHR `_unk_CC` (0x0CC..0x10B) and `_unk_114` (0x114..0x153), and both say
why nothing reads them. Sizes and offsets are unchanged. The IDB was read only for this pass.

How the readers were found: every Hex-Rays function where the base is typed CHR, CHRDATA or CHRPART (a ctree
walk over all 49149 functions), every `[reg+disp]` and immediate in the chr module 0x81F000..0x846000 with
the base checked by hand, and the external hits in functions that call `FFX_Ch_*`, `FFX_Atel_GetActorChr` or
`FFX_Battle_GetActor`. The magic DLLs are not in the IDB and not covered. Two IDB typing traps showed up: the
first loop of `FFX_Ch_RelocateChrData 0x8347A0` walks `g_ffxChrDataTable` through a `CHR *`, and
`FFX_BtlAi_SetProperty` has battle unit records typed `CHR *`. Their "CHR" hits at 0x12C, 0x384, 0x3F0,
0x4B0, 0x51C and 0x008..0x00B are not CHR accesses.

Named, with the evidence (all [V] unless marked):

| struct, offset | was | now | evidence |
|---|---|---|---|
| CHR 0x0C8 | `_unk_C8` | `m_effectW` | copied with the effect colour by `FFX_Ch_CopyState` 0x8286F2, nothing else [I for the name] |
| CHR 0x10C | `m_i10C` | `m_rootMatrixOverride` | `FFX_Ch_BuildSkinMatrices` 0x832944 copies it into the root world matrix when there is no parent |
| CHR 0x110 | `m_f110` | `m_rootScaleDivisor` | 0x832AAD..0x832AD1 divide the CHR scale by it for the second root matrix (0x210) |
| CHR 0x15C..0x164 | `_unk_15C` | `m_tiltX`, `m_tiltXTarget`, `m_tiltXStep` | `sub_8357F0` 0x8357F0 (ATEL 0xA79ADD) sets them, BuildSkinMatrices 0x8329AD..0x8329F8 slews and applies an X rotation |
| CHR 0x17C | `_unk_17C` | `m_f17C` | blob shadow parameter, `sub_830CB0` writes 0.2, `sub_82E3E0` 0x82E527 reads it |
| CHR 0x188 | `_unk_188` | `m_lightOverrideMask` | `sub_836250` 0x836250 picks CHR light slot k over the default per bit k |
| CHR 0x360 | `m_anim360` | `m_animBrightness` | debug label "P", `FFX_Ch_StepAll` 0x82F5C0 multiplies the material RGB by it while it moves |
| CHR 0x384, 0x3C4 | `_unk_384` | `m_lightColor[16]`, `m_lightDir[16]` | `FFX_Ch_AccumulateLights` 0x8305A0 resets and sums 4 float4 each |
| CHR 0x40C..0x4D3 | `_unk_40C`, `_unk_40E`, `_unk_4D0` | `m_lookAtEnabled`, `m_lookAtKindApplied`, `m_lookAtWeightTarget`, `m_lookAtWeight`, `m_lookAtYaw`, `m_lookAtBlendFrames`, `m_lookAtYawMin`, `m_lookAtYawMax`, `m_lookAtRestMatrix`, `m_lookAtMatrix`, `m_lookAtPrevQuat`, `m_lookAtSwitchBlend`, `m_lookAtSide` | `FFX_Ch_UpdateLookAt` 0x8348D0, `sub_834F60`, `sub_834E60` (limits 20 and 160 degrees), `sub_835020` (6.0 frames) |
| CHR 0x4F0 | `m_f4F0` | `m_height` | `maybe_FFX_Btl_GetUnitHeight` 0x7950F0, and the unit centre is pos.y - height / 2 (0x794C20) |
| CHR 0x4F4 | `m_f4F4` | `m_rootPointLift` | subtracted from bone 0 y for `m_rootPoint` in `FFX_Ch_UpdateBonePositions` 0x8350F0 |
| CHR 0x4FC, 0x500, 0x510, 0x514, 0x51C, 0x520 | | `m_waterFloatDepth`, `m_pushWeight`, `m_bodyHeight`, `m_feetDepth`, `m_collisionHeight`, `m_collisionTopY` | the section 6 proposals, applied |
| CHR 0x6A4, 0x6B4 | `m_vec6A4`, `m_vec6B4` | `m_headPoint`, `m_rootPoint` | section 6 proposals. `m_rootPoint` is also read by ATEL `Ch_056` 0xA7A2C9 |
| CHR 0x6C4 | `_unk_6C4` | `m_lightPoint[4]` | written by the bone pass, the origin `FFX_Ch_AccumulateLights` measures from |
| CHR 0x6DC..0x6F4 | `m_p6DC`, `_unk_6E0` | `m_texAnimBlock`, `m_texAnimSlots`, `m_texListA`, `m_texListACount`, `m_texListATotal`, `m_texListB`, `m_texListBCount`, `m_texListBTotal`, `m_texAnimSlotCount` | `sub_83CDD0` 0x83CDD0 lays them out from `CHRDATA.m_texData` |
| CHR 0x6FC, 0x700 | `m_i6FC`, `m_i700` | `m_texAnimProgram`, `m_texAnimHandle` | `sub_83D0A0` 0x83D0A0, `movie_str__f77C950` 0x77C950, `FFX_Ch_SetTexAnimPattern` 0x83D040 |
| CHR 0x704, 0x708 | `m_i704`, `m_p708` | `m_texListACopy`, `m_texListACopyActive` | `sub_83C710` copies, `sub_83C940` switches, `sub_83C810` frees |
| CHR 0x7EC..0x7EE | `_unk_7EC` | `m_anmJointCopyMode`, `m_motionMissing`, `m_b7EE` | `FFX_Mot_EvalAnmClip` 0x8388DE, `FFX_Ch_SetMotionKey` 0x837C03 and 0x837CDD |
| CHR 0x804..0x81C | `_unk_804`, `m_f814`, `m_f818`, `_unk_81C` | `m_windYaw`, `m_windPitch`, `m_windStrength`, `m_swaySettleRequest`, `m_swaySettleIters`, `m_swayGainY`, `m_swayGainXZ`, `m_swayBattleClamp` | `FFX_Ch_StepSecondaryPhysics` 0x822DE0, `sub_824C90`, `sub_824920` [I for "gain"] |
| CHR 0x840..0x863 | `_unk_840` | `m_cdfBoundsMin`, `m_cdfBoundsMax`, `m_cdfBoundsInvSize` | `sub_826340` 0x826340 from `FFX_ChCache_GetCdf`, no reader |
| CHRDATA 0x018..0x068 | `_unk_18`, `m_f1C`, `m_f28`, `m_f2C`, `_unk_3C` | `m_texDataAux`, `m_collisionRadiusScale`, `m_height`, `m_rootPointLift`, `m_f3C`, `m_lookAtMirror`, `m_headSwayAxis`, `m_headSwayFlip`, `m_headRestRot`, `m_paramRevision`, `m_specIntensity`, `m_specPower`, `m_p68` and placeholders with comments | `FFX_Ch_InitChrDataFromBlob` 0x825EE0 (the section 9 parameter block), `FFX_Ch_UpdateBlink`, `FFX_Ch_UpdateLookAt`, `FFX_Ch_StepAll` [I for the two spec names] |
| CHRDATA 0x0FC..0x11C | `_unk_F9` | `m_anmFrames`, `m_anmFile`, `m_anmName`, `m_sysMotTbl`, `m_relationTbl`, `m_motRelCount`, `m_motRelTabs`, `m_p11C` | `FFX_Ch_LoadAnmFile` 0x8283C0, `sub_837FA0`, `FFX_Ch_SetMotionKey` 0x837B05, `FFX_Ch_DataFree` 0x8258D0 |
| CHRPART 0x01C, 0x034 | `_unk_19`, `_unk_34` | `m_swayPrevPos[3]`, `m_boneBufBFilled` | `FFX_Ch_StepSecondaryPhysics`, base `m_parts` stride 56 |

Corrections to the old comments:

- `m_p708` is a byte flag (`movsx` at 0x82716B), not a heap block, and `m_i700` is a slot index in the
  texture animation table at 0x2310000, not a pointer. `FFX_Ch_MoveChrMemory` would relocate the slot's data
  through `sub_77E9F0` / `sub_77EA10`, not the field (it never runs in this build, `MEMORY_ARENAS.md` 4.3).
- `m_p6DC` is the texture animation block, not cloth. `CHR_POOL.md` proposes `FFX_Ch_ReleaseClothBlock` for
  `sub_83C810`, which frees `m_texListACopy` (0x704). "Cloth" is wrong there too, a better name is
  `FFX_Ch_FreeTexListACopy`.
- `m_blinkState` (0x4D4) is the idle head sway on bone 13, as the IDB comment on `FFX_Ch_UpdateBlink` says.
- `sub_82E3E0` never reads its second argument, so the 64 bytes at 0x114 that both shadow callers pass are
  dead in this build.

Still unnamed, each with a comment in `ffx_types.h`: CHR `_unk_CC` (64 bytes) and `_unk_114` (64 bytes),
`_unk_404`, `_unk_40F`, `_unk_508`, `_unk_518`, `_unk_6D4`, `_unk_6F8`, `_unk_864`, `m_f4EC`, `m_f4F8`,
`m_f17C`, `m_i408`, `m_anim378`, and in CHRDATA `m_f20`, `m_f24`, `m_f30`, `m_f3C`, `m_f50`, `m_f60`,
`m_b64`, `m_b65`, `m_p68`, `m_p11C`. Most have a writer and no reader in the IDB, so only the magic DLLs or
nothing at all can give them meaning.
