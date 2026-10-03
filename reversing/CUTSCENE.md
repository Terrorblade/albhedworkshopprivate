# Cutscenes, dialogue and FMV

How FFX plays an event scene, puts a message box on screen, waits for a button, and plays a movie.
Written for the co-op requirement "everything happening on the host should happen on the client,
cutscenes included, and either player should be able to advance a dialogue box".

All addresses in this document are VAs. The project convention is RVA + 0x400000 = VA, so subtract
0x400000 to get the RVA that lives in `loader\workshop\include\ffx\addresses\Cutscene.h`.

Everything below was read out of the IDB by decompiling and by walking every xref to each global.
Where something is inference rather than something read in the code, it says so.

The library side of this is `ffx/addresses/Cutscene.h`, `ffx/Cutscene.h` and `src/ffx/Cutscene.cpp`.

---

## Summary, read this first

Seven answers, in the order they were asked.

1. **What puts the game into `g_ffxPacingMode` 1, and is it a cutscene?**
   Answered. `FFX_StepPacing_LoadTimingTrack 0x821D60` is the **only** writer of `g_ffxPacingMode`,
   and it is called unconditionally by `FFX_StepPacing` every frame that `g_ffxPendingSteps` drained
   to zero. It reads the next entry of a **SyncData timing track**. If a track is selected and has
   entries left, mode 1. Otherwise mode 0. A track gets selected by
   `FFX_SyncData_SelectTimingTrack`, whose **single caller in the binary** is
   `FFX_Atel_CallActorScript0 0x867230`, the ATEL opcode that starts another actor's script entry.
   The data file is `/FFX_Data/GameData/<lang>Data/SyncData/SyncList.txt` plus per-map sync data,
   and the other tables in the same file are read by ATEL core syscalls 213, 214 and 215, whose own
   debug prints are `"VoiceStandbyInit!"`, `"VoiceStartInit!"` and `"VoiceStartResult!"`.
   **So yes, it is a cutscene, and specifically a voiced one.** A cutscene with no sync entry never
   leaves mode 0. See section 2.

2. **And the good news nobody expected.** Mode 1 is wall-clock *paced* but it is not
   non-deterministic in *content*. `FFX_MainStep_PublishStepScale 0x8206C0` takes
   `g_ffxStepScale88` straight out of `g_ffxTimingTrackStepTicks` while mode 1 is active, which is
   authored data from the file. Both machines consume the same track entries in the same order and
   get the same per-step motion scale. Only the number of steps per presented frame differs.
   **A mode-1 cutscene can be lockstepped by taking over the step count.** See section 2.4.
   And the engine already ships the compensation a stall needs: `g_ffxTimingTrackPauseMsPending
   0xCCC868`, which the Esc menu writes on close and `FFX_StepPacing` subtracts from the track's
   elapsed time. See section 2.5.

