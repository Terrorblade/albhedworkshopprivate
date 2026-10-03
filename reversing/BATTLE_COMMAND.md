# Battle command commit path

All addresses are VAs as IDA shows them. RVA = VA - 0x400000, given alongside so it can be
pasted straight into a header. Everything here was read out of the decompiler, not guessed,
unless a line says "inferred".

The short version: there is exactly one commit function, the player menu and the monster AI
both go through it, the command is a 72 byte record, and the targeting is a 32 bit bitmask of
unit indices. The commit itself checks almost nothing, so a replayed network command will be
accepted. The hard problem is not the commit, it is that the battle command *menu* is a single
global instance, start to finish.

---

## 1. The commit point

### FFX_Btl_CmdQueue_Push - VA 0x7B0B90, RVA 0x3B0B90

```c
int __cdecl FFX_Btl_CmdQueue_Push(
        int          actorIndex,   // 0..30, the acting unit
        const void  *cmd72,        // the 72 byte command record, see below
        char         postKind,     // lands at record+6, drives post action handling
        char         prio,         // lands at record+7, priority class in the high nibble
        char         variant);     // lands at record+1, 0 normal turn, 1 counter / extra turn
```

This is the single store that turns "a choice exists" into "this actor will perform this
action". It appends `cmd72` to the command queue, then overwrites the record header from its
own arguments. Only `record+3` (the entry count) and the four action entries survive from the
caller's buffer:

```
slot = &g_ffxBtlCmdQueue[72 * g_ffxBtlCmdQueueCount];
qmemcpy(slot, cmd72, 0x48);
slot[0] = actorIndex;  slot[1] = variant;  slot[2] = 0;
slot[4] = slot[5] = 0; slot[6] = postKind; slot[7] = prio;
++actor[0xDE7];                       // per actor pending command count
FFX_Btl_Cmd_ConsumeItems(actorIndex, cmd72, -1);   // this is where items leave the inventory
++g_ffxBtlCmdQueueCount;
```

Returns -1 on success, 0 when the queue is full.

**Call this one level up instead.** `FFX_Btl_CommitCommand` at **VA 0x792D60, RVA 0x392D60** is
the function every real caller uses, and it also retires the actor's turn queue entry, which
`FFX_Btl_CmdQueue_Push` does not:

```c
int __cdecl FFX_Btl_CommitCommand(u8 *cmd72, int gilCost, unsigned variant);
// sets g_ffxBtlPendingGilCost = gilCost
// if (unitIndex <= 30 && variant <= 1) {
//     if (FFX_Btl_CmdQueue_Push(unit, cmd72, 0, 0, variant)) return 0;
//     FFX_Btl_TurnQueue_RemoveAt(unit, actor[0xDE4], 0, 0);
// }
```

There is a second commit for interrupts and counters, `FFX_Btl_CmdQueue_Insert` at
**VA 0x7B0A30, RVA 0x3B0A30**, reached through `FFX_Btl_CommitCommandPriority` at
**VA 0x7929B0, RVA 0x3929B0**. Same record, same queue, it just computes an insertion point
instead of appending. Only three callers: `sub_7AE1A0`, `sub_7AE2E0`, and
`FFX_Btl_RunAiAndCommit` in its priority mode.

### The 72 byte command record

```
+0x00  u8   acting unit index, 0..30
+0x01  u8   variant. 0 = normal CTB turn, 1 = counter or extra turn.
            For units 0..30 this MUST be <= 1 or FFX_Btl_ExecCommand raises the
            internal error "com".
+0x02  u8   entry cursor, which action entry is executing. Zeroed at commit.
+0x03  u8   entry count, 1..4. A count of 0 means "no command chosen yet, go ask the AI".
+0x04  u8   exec state. 0 fresh, 1 finished, 2..6 waiting on an effect.
+0x05  u8   zero
+0x06  u8   post action kind, copied into actor+0xDEA and switched on in FFX_Btl_FinishActions
+0x07  u8   priority class, high nibble only
+0x08 .. +0x47   four 16 byte action entries, entry i at +0x08 + 0x10*i:

   entry +0x00  u16  command / ability id
   entry +0x02  u16  sub id, 0xFF means none
   entry +0x04  u32  never seen written. Reserved.
   entry +0x08  u32  TARGET BITMASK, bit n = unit index n
   entry +0x0C  u32  never seen written. Reserved.
```

The id space is 16 bit with the class in the top nibble, decoded by
`FFX_Btl_ResolveAbilityId` (**VA 0x78CE50, RVA 0x38CE50**) and
`FFX_Btl_GetAbilityRecord` (**VA 0x79A4B0, RVA 0x39A4B0**):

| top nibble | meaning | table lookup |
|---|---|---|
| 0x0 | character / party scope id | `sub_79AF10` |
| 0x2 | **item**, low 12 bits = item id | `sub_7909F0` |
| 0x3 | **player ability or command** | `sub_790A90` |
| 0x4 | **monster ability** | `sub_790A50` |
| 0x5, 0x7, 0xB | `sub_7ABDF0` | |
| 0x6 | other | `sub_790A70` |
| 0x8 | `sub_790970` | |
| 0xA | `sub_790860` | |

The id's low 12 bits are a row index into a kernel.bin table, fetched by
`FFX_KernelTable_GetRow`. The ability names are therefore **not in the exe**, so none of the
ids below are named here from the binary.

Ids seen in the wild: 0x3000 (the root command Auto-Battle forces the cursor onto, so almost
certainly Attack, but that is **inferred** from Auto-Battle behaviour not read from a table),
0x3002, 0x300E, 0x3016, 0x3017, 0x3022, 0x3028 (the forced ability when `actor+0x6DE` is set),
0x3056, 0x3057, 0x3103, 0x3108, 0x311E (list terminator in `FFX_Btl_ResolveAbilityId`), 0x3120
(the confusion self attack, from `sub_799CD0`), 0x3124..0x312B (the eight Magus Sisters orders,
from `FFX_Btl_AeonSpecialCommandHook`), 0x403C, 0x4086, 0x4125, 0x604D, 0x60BD, 0x60F1.

