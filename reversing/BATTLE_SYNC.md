# Battle command sync

A design note, not a reversing doc. The reversing is in BATTLE_COMMAND.md and this is what
happened when I built against it: what the code does, why it is shaped that way, what I left
out, and what I think is most likely to be wrong.

**None of this has been run in a game.** It compiles clean at /W4 and every address and every
byte pattern it patches was checked against the IDB, but no frame of FFX has ever executed it.
Read the "untested" section as the real status.

---

## What it does

One menu, replicated records.

Whoever owns the acting unit drives the game's own battle menu on their own machine, with their
own pad and their own cursor. At the instant the engine would commit that choice, the 72 byte
command record is captured, the local commit is dropped, and the record goes out on the ordered
command channel. Every machine then calls `FFX_Btl_CommitCommand` with those bytes on the step
the host picked. The machine that does not own the unit never opens a menu for it at all.

That shape is forced rather than chosen. The menu's staging record, its page stack, its page
depth, its owner and its target cursor are all single globals, and the staging record is rebuilt
from the page stack every frame. Two cursors on that is not an occasional race, it is one
player's half-built command being stamped with the other player's unit every frame.

## Files

New:

* `loader/workshop/include/ffx/Battle.h`, `loader/workshop/src/ffx/Battle.cpp` - the kit layer.
  The record as a struct, guarded readers for the queues and the per unit state, a bounds checked
  `CommitBattleCommand`, the ability and item row lookup, and the two hooks.
* `loader/plugins/pilgrimage/battle/BattleOwnership.h` and `.cpp` - who may choose for whom.
* `loader/plugins/pilgrimage/battle/BattleSync.h` and `.cpp` - capture, suppress, apply.

Edited, one or two lines each: `net/Commands.h` (the kind and the payload),
`PilgrimageMod.cpp` (install), `net/NetLink.cpp` (start and stop with the session),
`net/LockstepLink.cpp` (step it), `hooks/FrameHook.cpp` (ctrl+F11 log),
`ui/ControlPanel.cpp` (one readout line).

Also edited, and these two are worth calling out:

* `workshop/include/workshop/Protocol.h` - `MaxCommandBytes` 64 -> 96 because the battle payload
  is 80 bytes and did not fit, `ProtocolCommandSizeCheck` 76 -> 108 to match the new
  `sizeof(CommandPayload)`, and `ProtocolVersion` 4 -> 5 because that is a wire format change.
  Nothing else in that file was touched. **An old build and a new build will now refuse each
  other at the handshake, which is the intended behaviour.**
* `workshop/include/ffx/Ffx.h` - one `#include "ffx/Battle.h"` so the umbrella header is not
  missing an area. That was not on the list of files to edit, and leaving it out seemed worse.

---

## The decisions

### 1. The capture is a call site patch, not an inline detour

This is the biggest deviation from the plan I was given, and it is not a preference.

The plan said to detour `FFX_BtlMenu_ConfirmCommand` at 0x4975A0. **Its first instruction is a
rel32 call.**

```
0x8975A0  E8 1B 3A 00 00    call FFX_BtlMenu_BuildStagedCommand
0x8975A5  6A 00             push 0
0x8975A7  FF 35 38 C0 3C 02 push g_ffxBtlMenuStagedGil
```

`workshop::InstallDetour` steals at least five bytes and copies them to a trampoline
**verbatim**, with no relocation, which `Detour.h` rule 2 says out loud. Stealing those five
bytes would leave the trampoline with a call whose displacement points somewhere meaningless. So
that detour cannot be installed safely with this project's detour framework.

`FFX_BtlMenu_Open` has the same problem for the same reason:

```
0x89BB10  55                push ebp
0x89BB11  8B EC             mov  ebp, esp
0x89BB13  E8 D8 5B EE FF    call FFX_Btl_GetPhase
```

Three position independent bytes, then a rel32. Any five byte steal lands inside it.

What I did instead: **rewrite the caller's rel32.** Each site is a plain 5 byte
`E8 <displacement>`, and the patch verifies the opcode is E8 and that the displacement resolves
to the function it expects before it writes anything, then writes one dword. Nothing is
relocated, both engine functions are left byte for byte alone, and the original stays callable
simply by its address, so there is no trampoline at all.

