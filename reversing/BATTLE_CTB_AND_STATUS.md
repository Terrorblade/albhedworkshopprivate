# Battle CTB turn order and status effects

PROGRESS items 1.11.7 (turn order) and 1.11.8 (status effects) of Project Zanarkand. All addresses
are VAs as IDA shows them, RVA = VA - 0x400000. Every claim is marked:

- **[V]** verified, read off Hex-Rays and where it mattered the disassembly
- **[D]** verified against the shipped data files (the kernel tables in the install)
- **[I]** inferred, with the reason given

Read the IDA database only. Nothing here was renamed or commented in the IDB, the names this doc
uses for `sub_` functions are the proposals in section 6.

The model of all of this is `tools/ctb.py` in the projectzanarkand repo. See section 7.

---

## 0. The short version

- One CTB counter per unit, a **byte** at `actor+0x65C`, in the 0xF90 byte record only. Units
  0..30 are the only ones that ever get a turn. [V]
- Every call of `FFX_Btl_CtbTick` either hands a turn to the lowest sort key among units whose
  counter is 0, or, when nobody is at 0, takes 1 off every live unit's counter. There is no
  fractional time and no float anywhere in the CTB. [V]
- A turn costs `clamp(hasteSlow(max(rank, 1) * tick), 0, 255)`, added with a wrapping byte add.
  `tick` comes from `battle/kernel/ctb_base.bin` by agility, `rank` from byte +36 of the ability
  record. Haste halves (C truncation), slow doubles. [V] [D]
- The battle starts from `3 * tick`, minus a per-unit random draw for the party and divided by a
  random 90..100 percent for monsters, then the lowest live counter is subtracted from all. [V]
- The turn order strip on screen is a **separate** computation (`FFX_Btl_BuildCtbPreview`) that
  agrees with the real queue only for plain rank 3 turns. [V]
- 25 rollable statuses (12 flag bits plus 13 counted ones) and a 16 bit second word. Counters tick
  at three different points: per CTB tick (regen accumulator), at the start of the unit's own turn
  (regen, doom, the one-turn guard statuses) and at the end of its own turn (everything else that
  counts). [V] [D]
- Every RNG draw is on a stream picked per unit: `20 + slot` (kind 0), `+16` (kind 1), `+32`
  (kind 2), aeons share one, monsters have their own eight. The CTB itself draws only at battle
  start. [V]

---

## 1. CTB

### 1.1 Per-unit fields

The 0xF90 (3984) byte record `g_ffxBattleUnits + 3984 * i` holds units 0..30. Units 0..6 are
the characters, 8..17 the aeons, 20..27 the eight monster slots (`FFX_Btl_GetEnemyUnit(j)` is
unit 20 + j). The 912 (0x390) byte records at `g_ffxBattleSideRecords` for units 31..92 (the
weapon and armour side records, not monsters, BATTLE_ACTOR.md) carry
**no CTB and no status fields**: every offset below is past 0x390, and `FFX_Btl_CtbTick`,
`FFX_Btl_BuildCtbPreview` and the initial setup only walk 0..30. [V]

| off | type | field | evidence |
|---|---|---|---|
| +0x0C | u8 | own unit index, used to pick the RNG stream in `FFX_Btl_DamageFormula` | [V] |
| +0x5AC | u8 | agility used by the CTB (tick lookup and sort key) | `sub_78D1D0`, `sub_78F000`, `sub_78DED0` [V] |
| +0x613 | u8 | haste counter (status counter 11) | `sub_78C150` halves on it [V] |
| +0x614 | u8 | slow counter (status counter 12) | `sub_78C150` doubles on it [V] |
| **+0x65C** | **u8** | **CTB counter**. 0 means ready | `FFX_Btl_CtbTick` [V] |
| **+0x65D** | **u8** | **3 * tick**, the base. Battle-start value, revive value, and the reload for a unit that does not take turns | `sub_78DED0`, `sub_78D530`, `FFX_Btl_CtbTick` [V] |
| +0x6BC | u16 | auto-ability word 1. Bit 0x0002 = First Strike (counter forced to 0 at start) | `sub_78DED0` [V], bit name [D] from `a_ability.bin` row +98 |
| +0x6D2 | u8 | CTB ticks since this unit's last regen heal, saturates at 0xFF | `FFX_Btl_CtbTick`, `sub_7AF4F0` [V] |
| +0x6EC | s32 | working CTB during damage staging, the "current" for CTB-kind damage formulas | `FFX_Btl_StageEffectsForTarget`, `FFX_Btl_DamageFormula` kind 4 [V] |
| +0x716 | u8 | turn re-offered. Set when a chosen command was rejected and the turn handed back, suppresses the turn-start ticks the second time | `FFX_Btl_ExecCommand` 0x792761, `FFX_Btl_SwapUnitIntoPartySlot` [V] |
| +0xDC8 | u8 | present in the fight | [V] |
| +0xDCC | u8 | KO | [V] |
| +0xDCE | u8 | mirror of status-1 bit 0x0004 (Petrify), rewritten by `FFX_Btl_ApplyPendingEffects` | [V] |
| +0xDD6 | u8 | takes CTB turns. Script property 81 | `FFX_Btl_CtbTick`, `sub_7B4B70` case 81 [V] |
| +0xDD7 | u8 | shown in the turn order strip. Script property 83 | `FFX_Btl_BuildCtbPreview` [V] |
| +0xDE4 | u8 | index of this unit's turn queue entry, 0xFF none | [V] |
| +0xDE6 | u8 | number of turn queue entries for this unit | [V] |
| **+0xDE8** | **u8** | **rank of the current action** | written by `FFX_Btl_ApplyActionToTargets` 0x78934C, reset to 3 by `FFX_Btl_CtbTick` [V] |
| +0xDF7 | u8 | "turn start AI event already ran for this turn" | `FFX_Btl_SendMenu` [V] |

### 1.2 The agility to tick table: `battle/kernel/ctb_base.bin`

`FFX_Btl_Init` 0x781700 selects asset class 13 (the battle cd module) and reads index 11 into a
`FFX_MemAlloc` block whose pointer goes to `dword_112A94C` and size to `word_112A974`
(0x7817FA..0x78183E). Resolved through the shipped PS2 cd index (`cdrom.fid[13] = 7798`,
`cdrom.fnd` entry 7798 + 11), index 11 is `host0:/ffx/master/jppc/battle/kernel/ctb_base.bin`. [V] [D]

In the VBF it is `ffx_ps2/ffx/master/jppc/battle/kernel/ctb_base.bin`, 530 bytes, byte-identical to
the `inpc` copy. Standard kernel table (see `FFX_KernelTable_GetRow` 0x7AB870): dword count 1, one
descriptor `lo 0, hi 254, stride 2, dataOffset 0x14`, then 255 two-byte rows. [D]

```
row r (r = 0..254) is agility r + 1
  +0  u8  tick       ("tick speed", the CTB cost of one rank)
  +1  u8  ICV range  (the party start counter subtracts draw % (range + 1))
```

The two readers clamp first, so agility 0 reads row 0: [V]

```c
u8   sub_7909C0(int agi) { return GetRow(clamp(agi, 1, 255) - 1, dword_112A94C)[0]; }   // tick
u8 * sub_790990(int agi) { return GetRow(clamp(agi, 1, 255) - 1, dword_112A94C); }      // row
```

Shipped tick values (a short expected-value table, run `tools/ctb.py table` for the ranges too): [D]

| agility | tick | | agility | tick |
|---|---|---|---|---|
| 0, 1 | 28 | | 19..22 | 10 |
| 2 | 26 | | 23..28 | 9 |
| 3 | 24 | | 29..34 | 8 |
| 4 | 20 | | 35..43 | 7 |
| 5, 6 | 16 | | 44..61 | 6 |
| 7..9 | 15 | | 62..97 | 5 |
| 10, 11 | 14 | | 98..169 | 4 |
| 12..14 | 13 | | 170..255 | 3 |
| 15, 16 | 12 | | | |
| 17, 18 | 11 | | | |

Within each tick band the range byte counts up 1, 2, 3, ... (1..9, the 170..255 band runs to 6). This
matches the tick table in the community mechanics guides exactly, cross-check only. [I]

### 1.3 Battle start: the advantage roll and the initial counters