3. **Is a cutscene observable from one read?** There is no single "a cutscene is running" boolean.
   There are four cheap reads that together cover it, and the engine itself trusts three of them:
   `FFX_Atel_MesWinBlockingKind 0x86C8B0` (a dialogue box is up, and whether it is waiting on the
   player), `g_ffxPacingMode 0x12FB8A0 == 1` (a voiced cutscene segment is pacing itself),
   `FFX_Fmv_GetPlaybackState 0x6411E0 == 2` (an FMV owns the screen) and `g_ffxFmvScriptRunning
   0x112A008` (the script's own view of a movie). See section 3.

4. **Where is "advance the dialogue box" read?** `FFX_MesWin_OnConfirmPressed 0x8B64A0` does
   `test byte ptr g_ffxMenuPadHeldTrig+2, 20h`, which is the **newly pressed** half of the shared
   menu pad block at `MenuPadBlock + 0x14`, bit `0x20`, which is Circle. One global, one bit, no
   port and no player id. The block is filled by `FFX_MesWin_SamplePadPort0 0x8B7CD0` from
   `FFX_Pad__readButtons16(0, 0, 0)`, **port 0 hardcoded**. The hook point for "either player can
   advance" is a detour on that sampler: call the original, then OR the remote confirm bit into the
   pressed mask. Its prologue is a clean 5 bytes. See section 4.

5. **The dialogue box.** ATEL core syscalls 100 (set message), 106 (show), 107 (close), on an array
   of 8 window records of 44 bytes at `g_ffxAtelMesWinRecords 0x1326D70`, double buffered. Message
   ids resolve through `FFX_Atel_GetMessageText 0x86BF30` against the event package's own message
   table, not through `FFX_Text_GetLocalizedById`, which turned out to be the Esc menu's path only.
   Core 124, 125, 132 and 489 are the blocking waits that hold the script until the box closes.
   See section 5.

6. **Is an ATEL cutscene deterministic?** Yes, in the way that matters. `FFX_Atel_StepOnce
   0x88D3D0` runs inside the **FFX_MainStep sub-step loop**, so the opcode VM advances once per
   simulation step and never off a real clock. It draws from the seeded `FFX_Rand_Stream 0x7988F0`
   through exactly four script-visible syscalls, so the RNG stream has to be synced but nothing else
   does. See section 6.

7. **FMV cannot be lockstepped and has to be a barrier.** The video is **WebM, parsed by libwebm's
   `mkvparser`, decoded on its own thread** (`PVideoPlaybackWin32::_VideoDecodingThread 0xA27F20`)
   with a `GetTickCount` timebase and a 29.97 fallback frame rate. The simulation is not running at
   all while it plays, because `FFX_MainStep` early-returns on `FFX_Fmv_GetPlaybackState() == 2`, so
   there is nothing to keep in step. What has to be agreed is when it ends and whether anybody
   skipped it. See section 7.

8. **The cutscene camera is one global per screen, not per player.** All 66 ATEL camera syscalls go
   through `FFX_Came_GetHandleForClass 0x7BA780`, which caches one handle per (class, screen) in four
   arrays. A cutscene uses class 3, Event, cached in `g_ffxCameEventHandles 0x1136F94`. The client
   sees the host's framing for free as long as the same script runs. If two views are ever wanted,
   the escape hatch is the game's own 3-screen support, not a second camera class. See section 8.

**Verdict for the lockstep plan: a cutscene replicates, an FMV is a barrier.** Details in section 9.

---

## 1. The three separate things called "a cutscene"

They have nothing in common mechanically, so keep them apart.

| thing | what runs it | pacing | observable |
| --- | --- | --- | --- |
| an ATEL event scene | the opcode VM, `FFX_Atel_RunScript 0x8641E0`, stepped from `FFX_MainStep` | one step per simulation step | `g_ffxAtelCtx` per-context step counter at ctx+500 |
| a voiced line inside one | the same VM, but `FFX_StepPacing` switches to the SyncData timing track | the millisecond clock, with a busy-wait | `g_ffxPacingMode == 1` |
| an FMV | a Phyre WebM player on its own thread | decode speed and `GetTickCount` | `FFX_Fmv_GetPlaybackState() == 2` |

A dialogue box is a fourth thing again, driven by ATEL syscalls but stepped by its own module inside
the sub-step loop. It can be up outside a cutscene, which is why "a message window is open" is not
the same question as "a cutscene is running".

---

## 2. Pacing mode 1, which was the project's biggest open determinism question

### 2.1 Every writer of `g_ffxPacingMode 0x12FB8A0`

There are exactly two, and both are in the same function.

```
0x821DA5  FFX_StepPacing_LoadTimingTrack   mov g_ffxPacingMode, 0
0x821E04  FFX_StepPacing_LoadTimingTrack   mov g_ffxPacingMode, 1
```

Readers: `FFX_StepPacing` at 0x821EE7 and 0x822110, `FFX_MainStep_PublishStepScale` at 0x8206D2, and
`FFX_AtelSys_Core_215_resi` at 0x85E090. That is the whole list.

So one function decides the mode, and it is called from `FFX_StepPacing` **every frame that
`g_ffxPendingSteps` is zero**, unconditionally, before the mode is tested:

```c
if (g_ffxPendingSteps == 0) {
    g_ffxCatchUpExtraSteps = 0;
    FFX_StepPacing_LoadTimingTrack();        // <- decides the mode, every frame
    if (g_ffxPacingMode == 1) { ... the clock path ... }
    else                      { ... the accumulator path ... }
}
```

**The mode is not a sticky setting.** It is re-derived continuously from whether a timing track has
entries left. That is better news than "something turns it on" would have been, because it means a
mod can watch one int and know exactly which window it is in.

### 2.2 FFX_StepPacing_LoadTimingTrack 0x821D60, written out

```c
void FFX_StepPacing_LoadTimingTrack(void)
{
    int step88 = 0;
    if (FFX_SyncData_ReadNextTimingEntry(&g_ffxTimingTrackNextTime,
                                        &g_ffxTimingTrackTolerance,
                                        &step88) == 999) {
        g_ffxTimingTrackDueTime   = 0.0f;
        g_ffxTimingTrackElapsed   = 0.0f;
        g_ffxTimingTrackNextTime  = 0.0f;
        g_ffxPacingMode           = 0;              // <- back to the accumulator
        g_ffxTimingTrackTolerance = 0.0f;
        g_ffxTimingTrackFirstStep = 1;
        g_ffxTimingTrackStartMs   = 0;
        g_ffxCatchUpExtraSteps    = 0;
        memset(g_ffxTimingTrackStepTicks, 0, 0x78);  // DWORD[30]
    } else {
        g_ffxPacingMode = 1;                         // <- the wall clock path
        g_ffxTimingTrackStepTicks[g_ffxCatchUpExtraSteps] = step88 << 7;
        g_ffxTimingTrackNextTime  *= 1000.0;
        g_ffxTimingTrackTolerance *= 1000.0;
    }
}
```

999 is both "no track" and "end of track", because the terminator triple in the file is
`(1.0f, 1.0f, 999)` and the reader special-cases it and nulls the cursor.

### 2.3 Where the track comes from, the whole chain

```
map load
  maybe_FFX_Scene_Init 0x88E31B
    -> FFX_SyncData_RequestForMap 0x67A8E0 (mapId, 1..500)
         checks the id against SyncList, async-enqueues the map's sync data,
         CLEARS the track cursor, so a map change always drops to mode 0

ATEL script runs a call-actor-script opcode (0x36, 0x45..0x49)
  FFX_Atel_CallActorScript0 0x867230
    pops three operands, then unconditionally
    -> FFX_SyncData_SelectTimingTrack 0x67A9B0 (word_112D67C, op1, op2, op3)
         matches that 4-tuple against the map's sync table
         ON A HIT it sets the track cursor at mgr+8          <- ARMS MODE 1
         on a miss it does nothing and the game stays in mode 0

every frame, from FFX_StepPacing
  FFX_StepPacing_LoadTimingTrack 0x821D60
    -> FFX_SyncData_ReadNextTimingEntry 0x67A930
         reads 12 bytes from the cursor: float dueTime, float tolerance, int step88
         returns 999 at the terminator and nulls the cursor                <- BACK TO MODE 0
```

`FFX_SyncData_SelectTimingTrack` has **exactly one caller**, which is what makes the answer clean.
`word_112D67C` is a save-data field, written by `FFX_InitNewSaveData` and
`FFX_Debug_ApplyViewerSave`, and it is the first key of the lookup. Observed comparison values
elsewhere in the binary are 734, 777, 1470, 2920 and 3135, which is consistent with the story
progress counter rather than a map id, **but that identification is not proved** and nothing in this
document depends on it.

The manager object, `g_ffxSyncDataMgr 0xCE9014`, 0x44 bytes, allocated by `FFX_SyncData_Create`:

| offset | meaning |
| --- | --- |
| `+0x04` | array of track data pointers, indexed in parallel with the key arrays |
| `+0x08` | **the timing track cursor. Non-null is what makes mode 1 possible** |
| `+0x0C` | the currently loaded map id |
| `+0x10` | number of track-select entries |
| `+0x14` | number of voice-sync entries |
| `+0x18` | number of map ids in SyncList |
| `+0x1C` | this map has sync data at all |
| `+0x20` | the async load has finished |
| `+0x24` `+0x28` `+0x2C` `+0x30` `+0x34` `+0x38` `+0x3C` | the key and value arrays |
| `+0x40` | the SyncList array of map ids |

`FFX_SyncData__readNextTimingEntry 0x6FB160` **busy-waits on `+0x20` in 5 ms sleeps**, so the first
track read after a map change can block the frame thread while the file is still loading. Worth
knowing because it looks like a hitch and is not one.

### 2.4 What mode 1 actually does, and why it is still deterministic per step

```c
g_ffxPendingSteps = 1;
if (g_ffxTimingTrackFirstStep == 1) {
    g_ffxTimingTrackStartMs        = FFX_Time_NowMilliseconds();
    g_ffxTimingTrackDueTime        = g_ffxTimingTrackNextTime;
    g_ffxTimingTrackFirstStep      = 0;
    g_ffxTimingTrackPauseMsPending = 0;
    g_ffxTimingTrackPauseMsTotal   = 0;
} else {
    now = FFX_Time_NowMilliseconds() - (PauseMsPending + PauseMsTotal);
    PauseMsTotal += PauseMsPending;  PauseMsPending = 0;
    g_ffxTimingTrackElapsed = (float)(now - g_ffxTimingTrackStartMs) / 1000.0f;
    if (g_ffxTimingTrackDueTime <= g_ffxTimingTrackElapsed) {
        // BEHIND: take extra steps until the track catches up
        while (g_ffxTimingTrackTolerance < Elapsed - DueTime && g_ffxPacingMode == 1) {
            ++g_ffxPendingSteps;
            ++g_ffxCatchUpExtraSteps;
            g_ffxTimingTrackDueTime = g_ffxTimingTrackNextTime;
            FFX_StepPacing_LoadTimingTrack();        // consume another entry
        }
    } else if (DueTime - Elapsed >= 0.1f) {
        // AHEAD by 100 ms or more: BUSY-WAIT in a for(i=0;i<10000;++i) spin
        do { spin 10000; re-read the clock; } while (DueTime - Elapsed >= 0.1f);
    }
    g_ffxTimingTrackDueTime = g_ffxTimingTrackNextTime;
}
```

And the part that matters, in `FFX_MainStep_PublishStepScale 0x8206C0`:

```c
g_ffxStepScale88 = (g_ffxGameClock60Hz - previous) << 7;          // mode 0: integer derived
if (g_ffxPacingMode == 1)
    g_ffxStepScale88 = g_ffxTimingTrackStepTicks[g_ffxCatchUpExtraSteps - g_ffxPendingSteps];
```

`g_ffxTimingTrackStepTicks 0x12FB828` is `DWORD[30]`, filled one entry per catch-up step from the
`step88 << 7` field of the file. `FFX_Mot_AdvanceFrame` scales motion playback by
`g_ffxStepScale88`, so in mode 1 **the amount of animation each simulation step advances comes out of
the file, not out of the clock**.

That is the whole determinism picture for mode 1:

- the **content** of step N of a track is identical on both machines, because the track entry at
  index N is the same bytes on both
- the **number of steps per presented frame** is a function of the millisecond clock and therefore
  is not
- the busy-wait burns CPU but changes nothing about state

So lockstep does not need to fight mode 1. It needs to own the step count, which it already does
through `g_ffxPendingSteps` (`ffx::RequestCatchUpSteps`). The track then advances the agreed number
of entries on both machines.

**This is read out of the control flow, not observed in a running game.** The falsifiable version of
it: during a voiced cutscene, log `g_ffxPacingMode`, `g_ffxCatchUpExtraSteps` and
`g_ffxStepScale88` every frame on two machines with different frame pacing. The step scale sequence
should match entry for entry even when the per-frame counts differ.

### 2.5 The pause compensation the engine already ships

`g_ffxTimingTrackPauseMsPending 0xCCC868` is a qword. The only non-`FFX_StepPacing` writer is the
Esc menu step, `FFEscMenu` vtable slot 0 at `0x68FFB0` (the IDB name says `scalar_deleting_dtor`,
which is wrong, the function is the per-frame step). Near its end:

```c
if (escMenuOpen) {
    if (!wasOpen) pauseStartMs = FFX_Time_NowMilliseconds();
} else if (wasOpen) {
    g_ffxTimingTrackPauseMsPending = FFX_Time_NowMilliseconds() - pauseStartMs;
}
wasOpen = escMenuOpen;
```

`FFX_StepPacing`'s mode 1 path folds that into `g_ffxTimingTrackPauseMsTotal 0x12FB7E8`, zeroes the
pending value, and subtracts the total from the elapsed time. So **the game has a supported way to
tell a running timing track "pretend these N milliseconds did not happen".**

A lockstep gate that stalls the simulation during a voiced cutscene must write its stall duration
there, or the track will believe it fell behind and burn catch-up steps the moment the gate opens.
`ffx::AddTimingTrackPauseMs()` does the write. In mode 0 nothing reads or clears the pending value,
so a write made outside a track survives until the next track starts, which is harmless but means
the value should be written close to when it is needed.

### 2.6 The voice syscalls, for completeness

The other tables in the same SyncData file are read by `FFX_SyncData_LookupVoiceSync 0x67A9D0`,
which takes a voice id and a kind, 1, 2 or 3, and returns a per-kind value. The three callers are:

| site | debug print | kind |
| --- | --- | --- |
| `FFX_AtelSys_Core_213_start 0x85CD00` | `"VoiceStandbyInit!"` | 1 |
| `FFX_AtelSys_Core_214_start 0x85D6B0` | `"VoiceStartInit!"` | 2 |
| `0x85DE80`, the 214 result handler | `"VoiceStartResult!"` | 3 |

and `FFX_AtelSys_Core_215_resi 0x85E090` is the one that reads `g_ffxPacingMode` directly:

```c
int FFX_AtelSys_Core_215_resi(void) { if (g_ffxPacingMode != 1) sub_887780(); return 0; }
```

In other words the script's own voice-status command falls back to a manual advance when there is no
timing track. That is the strongest single piece of evidence that mode 1 means "a voiced line is
being kept in sync with its audio". The debug prints are gated on `dword_12FB80C == 1`, which is a
voice debug flag.

These handlers keep their positional `FFX_AtelSys_*` names in the IDB on purpose, per the convention
in `FFX_GAME_NOTES.md`, with the voice meaning recorded as a comment instead.

---

## 3. Can a mod tell "a cutscene is running right now"

Not from one boolean. There is no such flag in the binary, and the obvious candidate,
`g_ffxPlayerControlEnabled 0xC496D8`, turned out to be a dead end: its only writer,
`FFX_Player__setControlEnabled 0x82DB10`, has **zero call sites in the whole executable**, so it is
an exported plugin API that the shipped game never uses.

What does exist is four cheap reads, and the engine itself consults three of them:

| read | type | meaning | who trusts it |
| --- | --- | --- | --- |
| `FFX_Atel_MesWinBlockingKind 0x86C8B0` | `int (void)` | 0 no box, 1 a box is up, 2 a box is up and waiting for a button | `FFX_StepPacing_AllowCatchUp` and `FFX_Atel_StepFrame` |
| `g_ffxPacingMode 0x12FB8A0` | `int` | 1 means a voiced cutscene segment is pacing itself | `FFX_StepPacing`, `FFX_MainStep_PublishStepScale` |
| `FFX_Fmv_GetPlaybackState 0x6411E0` | `int (void)` | 2 means an FMV owns the screen | `FFX_MainStep`, which skips all simulation |
| `g_ffxFmvScriptRunning 0x112A008` | `int` | the script's own view of a movie | `FFX_Time_ScaleDtForBooster`, `FFX_AtelOp_WaitFrames_poll`, `FFX_Atel_StepMoveCmd` |

`FFX_Atel_MesWinBlockingKind` is the best of the four and it is worth reading in full:

```c
int FFX_Atel_MesWinBlockingKind(void)
{
    for (int i = 0; i < 8; ++i) {
        char *w = &g_ffxAtelMesWinRecords[352 * g_ffxAtelMesWinBufSel + 44 * i];
        int state = *(u16 *)(w + 20);
        if (state == 2 || state == 1) {
            if (w[29] & 0x22) return 2;                       // waits for a button
            if (*(u16 *)(w + 22) != 0 || (w[29] & 0x10)) return 1;
        }
    }
    return 0;
}
```

Pure reads of two globals, no allocation, no call out. Safe to call from anywhere. Its prologue
contains an absolute address so it is not a clean 5-byte detour target, but nothing needs to detour
it.

**For "do not accept gameplay input right now", the composite is the right answer**: any of those
four non-zero, OR'd with the pause and menu gates `ffx/MainLoop.h` already exposes.
`ffx::GameplayInputShouldBeIgnored()` is that one call.

For a desync cross-check during a cutscene there is also a per-context step counter at
`g_ffxAtelCtx + 500`, incremented once per step by `FFX_Atel_StepContextRange 0x8688F0` while the
context's flag bit 8 is set. That is the closest thing to "how far into this cutscene are we".

---

## 4. Advancing a dialogue box, which is the input question

### 4.1 The read

`FFX_MesWin_OnConfirmPressed 0x8B64A0`:

```asm
008B64A0  push    ebp
008B64A1  mov     ebp, esp
008B64A3  test    byte ptr g_ffxMenuPadHeldTrig+2, 20h    ; <- THE READ
008B64AA  jz      short loc_8B64B7
008B64AC  push    [ebp+arg_0]
008B64AF  call    FFX_MesWin_AdvanceText                  ; 0x8B8CA0
```

`g_ffxMenuPadHeldTrig` is at VA `0x25D09D2`, which is `MenuPadBlock 0x25D09C0 + 0x12`, the shared
menu pad block the in-game menu work already documented. `+0x12` is the held mask and `+0x14` is the
newly-pressed mask, so `g_ffxMenuPadHeldTrig+2` is the pressed mask, and bit `0x20` is
`ffx::Btn::Circle`.

So **the dialogue advance is MenuPadBlock + 0x14, bit 0x20**. One global, one bit, no port, no player
id. The same function also tests `0x400000`, the pressed-mask bit `0x40`, Cross, elsewhere in the
module, which is the "skip to the end of the line" and choice-cancel path.

`FFX_MesWin_OnConfirmPressed` is installed at `textObject + 252` by `FFX_MesWin_ResetDrawState
0x8ADA30` and reached from `FFX_MesWin_RunConfirmHandler 0x8B6B70`, which only calls it while
`textObject + 44 == 2`, the "text fully drawn, waiting for the player" sub-state.

### 4.2 Where the mask comes from, and the two co-op hazards

`FFX_MesWin_SamplePadPort0 0x8B7CD0`, called as the very first thing in `FFX_MesWin_StepAll
0x8AB910`:

```c
LOWORD(g_ffxMenuPadHeldTrig) = FFX_Pad__readButtons16(0, 0, 0);          // PORT 0 HARDCODED
HIWORD(g_ffxMenuPadHeldTrig) = cur & (prev ^ cur);                       // the pressed edge
LOBYTE(g_ffxMenuPadAnalog)   = FFX_Pad__readAnalogByte(0, 0, 2, 0);      // PORT 0 HARDCODED
BYTE1(g_ffxMenuPadAnalog)    = FFX_Pad__readAnalogByte(0, 0, 3, 0);
... synthesise dpad bits from the stick into the +0x22 pair ...
... 16 hold timers off FFX_Input__getTimeSeconds, first repeat 0.2333 s then 0.1333 s ...
```

**Hazard one: port 0 only.** The second player's controller never reaches a dialogue box. That is
the same collapse the rest of the input layer has, so it is not a surprise, but it is confirmed here
for this specific path.

**Hazard two: wall clock.** The repeat timers use `FFX_Input__getTimeSeconds`, which is Phyre's
process clock. Two machines pressing the same buttons will compute different repeat masks. A single
confirm press is **not** affected, because that is a pure edge test on the mask. Only held
directions in a choice list are.

**This is a second sampler of the same block.** `FFX_MenuSys_SamplePad 0x8BE500` is the in-game
menu's, documented in `MENU_SYSTEM.md` with repeat delays of 0.4667 s and 0.3 s.
`FFX_MesWin_SamplePadPort0 0x8B7CD0` is the message window's, with 0.2333 s and 0.1333 s. They write
the same 0xC0-byte block. That matters to whoever owns the menu replication, so it is called out
here rather than left to be rediscovered.

### 4.3 What a mod should hook

**Detour `FFX_MesWin_SamplePadPort0 0x8B7CD0`.** Call the original, then OR the remote player's
confirm bit into the pressed mask at `MenuPadBlock + 0x14` and, if the remote is holding it, into
the held mask at `+0x12`. `FFX_MesWin_StepAll` samples and then immediately walks the 8 windows, so
a value written inside the sampler hook is exactly what the advance read sees, in the same step.

Its prologue is clean:

```
008B7CD0  55           push ebp
008B7CD1  8B EC        mov  ebp, esp
008B7CD3  51           push ecx
008B7CD4  56           push esi
```

Five bytes, four instructions, no branch and no absolute address, so it relocates to a trampoline
unchanged and the expected-bytes check is stable across ASLR.

Three alternatives, rejected and worth recording so nobody tries them twice:

- **Detour `FFX_MesWin_OnConfirmPressed` itself.** Its prologue is
  `55 8B EC F6 05 D4 09 5D 02 20`, which embeds the absolute address of the global, so a 5-byte jump
  splits the `test` and the expected-bytes check is not ASLR-stable. It is also per-window, so it
  fires 8 times in a scan.
- **Detour `FFX_MesWin_StepAll 0x8AB910`.** Its prologue is `56 57 E8 <rel32>`, a relative call at
  offset 2. A 5-byte detour clobbers it.
- **The ATEL virtual pad** (`VirtualPadEnabled 0x146A430` and friends, ATEL syscalls 501, 502, 556
  and 562). It is global and all-or-nothing: while it is on, the real controller cannot reach the
  menu at all. Right for "the passenger watches", wrong for "either player may advance".

---

## 5. The dialogue box itself

### 5.1 Three ATEL core syscalls

| library:function | handler | what it does |
| --- | --- | --- |
| core:100 | `FFX_AtelOp_MesWinSetMessage 0x857870` | pops a window index and a message id, stores `FFX_Atel_GetMessageText(id)` into `record+8` and `record+12`, and `FFX_Atel_GetMessageAttr(id)` into `record+22` |
| core:106 | `FFX_AtelOp_MesWinShow 0x858B50` | sets `record+20 = 1` and `record+33 = 1`, then builds and activates the draw object |
| core:107 | `FFX_AtelOp_MesWinClose 0x859060` | calls `FFX_Atel_MesWinRequestClose 0x8640F0`, which sets `record+20 = 3` and `record+30 = -1` |

The usage counts recorded on those handlers by an earlier pass are 32982 uses in 359 files for
core:100, 33038 in 359 for core:106 and 32223 in 348 for core:107. That is the whole game's dialogue.

**So yes, there is one function that opens a box and one that closes it**, and they are
`FFX_AtelOp_MesWinShow 0x858B50` and `FFX_Atel_MesWinRequestClose 0x8640F0`. The close one is the
better hook of the two because it has nine call sites and catches the script-driven close, the
wait-syscall close and the player-driven close alike.

### 5.2 The window records

`g_ffxAtelMesWinRecords 0x1326D70`, 8 records of 44 bytes, **double buffered** as two 352-byte
blocks with `g_ffxAtelMesWinBufSel 0x1326B82` picking the live one. Always read through
`FFX_Atel_GetMessageWindow 0x86BE90` or apply the 352-byte stride yourself.

| offset | size | meaning |
| --- | --- | --- |
| `+0x00` | 4 x s16 | the box rect, x y w h |
| `+0x04` | ptr | a default text style pointer, set by `FFX_Atel_MesWinResetAll` |
| `+0x08` | ptr | message text |
| `+0x0C` | ptr | message text, same value |
| `+0x14` (20) | u16 | **the state. 0 idle, 1 shown, 2 waiting on the player, 3 closing, 4 finished** |
| `+0x16` (22) | u16 | the message attribute from the message table |
| `+0x1C` (28) | u8 | a style index |
| `+0x1D` (29) | u8 | flags, see below |
| `+0x1E` (30) | s16 | the chosen option, -1 for none |
| 31 to 35 | u8 | the choice window's geometry, passed straight to the draw module. Which byte is which was not established. |

Flag bits in byte 29. The byte is computed by the show syscall from its operand, and the three that
`FFX_Atel_MesWinBlockingKind` and `FFX_AtelSys_Core_125_start` act on are proved by what the code
does with them. The other two only select a draw call, so their meaning is inferred.

| bit | status | meaning |
| --- | --- | --- |
| `0x08` | proved | selects the 4-short rect draw instead of the 2-short one |
| `0x10` | proved | counts as "a box is up" but does not wait for the player |
| `0x22` | proved | the box waits for a button before it will close |
| `0x20` | proved | the choice wait syscall ORs this in |
| `0x02` | inferred | selects one extra draw call, and only when `0x40` is clear |
| `0x40` | inferred | selects a different one and suppresses `0x02` |

The state values 0, 1 and 3 were read directly from the writes. **2 and 4 are inferred** from
`FFX_AtelSys_Core_125_poll`, which reports completion on 0 and 4 and reads the player's choice on 2,
and from `FFX_MesWin_RunConfirmHandler` gating on the draw object's own `+44 == 2`. No direct write
of 2 or 4 to `record+20` was found, which suggests the draw module writes it through a pointer the
decompiler did not resolve. Treat 0, 1 and 3 as proved and 2 and 4 as strongly implied.

### 5.3 Where the text comes from

Not through `FFX_Text_GetLocalizedById 0x68E180`. That function's 60-odd call sites are all
`FFX_EscMenu_Build*Page` and the settings screens, which is a different system entirely. The event
path is:

```c
void *FFX_Atel_GetMessageText(int id)     // 0x86BF30
{
    table = *(int *)(g_ffxAtelCtx + 52);           // the event package's message table
    if (!table) return &unk_1325B74;               // the empty string
    off = *(u16 *)(table + 8 * id);
    if (off == 0) { table = g_ffxEvBuiltinMsgTable; off = *(u16 *)(table + 8 * id); }
    return (void *)(off + table);
}
```

8 bytes per message id: a u16 string offset, a u16 attribute, and a duplicated pair. The first u16 of
a table is the byte size of the record array, so the message count is that divided by 8. All of this
was already documented on the functions by an earlier pass and is repeated here because it is the
answer to "what is the message id".

### 5.4 The blocking waits

Four core syscalls hold the script while a box is up, all of them on
`FFX_Atel_GetMessageWindow`: 124, 125, 132 and 489. 125 is the full one and is worth having:

```c
int FFX_AtelSys_Core_125_poll(int a1, int *state)
{
    int idx = *state;
    char *w = FFX_Atel_GetMessageWindow(idx);
    short chosen = -1;
    if (*(u16 *)(w + 20) == 0 || *(u16 *)(w + 20) == 4) { read the choice; return 1; }   // done
    if (*(u16 *)(w + 20) != 2) return 0;                                                  // keep waiting
    if (drawState(idx) == 3 && subState(idx) == 2) {
        read the choice;
        FFX_Atel_MesWinRequestClose(idx);
    }
    return 0;
}
```

`_start` arms the wait and sets `record+29 |= 0x20`. So the script thread blocks, the window state
machine runs in the sub-step loop, the player presses Circle, `FFX_MesWin_AdvanceText` runs, the
window reaches state 2 with its sub-state 2, and the poll closes it and unblocks the script. All of
that happens on the step clock.

### 5.5 The per-step update

`FFX_MesWin_StepAll 0x8AB910`, called from the `FFX_MainStep` sub-step loop:

```c
int FFX_MesWin_StepAll(void)
{
    FFX_MesWin_SamplePadPort0();
    for (int i = 0; i < 8; ++i) {
        if (*(u16 *)g_ffxMesWinObjects[i] == 0) continue;
        int v = sub_906BD0(i);
        FFX_MesWin_RunStepHandler(i);
        if (v) sub_8B8AF0(i, ...);
        if (*(u16 *)(g_ffxMesWinObjects[i] + 6) == 1) FFX_MesWin_RunConfirmHandler(i);
    }
    if (g_ffxMenuPadHeldTrig == 0x01000100) return sub_8ABD00();   // a held-button shortcut
    return 256;
}
```

Three parallel arrays of 8 pointers each hold the draw state: `g_ffxMesWinObjects 0x1865B3C`,
`g_ffxMesWinTextObjects 0x18676F0` and `g_ffxMesWinChoiceObjects 0x1868A90`. The text object carries
the per-state function pointers at `+248`, `+252` and `+276`.

---

## 6. Is an ATEL cutscene deterministic

### 6.1 It steps on the simulation clock. Chain, proved by walking callers

```
FFX_MainStep 0x820AE0
  the sub-step loop, for i < FFX_Player__getSubStepCount()
    FFX_Atel_StepOnce 0x88D3D0
      FFX_Atel_StepFieldContexts 0x872BD0
        FFX_Atel_StepFieldContexts 0x8666D0  -> FFX_Atel_StepContextRange(0, 6)
        FFX_Atel_StepFieldFrame
      -> per context: ctx+0x4C, which FFX_Atel_InstallContextCallbacks set to
         FFX_Atel_ContextStepCallback 0x867710
           FFX_Atel_StepFrame 0x867950
             per actor: FFX_Atel_RunScript 0x8641E0, the 123-case opcode switch
```

and separately, for the menu context only:

```
FFX_MenuSys_StepFrame 0x8A9CA0 -> sub_873600 -> FFX_Atel_StepMenuContext 0x86DD60
                               -> FFX_Atel_StepContextRange(6, 7)
```

Contexts are 568 bytes each in `g_ffxAtelCtxArray`. `FFX_Atel_StepContextRange 0x8688F0` swaps
`g_ffxAtelCtx` to each one, calls its step callback, and bumps `ctx+500` while the context's flag
bit 8 is set.

**No clock read anywhere on that path.** `FFX_Atel_StepFrame` reads the pad through
`FFX_Atel__samplePadsBothPorts 0x871D70`, which ORs the pad ring history, and that is driven by the
pad layer rather than by time.

### 6.2 The RNG

`FFX_Rand_Stream 0x7988F0` has 50 call sites. Exactly four of them are script-reachable:

| caller | library:function |
| --- | --- |
| `FFX_AtelSys_Core_166_resi 0x85741F` | core:166 |
| `FFX_AtelSys_Core_169_resi 0x857682` | core:169 |
| `FFX_AtelSys_Came_128_resi 0x7B89DA` and `0x7B89FC` | came:128, two draws |
| `FFX_AtelSys_Btl_235_resi 0x7A7D12` | btl:235 |

It is a seeded stream, so the requirement is the usual one: both machines must agree on the seed and
make the same draws in the same order. Since the VM steps in lockstep with the simulation and every
draw comes from a script opcode, the draw order is identical by construction as long as the scripts
run in step.

### 6.3 So the verdict

**An ATEL cutscene replicates for free under lockstep**, given a synced RNG stream and the step
count in agreement. The one caveat is pacing mode 1, and section 2.4 shows why that is a pacing
problem rather than a determinism problem.

---

## 7. FMV

### 7.1 What decodes the video

Not the PS2 movie library. The binary still ships the whole PS2 `movie_*` syscall surface and every
one of them is a stub that prints
`"Virtuos Warning: Movie on Windows, PS3 & PS Vita is not impelemented yet! Call back by the
movie_init"` and so on, 30-odd of those strings at `0xB52268` onwards. Those are the ATEL Movie
library handlers, library 11.

The real player is Phyre's. `FFX_Fmv_CreatePlayer 0x6D9C80`, which is
`PhyFMVPlayerManager::CreateFMVPlayer`, resolves a video id to a path and calls
`PVideoPlaybackWin32___VideoDecodingThread 0xA27F20`. That function:

- calls into `mkvparser__*`, which is **libwebm**, so the container is **WebM / Matroska**
- picks the video and audio tracks out of the segment
- reads the track frame rate and **falls back to 29.97002997002997 when the container does not say**
- stores `1000.0 / fps` as the millisecond frame interval at `player + 264`
- seeds `dword_1A84C80` from `GetTickCount()`
- spawns a thread named `"PVideoPlaybackWin32::_VideoDecodingThread"`, alongside a
  `PVideoPlaybackWin32::_ReadingThread`

`FFX_AtelSys_Movie_011_resi` can force-destroy a player, printing
`"Force destroy fmv player for movieId:%d"`.

**So playback is paced by decode speed and the wall clock, on a thread the simulation does not
own.** It cannot be lockstepped. The owner's two machines with different hardware will not present
the same FMV frame on the same simulation step, ever.

### 7.2 Why that turns out not to matter much

`FFX_MainStep 0x820AE0` does, near its top:

```c
if (FFX_Fmv_GetPlaybackState() == 2) { ... draw the movie ... return; }
```

**No simulation runs during an FMV.** `g_ffxMainStepCounter` does not advance, `g_ffxGameClock60Hz`
does not advance, no RNG is drawn, no ATEL opcode executes. Both machines are frozen at the same
simulation step for the whole movie.

So an FMV is a **barrier**, and a natural one. The design needs:

1. agreement on when it starts. `g_ffxFmvScriptRunning 0x112A008` is the right signal for that,
   because it is set and cleared by ATEL Movie syscalls (9 and 10 set it, 0, 1 and 4 clear it), so it
   flips on the same opcode on both machines.
2. agreement on when it ends, which is the actual barrier. Each machine reports "my movie finished",
   and the simulation does not resume until both have. The read is
   `FFX_Fmv_IsPlaying 0x641CF0`, which is `*(u8 *)(g_ffxFmvPlayerManager + 1744)`, going from 1 to 0.
   `FFX_Fmv_IsDecoderBusy 0x641CE0` covers the drain.
3. a rule for skipping. Either player pressing skip has to skip on both, and the cleanest way is to
   treat a skip as a networked command rather than a local input, the same as any other.

### 7.3 The observables

| read | type | meaning |
| --- | --- | --- |
| `FFX_Fmv_GetPlaybackState 0x6411E0` | `int (void)` | 2 means the movie owns the frame. Lazily builds the 0x340-byte state object at `g_ffxFmvState 0xCCC830`, returns 2 if either byte at `+0x338` or `+0x33C` is set, else the state dword at `+0`. |
| `FFX_Fmv_IsPlaying 0x641CF0` | `int (void)` | `g_ffxFmvPlayerManager + 1744`, set to 1 when a player is created |
| `FFX_Fmv_IsDecoderBusy 0x641CE0` | `int (void)` | any decode slot still active, or the manager's queue not drained |
| `g_ffxFmvScriptRunning 0x112A008` | `int` | the ATEL script's own view. The one to replicate. |

`FFX_StepPacing_AllowCatchUp 0x81FD30` reads `FFX_Fmv_IsPlaying`, so the engine already refuses to
frame-skip during a movie.

Useful manager fields, from `FFX_Fmv_CreatePlayer`:

| offset | meaning |
| --- | --- |
| `+0x4F0` (1264) | the resolved file path, a char buffer |
| `+0x6D0` (1744) | u8, a video is playing |
| `+0x6D2` (1746) | u8, a player object exists and has not been released |
| `+0x6DC` (1756) | int, the video id |
| `+0x6F4` (1780) | float, width |
| `+0x6F8` (1784) | float, height |

### 7.4 The FMV camera

`FFX_AtelSys_Movie_001_resi 0x76EA09` and `FFX_AtelSys_Movie_004_poll 0x76EA7E` both call
`FFX_Came_SetMatrixOverride 0x7C06D0`, so the FMV path takes the `g_ffxCameOverrideFlag 0x23114C4`
route that bypasses every camera mode and priority. That is already documented in
`FFX_GAME_NOTES.md` as the pre-rendered background and FMV path, and this confirms it from the FMV
side.

---

## 8. The cutscene camera

### 8.1 One handle per class per screen

Every one of the 66 `FFX_AtelSys_Came_*` syscalls that needs a camera handle goes through

```c
int FFX_Came_GetHandleForClass(int screenArg, int kind, unsigned slot)   // 0x7BA780
{
    if (slot > 2) return 0;
    switch (kind) {
      case 0: if (!g_ffxCameEventHandles[slot])  g_ffxCameEventHandles[slot]  = FFX_Came_OpenHandle(screenArg, slot, 3); return g_ffxCameEventHandles[slot];
      case 1: if (!g_ffxCameMapHandles[slot])    g_ffxCameMapHandles[slot]    = FFX_Came_OpenHandle(screenArg, slot, 1); return g_ffxCameMapHandles[slot];
      case 2: if (!g_ffxCameBattleHandles[slot]) g_ffxCameBattleHandles[slot] = FFX_Came_OpenHandle(screenArg, slot, 5); return g_ffxCameBattleHandles[slot];
      case 3: if (!g_ffxCameViewerHandles[slot]) g_ffxCameViewerHandles[slot] = FFX_Came_OpenHandle(screenArg, slot, 7); return g_ffxCameViewerHandles[slot];
    }
    return 0;
}
```

| kind | handle class | array | used by |
| --- | --- | --- | --- |
| 0 | 3, Event | `g_ffxCameEventHandles 0x1136F94` | **cutscene scripts** |
| 1 | 1, Map | `g_ffxCameMapHandles 0x1136F88` | field maps |
| 2 | 5, Battle | `g_ffxCameBattleHandles 0x1136FA0` | battle |
| 3 | 7, Viewer | `g_ffxCameViewerHandles 0x1136FAC` | the debug viewer |

Three entries each, indexed by screen 0 to 2. Handles are cached forever once opened, so the array
being non-zero is **not** an "a cutscene is running" test.

### 8.2 What that means for co-op

**The cutscene camera is global.** A cutscene opens a class 3 handle, which outranks the class 1
player camera, writes into the shared camera slot, and both players see the same framing because
there is only one framing. For the owner's stated requirement, "the client should see what the host
sees", that is already the behaviour and nothing has to be done.

The place it bites is the opposite requirement. If a future design wants the passenger to keep their
own view during a cutscene, the only route is the game's own 3-screen support, which
`FFX_GAME_NOTES.md` already covers in detail, including the warning that there is no scissor so
full-screen passes bleed across the divider. Opening a second class 3 handle is not a route: the
priority arbitration picks one winner per screen.

---

## 9. Verdict for the lockstep plan

**A cutscene replicates. An FMV is a barrier.**

### What needs doing for a cutscene

1. Nothing for the script VM itself. It steps on the simulation clock.
2. Sync the RNG stream, which the plan already requires for battle.
3. During pacing mode 1, own the step count. The lockstep layer already writes
   `g_ffxPendingSteps`, and `ffx::TimingTrackState()` reports what the track is doing so the layer
   knows which window it is in.
4. **If the gate stalls during mode 1, write the stall duration to
   `g_ffxTimingTrackPauseMsPending`.** Otherwise the track thinks it fell behind and burns catch-up
   steps the instant the gate opens, which would look like the cutscene lurching. This is the single
   most actionable thing in this document.
5. Replicate the dialogue advance as a networked command, not as local input. Detour
   `FFX_MesWin_SamplePadPort0` and OR the remote confirm bit into `MenuPadBlock + 0x14`.
6. Decide the dialogue input policy. `FFX_Atel_MesWinBlockingKind() == 2` says "a box is waiting for
   a button". Whether either player may press it, or only the player whose script opened it, is a
   design choice the engine does not constrain, because the read has no player id at all.

### What needs doing for an FMV

1. Treat it as a barrier. Both machines stop simulating anyway.
2. Start on the script flag, `g_ffxFmvScriptRunning`, which flips on the same opcode on both.
3. End on both machines reporting `FFX_Fmv_IsPlaying() == 0`.
4. Make skip a networked command.
5. Do not try to pace it. WebM on a decode thread with a `GetTickCount` timebase will never line up
   with a simulation step.

### The one thing that still looks risky

The busy-wait in mode 1. It spins `for (i = 0; i < 10000; ++i)` with the clock re-read between
spins, and it does that **inside `FFX_StepPacing`, inside `FFX_MainStep`, inside `animate`**. A
lockstep gate living in animate runs before it, so the gate is fine. But a gate living in the
`FFX_MainStep` detour runs **after** `FFX_StepPacing` has already spun, so a machine that is ahead of
its timing track will burn up to 100 ms spinning before the gate even gets asked. Use the animate
hook during a cutscene, not the narrow `FFX_MainStep` gate.

---

## 10. What I could not settle

- **The state values 2 and 4 of `record+20`.** No direct write of either was found. Inferred from
  `FFX_AtelSys_Core_125_poll` and `FFX_MesWin_RunConfirmHandler`. Section 5.2.
- **What `word_112D67C` is.** It is the first key of the sync-table lookup and a save-data field.
  Comparison values elsewhere suggest a story progress counter. Not proved, and nothing here depends
  on it.
- **Whether a mode-1 track ever spans an FMV.** `FFX_MainStep` returns before `FFX_StepPacing` on
  the FMV path, so the track cannot advance during a movie, which means a track that is live when a
  movie starts resumes from where it was, with the elapsed time having run on in the meantime. The game
  presumably never authors that combination, but the compensation global exists, so it might. Not
  checked.
- **Whether the engine ever writes `g_ffxTimingTrackPauseMsPending` from anywhere but the Esc menu.**
  Four xrefs total, three of them in `FFX_StepPacing`. So almost certainly no, but the Esc menu
  function itself is another agent's area and I did not read all of it.
- **The FMV skip path.** I established that playback is a separate thread and that
  `FFX_Fmv_IsPlaying` and `g_ffxFmvScriptRunning` are the observables, but I did not find where a
  button press during a movie cancels it. The Movie library's `_poll` handlers are where to look.
- **Whether the two MenuPadBlock samplers can both run in the same frame.** `FFX_MesWin_StepAll`
  runs from the sub-step loop and `FFX_MenuSys_SamplePad` from the menu exec, and
  `g_ffxMenuPadBlock+0` is a "has been sampled" latch that both check. If they can both run, the
  second one wins and the repeat profile flips mid-frame. Worth a look before relying on the block's
  contents outside the message window path.
- **`sub_641DA0 0x641DA0`**, which reads `g_ffxGfxCtx + 0xFAF4` and is compared against values like
  7382 and 9465 inside `FFX_StepPacing_AllowCatchUp`. It is some kind of event or scene identity and
  it is the key to that function's long list of per-scene catch-up exceptions. Not identified.
