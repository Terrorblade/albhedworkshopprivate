# Blitzball

What Blitzball is made of, what in it does not replicate, and what single-instance state blocks a
second human player.

**Addresses in this document are RVAs** (preferred base 0x400000, so RVA = IDA VA - 0x400000).
Where I quote a disassembly line or an existing IDB comment, the address inside the quote is a VA,
because that is what IDA printed, and I say so each time. The IDB itself carries VAs, as it always
has.

The short version is at the top, the derivation is in the middle, every address is listed as an RVA
near the end, and the last section is what I could not settle.

---

## Headline

**Blitzball is not native code and it is not in a magic DLL. It is ATEL event-script bytecode.**

The match lives in `bltz0000.ebp` (5,422,592 bytes, of which 4,647,336 bytes are bytecode, 41
actors, 36 CHRs) and its twin `bltz0002.ebp` (4,642,912 bytes of bytecode). It is run by the same
event-script VM that runs every cutscene and every field map, stepped once per sub-step from inside
`FFX_MainStep`. There are exactly **two** places in all of FFX.exe that branch on "is this
Blitzball", and both are text layout. Nothing else in `.text` knows Blitzball exists.

That is very good news for lockstep. The simulation is already inside the gate, it is a step-driven
script machine with a deterministic instruction budget, it reads no wall clock by any path, and its
randomness comes from one seeded stream the kit already has to sync.

The bad news is the input and ownership model. Every script-visible input read in the Blitzball
packages is hardcoded to **pad port 0**, and the "which actor is the player" concept is a single
`u16` per ATEL context. Those two things, not the physics, are what block a second player.

---

## The six questions, answered

### 1. Where is the simulation, what is its per-step entry, is it inside FFX_MainStep

It is inside `FFX_MainStep`, one level down, and it is interpreted bytecode rather than compiled
code.

The chain, every link verified in the disassembly:

```
FFX_MainStep 0x420AE0
  (sub-step loop, count from FFX_Player__getSubStepCount 0x42D7E0)
  -> 0x42101A  call FFX_Atel_StepOnce 0x48D3D0
       -> FFX_Atel_StepFieldAll 0x472BD0
            -> FFX_Atel_StepFieldContexts 0x4666D0
                 -> FFX_Atel_StepContextRange(0, 6) 0x4688F0
                      -> FFX_Atel__samplePadsBothPorts 0x471D70   (once, before the loop)
                      -> for ctx in 0..5: ctx+0x4C step callback
                           -> FFX_Atel_StepFrame 0x467950
                                -> FFX_Atel_RunScript 0x4641E0    per actor
```

`FFX_Atel_RunScript` is the opcode interpreter. Blitzball's ball physics, AI, pass and shoot
resolution, stat growth and league bookkeeping are all opcodes in that loop. The per-step entry
point a mod would hook is `FFX_Atel_StepOnce 0x48D3D0`, and the single call site is RVA `0x42101A`.

Two things make this safe rather than scary:

- The VM is step-driven, not time-driven. One `FFX_Atel_StepOnce` per sub-step, and a script that
  calls the wait-frames opcode resumes on the next step, not after a number of milliseconds.
- There is a hard instruction budget. In `FFX_Atel_RunScript`, verified in the disassembly (the
  addresses in this quote are VAs, as IDA printed them):

  ```
  0x864390 cmp     eax, 10000h
  0x864394 jle     short loc_8643B4
  0x864396 mov     ax, [ebx+34h]
  0x86439f mov     edx, 1400h
  0x8643a4 or      ax, dx
  0x8643ae mov     g_ffxAtelActorClampCount, ecx
  ```

  An actor that runs more than 65,536 opcodes in one step is force-yielded with flag bits 0x1400
  set and a counter bumped. That clamp is deterministic, it depends only on the opcode count, so it
  fires identically on both machines. `g_ffxAtelActorClampCount 0xF270D8` is a free desync canary.

### 2. The nine wall-clock reads

**They are not Blitzball.** This is an error in the existing notes, and it propagated into
`MAIN_LOOP.md` and `COOP_DESIGN.md`. See the error list at the end of this document.

`FFX_Input__getTimeSeconds` is RVA `0x230C60`, not `0x230C40`, and it has exactly nine distinct
callers, which is where the number nine came from. Classified:

| Caller (RVA) | Name | What it is | Risk |
|---|---|---|---|
| 0x230F40 | `FFX_Input__ctor` | timestamps the input singleton at construction | none |
| 0x231270 | `FFX_Input__clearHoldTimers` | re-bases the hold timers | none |
| 0x2330E0 | `FFX_Input__updateHoldTimers` | key-repeat for held buttons | input layer only |
| 0x28C830 | `sub_68C830` (2 sites) | input thread pacing | thread, not sim |
| 0x28D740 | `FFX_InputThread__main` | the input thread's own loop | thread, not sim |
| 0x28DC37 | inside the same thread body | same | thread, not sim |
| 0x489980 | `sub_889980` | pad layer timestamp | none |
| 0x4B7CD0 | `FFX_MesWin_SamplePadPort0` | message-window key repeat | menu feel only |
| 0x4BE500 | `FFX_MenuSys_SamplePad` | menu key repeat | menu feel only |

Every one is input repeat timing or the input thread. None is reachable from script execution and
none decides anything the simulation branches on. The real gameplay clock consumer in that
neighbourhood is `FFX_Time_AppElapsedSeconds 0x241410` (QueryPerformanceCounter via Phyre), and its
one gameplay caller is Lulu's Fury overdrive, not Blitzball.