**The step.** `FFX_Btl_StepSubPhase` 0x783020 case 34 waits for `dword_112CA68` (map load pending)
to read 0, sets sub phase 10 and in the same call does: [V]

```
0x783211  push 0x20
0x783213  call FFX_Btl_RollPreemptiveOrAmbush      ; stream 1, one draw, always
0x78321E  mov  byte [0x112C9DC], al                ; the advantage byte
          ... sub_797A40 x2, sub_798390(0)          ; no RNG (sub_797A40 only appends a name)
0x783248  mov  al, [0x112A923]
          cmp  al, 0xFF / jz                       ; a forced advantage overrides the roll,
0x783254  mov  byte [0x112C9DC], al                ; but the draw has already happened
```

`0x112C9DC` is `BYTE1(dword_112C9DB)`. 0 normal, 1 pre-emptive, 2 ambush. It can also be written
mid-setup by script `Btl 034` (`FFX_AtelSys_Btl_034_resi` 0x7A4CD0) and read by `Btl 218`. The
override `0x112A923` is set by `Btl 125` case 25 (a debug-parameter style op, next to Skip Command and
Over Kill Off) and reset to 0xFF only by `FFX_Btl_Init`, which runs at `FFX_MainInit` and on a debug
reset, **not** per battle. So once a script forces it, it stays forced until a script puts 0xFF
back. [V] Whether shipped scripts always restore it is not checked. [I]

**`FFX_Btl_RollPreemptiveOrAmbush` 0x78D090**, threshold `t = 32`, bytes checked on the disassembly: [V]

```c
int r = FFX_Rand_Stream(1) & 0xFF;
for (i = 0; i < 18; ++i)                       // units 0..17, present only
    if (actor(i)+0xDC8 && (charRecord(i)[0x4A] & 4)) { r += -1 - t; break; }   // r -= 33
int pre = r < t;                               // signed compares (jge)
return (r < 255 - t) ? pre : 2;
```

So without the bit: r 0..31 pre-emptive (32/256), 223..255 ambush (33/256). With the bit: r - 33,
pre-emptive for r < 65 (65/256), ambush impossible. Bit 0x04 of the auto-ability word is
**Initiative** in `a_ability.bin` (row 2, +98 word bit 0x0004, next to Sensor 0x0001 and First Strike
0x0002) [D], and the char record byte +0x4A being that same word is [I] from the matching bit and the
matching behaviour.

**Order after that.** The same `FFX_Btl_StepSubPhase` call queues battle script event 24, runs AI
event 63 for each present party slot, and falls through phase 11 into phase 12, which calls
`FFX_Btl_LoadMonsterData` 0x783680 once the battle character loads have finished. That is the same
step at the earliest and a later one on a slow disk (the ASYNC_LOAD_REQUESTS.md point). Then
`FFX_Btl_LoadMonsterData` -> `FFX_Btl_RegisterMonsterMotions` 0x784070 -> `sub_7842F0` (monster group
offsets, draws, see section 3) -> **`sub_78DED0(advantage)`** (propose `FFX_Btl_InitCtbCounters`)
-> `sub_79A0E0`. The advantage byte is read here, so a script that writes it through `Btl 034`
before this point changes the counters. [V]

**`sub_78DED0`, every operation in order** (disassembly 0x78DED0..0x78E09A): [V]

```c
for (i = 0; i < 31; ++i)                       // all 31, present or not
    a(i)[0x65D] = (u8)(3 * tick(a(i)[0x5AC]));   // computed as al + 2*al in 8 bits

low = -1;
for (i = 0; i < 18; ++i) {                     // party and aeon slots, present or not
    if      (adv == 1) v = 0;
    else if (adv == 2) v = a(i)[0x65D];
    else {
        row = ctbRow(a(i)[0x5AC]);
        r   = FFX_Rand_Stream(streamFor(i, 0));                  // see 3.1
        v   = a(i)[0x65D] - (r % (row[1] + 1));                  // idiv, r >= 0
    }
    if (a(i)[0x6BC] & 2) v = 0;                                  // First Strike
    c = sub_78C150(a(i), v, a(i)[0x613], a(i)[0x614]);          // haste / slow, clamp 0..255
    a(i)[0x65C] = c;
    if (a(i)[0xDC8] && (low < 0 || c < low)) low = c;
}
for (j = 0; j < 8; ++j) {                      // monster slots 20..27, present or not
    m = unit 20 + j;
    if      (adv == 1) v = m[0x65D];
    else if (adv == 2) v = 0;
    else {
        r = FFX_Rand_Stream(streamFor(20 + j, 0));
        v = (100 * m[0x65D]) / (100 - r % 11);                  // idiv, divisor 90..100
        if (v < 0) v = 0;
    }
    if (m[0x6BC] & 2) v = 0;
    c = sub_78C150(m, v, m[0x613], m[0x614]);
    m[0x65C] = c;
    if (m[0xDC8] && (low < 0 || c < low)) low = c;
}
for (i = 0; i < 31; ++i)
    if (a(i)[0xDC8]) a(i)[0x65C] -= (u8)low;    // byte subtract
return low;
```

```c
int sub_78C150(actor, int v, int haste, int slow) {      // propose FFX_Btl_CtbApplyHasteSlow
    if (haste) v = v / 2;                                // C division, truncates
    if (slow)  v = v * 2;
    return clamp(v, 0, 255);
}
```

Things that matter for exact reproduction:

- The draws happen for **every** slot 0..17 and every monster slot 0..7, whether or not the unit is
  in the fight, and only on a normal start. A pre-emptive or ambush start draws nothing here. [V]
- The haste and slow test is "counter non zero", so an auto-haste counter of 0xFF halves the start
  counter as well. [V]
- First Strike zeroes the value **before** haste and slow and after the draw, so it never saves a
  draw. [V]
- Slots 18, 19 and 28..30 get a base but no counter. [V]

**Revive and re-entry.** `sub_78D530` (propose `FFX_Btl_ResetUnitForReentry`) sets
`+0x65C = +0x65D`, that is plain `3 * tick` with no draw, no haste or slow scaling and no First
Strike. It runs when the KO bit clears in `FFX_Btl_ApplyPendingEffects` (via `sub_789160`), when the
Eject bit clears (`sub_7891B0`), from script `Btl 080` (`sub_7A89C0`, which also recomputes `+0x65D`)
and from `sub_7AEF20`. [V]

**Swap in.** `FFX_Btl_SwapUnitIntoPartySlot` 0x7ADAE0 sets the incoming unit's counter to 0 and
`+0x716 = 1`, and `FFX_Btl_TurnQueue_Insert` with variant 0 also zeroes the counter. The swapped in
character acts at once. [V]

### 1.4 The tick: `FFX_Btl_CtbTick` 0x790FB0

Called from `FFX_Btl_MainStep` 0x790C10 only when `FFX_Btl_IsCtbTickAllowed` 0x791190 passes and
`g_ffxBtlSubPhase == 1`. The tick-allowed test fails while any of these is non zero:
`(u8)dword_112C9D0`, `unk_112C9EA`, `g_ffxBtlPoisonPendingActor >= 0`, `(u8)dword_112C944`,
`0x112A8E4` (set by scripts `Btl 060` / `Btl 151`), `byte_112C02B`, `BYTE1(dword_112CA0A)`,
`HIBYTE(dword_112CA0A)`. [V]