It is also strictly more precise than an inline detour would have been, which matters here:

`FFX_Btl_CommitCommand` has ten callers and only five of them are the player. The other five are
the AI script runner, the three forced action paths for confuse, berserk and provoke, and
`FFX_Btl_SendMenu`'s empty placeholder. **Those five must not be intercepted.** They are the
engine deciding for itself from state that is already in step, so both machines reach the same
decision on the same step on their own. Replicating them would commit each of those actions
twice. An inline detour on `CommitCommand` would have had to tell them apart by return address
or by guessing. Patching the five player call sites makes the distinction structural.

The five sites, all verified in the IDB, all exactly one call to `CommitCommand` per function:

| site | offset from | VA |
|---|---|---|
| menu confirm | `BtlMenuConfirmCommand + 0x12` | 0x8975B2 |
| page root escape | `BtlMenuPageProcRoot + 0xB4` | 0x89C9B4 |
| page B escape | `BtlMenuPageProcB + 0xAB` | 0x89CE8B |
| page C escape | `BtlMenuPageProcC + 0x130` | 0x8A2420 |
| page D escape | `BtlMenuPageProcD + 0x130` | 0x8A27C0 |

All five are offsets from addresses already in `addresses/Battle.h`, so no new address constant
was added. They are written as offsets rather than as addresses because the offset is what was
actually derived ("the single call to `CommitCommand` inside this function") and the byte check
proves it at install time rather than trusting it.

I included the four page proc escape shortcuts rather than only the confirm. They are the
flee and skip shortcuts, they are genuine player choices, and leaving them out would mean one
player fleeing on their machine alone. All four pass `(g_ffxBtlMenuStagedCmd, 0, 0)`, checked at
each call site.

### 2. The callback gets the ARGUMENTS, not the staged globals

The plan said to read `Rva::BtlMenuStagedCmd` and `Rva::BtlMenuStagedGil` in the hook. Since the
hook is at the call site, the record pointer, the gil cost and the variant are all handed to it
already, so it uses those. Same bytes today (all five sites pass the staged record, and the
confirm passes the staged gil) and it is authoritative rather than coincidental. It also means
the four page procs, which pass a gil cost of 0 instead of the staged one, are covered by the
same code without it having to know that.

### 3. Suppression is the one call to `FFX_BtlMenu_Open`

**Why this point.** `FFX_BtlMenu_Open` has exactly one caller in the whole binary,
`FFX_Btl_SendMenu` at 0x792CF3, and the fork it sits on is the engine's own "player or AI"
decision. Suppressing it means the menu never exists for a unit this machine does not own: no
page stack, no cursor, no staged record, and no chance of the local pad reaching the other
player's turn. The capture hook's ownership check is then a safety net rather than the mechanism,
which is the right way round.

**Why it is safe to simply not open it**, which is the part I spent the most time proving:

* `FFX_Btl_SendMenu` claims the turn queue entry (`entry+3 = 1`) *before* it decides between the
  menu and the AI, and the top of its loop returns the moment the head is already claimed. So a
  suppressed open leaves the turn sitting there claimed and the engine's own early return holds
  the battle. Nothing had to be invented for the waiting.
* Both machines are in that state anyway. The owner is also sitting on a claimed turn from the
  moment it confirmed until the ordered copy comes back, because the local commit was dropped.
* **The CTB clock is frozen on both of them for the whole of it.** See the corrections section:
  `FFX_Btl_CtbTick` returns immediately while the *turn* queue count is non zero.
* I listed the writers of both queue counts and of all eight flags `FFX_Btl_IsCtbTickAllowed`
  reads. Not one of them is in the menu module (0x89xxxx / 0x8Axxxx). So "a menu is open on this
  machine" is invisible to the simulation, which is the property the whole suppression rests on.

The other candidates I rejected: forcing `FFX_Btl_IsActorAiControlled` non-zero also hands the
unit to the AI, which then picks and commits an action of its own. Letting the menu open and
closing it from the step callback leaves one frame in which `FFX_BtlMenu_Step` reads the local
pad into the remote player's turn, and with auto battle on that frame synthesises a confirm.
Suppressing `FFX_BtlMenu_Step` wholesale leaves the menu open with nothing to close it.

