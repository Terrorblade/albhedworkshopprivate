# Menu sync and pause sync

A design note, not a reversing doc. What was built, what was decided and why, what was
deliberately left out, and an honest list of what is most likely to be wrong.

**Nothing in here has run in a game.** It compiles clean at `/W4` with zero warnings and
the reasoning is all from reading the binary. Every claim about behaviour under two
machines is a prediction.

Files:

| file | what it is |
|------|-----------|
| `loader/plugins/pilgrimage/menu/MenuSync.h` / `.cpp` | the Triangle menu: who drives, what it sees, the open, the host override, the desync hash |
| `loader/plugins/pilgrimage/menu/PauseSync.h` / `.cpp` | the Esc pause menu, as a shared hold |
| `loader/plugins/pilgrimage/net/Commands.h` | three new command kinds and their payloads |
| `loader/plugins/pilgrimage/net/LockstepLink.h` / `.cpp` | `kHoldPauseMenu`, the two per-step calls, the checksum change |
| `loader/plugins/pilgrimage/net/NetLink.cpp` | start and stop with the session, including the explicit disconnect path |
| `loader/plugins/pilgrimage/PilgrimageMod.cpp` | install the hook, non-fatal |
| `loader/plugins/pilgrimage/hooks/Hotkeys.cpp` / `FrameHook.cpp` | ctrl+F4, the frame-path pause step, the ctrl+F11 logs |
| `loader/plugins/pilgrimage/ui/ControlPanel.cpp` | a `menu:` line and a `pause:` line |
| `loader/workshop/include/ffx/MenuSystem.h`, `src/ffx/MenuSystem.cpp` | one new reader, and three comments that were wrong |

---

## 1. The shape, in one paragraph

The menu runs inside `FFX_MainStep`, which the lockstep gate already controls, so both
machines step the menu together for free. That leaves three jobs. Replicate the input,
which is one 192-byte global and is done by a detour on the single pad sampler. Replicate
the open, which is the one menu decision made outside the menu system from an
un-replicated global, and is done with an ordered command. And decide who drives, which
is three rules in priority order, all derived from replicated state so the two machines
cannot disagree.

The close needed nothing. The repeat needed deriving rather than transmitting. The Sphere
Grid turned out to have an input path the pad block does not cover.

---

## 2. Who drives

In priority order, and all three come out of replicated state only:

1. **The host override**, a flag that only changes when an ordered command lands.
2. **The Sphere Grid**: the driver is the owner of `ffx::SphereGridCharacter()`, which is
   the byte at `MenuWork + 71100` and the one `FFX_SaveData_SpendSphereLevels` is charged
   against. Control therefore changes hands as the editing player L1/R1s between
   characters, which is what was asked for.
3. **Otherwise the opener**, whose identity arrives as the `issuer` of the open command.

Ownership is asked of `pilgrimage::OwnerOfCharacter` in `coop/Ownership.h`. There is one
table.

Two small notes on this.

**I used `OwnerOfCharacter`, not `LocalOwnsCharacter`.** The brief named the latter and it
is the right gate when the question is "may I act", but here the question is "whose
buttons do I read", and both machines have to answer it with the same *peer id* so they
pull the same frame out of the same ring slot. `LocalOwnsCharacter` is `OwnerOfCharacter(c)
== LocalPeerIndex()`, so the two agree by construction and there is still one table.

**The fallback when nobody opened it is the host, not the local player.** A menu can be up
with no open command behind it: the session could have started with one already open, or
the open command could have been lost. Defaulting to "us" would have both machines driving
themselves, which diverges on the first button. Defaulting to the host is the only answer
both machines reach with no message, which is the same argument `Ownership.h` makes for
rule 3 there.

---

## 3. The open, and why there is no detour on `FFX_MenuSys_RequestOpen`

The brief said to intercept `FFX_MenuSys_RequestOpen` (RVA 0x421750). **It cannot carry a
5-byte jmp detour.** Its prologue is

```
821750  55              push ebp
821751  8B EC           mov  ebp, esp
821753  56              push esi
821754  E8 A7 69 E2 FF  call FFX_SaveUi_IsIdle     <- rel32
```