```c
if (g_ffxBtlDbgCtbPause || g_ffxBtlTurnQueueCount) return 0;      // turn queue, see BATTLE_SYNC

// pass 1: who is ready
readyCount = 0;
for (i = 0; i < 31; ++i) {
    a = actor(i);
    if (a[0xDE6] == 0 && a[0xDC8] && !a[0xDCC] && !(a[0x606] & 4) && a[0x65C] == 0 && a[0xDD6]) {
        if ((MagicFile__getMagicId() == 429 || dword_112C8D8 in {0x312C, 0x4125, 0x3022,
             0x3108, 0x403C, 0x60BD}) && SBYTE1(dword_112C9D0) > 0)
            return -1;                         // hold while one of these effects plays
        push i onto g_ffxBtlCtbReadyList (0x11333C4, count 0x11333C0, cap 31)
    }
}
list = FFX_Btl_CtbReadyList_SortByCounter(&n);    // selection sort by sub_78F000, ascending
if (n > 0) {
    if (FFX_Btl_TurnQueue_Push(list[0], 0, 1, 0))  // actor, prio 0, real turn 1, variant 0
        FFX_Btl_PlayTurnStartVoice(list[0], actor(list[0]));
    return -1;
}

// pass 2: nobody ready, advance time
if (++g_ffxBtlCtbSubTick >= g_ffxBtlCtbSubTickLimit) {         // limit is 1, see below
    g_ffxBtlCtbSubTick = 0;
    for (j = 0; j < 31; ++j) {
        a = actor(j);
        if (a[0xDE6] || !a[0xDC8] || a[0xDCC] || (a[0x606] & 4)) continue;
        if (a[0x6D2] != 0xFF) a[0x6D2]++;                         // regen accumulator
        if (a[0x65C] > 1) v = g_ffxBtlDbgCtbSameOrder ? 1 : a[0x65C] - 1;
        else { v = 0; a[0xDE8] = 3; if (!a[0xDD6]) v = a[0x65D]; }
        a[0x65C] = v;
    }
}
return -1;
```

- `g_ffxBtlCtbSubTickLimit` 0x112BDE3 is written once, to 1, in `FFX_Btl_Init` 0x781B61. Nothing
  else writes it (the debug info window only reads it), so in the shipped game every call that finds
  nobody ready decrements by exactly 1. [V]
- **A unit reaching 0 gets its turn on the next call**, not the same one, because pass 2 does not
  re-run pass 1. [V]
- **At most one turn is pushed per call**, and the early-out on `g_ffxBtlTurnQueueCount` stops the
  clock until that turn is retired. Ties at 0 therefore resolve one per turn, in sort-key order,
  with no time passing between them. [V]
- Reaching 0 by decrement resets the rank to 3, so a turn that performs no action (sleep) costs a
  rank 3 delay. A unit already at 0 after the battle-start normalisation keeps whatever `+0xDE8`
  held, which is not checked. [V] / open
- A unit with `+0xDD6 == 0` (script turned its turns off) is reloaded to `3 * tick` whenever it hits
  0 and never pushed. [V]
- `g_ffxBtlDbgCtbSameOrder` sets every counter above 1 straight to 1, it does not clamp the
  decrement. [V] (BATTLE_COMMAND.md section 8 calls it "decrement clamped to 1", see corrections.)

**The sort key**, `sub_78F000` (propose `FFX_Btl_CtbSortKey`): [V]

```c
int key(int unit) {
    if (unit - 20 <= 7u) return unit + 0x10000;                       // monster slots
    return unit + ((255 - actor(unit)[0x5AC]) << 8);                  // everyone else
}
```

Lowest key acts first. So every ally (characters and aeons) beats every monster, among allies the
higher agility wins, and slot index breaks the rest. Among monsters only the slot index counts,
agility does not. Deterministic, no RNG. [V]

### 1.5 The turn itself, and on which step it begins

Order inside `FFX_Btl_MainStep` (only the CTB-relevant calls): [V]

```
FFX_Btl_CtbTick          if IsCtbTickAllowed && subPhase == 1
FFX_Btl_FinishActions    poison damage lands here (sub_7AFAB0)
sub_791760
FFX_Btl_EndPhaseStep     if IsCtbTickAllowed
FFX_Btl_SendMenu         if IsCtbTickAllowed && subPhase == 1      <- the turn begins
FFX_BtlMenu_Step
FFX_Btl_ExecCommand
...
sub_788480               post-action kinds, see 1.7
```

So the turn begins **in the same `FFX_Btl_MainStep` as the push**, in `FFX_Btl_SendMenu` 0x792A90,
unless something between them closed the tick-allowed gate. Inside `FFX_Btl_SendMenu`, for the turn
queue head that is not yet claimed: [V]

1. `sub_7AF770(actor)` (propose `FFX_Btl_RegenPendingKind`): 2 when any live unit has a regen
   counter and Zombie, 1 when any has regen, else 0. Kind 2 waits for an empty command queue, kind 1
   waits for `SBYTE1(dword_112C9D0) == 0`. A wait returns and retries next step.
2. `sub_7AF4F0(unit, actor)`, the **turn-start status tick** (section 2.4).
3. Re-reads the head. KO, or `sub_7928D0()` (the battle-over test) non zero -> retire the turn.
4. Turn-start AI event: if `+0xDF7 == 0`, set it and `FFX_Btl_RunAiAndCommit(unit, 255, 1, 0, 2)`,
   plus event 12 for an aeon.
5. If `0x112A8E4` became non zero (a script hold set by that event) return **without claiming**.
   The next time the turn is offered, step 2 runs again. See 2.4.
6. Claim: entry+3 = 1, `+0xDE4 = 0`, `+0xDF7 = 0`.
7. Doom countdown, `sub_799CD0` (section 2.4). At 0 it commits the doom command and the turn is
   that.
8. Sleep counter `+0x608` non zero -> commit an empty record (entry count 0), which the executor
   hands to the AI runner, so the turn passes [V] up to the commit, [I] for "passes". Otherwise Provoke
   (status-1 0x400) -> `sub_799D50`, Berserk (0x200) -> `sub_799AC0(.., 0)`, Confusion (0x100) ->
   `sub_799AC0(.., 1)`, else `FFX_Btl_IsActorAiControlled` decides menu or AI. Yojimbo's RNG roll
   lives in that call, so it is skipped on any of the forced paths above.
9. `+0x716 = 0` after any of the dispatches.

### 1.6 Rank and the delay

**Rank.** `FFX_Btl_ApplyActionToTargets` 0x7892E0 stores `sub_7895B0(cmd)` (propose
`FFX_Btl_GetCommandRank`) into `+0xDE8` when the action executes: [V]

```c
int rank(cmd) {
    rec = 0;
    for (w = 0; w < 2; ++w) {                       // entry 0: +0 id, then +2 sub id
        id = cmd.entry0[w];  if (id == 0xFF) continue;
        if (actor(cmd[0])[0x6DE]) id = 0x3028;      // the forced-ability flag
        r = FFX_Btl_ResolveAbilityId(cmd[0], 0, -1, {id, 0xFF}, 0);
        stop = (id == 0x3029 || id == 0x305F || id == 0x311E);
        if (r) rec = r;
        if (stop) break;
    }
    if (!rec || rec[36] == 0) return 3;
    return rec[36];
}
```

So the rank is byte **+36 (0x24)** of the ability record, from the sub id's record when there is
one, except that ids 0x3029, 0x305F and 0x311E stop at their own record. A missing record or a 0
byte means 3. Shipped examples: Attack 3, Quick Hit 2, Delay Attack 6, Delay Buster 8, the Breaks 4,
Haste 4, Hastega 6, Defend 2. [D]

**The delay is added when the turn is retired**, not at commit. The retirement is
`FFX_Btl_TurnQueue_RemoveAt(unit, +0xDE4, 1, 0)` from the effect-completion path `sub_78D980`
(0x78DABD). For an entry whose variant byte is 0 (a real CTB turn), in this order: [V]

```c
actor[0x65C] += sub_78D1D0(actor, actor[0xDE8], actor[0x613], actor[0x614]);  // byte add, wraps
sub_7AF390(unit, actor);              // turn-end status countdown, AFTER the delay (2.4)
FFX_Btl_SpendCommandCost(unit);
if (a3 && (actor[0x606] & 8) && !actor[0xDCC] && actor[0xDC8] && !actor[0xDF8])
    g_ffxBtlPoisonPendingActor = unit;           // poison, which also stops the CTB until applied
...
FFX_Btl_RunAiAndCommit(unit, 255, 6, 0, 0, 0);  // "turn ended" AI event

int sub_78D1D0(actor, int rank, int haste, int slow) {        // propose FFX_Btl_CtbDelayForRank
    if (rank < 1) rank = 1;
    return sub_78C150(actor, rank * tick(actor[0x5AC]), haste, slow);
}
```

So: `delay = clamp(hasteSlow(max(rank,1) * tick), 0, 255)`, where hasteSlow is `/2` (truncating)
then `*2`, using the haste and slow counters **before** this turn decrements them. Variant 1 entries
(counters and extra turns) add no delay and tick no statuses. The add is `add [esi+65Ch], al`,
so a counter that was already non zero when the turn ends (hit by a delay during its own turn)
wraps modulo 256 instead of clamping. [V]

