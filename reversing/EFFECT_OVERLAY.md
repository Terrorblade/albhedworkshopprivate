# Effect overlay: GetEffectOverlayTable, the slot 3 and slot 4 step contract

PROGRESS 1.10.5 of Project Zanarkand. This extends `MAGIC_DLL.md` section 2 ("How a DLL gets stepped")
and corrects it in four places (section 11 below lists them).

All addresses are IDA VAs (RVA + 0x400000) unless a row says RVA. DLL addresses are the DLL's own VAs at
its preferred base 0x10000000. The exe was read with idalib on a scratch copy of the IDB, nothing renamed
and nothing saved. The 581 DLLs were read in place with `tools/magicdll.py`, and the few opened in IDA
were copied one at a time into the scratchpad and deleted afterwards. No game data is in either repo.
Proposed names are in `tools/apply_overlay_names.py`. This doc uses the current IDB names and gives the
proposed name the first time a function comes up.

Marks: **[V]** read in the binary, **[M]** measured across the 581 DLLs, **[I]** inferred.

Related: `MAGIC_DLL.md` (the 947 slot host table and InitMagicPRX), `MAIN_LOOP.md` (FFX_MainStep and
the sub-step loop), `THREADS_AND_CLOCKS.md` (threads and RNG streams), `ASYNC_LOAD_REQUESTS.md` H3 and
H4 (the async DLL load), `BATTLE_COMMAND.md`, `BATTLE_ACTOR.md` (actor fields), `CHEAT_BATTLE.md` (the
hit application chain), `ATEL_CONTEXT.md` 6.3 (BtlEv group 2).

---

## Summary, read this first

1. **GetEffectOverlayTable(which) returns a pointer to 16 dwords when `which` is 0, and NULL for anything
   else.** All 581 DLLs have the same 8 instruction stub. The host always passes 0. [V] [M]
2. **The host calls only five of the sixteen.** Slot 0 is start(handle, 2, 0), slot 1 is free(0, 0),
   slot 3 is the per sub-step step, slot 4 is the per effects pass step, and slot 8 is an end phase
   hook used only by magic 671. Slot 7 is not code. It points at an ATEL script blob the battle event
   system runs. Slots 2, 5, 6 and 9 to 15 have no host caller. [V]
3. **Slot 3 and slot 4 take one int that is always 0, are cdecl, and their return value is ignored.**
   `FFX_Magic_CallOverlayStep(0)` calls every live slot 3 and `FFX_Magic_CallOverlayStep(1)` every live
   slot 4. Live means the effect state byte is 4 or 5. The exe's own resident table runs first each
   time. [V]
4. **Both run on the main thread inside FFX_MainStep, and neither is gated on catch-up.** Slot 3 runs once
   per sub-step after the battle step, ATEL and the message windows, or once when a step has no
   sub-steps. Slot 4 runs once per effects pass, `max(subSteps, 1)` times, just before
   `FFX_Vfx_StepAll`. The gates on both are simulation state. [V]
5. **The overlay is the battle action's clock, so it is simulation, not presentation.** DLL code calls
   host 226 `FFX_Btl_EffectSetReady` (state 4 to 5, starts the hit reactions and overdrive gain) and
   host 214 `FFX_Btl_EffectAdvanceState` (state 5 to 6). `FFX_Btl_FinishActions` waits for state 0 or 6
   before it applies the HP, MP and CTB changes and ends the turn. 411 of 581 DLLs call both. So a magic
   effect decides the step on which its action resolves. [V] [M]
6. **137 DLLs draw the shared battle RNG** (host 324 `FFX_Btl_Rand` or 307, its float wrapper, on
   `g_ffxBattleRandState`), 705 call sites in all. 275 DLLs apply pending hits directly through host 239.
   [M]
7. **The "builtin effect overlay table" at 0x1340870 is not an overlay table.** It is a 5 MB menu GS
   backup buffer. The battle code stores its address as a nonzero "loading" marker and never calls
   through it. [V]
8. **224 DLLs rewrite their own slot 3 and slot 4 at run time**, once, as a phase switch. Exactly the 224
   whose slot 3 starts as a stub. A recreation cannot treat the table as constant. [M]
9. **4 DLLs read `g_ffxIsCatchUpStep` and 20 read `g_ffxMainStepCounter`.** `MAGIC_DLL.md` said none
   was found. The one catch-up reader decompiled skips draw bookkeeping only. [M] [V]

---

## 1. How the host finds the table

`MagicFile__getEffectOverlayTable 0x9DA370` returns 0 when `g_ffxMagicFileId` or `g_ffxMagicFileHinst`
is -1. Otherwise it tail jumps to the DLL export at 0x9DA386 `jmp g_ffxMagicGetEffectOverlayTable`
(0x22FB6A4, written by `MagicFile__onDllLoaded` at 0x9DB158). Because it is a jump, the caller's pushed
argument is what the export reads, and every caller pushes 0 (for example `push 0` at 0x788159). [V]

