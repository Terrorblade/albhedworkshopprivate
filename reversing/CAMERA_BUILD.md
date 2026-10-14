# Camera build: FFX_Came_ActivateScreen, the view and projection matrices, and shake

Project Zanarkand PROGRESS item 6.3.1 (camera view and projection from the active slot, shake included).
Written 2026-10-13 against the IDB of `FFX.exe` (32-bit, VA base `0x400000`, so RVA = VA - `0x400000`).
IDA was read only for this pass. Every name below that is not already in the IDB is a proposal, collected
in section 11. The port lives in `projectzanarkand/src/engine/came/` (namespace `zan::came`) with tests in
`tests/engine/CameraTest.cpp`.

Marks: **[V]** verified in Hex-Rays and, for anything float or branch sensitive, in the disassembly.
**[I]** inferred.

Related docs: `CHARACTER_MOTION.md` (player control reads the camera yaw), `MAIN_LOOP.md` (where the
calls sit), `RENDER_PASSES.md` (what the gfx side does with the pushed fov and matrices),
`SCREENS_AND_FADES.md` (the fade layer, which is not this).

---

## 0. The short version

- The camera is simulation. `FFX_Came_StepAll 0x7BE110` moves three camera slots once per sub-step, and
  player control reads the yaw of the active slot through `FFX_Came_GetYaw 0x7BCF20`. Camera shake
  draws from the game RNG, `FFX_Rand_Stream 0x7988F0` stream **9**, so a shake that runs on one peer and
  not the other moves every later stream 9 draw. [V]
- `FFX_Came_ActivateScreen 0x7BC110` picks one of three screens, steps that screen's WIN and DRW rects and
  its two screen shake channels, and writes `g_ffxCameProjMatrix 0x2311480` (a PS2 GS style projection
  from the screen) and `g_ffxCameViewMatrix 0x2311440` (a VU0 style look-at from the slot the screen
  shows). [V]
- ActivateScreen runs about three times per main step, not per sub-step, and its screen shake steps on
  every call. So screen shake consumes stream 9 at the activate cadence, not the sub-step cadence. [V]
- Everything is x87 at PC_53 through the PS2 VU0 scratch block at `0xC8F7D0..0xC8F85C`. Every scratch
  write is a float store, so the port rounds to float at each one. Section 9 has the operation order.
- The PC renderer does not use the camera's projection for drawing. It hands the fov and aspect to the
  main `PCameraPerspective` and that builds its own right handed projection (section 7). The view matrix
  and the fov are what reach the screen.

---

## 1. Functions

