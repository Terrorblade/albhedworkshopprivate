# Monster AI scripts

PROGRESS item 1.11.9 of Project Zanarkand. All addresses are VAs as IDA shows them, RVA = VA -
0x400000. Every claim is marked:

- **[V]** verified, read off Hex-Rays and, where it mattered, the disassembly
- **[D]** verified against the shipped data files in the install, all 361 monster files
- **[I]** inferred, with the reason given

The IDB was read only. Nothing was renamed or commented. Names for the `sub_` functions are the
proposals in section 10, and the doc uses them in brackets the first time.

Read with: `EBP_FORMAT.md` (the ATEL VM, its block format and opcodes), `BATTLE_COMMAND.md` (the
72 byte record, the commit, the target mask), `BATTLE_CTB_AND_STATUS.md` (the turn, the stream
map), `BATTLE_START_AND_END.md` (the monster file chain). The reader is
`tools/mon_ai.py` in the projectzanarkand repo, section 8.

---

## 0. The short version

- **A monster's AI is ATEL bytecode.** Same VM, same block format, same opcodes as a field `.ebp`.
  It is section 0 of `battle/mon/_mNNN/mNNN.bin`, bound to `actor+0xF78`. [V] [D]
- **Section 1 is an event map** at `actor+0xF7C`: (slot, event kind) -> (ATEL actor, entry point).
  Slot 61 is the AI. Slots 0, 4 and 64 are battle event and motion hooks. [V] [D]
- All eight monsters' blocks are loaded into **one ATEL context, number 3**, when the last monster
  file finishes loading. Each ATEL actor ("worker") of slots 61, 4 and 64 is bound to its battle
  unit by writing the unit index into `atelActor+0xC0`, and that byte is how every Btl syscall
  finds "self". [V]
- `FFX_Btl_RunAiAndCommit 0x7ACEB0` is the only door. It runs the AI worker's entry for an
  **event kind** (0 choose action, 1 turn start, 2 targeted, 3 counter, 4 defeated, 5 enters field,
  6 turn ended, 7 action wrap up, 8 poison tick), **synchronously, to completion, inside the call**,
  then commits what the script staged. [V]
- The script stages its action with **Btl syscall 11** (and 90, 279), which is
  `FFX_BtlAi_AddCommandEntry 0x7AC9C0`, into the single 72 byte `g_ffxBtlAiStagedCmd 0x1136A78`.
  For a normal turn that record is copied over the placeholder that `FFX_Btl_SendMenu` already put
  in the command queue, so **a monster action is executed by exactly the code that executes a player
  action**. [V]
- **AI RNG is two global streams.** `core:169` draws stream 2 (the shared event script stream),
  and the target pick syscalls `btl:16` / `btl:37` draw stream 4, once, only when more than one
  candidate survives the filter. No AI syscall touches the per-unit streams. [V] Static census:
  383 stream 2 sites and 909 stream 4 sites in the AI workers. [D]
- 24 `core:169` sites sit in init entries, so some monsters draw stream 2 **at battle load**, inside
  the last monster file completion, before the monster group and CTB draws. [V] [D]

---

## 1. Where the AI lives in mNNN.bin

The file header is `u32 count` (8 in every file) then `count` u32 offsets from the file start. The
loaded image is `actor+0x48`. `FFX_Btl_BindMonsterArchiveSections 0x783B00`, called from
`FFX_Btl_OnMonsterFileLoaded 0x783990` for a freshly read file and straight from
`FFX_Btl_LoadMonsterData 0x783680` for a slot that reuses an earlier slot's file, binds them: [V]

| section | hdr | bound to | what | status |
|---|---|---|---|---|
| 0 | +0x04 | `+0xF78` | **ATEL script block**, the AI and the monster's other scripts | [V] [D] |
| 1 | +0x08 | `+0xF7C` | **event map**, section 1.2 below. Its byte 0 is copied to `+0xDF6` | [V] [D] |
| 2 | +0x0C | `+0xF80` | parameter record (stats, command list, capture species) | [V], BATTLE_START_AND_END 2.5 |
| 3 | +0x10 | - | zero in all 361 files | [D] |
| 4 | +0x14 | `+0xF88` via a copy | the 280 byte reward record | [V], BATTLE_START_AND_END 4 |
| 5 | +0x18 | - | handed to `FFX_Btl_QueueSoundSections` when `+0x11` is set and count > 6 | [V] |
| 6 | +0x1C | `+0xF84` | not traced | [V] binding only |
| 7 | +0x20 | - | not read by the binder | [V] |

Bind also writes `+0xDF5 = dword_112CA80++`, the order in which the slots were bound. Because
`FFX_Btl_LoadMonsterData` walks slots 0..7 and reads one file at a time, that is slot order over the
slots that have a file. [V]

### 1.1 Section 0, the ATEL block

Byte for byte the `.ebp` ATEL block of `EBP_FORMAT.md`, without the `EV01` wrapper. `tools/ebp.py`'s
`EbpFile._parse_atel` parses it unchanged once `at` is set to the section offset, and the linear sweep
lands on the computed code end in every file. [D] Differences from the field files: [D]

- **All 898 ATEL actors are type 2** and all are "large" (`+0x36 == +0x34`). None is a type 0
  subroutine, and no `callactor`, `runscript*`, `runparty*` or `wait*` is reachable from any AI
  event entry. An AI event is one actor running alone.
- **15 files have 0 actors**, so no script: m000 and 14 others (`mon_ai.py show`). Their "code"
  region holds the name and a build date string (m348: `"348"`, `"01/28 13:33:05"`).
- `atel+0x08` / `+0x0C` are a short name and a build timestamp, like the field files.
- `atel+0x00` is 0 in 15 files, unlike the field files where it is never 0.

Entry 0 of each actor is its **init**, entry 1 is a `core:95` WaitForever park (the field
convention, EBP_FORMAT.md). Typical init: a run of `btl:171` / `btl:178` / `btl:24` writes that set
the unit's battle properties and layout floats. [D]

### 1.2 Section 1, the event map

Read by `FFX_BtlEv_LookupEventEntry 0x797410` [`FFX_BtlEv_LookupEventEntry`]. Layout from the decompile, confirmed
against all files: [V] [D]

```
+0       u8   worker count, == the ATEL actor count in all 346 scripted files, copied to +0xDF6
+1       u8   n, slot count (138 in all 361 files)
+2       u8[n] slot -> record index, 0xFF = slot not handled. Padded to even length
+2+2*((n+1)/2)   record[]: { u8 worker, u8 class, s16 listOffset }, 4 bytes each
list     u16 count, u16 entry[count]   indexed by event kind, 0xFFFF = kind not handled
```

`FFX_BtlEv_LookupEventEntry(map, slot, &kind)` returns the record pointer and replaces `kind` with the entry index,
or -1. The worker byte is the ATEL actor inside this monster's block. [V]

The record's byte +1 is not read by any of the lookup functions. In the data it is a function of the
slot: 0 for slot 0 (218 records), 1 for slots 4 and 64 (348), 2 for slot 61 (332). [D] Call it a
class tag, meaning [I].

Slots in use across all files: 61 (332 files), 0 (218), 64 (195), 4 (153). [D] What they are:

| slot | fired by | event kinds | |
|---|---|---|---|
| **61** | `FFX_Btl_RunAiAndCommit` | 0..8, section 4 | the AI [V] |
| 0 | `FFX_BtlEv_FireEvent` [`FFX_BtlEv_FireBattleEvent`] slot 0, kinds such as 24 (battle start), 26, 28, 70 | the monster can override a battle field script event | [V] mechanism, meaning of each kind [I] |
| 4, 64 | bound to the unit by `FFX_BtlEv_BindWorkersToUnits`, fired through `FFX_BtlEv_FireEvent` / `FFX_BtlEv_FireAbilityEvent` | e.g. 39 | motion and effect hooks [I] |
| 65, 68, 71, 74 | `FFX_BtlEv_FireAbilityEvent` (slot = 65/68/71/74 by the top nibble 2/3/4/6 of an id) | | none used by any monster [D] |

List lengths are fixed per slot in all files: slot 61 has 21 (kinds 0..20), slot 0 has 141, slots
4 and 64 have 81. [D] Only kinds 0..8 are ever
fired at a monster (9..12 are the aeon and Yojimbo calls of section 4). Kinds handled across the 332
AI slots: 0 by 330, 1 by 258, 2 by 276, 3 by 292, 4 by 291, 5 by 29, 6 by 18, 7 by 3, 8 by 5. [D]

---

## 2. The binding, from file to running ATEL actor

In the last monster file's completion, `FFX_Btl_RegisterMonsterMotions 0x784070` runs: [V]

```
FFX_Btl_LoadEnemyUnitParams()                   (FFX_Btl_LoadEnemyUnitParams) params, rewards
for slot 0..7 with +0x48:  FFX_BtlEv_AddScriptBlock(1, "m%03d", +0xF78)   queue the ATEL block
if any: dword_112C9DB.b0 = 1, FFX_BtlEv_BuildScriptContext(1)      build ATEL context 3, boot it
sub_7842F0()                                    monster group draws, streams 28..35
FFX_Btl_InitCtbCounters(advantage)
FFX_Btl_ClampUnitHpMp()
```

`FFX_BtlEv_AddScriptBlock 0x797A40` [`FFX_BtlEv_AddScriptBlock`] appends a block pointer to group 1's list
(`dword_1134484 + 84*group`, up to 8). A slot that shares a file with an earlier slot still gets its
own entry, so two Sahagins get two sets of workers over one block. [V]

`FFX_BtlEv_BuildScriptContext 0x798390` [`FFX_BtlEv_BuildScriptContext`] maps the group to an ATEL context and a step
callback: group 0 -> context 2 (`FFX_BtlEv_StepBattleContext`), **group 1 -> context 3 (`FFX_BtlEv_StepMonsterContext`)**, group 2 ->
context 4 (`FFX_BtlEv_StepContext4`). It sizes the actor pool with `FFX_Atel_CalcActorPoolSize` per block,
allocates, and calls `FFX_Atel_SetupMultiBlockContext 0x8756D0` [`FFX_Atel_SetupMultiBlockContext`]: [V]

1. `FFX_Atel_ResetContext(3, ..)`, which installs `FFX_Atel_BootAllActors` at `ctx+80`. Then
   `ctx+76 = FFX_BtlEv_StepMonsterContext` (the step callback) and `ctx+504 = block list`. [V]
2. Creates the ATEL actors block by block, large actors first then the rest. All monster actors are
   large [D], so the global ATEL actor number is "earlier blocks' actor counts + local index". [V]
3. `FFX_BtlEv_BindWorkersToUnits 0x7976A0` [`FFX_BtlEv_BindWorkersToUnits`] for context 3: for each unit 20..27 and
   for slots 61, 4 and 64, find the worker and write **`atelActor+0xC0 = unit index`**. [V]
4. `FFX_Atel_CallContextBootHook` -> `FFX_Atel_BootAllActors 0x8683E0`, which runs every actor's
   entry 0 (init) to completion, in global actor order, then parks it. [V]

So the init entries run **inside the last monster file completion**, which BATTLE_START_AND_END.md
places in `FFX_AsyncLoad_DispatchCompletions` at the head of `FFX_MainStep`, before
`sub_7842F0`. [V] for the order inside `FFX_Btl_RegisterMonsterMotions`.

**Finding a worker later.** `FFX_BtlEv_FindSlotRecord 0x798590` [`FFX_BtlEv_FindSlotRecord`], case context 3,
needs `dword_112C9DB.b0`, the unit to be <= 30 and `+0x48` set. It looks the slot up in the unit's
`+0xF7C` map, and returns the record plus a base = the sum of `+0xDF6` over units 0..unit-1. The
global ATEL actor is `base + record.worker`. [V] Party units never have `+0xDF6` written, so they add
0 [I] (no writer other than the binder).

**Finding self from a syscall.** Every Btl handler starts with
`*(u8*)FFX_Atel_GetActorBattleUnitByte(atelActor)`, which is `atelActor + 0xC0`. The current IDB
comment on `FFX_Atel_GetActorBattleUnitByte 0x86A990` says its argument is a battle unit. It is the
**ATEL actor**, and `+0xC0` is the bound unit index (255 = none). [V] (`FFX_Atel_GetActorBattleUnitById` is the same thing
by actor id, and is what `FFX_BtlEv_BindWorkersToUnits` writes through.)

---

## 3. The VM

ATEL, unchanged. `FFX_Atel_RunScript 0x8641E0`, the same 123 opcodes, the same syscall libraries
through `g_ffxAtelSysFuncLibs`. Context 3 is slot 3 of `g_ffxAtelCtxArray 0x1325BA0` (568 bytes
each). [V] The battle uses three contexts: [V]

| ctx | blocks | step callback | holds |
|---|---|---|---|
| 2 | `dword_112A994[0]`, `[8]`, two blocks | `FFX_BtlEv_StepBattleContext` | the battle field scripts, slot `unit+41` party AI, `unit+109` per unit event hooks [I] for the exact split |
| **3** | one per monster slot | `FFX_BtlEv_StepMonsterContext` | **monster scripts** |
| 4 | `dword_1135DF8` | `FFX_BtlEv_StepContext4` | one more block, not traced |

The AI's syscall usage is almost all library 7 (Btl). Library 0 is used for `core:0` WaitFrames,
`core:95` WaitForever and `core:169` the random. Library 6 (camera) and 8 (map effects) appear only
in non-AI workers, with one exception (`8:0` reachable from kind 4 in 30 sites). [D]

---

## 4. Scheduling

### 4.1 The door: FFX_Btl_RunAiAndCommit 0x7ACEB0

