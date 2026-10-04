# The magic DLLs, and the host API table they call back through

**Why this document exists.** The co-op design has always reasoned about `FFX.exe`. A large amount of
this game's per-effect logic is not in `FFX.exe` at all. It is in 581 separate DLLs that the exe loads
one at a time and hands a table of function pointers to, and they call back into the engine through
it. The design doc already knew one entry of that table. This is the whole table.

**A note on the numbers.** My first pass at this scanned the DLLs for the host-table call pattern with
a loose byte matcher, and three of its headline figures were false positives from ordinary struct
accesses that happen to use the same displacement. I then took the counts from `tools/magicdll.py`
and wrote that it does the job properly. **It did not.** Its matcher only saw
`mov reg,[reg+disp32]` immediately followed by `call reg` with one register throughout, which is
blind to displacements under 0x80, to a different destination register, and to the argument pushes
MSVC puts between the load and the call. The figures below are now from the rewritten scanner, which
decodes instructions and traces the base register back to the DLL's own stored table pointer, and
which agrees site for site with IDA Pro on three spot-checked DLLs. Section 7 has both the old
numbers and the new ones.

## Summary, read this first

1. **`g_ffxMagicHostApiTable` at RVA `0x864CE8` is 947 slots**, and it is **not** a pure function
   table. 913 slots are function pointers into `.text` and **34 are pointers to engine globals.**
   `MagicFile__start 0x5DA7F0` passes its address to the loaded DLL's `InitMagicPRX` export.

2. **581 DLLs, `magicFiles/FFX/magic_NNNN.dll`, 24 KB to 3.4 MB each.** `NNNN` is
   `g_ffxMagicFileId`. Every one exports exactly `GetEffectOverlayTable` and `InitMagicPRX`. Across
   all 581, **640 distinct slots** are either called or copied out of the table, highest index 946.

3. **The thing that changes the determinism argument: `g_ffxIsCatchUpStep` is slot 936, and all
   581 DLLs copy it into a global of their own at init.** `g_ffxMainStepCounter` is slot 764 and is
   copied by the same 581. So magic DLL code is handed the catch-up flag and the frame number. I
   did not find a DLL that branches on either, and I cannot show that none does. See section 5.
   (This used to read 576 of 581. The five were an artefact of the old byte-walking init reader,
   which mis-synced in them. All 581 have the same init body.)

4. **Magic DLLs reparent characters to bones.** Slot 632 is `FFX_Ch_AttachToParentBone 0x432630`,
   called by **two** DLLs, `magic_0393` and `magic_0450`, four sites each. One site per DLL passes a
   null carrier, which detaches, and one attaches to bone 18. That is the `m_parent` / `m_parentUid`
   / `m_parentJoint` state the held-object census hashes into checksum region 15. See section 4.

