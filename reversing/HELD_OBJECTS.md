# Held and carried objects: it is a bone parent, and nothing streams

This closes a research item that has been open on `COOP_DESIGN.md`'s list for several
sessions:

> **How a carried object is attached to its carrier.** Bone parent, script-written position, or
> Bullet rigid body. One answer decides whether held-object sync is one byte or a transform
> stream.

**It is a bone parent.** Not physics, not a per-frame script position write. The transform of a
carried object is derived from its carrier by the engine, once per frame, on both machines, from
state that lockstep already keeps identical. **Nothing about a carry has to cross the wire.**

Nothing in here has run in a game. Everything below is either read out of the binary, or built
and compiled, and the two are kept separate on purpose. The ranked list of what is most likely
wrong is at the bottom.

---

## 1. The answer, and the one function that proves it

`FFX_Ch_BuildSkinMatrices 0x832760` is where the attachment is applied. It runs per character
from `FFX_Ch_UpdateRenderJob 0x833530`, which `FFX_MainStep` dispatches. Its first branch tests
`chr->m_parent` and, when that is non-null, computes the carried object's world matrix like
this:

```
S          = identity, diagonal (1.0 / carrier->m_data->m_f30) * chr[0x1B4]
jointLocal = carrier->m_joints[352 * chr->m_parentJoint + 136]      the carrier's joint 4x4
M          = jointLocal * S
M.trans    = (M * (chr->m_attachOffset * carrier->m_data->m_f30)) * 1000
chr->m_rootJoint[8] = carrier->m_rootJoint[8] * M                   the child's world matrix
```

**The carried object's own position and rotation are not read on that path.** `m_posX/Y/Z` at
`CHR+0x0C` appears only in the *unparented* `else` branch, which is where an ordinary character
gets its matrix from its position and facing. A carried object skips that entirely.

So the carried object's transform is a pure function of four things, all of which both machines
already hold identically:

| what | where | why both machines agree |
|---|---|---|
| the carrier's world matrix | `CHR+0x1D0` | lockstep replicates the carrier's input and position |
| the carrier's joint palette | `*(CHR+0x328)`, stride 352 | derived from replicated animation state |
| `m_parentJoint` | `CHR+0x1A0` | written once by a replicated script |
| `m_attachOffset` | `CHR+0x1A4`, float4 | same |

Plus a fifth, `CHR+0x1B4`, the attach scale, which defaults to 1.0 and whose only writers are
also replicated. See section 6.

### Why it is not Bullet

Bullet **is** linked into `FFX.exe`. That is what made this question worth asking. It is not
what moves a carried object, and the check was structural rather than a spot read.

Every code reference into the 846 bullet-named functions was enumerated, and every referrer
whose address is below the Bullet range sits in one place: **PhyreEngine's reflection and
class-descriptor region, 0x5D7000 to 0x603000.** That is 87 referrers and they are all of one
shape, the `PPhysics{Box,Capsule,Cylinder,Mesh,Plane,RigidBody,Shape,Sphere,World,Interface,CharacterController}Bullet`
wrapper classes registering themselves with Phyre's reflection system, plus
`btAlignedAllocInternal` and `btAlignedFreeInternal` thunks inside those wrappers.

**No FFX game code references Bullet at all.** The CHR family, 0x820000 to 0x840000, references
it nowhere. Bullet is in the image because Phyre links it, and the `PPhysics*Bullet` classes
exist as asset types nothing instantiates on the paths that matter.

That is one half. The other half is stronger: even if a Phyre asset somewhere did create a rigid
body, it could not be what moves a carried object, because `FFX_Ch_BuildSkinMatrices` overwrites
the carried object's world matrix from its carrier every frame and never reads anything a solver
could have written.

### Why it is not a per-frame script write either

A script does set the carry up. Three ATEL syscalls do it, and all three are **one-shot**: they
write the carry fields and return. Nothing runs per frame.