Four bytes of position-independent prologue and then a rel32 call. Any five stolen bytes
either split that call or relocate a rel32 to somewhere meaningless, and
`workshop/Detour.h` refuses both on purpose (rule 2 in its header). Extending the detour
framework with a length disassembler and call fixups to hook one function was not a trade
worth making.

**What I did instead: watch the pending-mode global.** `FFX_MenuSys_PollOpenAndStep
0x820750` makes the request in one branch of an if/else and *consumes* it in the other, so
never both in one call. And when the menu is not running, only one of its three call sites
in `FFX_MainStep` runs per step: site A is guarded by the menu already running, and sites
B and C are the two arms of `if (sub_8202E0(mode))`, inside the gameplay block that is
skipped whenever `g_ffxMenuSysRunning` is set. So a pending mode written during step N is
not acted on until step N+1, and the lockstep gate at the top of step N+1 has a whole step
to cancel it.

So `PrepareMenuInput` does, in this order:

1. If a mode is pending that we did not order, and it is mode 0, write -1 back with
   `ffx::CancelMenuOpenRequest()` and ask for `kCommandMenuOpen`.
2. Apply the commands for this step. An open calls `ffx::RequestMenuOpen(0)`, and
   `FFX_MenuSys_PollOpenAndStep` consumes it inside this same step, so both machines enter
   the menu on the same step number.

The order is load-bearing: step 1 before step 2 means an open ordered in 2 is not seen and
cancelled by 1 in the same call, and a flag covers the case where the engine has not yet
consumed ours.

This turned out better than a detour would have been, for a reason that is not just
rationalisation: it catches **every** path that sets the pending mode, not only the one
function.

Two things are deliberately not intercepted:

- **Any mode other than 0.** The cloud save (`0x40100000`, `0x40200000`) and save-UI modes
  take a completely different branch, are not reached from the field Triangle test, and
  cancelling one would break saving. Those are logged loudly as opening on one machine
  only, which is honest rather than fixed.
- **The open succeeding.** `FFX_MenuSys_RequestOpen` stores nothing at all when
  `FFX_SaveUi_IsIdle()` returns 0, and it does not report that. So after replaying an
  ordered open, `MenuOpenRequested()` is checked and a failure is logged with the
  consequence spelled out. That is a real divergence source I cannot close from here, but
  it is now visible rather than silent.

### The close needed nothing, and here is the proof

There is no close function. `FFX_MenuSys_StepFrame 0x8A9CA0` calls
`FFX_MenuSys_AnythingStillRunning 0x8AA610` (was `sub_8AA610`, now named and commented in
the IDB) at the end of every menu frame and clears `g_ffxMenuSysRunning` when it returns
zero. That function is just the module active mask plus one extra flag. The last module to
stop itself is module 1, which does that when it sees the cancel button **in the pad
block** - the thing this file replicates.

Confirmed by enumerating every reference to `g_ffxMenuSysRunning 0x1340824`: the only
writers in the whole binary are the two inside `FFX_MenuSys_StepFrame`, one setting it to 1
on entry and one clearing it on that test.

So both machines close on the same step with nothing sent. **The open is special only
because its test lives outside the menu, reading `g_ffxPlayerPadPressed 0x23C44A8`, which
is the local hardware pad and is not replicated yet.** That asymmetry is the whole reason
one of the two needed a command and the other did not, and it is worth stating plainly
because it would be easy to add a close command out of symmetry and have it be pure cost.

---

## 4. The input, and the auto-repeat

`ffx::HookMenuSamplePad` detours `FFX_MenuSys_SamplePad`, the callback runs after the
original, and it replaces the block with the driver's replicated mask. Same shape as
`DialogueSync`, including the split: the masks and the button edge are worked out **once
per step** in `PrepareMenuInput` from the lockstep gate, and the callback only applies
what it finds. Working out an edge inside the callback would make it depend on how many
times a machine happened to sample, which comes from the wall clock.

### Write all nine masks, not two

`MENU_SYSTEM.md` lists the block's fields but does not mention that the three accessors
every screen reads through **pick their field at runtime**:

```c
FFX_MenuSys_GetHeld()    -> (held & 0xF000) ? held    : synthHeld      // +0x12 or +0x22
FFX_MenuSys_GetPressed() -> (held & 0xF000) ? pressed : synthPressed   // +0x14 or +0x24
FFX_MenuSys_GetRepeat()  -> (held & 0xF000) ? word10  : repeat         // +0x16 or +0x26
```

