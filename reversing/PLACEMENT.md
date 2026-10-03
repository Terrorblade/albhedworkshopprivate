# PLACEMENT.md

How to put a character at an arbitrary XYZ in FFX HD without breaking anything.

Everything below was read out of `FFX.exe` in IDA. Where I inferred rather than read, I say so
inline. Addresses are given as VA first, then RVA, where RVA = VA - 0x400000. The headers in this
project use RVAs.

Two things to keep in your head the whole way down:

* **+Y is DOWN.** A smaller Y is higher in the world. Half the comparisons in this document read
  backwards if you forget that.
* The walkmesh lives in its own fixed point space. World position times `g_ffxWalkmeshScale` gives
  walkmesh space, and the mesh vertices are signed 16-bit in that space.

---

## 1. The safe placement sequence

This is the whole recipe. Each step is justified in the sections below.

### If the character has an ATEL actor (party members do). Use this.

1. **Select ATEL context 0.** `g_ffxAtelCtx` VA `0x1326B28` / RVA `0xF26B28` must point at
   `g_ffxAtelCtxArray` VA `0x1325BA0` / RVA `0xF25BA0`. Do not trust the ambient value, several ATEL
   helpers read whatever is there.

2. **Resolve the actor.**
   ```
   int idx = FFX_Atel_FindActorByPartyChar(partyCharIndex);   // VA 0x86A470, RVA 0x46A470
   if (idx < 0) -> this character has no actor, use the CHR-only route below
   void *actor = (void *)FFX_Atel_GetActor(idx);              // VA 0x86A830, RVA 0x46A830
   ```
   `FFX_Atel_GetActor` takes a pool **INDEX**. `FFX_Atel_GetPlayerActorId` VA `0x86C1A0` returns an
   actor **ID** (`actor+0x2E`). The two are not interchangeable. `FindActorByPartyChar` returns an
   index, so that pairing is correct.

3. **Ask whether the destination XZ is on the walkmesh, before you commit.**
   ```
   float s = g_ffxWalkmeshScale;              // VA 0x1301A90, RVA 0xF01A90 (float)
   float q[3] = { x * s, y * s, z * s };
   int tri = FFX_Ch_WalkmeshFindTri(q);       // VA 0x83DE10, RVA 0x43DE10
   if (tri == -1) -> the XZ is off the mesh, DO NOT PLACE HERE
   ```
   `tri == -1` means and only means "no triangle contains this XZ". A wrong Y never produces -1.
   See section 4.

4. **Kill the motion carry-over, on the CHR.** `CHR *chr = *(CHR **)((char *)actor + 0x9C);`
   ```
   chr->m_speed   = 0.0f;   // CHR+0x154   THIS is the carry-over, not the velocity
   chr->m_velX    = 0.0f;   // CHR+0x4C
   chr->m_velY    = 0.0f;   // CHR+0x50
   chr->m_velZ    = 0.0f;   // CHR+0x54
   chr->m_vertVel = 0.0f;   // CHR+0x504
   ```
   Or go through the accessors: `FFX_Ch_SetMoveSpeed(chr, 0.0f)` VA `0x82B840` / RVA `0x42B840`.

5. **Cancel the actor's move command**, or the character walks back to wherever the script was
   sending it.
   ```
   int moveCmd = FFX_Atel_GetMoveCmd((int)actor);   // VA 0x86C0A0, RVA 0x46C0A0
   *(unsigned short *)(moveCmd + 2) = 0;
   ```
   A zero kind word there also makes `FFX_Atel_ApplyMoveToChr` force `m_speed = 0` for you every
   frame, which makes step 4's `m_speed` write stick.

6. **Place.** This is the one call that does the bookkeeping you cannot reach from the CHR.
   ```
   FFX_Atel_SetActorPos(actor, x, y - 0.1f, z, /*snapPrev=*/1);
   // VA 0x870B20, RVA 0x470B20
   // int __cdecl (void *actor, float x, float y, float z, int snapPrev)
   ```
   **`snapPrev = 1` is mandatory, not cosmetic.** With 0 you will fire every line trigger between
   the old position and the new one. See section 5.

   The `- 0.1f` on Y copies what the engine's own script placement does. `FFX_Atel_PushActorPosToChr`
   VA `0x866800` / RVA `0x466800` calls `FFX_Ch_SetPos(chr, x, y - 0.1f, z)`, lifting the character
   0.1 world units clear of the stated Y so it does not start inside the floor.

7. **Set the facing, both halves of it.**
   ```
   FFX_Ch_SetRotAndMoveDir(chr, yaw);   // VA 0x82B1B0, RVA 0x42B1B0
   ```
   Writing `m_rotY` alone is not enough, `flags1` bit `0x400` slews it back toward `m_moveDir`.
   See section 7.

8. **If the actor is script driven, write the facing into the actor record too.** Check
   `actor+0x34` bit `0x20`:
   * bit set, the actor is player controlled, the CHR is authoritative, you are done.
   * bit clear, the actor record is authoritative and will overwrite `m_speed`, `m_moveDir` and
     `m_rotY` on the next frame. Write `actorPos[8]` (desired heading) and `actorPos[13]` (rotY),
     where `actorPos` is `actor + 0x284` for actor kinds 5 and 6 and `actor + 0x558` otherwise.
     For a party character that is kind 1, so `actor + 0x558`, giving `actor+0x578` and
     `actor+0x58C`. Alternatively set bit `0x20` so the engine reads the CHR instead.

9. **Do all of the above before `FFX_Atel_StepOnce`** in the frame, i.e. before VA `0x82101A`
   inside `FFX_MainStep` VA `0x820AE0` / RVA `0x420AE0`. Then the actor pull sees the new position,
   the triggers step against an already collapsed segment, and `FFX_Ch_UpdateMotionAll` re-finds the
   triangle and refills the ground state in the same frame. See section 9 for the full frame order.

10. **Do not** write `m_walkmeshTri`, `m_groundHeight`, `m_groundNormal` or `m_groundAttrs`. Step 6
    already set `m_walkmeshTri = -1` and `FFX_Ch_WalkmeshMove` refills all four from the re-found
    triangle on the next step.

### If the character has no ATEL actor (a bare CHR you spawned)

Steps 3, 4 and 7 unchanged, then:

```
FFX_Ch_SetPos(chr, x, y - 0.1f, z);   // VA 0x82B480, RVA 0x42B480, also sets m_walkmeshTri = -1
FFX_Ch_MarkDirty(chr);                // VA 0x824650, RVA 0x424650
```