| syscall | library, function | calls | script arguments, in stack-pop order |
|---|---|---|---|
| `FFX_AtelSys_Ch_044_resi 0xA79BC0` | 5 (Ch), 44 | `FFX_Ch_AttachToParentJoint 0x832680` | `(actorId, jointIndex)` |
| `FFX_AtelSys_Ch_045_resi 0xA79C50` | 5 (Ch), 45 | `FFX_Ch_AttachToParentBone 0x832630` | `(actorId, boneId)` |
| `FFX_AtelSys_Ch_082_resi 0xA787E0` | 5 (Ch), 82 | `FFX_Ch_AttachToParentOffset 0x8326E0` | `(actorId, jointIndex, ox, oy, oz)` |

All three take `actorId == -1` to mean detach.

There are also C-side attach callers, and they are worth knowing about because they prove the
mechanism is the engine's general answer rather than a script trick:

- `FFX_Field_ChrRegTask 0x861850`, at `0x861D5F`. Attaches a character's weapon to its hand on
  the field, from the per-slot tables at `task+0xAF4` (child) and `task+0xAFC` (bone), with
  `task+0x9C` as the carrier. Category 7 slot 0 gets `FFX_Ch_ModelSetHide(child, 1, 1)` first.
- `sub_793330` at `0x793399`, `sub_793F10` at `0x793FB9`, `sub_795C90` at `0x795CE6`/`0x795D1D`
  and `sub_7AAB80` at `0x7AABC0`. All battle, all going through `FFX_Btl_GetUnitChr`. Weapons
  on battle units.

Both groups are driven by state that is itself replicated, so neither needs a sync.

### What this costs, against the design doc's estimate

`COOP_DESIGN.md` said the bone-parent case means syncing "who is holding what, which is about
one byte per object". That is the right shape. The arithmetic is wrong in the cheap direction:
**nothing per object per step**. The carry is set up by a replicated script, so even the "who is
holding what" does not need sending. The only thing that needs a message is a *correction*, and
only when one is wanted. See section 4.

---

## 2. The identity problem, and the engine's own answer to it

`m_parent` at `CHR+0x19C` is a raw `CHR *`. Useless on a wire: the CHR pool is freed and
reallocated on every map transition, so the pointer means nothing on the other machine and
nothing on this one after a load.

The engine hit this and solved it, and the fix is sitting in a function nobody had looked at the
tail of. The last four instructions of `FFX_Ch_CopyState 0x828620`:

```asm
0x828ab2  mov  eax, [edx+1B8h]        ; dst->m_parentUid
0x828aff  cmp  eax, 0FFFFFFFFh
0x828b02  jnz  short loc_828B10
0x828b04  mov  dword ptr [edx+19Ch], 0    ; -1 means no parent
0x828b10  push eax
0x828b11  call FFX_Atel_GetActorChrById
0x828b1c  mov  [ecx+19Ch], eax            ; rebuild the pointer from the id
```

So `m_parentUid` is read back **as an ATEL actor id**, and the whole point of it is to survive a
copy that a pointer would not. That makes it the durable name for a carry link, chosen by the
engine rather than invented by a mod.

### m_objId is the ATEL actor id

`FFX_Ch_AttachToParentBone` writes `chr->m_parentUid = carrier->m_objId`. So for
`FFX_Atel_GetActorChrById` to resolve it, `m_objId` has to be an actor id. It is:

```
FFX_AtelOp_SetActorModel 0x85D000
  actor[0x9C] = FFX_Ch_SpawnWithObjId(typeId, *(WORD *)(actor + 0x2E))
                                              ^^^^^^^^^^^^^^^^^^^^^^^
                                              actor+0x2E is the actor id
    -> FFX_Ch_AllocateWithObjId 0x825650:  chr->m_objId = objId
```

