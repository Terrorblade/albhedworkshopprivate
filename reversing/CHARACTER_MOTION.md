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