| VA | Name | Mark | What |
|---|---|---|---|
| `0x820AE0` | `FFX_MainStep` | [V] | calls StepAll at `0x82104D` per sub-step and at `0x8210C2` after `FFX_PlayerCam_Step` (gated by `g_ffxCameraAlwaysStep 0x12FBBB0`), calls ActivateScreen at `0x821189`, in the two pass loop at `0x82135E`, and at `0x821380` to restore the main screen |
| `0x7BE110` | `FFX_Came_StepAll` | [V] | per slot mode step, tweens, then the slot shake (section 5) unless overriding |
| `0x7BC110` | `FFX_Came_ActivateScreen` | [V] | section 3 |
| `0x7BD5D0` | `FFX_Came_Init` | [V] | defaults, up vector, screens |
| `0x7BC7F0` | `FFX_Came_SetScreenProjParams` | [V] | z scale and offset from zMin, zMax, near, far |
| `0x7C3D60` | `FFX_Came_BuildProjFromScreen` | [V] | section 4 |
| `0x7C1BE0` | `maybe_FFX_Came_BuildLookAtMatrix` | [V] | section 6, propose dropping `maybe_` |
| `0x7C50F0` | `FFX_Came_StepShake` | [V] | one shake channel, section 5 |
| `0x7C1480` | `FFX_Came_ApplyShake` | [V] | xyz add, stores a stale w (section 5) |
| `0x7C4990` | `FFX_Came_VecToPolar` | [V] | theta, phi, length from two points |
| `0x7BCF20` | `FFX_Came_GetYaw` | [V] | VecToPolar theta of the active slot's committed points |
| `0x7BC980` | `FFX_Came_PushViewToRenderCtx` | [V] | fov and aspect to the gfx context (section 7) |
| `0x7BDA80` | `FFX_Came_StepScreenRectTween` | [V] | linear, trapezoid, cosine tween of one float |
| `0x7C04C0` | `sub_7C04C0` | [V] | moves an angle by whole turns to within pi of a start |
| `0x7C06D0` | `FFX_Came_SetMatrixOverride` | [V] | copies a caller's proj and view straight in and sets the override flag |
| `0x7C4D40` | `sub_7C4D40` | [V] | scale about a pivot, gated by slot `+3210` |
| `0x7C56F0` | `sub_7C56F0` | [V] | screen shake setter (screen, mask, type, rate, duration, amp, range) |
| `0x7C57B0` | `sub_7C57B0` | [V] | screen spring shake (type 9) setter, draws stream 9 itself |
| `0x7C56C0` | `sub_7C56C0` | [V] | clears both screen shake channels (ATEL Came 97) |
| `0x7C5680` | `sub_7C5680` | [V] | whether any screen shake channel is live (ATEL Came 100) |
| `0x7BB840` | `sub_7BB840` | [V] | ATEL Came 94, 104, 107, 110, 113, 116 -> `sub_7C56F0` |
| `0x7988F0` | `FFX_Rand_Stream` | [V] | the game RNG, section 5 |
| `0x716570` | `FFX_Level_HashName` | [V] | hash of `/map/<group>/<name>/` |
| `0x641DA0` | `FFX_Level_GetNameHash` | [V] | the current level's hash, `g_ffxGfxCtx + 0xFAF4` |
| `0x62FCE0` | `sub_62FCE0` | [V] | 3 component dot, magic host API 394 |
| `0x75E870` | `sub_75E870` | [V] | VU0 vsub.xy with status flags |
| `0x42EF00` | `sub_42EF00` | [V] | resets the VU0 vf0 constants `flt_C0A004..C0A00C` to 0 |
| `0x42EF30`, `0x42EF20` | `sub_42EF30`, `sub_42EF20` | [V] | set VU0 status bits in `dword_C8FAC4`, nothing reads them |
| `0x642D60` | `FFX_Gfx_SetProjPixelOffset` | [V] | the clip space pixel offset |
| `0x642D40` | `FFX_Gfx_SetScreenOrigin` | [V] | the screen shake as an origin |
| `0x6395E0` | `FFX_Gfx_SetCameraFovPtr` | [V] | |
| `0x6395C0` | `FFX_Gfx_SetCameraAspectPtr` | [V] | |
| `0x66CDA0` | `maybe_FFX_Gfx_SetCameraFov` | [V] | the per level fov hacks (section 7) |
| `0x66CBE0` | `maybe_FFX_Gfx_SetCameraAspect` | [V] | the HD aspect (section 7) |
| `0x456B80` | `sub_456B80` | [V] | right handed perspective from fov, aspect, near, far |
| `0x456D10` | `PCameraPerspective` vtable slot 1 | [V] | rebuilds via `sub_456B80` from +0x154, +0x150, +0xB8, +0xBC |
| `0x456370` | `sub_456370` | [V] | PCameraPerspective constructor, fov 0.78539819, aspect 1 |
| `0x4530A0` | `sub_4530A0` | [V] | PCamera base, near 0.1, far 10000 |
| `0x6519F0` | `FFX_Gfx_FindGlobalCamera2` | [V] | sets the gfx+568 and gfx+1620 cameras to near 1, far 65536 |
| `0x645F00` | `sub_645F00` | [V] | sets the main camera near (the magic 540 latch) |
| `0x638E70` | `sub_638E70` | [I] | reads the main camera near |
| `0x668930` | `FFX_Frame_RenderScene` | [V] | at `0x668C23` borrows near from gfx+0x7E0 while gfx+0x93D is set, then 1.0 |
| `0x9DA430` | `MagicFile__load` | [V] | snapshots both camera matrices and the fov |
| `0x9DA700` | `sub_9DA700` | [V] | true when a magic effect freezes the camera |
| `0x9DA750` | `MagicFile__restoreCameraSnapshot` | [V] | copies the snapshot back over both matrices |
| `0x9DA780` | `sub_9DA780` | [V] | sets the fov from `flt_22FB600` |
| `0x787F20` | `FFX_Btl_BeginMagicEffect` | [V] | writes `dword_C421E8` |
| `0x788C60` | `maybe_FFX_Magic_BeginGlobalEffect` | [V] | writes `dword_C421E4` |
| `0x82CE20` | `sub_82CE20` | [V] | `dword_1300758[a] = b` plus a bit in `dword_1300768`, magic host API 371 |
| `0x82CB60` | `sub_82CB60` | [V] | `dword_130076C = a & 1`, magic host API 707 |
| `0x83F2E0` | `FFX_PlayerCam_Step` | [V] | the player follow camera, before the second StepAll |