and `FFX_Atel_GetActorChrById 0x86B3A0` resolves an actor id straight back to `actor[0x9C]`,
which is the CHR that `SetActorModel` stored.

`FFX_Ch_Allocate 0x824F90` initialises `m_objId` to -1, so a CHR with no event actor behind it
reads -1 and **cannot be named on a wire at all**. The mod refuses such an object rather than
falling back to a pool slot index, because a slot index would look like it worked.

Confirming note on the earlier pass: `PHASE1_CLONE.md` says `m_objId` "is read by exactly three
functions, all parent-bone attachment". That is right, and the three are the three attachers
reading the *carrier's* copy. What it missed is the fourth reader of the *child's* copy of it,
`m_parentUid`, in `FFX_Ch_CopyState`.

### The two have different lifetimes, and it matters

`FFX_Ch_OrphanChildren 0x826150`, called only from `FFX_Ch_Dispose 0x8266F0`, clears
`m_parent` on every child of a disposed carrier and **does not clear `m_parentUid`**. So a CHR
can sit with `m_parent == NULL` and `m_parentUid` still naming a dead carrier, which
`FFX_Ch_CopyState` would happily resolve back into a pointer. `m_parent` is the live truth,
`m_parentUid` is the durable id, and they are allowed to disagree.

---

## 3. The real co-op problem, and it is not a desync

The carry replicates for free. The thing that does not work in co-op is **who the script
decides should be carrying**.

`FFX_AtelSys_Core_126_resi 0x85C580` is a plain thunk for `FFX_Atel_GetPlayerActorId`, which
reads the bound player actor id at ATEL context `+10`. It is library 0 (Core) function 126, so a
script can ask "who is the player" and get back **one slot for the whole game**.

A field script that picks something up has to name the carrier, and a script that means "the
player" gets that one slot. So in co-op:

1. Player 2, driving Wakka, walks up to a Cloister pedestal and presses confirm.
2. The trigger pass is a pure function of replicated positions and replicated buttons, so both
   machines pick the same winner and fire the same event on the same step. That part already
   works and is written up in `INTERACTION_PATH.md`.
3. Both machines run the same script, and both attach the sphere to the **bound player**.
4. The two machines agree perfectly. They are both holding it on the wrong character.

**That is not a divergence, so no amount of sync fixes it.** It is the game's own single-slot
assumption showing through, and the only fix is to re-point the object after the fact, on a step
both machines apply. That is what the `kCommandHeldObject` handover is for.

This is the same shape as the problem `ffx/Atel.h` already describes for the trigger pass, where
the single-slot thing is the player position cache at `ctx+536`, and the same shape as
`DialogueSync.h`'s port 0. It is the third instance of "the engine is per-character except for a
handful of one-slot caches".

---

## 4. What got built

### The census, which is the part that earns its keep

A claim that something replicates for free is worth exactly what the detector that would catch
it not doing is worth. So `ServiceHeldObjectStep` walks the CHR pool once per simulation step,
records every carry, and hashes it.

Per carry it records the object's pool slot, its actor id, its CHR type id, the carrier's pool
slot, the carrier's actor id and `m_parentJoint`. The hash covers the three integers that decide
who is holding what, in **pool slot order**.

Slot order rather than sorted, on purpose. Under lockstep both machines allocate CHRs from the
same scripts in the same order, and `FFX_Ch_MoveChrMemory 0x826FA0` compacts the heap blocks a
CHR points at but does not relocate CHRs themselves, so the slot order already has to agree. If
it ever does not, the machines have already diverged somewhere upstream, and a hash that papered
over that by sorting would hide the bigger problem in order to report the smaller one.

The attach offset and the attach scale are deliberately **not** hashed. They are floats derived
from the carrier's model scale, and hashing a float across two machines turns a harmless
last-bit difference into a reported desync.

The census **refuses rather than truncates** on overflow, and a refusal hashes differently from
"no carries". A truncated census would hash cleanly while silently no longer covering part of
the world, which is the one failure mode a desync detector must not have.