**The one awkward part.** `FFX_Btl_SendMenu`'s address is not in `addresses/Battle.h`, and I was
told not to add addresses. So the call site is found by scanning for the single
`E8 <displacement to FFX_BtlMenu_Open>` between `Rva::BtlCommitCommandPriority` (0x3929B0) and
`Rva::BtlCommitCommand` (0x392D60), the two functions that bracket `FFX_Btl_SendMenu`. The window
is 0x2B0 bytes, the scan requires exactly one hit and refuses on any other count, and
`PatchCallSite` re-verifies the bytes afterwards. I checked in IDA that the window contains
exactly `CommitCommandPriority`, `sub_792A40` and `SendMenu`, and that the scan finds exactly
0x792CF3. It works, and it is the ugliest thing in this change. **If you want to make it plain,
the address to add is `BtlSendMenu = 0x00392A90` and the scan can go.**

### 4. Ownership is derived, not agreed

`BattleOwnerOfUnit` is a pure function of the local peer id and the battle party roster. Both are
already identical on both machines: the peer id is assigned once in the welcome, and the roster
is state lockstep keeps in step. So the two machines reach the same answer with no message and no
window in which they disagree because the answer was in flight.

The rule is peer N owns active battle slot N, read through `ffx::BattlePartyMember` (which reads
`Rva::BattlePartyOrder`) rather than through `unit+0x4FE`, because that field holds the aeon index
for an aeon and the enemy index for an enemy and both would look like a perfectly good slot
number.

Anything that is not an active battle slot falls to the host: slot 2 in a two player game, aeons,
reserve members, and an enemy if the shipped `Mon Input` debug flag ever hands one a menu. That
is not a judgement about who should drive them, it is the only answer both machines reach without
another message. **Getting this wrong in either direction is the worst failure this subsystem
has:** both machines owning a unit means two menus and a double commit, neither owning it means a
battle hung on a claimed turn. There is no third outcome, which is why the rule is a function and
not a table that gets pushed around.

`SetBattleOwner(peer, charIndex)` is the seam for the eventual "each player picks their
character", and it has no caller on purpose. The header says plainly that when it does get one,
the binding has to be ordered like any other state change, because setting it on one machine only
is exactly the double-commit failure above.

`LocalOwnsUnit` returns **true** when there is no session, so a solo game is byte for byte the
shipped game, and true again in the window where we are in a session but have no peer id yet. Both
fail-safe directions point at "behave as shipped" rather than at "suppress", because a hang is
worse than a one turn risk and the clock is not running in that window anyway.

### 5. Apply on the ordered step, from the wire only

`StepBattleSync` is the same shape as `StepBoosterSync` and runs beside it in
`AfterStepCallback`. It walks `CommandsForStep`, filters on `kCommandBattleCommit`, rebuilds the
72 byte record from the payload and calls `CommitBattleCommand`. Every machine applies, including
the one that chose it, because the local commit was dropped.

The record goes on the wire verbatim rather than being unpacked into fields. It is already a flat
72 bytes with no pointers and no padding, so there is nothing to serialise, and unpacking it would
mean `Commands.h` deciding which fields matter. Two dwords per action entry have no known writer,
and "reliably zero on the paths that were read" is not the same as "safe to drop".

### 6. Recovery when a send fails

If `RequestCommand` fails the command is dropped, because committing it locally would act on one
machine only. But the engine has already claimed the turn entry and never offers a claimed turn
again, so dropping it would hang the battle outright. So on that path only,
`ReleaseBattleTurnClaim` clears the claimed byte and the menu is offered again next frame.

That writes a byte of battle state the other machine is not writing. It is acceptable precisely
there: the send failed, so the other machine never heard about the turn and has nothing to be out
of step with. Nothing compares the turn queue anyway, since the checksum only covers the save
block. It is still the only deliberately asymmetric write in this change and it is called out in
the kit header as a recovery tool rather than a mechanism.

---

## What I deliberately did not do

* **Two menus.** Not attempted and nothing here helps you build it. The single-instance list in
  `addresses/Battle.h` is the reason.
* **Validating an incoming command.** The record is trusted. Under lockstep a peer sending a
  record it could not legally have chosen is a modified client, not a network condition, and the
  executor's own checks (MP, overdrive, surviving target) still run on both machines. The piece
  you would need to validate a target mask properly, the function that builds the candidate list,
  was never found.