```c
int FFX_Btl_RunAiAndCommit(u8 unit, int issuer, int kind, int aeon, u32 mode, u8 *fill)
{
    if (issuer != 255) actor(unit)[0xDED] = issuer;        // "who acted on me", selector -17
    g_ffxBtlAiScriptActorCtx = mode;                        // 0x1136A68, -1 when idle
    FFX_BtlAi_ResetStagedCommand(unit);                     // 0x7ACB00, clear 72 bytes, +0 = unit
    if (FFX_Btl_IsEnemySlot(unit)) {
        FFX_BtlEv_ClearRequests();                                       // clear request list
        FFX_BtlEv_PushRequest(3, unit, 61, kind);                      // request (ctx 3, unit, slot 61, kind)
        r = FFX_BtlEv_StartFirstMatch(3, unit, 61, 1, 0, 0);               // start the thread and run it
        if (r >= 0) FFX_BtlEv_RunWorkerToCompletion(3, unit, 61);                // run until it ends
    } else { ...context 2, slot unit+41, or 37..40 for aeon units 14..17... }
    if (staged.count != 0 && mode <= 1) {
        if (fill)          { byte_1136A7C = fill[4]; memcpy(fill, staged, 72); }
        else if (mode)     { if (issuer == 255 || noReorder || !actor(issuer)
                                 || (!actor(issuer)[0xDF1] && FFX_Btl_CmdQueue_IsVariantAbsent(unit,1)))
                                 FFX_Btl_CommitCommandPriority(staged, g_ffxBtlAiStagedPriority, mode, issuer); }
                             // FindByVariant 0x7B0720 returns 1 when NOT found, 0 when found
        else                 FFX_Btl_CommitCommand(staged, 0, 0);
    }
    if (kind <= 8) FFX_BtlEv_NotifyUnitEvent(unit, kind, 1, 0, 0);         // tell context 2 slot unit+109
    g_ffxBtlAiScriptActorCtx = -1;
}
```
[V]. Mode 2 never commits, so on those kinds the script runs for its side effects only. A mode 1
counter is only committed when the issuer is gone, `noReorder` is set, or the issuer's `+0xDF1` is
clear and this unit has **no** variant 1 entry queued yet, so one counter per unit at a time. [V]
(`FFX_Btl_CmdQueue_IsVariantAbsent 0x7B0720` returns 1 for "not found", the inverse of what its name
suggests.)

Correction 2026-10-29 (the 13.8 port, Project Zanarkand src/ffx/btl/MonsterAi.cpp). The function
returns `r`, what `FFX_BtlEv_StartFirstMatch` gave: 0 when a context 2 entry started, 1 for context 3,
2 for context 4, -1 when no request had an entry. So `FFX_BtlAi_FireTargetedEvent 0x7AD0C0`, which
counts the calls that return 0, counts party (context 2) matches only and never a monster's. And the
"issuer is gone" test never passes, because `FFX_Battle_GetActor` hands out the spare record at
0x11334D8 rather than null. [V]

**The request list.** `FFX_BtlEv_PushRequest 0x797B70` [`FFX_BtlEv_PushRequest`] appends `{ctx, unit, slot,
kind}` to a 16 entry list at `word_1135D70`, `FFX_BtlEv_ClearRequests` [`FFX_BtlEv_ClearRequests`] empties it.
`FFX_BtlEv_StartFirstMatch 0x797D50` [`FFX_BtlEv_StartFirstMatch`] walks the list in order and takes the first
request whose map row has an entry for that kind. That is how a battle event can be answered by the
monster's own script before the battle field script (`FFX_BtlEv_FireEvent` pushes ctx 2 set 1, then ctx 3 for
an enemy, then ctx 2 set 0). For the AI there is exactly one request. [V]

It then builds a thread record `{0xFFFF, worker, 0, level 7, entry, ctx, +0xDF5, localWorker}` and
`FFX_Atel_CreateThread 0x86EA00` queues it on the worker. With `flags & 3 == 1` it runs at once
(`sub_8653F0`, `sub_8641C0` -> `FFX_Atel_RunScript`). [V]

**Synchronous to completion.** `FFX_BtlEv_RunWorkerToCompletion 0x7979D0` [`FFX_BtlEv_RunWorkerToCompletion`] loops
`RunScript` then `sub_8653F0` while the worker's current level (`atelActor+50`) is >= 7. The AI thread is
level 7, so the call returns only when the AI entry has ended. [V] A `core:0` WaitFrames inside an
event entry does not cost real frames: its poll decrements once per `RunScript`, so it finishes
inside the loop. [V] for the poll, [I] that nothing else in the loop waits on the step clock.
Sinspawn Ammes's kind 3 has such a loop with `WaitFrames(1)` per pass. [D]

### 4.2 The event kinds

| kind | name | fired from | mode | issuer |
|---|---|---|---|---|
| **0** | **choose action** | `FFX_Btl_ExecCommand 0x792210`, when the head record has entry count 0 and variant 0 | fill | 255 |
| 1 | turn start | `FFX_Btl_SendMenu 0x792A90`, once per turn (`+0xDF7`) | 2 | 255 |
| 2 | targeted | `FFX_BtlAi_FireTargetedEvent` [`FFX_BtlAi_FireTargetedEvent`] from `FFX_Btl_ExecCommand`, every unit in the target mask without a variant 1 entry queued, before `FFX_Btl_ResolveTargets` | 1 | acting unit |
| 3 | counter | `FFX_Btl_ApplyPendingEffects 0x78F060` (mode 1), and `FFX_Btl_ExecCommand` for a variant != 0 record with count 0 (fill) | 1 / fill | attacker / 255 |
| 4 | defeated | `FFX_Btl_OnUnitDefeated 0x78C740` | 1 | killer |
| 5 | enters field | `FFX_Btl_OnUnitChrLoaded` and `FFX_Btl_OnUnitChrReloaded`, the CHR load completions pumped by `FFX_Btl_PumpChrLoadQueue` from `FFX_Btl_StepSubPhase` | 2 | 255 |
| 6 | turn ended | `FFX_Btl_TurnQueue_RemoveAt 0x7B20E0` | 0 | 255 |
| 7 | action wrap up | `sub_787D70`, `sub_788480`, `FFX_Btl_StartCommandExec 0x7AE370` | 2 | 255 |
| 8 | poison tick | `FFX_Btl_ApplyPoisonDamage 0x7AFAB0` | 1 | self |
| 9..12 | aeon specials | `FFX_Btl_AeonSpecialCommandHook`, `sub_7B2AD0`, party side only | | |

[V] for every call site and argument. The names of 5 and 7 are [I] from their callers.

### 4.3 One monster turn

All inside one `FFX_Btl_MainStep`, and so inside one `FFX_MainStep` sub step: [V]

1. `FFX_Btl_CtbTick` pushes the monster's turn.
2. `FFX_Btl_SendMenu`: status tick, then **kind 1** (once, `+0xDF7`). Mode 2, nothing commits. Then
   the forced action checks (doom, sleep, provoke, berserk, confusion). If none fire,
   `FFX_Btl_IsActorAiControlled` (`+0xDF3`, set on units 20..27) sends it to the AI branch, which
   commits a **placeholder** record with entry count 0. (BATTLE_CTB_AND_STATUS 1.5.)
3. `FFX_Btl_ExecCommand` sees the head with count 0, sets `+0xDF4 = 1`, and calls
   `FFX_Btl_RunAiAndCommit(unit, 255, 0, 1, 0, head)`: **kind 0**, fill. The staged entries are
   copied over the head record in place. Skipped while `+0x608` (sleep) is non zero.
4. The record continues down the player path: cost check, **kind 2** for every unit in the target
   mask (`FFX_BtlAi_FireTargetedEvent`, mode 1, only units that have no variant 1 entry queued yet), then
   `FFX_Btl_ResolveTargets`, and the action.