Agility is read live on every delay, so a mid-battle agility change takes effect at the next delay.
`+0x65D` is not recomputed then. [V]

**Haste and slow do not touch the current counter when applied.** Applying haste or slow only
writes the status counters (2.3). The counter changes from the next delay on. [V]

### 1.7 Other writers of the counter

| writer | what | |
|---|---|---|
| `FFX_Btl_ApplyCtbDelay` 0x78E1E0 | `+0x65C = clamp(+0x65C + delta, 0, 255)`, from `FFX_Btl_ApplyPendingEffects` slot 2. Not gated by invincibility | [V] |
| `sub_78E0F0` (propose `FFX_Btl_AddDelayAttack`) | ability dword +28 bit 0x4000 (Delay Buster) adds `2*3*tick/2`, bit 0x2000 (Delay Attack) adds `1*3*tick/2`, evaluated left to right in int, **target's** agility. Delay Attack is `floor(3*tick/2)`, Delay Buster `3*tick` | [V], names [D] |
| `FFX_Btl_RefuseCtbOnThreatened` | a target with status-1 Threaten (0x800) gets the CTB slot zeroed, delays do not land on it | [V] |
| `sub_78AE00` Threaten | CTB delta = `attacker.ctb + delay(attacker, attacker.rank) - target.ctb`, so the target lands exactly on the attacker's next turn | [V] |
| `FFX_Btl_DamageFormula` kind 4 | formulas that damage the CTB read `+0x6EC` as current and `+0x65D` as max (cases 5, 8, 11, 13) | [V] |
| `sub_7B1AA0` (post-action kind 8) | the acting unit's counter = 0, and each other unit in `g_ffxBattlePartyOrder` gets `+0x65C = delay(own tick, acting unit's rank)` (assignment, not add). Kind 8 comes from command record +6 via `FFX_Btl_ExecCommand`. Which command uses it is not known | [V] / [I] |
| `FFX_Btl_TurnQueue_Insert` | variant 0 insert zeroes the counter | [V] |
| `sub_7B4B70` case 232 | script sets the counter directly | [V] |

### 1.8 The turn order strip: `FFX_Btl_BuildCtbPreview` 0x79A200

Called by the menu draw code (`sub_89ECE0`, `sub_8A0CE0`). Output is `byte_112AA00`, 32 rows of 4
bytes: `+0 u8 unit` (0xFF empty), `+2 u16 counter`. 0x112AA00 + 128 = 0x112AA80, the turn queue, so
it is the block right in front of it. [V] It is **a separate computation** from the real queue: [V]

```c
pend = -1; pendRank = 3;
if (g_ffxBtlCmdPreviewValid) { pend = g_ffxBtlCmdPreview[0]; pendRank = sub_7895B0(g_ffxBtlCmdPreview); }
for (i = 0; i < 31; ++i) {
    h[i] = a(i)[0x613];  s[i] = a(i)[0x614];
    c[i] = (!a(i)[0xDC8] || !a(i)[0xDD7] || a(i)[0xDCC] || (a(i)[0x606] & 4)) ? -1 : a(i)[0x65C];
}
row = 0;
if (pend >= 0 && c[pend] >= 0) {
    out[0] = (pend, c[pend]);
    sub_7898D0(preview cmd, c, pendRank, h, s);   // the pending command's CTB effects on targets
    advance(pend, pendRank); row = 1;
}
loop {
    m = min of c[i] >= 0;  ready = { i : c[i] == m };  sort ready by sub_78F000;
    if (empty) { fill the remaining rows with (0xFF, 0); break; }
    for each i in ready: out[row++] = (i, c[i]); advance(i, 3); if (row >= 32) done;
}
advance(i, rank): c[i] += sub_78D1D0(a(i), rank, h[i], s[i]);
                  if (h[i] && h[i] < 0xFE) h[i]--;  if (s[i] && s[i] < 0xFE) s[i]--;