* **Replicating the AI or the forced action paths.** Both machines run them from identical state,
  so they replicate for free. Intercepting them would double them.
* **Stalling with `g_ffxBtlDbgCtbPause`.** Not needed. The turn queue count already freezes the
  CTB for the whole wait.
* **Aeon ownership.** An aeon should belong to whoever owns its summoner. Today it falls to the
  host. The summon itself is a normal command and replicates, so this is about who gets to pick
  the aeon's actions afterwards.
* **Overdrives, Mix, and anything with a submenu.** Nothing special was done for them. They
  should need nothing special, because they are the same record with different ids, but see
  untested.
* **A per-player HUD.** `g_ffxBtlActingActor` is cosmetic and single slot, so on the
  non-owning machine the HUD will show whatever the engine last stamped there. Not addressed.

---

## Corrections to BATTLE_COMMAND.md

Two, both found while building against it, both now recorded as function comments in the IDB.

### `FFX_Btl_CommitCommand` does NOT retire the turn queue entry on success

The report's prose says "it also retires the actor's turn queue entry, which
`FFX_Btl_CmdQueue_Push` does not", and `addresses/Battle.h` repeats it as "on success retires that
actor's turn queue entry". The report's own pseudo-code is correct and the reading of it is
backwards:

```c
if ( FFX_Btl_CmdQueue_Push(v3, a1, 0, 0, a3) != 0 )   // push SUCCEEDED, Push returns -1
    return 0;                                          // ... so it returns here
FFX_Btl_TurnQueue_RemoveAt(v3, Actor[3556], 0, 0);     // only reached when the push FAILED
return -1;
```

`FFX_Btl_CmdQueue_Push` returns **-1 on success** and 0 when the queue is full, which the report
states correctly in section 1. So the `!= 0` branch is the success branch, and `CommitCommand`
returns 0 **without** retiring the turn entry. The `RemoveAt` is cleanup for a failed push.

The normal retirement is later, in `sub_78D980` (the magic host API effect-completion path) via
`RemoveAt(actor, Actor[3556], 1, 0)`.

Also worth writing down because it reads backwards: **`CommitCommand` returns 0 for success and
-1 for "did nothing"**. `ffx::CommitBattleCommand` normalises that to 1, 0, -1 so no caller has
to remember it.

This does not change the design. The brief I was given said the arriving command satisfies the
engine "because CommitCommand retires the turn queue entry exactly as the menu's own confirm
does". The real mechanism is different and the outcome is the same: the push puts a command in
the queue, the executor runs it, and the effect-completion path retires the turn. Both machines
go through that identically.

### `FFX_Btl_CtbTick`'s early-out is the TURN queue count, not the command queue count

Section 6 of the report says:

```c
if (g_ffxBtlDbgCtbPause != 0 || g_ffxBtlCmdQueueCount != 0) return 0;
```

The actual test is `g_ffxBtlTurnQueueCount` (0x112BDE0), not `g_ffxBtlCmdQueueCount` (0x112BDE1).
Adjacent bytes, easy to transpose.

This is **better** than the doc claims and it is load bearing for this feature. The clock stops
the moment a turn queue entry exists, which is from the tick that pushed it until the
effect-completion path retires it. That one span covers the whole time a player spends in the
menu *and* the whole time a command spends on the wire, on both machines. If the gate really had
been the command queue count, the owner's machine would have kept ticking the CTB while the
player was choosing, and so would the non-owner, and I would have had to think much harder about
whether those two tickings stayed identical.

### Smaller things the report did not say

* **`FFX_Btl_IsActorAiControlled` is not safely callable from a mod.** Its Yojimbo special case
  rolls against `sub_7B2B20(actor)/4`, so calling it consumes battle RNG and would desync a
  lockstep session. The kit reads `unit+0xDF3` as a field instead.
* Hex-Rays types it as two arguments while `FFX_Btl_SendMenu` calls it with three, which is
  another reason not to call it.
* **`FFX_Btl_GetAbilityRecord` must not be called outside a battle.** Five of its eight class
  branches reach a per-class table pointer global and dereference it unchecked, so before
  kernel.bin is loaded it faults inside the engine. Three branches also dereference the out
  pointer without testing it, so a real one has to be passed even when the caller does not want
  the value. The kit gates on a running battle and always passes a real out pointer.