**There is no item slot field.** Items are addressed by ability id (0x2xxx) and consumed with
`FFX_SaveData_AddItem(id, -1)` inside `FFX_Btl_Cmd_ConsumeItems`
(**VA 0x7B0C20, RVA 0x3B0C20**). That is good for determinism, you never have to replicate an
inventory index.

---

## 2. The single-instance globals

This is the section the mod lives or dies on, so it goes before the queue detail.

### The dangerous ones, in order

#### g_ffxBtlMenuStagedCmd - VA 0x23CC040, RVA 0x1FCC040, 72 bytes

**The worst one.** There is exactly ONE 72 byte staging record for the battle command menu.

* Zeroed by `FFX_BtlMenu_Open` (VA 0x89BB10).
* Rebuilt **every single frame** by `FFX_BtlMenu_BuildStagedCommand` (VA 0x89AFC0), which is
  called from the tail of `FFX_BtlMenu_Step` (VA 0x89AE20), which `FFX_Btl_MainStep` calls once
  per frame. Not only on confirm. Every frame.
* `record+0` is stamped each frame from menu page 0's owner actor, so whoever owns the page
  stack owns the record.
* Committed by `FFX_BtlMenu_ConfirmCommand` (VA 0x8975A0) and written inline by the four menu
  page escape shortcuts at 0x89C900, 0x89CDE0, 0x8A22F0 and 0x8A2690.

A second simultaneous commander corrupts this outright. Player B's frame of
`FFX_BtlMenu_BuildStagedCommand` would overwrite player A's half-built command with B's actor,
B's ability ids and B's target mask. There is no second copy to fall back on.

#### g_ffxBtlMenuPages - VA 0x133C950, RVA 0xF3C950, 8 x 240 bytes
#### g_ffxBtlMenuPageDepth - VA 0x23CC092, RVA 0x1FCC092, s8

One menu page stack, 8 deep, with one depth cursor. -1 means empty. `FFX_BtlMenu_PushPage`
(VA 0x89A330) pushes, every menu field access in the 0x89xxxx / 0x8Axxxx module indexes
`240 * g_ffxBtlMenuPageDepth`. This is the "menu cursor owner" global. Page fields in use:

```
page +0x01  u8   state, 1 = new, 6 = suspended
page +0x02  u8   kind, 1 = a target / list page that contributes a mask
page +0x08  u16  owner actor index
page +0x0B  u8   cursor index into the chosen id array
page +0x0C  u16  the command id that opened this page
page +0x16       u16 array of chosen ids, indexed by the cursor at +0x0B
page +0xA4       count and limit words used by FFX_BtlMenu_PushPage
page +0xD8  u32  target bitmask contributed by this page
```

Two players navigating at once share one stack and one depth. Corruption, and it is worse than
the staged record because the depth drives which page memory every handler reads.

#### g_ffxBtlMenuOwnerActor - VA 0x23CC088, RVA 0x1FCC088, u16

The single actor the menu belongs to, 255 = none. Set by `FFX_BtlMenu_Open`, cleared by
`FFX_BtlMenu_ConfirmCommand`. Read in `FFX_BtlMenu_PushPage`, `FFX_BtlMenu_SaveCursorMemory`,
`FFX_BtlMenu_RestoreCursorMemory` and the page procs. A second commander overwrites it and the
first player's menu starts reading the second player's actor.

#### g_ffxBtlMenuTargetMask - VA 0x133D178, RVA 0xF3D178, u32

The mask currently under the targeting cursor. Single slot. Set from
`g_ffxBtlMenuTargetCandMasks[g_ffxBtlMenuTargetCursor]`, read by
`FFX_BtlMenu_BuildStagedCommand` to fill an action entry's mask whenever the page kind byte is
clear. Its candidate list is also single instance:

| what | VA | RVA |
|---|---|---|
| `g_ffxBtlMenuTargetCandMasks` (up to 20 u32) | 0x133D180 | 0xF3D180 |
| `g_ffxBtlMenuTargetCandUnits` (u16 unit indices) | 0x133F0DC | 0xF3F0DC |
| `g_ffxBtlMenuTargetCandCount` | 0x133F0D8 | 0xF3F0D8 |
| `g_ffxBtlMenuTargetCursor` (u16 index into the above) | 0x133D1FC | 0xF3D1FC |

Two target cursors on screen at once would fight over all four.

#### g_ffxBtlCmdPreview - VA 0x112BDF0, RVA 0xD2BDF0, 72 bytes
#### g_ffxBtlCmdPreviewValid - VA 0x112C9E6, RVA 0xD2C9E6, u8

A second 72 byte copy of the staged command, written every frame by
`FFX_Btl_SetCommandPreview` (VA 0x792E50) from `FFX_BtlMenu_BuildStagedCommand`. Read by
`FFX_Btl_BuildCtbPreview` (VA 0x79A200) to draw how the pending command would reshuffle the
turn order. Display only, so a clash here shows the wrong CTB preview rather than executing the
wrong action. Still single slot.

#### g_ffxBtlMenuStagedGil - VA 0x23CC038, RVA 0x1FCC038, u32
#### g_ffxBtlPendingGilCost - VA 0x112BE90, RVA 0xD2BE90, u32

The gil cost of the pending command (Bribe, paying Yojimbo). The menu writes the first, the
commit copies it into the second, `FFX_Btl_ExecCommand` spends it with
`FFX_SaveData_SpendGil`. One slot each for the whole battle. Serialised in practice because the
commit and the spend are the same frame, but a second commander between them would pay the
wrong amount.