---

## 2. Globals and layouts

| VA | RVA | Name | Mark | What |
|---|---|---|---|---|
| `0x2311440` | `0x1F11440` | `g_ffxCameViewMatrix` | [V] | 16 floats, row major, row vectors |
| `0x2311480` | `0x1F11480` | `g_ffxCameProjMatrix` | [V] | 16 floats |
| `0x2311430` | `0x1F11430` | `g_ffxCameActiveSlot` | [V] | pointer to the slot the last activate bound |
| `0x23114C0` | `0x1F114C0` | `g_ffxCameActiveScreen` | [V] | pointer to the last activated screen |
| `0x23114C4` | `0x1F114C4` | `g_ffxCameOverrideFlag` | [V] | set by SetMatrixOverride |
| `0x231142C` | `0x1F1142C` | `g_ffxCameDirectLookAtFlag` | [V] | |
| `0x1136FC8` | `0xD36FC8` | `g_ffxCameDirectEyePos` | [V] | |
| `0x1136FD8` | `0xD36FD8` | `g_ffxCameDirectAtPos` | [V] | |
| `0x1136FE8` | `0xD36FE8` | `g_ffxCameDirectUpPos` | [V] | |
| `0x1136FC4` | `0xD36FC4` | `flt_1136FC4` | [V] | the direct roll |
| `0x1136FB8`, `0x1136FBC` | `0xD36FB8` | `flt_1136FB8`, `flt_1136FBC` | [V] | pixel offset bias x, y |
| `0x1136FC0` | `0xD36FC0` | `dword_1136FC0` | [V] | debug fov mode, only an orphan debug page writes it, 0 in retail |
| `0x1137000` | `0xD37000` | `g_ffxCameScreens` | [V] | 3 x 448 bytes |
| `0x1137540` | `0xD37540` | `g_ffxCameSlots` | [V] | 3 x 3488 (0xDA0) bytes |
| `0xC421E8` | `0x8421E8` | `dword_C421E8` | [V] | pending battle magic effect id, -1 for none |
| `0xC421E4` | `0x8421E4` | `dword_C421E4` | [V] | pending global effect id, -1 for none |
| `0xC44240`, `0xC44244` | `0x844240` | `dword_C44240`, `dword_C44244` | [V] | 512 and 416, the PS2 frame |
| `0x22FB660` | `0x1EFB660` | `g_ffxMagicSavedCameViewMatrix` | [V] | |
| `0x22FB620` | `0x1EFB620` | `g_ffxMagicSavedCameProjMatrix` | [V] | |
| `0x22FB600` | `0x1EFB600` | `flt_22FB600` | [V] | the saved fov |
| `0x1940ABC` | `0x1540ABC` | `dword_1940ABC` | [V] | one of the freeze conditions of `sub_9DA700` |
| `0xCCB9C0` | `0x8CB9C0` | `byte_CCB9C0` | [V] | HD aspect flag, set by the map loader |
| `0xC44238` | `0x844238` | `byte_C44238` | [V] | the magic 540 / scene 208 near latch, initial 1 |
| `0xCCB9D8` | `0x8CB9D8` | `g_ffxGfxCtx` | [V] | level hash at +0xFAF4, fov hack gate at +0x93C |
| `0x23CBBF0` | `0x1FCBBF0` | `g_ffxMainStepCounter` | [V] | for tagging a capture |
| `0x12FBBB0` | `0xEFBBB0` | `g_ffxCameraAlwaysStep` | [V] | gates the second StepAll |
| `0xC42208` | `0x842208` | RNG multiplier table | [V] | game data, not copied anywhere |
| `0xC42318` | `0x842318` | RNG xor table | [V] | game data |
| `0x1135EE0` | `0xD35EE0` | RNG state table | [V] | stream 9 is at `0x1135F04` |