That branch is how the game folds the analog stick into the dpad. The consequence for an
injector is sharp: **an injector that writes `+0x12` and `+0x26` gets a repeat that works
when the player uses the stick and silently does nothing when they use the dpad**, because
in the dpad case `GetRepeat` reads `+0x16` instead. The clean way to be immune is to make
every field say the same thing, which is what `SpreadMasks` does: held into `+0x12`,
`+0x18` and `+0x22`, pressed into `+0x14`, `+0x1A` and `+0x24`, repeat into `+0x16`,
`+0x1C` and `+0x26`.

The sticky copy at `+0x18` is in there for the reason the report already gives: it is the
only field the Sphere Grid reads, and the grid ignores `+0x12` entirely.

### The repeat is derived from the step counter, not transmitted

`MENU_SYSTEM.md` says to transmit it. I did not, and the argument got stronger the closer
I looked.

The engine accumulates real elapsed seconds per button and compares against
`0.4666666388511658`. When it trips it sets the bit and **resets the accumulator to
`0.29999998`**. That reset value is not an interval - the comparison is still against
0.4666666 - so the gap between repeats is

```
0.4666666388511658 - 0.29999998 = 0.1666666...  s
```

At one step of 1/29.97 s that is **5 steps, not 9**. Both numbers are exact PS2 frame
counts at a flat 30 fps (14/30 = 0.46666, 9/30 = 0.3) and the port turned the first into a
threshold and the second into a seed, which silently changed the interval from 9 frames to
5. The `holdCount[i] = 9` the engine still writes alongside is dead: `holdCount` is only
ever tested `== 0`, and only on a fresh-press frame, and a release zeroes it. It survives
in my code as `kRepeatReloadSteps = 14 - 5 = 9`, which is the same number arrived at from
the other end.

The sequence, verified against the loop body: **fire on the press, then 14 steps of
nothing, then every 5.** The press frame does not advance the accumulator, because the
engine's increment lives in the branch it only takes when the button is *not* newly
pressed.

Three reasons deriving beats transmitting:

1. **There is no stable shipped behaviour to be faithful to.** Both thresholds sit within
   a fraction of a frame of a boundary, because they are 30 fps counts in a 29.97 Hz game:
   14 steps clears the first threshold by 1.4% of a frame and 5 steps clears the interval
   by 0.5% of a frame. At exactly 1/30 both would tip to 15 and 6. The accumulator adds
   real measured wall time, so under ordinary frame jitter the shipped repeat flutters
   between 5 and 6 steps **on a single machine**. Transmitting it would faithfully
   replicate noise.
2. **It costs nothing on the wire** for something only menus use, and it is deterministic
   by construction rather than by trusting a payload arrived.
3. **It is much better over the internet.** With the repeat derived, a held direction
   repeats locally on both machines at the right rate from the replicated held mask, so
   scrolling a list is smooth and only the initial press and the release pay the input
   delay. Transmitting the mask would have made every repeat tick its own
   latency-sensitive wire event, so one held press down a long item list would have been
   dozens of them instead of two.

And the structural one: `holdTimer[16]` and `lastSampleTime[16]` hold
`Phyre_Time_NowSeconds() - dbl_C90278`, which is per-process uptime. Two machines are not
in the same epoch. No value derived from that array can ever cross the wire.

Start (0x800) and Select (0x100) are excluded, same as the engine.

### `ClearMenuPad` is not the way to say "no buttons"

The brief and the kit header both recommended `ffx::ClearMenuPad()` on the passenger when
input is missing. That is wrong every step, and the reason is the block's layout:

```
+0x00, +0x01   the two ARM bytes FFX_MesWin_SamplePadPort0 bails out on
+0x02..+0x11   holdCount[16], shared with that sampler
+0x12..+0x27   the nine masks and the two analog bytes      <- the only input
+0x40..+0xBF   holdTimer[16] and lastSampleTime[16], this machine's process clock
```