#### g_ffxBtlActingActor - VA 0x112C9DF, RVA 0xD2C9DF, u8 (plus a duplicate in the byte above)

Written by `FFX_Btl_SendMenu` to the actor it is about to hand the menu to, and by
`FFX_Btl_ExecCommand` to the actor whose command is firing. Read by the battle HUD
(`sub_794330`, `sub_792F20`) and the debug window. Two commanders both stamp it. The damage is
cosmetic (HUD, camera), but it is the clearest "currently selected actor" variable in the
battle code and is worth shadowing per player if you want the HUD right.

#### g_ffxBtlCurTargetMask / g_ffxBtlShownTargetMask - VA 0x112C8C8 / 0x112C8CC, RVA 0xD2C8C8 / 0xD2C8CC

The mask of the action currently resolving, and its display twin. Written by
`FFX_Btl_ResolveTargets` and `FFX_Btl_ExecCommand`. Single slot, execution scoped, lower risk.

#### g_ffxBtlAiStagedCmd - VA 0x1136A78, RVA 0xD36A78, 72 bytes

The AI script's own single staging record, count at `+3` = **VA 0x1136A7B**. Cleared and
stamped by `FFX_BtlAi_ResetStagedCommand` (VA 0x7ACB00), filled one entry at a time by
`FFX_BtlAi_AddCommandEntry` (VA 0x7AC9C0), committed by `FFX_Btl_RunAiAndCommit`. Its gate
`g_ffxBtlAiScriptActorCtx` at **VA 0x1136A68** is -1 whenever no script is running, and every
staging opcode refuses while it is -1.

This one is **not** a two player hazard: only one AI script ever runs at a time, inside one
`FFX_Btl_RunAiAndCommit` call, which starts and finishes within one frame. It is a save-state
and hook-reentrancy hazard only. Do not re-enter the AI runner from a network callback.

### The safe one, for contrast

`g_ffxBtlMenuCursorMemValid` at **VA 0x133D770, RVA 0xF3D770** (one byte per actor) and
`g_ffxBtlMenuCursorMem` at **VA 0x133D788, RVA 0xF3D788** (360 bytes per actor, 10 pages of 36)
are already per-actor. This is the shipped "Cursor: Memory" battle option, gated by
`FFX_Btl_IsCursorMemoryOptionOn` (VA 0x7851C0, bit 1 of the battle option bits at 0x113079C).
It is the only part of the menu state that is indexed by actor rather than being one slot, and
it is a useful shape to copy when you per-player the rest.

`g_ffxBtlMenuOpenMask` at **VA 0x23CC08C, RVA 0x1FCC08C** is a half-exception. It is genuinely
a bitmask (`|= 1 << actor` in `FFX_BtlMenu_Open`) so it could hold two owners, but every reader
tests it as a boolean (`== 0` / `!= 0`) and `FFX_BtlMenu_Close` zeroes the whole thing. The
field is the right shape, the code around it is not.

### The design conclusion

Do not try to run two menus. Replicate the committed record. One player drives the menu
locally, the staged record at 0x23CC040 plus the gil cost at 0x23CC038 goes on the wire at
confirm, and the receiving peer calls `FFX_Btl_CommitCommand` directly without ever touching
the menu globals. That sidesteps every entry in this section.

If you later want both players in the menu at once, the smallest honest version is to make
`g_ffxBtlMenuStagedCmd`, `g_ffxBtlMenuPages`, `g_ffxBtlMenuPageDepth`,
`g_ffxBtlMenuOwnerActor` and the four targeting globals into per-player instances and swap the
active set around `FFX_BtlMenu_Step`. That is a lot of relocation, which is why replicating the
record is the recommended path.

---

## 3. The command queue and the per-actor record

### g_ffxBtlCmdQueue - VA 0x112AC70, RVA 0xD2AC70

72 bytes per slot, **62 slots**, count in `g_ffxBtlCmdQueueCount` at
**VA 0x112BDE1, RVA 0xD2BDE1** (one byte, signed, read as `(char)`).

Strict FIFO. `FFX_Btl_ExecCommand` only ever looks at slot 0 via
`FFX_Btl_CmdQueue_GetHead` (VA 0x7B0A10), which returns `g_ffxBtlCmdQueue` or null.

The whole battle queue block is laid out contiguously, which is how the capacities were
confirmed rather than assumed:

```
0x112AA80 .. 0x112AC6F   turn queue,     62 x 8   bytes
0x112AC70 .. 0x112BDDF   command queue,  62 x 72  bytes
0x112BDE0                turn queue count
0x112BDE1                command queue count
0x112BDE2                CTB sub tick counter
0x112BDE3                CTB sub tick limit
0x112BDF0 .. 0x112BE37   staged command preview, 72 bytes
```

### Per-actor fields on the command path

All relative to `FFX_Battle_GetActor(index)` (VA 0x794020). Ally / big records are 3984 bytes
(0xF90) at `g_ffxBattleAllyActors`, indices 0..30. Indices 31..92 use a 912 byte record at
`g_ffxBattleMonsterActors`.

