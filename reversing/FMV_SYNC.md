# The FMV barrier

Making a movie end on the same simulation step on both machines, and making one player's skip skip it
for everybody.

All addresses in this document are VAs. The project convention is RVA + 0x400000 = VA, so subtract
0x400000 to get the RVA that an `addresses\` file would hold.

The code is `loader\plugins\pilgrimage\world\FmvSync.h` and `.cpp` on the mod side, and
`loader\workshop\include\ffx\Cutscene.h` plus `loader\workshop\src\ffx\Cutscene.cpp` on the kit side.
Wiring touches `net\NetLink.cpp`, `PilgrimageMod.cpp`, `ui\ControlPanel.cpp` and `hooks\FrameHook.cpp`.

**Nothing in here has run in a game.** It compiles clean at /W4 and the reasoning below is from the
binary, but not one line of it has been executed with a movie on screen. Every behavioural claim is a
prediction.

---

## Summary, read this first

**The premise I was handed is wrong, and the whole design follows from that.** `COOP_DESIGN.md` and
`CUTSCENE.md` section 7.2 both say `FFX_MainStep` early-returns while a movie plays, so no simulation
runs and both machines freeze on the same step, which makes an FMV a free barrier. The function that
early return tests has nothing to do with video. **The simulation runs all the way through a movie and
always has.**

**So the start is already fine and needs no command.** Movie:0 start and Movie:10 start are ATEL
opcodes, the VM steps once per simulation step inside the `FFX_MainStep` sub-step loop, and two
lockstepped machines running the same script reach them on the same step. There is nothing to
replicate.

**The END is what breaks, on every single FMV in the game.** Each machine's script unparks when its own
decode thread finishes, which is wall-clock work on hardware the other machine knows nothing about. One
script resumes on step 900 and the other on step 950, and from the next step they are fifty steps apart
forever. That is not a hitch, it is unrecoverable.

**So the barrier goes on the script wait, not on the simulation.** The script's movie waits are ATEL
poll handlers in the Movie library, function slots 1, 9, 10 and 11. Their poll pointers live in
writable `.data`, so the kit swaps four pointers and the shim can answer "not finished yet" by
returning 0, which is exactly what the engine's own waits return while a movie plays. Each machine
reports "mine finished" as an ordered command, every machine sees the last report land on the same
step, and every machine lets its wait through on that step.

**`kHoldFmv` is deliberately never raised.** Holding the simulation stops `FFX_MainStep`, and the mod's
lockstep clock is advanced by that function's after-step observer, so a hold stops the local clock
while the machine still playing its video keeps stepping. See section 3.

**The cancel is `FFX_Fmv_PollSkipButtons 0x6D9460`**, reached from `FFX_Fmv_StepFromMainStep 0x645DF0`
at the very top of `FFX_MainStep`, before the suspend early return. Two presses, Start then Square, both
through `FFX_Input__isPressed`, which is a newly-pressed edge test on the merged keyboard-and-pad action
map with no port in it. Purely local input. See section 4, including the part I got wrong first.

---

## 1. `FFX_Fmv_GetPlaybackState` is misnamed and no simulation step is ever skipped

The address file calls `0x6411E0` `FmvGetPlaybackState`. Here it is in full:

```
mov     eax, g_ffxFmvState            ; 0xCCC830, a pointer
test    eax, eax
jnz     short have_it
push    340h                          ; lazily ALLOCATES the object
call    FFX_Alloc
...
mov     g_ffxFmvState, eax
have_it:
cmp     byte ptr [eax+338h], 0
jnz     short return_two
cmp     byte ptr [eax+33Ch], 0
jnz     short return_two
mov     eax, [eax]                    ; the dword at +0
retn
return_two:
mov     eax, 2
retn
```

It returns 2 when one of two bytes is set, and otherwise the int at `+0`. So the question is only: who
writes those three fields. Four independent answers, and they all say the same thing.

**1. Every writer is user input, and no FMV code is among them.**

| field | the only writers | what it is |
| --- | --- | --- |
| `+0` | the 0x340 constructor, and `FFX_Frame_StepSuspendOverlay 0x680030` forcing it to 0 | the debug free camera, on remappable action 30, mask 0x40000 |
| `+0x338` | the constructor, and `FFX_Frame_ToggleSuspendOverlay 0x680320` | a full-screen suspend overlay on remappable action 32, mask 0x100000. It also pauses audio and calls `FFX_Fmv_TogglePause` |
| `+0x33C` | the constructor, and `FFX_Frame_SetCutsceneSuspend 0x6803A0` | the Start-button in-cutscene pause, reached only from `FFX_Cutscene_StepPauseOverlay 0x8AB340` and `FFX_Cutscene_EnterPauseOverlay 0x8AB3D0` |

The byte-pattern scan that found the `+0x338` and `+0x33C` writers had to be run WITHOUT filtering the
disassembly text on "byte ptr", because `mov [reg+338h], al` does not contain that string. My first scan
had that filter and found nothing, which is how a wrong conclusion nearly survived.

**2. The function that steps the overlay forces the free camera off during a movie.**
`FFX_Frame_StepSuspendOverlay 0x680030` contains `if (g_ffxFmvScriptRunning != 0) state[0] = 0;`. There
would be no reason to write that if the frame were being thrown away anyway, and it means the free
camera can never be the reason the function returns 2 during a movie.

**3. This one settles it with no new reversing at all.** The script's movie waits are ATEL poll
handlers, and the ATEL VM is stepped from inside the `FFX_MainStep` sub-step loop, which is after the
early return. If the step were being discarded, the polls would never be called and **a movie could
never finish**. Movies finish. So the step runs.

**4. `FFX_StepPacing_AllowCatchUp 0x81FD30` goes out of its way to refuse frame skipping while a movie
is up**, which would be pointless work if the step were being skipped regardless.

Renamed in the IDB to `FFX_Frame_GetSuspendState`, with the old name and why it was wrong in the
function comment. The kit also deliberately never calls it, because of that `FFX_Alloc(0x340)` on the
first call: a reader that allocates is not a reader. `ffx::FrameSuspended()` reads the two bytes
directly instead.

---

## 2. What actually breaks, and what does not

**The start does not need a command.** Movie:0 start and Movie:10 start are ATEL opcodes. The VM steps
once per simulation step and both machines run the same script, so both machines issue them on the same
step. I looked for a reason this would not hold and did not find one.

**The body does not need anything either.** The simulation keeps stepping in lockstep, the video is
decoded on `PVideoPlaybackWin32::_VideoDecodingThread 0xA27F20` with a `GetTickCount` timebase, and
nothing simulated reads any of it.

**The exit is the whole problem.** The four waits and what each actually tests:

| slot | what it is | completion test |
| --- | --- | --- |
| Movie:1 | wait for the open movie to finish | `FFX_Fmv_IsPlaying()` or `FFX_Fmv_IsDecoderBusy()` |
| Movie:9 | wait, fullscreen | `FFX_Fmv_IsDecoderBusy()` or `FFX_Fmv_PlaybackFinished()` |
| Movie:10 | play a movie and wait | `g_ffxFmvScriptBlocking` and then the same pair as Movie:9 |
| Movie:11 | wait or skip | `FFX_Fmv_IsFinishedOrSkipped()` |

Every one of those bottoms out in state the decode thread owns. So each machine leaves its wait on
whatever simulation step its own hardware happened to finish on.

Movie:1 is the only one of the four with **side effects on its done path**: it clears
`g_ffxFmvScriptRunning`, writes `dword_C40E2C` and calls `sub_63DF30`. That is why the kit's shim for
Movie:1 mirrors the engine's own test itself rather than calling the original speculatively, and calls
the original exactly once, on the step the barrier opens. The other three are pure reads, so their
shims call the original every step and use its answer as the completion test.

---

## 3. Why the hold byte is the wrong tool, and why the trap in the brief does not apply

The brief warned, correctly, that **an ordered command can never be used to release a barrier**, because
`Lockstep::CommandsForStep` matches the current step exactly and nobody's clock advances while anybody
is held, so a command stamped for a future step is queued everywhere and applied nowhere. `PauseSync`
is built around that and its header explains it. I read that file carefully, as asked, and copied its
shape in three places: the three-rotating-buffer `PeerLabel`, the announce-once bookkeeping, and the
rule that Stop must release whatever is held.

**But nothing here is held, so the trap does not bite, and the reason is worth stating rather than
leaving to look like an oversight.**

`kHoldFmv` exists and `FmvSync.cpp` never raises it. Raising a hold makes `MainStepHook` refuse the
step, and refusing the step means no original, no after-step observer, and therefore no
`clock.AdvanceStep()`. The local lockstep clock stops. For the Esc menu that is right, because every
machine stops together. For a movie it is exactly wrong: **the machine still playing its video has to
keep stepping**, because the ATEL VM steps inside `FFX_MainStep` and that is what polls the wait. Hold
the finished machine and its clock stops while the playing machine's runs on. When the hold lifts the
two are on different step numbers, applying each other's input frames to the wrong steps, for the rest
of the session.

So in this barrier every clock keeps running the whole time, an ordered command is consumed on exactly
the step it was stamped for, and **the release IS the ordered command**. The warning is still right
about the mechanism it was written for.

One consequence of never raising a hold: there is no `AddTimingTrackPauseMs` compensation to do here,
because nothing stalls `FFX_MainStep` and so the SyncData timing track never falls behind.

---

## 4. The cancel, which was the research

### Where it is

`FFX_Fmv_PollSkipButtons 0x6D9460`. Found by walking down from `FFX_MainStep` rather than up from the
input layer: `FFX_Fmv_StepFromMainStep 0x645DF0` is called at `0x820B2F`, at the very top of
`FFX_MainStep` and before the suspend early return, it does nothing unless the manager's IsPlaying byte
is set, and then it runs the skip poll followed by `FFX_Fmv_StepPresentation 0x6D7680`.

### Two presses, not one

```c
if (mgr[1746] && !mgr[1870] && mgr[1744] && g_ffxFmvScriptRunning == 1)
{
    if (!mgr[1868] && mgr[1869])
    {
        mgr[1868] = FFX_Input__isPressed(0x800);          // START arms the prompt
        if (mgr[1868]) sub_6E78A0(...);                   // and puts it on screen
    }
    if (mgr[1868] && FFX_Input__isPressed(0x80))          // SQUARE performs the skip
    {
        sub_6E78C0(...);                                  // hide the prompt
        mgr[1760] = 65534; mgr[1752] = 65534;             // frame indices past the end
        v = sub_A27C40(mgr + 56); sub_A27E10(v, HIDWORD(v)); // seek the player to its duration
        g_ffxFmvSkipRequested = 1;
        mgr[1808] = 1;
        if (!mgr[1824]) mgr[1744] = 0;                    // a video is playing -> no
        mgr[1868] = 0;
    }
}
```

So `mgr+1869` is "this movie may be skipped" and `mgr+1870` is "skipping is forbidden", which is how the
gating reads.

### Why a skip is local and has to become a command

`FFX_Input__isPressed 0x630CF0` is `FFX_Input__maskIsPressed(FFX_Input__get(), action)`, and
`maskIsPressed` is `(mask & prev) == 0 && (mask & cur) != 0`. A newly-pressed edge test on one
remappable ACTION index, against the single global input object, which merges keyboard and pad into one
map with no port and no player argument. There is no second answer available on PC. So without a
networked command each player would skip their own copy and nobody else's, which is the exact
behaviour the brief said to avoid.

### The part I got wrong first, and how I caught it

My first answer was that `g_ffxFmvSkipRequested 0xCDED31` is a one-byte replication primitive, because
the global has five xrefs and three of them are readers that act on it. That is in the kit header I
wrote earlier in the session and in an IDB comment, and **it is wrong**. All five xrefs:

| function | what it does |
| --- | --- |
| `FFX_Fmv_PollSkipButtons` | sets it to 1 |
| `FFX_Fmv_CreatePlayer 0x6D9C80` | clears it, per movie |
| `FFX_Fmv_DrawFrame 0x6D7570` | reads it and stops drawing. Immediate, and real |
| `FFX_Fmv_IsFinishedOrSkipped 0x6DA240` | reads it and returns 1. This is Movie:11's test, so Movie:11 really does end on the flag |
| `FFX_Fmv_StepPresentation 0x6D7680` | reads it, and it is unreachable |

That last one is the error. The keep-presenting condition is

```c
if (mgr[1790] != 0 || mgr[1764] == 0 || (time < duration && g_ffxFmvSkipRequested == 0))
```

and `mgr+1764` is PlaybackComplete, which is 0 for the whole of normal playback. The second term short
circuits, so the third is never evaluated and **the flag never makes the presentation step take its stop
branch**. I had written the opposite in the function comment.

Meanwhile Movie:1 tests `FFX_Fmv_IsPlaying`, and Movie:9 and Movie:10 test
`FFX_Fmv_PlaybackFinished 0x645DC0`, which is `mgr[1744] ? mgr[1764] : 1`. None of the three reads the
flag. So **setting the flag alone would have ended Movie:11 and left Movie:1, Movie:9 and Movie:10
playing to the natural end of the video.** The remote-cancel path and the give-up path would both have
silently not worked.

What actually ends a movie is **clearing the manager's IsPlaying byte at `mgr+1744`**. That makes
`FFX_Fmv_PlaybackFinished` return 1, makes `FFX_Fmv_IsPlaying` false, and stops
`FFX_Fmv_StepFromMainStep` from doing anything at all, since it is gated on the same byte. The engine
has a one-line helper that does nothing else, `FFX_Fmv_StopPlaying 0x6DAC40`, called from Movie:4's
poll.

I caught it by reading `FFX_Fmv_StepPresentation` line by line while writing this document up, rather
than by any test, which is uncomfortable and worth saying plainly.

### So `ffx::RequestFmvSkip` writes the whole set

`FrameIndexPrev` and `FrameIndex` to 65534, `PresentationStopped` to 1, `g_ffxFmvSkipRequested` to 1,
`IsPlaying` to 0 when `StopSuppressed` is clear, and `SkipPromptUp` to 0. Same fields and same order as
the Square press.

Two things it does not do, both deliberate. The **Phyre seek** to the player's own duration needs two
unnamed methods on the embedded player at `mgr+56` whose signatures are inferred from one call site
where the `this` pointer appears to survive in ecx across a call, and it changes nothing that matters:
clearing IsPlaying stops `FFX_Fmv_StepFromMainStep`, so the presentation step is never called again and
the player's time is never read again before teardown. The engine's own skip gets no further use out of
the seek either. And the **prompt-hide draw call** `sub_6E78C0` is a UI object the kit has no wrapper
for, while the flag it would be hiding is cleared anyway, which is what `FFX_Fmv_DrawFrame` reads.

`mgr+1824`, which guards the IsPlaying clear, is only ever written to 0. I scanned `.text` for every
`mov byte ptr [reg+720h], imm` encoding and the only two writers are `FFX_Fmv_CreatePlayer` and the
manager constructor, both writing 0. The guard is still honoured rather than assumed away, because if
it ever were set the real button path would leave the movie playing too and matching the engine is the
honest choice.

### The local skip is not suppressed

A press skips the local movie immediately, the way the shipped game does, and then `FmvSync` announces it
as a command and the other machines apply the same skip when it lands. That costs the other player a
couple of hundred milliseconds of video they were about to lose anyway, and it cannot cause a divergence
because the barrier still decides which step both scripts resume on. Suppressing it would have meant
either scribbling the engine's input snapshot or re-implementing the Phyre seek, for nothing.

---

## 5. What I built

### Kit side, `ffx/Cutscene.h` and `src/ffx/Cutscene.cpp`

New readers, all guarded, all game thread:

- `FmvInProgress()`, `FmvScriptBlocking()`, `FmvPlaybackComplete()`, `FmvPlaybackFinished()` (which
  replicates the engine's own `mgr[IsPlaying] ? mgr[PlaybackComplete] : 1`), `FmvSkipRequested()`,
  `FmvDecoderBusy()` and `FrameSuspended()`.
- `FmvState` extended with `playbackComplete`, `scriptBlocking`, `skipRequested`, `skipPromptUp`,
  `skipAllowed` and `paused`.
- `FmvManager` offsets extended with `PlaybackComplete 0x6E4`, `Paused 0x6FC`, `SkipPromptUp 0x74C`,
  `SkipAllowed 0x74D`, `SkipForbidden 0x74E`, `FrameIndex 0x6D8`, `FrameIndexPrev 0x6E0`,
  `PresentationStopped 0x710` and `StopSuppressed 0x720`.
- `FmvStateLayout` rewritten with the three writers documented and `Overlay`/`Cutscene` aliases.

`FmvPlaying()` was changed to read only the manager's IsPlaying byte. It used to OR in
`playbackState == 2`, which was the misnaming leaking into a reader.

`FmvDecoderBusy()` needed a fix after the first draft. `FFX_Fmv_GetManager` returns 0 with a TTY warning
before the singleton exists and `FFX_Fmv__isDecoderBusy` does `mov esi, [ecx+4]` with no null check, so
calling it unconditionally would fault early in startup. It resolves and checks the manager pointer
first.

The writer is `RequestFmvSkip()`, section 4.

The hook is `HookFmvWaits(gate)`. `FFX_Atel_SysFuncPoll 0x877730` is
`*(fn*)(g_ffxAtelSysFuncLibs[lib] + 16 * func + 4)` and it reads the slot fresh on every call, so
swapping four pointers in `g_ffxAtelSysFuncLib11_Movie 0xC40E30` is enough and no code is patched. That
table is in `.data` with write permission, checked. Install is two passes, all four or none, because a
half-installed barrier would gate some of the script's movie waits and not others and the two machines
would then disagree about which. It refuses if a slot is unreadable, null, or already points at our own
shim.

The gate signature is `bool (int movieFunction, bool localComplete)`. It is called on **every** poll, not
only once the wait has finished, and when `localComplete` is false the return value is ignored. That was
another fix to the first draft: calling the gate only on completion left no per-step tick during the
movie itself, and without one there is no step on which an ordered cancel command could be consumed.

### Mod side, `world/FmvSync.cpp`

Three phases, all decisions made on the simulation step inside the gate.

**Idle.** Draining commands and watching for a local skip. When a wait reports complete, arm: set
Waiting and send `kFmvReasonDone`.

**Waiting.** The engine's wait is finished and the gate answers 0, so the script stays parked. Each step
drains `CommandsForStep(commands, 8)` once, guarded on the step number because the sub-step count can be
above one and draining twice would double-apply a cancel. Reports fill a per-peer mask, **including our
own coming back**, which is what makes the open step identical everywhere. The open happens inside the
drain, on `command.step`, not on the gate's current step. They are the same number today, since
`CommandsForStep` only ever hands back commands for the current step, but using the command's own step
makes "both machines open together" true by construction.

**Open.** Normally spent in the same gate call that opened it, because the drain that opened it runs
first and the wait is let through immediately. It only lingers on the give-up path, where a forced skip
needs a step or two before the engine's own wait agrees, and that is what
`kFmvBarrierOpenSteps = 8` covers.

**There is no barrier identity in the match.** That was the first design and it was wrong. Reports
carried a sequence number that had to stay in agreement between machines, and the case that breaks it is
exactly the case the number was for: a machine that never parks never opens a barrier, so its counter
falls permanently out of agreement and every later report is thrown away as stale. It is not needed
anyway, because a report is stamped `CommandLead` steps ahead and consumed on exactly that step, and the
barrier opens on the step the last report lands, so no report for a barrier can still be in flight when
it opens. The counter is still carried in the payload, for the log only.

**The payload** is 8 bytes against `MaxCommandBytes` of 96, with a `static_assert`. It lives in
`FmvSync.h` and not in `net/Commands.h`, since `RequestCommand` takes a void pointer and a length.

**Arming is unconditional**, which is the other thing I changed my mind about and the one most worth
reading. See section 7.

### Wiring, four files, minimal

- `PilgrimageMod.cpp`: `InstallFmvSync()` next to the other install calls, non-fatal, with a log line
  that names the consequence.
- `net/NetLink.cpp`: `StartFmvSync()` in `StepNetworking`'s became-active branch, `StopFmvSync()` in
  **both** stop sites, the became-inactive branch and `StopNetworking`, because those are two separate
  call sites and `StopNetworking` is the only one that runs on a deliberate disconnect.
- `hooks/FrameHook.cpp`: `StepFmvSync()` on the frame path next to `ServiceCoopConfigStep`, and
  `LogFmvSync()` in the ctrl+F11 block.
- `ui/ControlPanel.cpp`: an `fmv` readout line next to `dialogue`.

The frame-path call deliberately decides nothing. It exists because a session can go away while a
script is parked and the thing that would normally release it is a command from a peer that is no longer
there, and the frame path still runs then.

---

## 6. The four hazards that were called out

**A cancel and a natural end are not the same event.** Separate command reasons. A cancel makes
everybody skip, and then everybody's movie ends and everybody reports done through the ordinary path, so
the barrier logic itself never has to know a cancel happened. Two players cancelling on the same frame
produces two cancel commands and the second is a no-op, because `g_cancelHandled` is set before the
send, so our own cancel coming back is not treated as a second one, and applying a remote cancel does
not re-announce. One machine cancelling while the other is still playing is the normal case and is what
the design is for.

**No wall-clock timeout.** `kFmvBarrierTimeoutSteps = 600`, about 20 seconds at 29.97 Hz, counted in
simulation steps from the step the LOCAL wait finished. Two things follow. It is a number both machines
can compute and it does not depend on anybody's frame rate. And because it is measured from the end of
the local movie rather than its start, **the length of the movie does not come into it**: a five minute
video and a five second one both leave the same allowance for two decode threads to disagree with each
other. The brief's warning about `ArrivalTimeoutMs` being 20 seconds for LAN reasons was what pushed me
to measure from the end rather than the start.

**A machine that never reaches the end.** On the timeout the waiting machine sends
`kFmvReasonGiveUp`, and when that lands **every** machine forces the skip and the barrier opens on that
command's step. The stuck machine has its movie skipped out from under it, which is what makes its own
wait finish, and the open window is what lets that finish get through. Logged with the peer named, as a
deliberate outcome. If the give-up itself cannot be sent, the machine releases itself and logs that the
two games may now be out of step, because at that point there is nothing left that could agree on
anything.

**The frozen machine must say why.** Nothing freezes, which removes most of the problem, but the script
is parked and both the control panel line and the log name the reason and the peer by name, using
PauseSync's rotating-buffer `PeerLabel`. The readout has a separate wording for the case where this
machine never had a video to begin with, so it does not tell a player their video finished when they
never saw one.

---

## 7. The decision I reversed, and the one asymmetry that is left

The first design read local FMV state to decide whether a wait was worth arming. The helper is still
there, `MovieWasInvolved()`, and it is lenient: any of hasPlayer, playing, decoderBusy, scriptRunning,
scriptBlocking or skipRequested. The reasoning was that arming unnecessarily costs a command round trip
and failing to arm costs a desync, so lean towards arming.

**That is backwards, and realising why changed the outcome of the hardest case.**

The answer is read out of LOCAL state. The only case where two machines disagree about it is the case
where a video failed to open on one of them, because Movie:10 start sets `g_ffxFmvScriptRunning` and
`g_ffxFmvScriptBlocking` **only on success** and Movie:10's poll completes on its first look when
blocking is 0. So machine A has a movie and arms, machine B has none and does not, A parks waiting for a
report B is never going to send, and A sits there for the full 600 steps before forcing a skip. Twenty
seconds of stall and a script offset, produced by the cleverness.

So the gate now **arms for every one of the four waits and asks nothing about local state**. The machine
with no video parks for as long as the other machine's video runs, both come out of the wait on the same
step, and all the player on the broken machine loses is the video. What that costs when nothing is wrong
is one command round trip, about 200 ms of parked script per movie wait, on a wait that is already
seconds to minutes long.

`MovieWasInvolved()` is now a log signal only: a false at the moment of arming is the signature of
`FFX_Fmv_StartVideo` having failed here, and it is said loudly, because a player staring at a black
screen for two minutes deserves to be told their video file is the problem.

An earlier cut also watched the frame path for "the script says a movie is blocking but there is no
player object" as the same signature. **That is a false positive after every normal movie.**
`g_ffxFmvScriptBlocking` is set by Movie:10 start and the only thing in the whole binary that clears it
is `FFX_Fmv_AbortForSceneChange`, so it stays set long after the player has been destroyed. Removed.

The one genuinely local open that is left: if a peer we are waiting for LEAVES the session, the barrier
opens on a local step, because a peer dropping is not an ordered event. With two players there is no
survivor left to disagree with. With three there are two survivors who could open a step or two apart.
It is logged for what it is rather than hidden.

---

## 8. What I deliberately did not do

- **No suppression of the local skip.** Section 4.
- **No handling of the in-cutscene pause overlay.** `FFX_Cutscene_EnterPauseOverlay 0x8AB3D0` is the
  Start-button pause during an event scene or a movie. It is local input on the same merged action map,
  it sets the cutscene suspend field that makes `FFX_Frame_GetSuspendState` return 2, and when the
  current movie allows the menu or `g_ffxFmvScriptBlocking` is set it calls `FFX_Fmv_TogglePauseGlobal`
  so the video and its audio stop. **That is a real lockstep hazard and it is not mine.** It is a
  general cutscene pause rather than an FMV thing, and it wants the same treatment the Esc menu got in
  `menu/PauseSync.cpp`, not a special case buried in the movie code. Commented in the IDB as a co-op
  hazard so whoever picks it up finds it.
- **No Phyre seek in `RequestFmvSkip`.** Section 4.
- **No pacing work.** There is nothing to compensate because nothing stalls `FFX_MainStep`.
- **No command for the FMV start**, and no check on the video id either way. Section 2.
- **No unhook.** `HookFmvWaits` installs once and the gate returns true with no session, so a solo game
  is the shipped game. There is no `UnhookFmvWaits`, which is consistent with the rest of the kit but
  means a hot reload of the DLL would leave four dangling pointers. Nothing in the project hot reloads.
- **`FFX_Fmv_IsFinishedOrSkipped` is not wrapped on its own.** Movie:11's shim uses the original's
  return value as its completion test, so the mod never needs to ask the question itself.

---

## 9. Parked addresses, and everything changed in the IDB

### Parked RVAs

Three, in a commented `ParkedRva` namespace inside the anonymous namespace of
`loader\workshop\src\ffx\Cutscene.cpp`, because they are not in any `addresses\` file and I was told not
to edit those. All three verified against the IDB by name after the fact.

| RVA | VA | what it is |
| --- | --- | --- |
| `0x008DED31` | `0xCDED31` | `g_ffxFmvSkipRequested`. Set by the skip handler, cleared per movie by `FFX_Fmv_CreatePlayer`, read by three functions |
| `0x00D2A00C` | `0x112A00C` | `g_ffxFmvScriptBlocking`. Set to 1 only by Movie:10 start, cleared only by `FFX_Fmv_AbortForSceneChange` |
| `0x00840E30` | `0xC40E30` | `g_ffxAtelSysFuncLib11_Movie`, the Movie library's syscall table. 16 bytes per function, `[start, poll, resf, resi]`, so the poll pointer for function N is at `+ N*16 + 4`. Writable `.data` |

`g_ffxAtelSysFuncLib11_Movie` is the one that most wants promoting into
`addresses\Cutscene.h`, since the whole barrier hangs off it.

Also used and already present: `Rva::FmvState`, `FmvPlayerManager`, `FmvIsPlaying`, `FmvIsDecoderBusy`,
`FmvScriptRunning`, `FmvCreatePlayer`, `FmvGetManager`. `Rva::FmvGetPlaybackState` is still in the
addresses file under its wrong name and nothing in my code calls it.

### Renames

| address | was | now |
| --- | --- | --- |
| `0x6411E0` | `FFX_Fmv_GetPlaybackState` | `FFX_Frame_GetSuspendState` |
| `0x6DAC40` | `sub_643E50` | `FFX_Fmv_StopPlaying` |
| `0x6D6970` | `sub_6D6970` | `FFX_Fmv_Manager_Construct` |

Earlier in the session the same pass named `FFX_Fmv_PollSkipButtons`, `FFX_Fmv_StepFromMainStep`,
`FFX_Frame_StepSuspendOverlay`, `FFX_Frame_ToggleSuspendOverlay`, `FFX_Frame_SetCutsceneSuspend`,
`FFX_Cutscene_StepPauseOverlay`, `FFX_Cutscene_EnterPauseOverlay`, `FFX_Fmv_DrawFrame`,
`FFX_Fmv_StepPresentation`, `FFX_Fmv_PlaybackFinished`, `FFX_Fmv_IsFinishedOrSkipped` and
`FFX_Fmv_AbortForSceneChange`.

### Function comments written or corrected

All as repeatable function comments, database saved.

- `FFX_Frame_GetSuspendState`: the old name, why it was wrong, and the three writers.
- `FFX_Fmv_PollSkipButtons`: the two presses, every field the skip writes, the gating bytes, the
  local-input problem, and **a correction**: the tail used to say setting `g_ffxFmvSkipRequested` was
  enough to replicate a skip. It now says which waits that would and would not have ended.
- `FFX_Fmv_StepPresentation`: **a correction**. It used to say the skip is consumed there and that the
  flag makes it take its stop branch. It now spells out the short-circuit that makes the flag
  unreachable during playback, and points at `mgr+1744` as the byte that actually ends a movie.
- `FFX_Input__isPressed`: a newly-pressed edge test on one action index, with no port and no player
  argument, and why that makes every caller local-only input.
- `FFX_Cutscene_EnterPauseOverlay`: what it does and the co-op hazard, flagged as not handled.
- `FFX_Fmv_StopPlaying` and `FFX_Fmv_Manager_Construct`: new, including the `.text` scan result for
  `mgr+1824`.
- The four Movie poll handlers, Movie:10 start and Movie:0 start all carry a CO-OP BARRIER POINT note
  with the table arithmetic in it and a pointer at this document.

---

## 10. What is untested and what I think is most likely wrong

Ranked worst first. None of this has run.

1. **Whether the whole thing is even reached.** `HookFmvWaits` swaps four pointers after checking the
   table is readable and the slots are non-null, and `FFX_Atel_SysFuncPoll` reads the slot fresh on
   every call, so the mechanism should hold. But if the table address is wrong for the shipped build, or
   if some other dispatch path exists that I did not find, the barrier silently does nothing and FMVs
   desync exactly as they do today. The first thing to check in a game is the install log line and then
   `FmvWaitsWithheld()` going above zero on a real movie.

2. **The second-biggest thing I got wrong was only caught by re-reading, not by testing.** The
   `g_ffxFmvSkipRequested` mistake in section 4 was a confident, written-down, committed-to-the-IDB
   wrong answer that survived several hours. The honest conclusion is that the other confident claims in
   here have the same failure mode, and the most likely place for another one is the exact behaviour of
   `RequestFmvSkip` on a machine whose decode has genuinely failed, which is the one path I reasoned
   about entirely from the shape of the code.

3. **`FmvWait01Hook` duplicating Movie:1's test.** Because Movie:1 has side effects on its done path, the
   shim mirrors `FFX_Fmv_IsPlaying() || FFX_Fmv_IsDecoderBusy()` itself instead of calling the original
   speculatively. If that mirror is not byte-for-byte the same condition, Movie:1 either completes a step
   early, which loses nothing, or never completes, which hangs that script. It is two calls and both are
   wrapped, so I rate this fairly low, but it is the only place the kit re-implements an engine
   predicate.

4. **The 200 ms per wait from arming unconditionally.** I argued it is invisible because it lands at the
   end of a movie during a fade. If it turns out to be a visible hitch on short Movie:11 waits, the fix
   is not to re-introduce the local-state test, it is to let a machine pre-report when it can see the
   wait coming, which is a bigger change.

5. **A very short movie starting and finishing inside the eight-step open window.** The window is
   consumed by the first completed wait, so the normal path cannot leak into a later barrier at all. The
   residual case is a give-up open followed by a movie that both starts and finishes within 270 ms, which
   would skip that movie's barrier. I think no movie is that short and I did not check.

6. **Three players.** Everything is written for `MaxPlayers`, the mask is per peer, and the timeout and
   give-up paths do not care how many peers there are. The one known soft spot is the peer-dropped open
   in section 7, which is a local step. Two players is the target and the only configuration I reasoned
   about carefully.

7. **Whether a Movie:9 and Movie:1 pair behaves like a Movie:10.** Movie:9's resi sets
   `g_ffxFmvScriptRunning`, Movie:0 start clears it, and Movie:1's poll clears it again on its done path.
   I traced the flags but I did not work out which script patterns actually use which pair, so the
   Movie:9 and Movie:1 combination is the least exercised in my own head.

8. **`mgr+1790`**, the first term of the keep-presenting condition. Unidentified. If it can be set during
   normal playback then my reading of that condition has another branch in it I have not accounted for.

9. **Interaction with the in-cutscene pause overlay.** If one player pauses during a movie, that machine
   stops its video and its own wait stops completing, and the barrier will hold the other machine's
   script at the wait and then give up after 20 seconds. That is a bounded, logged outcome rather than a
   hang, but it is the wrong outcome and the right fix is in section 8.

---

## 11. Errors in the notes I was handed

Every agent on this project has found a real mistake in the notes they were given. Here are mine, and
one of them is in a file I had already edited myself earlier in the same session.

### `COOP_DESIGN.md`, "Cutscenes replicate. An FMV needs a barrier."

> An FMV does not and does not need to. It is WebM on its own thread with a `GetTickCount` timebase, but
> `FFX_MainStep` early-returns while it plays, so no simulation runs at all. Both machines freeze on the
> same step, which makes a barrier the natural and cheap answer rather than a compromise.

**Wrong, and it is the load-bearing sentence.** Section 1. The barrier is still the right answer but for
a completely different reason, it goes somewhere else, and it is not free.

Research item 4 on the list, "Where a button press cancels an FMV. The observables are known, the skip
path is not", is answered. Section 4.

### `reversing\CUTSCENE.md`

- **Section 7.2, "No simulation runs during an FMV."** Wrong, with the detail that
  `g_ffxMainStepCounter` and `g_ffxGameClock60Hz` do not advance and no ATEL opcode executes. The ATEL
  claim is the one that disproves itself, because a movie's own wait is an ATEL opcode.
- **Section 7.2's three design points.** Point 1, agreement on when it starts, is right about
  `g_ffxFmvScriptRunning` flipping on the same opcode, and is unnecessary: nothing needs to be agreed.
  Point 2's barrier is right in spirit and in the wrong place, on the simulation rather than the script
  wait. Point 3 is right.
- **Section 7.2's flag list**, "9 and 10 set it, 0, 1 and 4 clear it", is nearly right and incomplete.
  Movie:9 sets `g_ffxFmvScriptRunning` from its **resi** slot and not its start, Movie:10 sets it from
  its start **and only on success**, and `FFX_Fmv_AbortForSceneChange` is a fourth clearer that is not
  listed.
- **Section 3's table row** for `FFX_Fmv_GetPlaybackState`, "2 means an FMV owns the screen",
  trusted by "`FFX_MainStep`, which skips all simulation". Both halves wrong. The row's own detail is
  accurate, so the facts were already on the page and the interpretation on top of them was not.
  Section 7.3's table repeats it.
- **Section 10's "Whether a mode-1 track ever spans an FMV"** reasons from "`FFX_MainStep` returns
  before `FFX_StepPacing` on the FMV path, so the track cannot advance during a movie". The premise is
  wrong, so the question is still open but for the opposite reason: the track CAN advance during a
  movie.
- **Section 10's "The FMV skip path"**, listed as unsettled, is now settled. Section 4.
- **Section 7.3's manager table** is correct as far as it goes and misses PlaybackComplete at `+0x6E4`,
  which is the field Movie:9 and Movie:10 actually wait on.

### `loader\workshop\include\ffx\addresses\Cutscene.h`

Its FMV banner repeats the "no simulation runs" claim and the name `FmvGetPlaybackState`. I was told not
to edit that file. **It needs the banner rewritten and ideally the constant renamed**, because every
future reader starts there. Both are listed here rather than done.

### `loader\workshop\include\ffx\Cutscene.h`, which I wrote earlier this session

The SKIPPING A MOVIE block said setting `g_ffxFmvSkipRequested` was a usable one-byte primitive and that
`FFX_Fmv_StepPresentation` takes its stop branch on it. Wrong, section 4. Corrected in the header, in
`RequestFmvSkip`, and in two IDB comments. It is listed here because the next person to read that header
may have read the old version.