`FFX_MenuSys_ClearPad` is a flat `memset(block, 0, 0xC0)`. The game only ever calls it on
a module boundary, from `FFX_Module_Stop` and `FFX_MenuSys_Enter`, and gets away with it.
A replicator calling it every step would hold the message window's pad read disarmed and
stop the passenger being able to advance a dialogue box, **with the menu looking perfectly
fine**.

So a zeroed `ffx::MenuPadFrame` goes through `ffx::WriteMenuPad` instead, whose contract is
exactly the nine masks and the two analog bytes. The analog bytes are set to 0x80 and not
0, because zero is full deflection and would synthesise a held Up and Left. The fact that
`MenuPadFrame` has no field for the arm bytes is what makes it the right tool. The kit
comment has been corrected to say all of this.

---

## 5. The Sphere Grid reads the raw pad, and the block does not cover it

This is the one material thing `MENU_SYSTEM.md` has wrong, and it is in its headline
conclusion. The report says the 192-byte block is "the complete menu input surface" and
that "there is no need to touch anything deeper". For module 19 that is not true.

Two functions in the grid read the pad directly:

| function | what it reads | what it does with it |
|----------|---------------|----------------------|
| `FFX_Menu_SphereGridReadPad 0xA57520` | `FFX_MenuSys_ReadAnalogByte(0, 0, 3, -1)` | ORs a synthesised Up (0x1000) or Down (0x4000) bit into the grid's own held mask at `MenuWork + 71276`, at a **+-64** threshold |
| `FFX_Menu_SphereGridReadStick 0xA56BD0` (was `sub_A56BD0`) | axes **2 and 3** | the free cursor pan vector, plus a zoom/speed float at `MenuWork + 71140` and a counter at `+71114` |

`FFX_MenuSys_ReadAnalogByte 0x8BE450` reaches `FFX_Pad__readAnalogByte` and never looks at
the block. So overwriting all 192 bytes still leaves the grid partly driven by whichever
controller is plugged into each machine, and the grid cursor comes apart on the first stick
nudge - on the one screen the requirement cares most about. Note also the threshold is
+-64 on the signed value, not the `0x18`/`0xE8` the menu sampler uses, so the two
synthesised masks are not interchangeable.

**I neutralised it rather than replicating it, because there is nothing to replicate it
with.** `InputFrame` carries a camera-resolved world heading and a magnitude, which is the
right encoding for walking a character and useless for a menu cursor, and adding raw axes
is a wire format change that touches every other subsystem.

`FFX_MenuSys_ReadAnalogByte` checks `FFX_VirtualPad_IsEnabled()` first and returns
`FFX_VirtualPad_GetAxis` when it is set, and `FFX_VirtualPad_Latch` resets all four axes to
0x80 on every frame. So **an enabled but never written ATEL virtual pad reads as a
permanently centred stick**, identically on both machines, and the grid becomes dpad-only.
The dpad does everything the stick does there.

`ServiceGridStickNeutraliser` turns it on while module 19 is stepping and off otherwise,
re-asserting every step because `FFX_MenuSys_StepFrame` calls the engine's own virtual pad
reset on menu entry. It is behind a named `kNeutraliseGridStick` constant.

Costs, stated plainly:

- While it is on, `FFX_MenuSys_SamplePad` takes its virtual branch and stops computing the
  synthesised dpad mask and the repeat. Neither matters, because this file overwrites all
  nine masks, and in that branch the three accessors read `held`/`pressed`/`word10`
  directly, which is simpler and is what we write.
- An ATEL script that wanted to drive the grid through the same virtual pad would be
  fighting us. Scripts do that on both machines equally, so it is a cosmetic risk rather
  than a divergence, but it is a risk.
- Players lose stick panning on the grid in a session.

The other fix, if this one turns out badly, is a detour that serves replicated axes.
Neither of the two functions involved can take a 5-byte steal as it stands: both have a
rel32 call inside the first nine bytes. All of this is now written into the IDB on both
functions, on `FFX_MenuSys_ReadAnalogByte`, and in the new kit comment above
`ffx::SphereGridPadState`.

---

## 6. Pause sync

### The answer to the starvation question, which is both

**Does the lockstep layer keep submitting local input while the Esc menu is open? No.**