Edges are logged with the step number: picked up, put down, changed hands, moved to a different
joint on the same carrier. Edge logging is suppressed on the step the live map id changes,
because a transition disposes the whole pool and rebuilds it, so every carry on the old map
would report as a drop and every one on the new map as a pickup.

**The census runs with no session at all.** It only reads, and it is how anybody can re-confirm
the research in a solo game: start the mod, walk into a Cloister, press ctrl+F11, read the carry
list.

### The handover command

`kCommandHeldObject = 8`, payload defined in `world/HeldObjects.h` rather than in
`net/Commands.h`, since `RequestCommand` takes a void pointer and a length.

```c
struct HeldObjectCommand
{
    uint16_t mapId;          // which map the two actor ids mean
    int16_t  objectActorId;  // the carried object, as an ATEL actor id
    int16_t  carrierActorId; // the new carrier, or -1 to put it down
    int16_t  boneId;         // a LOGICAL bone id, or -1 for "keep the current bone"
    uint8_t  reserved[2];
};
```

10 bytes against `MaxCommandBytes` of 96, with a `static_assert`.

An **absolute state**, not a swap and not an edge, for the same reason `MenuOverrideCommand` and
`PauseCommand` are: the host orders these, so two crossing commands have to leave both machines
in the same place whichever way round they land, and only a state does that.

The **map id rides along** and a mismatch is a refusal with a log line. An ATEL actor id is map
scoped, so a handover issued just before a transition and stamped for a step after it would name
a completely different object on the new map, and the engine would not complain, it would attach
the wrong thing.

Applying it is consumed from `ServiceHeldObjectStep`, guarded so one step is drained once. It
resolves both actor ids through `ffx::ChrForActor`, works out the bone, asks whether the new
carrier actually has that bone, and then calls the engine's own attach followed by
`MarkCharacterDirty`.

### The bone id versus the joint index, which is the trap in this area

`m_parentJoint` is an index into one particular skeleton's joint array. It means nothing on
another character. A **logical bone id** is semantic, 0..21, resolved per skeleton through the
CHRDATA bone point table.

So moving a carried object from Tidus to Wakka has to go bone id -> new joint index, and copying
`m_parentJoint` across would attach the object to whatever joint happened to have that index.

Worse, `FFX_Ch_LookupBonePoint 0x833A70` **does not report a miss**. A bone id the new skeleton
has no record for returns 0 with the out joint left at 0 and the offset zeroed, which silently
attaches the object to the carrier's root. So the mod asks `ffx::JointForBoneId` first and
refuses with a log line rather than putting a sphere at somebody's feet.

The reverse direction, joint index back to bone id, is `ffx::BoneIdForJoint`, a walk of the bone
point table looking for the record whose joint field matches. First match wins, which is what
the engine's own forward lookup does too.

### That logical bone ids run 0..21, and how that was settled

`FFX_Ch_GetBoneWorldPos 0x8354F0` takes a second argument this file had previously only called
"boneIdx". It is a **logical bone id**, and the proof is its only writer.

`FFX_Ch_UpdateBonePositions 0x8350F0` zeroes all 22 w slots of `m_boneWorldPos` at `CHR+0x524`,
then walks the bone point table and stores each world position at
`m_boneWorldPos[4 * (*(WORD *)record & 0x3FFF)]`. That index is the bone id field. So the 22
slots are a semantic set and ids run 0..21. The body-centre work afterwards asks for ids 0, 1,
7 and 8 by hand, which is more evidence the numbering is semantic and shared across skeletons.

`FFX_Ch_GetBoneWorldPos` has no bounds check on it, so `ffx::BoneWorldPosition` clamps. That is
the point of the wrapper.

---

## 5. A carried object's own position is STALE, and that narrows an open design question