`FFX_Ch_MarkDirty` matters. Its second half, `FFX_Ch_ResetPartVertexBuffers` VA `0x824670` /
RVA `0x424670` (renamed from `sub_824670` in this pass), re-seeds the double buffered part vertex
arrays from the source mesh and zeroes the per-vertex delta, which is what stops the mesh smearing
from the old position. **Inferred from the loop shape, there is no string to confirm it.**

**The CHR-only route cannot avoid the trigger sweep if this CHR belongs to the bound player actor.**
`FFX_Atel_PullActorPosFromChr` VA `0x869E40` / RVA `0x469E40` runs every frame, shifts
`ctx+536..548` into `ctx+552..564` and writes the live CHR position into `ctx+536..548`, with no
`snapPrev` escape hatch. So a direct CHR write builds the giant trigger segment for you one frame
later. Use the actor route whenever an actor exists.

---

## 2. `g_ffxSuppressNextSetPos` is dead. Stop worrying about it.

`g_ffxSuppressNextSetPos` VA `0x12FFAD8` / RVA `0xEFFAD8`, one byte.

Complete writer list, this is all three xrefs on the global and there is no fourth:

| site | function | what it does |
|---|---|---|
| `0x82ACC0` | `FFX_Ch_SuppressNextSetPos` VA `0x82ACC0` / RVA `0x42ACC0` | `= 1` |
| `0x82B483` | `FFX_Ch_SetPos` | `cmp ..., 0`, the read |
| `0x82B48C` | `FFX_Ch_SetPos` | `= 0` on the swallow path |

And `FFX_Ch_SuppressNextSetPos` **has zero callers**. Zero code xrefs, zero data xrefs. I also
searched the entire image for the dword `0x0082ACC0` and got no hits, so it is not in
`g_ffxMagicHostApiTable` (`0xC64CE8..0xC65878`, 741 entries, `FFX_Ch_SetPos` is entry 33 at
`0xC64D6C`), not in any script syscall array and not in any vtable. A `magic_%04d.dll` cannot reach
it either.

The byte lives in the uninitialised tail of `.data` (`ida_bytes.is_loaded` returns false for it), so
it is 0 at process start.

**Answer: it cannot plausibly be set at map load, because nothing in the shipped binary can set it at
all.** Reading it and clearing it before a placement is free and breaks nothing. Treat it as a
defensive assert. If it is ever non-zero on your machine, something other than FFX.exe wrote it.

For completeness, what it does when armed: `FFX_Ch_SetPos` clears it and returns immediately,
writing no position, no `prevPos`, no `flags1` bit and no `m_walkmeshTri`. `FFX_Ch_SetPosXZ`
VA `0x82B440` / RVA `0x42B440` does **not** honour it.

---

## 3. `m_flags1` bit `0x02000000` has no reader

`m_flags1` is `CHR+0x194`. Writers of bit `0x02000000`:

| site | function |
|---|---|
| `0x82B46D` | `FFX_Ch_SetPosXZ` VA `0x82B440` |
| `0x82B4C5` | `FFX_Ch_SetPos` VA `0x82B480` |
| `0x832F41` | `FFX_Ch_UpdateMotionAll` VA `0x832E10`, when it generates velocity from a non-zero `m_speed` |

Readers: **none, anywhere in `.text`.** Checked two independent ways.

* `ida_search.find_imm` for the immediate `0x02000000` across `0x401000..0xB0C000` returns 43 sites.
  Exactly three of them touch `CHR+0x194` and all three are the `or` writes above.
* Byte pattern sweep for every reasonable encoding: `test dword [reg+194h], 2000000h`,
  `test byte [reg+197h], 2`, `bt dword [reg+194h], 19h`, all with disp8 and disp32 modrm forms. No
  hits. No shift-and-mask form either, I decompiled all 76 functions that reference displacement
  `0x194` or `0x197` and grepped for `>> 25`, `<< 25` and `0x2000000`.

What actually happens to the bit: `FFX_Ch_ClearFlags1StepBits` VA `0x832DB0` / RVA `0x432DB0`,
called from `FFX_MainStep` at `0x820FDF`, does `m_flags1 &= 0xF8FFFFFF` on every in-use CHR at the
top of every frame. That clears bits `0x01000000`, `0x02000000` and `0x04000000` together, a trio of
per-frame "this happened this frame" latches:

| bit | set by | reader |
|---|---|---|
| `0x01000000` | `FFX_Ch_UpdateMotionAll 0x83325B`, `FFX_Ch_SetMotionKey 0x837CC3` | yes, passed to `FFX_Mot_AdvanceFrame` |
| `0x02000000` | the three sites above | **none** |
| `0x04000000` | `FFX_Ch_AutoLocomotionAnim 0x835D0C` | **none** |

**Answer: it is not a "re-do something" flag in this build. It is a dead latch.** Setting a position
arms nothing through it, there is nothing to wait for and nothing to clear. The flag that genuinely
matters after a `SetPos` is `m_walkmeshTri = -1`, which the same function writes.

---

## 4. Off the walkmesh. The dangerous one.

`FFX_Ch_WalkmeshMove` VA `0x83E5F0` / RVA `0x43E5F0`. One caller,
`FFX_Ch_ResolveCollisionsAll` VA `0x83D3B0` / RVA `0x43D3B0`, and only when `m_flags1` bit `0x80` is
clear.

### What it does at the top, in order

1. `m_walkmeshPos[0..2]` (`CHR+0x3C..0x44`) `= pos * g_ffxWalkmeshScale`.
2. The query vector `v19/v20/v21` gets that same scaled XYZ.
3. If debug noclip is held **and** `flags1` bit `0x200` (the player), set the local `v3 = 1` and
   `m_walkmeshTri = -1`.
4. `if (m_walkmeshTri == -1) m_walkmeshTri = FFX_Ch_WalkmeshFindTri(&v19);` **with the real Y still
   in `v20`.**
5. `v19 += m_velX * scale`, `v21 += m_velZ * scale`, and only now is `v20` forced to `0.0`.

### The fling, confirmed

```c
if ( m_walkmeshTri == -1 )            // FindTri failed
{
  if ( v3 != 0 )                      // debug noclip only
  {
    chr->m_posX = chr->m_velX + chr->m_posX;
    chr->m_posZ = chr->m_velZ + chr->m_posZ;
  }
  else                                // the normal case
  {
    chr->m_posX = chr->m_velX * 10.0 + chr->m_posX;
    chr->m_posZ = 10.0 * chr->m_velZ + chr->m_posZ;
  }
}
```