`SubmitLocalInput` and `StepGate` are called from `GateCallback`, which is the gate
`ffx::HookMainStep` installs, and that detour is on `FFX_MainStep` itself. When the Esc
menu is open, `FFXApplication::animate` honours the byte at `+0x3AD` and skips
`FFX_MainStepLoop`, so `FFX_MainStep` is never called, so the gate is never called, so
nothing is submitted and nothing is consumed. The pausing machine's clock stops dead.

The animate **hook** still runs, because it runs after the original and the original is
what skips the step loop. So the session keeps pumping, messages keep flowing, and a paused
machine can still send. It just cannot consume a command, because a command is only visible
on the step it was stamped for.

So the other machine runs exactly `InputDelay` more steps, finds nothing in the ring, and
`StepGate` returns `GateWaiting`. **What happens next depends entirely on one flag:**

- **With the gate enforcing**, it starves. Two refused steps and `ServiceLockstep`
  escalates to the hold, so roughly 100 ms after the pause the other machine freezes with
  no explanation at all, indistinguishable from a dropped connection.
- **With the gate only measuring**, which is still the default until shift+F4,
  `GateCallback` returns `!enforced` and it **runs away**, simulating alone for as long as
  the pause lasts. That is an unrecoverable divergence rather than a hitch.

Both are bad for different reasons, which is the case for doing this deliberately.

### The asymmetry, which is the one awkward thing in this feature

The **raise is ordered**: the pausing peer asks for `kCommandPause`, the host stamps it, and
on that step every other machine raises `kHoldPauseMenu`. Both stop on a named step rather
than drifting into a stall.

The **release cannot be ordered, and this is not laziness.** Consuming an ordered command
requires the clock to reach the step it was stamped for, and while everyone is paused
nobody's clock is moving. A resume delivered as a command would be queued on every machine
and consumed by none of them, and the pause would be permanent. I nearly built that.

So the release comes from the frame path, from three conditions that need no step:

1. **Our own Esc menu**, read out of the engine every frame rather than edge-tracked, so
   there is no state to get stuck in.
2. **A paused peer's input starting to flow again.** `Lockstep::PeerInputLead(peer)` is
   `peerInputThrough[peer] - currentStep`. While they are paused both halves are frozen, so
   any increase is them stepping again. That is ground truth, not a guess.
3. **That peer leaving the session**, via `PeerInSession`.

Releasing a few frames apart on the two machines costs nothing, because nothing was
simulated while the hold was up: whichever machine resumes first immediately stalls at the
gate waiting for the other, which is the mechanism that was already there. **The gate is
the guarantee and the hold is the courtesy**, which is why an early release is safe.

**There is deliberately no wall-clock timeout anywhere in this layer.** A pause is a human
action with no sensible duration, and condition 2 is a real observation, so a timer would
only ever fire when it was wrong.

### Why the hold is raised only for a remote pause

A local Esc menu is already holding this machine through the engine's own byte, which
`animate` re-latches from the menu state every frame. Adding a second writer of the same
byte for the same condition would mean our release racing that latch for no benefit. The
one owner of `+0x3AD` is `ServiceLockstep`, and `kHoldPauseMenu` is how this file asks it,
exactly as `kHoldWorldTransfer` already does. No second writer was added.

### Legibility

A remote pause arriving over the internet with no visible cause reads as a dropped
connection. So the held machine logs, by player name:

```
pause: Wakka's Player (peer 1) opened their pause menu on step 41233. YOUR GAME IS HELD
until they close it. This is not a dropped connection.
```

and the `pause:` control panel line says the same while it lasts. The host override does
the same, because a player whose cursor silently stops responding will assume the mod
broke.

`PeerLabel` uses three rotating buffers rather than one static, because two calls in one
`Log` would otherwise both point at the same bytes and turn "the driver changed from A to
B" into "from B to B".

---

## 7. The host override

ctrl+F4, host only, a toggle, and **ordered like everything else**. A local flag flip would
mean the two machines disagreed about the owner for the length of a round trip, and acting
on different owners is the one failure in this subsystem that cannot be recovered from.

The payload carries the **wanted state**, not "toggle", for the reason `BoosterSync`
transmits values rather than keypresses: two commands crossing, or one arriving twice,
would leave the machines on opposite settings. A state is idempotent.