The parented branch of `FFX_Ch_BuildSkinMatrices` does not write `m_pos` either. Nothing does
while an object is held. So a carried object keeps whatever position it had when it was picked
up, for as long as it is carried.

`COOP_DESIGN.md` has this as an open question:

> **The state that actually must not diverge is the puzzle solution, not the sphere.** What opens
> the door is "which pedestals are filled". If that lives in a script variable or a save flag then
> it is part of the save data block and the existing sync covers it. If it is derived from object
> world positions then it is fragile and wants a snapshot taken at the moment the door check runs.

**The second branch is effectively ruled out for anything being carried.** A door check reading
the sphere's `m_pos` would read where the sphere was before anybody picked it up. No shipped
puzzle can be built on that, so the puzzle state has to be in script variables, save flags, or
read off the pedestal rather than off the sphere. All three are covered by the existing save
block sync.

What is NOT ruled out is a check against an object that has been **put down** somewhere, since a
drop clears `m_parent` and the object resumes being an ordinary CHR with a real position. That
is a narrower question than the one the design doc asked, and it is still open.

Where a held object actually is, visually, is the carrier's bone position, which is why
`ffx::BoneWorldPosition` exists and why the ctrl+F11 log prints it. **+Y IS DOWN**, so a larger Y
is lower.

---

## 6. CHR+0x1B4 is the attach scale, and that is a new name

It was `FFX_Ch_SetF1B4 0x832610` in the IDB, now `FFX_Ch_SetAttachScale`.

The evidence is that its **only** reader anywhere in the Ch family is the parented branch of
`FFX_Ch_BuildSkinMatrices`, at `0x8327FB`, where it becomes the diagonal of the scale matrix
applied to the parent joint. A sweep of every `disp == 0x1B4` instruction in `.text` found no
other read. **On an unparented CHR the field does nothing at all.**

`FFX_Ch_Allocate` sets 1.0, so the default is neutral. The other three writers are
`maybe_FFX_Btl_SetupUnitChr` at `0x793804`, which passes battle actor `+0x3AC` right after
setting the ordinary x/y/z scale from `+0x3A0`/`+0x3A4`, `FFX_AtelSys_Btl_191_resi` at
`0x7A6B24`, which writes the same actor field and pushes it through, and the script command
`FFX_AtelSys_Ch_110_resi 0xA79B30`, which pops one float. All replicated.

Caveat on the name: the reader is unambiguous, the callers are circumstantial. The battle
writers set it on unit CHRs that may or may not end up parented, so "attach scale" describes
what it *does* rather than necessarily what every caller was thinking.

---

## 7. What was deliberately not built

**No transform stream, no periodic corrective, no interpolation.** If the research had come back
"Bullet rigid body" all three would be here. It did not. There is no state to stream.

**Nothing calls `RequestHeldObjectHandover`.** This is a decision and it is the one thing in
this pass somebody might reasonably disagree with, so here is the reasoning in full.

To ask for a handover you have to know **which peer earned the pickup**, and that answer has to
be byte-identical on both machines. If it is not, one machine re-points the object and the other
does not, and that is an unrecoverable split. A sphere ending up in the wrong character's hands
is a cosmetic annoyance. Two machines disagreeing about where a sphere is, is the end of the
session.

The replicated source for it exists and is not exposed. `world/TriggerPass.cpp` runs one extra
trigger pass per remote player and knows which slot fired the examine, and because that pass is
a pure function of replicated positions and replicated buttons, its answer is identical on both
machines. Exposing it is one accessor in a file this pass was not allowed to edit.

The tempting shortcut is what `DialogueSync.cpp` does, which is to fall back to the local peer
when nobody asked through the remote pass. That is correct there, because the dialogue owner
only decides which replicated input stream gets injected into a box, and each machine injecting
its own is the intended behaviour. It would be exactly wrong here, because a re-point is a write
to the simulation.

So the seam is built, the applier is built, and the policy is not.