| offset | dec | meaning |
|---|---|---|
| +0x4FE | 1278 | battle party slot 0..2, or aeon index, or enemy index |
| +0x4FF | 1279 | target protection level, compared in `FFX_Btl_ResolveTargets` |
| +0x5BC | 1468 | overdrive gauge |
| +0x5BD | 1469 | overdrive gauge max |
| +0x5C4 | 1476 | forced target actor (provoke style statuses) |
| +0x5D4 | 1492 | **current MP**, a dword |
| +0x606 | 1542 | status word read by `FFX_Btl_SendMenu`: 0x400 / 0x200 / 0x100 pick a forced action path, bit 4 = escaped |
| +0x609 | 1545 | status byte, blocks abilities with record+28 & 0x20000 |
| +0x616 | 1558 | second status word, 0x4000 = confuse, 0x400 blocks overdrive abilities |
| +0x65C | 1628 | **CTB counter**, decremented by `FFX_Btl_CtbTick` |
| +0x65D | 1629 | CTB counter reload value |
| +0x690 | 1680 | sealed-ability bitfield, u16 per 16 ids. `FFX_Btl_IsCommandLocked` tests bit `(id & 0xF)` of the word at `+0x690 + 2*((id & 0xFFF)/16)` |
| +0x6C6 | 1734 | forced command id for the forced action paths |
| +0x6C8 | 1736 | this actor's escape / flee command id, returned by `FFX_Btl_GetEscapeCommandId` |
| +0x6CC | 1740 | resolved MP cost, cached by `FFX_Btl_CheckCommandCost` |
| +0x6CD | 1741 | resolved overdrive cost |
| +0x6DE | 1758 | when set, the current command is forced to ability 0x3028 |
| **+0x72C** | **1836** | **the per-actor 72 byte command record**, same layout. `FFX_Btl_ExecCommand` qmemcpy's the finished record here, `FFX_Btl_FinishActions` zeroes it, and `FFX_Btl_CmdQueue_GetActorCmdOrLast` falls back to it when the actor has no live queue slot |
| +0xDC0 | 3520 | active target mask of the running action |
| +0xDC8 | 3528 | unit is present / in combat |
| +0xDCC | 3532 | KO |
| +0xDCE | 3534 | escaped / removed |
| +0xDD6 | 3542 | ready to take a turn |
| +0xDE4 | 3556 | index into the TURN queue, 0xFF = none |
| **+0xDE5** | **3557** | **index into the COMMAND queue, 0xFF = none** |
| +0xDE6 | 3558 | count of this actor's turn queue entries |
| +0xDE7 | 3559 | count of this actor's command queue entries |
| +0xDEA | 3562 | post action kind, copied from record+6 |
| +0xDED | 3565 | the actor that issued the current command |
| +0xDF0 | 3568 | action state machine: 0 idle, 1 finishing, 2 acting, 3 done, 4/5 waiting |
| **+0xDF3** | **3571** | **AI controlled flag.** `FFX_Btl_SetupUnitRoster` sets it on units 20..27 |

So there are two places to look for "what is this actor doing": the live queue slot via
`+0xDE5`, and the last finished record at `+0x72C`.

### Queue accessors

| function | VA | RVA |
|---|---|---|
| `FFX_Btl_CmdQueue_GetHead` | 0x7B0A10 | 0x3B0A10 |
| `FFX_Btl_CmdQueue_GetActorCmd` (null when none) | 0x7B09E0 | 0x3B09E0 |
| `FFX_Btl_CmdQueue_GetActorCmdOrLast` (falls back to actor+0x72C) | 0x7B09B0 | 0x3B09B0 |
| `FFX_Btl_CmdQueue_RemoveAt` | 0x7B0860 | 0x3B0860 |
| `FFX_Btl_CmdQueue_RemoveAllForActor` | 0x7B0830 | 0x3B0830 |
| `FFX_Btl_CmdQueue_FindByVariant` | 0x7B0720 | 0x3B0720 |
| `FFX_Btl_CmdQueue_FindByPriority` | 0x7B0770 | 0x3B0770 |
| `FFX_Btl_CmdQueue_ActorHasOtherPending` | 0x7B07D0 | 0x3B07D0 |

### The turn queue, which is a different thing

`g_ffxBtlTurnQueue` at **VA 0x112AA80, RVA 0xD2AA80**, 8 bytes per entry, 62 entries, count in
`g_ffxBtlTurnQueueCount` at **VA 0x112BDE0, RVA 0xD2BDE0**.

```
+0  u8  actor index
+1  u8  variant, becomes command record+1
+2  u8  real turn flag
+3  u8  claimed flag. FFX_Btl_SendMenu sets it to 1 when it hands the turn out
+4  u8  priority class, high nibble
```

This is the CTB "whose turn is it" queue, not the command queue. Pushed by `FFX_Btl_CtbTick`,
claimed by `FFX_Btl_SendMenu`, retired by `FFX_Btl_TurnQueue_RemoveAt`.

| function | VA | RVA |
|---|---|---|
| `FFX_Btl_TurnQueue_Push` | 0x7B2430 | 0x3B2430 |
| `FFX_Btl_TurnQueue_Insert` | 0x7B2300 | 0x3B2300 |
| `FFX_Btl_TurnQueue_RemoveAt` | 0x7B20E0 | 0x3B20E0 |
| `FFX_Btl_TurnQueue_GetHead` | 0x7B22E0 | 0x3B22E0 |
| `FFX_Btl_TurnQueue_SetClaimed` | 0x7B2290 | 0x3B2290 |

---

## 4. Who writes the command. Player and AI really are the same path.

`FFX_Btl_CommitCommand` (0x792D60) has exactly ten callers, and that is the complete set of
ways a command gets committed in this build:

| caller | VA | what it is |
|---|---|---|
| `FFX_BtlMenu_ConfirmCommand` | 0x8975A0 | **the player menu confirm** |
| `FFX_BtlMenu_PageProc_Root` | 0x89C900 | escape / flee shortcut from the root page |
| `FFX_BtlMenu_PageProc_B` | 0x89CDE0 | same shortcut, another page |
| `FFX_BtlMenu_PageProc_C` | 0x8A22F0 | same shortcut, another page |
| `FFX_BtlMenu_PageProc_D` | 0x8A2690 | same shortcut, another page |
| `sub_799AC0` | 0x799AC0 | forced action, status 0x200 and 0x100 at actor+0x606 |
| `sub_799CD0` | 0x799CD0 | forced action, confuse (actor+0x616 bit 0x4000), self targeted 0x3120 |
| `sub_799D50` | 0x799D50 | forced action, status 0x400, forced id at actor+0x6C6 against actor+0x5C4 |
| **`FFX_Btl_RunAiAndCommit`** | **0x7ACEB0** | **the monster / aeon AI script** |
| `FFX_Btl_SendMenu` | 0x792A90 | the empty placeholder record (entry count 0) handed to the AI |