### 2.1 Camera slot, 3488 bytes [V]

Two 1564 byte sub-blocks, REF (the look-at point) at `S+0x30` and CAM (the eye) at `S+0x64C`. Inside a
sub-block `B`:

| Offset | What |
|---|---|
| `B+2`, `B+4`, `B+5` | mode bytes (B+2 is the mode the shake gate reads) |
| `B+16` | live position (committed + shake), what the view is built from |
| `B+32` | committed position |
| `B+64` | velocity limit |
| `B+80` | pending position |
| `B+96..104` | theta, phi, length |
| `B+112..136` | tween rates |
| `B+140` | pointer to the other sub-block's position |
| `B+144` | shake out, 3 floats |
| `B+160` | shake phase, 3 floats |
| `B+176` | shake rate, 3 floats |
| `B+192` | four 304 byte priority records |
| `B+1408` | three tween records |
| `B+1456` | three 36 byte shake records |

So REF live is `S+0x40`, REF committed `S+0x50`, CAM live `S+0x65C`, CAM committed `S+0x66C`.

| Slot offset | What |
|---|---|
| `S+0x00` | up vector. FFX_Came_Init writes -1.0 to `S+4`, so up is (0, -1, 0, 0) and +Y is down |
| `S+0xC90` | live roll |
| `S+0xC94` | roll target |
| `S+0xC9C` | ScrDpt, copied to screen +0x1C by StepAll |
| `S+3209` | per StepAll dirty byte |
| `S+3210` | scale about pivot flag (`sub_7C4D40`), written at `0x7BEE9A` |
| `S+3212` | last priority |
| `S+0xCA8..0xCB4` | lighting ids, sent through `sub_82CE20` and `sub_82CB60` on activate |

### 2.2 Screen, 448 bytes [V]

| Offset | What |
|---|---|
| `+0x00..0x0C` | zMin, zMax, near, far (SetScreenProjParams inputs) |
| `+0x10`, `+0x14` | aspectX 0.8, aspectY 0.93333334 |
| `+0x18` | proj m[14] |
| `+0x1C` | ScrDpt, 512 by default |
| `+0x20`, `+0x24` | centre, 2048 |
| `+0x28` | proj m[10] |
| `+0x30`, `+0x34` | screen shake out, the projection centre offset and the screen origin |
| `+0x38`, `+0x3C` | width 512, height 416 |
| `+0x40` | WIN rect (x, y, w, h) |
| `+0x50` | DRW rect |
| `+0x60` | DRW offset, (DRW size - frame) / 2, written by the WIN pass |
| `+0x70` | WIN offset, (WIN size - frame) / 2, written by the DRW pass, read by the projection |
| `+0x80`, `+0x90` | WIN and DRW targets |
| `+0xA0`, `+0xB0` | WIN and DRW tween starts |
| `+0xC0` | enabled byte, only screen 0 at init |
| `+0xC1` | slot index |
| `+0xC2`, `+0xC3` | WIN mode, DRW mode (1 take size, 2 copy rect, 3 centre) |
| `+0xC4` | kind |
| `+0xD0..0xD8` | lighting ids |
| `+0xDC` | eight 16 byte rect tweens, four WIN then four DRW |
| `+0x15C` | shake phase, 2 floats |
| `+0x164` | shake rate, 2 floats |
| `+0x16C` | two 36 byte shake records |

### 2.3 Shake record, 36 bytes [V]

`+0` type, `+1` jitter range, `+2` amplitude (u16), `+4` counter (s16), `+6` duration (s16), `+8` held
value (types 5, 6), `+0xC` spring v0 copy, `+0x10` spring position, `+0x14` spring velocity, `+0x18`
spring acceleration, `+0x1C` spring a0 copy, `+0x20` spring iterations, `+0x21` spring envelope, `+0x22`
spring random divisor. The port's `ShakeRecord` in `Shake.h` mirrors it field for field.

---

## 3. FFX_Came_ActivateScreen 0x7BC110 [V]