5. Hits fire **kind 3** on the targets (`FFX_Btl_ApplyPendingEffects`, mode 1, issuer = attacker),
   which is how counters get queued with `FFX_Btl_CommitCommandPriority`. A kill fires **kind 4**.
6. Retiring the turn fires **kind 6** with mode 0, the one kind that commits straight through
   `FFX_Btl_CommitCommand`.

If the kind 0 entry stages nothing, the head keeps count 0 and the executor skips the action. 44 of
the 330 kind 0 entries can never reach an AddCommand (`mon_ai.py --selftest` lists them): counter
only monsters, parts and scripted bosses. [D] What the executor then does with a count 0 head is [I]
(the `v23 == 0 && count != 0` gate in `FFX_Btl_ExecCommand` skips the action).

### 4.4 The per step tick

Context 3 is also stepped every sub step: `FFX_MainStep` calls `FFX_Btl_MainStep` then
`FFX_Atel_StepOnce` -> `FFX_Atel_StepFieldAll` -> `FFX_Atel_StepFieldContexts` ->
`FFX_Atel_StepContextRange(0, 6)` -> `ctx+76` = `FFX_BtlEv_StepMonsterContext`, which runs every monster ATEL actor
once. [V] That is where the non-AI threads (motion hooks of slots 4 and 64, threads started by
`btl:63`) advance. The AI threads themselves never survive a `RunAiAndCommit` call. [V]

---

## 5. How the script picks a command and a target

### 5.1 Staging

| syscall | handler | does |
|---|---|---|
| `btl:11` | `FFX_AtelSys_Btl_011_resi 0x7A44B0` | pops ability (top) then target selector, resolves the selector, `FFX_BtlAi_AddCommandEntry(unit, ability, mask, 0, -1)`. Refuses when `sub_7B24A0(actor, 0, !+0x5CB, 1)` is 0 or `g_ffxBtlDbgMonInput` is set |
| `btl:90` | `FFX_AtelSys_Btl_090_resi 0x7A49F0` | same, noReorder = 1, no gate |
| `btl:279` | `FFX_AtelSys_Btl_279_resi 0x7A4B00` | same, noReorder = 1, priority 64 |

[V]. `FFX_BtlAi_AddCommandEntry 0x7AC9C0` (BATTLE_COMMAND.md 1) refuses unless
`g_ffxBtlAiScriptActorCtx != -1`, the unit matches `staged+0`, and fewer than 4 entries are staged.
It writes `{ability, 0}` to `staged + 8 + 16*n`, resolves the ability, filters the mask to present
live units (or KO units for abilities with flag `0x40` at record +26), stores the mask, bumps the
count and calls `FFX_BtlAi_SetLastPickMask` to remember the mask in `+0xDC0`. [V]

Usage: 1615 `btl:11`, 68 `btl:90`, 20 `btl:279` across all files. [D]

### 5.2 Target selectors

A selector is an int popped off the ATEL stack and turned into a 31 bit unit mask by `FFX_BtlAi_ResolveTargetSelector
0x794330` [`FFX_BtlAi_ResolveTargetSelector`]. "Present" is `+0xDC8 != 0 && +0xDCD == 0`. [V]

| value | mask |
|---|---|
| 0..30 | `1 << value` if `FFX_Btl_IsAllyUnit` (see the correction below, any unit <= 30) |
| 0x1000..0x1FFF | present units whose type id `+0x0E` equals the value (Geneaux uses `0x1067`, his Tentacle) |
| -2 | the acting unit, while `+0xDF0` |
| -3 | the acting unit's current command targets, present |
| -4 | `g_ffxBtlCurTargetMask`, present |
| -5 | every present unit |
| -6..-12 | party order slot 0..6, the byte at `0x112C88F - value`, which is `g_ffxBattlePartyOrder 0x112C895` + slot |
| -13 | self |
| -14 | present non-enemy units, the usual "party" |
| -15 | present enemy units |
| -16 | self `+0xDC0`, the last mask stored by a pick or an AddCommand |
| -17 | `1 << self+0xDED`, the issuer of the current event |
| -18 | `g_ffxBtlActingActor` |
| -19 | present party, not summons |
| -20 | present summons |
| -21 | all 31 units |
| -22 | `1 << self+0x14`, the first slot of this monster type (written by `FFX_Btl_LoadMonsterData` for a shared file) [I] for the meaning |
| -23 | every non-enemy unit |
| -24 | every non-enemy non-summon unit |
| -25 | present party minus two states (`sub_795780` KO `+0xDCC`, `sub_795800` `+0xDCE`) |
| -26 | self `+0xDC4` |

Correction 2026-10-29. `FFX_Btl_IsAllyUnit 0x793650` is `unit <= 30`, true for the monsters 20..27
as well, so row 0..30 takes any unit (tools/apply_monster_ai_names.py renames it
`FFX_Btl_IsValidUnitIndex`). "The acting unit" of -2 and -3 is not the -18 byte: -2 and -3 read
`byte ptr g_ffxBtlActingActor+1`, 0x112C9E0, the unit `FFX_Btl_ExecCommand 0x7924E7` runs, and give 0
unless that unit's `+0xDF0` is set. -18 reads 0x112C9DF, which `FFX_Btl_SendMenu` also writes. -3 is
`entry[cursor].targetMask` of `FFX_Btl_CmdQueue_GetActorCmdOrLast` for that unit, present. [V]

### 5.3 Properties and target picks

`btl:15 GetProperty(selector, prop)` and `btl:170 GetSelfProperty(prop)` call `FFX_BtlAi_GetProperty
0x7B2DC0` [`FFX_BtlAi_GetProperty`], a 346 case switch over the units in the mask. It sums the
value, and for the counted properties returns the average. Ids pinned: 0 HP `+0x5D0`, 1 MP
`+0x5D4`, 2 max HP `+0x594`, 3 max MP `+0x598`, 4 alive (count of units with `+0xDCC == 0`), 82
`+0xDD9`, 342 the capture result `+0xDD1` (MONSTER_ARENA.md). [V] `btl:24 SetProperty(selector,
prop, value)` and `btl:171 SetSelfProperty(prop, value)` go to `FFX_BtlAi_SetProperty` [`FFX_BtlAi_SetProperty`].
[V]

The pick: `FFX_BtlAi_FilterAndPickTarget 0x7ACD40` [`FFX_BtlAi_FilterAndPickTarget`]

```c
int pick(unit, sel, prop, unused, stream, mode, selIsMask, int *out)
{
    mask = selIsMask ? sel : FFX_BtlAi_ResolveTargetSelector(unit, sel);
    for each unit u in mask:
        ok = GetProperty(unit, prop, u, &val) != 0;
        if (mode & 0x80) ok = !ok;
        if (!ok) continue;
        switch (mode & 0x7F) {       // 0 any, 1 keep the max of val, 2 keep the min, ties kept
        ...
        }
    if (count > 1 && out) *out = nth set bit of keep, n = FFX_Rand_Stream(stream) % count;
    else if (out) *out = the last kept unit;
    return keep;
}
```
[V]. `unused` really is not read.