It works whether or not a menu is open, because it only sets a flag that `DecideDriver`
reads as rule 1. The issuer is re-checked against `HostPeer` on the receiving side as well
as at the request, so a stale or hostile client cannot do it by sending the command
directly. Refusals on a client and with no session log a line rather than doing nothing,
because a silent no-op on a keypress reads as a broken mod.

---

## 8. The desync detector

Wired up, not left for later. Two things changed.

**Two extra checksum regions, in the spare slots.** `ChecksumPayload` carries
`parts[16]` and `HashGameState` fills 13, so `MenuHashRegions` writes two more and
`ServiceLockstep` appends them. No protocol change. `Lockstep::HandleChecksum` already
reports "first difference in region N", so `N >= 13` means the menu rather than the save
block. They are XORed into `combined` as well as carried alongside, because `combined` is
the only thing `HandleChecksum` actually compares - a region sent but not folded in would
never trigger a report.

- **Region 13, the shell and the driver.** Active and suspend masks, the screen id, the
  entered and requested modes, all 25 module state integers, **and the resolved driver, the
  opener and the override flag**. Putting the driver in the hash is the part I would keep if
  I had to drop the rest: two machines acting on different owners is the failure with no way
  back, and this catches it on the step it happens rather than by somebody noticing the
  cursor moved in two places.
- **Region 14, the cursor, the grid and the injected input.** Both character fields, because
  `MenuCursorChar` and `SphereGridCharacter` can legitimately differ for a frame during an
  L1/R1 switch and that difference is worth being able to see. Plus the grid's switch state,
  node index and pending cost, the three words of the grid's own pad struct, and the held,
  pressed and repeat masks this file injected.

The grid pad words are there to catch a **known** hole rather than an unknown one: if the
stick neutraliser fails or is turned off, a stick nudge on that screen moves those three
words on one machine and the detector says so on the next step.

**The checksum interval drops to 1 while a menu is up.** A menu desync reproduces, it
happens while nothing else is moving, and the two machines are generating no other
traffic, so there is no reason to find out up to a second later. If the two machines
disagree about whether a menu is open they check at different rates, and `HandleChecksum`
only compares steps they both hashed, which is still every thirtieth - that is the case
where the disagreement *is* the desync, so it still gets caught, just later.

The save-data regions the menu edits are already covered by the existing buckets
(Characters, Equipment, Inventory, Abilities). One note on that: `HashGameState`
deliberately excludes `kBucketItemMasks` from `combined`, with the comment that the item
change masks "diverge harmlessly whenever one machine has a menu open". Under menu sync
they should not diverge any more, so that exclusion is now conservative and hides a real
class of drift. I did not change it - it is `GameState.h`'s call and changing it would
affect every other subsystem - but it is worth revisiting.

---

## 9. The two samplers that share the block

`g_ffxMenuPadBlock` has two writers: `FFX_MenuSys_SamplePad 0x8BE500` and
`FFX_MesWin_SamplePadPort0 0x8B7CD0`, which `DialogueSync` already hooks. I checked whether
they can both be live in a frame rather than letting call order decide.

**They are mutually exclusive, and it is provable from the call graph.**
`FFX_MesWin_SamplePadPort0` has exactly one caller, `FFX_MesWin_StepAll 0x8AB910`, which has
exactly one caller, `FFX_MainStep` at `0x82101F`, inside the sub-step loop in the gameplay
block. That whole block is guarded by `if (v6 == 0 && v7 == 0 && g_ffxMenuOpenPending == 0)`
where `v6` and `v7` are the two menu running flags. And `FFX_MenuSys_SamplePad` is only
reached from `FFX_MenuSys_StepFrame`, which only runs when one of those flags is set or is
being set this step.

Two narrow steps can run both: the step a menu **closes** on (site A steps the menu, the
flag clears, `v6` is re-read as 0 and the gameplay block runs) and, if `dword_1340848` is
ever 1, the step one **opens** on. On the closing step the engine's own
`FFX_MenuSys_ClearPad` fires from `FFX_Module_Stop` in between, so the message window
samples fresh rather than inheriting our masks.

The menu itself is safe by construction regardless: nothing between our write and
`FFX_Module_StepAll` touches the block.