The export, the same in all 581 [M]:

```
push ebp
mov  ebp, esp
xor  ecx, ecx
cmp  [ebp+8], ecx
mov  eax, <table VA>
cmovne eax, ecx      ; which != 0 -> NULL
pop  ebp
ret
```

Five call sites [V]:

| caller | site | what it does with the pointer |
| --- | --- | --- |
| `FFX_Btl_StartMagicPrx 0x788110` | 0x78815B | `mov [esi+0F8Ch], eax` at 0x788163, then clears the loading flag `actor+0xDFD` |
| `maybe_FFX_Magic_StartGlobalPrx` | 0x788D36 | `mov dword_1133360, eax` at 0x788D3E (proposed `g_ffxGlobalEffectOverlayTable`) |
| `FFX_Magic_CallOverlayStep 0x787E00` | 0x787E7F, 0x787EE2 | compares it to the stored pointer to decide whether to set `g_ffxInMagicOverlayCall` |
| `maybe_FFX_Magic_OverlayFree 0x787AB0` | 0x787ABF | calls slot 8 of it, magic 671 only |

`FFX_Btl_StartMagicPrx` and the global twin run right after `MagicFile__start` (which hands the DLL the
host table through `InitMagicPRX`). They are reached from `FFX_MainStep_PollMagicPrxLoad 0x822250`, once
per step from `FFX_StepPacing`, when the async DLL load completes (`ASYNC_LOAD_REQUESTS.md` H3, H4). The
step that load completes on is wall clock dependent, see section 9. [V]

So the overlay pointer lives in two places. One per battle actor at `actor+0xF8C` (`m_iF8C` in
`ffx_types.h`), with the state byte at `actor+0xDFB`, for actors 0 to 30. One for the global effect (the
game over visual and similar, unit kind 0x80) at `dword_1133360`, with the state byte at
`byte_1133364`.

## 2. The table layout

### 2.1 The 16 slots

`which == 0` gives 16 dwords, 0x40 bytes. Counts are non-null entries across the 581 [M]:

| slot | +off | non-null | what it is | host caller | args | body classes in the 581 |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | +0x00 | 581 | start | battle state 11 in `sub_788480` (0x7886F1 on), global state 3 in `sub_788D60` | `(handle, 2, 0)` cdecl | real code 581 |
| 1 | +0x04 | 581 | free | `sub_787AF0` (battle state 6 cleanup), `sub_788C00` (global) | `(0, 0)` cdecl | real 473, return 0 in 100, return 1 in 8 |
| 2 | +0x08 | 581 | unused | none | | real 40, return 0 in 361, return 1 in 50, bare ret 130 |
| 3 | +0x0C | 581 | step | `FFX_Magic_CallOverlayStep(0)` | `(0)` cdecl, return ignored | real 357, return 0 stub 224 |
| 4 | +0x10 | 581 | effects pass | `FFX_Magic_CallOverlayStep(1)` | `(0)` cdecl, return ignored | real 580, return 0 in magic_0070 |
| 5, 6 | +0x14, +0x18 | 581 | unused | none | | return 0 in 380, return 1 in 50, bare ret 151 |
| 7 | +0x1C | 529 | data, ATEL script blob | `sub_788EF0` at battle state 3 | read, not called | .data pointer 529 |
| 8 | +0x20 | 465 | end phase hook | `maybe_FFX_Magic_OverlayFree`, only when `word_112C94E == 671` | `(handle, 2, 0, 0)` cdecl | real 1 (magic_0671), return 0 in 263, return 1 in 50, bare ret 151 |
| 9 to 14 | +0x24..+0x38 | 415 | unused | none | | return 0 in 264, bare ret 151 |
| 15 | +0x3C | 581 | unused | none | | return 0 in 401, return 1 in 50, bare ret 130 |

The handle passed to slot 0 is `sub_788DF0` (proposed `FFX_Magic_PackEffectHandle`):
`(counter & 0xFFF) << 16 | kind << 8 | unit`. Kind 2 is a battle unit, with the counter in the actor word
`+0xE1C` (bumped after the call). Kind 0x80 is the global effect, with the counter in `dword_1133358`.
The handle is what the DLL later hands back to hosts 226, 214 and 227. [V]