### Answer: yes, the same function

`FFX_Btl_RunAiAndCommit` at **VA 0x7ACEB0, RVA 0x3ACEB0**:

```c
int FFX_Btl_RunAiAndCommit(u8 actor, int issuerActor, int eventKind,
                           int aeonVariant, int commitMode, u8 *existingCmd);
```

It sets `g_ffxBtlAiScriptActorCtx`, resets `g_ffxBtlAiStagedCmd`, runs the unit's AI script
(which stages entries through `FFX_BtlAi_AddCommandEntry`), and then:

* `existingCmd != null` -> `qmemcpy(existingCmd, g_ffxBtlAiStagedCmd, 72)` in place. This is
  how the executor's placeholder record gets filled.
* `commitMode == 0` -> `FFX_Btl_CommitCommand(g_ffxBtlAiStagedCmd, 0, 0)`. **The same call the
  player menu makes.**
* `commitMode == 1` -> `FFX_Btl_CommitCommandPriority`, the insert variant, for counters.

So `FFX_Btl_CommitCommand` is the ideal co-op command executor. One network message shape
covers both the player and the AI, because the game itself already funnels both into one
72 byte record and one push.

### The player / AI fork

`FFX_Btl_SendMenu` at **VA 0x792A90, RVA 0x392A90** (its original symbol is `send_menu`, from
the internal error string it raises). Called once per frame from `FFX_Btl_MainStep`. It pops the
turn queue head and decides:

```c
if (FFX_Btl_IsActorAiControlled(actor) == 0 || g_ffxBtlDbgMonInput != 0)
    FFX_BtlMenu_Open(actorIndex);        // player gets the menu
else
    FFX_Btl_CommitCommand(placeholder, 0, variant);   // count 0, AI fills it later
```

`FFX_Btl_IsActorAiControlled` (**VA 0x792120, RVA 0x392120**) just returns `actor+0xDF3`, with
one special case: unit 14 (Yojimbo) can be forced to AI by a random roll against
`sub_7B2B20(actor)/4`, his motivation.

Two things fall out of this that matter a lot:

1. **`FFX_Btl_IsAllyUnit` does not mean "on my team".** It is literally `unitIndex <= 30`
   (VA 0x793650). Enemy units live at indices 20..27 inside the same 3984 byte array, so
   enemies come through `FFX_Btl_SendMenu`, `FFX_Btl_CtbTick` and `FFX_Btl_ExecCommand` on
   exactly the same code.

2. **There is a shipped debug flag that already does what you want.**
   `g_ffxBtlDbgMonInput` at **VA 0x112A8FA, RVA 0xD2A8FA** ("Mon Input" in the BattleConfig
   debug window) makes `FFX_Btl_SendMenu` open the *player command menu* for AI controlled
   units. That is a working proof inside the retail binary that the command menu can drive any
   unit index, and that the only thing standing between a unit and the menu is `actor+0xDF3`.

### Auto-Battle does not write the record

`g_boosterAutoBattle` at **VA 0x133D6E0, RVA 0xF3D6E0** never touches the command record. It
drives the menu by **synthesising pad bits**. The pattern repeats in all four page procs:

```c
// FFX_BtlMenu_PageProc_C, 0x8A22F0
if (g_boosterAutoBattle == 1)
    HIWORD(g_ffxMenuPadHeldTrig) = 0x2000;   // fake the confirm button
```

and in `FFX_BtlMenu_PageProc_Root`:

```c
if (g_boosterAutoBattle == 1 && dword_133D6EC == 1)
    HIWORD(g_ffxMenuPadHeldTrig) = 16;
if (g_boosterAutoBattle == 1) {
    if (*(s16*)(a1+66) > 0) *(s16*)(a1+66) = 0;      // force cursor to row 0 = Attack
    if (FFX_BtlMenu_ClassifyCommand(g_ffxBtlMenuOwnerActor, 0x3000) < 0) sub_7871F0();
    HIWORD(g_ffxMenuPadHeldTrig) = 32;
}
```

So Auto-Battle flows through the *same* staging record and the *same*
`FFX_BtlMenu_ConfirmCommand`. For lockstep that is good news: you do not have a third
independent writer. It is also a warning, because it reads `g_ffxMenuPadHeldTrig` and the menu
page state, so it is subject to every single-instance problem in section 2. `dword_133D6EC`
(**VA 0x133D6EC**) is the handshake between Auto-Battle and `FFX_Btl_ExecCommand`: the executor
sets it to 1 when the chosen action got rejected, which nudges Auto-Battle to pick again.

---

## 5. Target selection

**A 32 bit bitmask of unit indices.** `bit n` set means unit index `n` is a target, for
`n` in 0..30. Single target is one bit, multi target is several. There is no separate
single / multi representation and no party-slot form on the command path.

This is unambiguous across every writer:

```c
// FFX_BtlMenu_PageProc_Root, self targeted flee
dword_23CC050 = 1 << *(u8 *)(a1 + 8);      // 1 << ownerActor

// sub_799CD0, confuse self attack
*(u32 *)(cmd + 16) = 1 << actorIndex;

// FFX_BtlAi_AddCommandEntry, AI side
for (u = 0, bit = 1; u < 31; ++u, bit <<= 1)
    if ((candidateMask >> u) & 1) { ...keep or drop bit... }
*(u32 *)(entry + 8) = filtered;
```

The unit index space, from `FFX_Btl_SetupUnitRoster` (VA 0x79C110):