| syscall | handler | does |
|---|---|---|
| `btl:37` | `0x7A5CF0` | `pick(self, sel, prop, x, 4, mode, 0, &u)`, `+0xDC0 = keep`, returns `u` |
| `btl:16` | `0x7A5AF0` | the same after two fixed prefilters: `pick(sel, prop 4)` (alive) then `pick(.., prop 82)`, both with no `out`, so no draw. Returns `u` |

[V]. The usual idiom is `btl:16(-14, 4, 0, 0)` "a random live party member", then
`btl:16(-16, 0, 0, 2)` "of those, the lowest HP", with the result pushed as the target of
`btl:11`. [D]

### 5.4 To the shared command path

Kind 0 fills the placeholder in place, so the record that executes is the same 72 byte record in the
same queue slot that a player's confirm would have filled. `FFX_Btl_ExecCommand` then runs the same
cost check, kind 2 events, `FFX_Btl_ResolveTargets`, rank and delay, as for a party unit.
BATTLE_COMMAND.md section 4 has the player side. [V]

---

## 6. RNG

### 6.1 Every draw an AI event can make

| site | stream | draws | when |
|---|---|---|---|
| `core:169` `FFX_AtelSys_Core_169_resi 0x857680` | **2** | 1 | every call, returns `(u16)draw` |
| `core:166` `0x857400` | 2 | 1 | every call, `(n * (u16)draw) >> 16`. **No monster uses it** [D] |
| `btl:16`, `btl:37` via `FFX_BtlAi_FilterAndPickTarget` | **4** | 0 or 1 | only when more than one unit survives the filter, ties included. The prefilters of `btl:16` never draw |
| `came:128` `FFX_AtelSys_Came_128_resi 0x7B89A0` | 14 / 15 | 1 | in 148 sites, **none in an AI worker** [D] |

[V] for the handlers, [D] for the usage. Call graph check: every handler slot of libraries 0 (616
rows) and 7 (296 rows) was walked five direct calls deep, and every library 4, 6 and 8 function a
monster script calls six deep, for `FFX_Rand_Stream`, `FFX_Btl_Rand`, `rand` and
`FFX_Btl_RandPercentCheck`. Indirect calls are not followed. Besides the rows above, the hits a
monster script can reach were paths that fire **other game code** as a side effect: [V]

- `btl:24` / `btl:171` SetProperty -> `FFX_BtlAi_SetProperty` -> `FFX_Btl_OnUnitDefeated` when a write kills
  the unit. That pays the kill (streams 10..13, BATTLE_START_AND_END 4) and fires kind 4 on the dead
  unit, recursively.
- `btl:158` -> `FFX_Btl_OnMotionEvent` -> `sub_78D980` -> `FFX_Btl_ApplyPendingEffects`, the hit
  being applied (attacker streams, BATTLE_CTB_AND_STATUS 3.2 rows 7..11).
- `btl:80` / `btl:131` reach `FFX_Btl_DamageFormula` or `sub_7B7170` with the random flag off, so
  they do **not** draw.

No AI syscall uses the per-unit streams 20..67, and `FFX_Btl_RunAiAndCommit`, the request list, the
commit and `FFX_BtlAi_AddCommandEntry` draw nothing. [V]

### 6.2 Order

Draws happen in script order, inside the `FFX_Btl_RunAiAndCommit` call, which sits where section
4.2 puts the kind. For a normal monster turn that is: kind 1 draws (in `FFX_Btl_SendMenu`), then
kind 0 draws (in `FFX_Btl_ExecCommand`, same step), then kind 2 draws for every target in mask
order (units 0..30), then the action's own damage draws, then kind 3 draws per hit target, then
kind 6 draws at retirement. [V] for the call order of 1, 0 and 2 inside `FFX_Btl_ExecCommand`,
the later ones follow the effect pipeline [I] for their exact interleave with the damage draws.

At battle load, init entries draw stream 2 in global ATEL actor order (slot order), inside
`FFX_Btl_RegisterMonsterMotions`, **before** `sub_7842F0`'s stream 28..35 draws. 24 static
`core:169` sites are in init entries (m050, m087, m088, m121, m211 and others). [V] [D]

### 6.3 What it means for lockstep

- Stream 2 is **shared with every event script** (field, Blitzball, minigames, the battle field
  scripts of context 2). An AI decision that rolls `core:169` consumes the same stream a cutscene
  would. [V]
- Stream 4 is the **battle target pick stream**. It is also drawn by `sub_7996D0` (berserk and
  confusion target picks from `sub_799AC0`, and `sub_7B0520`), by `sub_799B40` for party units, and
  by `btl:231`, `btl:249`, `btl:251` (not used by monsters). [V]
- The draw count depends on hidden state: how many targets are alive, HP ties. Matching the
  stream means running the same script, not replaying the chosen command. [V]
- Because kind 0 only fills the record, replicating the **filled record** (BATTLE_SYNC.md) would
  also work, but then the peer must skip its own kind 0 run or its streams 2 and 4 advance on their
  own. [I]

---

## 7. Variables

AI scripts keep state in ATEL variables (Geneaux's pattern counter is `var 3`, Ammes's trigger is
`var 0`). Storage classes are those of `FFX_Atel_ResolveVarAddress` (EBP_FORMAT.md). When two slots
share one file image the class 2 and class 6 storage, which lives inside the block, would be shared
between them, while `sub_870870` hands each ATEL actor its own slice of the context's private
allocation. Which class the AI variables use is not checked. Open.

Settled 2026-10-29. Every monster block sits in context 3, a multi-block context, so class 3
resolves into the actor's own slice of the private allocation, not into the shared image. Over the
AI worker entries of the 332 shipped files with a slot 61 (`mon_ai.reachable` from every slot 61
kind), the variable references are 5883 class 3 (297 files), 709 class 1 (96 files) and 71 class 0
(6 files), with no class 2 or 6 at all. So two monsters of one type keep separate AI state, and what
they share is class 1 (the battle work) and class 0 (the save block script work), which every
monster shares anyway. The 13.8 port runs one block twice for units 20 and 21 and checks the two
counters apart (BtlMonsterAiRunsASyntheticScriptAndCommits). [V] for the resolver, [D] for the
census

---

## 8. The reader, tools/mon_ai.py

`E:\Git\projectzanarkand\tools\mon_ai.py`. Stdlib only, `python -I` safe, read only. It opens the
archive through the co-op `vbf.py` (via `btl_rewards.Install`, which resolves `battle/mon` through
`cdrom.fnd` / `cdrom.fid` kind 16) and parses the ATEL block with the co-op `ebp.py`, reusing its
parser, opcode table and syscall labels. Monster and ability names come from
`new_uspc/battle/kernel/monster1..3.bin`, `command.bin`, `monmagic1/2.bin`, read on demand.

```
python -I tools/mon_ai.py list                         one line per monster
python -I tools/mon_ai.py show 41 159                  sections, ATEL actors, the whole event map
python -I tools/mon_ai.py dis 41 [--worker W | --slot S | --all]
python -I tools/mon_ai.py rng 1 102 [--indirect]       RNG sites reachable from each AI kind
python -I tools/mon_ai.py --selftest
```

`dis` labels entry points with the (slot, kind) pairs that reach them, names AI syscalls, flags
RNG draws with their stream, names the ability of an AddCommand, and the selectors and properties it
recognises. `rng` follows the jump labels from each kind's entry.