**Yes, `velX * 10` and `velZ * 10`, and nothing else in the function runs.** The `10` is not a
deliberate boost, it is the missing divide: the on-mesh path does the move in walkmesh space and
divides by the scale on the way out, so a normal step is `pos += vel` while this one is
`pos += vel * 10`.

What else the `-1` branch skips:

* `m_groundAttrs` (`CHR+0x828`) not written, stale.
* `m_groundNormal` (`CHR+0x7C..0x84`) not written, stale.
* **`m_groundHeight` (`CHR+0x16C`) not written, stale.** This is the one that bites, see section 6.
* `m_triVert0/1/2` (`CHR+0x8C/0x9C/0xAC`) not written, stale.

### Does it leave `m_walkmeshTri` at -1?

**Yes.** It leaves it at -1 and never stores anything bad. The next substep calls `FindTri` again.
There is no "poisoned triangle index" state to clean up.

The only writer of a triangle index in this path is `FFX_Ch_WalkmeshTestEdgeCrossing`
VA `0x83D960` / RVA `0x43D960`, and it only ever writes `a1->m_walkmeshTri = a3` on the
point-is-inside path where it returns 0. Every return-1 path leaves the field alone. (A pre-existing
IDB comment claimed it writes -1. It does not. Corrected in the IDB.)

### Important corollary

**With `m_velX` and `m_velZ` both zero, being off the mesh is harmless for XZ.** Nothing moves.
Velocity is regenerated from `m_speed` and `m_moveDir` every substep by `FFX_Ch_UpdateMotionAll`, and
is zeroed again every frame by `FFX_Ch_ResolveCollisionsAll` at `0x83D4B8..0x83D4BE`. So the thing
that makes an off-mesh character fling is a non-zero `m_speed` (`CHR+0x154`), not leftover velocity.

The Y side is **not** harmless, because `m_groundHeight` stays stale. See section 6.

### The wall case, for contrast

If `FindTri` succeeded, the slide loop runs: up to 4 iterations of
`FFX_Ch_WalkmeshTestEdgeCrossing` + `FFX_Ch_WalkmeshSlideAlongEdge` VA `0x83E180` / RVA `0x43E180`,
and if it still has not resolved after 4 it **returns early**, writing no position and no ground
state at all. The character just does not move that substep. That is the safe behaviour, and it is
only reachable when the start XZ was already on the mesh.

### Is there a cheap way to ASK before committing?

Yes, one. `FFX_Ch_WalkmeshFindTri` VA `0x83DE10` / RVA `0x43DE10`.

```c
int __cdecl FFX_Ch_WalkmeshFindTri(float *xyz);   // xyz is WALKMESH space, 3 floats
```

Call it directly, exactly as `FFX_Ch_WalkmeshMove` does, with `{x*scale, y*scale, z*scale}`. Get the
scale from `g_ffxWalkmeshScale` VA `0x1301A90` / RVA `0xF01A90` or from
`FFX_Map_GetWalkmeshScale` VA `0x83E9A0` / RVA `0x43E9A0`.

It returns -1 when there is no walkmesh loaded (`g_ffxWalkmeshTris == 0`) or when no triangle
contains the XZ. It is a brute force linear scan over `g_ffxWalkmeshTriCount` triangles with no
spatial index, which is fine for a one-off placement and would not be fine per frame per character.

**Do not use `maybe_FFX_Map_WalkmeshFindTriWorld` VA `0x83EAE0`.** It is dead code with no callers
and it is broken: it divides the returned **triangle index** by the scale and returns that as a
float. Almost certainly a porting mistake where the height query was meant to go.

### How Y disambiguates stacked triangles

Straight from the loop body, for each triangle that passes the 2D edge test:

```
h = (int)FFX_Map_WalkmeshHeightAt(a1, tri);     // walkmesh units, truncated to int
if ((float)h < a1[1])      reject, keep scanning
else if (best == -1)       best = tri, bestH = h
else if (bestH > h)        best = tri, bestH = h
...
return best != -1 ? best : (lastXZHit != -1 ? lastXZHit : -1);
```

With +Y down, `h < queryY` means **the floor is above the query point**, and those are rejected.
Among the floors at or below the query Y it keeps the one with the **smallest** `h`, i.e. the highest
floor that is not above you. That is "the first floor under your feet".

So:

* **A Y that is too small (too high in the world) is safe.** You land on the highest floor below you,
  which is the one you wanted. Erring on the high side costs you nothing.
* **A Y that is too large (below the real floor) picks the wrong deck.** You get the floor under
  *that*, and if there is no floor below your Y at all the function falls through to `lastXZHit`, the
  last XZ-matching triangle in index order. That is effectively arbitrary.
* **The Y never causes a -1.** So `FindTri == -1` is a clean "this XZ is off the mesh" test and a bad
  Y is a separate failure mode, "right XZ, possibly wrong floor".

How wrong can a Y be? Exactly as wrong as the vertical gap to the next deck down, in the "too large"
direction, and unboundedly wrong in the "too small" direction. The `int` truncation of the height
adds 1 walkmesh unit of slop, which at the shipped scales (roughly 7.7 to 717, per the existing note
on `g_ffxWalkmeshScale`) is 0.0014 to 0.13 world units. Not a practical concern.

**Practical rule for the mod: send the host's exact Y, and if you want insurance, bias it upward
(subtract from Y) rather than downward.** The engine then recomputes the exact Y itself, see
section 6.

### The step height, since it comes up

`g_ffxWalkmeshStepHeight` VA `0xC4DEB0` / RVA `0x84DEB0` is `300.0` in `.data` and its setter
`FFX_Map_SetWalkmeshStepHeight` VA `0x83EB20` has no callers, so `300.0` is the live value.
`FFX_Ch_WalkmeshMove` scales it into `g_ffxWalkmeshStepHeightScaled` VA `0x1301AF4` at the top of
every step, and `FFX_Ch_WalkmeshTestEdgeCrossing` uses it as

```
if (heightAt(neighbour) < myWalkmeshY - stepScaled)  ->  treat the neighbour as a wall
```

With +Y down that rejects neighbours more than `stepHeight` **above** you, so it is a **climb**
limit, not a drop limit, and drops are not limited here at all. (Another pre-existing IDB comment had
this backwards. Corrected.) At 300 world units against character heights of about 8.5 (the water
submersion test uses `posY - 8.5`) it effectively never fires.