The slot 3 and slot 4 signatures in the decompiles are `int ovl_sN()`, they read no stack argument
(magic_0020's slot 4 decompiled as `__fastcall`, a Hex-Rays artefact). Slot 0 is `int __cdecl (int
handle)` in every DLL decompiled, it ignores the 2 and the 0. [V]

### 2.2 Slot 7, the script blob

`sub_788EF0` (proposed `FFX_Btl_InstallMagicEffectScript`) runs at battle state 3, right after
`FFX_BtlOd_MaybeStartMinigame` picks the next state. It reads `[actor+0xF8C] + 0x1C`, slot 7, unless
`dword_1133368` overrides it (only zeroing writes to that were found), and hands it to `sub_797550`
(proposed `FFX_BtlEv_LoadEffectScriptBlob`) [V]:

```c
dword_1135DF4 = blob + blob[1];         // +0x04 is always 0x10
dword_1135DF8 = blob + blob[2];         // read by FFX_BtlEv_StartFirstMatch and FindSlotRecord
FFX_BtlEv_AddScriptBlock(2, 0, dword_1135DF4);
FFX_BtlEv_BuildScriptContext(2);
```

BtlEv group 2 is ATEL context 4 (`ATEL_CONTEXT.md` 6.3). **So slot 7 is battle event script, and
simulation.** It is the same blob the `ef2` command of `magicdll.py` dumps, and its +0x04 and +0x08 are
now known to be the script block and the event table offsets. What the scripts do is not read here. [V]
for the host path, [I] for their content.

### 2.3 Past +0x3F: the yonishi header

The table is not always 16 dwords. Effect units (`g_ffxEffectUnitPtrs 0x12A40C0`) keep a pointer at
`unit+0x50` to an overlay header, and the exe's effect program code reads that header past slot 15
[V]:

| header offset | reader | what is called |
| --- | --- | --- |
| +0x48 | Oef2 op 221 `opu_part_run` at 0x80BCD8 | step a kind 3 object's ppp prim list (`[edi+350h]` is the header, then `call [eax+48h]`). `YONISHI_ENGINE.md` |
| +0x54 | `sub_8014C0` at 0x801661 | free an object's prim list, kind 3 objects |
| +0x60 | `sub_8170C0` at 0x81715C | a per object callback |
| +0x74 | Oef2 opcode `sub_80B180` at 0x80B3F2 | registered as a VFX callback, group from the op word |
| +0x78 | `opp_main` at 0x80C222 | the DLL's own particle operator table, `[(op & 0x7F)]` |
| +0x7C | `FFX_Magic_Oef2Dispatch` at 0x80D566 | the DLL's own Oef2 opcode table, for script words with bit 0x100 |

**130 DLLs** write `&table` into their effect unit's `+0x50` (magic_0021 slot 0: `*(this+20) =
&g_effectOverlayTable`). These are the ports of the exe's yonishi effect interpreter. In them the table
is a 32 dword header, and all 130 have `+0x7C` pointing at a `.data` table of function pointers. The
other 451 reference the table VA only once, from the export, and their bytes past +0x3F are unrelated
data. [M] (Corrected 2026-10-27 by `MAGIC_DLL_CONTENTS.md`: this said 132 and 131. magic_0350 and
magic_0380 hold the table VA a second time only as a loop bound, `cmp edi, table` over the array that
ends where the table starts, and magic_0605's +0x7C dword is an unrelated global that happens to point
at a `.data` array holding a code pointer. None of the three
calls `FFX_Magic_Oef2Dispatch`. All 130 real ports do. What a port DLL contains is in that doc.)

### 2.4 The exe's resident table

`off_C48D88` (proposed `g_ffxResidentEffectOverlayTablePtr`) points at `0xC48D90`, a 32 dword header of
the same shape [V]:

| slot | function | what |
| --- | --- | --- |
| 0 | `sub_800580` | writes the table into effect unit 0 `+0x50` (`off_C88A90+80`). `FFX_Rand_SeedCosmeticStreams` calls it at boot |
| 1, 2 | `sub_8005F0`, `sub_800600` | return 0 |
| 3 | `sub_800610` | `FFX_Magic_Oef2Dispatch(0, 0)` |
| 4 | `sub_800670` | `sub_7E4720` texture upload drain, `sub_800170` stream request, camera matrices, `FFX_Magic_Oef2Dispatch(0, 1)`, `opp_main(0)` |
| 5, 6, 8..14 | `nullsub_723` | |
| 7 | 0 | no script |
| 15 | `sub_8006F0` | returns 0 |
| +0x40..+0x54 | `sub_800700`, `sub_800770`, `sub_8008B0`, `sub_800960`, `sub_800970`, `sub_8009C0` | particle set relocate, prim list alloc, prim list clock step, stub, free all, free one |
| +0x74 | `sub_800A30` | `FFX_Magic_Oef2Dispatch(0, 2)`, a third Oef2 pass run as a VFX callback |

`0xC48D8C` points at `0xC48DD0`, the upper half, with no code reader. This is not a second 16 slot
table, it is the same header from +0x40.

### 2.5 The 0x1340870 marker, corrected

The IDB calls `0x1340870` `g_ffxBuiltinEffectOverlayTable` with the comment "the host's own fallback
effect overlay table" and "Same 16-slot shape". Both are wrong [V]:

- `FFX_MenuSys_RunFrame` does `memset(0x1340870, 0, 0x500000)`, and `sub_8AA690` and `sub_8AA6D0` fill it
  with thirteen 64 KB GS reads through `sub_8AA710`. It is the menu's GS backup buffer.
- `FFX_Btl_BeginMagicEffect` (0x788063), `FFX_Btl_StartMagicPrx` (0x788141) and the two global effect
  functions store its **address** into `actor+0xF8C` or `dword_1133360` while the DLL loads. State 1
  tests `actor+0xF8C != 0` to fire the cast event, and the address is a convenient nonzero value.
- Nothing ever calls through it. `FFX_Magic_CallOverlayStep` only calls in state 4 or 5, slot 0 is only
  called at state 11, and state 3 does not leave until `actor+0xDFD` clears, which
  `FFX_Btl_StartMagicPrx` does right after storing the real table.
- The 0xFF bytes IDA shows there are uninitialized data (see the memory note on uninitialized data
  reading as zero). At run time it is zero or menu pixels.

Proposed rename `g_ffxMenuSysGsBackupBuffer`.

## 3. The effect state machine

The byte at `actor+0xDFB` (`m_bDFB`), walked by `sub_788480` (proposed
`FFX_Btl_StepMagicEffectStates`) once per battle step from `FFX_Btl_MainStep` [V]:

| state | meaning | leaves when |
| --- | --- | --- |
| 0 | idle | `FFX_Btl_BeginMagicEffect 0x787F20` sets 1 ("ReEFF" error if not 0) |
| 16, 14 | async load pending, retry | the load poll (`ASYNC_LOAD_REQUESTS.md`) |
| 1 | begin | AI and commit, cast event fired if `actor+0xF8C != 0`, then 2 or 3 |
| 2 | cast motion | the motion code (`sub_7AB6C0`, Btl_018) sets 3 |
| 3 | wait | `actor+0xDFD` (loading) clear, no voice busy, no explode, debug stop off. Then slot 7 is installed and the state becomes `FFX_BtlOd_MaybeStartMinigame`'s answer, 11, or 12 for an overdrive minigame |
| 12, 13 | overdrive minigame | back to 11 |
| 11 | start | `actor+0xF8C != 0`: state 4, slot 0 `(handle, 2, 0)`, `FFX_SetEffectsPassEnabled(1)`. Otherwise reactions start directly and state 6 |
| 4 | effect running, not ready | DLL calls host 226 |
| 5 | effect ready, hits playing | `sub_789030` runs every battle step, DLL calls host 214 |
| 6 | effect done | `FFX_Btl_FinishActions` applies the hits and frees |
| 7, 8, 9, 10 | non-magic and follow-up paths | |

The transitions the DLL drives [V]:

- **4 to 5, host 226 `maybe_FFX_Btl_EffectSetReady 0x788200`.** `cmp byte ptr [esi+0DFBh], 4` at
  0x788236, then `sub_788F90` (proposed `FFX_Btl_StartEffectReactions`: the target reaction motion and
  `sub_789030`, which starts per target hit reactions, overdrive gain through `sub_7B12C0` and the damage
  numbers), the ability event, and `mov byte ptr [esi+0DFBh], 5` at 0x788261. The second argument picks
  the command's own reaction motion (1) or motion 51 (0).
- **State 5, every battle step:** `sub_789030` steps the reactions and sets `actor+0xE0D = 1` when all are
  done. Host 227 `maybe_FFX_Btl_GetUnitEffectSlot 0x787CA0` returns `actor+0xE0D` for kinds 2, 4 and 7
  and 1 for kind 0x80. That is "hits done", not a slot (proposed `FFX_Btl_GetEffectHitsDone`).
- **5 to 6, host 214 `maybe_FFX_Btl_EffectAdvanceState 0x788920`.** `cmp byte ptr [ecx+0DFBh], 5` at
  0x78895F, "effect does not ready call" if not, then `mov byte ptr [ecx+0DFBh], 6` at 0x78897A. Kind
  0x80 writes `byte_1133364 = 6` at 0x788986.
- **6 to 0, `FFX_Btl_FinishActions 0x7911E0`.** It reads `[esi+0DFBh]` at 0x79123E and waits unless it is
  0 or 6. Then it loops `sub_78D980(unit, -5, 0x400)` at 0x7912CB until it returns 0, which applies every
  pending HP, MP and CTB change, and calls `sub_787AF0` (slot 1 free, `op_ovl_effect_free`,
  `MagicFile__stop`, state 0).

DLL code can also apply hits early: host 239 is `sub_78D980` itself (`CHEAT_BATTLE.md`
`FFX_Btl_EffectCompleteStep`), and the Oef2 opcode `sub_815D90` (table 0xC48F50) calls it. The Oef2
opcode `sub_816680` (table 0xC491C8) is a sync op: cases 1 and 2 set and clear `byte_11A0061` (read by
host 225 `sub_7FCE10`), 3 and 5 wait, 4 and 6 call `EffectSetReady(h, 1 or 0)`, 7 and 8 toggle the map,
character and message window draw enables. `sub_80C490` and `sub_7FE6C0` are Oef2 opcodes that call
`EffectAdvanceState`, and Yanagi's host 437 `sub_925440` calls it when its program ends. [V]

The global effect runs the same machine on `byte_1133364` with fewer states: `sub_788D60` does 3 to 4
and slot 0, the DLL does 4 to 5 to 6 through the same hosts, `sub_788C00` frees at 6. `sub_788B90`
asserts "EFFECT SYNC ERROR" and forces 6 when a new global effect starts over a live one. [V]

## 4. When slots 3 and 4 run

### 4.1 FFX_Magic_CallOverlayStep 0x787E00

`void __cdecl FFX_Magic_CallOverlayStep(int pass)`, pass 0 or 1 [V]:

1. pass 0: `mov eax, off_C48D88` / `mov eax, [eax+0Ch]` / `push 0` / `call eax` (0x787E31 to 0x787E3B),
   the resident slot 3. pass 1: `FFX_Glue_SetDrawLayer(2)`, resident slot 4 (`[eax+10h]` at 0x787E20),
   `FFX_Glue_SetDrawLayer(4)`.
2. If `FFX_Battle_IsActive`, for actors 0 to 30 whose `[eax+0DFBh]` is 4 or 5 (test ends at 0x787E67):
   `mov esi, [eax+0F8Ch]` at 0x787E69, `g_ffxInMagicOverlayCall = 1` only if `esi` equals the current
   DLL's `MagicFile__getEffectOverlayTable()`, then `mov eax, [esi+0Ch]` at 0x787EA4 (pass 0) or `mov
   eax, [esi+10h]` at 0x787E9F (pass 1), `push 0` / `call eax` / `add esp, 4`, and the flag cleared.
3. The same for the global effect, `byte_1133364` 4 or 5 with the table in `dword_1133360` (0x787EBF to
   0x787F0F).

The argument is always 0, the return is discarded, the caller cleans the stack. Only one DLL is loaded
at a time (`g_ffxMagicFileId`), so every live table is the same DLL's, and the flag test only fails for a
stale pointer. [V]

### 4.2 The three call sites in FFX_MainStep 0x820AE0

All on the main thread, which is the simulation thread (`THREADS_AND_CLOCKS.md` T0) [V]:

| site | code | where | gate | runs |
| --- | --- | --- | --- | --- |
| 0x82103E | `push 0` / `call` | inside `for (i < FFX_Player__getSubStepCount())`, after `FFX_Player__stepControl`, `FFX_Btl_MainStep`, `FFX_Atel_StepOnce` and `FFX_MesWin_StepAll`, before the profiler mark and `FFX_Came_StepAll` | `g_ffxMovieScreenMode == 0` | once per sub-step |
| 0x8210AC | `push eax` (0) / `call` | the `getSubStepCount() == 0` arm | none | once, on a step with no sub-steps |
| 0x82149D | `push 1` / `call` | the effects pass loop, `for (k < max(subSteps, 1))`, after `maybe_FFX_Yonishi__feedPadStateBothPorts` and the map effects, before `FFX_Vfx_StepAll`, DmaSync and `FFX_Vfx_EndPass` | `(g_ffxMovieScreenMode == 0 or the movie draw env check passes) and g_ffxEffectsPassEnabled and g_ffxMovieScreenModePrev == 0 and g_ffxScriptEffectsPassOff == 0` | once per effects pass |

None is gated on `g_ffxIsCatchUpStep` or its twins. The gate flags are all simulation state:
`g_ffxEffectsPassEnabled` is only ever written 1 (`FFX_MainInit`, `FFX_Btl_Init`, state 11),
`g_ffxScriptEffectsPassOff` is set by script (Sg_070), and the movie mode bytes are set by movie start
and end. [V]

Order inside one sub-step, for a battle: battle step (state machine, reactions) -> ATEL -> message
windows -> **slot 3** -> camera. After all sub-steps: map effects -> **slot 4** -> VFX step -> VFX end
pass (callback group 32, which is where DLL callbacks registered through host 312 run). So slot 3 sees
the state the battle step just produced, and the battle step of the next sub-step sees what slot 3 and
slot 4 wrote. [V]

### 4.3 What a step with no sub-steps does

A step with no sub-steps still calls slot 3 once and slot 4 once. DLLs guard against that themselves:
host data slot 758 is `dword_230FD34` (proposed `g_ffxVfxTimeFrozen`), and DLLs write
`*g_hostData758 = (FFX_Player__getSubStepCount() == 0)` (magic_0003, magic_0515). About 140 exe readers
in the effect program code (for example `sub_712250`, the prim list clock) stop advancing time while it
is set, and the DLL task loops skip their advance. So effect time advances per sub-step, not per call.
[V]

## 5. What slots 3 and 4 touch

Everything goes through the host table. Measured over each DLL's slot 3 and slot 4 bodies, including the
bodies a DLL swaps in at run time, following direct calls ("direct") and also functions whose address
the code takes, which is how the task frameworks reach their methods ("taken"). Counts are DLLs [M]:

| host slot | what | slot 3 | slot 4 direct | slot 4 taken | anywhere in DLL code |
| --- | --- | --- | --- | --- | --- |
| 226 | `EffectSetReady` | 172 | 188 | 188 | 411 |
| 214 | `EffectAdvanceState` | 5 | 153 | 241 | 411 |
| 239 | apply pending hits now | 5 | 1 | 58 | 275 |
| 324 / 307 | `FFX_Btl_Rand` / float wrapper | 4 / 4 | 5 / 0 | 5 / 10 | 92 / 81, 137 together |
| 225 | effect hold flag | 184 | 215 | 215 | 411 |
| 227 | hits done | 116 | 164 | 164 | 357 |
| 313 | texture upload pending | 120 | 0 | 0 | 199 |
| 549 | stream request pending | 116 | 0 | 0 | 216 |
| 716 | `FFX_Magic_Oef2Dispatch` | 73 | 117 | 117 | 130 |
| 721 | `opp_main` | 0 | 73 | 73 | 130 |
| 437 | Yanagi program step | 0 | 36 | 36 | 40 |
| 253 | `FFX_Player__getSubStepCount` | 51 | 158 | 160 | 180 |
| 213 / 228 | unit motion | 0 / 6 | 3 / 0 | 52 / 45 | 288 / 289 |

Slot 0 also takes the addresses of task methods that reach 239 (78 DLLs), 307 (41) and 324 (19), which
run later from slot 3 or 4. 63 DLLs reach none of 226, 214, 716, 437 or 721 from slot 3 or 4 even
through taken addresses, so their task dispatch is further away than the scanner follows. All static
counts are lower bounds. The "anywhere" column is a plain count of call sites in the DLL. [M] These
counts were taken before `tools/magicdll.py` learned to see `jmp eax` tail calls through the host
table (2026-10-27, `MAGIC_DLL.md` section 7), so a host reached only through a forwarding thunk is
missing from them. None of the rows above is a ppp operator, which is where most thunk-only slots are.

Beyond the host table, slot 3 and slot 4 write [V] [M]:

- **The DLL's own globals**, task lists and phase counters.
- **The DLL's own overlay table**, in 224 DLLs (section 6).
- **Effect units** through `g_ffxEffectUnitPtrs`, which the host also exposes as data (slot 743).
- **Host data globals** copied out at init, notably slot 758 (`g_ffxVfxTimeFrozen`) and 827
  (`g_ffxInMagicOverlayCall`, which DLLs set around their own Oef2 calls, for example magic_0021).
- No DLL code was seen on a worker thread. `FFX_Vfx_PrimBuildRunSlots 0x71ED10` dispatches only host
  code.

The two waits slot 3 uses most are deterministic per step: host 313 `sub_7E46D0` polls the 64 entry
texture upload ring at `dword_11A03A0`, which the resident slot 4 drains through `sub_7E4720` at
0x100000 bytes per effects pass. Host 549 `sub_8000F0` polls a PS2 era stream request, and on PC
`sub_A44770` returns -1 so it clears after one resident slot 4 pass. [V]

## 6. The table is mutable

224 DLLs store new values into their own slot 3 and slot 4 at run time, with `mov dword ptr
[table+0Ch], imm32` and `[table+10h]`, once each. They are exactly the 224 whose initial slot 3 is a
`return 0` stub. magic_0009 is typical: slot 4 calls `EffectSetReady`, waits for host 227, then at
0x10001270 and 0x1000127A writes 0x10001290 and 0x100012E0 into slots 3 and 4. After that the step is
the second phase body. [M] [V]

The host reads `[esi+0Ch]` and `[esi+10h]` fresh every call, so the swap takes effect on the next call.
No DLL stores into slots 0 to 2 or 5 to 15. About 50 DLLs store at +0x40 to +0x7C, which is the yonishi
header in the ports and an unrelated global that happens to follow the table in the rest. [V] [M]

## 7. Representative decompiles

[V], each opened from a scratch copy, host table typed as a struct:

- **magic_0003 (the "ego" task framework, the most common shape).** Slot 3 is a phase sequencer. It
  waits on host 225 (hold), then 313(2) (textures), then 549(2) (stream), calls `EffectSetReady(h, 1)`,
  then waits for 227 to read 1 or more. Slot 4 does nothing until the phase is 4 or more, then runs the
  task list, and calls `EffectAdvanceState` when the list is empty. Writes `g_hostData758`.
- **magic_0009.** Slot 3 is a stub. Slot 4 calls `EffectSetReady`, waits on 227, then swaps slots 3 and
  4 (section 6).
- **magic_0021 (a yonishi port).** Slot 0 runs `FFX_Effect_InitUnit(1, ..)` and sets `unit+0x50 =
  table`. Slot 3 runs `FFX_Magic_Oef2Dispatch(1, 0)` with host data 827 set. Slot 4 runs the camera,
  `Oef2Dispatch(1, 1)` and `opp_main(1)`. The Oef2 script itself calls `EffectSetReady` and
  `EffectAdvanceState` through the exe's opcodes.
- **magic_0300 (Yanagi).** Slot 0 calls hosts 436 and 434, slot 3 calls 438, slot 4 calls 437, which
  advances the state when its program ends.
- **magic_0515.** `EffectSetReady(h, 0)` at start, `FFX_Btl_RandFloat1to2` draws in slot 3,
  `EffectAdvanceState` at its phase 100.
- **magic_0162.** Task lists keyed by a group byte. Slot 3 runs group 1, slot 4 runs groups 0 and 2.

## 8. Is anything they write observable

Yes. In order of weight [V] [M]:

1. **The step an action resolves on.** Hit reactions and overdrive gain start on the step the DLL calls
   226. HP, MP and CTB change on the step it calls 239, or on the step `FFX_Btl_FinishActions` runs after
   214. The next turn waits for state 0 or 6. Every one of those is DLL control flow.
2. **The battle RNG.** 137 DLLs advance `g_ffxBattleRandState`, the stream damage and AI draw from. A DLL
   that draws a different number of times shifts every later battle roll.
3. **Slot 7 is ATEL script** in BtlEv group 2, and it can do whatever battle event scripts do.
4. **The draw enables** toggled by the Oef2 sync op cases 7 and 8 (map, characters, message windows) and
   `g_ffxInMagicOverlayCall`, which draw functions read (`sub_71E610` special cases eleven magic ids, and
   `sub_6604D0`, `sub_661C80`, `sub_713790` and others). These are presentation.

What is presentation only: the effect unit draw, particle and prim lists, the cosmetic RNG
(`g_ffxEffectRandState`, hosts 314 and 404), and CRT `rand` (hosts 452, 153, 191, 613, per thread). Six
DLLs (0285, 0286, 0287, 0377, 0385, 0417) reach CRT `rand` through the IAT from slot 4. [V] [M]

## 9. Catch-up and the step counter

`MAGIC_DLL.md` said no DLL was found that branches on `g_ffxIsCatchUpStep` (host data 936) or
`g_ffxMainStepCounter` (764). A scan of loads through each DLL's copied pointer finds some [M]:

- **`g_ffxIsCatchUpStep`: 4 DLLs**, 0515 (at 0x1000420D), 0611, 0667 and 0668. In 0515 the reader is
  `sub_100041F0`, a callback slot 0 registers through host 312, so it runs in `FFX_Vfx_EndPass` inside
  the effects pass. It skips its trail mesh build (`sub_10002FA0`, `sub_10003520`) when the flag is set.
  The skipped code writes DLL private draw bookkeeping, mesh handles and a high water count. Slot 3 also
  ages records at `0x10351DD0`, and whether slot 3 reads anything the skipped build wrote is not
  settled (section 12). [V]
- **`g_ffxMainStepCounter`: 20 DLLs**, 0446, 0543 to 0546, 0555 to 0559, 0565, 0644 and 0691 to 0698.
  magic_0446 spawns particles when `(counter & 3) == 0`, with the cosmetic float RNG (host 314), which is
  presentation. The counter only advances on the gameplay path, so it is a step clock, but it is an
  absolute one, see section 10. [V]

The exe side has no catch-up gate on any overlay call (section 4.2).

## 10. Co-op and recreation

**Can a magic effect change the step a battle action resolves on? Yes.** The DLL is the clock for its
own action (section 8.1). Everything it reads to decide is per step and on the main thread, so two peers
running the same steps resolve on the same step, given the hazards below. **Is RNG drawn from the shared
stream? Yes, in 137 DLLs**, through hosts 324 and 307. [V] [M]

Hazards, ranked:

1. **The start step depends on the async DLL load.** `FFX_MainStep_PollMagicPrxLoad` advances the state
   when the file load completes, which is wall clock. A peer with a slower disk starts the effect on a
   later step, and every later step of the action moves with it. This is the big one and it is outside
   the DLL. Lockstep has to agree the step the load is consumed on (`ASYNC_LOAD_REQUESTS.md` H3, H4).
2. **Shared battle RNG draws inside effect code.** A recreation that skips, reorders or reimplements a
   DLL's draws changes damage and AI. The draw count per effect must match the DLL, step for step.
3. **Slot 4 drives simulation from the effects pass.** 241 DLLs reach `EffectAdvanceState` from slot 4,
   and 58 apply hits from there. A port that treats slot 4 as "the draw" and moves it to a render thread
   or skips it on catch-up breaks the battle. The effects pass is gated by `g_ffxEffectsPassEnabled` and
   the movie and script flags, all simulation, and must stay on the step.
4. **The texture upload budget is part of the timing.** Slot 3 waits on host 313, which clears at
   0x100000 bytes per effects pass. The step a spell becomes ready on depends on the size of its texture
   uploads. A recreation with a different upload path must still count the same passes.
5. **The table is mutable** (224 DLLs) and **slot 7 is script**. A recreation that caches the slots or
   treats slot 7 as data breaks.
6. **Four DLLs read the catch-up flag, twenty the absolute step counter.** The one decompiled catch-up
   reader only gates draw bookkeeping. The step counter readers are deterministic per step but read an
   absolute frame number, so a peer whose counter is offset (a late join, a transplant) gets different
   particle timing. That is presentation in 0446 and unread in the rest.
7. **A step with no sub-steps still calls slot 3 and 4.** Effect time is frozen through host data 758,
   which the DLL sets itself. A recreation must keep the extra call and the freeze, or its effects run one
   tick fast on those steps.

For Project Zanarkand: the contract to keep is "slot 3 once per sub-step after battle, ATEL and message
windows, slot 4 once per effects pass before the VFX step, both with argument 0, both only while the
state is 4 or 5, resident first". The state machine in section 3 is the interface between the effect and
the battle, and it is small enough to recreate exactly.

## 11. Doc fixes made

- `MAGIC_DLL.md` section 2: the slot 3 guard is `g_ffxMovieScreenMode` (was `byte_12FB8FB`), the slot 4
  guard names, and `g_ffxInMagicOverlayCall` is set only for the current DLL's table, not around every
  call, and DLLs set it themselves too.
- `MAGIC_DLL.md` summary item 3, section 3 slot 827, section 5 and section 8 item 1: the catch-up and
  step counter readers now exist (section 9 here).
- `BATTLE_START_AND_END.md` phase 32: `maybe_FFX_Magic_OverlayFree` is passed `word_112C94E`, and only
  671 calls slot 8. 670 takes the plain free path.
- `MAGIC_DLL.md` section 1 table: `MagicFile__getEffectOverlayTable` is exactly RVA `0x5DA370`.
- `tools/magicdll.py`: `OVERLAY_SLOT_NOTES` roles, the `ovl` footer that said nothing past +0x3F is part
  of the table, and the `ef2` header rows +0x04 and +0x08.
- IDB, proposed only: `g_ffxBuiltinEffectOverlayTable` and its comment, the `FFX_Magic_CallOverlayStep`
  comment ("guarding each call with dword_113335C = 1"), and the `g_ffxInMagicOverlayCall` comment ("set
  to 1 around every call"). The rename is in `apply_overlay_names.py`. The two comment fixes have to be
  made by hand, since the script only appends.

## 12. Addresses

| VA | what |
| --- | --- |
| 0x9DA370 | `MagicFile__getEffectOverlayTable`, tail jump at 0x9DA386 |
| 0x22FB6A4 | `g_ffxMagicGetEffectOverlayTable`, the resolved export |
| 0x787E00 | `FFX_Magic_CallOverlayStep` |
| 0x82103E, 0x8210AC, 0x82149D | its call sites in `FFX_MainStep` |
| 0xC48D88, 0xC48D90 | resident table pointer and table |
| 0x788110 | `FFX_Btl_StartMagicPrx`, stores `actor+0xF8C` at 0x788163 |
| 0x788480 | `sub_788480`, the state machine |
| 0x788200 | host 226, `EffectSetReady` |
| 0x788920 | host 214, `EffectAdvanceState` |
| 0x787CA0 | host 227, hits done |
| 0x78D980 | host 239, apply pending hits |
| 0x7911E0 | `FFX_Btl_FinishActions` |
| 0x787AF0 | battle free, slot 1 |
| 0x787AB0 | end phase, slot 8 for 671 |
| 0x788EF0, 0x797550 | slot 7 install |
| 0x788DF0 | handle pack |
| 0x1133358, 0x1133360, 0x1133364, 0x113336C | global effect counter, table, state, handle |
| 0x113335C | `g_ffxInMagicOverlayCall`, host data 827 |
| 0x230FD34 | effect time frozen, host data 758 |
| 0x1340870 | the menu GS backup buffer, not a table |
| actor+0xDFB, +0xDFD, +0xE0D, +0xE1C, +0xF8C | state, loading, hits done, handle counter, table |

## 13. Residuals

1. **Whether magic_0515's slot 3 reads anything its catch-up gated callback writes.** If it does, that
   DLL's simulation-side record ageing depends on frame rate. 0611, 0667 and 0668 are not decompiled.
2. **63 DLLs' dispatch was not followed** to the state hosts. They must call 226 and 214 somewhere (411
   DLLs call both, and the state machine stalls otherwise), but the step they do it on is not checked.
3. **Slot 7 script content** is not read. It runs in BtlEv group 2 and may itself touch battle state.
4. **Exe readers of resident header +0x40 to +0x50** were not found by the disassembly scan. They are
   probably reached through a copied pointer.
5. **The 224 DLL swap timing** was checked in magic_0009 only.