| indices | what |
|---|---|
| 0..6 | the seven playable characters |
| 8..17 | aeons, `actor+0x4FE = index - 8` |
| 18, 19 | not assigned in the roster setup |
| 20..27 | the eight enemy units, `actor+0xDF3 = 1` |
| 28..30 | not assigned |
| 31..92 | the 912 byte light records, a different array |

The three active party members are `g_ffxFieldPartyChars01[0..2]` (VA 0x11307E8), and the
roster loop writes their battle slot into `actor+0x4FE`. That mapping is exactly where a
per-player ownership table belongs.

The menu builds the mask through its candidate list rather than computing it at confirm:
`g_ffxBtlMenuTargetCandMasks` (0x133D180, up to 20 entries) holds one precomputed mask per
selectable target, `g_ffxBtlMenuTargetCursor` (0x133D1FC) indexes it, and
`g_ffxBtlMenuTargetMask` (0x133D178) is the current one. A multi-target ability gets a
candidate entry whose mask already has every bit set, so the menu never has to know whether it
is aiming at one unit or a row.

The mask is filtered twice:

* at stage time by `FFX_BtlAi_AddCommandEntry` on the AI side, keeping a bit only if the unit is
  present and not KO, or is KO and the ability's flag word at `record+26` has `0x40` set
  (the abilities that target the dead, Life and Phoenix Down)
* at execution time by `FFX_Btl_ResolveTargets` (next section)

---

## 6. Gates

### At commit: essentially none

`FFX_Btl_CmdQueue_Push` checks only `g_ffxBtlCmdQueueCount < 62`.
`FFX_Btl_CommitCommand` adds only `unitIndex <= 30 && variant <= 1`.

No CTB readiness test. No MP test. No silence test. No "this actor already acted" flag.
A command record handed to `FFX_Btl_CommitCommand` will be accepted. That is the single most
useful fact in this document for the mod.

The one side effect to be aware of is that `FFX_Btl_CmdQueue_Push` calls
`FFX_Btl_Cmd_ConsumeItems(actor, cmd, -1)`, so committing an item command decrements the
inventory right there. Replaying the command on the peer therefore also decrements its
inventory, which is correct behaviour and means you do not replicate inventory separately.
`FFX_Btl_ExecCommand` calls the same function with `+1` to refund a cancelled action.

### Before commit, in the menu only

These are what grey out a menu row. They never run on a replayed command, so they do not
threaten replay, but they tell you which commands a given actor would have been *allowed* to
pick, which matters if you want to validate an incoming message rather than trust it.

`FFX_BtlMenu_ClassifyCommand` (**VA 0x89ACA0, RVA 0x49ACA0**) returns negative to reject:

* `FFX_Btl_IsCommandLocked(actor, id)` (**VA 0x79A5B0, RVA 0x39A5B0**) -> -1.
  For class 0x3 ids this tests bit `(id & 0xF)` of the u16 at
  `actor + 0x690 + 2*((id & 0xFFF)/16)`, the per-actor sealed-ability bitfield.
  For class 0x2 (items) it rejects when `id == g_ffxItemInUse` and the inventory count has
  reached zero.
* `FFX_Btl_GetCommandMpCost(actor, record, extra) == -1` (**VA 0x78C690, RVA 0x38C690**) -> -2.

### At execution, and this is the one that can bite a network command

`FFX_Btl_ExecCommand` (**VA 0x792210, RVA 0x392210**) revalidates before the action fires:

**`FFX_Btl_ResolveTargets` - VA 0x791FA0, RVA 0x391FA0**

Reads the entry's mask at `cmd + 16 + 16*cursor`, walks units 0..30, and keeps a bit only if
both hold:

* `target[0x4FF] <= reach` (where reach is `sub_799690(actor, abilityRecord)`) **or** the target
  is the attacker itself
* the target is alive (`target[0xDCC] == 0`) **or** the ability's flag word at `record+26` has
  `0x40` set

It writes the filtered mask **back over** `cmd + 16 + 16*cursor` and into
`g_ffxBtlCurTargetMask`. If nothing survives and the ability requires a target
(`record+43 != 0 || record+28 & 0x200 || record+33 & 1`) it returns 0 and the action is dropped.

**`FFX_Btl_CheckCommandCost` - VA 0x78AB20, RVA 0x38AB20**

Returns 0 (reject) if any of:

* current MP, the dword at `actor+0x5D4`, is below the resolved cost
* the resolved cost came back negative (the ability is not usable at all)
* the overdrive gauge at `actor+0x5BC` is below the ability's requirement at `record+38`

On success it caches the MP cost in `actor+0x6CC` and the overdrive cost in `actor+0x6CD`.
If `actor+0x6DE` is set the ability is forced to id 0x3028 regardless of what was committed.

The underlying cost function `FFX_Btl_GetCommandMpCost` also rejects on:

* an overdrive-gated ability when the gauge at `actor+0x5BC` is below max `actor+0x5BD`, or
  `actor+0x616` has bit 0x400, unless `g_ffxBtlDbgLimitBreakOn` is set
* `record+28 & 0x20000` while `actor+0x609` is non zero
* and it zeroes the cost entirely when `sub_79AEF0(actor)` (an aeon) or
  `g_ffxBtlDbgMagFree` is set

**So the complete list of things a replayed command can fail on is: current MP, the overdrive
gauge, and whether any target survived.** Nothing else. That is a short list, and all three are
state that lockstep already has to keep in sync anyway, so if your frames match they will
match.

### The CTB gate, which is where the lockstep hook belongs

`FFX_Btl_CtbTick` (**VA 0x790FB0, RVA 0x390FB0**) is called from `FFX_Btl_MainStep` only when
`FFX_Btl_IsCtbTickAllowed` (**VA 0x791190**) passes and `g_ffxBtlSubPhase == 1`. It returns
immediately while `g_ffxBtlDbgCtbPause` is set **or the command queue is non empty**:

```c
if (g_ffxBtlDbgCtbPause != 0 || g_ffxBtlCmdQueueCount != 0) return 0;
```

That second clause is doing a lot of work for you. The turn clock does not advance while
anybody has a pending command, so the game is already self-serialising around commands.

Pass one picks who is ready: for units 0..30, an actor with no queued turn
(`+0xDE6 == 0`), present (`+0xDC8`), not KO (`+0xDCC == 0`), not escaped (`+0x606 & 4`),
counter expired (`+0x65C == 0`) and ready (`+0xDD6`) gets pushed onto the CTB ready list
(`g_ffxBtlCtbReadyList` at **VA 0x11333C4**, count at **0x11333C0**). The list is sorted by
`sub_78F000` and **only the single lowest actor gets a turn queue entry this tick**.

Pass two, when nobody was ready, decrements every actor's `+0x65C` once every
`g_ffxBtlCtbSubTickLimit` (**VA 0x112BDE3**) sub ticks, counted in `g_ffxBtlCtbSubTick`
(**VA 0x112BDE2**). Hitting zero sets `+0xDE8 = 3` and reloads from `+0x65D`.

Two shipped debug flags here are directly reusable as lockstep tools:

* `g_ffxBtlDbgCtbPause` - **VA 0x112A8E1, RVA 0xD2A8E1**. Freezes the turn clock. A ready-made
  stall for "waiting on the peer".
* `g_ffxBtlDbgCtbSameOrder` - **VA 0x112A8FB, RVA 0xD2A8FB**. Clamps the per sub tick decrement
  to 1 so the turn order never changes. Useful for a determinism test harness.

---

## 7. Per frame order inside FFX_Btl_MainStep

From `FFX_Btl_MainStep` at VA 0x790C10. Only the command-relevant calls:

```
FFX_Btl_CtbTick          0x790FB0   if FFX_Btl_IsCtbTickAllowed && g_ffxBtlSubPhase == 1
FFX_Btl_FinishActions    0x7911E0
sub_791760               0x791760   per actor death / removal pass
FFX_Btl_EndPhaseStep     0x7917D0   if FFX_Btl_IsCtbTickAllowed
FFX_Btl_SendMenu         0x792A90   if FFX_Btl_IsCtbTickAllowed && g_ffxBtlSubPhase == 1
FFX_BtlMenu_Step         0x89AE20   reads the pad, drives the page procs, then
                                    FFX_BtlMenu_BuildStagedCommand rebuilds 0x23CC040,
                                    and a confirm commits from inside here
FFX_Btl_ExecCommand      0x792210   pops queue slot 0 and runs it
```

A command confirmed by the menu on frame N therefore executes on frame N, in the same
`FFX_Btl_MainStep` call. There is no one-frame latency to model. For a replayed network command,
inject it before `FFX_Btl_ExecCommand` in the same frame slot that the originating peer
committed it, and both machines see identical queue contents at the executor.

---

## 8. The debug battle config window, which is a free map of battle behaviour

`SG_DebugWin_BattleConfigProc` at **VA 0x7C6E20, RVA 0x3C6E20** draws a two column panel of
toggles, and the draw case (message 32785) prints the label right next to the widget id, so
each debug flag byte can be matched to its human name. All of these survive in the retail
binary and all of them are live, the window is just not reachable without debug mode. These are
now named `g_ffxBtlDbg*` in the IDB. The command-path interesting ones:

| name | VA | RVA | effect |
|---|---|---|---|
| CTB pause | 0x112A8E1 | 0xD2A8E1 | `FFX_Btl_CtbTick` returns immediately |
| CTB Same Order | 0x112A8FB | 0xD2A8FB | CTB decrement clamped to 1, order frozen |
| Auto Execute | 0x112A8F5 | 0xD2A8F5 | read by `FFX_Btl_Init` and `FFX_Btl_CancelPendingBattle` |
| **Mon Input** | **0x112A8FA** | **0xD2A8FA** | **AI units get the player command menu** |
| **Skip Command** | **0x112A922** | **0xD2A922** | `FFX_Btl_GetEscapeCommandId` returns 255 always, any actor can skip or flee from any page |
| Mag Free | 0x112A901 | 0xD2A901 | zeroes all MP costs in `FFX_Btl_GetCommandMpCost` |
| LimitBreak On | 0x112A90C | 0xD2A90C | bypasses the overdrive gate in `FFX_Btl_GetCommandMpCost` |
| Print Info | 0x112A90B | 0xD2A90B | battle debug printing |
| Ply / Mon Invincible | 0x112A8F9 / 0x112A8F8 | 0xD2A8F9 / 0xD2A8F8 | |
| Dmg Random Off | 0x112A908 | 0xD2A908 | removes damage variance, useful for determinism testing |
| Dmg Prob Off | 0x112A90A | 0xD2A90A | |
| Full Set | 0x112A905 | 0xD2A905 | |

The full left column in order is Full Set, CTB pause, CTB Same Order, Mouse Camera,
Auto Execute, Ply Invincible, Mon Invincible, Mag Num 0, Mag Not Eff, Mag Free, Sum Not Eff,
Dmg Random Off, Dmg Prob Off, Dmg Crit On, Dmg Crit Off, Dmg Status Off, Over Kill Off,
LimitBreak On, LimitBreak Off. The right column is Print Info, ThrowAway Parm, Mon Input,
Skip Command, Every Look, Exchg WillDie, DVD slow start, DVD stop mag, Test Hit Eff,
Map Circle, Ply HP1, Mon HP1, Dmg Hit Miss, Weapon, Magic Item, Camera Prio, Camera Local,
Neck Off, Focus Off. All are named in the IDB now.

---

## 9. Recommended hook shape

Read this as a suggestion built on the above, not as something the binary told me.

