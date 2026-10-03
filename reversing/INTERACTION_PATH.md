# FFX world interaction path, notes for co-op

All addresses are VA. Base is 0x400000, so RVA + 0x400000 = VA.
Everything here was read out of Hex-Rays pseudocode unless a line says it is inference.

## 1. The trigger set

The dispatch hub is `FFX_Atel_StepActor` at **0x8666E0**. It switches on the actor kind
byte at `**actor` (actor+0 is a pointer, the first byte of what it points at is the kind).
Five kinds are handled:

| kind | handler | VA | position it reads | single player gate |
|---|---|---|---|---|
| 1 | character actor, then `FFX_Atel_TestPlayerProximity` | 0x866980 | `g_ffxAtelCtx+536/540/544` only | YES, hard-wired to the bound player, plus a one-slot nearest-target cache at ctx+488 |
| 2 | `FFX_Atel_StepLineTrigger` | 0x8684B0 | bound player actor pos, ctx+536 and +552 | YES, `actor->id == *(u16*)(g_ffxAtelCtx+10)` |
| 3 | `FFX_Atel_StepBoxTrigger` | 0x866CC0 | bound player actor pos, ctx+536 and +552 | YES, same id gate |
| 5 | `FFX_Atel_StepPathTrigger` (was sub_868020) | 0x868020 | per-actor target id, see below | NO id gate |
| 6 | `FFX_Atel_StepVolumeTrigger` (was sub_867C10) | 0x867C10 | per-actor target id at `pos+40`, falls back to ctx+552/+536 | NO id gate |

Kind 4 exists as a kind value but is not stepped here, and its position sub-struct pointer
is computed as null in every one of these functions.

Position sub-struct offset inside the actor depends on the kind:
- kinds 5 and 6: `actor + 644`
- kinds 1, 2, 3: `actor + 1368`
- kind 4: null

Fields inside that sub-struct that the triggers use:
- +0, +4, +8    previous position x y z
- +16, +20, +24 current position x y z
- +28           Y half height, 0 or less means do not gate on Y
- +40 (u16)     TARGET ACTOR ID, 0xFFFF means use the global bound player
- +48           X half extent
- +52           rotation about Y, used as `-value` for the sin/cos
- +56           Z half extent
- +72           radius, squared before compare

### The important asymmetry

Kinds 5 and 6 already carry their own target actor id at `pos+40`. They are per-character
capable today. Kinds 1, 2 and 3 do not, they read the global bound player only.

So a fix has to cover three handlers, not two: the line trigger, the box trigger, and the
proximity/examine test. That last one is the one that matters most for chests and NPCs.

## 2. The deliberate fire entry point

There is one, and it takes the actor id as a parameter.

`FFX_Atel_FireActorEvent` at **0x8764F0**, `__cdecl`:

    int FFX_Atel_FireActorEvent(int caller_actor_id, int target_actor_id, int event_kind)

Pass `0xFFFF` as `caller_actor_id` for "nobody in particular called this", which is what
every trigger stepper does. What it does, in order:

1. `FFX_Atel_GetActor(target_actor_id)` at 0x86... (see below) to get the actor record
2. `FFX_Atel_GetActorKind(target_actor_id)` 0x86A7B0, returns the kind byte, or 7 if the
   id is out of range (range check is against `*(u16*)(g_ffxAtelCtx+12)`, the actor count)
3. `FFX_Atel_GetEventChannel(kind, event_kind)` 0x876860, per-kind lookup tables at
   `byte_C52AC0` (kind 1), `byte_C52AC8` (kinds 2 and 3), `byte_C52AD0` (kinds 5 and 6).
   Returns 255 for "this kind has no channel for that event".
4. `FFX_Atel_GetEventScriptEntry(kind, event_kind, *(u16**)(actor+68))` 0x8763E0. If the
   actor has its own override table at actor+68 it indexes that, otherwise it falls back to
   per-kind default tables at `unk_C52AE8` (kinds 2 and 3), `unk_C52AF8` (kind 5),
   `unk_C52B08` (kind 6), `unk_C52AD8` (everything else). Returns 0xFFFF for none.
5. Gate: `(1 << event_kind) & *(u8*)(actor+171)`, a per-actor event enable bitmask.
6. `sub_86EBA0(caller_actor_id, target_actor_id, 0, channel, script_entry)` actually starts
   the thread.

Returns 0 if nothing started.

Sibling: `FFX_Atel_FireActorEventSync` at **0x876590**, identical except it calls
`FFX_Atel_StartThreadDeferred` (0x86EC40 -> sub_86EC80) instead of sub_86EBA0. The volume
trigger stepper uses this one.

Event kinds seen so far: 3 = proximity/examine in range, 5 = volume enter, 6 = volume leave.
Full list still being worked out.

## 3. The bound actor

`*(u16*)(g_ffxAtelCtx + 10)` is the id of the actor the triggers test. Writers and all
readers still being enumerated.