### 8.1 Selftest

```
monster ids in the cd index (kind 16): 361, distinct files 361
section count per file: {8: 361}
files with a nonzero section offset, by section: 0:361, 1:361, 2:361, 4:361, 5:361, 6:361, 7:361
files with an ATEL script: 346, empty (0 actors): 15
ATEL actors by type: {2: 898}
instructions swept: 190020, entry and label targets on a boundary: 20546, jumps checked: 17173
files with an AI slot 61: 332
  kind 0 entry can reach an AddCommand: 286, cannot: 44
    cannot: m000 m106 m108 m116 m118 m133 m148 m150 m154 m161 m162 m170 m171 m172 m175 m183 m201 m202 m203 m204 m205 m206 m211 m248 m249 m250 m255 m257 m258 m260 m261 m262 m263 m264 m266 m267 m268 m269 m270 m271 m273 m274 m275 m278
  cross-actor opcodes reachable from an AI kind entry: none
AI kinds handled: 0:330, 1:258, 2:276, 3:292, 4:291, 5:29, 6:18, 7:3, 8:5
event map slots used: 0:218, 4:153, 61:332, 64:195
event map record byte +1 by slot: slot 0=0 x218, slot 4=1 x153, slot 61=2 x332, slot 64=1 x195
static RNG sites in AI workers: 0:169=383, 7:16=907, 7:37=2
AddCommand syscalls, all actors: 7:11=1615, 7:90=68, 7:279=20
RNG syscalls, all actors: 0:166=0, 0:169=396, 6:128=148, 7:16=907, 7:37=2
problems: 0
```

Checked per file: the linear sweep ends exactly on the code end, every entry and label lands on an
instruction boundary, every jump indexes its own actor's label table, the event map's worker count
equals the actor count, every record's worker and entry index are in range, and every list offset
is inside the section.

### 8.2 Worked example, a simple early monster: Sahagin (m041, Baaj)

Two ATEL actors. Slot 61 -> actor 0, kinds 0..4 -> entries 2, 4, 5, 3, 6. Kinds 1..4 are a bare
`endscript`. Kind 0:

```
entry_0_2:   ; slot 61 kind 0 (choose action)
  0000DA  aef2ff    pushimm        -14   ; target present non-enemy units
  0000DD  ae0400    pushimm        4
  0000E0  ae0000    pushimm        0
  0000E3  ae0000    pushimm        0
  0000E6  b51070    syscallf       btl:16  AiPickTargetFiltered(selector, prop, unused, mode) -> unit   ; RNG stream 4 x<=1
  0000E9  a00300    storevar       3
  0000EC  9f0300    pushvar        3
  0000EF  ae0040    pushimm        16384   ; ability 0x4000 Attack
  0000F2  d80b70    syscall        btl:11  AiAddCommand(target, ability)
  0000F5  3c        endscript
```

"Pick a live party member at random, Attack it." One stream 4 draw per turn when two or more party
members are alive, none when one is. The command is `0x4000`, monmagic1 row 0. [D]

### 8.3 Worked example, a boss: Sinspawn Geneaux (m102)

Slot 61 -> actor 1, kinds 0, 2, 3, 4. Kind 0 is a three step pattern held in `var 3`, switched on
with `setsel` / `pushsel` (EBP_FORMAT.md):

```
var 4 != 0 -> switch var 3:
  0: btl:16(-14, alive, 0, 0)  btl:16(-16, HP, 0, 1 = max)  -> AiAddCommand(u, 0x6005 Venom)   var 3 = 1
  1: btl:16(-14, alive, 0, 0)  btl:16(-16, HP, 0, 2 = min)  -> AiAddCommand(u, 0x3044 Water)   var 3 = 2
  2: AiAddCommand(-14, 0x6006 Staccato)                                                         var 3 = 0
var 4 == 0 -> if var 3 == 1: AiAddCommand(-14, 0x6004 Sigh),  var 3 = !var 3
```

`rng 102`:

```
m102 Sinspawn Geneaux
  kind 0 (choose action), entry 2: 4 direct RNG site(s)
    00020E  7:16     stream 4      draws <=1
    00021D  7:16     stream 4      draws <=1
    000241  7:16     stream 4      draws <=1
    000250  7:16     stream 4      draws <=1
  kind 2 (targeted), entry 4: 1 direct RNG site(s)
    000417  0:169    stream 2      draws 1
  kind 3 (counter), entry 3: 1 direct RNG site(s)
    00033B  7:16     stream 4      draws <=1
  kind 4 (defeated), entry 5: 0 direct RNG site(s)
```

So a Geneaux turn draws stream 4 up to twice on steps 0 and 1 (the second only on an HP tie), never
on step 2, and his kind 2 handler (being targeted) can roll stream 2, gated on the count of his
Tentacles (`btl:30(-15, 0x1067)`). [D]

The deterministic contrast is **Sinspawn Ammes (m159)**: kind 0 is `AiAddCommand(-14, 0x304E Demi)`
every turn, plus a `var 0` triggered property write. No RNG site in any AI kind. [D]

---

## 9. The AI syscall ABI

Arguments in push order (the last is the top of the stack). [V] from the handlers.

| syscall | name used by the reader | args |
|---|---|---|
| `core:169` | Rand16 | -> `(u16)FFX_Rand_Stream(2)` |
| `btl:11` / `90` / `279` | AiAddCommand / NoReorder / Prio64 | target selector, ability id |
| `btl:15` | GetProperty | selector, prop |
| `btl:16` | AiPickTargetFiltered | selector, prop, unused, mode -> unit |
| `btl:24` | SetProperty | selector, prop, value |
| `btl:30` | CountUnitsInBoth | selector A, selector B -> count |
| `btl:33` | GetUnitTypeId | selector -> `+0x0E` of the first unit, -1 none |
| `btl:37` | AiPickTarget | selector, prop, unused, mode -> unit |
| `btl:158` | MotionEventHit | selector, x, y (fires `FFX_Btl_OnMotionEvent`) |
| `btl:170` | GetSelfProperty | prop |
| `btl:171` | SetSelfProperty | prop, value |
| `btl:178` | SetSelfFloat | index 0..9, float -> `+0x4BC..+0x4E4`, 9 also the collision radius |

Also seen and decompiled but not used to pick commands: `btl:7` PlayMotion, `btl:8` WaitMotion
(poll), `btl:9` ground mode, `btl:10`, `btl:13`, `btl:18`, `btl:20` returns `dword_112C8D8`,
`btl:21` (debug print, Ammes pushes `777777` and friends), `btl:22`, `btl:63` starts a battle event
through `FFX_BtlEv_FireEvent` and waits on it, `btl:123`, `btl:161` unit -> party order code. [V]

---

## 10. IDB names

Applied and saved on 2026-10-13 after spot checks of the binding, the inverted queue lookup, the
actor argument of 0x86A990 and both RNG paths. `FFX_Btl_CmdQueue_FindByVariant 0x7B0720` became
`FFX_Btl_CmdQueue_IsVariantAbsent`, and 0x86A990's comment now says it takes an ATEL actor.