1. Index >= 3 returns 1 (the end of the screen walk in FFX_MainStep). A disabled screen returns -1.
2. `g_ffxCameActiveScreen` = the screen, `g_ffxCameActiveSlot` = `g_ffxCameSlots + slot * 3488`.
3. `0x7BC1DE` WIN pass then DRW pass, the same body at `0x7BC1E5`:
   - rect = target
   - the other rect's offset = fl((other.w - fl(512)) * 0.5), fl((other.h - fl(416)) * 0.5). The WIN pass
     writes the DRW offset from the DRW rect, the DRW pass writes the WIN offset from the WIN rect it just
     made.
   - mode 1: rect.w, h = other.w, h. mode 2: both offsets 0, rect = other.
   - step the four tweens with their starts
   - mode 3: rect.x = fl(x + (512 - w) * 0.5), rect.y = fl((416 - h) * 0.5 + y)
4. `0x7BC345` the two screen shake channels: StepShake then phase = fl(rate + phase), only for a channel
   whose type is set.
5. `0x7BC381` WIN, DRW offset, DRW and WIN offset truncated through `__ftol2_sse`, fild, fstp.
6. Then one of three paths:
   - **Direct look-at** (`g_ffxCameDirectLookAtFlag`), `0x7BC3D9`: proj from the screen, view from the
     direct eye, at, up and roll. Nothing pushed to the gfx side.
   - **Override** (`g_ffxCameOverrideFlag`), `0x7BC615`: neither matrix is touched (SetMatrixOverride
     wrote both). Only `FFX_Gfx_SetProjPixelOffset(fl(2 * biasX * 0.001953125), fl(biasY * 2 / 416))`.
   - **Normal**: proj from the screen, then the view gate at `0x7BC493`: on levels 5381 (ssbt01), 2082
     (ssbt00) and 6275 (hiku15) the view is rebuilt only while `dword_C421E8 < 0`, `dword_C421E4 < 0`
     and both `Vec3Dot(eye, eye)` and `Vec3Dot(at, at)` lie strictly inside (fl(0.01), 1e9). A NaN
     fails. Every other level always rebuilds. The view is BuildLookAt(CAM live `S+0x65C`, REF live
     `S+0x40`, up `S+0`, roll `S+0xC90`). Then the pixel offset at `0x7BC589`:
     - y = fl(((((winOffset.y - shake.y) - drw.y) + biasY) * 2) / 416)
     - x = fl((((winOffset.x + shake.x) - drw.x) + biasX) * 2 * 0.001953125)
     
     then `FFX_Gfx_SetScreenOrigin(shake.x, shake.y)`, then the lighting ids through `sub_82CE20` and
     `sub_82CB60`, then if `sub_9DA700()` the magic snapshot replaces both matrices
     (`MagicFile__restoreCameraSnapshot`), then `FFX_Came_PushViewToRenderCtx`, then `sub_9DA780` puts
     the saved fov back when frozen.
7. Not modelled by the port, all render side: the magic 540 near plane in scene 208, the magic 152 and
   601 second camera, the 256 wide split screen, the lighting ids.

---

## 4. The projection, FFX_Came_BuildProjFromScreen 0x7C3D60 [V]

Through the VU0 scratch, in this order:

```
cx = fl(shake.x + centreX)      cy = fl(shake.y + centreY)
cx = fl(winOffset.x + cx)       cy = fl(winOffset.y + cy)
cx = fl(cx - drw.x)             cy = fl(cy - drw.y)          (sub_75E870, vsub.xy)
sx = fl(scrDpt * aspectX)       sy = fl(aspectY * scrDpt)
sx = fl(win.w * sx)             sy = fl(win.h * sy)
sx = fl(fl(sx / drw.w) + 0.0)   sy = fl(0.0 + fl(sy / drw.h))  (+ vf0, turns -0 into +0)

| sx  0   0    0 |
| 0   sy  0    0 |
| cx  cy  m10  1 |
| 0   0   m14  0 |
```

SetScreenProjParams 0x7BC7F0: m14 = fl((zMax - zMin) * (near * far) / (far - near)), m10 =
fl((zMin * far - zMax * near) / (far - near)). Init passes 512, 0.8, 0.93333334, 2048, 2048, 1, 16770000,
1, 65536, giving sx 409.6 (`0x43CCCCCD`), sy 477.87 (`0x43EEEEEF`), m10 `0xC37EE4CF`, m14 `0x4B7FE4CF`.