---

## CORRECTIONS to sections 1 to 3 above

Read these before you trust anything above this line. Three of the claims in the first
three sections are wrong or imprecise, and one of them changes the whole co-op plan.

### 3.0 g_ffxAtelCtx is a POINTER SLOT, not the struct

`g_ffxAtelCtx` at **0x1326B28** is a 4-byte variable holding a pointer. The contexts
themselves live in `g_ffxAtelCtxArray` at **0x1325BA0**, 568 bytes each. Seven of them fit
between the array base and the pointer slot (568 * 7 = 3976 = 0xF88 = 0x1326B28 - 0x1325BA0),
so treat 7 as the context count.

Everywhere Hex-Rays prints `*(u16 *)(g_ffxAtelCtx + 10)` it means "load the pointer, then
read +10". The asm is `mov eax, g_ffxAtelCtx` then `movzx ..., [eax+0Ah]`. A mod must do the
same double indirection. Reading 0x1326B32 directly reads the wrong thing.

Two tiny helpers do the context swap and they bracket almost every public ATEL entry point:

    int __cdecl FFX_Atel_SelectContext(int index)   0x862D30
        returns the old pointer, sets g_ffxAtelCtx = &g_ffxAtelCtxArray[568 * index]
    int __cdecl FFX_Atel_RestoreContext(int saved)  0x86F730
        returns the old pointer, sets g_ffxAtelCtx = saved

`FFX_Atel_FireActorEvent` already does `SelectContext(0)` on entry and `RestoreContext` on
exit, so calling it from a frame hook does not depend on which context happens to be
selected. That matters, see section 8.

### 3.1 Kinds 2 and 3 are NOT gated on an actor id compare. THIS IS THE GOOD NEWS.

Section 1 says the line and box triggers are gated on `actor->id == *(u16*)(ctx+10)`. They
are not. `FFX_Atel_StepActor` 0x8666E0 resolves the bound player ONCE and passes the player
actor POINTER as the third argument:

    v3 = *(u16 *)(g_ffxAtelCtx + 10);          // the bound player actor id
    switch ( **(BYTE **)actor ) {
    case 1: ... if (v3 != 0xFFFF) FFX_Atel_TestPlayerProximity(actor, dt, GetActor(v3));
    case 2:    if (v3 != 0xFFFF) FFX_Atel_StepLineTrigger     (actor, dt, GetActor(v3));
    case 3:    if (v3 != 0xFFFF) FFX_Atel_StepBoxTrigger      (actor, dt, GetActor(v3));
    case 5:    FFX_Atel_StepPathTrigger  (actor, dt);          // no player arg at all
    case 6:    FFX_Atel_StepVolumeTrigger(actor);              // no player arg at all
    }

The only gate is "is anybody bound at all". So all three handlers are already callable with
an arbitrary player actor pointer. A mod does not have to flip ctx+10 to run the trigger set
against a second character. It can call the three steppers itself with the client's actor.

The catch, and it is the whole remaining problem, is in 3.2.

### 3.2 The player POSITION is cached in the context, the player FACING is not