| VA | name | why |
|---|---|---|
| 0x797410 | `FFX_BtlEv_LookupEventEntry` | (map, slot, &kind) -> record, kind -> entry |
| 0x798590 | `FFX_BtlEv_FindSlotRecord` | (ctx, unit, slot, &workerBase) for contexts 2, 3, 4 |
| 0x797B70 | `FFX_BtlEv_PushRequest` | appends {ctx, unit, slot, kind} to the 16 entry list at 0x1135D70 |
| 0x797FF0 | `FFX_BtlEv_ClearRequests` | `dword_113456C = 0` |
| 0x797D50 | `FFX_BtlEv_StartFirstMatch` | first request with an entry wins, creates the level 7 thread, runs it |
| 0x7979D0 | `FFX_BtlEv_RunWorkerToCompletion` | loops RunScript while level >= 7 |
| 0x797A40 | `FFX_BtlEv_AddScriptBlock` | queues an ATEL block for a group |
| 0x798390 | `FFX_BtlEv_BuildScriptContext` | group 0/1/2 -> ATEL context 2/3/4 |
| 0x8756D0 | `FFX_Atel_SetupMultiBlockContext` | one context over several ATEL blocks |
| 0x7976A0 | `FFX_BtlEv_BindWorkersToUnits` | writes `atelActor+0xC0 = unit` for slots 61, 4, 64 |
| 0x7973B0 | `FFX_BtlEv_StepMonsterContext` | context 3 step callback |
| 0x7972E0 | `FFX_BtlEv_StepBattleContext` | context 2 step callback |
| 0x797350 | `FFX_BtlEv_StepContext4` | context 4 step callback |
| 0x797BC0 | `FFX_BtlEv_FireEvent` | slot, unit, kind, kind2, the battle event fire |
| 0x798000 | `FFX_BtlEv_FireAbilityEvent` | id class 2/3/4/6 -> slot 65/68/71/74 |
| 0x7A07B0 | `FFX_BtlEv_NotifyUnitEvent` | context 2 slot `unit+109` after an AI event |
| 0x7AD0C0 | `FFX_BtlAi_FireTargetedEvent` | kind 2 on every unit of a mask |
| 0x794330 | `FFX_BtlAi_ResolveTargetSelector` | selector -> unit mask |
| 0x7ACD40 | `FFX_BtlAi_FilterAndPickTarget` | the stream 4 pick |
| 0x796430 | `FFX_BtlAi_SetLastPickMask` | `+0xDC0 = mask` |
| 0x7B2DC0 | `FFX_BtlAi_GetProperty` | the property getter, MONSTER_ARENA.md already calls it that in prose |
| 0x7B4B70 | `FFX_BtlAi_SetProperty` | btl:24 / 171 |
| 0x7B4A90 | `FFX_BtlAi_SetUnitFloat` | btl:178 |
| 0x86A990 | `FFX_Atel_GetActorBattleUnitByte` | arg is an ATEL actor, `+0xC0` is the bound unit. comment fixed |
| 0x86A970 | `FFX_Atel_GetActorBattleUnitById` | the same by ATEL actor id |
| 0x7AD190 | `FFX_Btl_PumpChrLoadQueue` | drives the battle CHR loads, fires kind 5 |
| 0x7839B0 | `FFX_Btl_OnUnitChrLoaded` | kind 5 |
| 0x783BC0 | `FFX_Btl_OnUnitChrReloaded` | kind 5, the respawn and swap form [I] |
| 0x7A44B0 | `FFX_AtelSys_Btl_011_AiAddCommand_resi` | keeps the positional prefix |
| 0x7A49F0 | `FFX_AtelSys_Btl_090_AiAddCommandNoReorder_resi` | |
| 0x7A4B00 | `FFX_AtelSys_Btl_279_AiAddCommandPrio64_resi` | |
| 0x7A5AF0 | `FFX_AtelSys_Btl_016_AiPickLiveTarget_resi` | |
| 0x7A5CF0 | `FFX_AtelSys_Btl_037_AiPickTarget_resi` | |
| 0x7A50C0 | `FFX_AtelSys_Btl_024_SetProperty_resi` | |
| 0x7A5360 | `FFX_AtelSys_Btl_171_SetSelfProperty_resi` | |
| 0x7A4D50 | `FFX_AtelSys_Btl_015_GetProperty_resi` | |
| 0x7A4EA0 | `FFX_AtelSys_Btl_170_GetSelfProperty_resi` | |
| 0x1135D70 | `g_ffxBtlEvRequests` | 16 x {ctx, unit, slot, kind} |
| 0x113456C | `g_ffxBtlEvRequestCount` | |
| 0x112CA80 | `g_ffxBtlMonsterBindCounter` | source of `+0xDF5` |

Still proposed, the meaning is inferred:

| VA | proposed | why |
|---|---|---|
| 0x797CD0 | `FFX_BtlEv_ResetWorker` | `sub_871730`, `sub_869350`, `sub_8653F0` on a slot's worker, no run [I] for the meaning |
| 0x797490 | `FFX_BtlEv_ClearWorkerRegs` | `sub_86D260` zeroes a slot worker's 4 int and 10 float registers |
| 0x7947F0 | `FFX_BtlAi_ResolveSelectorToUnit` | the single unit form, used by btl:33, 123, 158, 161. Read whole on 2026-10-29, the lowest set bit below 31 or -1, now a row of tools/apply_monster_ai_names.py [V] |

---

## 11. Open

- Event map record byte +1 (0, 1, 2 by slot class) has no reader in the lookup path. A data
  breakpoint on a map record would settle whether anything reads it.
- What a count 0 head does after kind 0 stages nothing. The executor's gate skips the action, but
  whether the turn is still retired normally (and kind 6 fired) was not traced.
- The exact interleave of kind 3 and kind 4 draws with the damage draws of the action that caused
  them, inside `FFX_Btl_ApplyPendingEffects`.
- (Settled 2026-10-29) which variable storage class AI scripts use: class 3, private per actor in
  context 3, so two same type monsters do not share AI state (section 7).
- Context 4's block (`dword_1135DF8`). (Settled 2026-10-29) the split of context 2 between its two
  blocks: set 0 system_01, set 1 the battle's field file (section 12).
- The meanings of slot 0 event kinds (26, 28, 70 ...) and of slots 4 and 64. Kind 24 is the battle
  start event (section 12).
- Whether field contexts 0 and 1 step during a battle. If they do, their stream 2 draws interleave
  with the AI's inside one `FFX_MainStep` sub step (AI first, then `FFX_Atel_StepOnce`).
- (Settled 2026-10-13) the inverted lookup at 0x7B0720 is now named `FFX_Btl_CmdQueue_IsVariantAbsent`.

---

## 12. Context 2, the battle field scripts (2026-10-29)

Read for the 13.8 party AI and battle field AI port, Project Zanarkand src/ffx/btl/BattleEvents,
ForcedActions and BattleFieldSyscalls, and tests/engine/BtlFieldAiTest.cpp. The names are rows of
tools/apply_party_field_ai_names.py.