**`HeldObjectsHash` is not folded into the lockstep checksum**, because
`net/LockstepLink.cpp` was out of bounds. Until it is, a carry disagreement is visible in the
control panel and the ctrl+F11 log and nowhere else, which means somebody has to be looking.

**Contested pickup needed no machinery at all.** See section 9.

---

## 8. Files, and what needs wiring by hand

### New

- `loader/plugins/pilgrimage/world/HeldObjects.h`
- `loader/plugins/pilgrimage/world/HeldObjects.cpp`

### Kit additions

- `loader/workshop/include/ffx/Layout.h`. The whole carry block in `namespace Chr`: `ObjId`,
  `Parent`, `ParentJoint`, `AttachOffset`, `AttachScale`, `ParentUid`, `CharacterData`,
  `BoneWorldPos`. Plus a new `namespace ChrData` for the bone point table layout.
  **`Chr::Mode19C` is gone**, see section 9.
- `loader/workshop/include/ffx/Character.h` and `src/ffx/Character.cpp`. `CarryLink`,
  `ReadCarryLink`, `IsCarried`, `CharacterActorId`, `CarryApiReady`, `BoneIdForJoint`,
  `JointForBoneId`, `BoneWorldPosition`, `AttachToCarrierBone`, `AttachToCarrierJoint`,
  `DetachFromCarrier`.

### Minimal edits

- `net/NetLink.cpp`. `StartHeldObjects` in the session-active branch, `StopHeldObjects` in
  **both** stop sites, `StopNetworking` and the "was active, now is not" branch. Only the
  command half stops, the census keeps running.
- `PilgrimageMod.cpp`. `InstallHeldObjects`, non-fatal, with the consequence stated.
- `ui/ControlPanel.cpp`. A `held      : %s` readout line.
- `hooks/FrameHook.cpp`. The temporary service call and the ctrl+F11 log block.

### Parked addresses

In `namespace ffx::ParkedRva` at the top of `loader/workshop/src/ffx/Character.cpp`, because
`include/ffx/addresses/` was out of bounds. **They belong in `addresses/Character.h` and in its
`CharacterRvaList` so the startup check covers them.** All `__cdecl`, RVAs:

| RVA | VA | name | signature |
|---|---|---|---|
| `0x00432630` | `0x832630` | `FFX_Ch_AttachToParentBone` | `int (Character *, Character *carrier, int boneId)` |
| `0x00432680` | `0x832680` | `FFX_Ch_AttachToParentJoint` | `int (Character *, Character *carrier, int jointIndex)` |
| `0x004326E0` | `0x8326E0` | `FFX_Ch_AttachToParentOffset` | `CHRDATA *(Character *, Character *carrier, int joint, float ox, float oy, float oz)` |
| `0x00433A70` | `0x833A70` | `FFX_Ch_LookupBonePoint` | `int (Character *, int boneId, int *outJoint, float *outOffset4)` |
| `0x004354F0` | `0x8354F0` | `FFX_Ch_GetBoneWorldPos` | `float *(Character *, int boneId, float *out4)` |

`FFX_Ch_AttachToParentOffset` is parked without a wrapper, because nothing needs an explicit
offset yet and wrapping it would mean guessing the units.

### The three things that need a hand

1. **`ServiceHeldObjectStep` must be called once per simulation step from the lockstep gate**,
   next to `PrepareMenuInput` and `ServiceCoopConfigStep`. It consumes ordered commands, and an
   ordered command can only be consumed on the exact step it was stamped for, because
   `Lockstep::CommandsForStep` matches `step == currentStep` and `AdvanceStep` retires anything
   at or behind it. A consumer on the frame path drops one on every catch-up step. It is called
   from `FrameHook.cpp` meanwhile, with a comment saying it is temporary and why it is not
   enough. The census half is genuinely fine on the frame path, since it only reads.