---

## 5. `ctx+2` bit `0x10`, and the trigger problem it is not

`FFX_Atel_SetActorPos` ends with `*(BYTE *)(ctx + 2) |= 0x10`.

Every site in the binary that touches that bit:

| site | function | op |
|---|---|---|
| `0x870B0A` | `FFX_Atel_SetActorPosXZ` VA `0x870970` / RVA `0x470970` | `or byte [ecx+2], 10h` |
| `0x870CED` | `FFX_Atel_SetActorPos` VA `0x870B20` | `or byte [ecx+2], 10h` |
| `0x867AC1` | `FFX_Atel_StepFrame` VA `0x867950` / RVA `0x467950` | `and byte [ebx+2], 0EFh`, unconditional, right after the per-actor script loop |

**No reader.** I swept `.text` for every encoding of a test against that bit: `test`/`or`/`and`
`byte [reg+2]` with disp8 and disp32 modrm, `test word [reg+2], 10h`, and
`test dword [reg], 100000h`. Also decompiled all 113 functions that reference `g_ffxAtelCtx` or
`g_ffxAtelCtxArray` and grepped for `+ 2)` near `0x10`. Three sites, all above.

**Answer: `ctx+2` bit `0x10` is a write-only per-frame latch. Setting it does not redo the trigger
pass and has no co-op consequence by itself.**

Note the near miss: `RANDOM_ENCOUNTER.md` documents `ctx[0] & 0x10` as a gate on the encounter
check. That is the byte at **`ctx+0`**, tested at `0x871C92` as `test dl, 10h` where `dl` came from
`mov dl, [ecx]`. Different bit, different byte.

### The real trigger hazard, which is step 5 of `SetActorPos` and is serious

`ctx+552..560` (previous player position) and `ctx+536..544` (current) are a **segment**, and four
actor steppers intersect that segment against their own geometry:

| stepper | VA | RVA |
|---|---|---|
| `FFX_Atel_StepLineTrigger` | `0x8684B0` | `0x4684B0` |
| `FFX_Atel_StepBoxTrigger` | `0x866CC0` | `0x466CC0` |
| `FFX_Atel_StepVolumeTrigger` | `0x867C10` | `0x467C10` |
| `FFX_Atel_StepPathTriggerLink` | `0x8680A0` | `0x4680A0` |

All four read both `ctx+0x218` and `ctx+0x228`. `StepLineTrigger` is the clearest:

```c
if ( cur.x != prev.x || cur.z != prev.z || (*(BYTE *)(actor + 54) & 1) )
{
  *(WORD *)(actor + 54) &= ~1u;
  v8 = sub_889BB0(&prev, &cur, &lineA, &lineB);   // segment-segment intersection
  if ( v8 ) ... FFX_Atel_FireActorEvent(0xFFFF, actorId, 4);
}
```

A teleport that leaves `ctx+552` at the old position makes that segment span the whole jump, and
**every line trigger it crosses fires.** Doorways, cutscene triggers, map transitions.

`snapPrev != 0` writes the destination into *both* `ctx+536..548` and `ctx+552..564`, collapsing the
segment to a point, and the crossing test is then skipped outright by the guard above. That is why
the game ships two script opcodes side by side, Core 19 with `snapPrev = 0` and Core 294 with
`snapPrev = 1`.

`snapPrev` also snaps the actor's own previous position, `actorPos[4..6]`, under the same condition.

The alternative gate, `actor+0x34` bit `0x10` clear, also forces the snap, but that bit means "this
actor's init script reached its done point" (see `FFX_Atel_BootAllActors` VA `0x8683E0` and
`FFX_Atel_InitFrame` VA `0x8677A0`) and it is set for every live actor in normal play. Do not rely on
it. Pass `snapPrev = 1`.

### A CHR-only write does not escape this

`FFX_Atel_PullActorPosFromChr` VA `0x869E40` / RVA `0x469E40` runs every frame via
`FFX_Atel_PushActorPosToChr` (the `ctx+88` callback) and does, unconditionally for the actor whose
id matches `ctx+10`:

```
ctx+552..564 <- ctx+536..548
ctx+536..548 <- live CHR position via FFX_Ch_GetPos
```

So moving the bound player's CHR by any means builds the giant segment for you one frame later.
There is no `snapPrev` on this path. With `FFX_Atel_SetActorPos(snapPrev=1)` the shift is harmless,
because both halves already hold the destination, so new shifts into new.

### One more trigger, the CHR-vs-CHR touch event

In `FFX_Ch_ResolveCollisionsAll`, after the walkmesh integrate, the circle separation pass runs. If a
CHR with `flags1` bit `0x200` (the player) overlaps another CHR, it ORs bit `0x4000` into that other
CHR, and the tail of the same function turns bit `0x4000` into
`FFX_Atel_FireEventOnChrActor(chr, 2)` VA `0x8764A0` / RVA `0x4764A0`, the touch-an-NPC event.

The gate on the player side is `m_speed != 0` (`CHR+0x154`, tested as `0.0 != *(float *)(v10 + 340)`).
**So placing a character on top of an NPC with `m_speed` already zeroed will not fire a touch
event.** That is reason number three to zero `m_speed` before you place.

---

## 6. The Y clamp, and why your Y mostly does not matter

`FFX_Ch_UpdateMotionAll` VA `0x832E10` / RVA `0x432E10` runs a per-CHR vertical clamp right after
`FFX_Ch_ResolveCollisionsAll` returns, switching on `m_groundMode` (`CHR+0x182`):

| mode | behaviour |
|---|---|
| 0 | `m_vertVel = 0`, and `if (flags1 bit 0x80 clear && m_groundHeight < m_posY) m_posY = m_groundHeight`. With +Y down that **only ever pushes the character up out of the floor**. It never drops anyone down and there is no gravity in this path, so a character placed in mid air in mode 0 hovers there. |
| 1 | `m_posY = m_groundHeight` unconditionally, `m_vertVel = 0`. Hard snap every frame. |
| 2 | Water buoyancy. The only mode that uses `m_vertVel` (`CHR+0x504`), `g_ffxWaterLevel` and the radius at `CHR+0x4FC..0x508`. |

**The default is mode 1.** `FFX_Ch_Allocate` VA `0x824F90` calls
`FFX_Ch_SetGroundMode(chr, 1)` at `0x82508D` for every CHR it creates.