---

## 5. Shake and the RNG [V]

`FFX_Rand_Stream(i)`: s = mul[i] * state[i], s ^= xor[i], state[i] = (s sar 16) + (s shl 16), return
state[i] & 0x7FFFFFFF. Every camera shake draw is stream 9 (push 9 at `0x7C5110`, `0x7C533C`,
`0x7C53F4`). The tables are game data and are read from the exe at runtime by whoever needs them.

`FFX_Came_StepShake 0x7C50F0`, per channel:

- type 0 returns. Any other type with a jitter range draws first: j = (range >> 1) - draw % range.
- wave * amplitude * fl(j + 32) * 0.03125 is the amplitude chain, one fstp at the end.
- types 1, 2: wave fl(sin t). Types 3, 4: (fl(sin t) + fl(cos(t + 0.3))) * 0.5, the cos argument the
  double sum. Types 5, 6, 8: the same with 0.1.
- types 1 to 4: return if duration is 0. out = fl(A * env / duration), env = counter (odd types) or
  fl(duration - counter) (even types).
- types 5, 6: a second draw, & 0xF, **before** the duration test. If the counter is odd or the nibble is
  0, A = fl(0.5 * held). out = fl(A * env / duration), held = A.
- types 1 to 6 tail at `0x7C54B8`: 16 bit counter decrement. Negative -> type 0, out 0.
- types 7, 8: no envelope. End only when the counter goes negative and duration > 0.
- type 9 spring: iterations x (pos = fl(v + pos), a negated when pos > 0, v = fl(a + v)). Envelope 1
  fl(counter * pos / duration), 2 fl((duration - counter) * pos / duration). Ends like 7 and 8.
- unknown types: nothing after the jitter draw.

Draw order in a step:

1. StepAll (per sub-step, only while not overriding and the sub-block mode byte is non zero): REF x, y,
   z then CAM x, y, z, each StepShake then phase += rate, then `FFX_Came_ApplyShake(B+16, B+32, B+144)`
   so live = committed + shake. The w ApplyShake stores is VU0 scratch `0xC8F7FC`, stale from the
   previous scratch user. Nobody reads the w of a live position as far as traced [I].
2. Every ActivateScreen (about three per step): screen shake x then y.
3. The setters `sub_7C57B0` (spring, ATEL Came 137 and the shake editor) draw stream 9 when called.

The setter for the camera sub-block shake records was not found. Only the screen channel setters are
known.

---

## 6. The view, maybe_FFX_Came_BuildLookAtMatrix 0x7C1BE0 [V]

