# AlBhedWorkshop design: what we are building and what we know about building it

The target, as specified: a **networked** co-op mod for FINAL FANTASY X HD Remaster, host plus client,
over **Steam networking**. Broad state parity, so that what happens on the host happens on the client,
cutscenes and NPCs included. Per-character ownership of actions in and out of battle.

This document is the bridge between that goal and `reversing\`. For each requirement it says what is
already established, what is unknown, and what to look at next. **It marks claims honestly**, because
the whole value of this file is being able to trust it when planning. "Known" means read out of the
binary. "Unknown" means nobody has looked.

## Three players, one per active battle slot

**The cap is three.** Not a technical limit, a design one, and it comes straight from the game:
battle action ownership is per character, FFX fields three active characters at a time, so a fourth
player would have nobody to be.

That number does more work than it looks like it does.

A peer id fits in two bits. The ownership table is at most three entries. A full mesh is three links,
so topology is not a real question. Nothing needs a lobby browser, a server list or matchmaking, since
three people who want to play together already have a way to talk to each other. And the whole
category of problems that starts with "what if eight people" never arrives.

It also sets the shape of the ownership model. Each player owns exactly one active battle slot, and
whoever owns the slot acts for whichever character is in it. Swapping a character in battle therefore
hands control to whoever owns that slot, not to whoever owns the character, which is simpler and
matches how the game already thinks about the three slots.

## The one structural decision that makes this tractable

Networked rather than split-screen changes the problem completely, and in our favour.

Two separate game processes each render their own view with their own camera, so **the entire camera
problem disappears**. No split-screen, no second viewport, no second render pass. Each instance's
camera frames its own `g_ffxControlledChr` exactly as the shipped game already does. That is why the
phase 1 note "the camera needs no work" stays true all the way to the finished mod rather than being a
temporary simplification.

It also means we are not fighting the one-player assumptions nearly as hard as phase 1 implied. On each
machine there is still exactly one local player. `g_ffxControlledChr`, the player-gated walkmesh codes,
debug noclip, the player-bump trigger and the camera can all keep pointing at the local character. The
remote player is a second CHR that we drive from the network instead of from a pad. That is the same
code path phase 1 already proved, with the input coming from a socket.

**So the hard part of this project is not character control. It is state synchronisation.** Plan
accordingly.

## Transport: Steam networking

**Known:** the game already ships and loads `steam_api.dll` (145,600 bytes, in the game directory, and
`FFX.exe` imports it). So a Steamworks interface pointer is obtainable in-process without us
initialising Steam ourselves, and the user is already running under Steam.

**Settled, and the answer is not the one we wanted.** The shipped `steam_api.dll` was inspected
directly (86 exports, file version 02.37.91.26). **It is SDK 1.31**, pinned by `SteamClient016`,
which maps to 1.31 exactly since 1.32 and later carry `SteamClient017`. An earlier note here guessed
1.34 to 1.37 and that was wrong. It does not matter for networking, because `isteamnetworking.h` is
byte identical across every SDK from 1.25 to 1.42 and all of them declare `SteamNetworking005`. It
matters a lot for `ISteamClient`, which changed repeatedly. The DLL
contains exactly one networking interface version string:

```
SteamNetworking005
```

**There is no `SteamNetworkingSockets` interface in it at all**, and no
`SteamInternal_CreateInterface` export either. It is the old style of shim with one accessor function
per interface. The other versions it carries, for calibration: `SteamClient016`, `SteamUser017`,
`SteamFriends014`, `SteamMatchMaking009`, `SteamUtils007`.

So the modern `ISteamNetworkingSockets` is not reachable the easy way. Three options, best first:

1. **Use `ISteamNetworking005`, which IS directly exported** as `SteamNetworking`. This is the
   deprecated P2P API (`SendP2PPacket`, `ReadP2PPacket`, `AcceptP2PSessionWithUser`,
   `k_EP2PSendReliable`), and it still gives NAT traversal and Valve relay fallback. Deprecated is not
   removed, Valve still supports it, and plenty of shipped games used nothing else. **For two-player
   co-op this is almost certainly adequate, and it needs no tricks at all.** Start here.
2. **Reach a modern interface through `ISteamClient::GetISteamGenericInterface`.** `SteamClient` is
   exported, as are `SteamAPI_GetHSteamUser` and `SteamAPI_GetHSteamPipe`, so:

   ```c
   HMODULE h = GetModuleHandleA("steam_api.dll");
   ISteamClient *c = ((ISteamClient *(*)())GetProcAddress(h, "SteamClient"))();
   void *nss = c->GetISteamGenericInterface(GetHSteamUser(), GetHSteamPipe(),
                                            "SteamNetworkingSockets012");
   ```

   The reason this can work despite the old shim is that **the shim is not the implementation**. The
   real code lives in the Steam client's own `steamclient.dll`, which is current, so a request by
   version string can resolve an interface this `steam_api.dll` has never heard of. The catch: calling
   `GetISteamGenericInterface` means knowing its **exact vtable slot index in `ISteamClient016`**, which
   has to come from the Steamworks SDK headers of that version. Until that index is confirmed this is a
   plan, not a working route. **Do not write this code from memory, check the header.**
3. **Ship a newer `steam_api.dll`.** Rejected. The game links against the shipped one, Steam's file
   verification may restore it, and replacing a Valve binary is exactly the kind of thing that breaks
   on an unrelated Steam update.

**Recommendation: build the transport behind a small interface and implement it over
`ISteamNetworking005` first.** Option 2 then becomes a drop-in replacement if P2P proves limiting,
rather than a prerequisite. Nothing above the transport should know which one is in use.

### What is built, and the one place the plan changed

The interface exists: `workshop/Transport.h` in the Al Bhed Workshop library. Send, receive, pump,
peers, counters, and nothing else. Above it sits `workshop/Protocol.h` for the wire format and
`workshop/Session.h` for membership.

**The change: the first backend is plain UDP, not Steam.** The reasoning is practical rather than
architectural. Calling `ISteamNetworking005` means calling through a vtable whose exact layout has to
be verified first, and separately, testing over Steam P2P needs two Steam accounts on two machines, so
every iteration of the lockstep layer would mean coordinating with another person. Over loopback, two
copies of the game on one PC talk to each other. That is the difference between testing the session
layer a few times a day and testing it every build.

Nothing above the transport knows or cares. The Steam backend is still the shipping one and slots in
underneath the same interface.

**Being honest about the gap:** the UDP backend has no reliability layer. `SendReliable` and
`SendUnreliable` both come out as one `sendto`. On loopback that is fine because loopback does not
drop. Rather than paper over it, every message carries a per-sender sequence number and the receiver
reports gaps. Silent packet loss is the worst failure mode lockstep has, because the symptom arrives
minutes later as an unexplained divergence, so it is worth a counter and a log line. Steam P2P gives
real reliability, so this gap closes when that backend lands.

### The Steam ABI is settled, and calling it is safe

`workshop/SteamAbi.h` carries the whole thing, with provenance on every block and `static_assert`s
that turn a drift into a compile error rather than a crash.

How it was established matters, because this is the one area where being approximately right is worse
than useless. **Valve's own Proton repository vendors complete, version-pinned SDK header trees**, so
the real headers were read rather than a mirror or a summary. Then every layout claim was verified
against the actual 32-bit MSVC that builds the mod, using `-d1reportSingleClassLayout` and reading
emitted vtable offsets back out of the object file. Most items are therefore transcripts of what the
compiler did, not deductions about what it should do.

`ISteamNetworking005`'s 22 slots are pinned, with the eight P2P ones we use verified three independent
ways. `GetISteamGenericInterface` is slot 12 on `ISteamClient016`, which unblocks option 2 above if it
is ever wanted. Note it is slot 13 on `SteamClient009`, which is how a version-unpinned source gets
this wrong.

**Two findings that would have crashed us.** Worth recording because both are counterintuitive.

`CCallbackBase::Run` is overloaded, and MSVC emits an overload group in **reverse** declaration order.
So the vtable is slot 0 `Run(void*, bool, uint64)`, slot 1 `Run(void*)`, slot 2
`GetCallbackSizeBytes`. An ordinary callback arrives on slot 1, not slot 0. The header declares the
overloads in SDK order so the reversal cancels out, which means anyone who "fixes" the order breaks
it. There is a comment there saying exactly that.

`P2PSessionConnectFail_t` is **9 bytes**, not 12 or 16. `CSteamID` is declared under `pack(1)` and an
enclosing `pack(8)` can only lower alignment, never raise it. Substituting a plain `uint64` gives 16,
and then `GetCallbackSizeBytes()` lies to Steam about the size of the thing it is about to write.

**Decision: no callbacks.** All three peers proactively call `AcceptP2PSessionWithUser` on each other
and poll for packets. The reasoning is that the hand-rolled `CCallbackBase` is the only item on the
list that is never executed during verification, and the only place we hand Steam an object to call
*into* rather than calling into Steam. The SDK confirms both facts that make the alternative work:
`AcceptP2PSessionWithUser` is safe to call repeatedly, and `SendP2PPacket` implicitly accepts a
pending request. At three players that is two extra calls per peer and it retires the only unverified
item on the list.

**Packet sizes, which changed one of my decisions.** The 1200 byte cap applies to the two unreliable
modes only. `k_EP2PSendReliable` takes up to **1 MB per message and fragments and reassembles for
you**. So frequent per-step traffic stays capped at 1200 to stay inside one datagram, while reliable
control messages are free to be large. That matters later for transferring game state to someone
joining a game in progress, which is the one message that will not fit in a datagram.

**The risk that replaces ABI risk is init ordering.** The game owns `SteamAPI_Init`, so the
`SteamNetworking` export returns something that must not be dereferenced until the game has
initialised Steam. Null check it every time, and never call `SteamAPI_Init`, `SteamAPI_Shutdown` or
`SteamAPI_RunCallbacks` ourselves, because the game is already doing it on its own schedule.

### Topology: a star, with the host in the middle

Clients talk to the host and never directly to each other.

With three players a full mesh would need the host to introduce two clients to one another, which on
Steam means exchanging SteamIDs and on UDP means punching a hole. It buys nothing here, because under
lockstep the host is already what stamps a step onto every command, so a client to client message
would pass through the host regardless. The cost is one extra hop on client to client traffic, which
does not exist yet.

Everything above the session addresses peers by **session peer id**, never by transport slot. Those
two are not the same thing: the host is session peer 0, but from a client's point of view the host is
whichever transport slot the client dialled. Keeping them separate is what lets the relay appear later
without changing anything above.

### What the session layer does and deliberately does not do

It does membership: who is here, which one is the host, who has gone quiet, and whether the two ends
agree on protocol version, mod build and shared settings. A mismatch is refused at the handshake with
a reason the player can act on, because "connection failed" sends people hunting their firewall when
the real answer is that one of them forgot to rebuild.

It does **not** do the step clock, the input exchange or the command channel. Those are the lockstep
layer and they sit on top. Keeping membership separate from simulation is most of the value of having
a session object, because it means the handshake can be tested without a simulation running.

**Time in the session is counted in frames, not milliseconds.** Heartbeats and timeouts are measured
in calls to `Step`. The simulation is already fixed step at 29.97 Hz so a frame is a fine unit, and it
keeps the one genuinely non-deterministic input, the wall clock, out of a layer lockstep depends on.
The useful consequence: if the game stalls or pauses, the session stalls with it and nobody times out,
which is right, because a peer loading a map has not gone away.

## The requirements, mapped

### Player models from the party, including characters like Jecht

**Mostly known, and this looks easy.** `FFX_Ch_Allocate(chrId)` takes a model id where
`chrId = (category << 12) | number` and category 0 is `c` for playable characters. Tidus is id 1
(`c001`). The clone prototype already allocates by id, so **a different model is a different integer**,
nothing more.

What makes it cheap: motion banks are registered globally by model id into
`g_ffxMotGroupTable 0x1300A08`, not per CHR, and `FFX_Ch_LoadChrData` short-circuits if the data is
already resident. So a second character of an already-loaded model costs almost nothing.

**Now settled, and the answer is good.** `tools\pcmodels.py` enumerates every `c`-series model in the
shipped archive and reports which motion sets each one has. The test is exact rather than a guess:
`FFX_Mot_SetByModeIndex` reads `*(CHRDATA.m_blobBase + 16 + 8*g_ffxMotModeToSetSlot[mode])` and
`g_ffxMotModeToSetSlot 0xC49784` is `BYTE[4] = {5,6,7,8}`, so `.chr` sections 5 to 8 are the four mode
tables and a section's aux field is its motion id count. A non-zero section 5 means the model has field
locomotion.

**35 `c`-series models ship, and 25 of them have a field motion set, so 25 are safe to spawn and walk.**

The important part for the model picker: **ids 1 to 8 are the party, and all eight work.**

| id | field | fieldbtl | swim | notes |
|---|---|---|---|---|
| 1 (`c001`) | 5 | 216 | 4 | **Tidus, confirmed.** He has 5 field motions where everyone else has 3 |
| 2 to 8 | 3 | 169 to 240 | 0 or 4 | the rest of the party. 3 field motions is exactly idle, walk, run |
| 41, 45 | 3 | 201, 0 | 4, 0 | `c041` has a full battle set too, so it is another complete character |
| 43, 44, 121, 122 | 1 | 0 | 0 | one field motion only, so story or idle-only models. Not walkable in practice |
| 101 to 108 | 3 | 0 | 0 or 4 | the high-detail variants of 1 to 8, field motions but no battle set |
| 307, 901, 908 | 3 | 0 | 0 or 4 | walkable |
| 51, 902 to 907, 921, 922, 999 | 0 | 0 | 0 | **no field motions at all.** Spawning these would slide a T-pose |

Run `python tools\pcmodels.py` for the live table, or `--ids` for just the spawnable ids.

**Jecht is not identified.** The identity of every slot except `c001` is still unknown, and
`pcmodels.py` deliberately does not guess. Resolving the names needs something that maps an id to a
character, and the likely sources are the battle or menu character tables in the binary, or simply
spawning each id and looking at it. That is now a cheap experiment rather than research, because the
clone plugin can already spawn an arbitrary id.

### The client opening chests and interacting with the world

**This was the first genuinely hard requirement. It is not hard any more, and the earlier writeup in
this section was wrong on its central claim.** The corrected account is below, derived in
`reversing\INTERACTION_PATH.md` and verified independently.

**What the earlier version got wrong.** It said the line and box triggers are gated on
`actor->id == *(u16*)(g_ffxAtelCtx + 10)`. There is no such compare. `FFX_Atel_StepActor 0x8666E0`
resolves the bound player **once** and passes the player actor **pointer** as the third argument to
the proximity test and to both the line and box steppers. The only gate is "is anybody bound at all",
a test against 0xFFFF. All three handlers are therefore already callable with an arbitrary player
actor, and kinds 5 and 6 carry their own target id at `pos+40` so they were never player-specific at
all.

It also had `g_ffxAtelCtx 0x1326B28` wrong in a way that would have broken any code written against
it. That address is a **pointer slot**. The contexts live in `g_ffxAtelCtxArray 0x1325BA0`, 568 bytes
each, seven of them. Reading offsets off the pointer's own address reads the wrong memory.

**What actually opens a chest.** There is no chest-specific code anywhere. A chest is an ordinary
examinable ATEL actor whose script happens to call a give-treasure syscall. The chain:

1. `FFX_Atel_StepFrame 0x867950` samples the pad once per frame. Confirm pressed means it arms a scan
   and clears the winner slot.
2. Every player-aware handler competes for that one slot: closest actor inside its radius that also
   passes the facing cone wins `ctx+488`.
3. The commit, same function: `FFX_Atel_FireActorEvent(0xFFFF, ctx+488, ctx+486)`.
4. That starts the actor's script, which eventually calls one of the two give-treasure syscalls,
   `0x85A8A0` or `0x857B70`. Their only references are the script dispatch tables. **No C code calls
   them, they are script-only.**

So the whole mechanism reduces to one sentence: **make the host call
`ffx::FireActorEvent(chestActorId, kAtelEventExamine)`**. No input simulation, no flipping the bound
player, no byte patch. The engine's own deliberate fire entry point already takes the actor id as a
parameter, because the engine needed one too.

**Three things that fall out of this, all of them good:**

- The examine button is read **outside** the proximity test, once per frame, from a single port-0
  global. Event kind 3 (proximity) fires on distance alone with no input on its path at all. So the
  input side of "the client interacts" is not a problem to solve, it is a problem that does not exist.
- `FFX_Atel_StartThreadIfChannelFree` already refuses a second script on a busy channel. **A
  duplicated network command therefore cannot double-grant an item**, because the engine itself
  refuses the second one while the first script runs. That is a correctness property we get for free.
- `ffx::ActorCanFireEvent(actorId, eventKind)` reproduces all three engine gates through the engine's
  own table lookups, so a client can grey out a dead prompt locally without asking the host.

**CORRECTED: exactly one machine must fire the event is backwards under lockstep.**

The earlier reasoning here said the chest's script calls AddItem, so whichever machines run the
script each grant the item, therefore exactly one machine must fire the event and the whole thing
is host-authoritative. That is the right conclusion for a client-server architecture with state
replication. It is the wrong conclusion for the architecture this mod actually has.

Under lockstep the simulation is replicated, not the state. Both machines run the same inputs and
reach the same state. If the host fires the event and the client does not, they have diverged, and
the item is the smallest part of what is now different. **Both machines must fire it, on the same
step, and both must grant the item.** That is not a hazard to be avoided, it is the mechanism
working.

Better still, interaction needs no network message of its own. The trigger pass is a pure function
of the players' positions and their confirm buttons, and both of those are already replicated as
input. So both machines run an identical pass, pick the same winner, and fire the same event with
no coordination at all. The command channel is for things that are **not** derivable from input,
such as a player joining or the host changing a setting that affects the simulation.

The thing that does have to be host-ordered is anything with a step number attached, which is why
`effectiveStep` exists on a command. Interaction does not need one because the step it happens on
is already determined by the input it came from.

**What is still untested.** Whether a script started from a frame hook rather than from the trigger
stepper is safe during a transition, a fade, or a battle hand-off. `FFX_Atel_StepFieldFrame` has gates
on `ctx+528` and `ctx+500` that the fire path never consults. That is a runtime question, and the mod
ships the experiment: F12 lists what is interactable near the second character with the reason each
one would refuse, and shift+F6 fires the examine event on the nearest eligible one. If a chest opens
for a character that is not the bound player, the feature works.

### The interaction model: events are commands, held objects are owned state

The framing that settled this: a map transition or a chest opening can be a signal to the host, but an
object a character picks up and carries has to be properly synced. Those are two different problems and
they want two different mechanisms. Writing the split down here because it decides how much work every
later feature is.

**Three classes of state. Each gets a different treatment, and almost everything in the game is class
1.**

1. **Discrete events with one trigger moment.** Map transitions, chest opens, doors, cutscene starts,
   dialogue. One side decides, both sides run the real game code. Carried by the command channel.
2. **Continuous state owned by exactly one side.** Character position, facing, animation, battle input.
   Owner authoritative and replicated. Already being built.
3. **Continuous state whose owner changes.** A sphere carried through a Cloister of Trials. Ownership
   transfer plus replication, and the only class that needs new machinery.

#### Class 1, the command channel, and why it needs a step number even under lockstep

The naive reading of "send a signal to the host" is a plain remote call. Client says "I opened chest 7",
host opens it. That breaks in three ways, all cheap to handle up front and painful to retrofit.

- **Double fire.** Both players walk onto the same map transition in the same step. Two signals arrive,
  two map loads start. So every event needs a dedup key, which is the (map id, trigger id) or (map id,
  chest id) pair, plus a fired set that gets cleared on map change.
- **Ordering.** If the client opens a chest on step 1000 and the host opens a different one on step
  1000, both instances have to apply them in the same order or the RNG stream advances differently and
  the two games disagree about the loot. Commands carry a step number, with the peer id as the tiebreak,
  and both instances apply the sorted list at the top of that step.
- **Lockstep compatibility.** Under lockstep a command is not a remote call at all, it is an input in
  that step's packet, and both machines run the script themselves. If the channel is frame stamped from
  the start then the same wire format serves both models and switching between them never touches the
  interaction layer. That is the real reason to build it this way now.

So an event on the wire is `{step, peerId, kind, key, payload}`. The host assigns the step and it has to
be at least `currentStep + latencyBudget`. Both instances buffer until that step arrives and then call
the game's own code. Nobody ever sends "the chest is now open", they send "open chest 7 on step 1042".

**The known blocker, confirmed at runtime.** `FFX_Atel_StepLineTrigger 0x8684B0` and
`FFX_Atel_StepBoxTrigger 0x866CC0` only ever evaluate the bound player actor, so the remote character
fires nothing at all by walking into things. A spawned clone was driven onto a map transition trigger and
nothing happened, which matches the code exactly. The consequence for this design is that each instance
has to evaluate its own character against the trigger volumes itself and emit a command. The trigger
geometry is readable out of the `.ebp` bytecode that `tools\ebp.py` already decodes, so that is work
rather than a wall.

#### Class 3, held objects, and the cheap way out

**What decides the cost is how the game attaches a carried object to the carrier.** Three cases, and only
the last one is expensive:

- **Parented to a bone on the carrier.** Then sync "who is holding what", which is about one byte per
  object, and each engine computes the transform itself from the carrier it is already replicating.
  Pickup and drop become class 1 events and nothing streams.
- **Position recomputed each frame by a script.** Still nothing streams, because the script is
  deterministic and runs on both sides. All that has to agree is who is carrying.
- **Driven by physics.** Bullet is linked into this binary. If a carried or pushed object is a rigid body
  being integrated, it becomes class 2 state owned by the carrier and its transform has to stream.

Which of the three it is, is being checked now. The answer changes the held-object work from trivial to
moderate, and nothing else.

**Contested pickup falls out of the ordering rule for free.** Two players reach the same sphere on the
same step. The commands sort by (step, peerId), the first one wins and takes ownership, and the second
one finds the object already owned and is dropped. The thing that matters is that it is dropped on both
instances by the same rule rather than by whoever happened to get there first in wall clock terms. That
is the whole reason ownership is granted by a sorted command and not by a race.

**The state that actually must not diverge is the puzzle solution, not the sphere.** What opens the door
is "which pedestals are filled". If that lives in a script variable or a save flag then it is part of the
save data block and the existing sync covers it. If it is derived from object world positions then it is
fragile and wants a snapshot taken at the moment the door check runs. Also being checked now.

#### A staging recommendation, not a reversal

Lockstep stays the design and the evidence for it is good. But the interaction layer should be built host
authoritative first, over the frame stamped command channel described above, for one reason: lockstep has
no graceful degradation. The first time an FMV, an audio callback or the Blitzball clock reads nudge one
instance, everything diverges at once and there is no signal saying which feature broke. Host
authoritative events give a working build sooner and a natural place to hang a per event desync check.

Both models use the identical command channel, so this is a staging decision rather than an architectural
one, and it costs nothing to change our minds later.

### Cutscenes and NPC syncing

**This is the biggest piece of work in the project, and the format research has already de-risked it.**

**Known:** cutscenes and NPC behaviour are driven by **ATEL**, the `TK:`-prefixed event script VM.
`FFX_Atel_StepActor 0x8666E0` is the per-actor tick. The script-visible command set is 11 real
libraries registered by `FFX_Atel_Init 0x86D6D0` into `g_ffxAtelSysFuncLibs 0x1328558`, addressed as a
`(library, function)` pair packed into a 24-bit syscall operand. The actual opcode set is a 123-case
switch in `FFX_Atel_RunScript 0x8641E0`. The on-disk container and the full bytecode are decoded in
`reversing\EBP_FORMAT.md`.

**The architectural opportunity:** FFX is overwhelmingly script-driven, and a script VM is a state
machine with a small, enumerable state. Per actor that is the thread contexts (PC, stack, registers, 9
channels at `actor + 0x12C + 76*channel`), the motion state, and the MOVE command block. So the
realistic design is **not** streaming positions for every NPC. It is keeping both instances' ATEL VMs
running the same scripts from the same state, and syncing the inputs that make them diverge.

That is far less traffic and it keeps cutscenes frame-accurate rather than approximately similar.

**Determinism was the unknown that decided this, and it has now been investigated. The answer is
yes.** See the next section. Lockstep is the design.

## Determinism: lockstep is viable, and this is the evidence

This was the question that gated the whole architecture, so it got investigated properly. **Build for
lockstep.** The three things that would have killed it are all absent.

### 1. The game RNG is seeded once per process launch, before any gameplay

There are **five distinct RNGs**. Two are gameplay-decisive, two are cosmetic, one is dead.

**`FFX_Rand_Stream 0x7988F0` is the one that matters.** It is a 68-channel generator where the argument
selects the stream, partitioning randomness by subsystem:

```
s = mul[i] * state[i]
s ^= xor[i]
state[i] = (s sar 16) + (s shl 16)      // rotate by 16
return state[i] & 0x7FFFFFFF
```

- `g_ffxRandStreamState 0x1135EE0` is **272 bytes and is the entire mutable state.**
- `g_ffxRandStreamMul 0xC42208` and `g_ffxRandStreamXor 0xC42318` are 68 read-only constants each, zero
  writers.
- **The state has exactly two writers in the whole binary**, this function and the seeder. Verified by
  checking every xref to all 68 dwords. There is no battle-start reset and no map-load reset. A third
  function does reference the address, `sub_798820`, but only as a loop terminator for the array that
  ends where this one begins, so it never writes it.

The stream assignments that matter: **stream 18 is the battle chance roll**
(`FFX_Btl_RandPercentCheck 0x7A8AD0`, so hit, status and steal chance), streams 2 and 17 are
script-visible `rand` syscalls, and streams 12 and 13 pick a random live party member.

**`FFX_Btl_Rand 0x7989A0`** is a single-state LCG (`*0x5D588B65 + 0x3C35`, state
`g_ffxBattleRandState 0xC42200`) used for battle formation and party slot picks. Same two-writer
property.

**The seeding is the good news.** `FFX_Rand_SeedAllStreams 0x798890` is the only seeding site for
either gameplay RNG, and it has **exactly one xref**, from `FFX_InitNewSaveData`, reached
unconditionally from `FFX_MainInit`. So it runs **once per process launch, at boot, before any
gameplay.** Not per battle, not per encounter, not per frame.

The seed itself is weak and clock-derived: `GetSystemTime`, with second, minute, hour, day, month and
the low byte of year all xored together into effectively one byte. That does not matter, because a
single 272-byte copy pins both instances to the same stream.

**What to do:** at session start, copy `g_ffxRandStreamState 0x1135EE0` (272 bytes) and
`g_ffxBattleRandState 0xC42200` (4 bytes) from host to client. Also hook `0x798890`, because the ATEL
save-RAM-clear syscall (`library 0 function 573`) can reseed mid-session and a New Game probably issues
it. Do both.

**One hazard worth knowing:** `FFX_Btl_Rand` is exported to the magic-effect plugin DLLs as host API
entry 324. So every shipped visual-effect DLL can advance a gameplay RNG. If effect playback diverges,
that state diverges.

### 2. The simulation is not driven by the real frame delta

This was the specific fear, and it was a false alarm. `FFX_Ch_UpdateMotionAll 0x832E10` does compute
`k = dt * 30.0 * 0.05 * m_speed`, but `FFX_MainStep` calls it with a **hardcoded constant**, not the
frame delta. Verified in the disassembly at `0x821073`:

```
fld   ds:flt_B59158          ; 0.033373333513736725, one 29.97 Hz frame
push  ecx
fstp  [esp+...]
call  FFX_Ch_UpdateMotionAll
```

So **field character motion integration is already fixed-step and frame-rate independent.** The frame
delta reaches only four places in `FFX_MainStep`, none of them field or battle logic: the HD Remaster
fast-forward scaler, two Phyre presentation calls, and the step-pacing accumulator.

`FFX_StepPacing 0x821E80` is a real fixed-step accumulator at 30 Hz, and `FFX_MainStepLoop 0x822840`
runs `do { FFX_MainStep(dt) } while (g_ffxPendingSteps != 0)` handing the same dt to every catch-up
iteration. With a constant dt the iteration count is a pure function of dt.

### 3. The engine ships a fixed timestep already

`Phyre__PApplication__frameTick 0x627940` has two paths. When `PApplication+0x3C > 0.0` it uses that
value as the delta and **never reads the clock at all**. That field comes from
`g_phyreFixedTimeStepArg 0xCC9D10`, which the command-line parser fills from **`-timestep=<seconds>`**.
There is also a forced path that sets it to `0.016659999` when input logging is active, which says the
engine authors used exactly this mechanism for deterministic replay.

At runtime the field is `*(float *)(*(void **)0xCC9CD8 + 0x3C)`.

**What to do:** launch both instances with `-timestep=0.0333733`, matching the game's own motion step,
or poke the field. That removes `QueryPerformanceCounter` from the delta entirely. Verified in code,
**not yet tested at runtime** - confirm by watching audio sync and the FMV path.

### Two localised hazards, neither architectural

An exhaustive breadth-first walk of 5 call levels from `FFX_MainStep`, visiting 4,606 functions and
looking for every wall-clock source, found only four hits. Two of them are real problems:

- **Blitzball reads the wall clock directly**, at nine sites through
  `FFX_Input__getTimeSeconds 0x630C40`. It is the one gameplay subsystem that will not lockstep as-is.
  It is self-contained enough that host-authoritative streaming for just that minigame is a reasonable
  escape hatch.
- **The cutscene and FMV pacing path busy-waits on the millisecond clock.** `FFX_StepPacing` mode 1 is
  entered when a timing track exists, and it counts catch-up steps from real time in a spin loop.
  Cutscenes need either forced mode-0 pacing or an explicit resync at the end of each one.

The other two are minor: `sub_6F0670` gates a transition on a two-second `GetTickCount` ramp, which
will show up as a one-step divergence at scene transitions and should be patched to count steps.

### The step counter already exists, so use it

**This section used to say no clean step counter exists and that we should add one. That was
wrong.** `g_ffxMainStepCounter 0x23CBBF0` is the game's own simulation step number: zeroed by
`FFX_MainInit`, incremented once per `FFX_MainStep`, and it covers field and battle alike. It is
exactly the clock lockstep needs and the engine was maintaining it the whole time.

**Read it, never write it.** Three double-buffer parity sites test its low bit, so a write would
corrupt rendering rather than just confusing the mod.

There is a second useful counter, `g_phyreFrameTickCount 0x12FB8C4`, incremented once per presented
frame with **exactly one xref in the entire binary, the increment itself**. So it is a free, tamper
proof render-frame count, which is the right thing to measure presentation against while the step
counter measures simulation. Keeping those two apart matters, because a dropped or repeated presented
frame must not move the simulation clock.

The mod now reports `g_ffxMainStepCounter` as its step, falling back to the render frame only before
`FFX_MainInit` has run, where nothing is simulating anyway.

### The simulation can be stalled safely, and the game already does it

Lockstep sometimes has to wait for the other machine's input before advancing. The place to do that
is **one byte: `FFXApplication + 0x3AD`.**

All four FFXApplication vtable bodies already honour it. `update` skips the input update, `animate`
skips `FFX_MainStepLoop`, the play-time accumulator and `FFX_GameTick`, `render` takes
`FFX_Frame_PresentPausedOverlay 0x642BE0` instead of the scene branch, and `endFrame` does nothing.
**That is the shipped pause menu's own mechanism**, which is the strongest possible evidence that
stalling there is safe, and it is reachable without opening the menu UI because the mod already owns
the animate slot.

The question "does anything latch on being called exactly once" was checked rather than assumed.
Nothing breaks. Four step-counted timers simply stop.

So the gate is four lines in the existing animate hook:

```c
if (!PeerInputReady()) { ffx::HoldSimulation(self); return 0; }
ffx::ReleaseSimulationHold(self);
return ffx::OriginalAnimate()(self, unusedEdx);
```

The cost is that the scene stops redrawing and the booster hotkeys stop responding, so a stall longer
than a few frames wants our own overlay rather than a frozen screen.

**What not to do:** refuse a `FFX_MainStep`. That leaves `FFX_Frame_PresentScene` submitting a display
list that was never rebuilt, which is a configuration the shipped game never produces. Whether it is
harmless is unknown and there is no reason to find out, because the hold byte is a state the game
already ships.

### Fast forward is the catch-up mechanism, and it is already built

The HD Remaster's fast forward is `g_ffxPendingSteps`, and lockstep can drive it directly.
`FFX_StepPacing` only recomputes the step count **when it is already zero**, so writing
`g_ffxPendingSteps = N` before animate survives and runs N simulation steps inside one presented
frame. That is precisely what a peer who has fallen behind needs.

Three multipliers stack here and conflating them would be easy, so they are written up separately in
`reversing\MAIN_LOOP.md`: the booster's dt multiplier, the pending step count, and an inner sub-step
count.

**`g_boosterSpeedIndex 0xCE82B4` has to be synced or lockstep diverges.** It scales dt by 1, 2 or 4,
and dt feeds the step count. This is a game value rather than a mod setting, so it does not belong in
the shared-settings hash. It belongs in the synced game state.

### The frame rate is not constant, and the menu is why

The real top level loop is in `WinMain 0x62EE50`, not in the engine. It runs its own `PeekMessageA`
pump and sleeps to hold one frame at `g_phyreFrameRateDivider * (1/59.94)` seconds. One thread, no
render thread, no engine vsync wait.

The divider is 2 by default, giving 29.97 Hz. **`FFX_MenuSys_StepFrame` sets it to 1 the moment it
sets `g_ffxMenuSysRunning`**, so opening the in-game menu runs the whole loop, simulation included, at
59.94 Hz.

That is a direct hit on the menu sync design above, and it strengthens the argument there rather than
undermining it. The menu section already says "the menu is open" has to be global lockstep state
rather than local UI state. This is a second, independent reason: opening the menu doubles the rate
the simulation advances at. If one machine is in the menu and the other is not, they are not just
showing different screens, they are stepping at different speeds.

### The one determinism hole, and it is confined

`g_ffxPacingMode 0x12FB8A0` mode 1, the cutscene timing track, counts steps from the millisecond
clock and busy-waits. Mode 0 never touches the clock at all, and `FFX_Mot_AdvanceFrame`'s scale factor
is integer-derived from the 60 Hz tick delta, so it is deterministic.

`ffx::IsPacingFromWallClock()` reports which mode is live, which makes this something a desync
detector can watch rather than something to be surprised by.


### Build a desync detector early

Check exactly these words every N steps: the 68-dword stream state at `0x1135EE0`, `0xC42200`,
`g_ffxEffectRandState 0xC0A000`, and your own step counter. The stream state is the valuable one,
because it tells you immediately if the two instances drew a different **number** of randoms, which is
the failure mode that matters and the one that is otherwise invisible until something visibly diverges.

### Dead RNGs, recorded so nobody re-finds them and panics

`Phyre__GenerateRandomFloat01 0x45EA50` is only reachable through the Phyre scripting host, which is
never installed. `lua_math_random 0x953EE0` would have been a genuine hazard via
`math.randomseed(os.time())`, but the Lua VM is provably unreachable (see Closed avenues).
`FFX_Rand_UnusedSecondLcg 0x7989D0` has zero references. Bullet's `btRand2 0x9BD2F0` is seeded to a
constant 0 and only shuffles constraint solve order, so it follows the physics rather than driving it.

### Battle, with per-character action ownership

**Known:** there is a dedicated battle script library, `g_ffxAtelSysFuncLib07_Btl 0xC42628`, and its
real size is **296 entries** (not the 235 earlier notes claimed, corrected by scanning to the table's
actual end). Battle camera modes are 11, 12, 13, 15 and 16 of the 18 in `g_ffxCameModeNames 0xC44640`.
`maybe_FFX_Btl_SetupUnitChr 0x793670` and `FFX_BtlAtelOp_SetUnitMoveSpeed 0x7A5840` already drive CHR
movement from battle script, which is more evidence that per-character control is the engine's own
idiom.

**Unknown, and this is the research that has not started:** where a battle command is read from input
and committed to a unit. Per-character ownership means intercepting that commit point and gating it on
who owns the unit. The shape to look for is a function that takes a battle unit plus a chosen command
and queues it.

**The Aeon case** the requirements call out is a genuine design problem rather than a research one.
Whoever controls the summoner controls the Aeon, with a settings option to change it. That is an
ownership-table question, not a binary question, so it can be deferred until the ownership mechanism
exists.

**A caveat worth stating now:** the ATB turn order means battle is not symmetric like field movement.
Only one character acts at a time, so battle sync is closer to a turn-based lockstep than to continuous
state streaming. That is easier to get right, but it means the input gating has to be exact: if both
sides think they own the current turn, or neither does, the battle stalls.

### Menus: the party, system, equipment and sphere grid screens

**The requirement, restated precisely.** One menu, shared. Either player can open it. Whoever opened
it drives it. The other player sees exactly what the driver sees, cursor included. On top of that, only
a character's owner may act on that character's sphere grid.

That is two rules, and they pull against each other. See "the one real conflict" below.

**Known so far:** not much, and that is the honest answer. `FFX_GAME_NOTES.md` covers the save-data
globals, which is where party composition, inventory, ability and equipment state live, so the data the
menu edits is partly mapped. There is an ability-map script library `g_ffxAtelSysFuncLib13_AbMap
0xC85EB0` with a single entry, `abiritymap_debug`, which hints the sphere grid has some script surface.
Nobody has read the menu code itself yet. That research is running now, listed at the end of this
section.

#### Replicate input, not state

There are two ways to make two machines show the same menu.

**Replicate state.** The driver sends the cursor index, the current screen, the scroll offset and the
pending selection on every change. The follower writes those into its own menu and redraws. This
tolerates the menu being nondeterministic, because the follower never runs the menu logic. It is told
the answer.

**Replicate input.** The driver's button presses go onto the same step-stamped command channel the
interaction model above already describes. Both machines feed them into their own menu code on the same
step. Both menus walk the same path and arrive at the same place.

**Replicate input is the right choice**, for three reasons, all of them about reusing what is already
decided.

It is the same mechanism as everything else. Lockstep is already the architecture and the command
channel already exists in the design, so the menu becomes one more command kind. No new transport, no
new reconciliation path, no second way for state to travel.

It is nearly free on the wire. A menu input is a button mask and a step number. State replication is a
struct of unknown size that has to be discovered field by field first, then rediscovered every time a
screen is touched.

**And the commits come for free, which is the real argument.** Using an item, changing equipment,
learning a sphere grid node: under input replication both machines run the game's own code and both
land on the same save data. Under state replication the follower's menu is a puppet, the actual edit
happened only on the driver's machine, and then every single commit needs its own explicit sync
message. That is the version of this that never stops producing bugs.

The cost is that **the menu has to be deterministic given the same inputs and the same starting save
data.** For a menu that is a far safer bet than for a simulation. A menu is logic over save data with
no physics and no float integration. Two things could break it: a cursor repeat delay driven off the
wall clock, and a random visual flourish. Both are survivable. The first can be redirected at the step
counter, and the second does not touch state. Confirming which, if either, is present is part of the
research below.

#### Opening the menu is a pause, and that is the part that gets underestimated

The cursor is the easy half. The hard half is that opening the menu stops the field simulation. If the
host opens the menu and the host's field sim stops while the client's keeps running, the two have
diverged before the menu has drawn a single frame.

So **"the menu is open" is global lockstep state, not local UI state.** It is the same object as the
pause flag the requirements already call for syncing, and it has the same shape: one boolean that both
machines must flip on the same step.

That makes the pause flag the right first test of the entire sync layer. One boolean, an obvious
failure mode, and it exercises the step-stamped command path end to end with nothing else moving.
Build it first.

#### The control token

Opening the menu is a request, not an action. The player presses the button, the local machine sends
`{step, peerId, kind=MenuOpen}`, and nothing visible happens until that command comes back stamped for
a step both machines will execute. On that step both open the menu and both record the same driver.

If both players press on the same step, the tie breaks on `(step, peerId)`, exactly as the contested
pickup rule above does. One opens, the other's request is dropped. Reusing the same rule in both places
is deliberate. One ordering rule is one thing to get right, two rules are two things to get wrong.

While the menu is open, the follower's input is suppressed and the driver's is injected. **Suppress at
the narrowest input read, not inside the menu logic.** The menu very likely reads the same single
global button mask the field does, the one the pad research already found takes no port argument, so
there should be exactly one place to gate. Gating one read is auditable. Gating inside the menu means
finding every branch that consults input, and missing one gives a silent divergence rather than a
visible bug.

#### The one real conflict in the requirements

"Whoever opened the menu has control" and "only a character's owner may act on that character's sphere
grid" collide the moment the host opens the menu and navigates to a character the client owns.

Two readings, both defensible.

**Read it literally.** The menu lock governs the UI surface, the ownership rule governs the action. The
host can browse the client's grid and look at it, but confirming a node is refused with a message. One
gate at one commit point, and the lock never has to move mid-session.

**Hand the token over.** Entering a character's grid transfers driving to that character's owner while
that screen is open, then hands it back on the way out. Nicer to use and probably what a player would
expect, but the lock can now change owner inside a menu session, which is more state and more edge
cases around backing out of a screen.

**Recommendation: build the literal reading first**, because it is a gate at a single commit rather
than a second lock protocol, and the sphere grid is the only screen where the conflict arises at all.
Then revisit with something running, because this is a feel question and feel questions are badly
answered on paper. Flagging it as a decision rather than settling it unilaterally, since the
requirement genuinely reads both ways.

#### The desync detector for this layer

Because both machines run the real menu code, they can be checked cheaply. At the end of every step
where the menu is open, hash the menu's replicable state together with the save-data region the menu
edits, and compare. A menu desync is one of the best cases to catch: it reproduces, it happens while
nothing else in the game is moving, and the hash says which region drifted. Build the detector
alongside the menu, not after it.

**Why menus are scheduled after field and battle despite being simpler.** They are simpler, and they
depend on the command channel and the step clock, which field and battle sync are the things that force
into existence. Doing menus first would mean building the channel against a case that does not stress
it.

**Game settings stay unsynced**, per the requirement, except game-changing ones such as difficulty.
Worth encoding that split in the settings layer from the start rather than retrofitting it, because "is
this one game-changing" is a judgement call that will keep coming up. The mod's own settings divide the
same way: local ones such as which pad drives which character, and shared ones that have to match.
There is separate work running on moving the mod's settings into the game's own additional-settings
screen, the one that holds the fast-forward toggle.

#### What has to be answered before any of this can be built

1. **Is the menu native code, or an Iggy or Flash movie?** This decides everything else. A native state
   machine with a cursor index in a global is straightforward. A Flash movie carrying its own
   ActionScript state is a different and much worse problem, because the state that has to stay
   identical would live inside the movie's VM rather than in the game's globals.
2. **The menu state machine and the address of its mode variable**, so there is something concrete to
   checksum.
3. **The narrowest input read on the menu path.** The whole control-token design rests on there being
   one gate.
4. **The commit points** for using an item, changing equipment and moving on the grid, since those are
   where the ownership rule has to bite.
5. **Whether the menu path reads the wall clock anywhere**, a cursor repeat delay being the likely
   candidate. If it does, it has to be driven from the step counter instead.
6. **Every place the menu path consults the global player getter** `FFX_Ch_GetPlayerChr 0x82D860` or a
   one-slot cache instead of taking a character parameter.

## Menu sync: "replicate input, not state" survives, with three conditions

The menu research confirmed the plan and then found three ways to get it wrong. All three are the kind
of mistake that works in testing and fails on the one screen the requirements care most about.

The plan holds because the evidence is strong: the whole menu's input surface is one 192-byte block,
`g_ffxMenuPadBlock 0x25D09C0`, filled once per frame by `FFX_MenuSys_SamplePad 0x8BE500` with no port
argument anywhere, read through three accessors that every module calls. No menu decision path
reaches the RNG, established by walking 3,926 functions from all sixteen menu modules: the only path
that touches it at all is the Config screen's environment reload hitting the map-load encounter roll.
And the shipped code already memsets that block from `FFX_Module_Stop`, so a full wipe is a legal
operation the game performs on itself.

The conditions:

1. **The repeat mask at `+0x26` must be transmitted, not rederived.** It is paced off the wall clock,
   first repeat after 0.4667 s and then every 0.3 s. Two machines rederiving it from held masks will
   drift, and cursor repeat is most of what a menu does.
2. **The sticky held copy at `+0x18` must be written too.** The Sphere Grid is its only reader and it
   ignores `+0x12` entirely. Miss this and input replication works on every screen except the grid,
   which is the one with the per-character ownership requirement.
3. **Opening the menu is not part of the block.** The Triangle test is in `FFX_MainStep`, against the
   single global player pad mask, so the open replicates separately through
   `FFX_MenuSys_RequestOpen 0x821750`. That conveniently defers to the next frame, which is exactly
   what a command channel wants.

The hook point is a detour on `FFX_MenuSys_SamplePad`: run the original, then overwrite the block.
Five stolen bytes with no branch or absolute among them.

Two findings worth having beside that. The sphere grid is **module 19, proved** rather than inferred,
by the `Got ABMAP pad input!!!` string plus the sole caller of `FFX_SaveData_SpendSphereLevels
0x786EF0` living in its page-handler family. And the per-character gate the ownership requirement
needs is `g_ffxMenuCharList 0x1841C14` indexed by `g_ffxMenuCharCursor 0x1841C28`, read through
`FFX_Menu_GetCursorChar 0x8A9860` and its 46 callers.

The strongest single result: **no menu function touches the save block by address.** The entire menu
commits through about eleven named functions, checked three independent ways. Equipment is
`FFX_SaveData__setCharEquip 0x7AB970`, the sphere grid is `FFX_SaveData_SpendSphereLevels 0x786EF0`
with exactly one caller in the binary. That short list is the desync detector's natural attachment
point, and it is short enough to gate individually for per-character ownership.

A useful negative: **there is no party-order screen in the field menu.** Nothing in the menu writes
`saveData+0x3D58`.

## Cutscenes replicate. An FMV needs a barrier.

The cutscene research settled the project's biggest open determinism question, and the answer is
better than feared in one direction and more subtle in another.

**Pacing mode 1 is the voiced cutscene path, not the FMV path, and it is re-derived every frame.** Its
only writer is called unconditionally on every frame the pending step count hit zero, so the mode means
one thing at that instant: a SyncData timing track is selected and has entries left. A track is armed
by the ATEL call-actor-script opcode, and the sibling tables are read by the voice syscalls. **A
cutscene with no voiced line never leaves mode 0.**

**Mode 1 does not make a step's contents depend on the clock.** While it is active the per-step motion
scale comes out of authored file data rather than the clock delta. Both machines consume the same
entries in the same order. Only the step count per presented frame differs, and lockstep already owns
that.

**And the engine ships the compensation.** Its own pause menu has the identical problem, so there is a
pending-pause value the step pacer subtracts from the track's elapsed time. A lockstep gate that stalls
during mode 1 and says nothing leaves the track behind, and the track then burns catch-up steps that
the other machine does not take. `ffx::AddTimingTrackPauseMs()` is that primitive reached from outside,
and the mod calls it when it releases a hold. **This was a real defect in the first cut of the gate.**

The ATEL VM itself steps inside the `FFX_MainStep` sub-step loop, never reads a clock, and touches the
seeded RNG only through four script syscalls. So a cutscene replicates under lockstep for free.

An FMV does not and does not need to. It is WebM on its own thread with a `GetTickCount` timebase, but
`FFX_MainStep` early-returns while it plays, so no simulation runs at all. Both machines freeze on the
same step, which makes a barrier the natural and cheap answer rather than a compromise.

One trap recorded for later: during a cutscene the hold belongs on the **animate** hook, not the narrow
`FFX_MainStep` gate, because mode 1's busy-wait is inside `FFX_StepPacing`.

## Putting the mod's settings in the game's own menu

Answered, with one thing left to test in a running game.

**Rows are extensible, pages are not.** Every row of every page is one ActionScript call with the row
index as the first argument, and nothing in native code holds a maximum. Pages cannot be added without
editing the Iggy asset, because the page structure is fixed by the ActionScript function arity.

**No native code reads a row count back, and it cannot.** FFX.exe imports 41 Iggy entry points and not
one returns an array length. The read-back loop sets indices 0 to 11 unconditionally. So the native
half is count-blind and the indices are whatever we say.

**Editable rows must go on the Video page.** This corrects the first pass, which suggested Special
Feature as the safer target. The deciding factor is not cost: there is no `addSpecialFeature...Refresh`
name in the callback registration at all, so the movie never reports a Special Feature row back.
Special Feature is right for status text and wrong for a setting.

**Fast forward was never a menu setting.** It is `g_boosterSpeedIndex 0xCE82B4` driven by F1, which is
why the config file has no key for it. The Special Feature page is a read-only hotkey cheat sheet.

**All 24 persisted options are presentation only and must never be synced.** The four things that do
change the simulation are the booster globals: speed index, encounter rate, invincibility and auto
battle. The speed index is not optional, it scales dt and therefore the step count.

**The one thing left to test** is whether `escmenu.swf`'s `addVideoPageItem` appends a row or fills one
of a fixed set of pre-placed instances. Every native-side failure mode is now ruled out, so the whole
plan rests on one line of ActionScript inside an Iggy asset that nobody has read. The evidence says
append: the Video page emits 11 or 12 rows depending on the display adapter and in the 11-row case it
skips index 0, so the movie tolerates a gap.

The mod ships that experiment. Shift+F5 arms it, and the next time the Esc menu is built it adds a 13th
Video row reading "Pilgrimage Together". A row that appears means the mod's settings can live in the
game's own menu. No row means fall back to the Special Feature page for text and the mod's own window
for anything editable.

## The camera is a determinism surface, and the wire format had to change

Found while wiring a remote player's input to a character, and it is the kind of thing that
would have worked in a one-machine test and desynced immediately in a real session.

**The engine turns a stick into a heading using the local camera.** `FFX_Player__stepControl`
computes `desired = atan2(right, forward)` and then `m_moveDir = cameraYaw - desired`. So a raw
stick value does not mean a direction. It means a direction *relative to whichever camera read
it*.

**The camera is per-player and is deliberately not replicated.** It is driven by that player's
own right stick, it has its own integration, and the requirements already say presentation
settings do not sync. That is the right call. But it means two machines handed the same stick
byte compute two different headings, and the two characters walk apart on the first step anybody
moves. Input replication is not enough on its own, the frame the input is interpreted in has to
match too.

**The fix is in the wire format, and it costs nothing.** The owning machine resolves its stick
against its own camera before sending, and puts a world-space direction in the same two bytes.
The receiver takes `atan2(leftY, leftX)` and reads no camera at all. Same eight-byte
`InputFrame`, same quantisation, and the camera is out of the determinism surface entirely.
`leftX` and `leftY` are therefore NOT a stick despite the names, and Protocol.h says so loudly
at the struct.

A detail that is easy to get backwards: the engine stores left stick Y **positive-down**, so
forward is `-leftY`. Drop that negation and every heading is mirrored front to back.

**What this leaves open, and it is now the only place it matters.** The bound player's own
character is still driven by the engine's own control path, reading the pad directly inside
`FFX_MainStep` and applying the local camera. Every other character in the world is now driven
from replicated bytes. So the host's own character is the single remaining one that can diverge,
which turns the input injection problem from "nothing works without it" into "one character
needs it". That is a much better position to be in, and it narrows what the injection has to
achieve: not a general pad write, just getting the bound player onto the same world-direction
path everybody else is already on.

## A remote player triggering the world, as built

This is the first half of "a remote player can connect and interact with the world", and it is
done and installed. The second half, the world and save state a joiner needs, is separate.

`plugins\pilgrimage\world\TriggerPass.cpp` runs one extra trigger pass per remote player per
simulation step, after the engine's own. Each pass:

1. Saves the context's position cache, examine arbitration quartet, player radius, and the bound
   player actor's facing float.
2. Writes that player's current and previous position, and its facing.
3. Arms the examine scan only if that player asked AND the engine's own two gates agree, which are
   `FFX_Atel_MesWinBlockingKind() == 0` and `g_ffxAtelExamineAllowed != 0`. Skipping those would let
   a remote player examine during a sequence where the host cannot.
4. For every actor with a trigger step, swaps in that player's shadow edge bits, steps it, and
   takes the bits back out.
5. Reads the examine winner, then commits it exactly as `FFX_Atel_StepFrame` does: the talk bonus
   first, then the event.
6. Puts the host's edge bits and the whole context back.

**Why it runs on the step hook and not the frame hook.** The engine runs its trigger set once per
simulation step, and one presented frame can be several steps. Driving this per frame would make a
remote player's triggers depend on the frame rate, which is the exact class of bug lockstep exists
to prevent.

**What it cost.** Four bytes per actor per player of shadow state, one float saved and restored on
the bound player's actor, and the context quartet. No byte patches, no detours, no new actors. The
three steppers were already written to take the player as a parameter because the engine needed
that itself.

**The correction that made it work**, recorded because an audit had explicitly concluded the
opposite: positions alone are not enough. All three steppers keep a per-ACTOR "the player was
inside me last frame" bit and read it back to pick enter against leave. One slot per actor, not
one per player. Ignore it and a host standing inside a trigger while a remote stands outside fires
a spurious enter and leave pair every frame. See the corrected section 7 of
`reversing\INTERACTION_PATH.md` for the bit masks, which matter more than the mechanism, because
saving those two words wholesale instead of masked will stop actors stepping.

**Still untested in a running game, and the mod ships the experiment.** Shift+F12 points the pass
at the active clone so it needs no second machine, shift+F9 lets it commit rather than only report,
and shift+F11 prints what it sees. Walk a spawned clone into a trigger and the log says whether the
world responded to somebody who is not the bound player.

**One known gap.** The pass does not run the kind 1 move and rotate commands that
`FFX_Atel_StepActor` runs before the proximity test, and must not, because those integrate the
actor's position and running them twice in one frame would move every NPC twice. That is correct for
triggers. It does mean this pass is a trigger pass and not a general second-player actor step.

## The lockstep layer, as built

`workshop\Lockstep.h` and `.cpp` are the clock. `plugins\pilgrimage\net\LockstepLink.cpp` is the
part that knows where an FFX simulation step actually happens. The split is deliberate: the kit half
knows nothing about FFX and the mod half is about fifty lines of glue.

### Delayed input, not rollback

Input cannot cross a network and come back inside a frame, so a naive lockstep would stall on every
single step. Instead everything a player presses is stamped for a step a little way ahead. At step S
the local input is stamped for S + 2 and sent, which means it lands long before step S + 2 comes up,
and neither machine normally waits at all. The cost is 2 steps of felt input lag, about 67 ms.

Rollback was considered and rejected. It needs the whole simulation state saved and restored every
frame, and FFX's state is spread across a 26,816-byte save block, a 0x880-byte-per-CHR pool, battle
actor arrays and whatever the engine is holding. Snapshotting that at 30 Hz is a project of its own
and would almost certainly miss something. Delayed input needs nothing saved.

### The gate goes on FFX_MainStep, not on animate

One presented frame can run more than one simulation step, because `FFX_MainStepLoop` loops on the
pending step count. That is how the HD fast forward catches up. So counting animate calls counts
frames, not steps, and the two machines would be agreeing on the wrong number. The gate goes on
`FFX_MainStep` through `ffx::HookMainStep`, which already offers exactly the pair this needs: a
callback that can refuse the step, and an observer that runs after an allowed one.

### Two kinds of hold, for a good reason

Refusing a step inside `FFX_MainStep` is cheap and precise, but it leaves the render path presenting a
scene whose display list was not rebuilt this frame. That is a configuration the shipped game never
produces. Fine for a frame or two of jitter, not fine for half a second. So a stall of more than two
refused steps escalates to the stall byte at `FFXApplication+0x3AD`, the shipped pause menu's own
mechanism, which all four vtable bodies honour. The hold is released the moment input arrives.

### Input is unreliable with redundancy, commands are reliable

Each input message carries the last six frames of our own input, not just the newest. A single lost
packet then costs nothing, because the next packet repeats the frame. That is much better than a
reliable ordered channel, which would hold every later frame behind a lost one and turn one dropped
packet into a visible stall.

Commands are the opposite case. A command is a thing that happened once: a chest opened, a menu
opened, a battle action chosen. A lost command is a divergence and no amount of redundancy makes an
unreliable channel safe for one, so commands go reliable.

### Commands are host-ordered, and that is what makes two machines agree

A client does not get to decide when its own action happens. Two clients deciding at once would hand
the two machines different orders. So a client ASKS (`MessageCommandAsk`) and the host ISSUES
(`MessageCommand`), stamping the command with an effective step six steps ahead and an id from a
single counter. Everybody, the host included, queues the command and applies it when that step comes
up. The id gives a stable order when two land on the same step.

The failure mode is named and counted rather than hidden: if a command arrives for a step a machine
has already run, there is no correct way to apply it, so `LateCommands` goes up and the log says to
raise the command lead. That is the number to watch on a bad connection.

### The sticks are quantised on purpose, and the local player goes through it too

A stick arrives as a float and goes on the wire as a signed byte. The important part is that the
LOCAL player's own input is also round-tripped through that quantisation before it is simulated. If
one machine steps with the full float and the other with the byte that came off the wire, the two are
being fed different numbers, and that is the classic way to build a lockstep that drifts slowly and
inexplicably. `QuantiseStick` is the only place that conversion lives.

### Desync detection is wired to the game-state hash

`ffx::HashGameState` already produces a combined hash plus thirteen per-region values, with playtime
excluded because it ticks on its own. Every thirty steps each machine broadcasts its hash for that
step. A mismatch names the first region that differs, so the log says "the inventory differs" rather
than "the state differs". Which of those thirteen regions are genuinely stable is being measured
rather than assumed, see the probe under Status.

### What is deliberately not done yet

**The other player's input is not injected into the engine.** The exchange runs, the frames are there
to read, and the clock agrees across machines, but nothing is written into the game. That needs one
frame-order question settled by measurement rather than reading: `FFX_MainStep` reads the pad inside
itself, ahead of `FFX_StepPacing`, so a write from the gate callback would be overwritten. Until that
is measured, running the clock without the injection is the honest increment.

**The gate does not refuse a step by default.** It counts how often it would have. Shift+F4 switches
enforcement on. That way the whole exchange can be watched running, with the stall counts visible,
before anything is allowed to freeze the game.

## What to research next, in order

Eight of the ten items that used to head this list are answered. What is left:

1. **The battle command commit point.** The one function that has to be gated for per-character
   ownership, and now the most important unanswered question by a clear margin. Everything else in the
   requirements has a known mechanism.
2. **Does a simulation step allocate?** This is the question that decides whether rollback was ever on
   the table, and it would also tell us whether a state hash can include anything holding a pointer.
   Hook the allocator, count calls per step while walking a field, then in battle, then in a cutscene.
   An hour of work.
3. **Four unidentified menu modules, 6, 9, 20 and 23.** None writes the save block and none has a
   distinctive string, which is why they are still unknown. Module 21 is probably Overdrive, unproved.
4. **How a carried object is attached to its carrier.** Bone parent, script-written position, or Bullet
   rigid body. One answer decides whether held-object sync is one byte or a transform stream.
5. **Where a button press cancels an FMV.** The observables are known, the skip path is not.
6. **Chr ids 45, 307, 901 and 908.** They have field motion sets and no entry in
   `g_ffxCharIndexToChrId`. Cheapest route is to spawn each and look, several at once.
7. **Blitzball's nine wall-clock reads.** The one remaining pocket of clock-driven gameplay that has
   not been looked at.

### What the running game has to settle, each already one keypress

These are not research, they are experiments the mod ships because reading the disassembly took them
as far as it can go.

- **Shift+F2**, the fixed timestep. Still the single biggest lockstep unknown.
- **Shift+F5**, the Esc menu row. Decides whether the mod's settings live in the game's own menu.
- **F12 then shift+F6**, the examine event on a character that is not the bound player. Decides the
  chest feature.
- **Shift+F8**, which save-block regions sit still. Decides what the desync detector can hash.
- **Shift+F4**, the lockstep gate enforcing rather than counting.

## Closed avenues

Recording these so nobody spends time on them twice.

**Lua is not a scripting surface. It is linked-in dead weight.** The binary statically links a
complete Lua 5.2 and PhyreEngine's whole reflection-to-Lua binding layer, all of it now mapped and
named, and **none of it is reachable in the shipped game.** Three independent proofs, any one
sufficient: nothing in the binary ever installs the script-load host callback at `0xCBE6BC` (the three
functions that would, at `0x5D66D0`, `0x5D66F0` and `0x5D6710`, have zero references anywhere, checked
by an exhaustive rel32 decode plus an absolute-dword scan). No shipped asset contains a script object,
established by decompressing all 47,125 `.phyre` clusters and reading their class-name tables, where not
one contains "Script", "PEntity" or "PComponent". And there are no `.lua` files at all among the 71,979
shipped paths, nor any Lua source text in any cluster. Full detail in `reversing\LUA_MODULE.md`.

A useful corollary: **PhyreEngine is used as a scene-graph and rendering layer only.** The entire
`PGameplay` component layer is unused by the shipped data. FFX's game logic lives in the
`ffx_ps2/.../*.bin` data and the ATEL scripts, not in Phyre. So when looking for game behaviour, look
at the FFX-specific code and the PS2 data, not at the engine.

## Status

Phase 1, a locally controlled second character, is **done and confirmed in a running game**:
`loader\plugins\pilgrimage\`, split across core, game, clones, diag, hooks, net and ui. A spawned CHR
appears, animates, walks, collides and is driven with two float writes per frame. It is a stepping stone rather than the shape of the final
design, but it proves the part that mattered most, which is that a second CHR is a first-class citizen
of the engine.

What it took, and the one thing that was not obvious: a newly allocated CHR has to be bound to a
walkmesh triangle with `FFX_Ch_WalkmeshMove` right after `FFX_Ch_SetPos`, or ground mode 1 drags it to
`m_groundHeight`, which is still zero, and from there it fails the camera test and is hidden forever.
Full writeup in `reversing\PHASE1_CLONE.md`.

The plugin now runs up to eight clones at once, with the input focus moving between them, which is the
cheap way to test that none of the second-character work is single-clone specific.

**Known limitation that is expected, not a bug:** a clone walks through map transition and chest
triggers without firing them, because trigger evaluation is hard gated on the bound player actor id.
That is the subject of the interaction model section above.

### The networking, as it stands

Built, compiling clean at `/W4 /WX`, and installed:

- **Two transports.** `SteamTransport` over `ISteamNetworking005`, which is the shipping path, and
  `UdpTransport` on loopback, which is how everything above it gets tested without a second machine.
  Which one runs is a checkbox, not a build option. The Steam ABI was verified against Valve's own
  Proton-vendored SDK headers plus the real compiler's emitted vtable offsets, and the shipped
  `steam_api.dll` is SDK 1.31. Full detail in `reversing\STEAM_ABI.md`.
- **The session layer.** Handshake, peer ids, heartbeats counted in frames rather than milliseconds,
  timeouts, sequence-gap detection, and a build-id plus shared-settings check so two different builds
  refuse each other instead of desyncing in a way nobody can explain.
- **The lockstep layer.** The step clock, the delayed-input ring, the host-ordered command channel and
  the checksum comparison. See the section above.

What has NOT been exercised against a second machine is the Steam backend end to end, and what has
not been built is the input injection. Those are the next two things.

### The diagnostics that are in there because a guess was cheaper to measure than to argue about

- **Shift+F2** toggles the engine's fixed timestep live. This is the single biggest lockstep unknown,
  reduced to one keypress.
- **Shift+F8** runs the save-block stability probe: hashes all thirteen regions for 300 frames while
  the game sits still and reports which moved and how often. The game-state research could only prove
  that playtime ticks per frame, so the rest was a reasonable guess, and a desync detector built on a
  region that ticks on its own is noise that gets switched off.
- **Shift+F4** lets the lockstep gate actually refuse a step, so the exchange can be watched running
  before anything is allowed to freeze the game.
- Startup logs the threaded-pad-mode answer, which was the input layer's own biggest unknown.