So for a default field character the Y you hand `FFX_Ch_SetPos` is overwritten with
`m_groundHeight` on the very next `FFX_Ch_UpdateMotionAll`. **The networked Y therefore matters for
exactly one thing, picking the right deck in `FFX_Ch_WalkmeshFindTri`, after which the engine
computes the exact Y itself.**

The exception is the off-mesh case from section 4: `FFX_Ch_WalkmeshMove` never refreshes
`m_groundHeight`, so mode 1 snaps Y to the ground height of wherever the character used to be. An
off-mesh placement in mode 1 therefore gives you the right XZ and a Y from the previous map or the
previous position. That is the real cost of skipping the `FindTri` check in step 3.

The same clamp also sets `flags1` bit `0x20000000` when `(m_posY - 8.5) > g_ffxWaterLevel`, the
"submerged" flag read by `FFX_Map_GroundFootstepSound` VA `0x83D7E0` and `SndKickFoot` VA `0x835860`,
and bit `0x20000` when `m_speed > m_runThreshold`.

`m_flags1` bit `0x80` is worth knowing: set means free move, `pos += vel` on all three axes with no
walkmesh and no ground clamp at all. The disassembly is `test byte ptr [esi+194h], 80h` at
`0x83D476`, which Hex-Rays renders as `*(char *)(v5 + 404) >= 0`. (A pre-existing IDB comment called
it `0x80000000`. Corrected.)

---

## 7. Rotation

`FFX_Ch_SetRot` VA `0x82B520` / RVA `0x42B520` writes `m_rotY` (`CHR+0x158`) and validates the angle
with `sub_6EF2D0`. On failure it writes `0.0` and prints
`"VIRTUOS WARNING: got invalid rotation from atelscript in Ch_SetRot()!!"`. Do not hand it a NaN out
of a network packet.

**`m_rotY` alone is not enough.** `m_flags1` bit `0x400` makes `FFX_Ch_AutoLocomotionAnim`
VA `0x835BB0` / RVA `0x435BB0` (ground, `m_locomotionMode == 0`) and `FFX_Ch_AutoSwimAnim`
VA `0x835D30` / RVA `0x435D30` (swim, mode 2) slew `m_rotY` toward `m_moveDir` (`CHR+0x168`) every
substep via `maybe_FFX_ApproachAngle`:

| condition | max step per substep |
|---|---|
| `m_speed == 0` | 0.31415921 rad |
| `0 < m_speed <= m_runThreshold` | 0.34906578 rad |
| `m_speed > m_runThreshold` | 0.52359867 rad |

So leaving `m_moveDir` at an old heading drags the facing back over roughly 10 to 20 substeps, even
with `m_speed` zero.

**Minimum write set for a stable facing:** `m_rotY = a` **and** `m_moveDir = a`. Use
`FFX_Ch_SetRotAndMoveDir` VA `0x82B1B0` / RVA `0x42B1B0`, which calls `FFX_Ch_SetRot` then
`FFX_Ch_SetMoveDir` VA `0x82B190` / RVA `0x42B190`.

### And for a script-driven ATEL actor that is still not enough

`FFX_Atel_StepActor` VA `0x8666E0` / RVA `0x4666E0` picks the **direction** of the actor/CHR sync for
heading and rotation off `actor+0x34` bit `0x20`:

```c
case 1:
  FFX_Atel_StepMoveCmd(actor, chan);
  FFX_Atel_StepRotCmd(actor, chan);
  if ( (*(BYTE *)(actor + 52) & 0x20) != 0 )
  {
    FFX_Atel_ReadMoveDirFromChr(actor);   // 0x869DF0, CHR -> actor
    FFX_Atel_ReadRotFromChr(actor);       // 0x869F90, CHR -> actor
  }
  else
  {
    FFX_Atel_ApplyMoveToChr(actor, chan); // 0x870540, actor -> FFX_Ch_SetMoveSpeed
    FFX_Atel_ApplyDirToChr(actor);        // 0x870360, actor -> FFX_Ch_SetMoveDir
    FFX_Atel_ApplyRotToChr(actor, chan);  // 0x870DC0, actor -> FFX_Ch_SetRot
    FFX_Atel_IntegrateActorPos(actor);    // 0x8629C0
  }
```

* bit `0x20` **set** (player controlled): the CHR is authoritative, the actor record mirrors it.
  Write the CHR.
* bit `0x20` **clear** (script driven): the actor record is authoritative and overwrites
  `m_speed`, `m_moveDir` and `m_rotY` every single frame. A CHR-only write to those three is reverted
  on the next frame. Write the actor record, or flip the bit.

Position goes the other way in both cases. `FFX_Atel_PullActorPosFromChr` always copies the live CHR
position up into the actor record, and `FFX_Atel_PushActorPosToChr` only pushes down when
`actor+0x38` bit 1 is set.

`actor+0x34` is also where bit `0x80` means "this actor is stepping at all" (`FFX_Atel_StepActor`
opens with `if (*(char *)(a1 + 52) < 0)`) and bit `0x40` is `StepLineTrigger`'s inside/outside latch.

Also still live after a placement: the ATEL **rotate** command, `rotCmd+2`
(`FFX_Atel_SetupRotCmd` VA `0x871030` / RVA `0x471030`, bound into `motionChannel+72` by
`FFX_Atel_BindRotCmdToChannels` VA `0x871090`). `FFX_Atel_ApplyRotToChr` keeps driving `m_rotY`
toward the command's target angle until it reports completion. Cancel it the same way as the move
command if the script had one running.

---

## 8. Velocity and the other motion carry-over

Offsets confirmed from code, not from the struct names:

| field | offset | written by | read by |
|---|---|---|---|
| `m_velX` | `CHR+0x4C` | `FFX_Ch_UpdateMotionAll` as `cos(m_moveDir) * dt*30*0.05*m_speed` | `FFX_Ch_WalkmeshMove`, `FFX_Ch_ResolveCollisionsAll` |
| `m_velY` | `CHR+0x50` | `FFX_Ch_SetVelY` VA `0x82B920` as `arg * 0.05` | `FFX_Ch_ResolveCollisionsAll`, `posY += velY` |
| `m_velZ` | `CHR+0x54` | `FFX_Ch_UpdateMotionAll` as `sin(m_moveDir) * dt*30*0.05*m_speed` | same as velX |
| `m_vertVel` | `CHR+0x504` | `FFX_Ch_AddVerticalVelocity` VA `0x82A890` / RVA `0x42A890`, `FFX_Ch_AutoSwimAnim` | the mode 2 water clamp only |