* **`FFX_Battle_GetActor` does not check its array base**, only its index, so outside a battle it
  returns an address near zero rather than null. `FFX_Battle_IsActive` returns that same base, so
  it is both the "is there a battle" test and the pointer check.
* `FFX_Btl_GetPhase` and `Rva::BtlSubPhase` are **the same byte**, 0x112A8E0, read with a `movsx`.
* `FFX_BtlMenu_Open` refuses unless the phase is 1 **and** the open mask is zero, which is its own
  guard against a second menu.

---

## Untested, and what I think is most likely to be wrong

Everything is untested. No frame of FFX has run any of it. Ranked by how likely I think each is
to be the thing that actually bites:

1. **The one frame of latency from applying in `AfterStepCallback`.** The command is committed
   after `FFX_MainStep` has already run for that step, so `FFX_Btl_ExecCommand` picks it up on the
   next step rather than the one the host stamped. Both machines do the same thing at the same
   point, so it should be consistent, and the report's own advice was to inject *before*
   `ExecCommand` in the same frame slot. I followed the instruction to sit beside
   `StepBoosterSync` instead. If anything about command ordering looks off, this is the first
   thing to move.
2. **Ownership during a join or a drop.** `PeerPresent` is "me, or reachable", and the two
   machines' peer tables agree except during a join or a drop. In that window they can briefly
   disagree about who owns slot 2, and the cost is one turn where either both suppress the menu
   (a hang) or neither does (a double commit). The real fix is an ordered ownership command, which
   is what the `SetBattleOwner` seam is for.
3. **Auto battle.** It drives the menu by synthesising pad bits, so on the owner it confirms
   almost immediately and goes through the capture like any other confirm, which should be fine.
   What I have not thought all the way through is `dword_133D6EC`, the handshake where
   `FFX_Btl_ExecCommand` tells auto battle its choice was rejected so it picks again. On the
   non-owning machine the menu is not open, so nothing is there to pick again. I do not think that
   matters, because the owner's machine does the picking and sends the result, but I have not
   proved it.
4. **The gil cost.** `g_ffxBtlPendingGilCost` is one slot for the whole battle, set by the commit
   and spent later by the executor. Normally the commit and the spend are the same frame. With
   the commit now happening a step before the executor sees it, there is one more step in which
   something else could overwrite the pending cost. Bribe and paying Yojimbo are the only things
   that use it, so this will go a long time without being noticed.
5. **The item inventory.** Both machines push, so both call `FFX_Btl_Cmd_ConsumeItems` and both
   decrement, which is correct. But `FFX_Btl_ExecCommand` calls the same function with +1 to
   refund a cancelled action, and I have not checked that the cancel path is reached identically
   on both machines when a command is accepted and then resolves to no target.
6. **The 320 byte describe buffer.** `DescribeBattleCommand` truncates rather than overflowing
   now, but the original 240 would have overflowed on a four entry record and `strcat_s` answers
   an overflow by ending the process. That was a real crash in my own first draft, which is worth
   saying out loud because it means a log line nearly took the game down.
7. **The scan for the menu open call site.** It works on this build and refuses loudly on
   anything else, but a scan is a scan. The fix is one address constant.
8. **Multi-hit and submenu commands.** Entry count above 1 is accepted and replicated, and I have
   no reason to think it needs anything special, but I have not seen a real multi-entry record.
9. **`ReleaseBattleTurnClaim`.** It only runs on a failed send, so it will almost never run, which
   also means it will almost never be tested. If it has a bug it will show up as a battle that is
   stuck after a link hiccup.

## What to watch when it is first run

The ctrl+F11 log block and the control panel's `battle` line carry the numbers that matter.

* `commits seen` and `commits taken over` should be equal in a session. Any gap is a commit that
  reached the engine locally, which is a divergence.
* `commits dropped for a unit we do not own` should be 0. Anything else means the suppression hook
  is not doing its job.
* `awaiting a reply` should be 0 or 1 and should not stay at 1. A battle sitting there is a
  battle waiting on a command the host never issued, and the claimed turn means it will wait
  forever.
* The install log says how many of the five capture sites went in and whether the open hook did.
  Both numbers have to be complete for a session to be safe, and `InstallBattleSync` says what
  happens if they are not.