**Capture.** Hook `FFX_BtlMenu_ConfirmCommand` (0x8975A0) after its call to
`FFX_BtlMenu_BuildStagedCommand` and before its call to `FFX_Btl_CommitCommand`, or more simply
hook `FFX_Btl_CommitCommand` itself and look at what came in. The wire message is:

```c
struct FfxBtlCommandMsg
{
    uint8_t  record[72];   // the staged command record, verbatim
    uint32_t gilCost;      // g_ffxBtlMenuStagedGil / the gilCost argument
    uint8_t  variant;      // 0 or 1
};
```

72 + 4 + 1 bytes, and record+0 already carries the acting unit so you do not need a separate
actor field. The four action entries are fixed size, so there is nothing variable-length.

**Replay.** Call `FFX_Btl_CommitCommand(record, gilCost, variant)` directly. Do not touch the
menu globals and do not open a menu on the remote side at all. Note that
`FFX_Btl_CmdQueue_Push` will consume items and `FFX_Btl_ExecCommand` will spend gil on the
remote machine too, which is what you want.

**Suppress.** On the peer that is not driving a given actor's menu, stop
`FFX_Btl_SendMenu` from calling `FFX_BtlMenu_Open` for that actor. Returning non zero from
`FFX_Btl_IsActorAiControlled` (0x792120) does this without touching `FFX_Btl_SendMenu`, but it
also routes the actor to the AI, so you would instead want to leave the turn queue entry
unclaimed and wait. Entry+3 is the claimed flag, so an actor whose turn is waiting on the
network can simply have its entry left unclaimed and `FFX_Btl_SendMenu` will see it again next
frame.

**Stall.** `g_ffxBtlDbgCtbPause` (0x112A8E1) already freezes the turn clock cleanly and is read
by shipped code. That is a nicer stall than gating `FFX_Btl_MainStep` wholesale, because the
battle animations and camera keep running while the CTB waits.

---

## What is still unknown

* **Action entry `+0x04` and `+0x0C`.** Two dwords per entry that I never found a writer for.
  `FFX_BtlMenu_Open` and `FFX_BtlAi_ResetStagedCommand` both zero the whole 72 bytes, so they
  are reliably zero in practice, but I cannot say they are unused rather than just unused on the
  paths I read.
* **Command record `+0x01`, the variant, beyond 0 and 1.** The executor rejects >= 1 for
  units 0..30 with the internal error "com", and the CTB tick always pushes 0. I read 1 as
  "counter or extra turn" from `FFX_Btl_CmdQueue_FindByVariant` being called with 1 in
  `FFX_Btl_RunAiAndCommit`, but I did not find the site that pushes a 1 for a real counter, so
  that reading is **inferred**.
* **Command record `+0x06`, the post action kind.** It is copied into `actor+0xDEA` and
  switched on in `FFX_Btl_FinishActions` with cases 1..7 (camera, 0x7AEF20 twice, 0x78DE40,
  0x78DE80, 0x793410, and a flag set). Every commit site I found passes 0, so I never saw a
  non zero value produced and cannot say what each case means in gameplay terms.
* **The exact meaning of `FFX_Btl_ExecCommand` states 2..6.** I read them as "waiting on an
  effect" from the functions they poll (`sub_792880`, `sub_7AF8A0`, `sub_7AFA60`,
  `FFX_Btl_CmdQueue_GetHead`-driven), but I did not pin each state to a specific gameplay event.
* **`sub_78F000`, the CTB sort key.** `FFX_Btl_CtbTick` sorts the ready list by it and
  `FFX_Btl_CtbReadyList_SortByCounter` calls it per actor. I did not decompile it, so I cannot
  promise the sort is free of anything non deterministic. For a lockstep build this is worth
  checking before you trust the CTB.
* **`sub_799690`, the "reach" value** compared against `target[0x4FF]` in
  `FFX_Btl_ResolveTargets`. I read `+0x4FF` as a target protection level from how it is used,
  but I did not read the function that produces the attacker's side of the comparison.
* **Ability record field meanings.** I used `record+22` (class nibble `& 0xF8`, 8 and 16 mean
  "opens a submenu"), `record+23`, `record+26` (bit 0x40 targets the dead, bits 2..3 a target
  scope), `record+28` (0x200 requires a target, 0x8000000, 0x20000), `record+33` bit 0,
  `record+38` (overdrive requirement) and `record+43`. I did not map the record as a whole, and
  I did not find where it comes from on disk.
* **Bit 9 of the battle option bits at 0x113079C**, the one the task flagged as unidentified. I
  checked all the accessor thunks around 0x7851A0..0x785230 and they read bits 0 through 6 only.
  Nothing on the command path reads bit 9. Still unidentified, and now at least known not to be
  on this path. For the record, bit 1 (`FFX_Btl_IsCursorMemoryOptionOn`, VA 0x7851C0) is the
  "Cursor: Memory" option, which is what gates the per-actor cursor memory at 0x133D770.
* **Whether `g_ffxBtlMenuOpenMask` holding two bits actually works.** The field is a real
  bitmask, but I only verified that readers treat it as a boolean. I did not trace what would
  break if two bits were set, so "it is effectively a flag" is **inferred** from the read sites
  rather than tested.
* **Unit indices 18, 19 and 28..30**, and what the 62 light records at 31..92 are for. The
  roster setup does not assign them. 62 matching the queue capacities is suspicious but I have
  no evidence it is related.
* **How the target candidate list gets built.** I found where it is consumed
  (`FFX_BtlMenu_PushPage`, `FFX_BtlMenu_RestoreCursorMemory`,
  `FFX_BtlMenu_BuildStagedCommand`) and that it is capped at 20 entries, but not the function
  that fills `g_ffxBtlMenuTargetCandMasks` with the per-ability target scopes. That is the piece
  you would need if you ever wanted to validate an incoming target mask rather than trust it.