**Your offsets were right.** `0x4C` / `0x50` / `0x54` and `0x504`.

**All three of `0x4C`, `0x50`, `0x54` are zeroed every frame** at `0x83D4B8..0x83D4BE` inside
`FFX_Ch_ResolveCollisionsAll`, immediately after the integrate, for every CHR it touched. Velocity
cannot survive a frame boundary. Zeroing it is still correct and cheap because your placement may
land mid-frame between the velocity generation and the integrate, but it is not the carry-over you
have to worry about.

The actual motion carry-over a placement must deal with:

| what | where | why |
|---|---|---|
| `m_speed` | `CHR+0x154` | regenerates `m_velX`/`m_velZ` every substep. **The real one.** |
| `m_moveDir` | `CHR+0x168` | direction of that regenerated velocity, and the facing slew target |
| `m_vertVel` | `CHR+0x504` | only survives in `m_groundMode == 2` (water). Modes 0 and 1 zero it. |
| ATEL move command | `moveCmd+2 != 0`, `FFX_Atel_GetMoveCmd` VA `0x86C0A0` | re-asserts `m_speed` from `actorPos[3]` every frame and walks the character toward its old target |
| ATEL rotate command | `rotCmd+2` | keeps slewing `m_rotY` toward its old target |

`FFX_Atel_ApplyMoveToChr` is worth quoting because it cuts both ways:

```c
speed = 0.0f;
if ( *(u16 *)(moveCmd + 2) != 0 ) speed = actorPos[3];
...
FFX_Ch_SetMoveSpeed(chr, speed);        // unconditional for a CHR-backed actor
```

So a script-driven actor with **no** active move command has `m_speed` driven to 0 for you every
frame. With an active one it keeps walking no matter where you put it.

**There is no accumulated move vector, no pending step and no path-follow state on the CHR beyond
those.** In particular `m_walkmeshPos` (`CHR+0x3C`) is recomputed from the position at the top of
every walkmesh step, it is the segment origin for the edge tests and nothing else. Not a
destination, not a spawn point, not a last-safe-position.

`CHR+0x4E8` (effective collision radius) is recomputed every frame as
`(CHR+0x64 + CHR+0x5C) * 0.5 * CHR+0x4E4`, and `CHR+0x520` as `posY - CHR+0x51C`. Both derived, no
action needed.

---

## 9. Frame order

Call sites inside `FFX_MainStep` VA `0x820AE0` / RVA `0x420AE0` that matter:

| VA | call | what it does to a placement |
|---|---|---|
| `0x820FDF` | `FFX_Ch_ClearFlags1StepBits` | wipes `flags1` bits 24..26 on every CHR |
| `0x821010` | `FFX_Player__stepControl` | pad to `m_speed` / `m_moveDir` for the controlled CHR |
| `0x82101A` | `FFX_Atel_StepOnce` | `ctx+88` `PushActorPosToChr` then `PullActorPosFromChr`, then `ctx+92` `StepActor`: move and rot commands, the `Apply*ToChr` trio, and the line / box / path / volume trigger steps |
| `0x82104D` | `FFX_Came_StepAll` | |
| `0x82107D` | `FFX_Ch_UpdateMotionAll` | `m_speed` + `m_moveDir` to velocity, then `FFX_Ch_ResolveCollisionsAll` -> `FFX_Ch_WalkmeshMove` (re-finds the triangle, refills `m_groundHeight` / `m_groundNormal` / `m_groundAttrs`), then the Y ground clamp |
| `0x8210BD` / `0x8210C2` | `FFX_PlayerCam_Step`, `FFX_Came_StepAll` again | |
| `0x8210DA` | `FFX_Ch_DispatchInBatches` | |
| `0x8212F8`, `0x821325` | `FFX_Ch_StepAll` | |
| `0x821465` | `FFX_Ch_ShadowPassAll` | |

**Inject the placement before `0x82101A`.** Then the actor pull sees the new position, the triggers
step against an already collapsed segment, and `FFX_Ch_UpdateMotionAll` re-finds the triangle and
refills the ground state in the same frame. Placing after `0x82107D` leaves the character rendered
for one frame with `m_walkmeshTri == -1` and a stale `m_groundHeight`.

On a map change, `FFX_Ch_SetActiveWalkmesh` VA `0x83EA00` / RVA `0x43EA00` sets
`m_walkmeshTri = -1` on every in-use CHR and caches `g_ffxWalkmeshTris`, `g_ffxWalkmeshVerts`,
`g_ffxWalkmeshTriCount` and `g_ffxWalkmeshScale`. So you must wait for that before step 3's
`FindTri` call is meaningful, and you never need to invalidate the triangle yourself after a load.

---

## 10. `sub_870EF0` identified: it is the place name

Renamed to **`FFX_SaveData_SetMapNameIdFromGroundDic`** VA `0x870EF0` / RVA `0x470EF0`.

It writes `g_ffxSaveData + 0x0E`, the word that `FFX_SaveData_GetMapNameId` VA `0x86C410` returns and
the save-slot description prints. `WORLD_STATE.md` already had `+0x0E` as "map-name id for the
save-slot description", so this confirms the writer.

```c
pkg = g_ffxAtelCtx0EventPkg;                  // see below
if (pkg == 0) { savedata+0x0E = 0; return; }
w = *(s16 *)(pkg + 30);
if (w < 0)                                     // high bit set
    w = *(s16 *)(pkg + *(u32 *)(pkg + 40)
                     + 2 * (a1 >= (*(s16 *)(pkg + 30) & 0x7FFF) ? 0 : a1));
savedata+0x0E = w;
```

So the word at `pkg+30` is either the place name id directly, one value for the whole map, or (high
bit set) a count plus a table at `pkg + *(u32 *)(pkg + 40)` indexed by the argument. The argument is
the 2-bit `dic` ground attribute, `FFX_Map_GroundAttrGetDic` VA `0x83D7A0` =
`(CHR+0x828 >> 11) & 3`. That is how one map can report different place names in different regions.

Two callers only, both the player-position update:
`FFX_Atel_SetActorPos 0x870B20` and `FFX_Atel_PullActorPosFromChr 0x869E40`, in both cases right
after `savedata+0x10 = FFX_Map_GroundAttrGetEnc(chr)` (the random encounter zone byte, already
documented in `RANDOM_ENCOUNTER.md`).