`FFX_Atel_TestPlayerProximity` 0x866980 takes the player actor as `a3` but only ever reads
ONE field out of it: `a3->pos[+52]`, the rotation about Y, which it hands to
`FFX_Atel_TestFacingCone`. Every position read is from the context cache:

    ctx+536 / +540 / +544    player current x y z
    ctx+552 / +556 / +560    player previous x y z   (the swept segment's start)

`FFX_Atel_StepLineTrigger` and `FFX_Atel_StepBoxTrigger` are the same: the player actor
argument supplies only the facing, the positions come from ctx+536 and ctx+552.

So the per-player pass is: write the client's current and previous position into
ctx+536..544 and ctx+552..560, call the stepper with the client's actor pointer, and put the
host's six floats back. That is a six-float save and restore, not a code patch.

Who fills ctx+536 normally: `FFX_Atel_PullActorPosFromChr` 0x869E40, which runs per actor
and copies the position out of the CHR, and whose write is gated on
`actor->id == *(u16*)(ctx+10)`. It also copies current into previous when the actor was
teleported. `FFX_Atel_SetActorPos` 0x870B20 and `FFX_Atel_SetActorPosXZ` 0x870970 do the
same under the same id gate, and `FFX_Field_BootDoneTask` 0x861480 seeds previous from
current once on map load.

### 3.3 sub_86EBA0 already has a name

It is `FFX_Atel_StartThreadByChannel` in the IDB. See section 8.

## 4. Section 3 finished: every writer and reader of the bound actor id

Method: decompiled all 99 functions that reference `g_ffxAtelCtx` and matched
`g_ffxAtelCtx + 10)`. That is exhaustive for this variable, because there is no other way to
reach the field.

**Exactly one writer.**

| VA | function | what it does |
|---|---|---|
| 0x86F6A0 | `FFX_Atel_SetPlayerActorId(int actorId)` | the only `*(u16*)(ctx+10) = id` in the binary |

It has two callers: the ATEL script syscall that reassigns the player actor, and
`FFX_Atel_SetControlledActor` 0x86F580. After storing the id it resolves that actor's
position sub-struct and writes the same xyz into BOTH the current slot (ctx+536..544) and
the previous slot (ctx+552..560), so the swap itself cannot look like movement.

That last part is the trap for a one-frame swap: it destroys the previous position, so any
swept trigger crossing in flight is lost on the swap frame. If you swap, write ctx+10 by
hand and fill the six floats yourself rather than calling the setter.

**Seven readers.**

| VA | function | what it uses it for |
|---|---|---|
| 0x8666E0 | `FFX_Atel_StepActor` | resolves the player actor and passes it to the three handlers |
| 0x869E40 | `FFX_Atel_PullActorPosFromChr` | gate: only the bound actor's CHR position reaches ctx+536 |
| 0x86C1A0 | `FFX_Atel_GetPlayerActorId` | the public getter, returns -1 for 0xFFFF |
| 0x86F580 | `FFX_Atel_SetControlledActor` | stashes the outgoing id into ctx+14 so it can be put back |
| 0x870970 | `FFX_Atel_SetActorPosXZ` | gate: refresh ctx+536 and ctx+552 when the bound actor is moved |
| 0x870B20 | `FFX_Atel_SetActorPos` | same gate |
| 0x871BC0 | `FFX_Atel_StepFieldFrame` | gate: only step the field at all when somebody is bound |

**What flipping ctx+10 for one frame perturbs.** Only three things, and all three are
recoverable:

1. Which actor's CHR position lands in ctx+536 that frame. You are overwriting those floats
   anyway for a per-player pass.
2. `FFX_Atel_GetPlayerActorId`, which the ATEL script opcodes use. If a script runs during
   your swapped window it sees the wrong player. Scripts run from
   `FFX_Atel_RunScript` inside the actor loop, which is inside the window, so DO NOT swap
   across the actor loop. Swap around a single stepper call or not at all.
3. ctx+14, but only if you go through `FFX_Atel_SetControlledActor`.

Conclusion: you do not need to flip it. 3.1 means the steppers take the player as an
argument, so the cheaper and safer move is "leave ctx+10 alone, swap the six position floats,
call the stepper with the other actor".

## 5. The complete event kind list

There are exactly 8 event kinds, 0 to 7. The proof is the table stride:
`byte_C52AC0`, `byte_C52AC8` and `byte_C52AD0` are 8 bytes apart, and
`unk_C52AD8`, `unk_C52AE8`, `unk_C52AF8`, `unk_C52B08` are 16 bytes apart, which is 8 words.
`FFX_Atel_GetEventChannel` and `FFX_Atel_GetEventScriptEntry` index both with the event kind
and NEITHER bounds-checks it. **Passing an event kind above 7 reads off the end of the
table.** A kit wrapper has to clamp.

Channel tables, 255 meaning "this actor kind has no channel for that event":

| event | kind 1 `byte_C52AC0` | kinds 2,3 `byte_C52AC8` | kinds 5,6 `byte_C52AD0` |
|---|---|---|---|
| 0 | 7 | 7 | 1 |
| 1 | 7 | 7 | 1 |
| 2 | 6 | none | none |
| 3 | 5 | 3 | 1 |
| 4 | none | 5 | 1 |
| 5 | none | 4 | 1 |
| 6 | none | 2 | 1 |
| 7 | none | none | none |

Default script entry tables, 0xFFFF meaning none. The actor's own override table at
`actor+68` wins over all of these when it is non-null:

| event | `unk_C52AD8` kind 1 and default | `unk_C52AE8` kinds 2,3 | `unk_C52AF8` kind 5 | `unk_C52B08` kind 6 |
|---|---|---|---|---|
| 0 | 2 | 2 | 2 | 2 |
| 1 | 3 | 3 | 2 | 2 |
| 2 | 4 | none | none | none |
| 3 | 5 | 5 | 2 | 2 |
| 4 | none | 4 | 2 | 2 |
| 5 | none | 6 | 2 | 2 |
| 6 | none | 7 | 2 | 3 |
| 7 | 1 | 1 | 1 | 1 |

Event 7 has a script entry in every table but a channel in none, so `FFX_Atel_FireActorEvent`
can never start it. Script entry 1 is reached some other way, presumably the actor's main
body.

Names, each taken from an actual call site rather than guessed:

| kind | name | fired from | meaning |
|---|---|---|---|
| 0 | **Examine / confirm** | `FFX_Atel_StepFrame` 0x867950 commit | the player pressed the confirm button while this actor was the nearest examinable one. **This is the chest.** |
| 1 | **Examine alternate** | `FFX_Atel_StepFrame` commit | same, but the other of the two examine buttons (pad bit 0x80 rather than 0x20) |
| 2 | **Collision bump** | `FFX_Ch_ResolveCollisionsAll` 0x83D753 via `FFX_Atel_FireEventOnChrActor` 0x8764A0 | two CHRs physically collided. Only actor kind 1 has a channel for it |
| 3 | **Proximity in range** | `FFX_Atel_TestPlayerProximity`, `FFX_Atel_StepLineTrigger`, `FFX_Atel_StepBoxTrigger` | the player is inside my radius this frame. Distance only, NO button |
| 4 | **Touch / cross** | `FFX_Atel_StepLineTrigger`, `FFX_Atel_StepBoxTrigger`, `sub_8680A0` (the path trigger link step) | the swept player segment crossed me |
| 5 | **Enter** | line, box and volume triggers | the player went from outside to inside |
| 6 | **Leave** | line, box and volume triggers | the player went from inside to outside |
| 7 | unreachable through FireActorEvent | - | has a script entry, never has a channel |

## 6. Which event kind opens a chest

**Event kind 0, and there is no chest-specific code anywhere.** The chain, end to end:

1. `FFX_Atel_StepFrame` 0x867950, at the top of the frame, clears ctx+484 and samples the
   pad. If the examine mask has bit 0x20 or 0x80 set it arms a scan:
   `ctx+486 = (mask & 0x20) == 0` (the event kind to fire, 0 or 1),
   `ctx+484 = 1`, `ctx+488 = 0xFFFF`.
2. The actor loop runs `FFX_Atel_RunScript` for every actor, which reaches
   `FFX_Atel_StepActor` and therefore the three player-aware handlers. Each of them, when
   armed, competes for the single winner slot: closest actor inside
   `(ctx+496 + pos[+64])^2` that also passes `FFX_Atel_TestFacingCone` wins ctx+488, with
   its squared distance in ctx+492.
3. The commit, still in `FFX_Atel_StepFrame`: if `ctx+484 == 1` and `ctx+488 != 0xFFFF`,
   call `FFX_Atel_GrantTalkBonusOnce(winner, ctx+486)` then
   `FFX_Atel_FireActorEvent(0xFFFF, ctx+488, ctx+486)`.
4. That starts the actor's script at entry 2 (event 0) or 3 (event 1) on channel 7.
5. The script eventually executes an ATEL syscall. The give-treasure syscalls are
   **`FFX_AtelSys_Core_347_start` 0x85A8A0** (normal) and
   **`FFX_AtelSys_Core_423_start` 0x857B70** (silent). Their ONLY xrefs are the syscall
   dispatch tables at 0xC51600 and 0xC51AC0 in .data. No C code calls them. They are
   script-only.

So a chest is an ordinary examinable ATEL actor, the treasure lives in its script, and the
one engine decision that makes it happen is "who is in ctx+488 when the confirm press is
committed". For co-op that reduces to a single sentence: **make the host call
`FFX_Atel_FireActorEvent(0xFFFF, chestActorId, 0)`**. Everything downstream, the chest
opening animation, the item, the message box, is the script's own business and runs
identically whoever asked for it.

What it does NOT do: the script is the thing that calls AddItem, so the item lands once per
machine that runs the script. On a host-authoritative design only the host should fire the
event and the item then replicates through the save-block sync. Firing it on both machines
double-grants.

## 7. The one-slot caches, and what to do about each

| what | where | breaks a second player? | cheapest fix |
|---|---|---|---|
| player current position | ctx+536/540/544 | yes, every distance test reads it | save, overwrite, call, restore. Six floats |
| player previous position | ctx+552/556/560 | yes, every swept test reads it | same six floats |
| player interaction radius | ctx+496 | only if the two characters have different radii | leave it, or set it per pass |
| examine scan armed | ctx+484 (u8) | yes, one flag for the whole frame | set it yourself for your own pass, clear it after |
| examine event kind | ctx+486 (u8) | yes, one button kind for the whole frame | same |
| nearest examinable winner | ctx+488 (u16) | **yes, this is the real one** | it is cleared at the start of every scan, so a second scan is free. Run the whole arm-scan-commit three times, once per character |
| winner squared distance | ctx+492 (float) | yes, paired with ctx+488 | cleared implicitly when ctx+488 is 0xFFFF |
| bound player actor id | ctx+10 (u16) | no, see 3.1. The steppers take the player as a parameter | leave it alone |
| `FFX_Atel_GetActor` result cache | `dword_13270E4` ptr, `word_13270E0` id, `word_13270DC` ctx+26 | no, but see below | nothing to do |
| out of range counter | `dword_13270D8` | no | nothing |
| ATEL pad masks | `g_ffxAtelPadPressed` pressed port 0, `g_ffxAtelPadPort0Buttons`, `g_ffxAtelPadPort1Buttons` | yes, one mask | see section 9 |
| **player was inside me last frame** | **actor+0x34 bit 0x040** | **yes, and this one was missed below** | save, swap, restore, masked. One shadow word per actor per player |
| **box trigger fully inside** | **actor+0x34 bit 0x100** | **yes** | same shadow word |
| **line crossing direction and force-retest** | **actor+0x36 bits 0x007** | **yes** | same shadow word |

The `FFX_Atel_GetActor` cache at 0x13270E4 deserves a warning even though it is benign for
the position work. Its key is `(ctx+26, actorId)` and **it does not include the context
pointer**. Two contexts with the same value at +26 asking for the same actor id would get
each other's actor. Every public ATEL getter avoids this by selecting context 0 first, which
is why the kit wrappers must do the same rather than touching the pool directly.

**CORRECTED. The verdict below was wrong and the three bolded rows above are why.**

The original verdict said the position cache and the examine winner slot were the only two
things to deal with. That missed the per-ACTOR state. All three steppers keep "the player was
inside me last frame" on the actor and read it back at the top to decide enter against leave.
It is one slot per actor, not one per player.

What that costs if you ignore it: a host standing inside a trigger while a remote player stands
outside fires a spurious enter and leave PAIR every single frame, because each pass reads the
other player's bit as its own previous state. A script firing every frame on a trigger you are
merely standing in is not a subtle desync, it is immediately visible.

The fix is the same shape as the position cache, just on the actor rather than the context, and
it is still cheap. Each player gets a shadow word per actor. Before stepping actor A for player
P, write P's shadow into A. After, read it back out. The host's bits go back at the end of the
whole pass. Four bytes per actor per player, so 512 actors times 3 players is 6 KB.

**The masks matter more than the mechanism.** Only bits 0x0140 of actor+0x34 and 0x0007 of
actor+0x36 belong to the steppers. Everything else in those two words is the engine's, including
the step gate at 0x80 that `FFX_Atel_StepActor` tests and the 0x180 / 0x600 / 0x1000 script
bookkeeping that `FFX_Atel_StepFrame`'s second actor loop rewrites. Save and restore those two
words wholesale and you will stop actors stepping.

Corrected verdict: four things have to be dealt with, not two. The position cache, the examine
winner slot, the player actor's facing float, and the per-actor edge bits. None of them needs a
code patch, all four are save-use-restore, and the implementation is
`ffx::SaveAtelPass` / `SetAtelPassPlayer` / `ActorTriggerState` / `StepActorTriggers` in the kit
with `plugins\pilgrimage\world\TriggerPass.cpp` driving it.

Two things confirmed while building it, both worth having:

- **The player actor parameter contributes exactly one field**, `motionState[13]` at +52, the Y
  rotation handed to `FFX_Atel_TestFacingCone`, and only on the examine-scan path. So a remote
  player needs **no ATEL actor of its own at all**. Swap that one float on the bound player's
  actor and hand the steppers the bound player's pointer. This removes what looked like the
  hardest dependency, which was giving a spawned clone a real ATEL actor.
- **The delta time argument is never read** by any of the three. Checked at the disassembly
  level rather than taken from the decompiler: zero references to `[ebp+0Ch]` in all three
  functions. So a caller outside the engine does not have to source a correct dt.

## 8. Firing an event from outside the normal path

`FFX_Atel_FireActorEvent` -> `FFX_Atel_StartThreadByChannel` 0x86EBA0 (already named in the
IDB) -> `FFX_Atel_StartThreadIfChannelFree` 0x86EBE0 -> `FFX_Atel_CreateThread` 0x86EA00.

**Reentrancy.** `FFX_Atel_StartThreadIfChannelFree` walks the actor's own thread list at
`actor+128` and refuses to start if any thread on the list is both not in state 3 (finished)
and on the requested channel (`thread+14 & 0xF`). So double-firing the same event on the same
actor is already a no-op while the first script is still running. Spamming the command over
the network cannot stack scripts on one chest.

**Allocation.** `FFX_Atel_CreateThread` takes a thread off the free list at `actor+136` and
returns 0 if it is empty. There is no global concurrent-thread ceiling in this path, the
ceiling is per actor and is however many thread records that actor was allocated. A failed
start is a quiet 0, not a fault.

**State assumptions.** `FFX_Atel_CreateThread` reads `ctx+504` to decide whether the caller
supplied argument words or whether to pull them off the actor at +360/+362/+364, and reads
`ctx+26` for a thread-priority tweak. `FFX_Atel_FireActorEvent` wraps everything in
`SelectContext(0)` / `RestoreContext`, so it does not care which context was selected when
you called it. It does NOT check that the field state machine is running, that a map is
loaded, or that the actor pool is populated.

**The actual danger, and it is a real one.** `FFX_Atel_FireActorEvent` computes
`FFX_Atel_GetActor(targetId)` and then dereferences `actor+68` and `actor+171`
UNCONDITIONALLY, before the channel check can reject anything. `FFX_Atel_GetActorKind` does
bounds-check and returns kind 7 for an out of range id, and kind 7 has no channel, so nothing
fires, but the two dereferences already happened on a pointer `FFX_Atel_GetActor` computed
from a clamped index. Do not call it with no map loaded and do not call it with an id >=
`ctx+12`. A kit wrapper MUST bounds-check the actor id itself.

**Is a frame hook safe?** Honestly: probably, with one caveat I cannot settle by reading.
The thread it creates is not run by `FFX_Atel_FireActorEvent`, it is linked onto the actor's
list and executed by the next `FFX_Atel_RunScript` pass inside `FFX_Atel_StepFrame`. So
firing from an animate hook before `FFX_MainStep` behaves exactly like firing from the
commit point one frame earlier. What I cannot rule out by inspection is whether a script
started while the engine is mid-transition (a fade, a map load, a battle hand-off) sees a
half-built world, because `FFX_Atel_StepFieldFrame` 0x871BC0 has its own gates on ctx+528
bit 2 and ctx+500 that the fire path does not consult. The safe shape, and the one the kit
exposes, is to fire from a hook that runs at the same point in the frame the engine would
have, and to refuse when the actor count is zero or the id is out of range.
UNTESTED IN A RUNNING GAME.

## 9. The examine button is read OUTSIDE the proximity test, once, from one global

This was question 6 and the answer is the cheap one.

`FFX_Atel_TestPlayerProximity` reads no input at all. Event kind 3 fires on distance alone.
The button is read exactly once per frame, at the top of `FFX_Atel_StepFrame` 0x867950:

    v7 = FFX_Pad__remapButtonMask(0, 0, g_ffxAtelPadPressed);
    FFX_Pad__remapButtonMask(0, 0, g_ffxAtelPadPort0Buttons);   // result thrown away
    FFX_Pad__remapButtonMask(0, 0, g_ffxAtelPadPort1Buttons);   // result thrown away
    ...
    if ( (v7 & 0xA0) != 0 && sub_86C8B0() == 0 && (ctx[0] & 0x22) != 0 && dword_C526D4 )
    {
        *(u8  *)(ctx + 486) = (v7 & 0x20) == 0;
        *(u8  *)(ctx + 484) = 1;
        *(u16 *)(ctx + 488) = 0xFFFF;
    }

`g_ffxAtelPadPressed` (was `word_13270D0`) is at **0x13270D0** and is the ATEL layer's own edge-PRESSED mask. It is
filled by `FFX_Atel__samplePadsBothPorts` 0x871D70, which ORs
`FFX_Pad__readPressed16(0, 0, i)` over the whole input ring. **Port 0 only.**

The two port-aware globals next to it are a dead PS2 two-port API:

    g_ffxAtelPadPort0Buttons  0x13270C0   OR of FFX_Pad__readButtons16(0, ...)   HELD, not pressed
    g_ffxAtelPadPort1Buttons  0x13270C4   OR of FFX_Pad__readButtons16(1, ...)   HELD, not pressed

`FFX_Atel_StepFrame` remaps both and discards both results. So the engine samples port 1 and
throws it away.

Three consequences for "the client opens a chest", in increasing order of how much work they
are:

1. **The button read is not the hard part.** There is no per-player input plumbing to build.
   The whole press-to-examine decision is one 16-bit global and one armed flag.
2. **You do not have to touch the pad at all.** The commit is just
   `FFX_Atel_FireActorEvent(0xFFFF, actorId, 0)`. A network command that carries an actor id
   and an event kind reproduces the entire local path with no input involved.
3. **If you want the client's own scan rather than a client-chosen actor id**, you run the
   arm-scan-commit sequence a second time with the client's six position floats in
   ctx+536/552 and the client's actor as the player argument. ctx+488 is cleared at the
   start of each scan, so the second pass costs nothing but the actor loop.

`sub_86C8B0` is the other gate on arming the scan. It walks eight 44-byte records inside a
352-byte-strided table at `unk_1326D70` selected by `byte_1326B82`, and returns non-zero when
a message window is up or busy. That is the "do not examine while a text box is open" check,
so a co-op command executor should consult it too rather than firing into an open dialogue.

## 10. The ATEL actor to CHR link, both directions

Found it. Five fields on the actor, all confirmed from code that uses them:

| offset | field |
|---|---|
| +46 (0x2E) | u16 actor id, the thing `FFX_Atel_FireActorEvent` takes |
| +64 (0x40) | int party character index, 0..7, or -1 / 255 for "not a party member" |
| +156 (0x9C) | `CHR *`, the live character object |
| +170 (0xAA) | u8 sub-kind, 1 means "this actor is backed by a CHR" |
| +171 (0xAB) | u8 per-event enable bitmask, bit N enables event kind N |

And three game functions that walk them:

    int __cdecl FFX_Atel_GetActorChrById   (int actorId)   0x86B3A0  -> CHR *, actor+156
    int __cdecl FFX_Atel_FindActorIdByChr  (int chrPtr)    0x876440  -> actor id, or -1
    int __cdecl FFX_Atel_FindActorByPartyChar(int charIdx) 0x86A470  -> actor id, or -1

`FFX_Atel_FindActorIdByChr` is a linear scan requiring `actor+170 == 1 && actor+156 == chr`.
`FFX_Atel_FindActorByPartyChar` is a linear scan for `actor+64 == charIndex`.

For co-op the useful one is `FFX_Atel_FindActorByPartyChar`, because it goes straight from a
character index, which is what the ownership table is keyed on, to the actor id that
`FFX_Atel_FireActorEvent` wants. That is the missing piece: given "player 2 owns Yuna" and a
character index for Yuna, you get her ATEL actor id in one call, and then every per-character
trigger pass has its player argument.

Caveat worth knowing: `FFX_Atel_FindActorByPartyChar` 0x86A470 has `FFX_Atel_GetActor`
inlined into it and does NOT select context 0. It reads whatever `g_ffxAtelCtx` points at.
From a frame hook that is context 0 in practice, but the kit wrapper selects context 0
explicitly rather than trusting it.

The actor pool layout, which is inference from the strides in `FFX_Atel_GetActor` cross
checked against the position sub-struct offsets, is:

    [2904 bytes x (ctx+12 - ctx+20 - ctx+22 - ctx+24)]   kind 1 character actors, pos at +1368
    [1464 bytes x ctx+20]                                 kinds 2 and 3,          pos at +1368
    [1368 bytes x ctx+22]                                 kind 4,                 no pos
    [ 744 bytes x ctx+24]                                 kinds 5 and 6,          pos at +644
    [  48 bytes x the rest]                               minimal actors

The assignment of a stride group to an actor kind is INFERENCE: a kind 2 or 3 actor needs
1368 + 76 bytes for its position sub-struct so it cannot be in the 1368 group, and a kind 5
or 6 actor needs 644 + 76 so it fits the 744 group exactly. ctx+16 is the pool capacity and
`FFX_Atel_GetActor` clamps to index 0 when the id is out of range, bumping `dword_13270D8`.

## 11. Context field map, as far as it is known

568 bytes per context, base `g_ffxAtelCtxArray` 0x1325BA0, selected through the pointer at
`g_ffxAtelCtx` 0x1326B28.

| offset | type | meaning |
|---|---|---|
| +0 | u8 | flags. bits 0x02 and 0x20 both mean "a player actor is bound", 0x04 and 0x10 are per-frame scratch |
| +2 | u8 | flags, bit 0x08 and 0x10 used by the script layer |
| +8 | u8 | ATEL state byte, 3 means "a script asked to restart the frame" |
| +10 | u16 | **bound player actor id**, 0xFFFF for none |
| +12 | u16 | total actor count |
| +14 | u16 | the previous bound player actor id, saved by `FFX_Atel_SetControlledActor` |
| +16 | u16 | actor pool capacity, the bound used by `FFX_Atel_GetActor` |
| +20 | u16 | count of 1464-byte actors |
| +22 | u16 | count of 1368-byte actors |
| +24 | u16 | count of 744-byte actors |
| +26 | u16 | context generation, part of the `FFX_Atel_GetActor` cache key |
| +28 | ptr | actor pool base |
| +92 | ptr | the per-actor step callback, set to `FFX_Atel_StepActor` |
| +484 | u8 | examine scan armed for this frame |
| +486 | u8 | which event kind the commit will fire, 0 or 1 |
| +488 | u16 | **nearest examinable actor id**, 0xFFFF for none |
| +490 | u8 | a 3-state counter, set to 3 by `FFX_Field_ChrRegTask`, consumed by `sub_871F30` |
| +492 | float | squared distance of the ctx+488 winner |
| +496 | float | the player's own interaction radius, added to the actor's |
| +500 | u32 | a frame or script counter, incremented by `sub_8688F0` |
| +504 | u32 | non-zero means `FFX_Atel_CreateThread` takes its argument words from the caller |
| +520 | float | read by `sub_871E50` |
| +528 | u32 | field-frame gate flags, bit 2 tested by `FFX_Atel_StepFieldFrame` |
| +536 +540 +544 | float | **player current x y z** |
| +552 +556 +560 | float | **player previous x y z** |

## 12. The shortest honest path to "the client can open a chest"

In order, each step independently testable:

1. Host side only. From a frame hook, call
   `ffx::FireActorEvent(chestActorId, ffx::kAtelEventExamine)`. If that opens a chest the
   local player is standing next to, the command executor works and nothing else in this
   document matters for step 1.
2. Get the client an actor id to send. Either the client sends its position and the host runs
   `ffx::FindActorsNear` plus `ffx::ActorCanFireEvent`, or the client runs
   `FindActorsNear` locally against its own replicated actor list and sends the id. The
   second is less round trip and the id is stable within a map.
3. Gate it. `ffx::ActorCanFireEvent(actorId, kAtelEventExamine)` reproduces all three engine
   gates, so the client can grey out an unusable prompt without asking the host.
4. Only if you want the engine's own facing-cone arbitration rather than your own radius
   check: add the per-player scan. Save ctx+536..560, write the client's current and previous
   position, set ctx+484 and ctx+486, clear ctx+488, run the actor loop, read ctx+488,
   restore the floats. That is section 7's save-and-restore and it is the only part that
   needs the context writes.

Steps 1 to 3 need no engine patching at all, only reads plus one call. Step 4 is optional
polish. The item duplication question is a design decision, not a reversing one: the script
calls AddItem, so exactly one machine must fire the event.

## 13. Two late corrections to this document itself

Both found after sections 4 to 12 were written, so they override what is above.

### 13.1 Only ATEL contexts 0 and 1 read the controller

Section 9 says the pad is read at the top of `FFX_Atel_StepFrame`. True, but the read is
gated:

    if ( g_ffxAtelSteppingContextIndex >= 2 )
        mask = 0;                 // no pad at all for this context
    else
        mask = FFX_Pad__remapButtonMask(0, 0, g_ffxAtelPadPressed);

`g_ffxAtelSteppingContextIndex` is at **0x1326B30**. `FFX_Atel_StepContextRange` 0x8688F0
writes it before stepping each context, and the contexts are stepped as a range. Contexts 0
to 5 are the field set and context 6 is the menu one.

So of the six field contexts, only 0 and 1 ever arm an examine scan from the controller.
Both read the SAME mask, `g_ffxAtelPadPressed` (port 0 edge pressed), so this is not a free
second player. What it does mean is that a second ATEL context is already plumbed to run its
own examine scan and its own commit, with its own ctx+484, ctx+486, ctx+488 and ctx+492.
That is worth knowing before anyone writes the save-and-restore dance in section 7: there may
be a cleaner shape where player 2's character lives in context 1.

I have NOT established what context 1 is normally used for, or whether putting an actor in it
is viable. Flagging it as the most promising unexplored direction, not as a plan.

### 13.2 An out of range actor id does not fault, it lies

`FFX_Atel_GetActorPos` 0x86B1E0 bounds-checks against context 0's actor count and, when the
id is out of range, substitutes `g_ffxAtelCurrentActorIndex` at **0x1326B2C**, which is
whichever actor the ATEL frame loop last touched. `FFX_Atel_GetActor` separately clamps an
out of range index to 0 and bumps `g_ffxAtelActorClampCount` at 0x13270D8.

So a bad actor id coming off the network produces a plausible wrong answer rather than a
crash, which is the worse failure mode because it is silent. Every kit entry point bounds
checks the id against ctx+12 itself rather than relying on the engine.

## 14. What landed in the Al Bhed Workshop kit

Three new files, and nothing else was touched.

    loader\workshop\include\ffx\addresses\Atel.h   66 RVAs, every one verified against the
                                                   IDB by name and by "is this a function
                                                   start", plus AtelRvaList for VerifyLayout
    loader\workshop\include\ffx\Atel.h             the typed layer
    loader\workshop\src\ffx\Atel.cpp               compiles clean at /W4 /WX

`include\ffx\Addresses.h` still needs one `#include "ffx/addresses/Atel.h"` line and
`src\ffx\VerifyLayout.cpp` one `{ "atel", &Rva::AtelRvaList }` row. Those are shared files
and are deliberately left alone.

The functions that matter for the chest, in the order a co-op build uses them:

    int  ffx::ActorIdForPartyCharacter(int charIndex)      ownership table -> actor id
    int  ffx::FindActorsNearForEvent(xyz, radius, kind, ids, max)
                                                           what is interactable near here
    bool ffx::ActorCanFireEvent(int actorId, int eventKind)
                                                           would the command do anything
    int  ffx::FireActorEvent(int actorId, int eventKind)    the command executor
    int  ffx::InteractionBlockedKind()                      is a dialogue already open

plus `ffx::SavePlayerPosCache` / `SetPlayerPosCacheFromActor` / `RestorePlayerPosCache` for
section 7's per-player pass, and `ffx::LogAtelWorld`, `LogAtelActors`, `LogAtelEventTables`
for confirming the double indirection landed on something real before trusting any of it.

`ActorCanFireEvent` reads all three gates through the engine's own
`FFX_Atel_GetEventChannel` and `FFX_Atel_GetEventScriptEntry` rather than from a transcribed
copy of the tables, so it cannot drift from the binary even if the tables above are mistyped.