Row major, row vectors (v' = v * M). In operand order:

1. If eye and at are the same four dwords (w included), the result is zero with m[15] = 1.
2. c = fl(cos roll), s = fl(sin roll), ns = -s.
3. f = normalise(fl(at - eye)).
4. side = normalise(CrossTerm(Uz, fy, Uy, fz), CrossTerm(Ux, fz, Uz, fx), CrossTerm(Uy, fx, Ux, fy))
   where CrossTerm(a1, b2, a2, b1) = fl(fl(a1 * b2) - a2 * b1).
5. u = normalise(CrossTerm(fy, sz, sy, fz), CrossTerm(sx, fz, sz, fx), CrossTerm(fx, sy, fy, sx)).
6. Normalise: the three squares stored, sum = fl(yy + xx), sum = fl(zz + sum), fabs, scale =
   1 / fl(sqrt(sum)) unless the sum is exactly 0 (then everything collapses to signed zeros). x and y
   scale as v * scale, z as scale * v.
7. The transpose through the VU0 adds a +0.0 to ux, fx, sy, fy, sz and uz, which turns -0 into +0 in just
   those slots.
8. p = MulAdd chains of eye against each axis: px = fl(fl(fl(ex * sx) + ey * sy) + ez * sz), and the same
   for (ux, uy, uz) and (fx, fy, fz). t = fl(0 - p).
9. The roll: m0 = fl(fl(c * sx) + ns * ux), m1 = fl(fl(sx * s) + c * ux), m2 = fx, and the same for the
   y and z rows. m12 = fl(fl(tx * c) + ty * ns), m13 = fl(fl(tx * s) + ty * c), m14 = tz, m15 = 1, the w
   column +0.

Eye (0, 0, -10) at the origin with up (0, -1, 0) gives the identity with m14 = 10.

---

## 7. The fov, aspect and the PC projection [V]

`FFX_Came_PushViewToRenderCtx 0x7BC980`:

- w = fl(512 / (aspectX * 2)), h = fl(448 / (2 * aspectY)), ratio = h / scrDpt kept as a double (fst
  qword)
- fov = fl(fl(atan(ratio)) * 2), aspect = fl(w / h). Defaults: 320, 240, fov `0x3F606DA6` (0.87667),
  aspect 4:3.
- debug fov mode 2 (`dword_1136FC0`): fl(fl(atan(ratio * 0.75)) * 2). Never set in retail.

`maybe_FFX_Gfx_SetCameraFov 0x66CDA0`, gated by gfx+0x93C, replaces the fov on three levels: 3777
(bltz02) 0.471, 263 (genk13) 0.673, 3912 (sins00) 0.817. `maybe_FFX_Gfx_SetCameraAspect 0x66CBE0` with
`byte_CCB9C0` set stretches 4:3 to 16:9: fl(((a * 3) * 0.25) * 1.7777777910232544).

`PCameraPerspective` slot 1 `0x456D10` -> `sub_456B80(fov +0x154, aspect +0x150, near +0xB8, far +0xBC)`:

```
half = fl(1.5707963705062866 - fov * 0.5)
t = fl(tan(half))
inv = fl(1 / (near - far))
m14 = fl((near * far * inv) * 2)
m10 = fl((far + near) * inv)
m0 = fl(t / aspect), m5 = t, m11 = -1
```

Near and far: the constructor `sub_456370` defaults fov 0.78539819 and aspect 1, the base `sub_4530A0`
near 0.1 and far 10000. `FFX_Gfx_FindGlobalCamera2 0x6519F0` sets the gfx+568 and gfx+1620 cameras to
near 1, far 65536. `sub_645F00` sets the main camera's near (the magic 540 latch `byte_C44238`).
`FFX_Frame_RenderScene` at `0x668C23` borrows near from gfx+0x7E0 while gfx+0x93D is set, then puts 1.0
back. The runtime near and far of the main camera were not captured [I].

---

## 8. The override and the magic freeze [V]

- `FFX_Came_SetMatrixOverride 0x7C06D0` copies a proj and a view straight into the globals and sets
  `g_ffxCameOverrideFlag`. StepAll then forces the pending, committed and live positions of both
  sub-blocks to the override's ref and cam positions, with no shake. ActivateScreen only sets the bias
  pixel offset.
- `sub_9DA700` is true when `dword_1940ABC` is set, or the previous magic id is 165 or 307, or 338 while
  `sub_782970() == &unk_18B0046`. ActivateScreen then copies `g_ffxMagicSavedCameProjMatrix` and
  `g_ffxMagicSavedCameViewMatrix` (taken by `MagicFile__load 0x9DA430`) over both matrices after
  building them, and `sub_9DA780` restores the fov from `flt_22FB600` after the push.

---

## 9. Bit exactness notes [V]

- x87 at PC_53, so each operation is a double operation and each fstp dword one float rounding. The
  port does the arithmetic in double in the original's operand order and rounds at every store.
- Branch senses from the fucom flags: `test ah, 44h` then `jp` is taken on not equal or unordered.
  `test ah, 41h` then `jnz` on less, equal or unordered. `test ah, 5` then `jp` on greater or equal or
  unordered. The
  angle unwrap (`sub_7C04C0`) takes its subtract branch, fl((start - 2pi) + e), when e >= pi or NaN.
  Hex-Rays prints that sum in the other order. The disassembly is right.
- `_CIsin`, `_CIcos`, `_CIatan`, `_CItan` are MSVCR110's, which take the SSE2 libm path. The port uses
  the C++ library on the same double. They round to the same float except within a few double ulps of a
  float boundary. A recorded frame is the test for that.
- `__ftol2_sse` truncates and gives `0x80000000` for NaN and out of range.
- pi is the float pi widened, `dbl_B92288` 3.1415927410125732, 2pi `dbl_B39378`, pi/2 `dbl_B11338`.

---

## 10. Capturing a recorded frame

No recorded frame exists yet. To make one without changing the simulation, detour
`FFX_Came_ActivateScreen` at RVA `0x3BC110` and dump, tagged with `g_ffxMainStepCounter` (RVA
`0x1FCBBF0`) and the call index within the step:

On entry:

| What | RVA | Bytes |
|---|---|---|
| the screen index argument | | 4 |
| `g_ffxCameScreens` | `0xD37000` | 1344 |
| `g_ffxCameSlots` | `0xD37540` | 10464 |
| RNG state of stream 9 | `0xD35F04` | 4 |
| `g_ffxCameDirectLookAtFlag` | `0x1F1142C` | 4 |
| direct roll, bias, debug fov mode, eye, at, up | `0xD36FB8` | 64 (`0x1136FB8..0x1136FF7`) |
| `g_ffxCameOverrideFlag` | `0x1F114C4` | 4 |
| level hash, `[g_ffxGfxCtx 0x8CB9D8] + 0xFAF4` | | 4 |
| `dword_C421E4`, `dword_C421E8` | `0x8421E4` | 8 |
| `sub_9DA700()` result, or its inputs `dword_1940ABC` and the previous magic id | `0x5DA700` | 1 |
| 512, 416 frame | `0x844240` | 8 |

On exit: `g_ffxCameViewMatrix` (`0x1F11440`, 64), `g_ffxCameProjMatrix` (`0x1F11480`, 64),
`g_ffxCameActiveSlot` (`0x1F11430`), `g_ffxCameActiveScreen` (`0x1F114C0`), the stream 9 state, the
screens again, and the return value. Detouring `FFX_Came_StepAll` (RVA `0x3BE110`) with the slots and
the stream 9 state on entry and exit gives the slot shake the same way. In battle the slot modes may draw
stream 9 too, so a StepAll fixture should start with no shake and compare draws.

A fixture is then: feed the entry dump into `zan::came::ActivateScreen` with a `RandStream` that replays
the recorded stream 9 values (the before and after states bound the draws), and compare the exit
matrices bit for bit. Never check the dump of a real save in if it holds anything from the game's data
files. The camera state itself is runtime state, not game data.

---

## 11. Proposed IDB names (not applied)

| VA | Proposed | Why |
|---|---|---|
| `0x7C1BE0` | `FFX_Came_BuildLookAtMatrix` | confirmed, drop `maybe_` |
| `0x7C04C0` | `FFX_Came_UnwrapAngleNear` | moves an angle by whole turns into [start - pi, start + pi) |
| `0x62FCE0` | `FFX_Vec3_Dot` | |
| `0x456B80` | `PCameraPerspective_BuildProjection` | right handed perspective from fov, aspect, near, far |
| `0x456370` | `PCameraPerspective__ctor` | |
| `0x75E870` | `FFX_Vu0_SubXyWithFlags` | |
| `0x42EF00` | `FFX_Vu0_ResetVf0` | |
| `0x42EF30` | `FFX_Vu0_SetZeroFlag` | |
| `0x42EF20` | `FFX_Vu0_SetSignFlag` | |
| `0x7C4D40` | `FFX_Came_ScaleAboutPivot` | |
| `0x7C56F0` | `FFX_Came_SetScreenShake` | |
| `0x7C57B0` | `FFX_Came_SetScreenSpringShake` | |
| `0x7C56C0` | `FFX_Came_ClearScreenShake` | |
| `0x7C5680` | `FFX_Came_IsScreenShakeActive` | |
| `0x7BB840` | `FFX_AtelCmd_CameScreenShake` | |
| `0x9DA700` | `MagicFile__isCameraFrozen` | |
| `0x9DA780` | `MagicFile__restoreSavedFov` | |
| `0x82CE20` | `FFX_Ch_SetLightSetId` | tentative |
| `0x82CB60` | `FFX_Ch_SetLightDirty` | tentative |
| `0x645F00` | `FFX_Gfx_SetMainCameraNear` | |
| `0x638E70` | `FFX_Gfx_GetMainCameraNear` | tentative |