Also renamed: `dword_1325BE8` -> **`g_ffxAtelCtx0EventPkg`** VA `0x1325BE8` / RVA `0xF25BE8`. That
address **is** `g_ffxAtelCtxArray + 72`, i.e. ATEL context 0's event package pointer
(`0x1325BA0 + 0x48`), the same offset-based blob `FFX_Atel_GetEntryRecord` VA `0x86BFE0` calls
"atel". Confirmed by finding `*(_DWORD *)(ctx + 72)` reads in `maybe_FFX_Field_QueueBootTasks`,
`sub_862470`, `sub_865740`, `sub_8682F0` and `sub_86E8D0`. `FFX_Atel_Init 0x86D6D0` sets it to 0.

**Co-op note:** this is single-slot state. Only the bound player actor reaches it, so a second
character walking into a differently named region does not change the save's place name. One more
item for the single-instance-globals audit.

---

## 11. Other things that bite

**The camera will sweep.** `FFX_Came_StepAll` VA `0x7BE110` / RVA `0x3BE110` applies a per-axis first
order damper in its commit stage, `cur = prev + (target - prev) / divisor`, with the divisors at
`cameSlot+112` / `+116` / `+120` for the XYZ path and `+128` / `+132` / `+136` for the polar path
(`sub_7BDA80` is the filter, its state at `+1408` / `+1424` / `+1440`). A divisor of 0 means snap. So
moving a character the camera is framing makes the camera glide to the new spot over several frames
instead of cutting. Options, bluntest last: let the engine's own map-load path reset the camera,
drive the camera entry to mode 1 ("pos") with `FFX_Came_SetPos` VA `0x7BF940` / RVA `0x3BF940`, or
zero the three divisors for one frame. `FFX_Came_ModeAct_Step` VA `0x7C0560` writes the averaged
position of up to two actors with no damping of its own, but it still goes through the same commit
stage. That mode is also the ready-made "frame two characters" path if you want it for co-op.

**`FFX_Ch_MarkDirty` is not optional for the mesh.** Its second half resets the double buffered part
vertex arrays, see section 1. `FFX_Atel_SetActorPos` already calls it. A CHR-only placement must.

**Prefer `FFX_Atel_SetActorPos` over `FFX_Atel_SetActorPosXZ`.** The XZ variant writes `0.0` into the
actor record's Y before calling `FFX_Ch_SetPosXZ`, so the actor record's Y is clobbered even though
the CHR's Y is left alone.

**`FFX_Map_IsTriBlockedForChr` VA `0x83E4E0` / RVA `0x43E4E0` is a known co-op global.** Walkmesh
attribute 2 is "player only passage" and attribute 14 is "wall for the player only", and both resolve
against `FFX_Ch_GetPlayerChr` VA `0x82D860`. A second player's character gets the non-player answer,
so a player-only passage reads as a wall for it. This affects movement, not placement directly, but
it means a placement onto such a triangle can leave a second character unable to walk out of where
you put it. `FindTri` does not consult the blocked test, so a successful `FindTri` does not mean the
character can move there.

**Collision capsule, ground attributes, triangle vertices.** All derived per frame, nothing to
clear. `CHR+0x4E8`, `CHR+0x828`, `CHR+0x8C`/`0x9C`/`0xAC`.

**No LOD or culling cache keyed on the old position was found.** I did not find one. That is a
negative result from not looking exhaustively, not a proof. The render path takes the position
through the root joint transform each frame.

---

## 12. Quick reference

| symbol | VA | RVA |
|---|---|---|
| `FFX_Atel_SetActorPos` | `0x870B20` | `0x470B20` |
| `FFX_Atel_SetActorPosXZ` | `0x870970` | `0x470970` |
| `FFX_Atel_GetActor` (index) | `0x86A830` | `0x46A830` |
| `FFX_Atel_FindActorByPartyChar` | `0x86A470` | `0x46A470` |
| `FFX_Atel_GetPlayerActorId` (id) | `0x86C1A0` | `0x46C1A0` |
| `FFX_Atel_GetMoveCmd` | `0x86C0A0` | `0x46C0A0` |
| `FFX_Atel_PullActorPosFromChr` | `0x869E40` | `0x469E40` |
| `FFX_Atel_PushActorPosToChr` | `0x866800` | `0x466800` |
| `FFX_Atel_StepActor` | `0x8666E0` | `0x4666E0` |
| `FFX_Atel_StepFrame` | `0x867950` | `0x467950` |
| `FFX_Atel_StepLineTrigger` | `0x8684B0` | `0x4684B0` |
| `FFX_Atel_ApplyMoveToChr` | `0x870540` | `0x470540` |
| `FFX_Atel_ApplyDirToChr` | `0x870360` | `0x470360` |
| `FFX_Atel_ApplyRotToChr` | `0x870DC0` | `0x470DC0` |
| `FFX_SaveData_SetMapNameIdFromGroundDic` | `0x870EF0` | `0x470EF0` |
| `FFX_Ch_SetPos` | `0x82B480` | `0x42B480` |
| `FFX_Ch_SetPosXZ` | `0x82B440` | `0x42B440` |
| `FFX_Ch_SetRot` | `0x82B520` | `0x42B520` |
| `FFX_Ch_SetRotAndMoveDir` | `0x82B1B0` | `0x42B1B0` |
| `FFX_Ch_SetMoveDir` | `0x82B190` | `0x42B190` |
| `FFX_Ch_SetMoveSpeed` | `0x82B840` | `0x42B840` |
| `FFX_Ch_SetVelY` | `0x82B920` | `0x42B920` |
| `FFX_Ch_AddVerticalVelocity` | `0x82A890` | `0x42A890` |
| `FFX_Ch_SetGroundMode` | `0x82B240` | `0x42B240` |
| `FFX_Ch_SetGroundHeight` | `0x82B360` | `0x42B360` |
| `FFX_Ch_SetLocomotionMode` | `0x82B3A0` | `0x42B3A0` |
| `FFX_Ch_MarkDirty` | `0x824650` | `0x424650` |
| `FFX_Ch_ResetPartVertexBuffers` | `0x824670` | `0x424670` |
| `FFX_Ch_GetPos` | `0x82AC90` | `0x42AC90` |
| `FFX_Ch_GetWalkmeshTri` | `0x82ADC0` | `0x42ADC0` |
| `FFX_Ch_GetGroundHeight` | `0x82AC40` | `0x42AC40` |
| `FFX_Ch_SuppressNextSetPos` (dead) | `0x82ACC0` | `0x42ACC0` |
| `FFX_Ch_ClearFlags1StepBits` | `0x832DB0` | `0x432DB0` |
| `FFX_Ch_UpdateMotionAll` | `0x832E10` | `0x432E10` |
| `FFX_Ch_ResolveCollisionsAll` | `0x83D3B0` | `0x43D3B0` |
| `FFX_Ch_WalkmeshMove` | `0x83E5F0` | `0x43E5F0` |
| `FFX_Ch_WalkmeshFindTri` | `0x83DE10` | `0x43DE10` |
| `FFX_Ch_WalkmeshTestEdgeCrossing` | `0x83D960` | `0x43D960` |
| `FFX_Ch_WalkmeshSlideAlongEdge` | `0x83E180` | `0x43E180` |
| `FFX_Ch_SetActiveWalkmesh` | `0x83EA00` | `0x43EA00` |
| `FFX_Ch_AutoLocomotionAnim` | `0x835BB0` | `0x435BB0` |
| `FFX_Ch_AutoSwimAnim` | `0x835D30` | `0x435D30` |
| `FFX_Map_WalkmeshHeightAt` | `0x83E040` | `0x43E040` |
| `FFX_Map_WalkmeshGetTriNormal` | `0x83E130` | `0x43E130` |
| `FFX_Map_GetWalkmeshScale` | `0x83E9A0` | `0x43E9A0` |
| `FFX_Map_IsTriBlockedForChr` | `0x83E4E0` | `0x43E4E0` |
| `FFX_Map_GroundAttrGetDic` | `0x83D7A0` | `0x43D7A0` |
| `FFX_Map_GroundAttrGetEnc` | `0x83D820` | `0x43D820` |
| `FFX_Came_SetPos` | `0x7BF940` | `0x3BF940` |
| `FFX_Came_StepAll` | `0x7BE110` | `0x3BE110` |
| `FFX_MainStep` | `0x820AE0` | `0x420AE0` |