So the honest answer to question 2 is: **Blitzball reads no wall clock at all.** I proved that
rather than assuming it, and the method is worth stating because it is reusable.

Reverse reachability, not forward. Starting from eleven clock sources (`FFX_Time_AppElapsedSeconds
0x241410`, `FFX_Input__getTimeSeconds 0x230C60`, `Phyre_Time_NowSeconds 0x2FD80`, `GetTickCount`,
`GetTickCount64`, `QueryPerformanceCounter`, `timeGetTime`, `clock`, `_time64`,
`GetSystemTimeAsFileTime`, `GetSystemTime`) and walking cross-references backwards, which catches
both calls and address-taken references, the set of functions in FFX.exe that can reach a clock at
all is **427 out of 49,128**.

Then I collected every function pointer in the eleven registered ATEL syscall tables by walking the
library bases out of `g_ffxAtelSysFuncLibs 0xF28558` at sixteen bytes per entry, deliberately
over-scanning 1,024 entries per library so the collected set is a superset of the real tables. That
is 2,242 distinct handler functions.

The intersection is exactly **one** function: `FFX_AtelSys_Movie_136_start 0x36F850`, which gets
there through the FMV path. A Movie:136 syscall encodes as the three bytes `B5 88 B0` (syscallf) or
`D8 88 B0` (syscall), and neither sequence occurs anywhere in `bltz0000`'s 4,647,336 bytes of
bytecode or `bltz0002`'s 4,642,912 bytes. Blitzball does issue other Movie-library calls (functions
0, 1, 10 and 11, which look like the open, play, stop and poll set) but never 136.

One more time source that could have bitten and does not. `g_ffxAtelScriptSpeedScale 0xF26BD8` is a
script speed multiplier, but its only two touchers are now named
`FFX_Atel_SetScriptSpeedScale_DEAD 0x471020` and `FFX_Atel_ScaleByScriptSpeed_DEAD 0x46B5E0`,
neither of which has a caller in the image. The value is 1.0 forever. (`g_ffxAtelTextSpeedScale
0xF25B98` is alive and is 2.0, but it only affects message windows.)

### 3. The RNG

One seeded stream, shared with the rest of the game, not a private generator and not `rand()`.

Blitzball's only randomness comes through ATEL syscall `Core:166`
(`FFX_AtelSys_Core_166_resi 0x457400`):

```c
v3 = FFX_Atel_PopInt(a1, a3);
if (v3 <= 0) v3 = 1;
return (v3 * (unsigned)(unsigned __int16)FFX_Rand_Stream(2)) >> 16;
```

That is "random integer in [0, n)" built on **stream 2** of `FFX_Rand_Stream 0x3988F0`. The sibling
`Core:169 0x457680` returns the raw 16-bit draw from the same stream 2 but has zero static call
sites in any Blitzball package, so in practice Blitzball draws only through `Core:166`.

Stream 2's state is one dword in `g_ffxRandStreamState 0xD35EE0` (68 dwords). It is **not** in the
save block, it zero-fills at process start, and it has exactly two writers in the whole binary,
`FFX_Rand_Stream` itself and `FFX_Rand_SeedAllStreams 0x398890`. So:

- Blitzball needs no new RNG work beyond what the kit already does for battle. Sync stream 2 and
  Blitzball is deterministic.
- Stream 2 is shared with other script syscall users, so a client that draws from it out of band (a
  UI preview, a local-only effect) desyncs the match. Treat stream 2 as owned by the lockstep tick.
- The seed comes from the system clock on a new game and lives outside the save file, which is a
  pre-existing problem the kit already knows about, not a Blitzball problem.

### 4. Input

Blitzball's whole script-visible input surface is four syscalls, and **every one of them is
hardcoded to port 0**. Not "defaults to port 0", hardcoded, with the port passed as a literal
constant that no script operand can change.

| Syscall | Handler RVA | What it does | Sites in bltz0000 |
|---|---|---|---|
| Core:076 | 0x45E130 | digital button test, bit N of port 0 held | 11 (9 in actor 33, 2 in actor 36) |
| Core:077 | 0x45E820 | the other digital test | 3 (actors 24 and 30) |
| Core:289 | 0x45D1D0 | analog axis byte, port 0 | 4 (all in actor 27) |
| Core:290 | 0x45D310 | sibling of 289 | 0 |

`Core:289`, verified against the disassembly:

```c
v3 = FFX_Atel_PopInt(a1, a3);
if (v3 < 0) v3 = 0; else if (v3 > 13) v3 = 13;
v4 = FFX_Pad__mapButtonCode(0, 0, v3);
return (unsigned __int8)FFX_Pad__readStagingAnalogByte(0, 0, v4);
```

The script supplies only the axis index. The two leading zeros are the port and the slot. `Core:076`
is the same shape, with `FFX_Atel_GetPadPort0Held 0x46AF30` and `FFX_Pad__remapButtonMask(0, 0, ...)`.

So Blitzball does **not** go through `FFX_Input` directly and it does **not** have its own input
path. It reads the emulated scePad staging block for port 0, which the pad layer fills from the one
`FFX_Input` singleton. There is no port argument and no device argument anywhere in the chain that a
script can reach.

I chased the one hazard this raised, which is whether a replicated logical axis index could resolve
to a different physical axis on a different machine. It cannot, on two independent grounds:

- `FFX_Pad__mapButtonCode 0x488CF0` opens with `if (a3 <= 3 || a3 >= 12) return a3;`, so codes 0..3
  bypass the remap table entirely. Blitzball only ever passes 2 and 3.