**The two blocks.** Correction 2026-10-29: context 2 holds system_01 as set 0 and this battle's field
file as set 1, for every battle. `FFX_Btl_Init 0x781700` loads system_01 through
`FFX_Btl_BeginBattle(-1, 0, 0, 1)`, which is battle field kind 14 index 1 (index 0 names output.bin,
which does not ship). `FFX_Btl_KeepSystemFieldRecord 0x782670` then copies its 32 byte record
`g_ffxBtlFieldRecord 0x112A9A4` down to `g_ffxBtlFieldRecords 0x112A984`, where it stays for the
session. Every battle loads its own field into 0x112A9A4. `FFX_BtlEv_FindSlotRecord` indexes the event
map pointer `g_ffxBtlSystemFieldEventMap 0x112A994` by set * 32, so set 0 reads 0x112A994 and set 1
reads `g_ffxBtlFieldEventMap 0x112A9B4`. The battle start adds system_01's block first and the field's
second (0x783049..0x78306A), and the worker base of set 1 is system_01's map byte 0 read signed, 72
in the shipped data. [V]

The file. `FFX_Btl_SetupFieldPointers 0x783E20` reads a u32 count of 8 then section offsets: +4 the
ATEL block, +8 the event map, +12 the monster section (byte 0 has monsters, +1 the sound flag, +3
airborne), +16 the placement. 817 battle field files ship (system_01 and 816 fields), 2 have no ATEL
block, 2 more carry a block with 0 actors. [D]

**The fire order.** `FFX_BtlEv_FireEvent 0x797BC0` pushes (2, 1, slot, kind), (2, 1, slot, kind2),
then for an enemy (3, unit, slot, kind) and (3, unit, slot, kind2), then (2, 0, slot, kind) and
(2, 0, slot, kind2), each kind only when not 0xFFFF, and starts the first with an entry. So it is the
field file, then the monster's own map, then system_01. `FFX_BtlEv_FireAbilityEvent 0x798000` does
the same with the ability's class slot first in each group (class 2 -> +68, 3 -> +65, 4 -> +71,
6 -> +74, the id & 0xFFF as the kind) and context 4's effect map between the monster and system_01.
The thread always starts on the worker of set 0's record for the slot and runs the code of the block
that matched (the request words of section 5). Both refuse (-1) while `g_ffxBtlEventsOff 0x112A8FE`
is set, and with no caller while the script hold (0x112A8E4) is 2 or the explode hold (0x112C9D7) is
set. The fifth argument calls `FFX_Btl_ShowEveryUnitIfPending 0x795C70`. [V]

**The hold events.** `FFX_BtlEv_FireHoldEvent 0x7981E0`, behind Btl 60 (hold 2) and 151 (hold 1).
n 0..7 starts (2, 1, 62, n) from set 1 slot 62. n 8..23 asks (2, 1, 63, n) and, when the field has
monsters and is not airborne, (2, 0, 63, n), started from set 0, otherwise from set 1. The poll the
Btl wait rows share, `FFX_AtelSys_Btl_StartedEventPoll 0x7A8700`, returns 4 * (work == 0) + 1, so a
wait row ends at once whether its event landed on the caller or not. [V]

**The unit event lookup** of `FFX_Btl_StartUnitEvent 0x7AB360`: a party unit asks (2, 1, unit + 5),
(2, 1, 3), (2, 0, unit + 5), (2, 0, 3). An enemy asks (3, unit, 64), (2, 1, s), (3, unit, 4), (2, 0, s)
with s 4 when its +0x39 is 1, else 3. [V]

**The battle start.** Sub phase 10 of `FFX_Btl_StepSubPhase`: the advantage roll, the blocks, the
bind, `FFX_Atel_BootAllActors` (the init entries run at once), then sound command 12 with the monster
section's +1, the battle count, `FireEvent(0, 0xFF, 24, 0xFFFF, 0, 1)`, `g_ffxBtlStartDelay 0x112A919`
= 60, then units 0..17 shown and given unit event 63 when on the field, and sub phase 11. [V]

Kind 24 at slot 0 is handled by 795 fields. Run through the port on every shipped field: 814 have a
script with actors, 792 start the event, 791 finish within 600 steps. 3 fields boot a btl:60 whose
hold event lands on the booting worker itself, so the hold stays 2 and the battle start event is
refused (the independent model finds 5 such boot holds, 2 of them in fields with no kind 24). One
field loops on a unit test the bare rig never passes. [D]

**Which Btl rows the fields call.** 202 Btl rows are called across the 817 files, 97680 calls. The
port registers 27 of them, 81816 of the calls. The rows ported for context 2, calls / files: 1
807/807, 5 5378/542, 21 20771/562, 34 142/133, 52 33/21, 53 29/18, 60 87/27, 61 398/139, 63 3714/635,
105 540/536, 141 1075/536, 151 54/35, 153 5976/633, 160 193/4, 165 53/51, 184 39/23, 197 1/1, 216
33/31, 275 144/144. The unported rows in the most files: 62 (137), 59 (119), 68 (105), 100..104 and
112..114 (about 78 each), 169 and 222 (76). [D]

**The party AI workers.** 181 workers sit on the AI slots (set 1 slots 41..58, the aeon slots 37..40),
and the level 0 thread (entry 1) of every one waits forever on core:95. So a bare level 0 End in a
battle worker is never reached in shipped data. If it were, `FFX_BtlEv_RunWorkerToCompletion` would
spin, because its loop waits for the level to drop back and an ended level 0 thread never does. The
port faults Hang there, the same outcome. [V] for the loop, [D] for the census

**The forced turn** of `FFX_Btl_SendMenu`. After the claim and the doom count: Sleep (counter +0x608)
skips the choice, Provoke goes to `FFX_Btl_CommitProvokedAttack 0x799D50` and falls through to the AI
or menu when that returns 0, Berserk 0x200 and Confusion 0x100 of status 1 go to
`FFX_Btl_CommitForcedAttack 0x799AC0` with mode 0 and 1. The pick is `FFX_Btl_PickForcedTargets
0x7996D0` through `FFX_Btl_FilterTargetableUnits 0x799820`, one unit on stream 4 (0x79976F) or a target
group on stream 5 (`FFX_Btl_PickTargetGroup 0x799910`, 0x7999A4). `FFX_Btl_IsActorAiControlled
0x792120` draws stream 17 for unit 14 only (0x792137), the AI takes the turn when Yojimbo's motivation
/ 4 is above the draw & 0xFF. Auto-Battle is not a branch here, it lives in the battle menu. [V]

**The draws.** At the battle start only stream 2 is drawn, by the scripts (core 166 and 169). Btl 105
draws stream 7 through `FFX_BtlAi_FilterAndPickTarget` with self 0, at 0x7ACDD6, only when more than
one unit is kept. The forced turn draws streams 4, 5 and 17 as above. [V]

Still open for context 2:

- A monster's own entry for a battle event runs on a context 2 worker through a context 3 block. The
  port refuses it (BadContext). How the original's rebind handles the cross context case was not
  traced.
- `maybe_FFX_Btl_RepeatCopycatCommand 0x799B40` and `maybe_FFX_Btl_ResolveMixCommand 0x7B0520`,
  reached from `FFX_Btl_ExecCommand`, are named but not ported.
- The advantage roll `FFX_Btl_RollPreemptiveOrAmbush 0x78D090` and the angle copy 0x793CB0 of the
  battle start are not ported.