| global | VA | RVA |
|---|---|---|
| `g_ffxSuppressNextSetPos` (byte, always 0) | `0x12FFAD8` | `0xEFFAD8` |
| `g_ffxWalkmeshTris` | `0x1301A84` | `0xF01A84` |
| `g_ffxWalkmeshTriCount` | `0x1301A88` | `0xF01A88` |
| `g_ffxWalkmeshVerts` | `0x1301A8C` | `0xF01A8C` |
| `g_ffxWalkmeshScale` (float) | `0x1301A90` | `0xF01A90` |
| `g_ffxWalkmeshStepHeight` (float, 300.0) | `0xC4DEB0` | `0x84DEB0` |
| `g_ffxWalkmeshStepHeightScaled` | `0x1301AF4` | `0xF01AF4` |
| `g_ffxAtelCtx` (pointer) | `0x1326B28` | `0xF26B28` |
| `g_ffxAtelCtxArray` (context 0) | `0x1325BA0` | `0xF25BA0` |
| `g_ffxAtelCtx0EventPkg` (= ctx0 + 72) | `0x1325BE8` | `0xF25BE8` |
| `g_ffxChrArray` | `0x23C44E4` | `0x1FC44E4` |
| `g_ffxChrCount` | `0x23C44E0` | `0x1FC44E0` |
| `g_ffxSaveData` | `0x112CA90` | `0xD2CA90` |

CHR offsets used above: `m_posX/Y/Z` `0x0C/0x10/0x14`, `m_prevPos` `0x1C`, `m_walkmeshPos` `0x3C`,
`m_velX/Y/Z` `0x4C/0x50/0x54`, `m_groundNormal` `0x7C`, `m_triVert0/1/2` `0x8C/0x9C/0xAC`,
`m_speed` `0x154`, `m_rotY` `0x158`, `m_moveDir` `0x168`, `m_groundHeight` `0x16C`,
`m_runThreshold` `0x170`, `m_groundMode` `0x182`, `m_locomotionMode` `0x186`, `m_flags1` `0x194`,
`m_flags2` `0x198`, `m_collisionRadius` `0x4E8`, `m_vertVel` `0x504`, `m_walkmeshTri` `0x824` (s16),
`m_groundAttrs` `0x828`. CHR stride is 2176 bytes.

Actor offsets: `+0x00` type byte pointer, `+0x2E` actor id, `+0x34` flag word (bit `0x10` booted,
bit `0x20` player controlled, bit `0x40` line trigger latch, bit `0x80` stepping), `+0x38` bit 1
position dirty, `+0x40` party character index, `+0x9C` the `CHR *`, `+0xAA` kind (1 = CHR-backed,
2 = camera), `+0x284` / `+0x558` position vector base.

ATEL context offsets: `+0x02` bit `0x10` the dead placement latch, `+0x0A` bound player actor id,
`+0x48` event package, `+0x218` current player position, `+0x228` previous player position.

---

## What is still unknown

* **Who sets `g_ffxAtelCtx0EventPkg` to a non-zero value.** Every direct xref on that address is a
  read except `FFX_Atel_Init`'s `= 0`, so the real writer stores through a register base plus 72 and
  is not in the xref list. It does not affect placement, only the place-name lookup.
* **Whether `actorPos[9]` is a pitch.** `FFX_Atel_ApplyMoveToChr` feeds it through `sin`/`cos`
  alongside `actorPos[8]` for move kinds 2, 4, 5 and 6, which reads like an elevation angle for
  flying and airship actors. Not confirmed.
* **Whether any magic plugin DLL reads `flags1` bit `0x02000000` or `ctx+2` bit `0x10`.** My searches
  covered `FFX.exe` only. `FFX_Ch_SetPos` is host API entry 33 so the plugins can set the bit, but I
  did not disassemble the `magic_%04d.dll` files to check for readers. Low risk, since neither bit
  survives past the top of the next frame.
* **The exact semantics of `actor+0x38` bit 4** (`FFX_Atel_PushActorPosToChr` uses it to decide
  whether to also sync the heading back off the CHR after a push).
* **Whether placing a character onto a triangle that `FFX_Map_IsTriBlockedForChr` rejects for it is
  recoverable.** `FindTri` does not consult that test, so a placement can succeed and then leave the
  character boxed in. I did not trace what the slide loop does when every neighbour is blocked.
* **LOD and culling.** I found nothing keyed on the old position, but I did not audit the render
  submission path for it, so treat that as unexamined rather than clear.
* **Whether `FFX_Ch_WalkmeshFindTri`'s cost is acceptable at the moment of a map load.** It is a
  linear scan with no index and the shipped triangle counts are not something I measured.