5. **The design doc's "every shipped effect DLL can advance a gameplay RNG" now has a number.** Slot
   324 is `FFX_Btl_Rand`, and **92 of the 581 DLLs call it, at 274 call sites.** See section 5.
   (This used to read 83, which was the old scanner's undercount.)

6. **Two false alarms, recorded so nobody raises them.**
   - All 581 DLLs import `QueryPerformanceCounter`, `GetTickCount64` and `GetSystemTimeAsFileTime`.
     That is not gameplay. Their whole import list is 34 functions, every one at 581 of 581, which
     is exactly MSVC's `__security_init_cookie` and CRT startup boilerplate. No magic DLL imports a
     clock for anything of its own.
   - 81 DLLs import `rand` and none imports `srand`, which looks like 81 unseeded consumers of a
     shared stream. Nothing seeds that stream from a clock, so it is not a problem by itself.

## 1. The loader

`MagicFile__*` is the family, all around RVA `0x5DA200` to `0x5DB200`:

| function | RVA | what it does |
| --- | --- | --- |
| `MagicFile__init` | `0x5DA3C0`ish | sets up, writes `g_ffxMagicFileHinst` |
| `MagicFile__setMagicId` | `0x5DA7B0`ish | selects which `magic_NNNN.dll` is wanted |
| `MagicFile__load` | `0x5DA480`ish | loads it |
| `MagicFile__onDllLoaded` | `0x5DB100`ish | resolves the two exports |
| `MagicFile__start` | `0x5DA7F0` | **hands over the table** |
| `MagicFile__getEffectOverlayTable` | `0x5DA370`ish | the other export |
| `MagicFile__stop` | `0x5DA880`ish | unloads |

`MagicFile__start` is small enough to quote:

```c
int MagicFile__start()
{
  if ( g_ffxMagicFileHinst == -1 ) {
    printf("MagicFile: Start() failed: no magic loaded yet.\n");
    return 0;
  }
  sub_643260(g_ffxMagicFileId == 670);
  g_ffxMagicInitMagicPRX(&g_ffxMagicHostApiTable);   // <- the handover
  printf("MagicFile: Start(%d) completed.\n", g_ffxMagicFileId);
  if ( g_ffxMagicFileId == 168 ) sub_642F10(1);
  if ( g_ffxMagicFileId == 434 || g_ffxMagicFileId == 610 ) sub_7A8F20();
  return 1;
}
```

Note the four hardcoded ids: 670, 168, and the pair 434 / 610 get special treatment in the exe. None
of the four is identified.

## 2. The table

947 slots, ending at slot 946. Slot 947 is zero and slot 948 starts a `type_info` vtable followed by
RTTI name strings, so the end is unambiguous. Of the 947:

- **913 are function pointers** into `.text`
- **34 are pointers to engine globals**, listed in section 3

Only one thing in the exe references the table: `MagicFile__start` pushes its address.

**Use `tools/magicdll.py`, which was already in the repo.** `python tools/magicdll.py calls <dll>`
lists the host API slots one DLL calls, `abi <dll>` shows the `InitMagicPRX` wiring including which
slots the DLL copies into its own globals, and `survey <dir>` aggregates. It already knew the figure
741, which is where the first run of data slots begins, and it already distinguished indices below it
from indices above.

A real host API call looks like this, from `magic_0393.dll` at file offset `0x000ECB`:

```
A1 04 AB 0F 10        mov  eax, ds:[0x100FAB04]   ; the DLL's stored table pointer
57                    push edi
8B 80 E0 09 00 00     mov  eax, [eax+0x9E0]       ; 0x9E0 / 4 = slot 632
56                    push esi
53                    push ebx
FF D0                 call eax
83 C4 0C              add  esp, 0Ch               ; cdecl, 3 args
```

**The load from the stored table pointer global is what makes it a host call.** Without that, a
`mov reg, [reg+disp32]` with a small 4-aligned displacement is just a struct field access, and that
is the mistake section 7 describes. Each DLL has its own pointer global: `0x100FAB04` in
`magic_0393`, `0x100F2EC0` in `magic_0450`.

### How a DLL gets stepped, and why it is step-driven

`FFX_Magic_CallOverlayStep` fetches an overlay table from `actor+3980` and from `dword_1133360` and
calls slot 3 or slot 4 of it depending on its argument, setting `g_ffxInMagicOverlayCall` (the global
at `0xD3335C`, itself exposed as table slot 827) around each call.

**It is called from three places in `FFX_MainStep`, and none of them is gated on
`g_ffxIsCatchUpStepRender`.** That is worth stating plainly because two catch-up tests sit right next
to two of the calls and look like they guard them. They do not. They are one-line `if`s guarding only
the stripped profiler markers `nullsub_196("1")` and `nullsub_196("3")`.

| call | where | guard | runs |
| --- | --- | --- | --- |
| `CallOverlayStep(0)` | inside `for (i < FFX_Player__getSubStepCount())` | `byte_12FB8FB == 0` | once per sub-step |
| `CallOverlayStep(0)` | the `subStepCount == 0` arm | none | once, when there were no sub-steps |
| `CallOverlayStep(1)` | inside `for (k < max(subStepCount, 1))` | `byte_12FBBB8 && !byte_12FB8FC && !byte_12FB8FD` | once per sub-step |

So overlay step 0 runs exactly once per step whichever arm is taken, and both overlay steps are
driven by `FFX_Player__getSubStepCount()`, which is simulation state. **The engine calls magic DLL
code a step-deterministic number of times**, and the lockstep gate on `FFX_MainStep` already covers
all of it. That is the most reassuring fact in this document. Section 5 is about the fact that it does
not finish the argument.

## 3. The 34 engine globals handed to every DLL

These are the slots that are not functions. **All 581 DLLs copy 32 of them into plain globals of
their own in `InitMagicPRX`**, the same 32 in every DLL, slot 741 included. Slot 743 is not one of
the 32: every DLL loads it and then indexes it, storing four elements of the array it points at into
an array of its own, which fits its role as `g_ffxEffectUnitPtrs`.

This used to read "576 of the 581 copy all of them (580 for slot 741). The five that do not have a
non-standard init body." That was wrong on both counts. The five have an ordinary init body, the old
byte-walking reader just mis-synced on a `0xA3` byte inside another instruction's immediate and gave
up early. It also picked the wrong table pointer global in nine DLLs for the same reason.

| slot | RVA | name |
| --- | --- | --- |
| 741 | `0x1F0FFE0` | unnamed |
| 742 | `0x1F32E8C` | unnamed |
| **743** | `0xEA40C0` | **`g_ffxEffectUnitPtrs`**, effect-unit pointers indexed by `opp_main` / `FFX_Magic_Oef2Disp` |
| 744 | `0xD3FCD0` | unnamed |
| 745 | `0xD3FD10` | unnamed |
| 746 | `0x1F21540` | unnamed |
| 747 | `0xD9FE90` | unnamed |
| 748 | `0x1F22780` | unnamed, byte |
| 749 | `0x1EFB450` | unnamed |
| 750 | `0xDA0060` | unnamed |
| 751 | `0x844BF0` | unnamed, float |
| 752 | `0x1F0FF60` | unnamed |
| 753 | `0x1F0FD2C` | unnamed |
| 754 | `0x1F0FFA0` | unnamed |
| 755 | `0x1F0FE20` | unnamed |
| 756 | `0x1F0FE60` | unnamed, float |
| 757 | `0x1F0FF20` | unnamed |
| 758 | `0x1F0FD34` | unnamed |
| 759 | `0x8498DC` | unnamed, pointer |
| 760 | `0x21D5A20` | unnamed |
| 761 | `0x1F0FD30` | unnamed |
| 762 | `0x1F0FD50` | unnamed, float |
| 763 | `0xDA0710` | unnamed |
| **764** | `0x1FCBBF0` | **`g_ffxMainStepCounter`**, the game's own frame number |
| 765 | `0x83E6C0` | unnamed, byte |
| 769 | `0x864CE0` | unnamed, sits immediately before the table itself |
| **827** | `0xD3335C` | **`g_ffxInMagicOverlayCall`**, set around every overlay call |
| 850 | `0x8CCA58` | unnamed |
| 880 | `0x1F0FCA0` | unnamed |
| 919 | `0xD33398` | unnamed |
| 921 | `0x83BBB0` | unnamed, pointer |
| 935 | `0x1540AC4` | unnamed |
| **936** | `0xEFB7D0` | **`g_ffxIsCatchUpStep`**, set by `FFX_MainStep` when `g_ffxPendingSteps > 0` |

Twenty-seven of the thirty-four are unnamed, and naming them is a cheap way to learn what the effect
system actually shares with the engine. Note the cluster around `0x1F0FD2C..0x1F0FFE0`, which is
eleven slots into one small region, so that region is probably one effect-system structure worth
mapping as a unit.

## 4. Slot 632: magic DLLs attach characters to bones

Slot 632 is `FFX_Ch_AttachToParentBone 0x432630`, signature
`int (Character *chr, Character *carrier, int boneId)`, which writes `m_parent`,
`m_parentUid = carrier->m_objId`, `m_parentJoint` and `m_attachOffset`. `reversing/HELD_OBJECTS.md`
has the derivation of what that state does: the attachment is a pure bone parent, nothing streams,
and `FFX_Ch_BuildSkinMatrices` reads neither the carried object's position nor its rotation.

**Inside `FFX.exe` it has nine callers, three of which are the ATEL syscalls.** The script ones are
`FFX_AtelSys_Ch_045_resi` (twice), and the siblings `FFX_Ch_AttachToParentJoint` and
`FFX_Ch_AttachToParentOffset` are each called only from `FFX_AtelSys_Ch_044_resi` and
`FFX_AtelSys_Ch_082_resi`. The rest are C side and **`reversing/HELD_OBJECTS.md` already identifies
them**, which I missed on this pass: `FFX_Field_ChrRegTask 0x461D5F` attaches a character's weapon to
its hand on the field, and `sub_793330`, `sub_793F10`, `sub_795C90` (two sites) and `sub_7AAB80` are
battle weapon attachment through `FFX_Btl_GetUnitChr`. That doc's conclusion is that both groups are
driven by state that is itself replicated, so neither needs a sync. The one caller it does not
account for is `sub_8722B0`.

**Outside the exe, exactly two DLLs call it: `magic_0393` and `magic_0450`, four sites each.** The two
are structurally identical, same site shapes at the same relative offsets with the same bone id, so
they are one effect built twice, presumably for two casters. The four sites in `magic_0393`:

| offset | sequence | reading |
| --- | --- | --- |
| `0x000ECB` | `push edi; push esi; push ebx; call` | attach, all three args from registers |
| `0x0022B4` | `push 0; push 0; push esi; call` | **detach**, null carrier and bone 0 |
| `0x0022CF` | `push 0x12; push [0x102F7FF0]; push esi; call` | attach `esi` to **bone 18** of the character in a global |
| `0x002439` | three args pushed earlier; `call` | attach |

So the null-carrier detach is real, and the bone id is a plain constant 18 of the logical range 0..21.

What this means for the mod:

- The held-object census walks the CHR pool every step and hashes every carry it finds, so a magic
  effect that parents something to a bone during a spell animation **appears in checksum region 15**.
  Section 2 says the overlay step is step-driven, so this should replicate. That is a prediction, not
  a result, and it is the cheapest thing in this document to test: cast whatever `magic_0393` is and
  watch region 15.
- It is a second writer of the state a handover command would re-point, so any handover policy has to
  be sure it is not fighting a magic effect.

I did not identify what effects 393 and 450 are. Each is about 1 MB and only four sites matter, so
that is cheap.

Slot 634 is `FFX_Ch_SetRotAndMoveDir 0x42B1B0`, also character state, so slot 632 is not an isolated
case of a DLL writing a character.

## 5. The RNG and the determinism argument

### What was already established, and is in the IDB

Two RNGs are shared, and both are seeded from constants by `FFX_Rand_Init sub_7FF360`, one shot from
`FFX_MainInit` at `0x82097B`, reading no clock:

- the CRT stream, `srand(0x3F4CF27B)`
- the effect and particle RNG, `g_ffxEffectRandState 0x80A000` = `-1.4980391f` held as float bits,
  with `g_ffxEffectRandInited 0x88F728` set, drawn through `FFX_Rand_EffectFloat1to2`

`g_ffxEffectRandState` has 51 writers, all effect or particle code. The chaining hazard is already
recorded on that global: **42 of the particle operators call `srand(g_ffxEffectRandState & 0x7FFFFF)`,
so effect code re-seeds the CRT stream**, and the CRT stream is read by `sub_822DE0` once per CHR per
step, which is simulation.

One detail the decompiler hides: that seed renders as
`srand(((unsigned int)&loc_7FFFFD + 2) & g_ffxEffectRandState)`. `0x7FFFFD + 2` is `0x7FFFFF` and the
instruction is literally `and eax, 0x7FFFFF`. IDA is dressing a plain constant up as a code address
because `0x7FFFFD` lands on a label. It is **not** an ASLR-dependent value, which is what it looks
like at a glance.

### What this pass adds

**`FFX_Btl_Rand` is slot 324, and 92 of the 581 DLLs call it, at 274 call sites.** The design doc
said "every shipped visual-effect DLL can advance a gameplay RNG", which was the right worry stated
qualitatively. The number is 92. It was reported as 83 until the scanner was fixed, and a separate
loose scan in `OVERDRIVES.md` reported 99 DLLs and 292 sites, which is the raw count of
`mov reg,[reg+0x510]` instructions with no check that the base register holds the table. 18 of those
292 are struct field accesses, so 7 of those 99 DLLs do not call it at all. Section 7 lists them.
The particle operator library at `0x72C730..0x733F70` is also exposed (its operator
names are the `pppRand*` / `pppSRand*` strings at `0xB50258..0xB511D4`), which is the second path to
the same place.

**The CRT stream position is deliberately tied to the step count.**
`maybe_FFX_Yonishi__feedPadStateBothPorts 0x3FF680` calls `rand()` once per sub-step and discards the
result. That is not an accident, it is the game pinning the CRT sequence to the step count.

**Blink is not a hazard, and I had assumed it was.** `FFX_Ch_UpdateBlink 0x4344F0` calls CRT `rand()`
and its only caller `FFX_Ch_UpdateRenderJob 0x433530` has no code xrefs, because it is dispatched
rather than called. The single reference to it is a data reference at `0xA44BA4` inside

```c
BOOL FFX_Ch_DispatchUpdateJob(int a1, int a2, int a3)
{
  return sub_71E720((char *)dword_CDEDE4, (int)FFX_Ch_UpdateRenderJob, a1, a2, a3, 0);
}
```

`sub_71E720` is a producer/consumer enqueue with **no inline-execute fallback**: it takes a free slot
from an 8-entry ring, stores the function pointer and three args with `kind = 3`, pushes it onto a
second ring and `ReleaseSemaphore`s a worker. `JobWorker__vf01 0x303A20` is the consumer, an infinite
pop / run / release loop, and `sub_703910` is the dispatcher whose `case 3` does
`(*(this+3))(arg0, arg1, arg2)`. The pool's threads come from Phyre's `sub_300B0`, which is
`CreateThread` plus the `RaiseException 0x406D1388` thread-naming trick.

So blink runs on a **worker thread**. MSVC's `rand()` has used per-thread state since VC8
(`_getptd()->_rand_state`), so blink draws from the worker's own stream and cannot perturb the game
thread's. The frame-rate dependency is real, since render-side character work is skipped on catch-up
steps, but it is confined to a stream nothing else reads.

### Where that leaves it, honestly

I set out to decide whether the magic DLLs introduce a frame-rate dependency into the simulation, and
**I could not close it either way.** What I can say:

- Nothing seeds either RNG from a clock. Both are constants.
- The engine calls DLL overlay code a step-deterministic number of times.
- The one frame-driven `rand` draw I found is thread-isolated and harmless.
- **But `g_ffxIsCatchUpStep` is table slot 936 and `g_ffxMainStepCounter` is slot 764, and all
  581 DLLs copy both into their own globals at init.** A DLL that reads the catch-up flag and skips
  work on a catch-up step would be doing exactly what the engine's own render path does, and it would
  be invisible to every check described above. I found no DLL that branches on it, and a negative
  from a byte scan over 581 stripped binaries is not evidence of absence.

So the confirmed hazard is **coupling**: effect playback and per-character simulation share RNG
state, through 92 DLLs calling a gameplay RNG directly and a re-seed path through the particle
operators. The unresolved hazard is **whether DLL code is frame-dependent at all**, and the right way
to settle that is at runtime, not statically: run two instances at different frame rates with the
step counter pinned, cast the same spell, and compare the effect-RNG state. That is a better
experiment than any amount of further reading.

## 6. Addresses derived here

RVAs. Add 0x400000 for the IDA VA.

| RVA | type | what |
| --- | --- | --- |
| `0x864CE8` | `void*[947]` | `g_ffxMagicHostApiTable`, 913 functions + 34 globals |
| `0x864CA0` | `int` | `g_ffxMagicFileId`, which `magic_NNNN.dll` is current |
| `0x864CAC` | `HINSTANCE` | `g_ffxMagicFileHinst`, -1 when nothing is loaded |
| `0x1EFB6A0` | `void(*)(void*)` | `g_ffxMagicInitMagicPRX`, the resolved DLL export |
| `0x5DA7F0` | `int(void)` | `MagicFile__start`, the only thing that passes the table |
| `0x42103E` | call site | an `FFX_Magic_CallOverlayStep` call, inside `FFX_MainStep` |
| `0xD3335C` | `int` | `g_ffxInMagicOverlayCall`, table slot 827 |
| `0xEFB7D0` | `int` | `g_ffxIsCatchUpStep`, **table slot 936** |
| `0x1FCBBF0` | `int` | `g_ffxMainStepCounter`, **table slot 764** |
| `0xEA40C0` | array | `g_ffxEffectUnitPtrs`, table slot 743 |
| `0x644B90` | `BOOL(int,int,int)` | `FFX_Ch_DispatchUpdateJob`, enqueues the render job |
| `0x31E720` | enqueue | `sub_71E720`, the job queue push, no inline fallback |
| `0x303A20` | worker loop | `JobWorker__vf01`, pops and runs jobs forever |
| `0x303910` | dispatcher | `sub_703910`, `case 3` calls fn with 3 args |
| `0x300B0` | thread start | Phyre thread create plus the thread-naming exception |
| `0x3FF680` | step hook | advances CRT rand once per sub-step and discards it |
| `0x3FF360` | `FFX_Rand_Init` | seeds both shared RNGs from constants, no clock |
| `0x80A000` | `float` bits | `g_ffxEffectRandState`, 51 writers, feeds `srand` |
| `0x88F728` | `int` | `g_ffxEffectRandInited` |
| `0x4344F0` | `FFX_Ch_UpdateBlink` | CRT `rand`, on a worker thread, harmless |
| `0x433530` | `FFX_Ch_UpdateRenderJob` | job-dispatched, no code xrefs |
| `0x72C730..0x733F70` | library | the particle operators, exposed to the DLLs |
| `0xB50258..0xB511D4` | strings | `pppRand*` / `pppSRand*` operator names |

Table slots of note: **324 = `FFX_Btl_Rand`** (92 DLLs, 274 sites), **632 =
`FFX_Ch_AttachToParentBone`** (2 DLLs, 8 sites, byte offset `0x9E0`), **634 =
`FFX_Ch_SetRotAndMoveDir`** (2 DLLs), **630 = `FFX_BtlOd_StartMinigameForActor`** and **629 =
`FFX_BtlOd_IsMinigamePending`** (6 DLLs each, `magic_0393..0397` and `magic_0450`), **0 =
`FFX_Ch_Allocate`** (28 DLLs, 47 sites), **743 / 764 / 827 / 936** the globals above.

## 7. Corrections to my own first pass

Recorded because the failure mode is worth remembering, not for completeness.

My first scan looked for `(FF /2 | 8B /r) [reg+disp32]` with a 4-aligned `disp32` under 2964 and
divided by four. Three figures from it were wrong:

| I said | actually | why |
| --- | --- | --- |
| the table is 741 function pointers | 947 slots, 913 functions and 34 globals | 741 is where the first run of data slots starts, not the end |
| 718 of 741 slots referenced, 115426 references | 640 distinct slots, highest index 946 | the loose pattern matched struct accesses |
| slot 632 called by 4 DLLs, 16 sites | 2 DLLs, 4 sites each | `magic_0162` and `magic_0165` were false positives |

The `magic_0162` case shows the mistake clearly. It contains
`mov eax, [edi+0x9E0]; mov [esi+0xD0], eax` and `mov eax, [ebx+0x9E0]; movss xmm3, [...]`, with
neighbouring accesses at `0x960`, `0x964` and `0x9E4`. That is one big structure being copied field by
field, and it has nothing to do with the host table. **The distinguishing feature of a real host call
is that the base register is loaded from the DLL's own stored table pointer global first**, which is
what `tools/magicdll.py` now checks. It did not check it when this section was first written, and
this paragraph used to claim that it did.

### The tool's own undercount, now fixed

The old matcher wanted `mov reg,[reg+disp32]` immediately followed by `call reg` with one register
throughout, so it reported 1 site per DLL for slot 632 where there are 4, because the real sites
have argument pushes in between. That was only the smallest of four blind spots:

| blind spot | example | what it cost |
| --- | --- | --- |
| mod=01 disp8 form, which is how every slot under 32 compiles | `8B 40 28` = `mov eax,[eax+0x28]`, slot 10, in `magic_0162` at `0x10002DE5` | **every slot below 32 reported as zero.** Slots 0 and 3 through 31 are called, 2951 sites |
| mod=00 no-displacement form, which is slot 0 | `A1 8C A2 10 10; 56; 8B 00; 68 11 30 00 00; FF D0` in `magic_0162` at `0x10002DB0`, so `FFX_Ch_Allocate(0x3011)` | slot 0 reported as zero where 28 DLLs call it at 47 sites |
| a different destination register | `mov ecx,[eax+disp]; call ecx` | sites lost across the table |
| argument pushes between the load and the call | the slot 632 sites in section 4 | 3 of the 4 sites per DLL |

Replacing it changed the headline figures like this:

| figure | old matcher | fixed scanner |
| --- | --- | --- |
| distinct slots called | 459 | 608 |
| total call sites | 49442 | 74612 |
| highest slot called | 942 | 946 |
| distinct slots called or copied | 491 | 640 |
| slot 324 `FFX_Btl_Rand` | 83 DLLs, 224 sites | **92 DLLs, 274 sites** |
| slot 630 `FFX_BtlOd_StartMinigameForActor` | 0 DLLs | **6 DLLs, 6 sites** |
| slot 632 `FFX_Ch_AttachToParentBone` | 2 DLLs, 2 sites | **2 DLLs, 8 sites** |
| slot 0 `FFX_Ch_Allocate` | 0 DLLs | **28 DLLs, 47 sites** |

The fixed scanner is a strict superset of the old one: for every slot and every DLL it finds
everything the old matcher found and more, so nothing the old numbers claimed has been withdrawn.

**Two forms a reviewer may expect and that are simply absent.** There is not one single
`call dword ptr [reg+disp]` instruction in any of the 581 DLLs. At real instruction boundaries the
census is 89889 `call reg` and 11205 `call [abs32]` and nothing else. A byte-level regex does find
318 apparent `FF /2` memory calls, but every one of them is a coincidence inside another
instruction. Nor does any call go through a global holding a copied table entry, because all 32
copied entries are data pointers rather than functions.

**And the other direction, over-counting.** `OVERDRIVES.md` section 2.4 rescanned with a filter that
accepts any `8B /r` or `FF /2` with mod=10, no SIB and a 4-aligned displacement, and reported 99
DLLs and 292 sites for slot 324 and 8 DLLs for slots 629 and 630. 292 is exactly the number of
`mov reg,[reg+0x510]` instructions in the corpus, and 18 of them are not host calls at all. Six DLLs
(`magic_0132`, `0133`, `0134`, `0135`, `0347`, `0491`) do
`mov eax,[edi+0x510]; test eax,eax; jle; dec eax; mov [edi+0x510],eax`, which is a reference count
being decremented. Twelve more (`magic_0565`, `0644`, `0691` through `0698`) do
`add [edi+0x510],8; mov eax,[edi+0x510]; mov [edi+0x50c],eax`, a struct field pair at `0x50c` and
`0x510`. `magic_0205` does arithmetic on it and `magic_0387` dereferences it. Same story for slots
629 and 630, where `magic_0165` and `magic_0606` were counted on
`mov eax,[edi+0x9D8]; mov [edi+0x9B8],eax; mov eax,[edi+0x9DC]`, a field copy chain. So 92 / 274 and
6 / 6 are the figures to use. Every other row of that section's table is confirmed unchanged,
including the pad readers at slots 302, 303 and 304 and the reels cluster, which the old matcher
also reported as zero.

**How the fixed scanner was checked.** The length decoder sweeps all 581 `.text` sections end to end
with nothing left over once the switch jump tables MSVC embeds in `.text` are skipped, which 347 of
the DLLs have. Three DLLs were then scanned a second time inside IDA Pro, driving IDA's own
disassembler and cross-reference graph through an independently written tracker:
`magic_0393` (341 sites over 141 slots), `magic_0009` (124 sites over 62 slots) and `magic_0162`
(464 sites over 156 slots). All three agree with `tools/magicdll.py` on every single call site
address, and IDA's disassembly of `magic_0162`'s four `[reg+0x9E0]` accesses shows them as plain
`mov [esi+0xD0], eax` field copies, which is the hand verification in section 4 arrived at a second
way. The provenance test rests on one measured fact: across all 581 DLLs the global holding the
table pointer is written exactly once each, in `InitMagicPRX`, so any load of it in `.text` is the
host table.

One thing the fixed scanner found that is worth its own line. **MSVC tail-merges host calls**, so one
`call eax` instruction can serve two different table slots. `magic_0009` at `0x100035F3` is reached
from a slot 329 load by fallthrough and from a slot 554 load by a `jmp`, and 1520 of the 74612 sites
are shared like that. The scanner takes the union at control-flow joins for that reason, so a site
count counts reachable (slot, call) pairs rather than distinct call instructions.

What it still does not follow: the table pointer spilled to a stack slot and reloaded, and loop back
edges, where the state at the loop head is cleared rather than iterated to a fixpoint. Both can only
undercount. The residue is measurable and small: for slot 324 every one of the 292 candidate
instructions in the corpus is now either accepted or shown above to be a struct access, with nothing
left unexplained.

## 8. What is untested, and what I think is most likely wrong

Ranked.

1. **Whether any DLL branches on `g_ffxIsCatchUpStep` or `g_ffxMainStepCounter`.** 576 DLLs hold
   pointers to both. This is the open question that decides whether DLL code is frame-dependent, and
   section 5 says why a static answer is not worth chasing and what runtime experiment to run
   instead.
2. **Whether the two attaching DLLs attach on a step-driven schedule.** The overlay step that drives
   them is step-driven, which is a strong argument but not a proof about the DLL's own control flow.
   This decides whether checksum region 15 is stable during spell animations.
3. **The 640-distinct-slot figure** is the tool's, across both its call scanner and its init-copy
   decoder. It is a lower bound by construction: the scanner does not follow the table pointer
   through a stack spill or around a loop back edge, so a DLL that only ever reaches a slot that way
   is missed. Section 7 says how small the measured residue is.
4. **27 of the 34 exposed globals are unnamed**, including an eleven-slot cluster in one small
   region. Cheap to work out and probably informative.
5. **The four simulation-reachable CRT `rand` callers in the exe are unidentified** `sub_` functions
   (`sub_63DF70`, `sub_9210E0`, `sub_937190`, and the thunk path through `sub_810E20`), so I do not
   know whether any makes a decision that matters.
6. **`sub_8722B0`**, the one caller of `AttachToParentBone` that `HELD_OBJECTS.md` does not account
   for.
7. **The four special-cased magic ids** in `MagicFile__start`: 670, 168, 434 and 610.
8. Nothing here has been observed in a running game. It is all static.