```

Differences from the real queue, each one a place the strip can lie:

- it uses `+0xDD7` (shown) where the tick uses `+0xDD6` (takes turns) [V]
- counters are ints with no byte wrap [V]
- every future turn is rank 3 [V]
- it does not model regen, poison, doom, sleep, KO, revives or the reload of non-turn units [V]
- for the pending command it does model Delay Attack and Buster, Threaten, Haste and Slow on the
  targets and CTB-kind damage formulas, through `sub_7898D0`, which calls `FFX_Btl_DamageFormula`
  with the random flag 0, so the preview **never draws RNG** [V]

With only rank 3 turns and no wrap the two agree turn for turn, which `ctb.py selftest` checks over
300 random parties. [V by the model]

---

## 2. Status effects

### 2.1 The 25 rollable statuses and the bit layout

`sub_78AE00` (propose `FFX_Btl_RollStatus1AndCounters`) walks 25 statuses with a rotating bit. Index
0..11 are bits 0..11 of the **status-1 word** `+0x606`, index 12..24 are the 13 **status counters**
`+0x608 + (index - 12)`. The ability record carries a chance per index at **+46 + index** and a
duration per counter at **+71 + (index - 12)**. [V] The names come from the shipped ability data:
the ability called "Sleep Attack" has chance at index 12 and duration at counter 0, "Bio" index 3,
"Haste" index 23 and counter 11, and so on for every row below. [D]

| idx | status-1 bit / counter | name | behaviour seen in code |
|---|---|---|---|
| 0 | `+0x606` 0x0001 | KO (Death) | `FFX_Btl_OnUnitDefeated` sets it, revive path on clear [V] |
| 1 | 0x0002 | Zombie | regen and healing invert [V] |
| 2 | 0x0004 | Petrify | no CTB, applying it replaces the word with 0x4 and zeroes all counters, on a monster also KO + Eject [V] |
| 3 | 0x0008 | Poison | turn-end damage `maxHP * +0x5BA / 100` [V] |
| 4 | 0x0010 | Power Break | halves damage class 1 [V] |
| 5 | 0x0020 | Magic Break | halves damage class 2 [V] |
| 6 | 0x0040 | Armor Break | [D] only |
| 7 | 0x0080 | Mental Break | [D] only |
| 8 | 0x0100 | Confusion | forced action, clears 0x200/0x400/0x800 on apply [V] |
| 9 | 0x0200 | Berserk | forced action, clears 0x100/0x400/0x800 [V] |
| 10 | 0x0400 | Provoke | provoker unit into `+0x5C4`, clears 0x100/0x200/0x800 [V] |
| 11 | 0x0800 | Threaten | threatener into `+0x5C5`, CTB alignment, clears haste and slow [V] |
| 12 | `+0x608` | Sleep | turn passes with an empty command [V] |
| 13 | `+0x609` | Silence | [D] |
| 14 | `+0x60A` | Darkness | [D] |
| 15 | `+0x60B` | Shell | [D] |
| 16 | `+0x60C` | Protect | [D] |
| 17 | `+0x60D` | Reflect | [D] |
| 18 | `+0x60E` | NulTide | [D] |
| 19 | `+0x60F` | NulBlaze | [D] |
| 20 | `+0x610` | NulShock | [D] |
| 21 | `+0x611` | NulFrost | [D] |
| 22 | `+0x612` | Regen | turn-start heal [V] |
| 23 | `+0x613` | Haste | CTB / 2 [V] |
| 24 | `+0x614` | Slow | CTB * 2 [V] |

Bits 12..15 of `+0x606` are never touched by the status loop. [V]

**The status-2 word `+0x616`**, applied by `sub_78B4E0` (propose `FFX_Btl_ApplyStatus2`) from a
16 bit mask in the ability (no chance roll, it lands unless immune). The names come from the
abilities and items that carry exactly one bit, the behaviour from the code: [D] [V]

| bit | name | evidence |
|---|---|---|
| 0x0001 | Scan | ability "Scan" [D]. The only bit that can be re-applied while set [V] |
| 0x0002 | Distill Power | item "Power Distiller" [D] |
| 0x0004 | Distill Mana | "Mana Distiller" [D] |
| 0x0008 | Distill Speed | "Speed Distiller" [D] |
| 0x0010 | (unused) | no shipped carrier found, in the same exclusive group as the distills [V] |
| 0x0020 | Distill Ability | "Ability Distiller" [D]. Bits 0x02..0x20 are mutually exclusive on apply [V] |
| 0x0040 | Shield | "Shield" [D]. Damage taken / 4 (`FFX_Btl_ApplyShieldAndBoost`) [V]. Cleared at own turn start |
| 0x0080 | Boost | "Boost" [D]. Damage taken * 3 / 2 [V]. Cleared at own turn start |
| 0x0100 | Eject | Bribe success and petrify shatter set it [V], `FFX_Btl_OnUnitDefeated` treats it as a non-HP death cause [V], applying it clears Threaten [V] |
| 0x0200 | Auto-Life | "Auto-Life" [D]. `+0x5CA` armed flag [V] |
| 0x0400 | Curse | "Curse", "Pharaoh's Curse" [D]. Blocks overdrive abilities (`FFX_Btl_GetCommandMpCost`) [V] |
| 0x0800 | Defend | "Defend" [D]. Cleared at own turn start [V] |
| 0x1000 | Guard | "Guard" [D]. Cleared at own turn start [V] |
| 0x2000 | Sentinel | "Sentinel" [D]. Cleared at own turn start [V] |
| 0x4000 | Doom | "Doom" [D]. Countdown `+0x5C8` from `+0x5C9`, the self-targeted 0x3120 command at 0 [V] |
| 0x8000 | unknown | kept by the petrify mask 0x813F [V] |

### 2.2 Other status fields

| off | type | field | evidence |
|---|---|---|---|
| +0x5BA | u8 | poison damage percent of max HP | `sub_7AFAB0` [V] |
| +0x5C4 | u8 | provoker unit | `sub_78AE00` [V] |
| +0x5C5 | u8 | threatener unit (on the target) | `sub_78AE00`, `sub_78E460` [V] |
| +0x5C6 | u8 | threaten target unit (on the threatener) | `sub_78E460` [V] |
| +0x5C8 | u8 | doom count, live. Script property 160 | `sub_799CD0` [V] |
| +0x5C9 | u8 | doom start count, copied to +0x5C8 on apply. Script property 159 | `sub_78B4E0` [V] |
| +0x5CA | u8 | Auto-Life armed | `sub_78B4E0`, `FFX_Btl_ScaleMagicBooster` [V], meaning [I] |
| +0x5DE | u8[25] | weapon-borne status chances (the touch and strike auto-abilities) | `sub_78AE00` with ability dword +28 bit 0x40000 [V] |
| +0x5F7 | u8[13] | weapon-borne durations | same [V] |
| +0x606 | u16 | status-1 word | [V] [D] |
| +0x608 | u8[13] | status counters | [V] [D] |
| +0x616 | u16 | status-2 word | [V] [D] |
| +0x618 | u16 | status-1 saved under auto-statuses | `sub_79B290` / `sub_79B1A0` [V] |
| +0x61A | u8[13] | counters saved under auto-statuses | same [V] |
| +0x628 | u16 | status-2 saved | same [V] |
| +0x62A | u16 | permanent status-1 mask. Cannot be removed, blocks the confusion group | `sub_79B290`, `sub_78AE00` [V] |
| +0x62C | u16 | permanent counter mask, the counter is set to 0xFF | `sub_79B290` [V] |
| +0x62E | u16 | permanent status-2 mask. Also stops the turn-start clears | `sub_7AF4F0`, `sub_78B4E0` [V] |
| +0x630/+0x632/+0x634 | u16 x3 | base auto masks (equipment or monster data) | `sub_79B290` [V], source [I] |
| +0x636/+0x638/+0x63A | u16 x3 | extra masks added when `+0x63E` is 1 or 2 | `sub_79B290` [V], that this is the SOS (low HP) set [I] |
| +0x641 | u8[25] | per status resistance, 0xFF immune. For Threaten it is a susceptibility, see 2.3 | `sub_78AE00` [V] |
| +0x65A | u16 | status-2 immunity mask | `sub_78B4E0` [V] |

### 2.3 Application, durations and the rolls

Statuses are not written to the actor when an action resolves. `FFX_Btl_ComputeEffectForTarget`
0x78E630 stages them into the target's pending entry (+0x07 counters, +0x14 status-1, +0x16
status-2) and `FFX_Btl_ApplyPendingEffects` 0x78F060 later copies the whole lot over `+0x606`,
`+0x608..0x614` and `+0x616` in one go, after the HP, MP and CTB slots. If regen went from 0 to non
zero in that copy, `+0x6D2` is reset to 0. [V]

**Read from the 13.7 port (2026-10-29, Project Zanarkand `src/ffx/btl/Status`), missing below:**

- The draw comes before every test. The only path without a draw is a removal ability (0x78AE8D,
  0x78AE9B). Immune, petrified, already held and Haste against Slow are all tested after it. [V]
- "Already holds it" reads the LIVE target (0x78B03C `+0x606`, 0x78B337 `+0x608+k`, 0x78B5E6
  `+0x616`), the Petrify tests read the STAGED hit (0x78B02D, 0x78B2C9). [V per the port, the
  coordinator checked the draw order and not each read]
- A Haste or Slow that fails or is refused zeroes the staged CTB amount: `var_14 & 0x1800` at
  0x78B49E, then the store of 0 at 0x78B4B3. [V]
- Threaten needs `permStatus1 & 0xF00 == 0` and staged Haste and Slow not 0xFF (0x78B12E). It sets
  the threaten-landed local (0x78B15D) and flags |= 4 (0x78B1A1). Its CTB uses
  `FFX_Btl_CtbDelayForRank` with the attacker's `+0xDE8` rank. [V per the port]
- Petrify on a monster, or on anyone while `g_ffxBtlFieldAirborne` is set, adds KO and Eject
  (0x78B256..0x78B280, the airborne compare at 0x78B267) after masking status 2 with 0x813F at
  0x78B24D. So party members shatter in an airborne fight too. [V]
- The HitMiss debug byte also fails removals (0x78AF6F) and every status 2 bit (0x78B560). [V per
  the port]
- `FFX_Btl_ApplyStatus2` returns its arg_20 flags unchanged (0x78B752), and arms Auto-Life through
  `FFX_Btl_ScaleMagicBooster(attacker, ability, 0, 0)` at 0x78B6EF. [V]
- In the 2.4 regen formula the `(maxHp * +0x6D2) >> 8 != -100` guard is an unsigned compare, so it is
  always true. [per the port]

**`sub_78AE00`, per status index k = 0..24**, `chance = rec[46+k]`, raised to the attacker's
`+0x5DE+k` when the ability has dword +28 bit 0x40000: [V]

```
if chance == 0: skip
if rec[32] & 0x20:                       removal ability (Esuna, Dispel, Life)
    no draw. A flag status is cleared unless permanent (+0x62A). A counter that is not 0 or 0xFF
    has the duration subtracted, clamped at 0 (removal durations are 254, so it clears).
    On a petrified target only the petrify bit itself can be removed. Life (k 0) on a Zombie
    target sets KO instead of clearing it, unless +0x5B8 bit 0x4 is set
else if k == 11 (Threaten):
    r = Rand(attackerStream kind 2) % 100
    hit when r < target[0x641+11] (0 means immune). On a hit the byte becomes max(1, 7*byte/10)
else:
    r = Rand(attackerStream kind 2) % 101
    res = target[0x641+k], for k == 0 on a Zombie target res = 0xFE
    hit when chance == 0xFF,  or res != 0xFF and (chance == 0xFE or r < chance - res
                                                   or g_ffxBtlDbgDmgProbOff)