- The remap table `g_ffxPadButtonRemapTable 0xF30488` is the identity map anyway. Its only writer is
  `FFX_Pad__setButtonRemapEntry 0x4894F0`, whose only caller is `FFX_Pad__resetPort 0x489360`, whose
  loop is `for (i = 0; i < 16; ++i) setButtonRemapEntry(port, 0, i, i)`. No button-config path writes
  it on this build. (That second fact was established by an earlier pass and is recorded in the IDB
  comment on `FFX_Pad__remapButtonMask 0x488C40`. I re-read it and it holds.)

And axes 2 and 3 are the **left** stick. `FFX_Pad__fillAnalogAndSynthDpad 0x489570` fills the
staging block as +132 = right X, +133 = right Y, +134 = left X, +135 = left Y, and
`FFX_Pad__readStagingAnalogByte 0x488C00` is just `portState[index + 132]`. So `Core:289(2)` is left
stick X and `Core:289(3)` is left stick Y, as raw 0..255 bytes.

That last detail is the stick quantisation trap in its most benign form. The script compares the raw
byte against the literals 20, 66 and 106 (seen in actor 27's bytecode), so what has to match between
peers is the **byte**, not the float. If the kit replicates the quantised byte rather than the float
axis, Blitzball's stick input is exactly reproducible. If it replicates the float and lets each
machine quantise, a value sitting on a boundary can round differently.

Vibration is port 0 only too. `FFX_Atel_StepFieldAll` ends each step with `sub_889490(0, 0, ...)` and
`sub_8894B0(0, 0, ...)`, which write the actuator bytes at `portState+254/255` for port 0. A second
player gets no rumble. Cosmetic, but it is the kind of thing that reads as a bug.

### 5. Per-match state, and how much is in the save block

Two tiers, and the split is clean.

**Persistent: 2,560 bytes, all inside the save block.** `g_ffxBlitzSaveBlock 0xD2DC7C` is
`g_ffxSaveData + 0x11EC`, running to `+0x1BEC`. In ATEL terms it is variable class 0, offsets 0x1000
to 0x1A00, because class 0 resolves to `g_ffxSaveData + 0x1EC + off` (verified in
`FFX_Atel_ResolveVarAddress 0x46C2E0`, case 0 is `*(u32*)(g_ffxAtelCtx + 44) + (desc & 0xFFFFFF)`).

I recovered the shape by taking the union of the class-0 variable descriptors of the Blitzball event
packages. The 44 distinct descriptors tile the range end to end with a single 8-byte gap at 0x144A,
and that end-to-end tiling is what proves it is one structure rather than a coincidence of
addresses. Shape: `s32[60]` twice (the tech learned and known masks), `s32[16]` twice, `u8[6]`,
`u8[60][5]`, `u8[60]` twice, `u8[16]`, `u8[6][8]` (league rosters), `u8[100]`, `u16[100]`, `u16[60]`
four times, `s32[60]` twice, and around twenty scalars. 60 is the player roster, 16 is a team's slot
list, 6 is teams or on-field slots.

It has **no native cross-references at all** except one: the debug cheat
`FFX_Blitz_DebugFullBlitz 0x3845B0`, the PS2 developer menu's "Full Blitz" line. Verified in the
disassembly (the addresses in this quote are VAs, as IDA printed them):

```
0x7845b1 push    3Ch             ; Size
0x7845b3 mov     eax, 0FEFFFF7Fh
0x7845b8 push    5               ; Val
0x7845ba mov     ecx, 78h
0x7845bf mov     edi, offset unk_112DC7C
0x7845c4 push    offset unk_112E00E
0x7845c9 rep stosd
0x7845cb call    memset
```

0x78 dwords of 0xFEFFFF7F at `g_ffxBlitzSaveBlock + 0`, which is 480 bytes, then 60 bytes of 5 at
`g_ffxBlitzPlayerByteArray1392 0xD2E00E`, which is class-0 offset 0x1392. 0xFEFFFF7F is 31 of 32
bits set with bit 24 clear, so "every tech known except one" across two `s32[60]` arrays, and then
every player's level byte set to 5. That reads exactly like an unlock-everything cheat.

The whole 2,560 bytes is inside the 26,816-byte block at `g_ffxSaveData 0xD2CA90`, and
`FFX_SaveFile_Serialize 0x4B3E60` writes a 0x40 header plus a byte-for-byte copy of that block, so
**all Blitzball persistence is already covered by whatever the kit does for the save block.** It
falls in the Progress bucket that `GAME_STATE.md` maps as `+0x00C0 .. +0x3D0C`.

**Transient: the loaded package image and the actor pool, both reallocated per map.**
`FFX_Ev_LoadEventPackage 0x472EF0` frees and reallocates `g_ffxEvPackageBase 0x1FCBD70` and
`g_ffxAtelActorPool 0x1FCBD78` on every event load. ATEL classes 2, 3, 4 and 6 live inside the
package image (class 6 is `pkgBase + off + *(u32*)(pkgBase + 32)`) and class 5 is
`actor + 0x48 + off` in the pool. So every scratch variable a match uses, including ball position,
momentum, the per-player fatigue counters and the clock on the match, is in memory that is re-read
from the archive on the next map change. A mod does not have to snapshot any of it across a map
transition, and conversely a mid-match join would have to copy both regions, which is a lot of bytes
(`bltz0000`'s actor pool is 110,064 bytes and its class-6 scratch sits inside a 5.4 MB image).

The one native global worth naming is `g_ffxBlitzCheatEnabled 0x8CCACC`, the debug menu's "Blitz
Cheat" toggle. Its only consumer in the whole binary is syscall `Core:613`
(`FFX_AtelSys_Core_613_resi 0x45A7F0`), which just returns it. Script-read, so both peers must agree
on it, and it is not in the save block.

### 6. Is a second player plausible, and what blocks it

Plausible, yes, and the simulation side is close to free. What blocks it is ownership and input, and
it is a small, nameable list.

**The blocker, singular, if you had to pick one:** `*(u16*)(g_ffxAtelCtx + 10)`, the player-actor id.
`FFX_Atel_SetPlayerActorId 0x46F6A0` writes it and `FFX_Atel_GetPlayerActorId 0x46C1A0` reads it.
There is one slot per ATEL context. Syscall `Core:067` (`FFX_AtelSys_Core_067_resi 0x45C9A0`) is how
a script claims it:

```c
v1 = (float *)*(unsigned __int16 *)(a1 + 46);   // my own actor id
FFX_Atel_SetPlayerActorId(v1);                  // *(u16*)(g_ffxAtelCtx+10) = it
return v1;
```

"I am the player now." Ten different blitzer actors in `bltz0000` call it. Under one human that is a
baton being passed around as control changes hands. Under two humans it is a single variable two
people are fighting over, and the loser's input is read as if it were the winner's.

There are seven contexts of 568 bytes each in `g_ffxAtelCtxArray 0xF25BA0`, selected through
`FFX_Atel_SelectContext 0x462D30`, and each has its own `+10`. That sounds like a way out and is not,
because a Blitzball match is one event package with one actor pool, and the actors all live in the
context that loaded it. Contexts 0 to 5 are the field set and context 6 is the menu one. You cannot
give player two his own context and have him be on the same pitch.

The rest of the single-slot list:

- **One pad sample for all contexts.** `FFX_Atel_StepContextRange 0x4688F0` calls
  `FFX_Atel__samplePadsBothPorts 0x471D70` once, before the context loop. Everything in the step sees
  the same six globals: `g_ffxAtelPadPort0Buttons 0xF270C0`, `g_ffxAtelPadPort1Buttons 0xF270C4`,
  `g_ffxAtelPadWord10 0xF270C8`, `g_ffxAtelPadWord28 0xF270CC`, `g_ffxAtelPadPressed 0xF270D0`,
  `g_ffxAtelPadReleased 0xF270D4`.
- **Port 1 is sampled and thrown away.** The sampler ORs port 1's whole ring history into
  `g_ffxAtelPadPort1Buttons` and nothing ever reads it. That is a PS2-era two-port API that still
  works, and it is the most promising lever in the whole document: a second player's input can be
  injected into port 1 without inventing new plumbing, but then the script syscalls have to be made
  to read it, and they are hardcoded to 0.
- **The port 0 hardcodes themselves.** Core:076, Core:077, Core:289, Core:290. Four handlers, each
  with a literal 0 for the port. Patching them to take the port from the calling actor's identity is
  the natural fix, and it is a native patch rather than a script change.
- **The analog staging block is per port but filled from one source.**
  `FFX_Pad__fillAnalogAndSynthDpad 0x489570` reads the four `FFX_Input` analog floats through
  `FFX_Input__getAnalogAxes 0x230C90`, which is a singleton, and writes them into whichever port's
  staging block it was called for. So both ports currently hold the same stick values. That has to be
  split before a second stick means anything.
- **Rumble is port 0.** `sub_889490` and `sub_8894B0` with a literal 0.

What is **not** a blocker, which is the more useful half of the answer:

- The step gate. Blitzball is already inside `FFX_MainStep`, so the kit's existing gate covers it.
- Determinism. No clock, one seeded stream, a deterministic instruction clamp, script speed pinned at
  1.0.
- Persistence. All 2,560 bytes are in the save block the kit already syncs and hashes.
- Per-actor state. ATEL class 5 is `actor + 0x48 + off`, genuinely per-actor, so forty-one
  independent blitzers already have independent state. The engine is per-character here, exactly as
  the project's working model says it usually is.
- Entering a match. `FFX_Ev_LoadEventPackage 0x472EF0` takes an event id, which both peers can be
  told over the host-ordered command channel, and it publishes `g_ffxCurrentEventName 0x1FCBC60`
  (which holds `"bl/bltz0000"`) and the current event id at `0xEFBC40`. Two cheap globals to compare
  for "are we in the same place".

---

## Derivation

How I got there, including the dead ends, because the dead ends are the expensive part and the next
person should not pay for them twice.

### The magic-DLL lead, settled first and settled negative

The brief flagged `g_ffxMagicHostApiTable 0x864CE8` (741 function pointers, handed to magic DLLs by
`MagicFile__start 0x5DA7F0`) as the lead that mattered most, with
`FFX_Magic_CallOverlayStep 0x387E00` called from `FFX_MainStep` at RVA `0x42103E`. If Blitzball were
in a DLL, everything else would change.

**It is not.** `FFX_LoadDllOrFatal 0x2793E0` has exactly two callers, `MagicFile__load` and
`MagicFilePreload__preload`, so the magic system is the only dynamic-code path in the process. I
dumped and searched the shipped magic DLLs and there is no Blitzball string and no Blitzball-shaped
code in them. The host API table is real and the overlay step is real, but they are a spell-effect
plugin system. See `MAGIC_DLL.md`.

### Finding it, by way of the developer menu

The productive thread turned out to be the intact PS2 developer menu. Its game-parameter page has a
"Full Blitz" line and a "Blitz Cheat" line. Following "Blitz Cheat" to its storage gave
`g_ffxBlitzCheatEnabled 0x8CCACC`, written at two debug sites, and then the decisive fact: its only
reader in the entire binary is an ATEL syscall handler. A native cheat flag whose only consumer is
script bytecode means the thing it cheats at is script bytecode.

Following "Full Blitz" gave a function that had been misnamed
`FFX_SaveData_InitPersistentScriptFields`. It memsets two regions of the save block and does nothing
else. Renamed `FFX_Blitz_DebugFullBlitz 0x3845B0`. Those two regions pointed straight at the class-0
variable range, and from there the `.ebp` descriptor tables gave the whole 2,560-byte map.

Both of those are in the IDB with the evidence, not just the conclusion.

### Why it took a while

Three plausible places Blitzball is not:

- The 25 native menu modules (`g_ffxModuleTable 0x1440888`, see `MENU_SYSTEM.md`). Modules 6, 9, 20
  and 23 are unidentified and one of them looked like a candidate for the team-management screen. It
  is not. `bltz0200` and `bltz0201` are the team screens and they are ATEL packages like everything
  else.
- The battle module. `FFX_Btl_MainStep 0x390C10` is a sibling of the ATEL step in the sub-step loop,
  not a parent, and nothing in it mentions Blitzball.
- `sub_891B80`, which an existing note claimed was Blitzball setup. It is Lulu's Fury overdrive. See
  the error list.

### Verifying the bytecode claims

Counting syscall sites by scanning raw bytes for the three-byte `B5`/`D8` plus two-byte-operand
pattern has a false-positive floor, because 4.6 MB of data bytes will produce the pattern by chance.
The floor is about 2,250 hits per library index, which means **per-library totals from a byte scan
are worthless** and I do not quote them. For a specific (library, function) pair the expected noise
is 0.55 occurrences in `bltz0000`, so specific-pair counts are reliable, and I validated the method
against `tools/ebp.py dis -a <actor>`, which agreed exactly: four `Core:289` in actor 27, nine
`Core:076` in actor 33, one `Core:067` in actor 2.

The stick-reading loop in `bltz0000` actor 27, around file offset 0x2DABB3, decoded:

```
2DABB6  d80000  syscall  core:0        (wait frames)
2DABBC  b52101  syscallf core:289      ; axis 2, left stick X
2DABBF  a05a02  storevar 602
2DABC5  b52101  syscallf core:289      ; axis 3, left stick Y
2DABC8  a05b02  storevar 603
...     compares the raw 0..255 byte against 20, 66 and 106, drains a counter
```

That is the whole of Blitzball's stick handling, and it is why the raw byte is what matters for
replication.

### The two native Blitzball special cases

Both are in `FFX_Ev_LoadEventPackage 0x472EF0` and both are text:

- RVA 0x4730B0 sets `g_ffxEvMesWinBlitzTextMode 0xF27100` to 1 when the event name contains `"bltz"`
  (or `"hiku2"`, or the event id is 23, 93 or 291). Read only by `FFX_MesWin_MeasureTokenRun` at RVA
  0x4B96B3 and one sibling.
- RVA 0x473105 sets `g_ffxMesWinFontScale 0x85D784` to 0.9 for `"bltz"` and 1.0 otherwise. All eleven
  other readers are message-window and font layout.

There is a third name-based special case outside the loader, in `sub_874C50 0x474C50`, which builds
the per-event texture list path and gives five Blitzball packages (`bltz0000`, `bltz0005`,
`bltz0006`, `bltz0200`, `bltz0201`) a per-language texture directory. Those five are the only `.ebp`
names that appear as strings anywhere in FFX.exe, which fits them being the packages with text baked
into textures. Localisation, not simulation.

That is the complete list. Three sites, all cosmetic.

### The Blitzball package family

From `tools/ebp.py info`. Code sizes are the bytecode extent, actors and characters are from the ATEL
block header, and the map number is the first `s16` of the map-entry record.

| Package | File size | Bytecode | Actors | CHRs | Map |
|---|---|---|---|---|---|
| bltz0000 | 5,422,592 | 4,647,336 | 41 | 36 | 173 |
| bltz0002 | 5,445,248 | 4,642,912 | 41 | 36 | 173 |
| bltz0004 | 1,680,960 | 1,346,840 | 36 | 31 | 106 |
| bltz0201 | 1,110,784 | 876,048 | 9 | 6 | 7 |
| bltz0006 | 845,248 | 630,448 | 30 | 25 | 173 |
| bltz0001 | 774,144 | 544,276 | 36 | 31 | 173 |
| bltz0005 | 721,152 | 517,464 | 25 | 20 | 173 |
| bltz0200 | 222,464 | 113,300 | 7 | 4 | 7 |
| bltz0009 | 119,680 | 36,272 | 11 | 9 | 173 |

`bltz0000` and `bltz0002` are the match, near-identical in size and structure, 41 actors each, which
is the right order for two teams of six plus a ball plus cameras plus HUD drivers. `bltz0200` and
`bltz0201` are small and have almost no Ch-library traffic, which fits menu screens. Every one of
them also exists as `cn_`, `psv_` and `psvcn_` variants in the archive for the Chinese and Vita
builds, which is why a descriptor union over "the Blitzball packages" covers more files than this
table lists.

The internal name of all of them is `kita`, which is presumably the developer's name for the
Blitzball subsystem and is worth knowing if you grep the archive.

---

## Addresses derived

All RVAs. Add 0x400000 for the IDA VA. These are the ones I would promote.

### Step path and the VM

| RVA | Type | Note |
|---|---|---|
| 0x420AE0 | function | `FFX_MainStep`, the simulation step, already known |
| 0x42101A | call site | the single call to `FFX_Atel_StepOnce` inside the sub-step loop |
| 0x42D7E0 | function | `FFX_Player__getSubStepCount`, already known |
| 0x48D3D0 | function | `FFX_Atel_StepOnce`, **Blitzball's per-step entry** |
| 0x472BD0 | function | `FFX_Atel_StepFieldAll`, the field step body |
| 0x4666D0 | function | `FFX_Atel_StepFieldContexts`, calls `StepContextRange(0, 6)` |
| 0x4688F0 | function | `FFX_Atel_StepContextRange`, samples the pads once then steps contexts |
| 0x471BC0 | function | `FFX_Atel_StepFieldFrame`, the encounter check, not Blitzball |
| 0x467950 | function | `FFX_Atel_StepFrame`, per-context frame step |
| 0x4641E0 | function | `FFX_Atel_RunScript`, the opcode interpreter |
| 0xF270D8 | u32 | `g_ffxAtelActorClampCount`, bumped when an actor blows the 65,536-opcode budget, free desync canary |
| 0x4777A0 | function | `FFX_Atel_SysFuncStart`, syscall dispatch, entry+0 |
| 0x477730 | function | `FFX_Atel_SysFuncPoll`, entry+4 |
| 0x4777F0 | function | `FFX_Atel_SysFuncResult`, entry+8 float, entry+12 int |
| 0x477880 | function | `FFX_Atel_RegisterSysFuncLib` |
| 0xF28558 | ptr[16] | `g_ffxAtelSysFuncLibs`, library table, filled at init, 0xFFFFFFFF in the file |
| 0x852B60 | data | `g_ffxAtelSysFuncLibEmpty`, the table unregistered libraries point at |
| 0x46D6D0 | function | `FFX_Atel_Init`, registers the eleven real libraries |

### Syscall library tables, 16 bytes per entry

| RVA | Library | Note |
|---|---|---|
| 0x850050 | 0 Core | the big one, actor and character and input and RNG |
| 0x852BE0 | 1 Sys | |
| 0x888D88 | 4 Sg | sphere grid |
| 0x8891F8 | 5 Ch | character and model, the heaviest Blitzball user |
| 0x843998 | 6 Came | camera |
| 0x842628 | 7 Btl | battle |
| 0x85DC90 | 8 MapFx | map effects, second heaviest Blitzball user |
| 0x85D8C0 | 9 Test | |
| 0x840E30 | 11 Movie | FMV, the only clock-tainted handler lives here |
| 0x852DD8 | 12 Save | |
| 0x885EB0 | 13 | name not recovered |

Libraries 2, 3, 10, 14 and 15 are registered empty. Any apparent calls to them in a byte scan are
noise.

### Context and variable storage

| RVA | Type | Note |
|---|---|---|
| 0xF26B28 | ptr | `g_ffxAtelCtx`, a pointer slot, not the struct |
| 0xF25BA0 | struct[7] | `g_ffxAtelCtxArray`, 568 bytes per context, 0..5 field, 6 menu |
| 0xF26B30 | u32 | `g_ffxAtelSteppingContextIndex`, which context is currently stepping |
| 0x462D30 | function | `FFX_Atel_SelectContext`, returns the old pointer |
| 0x46F730 | function | `FFX_Atel_RestoreContext` |
| 0x46F6A0 | function | `FFX_Atel_SetPlayerActorId`, writes ctx+10 |
| 0x46C1A0 | function | `FFX_Atel_GetPlayerActorId`, reads ctx+10 |
| 0x46C2E0 | function | `FFX_Atel_ResolveVarAddress`, the class 0..6 variable resolver |
| 0x46A830 | function | `FFX_Atel_GetActor` |
| 0x46DF00 | function | `FFX_Atel_PopInt` |
| 0x46E3F0 | function | `FFX_Atel_PushInt` |
| 0x46DE50 | function | `FFX_Atel_PopFloat` |
| 0x46E3C0 | function | `FFX_Atel_PushFloat` |

### Event package loading

| RVA | Type | Note |
|---|---|---|
| 0x472EF0 | function | `FFX_Ev_LoadEventPackage`, takes an event id, the way into a match |
| 0xEFBC40 | u32 | current event id, currently named `maybe_g_ffxGameModeId`, suggested `g_ffxCurrentEventId` |
| 0x1FCBC60 | char[] | `g_ffxCurrentEventName`, holds `"bl/bltz0000"` during a match |
| 0x1FCBD70 | ptr | `g_ffxEvPackageBase`, the loaded `.ebp` image, freed and reallocated per load |
| 0x1FCBD78 | ptr | `g_ffxAtelActorPool`, 110,064 bytes for bltz0000 |
| 0x46A290 | function | `FFX_Atel_CalcActorPoolSize` |
| 0x46BD90 | function | `FFX_Atel_GetEntryMapNo`, reads the map-entry record |
| 0x46ADC0 | function | `FFX_Atel_GetResFlags` |
| 0x48CAC0 | function | `FFX_LoadLocalizedBin` |
| 0xF27100 | u32 | `g_ffxEvMesWinBlitzTextMode`, 1 during Blitzball, text measurement only |
| 0x85D784 | float | `g_ffxMesWinFontScale`, 0.9 during Blitzball, text only |
| 0x474C50 | function | builds the per-event TexList path, special cases five bltz names |

### Randomness

| RVA | Type | Note |
|---|---|---|
| 0x3988F0 | function | `FFX_Rand_Stream`, 68 seeded streams, Blitzball uses stream 2 |
| 0x398890 | function | `FFX_Rand_SeedAllStreams`, the other writer of the state table |
| 0xD35EE0 | u32[68] | `g_ffxRandStreamState`, outside the save block, zero-filled at start |
| 0x842208 | u32[68] | multiplier table, read only |
| 0x842318 | u16[68] | xor table, read only |
| 0x457400 | function | `FFX_AtelSys_Core_166_resi`, `rand(n)` on stream 2, **Blitzball's only RNG** |
| 0x457680 | function | `FFX_AtelSys_Core_169_resi`, raw 16-bit on stream 2, zero Blitzball sites |

### Input

| RVA | Type | Note |
|---|---|---|
| 0x471D70 | function | `FFX_Atel__samplePadsBothPorts`, one sample per step for all contexts |
| 0xF270C0 | u16 | `g_ffxAtelPadPort0Buttons`, held mask, what Core:076 and Core:077 read |
| 0xF270C4 | u16 | `g_ffxAtelPadPort1Buttons`, sampled and never read, the lever for player two |
| 0xF270C8 | u16 | `g_ffxAtelPadWord10` |
| 0xF270CC | u16 | `g_ffxAtelPadWord28` |
| 0xF270D0 | u16 | `g_ffxAtelPadPressed`, port 0 edge-pressed, the examine gate |
| 0xF270D4 | u16 | `g_ffxAtelPadReleased` |
| 0x46AF30 | function | `FFX_Atel_GetPadPort0Held` |
| 0x45E130 | function | `FFX_AtelSys_Core_076_resi`, digital test, port 0 hardcoded, 11 Blitzball sites |
| 0x45E820 | function | `FFX_AtelSys_Core_077_resi`, digital test, 3 Blitzball sites |
| 0x45D1D0 | function | `FFX_AtelSys_Core_289_resi`, analog axis, port 0 hardcoded, 4 Blitzball sites |
| 0x45D310 | function | `FFX_AtelSys_Core_290_resi`, same shape, 0 Blitzball sites |
| 0x488C00 | function | `FFX_Pad__readStagingAnalogByte`, `portState[index + 132]` |
| 0x489570 | function | `FFX_Pad__fillAnalogAndSynthDpad`, +132 RX, +133 RY, +134 LX, +135 LY |
| 0x488CF0 | function | `FFX_Pad__mapButtonCode`, returns codes 0..3 unchanged |
| 0x488C40 | function | `FFX_Pad__remapButtonMask`, identity on this build |
| 0xF30488 | u8[] | `g_ffxPadButtonRemapTable`, identity, only `FFX_Pad__resetPort` writes it |
| 0x4894F0 | function | `FFX_Pad__setButtonRemapEntry`, one caller |
| 0x489360 | function | `FFX_Pad__resetPort`, writes the identity remap |
| 0x488EC0 | function | `FFX_Pad__getPortState` |
| 0x230C90 | function | `FFX_Input__getAnalogAxes`, the singleton both ports are filled from |

### Ownership

| RVA | Type | Note |
|---|---|---|
| 0x45C9A0 | function | `FFX_AtelSys_Core_067_resi`, "I am the player now", 10 Blitzball actors call it |

### Persistent state

| RVA | Type | Note |
|---|---|---|
| 0xD2CA90 | struct | `g_ffxSaveData`, 0x68C0 = 26,816 bytes, already known |
| 0xD2DC7C | struct | `g_ffxBlitzSaveBlock`, 2,560 bytes, saveData+0x11EC, ATEL class 0 offsets 0x1000..0x1A00 |
| 0xD2E00E | u8[60] | `g_ffxBlitzPlayerByteArray1392`, class-0 offset 0x1392, one byte per player |
| 0x3845B0 | function | `FFX_Blitz_DebugFullBlitz`, the only native writer of the Blitzball block |
| 0x8CCACC | u32 | `g_ffxBlitzCheatEnabled`, debug toggle, read only by syscall Core:613 |
| 0x45A7F0 | function | `FFX_AtelSys_Core_613_resi`, returns the cheat flag to script |
| 0x4B3E60 | function | `FFX_SaveFile_Serialize`, 0x40 header plus a byte copy of the block |
| 0x386B00 | function | `FFX_InitNewSaveData` |
| 0x385240 | function | `FFX_GetSaveData` |

### Clocks, for the record

| RVA | Type | Note |
|---|---|---|
| 0x241410 | function | `FFX_Time_AppElapsedSeconds`, QueryPerformanceCounter via Phyre, the real gameplay clock |
| 0x230C60 | function | `FFX_Input__getTimeSeconds`, **correct address**, nine input-layer callers |
| 0x230C40 | function | `FFX_Input__clearThreadedSampleQueues`, the adjacent function the old notes meant |
| 0x2FD80 | function | `Phyre_Time_NowSeconds` |
| 0x36F850 | function | `FFX_AtelSys_Movie_136_start`, the only clock-reaching ATEL syscall handler, unused by Blitzball |
| 0x491B80 | function | `FFX_BtlOd_LuluFuryStickMinigame`, Lulu's Fury, two clock reads, **not Blitzball** |
| 0xF3C930 | u32 | `g_ffxThreadedPadMode`, a Lulu Fury thing, **not Blitzball** |

### Dead code worth knowing is dead

| RVA | Type | Note |
|---|---|---|
| 0xF26BD8 | float | `g_ffxAtelScriptSpeedScale`, permanently 1.0 |
| 0x471020 | function | `FFX_Atel_SetScriptSpeedScale_DEAD`, no callers |
| 0x46B5E0 | function | `FFX_Atel_ScaleByScriptSpeed_DEAD`, no callers |
| 0xF25B98 | float | `g_ffxAtelTextSpeedScale`, alive, 2.0, message windows only |

### Context offsets, not addresses

`ctx` is `g_ffxAtelCtx`, stride 568 bytes within `g_ffxAtelCtxArray`.

| Offset | Type | Note |
|---|---|---|
| +0x00 | u8 | flags, bit 0 = active, bit 3 = count steps |
| +0x08 | u8 | state, 1 and 2 become 3 after a step |
| +0x0A | u16 | **the player-actor id, the single slot that blocks player two** |
| +0x2C | ptr | class-0 base, equals `g_ffxSaveData + 0x1EC` |
| +0x4C | ptr | the context's step callback |
| +0x1E8 | ptr | nearest-examinable slot |
| +0x1F4 | u32 | per-context step counter, another desync canary |

Actor offsets, from the syscall handlers: `+0x2E` is the actor's own id (`Core:067` reads it), `+0x34`
is a flags word (the clamp sets 0x1400 in it), `+0x48` is the base of ATEL class-5 variables.

---

## What is untested, and what is most likely wrong

Ranked, most likely wrong first. Nothing here was run, so everything is static analysis.

**1. The field semantics inside the 2,560-byte save block.** The shape is solid and the tiling is
proof, but what each field means is inference from array dimensions and from what the "Full Blitz"
cheat writes. I am confident about the two `s32[60]` tech masks because the cheat sets them to
0xFEFFFF7F, and about `g_ffxBlitzPlayerByteArray1392` being a per-player level or tier because the
cheat sets all 60 to 5. The `u8[6][8]` being league rosters, the `u16[100]` and `u8[100]` being some
kind of contract or history table, and the twenty-odd scalars, are guesses. If the mod needs to
interpret rather than just copy these bytes, re-derive them from script writes.

**2. "Blitzball runs in context 0."** I know it runs in one of contexts 0 to 5, because
`StepContextRange(0, 6)` is the only path that reaches it, and I know contexts are 568 bytes apart. I
did not watch a match and read `g_ffxAtelSteppingContextIndex`. If a mod assumes context 0 and
Blitzball actually loads into 1 or 2, a hook that writes `ctx+10` writes the wrong context and
silently does nothing. This is the cheapest thing on the list to test: break on
`FFX_AtelSys_Core_067_resi 0x45C9A0` during a match and read
`g_ffxAtelSteppingContextIndex 0xF26B30`.

**3. The ten actors that call Core:067, and which one is "the player".** I counted static call sites,
not dynamic ones. Ten actors contain the opcode, and I inferred that control passes between blitzers
as the player's team gains and loses the ball. It is possible that only one or two of those sites
ever execute and the others are leftovers from a different control scheme, in which case the
ownership problem is smaller than I have described. It is also possible that HUD or camera actors
claim it too, in which case it is messier.

**4. The float determinism of the script VM.** I established that the VM has no clock and a
deterministic instruction clamp, and that the opcodes operate on 32-bit ints and floats. I did not
audit the float opcodes for x87 control-word sensitivity, and `FFX_Atel_PushFloat` and
`FFX_Atel_PopFloat` move values through the x87 stack. The project already knows about `fldcw` and
x87 determinism generally. Blitzball is the heaviest float-script workload in the game, so if there
is an x87 reproducibility problem anywhere, this is where it will show up first, and I have not
ruled it out.

**5. That the byte scan found every syscall site.** The three-byte pattern scan is a necessary
condition for a syscall with a two-byte operand, and I validated it against `ebp.py`'s real
disassembler for several specific pairs. But "zero occurrences of Movie:136" assumes Blitzball cannot
reach a syscall by a computed operand. The VM's dispatch takes the operand from the instruction
stream, not from a register, so I believe this is airtight. If ATEL has an indirect syscall form I
did not find, the clock claim weakens.

**6. The claim that stream 2 is Blitzball's only randomness.** I verified the two script RNG syscalls
and that only `Core:166` has Blitzball sites. If a Ch-library or MapFx-library handler draws from
another stream for something gameplay-visible (a deflection, a spectator animation that feeds back
into the sim), I would have missed it, because I only audited the handlers for clock reads and not for
RNG reads. Doing the same reverse-reachability pass with `FFX_Rand_Stream` as the root would close
this, and it is a half-hour job.

**7. Event ids.** I previously believed `bltz0000` was event id 62 and `bltz0002` was 352. **I could
not re-derive either and I now think they were wrong.** The id is the argument to
`FFX_Ev_LoadEventPackage` and resolves through asset group 12 at byte offset `18 * id`, so the mapping
lives in game data and not in the exe, and no event name table exists in FFX.exe (only five `bltz`
strings appear at all, in the texture-path builder). Do not use 62 or 352. The map numbers in the
package table above (173 for the stadium, 7 for the team screens) come from the `.ebp` map-entry
record and are solid.

**8. Whether a mid-match join is feasible at all.** I described what would have to be copied, which is
the package image and the actor pool, and said it is a lot of bytes. I did not work out whether the
pool contains pointers that would have to be relocated. It almost certainly does, since
`FFX_Atel_GetActor` hands out pointers into it. Treat mid-match join as unexplored, not as merely
expensive.

**9. Library 13.** Registered at table 0x885EB0, name not recovered, and I could not count its
Blitzball usage reliably because per-library byte-scan counts sit below the noise floor. It is
probably small. If it turned out to be a Blitzball-specific library that would change the picture, but
nothing suggests it.