2. **`HeldObjectsHash` wants folding into the game-state hash** the lockstep layer already
   exchanges.
3. **The five parked RVAs want moving** into `addresses/Character.h` and its `CharacterRvaList`.

---

## 9. Errors found in the notes I was handed

Every pass on this project so far has found a real mistake in the notes it was given. Six here,
and one of them is mine from an earlier pass if the Layout.h comment was mine.

**1. `Chr::Mode19C` in `ffx/Layout.h` was hiding `m_parent`.** It read
`const DWORD Mode19C = 0x19C; // dword, UpdateRenderJob branches on it`. It is `m_parent`, a CHR
pointer, and what `UpdateRenderJob` branches on is "do I have a parent". The name turned the
single most important field in this whole subsystem into an unknown, and
`reversing/FFX_GAME_NOTES.md` line 55 and `reversing/ffx_types.h` both already had it right as
`m_parent`, so the kit and the notes disagreed. Fixed.

**2. `COOP_DESIGN.md`'s contested-pickup story solves a problem the engine already solved.** It
says:

> Two players reach the same sphere on the same step. The commands sort by (step, peerId), the
> first one wins and takes ownership, and the second one finds the object already owned and is
> dropped.

There is no ownership-taking step, because the engine does the attach from a replicated script.
And contention is already resolved deterministically, twice over: the examine arbitration keeps
**one** winner slot at `ctx+488`, and `FFX_Atel_StartThreadIfChannelFree` refuses a second script
on a busy channel. Both machines run the same passes in the same order and reach the same
refusal. This is the same correction that was already applied to the chest paragraph a few
sections earlier in that file, and it was not carried down to this one.

**3. The three-class taxonomy over-counts, and class 3 is empty.** `COOP_DESIGN.md` lists
"continuous state whose owner changes" as "the only class that needs new machinery", with a
sphere carried through a Cloister of Trials as its example. **A carried object is not continuous
state.** It is a discrete attach that the engine re-derives every frame, so its one named
example is class 1. Class 3 may well have other members, but held objects are not them.

**4. "Pickup and drop become class 1 events" understates it.** They need no event either. The
script that does the pickup is itself replicated, so both machines attach on the same step with
no message at all. The command channel is needed for the *correction* of the bound-player slot,
not for the pickup.

**5. The "derived from object world positions" branch of the puzzle-state question is dead for
anything carried.** A carried object's `m_pos` is never written, so nothing can be derived from
it. See section 5.

**6. The brief pointed at `reversing/TRIGGERS.md`, which does not exist.** The file that
answers that question is `reversing/INTERACTION_PATH.md`, with `reversing/WORLD_STATE.md` for
the state side.

---

## 10. New in the IDB, and saved

Renames:

- `sub_832680` -> `FFX_Ch_AttachToParentJoint`
- `FFX_Ch_SetF1B4` -> `FFX_Ch_SetAttachScale`

Repeatable function comments added or extended, each carrying its own evidence:

`FFX_Ch_AttachToParentBone 0x832630`, `FFX_Ch_AttachToParentJoint 0x832680`,
`FFX_Ch_BuildSkinMatrices 0x832760`, `FFX_Ch_CopyState 0x828620`,
`FFX_Ch_LookupBonePoint 0x833A70`, `FFX_Ch_GetBoneWorldPos 0x8354F0`,
`FFX_Ch_UpdateBonePositions 0x8350F0`, `FFX_Ch_AllocateWithObjId 0x825650`,
`FFX_Ch_OrphanChildren 0x826150`, `FFX_Ch_SetAttachScale 0x832610`,
`FFX_AtelSys_Core_126_resi 0x85C580`, `FFX_AtelSys_Ch_044_resi 0xA79BC0`,
`FFX_Field_ChrRegTask 0x861850`.

The three ATEL syscalls were **not** renamed. Their positional names are the stable identity for
that table, because the (library, function) pair is the script ABI, and the real command names
are still unrecovered. The comments carry the argument order instead.