```

So **one draw per status with a non zero chance**, in index order, taken before the immunity test,
skipped only for removal abilities. A chance of 254 still draws. [V]

On a hit, for the counter statuses (k >= 12, counter c = k - 12):

- skipped if the target is petrified or the counter is already non zero (no refresh) [V]
- duration = `rec[71+c]`, raised to the attacker's `+0x5F7+c` for weapon-borne statuses [V]
- **self cast +1**: when attacker == target, duration < 253, and the counter's table flag has bit
  0x4 (it counts down at turn end), duration + 1, so the caster's own turn end does not eat a
  turn [V]
- Sleep (c 0) also clears status-2 0x800 (Defend) [V]
- Haste (c 11) requires haste and slow not 0xFF and the target not Threatened, and zeroes slow.
  Slow (c 12) is the mirror image [V]

Shipped durations worth knowing: Sleep, Silence and Darkness attacks 3, Regen 10, the Nul spells 1,
and Haste, Slow, Shell, Protect, Reflect **254**, which never counts down (2.4), so in practice
those last the battle. [D]

**`sub_78B4E0`** first, when the staged target is petrified: `Rand(attackerStream kind 2) % 101 <
rec[44]` adds KO and Eject (the shatter). Then each set bit of the ability's status-2 mask, in bit
order, with no draw. [V]

### 2.4 When each counter ticks

The per-counter behaviour comes from a 13 x 4 byte table at **0xC42464** (`unk_C42464`, flags in
byte +3, read as `byte_C42467[4*c]`). [V] Values (game constants in the exe, a short table):

| c | status | +3 flags | ticks |
|---|---|---|---|
| 0 | Sleep | 0xAC | turn end, notify |
| 1 | Silence | 0xCC | turn end, notify |
| 2 | Darkness | 0xCC | turn end, notify |
| 3 | Shell | 0x14 | turn end |
| 4 | Protect | 0x14 | turn end |
| 5 | Reflect | 0x14 | turn end |
| 6..9 | NulTide, NulBlaze, NulShock, NulFrost | 0x00 | never by turns |
| 10 | Regen | 0x03 | turn start, notify |
| 11 | Haste | 0x0C | turn end, notify |
| 12 | Slow | 0x8C | turn end, notify |

Flag bits: 0x1 count down at turn start, 0x2 notify (`sub_79EF40`) on that, 0x4 count down at turn
end, 0x8 notify on that. [V] Bits 0x10, 0x20, 0x40, 0x80 are returned by `sub_79E330` to the AI hit
tally in `sub_78AE00`, their meaning is not pinned. [I] A count down only happens when the counter is
1..253 (`counter - 1 <= 0xFC` unsigned), so 0 stays 0, 254 and 255 are permanent. [V]

**Per CTB tick.** Every pass-2 call of `FFX_Btl_CtbTick` adds 1 (saturating at 0xFF) to `+0x6D2` on
every live unit. Nothing else ticks per CTB tick. [V]

**At the start of the unit's own turn**, `sub_7AF4F0` (propose `FFX_Btl_TurnStartStatusTick`), only
when `+0xDC8` and `+0x716 == 0`: [V]

1. Regen for **every** live unit, not just the one whose turn it is: for each unit i in 0..30 with
   regen counter non zero, present, current HP > 0, not KO, not petrified:
   `amount = ((maxHP(+0x594, u32) * +0x6D2) >> 8) + 100` in unsigned 32 bit. Zombie (status-1 0x2)
   takes it as damage and can die of it, otherwise it heals. Then `+0x6D2 = 0`. The guard
   `amount - 100 != -100` in the code is always true. [V]
2. This unit's counters with flag 0x1 count down (only Regen). [V]
3. Status-2 Defend 0x800, Guard 0x1000, Sentinel 0x2000, Shield 0x40, Boost 0x80 are cleared unless
   permanent. Defend, Sentinel and Shield also call `sub_7AF840`. [V]
4. `sub_78E410`: if this unit carries the Threaten link bit, the link is released on both ends
   (`sub_78E460`). Threaten therefore lasts until the threatener's next turn starts. [V]

Then, after the claim, **doom** (`sub_799CD0`, propose `FFX_Btl_StepDoomCountdown`): if status-2
0x4000 and `+0x716 == 0`, `+0x5C8` counts down if non zero, and when it reads 0 the unit commits
`{unit, count 1, id 0x3120 sub 0xFF, mask 1 << unit}`. [V]

A known double-tick: when the turn-start AI event (step 4 of 1.5) sets the script hold `0x112A8E4`,
`FFX_Btl_SendMenu` returns without claiming, and once the hold clears the whole of `sub_7AF4F0` runs
again for the same turn (`+0x716` is still 0). Regen then heals a second time with `+0x6D2 = 0`
(so exactly 100, or 100 damage on a zombie) and the regen counter loses a second turn. [V] from the
control flow. Whether a shipped monster script does this is not checked.

**At the end of the unit's own turn** (variant 0 only), `sub_7AF390` (propose
`FFX_Btl_TurnEndStatusTick`), called from `FFX_Btl_TurnQueue_RemoveAt` right after the CTB delay is
added: counters with flag 0x4 count down. If Sleep went to 0 the wake event fires
(`sub_7AB360(unit, 27, ...)`), if Silence or Darkness went to 0 `sub_78CA20(unit, 2, 1)`. [V]

**Poison** at turn end: `FFX_Btl_TurnQueue_RemoveAt` latches `g_ffxBtlPoisonPendingActor` (a byte
at 0x112C9E4, -1 idle) which closes the CTB gate. `FFX_Btl_FinishActions` calls `sub_7AFAB0`
(propose `FFX_Btl_ApplyPoisonDamage`): `dmg = maxHP(u32) * +0x5BA / 100` unsigned, applied as HP
damage, then AI event 8, then the KO test. [V]

**NulTide..NulFrost** never count down by turns (flags 0). A hit consumes them in `FFX_Btl_ConsumeNulElements`
(proposed `FFX_Btl_ConsumeNulElements`), called first in `FFX_Btl_ComputeEffectForTarget`, on the
staged hit's counters: fire pairs with NulBlaze (entry +0x0E), thunder with NulShock (+0x0F), ice
with NulFrost (+0x10), water with NulTide (+0x0D). The hit is nulled only when every one of those
four element bits the attack carries is held, so fire plus ice on a NulBlaze target goes through,
while fire plus holy is nulled whole (the other bits are not counted). A nulled hit decrements each
held counter that is below 254, draws nothing, sets entry +0x03 to 2 and the number kind to 2. [V]

### 2.5 Which statuses change the CTB

| status | effect | |
|---|---|---|
| Haste | every delay `/ 2` (truncating), the start counter too | [V] |
| Slow | every delay `* 2`, the start counter too | [V] |
| Petrify | removed from the tick, the ready list and the strip | [V] |
| KO | same, and a revive restarts at `3 * tick` | [V] |
| Eject | the unit leaves (`+0xDC8 = 0`), re-entry restarts at `3 * tick` | [V] |
| Threaten | target counter set to the threatener's next counter, immune to delays while it lasts, cannot be hasted or slowed | [V] |
| Sleep | the turn still comes, does nothing, costs rank 3 | [V] |
| Doom | at 0 the turn becomes the doom command | [V] |
| Poison, Zombie+Regen | stall the CTB while their damage resolves (gate and regen wait) | [V] |
| First Strike (auto-ability) | start counter 0 | [V] [D] |
| Initiative (auto-ability) | changes the advantage roll only | [V] [D] / [I] link |

---

## 3. Every RNG draw on these paths

`FFX_Rand_Stream` 0x7988F0: `s = mul[i] * state[i]` (32 bit), `s ^= xor[i]` (u16),
`state[i] = (s sar 16) + (s shl 16)`, return `state & 0x7FFFFFFF`. Tables at 0xC42208 (68 dwords)
and 0xC42318 (68 words), state at 0x1135EE0. [V]

### 3.1 The per-unit stream map, `sub_78D210(unit, kind)` (propose `FFX_Btl_RandStreamForUnit`)

```c
int s = (kind == 1) ? 16 : (kind == 2) ? 32 : 0;
if (unit - 20 <= 7u)        return s + unit + 8;    // monster slot 20..27 -> 28..35 (+16, +32)
if (actor(unit)[0x590] & 4) return s + 27;          // FFX_Btl_IsSummonUnit
return s + unit + 20;                                // characters 0..6 -> 20..26
```

| | kind 0 | kind 1 | kind 2 |
|---|---|---|---|
| character 0..6 | 20..26 | 36..42 | 52..58 |
| any summon-flagged unit (aeons) | 27 | 43 | 59 |
| monster slot 20..27 | 28..35 | 44..51 | 60..67 |

Slot 7 maps to 27 either way. A slot 8..17 **without** the summon flag would map to 28..37, which
collides with the monster streams. [V] Whether every aeon slot always has the flag at battle start
is not checked. Open.

### 3.2 The draws, in the order they happen

| # | when | site | stream | count | use |
|---|---|---|---|---|---|
| 1 | field step | `FFX_Field_StepRandomEncounter` 0x780EEA / 0x780EFD | 0, 1 | 1 + 1 | encounter roll, formation (RANDOM_ENCOUNTER.md) |
| 2 | `FFX_Btl_StepSubPhase` case 34 -> 10 | `FFX_Btl_RollPreemptiveOrAmbush` 0x78D098 | **1** | 1, always, also when the advantage is forced | `& 0xFF` against 32 / 223 |
| 3 | `FFX_Btl_RegisterMonsterMotions` | `sub_7842F0` 0x784379 / 0x7843C0 | monster kind 0 (28..35) | for each slot v (20..27) with `+0x10` and `+0x11` set: n = 1 + later slots whose `+0x14` is v. If n > 1, n draws `& 3` on v's stream, then for each slot v..27 whose `+0x14` is v one draw `% remaining` on that slot's own stream | a shuffled value into `+0x424` per member, purpose [I] |
| 4 | same call, right after | `sub_78DED0` 0x78DF3E | party kind 0: 20..26 for slots 0..6, then 27 for slots 7..17 (11 draws) | 18, normal start only | `% (range + 1)` |
| 5 | same | `sub_78DED0` 0x78DFE7 | 28..35 | 8, normal start only | `% 11` |
| 6 | each turn start of unit 14 that reaches the menu or AI decision | `FFX_Btl_IsActorAiControlled` 0x792137 | **17** | 1 | `& 0xFF` vs Yojimbo's motivation / 4 |
| 7 | each action, per target, damage part | `FFX_Btl_RollHit` 0x78A942 (kind 1), `FFX_Btl_DamageFormula` 0x789C9E (kind 0), `FFX_Btl_RollCritical` 0x7896B3 (kind 0) | attacker | per staged hit, in this order: the hit roll `% 101`, then the HP variance, the crit `% 101` (HP part, ability +0x20 bit 4), the MP variance, the CTB variance. The hit roll is skipped for hit kind 0, a staged sleep counter or Petrify, an ability with +0x1C bit 0x800000 on a live target (no effect), a fully nulled element and the HitMiss debug byte. It is drawn before the Evade and Counter test, so that miss still draws. Each variance is drawn when its part bit is set and power != 0, BEFORE the formula switch, so the fixed and fraction formulas draw too. A miss or no effect draws only the hit roll | hit, variance, crit |
| 8 | each action, per target, after the damage | `sub_78AE00` 0x78AEBE / 0x78AEF9 | attacker kind 2 | 1 per status index with chance != 0, index order, not for removal abilities | `% 101`, Threaten `% 100` |
| 9 | right after | `FFX_Btl_ApplyStatus2` 0x78B4FD | attacker kind 2 | 1 if the STAGED status 1 holds Petrify after row 8 (test at 0x78B4EC), so a hit that lands Petrify also draws it | `% 101 < rec[44]` shatter |
| 10 | right after | `FFX_Btl_RollBribe` 0x78BE1E | **target** kind 2 (corrected 2026-10-29: 0x78EBF5 pushes the target index as arg_0, and 0x78BE04 passes arg_0 to the stream pick) | 1 for a bribe (ability dword +28 bit 31), drawn before the bribe immunity and Sleep tests | `& 0xFF` vs gil ratio |
| 11 | `FFX_Btl_ApplyPendingEffects` | 0x78F65C / 0x78F681 | **9** | 0 or 1, only when the reaction kind is 5, 6 or 8 and flag 0x20 is set | reaction animation pick |

None in: `FFX_Btl_CtbTick`, `FFX_Btl_BuildCtbPreview` (its damage formula call passes random 0),
`sub_7AF4F0` (regen), `sub_7AF390`, `sub_7AFAB0` (poison), `sub_799CD0` (doom), the haste and slow
maths, `FFX_Btl_PlayTurnStartVoice`, `sub_797A40`. [V] Caveat found by the 13.7 port (2026-10-29): that
holds for the regen and poison bodies only. A zombie regen victim and a poison victim that die go
through `FFX_Btl_OnUnitDefeated(i, i, 0, 1)`, which with a3 = 0 reaches `FFX_Btl_PayKillRewards` at
0x78C952 and `FFX_Btl_RunAiAndCommit(victim, killer, 4)`. The reward draws are at 0x799178, 0x7991DF,
0x799282, 0x7992B2, 0x79936B and in the equipment drop at 0x798C76, 0x798CB4, 0x798CF3, 0x798D00 and
0x798DBA. Answered by the 13.13 port (2026-10-29, checked in IDA): such a kill pays and draws. The
turn start tick (0x7AF5CC), the poison tick (0x7AFB3C) and `FFX_BtlAi_SetProperty` (0x7B6072) all
pass a3 = 0, the test at 0x78C935 then reaches the call at 0x78C952, and PayKillRewards' only gate
is the record pointer at 0x7990DA. A monster self kill pays its full record. A party self kill banks
0 AP and 0 gil but still draws stream 10 three times (0x799282 twice, 0x79936B). A normal hit's
`FFX_Btl_ApplyPendingHits` passes a3 = 1 and does not pay there.

Two lockstep consequences:

- **The attacker's kind 0 stream is shared** between its battle-start ICV draw (row 4 or 5) and every
  damage variance draw it makes later. A peer that skipped or added one ICV draw would diverge on
  every hit that unit lands. [V]
- **Draw counts depend on state that is not on screen.** Row 4 draws for absent slots, row 8 draws
  even when the target is immune, row 6 is skipped when Yojimbo is asleep, provoked or confused.
  Matching the draw order means running the same code paths, not counting visible events. [V]

---

## 4. One battle, worked through the model

`tools/ctb.py sim --party Tidus:10,Wakka:6,Auron:5 --enemies Piranha:3,Piranha:3,Condor:12
--haste Tidus=254 --rank Auron=4,3 --no-rng` (every draw forced to 0, so this is the largest
party ICV and the smallest monster ICV, not a real battle):

```
base 3*tick: Tidus 42, Auron 48, Wakka 48, Piranha 72, Piranha 72, Condor 39
start (haste halves Tidus): 21, 48, 48, 72, 72, 39 -> minus 21 -> 0, 27, 27, 51, 51, 18
turn 1 Tidus  delay 42/2 = 21 (rank 3, tick 14, hasted)
turn 2 Condor after 18 more decrements, delay 39
turn 4 Wakka and turn 5 Auron tie at 0, Wakka first (agility 6 beats 5), Auron's rank 4 costs 64
turn 7 and 8 the two Piranhas tie, slot 20 before slot 21 (monsters by slot only)
```

**Still open for 1.11.7: no recorded battle has been checked against this.** The test needs a
capture of `+0x65C`, `+0x65D`, `+0x613`, `+0x614` and `+0xDE8` for units 0..30 on the step the
battle reaches sub phase 1, the stream states 20..35 before `FFX_Btl_RegisterMonsterMotions`
runs, and the turn queue pushes after that. `ctb.py sim --state` takes the stream states.

---

## 5. Corrections to other docs

Appended as dated notes to the docs themselves:

- **BATTLE_COMMAND.md**: `+0x616` bit 0x4000 is Doom, not confuse, and `sub_799CD0` with id 0x3120 is
  the doom countdown, not the confusion self attack. Confusion is status-1 `+0x606` bit 0x0100.
  `+0x606` value 0x4 is Petrify and `+0xDCE` mirrors it, neither is "escaped". The CTB reload from
  `+0x65D` only happens for units with `+0xDD6 == 0`. `g_ffxBtlDbgCtbSameOrder` sets counters above
  1 to 1, it is not a decrement clamp. `sub_78F000` is settled (deterministic).
- **CHEAT_BATTLE.md**: `+0x616` bit 0x0100 is Eject, not Death, and bit 0x4000 is Doom.
  `+0x606` 0x0004 "removed from the field" is Petrify.
- **RANDOM_ENCOUNTER.md**: the `record+0x4A` bit 2 test does not force a pre-emptive strike, it
  lowers the roll by 33 (pre-emptive 65/256, ambush impossible), and the ability is Initiative.

Also wrong in IDB comments (not edited, IDA is read only here): the `FFX_Btl_CtbTick` comment
("reaching zero ... reloads from +0x65D"), the `FFX_Btl_SendMenu` comment ("confuse timer ->
0x799CD0") and the `FFX_Btl_RollPreemptiveOrAmbush` comment (still calls itself an encounter roll
and the bit No Encounters). Answer to an open item in ASYNC_LOAD_REQUESTS.md: `sub_797A40` draws no
RNG, it appends a name to a small per-kind table.

---

## 6. Proposed IDB names

| VA | current | proposed | evidence |
|---|---|---|---|
| 0x78DED0 | sub_78DED0 | `FFX_Btl_InitCtbCounters` | writes `+0x65D`, `+0x65C` for all units from the advantage byte, 1.3 |
| 0x78C150 | sub_78C150 | `FFX_Btl_CtbApplyHasteSlow` | `/2` on arg 3, `*2` on arg 4, clamp 0..255 |
| 0x78D1D0 | sub_78D1D0 | `FFX_Btl_CtbDelayForRank` | `max(rank,1) * tick` through the above |
| 0x7909C0 | sub_7909C0 | `FFX_Btl_GetTickSpeed` | byte 0 of the ctb_base row |
| 0x790990 | sub_790990 | `FFX_Btl_GetCtbBaseRow` | the row pointer, byte 1 is the ICV range |
| 0x78F000 | sub_78F000 | `FFX_Btl_CtbSortKey` | the ready list and strip key |
| 0x78D210 | sub_78D210 | `FFX_Btl_RandStreamForUnit` | unit + kind -> stream 20..67 |
| 0x7895B0 | sub_7895B0 | `FFX_Btl_GetCommandRank` | ability record +36, default 3 |
| 0x78E0F0 | sub_78E0F0 | `FFX_Btl_AddDelayAttackCtb` | record dword +28 bits 0x2000 / 0x4000 |
| 0x78BD50 | FFX_Btl_RefuseCtbOnThreatened | `FFX_Btl_ThreatenBlocksCtbDelta` | zeroes the CTB slot on a threatened target |
| 0x78AE00 | sub_78AE00 | `FFX_Btl_RollStatus1AndCounters` | the 25 status loop, kind 2 draws |
| 0x78B4E0 | sub_78B4E0 | `FFX_Btl_ApplyStatus2` | 16 bit loop over `+0x616`, the shatter roll |
| 0x78BDD0 | sub_78BDD0 | `FFX_Btl_RollBribe` | gil ratio vs a kind 2 draw, sets Eject |
| 0x789290 | FFX_Btl_ApplyBreaks | `FFX_Btl_ApplyBreakHalving` | status-1 0x10 / 0x20 halve by damage class |
| 0x78C530 | FFX_Btl_ApplyShieldAndBoost | `FFX_Btl_ApplyShieldBoost` | status-2 0x40 `/4`, 0x80 `*3/2` |
| 0x7AF4F0 | sub_7AF4F0 | `FFX_Btl_TurnStartStatusTick` | regen for all, flag 0x1 countdown, one-turn clears |
| 0x7AF390 | sub_7AF390 | `FFX_Btl_TurnEndStatusTick` | flag 0x4 countdown, wake event |
| 0x7AF770 | sub_7AF770 | `FFX_Btl_RegenPendingKind` | 2 zombie regen, 1 regen, 0 none |
| 0x7AFAB0 | sub_7AFAB0 | `FFX_Btl_ApplyPoisonDamage` | `maxHP * +0x5BA / 100` |
| 0x799CD0 | sub_799CD0 | `FFX_Btl_StepDoomCountdown` | `+0x5C8` countdown, commits 0x3120 |
| 0x78E410 | sub_78E410 | `FFX_Btl_ReleaseThreatenAtTurnStart` | clears the link bit at turn start |
| 0x78E460 | sub_78E460 | `FFX_Btl_UpdateThreatenLink` | sets or releases `+0x5C5` / `+0x5C6` both ways |
| 0x79B290 | sub_79B290 | `FFX_Btl_ApplyAutoStatuses` | permanent masks, counters to 0xFF, saves the old |
| 0x79B1A0 | sub_79B1A0 | `FFX_Btl_RestoreAutoStatuses` | the inverse |
| 0x78D530 | sub_78D530 | `FFX_Btl_ResetUnitForReentry` | revive and re-entry, counter = base |
| 0x7A89C0 | sub_7A89C0 | `FFX_Btl_ReloadUnitsByMask` | `Btl 080` handler body, reloads params, base and counter |
| 0x7B1AA0 | sub_7B1AA0 | `FFX_Btl_CtbOnPostActionKind8` | 1.7 |
| 0x7842F0 | sub_7842F0 | `FFX_Btl_RollMonsterGroupOffsets` | kind 0 draws into `+0x424` per group member [I] purpose |
| 0x79A020 | sub_79A020 | `FFX_Btl_CtbPreview_Advance` | adds the delay, counts down the local haste / slow |
| 0x79B170 | sub_79B170 | `FFX_Btl_CtbPreview_SetRow` | writes a row of 0x112AA00 |
| 0x7898D0 | sub_7898D0 | `FFX_Btl_CtbPreview_ApplyPendingCmd` | the pending command's CTB effects |
| 0x112AA00 | byte_112AA00 | `g_ffxBtlCtbDisplayList` | 32 x {u8 unit, u8, u16 counter} |
| 0x112A94C | dword_112A94C | `g_ffxBtlCtbBaseTable` | ctb_base.bin buffer |
| 0x112A974 | word_112A974 | `g_ffxBtlCtbBaseTableSize` | its size |
| 0x112C9DC | (dword_112C9DB+1) | `g_ffxBtlAdvantage` | 0 normal, 1 pre-emptive, 2 ambush |
| 0x112A923 | byte_112A923 | `g_ffxBtlForcedAdvantage` | 0xFF none, `Btl 125` case 25 |
| 0x112A8E4 | (word_112A8E3+1) | `g_ffxBtlScriptHold` | `Btl 060` = 2, `Btl 151` = 1, `Btl 061` = 0, closes the CTB gate [I] name |
| 0xC42464 | unk_C42464 | `g_ffxBtlCounterStatusInfo` | 13 x 4 bytes, +3 the tick flags |

---

## 7. tools/ctb.py

`E:\Git\projectzanarkand\tools\ctb.py`. Reads `ctb_base.bin` read-only through
`E:\Git\FFXCoopProject\tools\vbf.py` (loaded with importlib, `--reader` to point elsewhere) and,
for the RNG, the two constant tables out of the installed `FFX.exe` (mapped VA -> file offset from
the PE section table). Nothing from the game is written anywhere.

- `table` prints the agility runs with tick and range.
- `sim` builds units 0..6 by character name (other names get free party slots) and monster slots
  20.. in order, runs `FFX_Btl_InitCtbCounters` with the real stream map and RNG (states zero by
  default, `--state N=0x...` per stream, `--no-rng` forces every draw to 0), then the real
  `FFX_Btl_CtbTick` and turn-end arithmetic. `--rank` gives each unit's action ranks, `--haste`
  and `--slow` the status counters, `--first-strike` the auto-ability, `--preview` prints the strip
  computed the way `FFX_Btl_BuildCtbPreview` does it.
- `selftest` checks the file's header, the tick column against the community thresholds (marked as
  a cross-check), the haste and slow truncation, the stream map, the sort key, that the first draw
  of every stream from a zero state is `(xor << 16) & 0x7FFFFFFF`, and that the strip and the real
  queue give the same 32 turns for 300 random parties. All pass.

Not modelled: regen, poison, doom, sleep skipping, KO and revive, delay attacks, threaten, the
post-action kind 8 reset, and the script hold double tick. They are written out above for the
engine port.

---

## 8. What is still open

- **The recorded battle** for 1.11.7. Capture list in section 4.
- `+0xDE8` at battle start for a unit whose first turn comes straight from the normalisation (its
  counter was already 0, so the tick never reset the rank). If that turn performs no action the
  delay uses whatever the byte held, 0 behaving as rank 1.
- The summon flag on aeon slots 8..17 at battle start (stream aliasing, 3.1).
- What post-action kind 8 is (which command sets record +6 = 8).
- Status table flag bits 0x10..0x80, and status-2 bits 0x0010 and 0x8000.
- Whether shipped scripts that set the forced advantage (`Btl 125` case 25) always reset it.
- `sub_7842F0`'s purpose (`+0x424`), only its draws matter here.