**So instead of asserting a rule nobody can violate, I counted the overlap.**
`PrepareMenuInput` increments `g_dialogueClash` on any step where a menu is up and
`ffx::DialogueOpen()` is true, and it appears in `LogMenuSync`. If it ever climbs, the
precedence between the two files needs stating properly instead of being a consequence of
call order. The flows where it would matter are the ones that reach a menu through a
dialogue - shops (modules 12 and 13) and the save menu (11, 15, 16) are all opened from
ATEL script after a message window.

Related: `FFX_MesWin_SamplePadPort0` has its **own** repeat constants, threshold 0.2333333
with reset 0.13333333, so its interval is 0.1 s, which is 3 steps. The two samplers that
share this block pace differently: the menu is 14 then 5, the message window is 7 then 3.
Do not assume one set covers both.

---

## 10. What `MENU_SYSTEM.md` got wrong or left out

Everything below was found by building against it. The addresses are all correct - I did
not find a single wrong address.

1. **"The repeat mask must be transmitted, not rederived"** (condition 1 of the three named
   conditions, and the kit header's caveat one). Wrong, and wrong twice over: the stated
   interval of "every 0.3 s" is a reset value rather than an interval, so the real interval
   is 0.1667 s / 5 steps, and the shipped pacing is unstable to within a frame on a single
   machine so there is nothing faithful to transmit. See section 4.
2. **"One input surface, 192 bytes"** and "there is no need to touch anything deeper".
   False for module 19, which reads the raw pad analog bytes in two places through
   `FFX_MenuSys_ReadAnalogByte`, bypassing the block. This is the material one, because it
   is load-bearing in the report's "does replicate input, not state survive" conclusion.
   There is a fourth condition. See section 5.
3. **"Simply memset it and write the received masks"** / "wiping it is safe". Safe on a
   module boundary, which is the only place the game does it. Not safe every step: the
   memset covers the message window's arm bytes and the shared hold counters and clock
   timers. See section 4.
4. **The three accessors' runtime branch is not mentioned.** The field table lists
   `+0x12`, `+0x22` and `+0x26` without saying that `GetHeld`, `GetPressed` and `GetRepeat`
   choose between them on `(held & 0xF000)`, which is what makes a two-field injector work
   on the stick and fail on the dpad. See section 4.
5. **The close path was not identified**, and it turns out there is no close function at
   all. Worth having written down, because the absence is what makes a close command
   unnecessary. See section 3.
6. **`FFX_MenuSys_RequestOpen` is described as the replication point** without noting that
   it is not detourable. It also silently refuses when the save UI is busy, which is a
   divergence source the report does not flag. See section 3.

All six are now recorded in the IDB as function comments on
`FFX_MenuSys_SamplePad`, `FFX_MenuSys_ClearPad`, `FFX_MenuSys_RequestOpen`,
`FFX_MenuSys_AnythingStillRunning`, `FFX_Menu_SphereGridReadPad`,
`FFX_Menu_SphereGridReadStick` and `FFX_MenuSys_ReadAnalogByte`, and the database is saved.

---

## 11. What I deliberately did not do

- **No close command.** Section 3.
- **No repeat mask on the wire.** Section 4.
- **No replication of the stick for the menu.** The analog bytes are written centred. The
  dpad carries all the direction the menu needs, and the one screen that wanted a real
  stick is handled by neutralising rather than replicating. Replicating it means a wire
  format change.
- **No interception of the cloud-save or save-UI menu modes.** Logged, not fixed.
- **No second ownership table.** `coop/Ownership.h` is asked.
- **No detour on `FFX_MenuSys_RequestOpen`, `FFX_MenuSys_ReadAnalogByte` or
  `FFX_Menu_SphereGridReadPad`.** All three have a rel32 call inside the bytes a 5-byte jmp
  would steal, and extending `Detour.h` was out of scope.
- **No change to `kBucketItemMasks`.** Section 8.
- **No per-screen ownership beyond the grid.** The brief's rule 3 is "the opener drives",
  and the Equip screen in particular takes its character from the equipment entry's `+4`
  ForChar byte rather than from the cursor, so a per-character Equip gate would need a
  different field. The opener rule covers it correctly for now, it just does not stop one
  player equipping another player's character.
- **No new session message kind and no protocol version bump.** Everything fits the
  existing command channel and the spare checksum slots.

---

## 12. What is untested, ranked by how likely I think it is to be wrong

Everything here is untested. This is the order I would test it in.

1. **The Sphere Grid stick neutraliser.** Highest risk by a distance. It turns on a global
   the engine also writes (`FFX_MenuSys_StepFrame` resets the virtual pad on menu entry)
   and that ATEL scripts use. I re-assert it every step, which should win, but "should win"
   against engine code I have only read is exactly the kind of claim that fails in a game.
   Watch for: the grid cursor not responding at all, or the whole menu going dead the frame
   the grid opens. The fallback is to set `kNeutraliseGridStick` to false, which puts the
   grid back to the known hole rather than an unknown one.
2. **Whether the pause release actually fires.** The mutual case worries me:
   machine A unpauses and needs B's input for its next step, but B is held and therefore
   not submitting either. It should break out because A has `InputDelay` steps of B's input
   already buffered, so A runs two or three steps, B sees A's lead climb, B releases, and
   both resume. That is three steps of slack from a default `InputDelay` of 2. If
   `InputDelay` has adapted downward, or if the buffer is emptier than I think, **both
   machines could sit held forever.** I chose not to add a timeout because a timeout is
   unprincipled here, but if this deadlocks, a frame-counted backstop that simply drops the
   reason and lets the gate take over is the safe fix. Watch for: both games frozen after
   one player closes the Esc menu.
3. **Whether the ordered open actually lands on the same step.** It rests on
   `FFX_MenuSys_PollOpenAndStep` being called exactly once per step when the menu is not
   running, which I read off `FFX_MainStep` and believe, and on `FFX_SaveUi_IsIdle` and the
   fade gate agreeing on both machines, which I cannot know. The failure is loud: the log
   says the open was ordered but not stored. Watch for: a menu opening on one machine only.
4. **The derived repeat feeling wrong.** 14 then 5 is my arithmetic from the decompiled
   loop, and I am confident in the numbers, but I have not watched a cursor scroll. If it
   feels too fast or too slow the two constants are named and adjacent in `MenuSync.cpp`.
   This one is a feel bug, not a divergence: both machines derive it identically either way.
5. **`SpreadMasks` writing the same value into nine fields.** It is the right call for the
   three accessors, but `FFX_MesWin_SamplePadPort0` reads some of the same fields with
   different conventions, and the sticky fields exist because the engine ORs in the previous
   pad ring frame. Making them all equal removes information no reader I found uses, but
   "no reader I found" is doing work in that sentence.
6. **The driver handover mid-grid.** The grid switch writes `+71100` from the cursor when it
   sees L1 or R1 in its own held-or-repeat mask, and `+71101`/`+71102` keep the old value and
   the direction. Control changing hands in the same step the character changes should be
   clean, because both machines read the same byte, but the handover also resets the repeat
   counters, so a player holding a direction across a switch loses the repeat and has to
   re-press. That may be the right behaviour or may be annoying.
7. **The checksum interval of 1 while a menu is up.** Thirty times the checksum traffic for
   as long as somebody has a menu open. The payload is 80 bytes and it is reliable, so at
   29.97 Hz that is about 2.4 KB/s, which is nothing, but it is sent to every peer from every
   peer and I have not measured it on a real link.
8. **`StopNetworking` not reaching the other subsystems' stops.** I found that
   `StepNetworking`'s "was active, now is not" branch never runs on an explicit disconnect,
   because it returns early once the state is Idle. For the pause hold that would have been
   a permanent freeze, so I call `StopMenuSync` and `StopPauseSync` from `StopNetworking`
   directly. **`StopBoosterSync`, `StopBattleSync`, `StopDialogueSync` and `StopWorldSync`
   are still not called on that path.** I did not change them because they are not mine, but
   somebody should.
9. **Clock alignment between host and client.** `BeginLockstep` starts each machine's clock
   from its own `MainStepCounter`, which is a per-process counter. Every ordered command in
   the mod, mine included, assumes the two clocks use the same step numbering. If they do
   not, `effectiveStep` means different things on the two machines. This is pre-existing and
   `BoosterSync` and `BattleSync` share the assumption, so I did not touch it, but my open
   command depends on it being true and it is worth settling.