`FFX_AtelSys_Ch_045_resi 0xA79C50` and `FFX_AtelSys_Ch_082_resi 0xA787E0` already carried
"CO-OP:" comments from an earlier pass stating the bone-parent conclusion. Those were taken as a
hypothesis and re-derived from `FFX_Ch_BuildSkinMatrices`, not trusted. They hold up.

---

## 11. What is untested and most likely wrong, ranked

Nothing in here has run in a game. The build is clean at `/W4` with zero warnings and that is
all it is.

1. **The handover is wired to nothing, so the entire command path is unexercised.** Not one line
   of `ApplyHandover` has run. The payload round trip, the map refusal, the bone translation and
   the attach call are all guesses that compile. This is the biggest gap and it is deliberate,
   but it means a first test will be the first test of everything at once.
2. **Calling `FFX_Ch_AttachToParentBone` from outside the engine's own paths.** The guarded
   wrapper handles the null `m_data` that would fault inside `FFX_Ch_LookupBonePoint`, and
   nothing about the attach looks order-sensitive, but it is being called from a hook rather than
   from the field task or a script. If it misbehaves, suspect the ordering in
   `FFX_Ch_DispatchInBatches`: the parentless CHRs are dispatched by
   `FFX_Ch_UpdateMotionAll` with mode 0 and the parented ones by `DispatchInBatches` with mode 1,
   so re-parenting *after* that split has run for the step could leave the object one frame
   behind. That is a visual glitch rather than a desync, and it has not been tested.
3. **`MarkCharacterDirty` after the attach may be wrong, unnecessary, or both.** It is there
   because `FFX_Atel_SetActorPos` does it after a position write and an attach is a position
   change by another route. It is cheap unless the object has part vertex buffers, in which case
   it recopies every part's skinned vertices. Nobody has measured whether it is needed.
4. **The 22-slot bone id numbering being shared across skeletons.** The evidence is good: the
   array is indexed by the bone id field and `FFX_Ch_UpdateBonePositions` asks for ids 0, 1, 7
   and 8 by hand. But "shared across skeletons" is inferred from that, not demonstrated. If
   Tidus's bone 5 and Wakka's bone 5 are different body parts, a handover puts the sphere in the
   wrong place. `JointForBoneId` would still refuse an id the target has no record for, so the
   failure is cosmetic rather than a crash.
5. **The map id as a scoping key.** `ffx::ReadWorldLocation` reads the live map id at save block
   `+0x00`, and a handover is refused when it does not match the command's. Whether that field
   has already moved to the new map at the moment a stale command would land has not been
   checked. If it updates late, a cross-transition handover could slip through.
6. **The census hash depending on pool slot order.** Argued in section 4 and I believe it, but
   if the two machines ever allocate CHRs in a different order the hash will report a desync that
   is really an upstream one, which is confusing even though it is technically correct. Should
   that happen, the fix is to find the real divergence rather than to sort the census.
7. **`kMaxTrackedCarries` at 48.** A field map runs about 36 actors so this should be plenty,
   but a battle with several summons and weapons has not been counted. An overflow refuses the
   whole census, which is loud rather than silent, so the failure is safe.
8. **`CHR+0x1B4` being the attach scale.** Section 6. The reader is unambiguous, the name is
   circumstantial.
9. **That a shipped FFX script actually uses one of the three attach syscalls.** The mechanism is
   proved from the binary. No `.ebp` was decoded in this pass, so "the Cloister sphere script
   calls library 5 function 45" is an inference from the syscall existing and from
   `FFX_Field_ChrRegTask` using the C-side equivalent for weapons. Running `tools/ebp.py` over a
   Cloister map and grepping for syscall operands `(5, 44)`, `(5, 45)` and `(5, 82)` would settle
   it, and would also say which bone ids real content uses.
